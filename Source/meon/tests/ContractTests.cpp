// R11 계약 테스트: 진짜 서버(로컬 docker compose)의 응답을 앱의 디코더로 읽는다.
//   MEON_TEST_SERVER=http://localhost:8080 MEON_TEST_MAILPIT=http://localhost:8025 MEON_Tests Contract
// 서버 저장소(meonhq/server)의 compose.yaml 로 띄운다 (ALLOW_DEV_BUILD=true 라 빌드 0 으로 붙는다).
// 둘 중 하나라도 없으면 건너뛴다 (CI 는 서버를 띄우지 않는다).
// 한 번에 2명이 가입한다 (같은 이메일 가입 시도 1번 더). 서버의 가입 제한(IP 당 1시간 5회)에 걸리면 `docker compose restart api` 로 푼다.
#include <JuceHeader.h>
#include "../core/api/MeonApi.h"
#include "../core/api/Realtime.h"
#include "../core/api/net/HttpTransport.h"
#include "../core/api/net/WsTransport.h"
#include "../core/util/Scheduler.h"

using namespace meon::core;

namespace
{

juce::String env (const char* name) { return juce::SystemStats::getEnvironmentVariable (name, {}); }

struct EventLog : Realtime::Listener
{
    std::vector<Hello> hellos;
    std::vector<RealtimeEvent> events;
    juce::StringArray rejected;
    int revoked = 0;
    std::vector<Realtime::Link> links;
    void hello (const Hello& h) override                    { hellos.push_back (h); }
    void sessionRevoked() override                          { ++revoked; }
    void linkChanged (Realtime::Link l) override            { links.push_back (l); }
    void event (const RealtimeEvent& e) override            { events.push_back (e); }
    void authRejected (const juce::String& c) override      { rejected.add (c); }

    const RealtimeEvent* find (RealtimeEvent::Kind k) const
    {
        for (auto& e : events) if (e.kind == k) return &e;
        return nullptr;
    }
};

} // namespace

class ContractTests : public juce::UnitTest
{
public:
    ContractTests() : juce::UnitTest ("Contract (local server)", "Contract") {}

    CallbackQueue queue;
    juce::String server, mailpit;

    struct Client
    {
        Client (ContractTests& t, const juce::String& base)
            : rest (base + "/v1", t.queue.deliver()), api (rest, 0),
              ws (juce::String (base.startsWith ("https") ? "wss" : "ws") + base.fromFirstOccurrenceOf ("://", true, false) + "/v1/ws", t.queue.deliver()),
              rt (ws, sched, 0)
        {
            rt.addListener (&log);
        }
        ~Client() { rt.removeListener (&log); rt.disconnect(); }

        HttpTransport rest;
        MeonApi api;
        WsTransport ws;
        ManualScheduler sched;
        Realtime rt;
        EventLog log;
        Me me;
        juce::String email, username;
    };

    template <typename T>
    std::optional<meon::core::Result<T>> wait (std::function<void (Callback<T>)> start, int timeoutMs = 15000)
    {
        std::optional<meon::core::Result<T>> out;
        start ([&out] (meon::core::Result<T> r) { out.emplace (std::move (r)); });
        queue.runUntil ([&] { return out.has_value(); }, timeoutMs);
        return out;
    }

    template <typename T>
    T must (const std::optional<meon::core::Result<T>>& r, const juce::String& what)
    {
        if (r.has_value() && r->ok()) return r->get();
        expect (false, what + ": " + (r.has_value() ? errorKey (r->err().code) + " (HTTP " + juce::String (r->err().httpStatus) + ") " + r->err().message
                                                    : juce::String ("응답 없음")));
        return T {};
    }

    template <typename T>
    void mustFail (const std::optional<meon::core::Result<T>>& r, ErrorCode code, const juce::String& what)
    {
        expect (r.has_value() && ! r->ok(), what + ": 실패해야 함");
        if (r.has_value() && ! r->ok())
            expectEquals (errorKey (r->err().code), errorKey (code), what);
    }

