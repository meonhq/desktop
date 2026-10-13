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
            expectEquals ((int) h.store.state().joinRequests.size(), 1);   // 수락한 요청은 목록에서 빠진다
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

} // namespace meon::core
