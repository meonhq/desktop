// MEON ui - 조작: 토글 스위치, 세그먼트(작은 버튼 묶음), 칩 묶음, dB 슬라이더, 패닝 노브.
#pragma once

#include "../theme/Tokens.h"

namespace meon::ui
{

/** 0→1 로 시간을 두고 움직이는 값 (VBlank 마다). 0.15s / 0.3s 전환에 쓴다 */
class AnimatedValue
{
public:
    explicit AnimatedValue (float initial = 0.0f) : value (initial), target (initial) {}
    void setTarget (float t, double durationMs);
    void jumpTo (float v) { value = target = v; }
    /** 시간이 흘렀으면 true (다시 그려야 함) */
    bool tick (double nowMs);
    float get() const { return value; }
    float getTarget() const { return target; }
    bool isMoving() const { return value != target; }

private:
    float value, target, from = 0.0f;
    double startMs = 0.0, durMs = 0.0;
};

/** 토글 스위치 26×16, 켬 #FFD700 / 끔 #E0E0E0, 손잡이 12px 흰 원, 0.15s */
class Toggle : public juce::Component
{
public:
    Toggle();
    void setOn (bool, bool animate = true);
    bool isOn() const { return on; }
    std::function<void (bool)> onChange;

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    bool on = false;
    AnimatedValue pos;
    juce::VBlankAttachment vblank;
};

/** 고르기 묶음. small: 높이 19 작은 버튼 (설정의 모노/스테레오 …), chip: 높이 32 칩 (필터·공개 설정, 고정 폭 가능) */
class Choice : public juce::Component
{
public:
    enum class Style { small, chip };
    Choice (Style, bool multiSelect = false, int fixedChipWidth = 0);

    void setOptions (const juce::StringArray& values, const juce::StringArray& labels);
    /** values 가 파트 키(guitar …)일 때 칩 글자 앞에 파트 아이콘(얇은 버전 16px)을 그린다. 색은 칩 글자색 */
    void setPartIcons (bool on) { partIcons = on; repaint(); }
    void setSelected (const juce::String& value);          // 하나 고르기
    juce::String getSelected() const;
    void setSelectedSet (const juce::StringArray& values);  // 여러 개 고르기
    juce::StringArray getSelectedSet() const { return selected; }
    int getPreferredWidth() const;
    int getPreferredHeight() const { return style == Style::chip ? metric::buttonH : metric::smallH; }

    std::function<void()> onChange;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    Style style;
    bool multi;
    int fixedW;
    juce::StringArray values, labels, selected;
    int hover = -1;
    bool partIcons = false;
    static constexpr float partIconPx = 16.0f, partIconGap = 4.0f;
    int gapPx() const { return 6; }
    juce::Rectangle<float> itemBounds (int i) const;
    int itemWidth (int i) const;
};

/** dB 슬라이더 (디자인의 볼륨 막대). 높이 19, 트랙 4px #E0E0E0, 채움 #FFD700(꺼짐이면 #D5D5D5), 손잡이 19px 흰 원.
    0.5 dB 단위, ±1 dB 안이면 0, 더블클릭 0 dB, 드래그 중 위 말풍선 "+1.5 dB", 휠 없음.
    linear 모드는 0~1 위치 (파일 재생 위치). */
class DbSlider : public juce::Component, public juce::SettableTooltipClient
{
public:
    /** linearPosition: 0~1 위치 (파일 재생 위치). 단위·0 붙음·말풍선 없음 */
    DbSlider (float minValue, float maxValue, bool linearPosition = false);

    void setValue (float, bool notify = false);
    float getValue() const { return value; }
    void setMuted (bool);         // 채움을 회색으로
    void setShowTip (bool b) { showTip = b; }
    void setTitle (const juce::String& t) { setHelpText (t); setTooltip (t); }
    bool isDragging() const { return dragging; }

    std::function<void (float)> onChange;
    std::function<void()> onDragEnd;
    /** 말풍선 글자. 없으면 "+1.5 dB" */
    std::function<juce::String (float)> formatTip;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    static juce::String dbText (float db);   // "+1.5 dB", "0.0 dB", "-12.0 dB"
    /** 위치(0~1) → 값 (0.5 단위, ±1 안이면 0) */
    float valueFromFraction (float f) const;

private:
    float minV, maxV, value = 0.0f;
    bool linear = false, muted = false, dragging = false, showTip = true;
    std::unique_ptr<juce::Component> tip;
    void setFromX (float x);
    void updateTip();
};

/** 패닝 노브 19px. 세로 드래그 1px = 1, ±100, |값| < 3 이면 0, 더블클릭 가운데, 회전 = 값 × 1.35°.
    드래그 중 말풍선 "L 30" / "C" / "R 30" */
class PanKnob : public juce::Component
{
public:
    PanKnob();
    void setValue (int, bool notify = false);
    int getValue() const { return value; }
    std::function<void (int)> onChange;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    static juce::String panText (int v);   // "C", "L 30", "R 30"

private:
    int value = 0, startValue = 0;
    std::unique_ptr<juce::Component> tip;
    void updateTip();
};

/** 슬라이더·노브 위에 뜨는 검정 말풍선 (여백 4·7, 모서리 4, 흰 글자 500 12px). 부모 맨 위에 붙는다 */
class TipBubble : public juce::Component
{
public:
    void show (juce::Component& anchor, const juce::String& text) { showAt (anchor, anchor.getLocalBounds(), text); }
    /** anchor 안의 area 위 6px 에 띄운다 */
    void showAt (juce::Component& anchor, juce::Rectangle<int> area, const juce::String& text);
    void paint (juce::Graphics&) override;
private:
    juce::String text;
};

} // namespace meon::ui