    /** Mailpit 에서 그 주소로 온 마지막 인증 코드 (제목 끝 6자리) */
    juce::String mailedCode (const juce::String& email)
    {
        for (int attempt = 0; attempt < 30; ++attempt)
        {
            const auto res = HttpTransport::perform (mailpit, { "GET", "/api/v1/search?query=" + juce::URL::addEscapeChars ("to:" + email, true), {}, {}, 0 }, 5000);
            if (auto* list = res.body["messages"].getArray(); list != nullptr && ! list->isEmpty())
            {
                const auto subject = (*list)[0]["Subject"].toString();
                const auto code = subject.fromLastOccurrenceOf (" ", false, false).trim();
                if (code.length() == 6 && code.containsOnly ("0123456789")) return code;
            }
            juce::Thread::sleep (200);
        }
        expect (false, "인증 메일이 오지 않음: " + email);
        return {};
    }

    std::unique_ptr<Client> signedUp (const juce::String& tag, const juce::String& nickname, const juce::String& part)
    {
        auto c = std::make_unique<Client> (*this, server);
        const auto suffix = juce::String::toHexString (juce::Random::getSystemRandom().nextInt (0x7fffffff)).substring (0, 6);
        c->username = (tag + "_" + suffix).toLowerCase().substring (0, 16);
        c->email = c->username + "@example.com";

        auto ok = wait<bool> ([&] (Callback<bool> cb) { c->api.usernameAvailable (c->username, cb); });
        expect (must (ok, "username-available"), "새 아이디는 쓸 수 있어야 함");

        auto auth = must (wait<AuthResult> ([&] (Callback<AuthResult> cb) { c->api.signup (c->email, c->username, "Check-1234", true, cb); }), "signup");
        expect (! auth.me.emailVerified);
        expect (auth.tokens.accessToken.isNotEmpty() && auth.tokens.refreshToken.isNotEmpty());
        expectEquals (auth.me.username, c->username);

        auto me = must (wait<Me> ([&] (Callback<Me> cb) { c->api.verifyEmail (mailedCode (c->email), cb); }), "verify-email");
        expect (me.emailVerified);

        MePatch p;
        p.nickname = nickname;
        p.part = part;
        c->me = must (wait<Me> ([&] (Callback<Me> cb) { c->api.patchMe (p, cb); }), "PATCH /me");
        expect (c->me.nickname == nickname && c->me.part == part);
        return c;
    }

    void connect (Client& c)
    {
        c.rt.connect ([&c] { return c.api.getTokens().accessToken; });
        expect (queue.runUntil ([&] { return ! c.log.hellos.empty() || ! c.log.rejected.isEmpty(); }, 10000), "hello 가 오지 않음");
        expect (c.log.rejected.isEmpty(), "WS 인증 거부: " + c.log.rejected.joinIntoString (","));
    }

    bool waitEvent (Client& c, RealtimeEvent::Kind k, int timeoutMs = 10000)
    {
        return queue.runUntil ([&] { return c.log.find (k) != nullptr; }, timeoutMs);
    }

