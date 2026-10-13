#include "Meter.h"

namespace meon::ui
{

Meter::Meter() : vblank (this, [this] { step (juce::Time::getMillisecondCounterHiRes()); })
{
    setSource (1, {});
    setTooltip (TXT ("레벨"));
}

void Meter::setSource (int channels, Source s)
{
    chans.assign ((size_t) juce::jlimit (1, 2, channels), {});
    source = std::move (s);
    repaint();
}

void Meter::setActive (bool a) { active = a; }

void Meter::resetClip()
{
    clipped = false;
    for (auto& c : chans) { c.peak = c.db; c.peakAt = lastMs; }
    repaint();
}

void Meter::mouseUp (const juce::MouseEvent&) { resetClip(); }

float Meter::dbPos (float db)
{
    static const float map[][2] = { { -60.0f, 0.0f }, { -18.0f, 0.5f }, { -6.0f, 0.75f }, { 0.0f, 0.9f }, { 6.0f, 1.0f } };
    if (db <= -60.0f) return 0.0f;
    if (db >= 6.0f) return 1.0f;
    for (int i = 1; i < 5; ++i)
        if (db <= map[i][0])
            return map[i - 1][1] + (map[i][1] - map[i - 1][1]) * (db - map[i - 1][0]) / (map[i][0] - map[i - 1][0]);
    return 1.0f;
}

juce::Colour Meter::colourAt (float p)
{
    struct Stop { float at; juce::Colour c; };
    static const Stop stops[] = { { 0.0f, col::meterGreen }, { 0.5f, col::meterGreen }, { 0.75f, col::meterYellow },
                                  { 0.9f, col::meterOrange }, { 0.9001f, col::meterRed }, { 1.0f, col::meterRed } };
    for (int i = 1; i < 6; ++i)
    {
        if (p <= stops[i].at)
        {
            const float span = stops[i].at - stops[i - 1].at;
            const float t = span > 0.0f ? (p - stops[i - 1].at) / span : 1.0f;
            return stops[i - 1].c.interpolatedWith (stops[i].c, juce::jlimit (0.0f, 1.0f, t));
        }
    }
    return col::meterRed;
}

void Meter::step (double now)
{
    const double dt = lastMs <= 0.0 ? 0.0 : juce::jmin (0.2, (now - lastMs) / 1000.0);
    lastMs = now;
    bool changed = false;
    for (size_t i = 0; i < chans.size(); ++i)
    {
        auto& c = chans[i];
        float target = -60.0f;
        if (active && source)
        {
            const float lin = source ((int) i);
            target = lin > 0.0f ? juce::jmax (-60.0f, juce::Decibels::gainToDecibels (lin)) : -60.0f;
        }
        const float before = c.db, peakBefore = c.peak;
        c.db = target > c.db ? target : juce::jmax (target, c.db - 26.0f * (float) dt);
        if (c.db >= c.peak) { c.peak = c.db; c.peakAt = now; }
        else if (now - c.peakAt > 1500.0) c.peak = juce::jmax (c.db, c.peak - 10.0f * (float) dt);
        if (c.db >= 0.0f && ! clipped) { clipped = true; changed = true; }
        if (std::abs (before - c.db) > 0.05f || std::abs (peakBefore - c.peak) > 0.05f) changed = true;
    }
    if (changed) repaint();
}

void Meter::paint (juce::Graphics& g)
{
    auto box = getLocalBounds().toFloat().withSizeKeepingCentre ((float) getWidth(), 19.0f);
    g.setColour (col::softBg);
    g.fillRoundedRectangle (box, metric::smallRadius);
    g.setColour (col::line);
    g.drawRoundedRectangle (box.reduced (0.5f), metric::smallRadius, 1.0f);

    auto inner = box.reduced (9.0f, 5.0f).withSizeKeepingCentre (box.getWidth() - 18.0f, 7.0f);
    const int rows = (int) chans.size();
    const float rowH = (7.0f - (float) (rows - 1)) / (float) rows;
    const float w = inner.getWidth();

    juce::ColourGradient grad (col::meterGreen, inner.getX(), 0.0f, col::meterRed, inner.getRight(), 0.0f, false);
    grad.addColour (0.5, col::meterGreen);
    grad.addColour (0.75, col::meterYellow);
    grad.addColour (0.9, col::meterOrange);
    grad.addColour (0.9001, col::meterRed);

    for (int i = 0; i < rows; ++i)
    {
        const auto& c = chans[(size_t) i];
        auto row = juce::Rectangle<float> (inner.getX(), inner.getY() + (float) i * (rowH + 1.0f), w, rowH);
        juce::Graphics::ScopedSaveState s (g);
        juce::Path clip;
        clip.addRoundedRectangle (row, 1.0f);
        g.reduceClipRegion (clip);

        g.setGradientFill (grad);
        g.fillRect (row);
        const float p = dbPos (c.db);
        g.setColour (col::meterCover);
        g.fillRect (row.withLeft (row.getX() + p * w));

        // 클리핑이 남아 있으면 0 dB 표시 자리를 빨강으로 유지
        const float pp = clipped ? juce::jmax (dbPos (c.peak), 0.9f) : dbPos (c.peak);
        if (c.peak > -59.0f || clipped)
        {
            g.setColour (clipped ? col::meterRed : colourAt (pp));
            g.fillRect (juce::Rectangle<float> (row.getX() + pp * w - 1.0f, row.getY(), 2.0f, row.getHeight()));
        }
    }
}

} // namespace meon::ui
