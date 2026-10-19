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
            case K::large:
            case K::largeSecondary: return { metric::bigButtonH, 12, metric::radius, 13.0f };
            case K::linkSmall:
            case K::linkUnderline: return { 17, 0, 0.0f, 12.0f };
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

void Button::setConfirm (const juce::String& text, bool accent)
{
    confirm = true;
    armedText = text;
    confirmAccent = accent;
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
    if (onStateChange) onStateChange();
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
        if (onStateChange) onStateChange();
        return;
    }
    if (armed) disarm();
    if (onPress) onPress();
}

void Button::flashText (const juce::String& text, int ms)
{
    if (armed) return;   // 두 번 눌러 확정 버튼과는 같이 쓰지 않는다
    if (! flashing)
    {
        normalText = getButtonText();
        flashing = true;
    }
    startTimer (ms);   // 다시 누르면 처음부터
    if (getButtonText() == text) return;
    setButtonText (text);
    repaint();
    if (onStateChange) onStateChange();
}

void Button::timerCallback()
{
    if (! flashing) { disarm(); return; }
    flashing = false;
    stopTimer();
    setButtonText (normalText);
    repaint();
    if (onStateChange) onStateChange();
}

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
    const bool isLink = kind == Kind::link || kind == Kind::linkSmall || kind == Kind::linkUnderline;
    return (int) std::ceil (textWidth (font (look.weight, m.fontPx), getButtonText())) + m.padX * 2 + (isLink ? 1 : 2);
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
            if (armed) return confirmAccent ? Look { col::accent, col::accent, col::ink, 400 } : Look { col::danger, col::danger, col::white, 400 };
            if (on) return { col::accent, col::accent, col::ink, 400 };
            if (pending) return { col::accentSoft, col::accent, col::ink, 400 };
            if (disabled) return { col::softBg, col::line, col::inkFaint, 400 };
            return { col::softBg, col::line, col::inkSub, 400 };
        case Kind::largeSecondary:
        case Kind::cancel:
            return { col::white, col::field, disabled ? col::inkFaint : col::inkSub, 600 };
        case Kind::link:
        case Kind::linkSmall:
            return { juce::Colours::transparentWhite, juce::Colours::transparentWhite, disabled ? col::inkFaint : col::inkSub, 500 };
        case Kind::linkUnderline:
            return { juce::Colours::transparentWhite, juce::Colours::transparentWhite, disabled ? col::inkFaint : col::ink, 500 };
    }
    return {};
}

void Button::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto m = metricsFor (kind);
    auto look = currentLook();
    const bool disabled = dimmed || ! isEnabled();
    const bool isLink = kind == Kind::link || kind == Kind::linkSmall || kind == Kind::linkUnderline;
    auto r = getLocalBounds().toFloat();
    if (! isLink)
        r = r.withSizeKeepingCentre (r.getWidth(), (float) juce::jmin (m.height, getHeight())); // 칸보다 크게 그리면 위아래가 잘린다

    // 호버 (디자인 style-hover)
    if (highlighted && ! disabled && ! armed)
    {
        switch (kind)
        {
            case Kind::primary:
            case Kind::large:     look.fill = col::accentHover; look.border = col::accentHover; break;
            case Kind::secondary:
            case Kind::quiet:     look.fill = col::hoverBg; break;
            case Kind::largeSecondary:
            case Kind::cancel:    look.fill = col::hoverBg; look.ink = col::ink; break;
            case Kind::linkSmall: look.ink = col::ink; break;
            case Kind::small:     if (! on && ! pending) look.ink = col::ink; break;
            case Kind::link:      look.ink = col::ink; break;
            default: break;
        }
    }

    if (! isLink)
    {
        g.setColour (look.fill);
        g.fillRoundedRectangle (r, m.radius);
        g.setColour (look.border);
        g.drawRoundedRectangle (r.reduced (0.5f), m.radius, 1.0f);
    }

    g.setColour (look.ink);
    g.setFont (font (look.weight, m.fontPx));
    const auto f = font (look.weight, m.fontPx);
    const auto just = isLink ? juce::Justification::centred : juce::Justification::centred;
    g.drawText (getButtonText(), r.toNearestInt(), just, false);
    if (kind == Kind::linkUnderline)
    {
        // text-underline-offset 2px
        const float tw = textWidth (f, getButtonText());
        const float base = r.getCentreY() + f.getAscent() * 0.5f - f.getDescent() * 0.2f + 2.0f;
        g.fillRect (r.getCentreX() - tw * 0.5f, std::round (base), tw, 1.0f);
    }
    juce::ignoreUnused (isSmall);
}

//==============================================================================
DiscordButton::DiscordButton (const juce::String& text, int h) : juce::Button (text), height (h)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void DiscordButton::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    auto r = getLocalBounds().toFloat().withSizeKeepingCentre ((float) getWidth(), (float) height);
    g.setColour (highlighted ? col::discordHover : col::discord);
    g.fillRoundedRectangle (r, metric::radius);
    const auto f = font (600, 13.0f);
    const float logo = height >= 40 ? 18.0f : 16.0f;
    const float tw = textWidth (f, getButtonText());
    const float total = logo + 8.0f + tw + 8.0f + 10.0f;
    float x = r.getCentreX() - total * 0.5f;
    icons::drawDiscord (g, { x, r.getCentreY() - logo * 0.5f, logo, logo }, col::white);
    x += logo + 8.0f;
    g.setColour (col::white);
    g.setFont (f);
    g.drawText (getButtonText(), juce::Rectangle<float> (x, r.getY(), tw + 1.0f, r.getHeight()), juce::Justification::centredLeft, false);
    x += tw + 8.0f;
    icons::draw (g, icons::Id::external, { x, r.getCentreY() - 5.0f, 10.0f, 10.0f }, col::white);
}

//==============================================================================
CollapseButton::CollapseButton (bool side)
    : juce::Button ({}), sideways (side)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setProgress (1.0f);
}

void CollapseButton::setOpen (bool o)
{
    open = o;
    setTooltip (open ? TXT ("접기") : TXT ("펼치기"));
    setProgress (open ? 1.0f : 0.0f);
}

void CollapseButton::setProgress (float e)
{
    const float half = juce::MathConstants<float>::halfPi;
    const float a = -half + (sideways ? 2.0f * half : half) * e;
    if (a == angle) return;
    angle = a;
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
            // top:6 right:7, 8×8, 1.5px 흰 테두리, 알림 색 (합주 최종 디자인)
            juce::Rectangle<float> d (r.getRight() - 7.0f - 8.0f, r.getY() + 6.0f, 8.0f, 8.0f);
            g.setColour (col::notify);
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
