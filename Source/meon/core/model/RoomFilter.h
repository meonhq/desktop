// MEON core - 예상 지연 (API 2.11), 입장 가능 판정 (5.5 정원 규칙), 홈 필터 (5.2).
#pragma once

#include "../api/ApiTypes.h"

namespace meon::core
{

namespace latency
{
    /** round(coefficient × (myRtt + hostRtt)). 둘 중 하나라도 모르면 없음 */
    inline OptInt estimate (double coefficient, OptInt myRttMs, OptInt hostRttMs)
    {
        if (! myRttMs || ! hostRttMs) return std::nullopt;
        return (int) std::lround (coefficient * (double) (*myRttMs + *hostRttMs));
    }
}

namespace rooms
{
    /** 방장이 아닌 내가 지금 들어갈 수 있는 인원인지 (5.5-5). 방장 본인은 항상 true */
    inline bool hasRoomFor (const Room& r, const juce::String& myId)
    {
        if (r.hostId == myId) return true;
        const int limit = r.hostPresent ? r.capacity : r.capacity - 1;
        return r.memberCount < limit;
    }

    inline bool hasFriend (const Room& r)
    {
        for (auto& m : r.members)
            if (m.isFriend) return true;
        return false;
    }

    /** 고른 파트가 모두 멤버 중에 없는지 (AND) */
    inline bool lacksAllParts (const Room& r, const juce::StringArray& parts)
    {
        for (auto& p : parts)
            for (auto& m : r.members)
                if (m.user.part == p) return false;
        return true;
    }
}

struct RoomFilter
{
    juce::StringArray emptyParts;   // 비어 있는 파트 (다중)
    int maxLatencyMs = 0;           // 0 = 전체, 10, 20
    bool onlyWithRoom = false;      // 빈자리 있는 방만
    bool onlyWithFriends = false;   // 친구가 있는 방만

    bool isActive() const { return ! emptyParts.isEmpty() || maxLatencyMs > 0 || onlyWithRoom || onlyWithFriends; }

    bool matches (const Room& r, const juce::String& myId, double coefficient, OptInt myRttMs) const
    {
        if (! emptyParts.isEmpty() && ! rooms::lacksAllParts (r, emptyParts)) return false;
        if (maxLatencyMs > 0)
        {
            const auto est = latency::estimate (coefficient, myRttMs, r.hostRttMs);
            if (! est || *est > maxLatencyMs) return false;
        }
        if (onlyWithRoom && ! rooms::hasRoomFor (r, myId)) return false;
        if (onlyWithFriends && ! rooms::hasFriend (r)) return false;
        return true;
    }

    int count (const std::vector<Room>& list, const juce::String& myId, double coefficient, OptInt myRttMs) const
    {
        int n = 0;
        for (auto& r : list)
            if (matches (r, myId, coefficient, myRttMs)) ++n;
        return n;
    }
};

} // namespace meon::core
