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
    for (auto& [id, u] : users)
        if (! u.deleted && u.email.equalsIgnoreCase (email)) return &u;
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
        if (i.to == userId && i.delivered) ++n;
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
    auto ru = requestUntil.find (viewer);
    if (ru != requestUntil.end() && ru->second.roomId == r.id && ru->second.until > now())
        x.myJoinRequestUntil = json::isoTime (ru->second.until);
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
    f.canInvite = online && room == nullptr && u->settings.roomInvites == "friends";   // API 2.4·2.10: 상대 기준 (내 방 여부와 무관)
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
    auto rank = [] (const Friend& f) { return f.status == "in_room" ? 0 : f.status == "online" ? 1 : 2; };
    std::stable_sort (list.begin(), list.end(), [&] (const Friend& x, const Friend& y)
    {
        if (rank (x) != rank (y)) return rank (x) < rank (y);
        if (x.status == "offline") return x.lastSeenAt.value_or ("") > y.lastSeenAt.value_or ("");
        return x.user.nickname.value_or ("") < y.user.nickname.value_or ("");
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
        if (i.to == userId && i.delivered) h.pendingInvites.push_back (inviteView (i));
    h.config = config;
    return h;
}

//==============================================================================
// 이벤트

void FakeMeonServer::emit (const String& toUser, const String& type, const var& data)
{
    if (! wsOpen || wsUser.isEmpty() || toUser != wsUser || sink == nullptr) return;
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
    // 멤버를 친구로 둔 나에게 친구 상태(방 정보·joinable)도 바뀐다
    if (wsUser.isNotEmpty())
        for (auto& m : r.members)
            if (m.userId != wsUser && areFriends (wsUser, m.userId))
                emit (wsUser, "friend.updated", json::encode (friendView (m.userId, wsUser)));
}

void FakeMeonServer::friendChanged (const String& userId)
{
    if (wsUser.isNotEmpty() && areFriends (wsUser, userId))
        emit (wsUser, "friend.updated", json::encode (friendView (userId, wsUser)));
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
    if (userId == r.hostId)
        removeJoinRequestsForRoom (r.id);   // 방장이 나가면 대기 요청 즉시 삭제 (5.7)
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

    // 그 방 초대 전부 소멸 (받은 사람에게 invite.expired)
    for (auto it = invites.begin(); it != invites.end();)
    {
        if (it->roomId == roomId)
        {
            if (it->delivered) emit (it->to, "invite.expired", json::obj ({ { "invite_id", it->id } }));
            it = invites.erase (it);
        }
        else ++it;
    }
    removeJoinRequestsForRoom (roomId);

    closedRooms[roomId] = now();
    if (copy.type == "private" && copy.inviteCode.isNotEmpty()) closedRooms[copy.inviteCode] = now();
    rooms.erase (roomId);

    if (copy.type == "public") emitRoomRemoved (roomId, copy.hostId);
    for (auto& m : copy.members)
        friendChanged (m.userId);
}

void FakeMeonServer::expireInvitesTo (String userId)
{
    for (auto it = invites.begin(); it != invites.end();)
    {
        if (it->to == userId)
        {
            if (it->delivered) emit (userId, "invite.expired", json::obj ({ { "invite_id", it->id } }));
            it = invites.erase (it);
        }
        else ++it;
    }
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
        if (! hostPresent (r)) return fail (ErrorCode::hostAbsent);

        auto ru = requestUntil.find (actor);
        if (ru != requestUntil.end() && ru->second.roomId == r.id && ru->second.until > now())
            return { 202, json::obj ({ { "status", "requested" }, { "request_until", json::isoTime (ru->second.until) } }) };

        removeJoinRequestsOf (actor);   // 한 번에 한 방에만
        JoinReqRec j;
        j.id = newId ("jr_");
        j.roomId = r.id;
        j.userId = actor;
        j.viaInvite = viaInvite;
        j.at = now();
        j.expiresAt = j.at + joinRequestLifeMs;
        joinRequests.push_back (j);
        requestUntil[actor] = { r.id, j.at + requestUntilMs };
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
        const auto userId = j.userId;
        addMember (*r, userId);   // 요청도 지운다
        emit (userId, "join_request.accepted", json::obj ({ { "room", json::encode (detailView (*r, userId)) } }));
        roomChanged (*r);
        return;
    }
}

