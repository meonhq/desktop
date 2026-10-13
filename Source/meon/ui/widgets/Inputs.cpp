#include "Inputs.h"
#include "../theme/Icons.h"
#include "../../core/model/Validation.h"

namespace meon::ui
{

namespace
{
    /** 글자 수(코드포인트)·바이트 제한과 바꿔 넣기 */
    struct Filter : juce::TextEditor::InputFilter
    {
        int maxLength = 0, maxBytes = 0;
        std::function<juce::String (const juce::String&)> transform;

        juce::String filterNewText (juce::TextEditor& ed, const juce::String& in) override
        {
            auto text = transform ? transform (in) : in;
            const auto current = ed.getText();
            const auto selected = ed.getHighlightedRegion().getLength();

            if (maxLength > 0)
            {
                const int room = maxLength - (current.length() - selected);
                if (room <= 0) return {};
                if (text.length() > room) text = text.substring (0, room);
            }
            if (maxBytes > 0)
            {
                const auto sel = ed.getHighlightedText();
                const int used = (int) current.getNumBytesAsUTF8() - (int) sel.getNumBytesAsUTF8();
                int room = maxBytes - used;
                if (room <= 0) return {};
                // 글자를 자르지 않게 앞에서부터 바이트를 센다
                juce::String out;
                for (auto p = text.getCharPointer(); ! p.isEmpty();)
                {
                    const auto c = p.getAndAdvance();
                    const int n = (int) juce::CharPointer_UTF8::getBytesRequiredFor (c);
                    if (n > room) break;
                    room -= n;
                    out += juce::String::charToString (c);
                }
                text = out;
            }
            return text;
        }
    };
}

//==============================================================================
TextInput::TextInput (const juce::String& ph, bool multiLine) : placeholder (ph), multi (multiLine)
{
    addAndMakeVisible (ed);
    ed.setMultiLine (multi, true);
    ed.setReturnKeyStartsNewLine (false);
    ed.setScrollbarsShown (multi);
    ed.setBorder ({});
    ed.setIndents (0, 0);
    ed.setJustification (multi ? juce::Justification::topLeft : juce::Justification::centredLeft);
    ed.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentWhite);
    ed.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    ed.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    ed.setColour (juce::TextEditor::textColourId, col::ink);
    ed.setColour (juce::TextEditor::highlightColourId, col::accent.withAlpha (0.45f));
    ed.setColour (juce::CaretComponent::caretColourId, col::ink);
    ed.addListener (this);
    setMaxLength (0);
    setFontPx (13.0f);
    setPlaceholder (ph);
}

void TextInput::setFontPx (float px, int w)
{
    fontPx = px;
    weight = w;
    ed.setFont (meon::Fonts::forInput (weight, fontPx));
    ed.applyFontToAllText (meon::Fonts::forInput (weight, fontPx));
    setPlaceholder (placeholder);
}

void TextInput::setPlaceholder (const juce::String& p)
{
    placeholder = p;
    ed.setTextToShowWhenEmpty (placeholder, col::inkFaint);
}

void TextInput::setText (const juce::String& t, bool notify)
{
    applying = ! notify;
    ed.setText (t, notify);
    applying = false;
}

juce::String TextInput::getText() const { return ed.getText(); }

void TextInput::setError (bool e) { if (error != e) { error = e; repaint(); } }
void TextInput::setPassword (bool p) { ed.setPasswordCharacter (p ? (juce::juce_wchar) 0x2022 : 0); }
void TextInput::setReadOnly (bool r) { ed.setReadOnly (r); }
void TextInput::focus() { ed.grabKeyboardFocus(); }

void TextInput::setMaxLength (int n)
{
    maxLength = n;
    auto* f = new Filter();
    f->maxLength = maxLength;
    f->maxBytes = maxBytes;
    f->transform = [this] (const juce::String& s) { return transform ? transform (s) : s; };
    ed.setInputFilter (f, true);
}

void TextInput::setMaxBytes (int n)
{
    maxBytes = n;
    setMaxLength (maxLength);
}

void TextInput::enablementChanged() { ed.setEnabled (isEnabled()); repaint(); }

void TextInput::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (isEnabled() ? col::white : col::hoverBg);
    g.fillRoundedRectangle (r, metric::radius);
    g.setColour (error ? col::danger : (isEnabled() ? col::field : col::line));
    g.drawRoundedRectangle (r.reduced (0.5f), metric::radius, 1.0f);
}

void TextInput::resized()
{
    if (multi)
        ed.setBounds (getLocalBounds().reduced (12, 8));
    else
        ed.setBounds (getLocalBounds().reduced (12, 0));
}

