#include "Controls.h"
#include "../theme/Icons.h"

namespace meon::ui
{

//==============================================================================
void AnimatedValue::setTarget (float t, double ms)
{
    if (t == target) return;
    from = value;
    target = t;
    startMs = juce::Time::getMillisecondCounterHiRes();
    durMs = ms;
    if (ms <= 0.0) value = target;
}

bool AnimatedValue::tick (double now)
{
    if (value == target) return false;
    const double p = durMs <= 0.0 ? 1.0 : juce::jlimit (0.0, 1.0, (now - startMs) / durMs);
    // CSS ease 에 가까운 곡선
    const double e = p < 0.5 ? 2.0 * p * p : 1.0 - std::pow (-2.0 * p + 2.0, 2.0) / 2.0;
    value = p >= 1.0 ? target : (float) (from + (target - from) * e);
    return true;
}

//==============================================================================
Toggle::Toggle() : vblank (this, [this] { if (pos.tick (juce::Time::getMillisecondCounterHiRes())) repaint(); })
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void Toggle::setOn (bool b, bool animate)
{
    on = b;
    if (animate) pos.setTarget (on ? 1.0f : 0.0f, 150.0);
    else pos.jumpTo (on ? 1.0f : 0.0f);
    repaint();
}

void Toggle::mouseUp (const juce::MouseEvent& e)
{
    if (! isEnabled() || ! getLocalBounds().contains (e.getPosition())) return;
    setOn (! on);
    if (onChange) onChange (on);
}

void Toggle::paint (juce::Graphics& g)
{
    auto track = juce::Rectangle<float> (26.0f, 16.0f).withCentre (getLocalBounds().toFloat().getCentre());
    const float p = pos.get();
    g.setColour (col::line.interpolatedWith (col::accent, p));
    g.fillRoundedRectangle (track, 8.0f);
    const float left = 2.0f + 10.0f * p;
    g.setColour (col::white);
    g.fillEllipse (track.getX() + left, track.getY() + 2.0f, 12.0f, 12.0f);
}

//==============================================================================
Choice::Choice (Style s, bool m, int w) : style (s), multi (m), fixedW (w)
{
    // 커서는 항목 위에서만 손가락 (mouseMove). 줄이 항목보다 넓게 배치돼도 빈 곳·항목 사이는 기본 커서
}

void Choice::setOptions (const juce::StringArray& v, const juce::StringArray& l)
{
    values = v;
    labels = l.isEmpty() ? v : l;
    repaint();
}

void Choice::setSelected (const juce::String& v)          { selected = { v }; repaint(); }
juce::String Choice::getSelected() const                  { return selected.isEmpty() ? juce::String() : selected[0]; }
void Choice::setSelectedSet (const juce::StringArray& v)  { selected = v; repaint(); }

int Choice::itemWidth (int i) const
{
    if (style == Style::chip && fixedW > 0) return fixedW;
    const bool on = selected.contains (values[i]);
    const auto f = style == Style::chip ? font (on ? 600 : 500, 13.0f) : font (on ? 500 : 400, 12.0f);
    // 굵기가 바뀌어도 폭이 흔들리지 않게 굵은 쪽으로 잰다
    const auto fb = style == Style::chip ? font (600, 13.0f) : font (500, 12.0f);
    juce::ignoreUnused (f);
    if (partIcons)   // 아이콘 자리만큼 넓히고, 한 줄에 들어가게 좌우 여백은 8 로 줄인다
        return (int) std::ceil (textWidth (fb, labels[i]) + partIconPx + partIconGap) + 16 + 2;
    return (int) std::ceil (textWidth (fb, labels[i])) + (style == Style::chip ? 24 : 12) + 2;
}

int Choice::getPreferredWidth() const
{
    int w = 0;
    for (int i = 0; i < values.size(); ++i) w += itemWidth (i) + (i > 0 ? gapPx() : 0);
    return w;
}

juce::Rectangle<float> Choice::itemBounds (int i) const
{
    float x = 0.0f;
    for (int k = 0; k < i; ++k) x += (float) (itemWidth (k) + gapPx());
    const float h = (float) getPreferredHeight();
    return { x, ((float) getHeight() - h) * 0.5f, (float) itemWidth (i), h };
}

void Choice::paint (juce::Graphics& g)
{
    for (int i = 0; i < values.size(); ++i)
    {
        const auto r = itemBounds (i);
        const bool on = selected.contains (values[i]);
        juce::Colour fill, border, ink;
        int weight;
        float px, radius;
        if (style == Style::chip)
        {
            fill = on ? col::accentSoft : col::white;
            border = on ? col::accent : col::borderSubtle;   // 32px 선택 버튼 (방 만들기): 꺼짐·호버 #EBEBEB
            ink = on ? col::ink : col::inkSub;
            weight = on ? 600 : 500;
            px = 13.0f;
            radius = metric::radius;
        }
        else
        {
            fill = on ? col::accentSoft : col::softBg;
            border = on ? col::accent : col::miniBorder;   // 19px 작은 선택 버튼 = 미니 버튼
            ink = on ? col::ink : (hover == i ? col::ink : col::inkSub);
            weight = on ? 500 : 400;
            px = 12.0f;
            radius = metric::smallRadius;
        }
        g.setColour (fill);
        g.fillRoundedRectangle (r, radius);
        g.setColour (border);
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
        g.setColour (ink);
        g.setFont (font (weight, px));
        if (partIcons)
        {
            const auto f = font (weight, px);
            const float tw = textWidth (f, labels[i]);
            const float x = r.getCentreX() - (partIconPx + partIconGap + tw) * 0.5f;
            icons::drawPart (g, values[i], { x, r.getY(), partIconPx, r.getHeight() }, partIconPx, ink, icons::PartWeight::thin);
            g.setColour (ink);
            g.drawText (labels[i], juce::Rectangle<float> (x + partIconPx + partIconGap, r.getY(), tw + 2.0f, r.getHeight()),
                        juce::Justification::centredLeft, false);
            continue;
        }
        g.drawText (labels[i], r, juce::Justification::centred, false);
    }
}

void Choice::mouseMove (const juce::MouseEvent& e)
{
    int h = -1;
    for (int i = 0; i < values.size(); ++i)
        if (itemBounds (i).contains (e.position)) h = i;
    setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    if (h != hover) { hover = h; repaint(); }
}

void Choice::mouseExit (const juce::MouseEvent&)
{
    setMouseCursor (juce::MouseCursor::NormalCursor);
    hover = -1;
    repaint();
}

void Choice::mouseUp (const juce::MouseEvent& e)
{
    if (! isEnabled()) return;
    for (int i = 0; i < values.size(); ++i)
    {
        if (! itemBounds (i).contains (e.position)) continue;
        const auto& v = values[i];
        if (multi)
        {
            if (selected.contains (v)) selected.removeString (v);
            else selected.add (v);
        }
        else
        {
            if (getSelected() == v) return;
            selected = { v };
        }
        repaint();
        if (onChange) onChange();
        return;
    }
}

//==============================================================================
void TipBubble::showAt (juce::Component& anchor, juce::Rectangle<int> area, const juce::String& t)
{
    text = t;
    auto* top = anchor.getTopLevelComponent();
    if (top == nullptr) return;
    if (getParentComponent() != top) top->addAndMakeVisible (this);
    const int w = (int) std::ceil (textWidth (font (500, 12.0f), text)) + 14;
    const int h = 20;
    const auto a = top->getLocalArea (&anchor, area);
    setBounds (a.getCentreX() - w / 2, a.getY() - 6 - h, w, h);
    toFront (false);
    setInterceptsMouseClicks (false, false);
    repaint();
}

void TipBubble::paint (juce::Graphics& g)
{
    g.setColour (col::ink);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
    g.setColour (col::white);
    g.setFont (font (500, 12.0f));
    g.drawText (text, getLocalBounds(), juce::Justification::centred, false);
}

//==============================================================================
DbSlider::DbSlider (float mn, float mx, bool lin) : minV (mn), maxV (mx), linear (lin)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    if (linear) showTip = false;
}

