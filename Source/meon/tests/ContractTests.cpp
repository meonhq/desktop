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
            auto c = signedUp ("ct_c", "Cee", "bass");
            connect (*c);
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
