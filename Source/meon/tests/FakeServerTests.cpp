// 가짜 서버 규칙 + MeonApi·Realtime·AppStore 를 함께 돌려 본다 (가짜 시계).
#include "../core/api/MeonApi.h"
#include "../core/api/Realtime.h"
#include "../core/api/ApiJson.h"
#include "../core/api/fake/FakeMeonServer.h"
#include "../core/state/AppStore.h"
#include "MeonFixtures.h"

namespace meon::core
{

namespace
{
    juce::var worldJson()
    {
        return juce::JSON::parse (juce::String::fromUTF8 (MeonFixtures::world_json, MeonFixtures::world_jsonSize));
    }

    FakeMeonServer::Options testOptions()
    {
        FakeMeonServer::Options o;
        o.latencyMs = 10;
        o.scripts = false;
        o.seed = 1234;
        return o;
    }

    struct Harness : AppStore::Listener
    {
        ManualScheduler clock;
        FakeMeonServer server { clock, testOptions() };
        MeonApi api { server, 0 };
        Realtime rt { server, clock, 0 };
        AppStore store;
        std::vector<Notice> notices;

        Harness()
        {
            server.loadWorld (worldJson());
            rt.addListener (&store);
            store.addListener (this);
        }

        ~Harness() override { store.removeListener (this); rt.removeListener (&store); }

        void notice (const Notice& n) override { notices.push_back (n); }

        template <typename T, typename Fn>
        Result<T> run (Fn&& call)
        {
            std::optional<Result<T>> out;
            call ([&out] (Result<T> r) { out.emplace (std::move (r)); });
            clock.advance (50);
            jassert (out.has_value());
            return out.has_value() ? *out : Result<T> (ApiError::of (ErrorCode::timeout));
        }

        /** 가입 -> 인증 -> 닉네임·파트 -> 실시간 연결 */
        Me signupReady (const juce::String& email = "me@example.com", const juce::String& username = "chan_keys")
        {
            auto s = run<AuthResult> ([&] (auto cb) { api.signup (email, username, "abcd123!", true, cb); });
            jassert (s.ok());
            auto v = run<Me> ([&] (auto cb) { api.verifyEmail (server.getLastMailedCode(), cb); });
            jassert (v.ok());
            MePatch p;
            p.nickname = juce::String::fromUTF8 ("승찬");
            p.part = juce::String ("keys");
            auto m = run<Me> ([&] (auto cb) { api.patchMe (p, cb); });
            rt.connect ([this] { return api.getTokens().accessToken; });
            clock.advance (100);
            return m.get();
        }
    };
}

class FakeServerTests : public juce::UnitTest
{
public:
    FakeServerTests() : juce::UnitTest ("FakeServer", "Api") {}

