#include "MeonEditor.h"
#include "ui/dev/WidgetGallery.h"
#include "ui/dev/DevJamScreen.h"
#include "ui/dev/UiSelfTest.h"
#include "ui/dev/Showcase.h"
#include "ui/screens/OnboardingScreen.h"
#include "ui/AppRoot.h"
#include "ui/screens/JamScreen.h"
#include "ui/Messages.h"
#include "ui/theme/LookAndFeel.h"
#include "ui/overlays/SettingsModal.h"
#include "MeonWindowChrome.h"
#include "MeonWindowFrame.h"
#include "core/log/AppLog.h"
#include <optional>

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
    updater->setChannel (settings->getUpdateChannel());
    updater->onDownloaded = [this] { installUpdate(); };

   #if MEON_DEV_OPTIONS
    devMockChrome = ! pluginMode && meon::devOptions()["chrome"] == "mac";
    devMockWin = ! pluginMode && meon::devOptions()["chrome"] == "win";
    devJam = meon::devOptions().getAllKeys().contains ("dev-jam");
    // --global-scale=1.25: 화면 배율이 정수가 아닐 때(윈도우 125%·150%)처럼 그린다. 리눅스에서 다시 그리기 확인용
    if (const auto gs = meon::devOptions()["global-scale"]; gs.isNotEmpty())
        juce::Desktop::getInstance().setGlobalScaleFactor (juce::jlimit (0.5f, 3.0f, gs.getFloatValue()));
    {
        const auto scr = meon::devOptions()["screen"];
        if (scr == "home" || scr.startsWith ("home:"))
            devHomePreview = scr == "home" ? juce::String ("home") : scr.fromFirstOccurrenceOf (":", false, false);
        else if (scr == "jam" || scr.startsWith ("jam:") || scr.startsWith ("settings"))
            devHomePreview = scr;   // 합주 화면(가짜 계정으로 비공개 방)·설정 탭 스냅숏
        if (meon::devOptions().containsKey ("uitest"))
            devHomePreview = "uitest";   // 화면 자가 테스트 (가짜 계정으로 자동 가입한 홈에서 시작)
        if (meon::devOptions().containsKey ("showcase"))
            devHomePreview = "showcase";   // 소개 페이지 캡처 (ui/dev/Showcase: 가상 인물 승찬으로 자동 가입)
    }
    // --scaled-snapshot=경로@배율 (예: shot.png@1.25): 화면 배율(윈도우 125%·150%, 맥 레티나 200%)로 그린 화면을 PNG 로 남기고 끝낸다.
    // 1px 선이 물리 픽셀에 맞는지 보는 용도 (소프트웨어 렌더러에 배율 변환을 건다: 윈도우 앱이 그리는 방식과 같다)
    if (const auto spec = meon::devOptions()["scaled-snapshot"]; spec.isNotEmpty())
    {
        const auto path = spec.upToLastOccurrenceOf ("@", false, false);
        const float scale = juce::jmax (0.5f, spec.fromLastOccurrenceOf ("@", false, false).getFloatValue());
        const int delay = meon::devOptions().containsKey ("snapshot-delay") ? meon::devOptions()["snapshot-delay"].getIntValue() : 3500;
        juce::Component::SafePointer<MeonEditor> safe (this);
        juce::Timer::callAfterDelay (delay, [safe, path, scale]
        {
            if (safe == nullptr) return;
            const auto img = safe->createComponentSnapshot (safe->getLocalBounds(), true, scale);
            juce::File f (path);
            f.deleteFile();
            juce::FileOutputStream out (f);
            if (out.openedOk()) juce::PNGImageFormat().writeImageToStream (img, out);
            std::cerr << "MEON dev: scaled snapshot " << path << " " << img.getWidth() << "x" << img.getHeight() << std::endl;
            juce::JUCEApplicationBase::quit();
        });
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
    // 서버가 더 새 빌드를 알려 주면(latest_build, config.updated) 확인한다 (매니페스트·build.txt 와 같은 번호 체계).
    // 접속 중인 앱이 한꺼번에 확인하지 않게 0–60초 무작위로 늦춘다 (배포 설계 3.7). 이미 기다리는 확인이 있으면 더 걸지 않는다
    if (k != core::StateKey::config) return;
    const auto& cfg = core.store().state().config;
    if (cfg.latestBuild > MeonUpdater::currentBuild() && MeonUpdater::currentBuild() > 0
        && updater->getState() != MeonUpdater::State::Available && ! updateCheckQueued)
    {
        updateCheckQueued = true;
        juce::Component::SafePointer<MeonEditor> safe (this);
        juce::Timer::callAfterDelay (juce::Random::getSystemRandom().nextInt (60 * 1000), [safe]
        {
            if (safe == nullptr) return;
            safe->updateCheckQueued = false;
            safe->checkForUpdate();
        });
    }
}

MeonEditor::~MeonEditor()
{
    getProperties().set (ui::ModalBase::noCloseAnimationProperty, true);   // 끄는 중: 닫히는 모달 그림을 남기지 않는다
    stopTimer();
    if (chromeInstalled) chrome::detach();   // 창 모양은 그대로 (앱을 끄는 중). 창 크기는 창이 먼저 저장했다
    if (winFrameInstalled) winframe::detach();   // 창 프로시저를 원래대로 (창은 곧 닫힌다)
    core.store().removeListener (this);
    core.auth().removeListener (this);
    appRoot.reset();
    onboarding.reset();
    settingsOverlay = nullptr;
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
    auto modal = std::make_unique<ui::SettingsModal> (core, settingsUpdateHooks(), (ui::SettingsModal::Tab) tab);
    modal->onClose = [this]
    {
        juce::Component::SafePointer<MeonEditor> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->closeSettings(); });
    };
    settingsOverlay = std::move (modal);
    settingsOverlay->setBounds (contentBounds());
    addAndMakeVisible (*settingsOverlay);
    settingsOverlay->toFront (false);   // 열기 애니메이션은 모달 래퍼가 (화면에 붙는 순간)
    grabKeyboardFocus();
}

ui::SettingsModal::UpdateHooks MeonEditor::settingsUpdateHooks()
{
    // 독립 앱의 업데이터 (설정 › 정보, 강제 업데이트 화면). 플러그인은 비워 둔다 (Q18: 버튼 없이 안내만)
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
        hooks.problem = [this]
        {
            using P = ui::SettingsModal::UpdateHooks::Problem;
            switch (updater->getProblem())
            {
                case MeonUpdater::Problem::needsMove: return P::needsMove;
                case MeonUpdater::Problem::osTooOld:  return P::osTooOld;
                case MeonUpdater::Problem::none:      break;
            }
            return P::none;
        };
        hooks.check = [this] { checkForUpdate(); };
        hooks.start = [this] { startUpdate(); };
    }
    return hooks;
}

void MeonEditor::closeSettings()
{
    if (settingsOverlay == nullptr)
        return;
    settingsOverlay = nullptr;   // 닫는 애니메이션은 모달 래퍼가 (지우기 직전 모습을 남긴다)
    saveAll();
    grabKeyboardFocus();
}

