#include "Display.h"
#include "../theme/Hairline.h"
#include "../theme/Icons.h"

namespace meon::ui
{

//==============================================================================
PingBadge::PingBadge() { setInterceptsMouseClicks (false, false); }

void PingBadge::setPing (std::optional<int> ms)
{
    text = ms ? juce::String (*ms) + "ms" : juce::String (TXT ("—"));
    repaint();
}

void PingBadge::paint (juce::Graphics& g)
{
    // 테두리 1px 자리는 투명으로 둔다 (크기는 예전과 같음)
    auto r = juce::Rectangle<float> (48.0f, 19.0f).withCentre (getLocalBounds().toFloat().getCentre());
    g.setColour (col::badgeBg);
    g.fillRoundedRectangle (r, metric::smallRadius);
    g.setColour (col::inkSub);
    g.setFont (font (400, 12.0f));
    g.drawText (text, r, juce::Justification::centred, false);
}

//==============================================================================
Avatar::Avatar (float s, float i) : size (s), iconPx (i) {}

juce::Colour Avatar::statusColour (Status s)
{
    switch (s)
    {
        // 온라인과 합주 중은 같은 초록, 구분은 옆 상태 글자로 한다 (합주 최종 디자인)
        case Status::online:  return col::live;
        case Status::inRoom:  return col::live;
        case Status::offline: return col::statusOff;
        case Status::none:    break;
    }
    return juce::Colours::transparentBlack;
}

void Avatar::paint (juce::Graphics& g)
{
    auto r = juce::Rectangle<float> (size, size).withCentre (getLocalBounds().toFloat().getCentre());
    // 아이콘은 이름과 같은 색 (박힌 #1A1A1A 를 쓰지 않는다). 흐린 상태도 이름의 #AAAAAA 와 같게
    paintCircle (g, r, part, iconPx, dimmed ? col::inkFaint : ink, false);
    if (status != Status::none)
    {
        // 기본: right:-1 bottom:-1, 9×9, 흰 테두리 2px. 프로필 창: right:0 bottom:0, 12×12 (setLargeStatusDot)
        const float ds = largeDot ? 12.0f : 9.0f, out = largeDot ? 0.0f : 1.0f;
        juce::Rectangle<float> dot (r.getRight() + out - ds, r.getBottom() + out - ds, ds, ds);
        g.setColour (statusColour (status));
        g.fillEllipse (dot);
        g.setColour (col::white);
        g.drawEllipse (dot.reduced (1.0f), 2.0f);
    }
}

void Avatar::paintCircle (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& partKey, float iconPx,
                          juce::Colour inkColour, bool whiteFill)
{
    if (whiteFill)
    {
        g.setColour (col::white);
        g.fillEllipse (r);
    }
    g.setColour (col::borderSubtle);   // 아바타 원 테두리 (곡선이라 늘 부드럽게 그려진다: 물리 픽셀 맞춤은 하지 않는다)
    g.drawEllipse (r.reduced (0.5f), 1.0f);
    if (partKey.isNotEmpty())
        icons::drawPart (g, partKey, r, iconPx, inkColour);   // 얇은 아이콘 (PartWeight::thin)
}

void Avatar::mouseUp (const juce::MouseEvent& e)
{
    if (onClick && getLocalBounds().contains (e.getPosition())) onClick();
}

//==============================================================================
Tag::Tag (const juce::String& t) : text (t) { setInterceptsMouseClicks (false, false); }

int Tag::getIdealWidth() const { return (int) std::ceil (textWidth (font (500, 11.0f), text)) + 8 + 2; }

void paintTagBox (juce::Graphics& g, juce::Rectangle<float> box, const juce::String& text, const juce::Font& f)
{
    // 테두리 1px 자리는 투명으로 둔다 (크기는 예전과 같음)
    g.setColour (col::badgeBg);
    g.fillRoundedRectangle (box, 3.0f);
    g.setColour (col::inkSub);
    g.setFont (f);
    g.drawText (text, box, juce::Justification::centred, false);
}

void Tag::paint (juce::Graphics& g)
{
    paintTagBox (g, getLocalBounds().toFloat().withSizeKeepingCentre ((float) getIdealWidth(), 17.0f), text, font (500, 11.0f));
}

//==============================================================================
Card::Card() {}

void Card::setTitle (const juce::String& t, bool canCollapse, bool sideways)
{
    title = t;
    collapsible = canCollapse;
    sidewaysFold = canCollapse && sideways;
    if (collapsible && collapse == nullptr)
    {
        collapse = std::make_unique<CollapseButton> (sideways);
        addAndMakeVisible (*collapse);
        collapse->setOpen (open);
        collapse->onClick = [this] { setOpen (! open); if (onToggle) onToggle (open); };
    }
    resized();
    repaint();
}

void Card::setOpen (bool o)
{
    open = o;
    fold = o ? 1.0f : 0.0f;
    if (collapse) collapse->setOpen (o);
    repaint();
}

void Card::setFoldProgress (float e)
{
    if (collapse) collapse->setProgress (e);
    if (e == fold) return;
    fold = e;
    repaint();
}

void Card::setHeaderAccessory (juce::Component* c)
{
    accessory = c;
    if (c != nullptr) addAndMakeVisible (c);
    resized();
}

juce::Rectangle<int> Card::contentBounds() const
{
    return contentBoundsFor (getWidth(), getHeight());
}

juce::Rectangle<int> Card::contentBoundsFor (int w, int h) const
{
    return juce::Rectangle<int> (w, h).withTrimmedTop (headerHeight() + (title.isEmpty() ? 0 : 1)).reduced (1, 0).withTrimmedBottom (1);
}

void Card::resized()
{
    if (title.isEmpty()) return;
    auto header = getLocalBounds().withHeight (headerHeight()).reduced (metric::padX, 0);
    if (collapse) collapse->setBounds (header.removeFromRight (19).withSizeKeepingCentre (19, 19));
    if (accessory)
    {
        header.removeFromRight (8);
        const int w = accessory->getWidth() > 0 ? accessory->getWidth() : 60;
        accessory->setBounds (header.removeFromRight (w).withSizeKeepingCentre (w, juce::jmax (19, accessory->getHeight())));
    }
}

void Card::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::white);
    g.fillRoundedRectangle (r, metric::radius);
    if (title.isNotEmpty())
    {
        g.setColour (col::ink);
        g.setFont (font (600, 15.0f));
        g.drawText (title, getLocalBounds().withHeight (headerHeight()).reduced (metric::padX, 0), juce::Justification::centredLeft, true);
        // 세로로 접는 카드는 접히면(e = 0) 머리말만 남아 구분선이 없다.
        // 옆으로 접는 카드(내 오디오·모니터)는 접혀도 아래에 미터가 남으므로 늘 긋는다
        if (fold > 0.0f || ! collapsible || sidewaysFold)
        {
            g.setColour (col::borderSubtle);
            hairline::horizontal (g, *this, 1.0f, (float) headerHeight(), (float) getWidth() - 2.0f);
        }
    }
}

