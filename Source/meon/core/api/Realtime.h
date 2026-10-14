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
    enum class Link { offline, connecting, online, reconnecting };

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void linkChanged (Link) {}
        /** 연결·재연결·resync 때 전체 상태 (10.4) */
        virtual void hello (const Hello&) {}
        virtual void event (const RealtimeEvent&) {}
        /** auth 단계 거부: "unauthorized" | "email_not_verified" | "client_outdated". 재연결은 멈춘다 */
        virtual void authRejected (const juce::String& code) { juce::ignoreUnused (code); }
    };

    Realtime (RealtimeTransport&, Scheduler&, int buildNumber);
    ~Realtime() override;

    void addListener (Listener* l)      { listeners.add (l); }
    void removeListener (Listener* l)   { listeners.remove (l); }

    /** 연결 시작 (이미 연결돼 있으면 끊고 다시). accessToken 은 연결·재연결할 때마다 다시 묻는다 */
    void connect (std::function<juce::String()> accessToken);
    void disconnect();
    Link getLink() const { return link; }

    /** 홈 화면이 보일 때만 true (10.5). 재연결 뒤에도 자동으로 다시 구독한다 */
    void subscribePublicRooms (bool);
    void resync();

    static constexpr int heartbeatMs = 20000;
    static constexpr int maxBackoffMs = 30000;

private:
    RealtimeTransport& transport;
    Scheduler& scheduler;
    const int build;
    juce::ListenerList<Listener> listeners;
    std::function<juce::String()> tokenProvider;
    Link link = Link::offline;
    bool wanted = false, transportOpen = false, publicRooms = false;
    int lastSeq = 0, backoffMs = 1000, heartbeatTask = 0, reconnectTask = 0;
    std::shared_ptr<bool> alive { std::make_shared<bool> (true) };

    void setLink (Link);
    void openTransport();
    void scheduleReconnect();
    void scheduleHeartbeat();
    void sendSubscription();

    void transportOpened() override;
    void transportMessage (const juce::var&) override;
    void transportClosed() override;

    JUCE_DECLARE_NON_COPYABLE (Realtime)
};

} // namespace meon::core
