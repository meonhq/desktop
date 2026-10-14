#include "MeonParts.h"

namespace meon
{

namespace parts
{

juce::String key (Part p)
{
    switch (p)
    {
        case Part::Vocal:  return "vocal";
        case Part::Guitar: return "guitar";
        case Part::Bass:   return "bass";
        case Part::Drums:  return "drums";
        case Part::Keys:   return "keys";
        case Part::Other:  return "other";
        default:           return {};
    }
}

Part fromKey (const juce::String& k)
{
    for (int i = 0; i < count; ++i)
        if (key (at (i)) == k)
            return at (i);
    return Part::None;
}

juce::String label (Part p)
{
    switch (p)
    {
        case Part::Vocal:  return TXT ("보컬");
        case Part::Guitar: return TXT ("기타");
        case Part::Bass:   return TXT ("베이스");
        case Part::Drums:  return TXT ("드럼");
        case Part::Keys:   return TXT ("키보드");
        case Part::Other:  return TXT ("그 외");
        default:           return {};
    }
}

juce::juce_wchar code (Part p)
{
    static const char codes[] = "vgbdko";
    return p == Part::None ? 0 : (juce::juce_wchar) codes[(int) p];
}

Part fromCode (juce::juce_wchar c)
{
    for (int i = 0; i < count; ++i)
        if (code (at (i)) == c)
            return at (i);
    return Part::None;
}

//==============================================================================
void drawIcon (juce::Graphics& g, juce::Rectangle<float> area, float size, Part p, juce::Colour colour, IconWeight weight)
{
    if (p == Part::None)
        return;
    const auto file = "part-" + key (p) + (weight == IconWeight::thin ? "-thin" : "") + ".svg";
    const auto& shape = brandSvg (file.toRawUTF8());
    if (shape.viewBox.isEmpty())
        return;
    // 도형만 크기에 맞춰 늘리고, 선 굵기는 SVG 의 stroke-width 를 화면 px 그대로 (non-scaling-stroke)
    const float s = size / shape.viewBox.getWidth();
    const auto t = juce::AffineTransform::translation (-shape.viewBox.getX(), -shape.viewBox.getY())
                       .scaled (s)
                       .translated (area.getCentreX() - size * 0.5f, area.getCentreY() - size * 0.5f);
    g.setColour (colour);
    if (! shape.fill.isEmpty())
        g.fillPath (shape.fill, t);   // 면(키보드 검은 건반)에는 선을 덧그리지 않는다: 1x 화면에서 건반 틈이 뭉개진다
    g.strokePath (shape.stroke, juce::PathStrokeType (shape.strokeWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), t);
}

} // namespace parts

//==============================================================================
class PartSelector::Cell : public juce::Button
{
public:
    Cell (PartSelector& o, Part p) : juce::Button (parts::key (p)), owner (o), part (p)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setWantsKeyboardFocus (false);
        onClick = [this] { owner.setSelected (part); if (owner.onChange) owner.onChange (part); };
    }

    void paintButton (juce::Graphics& g, bool, bool) override
    {
        const auto& st = owner.style;
        const bool on = owner.selected == part;
        auto r = getLocalBounds().toFloat();
        g.setColour (on ? col::accent : col::white);
        g.fillRoundedRectangle (r, (float) metric::radiusCard);
        g.setColour (on ? col::accent : col::border);
        g.drawRoundedRectangle (r.reduced (0.5f), (float) metric::radiusCard, 1.0f);

        // 아이콘 위 · 라벨 아래, 둘을 묶어 세로 가운데
        auto font = Fonts::get (st.labelWeight, st.labelPx);
        const float labelH = font.getHeight();
        const float totalH = st.iconSize + (float) st.iconLabelGap + labelH;
        const float top = r.getCentreY() - totalH * 0.5f;
        const auto ink = isEnabled() ? col::ink : col::ink.withAlpha (0.4f);
        parts::drawIcon (g, { r.getX(), top, r.getWidth(), st.iconSize }, st.iconSize, part, ink);
        g.setColour (ink);
        g.setFont (font);
        g.drawText (parts::label (part), juce::Rectangle<float> (r.getX(), top + st.iconSize + (float) st.iconLabelGap, r.getWidth(), labelH),
                    juce::Justification::centred, false);
    }

private:
    PartSelector& owner;
    const Part part;
};

PartSelector::PartSelector (Style s) : style (s)
{
    for (int i = 0; i < parts::count; ++i)
        addAndMakeVisible (cells.add (new Cell (*this, parts::at (i))));
}

PartSelector::~PartSelector() = default;

void PartSelector::setSelected (Part p)
{
    selected = p;
    for (auto* c : cells)
        c->repaint();
}

void PartSelector::resized()
{
    // 6칸 균등 (grid repeat(6, 1fr))
    const float cellW = (float) (getWidth() - style.gap * (parts::count - 1)) / (float) parts::count;
    for (int i = 0; i < parts::count; ++i)
    {
        const int x0 = (int) std::round ((float) i * (cellW + (float) style.gap));
        const int x1 = (int) std::round ((float) i * (cellW + (float) style.gap) + cellW);
        cells[i]->setBounds (x0, 0, x1 - x0, getHeight());
    }
}

} // namespace meon
