// MEON - 자동 지터 버퍼의 늘리기·줄이기 판단 (E12, docs/REMAKE_PLAN.md 8.2).
//
// SonobusAudioProcessor 의 AOO_BLOCK_LOST_EVENT·AOO_PING_EVENT 처리에서 "언제 늘리고 줄이나" 만 떼어 낸 것이다.
// 엔진이 그대로 부르고, 시험(NetBufAutoTests)도 같은 코드를 쓴다. 값은 엔진의 RemotePeer 가 갖고 있고 여기서는
// 참조로 읽고 고친다. AOO 버퍼 바꾸기(set_buffersize)·지연 계산·상대에게 알리기는 부르는 쪽이 한다.
// 시각은 ms (Time::getMillisecondCounterHiRes), 버퍼는 ms.
//
// 규칙 (늘리기·줄이기는 SonoBus 1.7.2 그대로, 바닥·되돌리기는 E12)
//   - 끊김(AOO 가 포기한 블록) 이벤트가 앞에 센 끊김과 0.5초 이하로 붙어 있으면 같은 끊김이다 (아무것도 안 함).
//   - 0.5초 넘게 떨어진 끊김의 빈도(1/간격)가 기준(기본 0.5/초 = 간격 2초 미만)을 넘으면 한 블록 늘린다.
//   - 처음 끊김(앞에 센 것이 없음)과 초기 자동(InitAuto)이 끝난 뒤의 끊김은 시각만 적는다.
//   - 지연 우선(AutoFull)은 2초마다 오는 핑에서: 10초 넘게 끊김이 없고, 마지막으로 줄인 때(또는 바닥에 있던 때)부터
//     10초가 넘었으면 한 블록 줄인다. 바닥 아래로는 가지 않는다.
//   - 되돌리기 (E12): 줄이기 시작한 뒤 처음 오는 끊김에서, 시간이 얼마나 지났든 바로 줄이기 전 값으로 되돌리고
//     그 값을 바닥으로 한다. 이 상대와 이어져 있는 동안 그 아래로는 다시 줄이지 않는다.
//
// E12 전의 문제: 바닥 조건이 "줄이기 간격 타이머" 를 봤는데, 이 타이머는 버퍼가 바닥에 있는 동안 핑마다 지금으로
// 바뀐다. 그래서 바닥에서 늘 때마다 늘 "줄인 지 2초 안" 이 되어 바닥이 따라 올랐고, 바닥에서 시작한 상대(처음 보는
// 상대는 1 ms 에서 시작)는 한 번 늘면 다시 줄지 않았다. 지연 우선이 안정 우선과 같게 움직였다.
// 되돌리기에 시간 창을 두면(SonoBus 원래 뜻은 2초), 창보다 긴 간격으로 되풀이되는 흔들림(와이파이 등)에서 잠잠할 때
// 줄였다가 다음 흔들림에 다시 크게 끊기기를 되풀이한다 (시험 모형에서 2초 창 19배, 60초 창도 3분 간격에서 15배).
// 그래서 창 없이 "줄인 뒤 첫 끊김" 으로 판단한다 (NetBufAutoTests).
#pragma once

#include <algorithm>

namespace meon
{

struct NetBufAuto
{
    float& bufferMs;       // RemotePeer::buffertimeMs
    float& floorMs;        // RemotePeer::netBufAutoBaseline  (지연 우선이 줄일 수 있는 바닥)
    double& lastDropMs;    // RemotePeer::lastDroptime         (마지막으로 센 끊김, 0: 아직 없음)
    double& shrinkTimerMs; // RemotePeer::lastNetBufDecrTime   (줄이기 간격. 바닥에 있으면 핑마다 지금으로)
    float& beforeShrinkMs; // RemotePeer::netBufBeforeShrink   (E12: 줄이기 시작하기 전 값, 0: 줄이는 중 아님)

    enum class Drop
    {
        same,     // 앞의 끊김과 같은 것 (0.5초 이하)
        counted,  // 센 끊김, 버퍼는 그대로
        grew,     // 센 끊김, bufferMs 가 한 블록 늘었다
        reverted  // 줄인 뒤 첫 끊김: 줄이기 전 값으로 되돌렸고 그 값이 바닥이 됐다
    };

    /** 끊김 이벤트 하나. latencyMode = 지연 우선(AutoFull), countOnly = 초기 자동이 끝남 (시각만 적는다) */
    Drop drop (double nowMs, float blockMs, float dropRateThreshPerSec, bool latencyMode, bool countOnly) noexcept
    {
        const bool first = lastDropMs <= 0;
        const double sec = (nowMs - lastDropMs) * 1e-3;
        if (! first && ! countOnly && sec <= 0.5)
            return Drop::same;
        lastDropMs = nowMs;

        if (latencyMode && beforeShrinkMs > bufferMs)
        {
            bufferMs = std::max (bufferMs + blockMs, beforeShrinkMs);
            floorMs = bufferMs;
            beforeShrinkMs = 0;
            return Drop::reverted;
        }
        const float rate = (float) (1.0 / sec); // 이번 끊김 하나를 한 번으로 본 빈도
        if (first || countOnly || ! (rate > dropRateThreshPerSec))
            return Drop::counted;
        bufferMs += blockMs;
        beforeShrinkMs = 0;
        return Drop::grew;
    }

    /** 지연 우선의 핑 (상대가 2초마다 보낸다). true 면 bufferMs 가 한 블록 줄었다 */
    bool ping (double nowMs, float blockMs, bool latencyMatched) noexcept
    {
        if (shrinkTimerMs > 0 && bufferMs > floorMs && ! latencyMatched)
        {
            if ((nowMs - shrinkTimerMs) * 1e-3 > 10.0 && (nowMs - lastDropMs) * 1e-3 > 10.0)
            {
                if (beforeShrinkMs <= 0)
                    beforeShrinkMs = bufferMs;
                bufferMs -= blockMs;
                bufferMs = std::max (bufferMs, floorMs);
                shrinkTimerMs = nowMs;
                return true;
            }
            return false;
        }
        shrinkTimerMs = nowMs;
        return false;
    }
};

} // namespace meon
