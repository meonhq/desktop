#include "../MeonEarGuard.h"

namespace meon
{

class EarGuardTests : public juce::UnitTest
{
public:
    EarGuardTests() : juce::UnitTest ("EarGuard", "EarGuard") {}

    static void fillSine (juce::AudioBuffer<float>& b, double freq, float amp, double sr, int offset)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample (ch, i, amp * (float) std::sin (juce::MathConstants<double>::twoPi * freq * (offset + i) / sr));
    }

    float runPeak (EarGuard::Level lvl, double freq, float amp)
    {
        constexpr double sr = 48000.0;
        constexpr int block = 256;
        EarGuard g;
        g.setLevel (lvl);
        g.prepare (sr);
        juce::AudioBuffer<float> b (2, block);
        float lastPeak = 0.0f;
        for (int n = 0; n < 400; ++n)   // 약 2초
        {
            fillSine (b, freq, amp, sr, n * block);
            g.process (b, 2, block, sr);
            lastPeak = b.getMagnitude (0, block);
        }
        return lastPeak;
    }

    void runTest() override
    {
        beginTest ("Off leaves the signal unchanged");
        {
            constexpr double sr = 48000.0;
            EarGuard g;
            g.setLevel (EarGuard::Level::Off);
            g.prepare (sr);
            juce::AudioBuffer<float> b (2, 512), ref (2, 512);
            for (int n = 0; n < 20; ++n)
            {
                fillSine (b, 3000.0, 0.9f, sr, n * 512);
                ref.makeCopyOf (b);
                g.process (b, 2, 512, sr);
            }
            float maxDiff = 0.0f;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    maxDiff = juce::jmax (maxDiff, std::abs (b.getSample (ch, i) - ref.getSample (ch, i)));
            expectLessThan (maxDiff, 1.0e-5f);
        }

        beginTest ("Normal leaves quiet signals almost unchanged");
        expectWithinAbsoluteError (runPeak (EarGuard::Level::Normal, 440.0, 0.05f), 0.05f, 0.005f);

        beginTest ("Normal limits very loud signals near the peak ceiling");
        {
            const float peak = runPeak (EarGuard::Level::Normal, 3000.0, 1.0f);
            expectLessThan (peak, juce::Decibels::decibelsToGain (-0.5f));
        }

        beginTest ("Strong limits harder than Normal");
        expectLessThan (runPeak (EarGuard::Level::Strong, 3000.0, 1.0f),
                        runPeak (EarGuard::Level::Normal, 3000.0, 1.0f));
    }
};

static EarGuardTests earGuardTests;

} // namespace meon
