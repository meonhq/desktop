// R11 PR2: 세션 규칙.
//   닫는 코드 (API 10.1): 4000 다시 접속하지 않고 "다른 곳에서 접속", 4001 로그아웃, 4002 다시 접속, 조용한 연결은 45초 뒤 다시 접속
//   강제 업데이트 (API 13장): min_build·426·client_outdated → outdated 단계, 로그인은 남김, 합주 중이면 나갈 때까지 기다림
//   refresh 를 다른 프로세스와 나눠 쓰기 (RefreshCoordinator)
#include "../core/auth/AuthController.h"
#include "../core/home/HomeController.h"
#include "../core/api/ApiJson.h"
#include "../core/api/fake/FakeMeonServer.h"
#include "MeonFixtures.h"

namespace meon::core
{

namespace
{
    /** 닫는 코드를 손으로 넣는 실시간 전송 */
    struct StubLink : RealtimeTransport
    {
        Sink* sink = nullptr;
        int opens = 0, closes = 0;
        std::vector<juce::var> sent;
        void open (Sink* s) override { sink = s; ++opens; }
        void send (const juce::var& m) override { sent.push_back (m); }
        void close() override { ++closes; }
    };

    struct RevokeWatch : Realtime::Listener
    {
        int revoked = 0;
        void sessionRevoked() override { ++revoked; }
    };

    /** 응답을 손으로 정하는 REST 전송 */
    struct StubRest : RestTransport
    {
        std::vector<HttpRequest> sent;
        std::function<HttpResponse (const HttpRequest&)> reply;
        void send (const HttpRequest& r, std::function<void (HttpResponse)> done) override
        {
            sent.push_back (r);
            done (reply ? reply (r) : HttpResponse { 500, {} });
        }
    };

    struct StubCoordinator : RefreshCoordinator
    {
        int busyFor = 0, locks = 0, unlocks = 0, retries = 0;
        juce::String stored;
        std::vector<std::function<void()>> later;
        bool tryLock() override { if (busyFor > 0) { --busyFor; return false; } ++locks; return true; }
        void unlock() override { ++unlocks; }
        juce::String storedRefreshToken() override { return stored; }
        void retryLater (int, std::function<void()> fn) override { ++retries; later.push_back (std::move (fn)); }
        void runLater() { auto l = std::move (later); later.clear(); for (auto& f : l) f(); }
    };

    juce::var tokensJson (const char* at, const char* rt)
    {
        return json::obj ({ { "access_token", at }, { "refresh_token", rt } });
    }
}

class SessionTests : public juce::UnitTest
{
public:
    SessionTests() : juce::UnitTest ("Session rules (R11)", "Auth") {}

    struct Rig
    {
        MemoryTokenStore tokens;
        ManualScheduler clock;
        FakeMeonServer server { clock, [] { FakeMeonServer::Options o; o.latencyMs = 10; o.scripts = false; return o; }() };
        MeonApi api;
        Realtime rt;
        AppStore store;
        AuthController auth;
        explicit Rig (int build) : api (server, build), rt (server, clock, build), auth (api, rt, store, tokens, clock)
        {
            server.loadWorld (juce::JSON::parse (juce::String::fromUTF8 (MeonFixtures::world_json, MeonFixtures::world_jsonSize)));
            rt.addListener (&store);
        }
        ~Rig() { rt.removeListener (&store); }
        void wait (int ms = 100) { clock.advance (ms); }

        void signUpToHome (const juce::String& name)
        {
            auth.start();
            wait();
            auth.showSignup();
            auth.signup (name + "@example.com", name, "abcd123!", true, [] (Result<Done>) {});
            wait();
            auth.verify (server.getLastMailedCode(), [] (Result<Done>) {});
            wait();
            auth.setNickname ("Nick", [] (Result<Done>) {});
            wait();
            auth.setPart ("keys", [] (Result<Done>) {});
            wait();
            auth.finishDiscord();
            auth.finishDone();
        }
    };

