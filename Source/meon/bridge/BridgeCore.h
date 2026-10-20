// MEON 브리지 실험 (MEON_BRIDGE_SPIKE): 플러그인 ↔ 독립 앱 오디오 통로의 핵심.
// 제품에 넣지 않는다. JUCE 를 쓰지 않아서 리눅스 시험 프로그램(BridgeBench)도 같은 코드를 쓴다.
//
// 구조
//   - 공유 메모리 하나 (macOS·리눅스: POSIX shm, Windows: 이름 있는 파일 매핑) 안에 머리말과 블록 칸(slot) 링 두 개
//     (플러그인 → 앱 입력, 앱 → 플러그인 출력). 칸은 블록 번호(seq)로 쓰고 읽는다. 잠금 없음.
//   - 신호: 플러그인이 블록을 쓰면 앱을 깨운다 (POSIX 이름 있는 세마포어 / Windows 이름 있는 이벤트).
//     플러그인 쪽은 OS 객체로 기다리지 않는다. 방식 A 에서 결과를 기다릴 때는 시간 제한이 있는 바쁜 대기(spin)만 한다
//     (macOS 에는 시간 제한 있는 세마포어 대기가 없고, Windows 이벤트 대기는 1ms 단위라 블록 시간에 비해 거칠다).
//   - 플러그인의 process() 안에서는 메모리 할당·잠금·파일/네트워크 입출력을 하지 않는다 (sem_post/SetEvent 만 부른다).
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

namespace meon::bridge
{

constexpr uint32_t kMagic = 0x4d425247;   // 'MBRG'
constexpr uint32_t kVersion = 2;
constexpr int kMaxChannels = 2;
constexpr int kMaxBlock = 4096;
constexpr int kSlots = 8;

/** A = 같은 블록 안 왕복 (기다림), B = 한 블록 늦게 (기다리지 않음) */
enum class Mode : uint32_t { sameBlock = 0, nextBlock = 1 };

/** 단조 시계 (프로세스 사이에 같은 기준. 리눅스·macOS CLOCK_MONOTONIC, Windows QPC) */
int64_t nowNs();

struct Slot
{
    std::atomic<uint64_t> seq { 0 };
    std::atomic<int32_t> numSamples { 0 };
    std::atomic<int64_t> stampNs { 0 };   // 입력 칸: 플러그인이 쓴 시각 (출력 칸에도 그 값을 그대로 옮긴다)
    std::atomic<int64_t> doneNs { 0 };    // 출력 칸: 앱이 처리를 마친 시각
    float data[kMaxChannels][kMaxBlock];
};

/** 시간 분포 (µs 칸). 플러그인의 왕복 시간, 앱의 블록 처리 시간에 쓴다. 원자 변수만 있어서 공유 메모리에 둘 수 있다 */
struct RoundTripHistogram
{
    static constexpr int kBuckets = 12;
    static constexpr int64_t kEdgesUs[kBuckets] = { 50, 100, 200, 300, 500, 750, 1000, 1500, 2000, 3000, 5000, 10000 };
    std::atomic<uint64_t> counts[kBuckets + 1] {};
    std::atomic<int64_t> maxNs { 0 };
    void add (int64_t ns);
    void reset();
    /** 백분위(0~100) 이하가 들어가는 칸의 위쪽 경계 µs. 마지막 칸이면 최댓값 */
    double percentileUs (double p) const;
};

struct alignas (64) Shared
{
    std::atomic<uint32_t> initState { 0 };   // 0 = 아직, 1 = 만드는 중, 2 = 준비됨
    uint32_t magic, version, size;

    // 연결 주인 (플러그인 하나, 앱 하나). token 0 = 없음. beat = 마지막으로 살아 있다고 알린 시각(ns)
    alignas (64) std::atomic<uint64_t> pluginToken;
    std::atomic<int64_t> pluginBeatNs;
    alignas (64) std::atomic<uint64_t> appToken;
    std::atomic<int64_t> appBeatNs;

    // 형식 (플러그인 = DAW 가 정한다)
    alignas (64) std::atomic<uint32_t> formatSeq;
    std::atomic<uint32_t> sampleRate, blockSize, numChannels, mode;

    // 블록 번호: 플러그인이 마지막으로 쓴 입력 / 앱이 마지막으로 처리한 출력
    alignas (64) std::atomic<uint64_t> inSeq;
    alignas (64) std::atomic<uint64_t> outSeq;

    // 앱이 알리는 통계
    alignas (64) std::atomic<int64_t> appLastProcessNs, appMaxProcessNs;
    std::atomic<uint64_t> appProcessed, appSkipped, appFreeRun;
    RoundTripHistogram appProcess;   // 앱의 블록 처리 시간 분포 (플러그인 블록만)

    Slot in[kSlots];
    Slot out[kSlots];
};

static_assert (std::atomic<uint64_t>::is_always_lock_free && std::atomic<int64_t>::is_always_lock_free,
               "공유 메모리 안의 원자 변수는 잠금이 없어야 프로세스 사이에서 맞게 동작한다");

/** 공유 메모리 + 앱을 깨우는 신호. 이름 하나로 둘 다 만든다 (없으면 만들고, 있으면 연다). */
class Channel
{
public:
    Channel() = default;
    ~Channel();
    Channel (const Channel&) = delete;
    Channel& operator= (const Channel&) = delete;

    /** 기본 이름 "meonbrg". 시험은 다른 이름을 써서 서로 섞이지 않게 한다. 실패하면 false (errorText 에 이유) */
    bool open (const std::string& name);
    void close();
    bool isOpen() const { return shared != nullptr; }

