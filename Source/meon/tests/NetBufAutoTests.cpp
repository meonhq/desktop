#include "../MeonNetBufAuto.h"

#include <JuceHeader.h>

#include <cmath>
#include <functional>
#include <memory>
#include <random>
#include <vector>

namespace meon
{

// E12: 자동 지터 버퍼의 늘리기·줄이기 판단 (엔진이 부르는 코드 그대로)
class NetBufAutoTests : public juce::UnitTest
{
public:
    NetBufAutoTests() : juce::UnitTest ("NetBufAuto", "Engine") {}

    // 엔진의 RemotePeer 값들
    struct Peer
    {
        float bufferMs = 0, floorMs = 0;
        double lastDropMs = 0, shrinkTimerMs = 0;
        float beforeShrinkMs = 0;
        NetBufAuto rules() { return { bufferMs, floorMs, lastDropMs, shrinkTimerMs, beforeShrinkMs }; }
    };

    static constexpr float block = 1000.0f * 128 / 48000; // 48 kHz·128 샘플 블록 = 2.67 ms
    static constexpr double t0 = 1.0e6;                    // 엔진 시계는 0 이 아니다 (0 = "아직 없음")

    // ---- 네트워크 흔들림 → AOO 싱크 모형 ----------------------------------------------------------
    // - 블록 i 는 i × block 에 보내고 delay(i) ms 뒤에 도착한다 (음수: 오지 않는 패킷).
    // - 버퍼를 (다시) 만든 뒤 처음 보낸 블록의 지연이 재생 기준이 되고, 그보다 버퍼만큼 더 늦게 오는 블록은 잃는다
    //   (AOO 는 다시 만든 시각부터 재생 시각을 센다. 버퍼는 블록 단위로 올림, 최소 한 블록: sink.cpp do_update).
    // - 버퍼 크기를 바꾸면 AOO 는 버퍼 전체를 무음으로 다시 채운다: 그 길이만큼 끊긴다.
    // - 잃은 블록은 그 시각에 끊김 이벤트, 상대의 핑은 2초마다 (AOO 소스는 시작하자마자 첫 핑을 보낸다).
    // - 안정 우선(AutoIncreaseOnly)은 줄이지 않는다. E12 전의 지연 우선은 처음 보는 상대에서 안정 우선과 똑같이 움직였다.
    struct Result
    {
        int grew = 0, shrank = 0, reverted = 0, lostBlocks = 0, lostAfterCalm = 0;
        double lostLate = 0;     // 뒤쪽 3분의 1 동안 잃은 블록 (끊김이 되풀이되는지)
        double gapMs = 0;        // 잃은 블록 + 다시 채운 무음
        double events = 0;       // 소리가 끊긴 횟수 (붙어 있는 끊김은 하나)
        double meanBufferMs = 0; // 실제 버퍼(블록 올림)의 시간 평균
        double meanDelayMs = 0;  // 보낸 때부터 재생까지 실제 지연의 시간 평균 (= 재생 기준 블록의 망 지연 + 버퍼)
        float finalBufferMs = 0, maxBufferMs = 0;
    };

    static float effective (float ms) { return std::ceil (std::max (ms, block) / block - 1e-4f) * block; }

    static Result simulate (bool latency, double seconds, const std::function<double (int)>& delay, double calmFrom = 1e9)
    {
        Peer p;
        p.bufferMs = 1.0f;   // 처음 보는 상대 (mBufferTime 1 ms)
        p.floorMs = block;   // 바닥 = 한 블록 (setRemotePeerAutoresizeBufferMode)

        Result r;
        const int blocks = (int) (seconds * 1000.0 / block);
        double nextPingMs = t0;
        double anchorMs = std::abs (delay (0));
        float buf = effective (p.bufferMs);
        double area = 0, delayArea = 0;
        int silentUntil = -1;
        bool wasBad = false;

        for (int i = 0; i < blocks; ++i)
        {
            const double nowMs = t0 + i * (double) block;
            bool changed = false, lost = false;
            while (latency && nextPingMs <= nowMs)
            {
                if (p.rules().ping (nextPingMs, block, false)) { ++r.shrank; changed = true; }
                nextPingMs += 2000.0;
            }
            const double d = delay (i);
            if (! changed && (d < 0 || d > anchorMs + buf))
            {
                lost = true;
                ++r.lostBlocks;
                if (i * block * 1e-3 >= calmFrom) ++r.lostAfterCalm;
                if (i >= blocks * 2 / 3) r.lostLate += 1;
                const auto res = p.rules().drop (nowMs, block, 0.5f, latency, false);
                if (res == NetBufAuto::Drop::grew) { ++r.grew; changed = true; }
                if (res == NetBufAuto::Drop::reverted) { ++r.reverted; changed = true; }
            }
            if (changed)
            {
                buf = effective (p.bufferMs);
                r.gapMs += buf; // 다시 채운 무음
                anchorMs = std::abs (delay (i + 1));
                silentUntil = i + (int) std::lround (buf / block);
            }
            const bool bad = lost || changed || i <= silentUntil;
            if (bad && ! wasBad) r.events += 1;
            wasBad = bad;
            r.maxBufferMs = std::max (r.maxBufferMs, buf);
            area += buf;
            delayArea += anchorMs + buf;
        }
        r.gapMs += r.lostBlocks * block;
        r.meanBufferMs = area / blocks;
        r.meanDelayMs = delayArea / blocks;
        r.finalBufferMs = buf;
        return r;
    }

