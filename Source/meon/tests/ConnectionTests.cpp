// 연결 끊김 표시: ConnectionWatch 규칙(2초, 다시 붙음, 4000·4001, 방이 사라짐), Realtime 의 답 없는 ping, 가짜 서버로 전체 흐름.
#include "../core/api/MeonApi.h"
#include "../core/api/Realtime.h"
#include "../core/api/ApiJson.h"
#include "../core/api/fake/FakeMeonServer.h"
#include "../core/state/AppStore.h"
#include "../core/state/ConnectionWatch.h"

namespace meon::core
{

namespace
{
    struct WatchLog : ConnectionWatch::Listener
    {
        std::vector<bool> shown;
        int restored = 0;
        void connectionShownChanged (bool s) override { shown.push_back (s); }
        void connectionRestored() override { ++restored; }
    };

    /** 보낸 것을 세는 실시간 전송 (서버 답은 손으로) */
    struct CountingLink : RealtimeTransport
    {
        Sink* sink = nullptr;
        int opens = 0, closes = 0, pings = 0;
        void open (Sink* s) override { sink = s; ++opens; }
        void send (const juce::var& m) override { if (m["type"].toString() == "ping") ++pings; }
        void close() override { ++closes; }
        void hello()
        {
            Hello h;
            h.me.id = "u_1";
            h.me.username = "a";
            sink->transportMessage (json::obj ({ { "type", "hello" }, { "seq", 1 }, { "data", json::encode (h) } }));
        }
    };

    /** MeonCore 와 같은 연결: 서버 실시간 연결 상태 → 표시, "방이 사라짐" 알림 → 다시 연결 토스트 대신 */
    struct Wiring : AppStore::Listener
    {
        AppStore& store;
        ConnectionWatch& watch;
        std::vector<Notice::Kind> notices;
        Wiring (AppStore& s, ConnectionWatch& w) : store (s), watch (w) { store.addListener (this); }
        ~Wiring() override { store.removeListener (this); }
        void stateChanged (StateKey k) override
        {
            if (k == StateKey::link) watch.update (ConnectionWatch::inputFor (store.state().link, false));
        }
        void notice (const Notice& n) override
        {
            notices.push_back (n.kind);
            if (n.kind == Notice::Kind::roomLostWhileDisconnected) watch.roomLost();
        }
    };

    FakeMeonServer::Options fakeOptions()
    {
        FakeMeonServer::Options o;
        o.latencyMs = 10;
        o.scripts = false;
        o.seed = 99;
        return o;
    }

    /** 가짜 서버 + 앱 쪽 (MeonApi·Realtime·AppStore·ConnectionWatch) */
    struct FakeRig
    {
        ManualScheduler clock;
        FakeMeonServer server { clock, fakeOptions() };
        MeonApi api { server, 0 };
        Realtime rt { server, clock, 0 };
        AppStore store;
        ConnectionWatch watch { clock };
        Wiring wiring { store, watch };
        WatchLog log;

        FakeRig()
        {
            rt.addListener (&store);
            watch.addListener (&log);
        }
        ~FakeRig() { watch.removeListener (&log); rt.removeListener (&store); }

        template <typename T, typename Fn>
        Result<T> run (Fn&& call)
        {
            std::optional<Result<T>> out;
            call ([&out] (Result<T> r) { out.emplace (std::move (r)); });
            clock.advance (50);
            return out.has_value() ? *out : Result<T> (ApiError::of (ErrorCode::timeout));
        }

        /** 가입 → 인증 → 프로필 → 실시간 연결 → 비공개 방 */
        juce::String signUpAndEnterRoom()
        {
            run<AuthResult> ([&] (auto cb) { api.signup ("c@example.com", "conn_user", "abcd123!", true, cb); });
            run<Me> ([&] (auto cb) { api.verifyEmail (server.getLastMailedCode(), cb); });
            MePatch p;
            p.nickname = juce::String::fromUTF8 ("연결");
            p.part = juce::String ("keys");
            run<Me> ([&] (auto cb) { api.patchMe (p, cb); });
            rt.connect ([this] { return api.getTokens().accessToken; });
            clock.advance (100);
            CreateRoom c;
            c.type = "private";
            c.title = "net";
            auto room = run<RoomDetail> ([&] (auto cb) { api.createRoom (c, cb); });
            if (room.ok()) store.setCurrentRoom (room.get());
            return room.ok() ? room.get().id : juce::String();
        }
    };
}

class ConnectionTests : public juce::UnitTest
{
public:
    ConnectionTests() : juce::UnitTest ("Connection lost indicator", "Connection") {}

