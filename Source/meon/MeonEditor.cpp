#include "MeonEditor.h"
#include "ui/dev/WidgetGallery.h"
#include "ui/dev/DevJamScreen.h"
#include "ui/screens/OnboardingScreen.h"
#include "ui/AppRoot.h"
#include "ui/theme/LookAndFeel.h"
#include "ui/overlays/SettingsModal.h"

namespace meon
{

void MeonEditor::Fader::fade (juce::Component* c, float from, float to, int ms, std::function<void()> onDone)
{
    cancel (c);
    if (c == nullptr)
        return;
    c->setAlpha (from);
    items.push_back ({ c, from, to, juce::Time::getMillisecondCounterHiRes(), ms, std::move (onDone) });
    startTimerHz (60);
}

void MeonEditor::Fader::cancel (juce::Component* c)
{
    items.erase (std::remove_if (items.begin(), items.end(), [c] (const Item& i) { return i.comp == nullptr || i.comp.getComponent() == c; }), items.end());
    if (items.empty())
        stopTimer();
}

void MeonEditor::Fader::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    std::vector<std::function<void()>> finished;
    for (auto it = items.begin(); it != items.end();)
    {
        if (it->comp == nullptr) { it = items.erase (it); continue; }
        const double t = juce::jlimit (0.0, 1.0, (now - it->startMs) / (double) juce::jmax (1, it->ms));
        it->comp->setAlpha (it->from + (it->to - it->from) * (float) t);
        if (t >= 1.0)
        {
            if (it->onDone) finished.push_back (std::move (it->onDone));
            it = items.erase (it);
        }
        else ++it;
    }
    if (items.empty())
        stopTimer();
    for (auto& f : finished)
        f();
}

MeonEditor::MeonEditor (SonobusAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p),
      pluginMode (! juce::JUCEApplicationBase::isStandaloneApp()
                 #if MEON_DEV_OPTIONS
                  || meon::devOptions().getAllKeys().contains ("as-plugin")
                 #endif
                  ),
      core (p.getMeonCore())
{
    newLookAndFeel = std::make_unique<ui::LookAndFeel>();
    setLookAndFeel (newLookAndFeel.get());
    juce::LookAndFeel::setDefaultLookAndFeel (newLookAndFeel.get());

    settings = std::make_unique<MeonSettings>();

    setOpaque (true);
    setWantsKeyboardFocus (true);
    addMouseListener (this, true);   // 어느 자식을 클릭해도 mouseDown 을 받아 텍스트 입력 포커스를 거둘 수 있게

    // 크기: 독립 앱 기본 1280×800 (최소 1024×680), 플러그인 기본·최소 900×600 (Q19: 더 작으면 디자인이 깨진다)
    {
        const int minW = pluginMode ? 900 : 1024, minH = pluginMode ? 600 : 680;
        const int defW = pluginMode ? 900 : 1280, defH = pluginMode ? 600 : 800;
        auto last = processor.getLastPluginBounds();
        const int w = last.getWidth() >= minW ? last.getWidth() : defW;
        const int h = last.getHeight() >= minH ? last.getHeight() : defH;
        setSize (w, h);
        setResizable (true, pluginMode);
        setResizeLimits (minW, minH, pluginMode ? 4000 : 10000, pluginMode ? 4000 : 10000);
    }

    updater = std::make_unique<MeonUpdater>();
    updater->onDownloaded = [this] { installUpdate(); };

   #if MEON_DEV_OPTIONS
    devJam = meon::devOptions().getAllKeys().contains ("dev-jam");
    {
        const auto scr = meon::devOptions()["screen"];
        if (scr == "home" || scr.startsWith ("home:"))
            devHomePreview = scr == "home" ? juce::String ("home") : scr.fromFirstOccurrenceOf (":", false, false);
        else if (scr == "jam" || scr.startsWith ("jam:") || scr.startsWith ("settings"))
            devHomePreview = scr;   // 합주 화면(가짜 계정으로 비공개 방)·설정 탭 스냅숏
    }
   #endif
    core.auth().addListener (this);
    core.store().addListener (this);
    core.start();
    updateOnboarding();

    // 켤 때 한 번 새 버전 확인 (첫 화면이 뜨고 서버 연결이 시작된 뒤), 그 뒤 6시간마다
    if (MeonUpdater::isSupported() && ! pluginMode)
    {
        juce::Component::SafePointer<MeonEditor> safe (this);
        juce::Timer::callAfterDelay (3000, [safe] { if (safe != nullptr) safe->checkForUpdate(); });
        startTimer (60 * 1000);
    }
}

