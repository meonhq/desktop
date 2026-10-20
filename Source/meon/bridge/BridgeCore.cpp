// MEON 브리지 실험 (MEON_BRIDGE_SPIKE). BridgeCore.h 설명 참고.
#include "BridgeCore.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <random>

#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <intrin.h>
#else
 #include <cerrno>
 #include <fcntl.h>
 #include <pthread.h>
 #include <sched.h>
 #include <semaphore.h>
 #include <sys/mman.h>
 #include <sys/stat.h>
 #include <time.h>
 #include <unistd.h>
 #if defined(__APPLE__)
  #include <mach/mach.h>
  #include <mach/mach_time.h>
  #include <mach/thread_policy.h>
 #endif
#endif

namespace meon::bridge
{

namespace
{
    inline void cpuRelax()
    {
       #if defined(_M_X64) || defined(_M_IX86)
        _mm_pause();
       #elif defined(_M_ARM64)
        __yield();
       #elif defined(__x86_64__) || defined(__i386__)
        __builtin_ia32_pause();
       #elif defined(__aarch64__) || defined(__arm__)
        asm volatile ("yield");
       #endif
    }

    uint64_t randomToken()
    {
        std::random_device rd;
        uint64_t t = ((uint64_t) rd() << 32) ^ (uint64_t) rd() ^ (uint64_t) nowNs();
        return t == 0 ? 1 : t;
    }

    constexpr int64_t kPluginStaleNs = 1500000000;   // 플러그인 주인이 1.5초 동안 알리지 않으면 자리를 넘긴다
    constexpr int64_t kAppAliveNs = 1000000000;      // 앱이 1초 안에 알렸으면 살아 있다
    constexpr int64_t kNoBlocksNs = 100000000;       // 앱: 플러그인 블록이 0.1초 없으면 자기 시계로 돈다

   #if defined(_WIN32)
    std::wstring widen (const std::string& s)
    {
        return std::wstring (s.begin(), s.end());   // 이름은 ASCII 만 쓴다
    }
   #endif
}

int64_t nowNs()
{
   #if defined(_WIN32)
    static const int64_t freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency (&f); return (int64_t) f.QuadPart; }();
    LARGE_INTEGER c;
    QueryPerformanceCounter (&c);
    return (int64_t) ((double) c.QuadPart * 1.0e9 / (double) freq);
   #else
    timespec ts;
    clock_gettime (CLOCK_MONOTONIC, &ts);
    return (int64_t) ts.tv_sec * 1000000000LL + ts.tv_nsec;
   #endif
}

//==============================================================================
Channel::~Channel() { close(); }

bool Channel::open (const std::string& name)
{
    if (openOnce (name)) return true;
    if (! stale) return false;
   #if ! defined(_WIN32)
    // 다른 버전이 남긴 공유 메모리: 지우고 새로 만든다 (옛 버전을 쓰는 쪽과는 어차피 통하지 않는다)
    unlink (name);
    return openOnce (name);
   #else
    return false;   // Windows 는 핸들을 가진 프로세스가 모두 닫아야 사라진다 (옛 버전 앱·플러그인을 끄면 된다)
   #endif
}

