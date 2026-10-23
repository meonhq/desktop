// MEON - 최상위 에디터. 화면 전환, 단축키, 셸(독립 앱)과의 연결을 맡는다.
#pragma once

#include "../SonobusPluginProcessor.h"
#include "MeonTheme.h"
#include "MeonSettings.h"
#include "MeonUpdater.h"
#include "shell/ShellHooks.h"
#include "core/MeonCore.h"
#include "ui/overlays/SettingsModal.h"

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
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;

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
    juce::TooltipWindow tooltipWindow { this, 500 };

    std::unique_ptr<juce::Component> settingsOverlay;
    std::unique_ptr<juce::Component> settingsFadingOut;
    std::unique_ptr<juce::Component> devScreen;   // 개발용 화면 (--screen=widgets)

    core::MeonCore& core;
    std::unique_ptr<ui::LookAndFeel> newLookAndFeel;
    std::unique_ptr<ui::OnboardingScreen> onboarding;
    std::unique_ptr<ui::AppRoot> appRoot;   // 로그인 뒤: 홈 ↔ 합주
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
