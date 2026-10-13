// MEON ui - 레벨 미터 (Logic 스타일, 디자인의 mt()).
// 상자: 높이 19, 모서리 5, 테두리 #E0E0E0, 면 #FCFCFC, 안쪽 여백 5·9 → 막대 높이 7 (2채널이면 3 + 1 + 3).
// 막대: 초록(≤ −18) → 노랑(−6) → 주황 → 빨강(0 dB) 그라데이션 위에 남은 부분을 #E5E5EA 로 덮는다.
// 감쇠 26 dB/s, 피크 1.5초 유지 뒤 10 dB/s. 0 dBFS 에 닿으면 클리핑(빨강)으로 남고 클릭하면 지운다 (Q11 잠정안).
#pragma once

#include "../theme/Tokens.h"

namespace meon::ui
{

class Meter : public juce::Component, public juce::SettableTooltipClient
{
public:
    /** 채널 수와 채널별 현재 피크(선형, 1.0 = 0 dBFS). 화면 주사율로 부른다 */
    using Source = std::function<float (int channel)>;

    Meter();
    void setSource (int channels, Source);
    void setActive (bool);   // 꺼지면 소리가 없는 것으로 (−60 으로 내려감)
    bool isClipped() const { return clipped; }
    void resetClip();

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

    /** DB_MAP: −60→0, −18→0.5, −6→0.75, 0→0.9, +6→1 */
    static float dbPos (float db);
    /** M_STOPS 그라데이션의 색 (위치 0~1) */
    static juce::Colour colourAt (float pos);

    /** 테스트용: 시간을 직접 넣어 한 단계 진행 */
    void step (double nowMs);
    float getDb (int ch) const { return ch < (int) chans.size() ? chans[(size_t) ch].db : -60.0f; }
    float getPeak (int ch) const { return ch < (int) chans.size() ? chans[(size_t) ch].peak : -60.0f; }

private:
    struct Chan { float db = -60.0f, peak = -60.0f; double peakAt = 0.0; };
    std::vector<Chan> chans;
    Source source;
    bool active = true, clipped = false;
    double lastMs = 0.0;
    juce::VBlankAttachment vblank;
};

/** 미터 상자 (높이 19, 테두리 #E0E0E0, 면 #FCFCFC, 모서리 5, 여백 5·9, 막대 7px) — 멤버 행·내 오디오·모니터 */
class MeterBox : public juce::Component
{
public:
    explicit MeterBox (const juce::String& tooltip = {}) { addAndMakeVisible (meter); meter.setTooltip (tooltip); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (col::softBg);
        g.fillRoundedRectangle (r, metric::smallRadius);
        g.setColour (col::line);
        g.drawRoundedRectangle (r.reduced (0.5f), metric::smallRadius, 1.0f);
    }
    void resized() override { meter.setBounds (getLocalBounds().reduced (9, 0).withSizeKeepingCentre (getWidth() - 18, 7)); }
    Meter meter;
};

} // namespace meon::ui