void MeonEditor::checkForUpdate()
{
    if (! MeonUpdater::isSupported() || pluginMode) return;
    const auto st = updater->getState();
    if (st == MeonUpdater::State::Checking || st == MeonUpdater::State::Downloading || st == MeonUpdater::State::Ready) return;
    lastUpdateCheck = juce::Time::currentTimeMillis();
    updater->check();
}

void MeonEditor::timerCallback()
{
    if (juce::Time::currentTimeMillis() - lastUpdateCheck >= (juce::int64) 6 * 60 * 60 * 1000)
        checkForUpdate();
}

void MeonEditor::stateChanged (core::StateKey k)
{
    // 서버가 더 새 빌드를 알려 주면 바로 확인한다 (build.txt 와 같은 번호 체계)
    if (k != core::StateKey::config) return;
    const auto& cfg = core.store().state().config;
    if (cfg.latestBuild > MeonUpdater::currentBuild() && MeonUpdater::currentBuild() > 0
        && updater->getState() != MeonUpdater::State::Available)
        checkForUpdate();
}

MeonEditor::~MeonEditor()
{
    stopTimer();
    core.store().removeListener (this);
    core.auth().removeListener (this);
    appRoot.reset();
    onboarding.reset();
    settingsOverlay = nullptr;
    settingsFadingOut = nullptr;
    updater = nullptr;
    settings = nullptr;
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

//==============================================================================
void MeonEditor::openSettings (int tab)
{
    if (settingsOverlay != nullptr)
        return;
    if (core.auth().step() != core::AuthController::Step::ready)
        return;   // 로그인 전에는 설정이 없다
    ui::SettingsModal::UpdateHooks hooks;
    if (MeonUpdater::isSupported() && ! pluginMode)
    {
        using S = ui::SettingsModal::UpdateHooks::State;
        hooks.state = [this]
        {
            switch (updater->getState())
            {
                case MeonUpdater::State::Checking:    return S::checking;
                case MeonUpdater::State::UpToDate:    return S::upToDate;
                case MeonUpdater::State::Available:   return S::available;
                case MeonUpdater::State::Downloading: return S::downloading;
                case MeonUpdater::State::Ready:       return S::installing;
                case MeonUpdater::State::Failed:      return S::failed;
                default:                              return S::idle;
            }
        };
        hooks.progress = [this] { return updater->getProgress(); };
        hooks.latestBuild = [this] { return updater->getLatestBuild(); };
        hooks.check = [this] { checkForUpdate(); };
        hooks.start = [this] { startUpdate(); };
    }
    auto modal = std::make_unique<ui::SettingsModal> (core, std::move (hooks), (ui::SettingsModal::Tab) tab);
    modal->onClose = [this]
    {
        juce::Component::SafePointer<MeonEditor> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->closeSettings(); });
    };
    settingsOverlay = std::move (modal);
    settingsOverlay->setBounds (getLocalBounds());
    addAndMakeVisible (*settingsOverlay);
    settingsOverlay->toFront (false);
    fader.fade (settingsOverlay.get(), 0.0f, 1.0f, metric::fadeMs);
    grabKeyboardFocus();
}

void MeonEditor::closeSettings()
{
    if (settingsOverlay == nullptr)
        return;
    if (settingsFadingOut != nullptr)
    {
        fader.cancel (settingsFadingOut.get());
        settingsFadingOut = nullptr;
    }
    settingsFadingOut = std::move (settingsOverlay);
    juce::Component::SafePointer<MeonEditor> safe (this);
    fader.fade (settingsFadingOut.get(), 1.0f, 0.0f, metric::fadeMs, [safe] { if (safe != nullptr) safe->settingsFadingOut = nullptr; });
    saveAll();
    grabKeyboardFocus();
}

//==============================================================================
bool MeonEditor::requestedQuit()
{
    // 끄면 방에서 나간다 (서버에 나가기를 알리고 엔진을 정리한다)
    if (core.store().state().inRoom())
        core.home().leaveRoom();
    saveAll();
    return true;
}

void MeonEditor::startUpdate()
{
    if (! core.store().state().inRoom())
        updater->download();
}

void MeonEditor::installUpdate()
{
    // 합주 중에 받기가 끝났으면 설치를 미룬다 (설정의 업데이트 버튼으로 다시 시작)
    if (core.store().state().inRoom())
        return;
    if (! updater->launchInstaller())
        return;
    saveAll();
    updater->relaunchAfterInstall();   // 설정을 저장한 뒤에 새 앱을 켠다 (macOS)
    if (auto* app = juce::JUCEApplicationBase::getInstance())
        app->systemRequestedQuit();
}

