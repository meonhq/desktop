// MEON core - 같은 컴퓨터의 다른 MEON(독립 앱·플러그인 인스턴스)과 토큰 갱신을 나눠 쓴다 (R11).
// 모두 같은 로그인 토큰 저장소(TokenStore)를 쓰지만 토큰을 각자 메모리에 들고 있다. 서로 모르고 갱신하면
// 한쪽이 들고 있던 토큰이 서버에서 폐기되고, 그 토큰으로 다시 갱신할 때 재사용(도용)으로 걸려 모두 로그아웃된다 (API 1.3).
// 그래서 갱신은 프로세스 사이 잠금 안에서 하고, 저장소의 지금 토큰을 다시 읽어 쓴다.
#pragma once

#include "../api/MeonApi.h"
#include "../util/Scheduler.h"
#include "TokenStore.h"

namespace meon::core
{

class RefreshLock : public RefreshCoordinator
{
public:
    /** name: 서버마다 다른 이름 (토큰 저장 항목 이름) */
    RefreshLock (const juce::String& name, TokenStore& t, Scheduler& s)
        : lock ("MEON-refresh-" + juce::String::toHexString (name.hashCode64())), tokens (t), scheduler (s) {}

    ~RefreshLock() override
    {
        *alive = false;
        if (held) lock.exit();
    }

    bool tryLock() override
    {
        if (held) return true;
        held = lock.enter (0);
        return held;
    }

    void unlock() override
    {
        if (! held) return;
        held = false;
        lock.exit();
    }

    juce::String storedRefreshToken() override { return tokens.load(); }

    void retryLater (int ms, std::function<void()> fn) override
    {
        std::weak_ptr<bool> weak = alive;
        scheduler.post (ms, [weak, fn = std::move (fn)] { if (! weak.expired() && fn) fn(); });
    }

private:
    juce::InterProcessLock lock;
    TokenStore& tokens;
    Scheduler& scheduler;
    bool held = false;
    std::shared_ptr<bool> alive { std::make_shared<bool> (true) };

    JUCE_DECLARE_NON_COPYABLE (RefreshLock)
};

} // namespace meon::core
