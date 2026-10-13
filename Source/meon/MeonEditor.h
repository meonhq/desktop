// MEON - 최상위 에디터. 화면 전환, 단축키, 셸(독립 앱)과의 연결을 맡는다.
#pragma once

#include "../SonobusPluginProcessor.h"
#include "MeonTheme.h"
#include "MeonLookAndFeel.h"
#include "MeonSettings.h"
#include "MeonSession.h"
#include "MeonUpdater.h"
#include "shell/ShellHooks.h"

namespace meon
{

/** 모든 화면의 공통 베이스 */
class ScreenBase : public juce::Component
{
public:
    ScreenBase() { setOpaque (true); }
    /** 에디터가 전달하는 단축키. 처리했으면 true */
    virtual bool handleShortcut (const juce::KeyPress&) { return false; }
    /** 종료 요청 시 확인 대화상자를 띄웠으면 true (그 화면이 나가기 후 종료까지 처리한다) */
    virtual bool confirmLeaveForQuit() { return false; }
    void paint (juce::Graphics& g) override { g.fillAll (col::white); }
};

class MeonEditor : public juce::AudioProcessorEditor,
                   public ShellHooks,
                   private MeonSession::Listener
{
public:
    enum class Screen { Nickname, Audio, Headphone, Level, Home, Create, Join, Jam };

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
    MeonSession& getSession()               { return *session; }
    MeonSettings& getSettings()             { return *settings; }
    bool isPluginMode() const               { return pluginMode; }
    juce::AudioDeviceManager* deviceManager() const { return getAudioDeviceManager ? getAudioDeviceManager() : nullptr; }

    void go (Screen s, bool fade = true);
    Screen getCurrentScreen() const { return current; }
    void openSettings();
    void closeSettings();
    bool isSettingsOpen() const { return settingsOverlay != nullptr; }

    /** Windows 독립 앱 업데이트. 받기가 끝나면(합주 중이 아니면) 바로 설치 프로그램을 실행하고 앱을 끈다. */
    MeonUpdater& getUpdater()               { return *updater; }
    void startUpdate();
    void installUpdate();

    void copyRoomCodeToClipboard();
    void saveAll();

    /** 개발·테스트용 실행 옵션 (독립 앱 명령줄): 닉네임 지정, 특정 화면 열기, 자동으로 방 만들기/입장 */
    void applyTestOptions (const juce::String& nickname, const juce::String& screenName,
                           bool autoCreateRoom, const juce::String& autoJoinCode, bool autoEnterJam);

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
    MeonLookAndFeel lookAndFeel;
    std::unique_ptr<MeonSettings> settings;
    std::unique_ptr<MeonSession> session;
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
    juce::TooltipWindow tooltipWindow { this, 500 };   // 좁은 플러그인 창의 서버 상태 툴팁

    std::unique_ptr<juce::Component> screen;
    std::unique_ptr<juce::Component> fadingOut;
    std::unique_ptr<juce::Component> settingsOverlay;
    std::unique_ptr<juce::Component> settingsFadingOut;
    std::unique_ptr<juce::Component> devScreen;   // 개발용 화면 (--screen=widgets)
    Screen current = Screen::Home;
    bool quitAfterLeave = false;
    bool autoCreate = false, autoEnter = false;
    juce::String autoJoin;

    std::unique_ptr<juce::Component> createScreen (Screen);
    void showComponent (std::unique_ptr<juce::Component> c, bool fade);
    void finishFade();
    Screen initialScreen() const;
    MeonSessionLog::AudioInfo collectAudioInfo() const;

    // Session::Listener
    void roomJoined() override;
    void roomLeft() override;
    void sessionStateChanged() override;
    void runAutoRoomAction();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeonEditor)
};

} // namespace meon