void MeonEditor::saveAll()
{
    settings->save();
    if (saveSettingsIfNeeded)
        saveSettingsIfNeeded();
}

juce::String MeonEditor::getHostDescription() const
{
    if (! pluginMode)
        return "Meon";
    juce::PluginHostType host;
    juce::String desc (host.getHostDescription());
    if (desc.isEmpty() || desc == "Unknown")
        desc = TXT ("DAW");
    return desc;
}

juce::String MeonEditor::getVersionText() const
{
    juce::String v = "Meon " + juce::String (MEON_BUILD_VERSION);
    if (pluginMode)
        v += " (AU / VST3)";
    else
        v += " (build " + juce::String (MeonUpdater::currentBuild() > 0 ? MeonUpdater::currentBuild() : MEON_BUILD_NUMBER) + ")";
    return v;
}

//==============================================================================
void MeonEditor::authStepChanged (core::AuthController::Step)
{
    if (devPreview) return;
    updateOnboarding();
}

void MeonEditor::updateOnboarding()
{
    using Step = core::AuthController::Step;
    const auto step = core.auth().step();

   #if MEON_DEV_OPTIONS
    // 개발용 자동 가입 (--dev-signup=아이디): 두 인스턴스 자동 시험용. 비밀번호 abcd123!x, 인증 코드는 가짜 서버 값
    auto autoUser = meon::devOptions()["dev-signup"];
    if (autoUser.isEmpty() && devHomePreview.isNotEmpty()) autoUser = "snapshot";   // 홈 스냅숏: 가짜 계정으로 자동 가입
    if (autoUser.isNotEmpty())
    {
        auto& auth = core.auth();
        auto ignore = [] (core::Result<core::Done>) {};
        if (step == Step::login && ! devSignupStarted)
        {
            devSignupStarted = true;
            auth.signup (autoUser + "@example.com", autoUser, "abcd123!x", true, [this, autoUser] (core::Result<core::Done> r)
            {
                if (! r.ok()) core.auth().login (autoUser + "@example.com", "abcd123!x", [] (core::Result<core::Done>) {});
            });
        }
        else if (step == Step::verify && core.fakeServer() != nullptr) auth.verify (core.fakeServer()->getLastMailedCode(), ignore);
        else if (step == Step::nickname) auth.setNickname (autoUser, ignore);
        else if (step == Step::part) auth.setPart ("guitar", ignore);
        else if (step == Step::discord) auth.finishDiscord();
        else if (step == Step::done) auth.finishDone();
        if (auth.step() != step) return;   // 위에서 단계가 넘어갔으면 그 호출이 이미 처리했다
    }
   #endif
    if (step != Step::ready)
    {
        appRoot.reset();   // 로그아웃 등
        settingsOverlay = nullptr;
        settingsFadingOut = nullptr;
        if (onboarding == nullptr)
        {
            onboarding = std::make_unique<ui::OnboardingScreen> (core);
            onboarding->setLookAndFeel (newLookAndFeel.get());
            addAndMakeVisible (*onboarding);
            onboarding->setBounds (getLocalBounds());
        }
        onboarding->toFront (true);
        return;
    }

    if (devJam)
    {
        if (onboarding != nullptr) { removeChildComponent (onboarding.get()); onboarding.reset(); }
        if (devScreen == nullptr && core.jam() != nullptr)
        {
            devScreen = std::make_unique<ui::DevJamScreen> (core);
            devScreen->setLookAndFeel (newLookAndFeel.get());
            addAndMakeVisible (*devScreen);
            devScreen->setBounds (getLocalBounds());
        }
        return;
    }

    // 온보딩을 마쳤다: 서버 계정의 닉네임·파트를 옛 설정에도 넣고(옛 설정 화면이 R7 까지 남는다) 새 홈으로
    if (const auto& me = core.store().state().me)
    {
        if (me->nickname) settings->setNickname (*me->nickname);
        if (me->part) settings->setPartKey (*me->part);
        settings->setOnboardingDone (pluginMode, true);   // 옛 오디오·헤드폰 단계는 뺀다 (기획 결정)
        settings->save();
    }
    if (onboarding != nullptr)
    {
        removeChildComponent (onboarding.get());
        onboarding.reset();
    }
    showAppRoot();
}

