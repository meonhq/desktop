#include "FakeMeonServer.h"
#include "../ApiJson.h"
#include "../Realtime.h"
#include "../../model/Validation.h"
#include "../../model/Parts.h"

namespace meon::core
{

using juce::String;
using juce::var;

namespace v = validation;

//==============================================================================
FakeMeonServer::FakeMeonServer (Scheduler& s, Options o)
    : scheduler (s), options (std::move (o)),
      random (options.seed != 0 ? (juce::int64) options.seed : juce::Time::currentTimeMillis())
{
}

FakeMeonServer::~FakeMeonServer()
{
    *alive = false;
    scheduler.cancel (scriptTask);
}

String FakeMeonServer::newId (const char* prefix)
{
    return String (prefix) + juce::Uuid().toString().substring (0, 20);
}

String FakeMeonServer::newRoomCode()
{
    static const char* chars = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    for (;;)
    {
        String code;
        for (int i = 0; i < 6; ++i)
            code += String::charToString ((juce::juce_wchar) chars[random.nextInt (32)]);
        if (roomByCode (code) == nullptr && closedRooms.count (code) == 0 && fixedCodes.count (code) == 0)
            return code;
    }
}

String FakeMeonServer::hashPassword (const String& salt, const String& pw)
{
    return juce::SHA256 ((salt + ":" + pw).toUTF8()).toHexString();
}

String FakeMeonServer::groupPasswordFor (const String& groupName)
{
    return juce::SHA256 (("meon-fake:" + groupName).toUTF8()).toHexString().substring (0, 32);
}

String FakeMeonServer::newMailCode()
{
    lastMailedCode = String (random.nextInt (900000) + 100000);
    return lastMailedCode;
}

void FakeMeonServer::stateChanged()
{
    if (onStateChanged) onStateChanged (saveState());
}

FakeMeonServer::Out FakeMeonServer::fail (ErrorCode c, const String& msg, const String& field)
{
    return { errorHttpStatus (c), json::encodeError (ApiError::of (c, msg, field)) };
}

// 진짜 서버의 이메일 검사 (validate.Email). 앱의 validation::email 보다 엄격하다 (서버가 최종 판정)
static bool serverEmailOk (const String& s)
{
    if (s.isEmpty() || s.getNumBytesAsUTF8() > 254 || s.containsAnyOf (" \t\r\n<>\"(),;:\\[]")) return false;
    for (auto p = s.getCharPointer(); ! p.isEmpty();)
        if (p.getAndAdvance() > 0x7F) return false;
    const int at = s.lastIndexOfChar ('@');
    if (at < 1 || at > 64 || at == s.length() - 1) return false;
    const auto local = s.substring (0, at), domain = s.substring (at + 1);
    if (local.containsChar ('@')) return false;
    if (local.startsWithChar ('.') || local.endsWithChar ('.') || local.contains ("..")) return false;   // mail.ParseAddress
    return domain.containsChar ('.') && ! domain.startsWithChar ('.') && ! domain.endsWithChar ('.') && ! domain.contains ("..");
}

// 방 이름·소개의 제어 문자 (validate.Text): Cc 와 줄·문단 구분자. 소개는 줄바꿈(\n)만 허용
static bool hasControl (const String& s, bool multiline)
{
    for (auto p = s.getCharPointer(); ! p.isEmpty();)
    {
        const auto c = p.getAndAdvance();
        if (multiline && c == '\n') continue;
        if (c < 0x20 || (c >= 0x7F && c <= 0x9F) || c == 0x2028 || c == 0x2029) return true;
    }
    return false;
}

// PATCH 본문에 키가 있는데 값이 null 인지
static bool isNullField (const var& obj, const char* key)
{
    auto* o = obj.getDynamicObject();
    return o != nullptr && o->hasProperty (key) && obj.getProperty (key, var()).isVoid();
}

static bool hasField (const var& obj, const char* key)
{
    auto* o = obj.getDynamicObject();
    return o != nullptr && o->hasProperty (key);
}

//==============================================================================
// 조회

FakeMeonServer::User* FakeMeonServer::findUser (const String& id)
{
    auto it = users.find (id);
    return it != users.end() && ! it->second.deleted ? &it->second : nullptr;
}

const FakeMeonServer::User* FakeMeonServer::findUser (const String& id) const
{
    auto it = users.find (id);
    return it != users.end() && ! it->second.deleted ? &it->second : nullptr;
}

FakeMeonServer::User* FakeMeonServer::findByEmail (const String& email)
{
    for (auto& [id, u] : users)   // 탈퇴 유예 중인 계정도 (주소는 예약, 로그인하면 탈퇴 취소)
        if (u.email.equalsIgnoreCase (email)) return &u;
    return nullptr;
}

FakeMeonServer::User* FakeMeonServer::findByUsername (const String& username)
{
    for (auto& [id, u] : users)   // 탈퇴한 아이디도 예약 (deleted 포함)
        if (u.username.equalsIgnoreCase (username)) return &u;
    return nullptr;
}

FakeMeonServer::RoomRec* FakeMeonServer::findRoom (const String& id)
{
    auto it = rooms.find (id);
    return it != rooms.end() ? &it->second : nullptr;
}

FakeMeonServer::RoomRec* FakeMeonServer::roomOf (const String& userId)
{
    for (auto& [id, r] : rooms)
        for (auto& m : r.members)
            if (m.userId == userId) return &r;
    return nullptr;
}

const FakeMeonServer::RoomRec* FakeMeonServer::roomOf (const String& userId) const
{
    for (auto& [id, r] : rooms)
        for (auto& m : r.members)
            if (m.userId == userId) return &r;
    return nullptr;
}

FakeMeonServer::RoomRec* FakeMeonServer::roomByCode (const String& code)
{
    for (auto& [id, r] : rooms)
        if (r.type == "private" && r.inviteCode.equalsIgnoreCase (code)) return &r;
    return nullptr;
}

static String pairKey (const String& a, const String& b) { return a < b ? a + "|" + b : b + "|" + a; }

bool FakeMeonServer::areFriends (const String& a, const String& b) const { return friendships.count (pairKey (a, b)) > 0; }

void FakeMeonServer::removeFriendRequestsBetween (String a, String b)
{
    friendRequests.erase (std::remove_if (friendRequests.begin(), friendRequests.end(), [&] (const FriendReqRec& f)
                          { return (f.from == a && f.to == b) || (f.from == b && f.to == a); }),
                          friendRequests.end());
}

bool FakeMeonServer::hasBlocked (const String& by, const String& target) const
{
    auto it = blocks.find (by);
    return it != blocks.end() && it->second.count (target) > 0;
}

bool FakeMeonServer::blockedEitherWay (const String& a, const String& b) const { return hasBlocked (a, b) || hasBlocked (b, a); }

bool FakeMeonServer::hostPresent (const RoomRec& r) const
{
    for (auto& m : r.members)
        if (m.userId == r.hostId) return true;
    return false;
}

bool FakeMeonServer::hasRoomFor (const RoomRec& r, const String& userId) const
{
    if (r.hostId == userId) return true;
    const int limit = hostPresent (r) ? r.capacity : r.capacity - 1;
    return (int) r.members.size() < limit;
}

bool FakeMeonServer::isOnline (const String& userId) const
{
    auto* u = findUser (userId);
    return u != nullptr && u->online;
}

bool FakeMeonServer::isMember (const String& roomId, const String& userId) const
{
    auto it = rooms.find (roomId);
    if (it == rooms.end()) return false;
    for (auto& m : it->second.members)
        if (m.userId == userId) return true;
    return false;
}

int FakeMeonServer::pendingInviteCountFor (const String& userId) const
{
    int n = 0;
    for (auto& i : invites)
        if (i.to == userId && i.delivered && i.ended.isEmpty()) ++n;
    return n;
}

//==============================================================================
// 보는 사람 기준 모델

UserPublic FakeMeonServer::publicOf (const User& u) const { return { u.id, u.username, u.nickname, u.part }; }

UserPublic FakeMeonServer::publicOf (const String& id) const
{
    if (auto* u = findUser (id)) return publicOf (*u);
    return { id, "unknown", String::fromUTF8 ("알 수 없음"), String ("other") };
}

Me FakeMeonServer::meOf (const User& u) const
{
    Me m;
    m.id = u.id;
    m.username = u.username;
    m.nickname = u.nickname;
    m.part = u.part;
    m.email = u.email;
    m.emailVerified = u.verified;
    m.pendingEmail = u.pendingEmail;
    m.settings = u.settings;
    m.createdAt = json::isoTime (u.createdAt);
    return m;
}

Room FakeMeonServer::roomView (const RoomRec& r, const String& viewer) const
{
    Room x;
    x.id = r.id;
    x.type = r.type;
    x.entryMode = r.entryMode;
    x.title = r.title;
    x.description = r.description;
    x.capacity = r.capacity;
    x.memberCount = (int) r.members.size();
    x.hostId = r.hostId;
    x.hostPresent = hostPresent (r);
    if (auto* host = findUser (r.hostId)) x.hostRttMs = host->rttMs;
    for (auto& m : r.members)
        x.members.push_back ({ publicOf (m.userId), m.userId == r.hostId, areFriends (viewer, m.userId), json::isoTime (m.joinedAt) });
    if (auto ru = requestUntil.find (viewer); ru != requestUntil.end())
        if (auto mark = ru->second.find (r.id); mark != ru->second.end() && mark->second > now())
            x.myJoinRequestUntil = json::isoTime (mark->second);
    x.createdAt = json::isoTime (r.createdAt);
    return x;
}

Connection FakeMeonServer::connectionFor (const RoomRec& r, const String& viewer) const
{
    Connection c;
    c.aooHost = options.aooHost;
    c.aooPort = options.aooPort;
    c.groupName = r.groupName;
    c.groupPassword = groupPasswordFor (r.groupName);
    c.aooUsername = viewer;
    if (auto* u = findUser (viewer)) c.aooUserPassword = u->aooPassword;
    for (auto& m : r.members)
        c.members.add (m.userId);
    return c;
}

RoomDetail FakeMeonServer::detailView (const RoomRec& r, const String& viewer) const
{
    RoomDetail d;
    static_cast<Room&> (d) = roomView (r, viewer);
    if (r.type == "private") d.inviteCode = r.inviteCode;
    d.connection = connectionFor (r, viewer);
    return d;
}

Friend FakeMeonServer::friendView (const String& friendId, const String& viewer) const
{
    Friend f;
    f.user = publicOf (friendId);
    auto* u = findUser (friendId);
    auto* room = roomOf (friendId);
    const bool online = u != nullptr && u->online;
    f.status = ! online ? "offline" : (room != nullptr ? "in_room" : "online");
    if (! online && u != nullptr && u->lastSeenAt > 0) f.lastSeenAt = json::isoTime (u->lastSeenAt);
    if (online && room != nullptr && room->type == "public")
        f.room = RoomRef { room->id, room->title, hasRoomFor (*room, viewer) && ! blockedEitherWay (viewer, room->hostId) };
    // API 2.4·2.10: 상대가 온라인이고 방에 없고 초대를 받고, 내가 방에 있을 때
    f.canInvite = online && room == nullptr && u->settings.roomInvites == "friends" && roomOf (viewer) != nullptr;
    return f;
}

std::vector<Friend> FakeMeonServer::friendsOf (const String& viewer) const
{
    std::vector<Friend> list;
    for (auto& key : friendships)
    {
        const auto a = key.upToFirstOccurrenceOf ("|", false, false), b = key.fromFirstOccurrenceOf ("|", false, false);
        if (a != viewer && b != viewer) continue;
        const auto other = a == viewer ? b : a;
        if (findUser (other) != nullptr) list.push_back (friendView (other, viewer));
    }
    // 진짜 서버 (view/friends.go sortFriends): in_room → online → offline. offline 은 last_seen_at 최근순(없으면 뒤),
    // 그다음 닉네임순(한국어 정렬), 같으면 아이디순. 한국어 정렬은 대소문자를 가리지 않는 비교로 근사한다
    auto rank = [] (const Friend& f) { return f.status == "in_room" ? 0 : f.status == "online" ? 1 : 2; };
    std::stable_sort (list.begin(), list.end(), [&] (const Friend& x, const Friend& y)
    {
        if (rank (x) != rank (y)) return rank (x) < rank (y);
        if (x.status == "offline" && x.lastSeenAt != y.lastSeenAt)
        {
            if (! x.lastSeenAt || ! y.lastSeenAt) return x.lastSeenAt.has_value();
            return *x.lastSeenAt > *y.lastSeenAt;
        }
        if (const int c = x.user.nickname.value_or ("").compareIgnoreCase (y.user.nickname.value_or ("")); c != 0) return c < 0;
        return x.user.username < y.user.username;
    });
    return list;
}

JoinRequest FakeMeonServer::joinRequestView (const JoinReqRec& j) const
{
    return { j.id, j.roomId, publicOf (j.userId), j.viaInvite, json::isoTime (j.at), json::isoTime (j.expiresAt) };
}

Invite FakeMeonServer::inviteView (const InviteRec& i) const
{
    String title;
    if (auto it = rooms.find (i.roomId); it != rooms.end()) title = it->second.title;
    return { i.id, publicOf (i.from), { i.roomId, title }, json::isoTime (i.at) };
}

bool FakeMeonServer::hasUnread (const String& userId) const
{
    for (auto& f : friendRequests)
        if (f.to == userId && ! f.hidden && ! f.read && findUser (f.from) != nullptr) return true;
    return false;
}

Hello FakeMeonServer::helloFor (const String& userId) const
{
    Hello h;
    if (auto* u = findUser (userId)) h.me = meOf (*u);
    if (auto* r = roomOf (userId))
    {
        h.currentRoom = detailView (*r, userId);
        if (r->hostId == userId)
            for (auto& j : joinRequests)
                if (j.roomId == r->id) h.joinRequests.push_back (joinRequestView (j));
    }
    h.friends = friendsOf (userId);
    h.hasUnreadNotifications = hasUnread (userId);
    for (auto& i : invites)
        if (i.to == userId && i.delivered && i.ended.isEmpty()) h.pendingInvites.push_back (inviteView (i));
    h.config = config;
    return h;
}

//==============================================================================
// 이벤트

void FakeMeonServer::emit (const String& toUser, const String& type, const var& data)
{
    if (onEmitForTest) onEmitForTest (toUser, type, data);
    if (! wsOpen || wsUser.isEmpty() || toUser != wsUser || sink == nullptr) return;
    if (dropEventForTest && dropEventForTest (type, data)) return;
    auto msg = RealtimeEvent::encode (type, ++seq, data);
    std::weak_ptr<bool> weak = alive;
    const int gen = connectionGen;
    scheduler.post (options.latencyMs, [this, weak, gen, msg]
    {
        if (weak.expired() || gen != connectionGen || ! wsOpen || sink == nullptr) return;
        sink->transportMessage (msg);
    });
}

void FakeMeonServer::emitRooms (const String& type, const RoomRec& r)
{
    if (r.type != "public" || ! subscribed || wsUser.isEmpty()) return;
    if (blockedEitherWay (wsUser, r.hostId)) return;
    emit (wsUser, type, json::encode (roomView (r, wsUser)));
}

void FakeMeonServer::emitRoomRemoved (const String& roomId, const String& hostId)
{
    if (! subscribed || wsUser.isEmpty() || blockedEitherWay (wsUser, hostId)) return;
    emit (wsUser, "rooms.removed", json::obj ({ { "room_id", roomId } }));
}

void FakeMeonServer::roomChanged (const RoomRec& r, bool publicListChanged)
{
    for (auto& m : r.members)
        emit (m.userId, "room.updated", json::encode (detailView (r, m.userId)));
    if (publicListChanged) emitRooms ("rooms.updated", r);
    // friend.updated 는 여기서 보내지 않는다: 진짜 서버는 들어오고 나간 사람 본인의 상태가 바뀔 때만 보낸다 (friendChanged)
}

void FakeMeonServer::friendChanged (const String& userId)
{
    if (wsUser.isNotEmpty() && areFriends (wsUser, userId))
        emit (wsUser, "friend.updated", json::encode (friendView (userId, wsUser)));
}

// PATCH /me 로 닉네임·파트가 바뀌었을 때 (버그 24). 진짜 서버 반영 대기 (진짜 서버·API 문서는 서버 쪽에서 따로 맞춘다):
//   1) 내가 있는 방의 멤버에게 room.updated
//   2) 그 방이 공개 방이면 공개 방 목록 구독자에게 rooms.updated (카드의 멤버 줄·"비어 있는 파트" 필터)
//   3) 친구에게 friend.updated (방에 있든 없든)
// 가짜 서버는 실시간 연결이 하나뿐이라 실제로 전달되는 것은 그 사용자 몫뿐이다 (emit 이 나머지를 버린다)
void FakeMeonServer::profileChanged (const String& userId)
{
    if (auto* r = roomOf (userId))
    {
        for (auto& m : r->members)
            emit (m.userId, "room.updated", json::encode (detailView (*r, m.userId)));
        emitRooms ("rooms.updated", *r);
    }
    for (auto& key : friendships)
    {
        const auto a = key.upToFirstOccurrenceOf ("|", false, false), b = key.fromFirstOccurrenceOf ("|", false, false);
        if (a != userId && b != userId) continue;
        const auto other = a == userId ? b : a;
        if (findUser (other) != nullptr && ! blockedEitherWay (userId, other))
            emit (other, "friend.updated", json::encode (friendView (userId, other)));
    }
}

void FakeMeonServer::sendHello()
{
    emit (wsUser, "hello", json::encode (helloFor (wsUser)));
}

//==============================================================================
// 방 규칙

void FakeMeonServer::addMember (RoomRec& r, const String& userId)
{
    r.members.push_back ({ userId, now() });
    removeJoinRequestsOf (userId);
    expireInvitesTo (userId);   // 7.11: 어느 방에든 들어가면 받아 둔 초대 소멸
}

void FakeMeonServer::removeMember (RoomRec& r, String userId)
{
    r.members.erase (std::remove_if (r.members.begin(), r.members.end(), [&] (const MemberRec& m) { return m.userId == userId; }),
                     r.members.end());
    // 방장이 나가면 대기 요청 즉시 삭제 (5.7, 방장에게 removed). 마지막 사람이면 방이 닫히며 조용히 지운다 (deleteRoom)
    if (userId == r.hostId && ! r.members.empty())
        removeJoinRequestsForRoom (r.id);
}

void FakeMeonServer::deleteRoom (String roomId, bool closedByHost)   // roomId 는 값: 방을 지운 뒤에도 쓴다
{
    auto* r = findRoom (roomId);
    if (r == nullptr) return;
    const auto copy = *r;

    if (closedByHost)
        for (auto& m : copy.members)
            if (m.userId != copy.hostId)
                emit (m.userId, "room.closed", json::obj ({ { "room_id", roomId }, { "reason", "host_closed" } }));

    // 그 방 초대 전부 소멸 (이미 받은 사람에게만 invite.expired). 기록은 남긴다: 닫힌 방의 초대를 수락·무시하면 410
    for (auto& i : invites)
    {
        if (i.roomId != roomId || i.ended.isNotEmpty()) continue;
        i.ended = "expired";
        if (i.delivered) emit (i.to, "invite.expired", json::obj ({ { "invite_id", i.id } }));
    }
    // 승인 요청은 조용히 지운다 (방장에게도 removed 없음, 진짜 서버 closeRoom)
    joinRequests.erase (std::remove_if (joinRequests.begin(), joinRequests.end(), [&] (const JoinReqRec& j) { return j.roomId == roomId; }),
                        joinRequests.end());

    closedRooms[roomId] = now();
    if (copy.type == "private" && copy.inviteCode.isNotEmpty()) closedRooms[copy.inviteCode] = now();
    rooms.erase (roomId);

    if (copy.type == "public") emitRoomRemoved (roomId, copy.hostId);
    for (auto& m : copy.members)
        friendChanged (m.userId);
}

void FakeMeonServer::expireInvitesTo (String userId, bool notify)
{
    // 전달된 살아 있는 초대만 소멸 (전달 전인 것은 전달 시점에 방에 있으면 버려진다)
    for (auto& i : invites)
    {
        if (i.to != userId || ! i.delivered || i.ended.isNotEmpty()) continue;
        i.ended = "expired";
        if (notify) emit (userId, "invite.expired", json::obj ({ { "invite_id", i.id } }));
    }
}

bool FakeMeonServer::inviteExists (const String& roomId, const String& to)
{
    invites.erase (std::remove_if (invites.begin(), invites.end(), [&] (const InviteRec& i)
                   { return i.roomId == roomId && i.to == to && (i.ended == "accepted" || i.ended == "dropped" || i.ended == "expired"); }),
                   invites.end());
    for (auto& i : invites)
        if (i.roomId == roomId && i.to == to) return true;
    return false;
}

void FakeMeonServer::removeJoinRequestsOf (String userId)
{
    for (auto it = joinRequests.begin(); it != joinRequests.end();)
    {
        if (it->userId == userId)
        {
            if (auto* r = findRoom (it->roomId)) emit (r->hostId, "join_request.removed", json::obj ({ { "request_id", it->id } }));
            it = joinRequests.erase (it);
        }
        else ++it;
    }
}

void FakeMeonServer::removeJoinRequestsForRoom (String roomId)
{
    for (auto it = joinRequests.begin(); it != joinRequests.end();)
    {
        if (it->roomId == roomId)
        {
            if (auto* r = findRoom (roomId)) emit (r->hostId, "join_request.removed", json::obj ({ { "request_id", it->id } }));
            it = joinRequests.erase (it);
        }
        else ++it;
    }
}

void FakeMeonServer::expireJoinRequest (String requestId)
{
    for (auto it = joinRequests.begin(); it != joinRequests.end(); ++it)
    {
        if (it->id != requestId) continue;
        if (it->expiresAt > now()) return;
        if (auto* r = findRoom (it->roomId)) emit (r->hostId, "join_request.removed", json::obj ({ { "request_id", it->id } }));
        joinRequests.erase (it);
        return;
    }
}

FakeMeonServer::Out FakeMeonServer::joinCommon (const String& actor, RoomRec& r, bool viaInvite, const String& inviterId)
{
    // 이미 들어 있는 바로 그 방: 멱등
    if (isMember (r.id, actor)) return ok (json::encode (detailView (r, actor)));
    if (r.kicked.count (actor) > 0) return fail (ErrorCode::kicked);
    if (r.type == "public" && blockedEitherWay (actor, r.hostId)) return fail (ErrorCode::roomNotFound);
    if (roomOf (actor) != nullptr) return fail (ErrorCode::alreadyInRoom);
    if (! hasRoomFor (r, actor)) return fail (ErrorCode::roomFull);

    const bool approval = r.type == "public" && r.entryMode == String ("approval") && actor != r.hostId
                          && ! (viaInvite && inviterId == r.hostId);   // 방장의 초대는 승인 없이 (7.9)
    if (approval)
    {
        // 5분 안의 같은 방 재요청: 새로 만들지 않고 기존 request_until (방장이 없어도. 진짜 서버 requestJoin 순서)
        auto& marks = requestUntil[actor];
        if (auto mark = marks.find (r.id); mark != marks.end() && mark->second > now())
            return { 202, json::obj ({ { "status", "requested" }, { "request_until", json::isoTime (mark->second) } }) };

        if (! hostPresent (r)) return fail (ErrorCode::hostAbsent);

        removeJoinRequestsOf (actor);   // 한 번에 한 방에만
        JoinReqRec j;
        j.id = newId ("jr_");
        j.roomId = r.id;
        j.userId = actor;
        j.viaInvite = viaInvite;
        j.at = now();
        j.expiresAt = j.at + joinRequestLifeMs;
        joinRequests.push_back (j);
        requestUntil[actor][r.id] = j.at + requestUntilMs;
        emit (r.hostId, "join_request.received", json::encode (joinRequestView (j)));

        std::weak_ptr<bool> weak = alive;
        const auto rid = j.id;
        scheduler.post (joinRequestLifeMs, [this, weak, rid] { if (! weak.expired()) expireJoinRequest (rid); });
        if (actor == wsUser && findUser (r.hostId) != nullptr && ! findUser (r.hostId)->local)
            scheduler.post (5000, [this, weak, rid] { if (! weak.expired()) fakeHostConsider (rid); });

        return { 202, json::obj ({ { "status", "requested" }, { "request_until", json::isoTime (j.at + requestUntilMs) } }) };
    }

    addMember (r, actor);
    roomChanged (r);
    friendChanged (actor);
    return ok (json::encode (detailView (r, actor)));
}

void FakeMeonServer::fakeHostConsider (const String& requestId)
{
    if (! options.fakeHostsAccept) return;
    for (auto& j : joinRequests)
    {
        if (j.id != requestId) continue;
        auto* r = findRoom (j.roomId);
        if (r == nullptr || ! hasRoomFor (*r, j.userId) || roomOf (j.userId) != nullptr) return;
        const auto userId = j.userId, reqId = j.id;
        // 수락한 요청은 먼저 지운다 (방장에게 removed 가 가지 않게, 진짜 서버 AcceptJoinRequest)
        joinRequests.erase (std::remove_if (joinRequests.begin(), joinRequests.end(), [&] (const JoinReqRec& x) { return x.id == reqId; }),
                            joinRequests.end());
        addMember (*r, userId);
        emit (userId, "join_request.accepted", json::obj ({ { "room", json::encode (detailView (*r, userId)) } }));
        roomChanged (*r);
        friendChanged (userId);
        return;
    }
}

//==============================================================================
// RestTransport

void FakeMeonServer::send (const HttpRequest& req, std::function<void (HttpResponse)> done)
{
    if (holdForTest)
        if (const int ms = holdForTest (req); ms > 0)
        {
            std::weak_ptr<bool> weak = alive;
            scheduler.post (ms, [this, weak, req, done] { if (! weak.expired()) respond (req, done); });
            return;
        }
    respond (req, std::move (done));
}

void FakeMeonServer::respond (const HttpRequest& req, std::function<void (HttpResponse)> done)
{
    if (onRequestForTest) onRequestForTest (req);
    std::optional<HttpResponse> canned;
    if (replyForTest) canned = replyForTest (req);
    auto response = canned ? *canned : handle (req);
    std::weak_ptr<bool> weak = alive;
    scheduler.post (options.latencyMs, [weak, done, response]
    {
        if (! weak.expired() && done) done (response);
    });
}

HttpResponse FakeMeonServer::handle (const HttpRequest& req)
{
    auto out = route (req);
    HttpResponse r;
    r.status = out.status;
    r.body = out.body;
    return r;
}

String FakeMeonServer::newSession (const String& userId)
{
    const auto id = newId ("s_");
    sessions[id] = { userId, 0 };
    return id;
}

FakeMeonServer::Out FakeMeonServer::issueTokens (User& u, int status, bool withMe, const String& sessionId, const String& rotatedFrom)
{
    auto& sess = sessions[sessionId];
    sess.userId = u.id;
    if (rotatedFrom.isNotEmpty()) ++sess.generation;   // 회전하면 옛 access 토큰은 바로 무효 (진짜 서버 BumpSession)
    const auto at = newId ("at_"), rt = newId ("rt_");
    accessTokens[at] = { u.id, sessionId, sess.generation, now() + accessLifeMs };
    refreshOwners[rt] = { u.id, sessionId };
    u.refreshTokens.add (rt);
    if (rotatedFrom.isNotEmpty())
    {
        // 유예로 다시 발급할 때는 처음 회전한 시각을 그대로 둔다 (유예는 처음 회전부터 최대 10분)
        auto& rot = rotatedRefresh[rotatedFrom];
        if (rot.at == 0) rot.at = now();
        rot.userId = u.id;
        rot.sessionId = sessionId;
        rot.issued = rt;
    }
    // 오래된 회전 기록은 버린다 (유예가 끝난 뒤에도 하루는 남겨 도용으로 판단한다)
    for (auto it = rotatedRefresh.begin(); it != rotatedRefresh.end();)
        it = now() - it->second.at > 24LL * 60 * 60 * 1000 ? rotatedRefresh.erase (it) : std::next (it);
    stateChanged();
    if (withMe)
        return { status, json::obj ({ { "access_token", at }, { "refresh_token", rt }, { "me", json::encode (meOf (u)) } }) };
    return { status, json::obj ({ { "access_token", at }, { "refresh_token", rt } }) };
}

FakeMeonServer::Out FakeMeonServer::route (const HttpRequest& req)
{
    const auto pathOnly = req.path.upToFirstOccurrenceOf ("?", false, false);
    juce::StringPairArray query;
    for (auto& kv : juce::StringArray::fromTokens (req.path.fromFirstOccurrenceOf ("?", false, false), "&", ""))
        if (kv.isNotEmpty())
            query.set (kv.upToFirstOccurrenceOf ("=", false, false),
                       juce::URL::removeEscapeChars (kv.fromFirstOccurrenceOf ("=", false, false)));

    auto seg = juce::StringArray::fromTokens (pathOnly, "/", "");
    seg.removeEmptyStrings();
    for (auto& s : seg) s = juce::URL::removeEscapeChars (s);
    const auto& m = req.method;
    const auto& body = req.body;

    if (m == "GET" && seg.joinIntoString ("/") == "config")
        return ok (json::encode (config));

    // 1.4 버전 헤더 (로컬 빌드 0 은 개발 서버처럼 통과)
    if (req.build != 0 && req.build < config.minBuild)
        return fail (ErrorCode::clientOutdated);

    if (seg.size() >= 2 && seg[0] == "auth")
    {
        const auto what = seg[1];

        if (m == "GET" && what == "username-available")
        {
            const auto u = query["username"].toLowerCase();
            if (v::username (u) != v::Problem::none) return fail (ErrorCode::invalidRequest, {}, "username");
            return ok (json::obj ({ { "available", findByUsername (u) == nullptr } }));
        }

        if (m == "POST" && what == "signup")
        {
            // 진짜 서버 순서: email → username → password → agreed_terms (service/auth Signup)
            const auto email = body.getProperty ("email", "").toString().trim();
            const auto username = body.getProperty ("username", "").toString().trim().toLowerCase();
            const auto password = body.getProperty ("password", "").toString();
            if (! serverEmailOk (email)) return fail (ErrorCode::invalidRequest, {}, "email");
            if (v::username (username) != v::Problem::none) return fail (ErrorCode::invalidRequest, {}, "username");
            if (v::password (password) != v::Problem::none) return fail (ErrorCode::invalidRequest, {}, "password");
            if (! (bool) body.getProperty ("agreed_terms", false)) return fail (ErrorCode::invalidRequest, {}, "agreed_terms");
            if (findByEmail (email) != nullptr) return fail (ErrorCode::emailTaken);
            if (findByUsername (username) != nullptr) return fail (ErrorCode::usernameTaken);

            User u;
            u.id = "u_" + juce::Uuid().toDashedString();
            u.username = username;
            u.email = email;
            u.salt = juce::Uuid().toString();
            u.passwordHash = hashPassword (u.salt, password);
            u.createdAt = now();
            u.local = true;
            u.aooPassword = juce::Uuid().toString();
            u.verifyCode = newMailCode();
            u.verifyCodeAt = u.lastSignupMailAt = now();
            users[u.id] = u;
            return issueTokens (users[u.id], 201, true, newSession (u.id));
        }

        if (m == "POST" && what == "login")
        {
            const auto email = body.getProperty ("email", "").toString().trim();
            const auto password = body.getProperty ("password", "").toString();
            if (email.isEmpty()) return fail (ErrorCode::invalidRequest, {}, "email");
            if (password.isEmpty()) return fail (ErrorCode::invalidRequest, {}, "password");
            auto* u = findByEmail (email);
            if (u == nullptr || u->passwordHash != hashPassword (u->salt, password))
                return fail (ErrorCode::invalidCredentials, String::fromUTF8 ("이메일 또는 비밀번호가 맞지 않아요"));
            u->deleted = false;   // 탈퇴 유예 중이면 탈퇴 취소 (API 3.7, 4.5)
            return issueTokens (*u, 200, true, newSession (u->id));
        }

        if (m == "POST" && what == "refresh")
        {
            const auto rt = body.getProperty ("refresh_token", "").toString();
            if (rt.isEmpty()) return fail (ErrorCode::invalidRequest, {}, "refresh_token");
            // 도용 판단: 그 계정의 모든 세션 무효 (실시간 연결은 4001). 다시 붙으려 하면 무효가 된 토큰이라 로그인 화면으로 간다
            auto reuseDetected = [this] (const String& userId)
            {
                revokeAllSessions (userId);
                stateChanged();
                return fail (ErrorCode::unauthorized);
            };
            auto it = refreshOwners.find (rt);
            if (it == refreshOwners.end())
            {
                // 유예 재발급으로 버린 토큰이 다시 옴: 도용 (진짜 서버 errReuse)
                if (auto d = discardedRefresh.find (rt); d != discardedRefresh.end())
                    return reuseDetected (d->second.userId);
                // 이미 회전한 토큰이 다시 왔다 (API 1.3 재사용 유예):
                // - 그때 준 새 토큰이 한 번도 안 쓰였고 처음 회전한 지 10분 안이면, 앱이 응답을 못 받고 죽었다가
                //   다시 켠 것으로 본다 → 못 받은 새 토큰은 무효로 하고 새로 발급
                // - 새 토큰이 이미 쓰였거나 10분이 지났으면 도용으로 본다 → 그 계정의 모든 세션 무효
                auto rot = rotatedRefresh.find (rt);
                if (rot == rotatedRefresh.end()) return fail (ErrorCode::unauthorized);
                const auto userId = rot->second.userId, issued = rot->second.issued, sessionId = rot->second.sessionId;
                auto* u = findUser (userId);
                if (u == nullptr || sessions.count (sessionId) == 0) return fail (ErrorCode::unauthorized);
                const bool issuedUnused = refreshOwners.count (issued) > 0;
                if (issuedUnused && now() - rot->second.at <= refreshReuseGraceMs)
                {
                    refreshOwners.erase (issued);
                    u->refreshTokens.removeString (issued);
                    discardedRefresh[issued] = { userId, sessionId };
                    return issueTokens (*u, 200, false, sessionId, rt);
                }
                return reuseDetected (userId);
            }
            const auto owner = it->second;
            auto* u = findUser (owner.userId);
            if (u == nullptr) return fail (ErrorCode::unauthorized);
            refreshOwners.erase (it);
            u->refreshTokens.removeString (rt);
            return issueTokens (*u, 200, false, owner.sessionId, rt);
        }
    }

    // 여기부터 인증 필요
    const auto token = req.accessToken;
    auto sit = accessTokens.find (token);
    if (token.isEmpty() || sit == accessTokens.end()) return fail (ErrorCode::unauthorized);
    if (sit->second.expiresAt <= now()) return fail (ErrorCode::tokenExpired);
    // 폐기한 세션이거나 회전으로 세대가 지난 토큰 (진짜 서버 principal)
    if (auto se = sessions.find (sit->second.sessionId); se == sessions.end() || se->second.generation != sit->second.generation)
        return fail (ErrorCode::unauthorized);
    auto* me = findUser (sit->second.userId);
    if (me == nullptr) return fail (ErrorCode::unauthorized);

    const auto path = seg.joinIntoString ("/");
    const bool alwaysAllowed = (m == "POST" && (path == "auth/verify-email" || path == "auth/resend-verification" || path == "auth/logout"
                                                || path == "me/email"))
                               || (m == "GET" && path == "me") || (m == "DELETE" && path == "me");
    if (! me->verified && ! alwaysAllowed) return fail (ErrorCode::emailNotVerified);
    if (me->verified && (! me->nickname || ! me->part) && ! alwaysAllowed && ! (m == "PATCH" && path == "me"))
        return fail (ErrorCode::profileIncomplete);

    auto out = authorised (req, m, seg, query);
    if (me->local) stateChanged();
    return out;
}

FakeMeonServer::Out FakeMeonServer::authorised (const HttpRequest& req, const String& m, const juce::StringArray& seg,
                                                const juce::StringPairArray&)
{
    const auto actor = accessTokens[req.accessToken].userId;
    const auto mySession = accessTokens[req.accessToken].sessionId;
    auto& me = users[actor];
    const auto& body = req.body;
    const auto path = seg.joinIntoString ("/");
    const int n = seg.size();

    //------------------------------------------------------------------ 3 계정
    if (m == "POST" && path == "auth/logout")
    {
        // 그 refresh 의 세션을 폐기한다 (그 세션의 access 토큰 전부 무효, 실시간 연결은 4001).
        // 남의 토큰이거나 없는 토큰이면 아무것도 하지 않고 204 (API 3.9)
        const auto rt = body.getProperty ("refresh_token", "").toString();
        if (rt.isEmpty()) return fail (ErrorCode::invalidRequest, {}, "refresh_token");
        if (auto it = refreshOwners.find (rt); it != refreshOwners.end() && it->second.userId == actor)
            revokeSession (it->second.sessionId);
        return noContent();
    }

    if (m == "POST" && path == "auth/verify-email")
    {
        if (me.verified) return ok (json::encode (meOf (me)));
        const auto code = body.getProperty ("code", "").toString();
        if (me.verifyCode.isEmpty() || now() - me.verifyCodeAt > codeLifeMs) return fail (ErrorCode::codeExpired);
        if (code != me.verifyCode)
        {
            if (++me.verifyFails >= 5) me.verifyCode = {};
            return fail (ErrorCode::codeInvalid);
        }
        me.verified = true;
        me.verifyCode = {};
        me.verifyFails = 0;
        return ok (json::encode (meOf (me)));
    }

    if (m == "POST" && path == "auth/resend-verification")
    {
        if (me.verified) return noContent();   // 이미 인증함: 아무것도 하지 않는다
        if (now() - me.lastSignupMailAt < resendGapMs) return fail (ErrorCode::rateLimited);
        me.verifyCode = newMailCode();
        me.verifyCodeAt = me.lastSignupMailAt = now();
        me.verifyFails = 0;
        return noContent();
    }

    //------------------------------------------------------------------ 4 내 계정
    if (path == "me")
    {
        if (m == "GET") return ok (json::encode (meOf (me)));

        if (m == "PATCH")
        {
            // 진짜 서버 순서 (handlers_auth.go): null 인 필드 → settings: null → username·email 은 바꿀 수 없음 → 값 검사
            const auto settingsBody = body.getProperty ("settings", var());
            if (isNullField (body, "nickname")) return fail (ErrorCode::invalidRequest, {}, "nickname");
            if (isNullField (body, "part")) return fail (ErrorCode::invalidRequest, {}, "part");
            if (isNullField (settingsBody, "friend_requests")) return fail (ErrorCode::invalidRequest, {}, "settings.friend_requests");
            if (isNullField (settingsBody, "room_invites")) return fail (ErrorCode::invalidRequest, {}, "settings.room_invites");
            if (isNullField (body, "settings")) return fail (ErrorCode::invalidRequest, {}, "settings");
            if (hasField (body, "username")) return fail (ErrorCode::invalidRequest, {}, "username");
            if (hasField (body, "email")) return fail (ErrorCode::invalidRequest, {}, "email");
            MePatch p;
            if (! json::decode (body, p)) return fail (ErrorCode::invalidRequest);
            if (p.nickname && v::nickname (*p.nickname) != v::Problem::none) return fail (ErrorCode::invalidRequest, {}, "nickname");
            if (p.part && ! parts::isValid (*p.part)) return fail (ErrorCode::invalidRequest, {}, "part");
            if (p.friendRequests && *p.friendRequests != "everyone" && *p.friendRequests != "none")
                return fail (ErrorCode::invalidRequest, {}, "settings.friend_requests");
            if (p.roomInvites && *p.roomInvites != "friends" && *p.roomInvites != "none")
                return fail (ErrorCode::invalidRequest, {}, "settings.room_invites");
            const auto before = me;
            if (p.nickname) me.nickname = v::normalise (*p.nickname);
            if (p.part) me.part = *p.part;
            if (p.friendRequests)
            {
                me.settings.friendRequests = *p.friendRequests;
                if (before.settings.friendRequests == "none" && *p.friendRequests == "everyone")   // 받지 않음 → 모두: 숨은 요청 기록 삭제 (7.2)
                    friendRequests.erase (std::remove_if (friendRequests.begin(), friendRequests.end(),
                                                          [&] (const FriendReqRec& f) { return f.to == actor && f.hidden; }),
                                          friendRequests.end());
            }
            if (p.roomInvites) me.settings.roomInvites = *p.roomInvites;
            if (before.nickname != me.nickname || before.part != me.part) profileChanged (actor);   // 실제로 바뀌었을 때만
            return ok (json::encode (meOf (me)));
        }

        if (m == "DELETE")
        {
            // API 4.5 (v0.5): 비밀번호 없이 세션만으로. 본문이 와도 보지 않는다.
            // 탈퇴 유예(30일) 중에는 이메일·아이디를 예약하고, 다시 로그인하면 탈퇴를 취소한다 (가짜는 30일 뒤 삭제를 하지 않는다)
            juce::StringArray friendIds;
            for (auto& key : friendships)
            {
                const auto a = key.upToFirstOccurrenceOf ("|", false, false), b = key.fromFirstOccurrenceOf ("|", false, false);
                if (a == actor || b == actor) friendIds.add (a == actor ? b : a);
            }
            if (auto* r = roomOf (actor))
            {
                const auto rid = r->id;
                const bool wasHost = r->hostId == actor;
                r->members.erase (std::remove_if (r->members.begin(), r->members.end(), [&] (const MemberRec& x) { return x.userId == actor; }),
                                  r->members.end());
                if (r->members.empty()) deleteRoom (rid, false);
                else
                {
                    if (wasHost) r->hostId = r->members.front().userId;   // 방장이면 가장 오래 있던 멤버에게 (진짜 서버 DetachForDeletion)
                    roomChanged (*r);
                }
            }
            for (auto& [rid, r] : rooms)
                if (r.hostId == actor && ! r.members.empty()) { r.hostId = r.members.front().userId; roomChanged (r); }
            removeJoinRequestsOf (actor);
            expireInvitesTo (actor);
            me.deleted = true;
            revokeAllSessions (actor);   // 실시간 연결은 4001
            for (auto& f : friendIds)
            {
                emit (actor, "friend.removed", json::obj ({ { "user_id", f } }));
                emit (f, "friend.removed", json::obj ({ { "user_id", actor } }));
            }
            return noContent();
        }
    }

    if (m == "POST" && path == "me/password")
    {
        const auto current = body.getProperty ("current_password", "").toString();
        const auto next = body.getProperty ("new_password", "").toString();
        if (current.isEmpty()) return fail (ErrorCode::invalidRequest, {}, "current_password");
        if (v::password (next) != v::Problem::none) return fail (ErrorCode::invalidRequest, {}, "new_password");
        if (me.passwordHash != hashPassword (me.salt, current)) return fail (ErrorCode::invalidCredentials);
        me.passwordHash = hashPassword (me.salt, next);
        revokeAllSessions (actor, mySession);   // 지금 세션 외 모두 폐기 (API 4.3)
        return noContent();
    }

    if (m == "POST" && path == "me/email")
    {
        // 진짜 서버 순서 (account.RequestEmailChange): 형식 → 지금 주소와 같음 → (인증한 계정) 비밀번호 → email_taken → 30초 1회 (용도마다)
        const auto email = body.getProperty ("new_email", "").toString().trim();
        if (! serverEmailOk (email)) return fail (ErrorCode::invalidRequest, {}, "new_email");
        if (email.equalsIgnoreCase (me.email)) return fail (ErrorCode::invalidRequest, {}, "new_email");
        if (me.verified)
        {
            // 인증을 마친 계정은 지금 비밀번호가 맞아야 한다 (API 4.4)
            const auto pw = body.getProperty ("password", "").toString();
            if (pw.isEmpty()) return fail (ErrorCode::invalidRequest, {}, "password");
            if (me.passwordHash != hashPassword (me.salt, pw)) return fail (ErrorCode::invalidCredentials);
        }
        if (auto* other = findByEmail (email); other != nullptr && other != &me) return fail (ErrorCode::emailTaken);
        if (! me.verified)
        {
            // 인증 전: 주소를 바로 바꾸고 가입 인증 코드를 새 주소로 (4.4)
            if (now() - me.lastSignupMailAt < resendGapMs) return fail (ErrorCode::rateLimited);
            me.email = email;
            me.verifyCode = newMailCode();
            me.verifyCodeAt = me.lastSignupMailAt = now();
            me.verifyFails = 0;
            return noContent();
        }
        if (now() - me.lastEmailChangeMailAt < resendGapMs) return fail (ErrorCode::rateLimited);
        me.pendingEmail = email;
        me.emailCode = newMailCode();
        me.emailCodeAt = me.lastEmailChangeMailAt = now();
        me.emailFails = 0;
        return noContent();
    }

    if (m == "POST" && path == "me/email/verify")
    {
        if (! me.pendingEmail || me.emailCode.isEmpty() || now() - me.emailCodeAt > codeLifeMs) return fail (ErrorCode::codeExpired);
        if (body.getProperty ("code", "").toString() != me.emailCode)
        {
            if (++me.emailFails >= 5) me.emailCode = {};
            return fail (ErrorCode::codeInvalid);
        }
        if (auto* other = findByEmail (*me.pendingEmail); other != nullptr && other != &me)
        {
            // 그사이 다른 계정이 그 주소를 씀: 변경 요청을 지운다 (진짜 서버 account.go)
            me.pendingEmail.reset();
            me.emailCode = {};
            return fail (ErrorCode::emailTaken);
        }
        me.email = *me.pendingEmail;
        me.pendingEmail.reset();
        me.emailCode = {};
        return ok (json::encode (meOf (me)));
    }

    if (m == "GET" && n == 2 && seg[0] == "users")
    {
        auto* u = findUser (seg[1]);
        if (u == nullptr) return fail (ErrorCode::notFound);
        UserProfile p;
        p.user = publicOf (*u);
        const bool friends = areFriends (actor, u->id);
        if (friends) p.relation = "friend";
        else if (hasBlocked (actor, u->id)) p.relation = "blocked_by_me";
        else
        {
            for (auto& f : friendRequests)
            {
                if (f.from == actor && f.to == u->id) p.relation = "request_sent";
                if (f.from == u->id && f.to == actor && ! f.hidden) p.relation = "request_received";
            }
        }
        if (friends)
        {
            auto fv = friendView (u->id, actor);
            p.status = fv.status;
            p.lastSeenAt = fv.lastSeenAt;
            p.room = fv.room;
            p.canInvite = fv.canInvite;
        }
        // 상대 설정만 본다 (상대가 나를 차단했어도 설정이 everyone 이면 true, 차단 사실을 숨긴다)
        p.canSendFriendRequest = u->settings.friendRequests == "everyone";
        return ok (json::encode (p));
    }

    if (m == "POST" && path == "me/rtt")
    {
        const auto value = body.getProperty ("aoo_rtt_ms", var());
        if (! hasField (body, "aoo_rtt_ms") || value.isVoid()) return fail (ErrorCode::invalidRequest, {}, "aoo_rtt_ms");
        if (! (value.isInt() || value.isInt64())) return fail (ErrorCode::invalidRequest);   // 정수가 아님 (본문 해석 실패)
        const int ms = (int) value;
        if (ms < 0 || ms > 10000) return fail (ErrorCode::invalidRequest, {}, "aoo_rtt_ms");
        const bool changed = me.rttMs != OptInt (ms);
        me.rttMs = ms;
        if (changed)
            for (auto& [rid, r] : rooms)
                if (r.hostId == actor) emitRooms ("rooms.updated", r);
        return noContent();
    }

    if (m == "POST" && path == "telemetry/latency")
    {
        // 5.10: room_id 형식과 값 범위(0~10000 ms)만 본다 (진짜 서버와 같게. 그 방의 멤버인지는 보지 않는다).
        // 빠진 값은 0 으로 받는다. 정수가 아니면 본문 해석 실패 (필드 없음)
        static const char* fields[] = { "estimated_ms", "my_rtt_ms", "host_rtt_ms", "actual_p2p_rtt_ms" };
        for (auto* field : fields)
        {
            const auto value = body.getProperty (field, var());
            if (hasField (body, field) && ! value.isVoid() && ! (value.isInt() || value.isInt64())) return fail (ErrorCode::invalidRequest);
        }
        if (! body.getProperty ("room_id", "").toString().startsWith ("r_")) return fail (ErrorCode::invalidRequest, {}, "room_id");
        for (auto* field : fields)
        {
            const auto ms = (juce::int64) body.getProperty (field, 0);
            if (ms < 0 || ms > 10000) return fail (ErrorCode::invalidRequest, {}, field);
        }
        return noContent();
    }

    //------------------------------------------------------------------ 5 방
    if (seg.size() >= 1 && seg[0] == "rooms")
    {
        if (n == 1 && m == "GET")
        {
            std::vector<const RoomRec*> list;
            for (auto& [rid, r] : rooms)
                if (r.type == "public" && ! blockedEitherWay (actor, r.hostId)) list.push_back (&r);
            std::sort (list.begin(), list.end(), [] (const RoomRec* a, const RoomRec* b) { return a->createdAt > b->createdAt; });
            juce::Array<var> arr;
            for (auto* r : list) arr.add (json::encode (roomView (*r, actor)));
            return ok (json::obj ({ { "rooms", var (arr) } }));
        }

        if (n == 1 && m == "POST")
        {
            // 진짜 서버 순서: capacity 빠짐·null → type → entry_mode (비공개 방은 키 자체가 없어야) → title → description → capacity 범위
            if (! hasField (body, "capacity") || isNullField (body, "capacity")) return fail (ErrorCode::invalidRequest, {}, "capacity");
            CreateRoom c;
            if (! json::decode (body, c)) return fail (ErrorCode::invalidRequest);
            if (c.type != "public" && c.type != "private") return fail (ErrorCode::invalidRequest, {}, "type");
            if (c.type == "public" && c.entryMode != String ("instant") && c.entryMode != String ("approval"))
                return fail (ErrorCode::invalidRequest, {}, "entry_mode");
            if (c.type == "private" && hasField (body, "entry_mode")) return fail (ErrorCode::invalidRequest, {}, "entry_mode");
            if (v::roomTitle (c.title) != v::Problem::none || hasControl (v::normalise (c.title), false))
                return fail (ErrorCode::invalidRequest, {}, "title");
            if (v::roomDescription (c.description) != v::Problem::none || hasControl (v::normalise (c.description), true))
                return fail (ErrorCode::invalidRequest, {}, "description");
            if (c.capacity < 2 || c.capacity > 7) return fail (ErrorCode::invalidRequest, {}, "capacity");
            if (roomOf (actor) != nullptr) return fail (ErrorCode::alreadyInRoom);

            RoomRec r;
            r.id = newId ("r_");
            r.type = c.type;
            r.entryMode = c.entryMode;
            r.title = v::normalise (c.title);
            r.description = v::normalise (c.description);
            r.capacity = c.capacity;
            r.hostId = actor;
            r.createdAt = now();
            if (r.type == "private") { r.inviteCode = newRoomCode(); r.groupName = r.inviteCode; }
            else r.groupName = "meon-fake-" + r.id;
            rooms[r.id] = r;
            auto& rr = rooms[r.id];
            addMember (rr, actor);
            emitRooms ("rooms.added", rr);
            friendChanged (actor);
            return created (json::encode (detailView (rr, actor)));
        }

        if (n == 2 && seg[1] == "join-by-code" && m == "POST")
        {
            const auto code = body.getProperty ("code", "").toString().toUpperCase();
            if (! v::isRoomCode (code)) return fail (ErrorCode::invalidCode);
            if (auto f = fixedCodes.find (code); f != fixedCodes.end()) return fail (errorFromKey (f->second));
            if (closedRooms.count (code) > 0 && now() - closedRooms[code] < closedMemoryMs) return fail (ErrorCode::roomClosed);
            auto* r = roomByCode (code);
            if (r == nullptr)
            {
                // 가짜 기간: 다른 컴퓨터가 만든 방은 이 서버가 모른다. 코드가 형식에 맞으면 그 코드의 방으로 들어간다 (1.5).
                // 방장은 그 컴퓨터의 AOO 상대가 들어오면 알게 된다 (remotePeerJoined).
                RoomRec nr;
                nr.id = "r_code_" + code;
                nr.type = "private";
                nr.title = String::fromUTF8 ("비공개 방");
                nr.capacity = 7;
                nr.hostId = "u_remote_" + code;
                nr.inviteCode = code;
                nr.groupName = code;
                nr.createdAt = now();
                rooms[nr.id] = nr;
                r = &rooms[nr.id];
            }
            return joinCommon (actor, *r, false, {});
        }

        // 5.6 퇴장: 그 방 멤버가 아니거나, 방이 없거나 닫혔어도 204 (재시도해도 같게)
        if (n == 3 && seg[2] == "leave" && m == "POST")
        {
            auto* lr = findRoom (seg[1]);
            if (lr == nullptr || ! isMember (lr->id, actor)) return noContent();
            const auto rid = lr->id;
            removeMember (*lr, actor);
            if (lr->members.empty()) deleteRoom (rid, false);
            else roomChanged (*lr);
            friendChanged (actor);
            return noContent();
        }

        // 6.2·6.3 본문의 user_id 는 방을 찾기 전에 본다: 비었으면 400, 형식이 틀리면 방에 없는 사람 (진짜 서버 targetUser)
        if (n == 3 && (seg[2] == "transfer-host" || seg[2] == "kick") && m == "POST")
        {
            const auto target = body.getProperty ("user_id", "").toString();
            if (target.isEmpty()) return fail (ErrorCode::invalidRequest, {}, "user_id");
            if (! target.startsWith ("u_")) return fail (ErrorCode::targetNotInRoom);
        }

        auto* r = n >= 2 ? findRoom (seg[1]) : nullptr;
        if (n >= 2 && r == nullptr)
        {
            if (closedRooms.count (seg[1]) > 0 && now() - closedRooms[seg[1]] < closedMemoryMs) return fail (ErrorCode::roomClosed);
            return fail (ErrorCode::roomNotFound);
        }

        if (n == 2 && m == "GET")
        {
            if (isMember (r->id, actor)) return ok (json::encode (detailView (*r, actor)));
            if (r->type == "private" || blockedEitherWay (actor, r->hostId)) return fail (ErrorCode::roomNotFound);
            return ok (json::encode (roomView (*r, actor)));
        }

        if (n == 3 && seg[2] == "join" && m == "POST")
        {
            if (r->type != "public") return fail (ErrorCode::roomNotFound);
            return joinCommon (actor, *r, false, {});
        }

        // 방장 전용 (6장): 지금 방에 있는 방장만
        const bool isHostHere = r != nullptr && r->hostId == actor && isMember (r->id, actor);

        if (n == 3 && seg[2] == "join-requests" && m == "GET")
        {
            if (! isHostHere) return fail (ErrorCode::notHost);
            juce::Array<var> arr;
            for (auto& j : joinRequests)
                if (j.roomId == r->id) arr.add (json::encode (joinRequestView (j)));
            return ok (json::obj ({ { "requests", var (arr) } }));
        }

        if (n == 5 && seg[2] == "join-requests" && m == "POST")
        {
            if (! isHostHere) return fail (ErrorCode::notHost);
            auto it = std::find_if (joinRequests.begin(), joinRequests.end(),
                                    [&] (const JoinReqRec& j) { return j.id == seg[3] && j.roomId == r->id && j.expiresAt > now(); });
            if (it == joinRequests.end()) return fail (ErrorCode::notFound);   // 없거나 만료된 요청 (5.7)
            const auto userId = it->userId;

            if (seg[4] == "decline")
            {
                joinRequests.erase (it);
                return noContent();
            }
            if (seg[4] != "accept") return fail (ErrorCode::notFound);

            if (! isOnline (userId) || roomOf (userId) != nullptr)
            {
                joinRequests.erase (it);
                return fail (ErrorCode::requestUnavailable);
            }
            if (r->kicked.count (userId) > 0 || blockedEitherWay (userId, r->hostId))
            {
                joinRequests.erase (it);
                return fail (ErrorCode::requestUnavailable);
            }
            if (! hasRoomFor (*r, userId)) return fail (ErrorCode::roomFull);   // 요청은 남는다
            joinRequests.erase (it);   // 수락한 요청은 먼저 지운다 (방장에게 removed 가 가지 않게)
            addMember (*r, userId);
            emit (userId, "join_request.accepted", json::obj ({ { "room", json::encode (detailView (*r, userId)) } }));
            roomChanged (*r);
            friendChanged (userId);
            return ok (json::encode (detailView (*r, actor)));
        }

        if (n == 2 && m == "PATCH")
        {
            if (! isHostHere) return fail (ErrorCode::notHost);
            RoomPatch p;
            if (! json::decode (body, p)) return fail (ErrorCode::invalidRequest);
            // 진짜 서버 순서: title → description → type → entry_mode → capacity 범위 → 인원보다 작음
            const auto newType = p.type.value_or (r->type);
            if (p.title && (v::roomTitle (*p.title) != v::Problem::none || hasControl (v::normalise (*p.title), false)))
                return fail (ErrorCode::invalidRequest, {}, "title");
            if (p.description && (v::roomDescription (*p.description) != v::Problem::none || hasControl (v::normalise (*p.description), true)))
                return fail (ErrorCode::invalidRequest, {}, "description");
            if (newType != "public" && newType != "private") return fail (ErrorCode::invalidRequest, {}, "type");
            if (newType == "private" && p.setEntryMode && p.entryMode) return fail (ErrorCode::invalidRequest, {}, "entry_mode");
            if (newType == "public" && p.setEntryMode && p.entryMode != String ("instant") && p.entryMode != String ("approval"))
                return fail (ErrorCode::invalidRequest, {}, "entry_mode");
            if (p.capacity && (*p.capacity < 2 || *p.capacity > 7)) return fail (ErrorCode::invalidRequest, {}, "capacity");
            if (p.capacity && *p.capacity < (int) r->members.size() + (hostPresent (*r) ? 0 : 1))
                return fail (ErrorCode::capacityBelowMembers);

            const auto oldType = r->type;
            const auto oldMode = r->entryMode;
            if (p.title) r->title = v::normalise (*p.title);
            if (p.description) r->description = v::normalise (*p.description);
            if (p.capacity) r->capacity = *p.capacity;

            if (oldType == "public" && newType == "private")
            {
                r->type = "private";
                r->entryMode.reset();
                r->inviteCode = newRoomCode();
                removeJoinRequestsForRoom (r->id);
                emitRoomRemoved (r->id, r->hostId);
            }
            else if (oldType == "private" && newType == "public")
            {
                closedRooms.erase (r->inviteCode);
                r->type = "public";
                r->inviteCode = {};   // 옛 코드는 즉시 무효
                r->entryMode = p.setEntryMode && p.entryMode ? p.entryMode : OptString ("approval");
                emitRooms ("rooms.added", *r);
            }
            else if (newType == "public" && p.setEntryMode && p.entryMode)
            {
                r->entryMode = p.entryMode;
            }
            if (oldMode == String ("approval") && r->entryMode != String ("approval"))
                removeJoinRequestsForRoom (r->id);

            // 비공개 → 공개는 rooms.added 다음 rooms.updated 도 간다 (진짜 서버: 목록 이벤트 뒤 RoomChanged)
            roomChanged (*r);
            return ok (json::encode (detailView (*r, actor)));
        }

        if (n == 3 && seg[2] == "transfer-host" && m == "POST")
        {
            if (! isHostHere) return fail (ErrorCode::notHost);
            const auto target = body.getProperty ("user_id", "").toString();
            if (target == actor) return fail (ErrorCode::invalidRequest, {}, "user_id");
            if (! isMember (r->id, target)) return fail (ErrorCode::targetNotInRoom);
            r->hostId = target;
            roomChanged (*r);
            return ok (json::encode (detailView (*r, actor)));
        }

        if (n == 3 && seg[2] == "kick" && m == "POST")
        {
            if (! isHostHere) return fail (ErrorCode::notHost);
            const auto target = body.getProperty ("user_id", "").toString();
            if (target == actor) return fail (ErrorCode::invalidRequest, {}, "user_id");
            if (! isMember (r->id, target)) return fail (ErrorCode::targetNotInRoom);
            r->kicked.insert (target);
            emit (target, "room.kicked", json::obj ({ { "room_id", r->id } }));
            removeMember (*r, target);
            roomChanged (*r);
            friendChanged (target);
            return noContent();
        }

        if (n == 3 && seg[2] == "close" && m == "POST")
        {
            if (! isHostHere) return fail (ErrorCode::notHost);
            deleteRoom (r->id, true);
            return noContent();
        }

        return fail (ErrorCode::notFound);
    }

    //------------------------------------------------------------------ 7 친구
    if (seg.size() >= 1 && seg[0] == "friends")
    {
        if (n == 1 && m == "GET")
            return ok (json::obj ({ { "friends", json::encodeList (friendsOf (actor)) } }));

        if (n == 2 && seg[1] == "requests" && m == "GET")
        {
            std::vector<const FriendReqRec*> list;
            for (auto& f : friendRequests)
                if (f.to == actor && ! f.hidden && findUser (f.from) != nullptr) list.push_back (&f);
            // 최신순 (진짜 서버: created_at DESC, id DESC)
            std::stable_sort (list.begin(), list.end(), [] (const FriendReqRec* a, const FriendReqRec* b)
                              { return a->at != b->at ? a->at > b->at : a->id > b->id; });
            juce::Array<var> arr;
            for (auto* f : list)
                arr.add (json::encode (FriendRequest { f->id, publicOf (f->from), json::isoTime (f->at) }));
            return ok (json::obj ({ { "requests", var (arr) } }));
        }

        if (n == 2 && seg[1] == "requests" && m == "POST")
        {
            const auto username = body.getProperty ("username", "").toString().trim().toLowerCase();
            if (username.isEmpty()) return fail (ErrorCode::invalidRequest, {}, "username");
            auto result = [] (const char* r) { return ok (json::obj ({ { "result", r } })); };
            if (username == me.username) return result ("self");
            auto* target = findByUsername (username);
            if (target == nullptr || target->deleted) return result ("not_found");
            if (hasBlocked (actor, target->id)) return fail (ErrorCode::blockedByMe);
            if (areFriends (actor, target->id)) return result ("already_friends");

            for (auto& f : friendRequests)
            {
                if (f.from == target->id && f.to == actor && ! f.hidden)
                {
                    // 상대가 먼저 요청해 둠 → 바로 친구. 둘 사이의 요청은 숨은 기록까지 모두 지우고 양쪽에 friend.added
                    const auto other = target->id;
                    friendships.insert (pairKey (actor, other));
                    removeFriendRequestsBetween (actor, other);
                    emit (actor, "friend.added", json::encode (friendView (other, actor)));
                    emit (other, "friend.added", json::encode (friendView (actor, other)));
                    return result ("accepted");
                }
            }
            for (auto& f : friendRequests)
                if (f.from == actor && f.to == target->id) return result ("already_requested");

            FriendReqRec f { newId ("fr_"), actor, target->id, now() };
            f.hidden = hasBlocked (target->id, actor) || target->settings.friendRequests == "none";
            friendRequests.push_back (f);
            if (! f.hidden)
            {
                emit (target->id, "friend_request.received", json::encode (FriendRequest { f.id, publicOf (actor), json::isoTime (f.at) }));
                if (! target->local)
                {
                    std::weak_ptr<bool> weak = alive;
                    const auto fid = f.id;
                    scheduler.post (6000, [this, weak, fid] { if (! weak.expired()) fakeUserAcceptFriend (fid); });
                }
            }
            return result ("sent");
        }

        if (n == 4 && seg[1] == "requests" && m == "POST")
        {
            auto it = std::find_if (friendRequests.begin(), friendRequests.end(),
                                    [&] (const FriendReqRec& f) { return f.id == seg[2] && f.to == actor && ! f.hidden; });
            if (it == friendRequests.end() || findUser (it->from) == nullptr) return fail (ErrorCode::notFound);   // 보낸 사람이 탈퇴 유예 중이어도
            if (seg[3] != "decline" && seg[3] != "accept") return fail (ErrorCode::notFound);
            const auto from = it->from;
            if (seg[3] == "decline")
            {
                friendRequests.erase (it);
                return noContent();
            }
            friendships.insert (pairKey (actor, from));
            removeFriendRequestsBetween (actor, from);   // 반대 방향 요청(숨은 기록 포함)도 지운다
            emit (actor, "friend.added", json::encode (friendView (from, actor)));
            emit (from, "friend.added", json::encode (friendView (actor, from)));
            return noContent();
        }

        if (n == 2 && m == "DELETE")
        {
            if (! areFriends (actor, seg[1])) return fail (ErrorCode::notFound);   // 7.6
            friendships.erase (pairKey (actor, seg[1]));
            emit (actor, "friend.removed", json::obj ({ { "user_id", seg[1] } }));
            emit (seg[1], "friend.removed", json::obj ({ { "user_id", actor } }));
            return noContent();
        }
        return fail (ErrorCode::notFound);
    }

    //------------------------------------------------------------------ 7 초대
    if (seg.size() >= 1 && seg[0] == "invites")
    {
        if (n == 1 && m == "POST")
        {
            const auto target = body.getProperty ("user_id", "").toString();
            auto* r = roomOf (actor);
            if (r == nullptr) return fail (ErrorCode::notInRoom);
            if (! areFriends (actor, target)) return fail (ErrorCode::notFriend);
            auto* t = findUser (target);
            if (t == nullptr) return fail (ErrorCode::notFriend);
            if (t->settings.roomInvites == "none") return fail (ErrorCode::notAccepting);
            if (! t->online) return fail (ErrorCode::targetOffline);
            if (roomOf (target) != nullptr) return fail (ErrorCode::targetInRoom);
            if (! hasRoomFor (*r, target)) return fail (ErrorCode::roomFull);
            // 한 방에서 같은 사람에게 한 번 (누가 보냈든): 3초 대기 중, 전달됐지만 수락·무시 전, 무시함 (API 7.7)
            if (inviteExists (r->id, target)) return fail (ErrorCode::alreadyInvited);

            InviteRec i { newId ("inv_"), actor, target, r->id, now(), now() + inviteDelayMs, false };
            invites.push_back (i);
            std::weak_ptr<bool> weak = alive;
            const auto iid = i.id;
            scheduler.post (inviteDelayMs, [this, weak, iid] { if (! weak.expired()) deliverInvite (iid); });
            return created (json::obj ({ { "id", i.id }, { "deliver_at", json::isoTime (i.deliverAt) } }));
        }

        auto it = n >= 2 ? std::find_if (invites.begin(), invites.end(), [&] (const InviteRec& i) { return i.id == seg[1]; })
                         : invites.end();

        if (n == 2 && m == "DELETE")
        {
            if (it == invites.end() || it->from != actor) return fail (ErrorCode::notFound);
            if (it->delivered || it->ended.isNotEmpty()) return fail (ErrorCode::alreadyDelivered);   // 전달 때 버린 것 포함
            invites.erase (it);
            return noContent();
        }

        if (n == 3 && m == "POST" && (seg[2] == "accept" || seg[2] == "dismiss"))
        {
            // 진짜 서버 lockReceivedInvite: 나에게 전달된 초대가 아니면 404 → 방이 닫혔으면 410 → 끝난 초대면 404
            if (it == invites.end() || it->to != actor || ! it->delivered) return fail (ErrorCode::notFound);
            const auto inv = *it;
            auto* r = findRoom (inv.roomId);
            if (r == nullptr)
                return closedRooms.count (inv.roomId) > 0 && now() - closedRooms[inv.roomId] < closedMemoryMs
                           ? fail (ErrorCode::roomClosed) : fail (ErrorCode::notFound);
            if (inv.ended.isNotEmpty()) return fail (ErrorCode::notFound);
            if (seg[2] == "dismiss")
            {
                it->ended = "dismissed";   // 기록은 남는다: 같은 방에서 다시 초대할 수 없다
                return noContent();
            }
            // 수락으로 먼저 끝낸다 (입장 정리의 invite.expired 대상이 아니게). 입장이 실패하면 되돌린다.
            // 초대한 사람이 수락 시점에 방장이면 승인 없이 (7.9), 아니면 via_invite 승인 요청 (202 여도 초대는 끝난다)
            it->ended = "accepted";
            auto out = joinCommon (actor, *r, true, inv.from);
            if (out.status >= 400)
                for (auto& x : invites)
                    if (x.id == inv.id) x.ended = {};
            return out;
        }
        return fail (ErrorCode::notFound);
    }

    //------------------------------------------------------------------ 8 차단
    if (seg.size() >= 1 && seg[0] == "blocks")
    {
        if (n == 1 && m == "GET")
        {
            std::vector<std::pair<String, juce::int64>> list;
            if (auto b = blocks.find (actor); b != blocks.end())
                for (auto& [uid, at] : b->second)
                    if (findUser (uid) != nullptr) list.push_back ({ uid, at });
            // 최신순, 같으면 사용자 ID 순 (진짜 서버 ListBlocks)
            std::stable_sort (list.begin(), list.end(), [] (const auto& a, const auto& b) { return a.second != b.second ? a.second > b.second : a.first < b.first; });
            juce::Array<var> arr;
            for (auto& [uid, at] : list)
                arr.add (json::encode (Block { publicOf (uid), json::isoTime (at) }));
            return ok (json::obj ({ { "blocks", var (arr) } }));
        }
        if (n == 1 && m == "POST")
        {
            const auto target = body.getProperty ("user_id", "").toString();
            if (target == actor) return fail (ErrorCode::invalidRequest, {}, "user_id");
            if (findUser (target) == nullptr) return fail (ErrorCode::notFound);
            if (areFriends (actor, target)) return fail (ErrorCode::isFriend);
            if (hasBlocked (actor, target)) return noContent();   // 이미 차단함: 그대로 (차단 시각도 그대로)
            blocks[actor][target] = now();
            removeFriendRequestsBetween (actor, target);
            // 차단한 사람의 공개 방 목록에서 상대가 방장인 방이 빠진다 (구독 중이면 rooms.removed)
            if (subscribed && wsUser == actor)
                for (auto& [rid, r] : rooms)
                    if (r.type == "public" && r.hostId == target) emit (actor, "rooms.removed", json::obj ({ { "room_id", rid } }));
            return noContent();
        }
        if (n == 2 && m == "DELETE")
        {
            const auto other = seg[1];
            if (! hasBlocked (actor, other)) return noContent();   // 차단한 적 없음: 아무것도 하지 않는다
            blocks[actor].erase (other);
            friendRequests.erase (std::remove_if (friendRequests.begin(), friendRequests.end(), [&] (const FriendReqRec& f)
                                  { return f.from == other && f.to == actor && f.hidden; }),
                                  friendRequests.end());
            // 반대 방향 차단이 없으면 상대가 방장인 공개 방이 내 목록에 다시 나타난다 (구독 중이면 rooms.added)
            if (subscribed && wsUser == actor && ! hasBlocked (other, actor))
                for (auto& [rid, r] : rooms)
                    if (r.type == "public" && r.hostId == other) emit (actor, "rooms.added", json::encode (roomView (r, actor)));
            return noContent();
        }
        return fail (ErrorCode::notFound);
    }

    //------------------------------------------------------------------ 9 알림
    if (m == "GET" && path == "notifications/state")
        return ok (json::obj ({ { "has_unread", hasUnread (actor) } }));

    if (m == "POST" && path == "notifications/read")
    {
        for (auto& f : friendRequests)
            if (f.to == actor) f.read = true;
        return noContent();
    }

    return fail (ErrorCode::notFound);
}

//==============================================================================
// 초대·친구 요청의 상대가 가짜 사용자일 때

void FakeMeonServer::deliverInvite (const String& inviteId)
{
    for (auto it = invites.begin(); it != invites.end(); ++it)
    {
        if (it->id != inviteId || it->delivered || it->ended.isNotEmpty()) continue;
        // 전달 시점에 오프라인이거나 방에 있으면 조용히 버린다 (7.7). 보낸 사람에게는 "초대됨" 그대로. 받은 사람에게는 없음.
        // 기록은 dropped 로 남는다 (취소하면 already_delivered, 같은 사람에게 다시 초대할 수 있다)
        it->delivered = true;
        if (! isOnline (it->to) || roomOf (it->to) != nullptr || findRoom (it->roomId) == nullptr)
        {
            it->ended = "dropped";
            return;
        }
        emit (it->to, "invite.received", json::encode (inviteView (*it)));
        if (auto* u = findUser (it->to); u != nullptr && ! u->local)
        {
            std::weak_ptr<bool> weak = alive;
            scheduler.post (4000, [this, weak, inviteId] { if (! weak.expired()) fakeUserAcceptInvite (inviteId); });
        }
        return;
    }
}

void FakeMeonServer::fakeUserAcceptInvite (const String& inviteId)
{
    auto it = std::find_if (invites.begin(), invites.end(), [&] (const InviteRec& i) { return i.id == inviteId; });
    if (it == invites.end() || it->ended.isNotEmpty()) return;
    const auto inv = *it;
    auto* r = findRoom (inv.roomId);
    if (r == nullptr || roomOf (inv.to) != nullptr || ! hasRoomFor (*r, inv.to)) return;
    const bool direct = r->type == "private" || r->entryMode != String ("approval") || inv.from == r->hostId;
    if (! direct) return;
    it->ended = "accepted";
    addMember (*r, inv.to);
    roomChanged (*r);
    friendChanged (inv.to);
}

void FakeMeonServer::fakeUserAcceptFriend (const String& requestId)
{
    auto it = std::find_if (friendRequests.begin(), friendRequests.end(), [&] (const FriendReqRec& f) { return f.id == requestId; });
    if (it == friendRequests.end() || it->hidden) return;
    const auto from = it->from, to = it->to;
    friendships.insert (pairKey (from, to));
    removeFriendRequestsBetween (from, to);
    emit (from, "friend.added", json::encode (friendView (to, from)));
    emit (to, "friend.added", json::encode (friendView (from, to)));
}

//==============================================================================
// RealtimeTransport

void FakeMeonServer::open (Sink* s)
{
    if (wsOpen) close();
    sink = s;
    ++connectionGen;
    const int gen = connectionGen;
    std::weak_ptr<bool> weak = alive;
    const bool refuse = now() < dropUntil;
    scheduler.post (options.latencyMs, [this, weak, gen, refuse]
    {
        if (weak.expired() || gen != connectionGen || sink == nullptr) return;
        if (refuse) { sink->transportClosed (0); return; }
        wsOpen = true;
        wsAuthed = false;
        seq = 0;
        sink->transportOpened();
    });
}

void FakeMeonServer::send (const var& msg)
{
    if (! wsOpen) return;
    const auto type = msg.getProperty ("type", "").toString();

    auto reply = [this] (const var& m)
    {
        std::weak_ptr<bool> weak = alive;
        const int gen = connectionGen;
        scheduler.post (options.latencyMs, [this, weak, gen, m]
        {
            if (weak.expired() || gen != connectionGen || ! wsOpen || sink == nullptr) return;
            sink->transportMessage (m);
        });
    };
    auto rejectAndClose = [this, reply] (const char* code)
    {
        reply (json::obj ({ { "type", "error" }, { "code", code } }));
        std::weak_ptr<bool> weak = alive;
        const int gen = connectionGen;
        scheduler.post (options.latencyMs + 1, [this, weak, gen]
        {
            if (weak.expired() || gen != connectionGen) return;
            wsOpen = false;
            if (sink != nullptr) sink->transportClosed (1008);   // 인증 실패 (API 10.1)
        });
    };

    // 첫 메시지는 auth 여야 한다 (아니면 unauthorized 로 닫는다). 순서: 토큰 → email_not_verified → client_outdated (API 10.1)
    if (! wsAuthed)
    {
        if (type != "auth") { rejectAndClose ("unauthorized"); return; }
        auto it = accessTokens.find (msg.getProperty ("access_token", "").toString());
        if (it == accessTokens.end() || it->second.expiresAt <= now()) { rejectAndClose ("unauthorized"); return; }
        auto se = sessions.find (it->second.sessionId);
        if (se == sessions.end() || se->second.generation != it->second.generation) { rejectAndClose ("unauthorized"); return; }
        auto* u = findUser (it->second.userId);
        if (u == nullptr) { rejectAndClose ("unauthorized"); return; }
        if (! u->verified) { rejectAndClose ("email_not_verified"); return; }
        // build 가 없으면 옛 앱으로 본다. 0 은 개발 빌드 (가짜 서버는 개발 서버처럼 받아 준다)
        const auto buildValue = msg.getProperty ("build", var());
        if (! (buildValue.isInt() || buildValue.isInt64())) { rejectAndClose ("client_outdated"); return; }
        const auto build = (juce::int64) buildValue;
        if (build < 0 || build > 0x7fffffff || (build != 0 && build < config.minBuild)) { rejectAndClose ("client_outdated"); return; }

        wsUser = u->id;
        wsSession = it->second.sessionId;
        wsAuthed = true;
        subscribed = false;
        const bool wasOnline = u->online;
        u->online = true;
        attachWorld (*u);
        sendHello();
        if (! wasOnline) friendChanged (u->id);
        scheduleScript();
        return;
    }

    if (type == "ping")        reply (json::obj ({ { "type", "pong" } }));
    else if (type == "resync") sendHello();
    else if (type == "subscribe" || type == "unsubscribe")
    {
        if (msg.getProperty ("topic", "").toString() == "public_rooms")
            subscribed = type == "subscribe";
    }
}

void FakeMeonServer::close()
{
    if (! wsOpen && sink == nullptr) return;
    const bool wasOpen = wsOpen;
    wsOpen = false;
    ++connectionGen;
    sink = nullptr;
    if (wasOpen) linkLost();
    subscribed = false;
}

void FakeMeonServer::serverCloseLink (int code)
{
    if (! wsOpen || sink == nullptr) return;
    auto* s = sink;
    wsOpen = false;
    ++connectionGen;
    // 4000 은 같은 계정이 다른 곳에서 접속한 것이라 그 사용자는 계속 접속 중이다 (오프라인·30초 정리 없음)
    if (code == 4000) subscribed = false;
    else linkLost();
    s->transportClosed (code);
}

void FakeMeonServer::linkLost()
{
    subscribed = false;
    auto* u = findUser (wsUser);
    if (u == nullptr) return;
    u->online = false;
    u->lastSeenAt = now();
    friendChanged (u->id);
    // 30초 안에 다시 붙지 않으면 정리한다 (진짜 서버 SweepDisconnected·SweepJoinRequests·SweepInvites)
    std::weak_ptr<bool> weak = alive;
    const auto userId = u->id;
    const int gen = ++sweepGen;
    scheduler.post (disconnectGraceMs, [this, weak, userId, gen]
    {
        if (! weak.expired() && gen == sweepGen) sweepDisconnected (userId);
    });
}

void FakeMeonServer::sweepDisconnected (const String& userId)
{
    auto* u = findUser (userId);
    if (u == nullptr || u->online || now() - u->lastSeenAt < disconnectGraceMs) return;
    if (auto* r = roomOf (userId))
    {
        const auto rid = r->id;
        removeMember (*r, userId);
        if (r->members.empty()) deleteRoom (rid, false);
        else roomChanged (*r);
        friendChanged (userId);
    }
    removeJoinRequestsOf (userId);       // 방장에게 removed
    expireInvitesTo (userId, false);     // 받은 사람은 오프라인이라 이벤트 없음
}

//==============================================================================
// 개발용

void FakeMeonServer::devDropLink (int seconds)
{
    dropUntil = now() + seconds * 1000LL;
    serverCloseLink (0);   // 네트워크 끊김
}

void FakeMeonServer::devCloseLink (int code)
{
    serverCloseLink (code);
}

void FakeMeonServer::devKickMe()
{
    auto* r = roomOf (wsUser);
    if (r == nullptr || r->hostId == wsUser) return;
    r->kicked.insert (wsUser);
    emit (wsUser, "room.kicked", json::obj ({ { "room_id", r->id } }));
    removeMember (*r, wsUser);
    roomChanged (*r);
    friendChanged (wsUser);
}

void FakeMeonServer::devCloseMyRoom()
{
    auto* r = roomOf (wsUser);
    if (r == nullptr || r->hostId == wsUser) return;
    deleteRoom (r->id, true);
}

void FakeMeonServer::devSetConfig (int minBuild, int latestBuild)
{
    config.minBuild = minBuild;
    config.latestBuild = latestBuild;
    emit (wsUser, "config.updated", json::encode (config));
}

void FakeMeonServer::remotePeerJoined (const String& userId, const String& nickname, const String& part, bool isHost)
{
    auto* r = roomOf (wsUser);
    if (r == nullptr || userId == wsUser) return;

    auto& u = users[userId];
    if (u.id.isEmpty())
    {
        u.id = userId;
        u.username = "peer_" + userId.substring (2, 10).toLowerCase();
        u.createdAt = now();
    }
    u.online = true;
    if (nickname.isNotEmpty()) u.nickname = nickname;
    if (parts::isValid (part)) u.part = part;
    if (! u.nickname) u.nickname = String::fromUTF8 ("멤버");
    if (! u.part) u.part = String ("other");

    if (! isMember (r->id, userId)) r->members.push_back ({ userId, now() });
    if (isHost && r->hostId.startsWith ("u_remote_")) r->hostId = userId;
    roomChanged (*r);
}

bool FakeMeonServer::isScriptedUser (const String& userId) const
{
    const auto* u = findUser (userId);
    return u != nullptr && u->scripted;
}

void FakeMeonServer::remotePeerLeft (const String& userId)
{
    auto* r = roomOf (userId);
    if (r == nullptr) return;
    removeMember (*r, userId);
    if (auto* u = findUser (userId)) u->online = false;
    if (r->members.empty()) deleteRoom (r->id, false);
    else roomChanged (*r);
}

//==============================================================================
// 저장

void FakeMeonServer::revokeSession (const String& sessionId)
{
    const auto sit = sessions.find (sessionId);
    if (sit == sessions.end()) return;
    const auto userId = sit->second.userId;
    sessions.erase (sit);
    // access 토큰은 남겨 둔다: 만료 전이면 401, 만료 뒤면 token_expired (진짜 서버와 같은 순서)
    auto& u = users[userId];
    for (auto it = refreshOwners.begin(); it != refreshOwners.end();)
    {
        if (it->second.sessionId == sessionId) { u.refreshTokens.removeString (it->first); it = refreshOwners.erase (it); }
        else ++it;
    }
    for (auto it = rotatedRefresh.begin(); it != rotatedRefresh.end();)
        it = it->second.sessionId == sessionId ? rotatedRefresh.erase (it) : std::next (it);
    for (auto it = discardedRefresh.begin(); it != discardedRefresh.end();)
        it = it->second.sessionId == sessionId ? discardedRefresh.erase (it) : std::next (it);
    stateChanged();
    // 그 세션의 실시간 연결은 4001 (세션 폐기, API 10.1)
    if (userId == wsUser && sessionId == wsSession && wsOpen && sink != nullptr)
    {
        std::weak_ptr<bool> weak = alive;
        const int gen = connectionGen;
        scheduler.post (options.latencyMs, [this, weak, gen]
        {
            if (! weak.expired() && gen == connectionGen) serverCloseLink (4001);
        });
    }
}

void FakeMeonServer::revokeAllSessions (const String& userId, const String& keepSession)
{
    juce::StringArray ids;
    for (auto& [id, se] : sessions)
        if (se.userId == userId && id != keepSession) ids.add (id);
    for (auto& id : ids) revokeSession (id);
}

juce::var FakeMeonServer::saveState() const
{
    juce::Array<var> accounts;
    for (auto& [id, u] : users)
    {
        if (! u.local) continue;
        juce::Array<var> tokens;   // refresh 와 그 세션
        for (auto& t : u.refreshTokens)
            if (auto it = refreshOwners.find (t); it != refreshOwners.end())
                tokens.add (json::obj ({ { "token", t }, { "session", it->second.sessionId } }));
        accounts.add (json::obj ({
            { "id", u.id }, { "username", u.username }, { "email", u.email }, { "salt", u.salt }, { "password_hash", u.passwordHash },
            { "nickname", u.nickname ? var (*u.nickname) : var() }, { "part", u.part ? var (*u.part) : var() },
            { "pending_email", u.pendingEmail ? var (*u.pendingEmail) : var() },
            { "email_verified", u.verified }, { "settings", json::encode (u.settings) },
            { "created_at", json::isoTime (u.createdAt) }, { "aoo_password", u.aooPassword }, { "refresh_tokens", var (tokens) },
            { "verify_code", u.verifyCode }, { "verify_code_at", u.verifyCodeAt }, { "deleted", u.deleted }
        }));
    }
    // 회전 기록도 남긴다: 응답을 받기 전에 앱이 죽었다가 다시 켜도 유예가 이어지게
    juce::Array<var> rotations;
    for (auto& [old, r] : rotatedRefresh)
        rotations.add (json::obj ({ { "old", old }, { "issued", r.issued }, { "user", r.userId }, { "session", r.sessionId }, { "at", r.at } }));
    juce::Array<var> discarded;
    for (auto& [t, r] : discardedRefresh)
        discarded.add (json::obj ({ { "token", t }, { "user", r.userId }, { "session", r.sessionId } }));
    return json::obj ({ { "version", 1 }, { "accounts", var (accounts) }, { "rotated_refresh", var (rotations) },
                        { "discarded_refresh", var (discarded) } });
}

void FakeMeonServer::loadState (const var& saved)
{
    if (auto* arr = saved.getProperty ("accounts", var()).getArray())
    {
        for (auto& a : *arr)
        {
            User u;
            u.id = a.getProperty ("id", "").toString();
            if (! u.id.startsWith ("u_")) continue;
            u.username = a.getProperty ("username", "").toString();
            u.email = a.getProperty ("email", "").toString();
            u.salt = a.getProperty ("salt", "").toString();
            u.passwordHash = a.getProperty ("password_hash", "").toString();
            auto nick = a.getProperty ("nickname", var()), part = a.getProperty ("part", var()), pe = a.getProperty ("pending_email", var());
            if (nick.isString()) u.nickname = nick.toString();
            if (part.isString()) u.part = part.toString();
            if (pe.isString()) u.pendingEmail = pe.toString();
            u.verified = (bool) a.getProperty ("email_verified", false);
            json::decode (a.getProperty ("settings", var()), u.settings);
            u.createdAt = json::parseIsoTime (a.getProperty ("created_at", "").toString());
            u.aooPassword = a.getProperty ("aoo_password", juce::Uuid().toString()).toString();
            u.verifyCode = a.getProperty ("verify_code", "").toString();
            u.verifyCodeAt = (juce::int64) a.getProperty ("verify_code_at", 0);
            u.deleted = (bool) a.getProperty ("deleted", false);
            u.local = true;
            if (auto* toks = a.getProperty ("refresh_tokens", var()).getArray())
                for (auto& t : *toks)
                {
                    // { token, session } (옛 저장 파일은 토큰 문자열만: 토큰마다 새 세션)
                    const auto token = t.isObject() ? t.getProperty ("token", "").toString() : t.toString();
                    auto sessionId = t.isObject() ? t.getProperty ("session", "").toString() : String();
                    if (token.isEmpty()) continue;
                    if (sessionId.isEmpty()) sessionId = newId ("s_");
                    sessions[sessionId] = { u.id, 0 };
                    u.refreshTokens.add (token);
                    refreshOwners[token] = { u.id, sessionId };
                }
            users[u.id] = u;
        }
    }
    if (auto* arr = saved.getProperty ("rotated_refresh", var()).getArray())
        for (auto& r : *arr)
        {
            const auto old = r.getProperty ("old", "").toString();
            const auto issued = r.getProperty ("issued", "").toString();
            auto sessionId = r.getProperty ("session", "").toString();
            if (sessionId.isEmpty())
                if (auto it = refreshOwners.find (issued); it != refreshOwners.end()) sessionId = it->second.sessionId;
            if (old.isNotEmpty())
                rotatedRefresh[old] = { r.getProperty ("user", "").toString(), sessionId, issued, (juce::int64) r.getProperty ("at", 0) };
        }
    if (auto* arr = saved.getProperty ("discarded_refresh", var()).getArray())
        for (auto& d : *arr)
            if (const auto t = d.getProperty ("token", "").toString(); t.isNotEmpty())
                discardedRefresh[t] = { d.getProperty ("user", "").toString(), d.getProperty ("session", "").toString() };
}

} // namespace meon::core
