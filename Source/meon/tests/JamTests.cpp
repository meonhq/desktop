// 합주 로직: PeerState, SilenceDetector, ChatChannel, JamSession (가짜 엔진 + 가짜 서버)
#include "../core/jam/JamSession.h"
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
        bool connect (const juce::String&, int, const juce::String& u, const juce::String&) override { user = u; return true; }
        void finishConnect() { connected = true; ls.call ([] (Listener& l) { l.engineConnected (true, {}); }); }
        void disconnect() override { connected = false; group = {}; peerList.clear(); }
        bool isConnected() const override { return connected; }
        juce::String connectedUser() const override { return user; }
        juce::String joinedGroup() const override { return group; }
        bool joinGroup (const juce::String& g, const juce::String&) override { group = g; return true; }
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
            // 멤버는 있지만 아무도 연결되지 않음 (가짜 멤버만 있는 방): 혼자와 같다
            ch.setOthers ({ "u_fake" });
            ch.setAnyoneConnected (false);
            expect (ch.sendText ("solo") == ChatChannel::SendResult::localOnly);
            expect (out.empty());
            expectEquals ((int) ch.getMessages().size(), 2);
            ch.setAnyoneConnected (true);
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

            // 버그 17 재현: 입력 장치가 없음 (채널 0, 레벨 −100 dB, 보내기 켜짐) → 5초 뒤 "입력 장치 없음"
            engine.inLevel = 0.0f;
            engine.inputChannels = 0;
            clock.advance (5600);
            expect (jam.isInputMissing() && ! jam.isInputSilent() && jam.hasInputWarning());
            engine.inputChannels = 2;
            engine.inLevel = 0.1f;
            clock.advance (JamSession::tickMs);
            expect (! jam.hasInputWarning());

            // 출력 장치 없음: 5초 이어지면 모니터 카드 안내, 장치가 열리면 바로 사라진다. 입력 경고와 따로 논다
            expect (! jam.isOutputMissing());
            engine.outputOk = false;
            clock.advance (4000);
            expect (! jam.isOutputMissing());
            clock.advance (1600);
            expect (jam.isOutputMissing() && ! jam.hasInputWarning());
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

            // 출력 장치가 없는 채로 방을 나오면 안내도 꺼진다 (방 밖에서는 켜지지 않는다)
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
            jam.setHomeMonitor (true);                          // 버그 28: 홈에서 듣기를 켜면 방 밖에서도
            expect (engine.selfMon && jam.isHomeMonitorAudible());
            jam.setHomeMonitor (false);
            expect (! engine.connected && engine.group.isEmpty() && ! engine.allowEnabled);
            expect (! engine.file && ! engine.filePlaying && ! jam.hasFile());
            rt.removeListener (&store);
            o.logFolder.deleteRecursively();
        }
    }
};

static JamTests jamTests;

} // namespace meon::core
