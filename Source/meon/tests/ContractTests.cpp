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

    /** c 의 이벤트 중 from 번째 뒤에 온 kind (pred 를 만족하는 것) */
    const RealtimeEvent* newEvent (Client& c, RealtimeEvent::Kind k, size_t from, std::function<bool (const RealtimeEvent&)> pred = {})
    {
        for (size_t i = from; i < c.log.events.size(); ++i)
            if (c.log.events[i].kind == k && (! pred || pred (c.log.events[i]))) return &c.log.events[i];
        return nullptr;
    }

    bool waitNew (Client& c, RealtimeEvent::Kind k, size_t from, int timeoutMs = 10000, std::function<bool (const RealtimeEvent&)> pred = {})
    {
        return queue.runUntil ([&] { return newEvent (c, k, from, pred) != nullptr; }, timeoutMs);
    }

    void ensureOnline (Client& c)
    {
        if (c.rt.getLink() != Realtime::Link::online) connect (c);
    }

    std::optional<Friend> friendIn (const std::vector<Friend>& list, const juce::String& userId)
    {
        for (auto& f : list) if (f.user.id == userId) return f;
        return std::nullopt;
    }

    std::vector<Friend> friendsOf (Client& c)
    {
        return must (wait<std::vector<Friend>> ([&] (Callback<std::vector<Friend>> cb) { c.api.listFriends (cb); }), "GET /friends");
    }

    juce::String requestFriend (Client& from, const Client& to)
    {
        return must (wait<juce::String> ([&] (Callback<juce::String> cb) { from.api.sendFriendRequest (to.username, cb); }), "POST /friends/requests");
    }

    RoomDetail makeRoom (Client& host, const juce::String& type, int capacity = 7)
    {
        CreateRoom cr;
        cr.type = type;
        if (type == "public") cr.entryMode = juce::String ("instant");
        cr.title = "pr4 " + type;
        cr.capacity = capacity;
        return must (wait<RoomDetail> ([&] (Callback<RoomDetail> cb) { host.api.createRoom (cr, cb); }), "POST /rooms (" + type + ")");
    }

    /** API 2.10·7장·8장·9장·5.10 계약 (R11 PR4). a·b 는 친구, c 는 남. 셋 다 방 밖에서 시작해 방 밖에서 끝난다.
        서버 제한 (11장): 방 만들기는 사용자당 10분 5회(앞의 테스트에서 a 3번, b 1번), 초대는 사용자당 1분 10회 → 나눠서 만든다 */
    void socialContract (Client& a, Client& b, Client& c)
    {
        ensureOnline (a); ensureOnline (b); ensureOnline (c);
        using K = RealtimeEvent::Kind;
        auto isUpdate = [] (const juce::String& id, const char* status)
        {
            return [id, status] (const RealtimeEvent& e) { return e.friendInfo && e.friendInfo->user.id == id && e.friendInfo->status == juce::String (status); };
        };

        //==========================================================================
        beginTest ("friend list: order, in_room with room / private hides it, joinable, can_invite, offline last_seen (2.10, 7.1)");
        RoomDetail broom;   // b 의 비공개 방 (정원 2). 초대 오류·초대 흐름에 쓴다
        {
            // c ↔ b 친구 (c 가 요청, b 가 받은 요청 목록에서 수락)
            auto mark = b.log.events.size();
            expectEquals (requestFriend (c, b), juce::String ("sent"));
            expect (waitNew (b, K::friendRequestReceived, mark), "friend_request.received");
            auto reqs = must (wait<std::vector<FriendRequest>> ([&] (Callback<std::vector<FriendRequest>> cb) { b.api.listFriendRequests (cb); }), "GET /friends/requests");
            juce::String reqId;
            for (auto& r : reqs) if (r.from.id == c.me.id) reqId = r.id;
            expect (reqId.isNotEmpty(), "받은 요청 목록에 c");
            const auto markC = c.log.events.size();
            must (wait<Done> ([&] (Callback<Done> cb) { b.api.acceptFriendRequest (reqId, cb); }), "accept");
            expect (waitNew (c, K::friendAdded, markC), "friend.added (요청한 쪽)");

            // a 가 공개 방에 들어가면 b 에게 friend.updated: in_room + room { id, title, joinable }
            mark = b.log.events.size();
            auto pub = makeRoom (a, "public");
            expect (waitNew (b, K::friendUpdated, mark, 10000, isUpdate (a.me.id, "in_room")), "friend.updated (방 입장)");
            auto list = friendsOf (b);
            if (auto f = friendIn (list, a.me.id))
            {
                expectEquals (f->status, juce::String ("in_room"));
                expect (f->room.has_value() && f->room->id == pub.id && f->room->joinable, "공개 방이면 room { id, joinable: true }");
                expect (! f->canInvite, "방에 있는 친구는 초대할 수 없다");
            }
            if (auto f = friendIn (list, c.me.id))
            {
                expectEquals (f->status, juce::String ("online"));
                expect (! f->room.has_value());
                expect (! f->canInvite, "나(b)는 방에 없으므로 can_invite 는 false");
            }
            expect (list.size() >= 2 && list[0].user.id == a.me.id, "in_room 이 online 보다 앞");
            must (wait<Done> ([&] (Callback<Done> cb) { a.api.leaveRoom (pub.id, cb); }), "leave");

            // 비공개 방이면 room 은 null (위치 비공개)
            mark = b.log.events.size();
            auto priv = makeRoom (c, "private");
            expect (waitNew (b, K::friendUpdated, mark, 10000, isUpdate (c.me.id, "in_room")), "friend.updated (비공개 방)");
            if (auto f = friendIn (friendsOf (b), c.me.id))
                expect (f->status == "in_room" && ! f->room.has_value(), "비공개 방이면 room: null");
            must (wait<Done> ([&] (Callback<Done> cb) { c.api.leaveRoom (priv.id, cb); }), "leave");

            // 방 안에 있으면 방 밖의 온라인 친구는 can_invite: true
            broom = makeRoom (b, "private", 2);
            if (auto f = friendIn (friendsOf (b), c.me.id)) expect (f->canInvite, "방 안에서 온라인·방 밖 친구는 can_invite");

            // 연결이 끊기면 offline + last_seen_at
            mark = b.log.events.size();
            c.rt.disconnect();
            expect (waitNew (b, K::friendUpdated, mark, 15000, isUpdate (c.me.id, "offline")), "friend.updated (offline)");
            list = friendsOf (b);
            if (auto f = friendIn (list, c.me.id))
                expect (f->lastSeenAt.has_value() && ! f->canInvite, "offline 이면 last_seen_at, 초대 불가");
            expect (! list.empty() && list.back().user.id == c.me.id, "offline 은 뒤");
            connect (c);
        }

        //==========================================================================
        beginTest ("friend request results, disguised sent, blocks, notification dot (7.2, 8, 9)");
        {
            expectEquals (requestFriend (a, a), juce::String ("self"));
            {
                Client ghost (*this, server);
                ghost.username = "nobody_" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt (0x7fffffff)).substring (0, 6);
                expectEquals (requestFriend (a, ghost), juce::String ("not_found"));
            }
            expectEquals (requestFriend (a, b), juce::String ("already_friends"));
            expectEquals (requestFriend (c, a), juce::String ("sent"));
            expectEquals (requestFriend (c, a), juce::String ("already_requested"));
            auto markA = a.log.events.size(), markC = c.log.events.size();
            expectEquals (requestFriend (a, c), juce::String ("accepted"), "상대가 먼저 요청해 둔 상태면 바로 친구");
            expect (waitNew (a, K::friendAdded, markA) && waitNew (c, K::friendAdded, markC), "friend.added 양쪽");

            // 친구는 차단할 수 없다
            mustFail (wait<Done> ([&] (Callback<Done> cb) { a.api.block (c.me.id, cb); }), ErrorCode::isFriend, "친구 차단");

            // 친구 삭제: 양쪽 목록에서 사라짐 (상대에게는 조용히 friend.removed)
            markC = c.log.events.size();
            must (wait<Done> ([&] (Callback<Done> cb) { a.api.removeFriend (c.me.id, cb); }), "DELETE /friends/{id}");
            expect (waitNew (c, K::friendRemoved, markC), "friend.removed");
            expect (! friendIn (friendsOf (a), c.me.id).has_value());
            mustFail (wait<Done> ([&] (Callback<Done> cb) { a.api.removeFriend (c.me.id, cb); }), ErrorCode::notFound, "친구가 아닌 사람 삭제");

            // 차단: 목록에 나오고, 차단당한 사람의 요청은 sent 로 위장된다 (상대에게 아무것도 안 감)
            must (wait<Done> ([&] (Callback<Done> cb) { a.api.block (c.me.id, cb); }), "POST /blocks");
            must (wait<Done> ([&] (Callback<Done> cb) { a.api.block (c.me.id, cb); }), "이미 차단한 사람도 204");
            auto blocks = must (wait<std::vector<Block>> ([&] (Callback<std::vector<Block>> cb) { a.api.listBlocks (cb); }), "GET /blocks");
            bool listed = false;
            for (auto& bl : blocks) if (bl.user.id == c.me.id) listed = true;
            expect (listed, "차단 목록에 c");
            markA = a.log.events.size();
            expectEquals (requestFriend (c, a), juce::String ("sent"), "차단당한 쪽에는 sent 로 위장");
            queue.runUntil ([] { return false; }, 1500);
            expect (newEvent (a, K::friendRequestReceived, markA) == nullptr, "차단한 쪽에는 요청이 보이지 않는다");
            expectEquals (requestFriend (c, a), juce::String ("already_requested"), "숨은 기록: 다시 보내면 already_requested");
            auto prof = must (wait<UserProfile> ([&] (Callback<UserProfile> cb) { c.api.getUser (a.me.id, cb); }), "GET /users/{id}");
            expectEquals (prof.relation, juce::String ("request_sent"), "위장 유지: 내 화면에는 요청 보냄");
            mustFail (wait<juce::String> ([&] (Callback<juce::String> cb) { a.api.sendFriendRequest (c.username, cb); }), ErrorCode::blockedByMe, "내가 차단한 사람에게 요청");

            // 차단 해제: 숨은 기록은 지워진다 (진짜 요청으로 바뀌지 않음). 다시 보내면 진짜 요청
            must (wait<Done> ([&] (Callback<Done> cb) { a.api.unblock (c.me.id, cb); }), "DELETE /blocks/{id}");
            must (wait<Done> ([&] (Callback<Done> cb) { a.api.unblock (c.me.id, cb); }), "차단하지 않은 사람 해제도 204");
            auto reqs = must (wait<std::vector<FriendRequest>> ([&] (Callback<std::vector<FriendRequest>> cb) { a.api.listFriendRequests (cb); }), "GET /friends/requests");
            for (auto& r : reqs) expect (r.from.id != c.me.id, "숨은 기록은 진짜 요청이 되지 않는다");
            markA = a.log.events.size();
            expectEquals (requestFriend (c, a), juce::String ("sent"));
            expect (waitNew (a, K::friendRequestReceived, markA), "차단을 푼 뒤의 요청은 보인다");

            // 알림 점: 받은 요청이 있으면 has_unread, 읽으면 꺼짐 (9장)
            expect (must (wait<bool> ([&] (Callback<bool> cb) { a.api.notificationState (cb); }), "GET /notifications/state"), "has_unread");
            must (wait<Done> ([&] (Callback<Done> cb) { a.api.markNotificationsRead (cb); }), "POST /notifications/read");
            expect (! must (wait<bool> ([&] (Callback<bool> cb) { a.api.notificationState (cb); }), "GET /notifications/state"), "읽으면 꺼짐");

            // 거절 (보낸 사람에게 알리지 않는다)
            reqs = must (wait<std::vector<FriendRequest>> ([&] (Callback<std::vector<FriendRequest>> cb) { a.api.listFriendRequests (cb); }), "GET /friends/requests");
            for (auto& r : reqs)
                if (r.from.id == c.me.id) must (wait<Done> ([&] (Callback<Done> cb) { a.api.declineFriendRequest (r.id, cb); }), "decline");

            // 친구 요청을 받지 않는 사람(friend_requests: none)에게도 sent 로 위장
            MePatch off;
            off.friendRequests = juce::String ("none");
            must (wait<Me> ([&] (Callback<Me> cb) { a.api.patchMe (off, cb); }), "PATCH /me friend_requests none");
            markA = a.log.events.size();
            expectEquals (requestFriend (c, a), juce::String ("sent"), "요청을 막아 둔 사람에게도 sent");
            queue.runUntil ([] { return false; }, 1500);
            expect (newEvent (a, K::friendRequestReceived, markA) == nullptr, "막아 둔 쪽에는 요청이 보이지 않는다");
            MePatch on;
            on.friendRequests = juce::String ("everyone");
            must (wait<Me> ([&] (Callback<Me> cb) { a.api.patchMe (on, cb); }), "PATCH /me friend_requests everyone");
        }

        //==========================================================================
        beginTest ("invite errors (7.7)");
        {
            // b 는 자기 방(정원 2)에 있다. a·c 는 방 밖. c 는 b 의 친구, a 와는 친구가 아니다
            mustFail (wait<InviteSent> ([&] (Callback<InviteSent> cb) { c.api.sendInvite (b.me.id, cb); }), ErrorCode::notInRoom, "내가 방에 없음");
            MePatch noInv;
            noInv.roomInvites = juce::String ("none");
            must (wait<Me> ([&] (Callback<Me> cb) { c.api.patchMe (noInv, cb); }), "PATCH /me room_invites none");
            mustFail (wait<InviteSent> ([&] (Callback<InviteSent> cb) { b.api.sendInvite (c.me.id, cb); }), ErrorCode::notAccepting, "초대를 받지 않는 사람");
            MePatch yesInv;
            yesInv.roomInvites = juce::String ("friends");
            must (wait<Me> ([&] (Callback<Me> cb) { c.api.patchMe (yesInv, cb); }), "PATCH /me room_invites friends");
            c.rt.disconnect();
            queue.runUntil ([] { return false; }, 1500);
            mustFail (wait<InviteSent> ([&] (Callback<InviteSent> cb) { b.api.sendInvite (c.me.id, cb); }), ErrorCode::targetOffline, "오프라인 친구");
            connect (c);

            // c 가 코드로 들어오면 정원이 찬다 → b 가 a 를 초대하면 room_full. c 가 a 를 초대하면 not_friend
            must (wait<JoinResult> ([&] (Callback<JoinResult> cb) { c.api.joinByCode (broom.inviteCode.value_or (""), cb); }), "join-by-code");
            mustFail (wait<InviteSent> ([&] (Callback<InviteSent> cb) { b.api.sendInvite (a.me.id, cb); }), ErrorCode::roomFull, "정원이 남지 않음");
            mustFail (wait<InviteSent> ([&] (Callback<InviteSent> cb) { c.api.sendInvite (a.me.id, cb); }), ErrorCode::notFriend, "친구가 아님");
            must (wait<Done> ([&] (Callback<Done> cb) { c.api.leaveRoom (broom.id, cb); }), "leave");
        }

        //==========================================================================
        beginTest ("invites: 3-second delivery, cancel inside 3 s, already_delivered, dismiss -> already_invited (7.7-7.10)");
        {
            // 3초 안에 취소하면 상대에게 아무것도 가지 않는다
            auto markA = a.log.events.size();
            auto first = must (wait<InviteSent> ([&] (Callback<InviteSent> cb) { b.api.sendInvite (a.me.id, cb); }), "POST /invites");
            expect (first.id.startsWith ("inv_") && first.deliverAt.isNotEmpty());
            must (wait<Done> ([&] (Callback<Done> cb) { b.api.cancelInvite (first.id, cb); }), "DELETE /invites/{id} (3초 안)");
            queue.runUntil ([] { return false; }, 4500);
            expect (newEvent (a, K::inviteReceived, markA) == nullptr, "취소한 초대는 전달되지 않는다");

            // 취소한 초대는 다시 보낼 수 있다. 3초 뒤에 전달된다
            const auto sentAt = juce::Time::getMillisecondCounter();
            auto second = must (wait<InviteSent> ([&] (Callback<InviteSent> cb) { b.api.sendInvite (a.me.id, cb); }), "다시 초대");
            expect (waitNew (a, K::inviteReceived, markA, 10000), "invite.received");
            const auto delay = (int) (juce::Time::getMillisecondCounter() - sentAt);
            expect (delay >= 2500, "3초 뒤에 전달 (" + juce::String (delay) + " ms)");
            if (auto* e = newEvent (a, K::inviteReceived, markA))
                expect (e->invite && e->invite->id == second.id && e->invite->room.id == broom.id && e->invite->from.id == b.me.id);
            mustFail (wait<Done> ([&] (Callback<Done> cb) { b.api.cancelInvite (second.id, cb); }), ErrorCode::alreadyDelivered, "전달된 뒤 취소");

            // 무시: 204, 같은 방에서는 다시 초대받지 않는다. 끝난 초대 수락은 404
            must (wait<Done> ([&] (Callback<Done> cb) { a.api.dismissInvite (second.id, cb); }), "POST /invites/{id}/dismiss");
            mustFail (wait<InviteSent> ([&] (Callback<InviteSent> cb) { b.api.sendInvite (a.me.id, cb); }), ErrorCode::alreadyInvited, "무시한 초대가 있으면 다시 초대 불가");
            mustFail (wait<JoinResult> ([&] (Callback<JoinResult> cb) { a.api.acceptInvite (second.id, cb); }), ErrorCode::notFound, "끝난 초대 수락 404");
        }

        beginTest ("invite expiry: when I enter any room, when the room closes; accept 200; latency telemetry 204 (7.9, 7.11, 5.10)");
        {
            // 받은 사람이 어느 방에든 들어가면 소멸
            auto markC = c.log.events.size();
            auto inv = must (wait<InviteSent> ([&] (Callback<InviteSent> cb) { b.api.sendInvite (c.me.id, cb); }), "POST /invites");
            expect (waitNew (c, K::inviteReceived, markC, 10000), "invite.received");
            auto own = makeRoom (c, "private");
            expect (waitNew (c, K::inviteExpired, markC, 10000, [&] (const RealtimeEvent& e) { return e.inviteId == inv.id; }), "입장하면 invite.expired");
            must (wait<Done> ([&] (Callback<Done> cb) { c.api.leaveRoom (own.id, cb); }), "leave");

            // 방이 닫히면 소멸. 그 초대를 수락하면 410 room_closed
            markC = c.log.events.size();
            auto inv2 = must (wait<InviteSent> ([&] (Callback<InviteSent> cb) { b.api.sendInvite (c.me.id, cb); }), "소멸한 초대 뒤 다시 초대");
            expect (waitNew (c, K::inviteReceived, markC, 10000), "invite.received");
            must (wait<Done> ([&] (Callback<Done> cb) { b.api.closeRoom (broom.id, cb); }), "close");
            expect (waitNew (c, K::inviteExpired, markC, 10000, [&] (const RealtimeEvent& e) { return e.inviteId == inv2.id; }), "방이 닫히면 invite.expired");
            mustFail (wait<JoinResult> ([&] (Callback<JoinResult> cb) { c.api.acceptInvite (inv2.id, cb); }), ErrorCode::roomClosed, "닫힌 방 초대 수락 410");

            // 수락 200: 바로 입장
            auto r2 = makeRoom (b, "private");
            markC = c.log.events.size();
            auto inv3 = must (wait<InviteSent> ([&] (Callback<InviteSent> cb) { b.api.sendInvite (c.me.id, cb); }), "POST /invites");
            expect (waitNew (c, K::inviteReceived, markC, 10000), "invite.received");
            auto joined = must (wait<JoinResult> ([&] (Callback<JoinResult> cb) { c.api.acceptInvite (inv3.id, cb); }), "accept invite");
            expect (joined.room.has_value() && joined.room->id == r2.id, "비공개 방 초대 수락은 바로 입장");

            // 지연 보정 자료 (5.10): 방장(b)이 있는 방의 멤버(c)가 보낸다
            must (wait<Done> ([&] (Callback<Done> cb) { c.api.sendLatencyTelemetry (r2.id, 14, 8, 12, 11, cb); }), "POST /telemetry/latency");
            must (wait<Done> ([&] (Callback<Done> cb) { c.api.leaveRoom (r2.id, cb); }), "leave");
            must (wait<Done> ([&] (Callback<Done> cb) { b.api.leaveRoom (r2.id, cb); }), "leave");
        }
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

        // R11 PR4: 친구·차단·초대·알림·지연 보정 자료
        socialContract (*a, *b, *c);

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
