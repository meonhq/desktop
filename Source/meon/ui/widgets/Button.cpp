#include "Button.h"
#include "../theme/Icons.h"

namespace meon::ui
{

namespace
{
    struct Metrics { int height; int padX; float radius; float fontPx; };

    Metrics metricsFor (Button::Kind k)
    {
        using K = Button::Kind;
        switch (k)
        {
            case K::large:         return { metric::bigButtonH, 12, metric::radius, 13.0f };
            case K::small:
            case K::smallDanger:   return { metric::smallH, 6, metric::smallRadius, 12.0f };
            case K::smallPrimary:  return { metric::smallH, 8, metric::smallRadius, 12.0f };
            case K::link:          return { metric::smallH, 0, 0.0f, 13.0f };
            default:               return { metric::buttonH, 12, metric::radius, 13.0f };
        }
    }

    bool isSmall (Button::Kind k)
    {
        return k == Button::Kind::small || k == Button::Kind::smallPrimary || k == Button::Kind::smallDanger;
    }
}

Button::Button (Kind k, const juce::String& text) : juce::Button (text), kind (k), normalText (text)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus (false);
}

Button::~Button()
{
    if (armed) juce::Desktop::getInstance().removeGlobalMouseListener (&outside);
}

void Button::setKind (Kind k)       { kind = k; repaint(); }
void Button::setOn (bool b)         { if (on != b) { on = b; repaint(); } }
void Button::setPending (bool b)    { if (pending != b) { pending = b; repaint(); } }
void Button::setDimmed (bool b)     { if (dimmed != b) { dimmed = b; setMouseCursor (b ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor); repaint(); } }

void Button::setConfirm (const juce::String& text, bool yellow)
{
    confirm = true;
    armedText = text;
    confirmYellow = yellow;
    normalText = getButtonText();
}

void Button::disarm()
{
    if (! armed) return;
    armed = false;
    stopTimer();
    juce::Desktop::getInstance().removeGlobalMouseListener (&outside);
    setButtonText (normalText);
    repaint();
}

void Button::clicked()
{
    if (dimmed) return;   // 디자인: 조건이 안 되면 눌러도 아무 일 없음
    if (confirm && ! armed)
    {
        normalText = getButtonText();
        armed = true;
        setButtonText (armedText);
        startTimer (3000);
        juce::Desktop::getInstance().addGlobalMouseListener (&outside);
        repaint();
        return;
    }
    if (armed) disarm();
    if (onPress) onPress();
}

void Button::timerCallback() { disarm(); }

void Button::Outside::mouseDown (const juce::MouseEvent& e)
{
    if (owner.armed && e.eventComponent != &owner && ! owner.isParentOf (e.eventComponent))
        owner.disarm();
}

int Button::getIdealWidth() const
{
    if (fixedWidth > 0) return fixedWidth;
    const auto m = metricsFor (kind);
    const auto look = currentLook();
    return (int) std::ceil (textWidth (font (look.weight, m.fontPx), getButtonText())) + m.padX * 2 + (kind == Kind::link ? 0 : 2);
}

Button::Look Button::currentLook() const
{
    const bool disabled = dimmed || ! isEnabled();
    switch (kind)
    {
        case Kind::primary:
        case Kind::large:
            if (disabled) return { col::hoverBg, col::line, col::inkFaint, 600 };
            return { col::accent, col::accent, col::ink, 600 };
        case Kind::secondary:
            return { col::white, col::field, disabled ? col::inkFaint : col::ink, 600 };
        case Kind::quiet:
            return { juce::Colours::transparentWhite, col::field, disabled ? col::inkFaint : col::inkSub, 500 };
        case Kind::dangerOutline:
            if (armed) return { col::danger, col::danger, col::white, 600 };
            return { col::white, col::dangerLine, col::danger, 600 };
        case Kind::smallPrimary:
            if (disabled) return { col::hoverBg, col::line, col::inkFaint, 500 };
            return { col::accent, col::accent, col::ink, 500 };
        case Kind::smallDanger:
            if (disabled) return { col::hoverBg, col::line, col::inkFaint, 400 };
            if (armed) return { col::danger, col::danger, col::white, 400 };
            return { col::softBg, col::line, col::danger, 400 };
        case Kind::small:
            if (armed) return confirmYellow ? Look { col::accent, col::accent, col::ink, 400 } : Look { col::danger, col::danger, col::white, 400 };
            if (on) return { col::accent, col::accent, col::ink, 400 };
            if (pending) return { col::accentSoft, col::accent, col::ink, 400 };
            if (disabled) return { col::softBg, col::line, col::inkFaint, 400 };
            return { col::softBg, col::line, col::inkSub, 400 };
        case Kind::link:
            return { juce::Colours::transparentWhite, juce::Colours::transparentWhite, disabled ? col::inkFaint : col::inkSub, 500 };
    }
    return {};
}