    void runTest() override
    {
        beginTest ("signup -> verify -> profile gating");
        {
            Harness h;
            auto avail = h.run<bool> ([&] (auto cb) { h.api.usernameAvailable ("chan_keys", cb); });
            expect (avail.ok() && avail.get());
            auto bad = h.run<AuthResult> ([&] (auto cb) { h.api.signup ("me@example.com", "chan_keys", "short", true, cb); });
            expect (bad.code() == ErrorCode::invalidRequest);
            expectEquals (bad.err().field, juce::String ("password"));

            auto s = h.run<AuthResult> ([&] (auto cb) { h.api.signup ("me@example.com", "chan_keys", "abcd123!", true, cb); });
            expect (s.ok());
            expect (! s.get().me.emailVerified && ! s.get().me.nickname);
            expect (h.api.hasSession());

            auto taken = h.run<bool> ([&] (auto cb) { h.api.usernameAvailable ("CHAN_KEYS", cb); });
            expect (taken.ok() && ! taken.get());
            auto dup = h.run<AuthResult> ([&] (auto cb) { h.api.signup ("ME@example.com", "other1", "abcd123!", true, cb); });
            expect (dup.code() == ErrorCode::emailTaken);

            auto blocked = h.run<std::vector<Room>> ([&] (auto cb) { h.api.listPublicRooms (cb); });
            expect (blocked.code() == ErrorCode::emailNotVerified);

            auto wrong = h.run<Me> ([&] (auto cb) { h.api.verifyEmail ("000000", cb); });
            expect (wrong.code() == ErrorCode::codeInvalid);
            auto resendTooSoon = h.run<Done> ([&] (auto cb) { h.api.resendVerification (cb); });
            expect (resendTooSoon.code() == ErrorCode::rateLimited);
            h.clock.advance (30000);
            const auto before = h.server.getLastMailedCode();
            expect (h.run<Done> ([&] (auto cb) { h.api.resendVerification (cb); }).ok());
            expect (h.server.getLastMailedCode().length() == 6);
            juce::ignoreUnused (before);

            auto v = h.run<Me> ([&] (auto cb) { h.api.verifyEmail (h.server.getLastMailedCode(), cb); });
            expect (v.ok() && v.get().emailVerified);

            auto incomplete = h.run<std::vector<Room>> ([&] (auto cb) { h.api.listPublicRooms (cb); });
            expect (incomplete.code() == ErrorCode::profileIncomplete);

            MePatch p;
            p.nickname = juce::String::fromUTF8 ("준\xF0\x9F\x8E\xB8");
            expect (h.run<Me> ([&] (auto cb) { h.api.patchMe (p, cb); }).code() == ErrorCode::invalidRequest);
            p.nickname = juce::String::fromUTF8 ("  승찬 ");
            p.part = juce::String ("keys");
            auto m = h.run<Me> ([&] (auto cb) { h.api.patchMe (p, cb); });
            expect (m.ok());
            expectEquals (m.get().nickname.value_or (""), juce::String::fromUTF8 ("승찬"));

            auto rooms = h.run<std::vector<Room>> ([&] (auto cb) { h.api.listPublicRooms (cb); });
            expect (rooms.ok());
            // 차단한 사람(raon, hyunwoo)이 방장인 방은 없고, 7개 방 모두 보인다 (hyunwoo 는 멤버일 뿐)
            expectEquals ((int) rooms.get().size(), 7);
        }

        beginTest ("PATCH /me in a room: room.updated to members, rooms.updated to subscribers, friend.updated to friends (bug 24)");
        {
            Harness h;
            h.signupReady();
            const auto myId = h.server.meId();
            struct Ev { juce::String to, type; juce::var data; };
            std::vector<Ev> evs;
            h.server.onEmitForTest = [&] (const juce::String& to, const juce::String& type, const juce::var& d) { evs.push_back ({ to, type, d }); };
            h.rt.subscribePublicRooms (true);
            h.clock.advance (100);
            auto j = h.run<JoinResult> ([&] (auto cb) { h.api.joinRoom ("r_f_1", cb); });   // 바로 입장 공개 방 (멤버 4명)
            expect (j.ok() && j.get().room.has_value());
            if (j.ok() && j.get().room) h.store.setCurrentRoom (j.get().room);
            h.clock.advance (100);
            evs.clear();

            MePatch p;
            p.part = juce::String ("drums");
            p.nickname = juce::String::fromUTF8 ("새이름");
            expect (h.run<Me> ([&] (auto cb) { h.api.patchMe (p, cb); }).ok());

            auto count = [&] (const juce::String& type, const juce::String& to)
            {
                int n = 0;
                for (auto& e : evs) if (e.type == type && (to.isEmpty() || e.to == to)) ++n;
                return n;
            };
            // 1) 방 멤버 모두 (나 + 4명)
            expectEquals (count ("room.updated", {}), 5);
            for (auto& u : { "u_f_minjun", "u_f_seoyeon", "u_f_hajun", "u_f_jiwoo" }) expectEquals (count ("room.updated", u), 1);
            // 2) 공개 방 목록 구독자 (가짜 서버는 실시간 연결이 하나라 나)
            expectEquals (count ("rooms.updated", myId), 1);
            // 3) 친구 모두 (가짜 세계의 친구 5명), 내용은 나의 새 파트·닉네임
            expectEquals (count ("friend.updated", {}) - count ("friend.updated", myId), 5);
            for (auto& e : evs)
                if (e.type == "friend.updated" && e.to == "u_f_yuna")
                {
                    const auto user = e.data.getProperty ("user", {});
                    expectEquals (user.getProperty ("id", {}).toString(), myId);
                    expectEquals (user.getProperty ("part", {}).toString(), juce::String ("drums"));
                    expectEquals (user.getProperty ("nickname", {}).toString(), juce::String::fromUTF8 ("새이름"));
                }

            // 내 앱: 방 멤버 줄과 공개 방 목록이 바로 바뀐다
            h.clock.advance (100);
            bool roomOk = false, listOk = false;
            if (h.store.state().currentRoom)
                for (auto& m : h.store.state().currentRoom->members)
                    if (m.user.id == myId) roomOk = m.user.part.value_or ("") == "drums" && m.user.nickname.value_or ("") == juce::String::fromUTF8 ("새이름");
            if (auto* r = h.store.state().findPublicRoom ("r_f_1"))
                for (auto& m : r->members)
                    if (m.user.id == myId) listOk = m.user.part.value_or ("") == "drums";
            expect (roomOk);
            expect (listOk);

            // 방 밖에서 바꿔도 친구에게는 간다
            h.store.setCurrentRoom (std::nullopt);
            expect (h.run<Done> ([&] (auto cb) { h.api.leaveRoom ("r_f_1", cb); }).ok());
            evs.clear();
            p.part = juce::String ("bass");
            p.nickname.reset();
            expect (h.run<Me> ([&] (auto cb) { h.api.patchMe (p, cb); }).ok());
            expectEquals (count ("room.updated", {}), 0);
            expectEquals (count ("friend.updated", {}) - count ("friend.updated", myId), 5);
            h.server.onEmitForTest = nullptr;
        }

        beginTest ("login, token expiry -> refresh -> retry");
        {
            Harness h;
            h.signupReady();
            auto badLogin = h.run<AuthResult> ([&] (auto cb) { h.api.login ("me@example.com", "wrong123!", cb); });
            expect (badLogin.code() == ErrorCode::invalidCredentials);
            auto login = h.run<AuthResult> ([&] (auto cb) { h.api.login ("ME@example.com", "abcd123!", cb); });
            expect (login.ok());

            int tokenChanges = 0;
            h.api.onTokensChanged = [&] (const Tokens&) { ++tokenChanges; };
            const auto oldRefresh = h.api.getTokens().refreshToken;
            h.clock.advance (16 * 60 * 1000);   // access 만료
            auto me = h.run<Me> ([&] (auto cb) { h.api.getMe (cb); });
            expect (me.ok());
            expectEquals (tokenChanges, 1);
            expect (h.api.getTokens().refreshToken != oldRefresh);

            // 쓴 refresh 토큰은 다시 못 쓴다
            bool lost = false;
            h.api.onSessionLost = [&] { lost = true; };
            h.api.setTokens ({ "at_bogus", oldRefresh });
            h.clock.advance (16 * 60 * 1000);
            auto fail = h.run<Me> ([&] (auto cb) { h.api.getMe (cb); });
            expect (fail.code() == ErrorCode::unauthorized);
            expect (lost);
            expect (! h.api.hasSession());
        }

        // refresh 재사용 유예 (진짜 서버 반영 대기): 새 토큰이 안 쓰였으면 10분까지 옛 토큰으로 다시 발급,
        // 새 토큰이 쓰였거나 10분이 지났으면 도용 → 그 계정의 모든 세션 무효
        auto refreshWith = [] (Harness& h, const juce::String& rt)
        {
            h.api.setTokens ({ {}, rt });
            return h.run<Tokens> ([&] (auto cb) { h.api.refresh (cb); });
        };

        beginTest ("refresh reuse: new token unused -> old token accepted within 10 min");
        {
            Harness h;
            h.signupReady();
            const auto x = h.api.getTokens().refreshToken;
            auto y = refreshWith (h, x);                         // x → y, 앱이 y 를 받기 전에 죽었다고 치고
            expect (y.ok());
            h.clock.advance (5 * 60 * 1000);
            auto z = refreshWith (h, x);                         // 다시 켜서 옛 x 로
            expect (z.ok(), "old token accepted while the new one is unused");
            expect (z.ok() && z.get().refreshToken != y.get().refreshToken);
            auto viaY = refreshWith (h, y.get().refreshToken);   // 받지 못한 y 는 무효
            expect (viaY.code() == ErrorCode::unauthorized);
        }

        beginTest ("refresh reuse: grace counts from the first rotation");
        {
            Harness h;
            h.signupReady();
            const auto x = h.api.getTokens().refreshToken;
            expect (refreshWith (h, x).ok());
            h.clock.advance (6 * 60 * 1000);
            expect (refreshWith (h, x).ok());                    // 6분: 유예 안
            h.clock.advance (5 * 60 * 1000);
            expect (refreshWith (h, x).code() == ErrorCode::unauthorized, "11 min after the first rotation");
        }

        beginTest ("refresh reuse: new token already used -> theft, every session revoked");
        {
            Harness h;
            h.signupReady();
            const auto x = h.api.getTokens().refreshToken;
            auto y = refreshWith (h, x);
            auto w = refreshWith (h, y.get().refreshToken);      // y 를 씀 → w
            expect (w.ok());
            auto other = h.run<AuthResult> ([&] (auto cb) { h.api.login ("me@example.com", "abcd123!", cb); });   // 다른 기기 세션
            expect (other.ok());
            expect (refreshWith (h, x).code() == ErrorCode::unauthorized);
            expect (refreshWith (h, w.get().refreshToken).code() == ErrorCode::unauthorized, "this device revoked");
            expect (refreshWith (h, other.get().tokens.refreshToken).code() == ErrorCode::unauthorized, "other device revoked");
            h.api.setTokens (other.get().tokens);
            expect (h.run<Me> ([&] (auto cb) { h.api.getMe (cb); }).code() == ErrorCode::unauthorized, "access tokens revoked");
        }

        beginTest ("refresh reuse: unused new token but over 10 min -> theft");
        {
            Harness h;
            h.signupReady();
            const auto x = h.api.getTokens().refreshToken;
            auto y = refreshWith (h, x);
            h.clock.advance (11 * 60 * 1000);
            expect (refreshWith (h, x).code() == ErrorCode::unauthorized);
            expect (refreshWith (h, y.get().refreshToken).code() == ErrorCode::unauthorized, "revoked");
        }

        beginTest ("refresh reuse: after logout the old token is just invalid");
        {
            Harness h;
            h.signupReady();
            const auto x = h.api.getTokens().refreshToken;
            auto y = refreshWith (h, x);
            expect (h.run<Done> ([&] (auto cb) { h.api.logout (cb); }).ok());
            auto other = h.run<AuthResult> ([&] (auto cb) { h.api.login ("me@example.com", "abcd123!", cb); });
            expect (refreshWith (h, x).code() == ErrorCode::unauthorized);
            expect (refreshWith (h, other.get().tokens.refreshToken).ok(), "no theft revoke after a normal logout");
        }

        beginTest ("refresh reuse: grace survives a restart (saved state)");
        {
            juce::var saved;
            juce::String x;
            {
                Harness h;
                h.signupReady();
                x = h.api.getTokens().refreshToken;
                expect (refreshWith (h, x).ok());
                saved = h.server.saveState();
            }
            Harness h2;
            h2.server.loadState (saved);
            expect (refreshWith (h2, x).ok());
        }

        beginTest ("saved state restores account and refresh token");
        {
            juce::var saved;
            juce::String refresh;
            {
                Harness h;
                h.signupReady();
                refresh = h.api.getTokens().refreshToken;
                saved = h.server.saveState();
            }
            Harness h2;
            h2.server.loadState (saved);
            h2.api.setTokens ({ {}, refresh });
            auto t = h2.run<Tokens> ([&] (auto cb) { h2.api.refresh (cb); });
            expect (t.ok());
            auto me = h2.run<Me> ([&] (auto cb) { h2.api.getMe (cb); });
            expect (me.ok() && me.get().username == "chan_keys" && me.get().emailVerified);
        }

        beginTest ("hello fills the store; join public room; invites vanish on entry");
        {
            Harness h;
            h.signupReady();
            expect (h.store.state().link == Realtime::Link::online);
            expect (h.store.state().me.has_value());
            expectEquals ((int) h.store.state().friends.size(), 5);
            expect (h.store.state().friends.front().status == "in_room");   // 유나: 방 안 -> 맨 앞
            expect (h.store.state().hasUnreadNotifications);

            h.server.devInviteMe();
            h.clock.advance (FakeMeonServer::inviteDelayMs + 50);
            expectEquals ((int) h.store.state().invites.size(), 1);
            expect (! h.notices.empty() && h.notices.back().kind == Notice::Kind::inviteReceived);

            auto j = h.run<JoinResult> ([&] (auto cb) { h.api.joinRoom ("r_f_3", cb); });   // 어쿠스틱 (바로 입장)
            expect (j.ok() && ! j.get().requested());
            h.store.setCurrentRoom (j.get().room);
            expect (h.store.state().invites.empty());
            h.clock.advance (50);
            expectEquals (h.server.pendingInviteCountFor (h.server.meId()), 0);

            const auto& c = j.get().room->connection;
            expect (c.members.contains (h.server.meId()));
            expectEquals (c.aooUsername, h.server.meId());
            expectEquals (c.groupPassword, FakeMeonServer::groupPasswordFor (c.groupName));

            auto again = h.run<JoinResult> ([&] (auto cb) { h.api.joinRoom ("r_f_3", cb); });
            expect (again.ok() && ! again.get().requested());   // 같은 방 재입장: 멱등
            auto other = h.run<JoinResult> ([&] (auto cb) { h.api.joinRoom ("r_f_1", cb); });
            expect (other.code() == ErrorCode::alreadyInRoom);

            expect (h.run<Done> ([&] (auto cb) { h.api.leaveRoom ("r_f_3", cb); }).ok());
        }

        beginTest ("capacity: full room, host absent reserve");
        {
            Harness h;
            h.signupReady();
            auto full = h.run<JoinResult> ([&] (auto cb) { h.api.joinRoom ("r_f_4", cb); });   // 7/7
            expect (full.code() == ErrorCode::roomFull);
            auto code = h.run<JoinResult> ([&] (auto cb) { h.api.joinByCode ("full77", cb); });
            expect (code.code() == ErrorCode::roomFull);
            auto closed = h.run<JoinResult> ([&] (auto cb) { h.api.joinByCode ("CLSD99", cb); });
            expect (closed.code() == ErrorCode::roomClosed);
            auto bad = h.run<JoinResult> ([&] (auto cb) { h.api.joinByCode ("A0B1CD", cb); });
            expect (bad.code() == ErrorCode::invalidCode);
        }

        beginTest ("approval: request_until, re-request, fake host accepts");
        {
            Harness h;
            h.signupReady();
            auto r1 = h.run<JoinResult> ([&] (auto cb) { h.api.joinRoom ("r_f_7", cb); });   // 록 클래식 (승인, 4/5)
            expect (r1.ok() && r1.get().requested());
            const auto until = *r1.get().requestUntil;
            // 시각 문자열은 초 단위
            expectWithinAbsoluteError ((double) (json::parseIsoTime (until) - h.clock.nowMs()), (double) FakeMeonServer::requestUntilMs - 50, 1000.0);
            auto r2 = h.run<JoinResult> ([&] (auto cb) { h.api.joinRoom ("r_f_7", cb); });
            expect (r2.ok() && r2.get().requested() && *r2.get().requestUntil == until);

            h.clock.advance (6000);   // 가짜 방장이 5초 뒤 수락
            expect (h.store.state().inRoom());
            expectEquals (h.store.state().currentRoom->id, juce::String ("r_f_7"));
            expect (! h.notices.empty() && h.notices.back().kind == Notice::Kind::joinAccepted);
        }

        beginTest ("private room by code: my room, other computer's code");
        {
            Harness h;
            h.signupReady();
            CreateRoom c;
            c.type = "private";
            c.title = juce::String::fromUTF8 ("새벽 합주");
            c.entryMode = juce::String ("instant");
            expect (h.run<RoomDetail> ([&] (auto cb) { h.api.createRoom (c, cb); }).code() == ErrorCode::invalidRequest);
            c.entryMode.reset();
            auto room = h.run<RoomDetail> ([&] (auto cb) { h.api.createRoom (c, cb); });
            expect (room.ok());
            const auto code = room.get().inviteCode.value_or ("");
            expect (code.length() == 6);
            expectEquals (room.get().connection.groupName, code);
            expect (room.get().hostId == h.server.meId() && room.get().hostPresent);
            expect (h.run<Done> ([&] (auto cb) { h.api.leaveRoom (room.get().id, cb); }).ok());
            auto gone = h.run<JoinResult> ([&] (auto cb) { h.api.joinByCode (code, cb); });
            expect (gone.code() == ErrorCode::roomClosed);   // 마지막 사람이 나가 사라진 방: 24시간 410

            auto remote = h.run<JoinResult> ([&] (auto cb) { h.api.joinByCode ("q7k2mx", cb); });
            expect (remote.ok() && ! remote.get().requested());
            const auto& d = *remote.get().room;
            expectEquals (d.connection.groupName, juce::String ("Q7K2MX"));
            expectEquals (d.connection.groupPassword, FakeMeonServer::groupPasswordFor ("Q7K2MX"));
            expect (! d.hostPresent);
            h.store.setCurrentRoom (d);

            h.server.remotePeerJoined ("u_peer1", juce::String::fromUTF8 ("민준"), "guitar", true);
            h.clock.advance (50);
            const auto& cur = *h.store.state().currentRoom;
            expectEquals (cur.memberCount, 2);
            expect (cur.hostId == "u_peer1" && cur.hostPresent);
            expect (cur.connection.members.contains ("u_peer1"));
            h.server.remotePeerLeft ("u_peer1");
            h.clock.advance (50);
            expectEquals (h.store.state().currentRoom->memberCount, 1);
        }

        beginTest ("host: join requests expire after 60s, accept, kick, close");
        {
            Harness h;
            h.signupReady();
            CreateRoom c;
            c.title = "Host test";
            c.entryMode = juce::String ("approval");
            c.capacity = 3;
            auto room = h.run<RoomDetail> ([&] (auto cb) { h.api.createRoom (c, cb); });
            expect (room.ok());
            h.store.setCurrentRoom (room.get());
            const auto rid = room.get().id;

            h.server.devJoinRequest();
            h.clock.advance (50);
            expectEquals ((int) h.store.state().joinRequests.size(), 1);
            h.clock.advance (FakeMeonServer::joinRequestLifeMs);
            expectEquals ((int) h.store.state().joinRequests.size(), 0);

            h.server.devJoinRequest();
            h.server.devJoinRequest();
            h.clock.advance (50);
            expectEquals ((int) h.store.state().joinRequests.size(), 2);
            const auto first = h.store.state().joinRequests[0];
            const auto second = h.store.state().joinRequests[1];
            auto acc = h.run<RoomDetail> ([&] (auto cb) { h.api.acceptJoinRequest (rid, first.id, cb); });
            expect (acc.ok());
            expectEquals (acc.get().memberCount, 2);
            // 방장이 직접 수락한 요청에는 join_request.removed 가 오지 않는다 (API 5.7). 앱은 RoomController 가 목록에서 지운다
            expectEquals ((int) h.store.state().joinRequests.size(), 2);
            h.store.removeJoinRequest (first.id);
            auto acc2 = h.run<RoomDetail> ([&] (auto cb) { h.api.acceptJoinRequest (rid, second.id, cb); });
            expect (acc2.ok(), errorKey (acc2.code()));
            expectEquals (acc2.get().memberCount, 3);

            RoomPatch p;
            p.capacity = 2;
            expect (h.run<RoomDetail> ([&] (auto cb) { h.api.patchRoom (rid, p, cb); }).code() == ErrorCode::capacityBelowMembers);
            p.capacity.reset();
            p.type = juce::String ("private");
            auto priv = h.run<RoomDetail> ([&] (auto cb) { h.api.patchRoom (rid, p, cb); });
            expect (priv.ok() && priv.get().inviteCode.has_value() && ! priv.get().entryMode);

            const auto target = first.user.id;
            expect (h.run<Done> ([&] (auto cb) { h.api.kick (rid, target, cb); }).ok());
            expect (h.run<Done> ([&] (auto cb) { h.api.kick (rid, h.server.meId(), cb); }).code() == ErrorCode::invalidRequest);
            expect (! h.server.isMember (rid, target));

            auto tr = h.run<RoomDetail> ([&] (auto cb) { h.api.transferHost (rid, second.user.id, cb); });
            expect (tr.ok() && tr.get().hostId == second.user.id);
            expect (h.run<Done> ([&] (auto cb) { h.api.closeRoom (rid, cb); }).code() == ErrorCode::notHost);
        }

        beginTest ("kicked and closed notices (dev)");
        {
            Harness h;
            h.signupReady();
            auto j = h.run<JoinResult> ([&] (auto cb) { h.api.joinRoom ("r_f_1", cb); });
            h.store.setCurrentRoom (j.get().room);
            h.server.devKickMe();
            h.clock.advance (50);
            expect (! h.store.state().inRoom());
            expect (h.notices.back().kind == Notice::Kind::kicked);
            auto again = h.run<JoinResult> ([&] (auto cb) { h.api.joinRoom ("r_f_1", cb); });
            expect (again.code() == ErrorCode::kicked);

            auto j2 = h.run<JoinResult> ([&] (auto cb) { h.api.joinRoom ("r_f_6", cb); });
            h.store.setCurrentRoom (j2.get().room);
            h.server.devCloseMyRoom();
            h.clock.advance (50);
            expect (h.notices.back().kind == Notice::Kind::roomClosed);
            expect (h.run<JoinResult> ([&] (auto cb) { h.api.joinRoom ("r_f_6", cb); }).code() == ErrorCode::roomClosed);
        }

        beginTest ("friends: requests, hidden records, blocks, invites 3s");
        {
            Harness h;
            h.signupReady();
            auto self = h.run<juce::String> ([&] (auto cb) { h.api.sendFriendRequest ("chan_keys", cb); });
            expectEquals (self.get(), juce::String ("self"));
            expectEquals (h.run<juce::String> ([&] (auto cb) { h.api.sendFriendRequest ("nobody", cb); }).get(), juce::String ("not_found"));
            expectEquals (h.run<juce::String> ([&] (auto cb) { h.api.sendFriendRequest ("taeo", cb); }).get(), juce::String ("already_friends"));
            expect (h.run<juce::String> ([&] (auto cb) { h.api.sendFriendRequest ("raon", cb); }).code() == ErrorCode::blockedByMe);
            expectEquals (h.run<juce::String> ([&] (auto cb) { h.api.sendFriendRequest ("hyunseo", cb); }).get(), juce::String ("accepted"));
            expectEquals (h.run<juce::String> ([&] (auto cb) { h.api.sendFriendRequest ("boram", cb); }).get(), juce::String ("sent"));
            expectEquals (h.run<juce::String> ([&] (auto cb) { h.api.sendFriendRequest ("boram", cb); }).get(), juce::String ("already_requested"));
            h.clock.advance (7000);   // 가짜 사용자가 수락
            expect (h.store.state().findFriend ("u_f_boram") != nullptr);

            auto reqs = h.run<std::vector<FriendRequest>> ([&] (auto cb) { h.api.listFriendRequests (cb); });
            expectEquals ((int) reqs.get().size(), 1);   // 지안만 남음
            expect (h.run<bool> ([&] (auto cb) { h.api.notificationState (cb); }).get());
            expect (h.run<Done> ([&] (auto cb) { h.api.markNotificationsRead (cb); }).ok());
            expect (! h.run<bool> ([&] (auto cb) { h.api.notificationState (cb); }).get());

            expect (h.run<Done> ([&] (auto cb) { h.api.block ("u_f_taeo", cb); }).code() == ErrorCode::isFriend);
            expect (h.run<Done> ([&] (auto cb) { h.api.removeFriend ("u_f_taeo", cb); }).ok());
            expect (h.run<Done> ([&] (auto cb) { h.api.block ("u_f_taeo", cb); }).ok());
            auto prof = h.run<UserProfile> ([&] (auto cb) { h.api.getUser ("u_f_taeo", cb); });
            expect (prof.ok() && prof.get().relation == "blocked_by_me" && ! prof.get().status);
            expect (h.run<Done> ([&] (auto cb) { h.api.unblock ("u_f_taeo", cb); }).ok());
            expectEquals ((int) h.run<std::vector<Block>> ([&] (auto cb) { h.api.listBlocks (cb); }).get().size(), 2);

            // 초대: 방에 있어야 하고, 방에 있는 친구는 안 됨, 3초 안에만 취소
            expect (h.run<InviteSent> ([&] (auto cb) { h.api.sendInvite ("u_f_siu", cb); }).code() == ErrorCode::notInRoom);
            CreateRoom c;
            c.type = "private";
            c.title = "Invite test";
            auto room = h.run<RoomDetail> ([&] (auto cb) { h.api.createRoom (c, cb); });
            h.store.setCurrentRoom (room.get());
            expect (h.run<InviteSent> ([&] (auto cb) { h.api.sendInvite ("u_f_yuna", cb); }).code() == ErrorCode::targetInRoom);
            expect (h.run<InviteSent> ([&] (auto cb) { h.api.sendInvite ("u_f_chaewon", cb); }).code() == ErrorCode::targetOffline);
            auto inv = h.run<InviteSent> ([&] (auto cb) { h.api.sendInvite ("u_f_siu", cb); });
            expect (inv.ok());
            expect (h.run<InviteSent> ([&] (auto cb) { h.api.sendInvite ("u_f_siu", cb); }).code() == ErrorCode::alreadyInvited);
            expect (h.run<Done> ([&] (auto cb) { h.api.cancelInvite (inv.get().id, cb); }).ok());
            auto inv2 = h.run<InviteSent> ([&] (auto cb) { h.api.sendInvite ("u_f_siu", cb); });
            h.clock.advance (FakeMeonServer::inviteDelayMs);
            expect (h.run<Done> ([&] (auto cb) { h.api.cancelInvite (inv2.get().id, cb); }).code() == ErrorCode::alreadyDelivered);
            h.clock.advance (5000);   // 가짜 친구가 수락하고 들어옴
            expectEquals (h.store.state().currentRoom->memberCount, 2);
        }

        beginTest ("realtime: drop -> reconnecting -> hello again");
        {
            Harness h;
            h.signupReady();
            expect (h.store.state().link == Realtime::Link::online);
            h.server.devDropLink (5);
            expect (h.store.state().link == Realtime::Link::reconnecting);
            h.clock.advance (4000);
            expect (h.store.state().link == Realtime::Link::reconnecting);
            h.clock.advance (20000);
            expect (h.store.state().link == Realtime::Link::online);

            h.server.devSetConfig (5, 9);
            h.clock.advance (50);
            expectEquals (h.store.state().config.latestBuild, 9);

            // 인증 전 계정은 실시간 연결 거부
            Harness u;
            u.run<AuthResult> ([&] (auto cb) { u.api.signup ("x@example.com", "xuser", "abcd123!", true, cb); });
            struct Rejected : Realtime::Listener { juce::String code; void authRejected (const juce::String& c) override { code = c; } } rej;
            u.rt.addListener (&rej);
            u.rt.connect ([&u] { return u.api.getTokens().accessToken; });
            u.clock.advance (100);
            expectEquals (rej.code, juce::String ("email_not_verified"));
            u.rt.removeListener (&rej);
        }

        beginTest ("client_outdated (426) below min_build");
        {
            ManualScheduler clock;
            FakeMeonServer server { clock, testOptions() };
            server.loadWorld (worldJson());
            server.devSetConfig (10, 12);
            MeonApi oldApp { server, 5 };
            bool outdated = false;
            oldApp.onClientOutdated = [&] { outdated = true; };
            std::optional<Result<AuthResult>> r;
            oldApp.login ("a@b.com", "x", [&] (Result<AuthResult> x) { r.emplace (x); });
            std::optional<Result<Config>> cfg;
            oldApp.getConfig ([&] (Result<Config> x) { cfg.emplace (x); });
            clock.advance (50);
            expect (r->code() == ErrorCode::clientOutdated && outdated);
            expect (cfg->ok() && cfg->get().minBuild == 10);   // /config 는 예외
        }
    }
};

static FakeServerTests fakeServerTests;

//==============================================================================
// 진짜 서버 v0.5 와 같은 규칙인지 (R11 PR5). 본문·상태 코드를 직접 본다
namespace
{
    struct Raw : RealtimeTransport::Sink
    {
        ManualScheduler clock;
        FakeMeonServer server;
        std::vector<juce::var> messages;
        std::vector<int> closes;

