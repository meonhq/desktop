// MEON core - 예상 지연 보정 자료 (API 2.11, 5.10).
//   방에 들어가 방장과 P2P 가 연결되면 그때부터 10초 동안 1초마다 방장 핑(멤버 행의 ms 와 같은 왕복 시간)을 모아
//   중앙값을 입장 한 번에 한 번 보낸다.
//   보내지 않는 경우: 내가 방장, 방장이 방에 없음, 예상 지연을 계산할 값(내 RTT·방장 RTT)이 없음,
//   10초가 차기 전에 방장 연결이 끊기거나 내가 나감, 모은 값이 없음 (0 은 아직 못 잰 값이라 뺀다).
//   같은 방에 다시 들어가면 그 입장에서 다시 잰다 (입장마다 망 상태가 달라 각각 쓸 만한 표본이다).
#pragma once

#include "../api/ApiTypes.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace meon::core
{

class LatencyProbe
{
public:
    struct Report
    {
        juce::String roomId;
        int estimatedMs = 0, myRttMs = 0, hostRttMs = 0, actualP2pRttMs = 0;
    };

    static constexpr int windowMs = 10000;
    static constexpr int sampleEveryMs = 1000;

    /** 입장할 때. estimated 는 입장 시점의 round(계수 × (내 RTT + 방장 RTT)) */
    void begin (const juce::String& roomId, const juce::String& hostUser, bool iAmHost, bool hostPresent,
                OptInt estimatedMs, OptInt myRttMs, OptInt hostRttMs)
    {
        reset();
        if (iAmHost || ! hostPresent || hostUser.isEmpty() || ! estimatedMs || ! myRttMs || ! hostRttMs) return;
        armed = true;
        report.roomId = roomId;
        report.estimatedMs = *estimatedMs;
        report.myRttMs = *myRttMs;
        report.hostRttMs = *hostRttMs;
        host = hostUser;
    }

    void reset()
    {
        armed = started = false;
        samples.clear();
        report = {};
        host = {};
    }

    bool isArmed() const                 { return armed; }
    const juce::String& hostUser() const { return host; }

    /** 매 tick: 방장 피어가 연결돼 있는지와 그 핑. 10초가 차면 보낼 값을 한 번 돌려준다 */
    std::optional<Report> update (juce::int64 nowMs, bool hostConnected, float hostPingMs)
    {
        if (! armed) return std::nullopt;
        if (! hostConnected)
        {
            if (started) reset();   // 재는 도중에 끊기면 이번 입장은 보내지 않는다
            return std::nullopt;
        }
        if (! started)
        {
            started = true;
            startMs = nowMs;
            lastSampleMs = nowMs - sampleEveryMs;
        }
        if (nowMs - lastSampleMs >= sampleEveryMs && hostPingMs > 0.0f)
        {
            samples.push_back (hostPingMs);
            lastSampleMs = nowMs;
        }
        if (nowMs - startMs < windowMs) return std::nullopt;
        armed = false;
        if (samples.empty()) return std::nullopt;
        auto r = report;
        r.actualP2pRttMs = median (samples);
        return r;
    }

    static int median (std::vector<float> v)
    {
        if (v.empty()) return 0;
        std::sort (v.begin(), v.end());
        const auto n = v.size();
        const double m = n % 2 == 1 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
        return (int) std::lround (m);
    }

private:
    bool armed = false, started = false;
    juce::int64 startMs = 0, lastSampleMs = 0;
    std::vector<float> samples;
    Report report;
    juce::String host;
};

} // namespace meon::core
