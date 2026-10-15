// MEON core - 네트워크 스레드의 결과를 메시지 스레드로 넘기는 방법.
// 앱은 MessageManager::callAsync, 단위 테스트는 CallbackQueue (테스트가 직접 꺼내 실행한다).
#pragma once

#include <JuceHeader.h>
#include <deque>
#include <mutex>
#include <condition_variable>

namespace meon::core
{

using Deliver = std::function<void (std::function<void()>)>;

inline Deliver messageThreadDeliver()
{
    return [] (std::function<void()> fn) { juce::MessageManager::callAsync (std::move (fn)); };
}

/** 테스트용: 다른 스레드가 넣은 할 일을 테스트 스레드에서 꺼내 실행한다. */
class CallbackQueue
{
public:
    Deliver deliver()
    {
        return [this] (std::function<void()> fn)
        {
            { std::lock_guard<std::mutex> l (mutex); items.push_back (std::move (fn)); }
            cv.notify_all();
        };
    }

    /** done() 이 참이 되거나 timeoutMs 가 지날 때까지 할 일을 실행한다. done() 이 참이면 true */
    bool runUntil (std::function<bool()> done, int timeoutMs)
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
        while (! done())
        {
            std::function<void()> fn;
            {
                std::unique_lock<std::mutex> l (mutex);
                const auto now = juce::Time::getMillisecondCounter();
                if (now >= end) return done();
                cv.wait_for (l, std::chrono::milliseconds (end - now), [this] { return ! items.empty(); });
                if (items.empty()) continue;
                fn = std::move (items.front());
                items.pop_front();
            }
            if (fn) fn();
        }
        return true;
    }

private:
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<std::function<void()>> items;
};

} // namespace meon::core
