// R11 PR4: 친구 정렬, 친구·초대 화면 문구 매핑, 초대 버튼 상태 (초대 → 취소 → 초대됨), 알림 점, 지연 보정 자료 (중앙값·한 번만)
#include "../core/home/HomeController.h"
#include "../core/jam/LatencyProbe.h"
#include "../core/api/ApiJson.h"
#include "../ui/Messages.h"

namespace meon::core
{

namespace
{
    /** 응답을 손으로 정하는 REST 전송 (보낸 요청을 남긴다) */
    struct StubRest : RestTransport
    {
        std::vector<HttpRequest> sent;
        std::function<HttpResponse (const HttpRequest&)> reply;
        void send (const HttpRequest& r, std::function<void (HttpResponse)> done) override
        {
            sent.push_back (r);
            done (reply ? reply (r) : HttpResponse { 500, {} });
        }
        int count (const juce::String& method, const juce::String& path) const
        {
            int n = 0;
            for (auto& r : sent) if (r.method == method && r.path == path) ++n;
            return n;
        }
    };

    struct StubLink : RealtimeTransport
    {
        void open (Sink*) override {}
        void send (const juce::var&) override {}
        void close() override {}
    };

    HttpResponse error (ErrorCode c)
    {
        const auto e = ApiError::of (c);
        return { e.httpStatus, json::encodeError (e) };
    }

    Friend friendOf (const juce::String& id, const juce::String& nick, const juce::String& status, const char* lastSeen = nullptr)
    {
        Friend f;
        f.user.id = id;
        f.user.username = id.substring (2);
        f.user.nickname = nick;
        f.status = status;
        if (lastSeen != nullptr) f.lastSeenAt = juce::String (lastSeen);
        return f;
    }

    juce::StringArray ids (const std::vector<Friend>& v)
    {
        juce::StringArray out;
        for (auto& f : v) out.add (f.user.id);
        return out;
    }
}

class SocialTests : public juce::UnitTest
{
public:
    SocialTests() : juce::UnitTest ("Friends, invites, notifications, latency (R11 PR4)", "Social") {}

    struct Rig
    {
        StubRest rest;
        StubLink link;
        ManualScheduler clock;
        MeonApi api { rest, 0 };
        Realtime rt { link, clock, 0 };
        AppStore store;
        HomeController home { api, rt, store, clock };
    };

