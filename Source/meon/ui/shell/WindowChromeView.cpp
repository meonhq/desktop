// MEON ui - 창 윗부분 (운영체제별 구성 요소). 설명은 WindowChromeView.h
#include "WindowChromeView.h"
#include "../theme/Tokens.h"
#include "../theme/Hairline.h"

namespace meon::ui
{

//==============================================================================
MacTrafficLights::MacTrafficLights()
{
    setInterceptsMouseClicks (false, false);   // 신호등 누름은 macOS 가 받는다 (이 자리는 비워 둘 뿐)
}

void MacTrafficLights::place (juce::Rectangle<int> editor)
{
    setBounds (editor.getX(), editor.getY(), mockRight + 12, 52);
}

void MacTrafficLights::paint (juce::Graphics& g)
{
    if (mock) drawMock (g);
}

void MacTrafficLights::drawMock (juce::Graphics& g)
{
    // 52px 윗줄의 세로 가운데. 자리는 macOS 가 정한다 (빈 툴바 + unified): macOS 26 러너에서 잰 버튼 틀 (mac #224)
    const juce::uint32 fills[] = { 0xFFFF5F57, 0xFFFEBC2E, 0xFF28C840 }, rims[] = { 0xFFE2463F, 0xFFE1A116, 0xFF1AAB29 };
    for (int i = 0; i < 3; ++i)
    {
        const juce::Rectangle<float> c (19.0f + 23.0f * (float) i, 19.0f, 14.0f, 14.0f);
        g.setColour (juce::Colour (fills[i]));
        g.fillEllipse (c);
        g.setColour (juce::Colour (rims[i]));
        g.drawEllipse (c.reduced (0.25f), 0.5f);
    }
}

//==============================================================================
namespace
{
    // 디자인 v2.1 (os = 윈도우): 최소화·최대화 마우스 올림 #F5F5F5, 누름 #EBEBEB. 닫기 올림 #C42B1C, 누름 #A82418 (아이콘 흰색)
    const juce::Colour hoverFill   { 0xFFF5F5F5u };
    const juce::Colour pressFill   { 0xFFEBEBEBu };
    const juce::Colour closeHover  { 0xFFC42B1Cu };
    const juce::Colour closePress  { 0xFFA82418u };
    const juce::Colour inkActive   { 0xFF1A1A1Au };
    const juce::Colour inkInactive { 0xFFAAAAAAu };
}

WinCaptionButtons::WinCaptionButtons()
{
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void WinCaptionButtons::place (juce::Rectangle<int> editor)
{
    setBounds (editor.getRight() - width, editor.getY(), width, buttonH);
}

void WinCaptionButtons::setWindowState (bool m, bool a)
{
    if (m == maximised && a == active) return;
    maximised = m;
    active = a;
    repaint();
}

void WinCaptionButtons::setMaxButtonState (int s)
{
    if (s == ncMaxState) return;
    ncMaxState = s;
    repaint (buttonBounds (maximise));
}

WinCaptionButtons::Which WinCaptionButtons::buttonAt (juce::Point<int> p) const
{
    if (! getLocalBounds().contains (p)) return none;
    return (Which) juce::jlimit (0, 2, p.x / buttonW);
}

juce::Colour WinCaptionButtons::fillFor (Which w) const
{
    int state = 0;   // 0 보통, 1 올림, 2 누름
    if (w == maximise && ncMaxState > 0) state = ncMaxState;
    else if (pressed == w) state = hover == w ? 2 : 0;
    else if (pressed == none && hover == w) state = 1;
    if (state == 0) return juce::Colours::transparentWhite;
    if (w == closeButton) return state == 2 ? closePress : closeHover;
    return state == 2 ? pressFill : hoverFill;
}

juce::Colour WinCaptionButtons::devFill (Which w) const { return fillFor (w); }

juce::String WinCaptionButtons::getTooltip()
{
    switch (hover)
    {
        case minimise:    return TXT ("최소화");
        case maximise:    return maximised ? TXT ("이전 크기로") : TXT ("최대화");
        case closeButton: return TXT ("닫기");
        case none:        break;
    }
    return {};
}

void WinCaptionButtons::paint (juce::Graphics& g)
{
    for (auto w : { minimise, maximise, closeButton })
    {
        const auto b = buttonBounds (w);
        const auto fill = fillFor (w);
        if (! fill.isTransparent())
        {
            g.setColour (fill);
            g.fillRect (b);
        }
        const bool whiteIcon = w == closeButton && ! fill.isTransparent();
        const auto ink = whiteIcon ? juce::Colours::white : (active ? inkActive : inkInactive);
        // 아이콘 10×10 을 버튼 가운데에: (46 - 10) / 2 = 18, (51 - 10) / 2 = 20.5 → 21 (브라우저 배치처럼 정수 px 로. 200% 에서 디자인과 같은 줄)
        drawIcon (g, w, juce::Rectangle<float> ((float) b.getX() + (float) ((buttonW - 10) / 2), std::ceil ((float) (buttonH - 10) * 0.5f), 10.0f, 10.0f), ink);
    }
}

void WinCaptionButtons::drawIcon (juce::Graphics& g, Which w, juce::Rectangle<float> box, juce::Colour ink) const
{
    // 선 1px 을 물리 픽셀에 맞춘다 (100% 1줄, 125·150% 1줄, 200% 2줄): 디자인 SVG stroke-width 1
    const auto grid = hairline::gridFor (g, *this);
    const float t = grid.thickness();
    const float x0 = grid.snapX (box.getX()), y0 = grid.snapY (box.getY());
    auto cx = [&] (float v) { return grid.snapX (x0 + v) + t * 0.5f; };   // 디자인 좌표 v + 0.5 (그 열의 가운데)
    auto cy = [&] (float v) { return grid.snapY (y0 + v) + t * 0.5f; };
    g.setColour (ink);
    switch (w)
    {
        case minimise:   // M0 5.5h10
            hairline::horizontal (g, *this, x0, y0 + 5.0f, 10.0f);
            break;
        case maximise:
            if (! maximised)   // rect 0.5,0.5 9×9 rx 1
            {
                hairline::roundedBorder (g, *this, { x0, y0, 10.0f, 10.0f }, 1.0f);
            }
            else   // 이전 크기로: 앞 사각형 rect 0.5,2.5 7×7 rx 1 + 뒤 사각형의 보이는 부분
            {
                hairline::roundedBorder (g, *this, { x0, y0 + 2.0f, 8.0f, 8.0f }, 1.0f);
                juce::Path p;   // M2.5 2.5 v-1 a1 1 0 0 1 1-1 h5 a1 1 0 0 1 1 1 v5 a1 1 0 0 1-1 1 h-1
                p.startNewSubPath (cx (2.0f), cy (2.0f));
                p.lineTo (cx (2.0f), cy (1.0f));
                p.quadraticTo (cx (2.0f), cy (0.0f), cx (3.0f), cy (0.0f));
                p.lineTo (cx (8.0f), cy (0.0f));
                p.quadraticTo (cx (9.0f), cy (0.0f), cx (9.0f), cy (1.0f));
                p.lineTo (cx (9.0f), cy (6.0f));
                p.quadraticTo (cx (9.0f), cy (7.0f), cx (8.0f), cy (7.0f));
                p.lineTo (cx (7.0f), cy (7.0f));
                g.strokePath (p, juce::PathStrokeType (t));
            }
            break;
        case closeButton:   // M0.5 0.5 l9 9 M9.5 0.5 l-9 9
        {
            juce::Path p;
            p.startNewSubPath (cx (0.0f), cy (0.0f));
            p.lineTo (cx (9.0f), cy (9.0f));
            p.startNewSubPath (cx (9.0f), cy (0.0f));
            p.lineTo (cx (0.0f), cy (9.0f));
            g.strokePath (p, juce::PathStrokeType (t));
            break;
        }
        case none:
            break;
    }
}

void WinCaptionButtons::mouseMove (const juce::MouseEvent& e)
{
    const auto h = buttonAt (e.getPosition());
    if (h != hover) { hover = h; repaint(); }
}

void WinCaptionButtons::mouseEnter (const juce::MouseEvent& e) { mouseMove (e); }

void WinCaptionButtons::mouseExit (const juce::MouseEvent&)
{
    if (hover != none) { hover = none; repaint(); }
}

void WinCaptionButtons::mouseDown (const juce::MouseEvent& e)
{
    pressed = buttonAt (e.getPosition());
    hover = pressed;
    repaint();
}

void WinCaptionButtons::mouseDrag (const juce::MouseEvent& e)
{
    const auto h = buttonAt (e.getPosition());
    if (h != hover) { hover = h; repaint(); }
}

void WinCaptionButtons::mouseUp (const juce::MouseEvent& e)
{
    const auto was = pressed;
    pressed = none;
    hover = buttonAt (e.getPosition());
    repaint();
    if (was == none || hover != was) return;
    // 운영체제 창 버튼과 같은 길 (WM_SYSCOMMAND). 창이 바뀌는 중에 이 구성 요소가 다시 그려질 수 있어 다음 메시지에서
    std::function<void()> act = was == minimise ? onMinimise : was == maximise ? onToggleMaximise : onClose;
    if (act) juce::MessageManager::callAsync (act);
}

//==============================================================================
std::unique_ptr<WindowChromeView> createWindowChromeView (WindowChromeView::Kind k)
{
    if (k == WindowChromeView::Kind::windows) return std::make_unique<WinCaptionButtons>();
    return std::make_unique<MacTrafficLights>();
}

} // namespace meon::ui