//==============================================================================
// RestTransport

void FakeMeonServer::send (const HttpRequest& req, std::function<void (HttpResponse)> done)
{
    auto response = handle (req);
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

FakeMeonServer::Out FakeMeonServer::issueTokens (User& u, int status, bool withMe)
{
    const auto at = newId ("at_"), rt = newId ("rt_");
    accessTokens[at] = { u.id, now() + accessLifeMs };
    refreshOwners[rt] = u.id;
    u.refreshTokens.add (rt);
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
            const auto email = body.getProperty ("email", "").toString().trim();
            const auto username = body.getProperty ("username", "").toString().toLowerCase();
            const auto password = body.getProperty ("password", "").toString();
            if (! (bool) body.getProperty ("agreed_terms", false)) return fail (ErrorCode::invalidRequest, {}, "agreed_terms");
            if (v::email (email) != v::Problem::none) return fail (ErrorCode::invalidRequest, {}, "email");
            if (v::username (username) != v::Problem::none) return fail (ErrorCode::invalidRequest, {}, "username");
            if (v::password (password) != v::Problem::none) return fail (ErrorCode::invalidRequest, {}, "password");
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
            u.verifyCodeAt = u.lastMailAt = now();
            users[u.id] = u;
            return issueTokens (users[u.id], 201, true);
        }

        if (m == "POST" && what == "login")
        {
            auto* u = findByEmail (body.getProperty ("email", "").toString().trim());
            if (u == nullptr || u->passwordHash != hashPassword (u->salt, body.getProperty ("password", "").toString()))
                return fail (ErrorCode::invalidCredentials, String::fromUTF8 ("이메일 또는 비밀번호가 맞지 않아요"));
            return issueTokens (*u, 200, true);
        }

        if (m == "POST" && what == "refresh")
        {
            const auto rt = body.getProperty ("refresh_token", "").toString();
            auto it = refreshOwners.find (rt);
            if (it == refreshOwners.end()) return fail (ErrorCode::unauthorized);
            auto* u = findUser (it->second);
            if (u == nullptr) return fail (ErrorCode::unauthorized);
            refreshOwners.erase (it);
            u->refreshTokens.removeString (rt);
            return issueTokens (*u, 200, false);
        }
    }

    // 여기부터 인증 필요
    const auto token = req.accessToken;
    auto sit = accessTokens.find (token);
    if (token.isEmpty() || sit == accessTokens.end()) return fail (ErrorCode::unauthorized);
    if (sit->second.expiresAt <= now()) return fail (ErrorCode::tokenExpired);
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
    auto& me = users[actor];
    const auto& body = req.body;
    const auto path = seg.joinIntoString ("/");
    const int n = seg.size();

    //------------------------------------------------------------------ 3 계정
    if (m == "POST" && path == "auth/logout")
    {
        const auto rt = body.getProperty ("refresh_token", "").toString();
        refreshOwners.erase (rt);
        me.refreshTokens.removeString (rt);
        accessTokens.erase (req.accessToken);
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
        if (now() - me.lastMailAt < resendGapMs) return fail (ErrorCode::rateLimited);
        me.verifyCode = newMailCode();
        me.verifyCodeAt = me.lastMailAt = now();
        me.verifyFails = 0;
        return noContent();
    }

    //------------------------------------------------------------------ 4 내 계정
    if (path == "me")
    {
        if (m == "GET") return ok (json::encode (meOf (me)));

        if (m == "PATCH")
        {
            MePatch p;
            if (! json::decode (body, p)) return fail (ErrorCode::invalidRequest);
            if (p.nickname && v::nickname (*p.nickname) != v::Problem::none) return fail (ErrorCode::invalidRequest, {}, "nickname");
            if (p.part && ! parts::isValid (*p.part)) return fail (ErrorCode::invalidRequest, {}, "part");
            if (p.friendRequests && *p.friendRequests != "everyone" && *p.friendRequests != "none")
                return fail (ErrorCode::invalidRequest, {}, "friend_requests");
            if (p.roomInvites && *p.roomInvites != "friends" && *p.roomInvites != "none")
                return fail (ErrorCode::invalidRequest, {}, "room_invites");
            const bool profile = p.nickname || p.part;
            if (p.nickname) me.nickname = v::normalise (*p.nickname);
            if (p.part) me.part = *p.part;
            if (p.friendRequests)
            {
                me.settings.friendRequests = *p.friendRequests;
                if (*p.friendRequests == "everyone")   // 숨은 요청 기록 삭제 (7.2)
                    friendRequests.erase (std::remove_if (friendRequests.begin(), friendRequests.end(),
                                                          [&] (const FriendReqRec& f) { return f.to == actor && f.hidden; }),
                                          friendRequests.end());
            }
            if (p.roomInvites) me.settings.roomInvites = *p.roomInvites;
            if (profile)
                if (auto* r = roomOf (actor)) roomChanged (*r);
            return ok (json::encode (meOf (me)));
        }

        if (m == "DELETE")
        {
            // 화면에서 비밀번호 칸을 두지 않는다는 결정(Q2)에 맞춰, 가짜 서버는 비밀번호가 비어 있으면 검사하지 않는다.
            // API 4.5 는 비밀번호를 요구한다 → 사용자 질문으로 올림
            const auto pw = body.getProperty ("password", "").toString();
            if (pw.isNotEmpty() && me.passwordHash != hashPassword (me.salt, pw)) return fail (ErrorCode::invalidCredentials);
            if (auto* r = roomOf (actor))
            {
                const auto rid = r->id;
                removeMember (*r, actor);
                if (r->members.empty()) deleteRoom (rid, false);
                else
                {
                    if (r->hostId == actor) r->hostId = r->members.front().userId;   // 가장 오래 있던 멤버
                    roomChanged (*r);
                }
            }
            for (auto& [rid, r] : rooms)
                if (r.hostId == actor && ! r.members.empty()) { r.hostId = r.members.front().userId; roomChanged (r); }
            for (auto& rt : me.refreshTokens) refreshOwners.erase (rt);
            me.refreshTokens.clear();
            for (auto it = accessTokens.begin(); it != accessTokens.end();)
                it = it->second.userId == actor ? accessTokens.erase (it) : std::next (it);
            me.deleted = true;
            me.email = "deleted:" + me.email;   // 30일 뒤 재가입 허용 규칙은 가짜에서는 바로 허용
            if (wsUser == actor) close();
            return noContent();
        }
    }

    if (m == "POST" && path == "me/password")
    {
        if (me.passwordHash != hashPassword (me.salt, body.getProperty ("current_password", "").toString()))
            return fail (ErrorCode::invalidCredentials);
        const auto next = body.getProperty ("new_password", "").toString();
        if (v::password (next) != v::Problem::none) return fail (ErrorCode::invalidRequest, {}, "new_password");
        me.passwordHash = hashPassword (me.salt, next);
        return noContent();
    }

    if (m == "POST" && path == "me/email")
    {
        const auto email = body.getProperty ("new_email", "").toString().trim();
        if (v::email (email) != v::Problem::none) return fail (ErrorCode::invalidRequest, {}, "new_email");
        if (auto* other = findByEmail (email); other != nullptr && other != &me) return fail (ErrorCode::emailTaken);
        if (now() - me.lastMailAt < resendGapMs) return fail (ErrorCode::rateLimited);
        if (! me.verified)
        {
            // 인증 전: 주소를 바로 바꾸고 가입 인증 코드를 새 주소로 (4.4)
            me.email = email;
            me.verifyCode = newMailCode();
            me.verifyCodeAt = me.lastMailAt = now();
            me.verifyFails = 0;
            return noContent();
        }
        // 비밀번호 없이 이메일 변경 방법은 사용자가 정해 주기로 함 (R7 전). 지금은 비어 있으면 검사하지 않는다
        const auto pw = body.getProperty ("password", "").toString();
        if (pw.isNotEmpty() && me.passwordHash != hashPassword (me.salt, pw)) return fail (ErrorCode::invalidCredentials);
        me.pendingEmail = email;
        me.emailCode = newMailCode();
        me.emailCodeAt = me.lastMailAt = now();
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
        if (auto* other = findByEmail (*me.pendingEmail); other != nullptr && other != &me) return fail (ErrorCode::emailTaken);
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
        p.canSendFriendRequest = u->settings.friendRequests != "none" || hasBlocked (u->id, actor);
        return ok (json::encode (p));
    }

    if (m == "POST" && path == "me/rtt")
    {
        const int ms = (int) body.getProperty ("aoo_rtt_ms", -1);
        if (ms < 0) return fail (ErrorCode::invalidRequest, {}, "aoo_rtt_ms");
        const bool changed = me.rttMs != OptInt (ms);
        me.rttMs = ms;
        if (changed)
            for (auto& [rid, r] : rooms)
                if (r.hostId == actor) emitRooms ("rooms.updated", r);
        return noContent();
    }

    if (m == "POST" && path == "telemetry/latency")
        return noContent();

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
            CreateRoom c;
            if (! json::decode (body, c)) return fail (ErrorCode::invalidRequest);
            if (c.type != "public" && c.type != "private") return fail (ErrorCode::invalidRequest, {}, "type");
            if (c.type == "public" && c.entryMode != String ("instant") && c.entryMode != String ("approval"))
                return fail (ErrorCode::invalidRequest, {}, "entry_mode");
            if (c.type == "private" && c.entryMode) return fail (ErrorCode::invalidRequest, {}, "entry_mode");
            if (v::roomTitle (c.title) != v::Problem::none) return fail (ErrorCode::invalidRequest, {}, "title");
            if (v::roomDescription (c.description) != v::Problem::none) return fail (ErrorCode::invalidRequest, {}, "description");
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

        if (n == 3 && seg[2] == "leave" && m == "POST")
        {
            if (! isMember (r->id, actor)) return fail (ErrorCode::notInRoom);
            const auto rid = r->id;
            removeMember (*r, actor);
            if (r->members.empty()) deleteRoom (rid, false);
            else roomChanged (*r);
            friendChanged (actor);
            return noContent();
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
                                    [&] (const JoinReqRec& j) { return j.id == seg[3] && j.roomId == r->id; });
            if (it == joinRequests.end()) return fail (ErrorCode::requestUnavailable);
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
            const auto newType = p.type.value_or (r->type);
            if (newType != "public" && newType != "private") return fail (ErrorCode::invalidRequest, {}, "type");
            if (p.title && v::roomTitle (*p.title) != v::Problem::none) return fail (ErrorCode::invalidRequest, {}, "title");
            if (p.description && v::roomDescription (*p.description) != v::Problem::none) return fail (ErrorCode::invalidRequest, {}, "description");
            if (p.capacity && (*p.capacity < 2 || *p.capacity > 7)) return fail (ErrorCode::invalidRequest, {}, "capacity");
            if (newType == "private" && p.setEntryMode && p.entryMode) return fail (ErrorCode::invalidRequest, {}, "entry_mode");
            if (newType == "public" && p.setEntryMode && p.entryMode != String ("instant") && p.entryMode != String ("approval"))
                return fail (ErrorCode::invalidRequest, {}, "entry_mode");
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

            roomChanged (*r, oldType == newType && newType == "public");
            return ok (json::encode (detailView (*r, actor)));
        }

        if (n == 3 && seg[2] == "transfer-host" && m == "POST")
        {
            if (! isHostHere) return fail (ErrorCode::notHost);
            const auto target = body.getProperty ("user_id", "").toString();
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
            removeMember (*r, target);
            r->kicked.insert (target);
            emit (target, "room.kicked", json::obj ({ { "room_id", r->id } }));
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
            juce::Array<var> arr;
            for (auto& f : friendRequests)
                if (f.to == actor && ! f.hidden && findUser (f.from) != nullptr)
                    arr.add (json::encode (FriendRequest { f.id, publicOf (f.from), json::isoTime (f.at) }));
            return ok (json::obj ({ { "requests", var (arr) } }));
        }

        if (n == 2 && seg[1] == "requests" && m == "POST")
        {
            const auto username = body.getProperty ("username", "").toString().trim().toLowerCase();
            auto result = [] (const char* r) { return ok (json::obj ({ { "result", r } })); };
            if (username == me.username) return result ("self");
            auto* target = findByUsername (username);
            if (target == nullptr || target->deleted) return result ("not_found");
            if (hasBlocked (actor, target->id)) return fail (ErrorCode::blockedByMe);
            if (areFriends (actor, target->id)) return result ("already_friends");

            for (auto it = friendRequests.begin(); it != friendRequests.end(); ++it)
            {
                if (it->from == target->id && it->to == actor && ! it->hidden)
                {
                    friendships.insert (pairKey (actor, target->id));
                    friendRequests.erase (it);
                    emit (actor, "friend.added", json::encode (friendView (target->id, actor)));
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
            if (it == friendRequests.end()) return fail (ErrorCode::notFound);
            const auto from = it->from;
            friendRequests.erase (it);
            if (seg[3] == "decline") return noContent();
            if (seg[3] != "accept") return fail (ErrorCode::notFound);
            friendships.insert (pairKey (actor, from));
            emit (actor, "friend.added", json::encode (friendView (from, actor)));
            emit (from, "friend.added", json::encode (friendView (actor, from)));
            return noContent();
        }

        if (n == 2 && m == "DELETE")
        {
            if (! areFriends (actor, seg[1])) return fail (ErrorCode::notFriend);
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
            for (auto& i : invites)
                if (i.from == actor && i.to == target && i.roomId == r->id) return fail (ErrorCode::alreadyInvited);

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
            if (it->delivered) return fail (ErrorCode::alreadyDelivered);
            invites.erase (it);
            return noContent();
        }

        if (n == 3 && m == "POST")
        {
            if (it == invites.end() || it->to != actor || ! it->delivered)
                return fail (ErrorCode::notFound);
            const auto inv = *it;
            if (seg[2] == "dismiss")
            {
                invites.erase (it);
                return noContent();
            }
            if (seg[2] != "accept") return fail (ErrorCode::notFound);
            auto* r = findRoom (inv.roomId);
            if (r == nullptr) { invites.erase (it); return fail (ErrorCode::roomClosed); }
            // 초대한 사람이 수락 시점에 방장이면 승인 없이 (7.9)
            return joinCommon (actor, *r, true, inv.from);
        }
        return fail (ErrorCode::notFound);
    }

    //------------------------------------------------------------------ 8 차단
    if (seg.size() >= 1 && seg[0] == "blocks")
    {
        if (n == 1 && m == "GET")
        {
            juce::Array<var> arr;
            if (auto b = blocks.find (actor); b != blocks.end())
                for (auto& [uid, at] : b->second)
                    if (findUser (uid) != nullptr)
                        arr.add (json::encode (Block { publicOf (uid), json::isoTime (at) }));
            return ok (json::obj ({ { "blocks", var (arr) } }));
        }
        if (n == 1 && m == "POST")
        {
            const auto target = body.getProperty ("user_id", "").toString();
            if (findUser (target) == nullptr || target == actor) return fail (ErrorCode::notFound);
            if (areFriends (actor, target)) return fail (ErrorCode::isFriend);
            friendRequests.erase (std::remove_if (friendRequests.begin(), friendRequests.end(), [&] (const FriendReqRec& f)
                                  { return (f.from == actor && f.to == target) || (f.from == target && f.to == actor); }),
                                  friendRequests.end());
            blocks[actor][target] = now();
            return noContent();
        }
        if (n == 2 && m == "DELETE")
        {
            blocks[actor].erase (seg[1]);
            friendRequests.erase (std::remove_if (friendRequests.begin(), friendRequests.end(), [&] (const FriendReqRec& f)
                                  { return f.from == seg[1] && f.to == actor && f.hidden; }),
                                  friendRequests.end());
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
        if (it->id != inviteId || it->delivered) continue;
        // 전달 시점에 오프라인이거나 방에 있으면 조용히 버린다 (7.7)
        if (! isOnline (it->to) || roomOf (it->to) != nullptr || findRoom (it->roomId) == nullptr)
        {
            invites.erase (it);   // 보낸 사람에게는 "초대됨" 그대로. 받은 사람에게는 없음
            return;
        }
        it->delivered = true;
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
    if (it == invites.end()) return;
    const auto inv = *it;
    invites.erase (it);
    auto* r = findRoom (inv.roomId);
    if (r == nullptr || roomOf (inv.to) != nullptr || ! hasRoomFor (*r, inv.to)) return;
    const bool direct = r->type == "private" || r->entryMode != String ("approval") || inv.from == r->hostId;
    if (! direct) return;
    addMember (*r, inv.to);
    roomChanged (*r);
    friendChanged (inv.to);
}

void FakeMeonServer::fakeUserAcceptFriend (const String& requestId)
{
    auto it = std::find_if (friendRequests.begin(), friendRequests.end(), [&] (const FriendReqRec& f) { return f.id == requestId; });
    if (it == friendRequests.end() || it->hidden) return;
    const auto from = it->from, to = it->to;
    friendRequests.erase (it);
    friendships.insert (pairKey (from, to));
    emit (from, "friend.added", json::encode (friendView (to, from)));
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
        if (refuse) { sink->transportClosed(); return; }
        wsOpen = true;
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
            if (sink != nullptr) sink->transportClosed();
        });
    };

    if (type == "auth")
    {
        auto it = accessTokens.find (msg.getProperty ("access_token", "").toString());
        if (it == accessTokens.end() || it->second.expiresAt <= now()) { rejectAndClose ("unauthorized"); return; }
        const int build = (int) msg.getProperty ("build", 0);
        if (build != 0 && build < config.minBuild) { rejectAndClose ("client_outdated"); return; }
        auto* u = findUser (it->second.userId);
        if (u == nullptr) { rejectAndClose ("unauthorized"); return; }
        if (! u->verified) { rejectAndClose ("email_not_verified"); return; }

        wsUser = u->id;
        subscribed = false;
        const bool wasOnline = u->online;
        u->online = true;
        attachWorld (*u);
        sendHello();
        if (! wasOnline) friendChanged (u->id);
        scheduleScript();
        return;
    }
    if (wsUser.isEmpty()) return;

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
    wsOpen = false;
    ++connectionGen;
    if (auto* u = findUser (wsUser))
    {
        u->online = false;
        u->lastSeenAt = now();
    }
    sink = nullptr;
    subscribed = false;
}

