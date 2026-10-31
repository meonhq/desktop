// MEON ui - 팝오버 겉모양 그리기. 설명은 PopoverSurface.h
#include "PopoverSurface.h"
#include "Hairline.h"
#include "Tokens.h"

namespace meon::ui
{

namespace
{
    float sigmaOf (const popoverStyle::ShadowLayer& l) { return l.blur * popoverStyle::sigmaPerBlur; }

    /** 그림 안에서 판이 놓이는 자리 (물리 px, 정수: 판 왼쪽 위가 물리 픽셀 경계에 오게) */
    int padPx (float scale) { return (int) std::ceil ((float) PopoverSurface::shadowMargin() * scale - 0.001f); }

    /** 가우스 흐림을 상자 흐림 세 번으로 (상자 폭은 분산이 σ² 에 가장 가깝게: 브라우저도 같은 방식) */
    std::array<int, 3> boxRadii (float sigma)
    {
        constexpr int n = 3;
        const float wIdeal = std::sqrt (12.0f * sigma * sigma / n + 1.0f);
        int wl = (int) std::floor (wIdeal);
        if (wl % 2 == 0) --wl;
        wl = juce::jmax (1, wl);
        const int wu = wl + 2;
        const float mIdeal = (12.0f * sigma * sigma - n * wl * wl - 4.0f * n * wl - 3.0f * n) / (-4.0f * wl - 4.0f);
        const int m = juce::roundToInt (mIdeal);
        std::array<int, 3> r {};
        for (int i = 0; i < n; ++i) r[(size_t) i] = ((i < m ? wl : wu) - 1) / 2;
        return r;
    }

    // 가로 상자 흐림 (한 줄씩, 바깥은 0)
    void boxH (const float* src, float* dst, int W, int H, int r)
    {
        const double inv = 1.0 / (2 * r + 1);
        for (int y = 0; y < H; ++y)
        {
            const float* s = src + (size_t) y * (size_t) W;
            float* d = dst + (size_t) y * (size_t) W;
            double acc = 0.0;
            for (int x = 0; x <= r && x < W; ++x) acc += s[x];
            for (int x = 0; x < W; ++x)
            {
                d[x] = (float) (acc * inv);
                if (x + r + 1 < W) acc += s[x + r + 1];
                if (x - r >= 0)    acc -= s[x - r];
            }
        }
    }

    // 세로 상자 흐림 (줄 단위 누적: 메모리를 차례로 읽는다)
    void boxV (const float* src, float* dst, int W, int H, int r)
    {
        const double inv = 1.0 / (2 * r + 1);
        std::vector<double> acc ((size_t) W, 0.0);
        for (int y = 0; y <= r && y < H; ++y)
        {
            const float* s = src + (size_t) y * (size_t) W;
            for (int x = 0; x < W; ++x) acc[(size_t) x] += s[x];
        }
        for (int y = 0; y < H; ++y)
        {
            float* d = dst + (size_t) y * (size_t) W;
            for (int x = 0; x < W; ++x) d[x] = (float) (acc[(size_t) x] * inv);
            if (y + r + 1 < H) { const float* s = src + (size_t) (y + r + 1) * (size_t) W; for (int x = 0; x < W; ++x) acc[(size_t) x] += s[x]; }
            if (y - r >= 0)    { const float* s = src + (size_t) (y - r) * (size_t) W;     for (int x = 0; x < W; ++x) acc[(size_t) x] -= s[x]; }
        }
    }