    // 난수: mt19937 의 원값만 쓴다 (표준 분포 클래스는 플랫폼마다 값이 달라서 mac·windows·linux 결과가 갈린다)
    static double uni (std::mt19937& g) { return (double) (g() & 0xffffffffu) * (1.0 / 4294967296.0); }
    static double expo (std::mt19937& g, double mean) { return -mean * std::log (1.0 - uni (g)); }

    // 씨앗마다 같은 지연 열 (지연 우선·안정 우선이 같은 망을 본다)
    using Shape = std::function<double (std::mt19937&, double /*초*/)>;
    static std::function<double (int)> network (Shape shape, unsigned seed)
    {
        auto rng = std::make_shared<std::mt19937> (seed);
        auto delays = std::make_shared<std::vector<double>>();
        return [shape, rng, delays] (int i)
        {
            while ((int) delays->size() <= i)
                delays->push_back (shape (*rng, (double) delays->size() * block * 1e-3));
            return (*delays)[(size_t) i];
        };
    }

    struct Pair { Result latency, stable; };

    // 씨앗 여러 개: 횟수는 합, 버퍼·지연·끊김은 씨앗당 평균. 씨앗마다 차이가 커서(같은 망 모양에서도 씨앗 30개 묶음끼리
    // 끊김 배율이 0.3~0.5 오간다) 30개를 쓴다.
    Pair compare (const juce::String& label, Shape shape, double seconds, double calmFrom = 1e9, int seeds = 30)
    {
        Pair sum;
        auto add = [] (Result& a, const Result& b)
        {
            a.grew += b.grew; a.shrank += b.shrank; a.reverted += b.reverted;
            a.lostBlocks += b.lostBlocks; a.lostAfterCalm += b.lostAfterCalm;
            a.gapMs += b.gapMs; a.events += b.events; a.lostLate += b.lostLate; a.meanBufferMs += b.meanBufferMs; a.meanDelayMs += b.meanDelayMs;
            a.finalBufferMs += b.finalBufferMs; a.maxBufferMs += b.maxBufferMs;
        };
        for (int s = 1; s <= seeds; ++s)
        {
            add (sum.latency, simulate (true, seconds, network (shape, (unsigned) s), calmFrom));
            add (sum.stable, simulate (false, seconds, network (shape, (unsigned) s), calmFrom));
        }
        for (auto* r : { &sum.latency, &sum.stable })
        {
            r->gapMs /= seeds; r->events /= seeds; r->lostLate /= seeds; r->meanBufferMs /= seeds; r->meanDelayMs /= seeds;
            r->finalBufferMs /= (float) seeds; r->maxBufferMs /= (float) seeds;
        }
        auto line = [&] (const juce::String& mode, const Result& r)
        {
            logMessage (label + " " + mode + t (": 지연 ") + juce::String (r.meanDelayMs, 2) + t (" ms, 버퍼 ")
                        + juce::String (r.meanBufferMs, 2) + t (" ms (끝 ") + juce::String (r.finalBufferMs, 2) + t ("), 끊김 ")
                        + juce::String (r.gapMs, 1) + " ms " + juce::String (r.events, 1) + t ("번 (씨앗당), 늘림 ")
                        + juce::String (r.grew) + t (", 줄임 ") + juce::String (r.shrank) + t (", 되돌림 ") + juce::String (r.reverted)
                        + " (" + juce::String (seeds) + t ("회 합)"));
        };
        line (t ("지연 우선(E12)"), sum.latency);
        line (t ("안정 우선(= E12 전 지연 우선)"), sum.stable);
        logMessage (label + t (": 지연 차이 ") + juce::String (sum.latency.meanDelayMs - sum.stable.meanDelayMs, 2) + t (" ms, 끊긴 시간 ")
                    + juce::String (sum.latency.gapMs / sum.stable.gapMs, 2) + t ("배, 끊긴 횟수 ")
                    + juce::String (sum.latency.events / sum.stable.events, 2) + t ("배, 뒤쪽 3분의 1 손실 ")
                    + juce::String (sum.latency.lostLate, 1) + " / " + juce::String (sum.stable.lostLate, 1));
        return sum;
    }

