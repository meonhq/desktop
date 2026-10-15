// MEON core - 입력 무음 경고 (추가 반영 4, REMAKE_PLAN 6.3).
//   조건: 방 안 + 송출 정지가 아님 + 입력 채널 ≥ 1 + 입력 처리 뒤 레벨이 5초 내내 −60 dBFS 이하 → 경고 (silent)
//   −50 dBFS 를 넘으면 바로 해제. 송출 정지를 켜거나 조건이 깨지면 해제하고 처음부터 센다.
//   입력 채널이 0 (입력 장치가 없거나 켜진 입력 채널이 없음) 이면 레벨로 판정하지 않고, 그 상태가 5초 이어지면
//   noInput (로그만, 화면 안내는 2026-10-15 사용자 결정으로 없음). 장치가 생기면 바로 해제하고 무음 판정을 처음부터 센다.
// 시각과 레벨은 인자로 받는다 (테스트).
#pragma once

#include <JuceHeader.h>

namespace meon::core
{

class SilenceDetector
{
public:
    static constexpr juce::int64 holdMs = 5000;
    static constexpr float silentDb = -60.0f, clearDb = -50.0f;

    enum class Kind { none, silent, noInput };
    enum class Change { none, started, ended };

    /** levelDb: 그 순간의 입력 피크 (채널 중 최대). 경고 종류는 바로 바뀌지 않고 늘 none 을 거친다 */
    Change update (juce::int64 now, bool inRoom, bool sendMuted, int inputChannels, float levelDb)
    {
        if (! inRoom || sendMuted)
        {
            quietSince = missingSince = -1;
            return set (Kind::none, now);
        }
        if (inputChannels < 1)
        {
            quietSince = -1;
            if (missingSince < 0) missingSince = now;   // 장치가 없어진 순간부터 센다
            if (kind == Kind::silent) return set (Kind::none, now);
            if (kind == Kind::noInput) return Change::none;
            return now - missingSince >= holdMs ? set (Kind::noInput, now) : Change::none;
        }
        missingSince = -1;
        if (kind == Kind::noInput)
        {
            quietSince = levelDb > silentDb ? -1 : now;   // 장치가 생긴 순간부터 무음을 센다
            return set (Kind::none, now);
        }
        if (kind == Kind::silent)
        {
            if (levelDb > clearDb) { quietSince = -1; return set (Kind::none, now); }
            return Change::none;
        }
        if (levelDb > silentDb) { quietSince = -1; return Change::none; }
        if (quietSince < 0) quietSince = now;
        return now - quietSince >= holdMs ? set (Kind::silent, now) : Change::none;
    }

    Kind warningKind() const { return kind; }
    bool isWarning() const { return kind != Kind::none; }
    /** 마지막 경고의 지속 시간 (ended 직후 로그용) */
    juce::int64 lastDurationMs() const { return lastDuration; }
    /** 마지막으로 끝난 경고의 종류 (ended 직후 로그용) */
    Kind lastKind() const { return endedKind; }

private:
    Kind kind = Kind::none, endedKind = Kind::none;
    juce::int64 quietSince = -1, missingSince = -1, startedAt = 0, lastDuration = 0;

    Change set (Kind k, juce::int64 now)
    {
        if (k == kind) return Change::none;
        const auto before = kind;
        kind = k;
        if (k != Kind::none) { startedAt = now; return Change::started; }
        endedKind = before;
        lastDuration = now - startedAt;
        return Change::ended;
    }
};

} // namespace meon::core