    void runTest() override
    {
        using S = AuthController::Step;

        //==========================================================================
        beginTest ("auth: build, and the optional mode/os (API 10.1) when given");
        {
            ManualScheduler clock;
            StubLink link;
            Realtime rt (link, clock, 977);
            rt.setClientInfo ("app", "mac");
            rt.connect ([] { return juce::String ("t"); });
            link.sink->transportOpened();
            expect (! link.sent.empty());
            const auto a = link.sent.front();
            expectEquals (a["type"].toString(), juce::String ("auth"));
            expectEquals (a["access_token"].toString(), juce::String ("t"));
            expectEquals ((int) a["build"], 977);
            expectEquals (a["mode"].toString(), juce::String ("app"));
            expectEquals (a["os"].toString(), juce::String ("mac"));

            StubLink link2;
            Realtime rt2 (link2, clock, 1);
            rt2.connect ([] { return juce::String ("t"); });
            link2.sink->transportOpened();
            expect (! link2.sent.empty());
            expect (! link2.sent.front().getDynamicObject()->hasProperty ("mode"), "정하지 않으면 보내지 않는다");
            expect (! link2.sent.front().getDynamicObject()->hasProperty ("os"));
        }

        beginTest ("4000: no reconnect, link replaced");
        {
            ManualScheduler clock;
            StubLink link;
            Realtime rt (link, clock, 0);
            rt.connect ([] { return juce::String ("t"); });
            link.sink->transportOpened();
            link.sink->transportClosed (Realtime::closeReplaced);
            expect (rt.getLink() == Realtime::Link::replaced);
            clock.advance (120000);
            expectEquals (link.opens, 1, "다시 접속하지 않아야 함");
            rt.connect ([] { return juce::String ("t"); });
            expectEquals (link.opens, 2, "사용자가 다시 연결하면 연다");
        }

        beginTest ("4001: session revoked, no reconnect");
        {
            ManualScheduler clock;
            StubLink link;
            Realtime rt (link, clock, 0);
            RevokeWatch watch;
            rt.addListener (&watch);
            rt.connect ([] { return juce::String ("t"); });
            link.sink->transportOpened();
            link.sink->transportClosed (Realtime::closeSessionRevoked);
            expectEquals (watch.revoked, 1);
            expect (rt.getLink() == Realtime::Link::offline);
            clock.advance (120000);
            expectEquals (link.opens, 1);
            rt.removeListener (&watch);
        }

        beginTest ("4002 and plain drops reconnect with backoff");
        {
            ManualScheduler clock;
            StubLink link;
            Realtime rt (link, clock, 0);
            rt.connect ([] { return juce::String ("t"); });
            link.sink->transportOpened();
            link.sink->transportClosed (Realtime::closeSlow);
            expect (rt.getLink() != Realtime::Link::offline && rt.getLink() != Realtime::Link::replaced);
            clock.advance (1000);
            expectEquals (link.opens, 2);
            link.sink->transportClosed (0);
            clock.advance (2000);
            expectEquals (link.opens, 3);
        }

        beginTest ("silent connection: the 20 s ping gets no answer for 6 s -> closed and reopened");
        {
            ManualScheduler clock;
            StubLink link;
            Realtime rt (link, clock, 0);
            rt.connect ([] { return juce::String ("t"); });
            link.sink->transportOpened();
            clock.advance (Realtime::heartbeatMs + Realtime::pongTimeoutMs - 100);
            expectEquals (link.closes, 0, "ping 을 보낸 뒤 6초까지는 기다린다");
            clock.advance (200);
            expectEquals (link.closes, 1, "답이 없으면 닫는다 (와이파이가 조용히 끊긴 경우)");
            clock.advance (1000);
            expectEquals (link.opens, 2, "그리고 다시 연다");
        }

        //==========================================================================
        beginTest ("4000 from the server: replaced step, room kept (no leave), reconnect here");
        {
            Rig r (0);
            r.signUpToHome ("dup_user");
            expect (r.auth.step() == S::ready);
            r.wait();
            CreateRoom cr;
            cr.type = "private";
            cr.title = "x";
            r.api.createRoom (cr, [&] (Result<RoomDetail> x) { if (x.ok()) r.store.setCurrentRoom (x.get()); });
            r.wait();
            expect (r.store.state().inRoom());
            r.server.devCloseLink (4000);
            expect (r.auth.step() == S::replaced);
            expect (r.store.state().inRoom(), "방은 그대로 (leave 를 부르지 않는다. 합주 연결만 JamSession 이 끊는다)");
            r.wait (60000);
            expect (r.rt.getLink() == Realtime::Link::replaced, "저절로 다시 붙지 않는다");
            r.auth.reconnectHere();
            r.wait();
            expect (r.auth.step() == S::ready);
            expect (r.rt.getLink() == Realtime::Link::online);
            expect (r.store.state().inRoom(), "다시 연결하면 hello 로 방에 돌아온다");
        }

        beginTest ("Normal quit calls leave (no return); crash returns by hello; 4000 skips leave");
        {
            Rig r (0);
            r.signUpToHome ("quit_user");
            r.wait();
            HomeController home (r.api, r.rt, r.store, r.clock);
            CreateRoom cr;
            cr.type = "private";
            cr.title = "x";
            home.createRoom (cr, [] (Result<RoomDetail>) {});
            r.wait();
            expect (r.store.state().inRoom());
            const auto roomId = r.store.state().currentRoom->id;

            // 다시 켠 앱: 새 상태(AppStore)로 다시 연결해 hello 를 받는다
            auto restart = [&] (AppStore& fresh)
            {
                r.rt.disconnect();
                r.wait();
                r.rt.addListener (&fresh);
                r.rt.connect ([&] { return r.api.getTokens().accessToken; });
                r.wait();
                r.rt.removeListener (&fresh);
            };

            // 강제 종료·크래시: 나가기 없이 연결만 끊긴다 → 30초 안에 다시 켜면 hello 로 같은 방
            {
                AppStore fresh;
                restart (fresh);
                expect (fresh.state().inRoom() && fresh.state().currentRoom->id == roomId, "크래시 뒤 다시 켜면 같은 방");
            }

            // 4000 으로 끊긴 동안 정상 종료: 다른 기기가 방에 남아야 하므로 나가기를 보내지 않는다
            r.server.devCloseLink (4000);
            expect (r.rt.getLink() == Realtime::Link::replaced);
            expect (home.leaveOnQuit (1000) == HomeController::QuitLeave::skipped);
            {
                AppStore fresh;
                restart (fresh);
                expect (fresh.state().inRoom(), "4000 뒤 종료는 방을 그대로 둔다");
            }
            r.wait();
            expect (r.store.state().inRoom());

            // 정상 종료: 나가기를 보내고 응답을 기다린다 → 다시 켜도 방으로 돌아가지 않는다
            expect (home.leaveOnQuit (1000) == HomeController::QuitLeave::left);
            {
                AppStore fresh;
                restart (fresh);
                expect (! fresh.state().inRoom(), "정상 종료 뒤 다시 켜면 홈");
            }

            // 방 밖이면 아무것도 보내지 않는다
            AppStore empty;
            HomeController idle (r.api, r.rt, empty, r.clock);
            expect (idle.leaveOnQuit (1000) == HomeController::QuitLeave::notInRoom);
        }

        beginTest ("Normal quit: leave not delivered -> next start does not re-enter that room (once)");
        {
            // 응답 없음 (기본 sendBlocking) → failed
            StubRest rest;
            MeonApi api (rest, 0);
            ManualScheduler clock;
            StubLink link;
            Realtime rt (link, clock, 0);
            AppStore store;
            HomeController home (api, rt, store, clock);
            RoomDetail room;
            room.id = "r_left";
            store.setCurrentRoom (room);
            expect (home.leaveOnQuit (100) == HomeController::QuitLeave::failed);

            // 다음 실행: 표시된 방이 hello 로 오면 들어가지 않고 다시 나가기를 부탁한다
            AppStore next;
            juce::StringArray releft;
            next.onRoomLeftOnQuit = [&] (const juce::String& id) { releft.add (id); };
            next.setRoomLeftOnQuit ("r_left");
            JoinRequest jr;
            jr.id = "jr_1";
            Hello h;
            h.me.id = "u_me";
            h.currentRoom = room;
            h.joinRequests = { jr };
            next.hello (h);
            expect (! next.state().inRoom(), "표시된 방에는 들어가지 않는다");
            expect (next.state().joinRequests.empty());
            expectEquals (releft.joinIntoString (","), juce::String ("r_left"));
            // 한 번만 본다: 그 뒤의 hello 는 그대로 (다른 기기가 다시 들어간 경우 등)
            next.hello (h);
            expect (next.state().inRoom());
            expectEquals (releft.size(), 1);

            // 다른 방이면 그대로 들어간다
            AppStore other;
            other.setRoomLeftOnQuit ("r_left");
            RoomDetail b;
            b.id = "r_other";
            h.currentRoom = b;
            other.hello (h);
            expect (other.state().inRoom() && other.state().currentRoom->id == "r_other");
        }

        beginTest ("4001 from the server: logged out, token cleared");
        {
            Rig r (0);
            r.signUpToHome ("revoked_user");
            r.wait();
            expect (r.tokens.value.isNotEmpty());
            r.server.devCloseLink (4001);
            r.wait();
            expect (r.auth.step() == S::login);
            expect (r.tokens.value.isEmpty());
        }

        //==========================================================================
        beginTest ("min_build above my build: outdated, token kept; waits while in a room");
        {
            Rig r (50);
            r.signUpToHome ("old_user");
            r.wait();
            expect (r.auth.step() == S::ready);
            CreateRoom cr;
            cr.type = "private";
            cr.title = "x";
            r.api.createRoom (cr, [&] (Result<RoomDetail> x) { if (x.ok()) r.store.setCurrentRoom (x.get()); });
            r.wait();
            const auto savedToken = r.tokens.value;
            r.server.devSetConfig (60, 60);
            r.wait();
            expect (r.auth.isOutdated());
            expect (r.auth.step() == S::ready, "합주 중에는 나갈 때까지 기다린다");
            r.store.setCurrentRoom (std::nullopt);
            expect (r.auth.step() == S::outdated);
            r.auth.showLogin();
            expect (r.auth.step() == S::outdated, "강제 업데이트 화면에서는 다른 단계로 가지 않는다");
            expectEquals (r.tokens.value, savedToken, "로그인은 지우지 않는다");
        }

        beginTest ("start with a saved login on an outdated build: outdated, no refresh, token kept");
        {
            Rig first (50);
            first.signUpToHome ("old_start");
            first.wait();
            const auto saved = first.server.saveState();
            const auto token = first.tokens.value;

            Rig r (50);
            r.server.loadState (saved);
            r.tokens.save (token);
            r.server.devSetConfig (60, 60);
            r.auth.start();
            r.wait();
            expect (r.auth.step() == S::outdated);
            expectEquals (r.tokens.value, token);
        }

        beginTest ("426 on refresh keeps the login and reports outdated");
        {
            StubRest rest;
            rest.reply = [] (const HttpRequest&) { return HttpResponse { 426, json::encodeError (ApiError::of (ErrorCode::clientOutdated)) }; };
            MeonApi api (rest, 50);
            int outdated = 0, lost = 0;
            api.onClientOutdated = [&] { ++outdated; };
            api.onSessionLost = [&] { ++lost; };
            api.setTokens ({ "a", "r1" });
            std::optional<ErrorCode> code;
            api.refresh ([&] (Result<Tokens> t) { if (! t.ok()) code = t.code(); });
            expect (code == ErrorCode::clientOutdated);
            expectEquals (outdated, 1);
            expectEquals (api.getTokens().refreshToken, juce::String ("r1"), "로그인은 남긴다");
            expectEquals (lost, 0);
        }

        //==========================================================================
        beginTest ("refresh uses the token another process stored, under the lock");
        {
            StubRest rest;
            rest.reply = [] (const HttpRequest&) { return HttpResponse { 200, tokensJson ("a2", "r3") }; };
            MeonApi api (rest, 1);
            StubCoordinator co;
            co.stored = "r2";   // 다른 프로세스가 r1 → r2 로 바꿔 저장해 둠
            api.setRefreshCoordinator (&co);
            api.setTokens ({ "a1", "r1" });
            bool ok = false;
            api.refresh ([&] (Result<Tokens> t) { ok = t.ok(); });
            expect (ok);
            expectEquals ((int) rest.sent.size(), 1);
            expectEquals (rest.sent[0].body["refresh_token"].toString(), juce::String ("r2"), "저장소의 지금 토큰으로 갱신");
            expectEquals (api.getTokens().refreshToken, juce::String ("r3"));
            expectEquals (co.locks, 1);
            expectEquals (co.unlocks, 1);
        }

        beginTest ("refresh waits while another process holds the lock");
        {
            StubRest rest;
            rest.reply = [] (const HttpRequest&) { return HttpResponse { 200, tokensJson ("a2", "r3") }; };
            MeonApi api (rest, 1);
            StubCoordinator co;
            co.busyFor = 2;
            co.stored = "r1";
            api.setRefreshCoordinator (&co);
            api.setTokens ({ "a1", "r1" });
            bool ok = false;
            api.refresh ([&] (Result<Tokens> t) { ok = t.ok(); });
            expectEquals ((int) rest.sent.size(), 0, "잠겨 있으면 보내지 않는다");
            co.runLater();
            expectEquals ((int) rest.sent.size(), 0);
            co.runLater();
            expectEquals ((int) rest.sent.size(), 1);
            expect (ok);
            expectEquals (co.retries, 2);
            expectEquals (co.unlocks, 1);
        }

        beginTest ("delete account sends no body (API 4.5, v0.5)");
        {
            StubRest rest;
            rest.reply = [] (const HttpRequest&) { return HttpResponse { 204, {} }; };
            MeonApi api (rest, 1);
            api.setTokens ({ "a", "r" });
            bool ok = false;
            api.deleteAccount ([&] (Result<Done> d) { ok = d.ok(); });
            expect (ok);
            expectEquals (rest.sent.back().method, juce::String ("DELETE"));
            expect (rest.sent.back().body.isVoid());
            expect (! api.hasSession());
        }
    }
};

static SessionTests sessionTests;

} // namespace meon::core