bool Channel::openOnce (const std::string& name)
{
    close();
    errorText.clear();
    stale = false;
    const size_t size = sizeof (Shared);
   #if defined(_WIN32)
    auto h = CreateFileMappingW (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD) size, (L"Local\\" + widen (name) + L".shm").c_str());
    if (h == nullptr) { errorText = "CreateFileMapping " + std::to_string (GetLastError()); return false; }
    void* p = MapViewOfFile (h, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (p == nullptr) { errorText = "MapViewOfFile " + std::to_string (GetLastError()); CloseHandle (h); return false; }
    auto ev = CreateEventW (nullptr, FALSE, FALSE, (L"Local\\" + widen (name) + L".in").c_str());
    if (ev == nullptr) { errorText = "CreateEvent " + std::to_string (GetLastError()); UnmapViewOfFile (p); CloseHandle (h); return false; }
    mapHandle = h;
    sem = ev;
   #else
    const std::string shmName = "/" + name + ".shm";   // macOS: 이름 31자 이하
    int fd = shm_open (shmName.c_str(), O_CREAT | O_RDWR, 0600);
    if (fd < 0) { errorText = std::string ("shm_open: ") + std::strerror (errno); return false; }
    struct stat st {};
    fstat (fd, &st);
    if (st.st_size == 0 && ftruncate (fd, (off_t) size) != 0)
        {}   // macOS 는 이미 크기가 있으면 실패한다 (다른 쪽이 먼저 만들었을 때). 아래에서 크기를 다시 본다
    for (int i = 0; i < 200 && fstat (fd, &st) == 0 && (size_t) st.st_size < size; ++i)
        usleep (1000);   // 다른 쪽이 크기를 정하는 중 (메시지 스레드에서만 부른다)
    if ((size_t) st.st_size != size) { errorText = "공유 메모리 크기가 다름 (다른 버전?)"; stale = st.st_size != 0; ::close (fd); return false; }
    void* p = mmap (nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ::close (fd);
    if (p == MAP_FAILED) { errorText = std::string ("mmap: ") + std::strerror (errno); return false; }
    sem_t* s = sem_open (("/" + name + ".in").c_str(), O_CREAT, 0600, 0);
    if (s == SEM_FAILED) { errorText = std::string ("sem_open: ") + std::strerror (errno); munmap (p, size); return false; }
    sem = s;
   #endif
    shared = static_cast<Shared*> (p);

    // 머리말: 처음 연 쪽이 채운다 (공유 메모리는 0 으로 시작한다)
    uint32_t expect = 0;
    if (shared->initState.compare_exchange_strong (expect, 1))
    {
        shared->magic = kMagic;
        shared->version = kVersion;
        shared->size = (uint32_t) size;
        shared->initState.store (2, std::memory_order_release);
    }
    else
    {
        for (int i = 0; i < 1000 && shared->initState.load (std::memory_order_acquire) != 2; ++i)
            std::this_thread::sleep_for (std::chrono::microseconds (100));
    }
    if (shared->initState.load() != 2 || shared->magic != kMagic || shared->version != kVersion || shared->size != size)
    {
        errorText = "공유 메모리 형식이 다름 (다른 버전의 플러그인·앱?)";
        stale = true;
        close();
        return false;
    }
    return true;
}

void Channel::close()
{
   #if defined(_WIN32)
    if (shared != nullptr) UnmapViewOfFile (shared);
    if (mapHandle != nullptr) CloseHandle ((HANDLE) mapHandle);
    if (sem != nullptr) CloseHandle ((HANDLE) sem);
   #else
    if (shared != nullptr) munmap (shared, sizeof (Shared));
    if (sem != nullptr) sem_close ((sem_t*) sem);
   #endif
    shared = nullptr;
    mapHandle = nullptr;
    sem = nullptr;
}

void Channel::post()
{
    if (sem == nullptr) return;
   #if defined(_WIN32)
    SetEvent ((HANDLE) sem);
   #else
    sem_post ((sem_t*) sem);
   #endif
}

void Channel::wait()
{
    if (sem == nullptr) return;
   #if defined(_WIN32)
    WaitForSingleObject ((HANDLE) sem, INFINITE);
   #else
    while (sem_wait ((sem_t*) sem) != 0 && errno == EINTR) {}
   #endif
}

void Channel::drain()
{
    if (sem == nullptr) return;
   #if defined(_WIN32)
    ResetEvent ((HANDLE) sem);
   #else
    while (sem_trywait ((sem_t*) sem) == 0) {}
   #endif
}

void Channel::unlink (const std::string& name)
{
   #if ! defined(_WIN32)
    shm_unlink (("/" + name + ".shm").c_str());
    sem_unlink (("/" + name + ".in").c_str());
   #else
    (void) name;
   #endif
}

//==============================================================================
void ClickMeter::reset (int intervalSamples)
{
    interval = std::max (1024, intervalSamples);
    pos = 0;
    nextSend = interval / 4;
    pendingAt = -1;
    lastLatency = minLatency = maxLatency = -1;
    sent = received = 0;
}

void ClickMeter::writeInput (float* const* in, int numChannels, int numSamples)
{
    for (int c = 0; c < numChannels; ++c)
        std::memset (in[c], 0, sizeof (float) * (size_t) numSamples);
    if (nextSend >= pos && nextSend < pos + numSamples)
    {
        const int k = (int) (nextSend - pos);
        for (int c = 0; c < numChannels; ++c)
            in[c][k] = 1.0f;
        pendingAt = nextSend;   // 앞 클릭이 안 돌아왔으면 잃은 것으로 친다 (sent - received)
        sent++;
        nextSend += interval;
    }
}

void ClickMeter::readOutput (const float* const* out, int numChannels, int numSamples)
{
    if (pendingAt >= 0 && numChannels > 0)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            if (std::fabs (out[0][i]) > 0.5f)
            {
                const int64_t lat = pos + i - pendingAt;
                if (lat >= 0)
                {
                    lastLatency = lat;
                    if (minLatency < 0 || lat < minLatency) minLatency = lat;
                    if (lat > maxLatency) maxLatency = lat;
                    received++;
                    pendingAt = -1;
                }
                break;
            }
        }
        if (pendingAt >= 0 && pos + numSamples - pendingAt > interval / 2)
            pendingAt = -1;   // 반 간격 안에 안 돌아오면 잃은 것
    }
    pos += numSamples;
}