    /** API 5장·6장·10.6 방 계약. a = 방장, b·c = 멤버 (셋 다 로그인·WS 연결된 상태로 부른다. 끝나면 아무도 방에 없다) */
    void roomContract (Client& a, Client& b, Client& c)
    {
        using K = RealtimeEvent::Kind;
        auto clearLogs = [&] { for (auto* x : { &a, &b, &c }) x->log.events.clear(); };

        beginTest ("rooms v0.5: public instant join, rooms.* events, room_full, already_in_room, idempotent join");
        clearLogs();
        b.rt.subscribePublicRooms (true);
        queue.runUntil ([] { return false; }, 300);
        CreateRoom pub;
        pub.type = "public";
        pub.entryMode = juce::String ("instant");
        pub.title = "contract public";
        pub.capacity = 2;
        auto r1 = must (wait<RoomDetail> ([&] (Callback<RoomDetail> cb) { a.api.createRoom (pub, cb); }), "POST /rooms (public)");
        expect (! r1.inviteCode.has_value() || r1.inviteCode->isEmpty(), "공개 방은 초대 코드 없음");
        expect (waitEvent (b, K::roomsAdded), "rooms.added (구독 중)");
        auto joined = must (wait<JoinResult> ([&] (Callback<JoinResult> cb) { b.api.joinRoom (r1.id, cb); }), "POST /rooms/{id}/join");
        expect (joined.room.has_value() && joined.room->connection.aooUsername == b.me.id);
        expect (waitEvent (a, K::roomUpdated), "room.updated");
        if (auto* e = a.log.find (K::roomUpdated))
            expect (e->roomDetail && e->roomDetail->connection.members.contains (b.me.id), "connection.members 갱신");
        expect (waitEvent (b, K::roomsUpdated), "rooms.updated (인원 변경)");
        b.rt.subscribePublicRooms (false);
        mustFail (wait<JoinResult> ([&] (Callback<JoinResult> cb) { c.api.joinRoom (r1.id, cb); }), ErrorCode::roomFull, "정원 초과");
        auto again = must (wait<JoinResult> ([&] (Callback<JoinResult> cb) { b.api.joinRoom (r1.id, cb); }), "같은 방에 다시 입장 (멱등)");
        expect (again.room.has_value() && again.room->id == r1.id);
        mustFail (wait<RoomDetail> ([&] (Callback<RoomDetail> cb) { a.api.createRoom (pub, cb); }), ErrorCode::alreadyInRoom, "방에 있는 채로 방 만들기");
        mustFail (wait<JoinResult> ([&] (Callback<JoinResult> cb) { c.api.joinByCode ("ZZZZZZ", cb); }), ErrorCode::invalidCode, "없는 코드");

        beginTest ("rooms v0.5: reconnect → hello.current_room");
        b.rt.disconnect();
        b.log.hellos.clear();
        connect (b);
        if (! b.log.hellos.empty())
            expect (b.log.hellos.back().currentRoom && b.log.hellos.back().currentRoom->id == r1.id
                        && b.log.hellos.back().currentRoom->connection.groupName == r1.connection.groupName,
                    "다시 연결하면 hello 의 current_room 이 지금 방 (같은 Connection)");

        beginTest ("rooms v0.5: host absent seat, capacity_below_members, target_not_in_room, kicked, room.closed");
        RoomPatch cap3;
        cap3.capacity = 3;
        must (wait<RoomDetail> ([&] (Callback<RoomDetail> cb) { a.api.patchRoom (r1.id, cap3, cb); }), "정원 3");
        must (wait<Done> ([&] (Callback<Done> cb) { a.api.leaveRoom (r1.id, cb); }), "방장이 나감 (host_present false)");
        auto absent = must (wait<RoomDetail> ([&] (Callback<RoomDetail> cb) { b.api.getRoom (r1.id, cb); }), "GET (방장 없음)");
        expect (! absent.hostPresent && absent.hostId == a.me.id, "방장 권한은 남고 자리만 예약");
        must (wait<JoinResult> ([&] (Callback<JoinResult> cb) { c.api.joinRoom (r1.id, cb); }), "방장 자리를 비워 두고도 남는 자리로 입장");
        must (wait<JoinResult> ([&] (Callback<JoinResult> cb) { a.api.joinRoom (r1.id, cb); }), "방장은 예약된 자리로 돌아옴");
        RoomPatch cap2;
        cap2.capacity = 2;
        mustFail (wait<RoomDetail> ([&] (Callback<RoomDetail> cb) { a.api.patchRoom (r1.id, cap2, cb); }), ErrorCode::capacityBelowMembers, "인원보다 적은 정원");
        mustFail (wait<Done> ([&] (Callback<Done> cb) { a.api.kick (r1.id, "u_not_here", cb); }), ErrorCode::targetNotInRoom, "방에 없는 사람 내보내기");
        mustFail (wait<RoomDetail> ([&] (Callback<RoomDetail> cb) { a.api.transferHost (r1.id, "u_not_here", cb); }), ErrorCode::targetNotInRoom, "방에 없는 사람에게 방장");
        clearLogs();
        must (wait<Done> ([&] (Callback<Done> cb) { a.api.kick (r1.id, c.me.id, cb); }), "kick");
        expect (waitEvent (c, K::roomKicked), "room.kicked");
        mustFail (wait<JoinResult> ([&] (Callback<JoinResult> cb) { c.api.joinRoom (r1.id, cb); }), ErrorCode::kicked, "추방된 방 다시 입장");
        clearLogs();
        must (wait<Done> ([&] (Callback<Done> cb) { a.api.closeRoom (r1.id, cb); }), "close");
        expect (waitEvent (b, K::roomClosed), "room.closed (멤버)");
        if (auto* e = b.log.find (K::roomClosed)) expectEquals (e->roomId, r1.id);
        mustFail (wait<JoinResult> ([&] (Callback<JoinResult> cb) { b.api.joinRoom (r1.id, cb); }), ErrorCode::roomClosed, "닫힌 방 입장 410");

        beginTest ("rooms v0.5: approval → 202 request_until, join_request.received/accepted, removed, host_absent");
        CreateRoom appr;
        appr.type = "public";
        appr.entryMode = juce::String ("approval");
        appr.title = "contract approval";
        appr.capacity = 4;
        auto r2 = must (wait<RoomDetail> ([&] (Callback<RoomDetail> cb) { a.api.createRoom (appr, cb); }), "POST /rooms (approval)");
        clearLogs();
        auto req = must (wait<JoinResult> ([&] (Callback<JoinResult> cb) { b.api.joinRoom (r2.id, cb); }), "승인 요청");
        expect (req.requested() && req.requestUntil && req.requestUntil->isNotEmpty(), "202 + request_until");
        expect (waitEvent (a, K::joinRequestReceived), "join_request.received");
        auto req2 = must (wait<JoinResult> ([&] (Callback<JoinResult> cb) { b.api.joinRoom (r2.id, cb); }), "같은 방 다시 요청");
        expect (req2.requested() && req2.requestUntil == req.requestUntil, "같은 request_until (새로 만들지 않음)");
        auto list = must (wait<std::vector<JoinRequest>> ([&] (Callback<std::vector<JoinRequest>> cb) { a.api.listJoinRequests (r2.id, cb); }), "GET join-requests");
        expectEquals ((int) list.size(), 1);
        if (! list.empty())
        {
            auto acc = must (wait<RoomDetail> ([&] (Callback<RoomDetail> cb) { a.api.acceptJoinRequest (r2.id, list[0].id, cb); }), "accept");
            expect (acc.connection.members.contains (b.me.id));
            expect (waitEvent (b, K::joinRequestAccepted), "join_request.accepted (신청자 → 바로 합주 화면)");
            if (auto* e = b.log.find (K::joinRequestAccepted))
                expect (e->roomDetail && e->roomDetail->id == r2.id && e->roomDetail->connection.aooUsername == b.me.id);
        }
        // 대기 중인 요청은 방장이 입장 방식을 바로 입장으로 바꾸면 지워지고, 방장에게만 join_request.removed (API 5.7)
        // (같은 방에 5분 안에 다시 요청하면 새로 만들지 않으므로 사람마다 방마다 한 번씩만 요청한다)
        clearLogs();
        auto creq = must (wait<JoinResult> ([&] (Callback<JoinResult> cb) { c.api.joinRoom (r2.id, cb); }), "c 승인 요청");
        expect (creq.requested());
        expect (waitEvent (a, K::joinRequestReceived), "join_request.received (c)");
        RoomPatch instant;
        instant.setEntryMode = true;
        instant.entryMode = juce::String ("instant");
        must (wait<RoomDetail> ([&] (Callback<RoomDetail> cb) { a.api.patchRoom (r2.id, instant, cb); }), "승인 → 바로 입장");
        expect (waitEvent (a, K::joinRequestRemoved), "join_request.removed");
        must (wait<Done> ([&] (Callback<Done> cb) { a.api.leaveRoom (r2.id, cb); }), "a 나감");
        must (wait<Done> ([&] (Callback<Done> cb) { b.api.leaveRoom (r2.id, cb); }), "b 나감 (마지막 사람 → 방 삭제)");
        mustFail (wait<RoomDetail> ([&] (Callback<RoomDetail> cb) { a.api.getRoom (r2.id, cb); }), ErrorCode::roomClosed, "마지막 사람이 나간 방은 410");

        // 방장이 없는 승인 방은 요청을 받지 않는다 (host_absent). 방장이 돌아오면 받고, 오프라인 신청자 수락은 request_unavailable
        auto r4 = must (wait<RoomDetail> ([&] (Callback<RoomDetail> cb) { b.api.createRoom (appr, cb); }), "b 승인 방");
        clearLogs();
        must (wait<JoinResult> ([&] (Callback<JoinResult> cb) { a.api.joinRoom (r4.id, cb); }), "a 승인 요청");
        expect (waitEvent (b, K::joinRequestReceived), "join_request.received (a)");
        auto la = must (wait<std::vector<JoinRequest>> ([&] (Callback<std::vector<JoinRequest>> cb) { b.api.listJoinRequests (r4.id, cb); }), "GET join-requests (a)");
        if (! la.empty())
            must (wait<RoomDetail> ([&] (Callback<RoomDetail> cb) { b.api.acceptJoinRequest (r4.id, la[0].id, cb); }), "a 수락");
        must (wait<Done> ([&] (Callback<Done> cb) { b.api.leaveRoom (r4.id, cb); }), "방장 b 가 나감");
        mustFail (wait<JoinResult> ([&] (Callback<JoinResult> cb) { c.api.joinRoom (r4.id, cb); }), ErrorCode::hostAbsent, "방장 없는 승인 방에 요청");
        must (wait<JoinResult> ([&] (Callback<JoinResult> cb) { b.api.joinRoom (r4.id, cb); }), "방장 복귀");
        clearLogs();
        auto creq2 = must (wait<JoinResult> ([&] (Callback<JoinResult> cb) { c.api.joinRoom (r4.id, cb); }), "c 승인 요청 (방장 복귀 뒤)");
        expect (creq2.requested());
        expect (waitEvent (b, K::joinRequestReceived), "join_request.received (c)");
        auto lc = must (wait<std::vector<JoinRequest>> ([&] (Callback<std::vector<JoinRequest>> cb) { b.api.listJoinRequests (r4.id, cb); }), "GET join-requests (c)");
        c.rt.disconnect();
        queue.runUntil ([] { return false; }, 500);
        if (! lc.empty())
        {
            auto unavailable = wait<RoomDetail> ([&] (Callback<RoomDetail> cb) { b.api.acceptJoinRequest (r4.id, lc[0].id, cb); });
            mustFail (unavailable, ErrorCode::requestUnavailable, "오프라인 신청자 수락");
        }
        connect (c);
        must (wait<Done> ([&] (Callback<Done> cb) { a.api.leaveRoom (r4.id, cb); }), "a 나감");
        must (wait<Done> ([&] (Callback<Done> cb) { b.api.leaveRoom (r4.id, cb); }), "b 나감");
    }

