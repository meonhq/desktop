#include "MeonEarGuard.h"

namespace meon
{

//==============================================================================
// 단계별 기준값 (초기값 — 직접 들어 보고 조정)
//  · 대역 기준은 2–5 kHz 대역만의 RMS. 보통 믹스(전체 RMS -18 dBFS 안팎)에서 이 대역은 -30 dBFS 아래라 걸리지 않는다.
//  · K-가중 상한은 짧은 창(40 ms 평균 파워) 기준. 보통 연주는 -20 ~ -14 LKFS 안팎.
//  · 피크 상한은 미리보기 없이 즉시 누른다 (넘는 순간 게인을 바로 낮추고 천천히 회복).
static constexpr EarGuard::Params kOff    { 40.0f, 1.0f,  0.0f,  40.0f, 40.0f };   // 사실상 처리 없음 (전환용)
static constexpr EarGuard::Params kNormal { -18.0f, 4.0f, 12.0f, -10.0f, -1.0f };
static constexpr EarGuard::Params kStrong { -26.0f, 8.0f, 18.0f, -16.0f, -4.0f };

// 대역: 중심 3.16 kHz (2 kHz·5 kHz 의 기하 평균), Q = f0 / (5000 − 2000)
static constexpr double kBandCentreHz = 3162.0;
static constexpr double kBandQ        = 1.05;

// 시간 상수 (ms)
static constexpr double kBandAttackMs  = 5.0,  kBandReleaseMs  = 60.0;    // 대역 레벨 측정
static constexpr double kLoudAttackMs  = 40.0, kLoudReleaseMs  = 40.0;    // K-가중 레벨 측정 (같은 값 = 평균 파워)
static constexpr double kGainAttackMs  = 1.0,  kGainReleaseMs  = 150.0;   // 감쇠 적용
static constexpr double kPeakReleaseMs = 120.0;                           // 피크 감쇠 회복 (공격은 즉시)
static constexpr double kLevelRampSec  = 0.1;                             // 단계 전환

// 동작 횟수: 1 dB 이상 줄이기 시작하면 1 번, 0.1 dB 안쪽으로 돌아오면 다음 번을 셀 준비
static constexpr double kTriggerOnDb = -1.0, kTriggerOffDb = -0.1;

static const EarGuard::Params& paramsFor (int lvl)
{
    return lvl == (int) EarGuard::Level::Strong ? kStrong
         : lvl == (int) EarGuard::Level::Normal ? kNormal : kOff;
}

static inline double coef (double ms, double fs) { return 1.0 - std::exp (-1.0 / (ms * 0.001 * fs)); }
static inline double dbToGain (double db)        { return std::pow (10.0, db * 0.05); }

//==============================================================================
EarGuard::EarGuard() {}

juce::String EarGuard::levelKey (Level l)
{
    switch (l)
    {
        case Level::Off:    return "off";
        case Level::Normal: return "normal";
        case Level::Strong: return "strong";
    }
    return {};
}

void EarGuard::prepare (double sampleRate)
{
    if (sampleRate <= 0.0)
        return;
    computeCoefficients (sampleRate);
    resetState();
    appliedLevel = -1;   // 다음 process 에서 지금 단계로 바로 맞춘다
}

void EarGuard::computeCoefficients (double fs)
{
    preparedRate = fs;

    // 2–5 kHz 대역 통과 (RBJ, 중심에서 0 dB)
    {
        const double w0 = juce::MathConstants<double>::twoPi * juce::jmin (kBandCentreHz, fs * 0.45) / fs;
        const double alpha = std::sin (w0) / (2.0 * kBandQ);
        const double a0 = 1.0 + alpha;
        Biquad b;
        b.b0 = alpha / a0; b.b1 = 0.0; b.b2 = -alpha / a0;
        b.a1 = -2.0 * std::cos (w0) / a0; b.a2 = (1.0 - alpha) / a0;
        for (auto& f : band) f.copyCoeffs (b);
    }

    // ITU-R BS.1770 K-가중: 1단 고역 셸프(+4 dB) + 2단 고역 통과(RLB). 임의 샘플레이트용 공식 (libebur128 과 같음)
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan (juce::MathConstants<double>::pi * f0 / fs);
        const double Vh = std::pow (10.0, G / 20.0);
        const double Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        Biquad b;
        b.b0 = (Vh + Vb * K / Q + K * K) / a0;
        b.b1 = 2.0 * (K * K - Vh) / a0;
        b.b2 = (Vh - Vb * K / Q + K * K) / a0;
        b.a1 = 2.0 * (K * K - 1.0) / a0;
        b.a2 = (1.0 - K / Q + K * K) / a0;
        for (auto& f : kShelf) f.copyCoeffs (b);
    }
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan (juce::MathConstants<double>::pi * f0 / fs);
        const double a0 = 1.0 + K / Q + K * K;
        Biquad b;
        b.b0 = 1.0; b.b1 = -2.0; b.b2 = 1.0;
        b.a1 = 2.0 * (K * K - 1.0) / a0;
        b.a2 = (1.0 - K / Q + K * K) / a0;
        for (auto& f : kHigh) f.copyCoeffs (b);
    }

    bandAtt = coef (kBandAttackMs, fs);  bandRel = coef (kBandReleaseMs, fs);
    loudAtt = coef (kLoudAttackMs, fs);  loudRel = coef (kLoudReleaseMs, fs);
    gainAtt = coef (kGainAttackMs, fs);  gainRel = coef (kGainReleaseMs, fs);
    peakRel = coef (kPeakReleaseMs, fs);

    for (auto* s : { &sBandThr, &sBandRatio, &sBandMaxCut, &sLoudCeil, &sPeakCeil })
        s->reset (fs, kLevelRampSec);
}

