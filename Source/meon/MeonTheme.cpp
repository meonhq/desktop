#include "MeonTheme.h"
#include "MeonEmoji.h"
#include <map>

namespace meon
{

static FontCache* gFontCache = nullptr;

FontCache::FontCache()
{
#if JUCE_WINDOWS
    // JUCE 7 의 Windows 메모리 글꼴은 이름표 4번(전체 이름)으로 GDI 에서 다시 찾는다.
    // Medium/SemiBold 는 전체 이름이 곧 패밀리 이름이라 그대로 되지만, Regular/Bold 는
    // 'Pretendard Regular'/'Pretendard Bold' 라는 패밀리가 없어서 다른 글꼴로 대체된다.
    // 그래서 네 파일을 모두 프로세스에 등록해 둔 채로(memoryFonts 가 살아 있는 동안 유지),
    // Regular/Bold 는 패밀리 'Pretendard' + 스타일로 다시 만든다.
    memoryFonts.add (juce::Typeface::createSystemTypefaceFor (BinaryData::PretendardRegular_ttf,  BinaryData::PretendardRegular_ttfSize));
    memoryFonts.add (juce::Typeface::createSystemTypefaceFor (BinaryData::PretendardBold_ttf,     BinaryData::PretendardBold_ttfSize));
    medium   = juce::Typeface::createSystemTypefaceFor (BinaryData::PretendardMedium_ttf,   BinaryData::PretendardMedium_ttfSize);
    semiBold = juce::Typeface::createSystemTypefaceFor (BinaryData::PretendardSemiBold_ttf, BinaryData::PretendardSemiBold_ttfSize);
    regular  = juce::Typeface::createSystemTypefaceFor (juce::Font ("Pretendard", "Regular", 16.0f));
    bold     = juce::Typeface::createSystemTypefaceFor (juce::Font ("Pretendard", "Bold", 16.0f));
#else
    regular  = juce::Typeface::createSystemTypefaceFor (BinaryData::PretendardRegular_otf,  BinaryData::PretendardRegular_otfSize);
    medium   = juce::Typeface::createSystemTypefaceFor (BinaryData::PretendardMedium_otf,   BinaryData::PretendardMedium_otfSize);
    semiBold = juce::Typeface::createSystemTypefaceFor (BinaryData::PretendardSemiBold_otf, BinaryData::PretendardSemiBold_otfSize);
    bold     = juce::Typeface::createSystemTypefaceFor (BinaryData::PretendardBold_otf,     BinaryData::PretendardBold_otfSize);
#endif
    if constexpr (! emoji::nativeLayout)
    {
        inputRegular  = emoji::makeEmojiAwareTypeface (regular);
        inputMedium   = emoji::makeEmojiAwareTypeface (medium);
        inputSemiBold = emoji::makeEmojiAwareTypeface (semiBold);
        inputBold     = emoji::makeEmojiAwareTypeface (bold);
    }
    gFontCache = this;
}

FontCache::~FontCache()
{
    if (gFontCache == this)
        gFontCache = nullptr;
}

juce::Typeface::Ptr FontCache::get (int weight) const
{
    if (weight >= 700) return bold;
    if (weight >= 600) return semiBold;
    if (weight >= 500) return medium;
    return regular;
}

juce::Typeface::Ptr FontCache::getForInput (int weight) const
{
    if (weight >= 700) return inputBold;
    if (weight >= 600) return inputSemiBold;
    if (weight >= 500) return inputMedium;
    return inputRegular;
}

FontCache* FontCache::instance()
{
    return gFontCache;
}

juce::Font Fonts::get (int weight, float px)
{
    if (auto* cache = FontCache::instance())
        if (auto tf = cache->get (weight))
            return juce::Font (tf).withPointHeight (px);

    return juce::Font (px * 1.2f, weight >= 600 ? juce::Font::bold : juce::Font::plain);
}

juce::Font Fonts::forInput (int weight, float px)
{
    if (auto* cache = FontCache::instance())
        if (auto tf = cache->getForInput (weight))
            return juce::Font (tf).withPointHeight (px);

    return get (weight, px);
}

void drawParagraph (juce::Graphics& g, const juce::String& text, const juce::Font& font, juce::Colour colour,
                    juce::Rectangle<float> area, float lineHeightPx, juce::Justification justification)
{
    juce::AttributedString as;
    as.setJustification (justification);
    as.append (text, font, colour);
    as.setLineSpacing (juce::jmax (0.0f, lineHeightPx - font.getHeight()));

    juce::TextLayout layout;
    layout.createLayout (as, area.getWidth());
    layout.draw (g, area);
}

float paragraphHeight (const juce::String& text, const juce::Font& font, float width, float lineHeightPx)
{
    juce::AttributedString as;
    as.append (text, font, juce::Colours::black);
    as.setLineSpacing (juce::jmax (0.0f, lineHeightPx - font.getHeight()));
    juce::TextLayout layout;
    layout.createLayout (as, width);
    return layout.getHeight();
}

void drawParagraphWithEmoji (juce::Graphics& g, const juce::String& text, const juce::Font& font, juce::Colour colour,
                             juce::Rectangle<float> area, float lineHeightPx, juce::Justification justification)
{
    drawParagraphWithEmoji (g, text, font, colour, area, lineHeightPx, justification, {}, {});
}

/** links 범위만 linkColour·밑줄 글꼴로 넣은 AttributedString */
static juce::AttributedString makeLinkedParagraph (const juce::String& text, const juce::Font& font, juce::Colour colour,
                                                   float lineHeightPx, juce::Justification justification,
                                                   const TextRanges& links, juce::Colour linkColour)
{
    juce::AttributedString as;
    as.setJustification (justification);
    auto linkFont = font;
    linkFont.setUnderline (true);
    int pos = 0;
    for (auto r : links)
    {
        if (r.getStart() > pos)
            as.append (text.substring (pos, r.getStart()), font, colour);
        as.append (text.substring (r.getStart(), r.getEnd()), linkFont, linkColour);
        pos = r.getEnd();
    }
    if (pos < text.length())
        as.append (text.substring (pos), font, colour);
    as.setLineSpacing (juce::jmax (0.0f, lineHeightPx - font.getHeight()));
    return as;
}

void drawParagraphWithEmoji (juce::Graphics& g, const juce::String& text, const juce::Font& font, juce::Colour colour,
                             juce::Rectangle<float> area, float lineHeightPx, juce::Justification justification,
                             const TextRanges& links, juce::Colour linkColour)
{
    if constexpr (! emoji::nativeLayout)
    {
        emoji::drawLines (g, emoji::layout (text, font, area.getWidth()), font, colour, area, lineHeightPx, justification, links, linkColour);
        return;
    }

    auto as = makeLinkedParagraph (text, font, colour, lineHeightPx, justification, links, linkColour);

    // macOS 의 CTFrame 은 영역에 다 들어가지 않는 마지막 줄을 아예 빼 버린다. 위쪽 정렬이면 아래로 여유를 준다.
    if (justification.getOnlyVerticalFlags() == juce::Justification::top)
        area = area.withHeight (area.getHeight() + lineHeightPx * 2.0f);

    as.draw (g, area);   // macOS: CoreText 로 그려 컬러 이모지가 나온다. 그 외: TextLayout 으로 그린다.
}

int hitTestParagraphLink (const juce::String& text, const juce::Font& font, juce::Rectangle<float> area, float lineHeightPx,
                          juce::Justification justification, const TextRanges& links, juce::Point<float> p)
{
    if (links.empty())
        return -1;

    if constexpr (! emoji::nativeLayout)
        return emoji::hitTestLink (emoji::layout (text, font, area.getWidth()), font, area, lineHeightPx, justification, links, p);

    // CoreText 배치를 그대로 받는다 (macOS 의 TextLayout 은 그릴 때와 같은 CTFrame 을 쓴다).
    // TextLayout 은 줄을 왼쪽으로 붙여 버리므로 왼쪽 정렬로 배치하고, 오른쪽·가운데 정렬은 줄마다 직접 옮긴다.
    auto as = makeLinkedParagraph (text, font, juce::Colours::black, lineHeightPx, juce::Justification::topLeft, links, juce::Colours::black);
    juce::TextLayout layout;
    layout.createLayout (as, area.getWidth());

    // CoreText 의 글자 위치는 UTF-16 단위다 (이모지 등은 2칸)
    const int n = text.length();
    std::vector<int> utf16 ((size_t) n + 1, 0);
    for (int i = 0; i < n; ++i)
        utf16[(size_t) i + 1] = utf16[(size_t) i] + (text[i] >= 0x10000 ? 2 : 1);
    auto toIndex = [&utf16] (int u) { return (int) (std::lower_bound (utf16.begin(), utf16.end(), u) - utf16.begin()); };

    const float halfGap = juce::jmax (0.0f, lineHeightPx - font.getHeight()) * 0.5f;
    for (int li = 0; li < layout.getNumLines(); ++li)
    {
        const auto& line = layout.getLine (li);
        const float baseline = area.getY() + line.lineOrigin.y;
        if (p.y < baseline - line.ascent - halfGap || p.y >= baseline + line.descent + halfGap)
            continue;

        float shift = 0.0f;
        if (! justification.testFlags (juce::Justification::left))
        {
            float lineW = line.getLineBoundsX().getEnd();
            const int endIndex = toIndex (line.stringRange.getEnd());   // 줄 끝 공백은 정렬 폭에 들어가지 않는다
            if (endIndex > 0 && endIndex <= n && juce::CharacterFunctions::isWhitespace (text[endIndex - 1])
                && ! line.runs.isEmpty() && ! line.runs.getLast()->glyphs.isEmpty())
            {
                const auto& glyphs = line.runs.getLast()->glyphs;
                lineW -= glyphs.getReference (glyphs.size() - 1).width;
            }
            if (justification.testFlags (juce::Justification::right))
                shift = area.getWidth() - lineW;
            else if (justification.testFlags (juce::Justification::horizontallyCentred))
                shift = (area.getWidth() - lineW) * 0.5f;
        }

        for (auto* run : line.runs)
        {
            if (run->glyphs.isEmpty())
                continue;
            const int index = toIndex (run->stringRange.getStart());
            for (size_t k = 0; k < links.size(); ++k)
            {
                if (! links[k].contains (index))
                    continue;
                const auto& first = run->glyphs.getReference (0);   // Glyph 는 기본 생성자가 없어 getFirst/getLast 를 못 쓴다
                const auto& last = run->glyphs.getReference (run->glyphs.size() - 1);
                const float x0 = area.getX() + shift + line.lineOrigin.x + first.anchor.x;
                const float x1 = area.getX() + shift + line.lineOrigin.x + last.anchor.x + last.width;
                if (p.x >= x0 && p.x < x1)
                    return (int) k;
            }
        }
    }
    return -1;
}

float paragraphHeightWithEmoji (const juce::String& text, const juce::Font& font, float width, float lineHeightPx)
{
    if constexpr (! emoji::nativeLayout)
        return emoji::height (emoji::layout (text, font, width), font, lineHeightPx);

    return paragraphHeight (text, font, width, lineHeightPx);
}

float lineWidthWithEmoji (const juce::String& text, const juce::Font& font)
{
    if constexpr (! emoji::nativeLayout)
    {
        const auto lines = emoji::layout (text, font, 0.0f);
        return lines.empty() ? 0.0f : lines.front().width;
    }

    juce::AttributedString as;
    as.append (text, font, juce::Colours::black);
    as.setWordWrap (juce::AttributedString::none);
    juce::TextLayout layout;
    layout.createLayout (as, 1.0e6f);
    return layout.getNumLines() > 0 ? layout.getLine (0).getLineBoundsX().getLength() : 0.0f;
}

void drawLineWithEmoji (juce::Graphics& g, const juce::String& text, const juce::Font& font, juce::Colour colour,
                        juce::Rectangle<float> area, juce::Justification justification)
{
    auto shown = text.replaceCharacters ("\r\n", "  ");
    if (lineWidthWithEmoji (shown, font) > area.getWidth())
    {
        // 들어가는 가장 긴 앞부분 + '…' (글자 수로 이분 탐색)
        const juce::String ellipsis = juce::String::charToString ((juce::juce_wchar) 0x2026);
        int lo = 0, hi = shown.length();
        while (lo < hi)
        {
            const int mid = (lo + hi + 1) / 2;
            if (lineWidthWithEmoji (shown.substring (0, mid).trimEnd() + ellipsis, font) <= area.getWidth())
                lo = mid;
            else
                hi = mid - 1;
        }
        shown = shown.substring (0, lo).trimEnd() + ellipsis;
    }

    if constexpr (! emoji::nativeLayout)
    {
        emoji::drawLines (g, emoji::layout (shown, font, 0.0f), font, colour, area, font.getHeight(), justification);
        return;
    }

    juce::AttributedString as;
    as.setJustification (justification);
    as.setWordWrap (juce::AttributedString::none);
    as.append (shown, font, colour);
    as.draw (g, area);
}

float textWidth (const juce::Font& font, const juce::String& text)
{
    return font.getStringWidthFloat (text);
}

//==============================================================================
namespace
{
    void collectShapes (const juce::Drawable& d, const juce::AffineTransform& parent, SvgShape& out)
    {
        const auto t = d.getTransform().followedBy (parent);
        if (auto* shape = dynamic_cast<const juce::DrawableShape*> (&d))
        {
            juce::Path p;
            if (auto* dp = dynamic_cast<const juce::DrawablePath*> (shape))
                p = dp->getPath();
            p.applyTransform (t);
            if (! shape->getStrokeFill().isInvisible() && shape->getStrokeType().getStrokeThickness() > 0.0f)
            {
                out.stroke.addPath (p);
                out.strokeWidth = shape->getStrokeType().getStrokeThickness();
            }
            if (! shape->getFill().isInvisible())
                out.fill.addPath (p);
        }
        for (auto* c : d.getChildren())
            if (auto* child = dynamic_cast<const juce::Drawable*> (c))
                collectShapes (*child, t, out);
    }

