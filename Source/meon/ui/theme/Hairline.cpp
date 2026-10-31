// MEON ui - 1px 선을 물리 픽셀에 맞춰 그린다. 설명은 Hairline.h
#include "Hairline.h"

namespace meon::ui::hairline
{

Grid gridFor (juce::Graphics& g, const juce::Component& c)
{
    Grid gr;
    const float s = g.getInternalContext().getPhysicalPixelScaleFactor();
    gr.scale = s > 0.01f ? s : 1.0f;
    if (auto* top = c.getTopLevelComponent(); top != nullptr && top != &c)
        gr.origin = top->getLocalPoint (&c, juce::Point<float>());
    return gr;
}

void horizontal (juce::Graphics& g, const juce::Component& c, float x, float y, float w)
{
    const auto gr = gridFor (g, c);
    const float x0 = gr.snapX (x), x1 = gr.snapX (x + w), y0 = gr.snapY (y);
    if (x1 > x0) g.fillRect (juce::Rectangle<float> (x0, y0, x1 - x0, gr.thickness()));
}

void vertical (juce::Graphics& g, const juce::Component& c, float x, float y, float h)
{
    const auto gr = gridFor (g, c);
    const float y0 = gr.snapY (y), y1 = gr.snapY (y + h), x0 = gr.snapX (x);
    if (y1 > y0) g.fillRect (juce::Rectangle<float> (x0, y0, gr.thickness(), y1 - y0));
}

void line (juce::Graphics& g, const juce::Component& c, juce::Rectangle<float> r)
{
    if (r.getWidth() >= r.getHeight()) horizontal (g, c, r.getX(), r.getY(), r.getWidth());
    else                               vertical (g, c, r.getX(), r.getY(), r.getHeight());
}

void roundedBorder (juce::Graphics& g, const juce::Component& c, juce::Rectangle<float> outer, float radius)
{
    // 바깥 가장자리를 물리 픽셀 경계에 맞추고, 그 안쪽으로 두께만큼 선을 긋는다 (선 가운데가 픽셀 가운데)
    const auto gr = gridFor (g, c);
    const float x0 = gr.snapX (outer.getX()), x1 = gr.snapX (outer.getRight());
    const float y0 = gr.snapY (outer.getY()), y1 = gr.snapY (outer.getBottom());
    const float t = gr.thickness();
    const auto r = juce::Rectangle<float> (x0, y0, x1 - x0, y1 - y0).reduced (t * 0.5f);
    if (r.getWidth() <= 0.0f || r.getHeight() <= 0.0f) return;
    g.drawRoundedRectangle (r, radius, t);   // 모서리 반지름은 예전과 같은 값 (예전: outer.reduced (0.5f) 에 radius)
}

} // namespace meon::ui::hairline