//==============================================================================
bool MeonEditor::winFullscreen() const
{
    auto* peer = getPeer();
    return winFrameInstalled && peer != nullptr && winframe::isFullscreen (peer->getNativeHandle());
}

void MeonEditor::toggleFullscreen()
{
    if (pluginMode) return;
    auto* peer = getPeer();
    if (peer == nullptr) return;
    if (winFrameInstalled) { winframe::setFullscreen (peer->getNativeHandle(), ! winframe::isFullscreen (peer->getNativeHandle())); return; }
   #if JUCE_MAC
    chrome::toggleFullScreen (peer->getNativeHandle());
   #endif
}

void MeonEditor::updateCaptionReveal()
{
    // 윈도우 전체 화면: 마우스를 화면 맨 위(2px)에 대면 창 버튼이 나타나고, 맨 윗줄(창 버튼 높이 + 16) 아래로 내려가면 숨는다
    if (chromeView == nullptr || ! winFullscreen()) { revealTimer.stopTimer(); return; }
    const auto pos = getMouseXYRelative();
    const bool inside = getLocalBounds().contains (pos.withY (juce::jmax (0, pos.y)));
    bool want = captionRevealed;
    if (! captionRevealed) want = inside && pos.y <= 1;
    else want = inside && pos.y < chromeView->getBottom() + 16;
    if (want == captionRevealed) return;
    captionRevealed = want;
    chromeView->setVisible (want);
    if (want) childrenChanged();   // 맨 위로
}

