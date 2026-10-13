#include "Icons.h"
#include "../../MeonParts.h"
#include <map>

namespace meon::ui::icons
{

namespace
{
    struct Svg { const char* viewBox; const char* body; };

    // {C} 자리에 색을 넣는다. 경로·굵기는 디자인 그대로
    Svg svgFor (Id id)
    {
        switch (id)
        {
            case Id::chevronDown: return { "0 0 12 12", R"(<path d="M2.5 4.25L6 7.75L9.5 4.25" fill="none" stroke="{C}" stroke-width="1.75" stroke-linecap="round" stroke-linejoin="round"/>)" };
            case Id::close12:     return { "0 0 12 12", R"(<path d="M1.5 1.5L10.5 10.5M10.5 1.5L1.5 10.5" fill="none" stroke="{C}" stroke-width="2" stroke-linecap="round"/>)" };
            case Id::close24:     return { "0 0 24 24", R"(<path d="M6 6l12 12M18 6L6 18" fill="none" stroke="{C}" stroke-width="2" stroke-linecap="round"/>)" };
            case Id::gear:        return { "0 0 24 24", R"(<g fill="none" stroke="{C}" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 1 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06A1.65 1.65 0 0 0 4.68 15a1.65 1.65 0 0 0-1.51-1H3a2 2 0 1 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06A1.65 1.65 0 0 0 9 4.68a1.65 1.65 0 0 0 1-1.51V3a2 2 0 1 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06A1.65 1.65 0 0 0 19.4 9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 1 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z"/></g>)" };
            case Id::bell:        return { "0 0 24 24", R"(<g fill="none" stroke="{C}" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M18 16V11a6 6 0 0 0-12 0v5l-1.5 2h15L18 16z"/><path d="M10 21h4"/></g>)" };
            case Id::filter:      return { "0 0 24 24", R"(<g fill="none" stroke="{C}" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 7h9M17 7h3M4 17h3M11 17h9"/><circle cx="15" cy="7" r="2"/><circle cx="9" cy="17" r="2"/></g>)" };
            case Id::personPlus:  return { "0 0 24 24", R"(<g fill="none" stroke="{C}" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M15 20v-1.5a4 4 0 0 0-4-4H6a4 4 0 0 0-4 4V20"/><circle cx="8.5" cy="7.5" r="3.5"/><path d="M19 8v6M22 11h-6"/></g>)" };
            case Id::external:    return { "0 0 12 12", R"(<path d="M4 2h6v6M10 2L3 9" fill="none" stroke="{C}" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"/>)" };
            case Id::speaker:     return { "0 0 14 14", R"(<path d="M1.5 5H4.5L8 2V12L4.5 9H1.5Z" fill="{C}" stroke="{C}" stroke-width="3" stroke-linejoin="round"/><path d="M11 4.5C12.2 5.6 12.2 8.4 11 9.5" fill="none" stroke="{C}" stroke-width="2" stroke-linecap="round"/>)" };
            case Id::play:        return { "0 0 12 14", R"(<path d="M2 2L10 7L2 12Z" fill="{C}" stroke="{C}" stroke-width="3" stroke-linejoin="round"/>)" };
            case Id::check:       return { "0 0 12 12", R"(<path d="M2 6.5L5 9.5L10 3" fill="none" stroke="{C}" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>)" };
        }
        return { "0 0 12 12", "" };
    }

    juce::Drawable* cached (Id id, juce::Colour c)
    {
        static std::map<std::pair<int, juce::uint32>, std::unique_ptr<juce::Drawable>> cache;
        auto key = std::make_pair ((int) id, c.getARGB());
        auto it = cache.find (key);
        if (it != cache.end()) return it->second.get();

        const auto svg = svgFor (id);
        const auto hex = "#" + c.toDisplayString (false);
        juce::String text;
        text << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"" << svg.viewBox << "\">"
             << juce::String (svg.body).replace ("{C}", hex) << "</svg>";
        std::unique_ptr<juce::Drawable> d;
        if (auto xml = juce::XmlDocument::parse (text))
            d = juce::Drawable::createFromSVG (*xml);
        if (d != nullptr && c.getAlpha() < 255)
            d->setAlpha (c.getFloatAlpha());
        auto* raw = d.get();
        cache[key] = std::move (d);
        return raw;
    }

    juce::Rectangle<float> viewBoxOf (Id id)
    {
        auto parts = juce::StringArray::fromTokens (svgFor (id).viewBox, " ", "");
        return { 0.0f, 0.0f, parts[2].getFloatValue(), parts[3].getFloatValue() };
    }
}

void draw (juce::Graphics& g, Id id, juce::Rectangle<float> area, juce::Colour c, float angle)
{
    auto* d = cached (id, c);
    if (d == nullptr) return;
    // viewBox 를 area 에 맞춘다 (SVG 의 바깥 여백 포함 그대로)
    const auto vb = viewBoxOf (id);
    const float scale = juce::jmin (area.getWidth() / vb.getWidth(), area.getHeight() / vb.getHeight());
    const auto placed = vb.withSize (vb.getWidth() * scale, vb.getHeight() * scale).withCentre (area.getCentre());
    auto t = juce::AffineTransform::scale (scale).translated (placed.getX(), placed.getY());
    if (angle != 0.0f) t = t.rotated (angle, area.getCentreX(), area.getCentreY());
    // createFromSVG 는 viewBox 를 자기 좌표로 둔다. getDrawableBounds 기준이 아니라 viewBox 기준으로 놓는다
    d->draw (g, 1.0f, t);
}

void drawPart (juce::Graphics& g, const juce::String& key, juce::Rectangle<float> area, float size, juce::Colour c)
{
    meon::parts::drawIcon (g, area, size, meon::parts::fromKey (key), c);
}

juce::String partLabel (const juce::String& key)
{
    if (key == "vocal")  return TXT ("보컬");
    if (key == "guitar") return TXT ("기타");
    if (key == "bass")   return TXT ("베이스");
    if (key == "drums")  return TXT ("드럼");
    if (key == "keys")   return TXT ("키보드");
    return TXT ("그 외");
}

} // namespace meon::ui::icons
