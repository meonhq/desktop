// 합주 로직: PeerState, SilenceDetector, ChatChannel, JamSession (가짜 엔진 + 가짜 서버)
#include "../core/jam/JamSession.h"
#include "../core/jam/RttReporter.h"
#include "../core/jam/HomeMonitorGate.h"
#include "../core/auth/TokenStore.h"
#include "../core/api/MeonApi.h"
#include "../core/api/Realtime.h"
#include "MeonFixtures.h"

namespace meon::core
{

namespace
{
    /** 메모리 안의 엔진: 서버·그룹·피어를 손으로 움직인다 */
    struct FakeEngine : Engine
    {
        juce::ListenerList<Listener> ls;
        bool connected = false, sendMuted = false, monitorMuted = false, selfMon = false;
        int selfPan = 0;
        juce::String user, group;
        juce::StringArray allowed;
        bool allowEnabled = false;
        std::vector<EnginePeer> peerList;
        std::vector<EngineChat> sent;
        float inLevel = 0.0f;
        int inputChannels = 1;
        BufferMode mode = BufferMode::latency;
        float masterDb = 0.0f, sendGain = 0.0f;
        std::map<juce::String, bool> recv;
        bool file = false, filePlaying = false;

        bool loadFile (const juce::File&) override { file = true; filePlaying = false; return true; }
        void closeFile() override                  { file = filePlaying = false; }
        bool hasFile() const override              { return file; }
        bool isFilePlaying() const override        { return filePlaying; }
        void setFilePlaying (bool p) override      { filePlaying = file && p; }

        void addListener (Listener* l) override { ls.add (l); }
        void removeListener (Listener* l) override { ls.remove (l); }
        void applyDefaults() override {}
        int connects = 0, joins = 0;
        juce::String lastHost, lastPassword, lastGroupPassword;
        int lastPort = 0;
        bool connect (const juce::String& h, int port, const juce::String& u, const juce::String& pw) override
        {
            ++connects; user = u; lastHost = h; lastPort = port; lastPassword = pw;
            return true;
        }
        void finishConnect() { connected = true; ls.call ([] (Listener& l) { l.engineConnected (true, {}); }); }
        void failConnect()   { connected = false; ls.call ([] (Listener& l) { l.engineConnected (false, "refused"); }); }
        /** 서버만 끊김: 실제 엔진처럼 그룹 이름은 지우지 않는다 (직접 연결된 상대도 남는다) */
        void serverLost()    { connected = false; ls.call ([] (Listener& l) { l.engineDisconnected ("lost"); }); }
        void disconnect() override { connected = false; group = {}; peerList.clear(); }
        bool isConnected() const override { return connected; }
        juce::String connectedUser() const override { return user; }
        juce::String joinedGroup() const override { return group; }
        bool joinGroup (const juce::String& g, const juce::String& pw) override { ++joins; group = g; lastGroupPassword = pw; return true; }
        void finishJoin() { const auto g = group; ls.call ([g] (Listener& l) { l.engineGroupJoined (true, g, {}); }); }
        void leaveGroup (const juce::String&) override { group = {}; }
        void setAllowedPeers (const juce::StringArray& u, bool e) override
        {
            allowed = u; allowEnabled = e;
            if (e) peerList.erase (std::remove_if (peerList.begin(), peerList.end(), [&] (const EnginePeer& p) { return ! u.contains (p.user); }), peerList.end());
        }
        /** 상대가 그룹에 들어옴: 허용 목록에 있으면 연결된 피어로 */
        void peerArrives (const juce::String& u)
        {
            ls.call ([u] (Listener& l) { l.enginePeerJoined (u); });
            if (! allowEnabled || allowed.contains (u))
            {
                EnginePeer p;
                p.user = u; p.connected = true; p.recvActive = true;
                peerList.push_back (p);
            }
        }
        EnginePeer* peer (const juce::String& u) { for (auto& p : peerList) if (p.user == u) return &p; return nullptr; }
        std::vector<EnginePeer> peers() const override { return peerList; }
        void removePeer (const juce::String& u) override { peerList.erase (std::remove_if (peerList.begin(), peerList.end(), [&] (const EnginePeer& p) { return p.user == u; }), peerList.end()); }
        void setPeerReceive (const juce::String& u, bool on) override { recv[u] = on; }
        void setPeerGainDb (const juce::String&, float) override {}
        void setPeerPan (const juce::String&, int) override {}
        float peerLevel (const juce::String&, int) const override { return 0.5f; }
        void applyPeerDefaults (const juce::String&, BufferMode) override {}
        bool isSendMuted() const override { return sendMuted; }
        void setSendMuted (bool b) override { sendMuted = b; }
        void setSendGainDb (float db) override { sendGain = db; }
        void setSelfMonitor (bool b) override { selfMon = b; }
        void setSelfMonitorPan (int p) override { selfPan = p; }
        void setMasterDb (float db) override { masterDb = db; }
        void setMonitorMuted (bool b) override { monitorMuted = b; }
        void setBufferMode (BufferMode m) override { mode = m; }
        void setInputChannels (int, int c) override { inputChannels = c; }
        DeviceInfo deviceInfo() const override { return { "Windows Audio", {}, "LG HDR WQHD (HDMI)", 48000.0, 480 }; }   // 입력 장치 없음
        int inputChannelCount() const override { return inputChannels; }
        bool outputOk = true;
        bool outputAvailable() const override { return outputOk; }
        juce::String inputChannelText() const override { return "2"; }
        float inputLevel (int) const override { return inLevel; }
        float sendLevel (int) const override { return sendMuted ? 0.0f : inLevel; }
        float monitorLevel (int) const override { return 0.0f; }
        int sendChannelCount() const override { return 1; }
        juce::String audioDescription() const override { return "fake"; }
        bool sendChat (const EngineChat& c) override { sent.push_back (c); return true; }
    };
}

class JamTests : public juce::UnitTest
{
public:
    JamTests() : juce::UnitTest ("Jam", "Jam") {}