bool MeonEditor::requestedQuit()
{
    // 전체 화면에서 끄면 원래 창으로 되돌린 뒤 끈다 (다음에 켤 때 전체 화면 크기로 열리지 않게: 창 크기는 창이 사라질 때 저장된다)
    if (auto* peer = getPeer(); peer != nullptr && ! pluginMode)
    {
        if (winFullscreen()) winframe::setFullscreen (peer->getNativeHandle(), false);   // 바로 되돌아온다
        if (winFrameInstalled)
        {
            // 최대화였으면 보통 크기로 저장하고(창이 사라질 때 저장된다) 다음에 켤 때 다시 최대화한다
            settings->setWindowMaximised (winframe::unmaximiseForSave (peer->getNativeHandle()));
        }
       #if JUCE_MAC
        if (chrome::isFullScreen (peer->getNativeHandle()) && ! quittingFromFullscreen)
        {
            // macOS 전체 화면은 애니메이션 뒤에 나온다: 나온 다음에 다시 끄기 (2초 안에 안 나오면 그냥 끈다)
            quittingFromFullscreen = true;
            auto again = [] { if (auto* app = juce::JUCEApplicationBase::getInstance()) app->systemRequestedQuit(); };
            chrome::exitFullScreenThen (peer->getNativeHandle(), again);
            juce::Timer::callAfterDelay (2000, again);
            return false;
        }
       #endif
    }
    // 끄면 방에서 나간다: MeonCore 가 없어질 때 서버에 나가기를 보내고 응답을 기다린다 (플러그인 제거와 같은 길)
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
void MeonEditor::authStepChanged (core::AuthController::Step s)
{
    if (devPreview) return;
    // 강제 업데이트 화면: 새 버전을 바로 확인해 둔다 (버튼을 누르면 곧바로 받기). 이때는 단계적 배포(rollout)를 무시한다
    updater->setForced (s == core::AuthController::Step::outdated);
    if (s == core::AuthController::Step::outdated) checkForUpdate();
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
    juce::String autoNickname = autoUser, autoPart = "guitar";
    if (devHomePreview == "showcase") { autoUser = "seungchan"; autoNickname = juce::String::fromUTF8 ("승찬"); autoPart = "keys"; }
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
        else if (step == Step::nickname) auth.setNickname (autoNickname, ignore);
        else if (step == Step::part) auth.setPart (autoPart, ignore);
        else if (step == Step::discord) auth.finishDiscord();
        else if (step == Step::done) auth.finishDone();
        if (auth.step() != step) return;   // 위에서 단계가 넘어갔으면 그 호출이 이미 처리했다
    }
   #endif
    if (step != Step::ready)
    {
        appRoot.reset();   // 로그아웃 등
        settingsOverlay = nullptr;
        if (onboarding == nullptr)
        {
            onboarding = std::make_unique<ui::OnboardingScreen> (core, settingsUpdateHooks());
            onboarding->setLookAndFeel (newLookAndFeel.get());
            onboarding->setTitleBarInset (titleBarInset());
            onboarding->onTitleBarMouseDown = [this] { titleBarMouseDown(); };
            addAndMakeVisible (*onboarding);
            onboarding->setBounds (contentBounds());
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
            devScreen->setBounds (contentBounds());
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
        hooks.needsMove = [this] { return updater->getProblem() == MeonUpdater::Problem::needsMove; };
        hooks.start = [this] { startUpdate(); };
    }
    appRoot = std::make_unique<ui::AppRoot> (core, std::move (hooks));
    appRoot->setLookAndFeel (newLookAndFeel.get());
    appRoot->onOpenSettings = [this] (int tab) { openSettings (tab); };
    addAndMakeVisible (*appRoot);
    appRoot->setBounds (contentBounds());
    if (settingsOverlay != nullptr) settingsOverlay->toFront (false);
    windowChromeChanged();   // 창 윗부분 (제목 줄 오른쪽 여백·창 버튼을 맨 위로)
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
    // --chrome=mac|flat|win: 창 윗부분 배치 미리 보기 (홈·사이드바 디자인 v2.1 chromeVals). mac = 맥 창 모드 (가짜 신호등을 그린다),
    // flat = 신호등 없음 (맥 전체 화면·플러그인), win = 윈도우 독립 앱 (사이드바는 flat, 창 버튼 + 제목 줄 오른쪽 150). 없으면 실제 창을 따른다
    if (const auto ch = meon::devOptions()["chrome"]; ch == "mac" || ch == "flat" || ch == "win")
        appRoot->devForceChrome (ch == "mac" ? ui::Sidebar::Chrome::macWindow : ui::Sidebar::Chrome::flat);
    // --sidebar=collapsed: 다른 미리 보기와 같이 사이드바를 접은 채로 (홈·사이드바 디자인 v2.1 접힌 상태 확인)
    if (meon::devOptions()["sidebar"] == "collapsed")
        appRoot->getSidebar()->setCollapsed (true);   // 실제로 접는다 (멈춘 미리 보기는 프레임마다 다시 그려 다시 그리기 확인을 흐린다)
    auto* home = appRoot->getHome();
    const auto p = devHomePreview;
    // --anim-seq=sidebar,rooms,send,min,...: 화면이 준비된 뒤(2.5초) 1초 간격으로 하나씩 움직이고, 0.7초 뒤 창 전체를 다시 그린다.
    // 녹화해서 다시 그리기 직전·직후 프레임이 다르면 그 자리가 잔상이다 (다시 그리기 확인용)
    if (const auto seq = juce::StringArray::fromTokens (meon::devOptions()["anim-seq"], ",", ""); ! seq.isEmpty())
    {
        juce::Component::SafePointer<MeonEditor> safe (this);
        for (int k = 0; k < seq.size(); ++k)
        {
            const auto act = seq[k].trim();
            juce::Timer::callAfterDelay (2500 + 1000 * k, [safe, act]
            {
                if (safe == nullptr || safe->appRoot == nullptr) return;
                auto& root = *safe->appRoot;
                auto* sb = root.getSidebar();
                if (act == "sidebar") sb->toggleCollapsed();
                else if (act == "rooms" || act == "friends" || act == "offline") sb->devToggleSection (act);
                else if (act == "send" || act == "mon" || act == "chat") { if (auto* s = root.getJamScreen()) s->devToggleFold (act); }
                else if (act == "min" || act == "unmin") root.setMinimized (act == "min");
                else if (act == "modal") { if (sb->onAddFriend) sb->onAddFriend(); }
                else if (act == "close") { if (auto* h = root.getHome()) h->closeOverlays(); if (auto* s = root.getJamScreen()) s->closeOverlays(); }
                std::cerr << "MEON dev: anim " << act << std::endl;
            });
            juce::Timer::callAfterDelay (2500 + 1000 * k + 700, [safe] { if (safe != nullptr) safe->repaint(); });
            juce::Timer::callAfterDelay (2500 + 1000 * k + 850, [safe] { if (safe != nullptr) safe->repaint(); });   // 두 번째: 그리기 자체의 흔들림과 구별
        }
    }
    if (p == "uitest")
    {
        // 화면 자가 테스트: 끝나면 앱을 닫고 실패 수를 종료 코드로 (linux.yml)
        uiTest = std::make_unique<ui::UiSelfTest> (core, *appRoot, meon::devOptions()["uitest"]);
        uiTest->onDone = [] (int failures)
        {
            juce::MessageManager::callAsync ([failures]
            {
                if (auto* app = juce::JUCEApplicationBase::getInstance())
                {
                    app->setApplicationReturnValue (failures == 0 ? 0 : 1);
                    app->systemRequestedQuit();
                }
            });
        };
        uiTest->start();
        return;
    }
    if (p == "showcase")
    {
        // 소개 페이지 캡처: 끝나면 앱을 닫는다 (종료 코드 0 = 저장함)
        showcase = std::make_unique<ui::dev::Showcase> (core, *appRoot, *this, meon::devOptions());
        showcase->onDone = [] (int code)
        {
            juce::MessageManager::callAsync ([code]
            {
                if (auto* app = juce::JUCEApplicationBase::getInstance())
                {
                    app->setApplicationReturnValue (code);
                    app->systemRequestedQuit();
                }
            });
        };
        showcase->start();
        return;
    }
    if (p == "create")      home->openCreateRoom();
    else if (p == "filter") home->openFilter();
    else if (p == "code")   home->openJoinCode();
    else if (p == "notif")  home->toggleNotifications();
    else if (p == "profilemenu")   // 사이드바 맨 아래 프로필을 누른 메뉴 (알림·설정)
    {
        if (auto* sb = appRoot->getSidebar(); sb != nullptr && sb->onProfileMenu) sb->onProfileMenu();
    }
    else if (p == "rooms")  { if (home->onShowRooms) home->onShowRooms(); }   // 공개 방 쪽 (홈·사이드바 디자인)
    else if (p.startsWith ("addfriend"))   // 친구 추가 창 상태 (addfriend=empty|typing|sent|error|requests, 디자인 비교)
        home->devAddFriendState (p.fromFirstOccurrenceOf ("=", false, false).isEmpty() ? juce::String ("empty") : p.fromFirstOccurrenceOf ("=", false, false));
    else if (p.startsWith ("sidebar="))   // 사이드바가 접히는 중 e 에 멈춘 화면 (sidebar=0.5, 펼치는 중은 sidebar=open:0.5)
    {
        const bool open = p.contains ("open:");
        appRoot->getSidebar()->previewFold (open, p.fromLastOccurrenceOf (open ? ":" : "=", false, false).getFloatValue());
    }
    else if (p == "notifinv")   // 알림 목록에 친구 요청 + 초대 (초대는 가짜 서버가 조금 뒤에 보낸다)
    {
        if (auto* fake = core.fakeServer()) fake->devInviteMe();
        juce::Component::SafePointer<MeonEditor> safe (this);
        juce::Timer::callAfterDelay (2500, [safe] { if (safe != nullptr && safe->appRoot != nullptr) safe->appRoot->getHome()->toggleNotifications(); });
    }
    else if (p == "homemon")   // 설정 › 오디오 "입장 전에도 내 소리 듣기" 를 켠 홈 ("내 소리 듣는 중")
    {
        if (auto* jam = core.jam()) jam->setHomeMonitor (true);
    }
    else if (p == "sendoff")   // 송출 정지 중인 홈 (미터 회색)
    {
        if (auto* jam = core.jam()) jam->setSendMuted (true);
    }
    else if (p == "profile")   // 승인 방에 있는 친구의 프로필 ("요청")
    {
        if (const auto* f = core.store().state().findFriend ("u_f_yuna")) home->openProfile (f->user);
    }
    else if (p == "friendreq" || p == "profilereq")   // 친구 목록에서 승인 방에 요청을 보낸 뒤 ("요청 보냄", 같은 방 카드도)
    {
        if (const auto* f = core.store().state().findFriend ("u_f_yuna"))
        {
            core.home().joinFriend (*f, [] (core::Result<core::JoinResult>) {});
            if (p == "profilereq") home->openProfile (f->user);   // 프로필 창도 "요청 보냄"
        }
    }
    else if (p == "fail")   appRoot->showJoinFailure (core::HomeController::JoinFailure::full);
    else if (p == "toast")
    {
        if (auto* fake = core.fakeServer()) fake->devInviteMe();
        appRoot->addInfoToast (TXT ("방장이 방을 닫았어요"), TXT ("금요일 밤 홍대 밴드 합주"));
    }
    else if (p == "ping")   // 방 카드의 예상 지연 (내 RTT 를 정해 두면 방장 RTT 에 따라 낮고 높은 값이 섞인다)
    {
        core.store().setMyRtt (8);
    }
    else if (p.startsWith ("fold="))   // 친구 패널이 접히는 중 e 에 멈춘 화면 (fold=friends:0.5)
    {
        home->previewFold (p.fromLastOccurrenceOf (":", false, false).getFloatValue());
    }
    else if (p == "toastline")   // 한 줄 토스트 (초대 실패 등)가 초대 토스트 아래에 쌓인 모습
    {
        if (auto* fake = core.fakeServer()) fake->devInviteMe();
        appRoot->addInfoToast (ui::msg::inviteFailure (core::ErrorCode::targetOffline), {});
    }
    else if (p == "jam:chat" || p == "jam:links" || p == "jam:chatwide")   // 멤버가 있는 공개 방에 들어가 상대 메시지 여러 개(연달아·긴 글·나간 사람) + 내 말풍선
                                                    // jam:chatwide — 같은 화면에서 오른쪽 패널을 가장 넓게 (1280 → 640)
                                                    // jam:links — 같은 방에서 링크가 든 메시지 (남·나, 문장 끝 마침표·괄호·한글 뒤·여러 개·줄바꿈되는 긴 링크)
    {
        const core::Room* pick = nullptr;
        for (auto& r : core.store().state().publicRooms)
            if (! r.isApproval() && r.memberCount < r.capacity && (pick == nullptr || r.memberCount > pick->memberCount))
                pick = &r;
        if (pick == nullptr) return;
        juce::Component::SafePointer<MeonEditor> safe (this);
        core.home().joinRoom (pick->id, [safe, p] (core::Result<core::JoinResult>)
        {
            juce::Timer::callAfterDelay (800, [safe, p]
            {
                if (safe == nullptr) return;
                auto* jam = safe->core.jam();
                const auto& st = safe->core.store().state();
                if (jam == nullptr || ! st.currentRoom) return;
                juce::StringArray others;
                for (auto& m : st.currentRoom->members)
                    if (! st.me || m.user.id != st.me->id) others.add (m.user.id);
                if (others.isEmpty()) return;
                if (p == "jam:chatwide" && safe->appRoot != nullptr)
                    if (auto* screen = safe->appRoot->getJamScreen()) screen->setSideWidth (100000, false);
                auto& chat = jam->chat();
                int n = 0;
                auto say = [&] (const juce::String& from, const juce::String& text)
                {
                    chat.received ({ chat.group(), from, "id:preview" + juce::String (++n), text });
                };
                if (p == "jam:links")
                {
                    say (others[0], TXT ("오늘 악보는 https://example.com/score.pdf 에 올려 뒀어요."));
                    say (others[0], TXT ("참고 영상(www.example.org/live) 이랑 https://example.net/a, http://example.com/b 도 봐 주세요"));
                    chat.sendText (TXT ("확인했어요https://example.com/setlist 여기 셋리스트예요"));
                    chat.sendText (TXT ("긴 주소도 줄바꿈돼요 https://example.com/very/long/path/that/keeps/going/and/going?with=query&and=more"));
                    return;
                }
                say (others[0], TXT ("다들 들어오셨네요! 오늘은 셋리스트 순서대로 가 볼게요"));
                say (others[0], TXT ("첫 곡 템포 92로 맞춰 주세요"));
                if (others.size() > 1) say (others[1], TXT ("넵 좋아요. 브릿지에서 한 번 쉬고 들어가는 거 맞죠? 지난번에 거기서 다들 엇갈려서 이번엔 제가 손으로 신호 드릴게요"));
                chat.sendText (TXT ("좋아요, 저는 준비됐어요"));
                if (p == "jam:chatwide")   // 패널이 넓으면 내 말풍선은 75% (220..420) 까지
                    chat.sendText (TXT ("패널을 넓히면 내 말풍선도 넓어져요. 목록 안쪽 폭의 75% 까지, 최대 420 이에요. 상대 메시지는 전체 폭을 써요"));
                if (others.size() > 2) say (others[2], TXT ("드럼 소리 잘 들려요?"));
                say ("u_left_preview", TXT ("저는 먼저 나갈게요 (방을 나간 사람: 파트를 몰라 빈 원)"));
            });
        });
    }
    else if (p == "jam:ping")   // 멤버가 있는 공개 방에 들어가 멤버 행에 낮고 높은 핑을 보인다
    {
        const core::Room* pick = nullptr;
        for (auto& r : core.store().state().publicRooms)
            if (! r.isApproval() && r.memberCount < r.capacity && (pick == nullptr || r.memberCount > pick->memberCount))
                pick = &r;
        if (pick == nullptr) return;
        juce::Component::SafePointer<MeonEditor> safe (this);
        core.home().joinRoom (pick->id, [safe] (core::Result<core::JoinResult>)
        {
            if (safe == nullptr) return;
            auto* jam = safe->core.jam();
            const auto& room = safe->core.store().state().currentRoom;
            if (jam == nullptr || ! room) return;
            const int pings[] = { 9, 26, 14, 41, 17, 33 };
            int i = 0;
            for (auto& m : room->members) jam->setPreviewPing (m.user.id, pings[i++ % 6]);
        });
    }
    else if (p == "jam" || p.startsWith ("jam:"))
    {
        // jam:mini — 방 최소화 (홈 + 아래 막대, 배지 0). jam:minihost — 방장 + 긴 방 이름 + 입장 요청 1 + 새 채팅 3.
        // jam:miniarm — minihost 에서 막대의 나가기를 한 번 누른 상태 (방장 툴팁). jam:miniconfirm / jam:minicreate — 최소화 중
        // 공개 방 '바로 입장' / '방 만들기' 를 누른 확인 모달
        // jam:pop=requests|notif|profile|none — 방장 방(입장 요청 1)에서 헤더의 입장 요청·알림 팝오버 또는 프로필 메뉴를 연 화면 (그림자·CPU 확인)
        // jam:pop=addfriend|gear|notifmenu — 방 화면에서 사이드바 친구 추가 · 방 설정 · 프로필 › 알림 (모달 층: 창 전체)
        const bool hostRoom = p == "jam:host" || p == "jam:minihost" || p == "jam:miniarm" || p == "jam:wide" || p == "jam:minireqs" || p.startsWith ("jam:pop=");
        core::CreateRoom c;
        c.type = hostRoom ? "public" : "private";
        if (hostRoom) c.entryMode = juce::String ("approval");
        // jam:headerarm — 헤더 겹침 확인: 방장 + 비공개(입장 코드) + 긴 방 이름(30자) + 방 나가기 확인 상태 (툴팁)
        c.title = p == "jam:minihost" || p == "jam:miniarm" ? TXT ("금요일 밤 홍대 밴드 합주 셋리스트 처음부터 끝까지")
                : p == "jam:headerarm" ? TXT ("금요일 밤 홍대 밴드 합주 셋리스트를 처음부터 끝까지")
                                         : TXT ("준호의 새벽 합주");
        juce::Component::SafePointer<MeonEditor> safe (this);
        core.home().createRoom (c, [safe, p, hostRoom] (core::Result<core::RoomDetail>)
        {
            if (safe != nullptr && p.startsWith ("jam:mini"))
            {
                if (hostRoom)
                    if (auto* fake = safe->core.fakeServer())
                    {
                        fake->devJoinRequest();
                        if (p == "jam:minireqs") fake->devJoinRequest();   // 홈·사이드바: 입장 요청 둘 (쌓인 토스트 + 참여 중 점)
                    }
                juce::Timer::callAfterDelay (800, [safe, p, hostRoom]
                {
                    if (safe == nullptr || safe->appRoot == nullptr) return;
                    safe->appRoot->setMinimized (true);
                    if (auto* jam = safe->core.jam(); jam != nullptr && hostRoom)
                        for (int i = 1; i <= 3; ++i)   // 최소화한 뒤 받은 채팅 (막대의 "새 채팅 3")
                            jam->chat().received ({ jam->chat().group(), "u_left_preview", "id:mini" + juce::String (i), TXT ("미리 보기 채팅 ") + juce::String (i) });
                    if (auto* home = safe->appRoot->getHome())
                        home->devPreviewRoomBar (p.fromFirstOccurrenceOf ("jam:mini", false, false));
                });
                return;
            }
            if (safe != nullptr && p.startsWith ("jam:input") && safe->appRoot != nullptr)
            {
                // 채팅 입력 (합주 최종 디자인 7). jam:input — 여러 줄 메시지(내 것·받은 것·긴 단어) + 입력 칸 2줄,
                // jam:input=scroll — 5줄 (3줄 넘으면 칸 안에서 스크롤), jam:input=962 / jam:input=over — 바이트 표시 (1012 는 빨간색)
                juce::Timer::callAfterDelay (500, [safe, p]
                {
                    if (safe == nullptr || safe->appRoot == nullptr) return;
                    auto* screen = safe->appRoot->getJamScreen();
                    auto* jam = safe->core.jam();
                    if (screen == nullptr || jam == nullptr) return;
                    auto& chat = jam->chat();
                    chat.received ({ chat.group(), "u_left_preview", "id:in1", TXT ("받은 여러 줄 메시지예요\n둘째 줄\n\n한 줄 띄고 넷째 줄") });
                    chat.sendText (TXT ("내 여러 줄 메시지\n두 번째 줄이에요"));
                    chat.sendText (TXT ("긴단어도줄끝에서끊겨요ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"));
                    const auto what = p.fromFirstOccurrenceOf ("=", false, false);
                    juce::String text = TXT ("입력 칸 첫 줄\n둘째 줄");
                    if (what == "scroll") text = TXT ("한 줄\n두 줄\n세 줄\n네 줄 (여기부터 스크롤)\n다섯 줄");
                    auto bytesOf = [] (int n) { juce::String s; while ((int) s.getNumBytesAsUTF8() + 3 <= n) s << TXT ("가"); while ((int) s.getNumBytesAsUTF8() < n) s << "a"; return s; };
                    if (what == "962") text = bytesOf (962);
                    if (what == "over") text = bytesOf (1012);
                    screen->devSetChatInput (text);
                });
            }
            if (safe != nullptr && p.startsWith ("jam:pop="))
            {
                if (auto* fake = safe->core.fakeServer()) fake->devJoinRequest();
                juce::Timer::callAfterDelay (1200, [safe, p]
                {
                    if (safe == nullptr || safe->appRoot == nullptr) return;
                    const auto what = p.fromFirstOccurrenceOf ("=", false, false);
                    if (what == "profile")
                    {
                        if (auto* sb = safe->appRoot->getSidebar(); sb != nullptr && sb->onProfileMenu) sb->onProfileMenu();
                    }
                    else if (what == "addfriend")
                    {
                        if (auto* sb = safe->appRoot->getSidebar(); sb != nullptr && sb->onAddFriend) sb->onAddFriend();
                    }
                    else if (what == "notifmenu")
                    {
                        if (auto* home = safe->appRoot->getHome()) home->toggleNotifications();   // 프로필 메뉴 "알림" 과 같은 길
                    }
                    else if (auto* screen = safe->appRoot->getJamScreen())
                        screen->devOpenPopover (what);
                });
            }
            if (safe != nullptr && p == "jam:headerarm" && safe->appRoot != nullptr)   // 확인 상태는 3초 뒤 풀리므로 스냅숏(5초) 직전에
                juce::Timer::callAfterDelay (3500, [safe]
                {
                    if (safe != nullptr && safe->appRoot != nullptr)
                        if (auto* screen = safe->appRoot->getJamScreen()) screen->devArmLeave();
                });
            if (safe != nullptr && p == "jam:wide" && safe->appRoot != nullptr)   // 오른쪽 패널을 가장 넓게 (1280 → 640, 900 → 406)
                juce::Timer::callAfterDelay (300, [safe]
                {
                    if (safe != nullptr && safe->appRoot != nullptr)
                        if (auto* screen = safe->appRoot->getJamScreen()) screen->setSideWidth (100000, false);
                });
            if (safe != nullptr && p == "jam:sendoff")   // 셀프 모니터를 켠 채 송출 정지 (내 행이 "지금 안 들림")
                if (auto* jam = safe->core.jam()) { jam->setSelfMonitor (true); jam->setSendMuted (true); }
            if (safe != nullptr && (p.startsWith ("jam:fold=") || p == "jam:chatfill"))
            {
                // 접기 미리 보기 (jam:fold=chat:0.25 — 채팅 40줄을 채운 뒤 그 패널이 접히는 중 e 에 멈춤),
                // jam:chatfill — 채팅 300줄을 채운 뒤 채팅·내 오디오·모니터를 1초 간격으로 한 번씩 접었다 펼친다
                //                (채팅이 많을 때 프레임 확인: 앱 로그의 "접기 애니메이션" 줄. 방 화면 친구 패널은 v2.1 에서 뺐다)
                juce::Timer::callAfterDelay (800, [safe, p]
                {
                    if (safe == nullptr || safe->appRoot == nullptr) return;
                    auto* jam = safe->core.jam();
                    auto* screen = safe->appRoot->getJamScreen();
                    if (jam == nullptr || screen == nullptr) return;
                    const int n = p == "jam:chatfill" ? 300 : 40;
                    for (int i = 1; i <= n; ++i)
                        jam->chat().sendText (TXT ("미리 보기 메시지 ") + juce::String (i) + (i % 3 == 0 ? TXT (" · 조금 길게 써서 두 줄로 넘어가는지 보는 문장이에요") : juce::String()));
                    if (p == "jam:chatfill")
                    {
                        const char* order[] = { "chat", "chat", "send", "send", "mon", "mon" };
                        for (int k = 0; k < 6; ++k)
                        {
                            const juce::String panel (order[k]);
                            juce::Timer::callAfterDelay (1000 * (k + 1), [safe, panel]
                            {
                                if (safe != nullptr && safe->appRoot != nullptr)
                                    if (auto* s = safe->appRoot->getJamScreen()) s->devToggleFold (panel);
                            });
                        }
                    }
                    if (p.startsWith ("jam:fold="))
                    {
                        const auto spec = p.fromFirstOccurrenceOf ("jam:fold=", false, false);
                        screen->previewFold (spec.upToFirstOccurrenceOf (":", false, false), spec.fromLastOccurrenceOf (":", false, false).getFloatValue());
                    }
                });
            }
            if (safe != nullptr && (p == "jam:invite" || p == "jam:invite=pending") && safe->appRoot != nullptr)
            {
                // 방 안에서 친구 초대 (홈·사이드바 v2.1: 방 화면에 친구 패널이 없어 프로필 창의 "방 초대").
                // jam:invite — 초대할 수 있는 친구의 프로필 창, jam:invite=pending — "방 초대" 를 누른 뒤 (3초 동안 "취소")
                juce::Timer::callAfterDelay (800, [safe, p]
                {
                    if (safe == nullptr || safe->appRoot == nullptr) return;
                    auto* screen = safe->appRoot->getJamScreen();
                    if (screen == nullptr) return;
                    for (auto& f : safe->core.store().state().friends)
                        if (f.canInvite)
                        {
                            screen->openProfile (f.user.id);
                            if (p.endsWith ("pending"))
                            {
                                const auto id = f.user.id;
                                juce::Timer::callAfterDelay (2500, [safe, id]   // 스냅숏(5초) 때 아직 "취소" (3초)
                                {
                                    if (safe != nullptr) safe->core.home().invite (id, [] (core::Result<core::InviteSent>) {});
                                });
                            }
                            return;
                        }
                });
            }
            if (safe != nullptr && (p == "jam:disc" || p == "jam:discback"))   // 연결 끊김 토스트 (끊긴 채 / 4초 뒤 다시 붙음)
                if (auto* fake = safe->core.fakeServer()) fake->devDropLink (p == "jam:disc" ? 60 : 4);
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
    else if (p.startsWith ("disc") || p.startsWith ("roomlost"))
    {
        // 연결 끊김 토스트. disc: 끊긴 채, discback: 4초 끊겼다 다시 붙음 ("다시 연결됐어요"), roomlost: 오래 끊겨 방에서 나옴.
        // 뒤에 invite 가 붙으면 초대 토스트가 먼저 떠 있을 때 (초대가 3초 뒤 도착한 다음)
        const bool invite = p.endsWith ("invite");
        const auto what = p.upToFirstOccurrenceOf ("invite", false, false);
        if (invite)
            if (auto* fake = core.fakeServer()) fake->devInviteMe();
        juce::Component::SafePointer<MeonEditor> safe (this);
        juce::Timer::callAfterDelay (invite ? 4000 : 1, [safe, what]
        {
            if (safe == nullptr) return;
            if (what == "roomlost")
            {
                core::Notice n;
                n.kind = core::Notice::Kind::roomLostWhileDisconnected;
                if (safe->appRoot != nullptr) safe->appRoot->previewNotice (n);
            }
            else if (auto* fake = safe->core.fakeServer())
                fake->devDropLink (what == "discback" ? 4 : 60);
        });
    }
}

void MeonEditor::applyDevOptions (const juce::StringPairArray& o)
{
    {
        // 개발용: 온보딩 단계 미리 보기 (--screen=login|signup|verify|nickname|part|discord|done)
        using Step = core::AuthController::Step;
        const std::pair<const char*, Step> steps[] = { { "login", Step::login }, { "signup", Step::signup }, { "verify", Step::verify },
                                                        { "nickname", Step::nickname }, { "part", Step::part }, { "discord", Step::discord },
                                                        { "done", Step::done }, { "outdated", Step::outdated }, { "replaced", Step::replaced } };
        for (auto& st : steps)
        {
            if (o["screen"] != st.first) continue;
            if (onboarding == nullptr)
            {
                onboarding = std::make_unique<ui::OnboardingScreen> (core, settingsUpdateHooks());
                onboarding->setLookAndFeel (newLookAndFeel.get());
                onboarding->setTitleBarInset (titleBarInset());
                onboarding->onTitleBarMouseDown = [this] { titleBarMouseDown(); };
                addAndMakeVisible (*onboarding);
                onboarding->setBounds (contentBounds());
            }
            devPreview = true;
            if (st.second == Step::outdated && ! pluginMode)
            {
                // 스냅숏: 빌드 0 에는 업데이터가 없으므로 "새 버전이 있음" 상태로 버튼 모양을 보여 준다
                onboarding->update.state = [] { return ui::SettingsModal::UpdateHooks::State::available; };
                onboarding->update.start = [] {};
            }
            onboarding->showStepForPreview (st.second);
            return;
        }
    }
    if (o["screen"] == "widgets")
    {
        devScreen = std::make_unique<ui::WidgetGallery>();
        addAndMakeVisible (*devScreen);
        devScreen->setBounds (contentBounds());
        return;
    }
    // 홈·합주 미리 보기(--screen=home…|jam)는 가입이 끝난 뒤 showAppRoot 가 연다
}

//==============================================================================
void MeonEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::white);
}

void MeonEditor::paintOverChildren (juce::Graphics& g)
{
    // 윈도우 10 보통 창: 운영체제 1px 테두리(#707070, 색을 바꿀 수 없음) 대신 내용 네 변 끝 1px 에 #EBEBEB (윈도우 11 테두리와 같은 모양).
    // 물리 픽셀 한 줄에 정확히 (배율이 정수가 아니어도 번지지 않게): 실제 내용 크기(물리 px)로 그린다
    auto* peer = getPeer();
    if (peer == nullptr || ! winFrameInstalled || ! winframe::appDrawsBorder (peer->getNativeHandle())) return;
    int w = 0, h = 0;
    if (! winframe::clientPixelSize (peer->getNativeHandle(), w, h) || w <= 2 || h <= 2) return;
    const float s = (float) w / (float) juce::jmax (1, getWidth());
    juce::Graphics::ScopedSaveState save (g);
    g.addTransform (juce::AffineTransform::scale (1.0f / s));
    g.setColour (juce::Colour (0xFF000000u | winframe::borderColourRGB));
    g.fillRect (0, 0, w, 1);
    g.fillRect (0, h - 1, w, 1);
    g.fillRect (0, 0, 1, h);
    g.fillRect (w - 1, 0, 1, h);
}

//==============================================================================
// 맥 독립 앱 창 (MeonWindowChrome)
void MeonEditor::parentHierarchyChanged()
{
    // 창에 들어온 때 (독립 앱은 창이 화면에 뜨기 전): 바로 붙여서 처음부터 제목 줄이 없게 한다. 창이 아직 없으면 다음 메시지에서
    ensureWindowChrome();
    if (! chromeTried && ! pluginMode)
    {
        juce::Component::SafePointer<MeonEditor> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->ensureWindowChrome(); });
    }
}

