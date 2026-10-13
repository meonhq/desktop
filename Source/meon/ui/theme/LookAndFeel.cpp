#include "LookAndFeel.h"

namespace meon::ui
{

namespace
{
    constexpr float fieldFontPx = 13.0f;
    constexpr int fieldPad = 12;
    constexpr int menuItemH = 30;   // [임의] 펼친 목록 항목 높이
}

LookAndFeel::LookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, col::white);
    setColour (juce::Label::textColourId, col::ink);

    setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentWhite);
    setColour (juce::TextEditor::textColourId, col::ink);
    setColour (juce::TextEditor::highlightColourId, col::accent.withAlpha (0.45f));
    setColour (juce::TextEditor::highlightedTextColourId, col::ink);
    setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::shadowColourId, juce::Colours::transparentBlack);
    setColour (juce::CaretComponent::caretColourId, col::ink);   // 디자인: caret-color #1A1A1A

    setColour (juce::ComboBox::backgroundColourId, col::white);
    setColour (juce::ComboBox::textColourId, col::ink);
    setColour (juce::ComboBox::outlineColourId, col::field);
    setColour (juce::ComboBox::arrowColourId, col::inkSub);

    setColour (juce::PopupMenu::backgroundColourId, juce::Colours::transparentWhite);   // 불투명이면 메뉴 창이 사각 불투명 창이 된다
    setColour (juce::PopupMenu::textColourId, col::ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::hoverBg);
    setColour (juce::PopupMenu::highlightedTextColourId, col::ink);

    setColour (juce::ScrollBar::thumbColourId, col::field);
    setColour (juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);
    setColour (juce::ScrollBar::backgroundColourId, juce::Colours::transparentBlack);

    setColour (juce::TooltipWindow::backgroundColourId, col::ink);
    setColour (juce::TooltipWindow::textColourId, col::white);
    setColour (juce::TooltipWindow::outlineColourId, col::ink);
}

juce::Typeface::Ptr LookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    if (auto* cache = meon::FontCache::instance())
        if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName() || f.getTypefaceName() == "Pretendard")
            if (auto tf = cache->get (f.isBold() ? 700 : 400))
                return tf;
    return juce::LookAndFeel_V4::getTypefaceForFont (f);
}

//==============================================================================
void LookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    const bool enabled = box.isEnabled();
    g.setColour (enabled ? col::white : col::hoverBg);
    g.fillRoundedRectangle (r, metric::radius);
    g.setColour (enabled ? col::field : col::line);
    g.drawRoundedRectangle (r.reduced (0.5f), metric::radius, 1.0f);

    // 브라우저 select 의 화살표 자리: 작은 아래 화살표
    const float right = (float) width - (float) fieldPad;
    icons::draw (g, icons::Id::chevronDown, { right - 12.0f, (float) height * 0.5f - 6.0f, 12.0f, 12.0f },
                 enabled ? col::inkSub : col::inkFaint);
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox&) { return font (400, fieldFontPx); }

void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label, juce::Drawable*)
{
    label.setBounds (fieldPad, 0, box.getWidth() - fieldPad * 2 - 16, box.getHeight());
    label.setBorderSize ({});
    label.setFont (getComboBoxFont (box));
    label.setColour (juce::Label::textColourId, box.isEnabled() ? col::ink : col::inkFaint);
}

void LookAndFeel::drawComboBoxTextWhenNothingSelected (juce::Graphics& g, juce::ComboBox& box, juce::Label& label)
{
    g.setColour (col::inkFaint);
    g.setFont (getComboBoxFont (box));
    g.drawText (box.getTextWhenNothingSelected(), label.getBounds(), juce::Justification::centredLeft, true);
}

juce::PopupMenu::Options LookAndFeel::getOptionsForComboBoxPopupMenu (juce::ComboBox& box, juce::Label& label)
{
    return juce::LookAndFeel_V2::getOptionsForComboBoxPopupMenu (box, label)
             .withTargetScreenArea (box.getScreenBounds().expanded (0, 4));
}

void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    g.setColour (col::white);
    g.fillRoundedRectangle (r, metric::radius);
    g.setColour (col::field);
    g.drawRoundedRectangle (r.reduced (0.5f), metric::radius, 1.0f);
}

void LookAndFeel::drawPopupMenuItemWithOptions (juce::Graphics& g, const juce::Rectangle<int>& area, bool isHighlighted,
                                                const juce::PopupMenu::Item& item, const juce::PopupMenu::Options&)
{
    if (item.isSeparator)
    {
        g.setColour (col::line);
        g.fillRect (area.reduced (fieldPad, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }
    if (isHighlighted && item.isEnabled)
    {
        g.setColour (col::hoverBg);
        g.fillRoundedRectangle (area.reduced (4, 0).toFloat(), 4.0f);
    }
    g.setColour (! item.isEnabled ? col::inkFaint : col::ink);
    g.setFont (font (item.isTicked ? 600 : 400, fieldFontPx));
    g.drawText (item.text, area.reduced (fieldPad - 4, 0), juce::Justification::centredLeft, true);
}

void LookAndFeel::getIdealPopupMenuItemSizeWithOptions (const juce::String& text, bool isSeparator, int,
                                                        int& idealWidth, int& idealHeight, const juce::PopupMenu::Options&)
{
    if (isSeparator) { idealWidth = 50; idealHeight = 9; return; }
    idealHeight = menuItemH;
    idealWidth = (int) textWidth (font (600, fieldFontPx), text) + fieldPad * 2;
}

void LookAndFeel::drawPopupMenuUpDownArrow (juce::Graphics& g, int width, int height, bool up)
{
    g.setColour (col::white);
    g.fillRect (4, 0, width - 8, height);
    icons::draw (g, icons::Id::chevronDown, juce::Rectangle<float> ((float) width, (float) height).withSizeKeepingCentre (12.0f, 12.0f),
                 col::inkSub, up ? juce::MathConstants<float>::pi : 0.0f);
}

juce::Font LookAndFeel::getPopupMenuFont() { return font (400, fieldFontPx); }

juce::CaretComponent* LookAndFeel::createCaretComponent (juce::Component* owner)
{
    auto* caret = new juce::CaretComponent (owner);
    caret->setColour (juce::CaretComponent::caretColourId, col::ink);
    return caret;
}

void LookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
                                 bool vertical, int thumbStart, int thumbSize, bool, bool)
{
    if (thumbSize <= 0) return;
    auto thumb = vertical ? juce::Rectangle<float> ((float) x + 2.0f, (float) thumbStart, (float) width - 4.0f, (float) thumbSize)
                          : juce::Rectangle<float> ((float) thumbStart, (float) y + 2.0f, (float) thumbSize, (float) height - 4.0f);
    g.setColour (col::field);
    g.fillRoundedRectangle (thumb, 3.0f);
}

juce::Rectangle<int> LookAndFeel::getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent)
{
    const int w = (int) std::ceil (textWidth (font (500, 12.0f), text)) + 14;
    const int h = 20;
    return juce::Rectangle<int> (pos.x - w / 2, pos.y - h - 12, w, h).constrainedWithin (parent);
}

void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    g.setColour (col::ink);
    g.fillRoundedRectangle (0.0f, 0.0f, (float) width, (float) height, 4.0f);
    g.setColour (col::white);
    g.setFont (font (500, 12.0f));
    g.drawText (text, 0, 0, width, height, juce::Justification::centred, false);
}

} // namespace meon::ui