//==============================================================================
void RoundTripHistogram::add (int64_t ns)
{
    const int64_t us = ns / 1000;
    int k = 0;
    while (k < kBuckets && us >= kEdgesUs[k]) ++k;
    counts[k].fetch_add (1, std::memory_order_relaxed);
    if (ns > maxNs.load (std::memory_order_relaxed)) maxNs.store (ns, std::memory_order_relaxed);
}

void RoundTripHistogram::reset()
{
    for (auto& c : counts) c = 0;
    maxNs = 0;
}

double RoundTripHistogram::percentileUs (double p) const
{
    uint64_t total = 0;
    for (auto& c : counts) total += c.load();
    if (total == 0) return -1;
    const double want = total * p / 100.0;
    uint64_t acc = 0;
    for (int k = 0; k <= kBuckets; ++k)
    {
        acc += counts[k].load();
        if ((double) acc >= want)
            return k < kBuckets ? (double) kEdgesUs[k] : maxNs.load() / 1000.0;
    }
    return maxNs.load() / 1000.0;
}

void PluginStats::reset()
{
    blocks = late = notConnected = otherOwner = 0;
    lastWaitNs = maxWaitNs = maxProcessNs = 0;
    roundTrip.reset();
}

bool PluginEndpoint::open (const std::string& name)
{
    if (! channel.open (name))
    {
        currentState = State::closed;
        return false;
    }
    token = randomToken();
    heartbeat();
    return true;
}

void PluginEndpoint::close()
{
    if (auto* s = channel.get())
    {
        auto t = token;
        s->pluginToken.compare_exchange_strong (t, 0);
    }
    owner = false;
    channel.close();
    currentState = State::closed;
}

void PluginEndpoint::prepare (double sampleRate, int blockSize, int numChannels)
{
    rate = sampleRate > 0 ? sampleRate : 48000.0;
    if (auto* s = channel.get(); s != nullptr && owner)
    {
        s->sampleRate = (uint32_t) std::lround (sampleRate);
        s->blockSize = (uint32_t) blockSize;
        s->numChannels = (uint32_t) std::min (numChannels, kMaxChannels);
        s->formatSeq.fetch_add (1, std::memory_order_release);
    }
    lastPrepared = { sampleRate, blockSize, numChannels };
}