    SvgShape parseBrandSvg (const char* fileName)
    {
        SvgShape out;
        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        {
            const char* name = BinaryData::namedResourceList[i];
            if (juce::String (BinaryData::getNamedResourceOriginalFilename (name)) != fileName)
                continue;
            int size = 0;
            const char* data = BinaryData::getNamedResource (name, size);
            auto xml = juce::XmlDocument::parse (juce::String::fromUTF8 (data, size));
            if (xml == nullptr) break;
            auto vb = juce::StringArray::fromTokens (xml->getStringAttribute ("viewBox"), " ,", "");
            vb.removeEmptyStrings();
            if (vb.size() == 4)
                out.viewBox = { vb[0].getFloatValue(), vb[1].getFloatValue(), vb[2].getFloatValue(), vb[3].getFloatValue() };
            // preserveAspectRatio 가 없으면 JUCE 는 도형을 viewBox 좌표 그대로 둔다
            if (auto d = juce::Drawable::createFromSVG (*xml))
                collectShapes (*d, {}, out);
            break;
        }
        jassert (! out.viewBox.isEmpty());   // BinaryData 에 없는 파일
        return out;
    }

    const SvgShape& logoShape (LogoKind kind)
    {
        return brandSvg (kind == LogoKind::mark ? "meon_mark.svg" : "meon_logo.svg");
    }
}

const SvgShape& brandSvg (const char* fileName)
{
    static std::map<juce::String, SvgShape> cache;   // 메시지 스레드에서만 그린다
    auto it = cache.find (fileName);
    if (it == cache.end())
        it = cache.emplace (fileName, parseBrandSvg (fileName)).first;
    return it->second;
}

float logoWidth (float heightPx, LogoKind kind)
{
    const auto vb = logoShape (kind).viewBox;
    return vb.getHeight() > 0.0f ? heightPx * vb.getWidth() / vb.getHeight() : 0.0f;
}

void drawLogo (juce::Graphics& g, juce::Rectangle<float> area, float heightPx, juce::Colour colour, juce::Justification justification, LogoKind kind)
{
    const auto& shape = logoShape (kind);
    if (shape.viewBox.isEmpty()) return;
    const float w = logoWidth (heightPx, kind);
    float x = area.getX();
    if (justification.testFlags (juce::Justification::horizontallyCentred))
        x = area.getCentreX() - w * 0.5f;
    else if (justification.testFlags (juce::Justification::right))
        x = area.getRight() - w;

    float y = area.getCentreY() - heightPx * 0.5f;
    if (justification.testFlags (juce::Justification::top))
        y = area.getY();
    else if (justification.testFlags (juce::Justification::bottom))
        y = area.getBottom() - heightPx;

    const float s = heightPx / shape.viewBox.getHeight();
    g.setColour (colour);
    g.fillPath (shape.fill, juce::AffineTransform::translation (-shape.viewBox.getX(), -shape.viewBox.getY()).scaled (s).translated (x, y));
}

float spacedTextWidth (const juce::String& text, const juce::Font& font, float fontPx, float letterSpacingEm)
{
    float w = 0.0f;
    for (int i = 0; i < text.length(); ++i)
        w += font.getStringWidthFloat (text.substring (i, i + 1));
    return w + letterSpacingEm * fontPx * (float) juce::jmax (0, text.length() - 1);
}

void drawSpacedText (juce::Graphics& g, const juce::String& text, const juce::Font& font, float fontPx, float letterSpacingEm,
                     juce::Colour colour, juce::Rectangle<float> area, juce::Justification justification)
{
    const float total = spacedTextWidth (text, font, fontPx, letterSpacingEm);
    float x = area.getX();
    if (justification.testFlags (juce::Justification::horizontallyCentred))
        x = area.getCentreX() - total * 0.5f;
    else if (justification.testFlags (juce::Justification::right))
        x = area.getRight() - total;

    g.setFont (font);
    g.setColour (colour);
    for (int i = 0; i < text.length(); ++i)
    {
        auto ch = text.substring (i, i + 1);
        const float w = font.getStringWidthFloat (ch);
        g.drawText (ch, juce::Rectangle<float> (x, area.getY(), w + 2.0f, area.getHeight()), juce::Justification::centredLeft, false);
        x += w + letterSpacingEm * fontPx;
    }
}

} // namespace meon