    void gaussian (std::vector<float>& buf, std::vector<float>& tmp, int W, int H, float sigma)
    {
        if (sigma < 0.2f) return;
        const auto radii = boxRadii (sigma);
        for (int r : radii) if (r > 0) { boxH (buf.data(), tmp.data(), W, H, r); std::swap (buf, tmp); }
        for (int r : radii) if (r > 0) { boxV (buf.data(), tmp.data(), W, H, r); std::swap (buf, tmp); }
    }
}

int PopoverSurface::shadowMargin()
{
    // 가장 멀리 번지는 겹: 아래로 밀린 만큼 + 3σ (그 밖은 8비트로 0)
    float m = 0.0f;
    for (const auto& l : popoverStyle::shadow) m = juce::jmax (m, std::abs (l.dy) + 3.0f * sigmaOf (l));
    return (int) std::ceil (m);
}

juce::Image PopoverSurface::renderShadow (int w, int h, float scale)
{
    const int pad = padPx (scale);
    const int pw = juce::roundToInt ((float) w * scale), ph = juce::roundToInt ((float) h * scale);
    const int W = pw + 2 * pad, H = ph + 2 * pad;
    if (pw <= 0 || ph <= 0) return {};

    std::vector<float> total ((size_t) W * (size_t) H, 0.0f);   // 겹친 그림자의 알파 (0..1)
    std::vector<float> buf ((size_t) W * (size_t) H), tmp ((size_t) W * (size_t) H);
    juce::Image mask (juce::Image::SingleChannel, W, H, true, juce::SoftwareImageType());

    for (const auto& l : popoverStyle::shadow)
    {
        mask.clear (mask.getBounds());
        {
            juce::Graphics mg (mask);
            mg.setColour (juce::Colours::white);
            mg.fillRoundedRectangle (juce::Rectangle<float> ((float) pad, (float) pad + l.dy * scale, (float) pw, (float) ph),
                                     popoverStyle::radius * scale);
        }
        {
            const juce::Image::BitmapData bd (mask, juce::Image::BitmapData::readOnly);
            for (int y = 0; y < H; ++y)
            {
                const auto* line = bd.getLinePointer (y);
                float* d = buf.data() + (size_t) y * (size_t) W;
                for (int x = 0; x < W; ++x) d[x] = (float) line[x * bd.pixelStride] * (1.0f / 255.0f);
            }
        }
        gaussian (buf, tmp, W, H, sigmaOf (l) * scale);
        const float a = l.alpha * popoverStyle::opacityScale;
        // 검정 그림자 두 겹을 차례로 덮는다 (CSS 와 같은 합성: 1 - (1-a1)(1-a2))
        for (size_t i = 0; i < total.size(); ++i)
            total[i] = 1.0f - (1.0f - total[i]) * (1.0f - a * juce::jlimit (0.0f, 1.0f, buf[i]));
    }

    juce::Image out (juce::Image::ARGB, W, H, false);   // 맥은 CoreGraphics 그림 (그릴 때 시스템이 캐시한다)
    {
        juce::Image::BitmapData bd (out, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < H; ++y)
        {
            auto* line = bd.getLinePointer (y);
            const float* s = total.data() + (size_t) y * (size_t) W;
            for (int x = 0; x < W; ++x)
            {
                const auto a = (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (s[x] * 255.0f));
                reinterpret_cast<juce::PixelARGB*> (line + x * bd.pixelStride)->setARGB (a, 0, 0, 0);   // 검정 (미리 곱한 알파)
            }
        }
    }
    return out;
}

void PopoverSurface::paint (juce::Graphics& g, const juce::Component& c, juce::Rectangle<int> panel)
{
    paint (g, c, panel, popoverStyle::background);
}

void PopoverSurface::paint (juce::Graphics& g, const juce::Component& c, juce::Rectangle<int> panel, juce::Colour fill)
{
    if (panel.isEmpty()) return;
    const auto grid = hairline::gridFor (g, c);
    const float s = grid.scale;

    // 그림자: 판 크기나 배율이 바뀔 때만 새로 만든다
    if (! g.clipRegionIntersects (panel.expanded (shadowMargin()))) return;
    if (! shadow.isValid() || cachedW != panel.getWidth() || cachedH != panel.getHeight() || cachedScale != s)
    {
        shadow = renderShadow (panel.getWidth(), panel.getHeight(), s);
        cachedW = panel.getWidth(); cachedH = panel.getHeight(); cachedScale = s;
        ++builds;
    }
    const float x0 = grid.snapX ((float) panel.getX()), y0 = grid.snapY ((float) panel.getY());
    if (shadow.isValid())
    {
        // 그림의 물리 픽셀 하나 = 화면 물리 픽셀 하나 (판 왼쪽 위가 물리 픽셀 경계라 그대로 옮겨 붙인다)
        const float pad = (float) padPx (s) / s;
        g.setOpacity (1.0f);
        g.drawImageTransformed (shadow, juce::AffineTransform::scale (1.0f / s).translated (x0 - pad, y0 - pad));
    }

    // 면: 테두리와 같은 물리 픽셀 경계에 맞춰 채운다 (가장자리 반 픽셀이 테두리 밖으로 비치지 않게)
    g.setColour (fill);
    g.fillRoundedRectangle (grid.snap (panel.toFloat()), popoverStyle::radius);
    g.setColour (popoverStyle::border);
    hairline::roundedBorder (g, c, panel.toFloat(), popoverStyle::radius);
}

namespace
{
    /** 판 가장자리에서 이만큼(논리 px) 안쪽부터는 그림자가 가로로 같다: 모서리 + 가장 넓은 흐림 */
    int stretchEdge() { return (int) std::ceil (popoverStyle::radius) + PopoverSurface::shadowMargin() + 1; }
}

void StretchShadow::paint (juce::Graphics& g, const juce::Component& c, juce::Rectangle<int> panel, float opacity)
{
    if (panel.isEmpty() || opacity <= 0.0f) return;
    if (! g.clipRegionIntersects (panel.expanded (PopoverSurface::shadowMargin()))) return;
    const int edge = stretchEdge();
    const int refW = edge * 2 + 2;   // 기준 그림의 판 폭 (가운데 2px 는 가로로 같은 줄)
    if (panel.getWidth() < refW)
    {
        // 아주 좁으면 그 크기 그대로 (면·테두리는 그리지 않게 그림자만 쓰기 위해 투명도만 건다)
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (juce::RectangleList<int> (panel.expanded (PopoverSurface::shadowMargin())));
        g.excludeClipRegion (panel);
        g.beginTransparencyLayer (opacity);
        narrow.paint (g, c, panel);
        g.endTransparencyLayer();
        return;
    }
    const auto grid = hairline::gridFor (g, c);
    const float s = grid.scale;
    if (! shadow.isValid() || cachedH != panel.getHeight() || cachedScale != s)
    {
        shadow = PopoverSurface::renderShadow (refW, panel.getHeight(), s);
        cachedH = panel.getHeight(); cachedScale = s;
        ++builds;
    }
    if (! shadow.isValid()) return;
    // 물리 px: 그림 = 여백 pad + 판 + 여백. 왼쪽 [0, pad + E), 오른쪽 [W - pad - E, W), 가운데 한 줄 (pad + E)
    const int W = shadow.getWidth(), H = shadow.getHeight();
    const int pad = (int) std::ceil ((float) PopoverSurface::shadowMargin() * s - 0.001f);
    const int E = juce::roundToInt ((float) edge * s);
    const int targetPw = juce::roundToInt ((float) panel.getWidth() * s);
    const float x0 = grid.snapX ((float) panel.getX()) - (float) pad / s, y0 = grid.snapY ((float) panel.getY()) - (float) pad / s;
    const auto base = juce::AffineTransform::scale (1.0f / s).translated (x0, y0);
    const auto left = shadow.getClippedImage ({ 0, 0, pad + E, H });
    const auto right = shadow.getClippedImage ({ W - pad - E, 0, pad + E, H });
    const auto mid = shadow.getClippedImage ({ pad + E, 0, 1, H });
    const int midW = targetPw - 2 * E;   // 늘려 붙일 물리 px
    juce::Graphics::ScopedSaveState ss (g);
    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);   // 물리 픽셀 그대로 (가운데 한 줄은 그대로 늘린다)
    g.setOpacity (opacity);
    g.drawImageTransformed (left, base);
    if (midW > 0)
        g.drawImageTransformed (mid, juce::AffineTransform::scale ((float) midW, 1.0f).translated ((float) (pad + E), 0.0f).followedBy (base));
    g.drawImageTransformed (right, juce::AffineTransform::translation ((float) (pad + E + juce::jmax (0, midW)), 0.0f).followedBy (base));
}

void SurfaceComponent::paintSurface (juce::Graphics& g, juce::Colour fill) { surface.paint (g, *this, panel(), fill); }
void SurfaceComponent::paintSurface (juce::Graphics& g) { surface.paint (g, *this, panel()); }

} // namespace meon::ui