void MeonEditor::showAppRoot()
{
    if (appRoot != nullptr) return;
    ui::HomeScreen::UpdateHooks hooks;
    if (MeonUpdater::isSupported() && ! pluginMode)
    {
        using S = ui::HomeScreen::UpdateHooks::State;
        hooks.state = [this]
        {
            switch (updater->getState())
            {
                case MeonUpdater::State::Available:   return S::available;
                case MeonUpdater::State::Downloading: return S::downloading;
                case MeonUpdater::State::Ready:       return S::installing;
                case MeonUpdater::State::Failed:      return updater->lastFailureWasDownload() ? S::failed : S::none;
                default:                              return S::none;
            }
        };
        hooks.progress = [this] { return updater->getProgress(); };
        hooks.latestBuild = [this] { return updater->getLatestBuild(); };
        hooks.start = [this] { startUpdate(); };
    }
    appRoot = std::make_unique<ui::AppRoot> (core, std::move (hooks));
    appRoot->setLookAndFeel (newLookAndFeel.get());
    appRoot->onOpenSettings = [this] (int tab) { openSettings (tab); };
    addAndMakeVisible (*appRoot);
    appRoot->setBounds (getLocalBounds());
    if (settingsOverlay != nullptr) settingsOverlay->toFront (false);
    if (devHomePreview.isNotEmpty())
    {
        juce::Component::SafePointer<MeonEditor> safe (this);
        juce::Timer::callAfterDelay (500, [safe] { if (safe != nullptr) safe->applyHomePreview(); });
    }
   #if MEON_DEV_OPTIONS
    // 개발용 자동 시험 (새 홈에서): --dev-create 비공개 방 만들기, --dev-join=코드
    const auto& dev = meon::devOptions();
    if (! core.store().state().inRoom())
    {
        if (dev.getAllKeys().contains ("dev-create"))
        {
            core::CreateRoom c;
            c.type = "private";
            c.title = TXT ("개발용 방");
            core.home().createRoom (c, [] (core::Result<core::RoomDetail> r)
            {
                if (r.ok()) std::cerr << "MEON dev: room code " << r.get().inviteCode.value_or ("").toStdString() << std::endl;
            });
        }
        else if (dev["dev-join"].isNotEmpty())
        {
            core.home().joinByCode (dev["dev-join"], [] (core::Result<core::JoinResult>) {});
        }
    }
   #endif
}

void MeonEditor::applyHomePreview()
{
    // 개발용 스냅숏: 홈의 겹침 요소를 열어 둔다
    if (appRoot == nullptr) return;
    auto* home = appRoot->getHome();
    const auto p = devHomePreview;
    if (p == "create")      home->openCreateRoom();
    else if (p == "filter") home->openFilter();
    else if (p == "code")   home->openJoinCode();
    else if (p == "notif")  home->toggleNotifications();
    else if (p == "sendoff")   // 송출 정지 중인 홈 (미터 회색)
    {
        if (auto* jam = core.jam()) jam->setSendMuted (true);
    }
    else if (p == "friendreq")   // 친구 목록에서 승인 방에 요청을 보낸 뒤 ("요청 보냄", 같은 방 카드도)
    {
        if (const auto* f = core.store().state().findFriend ("u_f_yuna"))
            core.home().joinFriend (*f, [] (core::Result<core::JoinResult>) {});
    }
    else if (p == "fail")   appRoot->showJoinFailure (core::HomeController::JoinFailure::full);
    else if (p == "toast")
    {
        if (auto* fake = core.fakeServer()) fake->devInviteMe();
        appRoot->addInfoToast (TXT ("방장이 방을 닫았어요"), TXT ("금요일 밤 홍대 밴드 합주"));
    }
    else if (p == "jam" || p.startsWith ("jam:"))
    {
        core::CreateRoom c;
        c.type = p == "jam:host" ? "public" : "private";
        if (p == "jam:host") c.entryMode = juce::String ("approval");
        c.title = TXT ("준호의 새벽 합주");
        juce::Component::SafePointer<MeonEditor> safe (this);
        core.home().createRoom (c, [safe, p] (core::Result<core::RoomDetail>)
        {
            if (safe != nullptr && p == "jam:sendoff")   // 셀프 모니터를 켠 채 송출 정지 (내 행이 "지금 안 들림")
                if (auto* jam = safe->core.jam()) { jam->setSelfMonitor (true); jam->setSendMuted (true); }
            if (safe == nullptr || p != "jam:host") return;
            if (auto* fake = safe->core.fakeServer()) fake->devJoinRequest();   // 입장 요청 하나
        });
    }
    else if (p.startsWith ("settings"))
    {
        static const char* tabNames[] = { "account", "profile", "audio", "jam", "privacy", "notif", "keys", "about" };
        int tab = 0;
        const auto name = p.fromFirstOccurrenceOf (":", false, false);
        for (int i = 0; i < 8; ++i) if (name == tabNames[i]) tab = i;
        openSettings (tab);
    }
    else if (p == "disc")
    {
        if (auto* fake = core.fakeServer()) fake->devDropLink (60);
    }
}