void EarGuard::resetState()
{
    for (int c = 0; c < maxChannels; ++c)
    {
        band[c].reset();
        kShelf[c].reset();
        kHigh[c].reset();
    }
    bandEnv = loudEnv = 0.0;
    bandGainDb = fullGainDb = 0.0;
    peakGain = 1.0;
    bandActive = fullActive = false;
}

void EarGuard::setTargets (int lvl, bool immediate)
{
    const auto& p = paramsFor (lvl);
    if (immediate)
    {
        sBandThr.setCurrentAndTargetValue (p.bandThresholdDb);
        sBandRatio.setCurrentAndTargetValue (p.bandRatio);
        sBandMaxCut.setCurrentAndTargetValue (p.bandMaxCutDb);
        sLoudCeil.setCurrentAndTargetValue (p.loudCeilingDb);
        sPeakCeil.setCurrentAndTargetValue (p.peakCeilingDb);
    }
    else
    {
        sBandThr.setTargetValue (p.bandThresholdDb);
        sBandRatio.setTargetValue (p.bandRatio);
        sBandMaxCut.setTargetValue (p.bandMaxCutDb);
        sLoudCeil.setTargetValue (p.loudCeilingDb);
        sPeakCeil.setTargetValue (p.peakCeilingDb);
    }
}

