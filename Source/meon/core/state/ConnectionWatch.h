// MEON core - 연결 끊김 표시 (홈·합주 화면의 "연결이 끊겼어요" 줄과 "다시 연결됐어요" 토스트).
//   끊김: 서버 실시간 연결(WebSocket)이 다시 연결 중이거나, 방 안에서 합주 서버(AOO)와 끊김 (JamSession::isJamServerDown).
//   끊긴 지 showDelayMs 가 지나야 띄운다 (아주 짧게 끊겼다 붙으면 깜빡이지 않게). 다시 붙으면 바로 내리고 "다시 연결됐어요".
//   4000(다른 곳에서 접속)·4001(세션 폐기)·로그아웃처럼 일부러 끊은 경우는 띄우지 않는다 (다른 화면이 처리한다).
//   끊긴 사이 서버가 방에서 내보냈으면(다시 붙었을 때 hello 의 current_room 이 null) "다시 연결됐어요" 대신
//   "연결이 오래 끊겨서 방에서 나왔어요" 만 띄운다 (AppStore 의 Notice::roomLostWhileDisconnected → roomLost).
#pragma once

#include "../api/Realtime.h"
#include "../util/Scheduler.h"

namespace meon::core
{

class ConnectionWatch
{
public:
    enum class Input { connected, lost, stopped };

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void connectionShownChanged (bool shown) { juce::ignoreUnused (shown); }
        /** 표시를 띄웠던 끊김에서 다시 붙었다 ("다시 연결됐어요") */
        virtual void connectionRestored() {}
    };

    static constexpr int showDelayMs = 2000;

    explicit ConnectionWatch (Scheduler& s) : scheduler (s) {}
    ~ConnectionWatch()
    {
        *alive = false;
        scheduler.cancel (showTask);
        scheduler.cancel (restoreTask);
    }

    void addListener (Listener* l)    { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    bool isShown() const { return shown; }

    /** 서버 실시간 연결 상태와 합주 서버 끊김을 합친 입력 */
    static Input inputFor (Realtime::Link link, bool jamServerDown)
    {
        using L = Realtime::Link;
        if (link == L::replaced || link == L::offline) return Input::stopped;   // 4000·4001·로그아웃·아직 켜지 않음
        if (link == L::reconnecting || jamServerDown) return Input::lost;
        return Input::connected;                                             // 처음 연결 중(connecting)도 표시하지 않는다
    }

    void update (Input in)
    {
        if (in == Input::lost)
        {
            if (down) return;
            down = true;
            roomWasLost = false;
            scheduler.cancel (restoreTask);
            restoreTask = 0;
            std::weak_ptr<bool> weak = alive;
            showTask = scheduler.post (showDelayMs, [this, weak]
            {
                if (weak.expired()) return;
                showTask = 0;
                if (down) setShown (true);
            });
            return;
        }

        if (! down) return;
        down = false;
        scheduler.cancel (showTask);
        showTask = 0;
        if (! shown) return;   // 2초 안에 다시 붙음 (또는 일부러 끊음): 아무것도 띄우지 않았다
        setShown (false);
        if (in == Input::stopped) return;

        // 다시 붙었다. 같은 순간에 오는 hello 가 방이 사라졌다고 알릴 수 있어서 다음 차례에 토스트를 고른다
        std::weak_ptr<bool> weak = alive;
        restoreTask = scheduler.post (0, [this, weak]
        {
            if (weak.expired()) return;
            restoreTask = 0;
            if (roomWasLost) return;
            listeners.call ([] (Listener& l) { l.connectionRestored(); });
        });
    }

    /** 다시 연결했더니 방이 없다 (끊긴 사이 서버가 내보냄): "다시 연결됐어요" 를 띄우지 않는다 */
    void roomLost() { roomWasLost = true; }

private:
    Scheduler& scheduler;
    juce::ListenerList<Listener> listeners;
    std::shared_ptr<bool> alive { std::make_shared<bool> (true) };
    bool down = false, shown = false, roomWasLost = false;
    int showTask = 0, restoreTask = 0;

    void setShown (bool s)
    {
        if (shown == s) return;
        shown = s;
        listeners.call ([s] (Listener& l) { l.connectionShownChanged (s); });
    }
};

} // namespace meon::core
