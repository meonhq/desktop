// MEON core - 내 AOO 서버 왕복 시간 (추가 반영 2, API 5.9). 앱을 켤 때 한 번, 그 뒤 홈이든 방이든 1분마다 재서 보고한다.
// 측정: 서버에 TCP 로 접속하는 데 걸린 시간 = 왕복 1회. 3번 재서 가장 작은 값 (버그 #11: 주기 1분, 별도 스레드).
#pragma once

#include <JuceHeader.h>

namespace meon::core
{

class RttReporter : private juce::Thread
{
public:
    /** onMeasured 는 메시지 스레드에서 불린다 (재지 못하면 부르지 않는다) */
    explicit RttReporter (std::function<void (int ms)> onMeasured)
        : juce::Thread ("MEON rtt"), callback (std::move (onMeasured)) {}

    ~RttReporter() override { stop(); }

    void start (const juce::String& h, int p)
    {
        {
            const juce::ScopedLock sl (lock);
            host = h;
            port = p;
        }
        if (! isThreadRunning()) startThread();
        else notify();
    }

    void stop()
    {
        signalThreadShouldExit();
        notify();
        stopThread (4000);
    }

    static constexpr int intervalMs = 60000;
    static constexpr int tries = 3;

    /** 한 번 잰다 (가장 작은 값, 실패하면 -1) */
    static int measure (const juce::String& host, int port)
    {
        double best = -1.0;
        for (int i = 0; i < tries; ++i)
        {
            juce::StreamingSocket sock;
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            if (sock.connect (host, port, 3000))
            {
                const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
                best = best < 0.0 ? ms : juce::jmin (best, ms);
            }
            sock.close();
        }
        return best < 0.0 ? -1 : juce::jmax (1, (int) std::lround (best));
    }

private:
    std::function<void (int)> callback;
    juce::CriticalSection lock;
    juce::String host;
    int port = 0;

    void run() override
    {
        while (! threadShouldExit())
        {
            juce::String h;
            int p = 0;
            {
                const juce::ScopedLock sl (lock);
                h = host;
                p = port;
            }
            const int ms = measure (h, p);
            if (ms > 0 && ! threadShouldExit())
            {
                auto cb = callback;
                juce::MessageManager::callAsync ([cb, ms] { if (cb) cb (ms); });
            }
            wait (intervalMs);
        }
    }
};

} // namespace meon::core
