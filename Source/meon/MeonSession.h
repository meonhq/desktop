// MEON - 합주 세션 컨트롤러. SonobusAudioProcessor(엔진)와 새 UI 사이를 잇는다.
// 엔진 콜백은 네트워크 스레드에서 오므로 여기서 메시지 스레드로 옮긴다.
#pragma once

#include "../SonobusPluginProcessor.h"
#include "MeonTheme.h"
#include "MeonParts.h"
#include "MeonSettings.h"
#include "MeonSessionLog.h"
#include <vector>

namespace meon
{

class ServerPinger;
class PlaybackFader;

class MeonSession : private juce::AsyncUpdater,
                    private juce::Timer,
                    private SonobusAudioProcessor::ClientListener
{
public:
    enum class ServerState { Disconnected, Connecting, Connected };
    enum class RoomState   { None, Joining, Verifying, InRoom };
    enum class JoinFailure { None, InvalidCode, RoomFull, Error };

    struct Member
    {
        juce::String userName;      // 서버 사용자 이름 (닉네임#xxxx)
        juce::String displayName;   // 닉네임
        Part part = Part::None;     // 사용자 이름 끝 글자로 전달된 파트 (구버전은 None)
        int slot = -1;              // 0..3 (카드 위치). -1 이면 카드 없음
        bool pending = true;        // 아직 P2P 연결 전
        bool connected = false;
        bool joinFailed = false;    // P2P(NAT) 연결 실패
        double disconnectedSinceMs = 0.0;
        double joinedAtMs = 0.0;
        int dropCount = 0;          // 연결 끊김 횟수
        float pingMs = 0.0f, roundtripMs = 0.0f, jitterBufferMs = 0.0f;
        juce::int64 dropped = 0, resent = 0, received = 0;
        float gain = 1.0f;
        int pan = 0;                // -100(왼쪽)..100(오른쪽). 내 믹스에만 적용, 전송 안 함. 다시 들어오면 새 Member 라 0
        bool muted = false;         // 내가 이 멤버 소리를 껐는지 (setMemberMuted 로만 바뀐다. 엔진 recvActive 는 상대가 마이크를 끄면 false 가 되므로 쓰지 않는다)
        bool defaultsApplied = false;
        bool hasStats = false;
    };

    struct ChatMessage
    {
        enum Kind { Mine, Other, System };
        Kind kind = Other;
        juce::String from;      // 표시 이름
        juce::String text;
        juce::Time time;
    };

    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void sessionStateChanged() {}   // 서버 상태 / 방 상태
        virtual void membersChanged() {}        // 멤버 목록·슬롯 변화
        virtual void memberStatsChanged() {}    // 핑·지연 등 주기 갱신
        virtual void chatChanged() {}
        virtual void roomJoined() {}
        virtual void roomLeft() {}
        virtual void joinFailed (JoinFailure) {}
        virtual void playbackChanged() {}       // 파일 열기·닫기, 재생·정지 (위치는 화면이 주기적으로 읽는다)
    };

    MeonSession (SonobusAudioProcessor& processor, MeonSettings& settings, bool isPlugin);
    ~MeonSession() override;