        explicit Raw (FakeMeonServer::Options o = testOptions()) : server (clock, o) { server.loadWorld (worldJson()); }

        void transportOpened() override {}
        void transportMessage (const juce::var& m) override { messages.push_back (m); }
        void transportClosed (int code) override { closes.push_back (code); }

        HttpResponse call (const juce::String& method, const juce::String& path, const juce::var& body = {}, const juce::String& token = {})
        {
            HttpRequest r;
            r.method = method;
            r.path = path;
            r.body = body;
            r.accessToken = token;
            return server.handle (r);
        }

        struct Account { juce::String id, username, access, refresh; };

        Account make (const juce::String& username)
        {
            auto s = call ("POST", "/auth/signup", json::obj ({ { "email", username + "@example.com" }, { "username", username },
                                                                { "password", "abcd123!" }, { "agreed_terms", true } }));
            Account a { s.body["me"]["id"].toString(), username, s.body["access_token"].toString(), s.body["refresh_token"].toString() };
            call ("POST", "/auth/verify-email", json::obj ({ { "code", server.getLastMailedCode() } }), a.access);
            call ("PATCH", "/me", json::obj ({ { "nickname", juce::String::fromUTF8 ("멤버") }, { "part", "keys" } }), a.access);
            return a;
        }

        /** 이 계정으로 실시간 연결 (접속 중이 된다) */
        void connect (const Account& a)
        {
            server.open (this);
            clock.advance (20);
            server.send (json::obj ({ { "type", "auth" }, { "access_token", a.access }, { "build", 0 } }));
            clock.advance (20);
        }

