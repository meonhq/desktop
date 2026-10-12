#include "MeonWidgets.h"
#include "MeonKeyRepeat.h"
#include "MeonEmoji.h"

namespace meon
{

//==============================================================================
float Badge::width (const juce::String& text, float fontPx, float minWidth, float padX)
{
    auto font = Fonts::get (600, fontPx);
    return juce::jmax (minWidth, font.getStringWidthFloat (text) + padX * 2.0f);
}

void Badge::draw (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text,
                  juce::Colour border, juce::Colour bg, juce::Colour ink, float fontPx)
{
    g.setColour (bg);
    g.fillRoundedRectangle (area, 3.0f);
    g.setColour (border);
    g.drawRoundedRectangle (area.reduced (0.5f), 3.0f, 1.0f);
    g.setColour (ink);
    g.setFont (Fonts::get (600, fontPx));
    g.drawText (text, area, juce::Justification::centred, false);
}

//==============================================================================
MeonButton::MeonButton (const juce::String& labelIn, Style styleIn)
    : juce::Button (labelIn), style (styleIn), label (labelIn)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus (false);
}

void MeonButton::setStyle (Style s)                  { style = s; repaint(); }
void MeonButton::setLabel (const juce::String& t)    { label = t; setButtonText (t); repaint(); }
void MeonButton::setSubtitle (const juce::String& t) { subtitle = t; repaint(); }
void MeonButton::setBadge (const juce::String& t)    { badge = t; repaint(); }
void MeonButton::setFont (float px, int weight)      { fontPx = px; fontWeight = weight; repaint(); }
void MeonButton::setSubtitleFont (float px, int w)   { subPx = px; subWeight = w; repaint(); }
void MeonButton::setSubtitleInk (juce::Colour c)     { subInkOverride = c; repaint(); }
void MeonButton::setBadgeMetrics (float h, float px, float gap) { badgeH = h; badgePx = px; badgeGap = gap; repaint(); }
void MeonButton::setCornerRadius (float r)           { radius = r; repaint(); }
void MeonButton::setLeftAligned (bool l, int pad)    { leftAlign = l; leftPad = pad; repaint(); }

void MeonButton::setColourOverride (juce::Colour bg, juce::Colour ink, juce::Colour border, juce::Colour hoverBg)
{
    hasOverride = true; oBg = bg; oInk = ink; oBorder = border; oHover = hoverBg; repaint();
}

void MeonButton::clearColourOverride()
{
    hasOverride = false; repaint();
}

int MeonButton::getIdealWidth (int horizontalPadding) const
{
    auto font = Fonts::get (fontWeight, fontPx);
    float w = font.getStringWidthFloat (label);
    if (badge.isNotEmpty())
        w += badgeGap + Badge::width (badge, badgePx);
    return (int) std::ceil (w) + horizontalPadding * 2;
}

void MeonButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat();
    const bool enabled = isEnabled();
    const bool hot = enabled && (over || down);

    juce::Colour bg, ink, border, badgeBorder, badgeBg, badgeInk, subInk;

    switch (style)
    {
        case Style::Primary:
            bg = ! enabled ? col::disabled : (hot ? col::accentDown : col::accent);
            ink = enabled ? col::onAccent : col::white; border = juce::Colours::transparentBlack;
            badgeBorder = ink; badgeBg = bg; badgeInk = ink;
            subInk = ink;
            break;
        case Style::Dark:
            bg = ! enabled ? col::disabled : col::ink;
            ink = col::white; border = juce::Colours::transparentBlack;
            badgeBorder = col::white; badgeBg = bg; badgeInk = col::white;
            subInk = col::white;
            break;
        case Style::Secondary:
        default:
            bg = hot ? col::panel : col::white;
            ink = enabled ? col::ink : col::disabled;
            border = enabled ? col::border : col::cardBorder;
            badgeBorder = col::cardBorder; badgeBg = col::badgeBg; badgeInk = col::inkSub;
            subInk = col::inkSub;
            break;
    }

    if (hasOverride)
    {
        bg = (hot && ! oHover.isTransparent()) ? oHover : oBg;
        ink = oInk; border = oBorder;
        if (style != Style::Secondary) { badgeBorder = ink; badgeBg = bg; badgeInk = ink; }
    }

    g.setColour (bg);
    g.fillRoundedRectangle (r, radius);
    if (! border.isTransparent())
    {
        g.setColour (border);
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
    }

    auto font = Fonts::get (fontWeight, fontPx);
    const float labelW = font.getStringWidthFloat (label);
    const float badgeW = badge.isNotEmpty() ? Badge::width (badge, badgePx) : 0.0f;
    const float totalW = labelW + (badge.isNotEmpty() ? badgeGap + badgeW : 0.0f);

    if (subtitle.isEmpty())
    {
        float x = leftAlign ? r.getX() + (float) leftPad : r.getCentreX() - totalW * 0.5f;
        g.setColour (ink);
        g.setFont (font);
        g.drawText (label, juce::Rectangle<float> (x, r.getY(), labelW + 4.0f, r.getHeight()), juce::Justification::centredLeft, false);
        if (badge.isNotEmpty())
            Badge::draw (g, { x + labelW + badgeGap, r.getCentreY() - badgeH * 0.5f, badgeW, badgeH }, badge, badgeBorder, badgeBg, badgeInk, badgePx);
    }
    else
    {
        auto subFont = Fonts::get (subWeight, subPx);
        const float h1 = font.getHeight(), h2 = subFont.getHeight(), gap = 4.0f;
        const float totalH = h1 + gap + h2;
        const float y = r.getCentreY() - totalH * 0.5f;
        g.setColour (ink);
        g.setFont (font);
        g.drawText (label, r.withY (y).withHeight (h1), juce::Justification::centred, false);
        g.setColour (subInkOverride.isTransparent() ? subInk : subInkOverride);
        g.setFont (subFont);
        g.drawText (subtitle, r.withY (y + h1 + gap).withHeight (h2), juce::Justification::centred, false);
    }
}

//==============================================================================
MeonTextEditor::MeonTextEditor (float fontPx, int weight)
{
    setFont (Fonts::forInput (weight, fontPx));
    setIndents (14, 0);
    setJustification (juce::Justification::centredLeft);
    setSelectAllWhenFocused (false);
    setScrollbarsShown (false);
    setPopupMenuEnabled (false);
    setColour (juce::TextEditor::backgroundColourId, col::white);
    setColour (juce::TextEditor::textColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::highlightedTextColourId, juce::Colours::transparentBlack);
}

void MeonTextEditor::paintOverChildren (juce::Graphics& g)
{
    const auto text = getText();
    const int n = text.length();
    if (n > 0)
    {
        // 한 줄 입력창만 쓴다. 커서·선택 영역과 어긋나지 않도록 글자마다 JUCE 가 정한 x 위치에 그린다.
        // 이모지는 JUCE 가 준 칸에 맞춰 그린다 (macOS 는 CoreText 로 줄여서, Windows 는 컬러 그림으로).
        const auto font = getFont();
        const auto line = getTextBounds ({ 0, n }).getBounds();
        const float baseline = (float) line.getY() + (float) juce::roundToInt (font.getAscent());
        auto xAt = [this] (int index) { return (float) getCaretRectangleForCharIndex (index).getX(); };

        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (getBorder().subtractedFrom (getLocalBounds()));
        g.setColour (col::ink);

        for (int i = 0; i < n;)
        {
            if (const int end = emoji::clusterEnd (text, i); end > i)
            {
                const auto cluster = text.substring (i, end);
                const float x0 = xAt (i), slot = xAt (end) - x0;

                if (slot > 0.0f)
                {
                    if constexpr (emoji::nativeLayout)
                    {
                        const float natural = lineWidthWithEmoji (cluster, font);
                        const auto f = natural > 0.0f ? font.withHeight (font.getHeight() * juce::jlimit (0.5f, 1.5f, slot / natural)) : font;

                        juce::AttributedString as;
                        as.setWordWrap (juce::AttributedString::none);
                        as.append (cluster, f, col::ink);
                        juce::TextLayout layout;
                        layout.createLayout (as, 1.0e6f);
                        const float firstBaseline = layout.getNumLines() > 0 ? layout.getLine (0).lineOrigin.y : f.getAscent();
                        as.draw (g, { x0, baseline - firstBaseline, slot + f.getHeight(), f.getHeight() * 3.0f });
                    }
                    else
                    {
                        emoji::draw (g, cluster, font.getHeightInPoints(), x0, slot, baseline);
                    }
                }
                i = end;
                continue;
            }

            int j = i + 1;
            while (j < n && emoji::clusterEnd (text, j) == j)
                ++j;

            juce::GlyphArrangement ga;
            ga.addLineOfText (font, text.substring (i, j), xAt (i), baseline);
            ga.draw (g);
            i = j;
        }
    }

    juce::TextEditor::paintOverChildren (g);
}