void PluginEndpoint::heartbeat()
{
    auto* s = channel.get();
    if (s == nullptr) { currentState = State::closed; return; }
    const auto now = nowNs();
    auto cur = s->pluginToken.load();
    const bool stale = now - s->pluginBeatNs.load() > kPluginStaleNs;
    if (cur == token)
    {
        s->pluginBeatNs = now;
    }
    else if ((cur == 0 || stale) && s->pluginToken.compare_exchange_strong (cur, token))
    {
        s->pluginBeatNs = now;
        seq = s->inSeq.load();   // 블록 번호는 이어서 매긴다 (앱이 순서로 읽는다)
        owner = true;
        if (lastPrepared.blockSize > 0)
            prepare (lastPrepared.sampleRate, lastPrepared.blockSize, lastPrepared.numChannels);
    }
    owner = s->pluginToken.load() == token;
    if (! owner)
        currentState = State::otherInstance;
    else
        currentState = (s->appToken.load() != 0 && now - s->appBeatNs.load() < kAppAliveNs) ? State::connected : State::waitingForApp;
}

void PluginEndpoint::process (const float* const* in, float* const* out, int numChannels, int numSamples)
{
    const auto t0 = nowNs();
    stats.blocks++;
    auto silence = [&]
    {
        for (int c = 0; c < numChannels; ++c)
            std::memset (out[c], 0, sizeof (float) * (size_t) numSamples);
    };
    auto finish = [&]
    {
        const auto dt = nowNs() - t0;
        if (dt > stats.maxProcessNs.load (std::memory_order_relaxed)) stats.maxProcessNs = dt;
    };

    auto* s = channel.get();
    if (s == nullptr || numSamples > kMaxBlock || numSamples <= 0)
    {
        silence();
        stats.notConnected++;
        return finish();
    }
    if (! owner.load (std::memory_order_relaxed))
    {
        silence();
        stats.otherOwner++;
        return finish();
    }

    const int ch = std::min (numChannels, kMaxChannels);
    const uint64_t mySeq = ++seq;
    Slot& is = s->in[mySeq % kSlots];
    for (int c = 0; c < kMaxChannels; ++c)
        std::memcpy (is.data[c], in[std::min (c, ch - 1)], sizeof (float) * (size_t) numSamples);   // 모노 입력은 두 채널로
    is.numSamples.store (numSamples, std::memory_order_relaxed);
    is.stampNs.store (t0, std::memory_order_relaxed);
    is.seq.store (mySeq, std::memory_order_release);
    s->inSeq.store (mySeq, std::memory_order_release);

    const bool appAlive = s->appToken.load (std::memory_order_relaxed) != 0
                          && t0 - s->appBeatNs.load (std::memory_order_relaxed) < kAppAliveNs;
    if (! appAlive)
    {
        silence();
        stats.notConnected++;
        return finish();
    }
    channel.post();

    const Mode m = mode.load (std::memory_order_relaxed);
    const uint64_t want = (m == Mode::sameBlock) ? mySeq : mySeq - 1;
    if (m == Mode::sameBlock)
    {
        const double blockNs = (double) numSamples / rate.load (std::memory_order_relaxed) * 1.0e9;
        const auto deadline = t0 + (int64_t) (blockNs * (double) std::clamp (timeoutFraction.load (std::memory_order_relaxed), 0.0f, 0.95f));
        int spins = 0;
        while (s->outSeq.load (std::memory_order_acquire) < want)
        {
            if (nowNs() >= deadline) break;
            cpuRelax();
            if ((++spins & 63) == 0) std::this_thread::yield();
        }
        const auto waited = nowNs() - t0;
        stats.lastWaitNs = waited;
        if (waited > stats.maxWaitNs.load (std::memory_order_relaxed)) stats.maxWaitNs = waited;
    }

    bool got = false;
    if (want > 0 && s->outSeq.load (std::memory_order_acquire) >= want)
    {
        Slot& os = s->out[want % kSlots];
        if (os.seq.load (std::memory_order_acquire) == want && os.numSamples.load (std::memory_order_relaxed) == numSamples)
        {
            for (int c = 0; c < numChannels; ++c)
                std::memcpy (out[c], os.data[std::min (c, kMaxChannels - 1)], sizeof (float) * (size_t) numSamples);
            const auto stamp = os.stampNs.load (std::memory_order_relaxed);
            const auto done = os.doneNs.load (std::memory_order_relaxed);
            got = os.seq.load (std::memory_order_acquire) == want;   // 읽는 사이에 덮어쓰지 않았는지
            if (got && stamp > 0 && done >= stamp) stats.roundTrip.add (done - stamp);
        }
    }
    if (! got)
    {
        silence();
        stats.late++;
    }
    finish();
}