void MeonEditor::ensureWindowChrome()
{
    if (chromeTried || pluginMode) return;
    auto* peer = getPeer();
    if (peer == nullptr) return;
    chromeTried = true;
    juce::Component::SafePointer<MeonEditor> safe (this);
    chrome::installFullScreenShortcuts();   // 맥: 보기 > 전체 화면으로 전환 (⌃⌘F), fn+F (독립 앱. macOS 11 미만도)
    chromeInstalled = chrome::install (peer->getNativeHandle(), [safe] { if (safe != nullptr) safe->windowChromeChanged(); });
   #if JUCE_WINDOWS
    {
        winframe::Hooks hooks;
        hooks.hitTest = [safe] (int x, int y) { return safe != nullptr ? safe->windowRegionAt (x, y) : winframe::Region::client; };
        hooks.onStateChange = [safe] { if (safe != nullptr) safe->windowChromeChanged(); };
        hooks.onMaxButton = [safe] (int st)
        {
            if (safe != nullptr)
                if (auto* cb = dynamic_cast<ui::WinCaptionButtons*> (safe->chromeView.get())) cb->setMaxButtonState (st);
        };
        const auto clientBefore = peer->getBounds();   // JUCE 가 아는 내용 자리 (창 틀을 바꾸기 전, 맞는 값)
        bool frameOff = false;
       #if MEON_DEV_OPTIONS
        frameOff = meon::devOptions().containsKey ("no-winframe");   // 개발용 비교 (창 가장자리 조사): 운영체제 제목 줄 그대로
       #endif
        winFrameInstalled = ! frameOff && winframe::install (peer->getNativeHandle(), std::move (hooks));
        if (winFrameInstalled && ! winframe::isMaximised (peer->getNativeHandle()))
        {
            // JUCE 는 창 틀 두께를 크기가 바뀔 때만 다시 읽는다 (HWNDComponentPeer::updateBorderSize). 옛 두께(제목 줄 포함)를 쓰면
            // 내용 자리·마우스 좌표가 제목 줄 높이만큼 어긋나므로, 크기를 1px 바꿨다 되돌려 새 두께(위 0)를 읽게 하고 내용 자리를 그대로 둔다.
            // 그 뒤로는 틀 두께가 상태와 상관없이 같다 (checkJuceFrame 이 확인)
            peer->setBounds (clientBefore.withHeight (clientBefore.getHeight() + 1), false);
            peer->setBounds (clientBefore, false);
        }
        if (winFrameInstalled && settings->isWindowMaximised())
        {
            settings->setWindowMaximised (false);
            winframe::toggleMaximise (peer->getNativeHandle());   // 지난번에 최대화한 채 껐다
        }
       #if MEON_DEV_OPTIONS
        // 창 상태별 자가 테스트(--uitest=winstates): 켠 직후 온보딩 화면에서 가장자리·창 버튼 확인 (테스트는 로그인 뒤에 시작한다)
        if (winFrameInstalled && meon::devOptions()["uitest"] == "winstates")
          for (int ms : { 150, 350, 600 })   // 가짜 계정 자동 가입이 끝나기 전 (온보딩이 떠 있을 때) 한 번
            juce::Timer::callAfterDelay (ms, [safe]
            {
                if (safe == nullptr || safe->onboarding == nullptr || ! safe->onboarding->isShowing() || ui::UiSelfTest::launchFrameChecks > 0) return;
                juce::String detail;
                ++ui::UiSelfTest::launchFrameChecks;
                if (! ui::UiSelfTest::checkWindowFrame (*safe, TXT ("켠 직후 (온보딩)"), "00_launch_onboarding", detail))
                {
                    ++ui::UiSelfTest::launchFrameFailures;
                    ui::UiSelfTest::launchFrameDetail = detail;
                }
            });
        // 화면 자가 테스트(--uitest): 켠 직후 (창이 뜨기 전에 붙었는지, 창·내용 사각형)
        if (meon::devOptions().containsKey ("uitest"))
        {
            const auto p = winframe::probe (peer->getNativeHandle());
            std::cerr << "UITEST INFO winchrome launch: installed " << (winFrameInstalled ? 1 : 0) << " showing " << (peer->getComponent().isShowing() ? 1 : 0)
                      << " window " << p.window[0] << "," << p.window[1] << " " << p.window[2] << "x" << p.window[3]
                      << " client " << p.client[0] << "," << p.client[1] << " " << p.client[2] << "x" << p.client[3] << " dpi " << p.dpi << std::endl;
        }
       #endif
    }
   #endif
    windowChromeChanged();
   #if MEON_DEV_OPTIONS
    // 화면 자가 테스트(--uitest): 켠 직후 신호등 자리 (아무것도 움직이기 전, 온보딩 화면)
    if (chromeInstalled && meon::devOptions().containsKey ("uitest"))
        for (int ms : { 0, 1500 })
            juce::Timer::callAfterDelay (ms, [safe, ms]
            {
                if (safe == nullptr || safe->getPeer() == nullptr) return;
                const auto p = chrome::probe (safe->getPeer()->getNativeHandle(), 100, 26, 600, 26);
                std::cerr << "UITEST INFO chrome launch+" << ms << "ms (" << (safe->onboarding != nullptr ? "onboarding" : "app") << ") "
                          << ui::UiSelfTest::describe (p) << std::endl;
            });
   #endif
}