juce::String DbSlider::dbText (float db)
{
    return (db > 0.0f ? "+" : "") + juce::String (db, 1) + " dB";
}

float DbSlider::valueFromFraction (float f) const
{
    f = juce::jlimit (0.0f, 1.0f, f);
    if (linear) return minV + f * (maxV - minV);
    float v = std::round ((minV + f * (maxV - minV)) * 2.0f) / 2.0f;
    if (std::abs (v) <= 1.0f) v = 0.0f;
    return v;
}

void DbSlider::setValue (float v, bool notify)
{
    v = juce::jlimit (minV, maxV, v);
    if (v == value) return;
    value = v;
    repaint();
    updateTip();
    if (notify && onChange) onChange (value);
}

void DbSlider::setMuted (bool m) { if (muted != m) { muted = m; repaint(); } }

void DbSlider::setFromX (float x)
{
    // 디자인: (clientX - left - 9.5) / (width - 19)
    const float f = (x - 9.5f) / juce::jmax (1.0f, (float) getWidth() - 19.0f);
    setValue (valueFromFraction (f), true);
}

void DbSlider::updateTip()
{
    if (! dragging || ! showTip) { tip.reset(); return; }
    if (tip == nullptr) tip = std::make_unique<TipBubble>();
    // 말풍선은 손잡이 위
    const float f = (value - minV) / (maxV - minV);
    const juce::Rectangle<int> knob ((int) std::round (f * ((float) getWidth() - 19.0f)), (getHeight() - 19) / 2, 19, 19);
    static_cast<TipBubble*> (tip.get())->showAt (*this, knob, formatTip ? formatTip (value) : dbText (value));
}