    void addListener (Listener* l)    { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    //==============================================================================
    // 서버
    void start();                            // 닉네임이 있으면 서버 연결 (자동 재시도)
    void applyNicknameChange();              // 설정에서 닉네임 바꾼 뒤 (방 밖에서만 즉시 재연결)
    ServerState getServerState() const { return serverState; }
    bool isServerConnected() const { return serverState == ServerState::Connected; }
    /** 한 번 연결됐다가 끊겨 재연결을 기다리는 중 (연결 중 상태 포함) */
    bool isServerLost() const { return serverState != ServerState::Connected && everConnected; }
    /** 화면 표시용 서버 상태 문구. home = 홈 하단, 아니면 합주 상단 바 */
    juce::String getServerStatusText (bool home) const;
    float getServerPingMs() const;           // < 0 이면 미측정
    void setServerPingInterval (int ms);
    juce::String getServerHost() const { return DEFAULT_SERVER_HOST; }

    //==============================================================================
    // 방
    void createRoom();
    void joinRoom (const juce::String& code);
    void leaveRoom();
    RoomState getRoomState() const { return roomState; }
    bool isInRoom() const { return roomState == RoomState::InRoom; }
    bool isBusyJoining() const { return roomState == RoomState::Joining || roomState == RoomState::Verifying; }
    juce::String getRoomCode() const { return roomCode; }
    juce::String getDisplayRoomCode() const { return roomCode.isNotEmpty() ? roomCode : pendingCode; }
    juce::String getLastJoinError() const { return lastJoinError; }
    int getMemberCount() const;              // 나 포함
    bool isAlone() const { return getMemberCount() <= 1; }
    std::vector<Member> getMembers() const;  // slot 이 있는 멤버, slot 순
    const Member* getMemberInSlot (int slot) const;

    void setMemberMuted (int slot, bool muted);
    void setMemberGain (int slot, float gain);
    void setMemberPan (int slot, int pan);
    float getMemberLevelDb (int slot) const;
    float getMemberPeakLevelDb (int slot) const;

    //==============================================================================
    // 나
    bool isMyMuted() const;
    void setMyMuted (bool muted);
    float getMyInputLevelDb (int channel = -1) const;  // 장치 입력 (뮤트와 무관)
    float getMySendLevelDb() const;                    // 실제로 보내는 신호
    juce::String getDisplayName() const;
    Part getMyPart() const;                  // 설정의 파트 (고른 적 없으면 None)
    juce::String getUserName() const { return userName; }
    void setInputChannels (int start, int count);      // 독립 앱 전용

    /** 귀 테러 방지: 설정 저장 + 내 모니터 출력에 바로 적용 + 세션 로그 */
    EarGuard::Level getEarGuardLevel() const;
    void setEarGuardLevel (EarGuard::Level level);

    //==============================================================================
    // 파일 재생 (MR). SonoBus 엔진의 파일 재생을 그대로 쓴다: 소리는 내 송신에 섞여 나가고 나도 듣는다.
    // 상태는 엔진에 있으므로 플러그인 창을 다시 열어도 이어진다.
    bool loadPlaybackFile (const juce::File& file);    // 실패하면 false (지원하지 않는 형식 등)
    void closePlaybackFile();
    bool hasPlaybackFile() const;
    juce::String getPlaybackFileName() const;
    bool isPlaybackPlaying() const;
    void setPlaybackPlaying (bool play);               // 정지는 위치 유지. 끝나면 엔진이 처음으로 되돌린다
    double getPlaybackPosition() const;                // 초
    double getPlaybackLength() const;                  // 초
    void setPlaybackPosition (double seconds);         // 재생 중이면 짧게 줄였다가 옮기고 다시 키운다 (튀는 소리 방지)
    float getPlaybackGainDb() const;
    void setPlaybackGainDb (float db);                 // playbackMinDb 이하는 소리 없음 (-∞)
    static constexpr float playbackMinDb = -30.0f, playbackMaxDb = 10.0f;

    //==============================================================================
    // 채팅
    void sendChat (const juce::String& text);
    const std::vector<ChatMessage>& getChat() const { return chat; }
    int getUnreadCount() const { return unread; }
    void markChatRead() { unread = 0; }

    //==============================================================================
    // 로그
    MeonSessionLog& getLog() { return log; }
    void setAudioInfoProvider (std::function<MeonSessionLog::AudioInfo()> f) { audioInfoProvider = std::move (f); }

    /** 자모가 나뉜 한글(NFD, macOS 파일 이름)을 완성형 글자로 합친다 */
    static juce::String composeHangul (const juce::String& text);

    static juce::String generateRoomCode();
    static juce::String displayNameFor (const juce::String& userName);
    static Part partFor (const juce::String& userName);
    static bool isValidRoomCode (const juce::String& code);

    SonobusAudioProcessor& getProcessor() { return processor; }

private:
    //==============================================================================
    struct Event
    {
        enum Type { Connected, Disconnected, GroupJoined, GroupLeft, PeerPending, PeerJoined, PeerJoinFailed, PeerLeft, Chat, Error, PeerState };
        Type type;
        bool success = false;
        juce::String group, user, message;
        SBChatEvent chat;
    };

    SonobusAudioProcessor& processor;
    MeonSettings& settings;
    const bool isPlugin;
    juce::ListenerList<Listener> listeners;
    MeonSessionLog log;
    std::function<MeonSessionLog::AudioInfo()> audioInfoProvider;
    std::unique_ptr<ServerPinger> pinger;
    std::unique_ptr<PlaybackFader> fader;
    juce::File playbackFile;   // 이 세션에서 연 파일 (엔진 URL 과 같을 때 이름은 여기서)

    juce::CriticalSection eventLock;
    std::vector<Event> pendingEvents;

    ServerState serverState = ServerState::Disconnected;
    RoomState roomState = RoomState::None;
    bool wantConnected = false;
    bool everConnected = false;   // 닉네임 변경 등으로 직접 끊을 때는 다시 false
    bool creatingRoom = false;
    bool joinRequested = false;
    bool reattached = false;
    juce::String userName, roomCode, pendingCode, lastJoinError;
    double nextConnectAttemptMs = 0.0, connectDeadlineMs = 0.0, verifyDeadlineMs = 0.0, joinedAtMs = 0.0;
    double lastSampleMs = 0.0;
    int earGuardBandBase = 0, earGuardFullBase = 0;   // 세션 로그: 방에 들어온 뒤 동작 횟수만 센다
    void beginLog (const juce::String& firstEvent);
    int tickCount = 0;
    int pcmFormatIndex = -1;

    std::vector<Member> members;
    std::vector<ChatMessage> chat;
    int unread = 0;

    // ClientListener (네트워크 스레드)
    void aooClientConnected (SonobusAudioProcessor*, bool success, const juce::String& errmesg) override;
    void aooClientDisconnected (SonobusAudioProcessor*, bool success, const juce::String& errmesg) override;
    void aooClientGroupJoined (SonobusAudioProcessor*, bool success, const juce::String& group, const juce::String& errmesg) override;
    void aooClientGroupLeft (SonobusAudioProcessor*, bool success, const juce::String& group, const juce::String& errmesg) override;
    void aooClientPeerPendingJoin (SonobusAudioProcessor*, const juce::String& group, const juce::String& user) override;
    void aooClientPeerJoined (SonobusAudioProcessor*, const juce::String& group, const juce::String& user) override;
    void aooClientPeerJoinFailed (SonobusAudioProcessor*, const juce::String& group, const juce::String& user) override;
    void aooClientPeerLeft (SonobusAudioProcessor*, const juce::String& group, const juce::String& user) override;
    void aooClientError (SonobusAudioProcessor*, const juce::String& errmesg) override;
    void aooClientPeerChangedState (SonobusAudioProcessor*, const juce::String& mesg) override;
    void sbChatEventReceived (SonobusAudioProcessor*, const SBChatEvent& chatevent) override;

    void pushEvent (Event e);
    void handleAsyncUpdate() override;
    void handleEvent (const Event& e);
    void timerCallback() override;

    void connectNow();
    void performPendingRoomAction();
    void finishJoin (bool success, JoinFailure failure, const juce::String& message);
    void clearRoom (const juce::String& reason);
    void applyProcessorDefaults();
    void applyPeerDefaults (int peerIndex, Member& m);
    void applyPeerPan (int peerIndex, int pan, bool onlyIfChanged);
    void refreshStats();
    void reconcilePeers (bool adoptEnginePan = false);   // true: 에디터를 다시 열 때 엔진에 남은 패닝을 이어받는다
    int readPeerPan (int peerIndex) const;
    void writeLogSample();

    Member* findMember (const juce::String& user);
    int findPeerIndex (const juce::String& user) const;
    int firstFreeSlot() const;
    void addSystemChat (const juce::String& text);
    juce::String makeUserName() const;
    float meterDb (foleys::LevelMeterSource* src, int channel = -1) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeonSession)
};

} // namespace meon
