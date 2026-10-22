// MEON core - 멤버별 P2P 상태 (버그 #2·#3). 엔진 통계를 시간에 따라 보고 판정한다. 시각은 인자로 받는다 (테스트).
//   연결 중: 서버 멤버 목록에 있는데 아직 엔진 피어가 붙지 않음
//   연결 실패: 15초 안에 붙지 않음 (방화벽·NAT)
//   연결됨: 붙음
//   끊김(소리 안 옴): 붙어 있고, 상대가 보내는 중(recvActive)이고, 내가 음소거하지 않았는데 받은 패킷 수가 3초 동안 그대로
// "연결됨 → 끊김" 마다 dropCount + 1.
#pragma once

#include "Engine.h"
#include <optional>

namespace meon::core
{

class PeerState
{
public:
    enum class State { connecting, connected, interrupted, failed };
    enum class Change { none, connected, interrupted, resumed, failed };

    static constexpr juce::int64 interruptMs = 3000;
    static constexpr juce::int64 joinTimeoutMs = 15000;

    explicit PeerState (juce::int64 firstSeenMs) : since (firstSeenMs) {}

    /** 주기마다. peer = 엔진에 그 사용자의 피어가 있으면 그 통계 */
    Change update (juce::int64 now, const std::optional<EnginePeer>& peer, bool mutedByMe)
    {
        if (! peer || ! peer->connected)
        {
            if (state == State::connecting && now - since >= joinTimeoutMs)
            {
                state = State::failed;
                return Change::failed;
            }
            return Change::none;
        }

        if (state == State::connecting || state == State::failed)
        {
            state = State::connected;
            lastPackets = peer->packetsReceived;
            lastChangeAt = now;
            return Change::connected;
        }

        const bool flowing = peer->packetsReceived != lastPackets;
        if (flowing)
        {
            lastPackets = peer->packetsReceived;
            lastChangeAt = now;
        }

        const bool expectAudio = peer->recvActive && ! mutedByMe;
        if (state == State::connected)
        {
            if (! expectAudio) { lastChangeAt = now; return Change::none; }
            if (! flowing && now - lastChangeAt >= interruptMs)
            {
                state = State::interrupted;
                ++drops;
                return Change::interrupted;
            }
            return Change::none;
        }

        // interrupted
        if (flowing || ! expectAudio)
        {
            state = State::connected;
            lastChangeAt = now;
            return Change::resumed;
        }
        return Change::none;
    }

    State get() const { return state; }
    int dropCount() const { return drops; }
    /** 서버 멤버 목록에서 처음 본 때 (입장, 또는 그 멤버가 들어온 때) */
    juce::int64 firstSeenMs() const { return since; }

private:
    State state = State::connecting;
    juce::int64 since, lastChangeAt = 0, lastPackets = -1;
    int drops = 0;
};

} // namespace meon::core