        bool received (const juce::String& type) const
        {
            for (auto& m : messages)
                if (m["type"].toString() == type) return true;
            return false;
        }

        void befriend (const Account& a, const Account& b)
        {
            call ("POST", "/friends/requests", json::obj ({ { "username", b.username } }), a.access);
            auto list = call ("GET", "/friends/requests", {}, b.access);
            const auto id = list.body["requests"][0]["id"].toString();
            call ("POST", "/friends/requests/" + id + "/accept", {}, b.access);
        }

        juce::String createRoom (const Account& a, const juce::String& type = "public")
        {
            auto body = json::obj ({ { "type", type }, { "title", "t" }, { "description", "" }, { "capacity", 5 } });
            if (type == "public") body.getDynamicObject()->setProperty ("entry_mode", "instant");
            return call ("POST", "/rooms", body, a.access).body["id"].toString();
        }
    };

    juce::String errCode (const HttpResponse& r)  { return r.body["error"]["code"].toString(); }
    juce::String errField (const HttpResponse& r) { return r.body["error"]["field"].toString(); }
}

class FakeServerV05Tests : public juce::UnitTest
{
public:
    FakeServerV05Tests() : juce::UnitTest ("FakeServer matches server v0.5 (R11 PR5)", "Api") {}

    void runTest() override
    {
        beginTest ("invites: dismissed blocks re-invite, closed room 410, dropped cancel 409, accepted ends (7.7-7.11)");
        {
            Raw x;
            auto a = x.make ("user_a"), b = x.make ("user_b");
            x.befriend (a, b);
            x.connect (a);                       // a 는 접속 중 (초대를 받는 사람)
            const auto room = x.createRoom (b);
            auto inv = x.call ("POST", "/invites", json::obj ({ { "user_id", a.id } }), b.access);
            expectEquals (inv.status, 201);
            expectEquals (errCode (x.call ("POST", "/invites", json::obj ({ { "user_id", a.id } }), b.access)), juce::String ("already_invited"),
                          "3초 대기 중");
            x.clock.advance (FakeMeonServer::inviteDelayMs + 50);
            expect (x.received ("invite.received"));
            const auto id = inv.body["id"].toString();
            expectEquals (x.call ("DELETE", "/invites/" + id, {}, b.access).status, 409, "전달된 뒤 취소는 already_delivered");
            expectEquals (x.call ("POST", "/invites/" + id + "/dismiss", {}, a.access).status, 204);
            expectEquals (errCode (x.call ("POST", "/invites/" + id + "/dismiss", {}, a.access)), juce::String ("not_found"), "끝난 초대");
            expectEquals (errCode (x.call ("POST", "/invites", json::obj ({ { "user_id", a.id } }), b.access)), juce::String ("already_invited"),
                          "무시한 초대가 있으면 같은 방에서 다시 초대할 수 없다");
            expectEquals (x.server.pendingInviteCountFor (a.id), 0);

            // 닫힌 방: 끝난 초대여도 410 (방 확인이 먼저)
            expectEquals (x.call ("POST", "/rooms/" + room + "/close", {}, b.access).status, 204);
            expectEquals (errCode (x.call ("POST", "/invites/" + id + "/accept", {}, a.access)), juce::String ("room_closed"));
            expectEquals (errCode (x.call ("POST", "/invites/" + id + "/dismiss", {}, a.access)), juce::String ("room_closed"));

            // 새 방: 전달 시점에 받는 사람이 방에 있으면 버려진다 → 취소는 409, 같은 사람에게 다시 초대할 수 있다
            const auto room2 = x.createRoom (b);
            const auto aRoom = x.createRoom (a, "private");
            auto inv2 = x.call ("POST", "/invites", json::obj ({ { "user_id", a.id } }), b.access);
            expectEquals (errCode (inv2), juce::String ("target_in_room"));
            expectEquals (x.call ("POST", "/rooms/" + aRoom + "/leave", {}, a.access).status, 204);
            inv2 = x.call ("POST", "/invites", json::obj ({ { "user_id", a.id } }), b.access);
            expectEquals (inv2.status, 201);
            x.createRoom (a, "private");         // 3초 안에 a 가 방에 들어감
            x.clock.advance (FakeMeonServer::inviteDelayMs + 50);
            expectEquals (errCode (x.call ("DELETE", "/invites/" + inv2.body["id"].toString(), {}, b.access)), juce::String ("already_delivered"));
            expectEquals (x.server.pendingInviteCountFor (a.id), 0);
            juce::ignoreUnused (room2);
        }

        beginTest ("invite accept ends the invite before entering: no invite.expired for it, not in hello again");
        {
            Raw x;
            auto a = x.make ("user_a"), b = x.make ("user_b");
            x.befriend (a, b);
            x.connect (a);
            const auto room = x.createRoom (b);
            auto inv = x.call ("POST", "/invites", json::obj ({ { "user_id", a.id } }), b.access);
            x.clock.advance (FakeMeonServer::inviteDelayMs + 50);
            x.messages.clear();
            expectEquals (x.call ("POST", "/invites/" + inv.body["id"].toString() + "/accept", {}, a.access).status, 200);
            x.clock.advance (50);
            expect (! x.received ("invite.expired"));
            expect (x.server.isMember (room, a.id));
            expectEquals (errCode (x.call ("POST", "/invites/" + inv.body["id"].toString() + "/accept", {}, a.access)), juce::String ("not_found"));
        }

        beginTest ("can_invite needs me in a room (2.4, 2.10)");
        {
            Raw x;
            auto a = x.make ("user_a");
            x.connect (a);                       // 가짜 친구가 붙는다 (u_f_taeo: 온라인, 방에 없음)
            auto canInvite = [&]
            {
                auto list = x.call ("GET", "/friends", {}, a.access).body["friends"];
                for (int i = 0; i < list.size(); ++i)
                    if (list[i]["user"]["id"].toString() == "u_f_taeo") return (bool) list[i]["can_invite"];
                return false;
            };
            expect (! canInvite(), "내가 방에 없으면 false");
            x.createRoom (a);
            expect (canInvite(), "내가 방에 있으면 true");
            expect ((bool) x.call ("GET", "/users/u_f_taeo", {}, a.access).body["can_invite"]);
        }

        beginTest ("errors: 404 not_found for non-friend delete and missing join request, 400 field names");
        {
            Raw x;
            auto a = x.make ("user_a");
            expectEquals (errCode (x.call ("DELETE", "/friends/u_f_doyun", {}, a.access)), juce::String ("not_found"));
            const auto room = x.createRoom (a);
            expectEquals (errCode (x.call ("POST", "/rooms/" + room + "/join-requests/jr_none/accept", {}, a.access)), juce::String ("not_found"));
            expectEquals (errCode (x.call ("POST", "/rooms/" + room + "/join-requests/jr_none/decline", {}, a.access)), juce::String ("not_found"));

            // 5.10 텔레메트리: room_id 형식, 값 범위. 빠진 값은 0
            auto tele = [&] (const juce::var& body) { return x.call ("POST", "/telemetry/latency", body, a.access); };
            expectEquals (tele (json::obj ({ { "room_id", room }, { "estimated_ms", 40 } })).status, 204);
            expectEquals (errField (tele (json::obj ({ { "room_id", "nope" } }))), juce::String ("room_id"));
            expectEquals (errField (tele (json::obj ({ { "room_id", room }, { "my_rtt_ms", 10001 } }))), juce::String ("my_rtt_ms"));
            expectEquals (errField (tele (json::obj ({ { "room_id", room }, { "actual_p2p_rtt_ms", -1 } }))), juce::String ("actual_p2p_rtt_ms"));
            expectEquals (tele (json::obj ({ { "room_id", room }, { "host_rtt_ms", "12" } })).status, 400);

            // 5.9 RTT
            expectEquals (errField (x.call ("POST", "/me/rtt", json::obj ({ { "aoo_rtt_ms", juce::var() } }), a.access)), juce::String ("aoo_rtt_ms"));
            expectEquals (errField (x.call ("POST", "/me/rtt", json::obj ({ { "aoo_rtt_ms", 10001 } }), a.access)), juce::String ("aoo_rtt_ms"));
            expectEquals (x.call ("POST", "/me/rtt", json::obj ({ { "aoo_rtt_ms", 10000 } }), a.access).status, 204);

            // 4.2 PATCH /me
            expectEquals (errField (x.call ("PATCH", "/me", json::obj ({ { "username", "x" } }), a.access)), juce::String ("username"));
            expectEquals (errField (x.call ("PATCH", "/me", json::obj ({ { "nickname", juce::var() } }), a.access)), juce::String ("nickname"));
            expectEquals (errField (x.call ("PATCH", "/me", json::obj ({ { "settings", json::obj ({ { "room_invites", "all" } }) } }), a.access)),
                          juce::String ("settings.room_invites"));

            // 6장: 자기 자신에게 방장 넘기기, 빈 user_id
            expectEquals (errField (x.call ("POST", "/rooms/" + room + "/transfer-host", json::obj ({ { "user_id", a.id } }), a.access)),
                          juce::String ("user_id"));
            expectEquals (errField (x.call ("POST", "/rooms/r_missing/kick", json::obj ({ { "user_id", "" } }), a.access)), juce::String ("user_id"),
                          "user_id 를 방보다 먼저 본다");

            // 5.6 퇴장은 언제나 204
            expectEquals (x.call ("POST", "/rooms/r_f_1/leave", {}, a.access).status, 204, "멤버가 아님");
            expectEquals (x.call ("POST", "/rooms/r_missing/leave", {}, a.access).status, 204, "없는 방");

            // 5.3 방 만들기: capacity 빠짐, 비공개 방의 entry_mode: null, 제어 문자
            expectEquals (errField (x.call ("POST", "/rooms", json::obj ({ { "type", "private" }, { "title", "t" } }), a.access)),
                          juce::String ("capacity"));
            expectEquals (errField (x.call ("POST", "/rooms", json::obj ({ { "type", "private" }, { "title", "t" }, { "capacity", 3 }, { "entry_mode", juce::var() } }), a.access)),
                          juce::String ("entry_mode"));
            expectEquals (errField (x.call ("POST", "/rooms", json::obj ({ { "type", "private" }, { "title", "a\tb" }, { "capacity", 3 } }), a.access)),
                          juce::String ("title"));

            // 8장 차단
            expectEquals (errField (x.call ("POST", "/blocks", json::obj ({ { "user_id", a.id } }), a.access)), juce::String ("user_id"));
            // 7.2 빈 아이디
            expectEquals (errField (x.call ("POST", "/friends/requests", json::obj ({ { "username", " " } }), a.access)), juce::String ("username"));
            // 3장
            expectEquals (errField (x.call ("POST", "/auth/login", json::obj ({ { "email", "" }, { "password", "x" } }))), juce::String ("email"));
            expectEquals (errField (x.call ("POST", "/auth/refresh", json::obj ({ { "refresh_token", "" } }))), juce::String ("refresh_token"));
            expectEquals (x.call ("POST", "/auth/resend-verification", {}, a.access).status, 204, "인증한 계정은 아무것도 하지 않는다");
            expectEquals (errField (x.call ("POST", "/me/email", json::obj ({ { "new_email", "USER_A@example.com" }, { "password", "abcd123!" } }), a.access)),
                          juce::String ("new_email"), "지금 주소와 같음");
            expectEquals (errField (x.call ("POST", "/auth/signup", json::obj ({ { "email", "a<b@example.com" }, { "username", "zz_zz" },
                                                                               { "password", "abcd123!" }, { "agreed_terms", false } }))),
                          juce::String ("email"), "email 을 agreed_terms 보다 먼저 본다");
        }

        beginTest ("sessions: refresh retires the old access token, logout ends one session, password change ends the others");
        {
            Raw x;
            auto a = x.make ("user_a");
            auto login = x.call ("POST", "/auth/login", json::obj ({ { "email", "user_a@example.com" }, { "password", "abcd123!" } }));
            const auto access2 = login.body["access_token"].toString(), refresh2 = login.body["refresh_token"].toString();

            auto rot = x.call ("POST", "/auth/refresh", json::obj ({ { "refresh_token", a.refresh } }));
            expectEquals (rot.status, 200);
            expectEquals (x.call ("GET", "/me", {}, a.access).status, 401, "회전하면 옛 access 토큰은 바로 무효");
            a.access = rot.body["access_token"].toString();
            a.refresh = rot.body["refresh_token"].toString();
            expectEquals (x.call ("GET", "/me", {}, a.access).status, 200);

            expectEquals (x.call ("POST", "/auth/logout", json::obj ({ { "refresh_token", "rt_unknown" } }), a.access).status, 204);
            expectEquals (x.call ("GET", "/me", {}, a.access).status, 200, "모르는 토큰으로 로그아웃하면 아무것도 하지 않는다");
            expectEquals (x.call ("POST", "/auth/logout", json::obj ({ { "refresh_token", refresh2 } }), a.access).status, 204);
            expectEquals (x.call ("GET", "/me", {}, access2).status, 401, "그 세션의 access 토큰도 무효");
            expectEquals (x.call ("GET", "/me", {}, a.access).status, 200, "다른 세션은 그대로");

            // 비밀번호 변경: 지금 세션만 남고 나머지는 폐기 (실시간 연결이 다른 세션이면 4001)
            auto login3 = x.call ("POST", "/auth/login", json::obj ({ { "email", "user_a@example.com" }, { "password", "abcd123!" } }));
            Raw::Account other { a.id, a.username, login3.body["access_token"].toString(), login3.body["refresh_token"].toString() };
            x.connect (other);
            expectEquals (x.call ("POST", "/me/password", json::obj ({ { "current_password", "abcd123!" }, { "new_password", "efgh456!" } }), a.access).status, 204);
            x.clock.advance (50);
            expect (! x.closes.empty() && x.closes.back() == 4001);
            expectEquals (x.call ("GET", "/me", {}, other.access).status, 401);
            expectEquals (x.call ("GET", "/me", {}, a.access).status, 200);
        }

        beginTest ("refresh: a token discarded by the grace reissue counts as reuse (all sessions end)");
        {
            Raw x;
            auto a = x.make ("user_a");
            auto first = x.call ("POST", "/auth/refresh", json::obj ({ { "refresh_token", a.refresh } }));       // 응답을 못 받았다고 치자
            auto again = x.call ("POST", "/auth/refresh", json::obj ({ { "refresh_token", a.refresh } }));       // 유예: 새로 발급
            expectEquals (again.status, 200);
            const auto discarded = first.body["refresh_token"].toString();
            expectEquals (x.call ("POST", "/auth/refresh", json::obj ({ { "refresh_token", discarded } })).status, 401);
            expectEquals (x.call ("POST", "/auth/refresh", json::obj ({ { "refresh_token", again.body["refresh_token"].toString() } })).status, 401,
                          "도용으로 보고 모든 세션 폐기");
        }

        beginTest ("account deletion: email reserved, login cancels deletion; friends see friend.removed");
        {
            Raw x;
            auto a = x.make ("user_a"), b = x.make ("user_b");
            x.befriend (a, b);
            x.connect (b);
            x.messages.clear();
            expectEquals (x.call ("DELETE", "/me", {}, a.access).status, 204);
            x.clock.advance (50);
            expect (x.received ("friend.removed"));
            expectEquals (errCode (x.call ("POST", "/auth/signup", json::obj ({ { "email", "user_a@example.com" }, { "username", "other_a" },
                                                                              { "password", "abcd123!" }, { "agreed_terms", true } }))),
                          juce::String ("email_taken"), "유예 중에는 주소를 예약");
            auto back = x.call ("POST", "/auth/login", json::obj ({ { "email", "user_a@example.com" }, { "password", "abcd123!" } }));
            expectEquals (back.status, 200, "다시 로그인하면 탈퇴 취소");
            auto friends = x.call ("GET", "/friends", {}, b.access).body["friends"];
            bool found = false;
            for (int i = 0; i < friends.size(); ++i)
                found = found || friends[i]["user"]["id"].toString() == a.id;
            expect (found, "친구 관계도 돌아온다");
        }

        beginTest ("disconnect: after 30 s the member leaves the room; a reconnect inside 30 s keeps it (PLAN 5.1)");
        {
            Raw x;
            auto a = x.make ("user_a");
            x.connect (a);
            const auto room = x.createRoom (a, "private");
            x.server.devDropLink (5);
            x.clock.advance (10000);
            x.connect (a);
            x.clock.advance (FakeMeonServer::disconnectGraceMs + 1000);
            expect (x.server.isMember (room, a.id), "30초 안에 다시 붙음");
            x.server.devDropLink (0);
            x.clock.advance (FakeMeonServer::disconnectGraceMs + 1000);
            expect (! x.server.isMember (room, a.id), "30초가 지나면 퇴장");
            x.server.devCloseLink (4000);   // 이미 끊김: 아무 일 없음
        }

        beginTest ("join: re-request inside 5 min returns the old request_until even when another room was asked in between");
        {
            Raw x;
            auto a = x.make ("user_a");
            auto r1 = x.call ("POST", "/rooms/r_f_2/join", {}, a.access);
            expectEquals (r1.status, 202);
            x.clock.advance (1000);
            expectEquals (x.call ("POST", "/rooms/r_f_5/join", {}, a.access).status, 202);
            x.clock.advance (1000);
            auto again = x.call ("POST", "/rooms/r_f_2/join", {}, a.access);
            expectEquals (again.status, 202);
            expectEquals (again.body["request_until"].toString(), r1.body["request_until"].toString());
            auto view = x.call ("GET", "/rooms/r_f_2", {}, a.access);
            expectEquals (view.body["my_join_request_until"].toString(), r1.body["request_until"].toString(), "방마다 따로 남는다");
        }

        beginTest ("ws: first message must be auth; build missing is outdated");
        {
            Raw x;
            auto a = x.make ("user_a");
            x.server.open (&x);
            x.clock.advance (20);
            x.server.send (json::obj ({ { "type", "ping" } }));
            x.clock.advance (50);
            expect (x.received ("error") && ! x.closes.empty() && x.closes.back() == 1008);
            x.messages.clear();
            x.server.open (&x);
            x.clock.advance (20);
            x.server.send (json::obj ({ { "type", "auth" }, { "access_token", a.access } }));
            x.clock.advance (50);
            expect (! x.messages.empty() && x.messages.front()["code"].toString() == "client_outdated");
        }
    }
};

static FakeServerV05Tests fakeServerV05Tests;

} // namespace meon::core