void MeonEditor::checkJuceFrame()
{
    // JUCE 는 창 틀 두께를 자기 크기 변경 때만 다시 읽어(HWNDComponentPeer::updateBorderSize) 논리 px 로 기억하고, 내용 자리를
    // 그 값으로 셈한다. 최대화하면 틀 두께가 바뀐다 (화면 밖으로 나간 크기 조절 두께, MeonWindowFrame). 어긋나 있으면 창은 그대로 둔 채
    // (holdPosition) JUCE 에게 크기 변경을 시켜 틀 두께를 다시 읽게 하고, 그 두께로 내용 자리를 다시 셈하게 한다
    if (! winFrameInstalled) return;
    auto* peer = getPeer();
    if (peer == nullptr || peer->isMinimised()) return;
    void* h = peer->getNativeHandle();
    int in[4];
    winframe::frameInsets (h, in);
    const double s = juce::jmax (0.5, peer->getPlatformScaleFactor());
    const auto have = peer->getFrameSize();
    const juce::BorderSize<int> want (juce::roundToInt (in[1] / s), juce::roundToInt (in[0] / s), juce::roundToInt (in[3] / s), juce::roundToInt (in[2] / s));
    if (have == want) return;
    ++juceFrameFixes;
    core::AppLog::write ("window", "JUCE 창 틀 두께 다시 읽기: " + juce::String (have.getTop()) + "," + juce::String (have.getLeft()) + ","
                                   + juce::String (have.getBottom()) + "," + juce::String (have.getRight()) + " -> "
                                   + juce::String (want.getTop()) + "," + juce::String (want.getLeft()) + ","
                                   + juce::String (want.getBottom()) + "," + juce::String (want.getRight()));
    const auto b = peer->getBounds();
    winframe::holdPosition (h, true);
    peer->setBounds (b.withHeight (b.getHeight() + 1), peer->isFullScreen());   // 창은 움직이지 않고 JUCE 만 틀 두께를 다시 읽는다
    winframe::holdPosition (h, false);
    peer->handleMovedOrResized();   // 새 틀 두께로 내용 자리
}

