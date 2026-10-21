// MEON 브리지 실험: 플러그인 호스트 없이 통로를 재는 시험 프로그램 (리눅스·macOS 명령줄).
//
//   meon_bridge_bench app   --name=N [--load-us=X]          되돌려 보내는 앱 (X µs 만큼 일부러 계산해 엔진 부하를 흉내 낸다)
//   meon_bridge_bench host  --name=N --sr=48000 --bs=128 --mode=A|B --seconds=10 [--timeout=0.5]
//                                                              가짜 호스트: 블록 시간마다 플러그인 쪽 process() 를 부른다
//   meon_bridge_bench matrix [--seconds=10] [--load-us=X]    A/B × 블록 64·128·256·512 × 44.1k·48k 표
//   meon_bridge_bench chaos  [--seconds=30] [--mode=A|B]      호스트가 도는 동안 앱을 늦게 켜고·죽이고·멈추고·다시 켠다
//
// 결과는 한 줄 JSON (RESULT {...}) 과 사람이 읽는 표로 낸다.
#include "BridgeCore.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include <spawn.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char** environ;

using namespace meon::bridge;

namespace
{
std::map<std::string, std::string> args;

std::string arg (const std::string& k, const std::string& d) { auto it = args.find (k); return it == args.end() ? d : it->second; }
double argD (const std::string& k, double d) { auto it = args.find (k); return it == args.end() ? d : std::atof (it->second.c_str()); }

volatile std::sig_atomic_t stopFlag = 0;
void onSignal (int) { stopFlag = 1; }

std::string self;

void sleepUntil (int64_t t)
{
   #if defined(__APPLE__)
    // macOS 에는 clock_nanosleep 이 없다: 남은 시간만큼 잔다
    const auto d = t - nowNs();
    if (d <= 0) return;
    timespec ts { (time_t) (d / 1000000000LL), (long) (d % 1000000000LL) };
    while (nanosleep (&ts, &ts) != 0 && errno == EINTR) {}
   #else
    timespec ts { (time_t) (t / 1000000000LL), (long) (t % 1000000000LL) };
    while (clock_nanosleep (CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr) == EINTR) {}
   #endif
}

/** 이 스레드가 실제로 CPU 를 쓴 시간. 벽시계 시간과 비교해, 블록 초과가 코드 탓인지 스레드가 밀려난 탓인지 가른다 */
int64_t threadCpuNs()
{
    timespec ts {};
    clock_gettime (CLOCK_THREAD_CPUTIME_ID, &ts);
    return (int64_t) ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

void burnUs (double us)
{
    if (us <= 0) return;
    const auto end = nowNs() + (int64_t) (us * 1000.0);
    volatile double x = 0;
    while (nowNs() < end) x += 1.0;
}

//==============================================================================
int runApp()
{
    const auto name = arg ("name", "meonbrg");
    const double loadUs = argD ("load-us", 0);
    std::signal (SIGTERM, onSignal);
    std::signal (SIGINT, onSignal);
    AppEndpoint app;
    const bool ok = app.start (name,
        [loadUs] (const float* const* in, float* const* out, int ch, int n)
        {
            for (int c = 0; c < ch; ++c)
                std::memcpy (out[c], in[c], sizeof (float) * (size_t) n);
            burnUs (loadUs);
        },
        [] (double sr, int bs, int ch) { std::fprintf (stderr, "APP format %.0f Hz, %d samples, %d ch\n", sr, bs, ch); },
        true);
    if (! ok) { std::fprintf (stderr, "APP FAIL %s\n", app.channel.errorText.c_str()); return 1; }
    std::this_thread::sleep_for (std::chrono::milliseconds (100));   // 처리 스레드가 우선순위를 정할 때까지
    std::fprintf (stderr, "APP READY realtime=%d\n", app.realtime.load() ? 1 : 0);
    while (! stopFlag)
        std::this_thread::sleep_for (std::chrono::milliseconds (50));
    app.stop();
    return 0;
}

//==============================================================================
struct HostResult
{
    std::string mode;
    int sr = 0, bs = 0;
    uint64_t blocks = 0, late = 0, notConnected = 0, overruns = 0, otherOwner = 0, waitSkipped = 0;
    uint64_t overrunsCpu = 0;          // 블록 초과 중 호스트 스레드가 CPU 를 블록 시간 넘게 쓴 것 (코드 탓). 나머지는 스레드가 밀려난 것
    double maxHostCpuUs = 0, maxPostUs = 0;
    double maxWaitUs = 0, maxProcessUs = 0, appMaxProcessUs = 0, maxWakeLateUs = 0;
    int64_t latMin = -1, latMax = -1;
    uint64_t clicksSent = 0, clicksBack = 0;
    double seconds = 0;
    double rtP50 = -1, rtP99 = -1, rtP999 = -1, rtMax = -1;
    uint64_t wakeLateOverHalf = 0;   // 호스트가 블록 시간의 절반 넘게 늦게 깬 횟수 (컨테이너 스케줄링 잡음)
    uint64_t hostSkipped = 0;        // 호스트가 한 블록 넘게 늦게 깨서 건너뛴 블록 (DAW 였다면 호스트 자체의 끊김)
    bool realtime = false;
};

/** 가짜 호스트. phaseFn 이 있으면 매 블록 뒤에 경과 시간(초)을 넘겨 부른다 (chaos 용) */
HostResult runHost (const std::string& name, int sr, int bs, Mode mode, double seconds, float timeoutFrac,
                    const std::function<void (double, PluginEndpoint&)>& phaseFn = {}, bool nullPath = false)
{
    PluginEndpoint ep;
    HostResult r;
    r.mode = nullPath ? "기준(통로 없음)" : (mode == Mode::sameBlock ? "A" : "B");
    r.sr = sr;
    r.bs = bs;
    if (! nullPath && ! ep.open (name)) { std::fprintf (stderr, "HOST open fail: %s\n", ep.channel.errorText.c_str()); return r; }
    ep.mode = mode;
    ep.timeoutFraction = timeoutFrac;
    ep.prepare (sr, bs, 2);

    std::atomic<bool> run { true };
    std::thread beat ([&] { while (run) { ep.heartbeat(); std::this_thread::sleep_for (std::chrono::milliseconds (100)); } });

    std::vector<float> inL (bs), inR (bs), outL (bs), outR (bs);
    float* in[2] = { inL.data(), inR.data() };
    float* out[2] = { outL.data(), outR.data() };
    ClickMeter meter;
    meter.reset (sr / 4);

    r.realtime = makeCurrentThreadRealtime (bs * 1000.0 / sr);
    const int64_t period = (int64_t) std::llround ((double) bs * 1.0e9 / sr);
    const auto start = nowNs();
    int64_t next = start;
    const int64_t total = (int64_t) (seconds * 1e9);
    while (nowNs() - start < total && ! stopFlag)
    {
        sleepUntil (next);
        auto woke = nowNs();
        if (woke - next > period)
        {
            // 진짜 DAW 처럼: 늦게 깬 만큼 블록을 몰아서 부르지 않고 건너뛴다
            r.hostSkipped += (uint64_t) ((woke - next) / period);
            next = woke;
        }
        const double wakeLate = (woke - next) / 1000.0;
        if (woke - start > 500000000) r.maxWakeLateUs = std::max (r.maxWakeLateUs, wakeLate);   // 시작 0.5초는 뺀다
        if (woke - next > period / 2) r.wakeLateOverHalf++;

        const auto cpu0 = threadCpuNs();
        meter.writeInput (in, 2, bs);
        if (nullPath)
        {
            ep.stats.blocks++;
            for (int c = 0; c < 2; ++c) std::memcpy (out[c], in[c], sizeof (float) * (size_t) bs);
        }
        else
            ep.process (in, out, 2, bs);
        meter.readOutput (out, 2, bs);
        const auto took = nowNs() - woke;
        const auto cpuTook = threadCpuNs() - cpu0;
        if (woke - start > 500000000) r.maxHostCpuUs = std::max (r.maxHostCpuUs, cpuTook / 1000.0);
        if (took > period) r.overruns++;   // DAW 였다면 소리가 끊긴 블록
        if (cpuTook > period) r.overrunsCpu++;
        if (phaseFn) phaseFn ((nowNs() - start) / 1e9, ep);
        next += period;
    }
    run = false;
    beat.join();
    r.seconds = (nowNs() - start) / 1e9;
    r.blocks = ep.stats.blocks;
    r.late = ep.stats.late;
    r.notConnected = ep.stats.notConnected;
    r.otherOwner = ep.stats.otherOwner;
    r.waitSkipped = ep.stats.waitSkipped;
    r.maxWaitUs = ep.stats.maxWaitNs / 1000.0;
    r.maxProcessUs = ep.stats.maxProcessNs / 1000.0;
    r.maxPostUs = ep.stats.maxPostNs / 1000.0;
    if (auto* s = ep.channel.get()) r.appMaxProcessUs = s->appMaxProcessNs.load() / 1000.0;
    r.latMin = meter.minLatency;
    r.latMax = meter.maxLatency;
    r.clicksSent = meter.sent;
    r.clicksBack = meter.received;
    r.rtP50 = ep.stats.roundTrip.percentileUs (50);
    r.rtP99 = ep.stats.roundTrip.percentileUs (99);
    r.rtP999 = ep.stats.roundTrip.percentileUs (99.9);
    r.rtMax = ep.stats.roundTrip.maxNs.load() / 1000.0;
    ep.close();
    return r;
}

void printResult (const HostResult& r)
{
    std::printf ("RESULT {\"waitSkipped\":%llu,\"otherOwner\":%llu,\"hostSkipped\":%llu,\"mode\":\"%s\",\"sr\":%d,\"bs\":%d,\"seconds\":%.1f,\"blocks\":%llu,\"late\":%llu,\"notConnected\":%llu,"
                 "\"overruns\":%llu,\"overrunsCpu\":%llu,\"maxHostCpuUs\":%.1f,\"maxPostUs\":%.1f,\"latMinSamples\":%lld,\"latMaxSamples\":%lld,\"extraMs\":%.3f,\"clicks\":\"%llu/%llu\","
                 "\"maxWaitUs\":%.1f,\"maxPluginProcessUs\":%.1f,\"appMaxProcessUs\":%.1f,\"hostWakeLateMaxUs\":%.1f,"
                 "\"hostWakeLateOverHalf\":%llu,\"rtUs\":{\"p50\":%.0f,\"p99\":%.0f,\"p99.9\":%.0f,\"max\":%.0f},\"realtime\":%d}\n",
                 (unsigned long long) r.waitSkipped, (unsigned long long) r.otherOwner, (unsigned long long) r.hostSkipped, r.mode.c_str(), r.sr, r.bs, r.seconds, (unsigned long long) r.blocks, (unsigned long long) r.late,
                 (unsigned long long) r.notConnected, (unsigned long long) r.overruns, (unsigned long long) r.overrunsCpu, r.maxHostCpuUs, r.maxPostUs, (long long) r.latMin, (long long) r.latMax,
                 r.latMax >= 0 ? r.latMax * 1000.0 / r.sr : -1.0, (unsigned long long) r.clicksBack, (unsigned long long) r.clicksSent,
                 r.maxWaitUs, r.maxProcessUs, r.appMaxProcessUs, r.maxWakeLateUs, (unsigned long long) r.wakeLateOverHalf,
                 r.rtP50, r.rtP99, r.rtP999, r.rtMax, r.realtime ? 1 : 0);
    std::fflush (stdout);
}

int runHostCmd()
{
    std::signal (SIGINT, onSignal);
    const auto r = runHost (arg ("name", "meonbrg"), (int) argD ("sr", 48000), (int) argD ("bs", 128),
                            arg ("mode", "B") == "A" ? Mode::sameBlock : Mode::nextBlock, argD ("seconds", 10), (float) argD ("timeout", 0.5),
                            {}, arg ("mode", "B") == "N");
    printResult (r);
    return 0;
}

//==============================================================================
/** posix_spawn: fork 와 달리 호스트 프로세스의 메모리를 복사하지 않아 호스트의 오디오 루프를 멈추지 않는다 */
pid_t spawnApp (const std::string& name, double loadUs)
{
    const auto n = "--name=" + name;
    const auto l = "--load-us=" + std::to_string (loadUs);
    char* argv[] = { (char*) self.c_str(), (char*) "app", (char*) n.c_str(), (char*) l.c_str(), nullptr };
    pid_t pid = -1;
    if (posix_spawn (&pid, self.c_str(), nullptr, nullptr, argv, environ) != 0) return -1;
    return pid;
}

void killApp (pid_t pid, int sig)
{
    if (pid <= 0) return;
    kill (pid, sig);
    if (sig == SIGKILL || sig == SIGTERM)
        waitpid (pid, nullptr, 0);
}

/** 앱이 붙을 때까지 (최대 3초) */
void waitConnected (const std::string& name)
{
    PluginEndpoint probe;
    for (int i = 0; i < 300; ++i)
    {
        Channel c;
        if (c.open (name) && c.get()->appToken.load() != 0 && nowNs() - c.get()->appBeatNs.load() < 500000000) return;
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
}

int runMatrix()
{
    const double secs = argD ("seconds", 10);
    const double loadUs = argD ("load-us", 0);
    const float timeout = (float) argD ("timeout", 0.5);
    std::vector<HostResult> all;
    for (int sr : { 44100, 48000 })
        for (int bs : { 64, 128, 256, 512 })
        {
            {
                auto base = runHost ("", sr, bs, Mode::nextBlock, secs, timeout, {}, true);
                printResult (base);
                all.push_back (base);
            }
            for (Mode m : { Mode::sameBlock, Mode::nextBlock })
            {
                const std::string name = "mbm" + std::to_string (getpid());
                Channel::unlink (name);
                const auto app = spawnApp (name, loadUs);
                waitConnected (name);
                std::this_thread::sleep_for (std::chrono::milliseconds (200));
                auto r = runHost (name, sr, bs, m, secs, timeout);
                killApp (app, SIGTERM);
                Channel::unlink (name);
                printResult (r);
                all.push_back (r);
            }
        }
    std::printf ("\n| 방식 | 샘플레이트 | 블록 | 블록 ms | 추가 지연 ms (샘플) | 늦은 블록 | 비율 | 왕복 µs p50 / p99 / p99.9 / 최대 | 플러그인 최대 기다림 µs | 앱 최대 처리 µs | 호스트가 늦게 깬 블록 (½블록↑ / 건너뜀) |\n");
    std::printf ("|---|---|---|---|---|---|---|---|---|---|---|\n");
    for (auto& r : all)
        std::printf ("| %s | %d | %d | %.2f | %s | %llu / %llu | %.3f%% | %s | %.0f | %.0f | %llu / %llu |\n", r.mode.c_str(), r.sr, r.bs,
                     r.bs * 1000.0 / r.sr,
                     r.latMax >= 0 ? (std::to_string (r.latMax * 1000.0 / r.sr).substr (0, 5) + " (" + std::to_string (r.latMax) + ")").c_str() : "–",
                     (unsigned long long) r.late, (unsigned long long) r.blocks, r.blocks ? 100.0 * r.late / r.blocks : 0.0,
                     r.rtP50 >= 0 ? (std::to_string ((int) r.rtP50) + " / " + std::to_string ((int) r.rtP99) + " / " + std::to_string ((int) r.rtP999) + " / " + std::to_string ((int) r.rtMax)).c_str() : "–",
                     r.maxWaitUs, r.appMaxProcessUs, (unsigned long long) r.wakeLateOverHalf, (unsigned long long) r.hostSkipped);
    return 0;
}

//==============================================================================
// 앱을 늦게 켜고(3초), 죽이고(8초, SIGKILL), 다시 켜고(11초), 얼리고(15초 SIGSTOP 2초), 정상 종료(22초), 다시 켠다(25초).
// 호스트가 한 번도 멈추지 않고(블록 초과 0), 앱이 돌아오면 다시 소리가 이어지는지 본다.
// 앱을 켜고 죽이는 일은 따로 스레드에서 한다 (호스트의 오디오 루프를 막지 않게).
int runChaos()
{
    const double secs = std::max (28.0, argD ("seconds", 30));
    const Mode mode = arg ("mode", "B") == "A" ? Mode::sameBlock : Mode::nextBlock;
    const int sr = (int) argD ("sr", 48000), bs = (int) argD ("bs", 128);
    const std::string name = "mbc" + std::to_string (getpid());
    Channel::unlink (name);
    struct Phase { const char* what; double at; };
    const std::vector<Phase> phases { { "앱 켜기 (늦게)", 3 }, { "앱 강제 종료 (SIGKILL)", 8 }, { "앱 다시 켜기", 11 },
                                      { "앱 얼리기 (SIGSTOP)", 15 }, { "앱 풀기 (SIGCONT)", 17 }, { "앱 정상 종료 (SIGTERM)", 22 }, { "앱 다시 켜기", 25 } };

    std::atomic<int> phaseDone { 0 };           // 컨트롤러가 마친 단계 수
    std::atomic<int64_t> phaseAtNs[16] {};
    std::atomic<int64_t> hostStartNs { 0 };
    std::thread controller ([&]
    {
        while (hostStartNs.load() == 0 && ! stopFlag) std::this_thread::sleep_for (std::chrono::milliseconds (1));
        if (hostStartNs.load() == 0) return;   // 호스트가 통로를 못 열고 끝났다
        pid_t app = -1;
        for (size_t k = 0; k < phases.size(); ++k)
        {
            const auto& p = phases[k];
            sleepUntil (hostStartNs.load() + (int64_t) (p.at * 1e9));
            const std::string w = p.what;
            if (w.find ("켜기") != std::string::npos) app = spawnApp (name, argD ("load-us", 0));
            else if (w.find ("SIGKILL") != std::string::npos) { killApp (app, SIGKILL); app = -1; }
            else if (w.find ("SIGSTOP") != std::string::npos) kill (app, SIGSTOP);
            else if (w.find ("SIGCONT") != std::string::npos) kill (app, SIGCONT);
            else if (w.find ("SIGTERM") != std::string::npos) { killApp (app, SIGTERM); app = -1; }
            phaseAtNs[k] = nowNs();
            phaseDone = (int) k + 1;
        }
        while (! stopFlag && nowNs() - hostStartNs.load() < (int64_t) ((secs + 1) * 1e9))
            std::this_thread::sleep_for (std::chrono::milliseconds (20));
        killApp (app, SIGTERM);
    });

    struct Mark { double t; uint64_t blocks, bad; };
    std::vector<Mark> marks (phases.size());
    std::vector<double> reconnectMs;           // 앱을 켠 뒤 처음으로 소리가 돌아온 블록까지
    int seen = 0;
    bool awaiting = false;
    int64_t spawnNs = 0;
    uint64_t prevBad = 0;
    int64_t lastBlockNs = 0, worstGapNs = 0;

    auto r = runHost (name, sr, bs, mode, secs, (float) argD ("timeout", 0.5), [&] (double t, PluginEndpoint& ep)
    {
        const auto now = nowNs();
        if (hostStartNs.load() == 0) hostStartNs = now - (int64_t) (t * 1e9);
        if (lastBlockNs != 0) worstGapNs = std::max (worstGapNs, now - lastBlockNs);
        lastBlockNs = now;
        const uint64_t bad = ep.stats.late + ep.stats.notConnected;
        while (seen < phaseDone.load())
        {
            marks[(size_t) seen] = { t, ep.stats.blocks.load(), bad };
            if (std::string (phases[(size_t) seen].what).find ("켜기") != std::string::npos || std::string (phases[(size_t) seen].what).find ("SIGCONT") != std::string::npos)
            {
                awaiting = true;
                spawnNs = phaseAtNs[seen].load();
            }
            ++seen;
        }
        if (awaiting && bad == prevBad && ep.stats.blocks > 1)
        {
            reconnectMs.push_back ((now - spawnNs) / 1e6);
            awaiting = false;
        }
        prevBad = bad;
    });
    stopFlag = 1;
    controller.join();
    stopFlag = 0;
    Channel::unlink (name);
    printResult (r);
    std::printf ("CHAOS mode=%s host_blocks=%llu host_overruns=%llu host_overruns_cpu=%llu worst_gap_ms=%.2f (블록 시간 %.2f ms)\n", r.mode.c_str(),
                 (unsigned long long) r.blocks, (unsigned long long) r.overruns, (unsigned long long) r.overrunsCpu, worstGapNs / 1e6, bs * 1000.0 / sr);
    for (size_t i = 0; i < phases.size(); ++i)
    {
        const uint64_t badNext = i + 1 < phases.size() ? marks[i + 1].bad : r.late + r.notConnected;
        std::printf ("CHAOS %5.2fs %-26s 이 구간 무음 블록 %llu\n", marks[i].t, phases[i].what, (unsigned long long) (badNext - marks[i].bad));
    }
    for (auto ms : reconnectMs)
        std::printf ("CHAOS 앱을 켜거나 풀고 소리가 돌아오기까지 %.0f ms\n", ms);
    // 실패로 보는 것: 호스트가 앱 때문에 막힌 경우 (앱이 죽거나 언 2~3초 동안 기다리면 간격이 초 단위로 벌어진다).
    // 블록 초과 수는 숫자로만 남긴다. 러너·컨테이너(가상 머신)는 통로가 없어도 스레드가 수십 ms 씩 밀려나고,
    // 빼앗긴 시간이 스레드 CPU 시간에도 잡혀서 코드 탓과 기계 탓을 완전히 가를 수 없다.
    return worstGapNs < 500000000 ? 0 : 2;
}
//==============================================================================
// 2단계 끝에서 끝까지: 가짜 호스트 두 개(통로 이름 A, B). A 는 0.5초마다 클릭을 앱 A 에 넣고,
// B 는 앱 B 의 출력(= 합주 엔진이 받은 상대 소리)에서 클릭을 찾는다. 앱 A·B 는 같은 방에 들어가 있어야 한다.
int runE2E()
{
    const auto nameA = arg ("a", "brgA"), nameB = arg ("b", "brgB");
    const int sr = (int) argD ("sr", 48000), bs = (int) argD ("bs", 256);
    const int srB = (int) argD ("srb", sr);   // B 의 DAW 샘플레이트 (서로 다를 때)
    const double secs = argD ("seconds", 60);
    const Mode mode = arg ("mode", "B") == "A" ? Mode::sameBlock : Mode::nextBlock;
    PluginEndpoint ea, eb;
    if (! ea.open (nameA) || ! eb.open (nameB)) { std::fprintf (stderr, "E2E open fail\n"); return 1; }
    for (auto* e : { &ea, &eb }) { e->mode = mode; e->timeoutFraction = (float) argD ("timeout", 0.5); e->prepare (e == &ea ? sr : srB, bs, 2); }

    std::atomic<bool> run { true };
    std::thread beat ([&] { while (run) { ea.heartbeat(); eb.heartbeat(); std::this_thread::sleep_for (std::chrono::milliseconds (100)); } });
    std::vector<int64_t> sentAt;           // A 가 클릭을 넣은 시각
    std::vector<int64_t> gotAt;            // B 에서 찾은 시각
    std::atomic<int64_t> startNs { nowNs() + 200000000 };
    const int64_t clickEvery = (int64_t) (0.5 * sr);
    sentAt.reserve (1000);
    gotAt.reserve (1000);
    double peakB = 0;

    auto host = [&] (PluginEndpoint& ep, bool sender)
    {
        const int rate = sender ? sr : srB;
        const int64_t period = (int64_t) std::llround ((double) bs * 1e9 / rate);
        makeCurrentThreadRealtime (bs * 1000.0 / rate);
        std::vector<float> l (bs), r (bs);
        float* io[2] = { l.data(), r.data() };
        int64_t next = startNs.load(), pos = 0, lastHit = -1000000, nextClick = clickEvery;
        while (run)
        {
            sleepUntil (next);
            auto woke = nowNs();
            if (woke - next > period) next = woke;
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            if (sender && nextClick >= pos && nextClick < pos + bs)
            {
                const int k = (int) (nextClick - pos);
                l[k] = r[k] = 0.9f;
                sentAt.push_back (woke + (int64_t) (k * 1e9 / sr));
                nextClick += clickEvery;
            }
            ep.process (io, io, 2, bs);
            if (! sender)
                for (int i = 0; i < bs; ++i)
                {
                    peakB = std::max (peakB, (double) std::fabs (l[i]));
                    if (std::fabs (l[i]) > 0.2f && pos + i - lastHit > rate / 10)
                    {
                        lastHit = pos + i;
                        gotAt.push_back (woke + (int64_t) (i * 1e9 / rate));
                    }
                }
            pos += bs;
            next += period;
        }
    };
    std::thread ta ([&] { host (ea, true); }), tb ([&] { host (eb, false); });
    // 처음 몇 초(엔진 준비·형식 바꿈)는 빼고 잰다 (--warmup=초, 기본 3)
    std::this_thread::sleep_for (std::chrono::milliseconds ((int) (argD ("warmup", 3) * 1000)));
    for (auto* e : { &ea, &eb })
    {
        e->stats.reset();
        if (auto* sh = e->channel.get()) { sh->appProcess.reset(); sh->appMaxProcessNs = 0; }
    }
    std::this_thread::sleep_for (std::chrono::milliseconds ((int) (secs * 1000)));
    run = false;
    ta.join();
    tb.join();
    beat.join();

    // 보낸 클릭마다 그 뒤 0.4초 안에 B 에서 찾은 첫 클릭을 짝짓는다
    std::vector<double> lat;
    size_t j = 0;
    for (auto t : sentAt)
    {
        while (j < gotAt.size() && gotAt[j] < t) ++j;
        if (j < gotAt.size() && gotAt[j] - t < 400000000) lat.push_back ((gotAt[j] - t) / 1e6);
    }
    std::sort (lat.begin(), lat.end());
    auto pct = [&] (double p) { return lat.empty() ? -1.0 : lat[(size_t) std::min ((double) lat.size() - 1, p / 100.0 * (double) lat.size())]; };
    auto* sa = ea.channel.get();
    auto* sb = eb.channel.get();
    std::printf ("E2E srB=%d mode=%s sr=%d bs=%d secs=%.0f clicks_sent=%zu clicks_heard=%zu peakB=%.3f latency_ms p50=%.1f p90=%.1f max=%.1f\n",
                 srB, mode == Mode::sameBlock ? "A" : "B", sr, bs, secs, sentAt.size(), lat.size(), peakB, pct (50), pct (90), lat.empty() ? -1.0 : lat.back());
    for (auto [label, e, sh] : { std::tuple { "A", &ea, sa }, std::tuple { "B", &eb, sb } })
        std::printf ("E2E %s: blocks=%llu late=%llu notConnected=%llu rt_us p50=%.0f p99=%.0f max=%.0f | engine_process_us p50=%.0f p99=%.0f p99.9=%.0f max=%.0f\n",
                     label, (unsigned long long) e->stats.blocks, (unsigned long long) e->stats.late, (unsigned long long) e->stats.notConnected,
                     e->stats.roundTrip.percentileUs (50), e->stats.roundTrip.percentileUs (99), e->stats.roundTrip.maxNs / 1000.0,
                     sh ? sh->appProcess.percentileUs (50) : -1.0, sh ? sh->appProcess.percentileUs (99) : -1.0,
                     sh ? sh->appProcess.percentileUs (99.9) : -1.0, sh ? sh->appProcess.maxNs / 1000.0 : -1.0);
    ea.close();
    eb.close();
    return lat.empty() ? 3 : 0;
}
} // namespace

int main (int argc, char** argv)
{
    self = argv[0];
    std::string cmd = argc > 1 ? argv[1] : "matrix";
    for (int i = 2; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a.rfind ("--", 0) != 0) continue;
        const auto eq = a.find ('=');
        args[a.substr (2, eq == std::string::npos ? std::string::npos : eq - 2)] = eq == std::string::npos ? "1" : a.substr (eq + 1);
    }
    std::signal (SIGPIPE, SIG_IGN);   // 시험 프로그램만 (플러그인·앱 코드는 소켓을 쓰지 않는다)
    if (cmd == "app") return runApp();
    if (cmd == "host") return runHostCmd();
    if (cmd == "chaos") return runChaos();
    if (cmd == "e2e") return runE2E();
    return runMatrix();
}