void MeonEditor::applyDevOptions (const juce::StringPairArray& o)
{
    {
        // 개발용: 온보딩 단계 미리 보기 (--screen=login|signup|verify|nickname|part|discord|done)
        using Step = core::AuthController::Step;
        const std::pair<const char*, Step> steps[] = { { "login", Step::login }, { "signup", Step::signup }, { "verify", Step::verify },
                                                        { "nickname", Step::nickname }, { "part", Step::part }, { "discord", Step::discord },
                                                        { "done", Step::done } };
        for (auto& st : steps)
        {
            if (o["screen"] != st.first) continue;
            if (onboarding == nullptr)
            {
                onboarding = std::make_unique<ui::OnboardingScreen> (core);
                onboarding->setLookAndFeel (newLookAndFeel.get());
                addAndMakeVisible (*onboarding);
                onboarding->setBounds (getLocalBounds());
            }
            devPreview = true;
            onboarding->showStepForPreview (st.second);
            return;
        }
    }
    if (o["screen"] == "widgets")
    {
        devScreen = std::make_unique<ui::WidgetGallery>();
        addAndMakeVisible (*devScreen);
        devScreen->setBounds (getLocalBounds());
        return;
    }
    // 홈·합주 미리 보기(--screen=home…|jam)는 가입이 끝난 뒤 showAppRoot 가 연다
}

//==============================================================================
void MeonEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::white);
}

void MeonEditor::resized()
{
    if (settingsOverlay != nullptr)
        settingsOverlay->setBounds (getLocalBounds());
    if (settingsFadingOut != nullptr)
        settingsFadingOut->setBounds (getLocalBounds());
    if (devScreen != nullptr)
        devScreen->setBounds (getLocalBounds());
    if (appRoot != nullptr)
        appRoot->setBounds (getLocalBounds());
    if (onboarding != nullptr)
        onboarding->setBounds (getLocalBounds());
    processor.setLastPluginBounds (getLocalBounds());
}

void MeonEditor::mouseDown (const juce::MouseEvent& e)
{
    // 입력(채팅·닉네임·초대 코드) 밖을 클릭하면 에디터가 포커스를 가져가 입력에서 포커스가 빠지고
    // 글자 단축키가 다시 듣는다. JUCE 는 클릭한 곳이 포커스를 가진 입력을 품고 있으면 포커스를
    // 그대로 두기 때문에(설정 화면 본문, 합주 화면 등) 여기서 직접 옮긴다.
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    if (focused == nullptr || focused == this)
        return;

    // 지금 포커스를 가진 입력 안을 클릭한 것이면 그대로
    if (focused == e.eventComponent || focused->isParentOf (e.eventComponent))
        return;

    // 클릭한 컴포넌트(또는 상위)가 스스로 포커스를 받는 것이면 JUCE 가 옮긴다.
    // Viewport 는 포커스를 원하지만 스크롤 영역일 뿐이므로 예외.
    for (auto* c = e.eventComponent; c != nullptr && c != this; c = c->getParentComponent())
        if (c->getWantsKeyboardFocus() && dynamic_cast<juce::Viewport*> (c) == nullptr)
            return;

    grabKeyboardFocus();
}

bool MeonEditor::keyPressed (const juce::KeyPress& k)
{
    // 설정: ⌘ , (Windows: Ctrl ,)
    if (k.getModifiers().isCommandDown() && k.getTextCharacter() == ',')
    {
        if (settingsOverlay != nullptr) closeSettings();
        else openSettings();
        return true;
    }

    // 텍스트 입력 중에는 글자 단축키를 먹지 않는다 (Esc 는 입력 컴포넌트가 onEscapeKey 로 전달)
    if (auto* focused = juce::Component::getCurrentlyFocusedComponent())
        if (dynamic_cast<juce::TextEditor*> (focused) != nullptr && ! k.getModifiers().isCommandDown())
            return false;

    if (settingsOverlay != nullptr)
    {
        if (k == juce::KeyPress::escapeKey) { closeSettings(); return true; }
        return false;
    }
    if (appRoot != nullptr && appRoot->handleShortcut (k))
        return true;
    return false;
}

} // namespace meon