bool MeonTextEditor::keyPressed (const juce::KeyPress& k)
{
    KeyRepeat::noteKeyEvent (*this);
    return juce::TextEditor::keyPressed (k);
}

void MeonTextEditor::insertTextAtCaret (const juce::String& t)
{
    KeyRepeat::noteKeyEvent (*this);   // 글자·한글 조합은 keyPressed 를 거치지 않고 입력기에서 바로 온다
    juce::TextEditor::insertTextAtCaret (t);
}

bool MeonTextEditor::keyStateChanged (bool isKeyDown)
{
    if (! isKeyDown)
        KeyRepeat::stop();
    return juce::TextEditor::keyStateChanged (isKeyDown);
}

void MeonTextEditor::focusLost (FocusChangeType cause)
{
    KeyRepeat::stop();
    juce::TextEditor::focusLost (cause);
}

void MeonTextEditor::setPlaceholder (const juce::String& text)
{
    setTextToShowWhenEmpty (text, col::disabled);
}

//==============================================================================
LevelBar::LevelBar()
{
    setInterceptsMouseClicks (false, false);
}

float LevelBar::dbToPosition (float db)
{
    // 디자인 눈금(-60, -40, -20, -12, -6, 0)이 균등 간격으로 놓이도록 구간별 선형 보간
    static const float anchorsDb[]  = { -60.0f, -40.0f, -20.0f, -12.0f, -6.0f, 0.0f };
    static const float anchorsPos[] = {   0.0f,   0.2f,   0.4f,   0.6f,  0.8f, 1.0f };
    if (db <= anchorsDb[0]) return 0.0f;
    if (db >= 0.0f) return 1.0f;
    for (int i = 1; i < 6; ++i)
        if (db <= anchorsDb[i])
        {
            const float t = (db - anchorsDb[i - 1]) / (anchorsDb[i] - anchorsDb[i - 1]);
            return anchorsPos[i - 1] + t * (anchorsPos[i] - anchorsPos[i - 1]);
        }
    return 1.0f;
}

void LevelBar::push (float db, double dtSeconds)
{
    // DAW(Logic 등) 피크 미터 방식: 오를 때는 즉시, 내릴 때는 일정한 속도(초당 30 dB)로 미끄러지듯.
    // 프레임마다(VBlank) 호출되면 하강이 끊김 없이 이어진다.
    const float fallDbPerSec = 30.0f;
    db = juce::jmax (db, -100.0f);
    const float prev = shownDb;
    if (db >= shownDb)
        shownDb = db;
    else
        shownDb = juce::jmax (db, juce::jmin (shownDb, 0.0f) - (float) dtSeconds * fallDbPerSec);
    if (shownDb < -70.0f)   // 화면 밖(-60 아래)은 바로 바닥으로
        shownDb = db;

    const double now = juce::Time::getMillisecondCounterHiRes();
    if (db >= -0.1f)
        clipUntilMs = now + 1500.0;
    const bool clip = now < clipUntilMs;

    // 화면상 0.1px 이상 움직였거나 클립 표시가 바뀔 때만 다시 그린다 (조용할 때 매 프레임 그리지 않게)
    if (std::abs (dbToPosition (shownDb) - dbToPosition (prev)) * (float) getWidth() > 0.1f || clip != clipShown)
        repaint();
    clipShown = clip;
}