void MeonEditor::windowChromeChanged()
{
    checkJuceFrame();
    syncChromeView();
    if (onboarding != nullptr) onboarding->setTitleBarInset (titleBarInset());
    if (appRoot != nullptr)
    {
        // 제목 줄 오른쪽 여백: 창 버튼이 있으면 그 폭 + 12 (윈도우 150), 윈도우 전체 화면은 창 버튼이 없으므로 18 (디자인 hdPR)
        appRoot->setTitleRightInset (chromeView != nullptr && ! winFullscreen() ? juce::jmax (ui::metric::padX, chromeView->titleRightInset()) : ui::metric::padX);
        appRoot->windowChromeChanged();
    }
    repaint();
}

void MeonEditor::syncChromeView()
{
    using Kind = ui::WindowChromeView::Kind;
    std::optional<Kind> kind;
    if (winFrameInstalled || devMockWin) kind = Kind::windows;
    else if (chromeInstalled || devMockChrome) kind = Kind::mac;
    if (! kind.has_value()) { chromeView.reset(); return; }
    if (chromeView == nullptr || chromeView->kind() != *kind)
    {
        chromeView = ui::createWindowChromeView (*kind);
        chromeView->setMock (devMockChrome);
        if (auto* cb = dynamic_cast<ui::WinCaptionButtons*> (chromeView.get()))
        {
            // 운영체제 창 버튼과 같은 길 (WM_SYSCOMMAND). 미리 보기(--chrome=win, 윈도우 밖)는 JUCE 창 동작으로
            juce::Component::SafePointer<MeonEditor> safe (this);
            auto handle = [safe]() -> void* { return safe != nullptr && safe->getPeer() != nullptr ? safe->getPeer()->getNativeHandle() : nullptr; };
            cb->onMinimise = [safe, handle]
            {
                if (safe == nullptr) return;
                if (safe->winFrameInstalled) winframe::minimise (handle());
                else if (auto* p = safe->getPeer()) p->setMinimised (true);
            };
            cb->onToggleMaximise = [safe, handle]
            {
                if (safe == nullptr) return;
                if (safe->winFullscreen()) winframe::setFullscreen (handle(), false);   // 전체 화면: "이전 크기로" = 빠져나가기
                else if (safe->winFrameInstalled) winframe::toggleMaximise (handle());
                else if (auto* p = safe->getPeer()) p->setFullScreen (! p->isFullScreen());
            };
            cb->onClose = [safe, handle]
            {
                if (safe == nullptr) return;
                if (safe->winFrameInstalled) winframe::close (handle());
                else if (auto* app = juce::JUCEApplicationBase::getInstance()) app->systemRequestedQuit();
            };
        }
        addAndMakeVisible (*chromeView);
    }
    chromeView->place (contentBounds());
    if (chromeView->kind() == Kind::windows)
    {
        updateWindowState();
        // F11 전체 화면에서는 숨긴다 (디자인 fullscreen=켜짐). 마우스를 화면 맨 위에 대면 보인다 (updateCaptionReveal)
        const bool fs = winFullscreen();
        if (! fs) captionRevealed = false;
        chromeView->setVisible (! fs || captionRevealed);
        if (fs) { revealTimer.tick = [this] { updateCaptionReveal(); }; revealTimer.startTimerHz (20); }
        else revealTimer.stopTimer();
    }
    else
    {
        chromeView->setVisible (titleBarInset() > 0);   // 맥 전체 화면이면 신호등이 없다
    }
    childrenChanged();   // 맨 위로 (툴팁 창 아래)
}