void Button::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto m = metricsFor (kind);
    auto look = currentLook();
    const bool disabled = dimmed || ! isEnabled();
    auto r = getLocalBounds().toFloat();
    if (kind != Kind::link)
        r = r.withSizeKeepingCentre (r.getWidth(), (float) m.height);

    // 호버 (디자인 style-hover)
    if (highlighted && ! disabled && ! armed)
    {
        switch (kind)
        {
            case Kind::primary:
            case Kind::large:     look.fill = col::accentHover; look.border = col::accentHover; break;
            case Kind::secondary:
            case Kind::quiet:     look.fill = col::hoverBg; break;
            case Kind::small:     if (! on && ! pending) look.ink = col::ink; break;
            case Kind::link:      look.ink = col::ink; break;
            default: break;
        }
    }

    if (kind != Kind::link)
    {
        g.setColour (look.fill);
        g.fillRoundedRectangle (r, m.radius);
        g.setColour (look.border);
        g.drawRoundedRectangle (r.reduced (0.5f), m.radius, 1.0f);
    }

    g.setColour (look.ink);
    g.setFont (font (look.weight, m.fontPx));
    const auto just = kind == Kind::link ? juce::Justification::centredLeft : juce::Justification::centred;
    g.drawText (getButtonText(), r.toNearestInt(), just, false);
    juce::ignoreUnused (isSmall);
}

//==============================================================================
CollapseButton::CollapseButton (bool side)
    : juce::Button ({}), sideways (side),
      vblank (this, [this]
      {
          if (std::abs (angle - target) < 0.001f) return;
          // 0.15s 회전 (VBlank 마다 남은 각의 일정 비율)
          angle += (target - angle) * 0.35f;
          if (std::abs (angle - target) < 0.01f) angle = target;
          repaint();
      })
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    target = angle = sideways ? juce::MathConstants<float>::halfPi : 0.0f;
}

void CollapseButton::setOpen (bool o)
{
    open = o;
    const float half = juce::MathConstants<float>::halfPi;
    target = open ? (sideways ? half : 0.0f) : -half;
    setTooltip (open ? TXT ("접기") : TXT ("펼치기"));
    repaint();
}

void CollapseButton::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    auto r = getLocalBounds().toFloat().withSizeKeepingCentre (19.0f, 19.0f);
    if (highlighted)
    {
        g.setColour (col::hoverBg);
        g.fillRoundedRectangle (r, 4.0f);
    }
    icons::draw (g, icons::Id::chevronDown, r.withSizeKeepingCentre (12.0f, 12.0f), highlighted ? col::ink : col::inkSub, angle);
}

//==============================================================================
IconButton::IconButton (int id, Style s, float px)
    : juce::Button ({}), icon (id), style (s), iconPx (px)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    if (style == Style::bordered32) { ink = col::inkSub; inkHover = col::ink; }
    else { ink = col::inkFaint; inkHover = col::ink; }
}

void IconButton::setDot (bool d) { if (dot != d) { dot = d; repaint(); } }

void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    auto b = getLocalBounds().toFloat();
    if (style == Style::bordered32)
    {
        auto r = b.withSizeKeepingCentre (32.0f, 32.0f);
        g.setColour (highlighted ? col::hoverBg : col::white);
        g.fillRoundedRectangle (r, metric::radius);
        g.setColour (col::field);
        g.drawRoundedRectangle (r.reduced (0.5f), metric::radius, 1.0f);
        icons::draw (g, (icons::Id) icon, r.withSizeKeepingCentre (iconPx, iconPx), highlighted ? inkHover : ink);
        if (dot)
        {
            // top:6 right:7, 7×7, 1.5px 흰 테두리, 검정 점
            juce::Rectangle<float> d (r.getRight() - 7.0f - 7.0f, r.getY() + 6.0f, 7.0f, 7.0f);
            g.setColour (col::ink);
            g.fillEllipse (d);
            g.setColour (col::white);
            g.drawEllipse (d.reduced (0.75f), 1.5f);
        }
        return;
    }
    const float side = style == Style::plain24 ? 24.0f : 19.0f;
    auto r = b.withSizeKeepingCentre (side, side);
    if (highlighted && style == Style::plain24)
    {
        g.setColour (col::hoverBg);
        g.fillRoundedRectangle (r, 5.0f);
    }
    icons::draw (g, (icons::Id) icon, r.withSizeKeepingCentre (iconPx, iconPx), highlighted ? inkHover : ink);
}

} // namespace meon::ui
