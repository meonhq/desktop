// MEON core - 입력 무음 경고 (추가 반영 4, REMAKE_PLAN 6.3).
//   조건: 방 안 + 송출 정지가 아님 + 입력 채널 ≥ 1 + 입력 처리 뒤 레벨이 5초 내내 −60 dBFS 이하 → 경고
//   −50 dBFS 를 넘으면 바로 해제. 송출 정지를 켜거나 조건이 깨지면 해제하고 처음부터 센다.
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

    enum class Change { none, started, ended };

    /** levelDb: 그 순간의 입력 피크 (채널 중 최대) */
    Change update (juce::int64 now, bool inRoom, bool sendMuted, int inputChannels, float levelDb)
    {
        const bool eligible = inRoom && ! sendMuted && inputChannels >= 1;
        if (! eligible)
        {
            quietSince = -1;
            return setWarning (false, now);
        }
        if (warning)
        {
            if (levelDb > clearDb) { quietSince = -1; return setWarning (false, now); }
            return Change::none;
        }
        if (levelDb > silentDb) { quietSince = -1; return Change::none; }
        if (quietSince < 0) quietSince = now;
        if (now - quietSince >= holdMs) return setWarning (true, now);
        return Change::none;
    }

    bool isWarning() const { return warning; }
    /** 마지막 경고의 지속 시간 (ended 직후 로그용) */
    juce::int64 lastDurationMs() const { return lastDuration; }

private:
    bool warning = false;
    juce::int64 quietSince = -1, startedAt = 0, lastDuration = 0;

    Change setWarning (bool on, juce::int64 now)
    {
        if (on == warning) return Change::none;
        warning = on;
        if (on) { startedAt = now; return Change::started; }
        lastDuration = now - startedAt;
        return Change::ended;
    }
};

} // namespace meon::core