    void runTest() override
    {
        beginTest ("friend order: in_room -> online -> offline, nickname, offline by last_seen_at (API 7.1)");
        {
            std::vector<Friend> v {
                friendOf ("u_a", "다은", "offline", "2026-10-13T01:00:00Z"),
                friendOf ("u_b", "가람", "online"),
                friendOf ("u_c", "나래", "in_room"),
                friendOf ("u_d", "가윤", "offline", "2026-10-13T05:00:00Z"),
                friendOf ("u_e", "가람", "in_room"),
                friendOf ("u_f", "바다", "offline"),   // 접속 기록 없음 → 맨 뒤
                friendOf ("u_g", "하늘", "online"),
            };
            AppStore::sortFriends (v);
            expectEquals (ids (v).joinIntoString (","), juce::String ("u_e,u_c,u_b,u_g,u_d,u_a,u_f"));

            // 상태가 바뀌거나(friend.updated) 새 친구가 오면(friend.added) 다시 정렬한다
            AppStore store;
            store.setFriends (v);
            RealtimeEvent e;
            e.kind = RealtimeEvent::Kind::friendUpdated;
            e.friendInfo = friendOf ("u_a", "다은", "in_room");
            store.event (e);
            expectEquals (ids (store.state().friends).joinIntoString (","), juce::String ("u_e,u_c,u_a,u_b,u_g,u_d,u_f"));
            e.kind = RealtimeEvent::Kind::friendAdded;
            e.friendInfo = friendOf ("u_h", "가", "online");
            store.event (e);
            expectEquals (ids (store.state().friends).indexOf ("u_h"), 3);   // 방 안 둘 다음, 온라인 중 첫째
        }

        beginTest ("screen text: invite failures, invite accept failures, last seen");
        {
            using E = ErrorCode;
            namespace msg = meon::ui::msg;
            expectEquals (msg::inviteFailure (E::targetOffline), TXT ("친구가 지금 오프라인이에요"));
            expectEquals (msg::inviteFailure (E::targetInRoom), TXT ("친구가 다른 방에서 합주 중이에요"));
            expectEquals (msg::inviteFailure (E::roomFull), TXT ("방이 가득 차서 초대할 수 없어요"));
            expectEquals (msg::inviteFailure (E::notAccepting), TXT ("지금은 초대를 받을 수 없는 친구예요"));
            expectEquals (msg::inviteFailure (E::notFriend), TXT ("친구에게만 초대를 보낼 수 있어요"));
            expectEquals (msg::inviteFailure (E::notInRoom), TXT ("방에 있을 때만 초대할 수 있어요"));
            expect (msg::inviteFailure (E::alreadyInvited).isEmpty(), "already_invited 는 토스트 없이 \"초대됨\"");
            expectEquals (msg::inviteAcceptFailure (E::roomClosed), TXT ("방이 닫혀서 들어갈 수 없어요"));
            expectEquals (msg::inviteAcceptFailure (E::notFound), TXT ("이미 끝난 초대예요"));
            expect (msg::inviteAcceptFailure (E::roomFull).isEmpty(), "그 밖의 입장 오류는 입장 실패 모달");
            const juce::int64 now = 1'000'000'000'000;
            expectEquals (msg::lastSeen (now - 3 * 3600 * 1000, now), TXT ("3시간 전 접속"));
            expectEquals (msg::lastSeen (now - 20 * 1000, now), TXT ("방금 접속"));
            expectEquals (msg::lastSeen (now - 25 * 60 * 1000, now), TXT ("25분 전 접속"));
            expectEquals (msg::lastSeen (0, now), TXT ("오프라인"));
            expectEquals (msg::friendRequestResult ("sent", "minjun"), TXT ("@minjun님에게 친구 요청을 보냈어요"));
        }

        beginTest ("invite button: invite -> cancel (3 s by this computer's clock) -> invited; cancel inside 3 s");
        {
            using P = HomeController::InviteState::Phase;
            Rig r;
            RoomDetail room;
            room.id = "r_1";
            r.store.setCurrentRoom (room);
            // 서버 시계가 이 컴퓨터보다 한참 앞서 있어도(deliver_at 이 이미 지난 시각) "취소"는 3초 보인다
            r.rest.reply = [] (const HttpRequest& q) -> HttpResponse
            {
                if (q.method == "POST" && q.path == "/invites")
                    return { 201, json::obj ({ { "id", "inv_" + q.body.getProperty ("user_id", "").toString() }, { "deliver_at", "2020-01-01T00:00:00Z" } }) };
                if (q.method == "DELETE") return { 204, {} };
                return { 500, {} };
            };
            r.home.invite ("u_a");
            expect (r.home.inviteState ("u_a").phase == P::pending);
            r.clock.advance (2900);
            expect (r.home.inviteState ("u_a").phase == P::pending, "\"취소\" 3초");
            r.clock.advance (200);
            expect (r.home.inviteState ("u_a").phase == P::sent, "3초 뒤 \"초대됨\"");

            r.home.invite ("u_b");
            r.clock.advance (1000);
            r.home.cancelInvite ("u_b");
            expect (r.home.inviteState ("u_b").phase == P::none, "3초 안에 취소하면 다시 \"초대\"");
            expectEquals (r.rest.count ("DELETE", "/invites/inv_u_b"), 1);

            // 취소가 늦어 이미 전달됐으면 (409 already_delivered) "초대됨"
            r.rest.reply = [] (const HttpRequest& q) -> HttpResponse
            {
                if (q.method == "POST") return { 201, json::obj ({ { "id", "inv_c" }, { "deliver_at", "2099-01-01T00:00:00Z" } }) };
                return error (ErrorCode::alreadyDelivered);
            };
            r.home.invite ("u_c");
            r.clock.advance (500);
            r.home.cancelInvite ("u_c");
            expect (r.home.inviteState ("u_c").phase == P::sent);

            // 실패: 버튼은 "초대"로 돌아가고 이유는 화면이 토스트로. already_invited 는 토스트 없이 "초대됨"
            juce::Array<ErrorCode> failed;
            r.home.onInviteFailed = [&] (ErrorCode c) { failed.add (c); };
            for (auto code : { ErrorCode::targetOffline, ErrorCode::targetInRoom, ErrorCode::roomFull, ErrorCode::notAccepting,
                               ErrorCode::notFriend, ErrorCode::notInRoom })
            {
                r.rest.reply = [code] (const HttpRequest&) { return error (code); };
                const auto user = "u_" + errorKey (code);
                r.home.invite (user);
                expect (r.home.inviteState (user).phase == P::none, errorKey (code));
            }
            expectEquals (failed.size(), 6);
            expect (failed[0] == ErrorCode::targetOffline && failed[5] == ErrorCode::notInRoom);
            r.rest.reply = [] (const HttpRequest&) { return error (ErrorCode::alreadyInvited); };
            r.home.invite ("u_d");
            expect (r.home.inviteState ("u_d").phase == P::sent);
            expectEquals (failed.size(), 6, "already_invited 는 실패로 알리지 않는다");
        }

        beginTest ("notification dot: GET /notifications/state when home shows, read on open");
        {
            Rig r;
            bool unread = true;
            r.rest.reply = [&] (const HttpRequest& q) -> HttpResponse
            {
                if (q.path == "/notifications/state") return { 200, json::obj ({ { "has_unread", unread } }) };
                if (q.path == "/notifications/read") return { 204, {} };
                return { 500, {} };
            };
            Hello h;
            h.me.id = "u_me";
            h.hasUnreadNotifications = false;
            r.store.hello (h);
            expect (! r.store.state().hasUnreadNotifications);
            r.home.setHomeVisible (true);
            expectEquals (r.rest.count ("GET", "/notifications/state"), 1);
            expect (r.store.state().hasUnreadNotifications, "홈이 보이면 서버 상태로 맞춘다");
            r.home.markNotificationsRead();
            expect (! r.store.state().hasUnreadNotifications);
            expectEquals (r.rest.count ("POST", "/notifications/read"), 1);

            // 다른 기기에서 읽었으면 다음에 홈이 보일 때 꺼진다
            unread = false;
            r.store.setHasUnread (true);
            r.home.setHomeVisible (false);
            r.home.setHomeVisible (true);
            expect (! r.store.state().hasUnreadNotifications);

            // 친구 요청을 받으면 켜진다
            RealtimeEvent e;
            e.kind = RealtimeEvent::Kind::friendRequestReceived;
            FriendRequest fr;
            fr.id = "fr_1";
            fr.from.id = "u_x";
            fr.from.username = "xx";
            e.friendRequest = fr;
            r.store.event (e);
            expect (r.store.state().hasUnreadNotifications);
        }

        beginTest ("friend request result accepted removes their pending request from the list");
        {
            Rig r;
            FriendRequest fr;
            fr.id = "fr_9";
            fr.from.id = "u_minjun";
            fr.from.username = "minjun";
            r.store.setFriendRequests ({ fr });
            r.rest.reply = [] (const HttpRequest&) -> HttpResponse { return { 200, json::obj ({ { "result", "accepted" } }) }; };
            juce::String result;
            r.home.sendFriendRequest ("@MinJun", [&] (Result<juce::String> x) { if (x.ok()) result = x.get(); });
            expectEquals (result, juce::String ("accepted"));
            expect (r.store.state().friendRequests.empty());
        }

        beginTest ("accept invite: 200 enters, 202 makes that room's button \"requested\", errors keep the toast gone (7.9)");
        {
            Rig r;
            Invite inv;
            inv.id = "inv_1";
            inv.room.id = "r_appr";
            inv.room.title = "approval";
            RealtimeEvent e;
            e.kind = RealtimeEvent::Kind::inviteReceived;
            e.invite = inv;
            r.store.event (e);
            expectEquals ((int) r.store.state().invites.size(), 1);
            r.rest.reply = [] (const HttpRequest&) -> HttpResponse { return { 202, json::obj ({ { "request_until", "2099-01-01T00:00:00Z" } }) }; };
            bool requested = false;
            r.home.acceptInvite ("inv_1", [&] (Result<JoinResult> x) { requested = x.ok() && x.get().requested(); });
            expect (requested);
            expect (r.store.state().invites.empty(), "수락하면 토스트가 사라진다");
            expect (r.home.isRequestPending ("r_appr"), "승인 방이면 그 방 버튼은 \"요청 보냄\"");
            expect (! r.store.state().inRoom());

            // 410·404: 토스트는 사라지고, 화면이 안내 토스트를 띄운다 (msg::inviteAcceptFailure)
            inv.id = "inv_2";
            e.invite = inv;
            r.store.event (e);
            r.rest.reply = [] (const HttpRequest&) { return error (ErrorCode::roomClosed); };
            ErrorCode code = ErrorCode::unknown;
            r.home.acceptInvite ("inv_2", [&] (Result<JoinResult> x) { if (! x.ok()) code = x.code(); });
            expect (code == ErrorCode::roomClosed && r.store.state().invites.empty());
        }

        beginTest ("latency probe: median of 10 s after host connects, once; skip rules");
        {
            expectEquals (LatencyProbe::median ({ 12.0f, 9.0f, 30.0f }), 12);
            expectEquals (LatencyProbe::median ({ 10.0f, 12.0f, 9.0f, 30.0f }), 11);

            LatencyProbe p;
            p.begin ("r_1", "u_host", false, true, 14, 8, 12);
            expect (p.isArmed());
            juce::int64 t = 0;
            expect (! p.update (t, false, 0.0f), "방장과 연결되기 전에는 재지 않는다");
            t += 5000;
            std::optional<LatencyProbe::Report> got;
            int reports = 0;
            const float pings[] = { 0.0f, 11.0f, 13.0f, 10.0f, 50.0f, 12.0f, 11.0f, 12.0f, 14.0f, 11.0f, 12.0f, 11.0f };
            for (int i = 0; i < 40; ++i)   // 0.4초마다 16초
            {
                const auto r = p.update (t, true, pings[(size_t) juce::jmin (11, (i * 400) / 1000)]);
                if (r) { got = r; ++reports; }
                t += 400;
            }
            expectEquals (reports, 1, "한 번만");
            expect (got.has_value());
            if (got)
            {
                expectEquals (got->roomId, juce::String ("r_1"));
                expectEquals (got->estimatedMs, 14);
                expectEquals (got->myRttMs, 8);
                expectEquals (got->hostRttMs, 12);
                expectEquals (got->actualP2pRttMs, 12, "0 은 빼고 중앙값");
            }

            // 재는 도중에 방장 연결이 끊기면 이번 입장은 보내지 않는다
            p.begin ("r_1", "u_host", false, true, 14, 8, 12);
            p.update (0, true, 10.0f);
            p.update (4000, false, 0.0f);
            expect (! p.update (11000, true, 10.0f) && ! p.update (22000, true, 10.0f));

            // 보내지 않는 경우: 내가 방장, 방장 부재, 예상 지연을 계산할 값 없음
            p.begin ("r_1", "u_host", true, true, 14, 8, 12);  expect (! p.isArmed());
            p.begin ("r_1", "u_host", false, false, 14, 8, 12); expect (! p.isArmed());
            p.begin ("r_1", "u_host", false, true, std::nullopt, std::nullopt, 12); expect (! p.isArmed());
            p.begin ("r_1", "u_host", false, true, std::nullopt, 8, std::nullopt); expect (! p.isArmed());
        }
    }
};

static SocialTests socialTests;

} // namespace meon::core
