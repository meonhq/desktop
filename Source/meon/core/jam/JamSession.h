// MEON core - 합주. AppStore 의 현재 방(currentRoom)을 따라 엔진을 붙이고 뗀다.
//   방에 들어가면(REST 입장 성공 → store.setCurrentRoom) Connection 으로 AOO 서버에 접속 → 그룹 입장
//   room.updated 마다 Connection.members 로 엔진 허용 목록(E1)·채팅 상대를 갱신
//   방에서 나오면(나가기·추방·방 닫힘) 그룹을 나가고 접속을 끊는다
// 멤버별 상태(PeerState), 무음 경고(SilenceDetector), 채팅(ChatChannel), 세션 로그도 여기서.
// 가짜 기간: 실제 AOO 상대를 가짜 서버의 방 멤버로 올린다 (FakeAooDirectory 역할, 프로필은 채팅 태그로 교환).
#pragma once

#include "Engine.h"
#include "PeerState.h"
#include "SilenceDetector.h"
#include "ChatChannel.h"
#include "../state/AppStore.h"
#include "../log/SessionLog.h"
#include "../api/fake/FakeMeonServer.h"
#include <map>

namespace meon::core
{

/** 설정 파일에 남기는 합주 값 (REMAKE_PLAN 7장) */
struct JamPrefs
{
    bool sendMuted = false;                       // 앱만 (플러그인은 프로젝트 상태 = 엔진 값)
    float sendGainDb = 0.0f;                      // -30 ~ +10
    Engine::BufferMode bufferMode = Engine::BufferMode::latency;
    bool selfMonitor = false;                     // 기본 끔 (앱·플러그인 모두, Q30)
    float selfMonitorDb = 0.0f;                   // 합주 화면 내 행의 볼륨 (-60 ~ +6)
    bool keepMasterVolume = true;
    float masterDb = 0.0f;                        // -30 ~ +6 (Q10)
    int inputChannelStart = 0, inputChannelCount = 1;   // 앱만 (Q8: 입력 채널 위치)
    int earGuard = 1;                             // 0 끔 / 1 보통 / 2 강하게
};

class JamSession : private AppStore::Listener,
                   private Engine::Listener,
                   private ChatChannel::Listener
{
public:
    enum class Phase { idle, connecting, joining, inRoom };

    struct Member
    {
        juce::String userId, nickname, part;
        bool isMe = false, isHost = false;
        PeerState::State state = PeerState::State::connecting;
        int dropCount = 0;
        std::optional<int> pingMs;
        bool muted = false;
        float gainDb = 0.0f;
        int pan = 0;
        int channels = 1;
    };

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void jamPhaseChanged (Phase) {}
        virtual void jamMembersChanged() {}     // 목록·상태
        virtual void jamStatsChanged() {}       // 핑 등 (0.4초마다)
        virtual void jamSilenceChanged (bool warning) { juce::ignoreUnused (warning); }
        virtual void jamFileChanged() {}        // 파일 열기·닫기·재생·정지
    };

    struct Options
    {
        bool plugin = false;
        juce::File logFolder;
        JamPrefs prefs;
        std::function<void (const JamPrefs&)> savePrefs;
        /** 다른 인스턴스(앱·플러그인)가 설정 파일의 귀 테러 방지 단계를 바꿨으면 그 값, 아니면 -1 (버그 #18, 1초마다) */
        std::function<int()> pollEarGuard;
        FakeMeonServer* fake = nullptr;           // 가짜 기간에만
    };

    JamSession (Engine&, AppStore&, Scheduler&, Options);
    ~JamSession() override;