    void runTest() override
    {
        server = env ("MEON_TEST_SERVER");
        mailpit = env ("MEON_TEST_MAILPIT");
        beginTest ("config and account onboarding");
        if (server.isEmpty() || mailpit.isEmpty())
        {
            logMessage ("MEON_TEST_SERVER / MEON_TEST_MAILPIT 이 없어 건너뜀");
            return;
        }

        {
            Client c (*this, server);
            auto cfg = must (wait<Config> ([&] (Callback<Config> cb) { c.api.getConfig (cb); }), "GET /config");
            expect (cfg.discordUrl.startsWith ("https://"));
        }

        auto a = signedUp ("ct_a", juce::String (juce::CharPointer_UTF8 ("\xea\xb8\xb0\xed\x83\x80\xec\xb9\x98")), "guitar");
        auto b = signedUp ("ct_b", "Bee", "drums");

        must (wait<Me> ([&] (Callback<Me> cb) { a->api.getMe (cb); }), "GET /me");

        beginTest ("rooms: create, list, get, hello");
        CreateRoom cr;
        cr.type = "private";
        cr.title = juce::String (juce::CharPointer_UTF8 ("\xea\xb3\x84\xec\x95\xbd \xed\x85\x8c\xec\x8a\xa4\xed\x8a\xb8"));
        cr.description = "line1\nline2";
        cr.capacity = 4;
        auto room = must (wait<RoomDetail> ([&] (Callback<RoomDetail> cb) { a->api.createRoom (cr, cb); }), "POST /rooms");
        expect (room.inviteCode.has_value() && room.inviteCode->length() == 6);
        expect (room.connection.aooHost.isNotEmpty() && room.connection.aooPort > 0);
        expect (room.connection.groupName.isNotEmpty() && room.connection.groupPassword.isNotEmpty());
        expectEquals (room.connection.aooUsername, a->me.id, "AOO 이름은 계정 ID");
        expect (room.connection.aooUserPassword.isNotEmpty());
        expect (room.connection.members.contains (a->me.id));
        expectEquals ((int) room.members.size(), 1);

        must (wait<std::vector<Room>> ([&] (Callback<std::vector<Room>> cb) { a->api.listPublicRooms (cb); }), "GET /rooms");
        auto got = must (wait<RoomDetail> ([&] (Callback<RoomDetail> cb) { a->api.getRoom (room.id, cb); }), "GET /rooms/{id}");
        expectEquals (got.id, room.id);

        connect (*a);
        if (! a->log.hellos.empty())
        {
            const auto& h = a->log.hellos.front();
            expect (h.currentRoom.has_value() && h.currentRoom->id == room.id, "hello.current_room");
            expect (h.config.discordUrl.isNotEmpty());
        }

        beginTest ("join by code → room.updated with members");
        auto join = must (wait<JoinResult> ([&] (Callback<JoinResult> cb) { b->api.joinByCode (*room.inviteCode, cb); }), "join-by-code");
        expect (! join.requested() && join.room.has_value());
        if (join.room)
        {
            expectEquals (join.room->connection.aooUsername, b->me.id);
            expect (join.room->connection.members.contains (a->me.id) && join.room->connection.members.contains (b->me.id));
        }
        connect (*b);
        expect (waitEvent (*a, RealtimeEvent::Kind::roomUpdated), "room.updated 가 오지 않음 (디코드 실패일 수 있음)");
        if (auto* e = a->log.find (RealtimeEvent::Kind::roomUpdated))
            expect (e->roomDetail.has_value() && e->roomDetail->connection.members.contains (b->me.id));

        beginTest ("friends: request, accept, lists, events");
        auto sent = must (wait<juce::String> ([&] (Callback<juce::String> cb) { b->api.sendFriendRequest (a->username, cb); }), "POST /friends/requests");
        expectEquals (sent, juce::String ("sent"));
        expect (waitEvent (*a, RealtimeEvent::Kind::friendRequestReceived), "friend_request.received");
        auto reqs = must (wait<std::vector<FriendRequest>> ([&] (Callback<std::vector<FriendRequest>> cb) { a->api.listFriendRequests (cb); }), "GET /friends/requests");
        expectEquals ((int) reqs.size(), 1);
        if (! reqs.empty())
            must (wait<Done> ([&] (Callback<Done> cb) { a->api.acceptFriendRequest (reqs[0].id, cb); }), "accept friend");
        expect (waitEvent (*b, RealtimeEvent::Kind::friendAdded), "friend.added");
        auto friends = must (wait<std::vector<Friend>> ([&] (Callback<std::vector<Friend>> cb) { b->api.listFriends (cb); }), "GET /friends");
        expectEquals ((int) friends.size(), 1);
        auto profile = must (wait<UserProfile> ([&] (Callback<UserProfile> cb) { b->api.getUser (a->me.id, cb); }), "GET /users/{id}");
        expectEquals (profile.user.id, a->me.id);

        beginTest ("invite after leaving: 3-second delivery");
        must (wait<Done> ([&] (Callback<Done> cb) { b->api.leaveRoom (room.id, cb); }), "leave");
        auto inv = must (wait<InviteSent> ([&] (Callback<InviteSent> cb) { a->api.sendInvite (b->me.id, cb); }), "POST /invites");
        expect (inv.id.startsWith ("inv_") && inv.deliverAt.isNotEmpty());
        expect (waitEvent (*b, RealtimeEvent::Kind::inviteReceived, 8000), "invite.received (3초 뒤)");
        if (auto* e = b->log.find (RealtimeEvent::Kind::inviteReceived))
            expect (e->invite.has_value() && e->invite->id == inv.id);
        auto accepted = must (wait<JoinResult> ([&] (Callback<JoinResult> cb) { b->api.acceptInvite (inv.id, cb); }), "accept invite");
        expect (accepted.room.has_value());

        beginTest ("host actions and errors");
        auto again = wait<InviteSent> ([&] (Callback<InviteSent> cb) { a->api.sendInvite (b->me.id, cb); });
        mustFail (again, ErrorCode::targetInRoom, "방에 있는 친구 초대");
        must (wait<RoomDetail> ([&] (Callback<RoomDetail> cb) { a->api.transferHost (room.id, b->me.id, cb); }), "transfer-host");
        RoomPatch rp;
        rp.title = juce::String ("renamed");
        auto notHost = wait<RoomDetail> ([&] (Callback<RoomDetail> cb) { a->api.patchRoom (room.id, rp, cb); });
        mustFail (notHost, ErrorCode::notHost, "방장이 아닌 사람의 방 설정");
        must (wait<RoomDetail> ([&] (Callback<RoomDetail> cb) { b->api.patchRoom (room.id, rp, cb); }), "PATCH /rooms/{id}");
        must (wait<std::vector<JoinRequest>> ([&] (Callback<std::vector<JoinRequest>> cb) { b->api.listJoinRequests (room.id, cb); }), "GET join-requests");
        must (wait<Done> ([&] (Callback<Done> cb) { b->api.kick (room.id, a->me.id, cb); }), "kick");
        expect (waitEvent (*a, RealtimeEvent::Kind::roomKicked), "room.kicked");
        must (wait<Done> ([&] (Callback<Done> cb) { b->api.closeRoom (room.id, cb); }), "close");
        auto closed = wait<RoomDetail> ([&] (Callback<RoomDetail> cb) { a->api.getRoom (room.id, cb); });
        mustFail (closed, ErrorCode::roomClosed, "닫힌 방 조회 410");
        must (wait<Done> ([&] (Callback<Done> cb) { a->api.leaveRoom (room.id, cb); }), "멤버가 아닌 방 leave 204");

        //==========================================================================
        // R11 PR3: 방 API v0.5 (공개 방 바로 입장·승인 요청, 오류 코드, 방 이벤트, 재연결 hello)
        auto c = signedUp ("ct_c", "Cee", "bass");
        connect (*c);
        roomContract (*a, *b, *c);

        beginTest ("blocks, notifications, rtt, tokens");
        must (wait<std::vector<Block>> ([&] (Callback<std::vector<Block>> cb) { a->api.listBlocks (cb); }), "GET /blocks");
        must (wait<bool> ([&] (Callback<bool> cb) { a->api.notificationState (cb); }), "GET /notifications/state");
        must (wait<Done> ([&] (Callback<Done> cb) { a->api.markNotificationsRead (cb); }), "POST /notifications/read");
        must (wait<Done> ([&] (Callback<Done> cb) { a->api.reportRtt (42, cb); }), "POST /me/rtt");
        const auto before = a->api.getTokens();
        auto rotated = must (wait<Tokens> ([&] (Callback<Tokens> cb) { a->api.refresh (cb, "contract"); }), "POST /auth/refresh");
        expect (rotated.refreshToken.isNotEmpty() && rotated.refreshToken != before.refreshToken);
        auto dup = wait<AuthResult> ([&] (Callback<AuthResult> cb) { b->api.signup (a->email, "other_" + a->username.substring (0, 8), "Check-1234", true, cb); });
        expect (dup.has_value() && ! dup->ok() && (dup->code() == ErrorCode::emailTaken || dup->code() == ErrorCode::rateLimited),
                "같은 이메일 가입은 email_taken (가입 제한에 걸렸으면 rate_limited)");
        must (wait<Done> ([&] (Callback<Done> cb) { a->api.logout (cb); }), "POST /auth/logout");

        //==========================================================================
        beginTest ("close codes: second login takes over (4000), logout closes (4001)");
        {
            if (c->rt.getLink() != Realtime::Link::online) connect (*c);
            Client again (*this, server);
            auto auth = must (wait<AuthResult> ([&] (Callback<AuthResult> cb) { again.api.login (c->email, "Check-1234", cb); }), "login (다른 곳)");
            juce::ignoreUnused (auth);
            connect (again);
            expect (queue.runUntil ([&] { return c->rt.getLink() == Realtime::Link::replaced; }, 10000), "먼저 연결한 쪽은 4000 으로 끊기고 다시 붙지 않아야 함");
            queue.runUntil ([] { return false; }, 1500);
            expect (c->rt.getLink() == Realtime::Link::replaced, "저절로 다시 붙지 않음");
            expect (again.rt.getLink() == Realtime::Link::online, "나중에 연결한 쪽이 남음");

            must (wait<Done> ([&] (Callback<Done> cb) { again.api.logout (cb); }), "logout");
            expect (queue.runUntil ([&] { return again.log.revoked == 1; }, 10000), "로그아웃한 세션의 연결은 4001 로 닫힘");
            expect (again.rt.getLink() == Realtime::Link::offline);
        }

        beginTest ("email change needs the current password; delete account without a body");
        {
            auto d = signedUp ("ct_d", "Dee", "vocal");
            const auto newEmail = "new_" + d->email;
            auto wrong = wait<Done> ([&] (Callback<Done> cb) { d->api.requestEmailChange (newEmail, "Wrong-1234", cb); });
            mustFail (wrong, ErrorCode::invalidCredentials, "틀린 비밀번호로 이메일 변경");
            juce::Thread::sleep (100);
            auto sent = wait<Done> ([&] (Callback<Done> cb) { d->api.requestEmailChange (newEmail, "Check-1234", cb); });
            if (sent.has_value() && ! sent->ok() && sent->code() == ErrorCode::rateLimited)
                logMessage ("메일 재발송 간격(30초)에 걸려 이메일 변경 확인은 건너뜀");
            else
            {
                must (sent, "POST /me/email");
                auto me = must (wait<Me> ([&] (Callback<Me> cb) { d->api.verifyEmailChange (mailedCode (newEmail), cb); }), "POST /me/email/verify");
                expectEquals (me.email, newEmail);
            }
            must (wait<Done> ([&] (Callback<Done> cb) { d->api.deleteAccount (cb); }), "DELETE /me (본문 없음)");
            expect (! d->api.hasSession());
        }
    }
};

static ContractTests contractTests;
