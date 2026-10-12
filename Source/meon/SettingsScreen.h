// MEON - 설정 (오디오 장치·귀 테러 방지, 닉네임, 로그 폴더 열기, 앱 정보)
#pragma once

#include "MeonEditor.h"
#include "MeonWidgets.h"
#include "MeonAudio.h"
#include "MeonParts.h"

namespace meon
{

class SettingsScreen : public ScreenBase,
                       private juce::Timer,
                       private juce::ChangeListener
{
public:
    explicit SettingsScreen (MeonEditor&);
    ~SettingsScreen() override;

    bool handleShortcut (const juce::KeyPress&) override;
    void resized() override;
    void paint (juce::Graphics&) override;

    /** 개발용: --screen=settings:input|output|buffer 로 드롭다운을 열어 둔 상태를 캡처한다. */
    void showPopupForTest (const juce::String& which);

private:
    class LinkLabel : public juce::Button
    {
    public:
        LinkLabel (const juce::String& text, const juce::String& url, float px);
        void paintButton (juce::Graphics&, bool, bool) override;
        int getIdealWidth() const;
    private:
        juce::String text, url;
        float px;
    };

    class Content : public juce::Component
    {
    public:
        explicit Content (SettingsScreen& owner);
        void layout (int width, int topPad);
        void paint (juce::Graphics&) override;
        void refreshDevices();
        void pushLevel (float db, double dt)
        {
            meter.push (db, dt);
            const int shown = meter.getShownDb() <= -60.0f ? -60 : (int) std::lround (meter.getShownDb());
            if (shown != lastLevelText)   // 숫자가 바뀔 때만 다시 그린다
            {
                lastLevelText = shown;
                repaint (levelTextArea);
            }
        }
        int lastLevelText = 1000;

        SettingsScreen& owner;
        const bool plugin;
        juce::ComboBox driverCombo, inCombo, outCombo, bufCombo;
        MeonButton driverPanelButton;
        LevelBar meter;
        MeonTextEditor nickInput;
        PartSelector partSelector;
        SegmentedTabs earGuardTabs;   // 귀 테러 방지: 끔 · 보통 · 강하게 (플러그인에서도 보인다)
        juce::Rectangle<int> earGuardDescArea;
        static juce::String earGuardDescription (int level);
        MeonButton saveButton, logButton, updateButton;
        LinkLabel licenseLink, sourceLink;
        juce::Rectangle<int> levelTextArea;
        struct Row { juce::String label; juce::Rectangle<int> area; };
        std::vector<std::pair<juce::String, juce::Rectangle<int>>> sectionTitles, rowLabels, infoRows, texts, subTexts;
        std::vector<juce::Rectangle<int>> dividers;
        juce::Rectangle<int> audioPanel, updateArea;
        const bool showUpdate;
        bool inRoom = false;
        /** 업데이트 상태에 맞춰 버튼 이름·모양을 바꾼다. 버튼 폭이 바뀌면 다시 배치한다. */
        void refreshUpdate();
        juce::String updateTitle() const;
        juce::String updateDetail() const;
        bool updating = false;
        juce::Array<int> bufferChoices;
        juce::StringArray driverChoices;
        void applyDriver();
        void applyDevices();
        void applyBuffer();
        void saveNickname();
    };

    MeonEditor& editor;
    const bool plugin;
    MeonButton closeButton;
    juce::Viewport viewport;
    Content content;
    double lastTickMs = 0.0;
    int lastBlockSize = 0;

    void timerCallback() override;
    void meterFrame();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    std::unique_ptr<juce::VBlankAttachment> vblank;   // 레벨 막대: 화면 프레임마다 (독립 앱만)
};

} // namespace meon