//==============================================================================
bool AppEndpoint::start (const std::string& name, ProcessFn p, FormatFn f, bool freeRunWhenAlone)
{
    stop();
    if (! channel.open (name))
        return false;
    auto* s = channel.get();
    token = randomToken();
    auto cur = s->appToken.load();
    const auto now = nowNs();
    if (cur != 0 && now - s->appBeatNs.load() < kAppAliveNs)
    {
        channel.errorText = "다른 앱이 이미 붙어 있음";
        channel.close();
        return false;
    }
    if (! s->appToken.compare_exchange_strong (cur, token))
    {
        channel.errorText = "다른 앱이 동시에 붙음";
        channel.close();
        return false;
    }
    s->appBeatNs = now;
    channel.drain();
    process = std::move (p);
    format = std::move (f);
    freeRun = freeRunWhenAlone;
    lastProcessed = s->inSeq.load();
    lastFormatSeq = s->formatSeq.load() - 1;   // 처음에 한 번 형식을 알린다
    lastBlockNs = 0;
    running = true;
    worker = std::thread ([this] { run(); });
    clockThread = std::thread ([this] { clock(); });
    return true;
}

void AppEndpoint::stop()
{
    if (running.exchange (false))
    {
        channel.post();
        if (worker.joinable()) worker.join();
        if (clockThread.joinable()) clockThread.join();
    }
    if (auto* s = channel.get())
    {
        auto t = token;
        s->appToken.compare_exchange_strong (t, 0);
    }
    channel.close();
}

bool AppEndpoint::pluginConnected() const
{
    auto* s = channel.get();
    return s != nullptr && s->pluginToken.load() != 0 && nowNs() - s->pluginBeatNs.load() < kPluginStaleNs;
}

bool AppEndpoint::receivingBlocks() const
{
    const auto t = lastBlockNs.load();
    return t != 0 && nowNs() - t < kNoBlocksNs;
}

void AppEndpoint::clock()
{
    auto* s = channel.get();
    int64_t next = nowNs();
    while (running)
    {
        const auto now = nowNs();
        if (s->appToken.load() == token)
            s->appBeatNs = now;
        if (freeRun && ! receivingBlocks())
        {
            // 플러그인 블록이 없다: 자기 시계로 무음 블록 (마지막 형식, 없으면 48k/256)
            const double sr = currentRate.load() > 0 ? currentRate.load() : 48000.0;
            const int bs = currentBlock.load() > 0 ? currentBlock.load() : 256;
            freeRunTick = true;
            channel.post();
            next += (int64_t) ((double) bs / sr * 1.0e9);
            if (next < now - 50000000) next = now;   // 많이 밀렸으면 따라잡지 않는다
            std::this_thread::sleep_for (std::chrono::nanoseconds (std::max<int64_t> (0, next - nowNs())));
        }
        else
        {
            next = now;
            std::this_thread::sleep_for (std::chrono::milliseconds (20));
        }
    }
}