void LevelBar::reset()
{
    shownDb = -100.0f;
    clipUntilMs = 0.0;
    clipShown = false;
    repaint();
}

void LevelBar::setDisabledLook (bool disabled)
{
    disabledLook = disabled;
    repaint();
}

void LevelBar::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::meterTrack);
    g.fillRoundedRectangle (r, radius);

    const float pos = disabledLook ? 0.0f : dbToPosition (shownDb);
    if (pos > 0.0f)
    {
        g.setColour (disabledLook ? col::border : col::accent);
        g.fillRoundedRectangle (r.withWidth (juce::jmax (radius * 2.0f, r.getWidth() * pos)), radius);
    }

    if (! disabledLook && juce::Time::getMillisecondCounterHiRes() < clipUntilMs)
    {
        juce::Path track;   // 둥근 모서리 밖으로 나가지 않게 (CSS 의 overflow: hidden)
        track.addRoundedRectangle (r, radius);
        g.reduceClipRegion (track);
        g.setColour (col::ink);
        g.fillRect (r.removeFromRight (r.getWidth() * 0.06f));
    }
}

//==============================================================================
VolumeSlider::VolumeSlider()
{
    setSliderStyle (juce::Slider::LinearHorizontal);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setNormalisableRange (juce::NormalisableRange<double> (0.0, 2.0, 0.0, 0.69));
    setValue (1.0, juce::dontSendNotification);
    setDoubleClickReturnValue (true, 1.0);
    setScrollWheelEnabled (false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus (false);
}

juce::String VolumeSlider::dbText (double gain)
{
    if (gain <= 0.001)
        return "-60 dB";
    double db = juce::Decibels::gainToDecibels (gain);
    db = juce::jlimit (-60.0, 6.0, db);
    return (db > 0.05 ? "+" : "") + juce::String (db, 1) + " dB";
}

//==============================================================================
PanKnob::PanKnob (int diameter)
{
    setSliderStyle (juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setRotaryParameters (juce::degreesToRadians (-135.0f), juce::degreesToRadians (135.0f), true);
    setRange (-100.0, 100.0, 1.0);
    setValue (0.0, juce::dontSendNotification);
    setDoubleClickReturnValue (true, 0.0);
    setMouseDragSensitivity (200);
    setWantsKeyboardFocus (false);
    getProperties().set ("meonKnob", diameter);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

// 드래그는 직접 처리한다 (1px = 1, Shift 미세 조절, 0 근처 스냅)
void PanKnob::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled())
        return;
    dragging = true;
    dragRaw = getValue();
    lastY = e.position.y;
}

void PanKnob::mouseDrag (const juce::MouseEvent& e)
{
    if (! isEnabled() || ! dragging)
        return;
    const float dy = lastY - e.position.y;   // 위로 올리면 증가
    lastY = e.position.y;
    dragRaw = juce::jlimit (-100.0, 100.0, dragRaw + dy * (e.mods.isShiftDown() ? 0.25 : 1.0));
    int v = (int) std::lround (dragRaw);
    if (std::abs (v) < 3)
        v = 0;
    setValue ((double) v, juce::sendNotificationSync);
}

void PanKnob::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
}

void PanKnob::mouseDoubleClick (const juce::MouseEvent&)
{
    if (isEnabled())
        setValue (0.0, juce::sendNotificationSync);
}

void PanKnob::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (! isEnabled() || dragging)
        return;
    const float d = (std::abs (wheel.deltaY) >= std::abs (wheel.deltaX) ? wheel.deltaY : -wheel.deltaX) * (wheel.isReversed ? -1.0f : 1.0f);
    int steps = 0;
    if (wheel.isSmooth)
    {
        // 트랙패드: 조금씩 모아서 한 칸씩
        wheelAcc += d;
        const float unit = 0.05f;
        steps = (int) (wheelAcc / unit);
        wheelAcc -= (float) steps * unit;
    }
    else if (d != 0.0f)
    {
        steps = d > 0.0f ? 1 : -1;
    }
    if (steps != 0)
        setValue (juce::jlimit (-100.0, 100.0, getValue() + steps), juce::sendNotificationSync);
}

