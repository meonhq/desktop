// MEON core - 실시간 이벤트 (API.md 10장). RealtimeTransport 위에서 인증·하트비트·재연결·seq 검사·구독을 맡는다.
// 메시지 스레드 전용.
#pragma once

#include "ApiTypes.h"
#include "Transport.h"
#include "../util/Scheduler.h"

namespace meon::core
{

/** 10.6 이벤트 하나. type 에 따라 해당 필드만 채운다. */
struct RealtimeEvent
{
    enum class Kind
    {
        roomsAdded, roomsUpdated, roomsRemoved,
        roomUpdated, roomKicked, roomClosed,
        joinRequestReceived, joinRequestRemoved, joinRequestAccepted,
        friendUpdated, friendAdded, friendRemoved, friendRequestReceived,
        inviteReceived, inviteExpired,
        configUpdated,
        unknown
    };

    Kind kind = Kind::unknown;
    juce::String type;
    int seq = 0;

    std::optional<Room> room;              // rooms.added / rooms.updated
    std::optional<RoomDetail> roomDetail;  // room.updated / join_request.accepted
    juce::String roomId;                   // rooms.removed / room.kicked / room.closed
    juce::String reason;                   // room.closed
    std::optional<JoinRequest> joinRequest;
    juce::String requestId;                // join_request.removed
    std::optional<Friend> friendInfo;      // friend.updated / friend.added
    juce::String userId;                   // friend.removed
    std::optional<FriendRequest> friendRequest;
    std::optional<Invite> invite;
    juce::String inviteId;                 // invite.expired
    std::optional<Config> config;

    static Kind kindFromType (const juce::String&);
    /** {"type","seq","data"} → 이벤트. 해석하지 못하면 false */
    static bool decode (const juce::var& message, RealtimeEvent& out);
    /** 이벤트 → {"type","seq","data"} (가짜 서버가 보낼 때) */
    static juce::var encode (const juce::String& type, int seq, const juce::var& data);
};

class Realtime : private RealtimeTransport::Sink
{
public:
    /** replaced: 같은 계정이 다른 곳(앱·플러그인)에서 접속해서 서버가 이 연결을 닫았다 (닫는 코드 4000, API 10.1).
        다시 접속하지 않는다. 사용자가 connect() 로 직접 다시 붙을 때까지 그대로 */
    enum class Link { offline, connecting, online, reconnecting, replaced };

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void linkChanged (Link) {}
        /** 연결·재연결·resync 때 전체 상태 (10.4) */
        virtual void hello (const Hello&) {}
        virtual void event (const RealtimeEvent&) {}
        /** auth 단계 거부: "unauthorized" | "email_not_verified" | "client_outdated". 재연결은 멈춘다 */
        virtual void authRejected (const juce::String& code) { juce::ignoreUnused (code); }
        /** 서버가 세션을 폐기했다 (닫는 코드 4001: 로그아웃·비밀번호 변경·refresh 재사용 감지·탈퇴). 재연결은 멈춘다 */
        virtual void sessionRevoked() {}
    };

    Realtime (RealtimeTransport&, Scheduler&, int buildNumber);
    ~Realtime() override;

    /** auth 메시지의 선택 필드 (API 10.1): mode "app" | "plugin", os "mac" | "windows" | "linux". 비우면 보내지 않는다.
        서버는 행동 기록에만 쓴다 (플랫폼·빌드별로 남은 옛 앱 보기, 배포 설계 4.2) */
    void setClientInfo (const juce::String& mode, const juce::String& os) { clientMode = mode; clientOs = os; }

    void addListener (Listener* l)      { listeners.add (l); }
    void removeListener (Listener* l)   { listeners.remove (l); }

    /** 연결 시작 (이미 연결돼 있으면 끊고 다시). accessToken 은 연결·재연결할 때마다 다시 묻는다 */
    void connect (std::function<juce::String()> accessToken);
    void disconnect();
    Link getLink() const { return link; }

    /** 홈 화면이 보일 때만 true (10.5). 재연결 뒤에도 자동으로 다시 구독한다 */
    void subscribePublicRooms (bool);
    void resync();
    /** 연결이 살아 있는지 지금 확인한다 (합주 상대의 소리가 모두 끊겼을 때 등). ping 을 보내고 pongTimeoutMs 안에
        아무것도 오지 않으면 끊긴 것으로 보고 다시 연결한다. 확인 중이거나 checkGapMs 안에 확인했으면 아무것도 하지 않는다 */
    void checkNow();

    static constexpr int heartbeatMs = 20000;
    static constexpr int maxBackoffMs = 30000;
    /** 이만큼 아무것도 받지 못하면 연결이 죽은 것으로 보고 다시 연결한다 (서버도 45초, API 10.3) */
    static constexpr int silenceLimitMs = 45000;
    /** ping 을 보낸 뒤 이만큼 아무것도 오지 않으면 끊긴 것으로 본다. 와이파이가 조용히 끊기면 TCP 는 한참 동안
        오류를 내지 않아서, 하트비트 주기(20초, API 10.3)는 그대로 두고 답이 오는지로 판단한다 */
    static constexpr int pongTimeoutMs = 6000;
    static constexpr int checkGapMs = 10000;

    /** 닫는 코드 (API 10.1) */
    static constexpr int closeReplaced = 4000, closeSessionRevoked = 4001, closeSlow = 4002;

private:
    RealtimeTransport& transport;
    Scheduler& scheduler;
    const int build;
    juce::String clientMode, clientOs;
    juce::ListenerList<Listener> listeners;
    std::function<juce::String()> tokenProvider;
    Link link = Link::offline;
    bool wanted = false, transportOpen = false, publicRooms = false;
    int lastSeq = 0, backoffMs = 1000, heartbeatTask = 0, reconnectTask = 0, pongTask = 0;
    juce::int64 lastReceivedMs = 0, lastCheckMs = -checkGapMs;
    std::shared_ptr<bool> alive { std::make_shared<bool> (true) };

    void setLink (Link);
    void openTransport();
    void scheduleReconnect();
    void scheduleHeartbeat();
    void sendPing();
    void dropDeadLink (const juce::String& why);
    void sendSubscription();

    void transportOpened() override;
    void transportMessage (const juce::var&) override;
    void transportClosed (int code) override;

    JUCE_DECLARE_NON_COPYABLE (Realtime)
};

} // namespace meon::core
