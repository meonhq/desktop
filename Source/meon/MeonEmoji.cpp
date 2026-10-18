#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <d2d1.h>
 #include <dwrite.h>
 #include <wincodec.h>
 #include <wrl/client.h>
 #pragma comment (lib, "d2d1.lib")
 #pragma comment (lib, "dwrite.lib")
 #pragma comment (lib, "windowscodecs.lib")
#endif

#include "MeonEmoji.h"
#include "core/util/EmojiCluster.h"

#include <map>

namespace meon::emoji
{

//==============================================================================
int clusterEnd (const juce::String& text, int start) { return core::emojiClusterEnd (text, start); }

//==============================================================================
// 이모지 그림 만들기: 글자 크기 px 로 그려서 투명하지 않은 영역만 잘라 둔다.

#if defined (_WIN32)
static juce::Image renderEmoji (const juce::String& cluster, int px)
{
    using Microsoft::WRL::ComPtr;

    struct Factories
    {
        ComPtr<ID2D1Factory> d2d;
        ComPtr<IDWriteFactory> dwrite;
        ComPtr<IWICImagingFactory> wic;

        Factories()
        {
            D2D1CreateFactory (D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf());
            DWriteCreateFactory (DWRITE_FACTORY_TYPE_SHARED, __uuidof (IDWriteFactory), reinterpret_cast<IUnknown**> (dwrite.GetAddressOf()));
            CoCreateInstance (CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS (wic.GetAddressOf()));
        }
    };
    static Factories f;
    if (f.d2d == nullptr || f.dwrite == nullptr || f.wic == nullptr)
        return {};

    ComPtr<IDWriteTextFormat> format;
    if (FAILED (f.dwrite->CreateTextFormat (L"Segoe UI Emoji", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                            DWRITE_FONT_STRETCH_NORMAL, (FLOAT) px, L"ko-kr", format.GetAddressOf())))
        return {};

    const auto utf16 = cluster.toUTF16();
    const int w = px * 4, h = px * 2;   // 이어 붙지 않는 ZWJ 묶음은 여러 개로 나란히 나올 수 있어 넉넉하게

    ComPtr<IDWriteTextLayout> textLayout;
    if (FAILED (f.dwrite->CreateTextLayout (reinterpret_cast<const WCHAR*> (utf16.getAddress()), (UINT32) utf16.length(),
                                            format.Get(), (FLOAT) w, (FLOAT) h, textLayout.GetAddressOf())))
        return {};
    textLayout->SetWordWrapping (DWRITE_WORD_WRAPPING_NO_WRAP);

    ComPtr<IWICBitmap> bitmap;
    if (FAILED (f.wic->CreateBitmap ((UINT) w, (UINT) h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, bitmap.GetAddressOf())))
        return {};

    const auto props = D2D1::RenderTargetProperties (D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                                     D2D1::PixelFormat (DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                                                     96.0f, 96.0f);
    ComPtr<ID2D1RenderTarget> target;
    if (FAILED (f.d2d->CreateWicBitmapRenderTarget (bitmap.Get(), props, target.GetAddressOf())))
        return {};

    ComPtr<ID2D1SolidColorBrush> brush;
    target->CreateSolidColorBrush (D2D1::ColorF (0.1f, 0.1f, 0.1f, 1.0f), brush.GetAddressOf());
    target->SetTextAntialiasMode (D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    target->BeginDraw();
    target->Clear (D2D1::ColorF (0.0f, 0.0f, 0.0f, 0.0f));
    target->DrawTextLayout (D2D1::Point2F ((FLOAT) px * 0.25f, (FLOAT) px * 0.25f), textLayout.Get(), brush.Get(),
                            D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    if (FAILED (target->EndDraw()))
        return {};

    // WIC 32bppPBGRA 와 JUCE ARGB(리틀 엔디언, 미리 곱한 알파)는 메모리 배치가 같다
    juce::Image image (juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
    {
        juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);
        if (FAILED (bitmap->CopyPixels (nullptr, (UINT) data.lineStride, (UINT) (data.lineStride * h), data.data)))
            return {};
    }
    return image;
}
#else
static juce::Image renderEmoji (const juce::String& cluster, int px)
{
    // macOS 는 보통 CoreText 로 바로 그리지만, 직접 배치를 시험할 때를 위해 같은 방식으로 그림을 만든다.
    const int w = px * 4, h = px * 2;
    juce::Image image (juce::Image::ARGB, w, h, true);
    juce::Graphics g (image);
    juce::AttributedString as;
    as.setWordWrap (juce::AttributedString::none);
    as.append (cluster, juce::Font ((float) px), juce::Colour (0xFF1A1A1Au));
    as.draw (g, { (float) px * 0.25f, (float) px * 0.25f, (float) w, (float) h });
    return image;
}
#endif

static juce::Image cropToInk (const juce::Image& image)
{
    if (! image.isValid())
        return {};

    const juce::Image::BitmapData data (image, juce::Image::BitmapData::readOnly);
    int left = image.getWidth(), top = image.getHeight(), right = -1, bottom = -1;
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
            if (data.getPixelColour (x, y).getAlpha() > 8)
            {
                left = juce::jmin (left, x);  right = juce::jmax (right, x);
                top = juce::jmin (top, y);    bottom = juce::jmax (bottom, y);
            }

    if (right < left)
        return {};
    return image.getClippedImage ({ left, top, right - left + 1, bottom - top + 1 }).createCopy();
}

static juce::Image getEmojiImage (const juce::String& cluster, int px)
{
    static std::map<juce::String, juce::Image> cache;
    const auto key = cluster + "|" + juce::String (px);
    if (auto it = cache.find (key); it != cache.end())
        return it->second;

    if (cache.size() > 400)
        cache.clear();
    return cache[key] = cropToInk (renderEmoji (cluster, px));
}

void draw (juce::Graphics& g, const juce::String& cluster, float em, float x, float slot, float baseline)
{
    // 한글 글자 가운데 높이(기준선 위 약 0.36em)에 맞춰 em 크기 정사각형 안에 그린다
    const float side = juce::jmin (em * 1.1f, juce::jmax (slot, em * 0.5f));
    const auto box = juce::Rectangle<float> (side, side).withCentre ({ x + slot * 0.5f, baseline - em * 0.36f });

    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const int px = juce::jlimit (8, 256, juce::roundToInt (box.getHeight() * scale * 1.1f));
    const auto image = getEmojiImage (cluster, px);
    if (! image.isValid())
        return;

    juce::Graphics::ScopedSaveState state (g);
    g.setOpacity (1.0f);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (image, box, juce::RectanglePlacement::centred);
}

//==============================================================================
namespace
{
    constexpr int emojiGlyph = 0x7FFF0001;           // 이모지 묶음 첫 글자
    constexpr int emojiContinuationGlyph = 0x7FFF0002; // 묶음의 나머지 글자 (폭 0)

    class EmojiAwareTypeface final : public juce::Typeface
    {
    public:
        explicit EmojiAwareTypeface (juce::Typeface::Ptr b)
            : juce::Typeface (b->getName(), b->getStyle()), base (std::move (b)) {}

        float getAscent() const override               { return base->getAscent(); }
        float getDescent() const override              { return base->getDescent(); }
        float getHeightToPointsFactor() const override { return base->getHeightToPointsFactor(); }
        bool isHinted() const override                 { return base->isHinted(); }

        float getStringWidth (const juce::String& text) override
        {
            juce::Array<int> glyphs;
            juce::Array<float> offsets;
            getGlyphPositions (text, glyphs, offsets);
            return offsets.isEmpty() ? 0.0f : offsets.getLast();
        }

        // JUCE 는 글자 하나에 글리프 하나를 기대한다 (TextEditor 커서 위치). 이모지 묶음은 첫 글자만 폭을 갖는다.
        void getGlyphPositions (const juce::String& text, juce::Array<int>& glyphs, juce::Array<float>& offsets) override
        {
            // 높이 1 기준 단위. em = getHeightToPointsFactor()
            const float emojiAdvance = advanceForEm (getHeightToPointsFactor());
            const int n = text.length();
            float x = 0.0f;
            offsets.add (0.0f);

            for (int i = 0; i < n;)
            {
                const int end = clusterEnd (text, i);
                if (end > i)
                {
                    glyphs.add (emojiGlyph);
                    x += emojiAdvance;
                    offsets.add (x);
                    for (int k = i + 1; k < end; ++k)
                    {
                        glyphs.add (emojiContinuationGlyph);
                        offsets.add (x);
                    }
                    i = end;
                    continue;
                }

                int j = i + 1;
                while (j < n && clusterEnd (text, j) == j)
                    ++j;

                juce::Array<int> runGlyphs;
                juce::Array<float> runOffsets;
                base->getGlyphPositions (text.substring (i, j), runGlyphs, runOffsets);
                for (int k = 0; k < runGlyphs.size(); ++k)
                {
                    glyphs.add (runGlyphs.getUnchecked (k));
                    offsets.add (x + runOffsets[k + 1]);
                }
                x += runOffsets.isEmpty() ? 0.0f : runOffsets.getLast();
                i = j;
            }
        }

        bool getOutlineForGlyph (int glyph, juce::Path& path) override
        {
            if (glyph == emojiGlyph || glyph == emojiContinuationGlyph)
                return false;
            return base->getOutlineForGlyph (glyph, path);
        }

        juce::EdgeTable* getEdgeTableForGlyph (int glyph, const juce::AffineTransform& transform, float fontHeight) override
        {
            if (glyph == emojiGlyph || glyph == emojiContinuationGlyph)
                return nullptr;
            return base->getEdgeTableForGlyph (glyph, transform, fontHeight);
        }

    private:
        juce::Typeface::Ptr base;
    };
}

juce::Typeface::Ptr makeEmojiAwareTypeface (juce::Typeface::Ptr base)
{
    if (base == nullptr)
        return nullptr;
    return new EmojiAwareTypeface (base);
}

//==============================================================================
static bool isCJK (juce::juce_wchar c)
{
    return (c >= 0x1100 && c <= 0x11FF) || (c >= 0x3000 && c <= 0x9FFF) || (c >= 0xAC00 && c <= 0xD7AF)
        || (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFF00 && c <= 0xFFEF);
}

std::vector<Line> layout (const juce::String& textIn, const juce::Font& font, float maxWidth)
{
    const bool wrap = maxWidth > 0.0f;
    // 글자 위치(Piece::start)가 원래 문자열과 맞도록 글자 수를 바꾸지 않는다 ("\r\n" 의 '\r' 은 아래에서 건너뛴다)
    const auto text = wrap ? textIn : textIn.replaceCharacters ("\r\n", "  ");
    const float emojiW = advanceForEm (font.getHeightInPoints());
    const int n = text.length();

    std::vector<Line> lines (1);
    bool wrappedLine = false;   // 자동 줄바꿈으로 시작한 줄은 앞 공백을 버린다
    float x = 0.0f;

    auto finishLine = [&lines]
    {
        auto& line = lines.back();
        while (! line.pieces.empty() && line.pieces.back().text.trim().isEmpty() && ! line.pieces.back().isEmoji)
            line.pieces.pop_back();
        line.width = line.pieces.empty() ? 0.0f : line.pieces.back().x + line.pieces.back().width;
    };
    auto newLine = [&] (bool wrapped)
    {
        finishLine();
        lines.emplace_back();
        wrappedLine = wrapped;
        x = 0.0f;
    };
    auto place = [&] (const juce::String& s, bool isEmoji, float w, int start)
    {
        if (wrap && x > 0.0f && x + w > maxWidth)
            newLine (true);
        lines.back().pieces.push_back ({ s, isEmoji, x, w, start });
        x += w;
    };

    for (int i = 0; i < n;)
    {
        const auto c = text[i];

        if (c == '\r' && i + 1 < n && text[i + 1] == '\n')
        {
            ++i;
            continue;
        }

        if (c == '\n')
        {
            newLine (false);
            ++i;
            continue;
        }

        if (const int end = clusterEnd (text, i); end > i)
        {
            place (text.substring (i, end), true, emojiW, i);
            i = end;
            continue;
        }

        if (juce::CharacterFunctions::isWhitespace (c))
        {
            int j = i + 1;
            while (j < n && text[j] != '\n' && juce::CharacterFunctions::isWhitespace (text[j]))
                ++j;
            if (! (wrappedLine && lines.back().pieces.empty()))
            {
                const auto s = text.substring (i, j);
                const float w = font.getStringWidthFloat (s);
                lines.back().pieces.push_back ({ s, false, x, w, i });   // 공백은 줄 끝에 걸쳐도 된다
                x += w;
            }
            i = j;
            continue;
        }

        if (isCJK (c))
        {
            const auto s = text.substring (i, i + 1);
            place (s, false, font.getStringWidthFloat (s), i);
            ++i;
            continue;
        }

        // 그 밖의 단어: 공백·한글·이모지·줄바꿈 전까지
        int j = i + 1;
        while (j < n && text[j] != '\n' && ! juce::CharacterFunctions::isWhitespace (text[j])
               && ! isCJK (text[j]) && clusterEnd (text, j) == j)
            ++j;

        const auto word = text.substring (i, j);
        const float w = font.getStringWidthFloat (word);
        if (! wrap || w <= maxWidth)
        {
            place (word, false, w, i);
        }
        else
        {
            // 한 줄보다 긴 단어 (URL, ㅋㅋㅋ 등): 글자 단위로 자른다
            for (int k = 0; k < word.length(); ++k)
            {
                const auto s = word.substring (k, k + 1);
                place (s, false, font.getStringWidthFloat (s), i + k);
            }
        }
        i = j;
    }

    finishLine();
    return lines;
}

float height (const std::vector<Line>& lines, const juce::Font& font, float lineHeightPx)
{
    if (lines.empty())
        return 0.0f;
    return font.getHeight() + (float) (lines.size() - 1) * juce::jmax (lineHeightPx, font.getHeight());
}

namespace
{
    /** 그릴 조각 하나 (링크 경계에서 나눈 글자 조각이나 이모지 하나) */
    struct Segment
    {
        juce::String text;
        bool isEmoji;
        float x, width, baseline, top, bottom;
        int link;   // links 의 번호, 링크가 아니면 -1
    };

    template <typename Fn>
    void forEachSegment (const std::vector<Line>& lines, const juce::Font& font, juce::Rectangle<float> area, float lineHeightPx,
                         juce::Justification justification, const TextRanges& links, Fn&& fn)
    {
        const float step = juce::jmax (lineHeightPx, font.getHeight());
        const float total = height (lines, font, lineHeightPx);

        float top = area.getY();
        if (justification.testFlags (juce::Justification::verticallyCentred))
            top = area.getCentreY() - total * 0.5f;
        else if (justification.testFlags (juce::Justification::bottom))
            top = area.getBottom() - total;

        auto linkAt = [&links] (int index)
        {
            for (size_t i = 0; i < links.size(); ++i)
                if (links[i].contains (index))
                    return (int) i;
            return -1;
        };

        for (size_t k = 0; k < lines.size(); ++k)
        {
            const auto& line = lines[k];
            float left = area.getX();
            if (justification.testFlags (juce::Justification::right))
                left = area.getRight() - line.width;
            else if (justification.testFlags (juce::Justification::horizontallyCentred))
                left = area.getCentreX() - line.width * 0.5f;

            const float baseline = top + font.getAscent() + (float) k * step;
            const float rowTop = baseline - font.getAscent() - (step - font.getHeight()) * 0.5f;

            for (auto& p : line.pieces)
            {
                if (p.isEmoji)
                {
                    fn (Segment { p.text, true, left + p.x, p.width, baseline, rowTop, rowTop + step, linkAt (p.start) });
                    continue;
                }

                const int len = p.text.length();
                for (int a = 0; a < len;)
                {
                    const int link = linkAt (p.start + a);
                    int b = a + 1;
                    while (b < len && linkAt (p.start + b) == link)
                        ++b;
                    const float x0 = a == 0 ? 0.0f : font.getStringWidthFloat (p.text.substring (0, a));
                    const float x1 = b == len ? p.width : font.getStringWidthFloat (p.text.substring (0, b));
                    fn (Segment { p.text.substring (a, b), false, left + p.x + x0, x1 - x0, baseline, rowTop, rowTop + step, link });
                    a = b;
                }
            }
        }
    }
}

void drawLines (juce::Graphics& g, const std::vector<Line>& lines, const juce::Font& font, juce::Colour colour,
                juce::Rectangle<float> area, float lineHeightPx, juce::Justification justification,
                const TextRanges& links, juce::Colour linkColour)
{
    const float em = font.getHeightInPoints();
    const float underline = juce::jmax (1.0f, font.getDescent() * 0.3f);   // GlyphArrangement 의 밑줄과 같은 비율

    forEachSegment (lines, font, area, lineHeightPx, justification, links, [&] (const Segment& s)
    {
        g.setColour (s.link >= 0 ? linkColour : colour);
        if (s.isEmoji)
        {
            draw (g, s.text, em, s.x, s.width, s.baseline);
        }
        else if (s.text.trim().isNotEmpty())
        {
            juce::GlyphArrangement ga;
            ga.addLineOfText (font, s.text, s.x, s.baseline);
            ga.draw (g);
        }
        if (s.link >= 0)
            g.fillRect (juce::Rectangle<float> (s.x, s.baseline + underline * 2.0f, s.width, underline));
    });
}

int hitTestLink (const std::vector<Line>& lines, const juce::Font& font, juce::Rectangle<float> area, float lineHeightPx,
                 juce::Justification justification, const TextRanges& links, juce::Point<float> p)
{
    int found = -1;
    if (links.empty())
        return found;
    forEachSegment (lines, font, area, lineHeightPx, justification, links, [&] (const Segment& s)
    {
        if (s.link >= 0 && p.x >= s.x && p.x < s.x + s.width && p.y >= s.top && p.y < s.bottom)
            found = s.link;
    });
    return found;
}

} // namespace meon::emoji
