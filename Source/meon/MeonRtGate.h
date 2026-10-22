// MEON - 오디오 스레드 관문 (E10, docs/REMAKE_PLAN.md 8.2).
// 오디오 스레드가 엔진 잠금(mCoreLock)을 기다리지 않게 한다.
//
//   오디오 스레드: Reader 를 만들고 entered() 를 본다. 원자 변수만 쓴다 (잠금·스핀·시스템 호출 없음).
//                  쓰는 쪽이 일하는 중이면 entered() == false → 그 블록은 기다리지 않고 건너뛴다.
//   쓰는 쪽:       Writer 를 만든다. 생성자는 오디오 스레드가 지금 읽고 있는 블록을 끝내고 나갈 때까지 기다린 뒤에 돌아온다.
//                  그동안·그 뒤 Writer 가 있는 동안 새로 들어오려는 블록은 건너뛴다. 그러니 Writer 가 있는 동안에는
//                  오디오 스레드가 같은 자료를 읽고 있지 않다 ("플래그만 세우고 바로 바꾸기" 가 아니다).
//
// 보장 (모든 연산 seq_cst, 데커 방식):
//   오디오: inside += 1  →  writers 읽기      쓰는 쪽: writers += 1  →  inside == 0 이 될 때까지 읽기
//   두 쪽 모두 "내 표시 → 상대 표시 읽기" 순서라, 하나의 전체 순서 안에서 적어도 한쪽은 상대 표시를 본다.
//   - 오디오가 writers == 0 을 봤다면 쓰는 쪽의 writers += 1 은 그 뒤다 → 쓰는 쪽은 inside 를 1 로 보고 exit 까지 기다린다.
//   - 쓰는 쪽이 inside == 0 을 봤다면 오디오의 inside += 1 은 그 뒤다 → 오디오는 writers ≥ 1 을 보고 건너뛴다.
//
// 규칙
//   - Writer 는 오디오 스레드가 Reader 안에 있는 동안 그 스레드에서 만들면 안 된다 (자기 자신을 기다린다).
//   - 잠금과 같이 쓸 때는 잠금을 먼저 잡고 Writer 를 만든다 (ScopedGatedWrite). 오디오 스레드는 그 잠금을 잡지 않으므로
//     기다림이 순환하지 않는다.
//   - Writer 는 겹쳐 만들어도 된다 (writers 는 개수다).
#pragma once

#include <JuceHeader.h>

#include <atomic>
#include <cstdint>
#include <thread>

namespace meon
{

class RtGate
{
public:
    RtGate() = default;
    RtGate (const RtGate&) = delete;
    RtGate& operator= (const RtGate&) = delete;

    /** 오디오 스레드. 블록마다 하나. 범위를 벗어나면 나간다. */
    class Reader
    {
    public:
        explicit Reader (RtGate& g) noexcept : gate (g), in (g.tryEnter()) {}
        ~Reader() noexcept { if (in) gate.exit(); }
        bool entered() const noexcept { return in; }

        Reader (const Reader&) = delete;
        Reader& operator= (const Reader&) = delete;
    private:
        RtGate& gate;
        const bool in;
    };

    /** 쓰는 쪽. 생성자가 돌아오면 오디오 스레드는 관문 안에 없다. release() 나 소멸자에서 연다. */
    class Writer
    {
    public:
        explicit Writer (RtGate& g) noexcept : gate (g) { gate.beginWrite(); }
        ~Writer() noexcept { release(); }
        /** 범위가 끝나기 전에 열 때 (예: 리스너를 부르기 전에). 두 번 불러도 된다. */
        void release() noexcept { if (held) { held = false; gate.endWrite(); } }

        Writer (const Writer&) = delete;
        Writer& operator= (const Writer&) = delete;
    private:
        RtGate& gate;
        bool held = true;
    };

    /** 건너뛴 블록 수 (진단·시험용). */
    std::uint64_t skippedBlocks() const noexcept { return skipped.load (std::memory_order_relaxed); }

private:
    bool tryEnter() noexcept
    {
        inside.fetch_add (1);                 // seq_cst
        if (writers.load() == 0)              // seq_cst
            return true;
        inside.fetch_sub (1);
        skipped.fetch_add (1, std::memory_order_relaxed);
        return false;
    }

    void exit() noexcept { inside.fetch_sub (1); }

    void beginWrite() noexcept
    {
        writers.fetch_add (1);                // seq_cst
        while (inside.load() != 0)            // seq_cst. 오디오 스레드가 지금 블록을 끝낼 때까지 (보통 한 블록 안)
            std::this_thread::yield();
    }

    void endWrite() noexcept { writers.fetch_sub (1); }

    std::atomic<int> writers { 0 };
    std::atomic<int> inside { 0 };
    std::atomic<std::uint64_t> skipped { 0 };
};

/** 쓰기 잠금을 먼저 잡고, 그다음 오디오 스레드가 관문에서 나갈 때까지 기다린다. 풀 때는 반대 순서. */
class ScopedGatedWrite
{
public:
    ScopedGatedWrite (juce::ReadWriteLock& l, RtGate& g) : lock (l), writer (g) {}
private:
    const juce::ScopedWriteLock lock;   // 선언 순서 = 생성 순서: 잠금 → 관문
    RtGate::Writer writer;
};

} // namespace meon