void TextInput::textEditorTextChanged (juce::TextEditor&)
{
    if (transform && ! applying)
    {
        // 붙여넣기 외 경로(IME 조합 결과)도 바꿔 넣기
        const auto t = ed.getText();
        const auto x = transform (t);
        if (x != t)
        {
            const auto caret = ed.getCaretPosition();
            applying = true;
            ed.setText (x, false);
            ed.setCaretPosition (caret);
            applying = false;
        }
    }
    if (! applying && onChange) onChange();
}

void TextInput::textEditorReturnKeyPressed (juce::TextEditor&) { if (onReturn) onReturn(); }
void TextInput::textEditorEscapeKeyPressed (juce::TextEditor&) { if (onEscape) onEscape(); }
void TextInput::textEditorFocusLost (juce::TextEditor&) { if (onFocusLost) onFocusLost(); }

//==============================================================================
OtpInput::OtpInput (Charset c, Size s)
    : charset (c),
      blink (this, [this]
      {
          if (! hasKeyboardFocus (false)) return;
          const double now = juce::Time::getMillisecondCounterHiRes();
          if (now - lastBlink > 530.0) { lastBlink = now; caretOn = ! caretOn; repaint(); }
      })
{
    switch (s)
    {
        case Size::large:  cellW = 44; cellH = 52; break;
        case Size::medium: cellW = 40; cellH = 48; break;
        case Size::small:  cellW = 36; cellH = 44; break;
    }
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::IBeamCursor);
    for (auto& x : cells) x = ' ';
}

int OtpInput::getPreferredWidth() const  { return cellW * 6 + gap * 5; }
int OtpInput::getPreferredHeight() const { return cellH; }

juce::Rectangle<float> OtpInput::cellBounds (int i) const
{
    const float x0 = ((float) getWidth() - (float) getPreferredWidth()) * 0.5f;
    return { x0 + (float) (i * (cellW + gap)), 0.0f, (float) cellW, (float) cellH };
}

juce::String OtpInput::getCode() const
{
    juce::String s;
    for (auto c : cells) s += juce::String::charToString (c);
    return s.trimEnd();
}

bool OtpInput::isComplete() const
{
    for (auto c : cells)
        if (c == ' ') return false;
    return true;
}

void OtpInput::setCode (const juce::String& code)
{
    for (int i = 0; i < 6; ++i)
        cells[i] = i < code.length() ? code[i] : (juce::juce_wchar) ' ';
    active = juce::jmin (code.length(), 5);
    repaint();
}

void OtpInput::clear() { setCode ({}); error = false; active = 0; repaint(); }
void OtpInput::setError (bool e) { if (error != e) { error = e; repaint(); } }
void OtpInput::focusFirst() { active = 0; grabKeyboardFocus(); repaint(); }

void OtpInput::changed()
{
    error = false;
    caretOn = true;
    lastBlink = juce::Time::getMillisecondCounterHiRes();
    repaint();
    if (onChange) onChange();
}

juce::juce_wchar OtpInput::mapKey (juce::juce_wchar t, Charset cs)
{
    // 두벌식 자판에서 눌린 자모 → 같은 자리의 영문 키
    static const std::pair<juce::juce_wchar, char> jamo[] = {
        { 0x3142, 'Q' }, { 0x3148, 'W' }, { 0x3137, 'E' }, { 0x3131, 'R' }, { 0x3145, 'T' }, { 0x315B, 'Y' }, { 0x3155, 'U' },
        { 0x3151, 'I' }, { 0x3150, 'O' }, { 0x3154, 'P' }, { 0x3141, 'A' }, { 0x3134, 'S' }, { 0x3147, 'D' }, { 0x3139, 'F' },
        { 0x314E, 'G' }, { 0x3157, 'H' }, { 0x3153, 'J' }, { 0x314F, 'K' }, { 0x3163, 'L' }, { 0x314B, 'Z' }, { 0x314C, 'X' },
        { 0x314A, 'C' }, { 0x314D, 'V' }, { 0x3160, 'B' }, { 0x315C, 'N' }, { 0x3161, 'M' },
        { 0x3143, 'Q' }, { 0x3149, 'W' }, { 0x3138, 'E' }, { 0x3132, 'R' }, { 0x3146, 'T' }, { 0x3152, 'O' }, { 0x3156, 'P' },
    };
    for (auto& j : jamo)
        if (t == j.first) { t = (juce::juce_wchar) j.second; break; }

    const auto up = juce::CharacterFunctions::toUpperCase (t);
    if (cs == Charset::digits)
        return (up >= '0' && up <= '9') ? up : 0;
    return core::validation::isRoomCodeChar (up) ? up : 0;
}

void OtpInput::put (int i, juce::juce_wchar c)
{
    cells[i] = c;
}