void AppEndpoint::run()
{
    auto* s = channel.get();
    const double guessPeriodMs = 256.0 / 48.0;
    realtime = makeCurrentThreadRealtime (guessPeriodMs);

    static thread_local float inBuf[kMaxChannels][kMaxBlock];
    static thread_local float outBuf[kMaxChannels][kMaxBlock];
    float* inPtr[kMaxChannels] = { inBuf[0], inBuf[1] };
    float* outPtr[kMaxChannels] = { outBuf[0], outBuf[1] };

    while (running)
    {
        channel.wait();
        if (! running) break;

        const auto fs = s->formatSeq.load (std::memory_order_acquire);
        if (fs != lastFormatSeq && s->blockSize.load() > 0)
        {
            lastFormatSeq = fs;
            currentRate = (double) s->sampleRate.load();
            currentBlock = (int) s->blockSize.load();
            if (format) format (currentRate.load(), currentBlock.load(), (int) std::max<uint32_t> (1, s->numChannels.load()));
        }

        const uint64_t newest = s->inSeq.load (std::memory_order_acquire);
        if (newest < lastProcessed)
            lastProcessed = newest == 0 ? 0 : newest - 1;   // 공유 메모리를 새로 만들었다
        if (newest > lastProcessed)
        {
            lastBlockNs = nowNs();
            uint64_t first = lastProcessed + 1;
            if (newest - first >= (uint64_t) kSlots - 1)
            {
                s->appSkipped.fetch_add (newest - first);
                first = newest;
            }
            for (uint64_t q = first; q <= newest; ++q)
            {
                Slot& is = s->in[q % kSlots];
                if (is.seq.load (std::memory_order_acquire) != q) { s->appSkipped++; continue; }
                const int n = std::min (is.numSamples.load(), kMaxBlock);
                for (int c = 0; c < kMaxChannels; ++c)
                    std::memcpy (inBuf[c], is.data[c], sizeof (float) * (size_t) n);
                if (is.seq.load (std::memory_order_acquire) != q) { s->appSkipped++; continue; }

                const auto t = nowNs();
                if (process) process (inPtr, outPtr, kMaxChannels, n);
                const auto dt = nowNs() - t;
                s->appLastProcessNs = dt;
                if (dt > s->appMaxProcessNs.load()) s->appMaxProcessNs = dt;
                s->appProcess.add (dt);

                Slot& os = s->out[q % kSlots];
                for (int c = 0; c < kMaxChannels; ++c)
                    std::memcpy (os.data[c], outBuf[c], sizeof (float) * (size_t) n);
                os.numSamples.store (n, std::memory_order_relaxed);
                os.stampNs.store (is.stampNs.load (std::memory_order_relaxed), std::memory_order_relaxed);
                os.doneNs.store (nowNs(), std::memory_order_relaxed);
                os.seq.store (q, std::memory_order_release);
                s->outSeq.store (q, std::memory_order_release);
                s->appProcessed++;
            }
            lastProcessed = newest;
            s->appBeatNs = nowNs();   // 블록이 오가는 동안은 처리 스레드가 살아 있다고 알린다 (macOS App Nap 으로 시계 스레드가 늦어져도)
        }
        else if (freeRunTick.exchange (false) && ! receivingBlocks())
        {
            const int n = currentBlock.load() > 0 ? currentBlock.load() : 256;
            for (int c = 0; c < kMaxChannels; ++c)
                std::memset (inBuf[c], 0, sizeof (float) * (size_t) n);
            if (process) process (inPtr, outPtr, kMaxChannels, n);
            s->appFreeRun++;
        }
    }
}

//==============================================================================
bool makeCurrentThreadRealtime (double periodMs)
{
   #if defined(_WIN32)
    (void) periodMs;
    return SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL) != 0;
   #elif defined(__APPLE__)
    mach_timebase_info_data_t tb;
    mach_timebase_info (&tb);
    const double nsToAbs = (double) tb.denom / (double) tb.numer;
    thread_time_constraint_policy_data_t pol;
    pol.period = (uint32_t) (periodMs * 1.0e6 * nsToAbs);
    pol.computation = (uint32_t) (periodMs * 0.5e6 * nsToAbs);
    pol.constraint = (uint32_t) (periodMs * 1.0e6 * nsToAbs);
    pol.preemptible = 1;
    return thread_policy_set (mach_thread_self(), THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t) &pol,
                              THREAD_TIME_CONSTRAINT_POLICY_COUNT) == KERN_SUCCESS;
   #else
    (void) periodMs;
    sched_param p {};
    p.sched_priority = std::max (1, sched_get_priority_max (SCHED_FIFO) - 10);
    return pthread_setschedparam (pthread_self(), SCHED_FIFO, &p) == 0;
   #endif
}

} // namespace meon::bridge
