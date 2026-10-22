// 합주 로직: PeerState, SilenceDetector, ChatChannel, JamSession (가짜 엔진 + 가짜 서버)
#include "../core/jam/JamSession.h"
#include "../core/jam/RttReporter.h"
#include "../core/jam/HomeMonitorGate.h"
#include "../core/auth/TokenStore.h"
#include "../core/api/MeonApi.h"
#include "../core/api/ApiJson.h"
#include "../core/log/EventReporter.h"
#include "../core/api/Realtime.h"
#include "../core/home/HomeController.h"
#include "../core/model/RoomFilter.h"
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
        void serverLost()    { connected = false; ls.call ([] (Listener& l) { l.engineDisconnected (false, "disconnected from server"); }); }
        /** AOO 가 TCP 로 붙은 뒤 서버의 UDP 확인 답을 5초 안에 못 받아 포기 (UDP 가 막힌 네트워크). 붙기 이벤트 없이 끊김만 온다 */
        void udpTimeout()    { connected = false; ls.call ([] (Listener& l) { l.engineDisconnected (false, "timed out"); }); }
        /** 직접 끊은 연결의 늦게 온 알림 (직전 방) */
        void staleDisconnect() { ls.call ([] (Listener& l) { l.engineDisconnected (true, {}); }); }
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
            // 무음 판정 중 장치가 빠지면 무음은 바로 해제, 5초 뒤 장치 없음
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

        beginTest ("chat log: .txt per room entry (no file without messages), header / lines / end, resend & duplicates once, ack & profile skipped, multi-line indent");
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
            run ([&] { api.signup ("chatlog@example.com", "chatlog_keys", "abcd123!", true, [] (auto) {}); });
            run ([&] { api.verifyEmail (server.getLastMailedCode(), [] (auto) {}); });
            MePatch p; p.nickname = juce::String::fromUTF8 ("승찬"); p.part = juce::String ("keys");
            run ([&] { api.patchMe (p, [&] (Result<Me> r) { store.setMe (r.get()); }); });
            rt.connect ([&] { return api.getTokens().accessToken; });
            clock.advance (100);

            FakeEngine engine;
            JamSession::Options o;
            o.logFolder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("meon-test-logs-chatlog");
            o.logFolder.deleteRecursively();
            o.fake = &server;
            // 벽시계 주입: 2026-10-18 21:03:10 부터 부를 때마다 5초씩
            int tick = 0;
            o.wallClock = [&tick] { return juce::Time (2026, 9, 18, 21, 3, 10) + juce::RelativeTime::seconds (5.0 * tick++); };
            JamSession jam (engine, store, clock, o);
            const auto chatDir = o.logFolder.getChildFile ("chat");
            auto chatFiles = [&] { return chatDir.findChildFiles (juce::File::findFiles, false, "*.txt"); };
            auto enter = [&]
            {
                run ([&] { api.joinRoom ("r_f_3", [&] (Result<JoinResult> r) { store.setCurrentRoom (r.get().room); }); });
                engine.finishConnect();
                engine.finishJoin();
            };
            auto leave = [&] { store.setCurrentRoom (std::nullopt); clock.advance (50); };

            // 1) 입장 → 메시지 0개 → 퇴장: 파일이 없다
            enter();
            expect (jam.isInRoom());
            leave();
            expect (! jam.isInRoom());
            expectEquals (chatFiles().size(), 0);

            // 2) 다시 입장: 내 메시지 1 + 상대 메시지 1(여러 줄) + 시스템 줄 1
            enter();
            expect (jam.isInRoom());
            server.remotePeerJoined ("u_realpeer", juce::String::fromUTF8 ("민준"), "bass", false);
            clock.advance (50);
            expect (jam.chat().sendText (juce::String::fromUTF8 ("안녕하세요")) == ChatChannel::SendResult::sent);
            const auto myId = jam.chat().getMessages().back().id;
            jam.chat().received ({ engine.group, "u_realpeer", "id:abc123", juce::String::fromUTF8 ("반가워요\n오늘은\n세 곡") });
            jam.chat().addSystem (juce::String::fromUTF8 ("서연님이 입장했어요"));
            // 기록되지 않는 것: ack·profile, 다시 보내기, 다시 받은 같은 메시지, 전달 실패 표시
            jam.chat().received ({ engine.group, "u_realpeer", "ack:" + myId, {} });
            jam.chat().received ({ engine.group, "u_realpeer", "profile", "{\"nickname\":\"x\"}" });
            clock.advance (3100);   // (이미 확인됐으므로 실패 표시 없음) 다시 보내기
            jam.chat().resend (myId);
            jam.chat().received ({ engine.group, "u_realpeer", "id:abc123", juce::String::fromUTF8 ("반가워요\n오늘은\n세 곡") });
            expectEquals (chatFiles().size(), 1);
            const auto file = chatFiles()[0];
            // 세션 로그와 같은 이름
            expect (jam.log().getFile().getFileNameWithoutExtension() == file.getFileNameWithoutExtension(), file.getFileName());
            // 퇴장 전에도 (강제 종료 대비) 그때까지의 줄이 파일에 있다
            expect (file.loadFileAsString().contains (juce::String::fromUTF8 ("민준: 반가워요")));
            leave();

            const auto expected = juce::String::fromUTF8 (
                "# Meon 채팅 기록\n"
                "# 방: 어쿠스틱 커버 연습\n"
                "# 시작: 2026-10-18 21:03:15\n"
                "[21:03:20] 승찬: 안녕하세요\n"
                "[21:03:25] 민준: 반가워요\n"
                "  오늘은\n"
                "  세 곡\n"
                "[21:03:30] * 서연님이 입장했어요\n"
                "# 끝: 2026-10-18 21:03:35\n");
            expectEquals (file.loadFileAsString(), expected);

            // 3) 퇴장 후 다시 입장 → 새 파일
            enter();
            expect (jam.chat().sendText ("again") != ChatChannel::SendResult::failed);
            leave();
            expectEquals (chatFiles().size(), 2);
            for (auto& f : chatFiles())
                if (f != file) expect (f.loadFileAsString().contains ("again") && ! f.loadFileAsString().contains (juce::String::fromUTF8 ("반가워요")));

            // 형식: 이름을 모르면 user id, 시스템 줄은 이름을 안 쓴다
            expectEquals (ChatLog::formatLine (juce::Time (2026, 9, 18, 9, 5, 7), ChatLog::Kind::other, "u_x", "a\r\nb"),
                          juce::String ("[09:05:07] u_x: a\n  b\n"));
            expectEquals (ChatLog::formatLine (juce::Time (2026, 9, 18, 9, 5, 7), ChatLog::Kind::system, "ignored", "hi"),
                          juce::String ("[09:05:07] * hi\n"));

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
                    engine.staleDisconnect();
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

        beginTest ("Peer audio events (POST /v1/events): connected + ms_to_connect, failed once per member stay (aoo_joinfail, timeout, aoo_server_lost)");
        {
            ManualScheduler clock;
            AppStore store;
            FakeEngine engine;
            JamSession::Options o;
            o.logFolder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("meon-test-logs-peer-events");
            struct Ev { juce::String type, roomId; juce::var props; };
            std::vector<Ev> evs;
            o.reportEvent = [&] (const juce::String& t, const juce::String& r, const juce::var& p) { evs.push_back ({ t, r, p }); };
            JamSession jam (engine, store, clock, o);
            auto count = [&] (const juce::String& type, const juce::String& reason = {})
            {
                int n = 0;
                for (auto& e : evs)
                    if (e.type == type && (reason.isEmpty() || e.props.getProperty ("reason", "").toString() == reason)) ++n;
                return n;
            };
            auto joinFail = [&] (const juce::String& u) { engine.ls.call ([u] (Engine::Listener& l) { l.enginePeerJoinFailed (u); }); };

            RoomDetail room;
            room.id = "r_ev";
            room.hostId = "u_me";
            room.connection.aooHost = "127.0.0.1";
            room.connection.aooPort = 10999;
            room.connection.groupName = "g";
            room.connection.aooUsername = "u_me";
            room.connection.members = { "u_me", "u_ok", "u_nat", "u_gone" };
            store.setCurrentRoom (room);
            engine.finishConnect();
            engine.finishJoin();

            // 2.4초 뒤 이어진 상대: 한 번, 입장부터 이어질 때까지 ms
            clock.advance (2400);
            engine.peerArrives ("u_ok");
            clock.advance (1000);
            expectEquals (count ("peer_audio_connected"), 1);
            if (! evs.empty())
            {
                expectEquals (evs[0].roomId, juce::String ("r_ev"));
                const auto ms = (int) evs[0].props.getProperty ("ms_to_connect", -1);
                expect (ms >= 2400 && ms <= 3400, "ms_to_connect " + juce::String (ms));
            }

            // AOO 가 길 뚫기에 실패: 15초를 기다리지 않고 바로, 이유 aoo_joinfail. 같은 알림이 또 와도 한 번
            joinFail ("u_nat");
            joinFail ("u_nat");
            expectEquals (count ("peer_audio_failed", "aoo_joinfail"), 1);
            // 이어진 상대의 실패 알림, 멤버가 아닌 사람의 알림은 보내지 않는다
            joinFail ("u_ok");
            joinFail ("u_stranger");
            expectEquals (count ("peer_audio_failed"), 1);

            // 15초: 아무 소식 없는 상대는 timeout. 이미 보낸 u_nat 는 다시 보내지 않는다
            clock.advance (PeerState::joinTimeoutMs);
            expectEquals (count ("peer_audio_failed", "timeout"), 1);
            expectEquals (count ("peer_audio_failed"), 2);

            // 실패 뒤 늦게 이어지면 connected 를 보낸다 (ms_to_connect 15초 이상이라 늦은 연결과 구별된다)
            engine.peerArrives ("u_nat");
            clock.advance (1000);
            expectEquals (count ("peer_audio_connected"), 2);
            expect ((int) evs.back().props.getProperty ("ms_to_connect", 0) >= PeerState::joinTimeoutMs);

            // 멤버가 나갔다 다시 들어오면 새 구간. 그동안 합주 서버와 끊겼으면 이유 aoo_server_lost
            room.connection.members = { "u_me", "u_ok", "u_nat" };
            store.setCurrentRoom (room);
            clock.advance (500);
            engine.serverLost();
            expect (jam.isJamServerDown());
            room.connection.members = { "u_me", "u_ok", "u_nat", "u_gone" };
            store.setCurrentRoom (room);
            clock.advance (PeerState::joinTimeoutMs + 1000);
            expectEquals (count ("peer_audio_failed", "aoo_server_lost"), 1);
            expectEquals (count ("peer_audio_failed"), 3);
            for (auto& e : evs) expectEquals (e.roomId, juce::String ("r_ev"));

            store.setCurrentRoom (std::nullopt);
            o.logFolder.deleteRecursively();
        }

        beginTest ("EventReporter: waits 3 s then sends once, 50 per request, keeps the newest 200, at/room_id/props; takeBatch for quit");
        {
            ManualScheduler clock;
            std::vector<juce::Array<juce::var>> sent;
            EventReporter rep (clock, [&] (const juce::Array<juce::var>& b) { sent.push_back (b); });

            rep.add ("peer_audio_failed", "r_1", json::obj ({ { "reason", "aoo_joinfail" } }));
            rep.add ("peer_audio_connected");
            clock.advance (EventReporter::delayMs - 1);
            expect (sent.empty(), "모으는 동안은 보내지 않는다");
            clock.advance (1);
            expectEquals ((int) sent.size(), 1);
            if (sent.size() == 1)
            {
                expectEquals (sent[0].size(), 2);
                const auto& e = sent[0].getReference (0);
                expectEquals (e.getProperty ("type", "").toString(), juce::String ("peer_audio_failed"));
                expectEquals (e.getProperty ("room_id", "").toString(), juce::String ("r_1"));
                expectEquals (e.getProperty ("props", juce::var()).getProperty ("reason", "").toString(), juce::String ("aoo_joinfail"));
                expect (juce::Time::fromISO8601 (e.getProperty ("at", "").toString()).toMilliseconds() == clock.nowMs() - (EventReporter::delayMs),
                        "at = 기록한 때 (RFC3339)");
                const auto& e2 = sent[0].getReference (1);
                expect (! e2.hasProperty ("room_id") && ! e2.hasProperty ("props"), "빈 값은 넣지 않는다");
            }
            expectEquals (rep.pendingCount(), 0);

            sent.clear();
            for (int i = 0; i < 230; ++i) rep.add ("peer_audio_connected", {}, json::obj ({ { "ms_to_connect", i } }));
            expectEquals (rep.pendingCount(), EventReporter::maxPending);
            clock.advance (EventReporter::delayMs);
            expectEquals ((int) sent.size(), 4);   // 200 = 50 × 4
            for (auto& b : sent) expect (b.size() <= EventReporter::maxBatch);
            if (! sent.empty())
                expectEquals ((int) sent[0].getReference (0).getProperty ("props", juce::var()).getProperty ("ms_to_connect", -1), 30);   // 오래된 30개를 버림

            sent.clear();
            rep.add ("peer_audio_failed", "r_2", json::obj ({ { "reason", "timeout" } }));
            const auto batch = rep.takeBatch();
            expectEquals (batch.size(), 1);
            expectEquals (rep.pendingCount(), 0);
            clock.advance (EventReporter::delayMs);
            expect (sent.empty(), "꺼낸 것은 다시 보내지 않는다");
        }

        beginTest ("Connection: jam server down = engine says lost, or every partner silent and the AOO server unreachable (checks the realtime link too)");
        {
            ManualScheduler clock;
            AppStore store;
            FakeEngine engine;
            JamSession::Options o;
            o.logFolder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("meon-test-logs-conn");
            int realtimeChecks = 0;
            juce::StringArray probed;
            std::vector<std::function<void (bool)>> pending;
            o.peersAllInterrupted = [&] { ++realtimeChecks; };
            o.checkReachable = [&] (const juce::String& h, int p, std::function<void (bool)> done)
            {
                probed.add (h + ":" + juce::String (p));
                pending.push_back (std::move (done));
            };
            JamSession jam (engine, store, clock, o);
            struct LinkLog : JamSession::Listener { int changes = 0; void jamServerLinkChanged() override { ++changes; } } log;
            jam.addListener (&log);

            RoomDetail room;
            room.id = "r_c";
            room.hostId = "u_me";
            room.connection.aooHost = "127.0.0.1";
            room.connection.aooPort = 10999;
            room.connection.groupName = "g";
            room.connection.aooUsername = "u_me";
            room.connection.members = { "u_me", "u_b" };
            store.setCurrentRoom (room);
            engine.finishConnect();
            engine.finishJoin();
            engine.peerArrives ("u_b");
            // 상대 소리가 들어오는 동안
            auto flow = [&] (int ms)
            {
                for (int t = 0; t < ms; t += 200)
                {
                    if (auto* p = engine.peer ("u_b")) p->packetsReceived += 10;
                    clock.advance (200);
                }
            };
            flow (2000);
            expect (! jam.isJamServerDown() && realtimeChecks == 0 && pending.empty());

            // 엔진이 합주 서버 끊김을 알림 → 다시 붙음
            engine.serverLost();
            expect (jam.isJamServerDown());
            expectEquals (log.changes, 1);
            clock.advance (1100);
            engine.finishConnect();
            expect (! jam.isJamServerDown());
            expectEquals (log.changes, 2);
            flow (1000);

            // 상대 소리가 모두 끊김 (내 와이파이?) → 서버 실시간 연결과 합주 서버를 확인한다
            clock.advance (3500);
            expectEquals (realtimeChecks, 1);
            expectEquals ((int) pending.size(), 1);
            expectEquals (probed[0], juce::String ("127.0.0.1:10999"));
            expect (! jam.isJamServerDown(), "확인 결과가 오기 전에는 그대로");
            { auto done = pending.back(); pending.clear(); done (false); }
            expect (jam.isJamServerDown(), "합주 서버에 닿지 않으면 끊김");
            expectEquals (log.changes, 3);
            clock.advance (JamSession::reachCheckEveryMs + 100);
            expectEquals ((int) pending.size(), 1, "끊겨 있는 동안 5초마다 다시 확인");
            { auto done = pending.back(); pending.clear(); done (false); }

            // 상대 소리가 다시 들어오면 바로 내린다
            flow (1000);
            expect (! jam.isJamServerDown());
            expectEquals (log.changes, 4);
            flow (20000);
            expect (pending.empty(), "소리가 들리는 동안은 확인하지 않는다");

            // 상대만 끊겼다 (합주 서버에는 닿음): 내 연결은 정상
            clock.advance (3500);
            expectEquals ((int) pending.size(), 1);
            { auto done = pending.back(); pending.clear(); done (true); }
            expect (! jam.isJamServerDown(), "상대 쪽 문제는 내 끊김으로 보지 않는다");

            store.setCurrentRoom (std::nullopt);
            expect (! jam.isJamServerDown());
            jam.removeListener (&log);
            o.logFolder.deleteRecursively();
        }

        beginTest ("Connection: first AOO connect failing (name not found, refused, timed out) is jam server down, retried every 3 s, cleared on connect or leave (E9)");
        {
            ManualScheduler clock;
            AppStore store;
            FakeEngine engine;
            JamSession::Options o;
            o.logFolder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("meon-test-logs-connfail");
            JamSession jam (engine, store, clock, o);
            struct LinkLog : JamSession::Listener { int changes = 0; void jamServerLinkChanged() override { ++changes; } } log;
            jam.addListener (&log);

            RoomDetail room;
            room.id = "r_cf";
            room.hostId = "u_me";
            room.connection.aooHost = "aoo.invalid";
            room.connection.aooPort = 10998;
            room.connection.groupName = "g";
            room.connection.aooUsername = "u_me";
            room.connection.members = { "u_me" };
            store.setCurrentRoom (room);
            expect (jam.getPhase() == JamSession::Phase::connecting && ! jam.isJamServerDown());
            const int c0 = engine.connects;

            engine.failConnect();   // 엔진(AOO)이 붙기 실패를 알림
            expect (jam.isJamServerDown(), "처음 붙기 실패: 끊김 (\"연결이 끊겼어요\")");
            expectEquals (log.changes, 1);
            clock.advance (2999);
            expectEquals (engine.connects, c0);
            clock.advance (1);
            expectEquals (engine.connects, c0 + 1, "3초 뒤 다시");
            engine.failConnect();
            expect (jam.isJamServerDown());
            expectEquals (log.changes, 1, "실패가 이어지는 동안 한 번만 알린다");
            clock.advance (3000);
            expectEquals (engine.connects, c0 + 2);

            // 네트워크가 돌아와 붙는다 → 내리고 그룹에 들어간다
            engine.finishConnect();
            expect (! jam.isJamServerDown(), "붙으면 내린다 (\"다시 연결됐어요\")");
            expectEquals (log.changes, 2);
            engine.finishJoin();
            expect (jam.isInRoom());
            clock.advance (30000);
            expectEquals (engine.connects, c0 + 2, "붙은 뒤에는 더 시도하지 않는다");

            // 실패 중에 나가면 내린다
            store.setCurrentRoom (std::nullopt);
            room.id = "r_cf2";
            store.setCurrentRoom (room);
            engine.failConnect();
            expect (jam.isJamServerDown());
            store.setCurrentRoom (std::nullopt);
            expect (! jam.isJamServerDown() && jam.getPhase() == JamSession::Phase::idle, "나가면 내린다");
            const int c1 = engine.connects;
            clock.advance (10000);
            expectEquals (engine.connects, c1, "나간 뒤에는 다시 시도하지 않는다");
            jam.removeListener (&log);
            o.logFolder.deleteRecursively();
        }

        beginTest ("Connection: UDP blocked while connecting (AOO gives up the UDP check, 'timed out') shows as a failure with that reason, retried every 3 s, cleared on connect");
        {
            ManualScheduler clock;
            AppStore store;
            FakeEngine engine;
            JamSession::Options o;
            o.logFolder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("meon-test-logs-udp");
            struct Ev { juce::String type; juce::var props; };
            std::vector<Ev> evs;
            o.reportEvent = [&] (const juce::String& t, const juce::String&, const juce::var& p) { evs.push_back ({ t, p }); };
            JamSession jam (engine, store, clock, o);
            struct LinkLog : JamSession::Listener { int changes = 0; void jamServerLinkChanged() override { ++changes; } } log;
            jam.addListener (&log);
            using P = JamSession::ServerProblem;

            RoomDetail room;
            room.id = "r_udp";
            room.hostId = "u_me";
            room.connection.aooHost = "aoo.meon.co.kr";
            room.connection.aooPort = 10999;
            room.connection.groupName = "g";
            room.connection.aooUsername = "u_me";
            room.connection.members = { "u_me", "u_b" };
            store.setCurrentRoom (room);
            expect (jam.getPhase() == JamSession::Phase::connecting);
            const int c0 = engine.connects;

            // 직전 방 연결의 늦게 온 끊김 알림(직접 끊음, 서버가 끊김)은 그대로 무시한다
            engine.staleDisconnect();
            engine.serverLost();
            engine.connected = false;
            expect (! jam.isJamServerDown() && jam.serverProblem() == P::none);
            expectEquals (engine.connects, c0);

            // 5초: AOO 가 UDP 확인을 포기 → 실패, 이유는 UDP 막힘 (전에는 무시해서 "연결 중" 에 멈췄다)
            clock.advance (5000);
            engine.udpTimeout();
            expect (jam.isJamServerDown(), "UDP 확인 실패는 실패로 보인다");
            expect (jam.serverProblem() == P::udpBlocked);
            expectEquals (log.changes, 1);
            expect (jam.getPhase() == JamSession::Phase::connecting);
            clock.advance (3000);
            expectEquals (engine.connects, c0 + 1, "3초 뒤 다시");
            engine.udpTimeout();
            expectEquals (log.changes, 1, "같은 이유가 이어지면 한 번만 알린다");

            // 15초가 지나도 이유를 아는 실패는 그 이유 그대로 (무응답으로 덮지 않는다). 상대 상태는 0.4초마다 보므로 15.4초까지
            clock.advance (7400);
            expect (jam.serverProblem() == P::udpBlocked);
            // 상대는 15초에 연결 실패, 이유 udp_blocked (서버가 끊긴 것과 따로 센다)
            int udpFails = 0;
            for (auto& e : evs)
                if (e.type == "peer_audio_failed" && e.props.getProperty ("reason", "").toString() == "udp_blocked") ++udpFails;
            expectEquals (udpFails, 1);

            // 다음 시도는 TCP 에서 실패: 이유가 바뀌면 다시 알린다 (화면이 문구를 바꾼다)
            engine.failConnect();
            expect (jam.serverProblem() == P::lost);
            expectEquals (log.changes, 2);

            // 네트워크를 바꿔 붙는다 → 내리고 그룹에 들어간다
            engine.finishConnect();
            expect (! jam.isJamServerDown() && jam.serverProblem() == P::none);
            expectEquals (log.changes, 3);
            engine.finishJoin();
            expect (jam.isInRoom());
            const int c1 = engine.connects;
            clock.advance (60000);
            expectEquals (engine.connects, c1, "방에 들어간 뒤에는 더 시도하지 않는다");

            // 방 안에서 서버가 끊기고, 다시 붙다가 UDP 확인에 실패하면 그 이유로 안내한다
            engine.serverLost();
            expect (jam.isJamServerDown() && jam.serverProblem() == P::lost);
            clock.advance (JamSession::reconnectFirstMs);
            engine.udpTimeout();
            expect (jam.serverProblem() == P::udpBlocked);
            engine.finishConnect();
            expect (! jam.isJamServerDown() && jam.serverProblem() == P::none);

            store.setCurrentRoom (std::nullopt);
            expect (jam.serverProblem() == P::none);
            jam.removeListener (&log);
            o.logFolder.deleteRecursively();
        }

        beginTest ("Connection: not in the room 15 s after entering with no failure heard (no answer) shows as a failure and retries until in");
        {
            ManualScheduler clock;
            AppStore store;
            FakeEngine engine;
            JamSession::Options o;
            o.logFolder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("meon-test-logs-deadline");
            JamSession jam (engine, store, clock, o);
            using P = JamSession::ServerProblem;

            RoomDetail room;
            room.id = "r_dl";
            room.hostId = "u_me";
            room.connection.aooHost = "aoo.meon.co.kr";
            room.connection.aooPort = 10999;
            room.connection.groupName = "g";
            room.connection.aooUsername = "u_me";
            room.connection.members = { "u_me" };
            store.setCurrentRoom (room);
            const int c0 = engine.connects;

            // 엔진이 아무 소식이 없다 (붙지도 실패하지도 않음)
            clock.advance (JamSession::connectDeadlineMs - 1);
            expect (! jam.isJamServerDown());
            clock.advance (1);
            expect (jam.isJamServerDown() && jam.serverProblem() == P::noAnswer, "15초: 실패로 보인다");
            expectEquals (engine.connects, c0 + 1, "다시 붙어 본다");
            clock.advance (JamSession::connectDeadlineMs);
            expectEquals (engine.connects, c0 + 2, "그 뒤로도 15초마다");

            // 붙으면 내린다. 그룹 입장 답이 없으면 15초 뒤 다시 실패로 보이고 그룹에 다시 들어간다
            engine.finishConnect();
            expect (! jam.isJamServerDown() && jam.getPhase() == JamSession::Phase::joining);
            engine.group = {};   // 엔진은 그룹 입장 성공 이벤트를 받아야 그룹 이름을 적는다 (가짜는 요청 때 적어서 지운다)
            const int j0 = engine.joins;
            clock.advance (JamSession::connectDeadlineMs);
            expect (jam.isJamServerDown() && jam.serverProblem() == P::noAnswer);
            expectEquals (engine.joins, j0 + 1);
            engine.finishJoin();
            expect (jam.isInRoom() && ! jam.isJamServerDown() && jam.serverProblem() == P::none, "들어가면 내린다");
            const int c1 = engine.connects, j1 = engine.joins;
            clock.advance (60000);
            expect (engine.connects == c1 && engine.joins == j1, "방 안에서는 15초 확인을 하지 않는다");

            // 빨리 들어가면 아무 일도 없다
            store.setCurrentRoom (std::nullopt);
            room.id = "r_dl2";
            store.setCurrentRoom (room);
            engine.finishConnect();
            engine.finishJoin();
            clock.advance (JamSession::connectDeadlineMs * 2);
            expect (jam.isInRoom() && ! jam.isJamServerDown());

            // 나가면 멈춘다
            store.setCurrentRoom (std::nullopt);
            room.id = "r_dl3";
            store.setCurrentRoom (room);
            store.setCurrentRoom (std::nullopt);
            const int c2 = engine.connects;
            clock.advance (JamSession::connectDeadlineMs * 2);
            expectEquals (engine.connects, c2);
            expect (! jam.isJamServerDown());
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

            // 무음 판정(로그용, 화면 경고 없음): 송출 정지 중에는 없음 → 켜면 5초 뒤 silent
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

            // 입력 장치가 없음 (채널 0, 레벨 −100 dB, 보내기 켜짐) → 5초 뒤 로그용 inputMissing. 무음(silent)으로는 판정하지 않는다
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

        beginTest ("Room minimize (final design 5): home while in a room keeps the room, audio, chat; self-monitor identical; leave-then-proceed; kick / close");
        {
            ManualScheduler clock;
            FakeMeonServer server (clock, [] { FakeMeonServer::Options f; f.latencyMs = 10; f.scripts = false; f.fakeHostsAccept = false; return f; }());
            server.loadWorld (juce::JSON::parse (juce::String::fromUTF8 (MeonFixtures::world_json, MeonFixtures::world_jsonSize)));
            // 방 나가기(POST /rooms/{id}/leave)만 네트워크 오류로 만들 수 있는 전송층
            struct FlakyLeave : RestTransport
            {
                FlakyLeave (RestTransport& i, Scheduler& c) : inner (i), clock (c) {}
                RestTransport& inner;
                Scheduler& clock;
                bool failLeave = false;
                int leaves = 0;
                void send (const HttpRequest& r, std::function<void (HttpResponse)> done) override
                {
                    if (r.path.endsWith ("/leave"))
                    {
                        ++leaves;
                        if (failLeave) { clock.post (10, [done] { done ({}); }); return; }
                    }
                    inner.send (r, std::move (done));
                }
            } rest (server, clock);
            MeonApi api { rest, 0 };
            Realtime rt { server, clock, 0 };
            AppStore store;
            rt.addListener (&store);
            HomeController home { api, rt, store, clock };
            auto run = [&] (auto&& fn) { fn(); clock.advance (50); };
            run ([&] { api.signup ("mini@example.com", "mini_keys", "abcd123!", true, [] (auto) {}); });
            run ([&] { api.verifyEmail (server.getLastMailedCode(), [] (auto) {}); });
            MePatch p; p.nickname = juce::String ("Mini"); p.part = juce::String ("keys");
            run ([&] { api.patchMe (p, [&] (Result<Me> r) { store.setMe (r.get()); }); });
            rt.connect ([&] { return api.getTokens().accessToken; });
            clock.advance (100);

            FakeEngine engine;
            JamSession::Options o;
            o.logFolder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("meon-test-logs-minimize");
            o.logFolder.deleteRecursively();
            o.fake = &server;
            o.prefs.selfMonitor = false;   // 방 안 셀프 모니터 끔
            o.prefs.homeMonitor = true;    // "입장 전에도 내 소리 듣기" 켬: 최소화한 홈에서 이것으로 바뀌면 안 된다
            o.savePrefs = [] (const JamPrefs&) {};
            JamSession jam (engine, store, clock, o);
            jam.setHomeVisible (true);     // 로그인을 마친 상태 (HomeMonitorGate: 홈·합주 화면 모두)
            expect (engine.selfMon, "방 밖 홈: 입장 전 듣기");

            run ([&] { home.joinByCode ("Q7K2MX", [] (Result<JoinResult>) {}); });
            engine.finishConnect();
            engine.finishJoin();
            expect (jam.isInRoom() && store.state().currentRoom.has_value());
            expect (! engine.selfMon && ! jam.isSelfMonitorAudible(), "방 안: 셀프 모니터 설정값(끔)");

            // 최소화 = AppRoot 가 홈을 보이고 HomeController::setHomeVisible(true). 화면만 바뀐다
            const auto roomId = store.state().currentRoom->id;
            home.setHomeVisible (true);
            clock.advance (200);
            expect (store.state().currentRoom && store.state().currentRoom->id == roomId, "방은 그대로");
            expect (jam.isInRoom() && engine.connected && engine.group == "Q7K2MX", "오디오 연결 그대로");
            expect (! engine.selfMon && ! jam.isSelfMonitorAudible() && ! jam.isHomeMonitorAudible(), "셀프 모니터 같음 (끔)");
            expectEquals (rest.leaves, 0);
            jam.setSelfMonitor (true);
            expect (engine.selfMon, "최소화 중에 켜면 켜진다");
            home.setHomeVisible (false);   // 돌아옴
            expect (engine.selfMon && jam.isSelfMonitorAudible(), "돌아와도 같음 (켬)");
            home.setHomeVisible (true);
            expect (engine.selfMon, "다시 최소화해도 같음 (켬)");
            jam.setSelfMonitor (false);
            expect (! engine.selfMon);

            // 채팅: 최소화 중에도 받는다. 새 채팅 수는 최소화할 때 0 부터 (AppRoot::setMinimized 의 markRead)
            jam.chat().received ({ "Q7K2MX", "u_peer", "id:a", "before" });
            jam.chat().markRead();
            expectEquals (jam.chat().getUnread(), 0);
            jam.chat().received ({ "Q7K2MX", "u_peer", "id:b", "one" });
            jam.chat().received ({ "Q7K2MX", "u_peer", "id:c", "two" });
            expect (jam.chat().sendText ("mine") != ChatChannel::SendResult::failed);
            expectEquals (jam.chat().getUnread(), 2);   // 남의 메시지만
            const auto msgCount = jam.chat().getMessages().size();
            jam.chat().markRead();                       // 돌아오면 읽음
            expectEquals (jam.chat().getUnread(), 0);
            expectEquals ((int) jam.chat().getMessages().size(), (int) msgCount);   // 목록은 그대로

            // 나가고 들어가기: 나가기가 실패하면 방은 그대로, 오류를 돌려준다 (진행하지 않는다)
            rest.failLeave = true;
            bool called = false, ok = false;
            home.leaveRoomThen ([&] (Result<Done> r) { called = true; ok = r.ok(); });
            clock.advance (100);
            expect (called && ! ok, "나가기 실패");
            expect (store.state().currentRoom && jam.isInRoom(), "실패하면 방 그대로");

            // 성공: 서버가 받아 준 뒤에 방을 비우고 done(ok)
            rest.failLeave = false;
            called = ok = false;
            home.leaveRoomThen ([&] (Result<Done> r) { called = true; ok = r.ok(); });
            expect (! called && store.state().currentRoom.has_value(), "응답 전에는 방 그대로");
            clock.advance (100);
            expect (called && ok && ! store.state().currentRoom, "나간 뒤 진행");
            expect (jam.getPhase() == JamSession::Phase::idle && ! engine.connected);

            // 방에 없으면 바로 done(ok) (서버에 보내지 않는다)
            const int leavesBefore = rest.leaves;
            called = ok = false;
            home.leaveRoomThen ([&] (Result<Done> r) { called = true; ok = r.ok(); });
            expect (called && ok && rest.leaves == leavesBefore);

            // 새 방에 들어가면 새 채팅 수는 0 부터
            jam.chat().received ({ "x", "u_peer", "id:d", "stale" });
            const auto myId = store.state().me->id;
            const Room* pub = nullptr;
            for (auto& r : store.state().publicRooms)
                if (! r.isApproval() && rooms::hasRoomFor (r, myId)) { pub = &r; break; }
            if (pub == nullptr)
            {
                run ([&] { home.refreshRooms(); });
                for (auto& r : store.state().publicRooms)
                    if (! r.isApproval() && rooms::hasRoomFor (r, myId)) { pub = &r; break; }
            }
            expect (pub != nullptr);
            if (pub != nullptr)
            {
                const auto pubId = pub->id;
                run ([&] { home.joinRoom (pubId, [] (Result<JoinResult>) {}); });
                engine.finishConnect();
                engine.finishJoin();
                expect (store.state().currentRoom && store.state().currentRoom->id == pubId);
                expectEquals (jam.chat().getUnread(), 0);

                // 최소화 중 내보내짐: currentRoom 이 비고(막대가 사라짐) 엔진도 나온다
                home.setHomeVisible (true);
                server.devKickMe();
                clock.advance (200);
                expect (! store.state().currentRoom && jam.getPhase() == JamSession::Phase::idle, "강퇴");

                // 최소화 중 방장이 방을 닫음: 같은 길
                run ([&] { home.joinRoom (pubId, [] (Result<JoinResult>) {}); });
                engine.finishConnect();
                engine.finishJoin();
                if (! store.state().currentRoom)   // 내보낸 방에 다시 못 들어가면 다른 방
                    for (auto& r : store.state().publicRooms)
                        if (r.id != pubId && ! r.isApproval() && rooms::hasRoomFor (r, myId))
                        {
                            const auto id2 = r.id;
                            run ([&] { home.joinRoom (id2, [] (Result<JoinResult>) {}); });
                            engine.finishConnect();
                            engine.finishJoin();
                            break;
                        }
                expect (store.state().currentRoom.has_value());
                server.devCloseMyRoom();
                clock.advance (200);
                expect (! store.state().currentRoom && jam.getPhase() == JamSession::Phase::idle, "방 닫힘");
            }
            home.setHomeVisible (false);
            rt.removeListener (&store);
            o.logFolder.deleteRecursively();
        }

        beginTest ("Chat sender snapshot: part/nickname at receive time stay (part change, nickname change, left); unknown filled once; chat log uses it");
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
            auto u8 = [] (const char* t) { return juce::String::fromUTF8 (t); };
            run ([&] { api.signup ("snap@example.com", "snap_keys", "abcd123!", true, [] (auto) {}); });
            run ([&] { api.verifyEmail (server.getLastMailedCode(), [] (auto) {}); });
            MePatch p; p.nickname = u8 ("스냅"); p.part = juce::String ("keys");
            run ([&] { api.patchMe (p, [&] (Result<Me> r) { store.setMe (r.get()); }); });
            rt.connect ([&] { return api.getTokens().accessToken; });
            clock.advance (100);

            FakeEngine engine;
            JamSession::Options o;
            o.logFolder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("meon-test-logs-snapshot");
            o.logFolder.deleteRecursively();
            o.fake = &server;
            JamSession jam (engine, store, clock, o);
            run ([&] { api.joinRoom ("r_f_3", [&] (Result<JoinResult> r) { store.setCurrentRoom (r.get().room); }); });
            engine.finishConnect();
            engine.finishJoin();
            expect (jam.isInRoom());

            ChatChannel::Message none;
            auto msg = [&] (const juce::String& id) -> const ChatChannel::Message*
            {
                for (auto& m : jam.chat().getMessages()) if (m.id == id) return &m;
                expect (false, "메시지 없음: " + id);
                return &none;
            };
            auto memberPart = [&] (const juce::String& uid)
            {
                if (store.state().currentRoom)
                    for (auto& m : store.state().currentRoom->members) if (m.user.id == uid) return m.user.part.value_or (juce::String());
                return juce::String ("(없음)");
            };

            // 기타인 채로 보낸 메시지 → 보컬로 바꾼 뒤 보낸 메시지: 메시지마다 그때의 파트
            server.remotePeerJoined ("u_snap", u8 ("민준"), "guitar", false);
            clock.advance (50);
            jam.chat().received ({ engine.group, "u_snap", "id:s1", u8 ("기타로 보냄") });
            server.remotePeerJoined ("u_snap", {}, "vocal", false);
            clock.advance (50);
            expectEquals (memberPart ("u_snap"), juce::String ("vocal"));
            jam.chat().received ({ engine.group, "u_snap", "id:s2", u8 ("보컬로 보냄") });
            expectEquals (msg ("s1")->senderPart, juce::String ("guitar"), "파트를 바꿔도 지난 메시지는 그대로");
            expectEquals (msg ("s2")->senderPart, juce::String ("vocal"));
            expectEquals (msg ("s1")->senderName, u8 ("민준"));

            // 닉네임을 바꿔도 지난 메시지 이름은 그대로, 새 메시지는 새 이름
            server.remotePeerJoined ("u_snap", u8 ("준"), {}, false);
            clock.advance (50);
            jam.chat().received ({ engine.group, "u_snap", "id:s3", u8 ("새 이름") });
            expectEquals (msg ("s1")->senderName, u8 ("민준"));
            expectEquals (msg ("s2")->senderName, u8 ("민준"));
            expectEquals (msg ("s3")->senderName, u8 ("준"));

            // 보낸 사람이 방을 나가도 그대로 (멤버 목록에서 빠져도)
            server.remotePeerLeft ("u_snap");
            clock.advance (50);
            expectEquals (memberPart ("u_snap"), juce::String ("(없음)"));
            expect (msg ("s1")->senderPart == "guitar" && msg ("s1")->senderName == u8 ("민준"), "나간 뒤에도 남는다");
            expect (msg ("s3")->senderPart == "vocal" && msg ("s3")->senderName == u8 ("준"));

            // 받을 때 몰랐던 사람 (가짜 기간처럼 프로필이 늦게 옴): 처음 알게 된 값으로 한 번만 채운다
            jam.chat().received ({ engine.group, "u_late", "id:l1", u8 ("프로필 전") });
            expect (msg ("l1")->senderPart.isEmpty() && msg ("l1")->senderName.isEmpty(), "모르면 빈 값 (빈 원·\"멤버\")");
            server.remotePeerJoined ("u_late", u8 ("서연"), "drums", false);
            clock.advance (50);
            expectEquals (msg ("l1")->senderPart, juce::String ("drums"), "처음 알게 된 값으로 채움");
            expectEquals (msg ("l1")->senderName, u8 ("서연"));
            server.remotePeerJoined ("u_late", u8 ("연"), "bass", false);
            clock.advance (50);
            expectEquals (msg ("l1")->senderPart, juce::String ("drums"), "채운 뒤에는 덮어쓰지 않는다");
            expectEquals (msg ("l1")->senderName, u8 ("서연"));
            // 내 메시지·시스템 줄은 그대로 (스냅샷 없음)
            expect (jam.chat().sendText ("mine") != ChatChannel::SendResult::failed);
            expect (jam.chat().getMessages().back().senderPart.isEmpty());

            // 채팅 기록도 받은 순간의 이름 (몰랐으면 user id)
            store.setCurrentRoom (std::nullopt);
            clock.advance (50);
            const auto files = o.logFolder.getChildFile ("chat").findChildFiles (juce::File::findFiles, false, "*.txt");
            expectEquals (files.size(), 1);
            if (files.size() == 1)
            {
                const auto text = files[0].loadFileAsString();
                expect (text.contains (u8 ("민준: 기타로 보냄")) && text.contains (u8 ("민준: 보컬로 보냄")) && text.contains (u8 ("준: 새 이름")), text);
                expect (text.contains (u8 ("u_late: 프로필 전")), text);
            }
            rt.removeListener (&store);
            o.logFolder.deleteRecursively();
        }
    }
};

static JamTests jamTests;

} // namespace meon::core
