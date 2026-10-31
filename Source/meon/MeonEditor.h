// MEON - 최상위 에디터. 화면 전환, 단축키, 셸(독립 앱)과의 연결을 맡는다.
#pragma once

#include "../SonobusPluginProcessor.h"
#include "MeonTheme.h"
#include "MeonSettings.h"
#include "MeonUpdater.h"
#include "shell/ShellHooks.h"
#include "core/MeonCore.h"
#include "ui/overlays/SettingsModal.h"
#include "ui/shell/WindowChromeView.h"
#include "MeonWindowFrame.h"

namespace meon
{

namespace ui { class OnboardingScreen; class LookAndFeel; class AppRoot; class UiSelfTest; namespace dev { class Showcase; } }

class MeonEditor : public juce::AudioProcessorEditor,
                   public ShellHooks,
                   private core::AuthController::Listener,
                   private core::AppStore::Listener,
                   private juce::Timer
{
public:
    explicit MeonEditor (SonobusAudioProcessor&);
    ~MeonEditor() override;

    //==============================================================================
    // 셸(독립 앱)과의 연결점
    /** 독립 앱 종료 요청. true 를 돌려주면 바로 종료해도 된다. false 면 확인 뒤 스스로 종료한다. */
    bool requestedQuit() override;
    /** 개발용 실행 옵션 (--nickname= --screen= --create --join= --enter) */
    void applyDevOptions (const juce::StringPairArray& options) override;

    //==============================================================================
    SonobusAudioProcessor& getProcessor()   { return processor; }
    MeonSettings& getSettings()             { return *settings; }
    bool isPluginMode() const               { return pluginMode; }
    juce::AudioDeviceManager* deviceManager() const { return getAudioDeviceManager ? getAudioDeviceManager() : nullptr; }

    /** tab: 0 계정 … (ui::SettingsModal::Tab 순서) */
    void openSettings (int tab = 0);
    ui::SettingsModal::UpdateHooks settingsUpdateHooks();
    void closeSettings();
    bool isSettingsOpen() const { return settingsOverlay != nullptr; }

    /** Windows 독립 앱 업데이트. 받기가 끝나면(합주 중이 아니면) 바로 설치 프로그램을 실행하고 앱을 끈다. */
    MeonUpdater& getUpdater()               { return *updater; }
    void startUpdate();
    void installUpdate();

    void saveAll();

    juce::String getHostDescription() const;
    juce::String getVersionText() const;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;   // 개발용 (--chrome=mac): 온보딩 위의 가짜 신호등
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void parentHierarchyChanged() override;

private:
    SonobusAudioProcessor& processor;
    juce::SharedResourcePointer<FontCache> fontCache;
    std::unique_ptr<MeonSettings> settings;
    std::unique_ptr<MeonUpdater> updater;
    const bool pluginMode;

    /** 120 ms 페이드 (알파만 바꾼다. ComponentAnimator 는 bounds 까지 되돌리므로 쓰지 않는다) */
    class Fader : private juce::Timer
    {
    public:
        void fade (juce::Component* c, float from, float to, int ms, std::function<void()> onDone = {});
        void cancel (juce::Component* c);
    private:
        struct Item { juce::Component::SafePointer<juce::Component> comp; float from, to; double startMs; int ms; std::function<void()> onDone; };
        std::vector<Item> items;
        void timerCallback() override;
    };
    Fader fader;
    // 창 윗부분 구성 요소 (아래 설명). 툴팁 창보다 먼저 만든다: 툴팁 창이 에디터에 붙을 때 childrenChanged 가 이것을 본다
    std::unique_ptr<ui::WindowChromeView> chromeView;
    juce::TooltipWindow tooltipWindow { this, 500 };

    ui::ModalSlot<ui::SettingsModal> settingsOverlay;   // 열고 닫는 애니메이션은 공통 모달 래퍼 (ModalBase)
    std::unique_ptr<juce::Component> devScreen;   // 개발용 화면 (--screen=widgets)

    core::MeonCore& core;
    std::unique_ptr<ui::LookAndFeel> newLookAndFeel;
    std::unique_ptr<ui::OnboardingScreen> onboarding;
    std::unique_ptr<ui::AppRoot> appRoot;   // 로그인 뒤: 홈 ↔ 합주

    // 독립 앱 창 윗부분: 제목 줄 없이 내용이 창 맨 위까지. 창이 뜨기 전에 붙여서 온보딩부터 앱을 끌 때까지 같다.
    //   맥 (MeonWindowChrome): 빈 툴바 + unified, 신호등은 macOS 가 그린다
    //   윈도우 (MeonWindowFrame): 운영체제 창 틀은 두고 제목 줄 자리만 내용으로 (WM_NCCALCSIZE·WM_NCHITTEST), 창 버튼은 앱이 그린다
    // 운영체제별 창 윗부분 구성 요소(chromeView: 맥 신호등 자리 / 윈도우 창 버튼)는 에디터 맨 위 (설정·방 만들기 팝업 위에서도 보이고 눌러진다)
    bool chromeTried = false, chromeInstalled = false, winFrameInstalled = false;
    bool devMockChrome = false;   // 개발용 --chrome=mac: 맥이 아닌 곳에서 온보딩도 맥 창 배치로 (가짜 신호등)
    bool devMockWin = false;      // 개발용 --chrome=win: 윈도우가 아닌 곳에서 윈도우 창 버튼 배치 미리 보기
    void ensureWindowChrome();
    void windowChromeChanged();   // 붙은 뒤·전체 화면에 들어가고 나온 뒤·최대화·활성 바뀜
    void syncChromeView();        // 운영체제별 창 윗부분 구성 요소: 만들기·자리·상태·맨 위로
    void updateWindowState();     // 창 버튼 표시 (최대화·활성)
    // 전체 화면 (독립 앱): 맥 ⌃⌘F·fn+F·보기 메뉴 (macOS 기본 전체 화면), 윈도우 F11 (창 틀·작업 표시줄까지 덮는다).
    // 윈도우 전체 화면에서는 창 버튼을 숨기고, 마우스를 화면 맨 위에 대면 보인다 (빠져나갈 수 있게)
    bool winFullscreen() const;
    void toggleFullscreen();
    bool captionRevealed = false;
    struct RevealTimer : juce::Timer { std::function<void()> tick; void timerCallback() override { if (tick) tick(); } } revealTimer;
    void updateCaptionReveal();
    bool quittingFromFullscreen = false;
    /** 윈도우 창 영역 판정 (화면 물리 좌표): 창 버튼(최대화는 운영체제 영역) · 제목 줄 빈 곳 · 그 밖 */
    winframe::Region windowRegionAt (int screenX, int screenY);
    void childrenChanged() override;
    /** 화면 배치 자리: 윈도우 최대화 때 화면 밖으로 나간 위쪽(hiddenTop)을 뺀 나머지. 그 밖에는 에디터 전체 */
    juce::Rectangle<int> contentBounds() const;
    int hiddenTopLogical() const;
    int lastHiddenTop = 0;
    /** 맨 위 창 버튼 줄 높이: 맥 창 모드·윈도우 독립 앱이면 52, 아니면(플러그인·맥 전체 화면) 0 */
    int titleBarInset() const;
    void titleBarMouseDown();
    void authStepChanged (core::AuthController::Step) override;
    void updateOnboarding();
    void showAppRoot();
    void applyHomePreview();
    bool devPreview = false, devJam = false, devSignupStarted = false;
    juce::String devHomePreview;   // --screen=home[:create|filter|code|notif|fail|toast|disc] | jam (스냅숏용)
    std::unique_ptr<ui::UiSelfTest> uiTest;   // --uitest (appRoot 보다 먼저 지워진다)
    std::unique_ptr<ui::dev::Showcase> showcase;   // --showcase (소개 페이지 캡처, appRoot 보다 먼저 지워진다)

    // 업데이트 (버그 #16): 켤 때 + 6시간마다 확인, 서버 config 의 latest_build 가 내 빌드보다 크면 바로 확인.
    // 받기·설치는 "업데이트"를 눌렀을 때만, 합주 중에는 막는다
    juce::int64 lastUpdateCheck = 0;
    bool updateCheckQueued = false;   // config.updated 로 무작위 지연 확인을 걸어 둠
    void checkForUpdate();
    void timerCallback() override;
    void stateChanged (core::StateKey) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeonEditor)
};

} // namespace meon