void PanKnob::enablementChanged()
{
    setMouseCursor (isEnabled() ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
    juce::Slider::enablementChanged();
    repaint();
}

//==============================================================================
MeonCheckbox::MeonCheckbox() : juce::Button ("checkbox")
{
    setClickingTogglesState (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus (false);
}

void MeonCheckbox::paintButton (juce::Graphics& g, bool, bool)
{
    auto r = getLocalBounds().toFloat();
    const bool on = getToggleState();
    g.setColour (on ? col::accent : col::white);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (on ? col::accent : col::border);
    g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);

    if (on)
    {
        juce::Path p;
        const float w = r.getWidth();
        p.startNewSubPath (r.getX() + w * 0.26f, r.getY() + w * 0.52f);
        p.lineTo (r.getX() + w * 0.44f, r.getY() + w * 0.70f);
        p.lineTo (r.getX() + w * 0.76f, r.getY() + w * 0.32f);
        g.setColour (col::onAccent);
        g.strokePath (p, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

//==============================================================================
WarningBox::WarningBox()
{
    setInterceptsMouseClicks (false, true);
}

void WarningBox::setTitle (const juce::String& t) { title = t; repaint(); }

void WarningBox::setLines (const juce::StringArray& l, float lineHeight)
{
    lines = l;
    lineHeightMul = lineHeight;
    repaint();
}

float WarningBox::bodyHeight (int width) const
{
    auto bodyFont = Fonts::get (400, 14.0f);
    const float lineH = 14.0f * lineHeightMul;
    const float textW = (float) (width - getTextLeft() - padX);
    float h = 0.0f;
    for (auto& line : lines)
        h += paragraphHeight (line, bodyFont, textW, lineH);
    return h;
}

int WarningBox::getButtonsTop() const
{
    auto titleFont = Fonts::get (600, 16.0f);
    float y = (float) padY + titleFont.getHeight() + 6.0f + bodyHeight (getWidth());
    return (int) std::ceil (y) + 6;
}

int WarningBox::getPreferredHeight (int width) const
{
    auto titleFont = Fonts::get (600, 16.0f);
    float h = (float) padY + titleFont.getHeight() + 6.0f + bodyHeight (width);
    if (buttonsH > 0)
        h += 6.0f + (float) buttonsH;
    return (int) std::ceil (h) + padY;
}

void WarningBox::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::warnBg);
    g.fillRoundedRectangle (r, (float) metric::radiusCard);
    g.setColour (col::warnBorder);
    g.drawRoundedRectangle (r.reduced (0.5f), (float) metric::radiusCard, 1.0f);

    // 아이콘 "!"
    juce::Rectangle<float> icon ((float) padX, (float) padY, (float) iconSize, (float) iconSize);
    g.setColour (col::accent);
    g.fillRoundedRectangle (icon, 4.0f);
    g.setColour (col::onAccent);
    g.setFont (Fonts::get (700, 14.0f));
    g.drawText ("!", icon, juce::Justification::centred, false);

    auto titleFont = Fonts::get (600, 16.0f);
    const float x = (float) getTextLeft();
    const float textW = (float) getWidth() - x - (float) padX;
    float y = (float) padY;
    g.setColour (col::ink);
    g.setFont (titleFont);
    g.drawText (title, juce::Rectangle<float> (x, y, textW, titleFont.getHeight()), juce::Justification::centredLeft, false);
    y += titleFont.getHeight() + 6.0f;

    auto bodyFont = Fonts::get (400, 14.0f);
    const float lineH = 14.0f * lineHeightMul;
    for (auto& line : lines)
    {
        float h = paragraphHeight (line, bodyFont, textW, lineH);
        drawParagraph (g, line, bodyFont, col::inkSub, juce::Rectangle<float> (x, y, textW, h + 2.0f), lineH);
        y += h;
    }
}

//==============================================================================
CodeInput::CodeInput (int w, int h, int g, float px) : boxW (w), boxH (h), gap (g), fontPx (px)
{
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::IBeamCursor);
}

bool CodeInput::isAllowedChar (juce::juce_wchar c)
{
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

void CodeInput::setErrorState (bool isError)
{
    error = isError;
    repaint();
}

void CodeInput::clear()
{
    code.clear();
    error = false;
    repaint();
    if (onChanged) onChanged();
}

void CodeInput::mouseDown (const juce::MouseEvent&)
{
    grabKeyboardFocus();
}

bool CodeInput::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::backspaceKey || k == juce::KeyPress::deleteKey)
    {
        if (code.isNotEmpty())
            code = code.dropLastCharacters (1);
        error = false;
        repaint();
        if (onChanged) onChanged();
        return true;
    }

    if (k == juce::KeyPress::returnKey)
    {
        if (onSubmit) onSubmit();
        return true;
    }

    if (k.getModifiers().isCommandDown())
    {
        // 한글 자판에서는 ⌘V 의 문자가 'ㅍ' 로 들어오므로 자판과 무관한 키 코드로 판별한다
        if (juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) k.getKeyCode()) == 'V')
        {
            auto clip = juce::SystemClipboard::getTextFromClipboard().toUpperCase();
            for (int i = 0; i < clip.length() && code.length() < 6; ++i)
                if (isAllowedChar (clip[i]))
                    code += juce::String::charToString (clip[i]);
            error = false;
            repaint();
            if (onChanged) onChanged();
            return true;
        }
        return false;
    }

    const auto ch = k.getTextCharacter();
    if (ch != 0 && code.length() < 6)
    {
        const auto up = juce::CharacterFunctions::toUpperCase (ch);
        if (isAllowedChar (up))
        {
            code += juce::String::charToString (up);
            error = false;
            repaint();
            if (onChanged) onChanged();
            return true;
        }
    }
    return false;
}