void Card::paintOverChildren (juce::Graphics& g)
{
    g.setColour (col::borderSubtle);
    hairline::roundedBorder (g, *this, getLocalBounds().toFloat(), metric::radius);
}

//==============================================================================
Modal::Modal (const juce::String& t, int w)
    : title (t), width (w), closeButton ((int) icons::Id::close24, IconButton::Style::plain24, 14.0f)
{
    setWantsKeyboardFocus (true);
    setFocusContainerType (FocusContainerType::keyboardFocusContainer);   // Tab 이동이 모달 안에서만 돈다
    juce::Component::setTitle (t);
    addAndMakeVisible (closeButton);
    closeButton.setTooltip (TXT ("닫기"));
    closeButton.onClick = [this] { if (onClose) onClose(); };
}

void Modal::setContent (juce::Component* c, int h)
{
    content = c;
    contentH = h;
    if (c != nullptr) addAndMakeVisible (c);
    resized();
}

void Modal::setContentHeight (int h) { contentH = h; resized(); repaint(); }

void Modal::setHeaderAccessory (juce::Component* c)
{
    accessory = c;
    if (c != nullptr) addAndMakeVisible (c);
    resized();
}

juce::Rectangle<int> Modal::boxBounds() const
{
    auto area = insets.subtractedFrom (getLocalBounds()).reduced (metric::padX);
    const int h = (showHeader ? 48 : 0) + (showHeader ? 1 : 0) + contentH;
    return juce::Rectangle<int> (juce::jmin (width, area.getWidth()), juce::jmin (h, area.getHeight())).withCentre (area.getCentre());
}

void Modal::resized()
{
    const auto box = boxBounds();
    closeButton.setVisible (showHeader && showClose);
    closeButton.setBounds (box.getRight() - metric::padX - 24, box.getY() + 12, 24, 24);
    if (accessory != nullptr)
    {
        const int w = accessory->getWidth() > 0 ? accessory->getWidth() : 60;
        const int right = box.getRight() - metric::padX - (showClose ? 24 + 9 : 0);
        accessory->setBounds (right - w, box.getY() + 24 - metric::smallH / 2, w, metric::smallH);
        accessory->setVisible (showHeader);
    }
    if (content != nullptr)
        content->setBounds (box.withTrimmedTop (showHeader ? 49 : 0));
}