void DbSlider::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled()) return;
    dragging = true;
    setFromX (e.position.x);
    updateTip();
}

void DbSlider::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    setFromX (e.position.x);
}

void DbSlider::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    updateTip();
    if (onDragEnd) onDragEnd();
}

void DbSlider::mouseDoubleClick (const juce::MouseEvent&)
{
    if (linear || ! isEnabled()) return;
    setValue (0.0f, true);
}

void DbSlider::paint (juce::Graphics& g)
{
    const float w = (float) getWidth();
    const float f = (value - minV) / (maxV - minV);
    const float top = ((float) getHeight() - 19.0f) * 0.5f;
    g.setColour (col::line);
    g.fillRoundedRectangle (0.0f, top + 7.5f, w, 4.0f, 2.0f);
    // 채움 폭: f*100% - (f*19 - 9.5)px (손잡이 가운데까지)
    const float fillW = f * w - (f * 19.0f - 9.5f);
    g.setColour (muted || ! isEnabled() ? col::field : col::accent);
    g.fillRoundedRectangle (0.0f, top + 7.5f, juce::jmax (0.0f, fillW), 4.0f, 2.0f);
    const float left = f * w - f * 19.0f;
    auto knob = juce::Rectangle<float> (left, top, 19.0f, 19.0f);
    g.setColour (col::white);
    g.fillEllipse (knob);
    g.setColour (col::field);
    g.drawEllipse (knob.reduced (0.5f), 1.0f);
}

//==============================================================================
PanKnob::PanKnob()
{
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

juce::String PanKnob::panText (int v)
{
    if (v == 0) return "C";
    return v < 0 ? "L " + juce::String (-v) : "R " + juce::String (v);
}

void PanKnob::setValue (int v, bool notify)
{
    v = juce::jlimit (-100, 100, v);
    if (std::abs (v) < 3) v = 0;
    if (v == value) return;
    value = v;
    repaint();
    updateTip();
    if (notify && onChange) onChange (value);
}

void PanKnob::updateTip()
{
    if (! isMouseButtonDown()) { tip.reset(); return; }
    if (tip == nullptr) tip = std::make_unique<TipBubble>();
    static_cast<TipBubble*> (tip.get())->show (*this, panText (value));
}

void PanKnob::mouseDown (const juce::MouseEvent&)  { startValue = value; updateTip(); }

void PanKnob::mouseDrag (const juce::MouseEvent& e)
{
    setValue ((int) std::round ((float) startValue - (float) e.getDistanceFromDragStartY()), true);
    updateTip();
}

void PanKnob::mouseUp (const juce::MouseEvent&)            { tip.reset(); }
void PanKnob::mouseDoubleClick (const juce::MouseEvent&)   { setValue (0, true); }

void PanKnob::paint (juce::Graphics& g)
{
    auto r = juce::Rectangle<float> (19.0f, 19.0f).withCentre (getLocalBounds().toFloat().getCentre());
    g.setColour (col::softBg);
    g.fillEllipse (r);
    g.setColour (col::line);
    g.drawEllipse (r.reduced (0.5f), 1.0f);
    // 지시선: 위 1px 에서 2×5, #AAAAAA, 모서리 1. 값 × 1.35° 회전
    juce::Path p;
    p.addRoundedRectangle (r.getCentreX() - 1.0f, r.getY() + 1.0f, 2.0f, 5.0f, 1.0f);
    g.setColour (col::inkFaint);
    g.fillPath (p, juce::AffineTransform::rotation (juce::degreesToRadians ((float) value * 1.35f), r.getCentreX(), r.getCentreY()));
}

} // namespace meon::ui