void CodeInput::paint (juce::Graphics& g)
{
    const bool focused = hasKeyboardFocus (true);
    auto font = Fonts::get (700, fontPx);
    for (int i = 0; i < 6; ++i)
    {
        juce::Rectangle<float> box ((float) (i * (boxW + gap)), 0.0f, (float) boxW, (float) boxH);
        const bool cursor = ! error && focused && i == code.length();
        g.setColour (col::white);
        g.fillRoundedRectangle (box, (float) metric::radiusButton);
        g.setColour (error ? col::accent : (cursor ? col::accent : col::border));
        g.drawRoundedRectangle (box.reduced (0.5f), (float) metric::radiusButton, 1.0f);

        if (i < code.length())
        {
            g.setColour (col::ink);
            g.setFont (font);
            g.drawText (code.substring (i, i + 1), box, juce::Justification::centred, false);
        }
        else if (cursor)
        {
            g.setColour (col::accent);
            g.fillRect (juce::Rectangle<float> (box.getCentreX() - 0.5f, box.getY() + box.getHeight() * 0.28f, 1.5f, box.getHeight() * 0.44f));
        }
    }
}

//==============================================================================
ProgressSteps::ProgressSteps (int t, int c, int barWidth) : total (t), current (c), barW (barWidth)
{
    setInterceptsMouseClicks (false, false);
}

void ProgressSteps::setCurrent (int c)
{
    current = c;
    repaint();
}

int ProgressSteps::getPreferredWidth() const
{
    auto font = Fonts::get (500, 13.0f);
    return total * barW + (total - 1) * 6 + 14 + (int) font.getStringWidthFloat (juce::String (current) + " / " + juce::String (total)) + 2;
}

void ProgressSteps::paint (juce::Graphics& g)
{
    const float cy = (float) getHeight() * 0.5f;
    float x = 0.0f;
    for (int i = 0; i < total; ++i)
    {
        g.setColour (i < current ? col::accent : col::progressOff);
        g.fillRoundedRectangle (juce::Rectangle<float> (x, cy - 2.0f, (float) barW, 4.0f), 2.0f);
        x += (float) barW + 6.0f;
    }
    x += 8.0f;
    g.setColour (col::inkSub);
    g.setFont (Fonts::get (500, 13.0f));
    g.drawText (juce::String (current) + " / " + juce::String (total),
                juce::Rectangle<float> (x, 0.0f, (float) getWidth() - x, (float) getHeight()), juce::Justification::centredLeft, false);
}

