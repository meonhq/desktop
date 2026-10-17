#include "FoldAnimator.h"
#include "../theme/Tokens.h"
#include "../../MeonReduceMotion.h"
#include "../../core/log/AppLog.h"

namespace meon::ui
{

core::CubicBezier FoldAnimator::ease()
{
    return { motion::foldEase[0], motion::foldEase[1], motion::foldEase[2], motion::foldEase[3] };
}

FoldAnimator::FoldAnimator (juce::Component& h, std::function<void()> f)
    : onFrame (std::move (f)), vblank (&h, [this] { frame(); })
{
}

FoldAnimator::~FoldAnimator() { stopTimer(); }

int FoldAnimator::add (const juce::String& name, bool open)
{
    panels.push_back ({ name, core::FoldProgress (open, (double) motion::foldDurationMs, ease()) });
    return (int) panels.size() - 1;
}

float FoldAnimator::value (int panel) const
{
    return (float) panels[(size_t) panel].progress.value (juce::Time::getMillisecondCounterHiRes());
}

bool FoldAnimator::anyMoving() const
{
    for (auto& p : panels)
        if (p.progress.isMoving()) return true;
    return false;
}

void FoldAnimator::setOpen (int panel, bool open)
{
    auto& p = panels[(size_t) panel].progress;
    const double now = juce::Time::getMillisecondCounterHiRes();
    const bool animate = ! systemPrefersReducedMotion();
    const bool wasMoving = anyMoving();
    p.setTarget (open, now, animate);
    if (p.isMoving())
    {
        if (! wasMoving) { lastFrameMs = now; startTimerHz (60); }
        beginRun (panels[(size_t) panel].name, open, now);
    }
    if (onFrame) onFrame();
}

void FoldAnimator::snap (int panel, bool open)
{
    panels[(size_t) panel].progress.setTarget (open, juce::Time::getMillisecondCounterHiRes(), false);
    if (onFrame) onFrame();
}

void FoldAnimator::freeze (int panel, bool open, float e)
{
    panels[(size_t) panel].progress.freeze (open, (double) e);
    if (onFrame) onFrame();
}

void FoldAnimator::timerCallback()
{
    // VBlank 가 오지 않을 때만 (창이 아직 없거나 가려져 VBlank 가 멈춘 경우)
    if (juce::Time::getMillisecondCounterHiRes() - lastFrameMs > 50.0) frame();
}

void FoldAnimator::frame()
{
    if (! anyMoving()) return;
    const double now = juce::Time::getMillisecondCounterHiRes();
    lastFrameMs = now;
    bool still = false;
    for (auto& p : panels)
        still = p.progress.update (now) || still;

    const double t0 = juce::Time::getMillisecondCounterHiRes();
    if (onFrame) onFrame();
    const double layoutMs = juce::Time::getMillisecondCounterHiRes() - t0;

    if (frames > 0)
    {
        const double gap = now - prevFrameMs;
        sumGap += gap;
        maxGap = juce::jmax (maxGap, gap);
        if (gap > 25.0) ++longFrames;   // 60Hz 한 프레임(16.7ms)의 1.5배를 넘으면 끊긴 것으로 센다
    }
    prevFrameMs = now;
    ++frames;
    maxLayout = juce::jmax (maxLayout, layoutMs);
    sumLayout += layoutMs;

    if (! still)
    {
        stopTimer();
        endRun (now);
    }
}

void FoldAnimator::beginRun (const juce::String& name, bool open, double now)
{
    const auto entry = name + (open ? TXT (" 펼침") : TXT (" 접힘"));
    if (runNames.isEmpty())
    {
        frames = longFrames = 0;
        sumGap = maxGap = maxLayout = sumLayout = 0.0;
        runStartMs = prevFrameMs = now;
    }
    runNames.add (entry);
}

void FoldAnimator::endRun (double now)
{
   #if MEON_DEV_OPTIONS
    if (frames > 1)
        core::AppLog::write ("ui", LOGTXT ("접기 애니메이션 ") + runNames.joinIntoString (", ")
                                   + LOGTXT (": ") + juce::String (now - runStartMs, 0) + LOGTXT ("ms, 프레임 ") + juce::String (frames)
                                   + LOGTXT ("개, 간격 평균 ") + juce::String (sumGap / (frames - 1), 1)
                                   + LOGTXT ("ms 최대 ") + juce::String (maxGap, 1)
                                   + LOGTXT ("ms, 25ms 넘은 간격 ") + juce::String (longFrames)
                                   + LOGTXT ("번, 배치 평균 ") + juce::String (sumLayout / frames, 1)
                                   + LOGTXT ("ms 최대 ") + juce::String (maxLayout, 1) + "ms");
   #else
    juce::ignoreUnused (now);
   #endif
    runNames.clear();
}

} // namespace meon::ui