//==============================================================================
// 개발용

void FakeMeonServer::devDropLink (int seconds)
{
    dropUntil = now() + seconds * 1000LL;
    if (! wsOpen || sink == nullptr) return;
    auto* s = sink;
    wsOpen = false;
    ++connectionGen;
    s->transportClosed();
}

void FakeMeonServer::devKickMe()
{
    auto* r = roomOf (wsUser);
    if (r == nullptr || r->hostId == wsUser) return;
    removeMember (*r, wsUser);
    r->kicked.insert (wsUser);
    emit (wsUser, "room.kicked", json::obj ({ { "room_id", r->id } }));
    roomChanged (*r);
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

juce::var FakeMeonServer::saveState() const
{
    juce::Array<var> accounts;
    for (auto& [id, u] : users)
    {
        if (! u.local || u.deleted) continue;
        juce::Array<var> tokens;
        for (auto& t : u.refreshTokens) tokens.add (t);
        accounts.add (json::obj ({
            { "id", u.id }, { "username", u.username }, { "email", u.email }, { "salt", u.salt }, { "password_hash", u.passwordHash },
            { "nickname", u.nickname ? var (*u.nickname) : var() }, { "part", u.part ? var (*u.part) : var() },
            { "pending_email", u.pendingEmail ? var (*u.pendingEmail) : var() },
            { "email_verified", u.verified }, { "settings", json::encode (u.settings) },
            { "created_at", json::isoTime (u.createdAt) }, { "aoo_password", u.aooPassword }, { "refresh_tokens", var (tokens) },
            { "verify_code", u.verifyCode }, { "verify_code_at", u.verifyCodeAt }
        }));
    }
    return json::obj ({ { "version", 1 }, { "accounts", var (accounts) } });
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
            u.local = true;
            if (auto* toks = a.getProperty ("refresh_tokens", var()).getArray())
                for (auto& t : *toks)
                {
                    u.refreshTokens.add (t.toString());
                    refreshOwners[t.toString()] = u.id;
                }
            users[u.id] = u;
        }
    }
}

} // namespace meon::core