//==============================================================================
SegmentedTabs::SegmentedTabs (const juce::StringArray& l, float p) : labels (l), px (p)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus (false);
}

void SegmentedTabs::setSelected (int index)
{
    index = juce::jlimit (0, labels.size() - 1, index);
    if (index != selected)
    {
        selected = index;
        repaint();
    }
}

int SegmentedTabs::getIdealWidth (int pad) const
{
    float w = 0.0f;
    for (auto& t : labels)
        w = juce::jmax (w, Fonts::get (600, px).getStringWidthFloat (t));
    return ((int) std::ceil (w) + pad * 2) * labels.size() + 6;
}

juce::Rectangle<float> SegmentedTabs::cellBounds (int i) const
{
    auto inner = getLocalBounds().toFloat().reduced (3.0f);
    const float cw = inner.getWidth() / (float) juce::jmax (1, labels.size());
    return { inner.getX() + cw * (float) i, inner.getY(), cw, inner.getHeight() };
}

int SegmentedTabs::indexAt (juce::Point<float> p) const
{
    for (int i = 0; i < labels.size(); ++i)
        if (cellBounds (i).contains (p))
            return i;
    return -1;
}

void SegmentedTabs::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float radius = (float) metric::radiusButton;
    g.setColour (col::white);
    g.fillRoundedRectangle (r, radius);
    g.setColour (col::border);
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
    for (int i = 0; i < labels.size(); ++i)
    {
        auto c = cellBounds (i);
        const bool on = i == selected;
        if (on || i == hover)
        {
            g.setColour (on ? col::accent : col::badgeBg);
            g.fillRoundedRectangle (c, radius - 2.0f);
        }
        g.setColour (on ? col::onAccent : col::inkSub);
        g.setFont (Fonts::get (on ? 600 : 500, px));
        g.drawText (labels[i], c, juce::Justification::centred, false);
    }
}

void SegmentedTabs::mouseDown (const juce::MouseEvent& e)
{
    const int i = indexAt (e.position);
    if (i < 0 || i == selected)
        return;
    selected = i;
    repaint();
    if (onChange)
        onChange (i);
}

void SegmentedTabs::mouseMove (const juce::MouseEvent& e)
{
    const int i = indexAt (e.position);
    if (i != hover) { hover = i; repaint(); }
}

void SegmentedTabs::mouseExit (const juce::MouseEvent&)
{
    if (hover != -1) { hover = -1; repaint(); }
}

//==============================================================================
//==============================================================================
TextLabel::TextLabel (const juce::String& t, float p, int w, juce::Colour c, juce::Justification j)
    : text (t), px (p), weight (w), ink (c), just (j)
{
    setInterceptsMouseClicks (false, false);
}

void TextLabel::setText (const juce::String& t)  { if (text != t) { text = t; repaint(); } }
void TextLabel::setFont (float p, int w)          { px = p; weight = w; repaint(); }
void TextLabel::setInk (juce::Colour c)           { ink = c; repaint(); }
void TextLabel::setJustification (juce::Justification j) { just = j; repaint(); }
void TextLabel::setLineHeight (float mul)         { lineHeightMul = mul; repaint(); }

float TextLabel::getTextWidth() const
{
    return Fonts::get (weight, px).getStringWidthFloat (text);
}

void TextLabel::paint (juce::Graphics& g)
{
    auto font = Fonts::get (weight, px);
    if (lineHeightMul > 0.0f)
    {
        drawParagraph (g, text, font, ink, getLocalBounds().toFloat(), px * lineHeightMul, just);
        return;
    }
    g.setColour (ink);
    g.setFont (font);
    g.drawText (text, getLocalBounds(), just, false);
}

} // namespace meon