//==============================================================================
void EarGuard::process (juce::AudioBuffer<float>& buffer, int numChannels, int numSamples, double sampleRate)
{
    numChannels = juce::jmin (numChannels, buffer.getNumChannels());
    if (numChannels <= 0 || numSamples <= 0)
        return;

    if (sampleRate > 0.0 && sampleRate != preparedRate)
    {
        computeCoefficients (sampleRate);   // 할당 없음
        resetState();
        appliedLevel = -1;
    }
    if (preparedRate <= 0.0)
        return;

    const int lvl = level.load (std::memory_order_relaxed);
    if (lvl != appliedLevel)
    {
        // 처음(또는 샘플레이트가 바뀐 뒤)에는 기준값을 바로 맞춘다. 그 밖에는 기준값을 100 ms 동안 옮겨 가며
        // 감쇠가 연속으로 바뀐다. 끔으로 가라앉아 쉬던 중이면 필터 상태를 비우고 끔 기준값에서 출발한다.
        const bool immediate = appliedLevel < 0;
        if (immediate || bypassed)
            resetState();
        setTargets (lvl, immediate);
        appliedLevel = lvl;
    }

    const bool smoothing = sBandThr.isSmoothing() || sLoudCeil.isSmoothing() || sPeakCeil.isSmoothing()
                        || sBandRatio.isSmoothing() || sBandMaxCut.isSmoothing();
    if (lvl == (int) Level::Off && ! smoothing && bandGainDb == 0.0 && fullGainDb == 0.0 && peakGain == 1.0)
    {
        bypassed = true;   // 끔: 손대지 않는다
        return;
    }
    bypassed = false;

    const int nf = juce::jmin (numChannels, maxChannels);   // 대역 필터를 거치는 채널
    float* const* ch = buffer.getArrayOfWritePointers();
    double bp[maxChannels];

    for (int s = 0; s < numSamples; ++s)
    {
        const double thrB    = sBandThr.getNextValue();
        const double ratio   = juce::jmax (1.0f, sBandRatio.getNextValue());
        const double maxCut  = sBandMaxCut.getNextValue();
        const double loudC   = sLoudCeil.getNextValue();
        const double peakC   = sPeakCeil.getNextValue();

        // 1) 대역 레벨 (채널 중 최대 → 모든 채널 같은 게인)
        double bmax = 0.0;
        for (int c = 0; c < nf; ++c)
        {
            float& x = ch[c][s];
            if (! std::isfinite (x)) x = 0.0f;
            bp[c] = band[c].tick ((double) x);
            bmax = juce::jmax (bmax, bp[c] * bp[c]);
        }
        bandEnv += (bmax > bandEnv ? bandAtt : bandRel) * (bmax - bandEnv);
        const double overB = 10.0 * std::log10 (bandEnv + 1.0e-20) - thrB;
        const double targetB = overB > 0.0 ? -juce::jmin (overB * (1.0 - 1.0 / ratio), maxCut) : 0.0;
        bandGainDb += (targetB < bandGainDb ? gainAtt : gainRel) * (targetB - bandGainDb);
        if (bandGainDb > -1.0e-4) bandGainDb = 0.0;   // 기준 아래: 정확히 원래 소리 (y == x)
        const double k = bandGainDb < 0.0 ? 1.0 - dbToGain (bandGainDb) : 0.0;

        // 2) 대역 처리한 소리로 K-가중 레벨과 피크를 잰다
        double kmax = 0.0, peak = 0.0;
        for (int c = 0; c < nf; ++c)
        {
            const double y = (double) ch[c][s] - k * bp[c];
            ch[c][s] = (float) y;
            const double kw = kHigh[c].tick (kShelf[c].tick (y));
            kmax = juce::jmax (kmax, kw * kw);
            peak = juce::jmax (peak, std::abs (y));
        }
        for (int c = nf; c < numChannels; ++c)
            peak = juce::jmax (peak, (double) std::abs (ch[c][s]));

        loudEnv += (kmax > loudEnv ? loudAtt : loudRel) * (kmax - loudEnv);
        const double overL = -0.691 + 10.0 * std::log10 (loudEnv + 1.0e-20) - loudC;
        const double targetL = overL > 0.0 ? -overL : 0.0;   // 상한: 넘은 만큼 그대로 줄인다
        fullGainDb += (targetL < fullGainDb ? gainAtt : gainRel) * (targetL - fullGainDb);
        if (fullGainDb > -1.0e-4) fullGainDb = 0.0;
        const double gL = fullGainDb < 0.0 ? dbToGain (fullGainDb) : 1.0;

        // 3) 피크 상한 (미리보기 없이 즉시)
        const double ceiling = dbToGain (peakC);
        const double need = peak * gL > ceiling ? ceiling / (peak * gL) : 1.0;
        peakGain = juce::jmin (need, peakGain + peakRel * (1.0 - peakGain));
        if (peakGain > 0.99999 && need >= 1.0) peakGain = 1.0;

        const double total = gL * peakGain;
        if (total < 1.0)
            for (int c = 0; c < numChannels; ++c)
                ch[c][s] = (float) (ch[c][s] * total);

        // 동작 횟수
        if (! bandActive && bandGainDb < kTriggerOnDb)       { bandActive = true; bandTriggers.fetch_add (1, std::memory_order_relaxed); }
        else if (bandActive && bandGainDb > kTriggerOffDb)   bandActive = false;
        const double fullDb = total < 1.0 ? 20.0 * std::log10 (juce::jmax (total, 1.0e-10)) : 0.0;
        if (! fullActive && fullDb < kTriggerOnDb)           { fullActive = true; fullTriggers.fetch_add (1, std::memory_order_relaxed); }
        else if (fullActive && fullDb > kTriggerOffDb)       fullActive = false;
    }
}

} // namespace meon