    Shared* get() const { return shared; }
    void post();             // 앱 깨우기 (실시간 스레드에서 불러도 된다)
    void wait();             // 앱 쪽: 깨울 때까지 (시간 제한 없음. 앱의 시계 스레드가 주기적으로 깨운다)
    void drain();            // 쌓인 신호 비우기 (앱이 처음 붙을 때)

    std::string errorText;

    /** 시험용: 남은 공유 메모리·세마포어를 지운다 (POSIX). Windows 는 마지막 핸들을 닫으면 사라진다 */
    static void unlink (const std::string& name);

private:
    bool openOnce (const std::string& name);
    bool stale = false;          // 다른 버전이 남긴 것이라 지우고 다시 만들 수 있다
    Shared* shared = nullptr;
    void* mapHandle = nullptr;   // Windows
    void* sem = nullptr;         // sem_t* 또는 HANDLE
};

/** 클릭(임펄스)을 보내고 돌아온 위치로 왕복 지연(샘플)을 잰다. 실시간 안전 (할당 없음). */
class ClickMeter
{
public:
    void reset (int intervalSamples);
    /** 이 블록 입력을 측정 신호로 바꾼다 (무음 + 정해진 간격의 클릭) */
    void writeInput (float* const* in, int numChannels, int numSamples);
    /** 돌아온 출력에서 클릭을 찾는다. writeInput 과 같은 블록에서 부른다 */
    void readOutput (const float* const* out, int numChannels, int numSamples);

    std::atomic<int64_t> lastLatency { -1 }, minLatency { -1 }, maxLatency { -1 };
    std::atomic<uint64_t> sent { 0 }, received { 0 };

private:
    int64_t pos = 0, nextSend = 0, pendingAt = -1;
    int interval = 24000;
};

struct PluginStats
{
    std::atomic<uint64_t> blocks { 0 }, late { 0 }, notConnected { 0 }, otherOwner { 0 };
    std::atomic<uint64_t> waitSkipped { 0 };   // 방식 A: 앱이 앞 블록도 못 끝내서 기다리지 않은 블록 (앱이 멈췄을 때 DAW 시간을 쓰지 않게)
    std::atomic<int64_t> lastWaitNs { 0 }, maxWaitNs { 0 }, maxProcessNs { 0 };
    RoundTripHistogram roundTrip;   // 플러그인이 블록을 쓴 때부터 앱 결과가 보일 때까지 (방식과 상관없이 잰다)
    void reset();
};

/** 플러그인(DAW 안) 쪽. open·prepare·heartbeat 는 메시지 스레드, process 는 오디오 스레드. */
class PluginEndpoint
{
public:
    enum class State { closed, waitingForApp, connected, otherInstance };

    bool open (const std::string& name = "meonbrg");
    void close();
    /** DAW 의 샘플레이트·블록 크기 (prepareToPlay 에서) */
    void prepare (double sampleRate, int blockSize, int numChannels);
    /** 0.25초쯤마다 메시지 스레드에서: 살아 있다고 알리고, 주인 자리를 잡고, 앱이 살아 있는지 본다 */
    void heartbeat();

    /** 오디오 스레드. 출력이 없으면(앱 없음·늦음) 무음. 절대 오래 기다리지 않는다 (방식 A 도 timeoutFraction 안) */
    void process (const float* const* in, float* const* out, int numChannels, int numSamples);

    std::atomic<Mode> mode { Mode::nextBlock };
    std::atomic<float> timeoutFraction { 0.5f };   // 방식 A: 블록 시간에 대한 기다림 한도
    State state() const { return currentState.load(); }
    PluginStats stats;
    Channel channel;

private:
    struct Prepared { double sampleRate = 0; int blockSize = 0; int numChannels = 0; };
    uint64_t token = 0;
    uint64_t seq = 0;
    std::atomic<bool> owner { false };
    std::atomic<State> currentState { State::closed };
    std::atomic<double> rate { 48000.0 };
    Prepared lastPrepared;
};

/** 앱(독립 앱) 쪽. 별도 스레드에서 플러그인 블록을 박자 삼아 process 를 부른다.
    플러그인이 없을 때는 자기 시계로 무음 블록을 부른다 (합주 엔진이 멈추지 않게. 결과는 버린다). */
class AppEndpoint
{
public:
    using ProcessFn = std::function<void (const float* const* in, float* const* out, int numChannels, int numSamples)>;
    using FormatFn = std::function<void (double sampleRate, int blockSize, int numChannels)>;

    ~AppEndpoint() { stop(); }

    bool start (const std::string& name, ProcessFn process, FormatFn format, bool freeRunWhenAlone);
    void stop();
    bool pluginConnected() const;
    /** 플러그인 블록이 지금 오고 있는지 (0.1초 안에 왔음) */
    bool receivingBlocks() const;
    Channel channel;

    std::atomic<double> currentRate { 0.0 };
    std::atomic<int> currentBlock { 0 };
    std::atomic<bool> realtime { false };   // 처리 스레드가 실시간 우선순위를 얻었는지

private:
    void run();
    void clock();
    ProcessFn process;
    FormatFn format;
    bool freeRun = false;
    std::atomic<bool> running { false };
    std::thread worker, clockThread;
    uint64_t token = 0;
    uint64_t lastProcessed = 0;
    uint32_t lastFormatSeq = 0;
    std::atomic<bool> freeRunTick { false };
    std::atomic<int64_t> lastBlockNs { 0 };
};

/** 실시간 우선순위를 시도한다 (리눅스 SCHED_FIFO, macOS 시간 제약 정책, Windows 높은 우선순위). 됐으면 true */
bool makeCurrentThreadRealtime (double periodMs);

} // namespace meon::bridge
