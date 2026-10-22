// MEON core - 연결 끊김 토스트의 상태 (홈·합주 화면 공통. 화면은 AppRoot 가 기존 토스트로 띄운다).
//   끊김: 서버 실시간 연결(WebSocket)이 다시 연결 중이거나, 방 안에서 합주 서버(AOO)와 끊김 (JamSession::isJamServerDown).
//   - 끊긴 지 showDelayMs 가 지나야 lost (경고 토스트 "연결이 끊겼어요"). 아주 짧게 끊겼다 붙으면 아무것도 없다.
//     끊긴 동안은 계속 lost (저절로 닫히지 않는다)
//   - 다시 붙으면 같은 자리의 토스트가 restored (일반 토스트 "다시 연결됐어요")로 바뀌고 restoredShowMs 뒤 none
//   - 4000(다른 곳에서 접속)·4001(세션 폐기)·로그아웃처럼 일부러 끊은 경우는 띄우지 않는다 (떠 있으면 바로 none)
//   - 끊긴 동안 이유(JamSession::serverProblem)가 바뀌면 lost 를 한 번 더 알린다 (detailChanged): 화면이 문구를 다시 고른다
//   - 끊긴 사이 서버가 방에서 내보냈으면(다시 붙었을 때 hello 의 current_room 이 null) restored 없이 바로 none.
//     그쪽은 AppStore 의 Notice::roomLostWhileDisconnected 로 AppRoot 가 경고 토스트 "방에서 나왔어요" 를 띄운다 (roomLost)
#pragma once

#include "../api/Realtime.h"
#include "../util/Scheduler.h"

namespace meon::core
{

class ConnectionWatch
{
public:
    enum class Input { connected, lost, stopped };
    enum class Toast { none, lost, restored };

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void connectionToastChanged (Toast) {}
    };

    static constexpr int showDelayMs = 2000;
    static constexpr int restoredShowMs = 3000;

    explicit ConnectionWatch (Scheduler& s) : scheduler (s) {}
    ~ConnectionWatch()
    {
        *alive = false;
        scheduler.cancel (showTask);
        scheduler.cancel (restoreTask);
        scheduler.cancel (hideTask);
    }

    void addListener (Listener* l)    { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    Toast toast() const { return state; }

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
                if (! down) return;
                scheduler.cancel (hideTask);   // "다시 연결됐어요" 를 보이던 중이면 다시 끊김으로
                hideTask = 0;
                setState (Toast::lost);
            });
            return;
        }

        if (! down) return;
        down = false;
        scheduler.cancel (showTask);
        showTask = 0;
        if (state != Toast::lost) return;   // 2초 안에 다시 붙음 (또는 일부러 끊음): 아무것도 띄우지 않았다
        if (in == Input::stopped) { setState (Toast::none); return; }

        // 다시 붙었다. 같은 순간에 오는 hello 가 방이 사라졌다고 알릴 수 있어서 다음 차례에 고른다
        std::weak_ptr<bool> weak = alive;
        restoreTask = scheduler.post (0, [this, weak]
        {
            if (weak.expired()) return;
            restoreTask = 0;
            if (roomWasLost) { setState (Toast::none); return; }
            setState (Toast::restored);
            hideTask = scheduler.post (restoredShowMs, [this, weak]
            {
                if (weak.expired()) return;
                hideTask = 0;
                if (state == Toast::restored) setState (Toast::none);
            });
        });
    }

    /** 다시 연결했더니 방이 없다 (끊긴 사이 서버가 내보냄): "다시 연결됐어요" 를 띄우지 않는다 */
    void roomLost() { roomWasLost = true; }

    /** 끊긴 동안 그 이유가 바뀌었다 (합주 서버 UDP 막힘 등): 떠 있는 끊김 토스트가 문구를 다시 고르게 같은 상태를 한 번 더 알린다 */
    void detailChanged()
    {
        if (state == Toast::lost)
            listeners.call ([] (Listener& l) { l.connectionToastChanged (Toast::lost); });
    }

private:
    Scheduler& scheduler;
    juce::ListenerList<Listener> listeners;
    std::shared_ptr<bool> alive { std::make_shared<bool> (true) };
    bool down = false, roomWasLost = false;
    Toast state = Toast::none;
    int showTask = 0, restoreTask = 0, hideTask = 0;

    void setState (Toast t)
    {
        if (state == t) return;
        state = t;
        listeners.call ([t] (Listener& l) { l.connectionToastChanged (t); });
    }
};

} // namespace meon::core
