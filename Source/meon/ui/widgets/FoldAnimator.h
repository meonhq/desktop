// MEON ui - 화면 하나의 접는 패널들(채팅·친구·내 오디오·모니터, 홈 친구)의 접기·펼치기 진행값을 프레임마다 움직인다.
//   진행값 계산은 core::FoldProgress (길이 motion::foldDurationMs, 곡선 motion::foldEase).
//   프레임은 VBlankAttachment 로 받고, VBlank 가 50ms 넘게 오지 않으면(창 없음 등) 60Hz Timer 가 대신 돈다.
//   경과 시간은 Time::getMillisecondCounterHiRes 기준 (프레임 수로 세지 않는다).
//   운영체제의 동작 줄이기가 켜져 있으면 애니메이션 없이 바로 바꾼다.
//   빌드 0(MEON_DEV_OPTIONS)에서는 애니메이션이 끝날 때마다 프레임 간격을 앱 로그에 한 줄 남긴다.
#pragma once

#include <JuceHeader.h>
#include "../../core/util/Fold.h"

namespace meon::ui
{

class FoldAnimator : private juce::Timer
{
public:
    /** onFrame: 움직이는 동안 매 프레임, 그리고 끝난 프레임에 한 번 더 (보통 host.resized()) */
    FoldAnimator (juce::Component& host, std::function<void()> onFrame);
    ~FoldAnimator() override;

    /** 패널 하나를 더한다. 돌려준 번호로 부른다 */
    int add (const juce::String& name, bool open);

    /** 누름: 동작 줄이기가 아니면 지금 e 에서 목표 쪽으로 움직인다 */
    void setOpen (int panel, bool open);
    bool isOpen (int panel) const { return panels[(size_t) panel].progress.isOpenTarget(); }
    /** 진행값 e (접힘 0 ~ 펼침 1) */
    float value (int panel) const;
    bool isMoving (int panel) const { return panels[(size_t) panel].progress.isMoving(); }
    bool anyMoving() const;

    /** 애니메이션 없이 바로 (개발용 비교 스냅숏) */
    void snap (int panel, bool open);
    /** 개발용 스냅숏: 접히는(open=false)·펼쳐지는 중 e 에 멈춘다 (--screen=jam:fold=chat:0.25) */
    void freeze (int panel, bool open, float e);

    static core::CubicBezier ease();

private:
    struct Panel
    {
        juce::String name;
        core::FoldProgress progress;
    };
    std::function<void()> onFrame;
    std::vector<Panel> panels;
    juce::VBlankAttachment vblank;
    double lastFrameMs = 0.0;

    // 프레임 간격 기록 (빌드 0)
    juce::StringArray runNames;
    int frames = 0, longFrames = 0;
    double runStartMs = 0.0, prevFrameMs = 0.0, sumGap = 0.0, maxGap = 0.0, maxLayout = 0.0, sumLayout = 0.0;

    void frame();
    void timerCallback() override;
    void beginRun (const juce::String& name, bool open, double now);
    void endRun (double now);
};

} // namespace meon::ui
