#include "Realtime.h"
#include "../log/AppLog.h"
#include "ApiJson.h"

namespace meon::core
{

using juce::String;
using juce::var;

RealtimeEvent::Kind RealtimeEvent::kindFromType (const String& t)
{
    static const std::pair<const char*, Kind> map[] = {
        { "rooms.added", Kind::roomsAdded }, { "rooms.updated", Kind::roomsUpdated }, { "rooms.removed", Kind::roomsRemoved },
        { "room.updated", Kind::roomUpdated }, { "room.kicked", Kind::roomKicked }, { "room.closed", Kind::roomClosed },
        { "join_request.received", Kind::joinRequestReceived }, { "join_request.removed", Kind::joinRequestRemoved },
        { "join_request.accepted", Kind::joinRequestAccepted },
        { "friend.updated", Kind::friendUpdated }, { "friend.added", Kind::friendAdded }, { "friend.removed", Kind::friendRemoved },
        { "friend_request.received", Kind::friendRequestReceived },
        { "invite.received", Kind::inviteReceived }, { "invite.expired", Kind::inviteExpired },
        { "config.updated", Kind::configUpdated },
    };
    for (auto& m : map)
        if (t == m.first) return m.second;
    return Kind::unknown;
}

var RealtimeEvent::encode (const String& type, int seq, const var& data)
{
    return json::obj ({ { "type", type }, { "seq", seq }, { "data", data } });
}

template <typename T>
static bool decodeInto (const var& v, std::optional<T>& out)
{
    T t;
    if (! json::decode (v, t)) return false;
    out = std::move (t);
    return true;
}

static bool stringField (const var& data, const char* key, String& out)
{
    auto v = data.getProperty (key, var());
    if (! v.isString()) return false;
    out = v.toString();
    return true;
}

bool RealtimeEvent::decode (const var& msg, RealtimeEvent& e)
{
    e = {};
    e.type = msg.getProperty ("type", "").toString();
    e.seq = (int) msg.getProperty ("seq", 0);
    e.kind = kindFromType (e.type);
    const auto data = msg.getProperty ("data", var());

    switch (e.kind)
    {
        case Kind::roomsAdded:
        case Kind::roomsUpdated:         return decodeInto (data, e.room);
        case Kind::roomsRemoved:
        case Kind::roomKicked:           return stringField (data, "room_id", e.roomId);
        case Kind::roomClosed:           return stringField (data, "room_id", e.roomId) && stringField (data, "reason", e.reason);
        case Kind::roomUpdated:          return decodeInto (data, e.roomDetail);
        case Kind::joinRequestReceived:  return decodeInto (data, e.joinRequest);
        case Kind::joinRequestRemoved:   return stringField (data, "request_id", e.requestId);
        case Kind::joinRequestAccepted:  return decodeInto (data.getProperty ("room", var()), e.roomDetail);
        case Kind::friendUpdated:
        case Kind::friendAdded:          return decodeInto (data, e.friendInfo);
        case Kind::friendRemoved:        return stringField (data, "user_id", e.userId);
        case Kind::friendRequestReceived:return decodeInto (data, e.friendRequest);
        case Kind::inviteReceived:       return decodeInto (data, e.invite);
        case Kind::inviteExpired:        return stringField (data, "invite_id", e.inviteId);
        case Kind::configUpdated:        return decodeInto (data, e.config);
        case Kind::unknown:              return false;
    }
    return false;
}

//==============================================================================
Realtime::Realtime (RealtimeTransport& t, Scheduler& s, int buildNumber) : transport (t), scheduler (s), build (buildNumber) {}

Realtime::~Realtime()
{
    *alive = false;
    scheduler.cancel (heartbeatTask);
    scheduler.cancel (reconnectTask);
    if (transportOpen) transport.close();
}

static juce::String linkName (Realtime::Link l)
{
    switch (l)
    {
        case Realtime::Link::offline:      return LOGTXT ("꺼짐");
        case Realtime::Link::connecting:   return LOGTXT ("연결 중");
        case Realtime::Link::online:       return LOGTXT ("연결됨");
        case Realtime::Link::reconnecting: return LOGTXT ("끊김, 다시 연결 중");
        case Realtime::Link::replaced:     return LOGTXT ("다른 곳에서 접속해 끊김");
    }
    return LOGTXT ("?");
}

void Realtime::setLink (Link l)
{
    if (link == l) return;
    AppLog::write ("realtime", juce::String (LOGTXT ("실시간 연결: ")) + linkName (link) + LOGTXT (" → ") + linkName (l));
    link = l;
    listeners.call ([l] (Listener& x) { x.linkChanged (l); });
}

void Realtime::connect (std::function<String()> accessToken)
{
    tokenProvider = std::move (accessToken);
    wanted = true;
    backoffMs = 1000;
    scheduler.cancel (reconnectTask);
    reconnectTask = 0;
    setLink (Link::connecting);
    openTransport();
}

void Realtime::disconnect()
{
    wanted = false;
    scheduler.cancel (heartbeatTask);
    scheduler.cancel (reconnectTask);
    scheduler.cancel (pongTask);
    heartbeatTask = reconnectTask = pongTask = 0;
    if (transportOpen)
    {
        transportOpen = false;
        transport.close();
    }
    setLink (Link::offline);
}

void Realtime::openTransport()
{
    lastSeq = 0;
    transport.open (this);
}

void Realtime::subscribePublicRooms (bool on)
{
    if (publicRooms == on) return;
    publicRooms = on;
    if (link == Link::online) sendSubscription();
}

void Realtime::sendSubscription()
{
    transport.send (json::obj ({ { "type", publicRooms ? "subscribe" : "unsubscribe" }, { "topic", "public_rooms" } }));
}

void Realtime::resync()
{
    if (link == Link::online)
        transport.send (json::obj ({ { "type", "resync" } }));
}

void Realtime::checkNow()
{
    if (! transportOpen || link != Link::online || pongTask != 0) return;
    if (scheduler.nowMs() - lastCheckMs < checkGapMs) return;
    lastCheckMs = scheduler.nowMs();
    AppLog::write ("realtime", LOGTXT ("연결 확인 (ping)"));
    sendPing();
}

void Realtime::sendPing()
{
    transport.send (json::obj ({ { "type", "ping" } }));
    if (pongTask != 0) return;   // 이미 답을 기다리는 중
    const auto sentAt = scheduler.nowMs();
    std::weak_ptr<bool> weak = alive;
    pongTask = scheduler.post (pongTimeoutMs, [this, weak, sentAt]
    {
        if (weak.expired()) return;
        pongTask = 0;
        if (! transportOpen || lastReceivedMs >= sentAt) return;
        dropDeadLink (LOGTXT ("ping 을 보내고 ") + juce::String (pongTimeoutMs / 1000) + LOGTXT ("초 동안 답이 없음"));
    });
}

void Realtime::dropDeadLink (const juce::String& why)
{
    AppLog::write ("realtime", why + LOGTXT (" → 다시 연결"));
    transport.close();
    transportClosed (0);
}

void Realtime::scheduleHeartbeat()
{
    scheduler.cancel (heartbeatTask);
    std::weak_ptr<bool> weak = alive;
    heartbeatTask = scheduler.post (heartbeatMs, [this, weak]
    {
        if (weak.expired() || ! transportOpen) return;
        // 끊긴 줄 모르는 연결(공유기가 조용히 끊은 경우 등): 서버의 pong 도 오지 않으면 닫고 다시 연결한다
        if (scheduler.nowMs() - lastReceivedMs > silenceLimitMs)
        {
            dropDeadLink (LOGTXT ("서버에서 ") + juce::String (silenceLimitMs / 1000) + LOGTXT ("초 넘게 아무것도 오지 않음"));
            return;
        }
        sendPing();
        scheduleHeartbeat();
    });
}

void Realtime::scheduleReconnect()
{
    scheduler.cancel (reconnectTask);
    std::weak_ptr<bool> weak = alive;
    const int wait = backoffMs;
    backoffMs = juce::jmin (backoffMs * 2, maxBackoffMs);
    reconnectTask = scheduler.post (wait, [this, weak]
    {
        if (weak.expired() || ! wanted) return;
        reconnectTask = 0;
        openTransport();
    });
}

void Realtime::transportOpened()
{
    transportOpen = true;
    lastReceivedMs = scheduler.nowMs();
    const auto token = tokenProvider ? tokenProvider() : String();
    transport.send (json::obj ({ { "type", "auth" }, { "access_token", token }, { "build", build } }));
    scheduleHeartbeat();
}

void Realtime::transportMessage (const var& msg)
{
    lastReceivedMs = scheduler.nowMs();
    const auto type = msg.getProperty ("type", "").toString();
    if (type == "pong") return;

    if (type == "error")
    {
        const auto code = msg.getProperty ("code", "").toString();
        AppLog::write ("realtime", LOGTXT ("서버가 오류를 보냄: ") + code);
        wanted = false;   // 같은 토큰으로 다시 붙어도 같은 결과. 위층이 토큰을 고친 뒤 connect() 를 다시 부른다
        listeners.call ([&code] (Listener& l) { l.authRejected (code); });
        return;
    }

    const int seq = (int) msg.getProperty ("seq", 0);

    if (type == "hello")
    {
        Hello h;
        if (! json::decode (msg.getProperty ("data", var()), h)) return;
        lastSeq = seq;
        backoffMs = 1000;
        setLink (Link::online);
        if (publicRooms) sendSubscription();
        listeners.call ([&h] (Listener& l) { l.hello (h); });
        return;
    }

    if (link != Link::online) return;

    if (seq != lastSeq + 1)
    {
        // 빠진 번호: 전체 상태를 다시 받는다. 그 hello 가 오기 전까지 오는 이벤트는 hello 가 덮어쓴다
        lastSeq = seq;
        resync();
        return;
    }
    lastSeq = seq;

    RealtimeEvent e;
    if (! RealtimeEvent::decode (msg, e)) return;   // 모르는 이벤트는 무시 (서버가 먼저 새 이벤트를 넣을 수 있다)
    listeners.call ([&e] (Listener& l) { l.event (e); });
}

void Realtime::transportClosed (int code)
{
    transportOpen = false;
    scheduler.cancel (heartbeatTask);
    scheduler.cancel (pongTask);
    heartbeatTask = pongTask = 0;

    // 닫는 코드별 동작 (API 10.1). 1008(인증 실패)은 앞서 온 error 메시지가 이미 처리했다 (wanted = false)
    if (code == closeReplaced)
    {
        AppLog::write ("realtime", LOGTXT ("다른 곳에서 같은 계정으로 접속해서 연결이 닫힘 (4000) → 다시 접속하지 않음"));
        wanted = false;
        scheduler.cancel (reconnectTask);
        reconnectTask = 0;
        setLink (Link::replaced);
        return;
    }
    if (code == closeSessionRevoked)
    {
        AppLog::write ("realtime", LOGTXT ("서버가 세션을 폐기함 (4001) → 로그아웃"));
        wanted = false;
        scheduler.cancel (reconnectTask);
        reconnectTask = 0;
        setLink (Link::offline);
        listeners.call ([] (Listener& l) { l.sessionRevoked(); });
        return;
    }
    if (code == closeSlow)
        AppLog::write ("realtime", LOGTXT ("서버가 느린 연결로 보고 닫음 (4002) → 다시 연결"));

    if (! wanted)
    {
        if (link != Link::offline) setLink (Link::offline);
        return;
    }
    setLink (link == Link::connecting ? Link::connecting : Link::reconnecting);
    AppLog::write ("realtime", LOGTXT ("연결이 닫힘 (코드 ") + juce::String (code) + LOGTXT (") → ") + juce::String (backoffMs / 1000.0, 0) + LOGTXT ("초 뒤 다시 연결"));
    scheduleReconnect();
}

} // namespace meon::core
