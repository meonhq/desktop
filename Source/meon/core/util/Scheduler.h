// MEON core - 시계와 지연 실행. 앱은 메시지 스레드 타이머, 테스트는 손으로 돌리는 가짜 시계를 쓴다.
// core 의 시간 규칙(초대 3초, 요청 60초, 무음 5초 …)은 모두 이것을 거쳐서 테스트에서 시간을 건너뛸 수 있다.
#pragma once

#include <JuceHeader.h>
#include <map>

namespace meon::core
{

class Scheduler
{
public:
    virtual ~Scheduler() = default;

    /** 지금 시각 (밀리초, Unix epoch) */
    virtual juce::int64 nowMs() const = 0;

    /** delayMs 뒤 메시지 스레드에서 fn 실행. 같은 시각이면 넣은 순서대로. 돌려준 번호로 취소할 수 있다. */
    virtual int post (int delayMs, std::function<void()> fn) = 0;
    virtual void cancel (int id) = 0;
};

/** 앱용: juce::Timer 하나로 대기열을 돈다. 메시지 스레드에서만 쓴다. */
class RealScheduler : public Scheduler, private juce::Timer
{
public:
    RealScheduler() = default;
    ~RealScheduler() override { stopTimer(); }

    juce::int64 nowMs() const override { return juce::Time::currentTimeMillis(); }

    int post (int delayMs, std::function<void()> fn) override
    {
        const int id = ++lastId;
        queue.emplace (Key { nowMs() + juce::jmax (0, delayMs), id }, std::move (fn));
        reschedule();
        return id;
    }

    void cancel (int id) override
    {
        for (auto it = queue.begin(); it != queue.end(); ++it)
            if (it->first.id == id) { queue.erase (it); break; }
    }

private:
    struct Key
    {
        juce::int64 at; int id;
        bool operator< (const Key& o) const { return at != o.at ? at < o.at : id < o.id; }
    };
    std::map<Key, std::function<void()>> queue;
    int lastId = 0;

    void reschedule()
    {
        if (queue.empty()) { stopTimer(); return; }
        const auto wait = queue.begin()->first.at - nowMs();
        startTimer ((int) juce::jlimit<juce::int64> (1, 1000, wait));
    }

    void timerCallback() override
    {
        const auto now = nowMs();
        while (! queue.empty() && queue.begin()->first.at <= now)
        {
            auto fn = std::move (queue.begin()->second);
            queue.erase (queue.begin());
            if (fn) fn();
        }
        reschedule();
    }
};

/** 테스트용: advance() 로 시간을 옮기면 그사이 할 일을 시각 순서대로 실행한다. */
class ManualScheduler : public Scheduler
{
public:
    explicit ManualScheduler (juce::int64 startMs = 1791876780000)   // 2026-10-13T07:33:00Z
        : now (startMs) {}

    juce::int64 nowMs() const override { return now; }

    int post (int delayMs, std::function<void()> fn) override
    {
        const int id = ++lastId;
        queue.emplace (Key { now + juce::jmax (0, delayMs), id }, std::move (fn));
        return id;
    }

    void cancel (int id) override
    {
        for (auto it = queue.begin(); it != queue.end(); ++it)
            if (it->first.id == id) { queue.erase (it); break; }
    }

    /** ms 만큼 시간을 옮기며 할 일을 실행 (할 일이 새로 넣은 할 일도 기한 안이면 실행) */
    void advance (juce::int64 ms)
    {
        const auto end = now + ms;
        while (! queue.empty() && queue.begin()->first.at <= end)
        {
            now = juce::jmax (now, queue.begin()->first.at);
            auto fn = std::move (queue.begin()->second);
            queue.erase (queue.begin());
            if (fn) fn();
        }
        now = end;
    }

    /** 지금 시각에 할 일만 모두 실행 */
    void flush() { advance (0); }

    size_t pending() const { return queue.size(); }

private:
    struct Key
    {
        juce::int64 at; int id;
        bool operator< (const Key& o) const { return at != o.at ? at < o.at : id < o.id; }
    };
    std::map<Key, std::function<void()>> queue;
    juce::int64 now;
    int lastId = 0;
};

} // namespace meon::core