bool OtpInput::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::returnKey) { if (onSubmit) onSubmit(); return true; }
    if (k == juce::KeyPress::leftKey)  { if (active > 0) { --active; repaint(); } return true; }
    if (k == juce::KeyPress::rightKey) { if (active < 5) { ++active; repaint(); } return true; }
    if (k == juce::KeyPress::backspaceKey)
    {
        if (cells[active] != ' ') put (active, ' ');
        else if (active > 0) { --active; put (active, ' '); }
        // 디자인: 칸을 비우면 뒤 칸은 그대로, 끝의 빈 칸만 잘린다
        changed();
        return true;
    }
    if (k == juce::KeyPress::deleteKey) { put (active, ' '); changed(); return true; }
    if (k == juce::KeyPress::tabKey) return false;

    if (k.getModifiers().isCommandDown())
    {
        // 한글 자판에서는 ⌘V 가 'ㅍ' 로 들어오므로 키 코드로도 본다
        const auto code = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) k.getKeyCode());
        if (code == 'V' || k.getTextCharacter() == 0x314D)
        {
            juce::String got;
            for (auto p = juce::SystemClipboard::getTextFromClipboard().getCharPointer(); ! p.isEmpty() && got.length() < 6;)
                if (auto c = mapKey (p.getAndAdvance(), charset)) got += juce::String::charToString (c);
            if (got.isEmpty()) return true;
            setCode (got);
            changed();
            return true;
        }
        return false;
    }

    const auto typed = k.getTextCharacter();
    if (typed == 0) return false;
    if (const auto c = mapKey (typed, charset))
    {
        put (active, c);
        if (active < 5) ++active;
        changed();
    }
    else if (onRejected && ! juce::CharacterFunctions::isWhitespace (typed))
    {
        onRejected (juce::CharacterFunctions::toUpperCase (typed));
    }
    return true;
}

void OtpInput::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < 6; ++i)
        if (cellBounds (i).expanded ((float) gap * 0.5f, 0.0f).contains (e.position)) active = i;
    grabKeyboardFocus();
    caretOn = true;
    repaint();
}

void OtpInput::focusGained (FocusChangeType) { caretOn = true; repaint(); }
void OtpInput::focusLost (FocusChangeType)   { repaint(); }

void OtpInput::paint (juce::Graphics& g)
{
    const bool focused = hasKeyboardFocus (false);
    const auto f = font (600, 20.0f);
    for (int i = 0; i < 6; ++i)
    {
        const auto r = cellBounds (i);
        const bool filled = cells[i] != ' ';
        g.setColour (col::white);
        g.fillRoundedRectangle (r, metric::radius);
        g.setColour (error ? col::danger : (filled ? col::inkFaint : col::field));
        g.drawRoundedRectangle (r.reduced (0.5f), metric::radius, 1.0f);

        const bool here = focused && i == active;
        if (filled)
        {
            const auto s = juce::String::charToString (cells[i]);
            if (here)
            {
                // 칸에 들어가면 글자가 선택된다 (디자인 focus → select)
                const float w = textWidth (f, s);
                g.setColour (col::accent.withAlpha (0.45f));
                g.fillRect (juce::Rectangle<float> (w + 2.0f, 24.0f).withCentre (r.getCentre()));
            }
            g.setColour (col::ink);
            g.setFont (f);
            g.drawText (s, r, juce::Justification::centred, false);
        }
        else if (here && caretOn)
        {
            g.setColour (col::ink);
            g.fillRect (juce::Rectangle<float> (1.0f, 22.0f).withCentre (r.getCentre()));
        }
    }
}

//==============================================================================
Select::Select()
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    juce::ComboBox::onChange = [this] { if (onValueChange) onValueChange (getValue()); };
}

void Select::setItems (const juce::StringArray& v, const juce::StringArray& labels)
{
    const auto current = getValue();
    values = v;
    clear (juce::dontSendNotification);
    for (int i = 0; i < values.size(); ++i)
        addItem (labels.isEmpty() ? values[i] : labels[i], i + 1);
    setValue (current);
}

void Select::setValue (const juce::String& v, juce::NotificationType n)
{
    const int idx = values.indexOf (v);
    if (idx >= 0) setSelectedId (idx + 1, n);
}

juce::String Select::getValue() const
{
    const int id = getSelectedId();
    return id > 0 ? values[id - 1] : juce::String();
}

void Select::setItemDisabled (const juce::String& v, bool disabled)
{
    const int idx = values.indexOf (v);
    if (idx >= 0) setItemEnabled (idx + 1, ! disabled);
}

//==============================================================================
Checkbox::Checkbox() : juce::Button ({})
{
    setClickingTogglesState (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void Checkbox::paintButton (juce::Graphics& g, bool, bool)
{
    auto r = juce::Rectangle<float> (16.0f, 16.0f).withCentre (getLocalBounds().toFloat().getCentre());
    const bool on = getToggleState();
    g.setColour (on ? col::accent : col::white);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (on ? col::accent : col::field);
    g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
    if (on) icons::draw (g, icons::Id::check, r.withSizeKeepingCentre (10.0f, 10.0f), col::ink);
}

} // namespace meon::ui