void MeonEditor::updateWindowState()
{
    // 창 버튼 표시: 최대화(이전 크기로 아이콘)·활성(비활성이면 회색)
    if (chromeView == nullptr || chromeView->kind() != ui::WindowChromeView::Kind::windows) return;
    auto* peer = getPeer();
    void* h = peer != nullptr ? peer->getNativeHandle() : nullptr;
    bool maxed = winFrameInstalled && (winframe::isMaximised (h) || winframe::isFullscreen (h)), active = ! winFrameInstalled || winframe::isActive (h);   // 전체 화면: "이전 크기로" 아이콘
   #if MEON_DEV_OPTIONS
    if (devMockWin)   // 미리 보기: --winstate=max|inactive|max+inactive (디자인 winState·winActive)
    {
        const auto ws = meon::devOptions()["winstate"];
        maxed = ws.contains ("max");
        active = ! ws.contains ("inactive");
    }
   #endif
    chromeView->setWindowState (maxed, active);
}

void MeonEditor::childrenChanged()
{
    // 창 윗부분은 늘 맨 위: 화면(온보딩·앱·설정 팝업)이 새로 붙거나 앞으로 와도 그 위. 툴팁 창(에디터의 자식)만 그보다 위
    if (chromeView == nullptr) return;
    const int n = getNumChildComponents();
    const bool tipOnTop = tooltipWindow.getParentComponent() == this && getIndexOfChildComponent (&tooltipWindow) == n - 1;
    const int want = tipOnTop ? n - 2 : n - 1;
    if (getIndexOfChildComponent (chromeView.get()) == want) return;
    if (tipOnTop) chromeView->toBehind (&tooltipWindow);
    else chromeView->toFront (false);
}