void Modal::paint (juce::Graphics& g)
{
    g.setColour (col::overlay);
    g.fillRect (insets.subtractedFrom (getLocalBounds()));
    const auto box = boxBounds().toFloat();
    g.setColour (col::white);
    g.fillRoundedRectangle (box, metric::radius);
    if (showHeader)
    {
        g.setColour (col::ink);
        g.setFont (font (600, 15.0f));
        g.drawText (title, box.withHeight (48.0f).reduced ((float) metric::padX, 0.0f).toNearestInt(), juce::Justification::centredLeft, true);
        g.setColour (col::borderSubtle);
        hairline::horizontal (g, *this, box.getX(), box.getY() + 48.0f, box.getWidth());
    }
}

void Modal::mouseUp (const juce::MouseEvent& e)
{
    if (! boxBounds().contains (e.getPosition()) && onClose) onClose();
}

bool Modal::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey && onClose) { onClose(); return true; }
    return false;
}

//==============================================================================
namespace
{
    float digitCell (const juce::Font& f)
    {
        float w = 0.0f;
        for (char c = '0'; c <= '9'; ++c) w = juce::jmax (w, textWidth (f, juce::String::charToString (c)));
        return w;
    }
}

float tabularWidth (const juce::Font& f, const juce::String& s)
{
    const float cell = digitCell (f);
    float w = 0.0f;
    for (auto c : s) w += juce::CharacterFunctions::isDigit (c) ? cell : textWidth (f, juce::String::charToString (c));
    return w;
}

float drawTabular (juce::Graphics& g, const juce::Font& f, const juce::String& s, float x, float y, float h)
{
    const float cell = digitCell (f);
    g.setFont (f);
    float cx = x;
    for (auto c : s)
    {
        const auto ch = juce::String::charToString (c);
        const float cw = textWidth (f, ch);
        const float w = juce::CharacterFunctions::isDigit (c) ? cell : cw;
        g.drawText (ch, juce::Rectangle<float> (cx + (w - cw) * 0.5f, y, cw + 1.0f, h), juce::Justification::centredLeft, false);
        cx += w;
    }
    return cx - x;
}

//==============================================================================
//==============================================================================
Tip::Tip() { setInterceptsMouseClicks (false, false); setAlwaysOnTop (true); }

int Tip::widthFor (const juce::String& t) { return (int) std::ceil (textWidth (font (500, 12.0f), t)) + 14; }

void Tip::show (juce::Component& host, juce::Component& t, const juce::String& s, Place where)
{
    text = s;
    sub = {};
    note = false;
    place (host, t, widthFor (text), height, where);
}

void Tip::showNote (juce::Component& host, juce::Component& t, const juce::String& title, const juce::String& subText, Place where)
{
    // 디자인: padding 6px 8px, font 500 12px/1.4 (줄 17), gap 2, 첫 줄 600
    text = title;
    sub = subText;
    note = true;
    int w = (int) std::ceil (textWidth (font (600, 12.0f), title));
    if (sub.isNotEmpty()) w = juce::jmax (w, (int) std::ceil (textWidth (font (500, 12.0f), sub)));
    place (host, t, w + 16, 6 + 17 + (sub.isNotEmpty() ? 2 + 17 : 0) + 6, where);
}

void Tip::place (juce::Component& host, juce::Component& t, int w, int h, Place where)
{
    target = &t;
    if (getParentComponent() != &host) host.addChildComponent (this);
    const auto b = host.getLocalArea (t.getParentComponent(), t.getBounds());
    int x = where == Place::belowCentre ? b.getCentreX() - w / 2 : b.getRight() - w;
    x = juce::jlimit (0, juce::jmax (0, host.getWidth() - w), x);
    const int y = where == Place::aboveRight ? b.getY() - 6 - h : b.getBottom() + 6;
    setBounds (x, y, w, h);
    setVisible (true);
    toFront (false);
    repaint();
}

void Tip::hide()
{
    target = nullptr;
    setVisible (false);
}