    void runTest() override
    {
        beginTest ("RTT target: room's Connection while in a room, otherwise the server config's AOO address (R11 PR3)");
        {
            const RttReporter::Target fallback { "aoo.meon.co.kr", 10999 };
            expect (RttReporter::targetFor (std::nullopt, fallback) == fallback);
            RoomDetail room;
            room.connection.aooHost = "aoo2.meon.co.kr";
            room.connection.aooPort = 11000;
            expect (RttReporter::targetFor (room, fallback) == RttReporter::Target { "aoo2.meon.co.kr", 11000 });
            room.connection.aooHost = {};   // 접속 정보가 비어 있으면 기본
            expect (RttReporter::targetFor (room, fallback) == fallback);
            RttReporter rtt ([] (int) {});
            rtt.setTarget ({ "a", 1 });   // 스레드를 시작하지 않고 대상만 바꾼다 (바로 재지 않는다)
            expect (rtt.target() == RttReporter::Target { "a", 1 });
        }

        beginTest ("PeerState: connect, 3s interrupt, resume, dropCount, 15s fail");
        {
            PeerState ps (0);
            EnginePeer p;
            p.user = "u_b"; p.connected = true; p.recvActive = true; p.packetsReceived = 10;
            expect (ps.update (100, std::nullopt, false) == PeerState::Change::none);
            expect (ps.update (200, p, false) == PeerState::Change::connected);
            p.packetsReceived = 20;
            expect (ps.update (600, p, false) == PeerState::Change::none);
            expect (ps.update (3500, p, false) == PeerState::Change::none);       // 2.9초 그대로
            expect (ps.update (3700, p, false) == PeerState::Change::interrupted); // 3초
            expectEquals (ps.dropCount(), 1);
            p.packetsReceived = 30;
            expect (ps.update (4000, p, false) == PeerState::Change::resumed);
            // 상대가 송출을 끄면(recvActive false) 끊김이 아니다
            p.recvActive = false;
            expect (ps.update (9000, p, false) == PeerState::Change::none);
            // 내가 음소거해도 끊김이 아니다
            p.recvActive = true;
            expect (ps.update (9100, p, true) == PeerState::Change::none);
            expect (ps.update (14000, p, true) == PeerState::Change::none);
            expectEquals (ps.dropCount(), 1);

            PeerState never (0);
            expect (never.update (14999, std::nullopt, false) == PeerState::Change::none);
            expect (never.update (15000, std::nullopt, false) == PeerState::Change::failed);
            expect (never.get() == PeerState::State::failed);
        }

        beginTest ("SilenceDetector: 5s, -60/-50, send mute, no input");
        {
            SilenceDetector d;
            expect (d.update (0, true, false, 1, -70.0f) == SilenceDetector::Change::none);
            expect (d.update (4900, true, false, 1, -61.0f) == SilenceDetector::Change::none);
            expect (d.update (5000, true, false, 1, -65.0f) == SilenceDetector::Change::started);
            expect (d.update (6000, true, false, 1, -55.0f) == SilenceDetector::Change::none);   // -50 을 넘어야 해제
            expect (d.update (6400, true, false, 1, -45.0f) == SilenceDetector::Change::ended);
            expectEquals (d.lastDurationMs(), (juce::int64) 1400);
            // 중간에 소리가 나면 처음부터
            d.update (7000, true, false, 1, -70.0f);
            d.update (10000, true, false, 1, -40.0f);
            expect (d.update (14000, true, false, 1, -70.0f) == SilenceDetector::Change::none);
            expect (d.update (19000, true, false, 1, -70.0f) == SilenceDetector::Change::started);
            // 송출 정지면 지우고, 다시 켜면 처음부터
            expect (d.update (19400, true, true, 1, -70.0f) == SilenceDetector::Change::ended);
            expect (d.update (19800, true, false, 1, -70.0f) == SilenceDetector::Change::none);
            expect (d.update (24700, true, false, 1, -70.0f) == SilenceDetector::Change::none);
            expect (d.update (24800, true, false, 1, -70.0f) == SilenceDetector::Change::started);
            // 입력 장치(채널)가 없으면 무음 대신 "입력 장치 없음" (버그 17: 예전에는 판정을 건너뛰어 아무것도 안 떴다)
            SilenceDetector d2;
            using K = SilenceDetector::Kind;
            expect (d2.update (0, true, false, 0, -100.0f) == SilenceDetector::Change::none);
            expect (d2.update (4900, true, false, 0, -100.0f) == SilenceDetector::Change::none);
            expect (d2.update (5000, true, false, 0, -100.0f) == SilenceDetector::Change::started);
            expect (d2.warningKind() == K::noInput);
            // 송출 정지면 지움
            expect (d2.update (6000, true, true, 0, -100.0f) == SilenceDetector::Change::ended);
            expect (d2.lastKind() == K::noInput);
            expect (d2.update (6500, true, false, 0, -100.0f) == SilenceDetector::Change::none);
            expect (d2.update (11500, true, false, 0, -100.0f) == SilenceDetector::Change::started);
            // 장치가 생기면 바로 해제하고, 무음 판정은 처음부터 (5초)
            expect (d2.update (12000, true, false, 1, -100.0f) == SilenceDetector::Change::ended);
            expect (d2.update (16900, true, false, 1, -100.0f) == SilenceDetector::Change::none);
            expect (d2.update (17000, true, false, 1, -100.0f) == SilenceDetector::Change::started);
            expect (d2.warningKind() == K::silent);
            // 무음 경고 중 장치가 빠지면 무음은 바로 해제, 5초 뒤 장치 없음
            expect (d2.update (17500, true, false, 0, -100.0f) == SilenceDetector::Change::ended);
            expect (d2.lastKind() == K::silent);
            expect (d2.update (22500, true, false, 0, -100.0f) == SilenceDetector::Change::started);
            expect (d2.warningKind() == K::noInput);
            // 방 밖이면 경고하지 않음
            SilenceDetector d3;
            expect (d3.update (0, false, false, 0, -90.0f) == SilenceDetector::Change::none);
            expect (d3.update (19000, false, false, 1, -90.0f) == SilenceDetector::Change::none);
        }

        beginTest ("ChatChannel: 1000 bytes, alone, ack, partial ack fails after 3s, resend");
        {
            ManualScheduler clock;
            std::vector<EngineChat> out;
            bool engineOk = true;
            ChatChannel ch (clock, [&] (const EngineChat& c) { out.push_back (c); return engineOk; });
            ch.begin ("G", "u_a");
            // 혼자: 엔진으로 보내지 않고 내 목록에만 (확인·실패 표시 없음, 3초가 지나도 그대로)
            expect (ch.sendText ("hi") == ChatChannel::SendResult::localOnly);
            expect (out.empty());
            expectEquals ((int) ch.getMessages().size(), 1);
            expect (ch.getMessages().back().kind == ChatChannel::Message::Kind::mine && ch.getMessages().back().text == "hi");
            expect (ch.getMessages().back().delivery == ChatChannel::Message::Delivery::none);
            clock.advance (3000);
            expect (ch.getMessages().back().delivery == ChatChannel::Message::Delivery::none);
            ch.setOthers ({ "u_b", "u_c" });
            expect (ch.sendText ("   ") == ChatChannel::SendResult::empty);
            expect (ch.sendText (juce::String::repeatedString (juce::String::fromUTF8 ("가"), 334)) == ChatChannel::SendResult::tooLong);  // 1002 B
            expect (ch.sendText (juce::String::repeatedString (juce::String::fromUTF8 ("가"), 333)) == ChatChannel::SendResult::sent);    // 999 B
            const auto id = ch.getMessages().back().id;
            expect (out.back().tags == "id:" + id);
            ch.received ({ "G", "u_b", "ack:" + id, {} });
            clock.advance (3000);
            expect (ch.getMessages().back().delivery == ChatChannel::Message::Delivery::failed);
            expect (ch.getMessages().back().waitingFor == juce::StringArray { "u_c" });
            ch.resend (id);
            ch.received ({ "G", "u_b", "ack:" + id, {} });
            ch.received ({ "G", "u_c", "ack:" + id, {} });
            expect (ch.getMessages().back().delivery == ChatChannel::Message::Delivery::delivered);
            clock.advance (3000);
            expect (ch.getMessages().back().delivery == ChatChannel::Message::Delivery::delivered);

            // 받은 메시지에는 ack 를 돌려보내고, 같은 ID 가 다시 와도 한 번만 보인다
            out.clear();
            ch.received ({ "G", "u_b", "id:XYZ", "yo" });
            ch.received ({ "G", "u_b", "id:XYZ", "yo" });
            expectEquals ((int) out.size(), 2);
            expect (out[0].tags == "ack:XYZ" && out[0].message.isEmpty());
            int others = 0;
            for (auto& m : ch.getMessages()) if (m.kind == ChatChannel::Message::Kind::other) ++others;
            expectEquals (others, 1);

            engineOk = false;
            expect (ch.sendText ("x") == ChatChannel::SendResult::failed);
        }

        beginTest ("JamSession chat: alone / scripted fake members only -> local, real member (even not connected) -> ack or fail");
        {
            ManualScheduler clock;
            FakeMeonServer::Options fo;
            fo.latencyMs = 0;
            fo.scripts = false;
            FakeMeonServer server { clock, fo };
            server.loadWorld (juce::JSON::parse (juce::String::fromUTF8 (MeonFixtures::world_json, MeonFixtures::world_jsonSize)));
            MeonApi api { server, 0 };
            Realtime rt { server, clock, 0 };
            AppStore store;
            rt.addListener (&store);
            auto run = [&] (auto&& fn) { fn(); clock.advance (50); };
            run ([&] { api.signup ("solo@example.com", "solo_keys", "abcd123!", true, [] (auto) {}); });
            run ([&] { api.verifyEmail (server.getLastMailedCode(), [] (auto) {}); });
            MePatch p; p.nickname = juce::String ("Solo"); p.part = juce::String ("keys");
            run ([&] { api.patchMe (p, [&] (Result<Me> r) { store.setMe (r.get()); }); });
            rt.connect ([&] { return api.getTokens().accessToken; });
            clock.advance (100);

            FakeEngine engine;
            JamSession::Options o;
            o.logFolder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("meon-test-logs-chat");
            o.fake = &server;
            JamSession jam (engine, store, clock, o);

            // 가짜 각본 멤버만 있는 공개 방 (u_f_doyun, u_f_eunseo): 혼자와 같다
            expect (server.isScriptedUser ("u_f_doyun") && ! server.isScriptedUser (server.meId()));
            run ([&] { api.joinRoom ("r_f_3", [&] (Result<JoinResult> r) { store.setCurrentRoom (r.get().room); }); });
            engine.finishConnect();
            engine.finishJoin();
            expect (jam.isInRoom());
            expect (engine.allowed.contains ("u_f_doyun"));   // 엔진 허용 목록에는 그대로 (오디오 규칙은 바꾸지 않는다)
            engine.sent.clear();
            expect (jam.chat().sendText ("fake only") == ChatChannel::SendResult::localOnly);
            expect (engine.sent.empty());
            clock.advance (3500);
            expect (jam.chat().getMessages().back().delivery == ChatChannel::Message::Delivery::none);

            // 실제 상대(다른 컴퓨터의 앱)가 들어옴: 아직 연결 중이어도 예전처럼 보내고, 확인이 없으면 3초 뒤 실패 → 다시 보내기
            server.remotePeerJoined ("u_realpeer", juce::String::fromUTF8 ("실제"), "bass", false);
            clock.advance (50);
            expect (engine.allowed.contains ("u_realpeer"));
            engine.sent.clear();
            expect (jam.chat().sendText ("to real") == ChatChannel::SendResult::sent);
            expectEquals ((int) engine.sent.size(), 1);
            const auto id = jam.chat().getMessages().back().id;
            expect (jam.chat().getMessages().back().waitingFor == juce::StringArray { "u_realpeer" });   // 가짜 멤버는 기다리지 않는다
            clock.advance (3100);
            expect (jam.chat().getMessages().back().delivery == ChatChannel::Message::Delivery::failed);
            // 다시 보내기 → 상대가 받으면 확인 (내 메시지는 한 번만)
            const auto count = jam.chat().getMessages().size();
            jam.chat().resend (id);
            jam.chat().received ({ engine.group, "u_realpeer", "ack:" + id, {} });
            expect (jam.chat().getMessages().back().delivery == ChatChannel::Message::Delivery::delivered);
            expectEquals ((int) jam.chat().getMessages().size(), (int) count);

            // 실제 상대가 연결 실패(15초)여도 예전처럼 보내고 3초 뒤 실패
            clock.advance (PeerState::joinTimeoutMs + 1000);
            for (auto& m : jam.members()) if (m.userId == "u_realpeer") expect (m.state == PeerState::State::failed);
            expect (jam.chat().sendText ("still real") == ChatChannel::SendResult::sent);
            clock.advance (3100);
            expect (jam.chat().getMessages().back().delivery == ChatChannel::Message::Delivery::failed);

            store.setCurrentRoom (std::nullopt);
            rt.removeListener (&store);
            o.logFolder.deleteRecursively();
        }

        beginTest ("R11 PR3: 4000 cuts only AOO, hello overwrite (same / other room / null), AOO server reconnect");
        {
            ManualScheduler clock;
            FakeMeonServer::Options fo;
            fo.latencyMs = 0;
            fo.scripts = false;
            FakeMeonServer server { clock, fo };
            server.loadWorld (juce::JSON::parse (juce::String::fromUTF8 (MeonFixtures::world_json, MeonFixtures::world_jsonSize)));
            MeonApi api { server, 0 };
            Realtime rt { server, clock, 0 };
            AppStore store;
            rt.addListener (&store);
            struct Notices : AppStore::Listener
            {
                std::vector<Notice::Kind> got;
                void notice (const Notice& n) override { got.push_back (n.kind); }
            } notices;
            store.addListener (&notices);
            auto run = [&] (auto&& fn) { fn(); clock.advance (50); };
            run ([&] { api.signup ("re@example.com", "re_conn", "abcd123!", true, [] (auto) {}); });
            run ([&] { api.verifyEmail (server.getLastMailedCode(), [] (auto) {}); });
            MePatch p; p.nickname = juce::String ("Re"); p.part = juce::String ("bass");
            run ([&] { api.patchMe (p, [&] (Result<Me> r) { store.setMe (r.get()); }); });
            rt.connect ([&] { return api.getTokens().accessToken; });
            clock.advance (100);

            FakeEngine engine;
            JamPrefs saved;
            JamSession::Options o;
            o.logFolder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("meon-test-logs-pr3");
            o.logFolder.deleteRecursively();
            o.prefs.sendMuted = false;
            o.savePrefs = [&] (const JamPrefs& x) { saved = x; };
            JamSession jam (engine, store, clock, o);
            expect (engine.allowEnabled && engine.allowed.isEmpty(), "켤 때부터 방 밖은 허용 목록이 빈 채로 켜져 있다 (E5)");

            CreateRoom cr;
            cr.type = "private";
            cr.title = "pr3";
            RoomDetail room;
            run ([&] { api.createRoom (cr, [&] (Result<RoomDetail> r) { room = r.get(); store.setCurrentRoom (r.get()); }); });
            // Connection 값을 그대로 엔진에 (API 2.8): AOO 이름은 계정 ID, 닉네임을 쓰지 않는다
            expectEquals (engine.user, room.connection.aooUsername);
            expectEquals (engine.user, server.meId());
            expectEquals (engine.lastHost, room.connection.aooHost);
            expectEquals (engine.lastPort, room.connection.aooPort);
            expectEquals (engine.lastPassword, room.connection.aooUserPassword);
            engine.finishConnect();
            expectEquals (engine.lastGroupPassword, room.connection.groupPassword);
            engine.finishJoin();
            expect (jam.isInRoom());

            auto hello = [&] (std::optional<RoomDetail> r)
            {
                Hello h;
                h.me = *store.state().me;
                h.config = store.state().config;
                h.currentRoom = std::move (r);
                store.hello (h);
            };

            // 1) WebSocket 만 끊겼다 다시 붙음: 같은 방이고 AOO 가 살아 있으면 건드리지 않는다
            {
                const int c0 = engine.connects, j0 = engine.joins;
                store.linkChanged (Realtime::Link::reconnecting);
                store.linkChanged (Realtime::Link::online);
                hello (room);
                expect (jam.isInRoom() && engine.connects == c0 && engine.joins == j0, "AOO 연결 유지");
            }

            // 2) 4000: leave 하지 않고(서버에서는 그대로 멤버) AOO 만 바로 끊는다. 다시 붙지 않는다
            store.linkChanged (Realtime::Link::replaced);
            expect (! engine.connected && engine.group.isEmpty(), "AOO 연결만 끊는다");
            expect (store.state().inRoom(), "방은 그대로");
            expect (server.isMember (room.id, server.meId()), "leave 를 부르지 않았다");
            {
                const int c0 = engine.connects;
                clock.advance (60000);
                expect (engine.connects == c0, "저절로 다시 붙지 않는다");
            }
            // "여기서 다시 연결" → hello 의 같은 방 → 같은 Connection 으로 다시 들어간다
            store.linkChanged (Realtime::Link::online);
            hello (room);
            expectEquals (engine.lastPassword, room.connection.aooUserPassword);
            engine.finishConnect();
            engine.finishJoin();
            expect (jam.isInRoom() && engine.group == room.connection.groupName);

            // 3) AOO 서버만 끊김 (WebSocket 정상): 1·2·4초… 간격으로 같은 Connection 으로 다시 붙고, 그룹에도 다시 들어간다
            {
                const int c0 = engine.connects, j0 = engine.joins;
                engine.serverLost();
                expect (engine.group == room.connection.groupName, "엔진은 그룹 이름을 지우지 않는다");
                clock.advance (JamSession::reconnectFirstMs);
                expectEquals (engine.connects, c0 + 1);
                engine.failConnect();
                clock.advance (1999);
                expectEquals (engine.connects, c0 + 1);
                clock.advance (1);
                expectEquals (engine.connects, c0 + 2, "두 번째는 2초 뒤");
                engine.failConnect();
                clock.advance (4000);
                expectEquals (engine.connects, c0 + 3, "세 번째는 4초 뒤");
                engine.finishConnect();
                expectEquals (engine.joins, j0 + 1, "그룹 이름이 남아 있어도 다시 들어간다");
                expect (jam.isInRoom());
                clock.advance (120000);
                expectEquals (engine.connects, c0 + 3, "붙은 뒤에는 더 시도하지 않는다");
            }

            // 4) hello 가 다른 방: 원래 방 AOO 를 끊고 그 방으로, 송출 정지(이번 입장만), 토스트
            {
                RoomDetail other = room;
                other.id = "r_other_device";
                other.title = "other";
                other.connection.groupName = "G-other";
                other.connection.groupPassword = "gp-other";
                notices.got.clear();
                expect (! engine.sendMuted);
                hello (other);
                expect (engine.sendMuted, "송출 정지로 들어간다");
                // 원래 방에서 직접 끊은 연결의 끊김 알림이 새 방에 붙는 중에 늦게 와도 서버 끊김으로 보지 않는다
                {
                    const int c0 = engine.connects;
                    engine.serverLost();
                    clock.advance (5000);
                    expectEquals (engine.connects, c0, "늦게 온 끊김 알림으로 다시 붙지 않는다");
                }
                expect (! saved.sendMuted, "저장된 송출 설정은 그대로");
                expect (std::find (notices.got.begin(), notices.got.end(), Notice::Kind::movedToOtherRoom) != notices.got.end(), "토스트");
                engine.finishConnect();
                expectEquals (engine.lastGroupPassword, juce::String ("gp-other"));
                engine.finishJoin();
                expect (jam.isInRoom() && engine.group == "G-other");

                // 5) hello 가 null: 끊긴 사이 추방·닫힘·삭제 → AOO 끊고 홈. 이번 입장에만 건 송출 정지는 풀린다
                hello (std::nullopt);
                expect (! store.state().inRoom() && jam.getPhase() == JamSession::Phase::idle);
                expect (! engine.connected && engine.group.isEmpty());
                expect (! engine.sendMuted, "원래 송출 상태로");
            }

            // 세션 로그: 서버 끊김은 상태가 바뀔 때만 (serverLost 1번 → serverReconnected 1번, 시도 횟수와 시간). 4000 은 "replaced"
            {
                int lost = 0, back = 0, replaced = 0;
                juce::String backDetail;
                for (auto& f : o.logFolder.findChildFiles (juce::File::findFiles, false, "meon-session-*.json"))
                {
                    const auto root = juce::JSON::parse (f);
                    const auto events = root.getProperty ("events", {});
                    if (auto* ev = events.getArray())
                        for (auto& e : *ev)
                        {
                            const auto type = e.getProperty ("type", {}).toString();
                            if (type == "serverLost") ++lost;
                            if (type == "serverReconnected") { ++back; backDetail = e.getProperty ("detail", {}).toString(); }
                            if (type == "leave" && e.getProperty ("detail", {}).toString() == "replaced") ++replaced;
                        }
                }
                expectEquals (lost, 1);
                expectEquals (back, 1);
                expect (backDetail.startsWith ("3 attempts, 7.0 s"), backDetail);
                expectEquals (replaced, 1);
            }

            rt.removeListener (&store);
            store.removeListener (&notices);
            o.logFolder.deleteRecursively();
        }

        beginTest ("Latency telemetry: once per entry, 10 s after the host connects, median; again on re-entry; not as host (R11 PR4)");
        {
            ManualScheduler clock;
            AppStore store;
            Config cfg;
            cfg.latencyCoefficient = 0.7;
            store.setConfig (cfg);
            store.setMyRtt (8);
            FakeEngine engine;
            JamSession::Options o;
            o.logFolder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("meon-test-logs-telemetry");
            std::vector<LatencyProbe::Report> sent;
            o.sendLatency = [&] (const LatencyProbe::Report& r) { sent.push_back (r); };
            JamSession jam (engine, store, clock, o);

            RoomDetail room;
            room.id = "r_t";
            room.hostId = "u_host";
            room.hostPresent = true;
            room.hostRttMs = 12;
            room.connection.aooHost = "127.0.0.1";
            room.connection.aooPort = 10999;
            room.connection.groupName = "g";
            room.connection.aooUsername = "u_me";
            room.connection.members = { "u_me", "u_host" };
            auto enterAndConnect = [&]
            {
                store.setCurrentRoom (room);
                engine.finishConnect();
                engine.finishJoin();
                expect (jam.isInRoom());
                clock.advance (3000);   // 방장과 연결되기 전에는 재지 않는다
                engine.peerArrives ("u_host");
                engine.peer ("u_host")->pingMs = 11.0f;
            };

            enterAndConnect();
            clock.advance (9000);
            expect (sent.empty(), "10초가 차기 전에는 보내지 않는다");
            clock.advance (2000);
            expectEquals ((int) sent.size(), 1);
            if (! sent.empty())
            {
                expectEquals (sent[0].roomId, juce::String ("r_t"));
                expectEquals (sent[0].estimatedMs, 14);   // round (0.7 × (8 + 12))
                expectEquals (sent[0].myRttMs, 8);
                expectEquals (sent[0].hostRttMs, 12);
                expectEquals (sent[0].actualP2pRttMs, 11);
            }
            clock.advance (60000);
            expectEquals ((int) sent.size(), 1, "입장 한 번에 한 번만");

            // 같은 방에 다시 들어가면 다시 잰다
            store.setCurrentRoom (std::nullopt);
            enterAndConnect();
            clock.advance (11000);
            expectEquals ((int) sent.size(), 2, "다시 들어가면 다시 보낸다");

            // 10초가 차기 전에 나가면 보내지 않는다
            store.setCurrentRoom (std::nullopt);
            enterAndConnect();
            clock.advance (4000);
            store.setCurrentRoom (std::nullopt);
            clock.advance (20000);
            expectEquals ((int) sent.size(), 2, "다 재기 전에 나가면 보내지 않는다");

            // 내가 방장이면 보내지 않는다
            room.hostId = "u_me";
            store.setCurrentRoom (room);
            engine.finishConnect();
            engine.finishJoin();
            clock.advance (20000);
            expectEquals ((int) sent.size(), 2, "방장은 보내지 않는다");
            store.setCurrentRoom (std::nullopt);
            o.logFolder.deleteRecursively();
        }

        beginTest ("Home monitor only on the signed-in home screen: start, logout, 4000, forced update, back home");
        {
            using S = AuthController::Step;
            ManualScheduler clock;
            FakeMeonServer server (clock, [] { FakeMeonServer::Options f; f.latencyMs = 10; f.scripts = false; return f; }());
            server.loadWorld (juce::JSON::parse (juce::String::fromUTF8 (MeonFixtures::world_json, MeonFixtures::world_jsonSize)));
            MeonApi api (server, 50);
            Realtime rt (server, clock, 50);
            AppStore store;
            rt.addListener (&store);
            MemoryTokenStore tokens;
            AuthController auth (api, rt, store, tokens, clock);
            FakeEngine engine;
            JamPrefs saved;
            JamSession::Options o;
            o.logFolder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("meon-test-logs-homegate");
            o.prefs.homeMonitor = true;   // 저장값: 켬
            o.savePrefs = [&] (const JamPrefs& x) { saved = x; };
            JamSession jam (engine, store, clock, o);
            HomeMonitorGate gate (auth, jam);
            // 게이트보다 먼저 알림을 받는 쪽(ListenerList 는 나중에 등록한 쪽부터 부른다. 앱에서는 화면)이 알림 안에서
            // 단계를 또 바꾸는 경우: "완료"에서 바로 홈으로 넘기는 개발 자동 진행 등
            struct AutoDone : AuthController::Listener
            {
                AuthController& a; bool on = false;
                explicit AutoDone (AuthController& x) : a (x) {}
                void authStepChanged (AuthController::Step s) override { if (on && s == AuthController::Step::done) a.finishDone(); }
            } autoDone (auth);
            auth.addListener (&autoDone);
            auto wait = [&] (int ms = 100) { clock.advance (ms); };

            // 앱을 켠 직후 (로그인 상태 확인 중): 첫 화면이 정해지기 전
            expect (auth.step() == S::loading);
            expect (! engine.selfMon && ! jam.isHomeMonitorAudible(), "시작 직후에는 듣지 않는다");
            auth.start();
            expect (! engine.selfMon);
            wait();
            expect (auth.step() == S::login);
            expect (! engine.selfMon, "로그인 화면");

            // 가입·인증·온보딩 내내 꺼져 있다
            auth.showSignup();
            expect (! engine.selfMon, "회원가입");
            auth.signup ("gate@example.com", "gate_user", "abcd123!", true, [] (Result<Done>) {});
            wait();
            expect (auth.step() == S::verify && ! engine.selfMon, "이메일 인증");
            auth.verify (server.getLastMailedCode(), [] (Result<Done>) {});
            wait();
            expect (auth.step() == S::nickname && ! engine.selfMon, "닉네임");
            auth.setNickname ("Gate", [] (Result<Done>) {});
            wait();
            expect (auth.step() == S::part && ! engine.selfMon, "파트");
            auth.setPart ("keys", [] (Result<Done>) {});
            wait();
            expect (! engine.selfMon, "디스코드 안내");
            auth.finishDiscord();
            expect (auth.step() == S::done && ! engine.selfMon, "완료");
            auth.finishDone();

            // 홈: 저장값대로 켜진다
            expect (auth.step() == S::ready);
            expect (engine.selfMon && jam.isHomeMonitorAudible(), "홈에서는 저장값대로");

            // 홈에서 로그아웃: 그 자리에서 꺼지고, 저장값은 그대로
            auth.logout();
            expect (auth.step() == S::login);
            expect (! engine.selfMon && ! jam.isHomeMonitorAudible(), "로그아웃 즉시 꺼진다");
            expect (jam.isHomeMonitor(), "저장된 설정값은 그대로");
            wait();

            // 다시 로그인해 홈으로 오면 저장값대로 다시 켜진다
            auth.login ("gate@example.com", "abcd123!", [] (Result<Done>) {});
            wait();
            expect (auth.step() == S::done && ! engine.selfMon, "로그인 뒤 완료 화면에서는 아직 꺼져 있다");
            auth.finishDone();
            expect (auth.step() == S::ready);
            expect (engine.selfMon, "다시 로그인해 홈으로 오면 저장값대로");

            // 4000 (다른 곳에서 접속): 꺼진다 → 여기서 다시 연결해 홈이면 다시 켜진다
            server.devCloseLink (4000);
            expect (auth.step() == S::replaced);
            expect (! engine.selfMon, "4000 화면");
            auth.reconnectHere();
            wait();
            expect (auth.step() == S::ready && engine.selfMon);

            // 완료 → 홈 이 다른 알림 안에서 이어져도(옛 "완료" 알림이 뒤늦게 와도) 홈에서는 켜져 있다
            auth.logout();
            expect (! engine.selfMon);
            wait();
            autoDone.on = true;
            auth.login ("gate@example.com", "abcd123!", [] (Result<Done>) {});
            wait();
            expect (auth.step() == S::ready);
            expect (engine.selfMon, "알림 안에서 홈으로 넘어가도 저장값대로");
            autoDone.on = false;

            // 강제 업데이트: 꺼진다
            server.devSetConfig (60, 60);
            wait();
            expect (auth.step() == S::outdated);
            expect (! engine.selfMon, "강제 업데이트 화면");
            expect (jam.isHomeMonitor());

            // 저장값을 꺼 두면 홈에서도 듣지 않는다 (기존 규칙)
            jam.setHomeMonitor (false);
            jam.setHomeVisible (true);
            expect (! engine.selfMon);

            auth.removeListener (&autoDone);
            rt.removeListener (&store);
            o.logFolder.deleteRecursively();
        }

        beginTest ("JamSession: hear myself before entering (setting 28), send mute, plugin");
        {
            ManualScheduler clock;
            AppStore store;
            FakeEngine engine;
            JamPrefs saved;
            JamSession::Options o;
            o.logFolder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("meon-test-logs-28");
            o.prefs.homeMonitor = true;
            o.prefs.selfMonitor = false;   // 방 안의 내 행 음소거와는 따로
            o.savePrefs = [&] (const JamPrefs& x) { saved = x; };
            {
                JamSession jam (engine, store, clock, o);
                expect (! engine.selfMon, "홈 화면이 보이기 전에는 듣지 않는다");
                jam.setHomeVisible (true);
                expect (engine.selfMon && jam.isHomeMonitorAudible());     // 켜 두면 홈에서도 들린다
                jam.setSendMuted (true);
                expect (! engine.selfMon && ! jam.isHomeMonitorAudible()); // 송출 정지 중에는 홈에서도 안 들린다
                jam.setSendMuted (false);
                expect (engine.selfMon);
                jam.setHomeMonitor (false);
                expect (! engine.selfMon && ! saved.homeMonitor);           // 끄면 22번 규칙대로
                jam.setHomeMonitor (true);
                expect (engine.selfMon && saved.homeMonitor);
            }
            FakeEngine pluginEngine;
            o.plugin = true;
            {
                JamSession jam (pluginEngine, store, clock, o);
                jam.setHomeVisible (true);
                expect (! pluginEngine.selfMon && ! jam.isHomeMonitorAudible());   // 플러그인에는 이 설정이 없다
            }
            o.logFolder.deleteRecursively();
        }

        beginTest ("JamSession: follows current room, E1 allow list, fake directory, silence, leave");
        {
            ManualScheduler clock;
            FakeMeonServer::Options fo;
            fo.latencyMs = 10;
            fo.scripts = false;
            FakeMeonServer server { clock, fo };
            server.loadWorld (juce::JSON::parse (juce::String::fromUTF8 (MeonFixtures::world_json, MeonFixtures::world_jsonSize)));
            MeonApi api { server, 0 };
            Realtime rt { server, clock, 0 };
            AppStore store;
            rt.addListener (&store);
            auto run = [&] (auto&& fn) { fn(); clock.advance (50); };

            run ([&] { api.signup ("me@example.com", "chan_keys", "abcd123!", true, [] (auto) {}); });
            run ([&] { api.verifyEmail (server.getLastMailedCode(), [] (auto) {}); });
            MePatch p; p.nickname = juce::String ("Me"); p.part = juce::String ("keys");
            run ([&] { api.patchMe (p, [&] (Result<Me> r) { store.setMe (r.get()); }); });
            rt.connect ([&] { return api.getTokens().accessToken; });
            clock.advance (100);

            FakeEngine engine;
            JamPrefs saved;
            JamSession::Options o;
            o.logFolder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("meon-test-logs");
            o.logFolder.deleteRecursively();   // 지난 실행이 남긴 로그
            o.fake = &server;
            o.prefs.sendMuted = true;     // 지난번에 송출 정지 상태로 끔 → 켤 때 그대로 (버그 #1)
            o.prefs.selfMonitor = true;   // 셀프 모니터를 켜 둔 사람
            o.savePrefs = [&] (const JamPrefs& x) { saved = x; };
            engine.selfMon = true;        // 지난 실행의 엔진 상태가 켜진 채로 남아 있어도
            JamSession jam (engine, store, clock, o);
            expect (engine.sendMuted);     // 홈의 송출 정지 버튼도 설정 파일 값
            expect (! engine.selfMon);     // 버그 22: 홈(방 밖)에서는 내 소리를 내보내지 않는다
            expect (jam.isSelfMonitor() && ! jam.isSelfMonitorAudible());

            run ([&] { api.joinByCode ("Q7K2MX", [&] (Result<JoinResult> r) { store.setCurrentRoom (r.get().room); }); });
            expect (jam.getPhase() == JamSession::Phase::connecting);
            expect (engine.sendMuted);     // 지난 송출 정지 상태 그대로
            expect (! engine.selfMon);     // 버그 23: 송출 정지 중에는 셀프 모니터도 꺼진다
            expectEquals (engine.user, server.meId());
            engine.finishConnect();
            expect (jam.getPhase() == JamSession::Phase::joining);
            expectEquals (engine.group, juce::String ("Q7K2MX"));
            engine.finishJoin();
            expect (jam.isInRoom());
            expect (engine.allowEnabled && engine.allowed.isEmpty());

            // 혼자일 때 채팅: 내 목록에만 남고 엔진으로는 보내지 않는다
            engine.sent.clear();
            expect (jam.chat().sendText (juce::String::fromUTF8 ("혼자 연습")) == ChatChannel::SendResult::localOnly);
            expect (engine.sent.empty());
            expectEquals ((int) jam.chat().getMessages().size(), 1);

            // 상대가 들어오면: 허용 목록에 없어 엔진은 연결하지 않고, 가짜 서버가 멤버로 올린 뒤 허용 → 연결
            engine.peerArrives ("u_peer");
            expect (engine.peer ("u_peer") == nullptr);
            clock.advance (50);   // room.updated
            expect (engine.allowed.contains ("u_peer"));
            engine.peerArrives ("u_peer");
            expect (engine.peer ("u_peer") != nullptr);
            clock.advance (JamSession::tickMs * 2);
            auto ms = jam.members();
            expectEquals ((int) ms.size(), 2);
            bool found = false;
            for (auto& m : ms) if (m.userId == "u_peer") { found = true; expect (m.state == PeerState::State::connected); }
            expect (found);

            // 연결된 상대가 생기면 평소처럼 보낸다 (내 메시지는 한 번만 보인다)
            {
                const auto before = jam.chat().getMessages().size();
                engine.sent.clear();
                expect (jam.chat().sendText ("hello") == ChatChannel::SendResult::sent);
                expectEquals ((int) engine.sent.size(), 1);
                expectEquals ((int) jam.chat().getMessages().size(), (int) before + 1);
            }

            // 프로필 채팅으로 닉네임·파트·방장
            jam.chat().received ({ "Q7K2MX", "u_peer", "profile", juce::String::fromUTF8 (R"({"nickname":"민준","part":"guitar","host":true})") });
            clock.advance (50);
            for (auto& m : jam.members())
                if (m.userId == "u_peer") { expect (m.nickname == juce::String::fromUTF8 ("민준") && m.isHost); }

            // 버그 24: 방 안에서 닉네임·파트를 바꾸면 내 프로필을 바로 다시 보낸다 (다른 컴퓨터의 멤버 목록)
            {
                auto profiles = [&] { int n = 0; for (auto& c : engine.sent) if (c.tags == "profile") ++n; return n; };
                const int before = profiles();
                auto me = *store.state().me;
                me.part = juce::String ("drums");
                store.setMe (me);
                expectEquals (profiles(), before + 1);
                const auto last = juce::JSON::parse (engine.sent.back().message);
                expectEquals (last.getProperty ("part", {}).toString(), juce::String ("drums"));
            }

            // 멤버가 아닌 사람의 채팅은 버린다
            const auto before = jam.chat().getMessages().size();
            jam.chat().received ({ "Q7K2MX", "u_stranger", "id:1", "spam" });   // (직접 넣으면 들어가지만)
            engine.ls.call ([] (Engine::Listener& l) { l.engineChat ({ "Q7K2MX", "u_stranger2", "id:2", "spam" }); });
            expectEquals ((int) jam.chat().getMessages().size(), (int) before + 1);

            // 패킷이 3초 멈추면 끊김
            clock.advance (3500);
            for (auto& m : jam.members()) if (m.userId == "u_peer") expect (m.state == PeerState::State::interrupted && m.dropCount == 1);

            // 무음 경고: 송출 정지 중에는 없음 → 켜면 5초 뒤 경고
            clock.advance (6000);
            expect (! jam.isInputSilent());
            jam.setSendMuted (false);
            expect (! saved.sendMuted);
            expect (engine.selfMon);       // 송출을 다시 켜면 설정값대로 돌아온다
            jam.setSendMuted (true);
            expect (! engine.selfMon && jam.isSelfMonitor() && saved.selfMonitor);   // 설정값 자체는 그대로
            jam.setSendMuted (false);
            expect (engine.selfMon && jam.isSelfMonitorAudible());
            clock.advance (5600);
            expect (jam.isInputSilent());
            engine.inLevel = 0.1f;
            clock.advance (JamSession::tickMs);
            expect (! jam.isInputSilent());

            // 입력 장치가 없음 (채널 0, 레벨 −100 dB, 보내기 켜짐) → 5초 뒤 로그용 inputMissing. 무음 경고(화면)는 뜨지 않는다
            engine.inLevel = 0.0f;
            engine.inputChannels = 0;
            clock.advance (5600);
            expect (jam.isInputMissing() && ! jam.isInputSilent());
            engine.inputChannels = 2;
            engine.inLevel = 0.1f;
            clock.advance (JamSession::tickMs);
            expect (! jam.isInputMissing() && ! jam.isInputSilent());

            // 출력 장치 없음 (로그용): 5초 이어지면 켜지고, 장치가 열리면 바로 꺼진다. 입력 판정과 따로 논다
            expect (! jam.isOutputMissing());
            engine.outputOk = false;
            clock.advance (4000);
            expect (! jam.isOutputMissing());
            clock.advance (1600);
            expect (jam.isOutputMissing() && ! jam.isInputMissing() && ! jam.isInputSilent());
            engine.outputOk = true;
            clock.advance (JamSession::tickMs);
            expect (! jam.isOutputMissing());

            // 버그 18: 세션 로그의 audio 칸 (입력이 없으면 "없음"), 장치 없음 이벤트
            {
                const auto logs = o.logFolder.findChildFiles (juce::File::findFiles, false, "meon-session-*.json");
                expect (logs.size() == 1);
                if (logs.size() == 1)
                {
                    const auto root = juce::JSON::parse (logs[0]);
                    const auto audio = root.getProperty ("audio", {});
                    expectEquals (audio.getProperty ("deviceType", {}).toString(), juce::String ("Windows Audio"));
                    expectEquals (audio.getProperty ("inputDevice", {}).toString(), juce::String (juce::CharPointer_UTF8 ("없음")));
                    expectEquals (audio.getProperty ("outputDevice", {}).toString(), juce::String ("LG HDR WQHD (HDMI)"));
                    expectEquals ((int) audio.getProperty ("sampleRate", 0), 48000);
                    expectEquals ((int) audio.getProperty ("bufferSize", 0), 480);
                    bool started = false, ended = false, outStarted = false, outEnded = false;
                    if (auto* ev = root.getProperty ("events", {}).getArray())
                        for (auto& e : *ev)
                        {
                            const auto type = e.getProperty ("type", {}).toString();
                            started = started || type == "inputMissingStart";
                            ended = ended || type == "inputMissingEnd";
                            outStarted = outStarted || type == "outputMissingStart";
                            outEnded = outEnded || type == "outputMissingEnd";
                        }
                    expect (started && ended);
                    expect (outStarted && outEnded);
                }
            }

            // 버퍼 모드·마스터 저장
            jam.setBufferMode (Engine::BufferMode::stable);
            expect (engine.mode == Engine::BufferMode::stable && saved.bufferMode == Engine::BufferMode::stable);
            jam.setMasterDb (9.0f);
            expectEquals (engine.masterDb, 6.0f);

            // 셀프 모니터 패닝: 엔진에 넘기고 저장 (-100..100)
            jam.setSelfMonitorPan (-40);
            expectEquals (engine.selfPan, -40);
            expectEquals (saved.selfMonitorPan, -40);
            jam.setSelfMonitorPan (250);
            expectEquals (engine.selfPan, 100);

            // 파일을 틀어 둔 채
            expect (jam.loadFile (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("song.wav")));
            jam.setFilePlaying (true);
            expect (engine.filePlaying);

            // 출력 장치가 없는 채로 방을 나오면 판정도 꺼진다 (방 밖에서는 켜지지 않는다)
            engine.outputOk = false;
            clock.advance (5600);
            expect (jam.isOutputMissing());

            // 방에서 나오면 엔진도 나오고, 파일도 닫는다 (방 닫힘·추방도 같은 길: currentRoom 이 빈다)
            store.setCurrentRoom (std::nullopt);
            expect (jam.getPhase() == JamSession::Phase::idle);
            expect (! jam.isOutputMissing());
            clock.advance (5600);
            expect (! jam.isOutputMissing());
            engine.outputOk = true;
            expect (! engine.selfMon && jam.isSelfMonitor());   // 버그 22: 방을 나오면 꺼지고, 설정값은 그대로
            jam.setHomeVisible (true);                          // 로그인을 마친 홈 화면 (HomeMonitorGate)
            jam.setHomeMonitor (true);                          // 버그 28: 홈에서 듣기를 켜면 방 밖에서도
            expect (engine.selfMon && jam.isHomeMonitorAudible());
            jam.setHomeMonitor (false);
            expect (! engine.connected && engine.group.isEmpty());
            expect (engine.allowEnabled && engine.allowed.isEmpty(), "방 밖: 허용 목록을 빈 채로 켜 둔다 (E5)");
            expect (! engine.file && ! engine.filePlaying && ! jam.hasFile());
            rt.removeListener (&store);
            o.logFolder.deleteRecursively();
        }
    }
};

static JamTests jamTests;

} // namespace meon::core
