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
    heartbeatTask = reconnectTask = 0;
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

void Realtime::scheduleHeartbeat()
{
    scheduler.cancel (heartbeatTask);
    std::weak_ptr<bool> weak = alive;
    heartbeatTask = scheduler.post (heartbeatMs, [this, weak]
    {
        if (weak.expired() || ! transportOpen) return;
        transport.send (json::obj ({ { "type", "ping" } }));
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
    const auto token = tokenProvider ? tokenProvider() : String();
    transport.send (json::obj ({ { "type", "auth" }, { "access_token", token }, { "build", build } }));
    scheduleHeartbeat();
}

void Realtime::transportMessage (const var& msg)
{
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

void Realtime::transportClosed()
{
    transportOpen = false;
    scheduler.cancel (heartbeatTask);
    heartbeatTask = 0;
    if (! wanted)
    {
        if (link != Link::offline) setLink (Link::offline);
        return;
    }
    setLink (link == Link::connecting ? Link::connecting : Link::reconnecting);
    AppLog::write ("realtime", LOGTXT ("연결이 닫힘 → ") + juce::String (backoffMs / 1000.0, 0) + LOGTXT ("초 뒤 다시 연결"));
    scheduleReconnect();
}

} // namespace meon::core
