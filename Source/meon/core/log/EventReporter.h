// MEON core - 앱이 보내는 행동 기록 (POST /v1/events, 서버 docs/EVENTS.md 2장).
// 기록을 모았다가 첫 기록 뒤 delayMs 에 한 번에 보낸다 (한 요청 50개 이하). 보내고 잊는다: 서버는 언제나 204 를 주고,
// 실패해도 다시 보내지 않는다 (서버 규칙). 종료할 때 남은 것은 takeBatch() 로 꺼내 바로 보낸다 (MeonCore).
// 메시지 스레드 전용.
#pragma once

#include <JuceHeader.h>
#include "../util/Scheduler.h"

namespace meon::core
{

class EventReporter
{
public:
    using Send = std::function<void (const juce::Array<juce::var>& batch)>;

    static constexpr int maxBatch = 50;     // 서버가 한 요청에 받는 수 (넘으면 400)
    static constexpr int maxPending = 200;  // 보내지 못하고 쌓이면 오래된 것부터 버린다
    static constexpr int delayMs = 3000;    // 첫 기록 뒤 이만큼 모아서 보낸다 (방에 들어가 여러 명과 붙을 때 한 번에)

    EventReporter (Scheduler& s, Send sendFn) : scheduler (s), send (std::move (sendFn)) {}
    ~EventReporter() { scheduler.cancel (task); }

    /** type·props 는 EVENTS.md 2장 표대로. roomId 가 비어 있으면 넣지 않는다 */
    void add (const juce::String& type, const juce::String& roomId = {}, const juce::var& props = {})
    {
        auto* e = new juce::DynamicObject();
        e->setProperty ("type", type);
        e->setProperty ("at", juce::Time (scheduler.nowMs()).toISO8601 (true));
        if (roomId.isNotEmpty()) e->setProperty ("room_id", roomId);
        if (props.isObject()) e->setProperty ("props", props);
        pending.add (juce::var (e));
        if (pending.size() > maxPending) pending.removeRange (0, pending.size() - maxPending);

        if (task != 0) return;
        std::weak_ptr<bool> weak = alive;
        task = scheduler.post (delayMs, [this, weak]
        {
            if (weak.expired()) return;
            task = 0;
            flush();
        });
    }

    /** 모인 것을 바로 보낸다 (50개씩) */
    void flush()
    {
        scheduler.cancel (task);
        task = 0;
        while (! pending.isEmpty())
        {
            const auto batch = takeBatch();
            if (send) send (batch);
        }
    }

    /** 보낼 것 하나 (최대 50개)를 꺼낸다. 예약한 보내기는 그대로 둔다 (남은 것이 없으면 그때 아무것도 하지 않는다) */
    juce::Array<juce::var> takeBatch()
    {
        juce::Array<juce::var> batch;
        const int n = juce::jmin (maxBatch, pending.size());
        for (int i = 0; i < n; ++i) batch.add (pending.getReference (i));
        pending.removeRange (0, n);
        return batch;
    }

    int pendingCount() const { return pending.size(); }

private:
    Scheduler& scheduler;
    Send send;
    juce::Array<juce::var> pending;
    int task = 0;
    std::shared_ptr<bool> alive { std::make_shared<bool> (true) };
};

} // namespace meon::core