    static juce::String t (const char* utf8) { return juce::String::fromUTF8 (utf8); }

    // 끊김이 과하게 늘지 않는다: 끊긴 시간과 횟수 모두 안정 우선(= E12 전)의 limit 배 이하
    void expectDropoutsBounded (const Pair& r, double limit = 1.5)
    {
        expect (r.latency.gapMs <= limit * r.stable.gapMs,
                "gap " + juce::String (r.latency.gapMs) + " / " + juce::String (r.stable.gapMs));
        expect (r.latency.events <= limit * r.stable.events,
                "events " + juce::String (r.latency.events) + " / " + juce::String (r.stable.events));
    }

    void runTest() override
    {
        beginTest ("Drops closer than 0.5 s are one event; two events 0.5-2 s apart grow by one block");
        {
            Peer p;
            p.bufferMs = 5.0f;
            p.floorMs = block;
            expect (p.rules().drop (t0 + 1000, block, 0.5f, true, false) == NetBufAuto::Drop::counted); // 처음: 시각만
            expect (p.rules().drop (t0 + 1400, block, 0.5f, true, false) == NetBufAuto::Drop::same);
            expect (p.rules().drop (t0 + 1500, block, 0.5f, true, false) == NetBufAuto::Drop::same);    // 정확히 0.5초도 같은 끊김
            expect (p.rules().drop (t0 + 2600, block, 0.5f, true, false) == NetBufAuto::Drop::grew);    // 1.6초 뒤
            expectWithinAbsoluteError (p.bufferMs, 5.0f + block, 1e-4f);
            expect (p.rules().drop (t0 + 4700, block, 0.5f, true, false) == NetBufAuto::Drop::counted); // 2.1초 뒤: 늘리지 않음
            expect (p.rules().drop (t0 + 4800, block, 0.5f, true, true) == NetBufAuto::Drop::counted);  // 초기 자동 끝: 시각만
            expectEquals (p.lastDropMs, t0 + 4800);
        }

        beginTest ("Latency mode shrinks one block per 10 quiet seconds down to the floor");
        {
            Peer p;
            p.bufferMs = 4 * block;
            p.floorMs = block;
            p.lastDropMs = t0;
            int shrinks = 0;
            for (double at = t0; at <= t0 + 60000; at += 2000)
                if (p.rules().ping (at, block, false))
                    ++shrinks;
            expectEquals (shrinks, 3);
            expectWithinAbsoluteError (p.bufferMs, block, 1e-4f);
            expectWithinAbsoluteError (p.beforeShrinkMs, 4 * block, 1e-4f); // 줄이기 전 값을 기억한다

            Peer m; // 지연 맞추기 중에는 줄이지 않는다
            m.bufferMs = 4 * block;
            m.floorMs = block;
            m.shrinkTimerMs = t0;
            expect (! m.rules().ping (t0 + 30000, block, true));
            expectWithinAbsoluteError (m.bufferMs, 4 * block, 1e-4f);
        }

        beginTest ("E12: growing at the floor no longer drags the floor up");
        {
            // 바닥에서 시작한 상대: 핑마다 타이머가 지금으로 바뀌는 중에 늘어도 바닥은 그대로 (E12 전에는 따라 올랐다)
            Peer p;
            p.bufferMs = block;
            p.floorMs = block;
            p.rules().ping (t0, block, false);
            p.rules().ping (t0 + 2000, block, false);
            p.rules().drop (t0 + 2500, block, 0.5f, true, false);
            expect (p.rules().drop (t0 + 3500, block, 0.5f, true, false) == NetBufAuto::Drop::grew);
            expectWithinAbsoluteError (p.floorMs, block, 1e-4f);
            // 그래서 10초 동안 끊김이 없으면 다시 줄어든다
            bool shrank = false;
            for (double at = t0 + 4000; at <= t0 + 20000; at += 2000)
                shrank = p.rules().ping (at, block, false) || shrank;
            expect (shrank);
            expectWithinAbsoluteError (p.bufferMs, block, 1e-4f);
        }

        beginTest ("E12: the first drop after shrinking reverts at once, however late, and becomes the floor");
        {
            Peer p;
            p.bufferMs = 3 * block;
            p.floorMs = block;
            p.lastDropMs = t0;
            p.shrinkTimerMs = t0;
            expect (! p.rules().ping (t0 + 10000, block, false)); // 정확히 10초는 아직
            expect (p.rules().ping (t0 + 12000, block, false));
            expect (p.rules().ping (t0 + 24000, block, false));
            expectWithinAbsoluteError (p.bufferMs, block, 1e-4f);
            // 10분 뒤 끊김 한 번 (짝이 없어도) → 처음 줄이기 전 값으로 되돌리고 바닥
            expect (p.rules().drop (t0 + 624000, block, 0.5f, true, false) == NetBufAuto::Drop::reverted);
            expectWithinAbsoluteError (p.bufferMs, 3 * block, 1e-4f);
            expectWithinAbsoluteError (p.floorMs, 3 * block, 1e-4f);
            bool shrankAgain = false;
            for (double at = t0 + 626000; at <= t0 + 900000; at += 2000)
                shrankAgain = p.rules().ping (at, block, false) || shrankAgain;
            expect (! shrankAgain); // 그 아래로는 다시 줄이지 않는다

            // 그 위로 늘었다가 줄인 뒤의 끊김은 새로 줄이기 시작한 값으로 되돌린다
            p.rules().drop (t0 + 901000, block, 0.5f, true, false);
            expect (p.rules().drop (t0 + 902000, block, 0.5f, true, false) == NetBufAuto::Drop::grew);
            expect (p.rules().ping (t0 + 914000, block, false));
            expectWithinAbsoluteError (p.bufferMs, 3 * block, 1e-4f);
            expect (p.rules().drop (t0 + 1000000, block, 0.5f, true, false) == NetBufAuto::Drop::reverted);
            expectWithinAbsoluteError (p.floorMs, 4 * block, 1e-4f);

            // 아직 한 번도 끊긴 적 없는 상대(기억해 둔 버퍼에서 시작)도 줄인 뒤 첫 끊김에 되돌린다
            Peer q;
            q.bufferMs = 3 * block;
            q.floorMs = block;
            q.shrinkTimerMs = t0;
            expect (q.rules().ping (t0 + 12000, block, false));
            expect (q.rules().drop (t0 + 13000, block, 0.5f, true, false) == NetBufAuto::Drop::reverted);
            expectWithinAbsoluteError (q.bufferMs, 3 * block, 1e-4f);

            // 안정 우선은 되돌리기가 없다 (줄이지도 않는다)
            Peer s;
            s.bufferMs = 2 * block;
            s.lastDropMs = t0;
            s.beforeShrinkMs = 3 * block;
            expect (s.rules().drop (t0 + 20000, block, 0.5f, false, false) == NetBufAuto::Drop::counted);
            expectWithinAbsoluteError (s.bufferMs, 2 * block, 1e-4f);
        }

        // ---- 흔들림 시험: 지연 우선이 실제로 줄어드는지, 끊김이 과하게 늘지 않는지 ----
        // 지연은 실제로 듣는 지연(재생 기준 블록의 망 지연 + 버퍼)으로 비교한다. 안정 우선은 흔들림 한가운데서 늘며
        // 재생 기준을 높게 잡은 채 멈추기 때문에 버퍼 숫자만 보면 실제보다 작아 보인다.

        beginTest ("Jitter for 15 s then calm: latency mode comes back down, dropouts barely change");
        {
            const auto r = compare (t ("15초 흔들림 → 잠잠"), [] (std::mt19937& g, double s)
            {
                return 5.0 + uni (g) * (s < 15.0 ? 12.0 : 1.0);
            }, 300, 15);
            expect (r.latency.shrank > 0);
            expect (r.stable.shrank == 0);
            expect (r.latency.meanDelayMs < r.stable.meanDelayMs - 2.0,
                    "delay " + juce::String (r.latency.meanDelayMs) + " / " + juce::String (r.stable.meanDelayMs));
            expect (r.latency.meanBufferMs < r.stable.meanBufferMs);
            expect (r.latency.finalBufferMs < r.latency.maxBufferMs, "final buffer must be below the peak");
            expectEquals (r.latency.lostAfterCalm, 0);
            expectDropoutsBounded (r);
        }

        beginTest ("Wi-Fi style bursts at any interval: latency mode does not keep falling into the next burst");
        {
            // 5초 동안 0~15 ms 더 늦는 흔들림이 일정 간격으로 온다. 간격이 길수록 잠잠할 때 더 많이 줄인다.
            // AOO 는 버퍼를 바꿀 때마다 재생 기준을 다시 잡아서, 줄이기 전 값으로 되돌려도 그 흔들림을 다 못 막고 한 번 더
            // 늘리는 일이 처음 한두 번의 흔들림에서 생긴다 (끊김이 그만큼 더 많다). 바닥이 자리 잡은 뒤에는 되풀이되지 않는다.
            for (const double every : { 45.0, 90.0, 180.0, 300.0, 600.0 })
            {
                const auto r = compare (t ("와이파이식 흔들림 ") + juce::String ((int) every) + t ("초마다"),
                                        [every] (std::mt19937& g, double s)
                {
                    const double u = uni (g), b = uni (g);
                    return 5.0 + u + (std::fmod (s, every) < 5.0 ? 15.0 * b : 0.0);
                }, std::max (900.0, 6 * every));
                expect (r.latency.meanDelayMs <= r.stable.meanDelayMs + 1.0,
                        "delay " + juce::String (r.latency.meanDelayMs) + " / " + juce::String (r.stable.meanDelayMs));
                expectDropoutsBounded (r, 2.0);
                expect (r.latency.lostLate <= r.stable.lostLate + 5.0,
                        "lost late " + juce::String (r.latency.lostLate) + " / " + juce::String (r.stable.lostLate));
            }
        }

        beginTest ("Rare single lost packets after the network calms: latency stays below stable mode");
        {
            // 15초 흔들린 뒤 잠잠, 3분에 한 번꼴로 패킷 하나가 오지 않는다 (되돌리기가 이 한 번에도 걸린다)
            const auto r = compare (t ("15초 흔들림 → 잠잠 + 3분에 한 번 단발 손실"), [] (std::mt19937& g, double s)
            {
                const double d = 5.0 + uni (g) * (s < 15.0 ? 12.0 : 1.0);
                return uni (g) < block / 180000.0 ? -d : d;
            }, 1800);
            expect (r.latency.meanDelayMs < r.stable.meanDelayMs - 1.0,
                    "delay " + juce::String (r.latency.meanDelayMs) + " / " + juce::String (r.stable.meanDelayMs));
            expectDropoutsBounded (r);
        }

        beginTest ("Steady jitter: nothing to gain, dropouts not excessive");
        {
            const auto spikes = compare (t ("0~8 ms + 가끔 10 ms 튐"), [] (std::mt19937& g, double)
            {
                const double u = uni (g), p = uni (g);
                return 5.0 + 8.0 * u + (p < 0.002 ? 10.0 : 0.0);
            }, 300);
            expect (spikes.latency.meanDelayMs <= spikes.stable.meanDelayMs + 1.5);
            expectDropoutsBounded (spikes);

            const auto uniform = compare (t ("0~8 ms"), [] (std::mt19937& g, double)
            {
                return 5.0 + 8.0 * uni (g);
            }, 300);
            expect (uniform.latency.meanDelayMs <= uniform.stable.meanDelayMs + 1.5);
            expectDropoutsBounded (uniform);

            const auto tail = compare (t ("지수 분포 평균 2 ms"), [] (std::mt19937& g, double)
            {
                return 5.0 + expo (g, 2.0);
            }, 300);
            expect (tail.latency.meanDelayMs <= tail.stable.meanDelayMs + 1.5);
            expectDropoutsBounded (tail);
        }
    }
};

static NetBufAutoTests netBufAutoTests;

} // namespace meon