    void runTest() override
    {
        using In = ConnectionWatch::Input;
        using L = Realtime::Link;

        beginTest ("inputFor: reconnecting or jam server down = lost; replaced / offline = stopped (4000·4001·logout)");
        {
            expect (ConnectionWatch::inputFor (L::reconnecting, false) == In::lost);
            expect (ConnectionWatch::inputFor (L::online, true) == In::lost, "합주 서버(AOO)만 끊겨도");
            expect (ConnectionWatch::inputFor (L::online, false) == In::connected);
            expect (ConnectionWatch::inputFor (L::connecting, false) == In::connected, "처음 연결 중은 표시하지 않는다");
            expect (ConnectionWatch::inputFor (L::replaced, true) == In::stopped);
            expect (ConnectionWatch::inputFor (L::offline, false) == In::stopped);
        }

        beginTest ("watch: under 2 s nothing, from 2 s shown, back -> hidden at once and one 'reconnected'");
        {
            ManualScheduler clock;
            ConnectionWatch w (clock);
            WatchLog log;
            w.addListener (&log);

            w.update (In::lost);
            clock.advance (1900);
            w.update (In::connected);
            clock.advance (5000);
            expect (log.shown.empty() && log.restored == 0, "2초 안에 다시 붙으면 깜빡이지 않는다");

            w.update (In::lost);
            clock.advance (1999);
            expect (! w.isShown());
            clock.advance (1);
            expect (w.isShown(), "2초가 지나면 띄운다");
            w.update (In::lost);   // 같은 끊김이 이어짐
            clock.advance (10000);
            expectEquals ((int) log.shown.size(), 1);
            w.update (In::connected);
            expect (! w.isShown(), "다시 붙으면 바로 내린다");
            expectEquals (log.restored, 0, "토스트는 같은 차례의 hello 를 본 뒤");
            clock.advance (0);
            expectEquals (log.restored, 1, "\"다시 연결됐어요\"");
            w.removeListener (&log);
        }

        beginTest ("watch: 4000 / 4001 (stopped) hides without a toast; a stop from the start shows nothing");
        {
            ManualScheduler clock;
            ConnectionWatch w (clock);
            WatchLog log;
            w.addListener (&log);
            w.update (In::lost);
            clock.advance (3000);
            expect (w.isShown());
            w.update (In::stopped);
            clock.advance (100);
            expect (! w.isShown());
            expectEquals (log.restored, 0, "일부러 끊긴 경우는 다시 연결 토스트가 없다");
            log.shown.clear();
            w.update (In::stopped);
            clock.advance (5000);
            expect (log.shown.empty(), "끊김으로 보지 않는다");
            w.removeListener (&log);
        }

        beginTest ("watch: room lost on reconnect replaces 'reconnected'");
        {
            ManualScheduler clock;
            ConnectionWatch w (clock);
            WatchLog log;
            w.addListener (&log);
            w.update (In::lost);
            clock.advance (40000);
            w.update (In::connected);
            w.roomLost();   // 같은 차례의 hello: current_room 이 null
            clock.advance (0);
            expectEquals (log.restored, 0);
            // 다음 끊김은 다시 평소대로
            w.update (In::lost);
            clock.advance (3000);
            w.update (In::connected);
            clock.advance (0);
            expectEquals (log.restored, 1);
            w.removeListener (&log);
        }

        beginTest ("Realtime: a ping with no answer for 6 s closes and reconnects; checkNow pings at once, at most every 10 s");
        {
            ManualScheduler clock;
            CountingLink link;
            Realtime rt (link, clock, 0);
            rt.connect ([] { return juce::String ("t"); });
            link.sink->transportOpened();
            link.hello();
            expect (rt.getLink() == L::online);

            clock.advance (Realtime::heartbeatMs);
            expectEquals (link.pings, 1);
            link.sink->transportMessage (json::obj ({ { "type", "pong" } }));
            clock.advance (Realtime::pongTimeoutMs + 100);
            expectEquals (link.closes, 0, "답이 오면 그대로");

            rt.checkNow();
            expectEquals (link.pings, 2, "의심되면 바로 ping");
            rt.checkNow();
            expectEquals (link.pings, 2, "답을 기다리는 동안은 다시 보내지 않는다");
            clock.advance (Realtime::pongTimeoutMs - 100);
            expectEquals (link.closes, 0);
            clock.advance (200);
            expectEquals (link.closes, 1, "6초 동안 답이 없으면 닫는다");
            expect (rt.getLink() == L::reconnecting);
            clock.advance (1000);
            expectEquals (link.opens, 2, "그리고 다시 연다");
            link.sink->transportOpened();
            link.hello();
            rt.checkNow();
            expectEquals (link.pings, 2, "10초 안에 다시 확인하지 않는다");
        }

        beginTest ("fake server: drop under 2 s -> nothing; drop 5 s -> shown, back in the room + 'reconnected'");
        {
            FakeRig r;
            const auto roomId = r.signUpAndEnterRoom();
            r.server.devDropLink (1);   // 1초 뒤 다시 붙음
            r.clock.advance (1900);
            expect (r.rt.getLink() == L::online);
            r.clock.advance (5000);
            expect (r.log.shown.empty() && r.log.restored == 0, "짧은 끊김은 깜빡이지 않는다");

            r.server.devDropLink (5);   // 1·3초 시도는 거절, 7초에 붙음
            r.clock.advance (2100);
            expect (r.watch.isShown(), "2초가 지나면 띄운다");
            r.clock.advance (6000);
            expect (r.rt.getLink() == L::online);
            expect (! r.watch.isShown(), "다시 붙으면 내린다");
            expectEquals (r.log.restored, 1, "\"다시 연결됐어요\"");
            expect (r.store.state().inRoom() && r.server.isMember (roomId, r.server.meId()), "30초 안이라 방에 그대로");
        }

        beginTest ("fake server: 4000 and 4001 show nothing");
        {
            for (int code : { 4000, 4001 })
            {
                FakeRig r;
                r.signUpAndEnterRoom();
                r.server.devCloseLink (code);
                r.clock.advance (10000);
                expect (r.log.shown.empty(), juce::String (code));
                expectEquals (r.log.restored, 0);
            }
        }

        beginTest ("fake server: cut over 30 s -> server removes me; on reconnect home + 'left the room' toast, no 'reconnected'");
        {
            FakeRig r;
            const auto roomId = r.signUpAndEnterRoom();
            expect (r.store.state().inRoom());
            r.server.devDropLink (60);
            r.clock.advance (2500);
            expect (r.watch.isShown());
            r.clock.advance (35000);
            expect (! r.server.isMember (roomId, r.server.meId()), "끊긴 지 30초가 지나 서버가 방에서 내보냄");
            expect (r.store.state().inRoom(), "끊겨 있는 동안 앱은 아직 모른다");
            r.clock.advance (60000);   // 다시 붙음 (재연결 간격 1·2·4…30초)
            expect (r.rt.getLink() == L::online);
            expect (! r.watch.isShown());
            expect (! r.store.state().inRoom(), "hello 의 current_room 이 null → 홈");
            expect (std::find (r.wiring.notices.begin(), r.wiring.notices.end(), Notice::Kind::roomLostWhileDisconnected) != r.wiring.notices.end(),
                    "\"연결이 오래 끊겨서 방에서 나왔어요\"");
            expectEquals (r.log.restored, 0, "\"다시 연결됐어요\" 는 띄우지 않는다");
        }
    }
};

static ConnectionTests connectionTests;

} // namespace meon::core