void Tip::paint (juce::Graphics& g)
{
    g.setColour (col::ink);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
    g.setColour (col::white);
    if (! note)
    {
        g.setFont (font (500, 12.0f));
        g.drawText (text, getLocalBounds(), juce::Justification::centred, false);
        return;
    }
    g.setFont (font (600, 12.0f));
    g.drawText (text, juce::Rectangle<int> (8, 6, getWidth() - 16, 17), juce::Justification::centredLeft, false);
    if (sub.isEmpty()) return;
    g.setColour (juce::Colour (0xFFD5D5D5u));
    g.setFont (font (500, 12.0f));
    g.drawText (sub, juce::Rectangle<int> (8, 6 + 17 + 2, getWidth() - 16, 17), juce::Justification::centredLeft, false);
}

TipOnHover::TipOnHover (Tip& t, juce::Component& h, juce::Component& c, const juce::String& s, Tip::Place p)
    : tip (t), host (h), target (c), text (s), place (p)
{
    target.addMouseListener (this, false);
}

TipOnHover::~TipOnHover() { target.removeMouseListener (this); }

void TipOnHover::mouseEnter (const juce::MouseEvent&) { if (target.isShowing()) tip.show (host, target, text, place); }
void TipOnHover::mouseExit (const juce::MouseEvent&)  { if (tip.getTarget() == &target) tip.hide(); }
void TipOnHover::mouseDown (const juce::MouseEvent&)  { if (tip.getTarget() == &target) tip.hide(); }

//==============================================================================
Popover::Popover (const juce::String& t, int w)
    : title (t), width (w), closeButton ((int) icons::Id::close12, IconButton::Style::bare19, 10.0f)
{
    addAndMakeVisible (closeButton);
    closeButton.setTooltip (TXT ("닫기"));
    closeButton.onClick = [this] { if (onClose) onClose(); };
}

void Popover::setContent (juce::Component* c, int h)
{
    content = c;
    contentH = h;
    if (c != nullptr) addAndMakeVisible (c);
    resized();
}

void Popover::resized()
{
    const auto p = panel();
    closeButton.setBounds (p.getRight() - metric::padX - 19, p.getY() + 12, 19, 19);
    if (content != nullptr) content->setBounds (p.withTrimmedTop (44).reduced (1, 0).withTrimmedBottom (1));
}

void Popover::paint (juce::Graphics& g)
{
    paintSurface (g);
    const auto r = panel().toFloat();
    const auto tf = font (600, 15.0f);
    g.setColour (col::ink);
    g.setFont (tf);
    const float tw = textWidth (tf, title);
    g.drawText (title, juce::Rectangle<float> (r.getX() + (float) metric::padX, r.getY(), tw + 2.0f, 43.0f), juce::Justification::centredLeft, false);
    if (count.isNotEmpty())
    {
        g.setColour (col::inkSub);
        g.setFont (font (500, 13.0f));
        g.drawText (count, juce::Rectangle<float> (r.getX() + (float) metric::padX + tw + 6.0f, r.getY(), 60.0f, 43.0f), juce::Justification::centredLeft, false);
    }
    g.setColour (col::borderSubtle);
    hairline::horizontal (g, *this, r.getX() + 1.0f, r.getY() + 43.0f, r.getWidth() - 2.0f);
}

//==============================================================================
Text::Text (const juce::String& t, int w, float p, juce::Colour c) : text (t), weight (w), px (p), ink (c)
{
    setInterceptsMouseClicks (false, false);
}

void Text::setText (const juce::String& t) { if (text != t) { text = t; repaint(); } }
void Text::setStyle (int w, float p, juce::Colour c) { weight = w; px = p; ink = c; repaint(); }

int Text::getIdealWidth() const { return (int) std::ceil (meon::lineWidthWithEmoji (text, font (weight, px))) + 1; }

int Text::getHeightForWidth (int w) const
{
    if (lineH <= 0.0f) return (int) std::ceil (px * 1.3f);
    return (int) std::ceil (meon::paragraphHeightWithEmoji (text, font (weight, px), (float) w, lineH));
}

void Text::paint (juce::Graphics& g)
{
    const auto f = font (weight, px);
    if (lineH > 0.0f)
        meon::drawParagraphWithEmoji (g, text, f, ink, getLocalBounds().toFloat(), lineH, just);
    else if (ellipsis)
        meon::drawLineWithEmoji (g, text, f, ink, getLocalBounds().toFloat(), just);
    else
    {
        g.setColour (ink);
        g.setFont (f);
        g.drawText (text, getLocalBounds(), just, false);
    }
}

void Text::mouseUp (const juce::MouseEvent& e)
{
    if (onClick && getLocalBounds().contains (e.getPosition())) onClick();
}

} // namespace meon::ui
