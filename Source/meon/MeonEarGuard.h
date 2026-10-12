// MEON - 귀 테러 방지. 내 모니터 최종 출력(멤버 믹스·마스터 볼륨 적용 뒤, 출력 장치 직전)에서 갑자기 큰 소리를 누른다.
// 내가 보내는 소리에는 적용하지 않는다. 미리보기(lookahead) 없음 → 지연 0. 모든 채널을 한 게인으로 연동한다.
//
//   1) 대역 리미터: 2–5 kHz 대역 레벨이 기준을 넘으면 그 대역만 판다 (y = x − (1−g)·BP(x)). g = 1 이면 y == x.
//   2) 전체 리미터: 그래도 K-가중 레벨(BS.1770)이 상한을 넘거나 샘플 피크가 상한을 넘으면 전체 게인을 낮춘다.
//
// 오디오 스레드에서 메모리 할당·잠금 없음. 단계 전환은 기준값을 ~100 ms 동안 옮겨 가며 부드럽게 바꾼다.
#pragma once

#include <JuceHeader.h>

namespace meon
{

class EarGuard
{
public:
    enum class Level { Off = 0, Normal = 1, Strong = 2 };

    /** 단계별 기준값. 초기값이며 직접 들어 보고 조정한다 (MeonEarGuard.cpp 의 kNormal / kStrong). */
    struct Params
    {
        float bandThresholdDb;   // 2–5 kHz 대역 레벨(RMS, dBFS) 기준. 넘은 만큼만 판다
        float bandRatio;         // 기준을 넘은 양 대비 누르는 비율 (∞ 에 가까울수록 리미터)
        float bandMaxCutDb;      // 대역을 최대 몇 dB 까지 팔지
        float loudCeilingDb;     // K-가중 레벨(LKFS 단위, 짧은 창) 상한
        float peakCeilingDb;     // 샘플 피크 상한 (dBFS)
    };

    static constexpr int maxChannels = 16;   // 이보다 많은 채널은 전체 게인만 적용 (대역 처리 없음)

    EarGuard();

    /** 메시지 스레드. 처음 한 번은 바로 적용, 이후는 오디오 스레드에서 부드럽게 넘어간다. */
    void setLevel (Level l)   { level.store ((int) l); }
    Level getLevel() const    { return (Level) level.load(); }

    /** 오디오 스레드가 아닐 때 (prepareToPlay). 샘플레이트가 바뀌면 process 안에서도 다시 계산한다 (할당 없음). */
    void prepare (double sampleRate);

    /** 오디오 스레드. 채널 [0, numChannels) 를 제자리 처리. */
    void process (juce::AudioBuffer<float>& buffer, int numChannels, int numSamples, double sampleRate);

    /** 동작 횟수 (앱이 켜진 뒤 누적). 줄이기 시작한 순간을 1 번으로 센다. */
    int getBandTriggerCount() const { return bandTriggers.load(); }
    int getFullTriggerCount() const { return fullTriggers.load(); }

    static juce::String levelKey (Level l);   // 로그용 "off" "normal" "strong"

private:
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1 = 0, z2 = 0;
        inline double tick (double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void reset() noexcept { z1 = z2 = 0; }
        void copyCoeffs (const Biquad& o) noexcept { b0 = o.b0; b1 = o.b1; b2 = o.b2; a1 = o.a1; a2 = o.a2; }
    };

    std::atomic<int> level { (int) Level::Normal };
    std::atomic<int> bandTriggers { 0 }, fullTriggers { 0 };

    // 오디오 스레드 전용 상태
    double preparedRate = 0.0;
    Biquad band[maxChannels], kShelf[maxChannels], kHigh[maxChannels];
    double bandEnv = 0.0, loudEnv = 0.0;         // 파워 엔벨로프 (채널 중 최대)
    double bandGainDb = 0.0, fullGainDb = 0.0;   // 적용 중인 감쇠 (0 이하)
    double peakGain = 1.0;
    bool bandActive = false, fullActive = false;  // 동작 횟수 세기 (히스테리시스)
    bool bypassed = true;                         // 끔 상태로 완전히 가라앉았으면 처리하지 않는다
    int appliedLevel = -1;
    juce::SmoothedValue<float> sBandThr, sBandRatio, sBandMaxCut, sLoudCeil, sPeakCeil;

    // 엔벨로프·게인 계수 (prepare 에서 계산)
    double bandAtt = 0, bandRel = 0, loudAtt = 0, loudRel = 0, gainAtt = 0, gainRel = 0, peakRel = 0;

    void computeCoefficients (double sampleRate);
    void resetState();
    void setTargets (int lvl, bool immediate);
};

} // namespace meon
