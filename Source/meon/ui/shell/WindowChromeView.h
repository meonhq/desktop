// MEON ui - 창 윗부분 (운영체제별 구성 요소). 실행 중인 운영체제에 따라 하나를 고른다 (createWindowChromeView):
//   MacTrafficLights   맥 창 모드: 신호등은 macOS 가 그린다 (MeonWindowChrome: 빈 툴바). 이 구성 요소는 그 자리를 비워 둘 뿐이고,
//                      맥이 아닌 곳의 미리 보기(--chrome=mac)에서만 가짜 신호등을 그린다
//   WinCaptionButtons  윈도우: 창 버튼 셋 (최소화 · 최대화/이전 크기로 · 닫기). 윈도우 10 에서도 윈도우 11 모양을 직접 그린다.
//                      버튼 46×51 (제목 줄 아래 1px 선을 덮지 않게 51), 창 오른쪽 위 끝. 실제 창 제어는 MeonWindowFrame
// 에디터 맨 위에 둔다: 설정·방 만들기 같은 팝업이 떠 있어도 그 위에 보이고 눌러진다.
#pragma once

#include <JuceHeader.h>

namespace meon::ui
{

class WindowChromeView : public juce::Component
{
public:
    enum class Kind { mac, windows };
    virtual Kind kind() const = 0;
    /** 본문 제목 줄 오른쪽에서 비워 둘 폭 (윈도우: 창 버튼 138 + 12 = 150). 0 이면 지금까지대로 */
    virtual int titleRightInset() const { return 0; }
    /** 창 상태: 최대화(이전 크기로 아이콘), 활성(비활성이면 아이콘 #AAAAAA) */
    virtual void setWindowState (bool /*maximised*/, bool /*active*/) {}
    /** 자리: 에디터 크기가 바뀔 때 */
    virtual void place (juce::Rectangle<int> editorBounds) = 0;
    /** 개발용: 운영체제가 그리는 것을 대신 그린다 (맥 신호등) */
    virtual void setMock (bool) {}
};

/** 맥: 신호등 자리 (보이는 것은 macOS 가 그린다) */
class MacTrafficLights : public WindowChromeView
{
public:
    MacTrafficLights();
    Kind kind() const override { return Kind::mac; }
    void place (juce::Rectangle<int>) override;
    void setMock (bool m) override { mock = m; repaint(); }
    void paint (juce::Graphics&) override;
    /** 가짜 신호등 오른쪽 끝 (macOS 26 러너에서 잰 확대 버튼 틀 오른쪽, mac #224) */
    static constexpr int mockRight = 79;
    static void drawMock (juce::Graphics&);
private:
    bool mock = false;
};

/** 윈도우: 창 버튼 셋 (윈도우 11 모양) */
class WinCaptionButtons : public WindowChromeView, public juce::TooltipClient
{
public:
    static constexpr int buttonW = 46, buttonH = 51, width = buttonW * 3, iconGap = 12;
    enum Which { none = -1, minimise = 0, maximise = 1, closeButton = 2 };

    WinCaptionButtons();
    Kind kind() const override { return Kind::windows; }
    int titleRightInset() const override { return width + iconGap; }
    void setWindowState (bool maximised, bool active) override;
    void place (juce::Rectangle<int> editorBounds) override;

    /** 최대화 버튼 표시 (운영체제 영역이라 MeonWindowFrame 이 알려 준다): 0 보통, 1 올림, 2 누름 */
    void setMaxButtonState (int);
    /** 이 구성 요소 좌표의 버튼 (없으면 none) */
    Which buttonAt (juce::Point<int>) const;
    juce::Rectangle<int> buttonBounds (Which w) const { return { (int) w * buttonW, 0, buttonW, buttonH }; }
    bool isMaximised() const { return maximised; }
    bool isActiveLook() const { return active; }
    /** 개발용 확인: 버튼의 지금 배경색 */
    juce::Colour devFill (Which) const;

    std::function<void()> onMinimise, onToggleMaximise, onClose;

    /** 디자인 title: 최소화 · 최대화/이전 크기로 · 닫기 */
    juce::String getTooltip() override;
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    bool maximised = false, active = true;
    Which hover = none, pressed = none;
    int ncMaxState = 0;   // 최대화 버튼 (운영체제 영역에서 온 표시)
    juce::Colour fillFor (Which) const;
    void drawIcon (juce::Graphics&, Which, juce::Rectangle<float> box, juce::Colour) const;
};

/** 실행 중인 운영체제에 맞는 창 윗부분 (맥 → 신호등 자리, 윈도우 → 창 버튼). 개발용 미리 보기는 forced 로 고른다 */
std::unique_ptr<WindowChromeView> createWindowChromeView (WindowChromeView::Kind);

} // namespace meon::ui