    void addListener (Listener* l)    { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    Phase getPhase() const { return phase; }
    bool isInRoom() const { return phase == Phase::inRoom; }
    bool isEngineConnected() const { return engine.isConnected(); }
    juce::String roomId() const { return currentRoomId; }

    // 멤버 (나 포함, 서버 목록 순)
    std::vector<Member> members() const;
    float memberLevel (const juce::String& userId, int channel) const;
    void setMemberMuted (const juce::String& userId, bool);
    void setMemberGainDb (const juce::String& userId, float db);   // -60 ~ +6
    void setMemberPan (const juce::String& userId, int pan);
    void resetMember (const juce::String& userId);

    // 나
    bool isSendMuted() const { return engine.isSendMuted(); }
    void setSendMuted (bool);
    float getSendGainDb() const { return prefs.sendGainDb; }
    void setSendGainDb (float);
    bool isSelfMonitor() const { return prefs.selfMonitor; }
    float getSelfMonitorDb() const { return prefs.selfMonitorDb; }
    void setSelfMonitorDb (float);
    void setSelfMonitor (bool);
    float getMasterDb() const { return masterDb; }
    void setMasterDb (float);
    bool isMonitorMuted() const { return monitorMuted; }
    void setMonitorMuted (bool);
    Engine::BufferMode getBufferMode() const { return prefs.bufferMode; }
    void setBufferMode (Engine::BufferMode);
    const JamPrefs& getPrefs() const { return prefs; }
    int getEarGuardLevel() const { return prefs.earGuard; }
    void setEarGuardLevel (int);
    /** 설정 화면의 기본값 바꾸기 (다음 입장부터) */
    void setDefaultBufferMode (Engine::BufferMode m) { prefs.bufferMode = m; savePrefs(); }
    void setKeepMasterVolume (bool b) { prefs.keepMasterVolume = b; savePrefs(); }
    /** 1초마다 (방 밖에서도): 다른 인스턴스가 바꾼 귀 테러 방지 단계를 따라간다 */
    void pollSharedPrefs();
    /** 앱: 입력 채널 위치·모노/스테레오 (합주 중이면 바로) */
    void setInputChannels (int start, int count);

    float inputLevel (int ch) const   { return engine.inputLevel (ch); }
    float sendLevel (int ch) const    { return engine.sendLevel (ch); }
    float monitorLevel (int ch) const { return engine.monitorLevel (ch); }
    int sendChannelCount() const      { return engine.sendChannelCount(); }
    bool isInputSilent() const        { return silence.isWarning(); }

    // 파일 재생 (내 소리와 섞여 모두에게)
    bool loadFile (const juce::File&);
    void closeFile();
    bool hasFile() const                { return engine.hasFile(); }
    juce::String fileName() const       { return engine.fileName(); }
    bool isFilePlaying() const          { return engine.isFilePlaying(); }
    void setFilePlaying (bool);
    double filePosition() const         { return engine.filePosition(); }
    double fileLength() const           { return engine.fileLength(); }
    void setFilePosition (double s)     { engine.setFilePosition (s); }
    float fileGainDb() const            { return engine.fileGainDb(); }
    void setFileGainDb (float db)       { engine.setFileGainDb (db); }

    ChatChannel& chat() { return chatChannel; }
    SessionLog& log() { return sessionLog; }

    static constexpr int tickMs = 400;
    static constexpr int sampleMs = 5000;

private:
    Engine& engine;
    AppStore& store;
    Scheduler& scheduler;
    Options options;
    JamPrefs prefs;
    juce::ListenerList<Listener> listeners;
    std::shared_ptr<bool> alive { std::make_shared<bool> (true) };

    Phase phase = Phase::idle;
    juce::String currentRoomId, myId;
    Connection connection;
    float masterDb = 0.0f;
    bool monitorMuted = false;

    struct Mix { bool muted = false; float gainDb = 0.0f; int pan = 0; bool defaultsApplied = false; };
    std::map<juce::String, PeerState> peerStates;
    std::map<juce::String, Mix> mix;
    SilenceDetector silence;
    ChatChannel chatChannel;
    SessionLog sessionLog;

    int tickTask = 0, retryTask = 0, pollTask = 0;
    void schedulePoll();
    juce::int64 lastSampleAt = 0;
    float sendPeakDb = -100.0f, inputPeakDb = -100.0f;
    // 서버 끊김 기록 (버그 #12): 상태가 바뀔 때만
    juce::int64 lostAt = 0;
    int reconnectAttempts = 0;
    bool serverLost = false;

    void savePrefs();
    void enter (const RoomDetail&);
    void exit (const juce::String& reason);
    void connectEngine();
    void joinGroup();
    void roomUpdated (const RoomDetail&);
    void applyAllowed();
    void scheduleTick();
    void tick();
    void writeSample (juce::int64 now);
    void sendMyProfile();
    juce::String nameOf (const juce::String& userId) const;
    void setPhase (Phase);
    void applyMonitor();

    // AppStore::Listener
    void stateChanged (StateKey) override;
    // Engine::Listener
    void engineConnected (bool ok, const juce::String& message) override;
    void engineDisconnected (const juce::String& message) override;
    void engineGroupJoined (bool ok, const juce::String& group, const juce::String& message) override;
    void engineGroupLeft (const juce::String& group) override;
    void enginePeerJoined (const juce::String& user) override;
    void enginePeerJoinFailed (const juce::String& user) override;
    void enginePeerLeft (const juce::String& user) override;
    void engineChat (const EngineChat&) override;
    // ChatChannel::Listener
    void chatProfile (const juce::String& user, const juce::var& profile) override;
    void chatAckMissing (const juce::String& id, const juce::StringArray& missing) override;

    JUCE_DECLARE_NON_COPYABLE (JamSession)
};

} // namespace meon::core
