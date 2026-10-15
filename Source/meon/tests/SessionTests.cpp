// R11 PR2: 세션 규칙.
//   닫는 코드 (API 10.1): 4000 다시 접속하지 않고 "다른 곳에서 접속", 4001 로그아웃, 4002 다시 접속, 조용한 연결은 45초 뒤 다시 접속
//   강제 업데이트 (API 13장): min_build·426·client_outdated → outdated 단계, 로그인은 남김, 합주 중이면 나갈 때까지 기다림
//   refresh 를 다른 프로세스와 나눠 쓰기 (RefreshCoordinator)
#include "../core/auth/AuthController.h"
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
        void open (Sink* s) override { sink = s; ++opens; }
        void send (const juce::var&) override {}
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

        beginTest ("silent connection is closed and reopened after 45 s");
        {
            ManualScheduler clock;
            StubLink link;
            Realtime rt (link, clock, 0);
            rt.connect ([] { return juce::String ("t"); });
            link.sink->transportOpened();
            clock.advance (40000);
            expectEquals (link.closes, 0, "40초까지는 기다린다 (서버도 45초)");
            clock.advance (25000);
            expectEquals (link.closes, 1, "아무것도 오지 않으면 닫는다");
            clock.advance (1000);
            expectEquals (link.opens, 2, "그리고 다시 연다");
        }

        //==========================================================================
        beginTest ("4000 from the server: replaced step, room released locally, reconnect here");
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
            expect (! r.store.state().inRoom(), "이 기기는 방에서 손을 뗀다 (합주 연결을 끊게)");
            r.wait (60000);
            expect (r.rt.getLink() == Realtime::Link::replaced, "저절로 다시 붙지 않는다");
            r.auth.reconnectHere();
            r.wait();
            expect (r.auth.step() == S::ready);
            expect (r.rt.getLink() == Realtime::Link::online);
            expect (r.store.state().inRoom(), "다시 연결하면 hello 로 방에 돌아온다");
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
