#include "MeonEditor.h"
#include "ui/dev/WidgetGallery.h"
#include "ui/dev/DevJamScreen.h"
#include "ui/screens/OnboardingScreen.h"
#include "ui/theme/LookAndFeel.h"
#include "OnboardingScreens.h"
#include "LobbyScreens.h"
#include "JamScreen.h"
#include "SettingsScreen.h"

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
      pluginMode (! juce::JUCEApplicationBase::isStandaloneApp()),
      core (p.getMeonCore())
{
    setLookAndFeel (&lookAndFeel);
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

    settings = std::make_unique<MeonSettings>();
    session = std::make_unique<MeonSession> (processor, *settings, pluginMode);
    session->setAudioInfoProvider ([this] { return collectAudioInfo(); });
    session->addListener (this);

    setOpaque (true);
    setWantsKeyboardFocus (true);
    addMouseListener (this, true);   // 어느 자식을 클릭해도 mouseDown 을 받아 텍스트 입력 포커스를 거둘 수 있게

    // 크기: 독립 앱 기본 1280×800 (최소 1024×680), 플러그인 기본 900×600 (최소 760×520)
    {
        const int minW = pluginMode ? 760 : 1024, minH = pluginMode ? 520 : 680;
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

    go (initialScreen(), false);
   #if MEON_DEV_OPTIONS
    devJam = meon::devOptions().getAllKeys().contains ("dev-jam");
   #endif
    // --dev-jam: 새 합주 로직(JamSession)만 쓴다. 옛 세션은 서버에 붙이지 않는다 (둘을 동시에 쓰지 않음)
    if (! devJam)
        session->start();

    newLookAndFeel = std::make_unique<ui::LookAndFeel>();
    core.auth().addListener (this);
    core.start();
    updateOnboarding();

    // 켤 때 한 번 새 버전 확인 (첫 화면이 뜨고 서버 연결이 시작된 뒤)
    if (MeonUpdater::isSupported())
    {
        juce::Component::SafePointer<MeonEditor> safe (this);
        juce::Timer::callAfterDelay (3000, [safe] { if (safe != nullptr) safe->updater->check(); });
    }
}

MeonEditor::~MeonEditor()
{
    core.auth().removeListener (this);
    onboarding.reset();
    session->removeListener (this);
    settingsOverlay = nullptr;
    settingsFadingOut = nullptr;
    screen = nullptr;
    fadingOut = nullptr;
    updater = nullptr;
    session = nullptr;
    settings = nullptr;
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

//==============================================================================
MeonEditor::Screen MeonEditor::initialScreen() const
{
    if (session->isInRoom())
        return Screen::Jam;   // 플러그인 창을 다시 연 경우
    if (settings->getNickname().trim().isEmpty())
        return Screen::Nickname;
    if (! settings->isOnboardingDone (pluginMode))
        return pluginMode ? Screen::Headphone : Screen::Audio;
    return Screen::Home;
}

std::unique_ptr<juce::Component> MeonEditor::createScreen (Screen s)
{
    switch (s)
    {
        case Screen::Nickname:  return std::make_unique<NicknameScreen> (*this);
        case Screen::Audio:     return std::make_unique<AudioDeviceScreen> (*this);
        case Screen::Headphone: return std::make_unique<HeadphoneScreen> (*this);
        case Screen::Level:     return std::make_unique<InputLevelScreen> (*this);
        case Screen::Create:    return std::make_unique<CreateRoomScreen> (*this);
        case Screen::Join:      return std::make_unique<JoinRoomScreen> (*this);
        case Screen::Jam:       return std::make_unique<JamScreen> (*this);
        case Screen::Home:
        default:                return std::make_unique<HomeScreen> (*this);
    }
}

void MeonEditor::go (Screen s, bool fade)
{
    if (pluginMode && (s == Screen::Audio || s == Screen::Level))
        s = Screen::Home;
    current = s;
    showComponent (createScreen (s), fade);
    if (s != Screen::Jam)
        session->setServerPingInterval (s == Screen::Home ? 5000 : 15000);
}

void MeonEditor::showComponent (std::unique_ptr<juce::Component> c, bool fade)
{
    if (fadingOut != nullptr)
    {
        fader.cancel (fadingOut.get());
        fadingOut = nullptr;
    }

    auto old = std::move (screen);
    screen = std::move (c);
    screen->setBounds (getLocalBounds());
    addAndMakeVisible (*screen);
    if (settingsOverlay != nullptr)
        settingsOverlay->toFront (false);

    if (old != nullptr)
    {
        if (fade)
        {
            fadingOut = std::move (old);
            juce::Component::SafePointer<MeonEditor> safe (this);
            fader.fade (fadingOut.get(), 1.0f, 0.0f, metric::fadeMs, [safe] { if (safe != nullptr) safe->finishFade(); });
        }
        else
        {
            old = nullptr;
        }
    }

    if (fade)
        fader.fade (screen.get(), 0.0f, 1.0f, metric::fadeMs);
    grabKeyboardFocus();
}

void MeonEditor::finishFade()
{
    fadingOut = nullptr;
}

void MeonEditor::openSettings()
{
    if (settingsOverlay != nullptr)
        return;
    settingsOverlay = std::make_unique<SettingsScreen> (*this);
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
    if (session->isInRoom())
    {
        if (settingsOverlay != nullptr)
            closeSettings();
        if (auto* s = dynamic_cast<ScreenBase*> (screen.get()))
            if (s->confirmLeaveForQuit())
                return false;
        session->leaveRoom();
    }
    saveAll();
    return true;
}

void MeonEditor::startUpdate()
{
    if (! session->isInRoom())
        updater->download();
}

void MeonEditor::installUpdate()
{
    // 합주 중에 받기가 끝났으면 설치를 미룬다 (설정의 업데이트 버튼으로 다시 시작)
    if (session->isInRoom())
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

void MeonEditor::copyRoomCodeToClipboard()
{
    auto code = session->getDisplayRoomCode();
    if (code.isNotEmpty())
        juce::SystemClipboard::copyTextToClipboard (code);
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

MeonSessionLog::AudioInfo MeonEditor::collectAudioInfo() const
{
    MeonSessionLog::AudioInfo info;
    if (pluginMode)
    {
        info.deviceType = "host";
        info.host = getHostDescription();
        info.sampleRate = processor.getSampleRate();
        info.bufferSize = processor.getCurrSamplesPerBlock();
    }
    else if (auto* dm = deviceManager())
    {
        auto setup = dm->getAudioDeviceSetup();
        info.deviceType = dm->getCurrentAudioDeviceType();
        info.inputDevice = setup.inputDeviceName;
        info.outputDevice = setup.outputDeviceName;
        info.sampleRate = setup.sampleRate;
        info.bufferSize = setup.bufferSize;
        if (auto* dev = dm->getCurrentAudioDevice())
        {
            info.sampleRate = dev->getCurrentSampleRate();
            info.bufferSize = dev->getCurrentBufferSizeSamples();
        }
    }
    return info;
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
    const auto autoUser = meon::devOptions()["dev-signup"];
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

    // 온보딩을 마쳤다: 서버 계정의 닉네임·파트를 옛 화면 설정에 넣고 옛 홈으로 (R6 까지 임시)
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
        if (current != Screen::Jam) go (Screen::Home, false);
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
    const auto& keys = o.getAllKeys();
    applyTestOptions (o["nickname"], o["screen"], keys.contains ("create"), o["join"], keys.contains ("enter"));
}

void MeonEditor::applyTestOptions (const juce::String& nickname, const juce::String& screenName,
                                   bool autoCreateRoom, const juce::String& autoJoinCode, bool autoEnterJam)
{
    if (nickname.isNotEmpty())
    {
        settings->setNickname (nickname);
        settings->setOnboardingDone (pluginMode, true);
        session->applyNicknameChange();   // 이미 연결 중이면 새 이름으로 다시 연결
    }

    autoCreate = autoCreateRoom;
    autoJoin = autoJoinCode.trim().toUpperCase();
    autoEnter = autoEnterJam;

    if (screenName.isNotEmpty())
    {
        const auto n = screenName.toLowerCase();
        if (n.startsWith ("settings"))
        {
            go (Screen::Home, false);
            openSettings();
            const auto which = n.fromFirstOccurrenceOf (":", false, false);   // settings:input 처럼 드롭다운을 열어 둔다
            if (auto* screen = which.isNotEmpty() ? dynamic_cast<SettingsScreen*> (settingsOverlay.get()) : nullptr)
            {
                juce::Component::SafePointer<SettingsScreen> sp (screen);
                juce::Timer::callAfterDelay (700, [sp, which] { if (sp != nullptr) sp->showPopupForTest (which); });
            }
        }
        else if (n == "nick" || n == "nickname") go (Screen::Nickname, false);
        else if (n == "audio")     go (Screen::Audio, false);
        else if (n == "hp" || n == "headphone")  go (Screen::Headphone, false);
        else if (n == "level")     go (Screen::Level, false);
        else if (n == "create")    go (Screen::Create, false);
        else if (n == "join")      go (Screen::Join, false);
        else if (n == "jam")       go (Screen::Jam, false);
        else                       go (Screen::Home, false);
    }
    runAutoRoomAction();
}

void MeonEditor::runAutoRoomAction()
{
    if (! session->isServerConnected() || session->getRoomState() != MeonSession::RoomState::None)
        return;
    if (autoCreate)
    {
        autoCreate = false;
        session->createRoom();
        go (Screen::Create);
    }
    else if (autoJoin.isNotEmpty())
    {
        const auto code = autoJoin;
        autoJoin.clear();
        go (Screen::Join);
        session->joinRoom (code);
    }
}

void MeonEditor::sessionStateChanged()
{
    runAutoRoomAction();
}

void MeonEditor::roomJoined()
{
    if (current == Screen::Join || (autoEnter && current == Screen::Create))
        go (Screen::Jam);
}

void MeonEditor::roomLeft()
{
    if (current == Screen::Jam)
        go (Screen::Home);
}

//==============================================================================
void MeonEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::white);
}

void MeonEditor::resized()
{
    if (screen != nullptr)
        screen->setBounds (getLocalBounds());
    if (fadingOut != nullptr)
        fadingOut->setBounds (getLocalBounds());
    if (settingsOverlay != nullptr)
        settingsOverlay->setBounds (getLocalBounds());
    if (settingsFadingOut != nullptr)
        settingsFadingOut->setBounds (getLocalBounds());
    if (devScreen != nullptr)
        devScreen->setBounds (getLocalBounds());
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
        if (auto* s = dynamic_cast<ScreenBase*> (settingsOverlay.get()))
            return s->handleShortcut (k);
        return false;
    }
    if (auto* s = dynamic_cast<ScreenBase*> (screen.get()))
        return s->handleShortcut (k);
    return false;
}

} // namespace meon
