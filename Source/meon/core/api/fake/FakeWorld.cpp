// MEON core - 가짜 서버의 다른 사용자들: fixtures/world.json 을 읽고, 각본대로 방·친구 상태를 바꾼다.
// 각본은 내가 들어 있는 방은 건드리지 않는다 (두 컴퓨터로 소리를 확인할 때 가짜 멤버가 끼어들지 않게).
#include "FakeMeonServer.h"
#include "../ApiJson.h"

namespace meon::core
{

using juce::String;
using juce::var;

void FakeMeonServer::loadWorld (const var& world)
{
    worldData = world;
    json::decode (world.getProperty ("config", var()), config);

    if (auto* arr = world.getProperty ("users", var()).getArray())
    {
        for (auto& w : *arr)
        {
            User u;
            u.id = w.getProperty ("id", "").toString();
            u.username = w.getProperty ("username", "").toString();
            u.email = u.username + "@example.com";
            u.nickname = w.getProperty ("nickname", "").toString();
            u.part = w.getProperty ("part", "other").toString();
            u.verified = true;
            u.online = true;
            u.scripted = true;
            u.createdAt = now() - 30LL * 24 * 3600 * 1000;
            if (w.hasProperty ("rtt_ms")) u.rttMs = (int) w.getProperty ("rtt_ms", 0);
            u.aooPassword = juce::Uuid().toString();
            users[u.id] = u;
        }
    }

    auto makeRoom = [this] (const var& w, const String& id, juce::int64 createdAt)
    {
        RoomRec r;
        r.id = id;
        r.type = "public";
        r.entryMode = w.getProperty ("entry_mode", "instant").toString();
        r.title = w.getProperty ("title", "").toString();
        r.description = w.getProperty ("description", "").toString();
        r.capacity = (int) w.getProperty ("capacity", 7);
        r.hostId = w.getProperty ("host", "").toString();
        r.groupName = "meon-fake-" + id;
        r.createdAt = createdAt;
        if (auto* ms = w.getProperty ("members", var()).getArray())
        {
            juce::int64 t = createdAt;
            for (auto& m : *ms)
                r.members.push_back ({ m.toString(), t += 60000 });
        }
        return r;
    };

    if (auto* arr = world.getProperty ("rooms", var()).getArray())
        for (auto& w : *arr)
        {
            const auto id = w.getProperty ("id", "").toString();
            rooms[id] = makeRoom (w, id, now() - (juce::int64) (int) w.getProperty ("age_min", 10) * 60000);
        }

    extraPool.clear();
    if (auto* arr = world.getProperty ("extra_rooms", var()).getArray())
        for (auto& w : *arr) extraPool.push_back (w);

    auto strings = [&world] (const char* key)
    {
        juce::StringArray out;
        if (auto* arr = world.getProperty (key, var()).getArray())
            for (auto& s : *arr) out.add (s.toString());
        return out;
    };
    joiners = strings ("joiners");
    joinRequesters = strings ("join_requesters");
    inviters = strings ("inviters");
    worldFriendRequests = strings ("friend_requests");
    worldBlocks = strings ("blocks");

    worldFriends.clear();
    if (auto* arr = world.getProperty ("friends", var()).getArray())
    {
        for (auto& f : *arr)
        {
            const auto uid = f.getProperty ("user", "").toString();
            worldFriends.push_back ({ uid, f });
            if (auto* u = findUser (uid))
            {
                const auto status = f.getProperty ("status", "online").toString();
                u->online = status != "offline";
                if (status == "offline")
                    u->lastSeenAt = now() - (juce::int64) ((double) f.getProperty ("last_seen_hours", 1) * 3600000.0);
            }
        }
    }
    // 친구가 아닌 가짜 사용자 중 방에 없는 사람은 접속 중, 친구 목록의 오프라인 친구만 오프라인

    if (auto* codes = world.getProperty ("codes", var()).getDynamicObject())
        for (auto& p : codes->getProperties())
            fixedCodes[p.name.toString()] = p.value.toString();
}

void FakeMeonServer::attachWorld (User& me)
{
    if (me.worldAttached) return;
    me.worldAttached = true;

    for (auto& [uid, f] : worldFriends)
        if (findUser (uid) != nullptr) friendships.insert (me.id < uid ? me.id + "|" + uid : uid + "|" + me.id);

    juce::int64 t = now() - 20 * 60000;
    for (auto& uid : worldFriendRequests)
        if (findUser (uid) != nullptr)
            friendRequests.push_back ({ newId ("fr_"), uid, me.id, t += 60000 });

    for (auto& uid : worldBlocks)
        if (findUser (uid) != nullptr) blocks[me.id][uid] = now() - 3LL * 24 * 3600 * 1000;
}

void FakeMeonServer::setScriptsEnabled (bool on)
{
    options.scripts = on;
    if (! on) { scheduler.cancel (scriptTask); scriptTask = 0; }
    else scheduleScript();
}

void FakeMeonServer::scheduleScript()
{
    if (! options.scripts || scriptTask != 0) return;
    std::weak_ptr<bool> weak = alive;
    const int wait = (int) ((6000 + random.nextInt (6000)) / juce::jmax (0.1, options.scriptSpeed));
    scriptTask = scheduler.post (wait, [this, weak]
    {
        if (weak.expired()) return;
        scriptTask = 0;
        scriptTick();
        scheduleScript();
    });
}

String FakeMeonServer::randomUserNotInRoom (const juce::StringArray& from)
{
    juce::StringArray free;
    for (auto& uid : from)
        if (findUser (uid) != nullptr && roomOf (uid) == nullptr && ! areFriends (wsUser, uid)) free.add (uid);
    return free.isEmpty() ? String() : free[random.nextInt (free.size())];
}

void FakeMeonServer::scriptTick()
{
    if (wsUser.isEmpty() || ! wsOpen) return;
    auto* mine = roomOf (wsUser);

    // 각본이 건드려도 되는 공개 방 (내가 없는 방)
    std::vector<RoomRec*> open;
    for (auto& [id, r] : rooms)
        if (r.type == "public" && &r != mine && ! r.id.startsWith ("r_code_")) open.push_back (&r);

    switch (random.nextInt (6))
    {
        case 0:   // 누군가 방에 들어옴
        {
            const auto uid = randomUserNotInRoom (joiners);
            if (uid.isEmpty() || open.empty()) break;
            auto* r = open[(size_t) random.nextInt ((int) open.size())];
            if (! hasRoomFor (*r, uid)) break;
            r->members.push_back ({ uid, now() });
            roomChanged (*r);
            friendChanged (uid);
            break;
        }
        case 1:   // 방장이 아닌 멤버가 나감
        {
            if (open.empty()) break;
            auto* r = open[(size_t) random.nextInt ((int) open.size())];
            for (auto& m : r->members)
            {
                if (m.userId == r->hostId || areFriends (wsUser, m.userId)) continue;
                const auto uid = m.userId;
                removeMember (*r, uid);
                roomChanged (*r);
                friendChanged (uid);
                break;
            }
            break;
        }
        case 2:   // 새 방이 생김 (extra_rooms 중 없는 것)
        {
            for (auto& w : extraPool)
            {
                const auto host = w.getProperty ("host", "").toString();
                if (findUser (host) == nullptr || roomOf (host) != nullptr) continue;
                RoomRec r;
                r.id = newId ("r_");
                r.type = "public";
                r.entryMode = w.getProperty ("entry_mode", "instant").toString();
                r.title = w.getProperty ("title", "").toString();
                r.description = w.getProperty ("description", "").toString();
                r.capacity = (int) w.getProperty ("capacity", 7);
                r.hostId = host;
                r.groupName = "meon-fake-" + r.id;
                r.createdAt = now();
                r.extra = true;
                if (auto* ms = w.getProperty ("members", var()).getArray())
                    for (auto& m : *ms)
                        if (roomOf (m.toString()) == nullptr) r.members.push_back ({ m.toString(), now() });
                rooms[r.id] = r;
                emitRooms ("rooms.added", rooms[r.id]);
                break;
            }
            break;
        }
        case 3:   // 각본이 만든 방이 닫힘
        {
            for (auto* r : open)
                if (r->extra) { deleteRoom (r->id, false); break; }
            break;
        }
        case 4:   // 친구 상태 바뀜 (접속·종료). 방에 있는 친구는 그대로
        {
            juce::StringArray candidates;
            for (auto& [uid, f] : worldFriends)
                if (findUser (uid) != nullptr && roomOf (uid) == nullptr) candidates.add (uid);
            if (candidates.isEmpty()) break;
            auto* u = findUser (candidates[random.nextInt (candidates.size())]);
            u->online = ! u->online;
            if (! u->online) u->lastSeenAt = now();
            friendChanged (u->id);
            break;
        }
        case 5:   // 내가 방장인 승인 방이면 가끔 요청이 온다
        {
            if (mine != nullptr && mine->hostId == wsUser && mine->entryMode == String ("approval") && random.nextBool())
                devJoinRequest();
            break;
        }
        default: break;
    }
}

void FakeMeonServer::devFriendRequest()
{
    for (auto& uid : joiners)
    {
        if (findUser (uid) == nullptr || areFriends (wsUser, uid) || hasBlocked (wsUser, uid)) continue;
        bool pending = false;
        for (auto& f : friendRequests)
            if ((f.from == uid && f.to == wsUser) || (f.from == wsUser && f.to == uid)) pending = true;
        if (pending) continue;
        FriendReqRec f { newId ("fr_"), uid, wsUser, now() };
        friendRequests.push_back (f);
        emit (wsUser, "friend_request.received", json::encode (FriendRequest { f.id, publicOf (uid), json::isoTime (f.at) }));
        return;
    }
}

void FakeMeonServer::devInviteMe()
{
    if (wsUser.isEmpty() || roomOf (wsUser) != nullptr) return;
    for (auto& uid : inviters)
    {
        auto* u = findUser (uid);
        if (u == nullptr || ! areFriends (wsUser, uid)) continue;
        // 초대한 친구는 공개 방에 들어가 있어야 한다: 자리가 있는 방으로 옮긴다
        auto* r = roomOf (uid);
        if (r == nullptr)
        {
            for (auto& [id, room] : rooms)
                if (room.type == "public" && hasRoomFor (room, uid) && hasRoomFor (room, wsUser) && ! blockedEitherWay (wsUser, room.hostId))
                {
                    u->online = true;
                    room.members.push_back ({ uid, now() });
                    roomChanged (room);
                    friendChanged (uid);
                    r = &room;
                    break;
                }
        }
        if (r == nullptr) continue;
        bool already = false;
        for (auto& i : invites)
            if (i.to == wsUser && i.from == uid && i.ended.isEmpty()) already = true;
        if (already || inviteExists (r->id, wsUser)) continue;   // 그 방에서 이미 받았거나 무시한 초대가 있으면 (API 7.7)
        InviteRec i { newId ("inv_"), uid, wsUser, r->id, now(), now() + inviteDelayMs, false };
        invites.push_back (i);
        std::weak_ptr<bool> weak = alive;
        const auto iid = i.id;
        scheduler.post (inviteDelayMs, [this, weak, iid] { if (! weak.expired()) deliverInvite (iid); });
        return;
    }
}

void FakeMeonServer::devJoinRequest()
{
    auto* r = roomOf (wsUser);
    if (r == nullptr || r->hostId != wsUser || r->type != "public" || r->entryMode != String ("approval")) return;
    for (auto& uid : joinRequesters)
    {
        if (findUser (uid) == nullptr || roomOf (uid) != nullptr) continue;
        bool pending = false;
        for (auto& j : joinRequests)
            if (j.userId == uid) pending = true;
        if (pending) continue;
        findUser (uid)->online = true;
        JoinReqRec j;
        j.id = newId ("jr_");
        j.roomId = r->id;
        j.userId = uid;
        j.at = now();
        j.expiresAt = j.at + joinRequestLifeMs;
        joinRequests.push_back (j);
        emit (wsUser, "join_request.received", json::encode (joinRequestView (j)));
        std::weak_ptr<bool> weak = alive;
        const auto rid = j.id;
        scheduler.post (joinRequestLifeMs, [this, weak, rid] { if (! weak.expired()) expireJoinRequest (rid); });
        return;
    }
}

} // namespace meon::core