winframe::Region MeonEditor::windowRegionAt (int sx, int sy)
{
    // 화면 물리 좌표 → 에디터 좌표 (모니터 배율 반영)
    const auto logical = juce::Desktop::getInstance().getDisplays().physicalToLogical (juce::Point<int> (sx, sy));
    const auto p = getLocalPoint (nullptr, logical);
    const auto content = contentBounds();
    if (! content.contains (p) || p.y >= content.getY() + chrome::barHeight) return winframe::Region::client;
    if (auto* cb = dynamic_cast<ui::WinCaptionButtons*> (chromeView.get()); cb != nullptr && cb->isVisible())
    {
        const auto which = cb->buttonAt (cb->getLocalPoint (this, p));
        if (which == ui::WinCaptionButtons::maximise) return winframe::Region::maxButton;   // 윈도우 11 스냅 레이아웃
        if (which != ui::WinCaptionButtons::none) return winframe::Region::client;          // 최소화·닫기는 앱이 받는다
    }
    // 맨 위에 뜬 것(설정 팝업·개발 화면)이 있으면 그 자리는 앱 화면 (맥에서 팝업이 떠 있으면 끌리지 않는 것과 같다)
    auto* hit = getComponentAt (p);
    if (hit == nullptr) return winframe::Region::client;
    if (hit == this) return winframe::Region::caption;
    if (onboarding != nullptr && onboarding->isVisible() && (hit == onboarding.get() || onboarding->isParentOf (hit)))
        return onboarding->titleBarDragAt (hit, onboarding->getLocalPoint (this, p)) ? winframe::Region::caption : winframe::Region::client;
    if (appRoot != nullptr && appRoot->isVisible() && (hit == appRoot.get() || appRoot->isParentOf (hit)))
        return appRoot->titleBarDragAt (appRoot->getLocalPoint (this, p)) ? winframe::Region::caption : winframe::Region::client;
    return winframe::Region::client;
}

int MeonEditor::titleBarInset() const
{
    if (devMockChrome || devMockWin) return chrome::barHeight;
    if (winFrameInstalled) return chrome::barHeight;   // 윈도우: 최대화해도 창 버튼 줄 그대로
    auto* peer = getPeer();
    return chromeInstalled && peer != nullptr && chrome::installedOn (peer->getNativeHandle()) && ! peer->isFullScreen() ? chrome::barHeight : 0;
}

void MeonEditor::titleBarMouseDown()
{
    if (titleBarInset() <= 0) return;
    if (auto* peer = getPeer())
        chrome::titleBarMouseDown (peer->getNativeHandle());
}

void MeonEditor::resized()
{
    if (settingsOverlay != nullptr)
        settingsOverlay->setBounds (contentBounds());
    if (devScreen != nullptr)
        devScreen->setBounds (contentBounds());
    if (appRoot != nullptr)
        appRoot->setBounds (contentBounds());
    if (onboarding != nullptr)
        onboarding->setBounds (contentBounds());
    if (chromeView != nullptr) { chromeView->place (contentBounds()); updateWindowState(); }   // 최대화·복원은 크기가 바뀌므로 아이콘도 바로
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
