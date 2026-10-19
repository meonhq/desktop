// MEON core - 화면이 읽는 상태 하나(AppState)와, REST 응답·실시간 이벤트를 그 상태에 반영하는 저장소(AppStore).
// 화면은 AppState 를 읽고, 바뀐 부분(StateKey)만 알림을 받는다. 메시지 스레드 전용.
// 이벤트와 REST 응답이 다르면 REST 가 맞다 (API 10.7): 명령의 응답이 오면 컨트롤러가 set* 로 다시 덮어쓴다.
#pragma once

#include "../api/ApiTypes.h"
#include "../api/Realtime.h"

namespace meon::core
{

enum class StateKey
{
    me, config, publicRooms, friends, friendRequests, notifications, invites, currentRoom, joinRequests, link, myRtt
};

struct AppState
{
    std::optional<Me> me;
    Config config;
    bool configLoaded = false;

    std::vector<Room> publicRooms;
    bool publicRoomsLoaded = false;

    std::vector<Friend> friends;
    std::vector<FriendRequest> friendRequests;   // 알림 팝오버 (받은 친구 요청)
    bool hasUnreadNotifications = false;
    std::vector<Invite> invites;                 // 받은 초대 (토스트)

    std::optional<RoomDetail> currentRoom;
    std::vector<JoinRequest> joinRequests;       // 내가 방장일 때 받은 승인 요청

    Realtime::Link link = Realtime::Link::offline;
    OptInt myRttMs;

    const Room* findPublicRoom (const juce::String& id) const;
    const Friend* findFriend (const juce::String& userId) const;
    bool inRoom() const { return currentRoom.has_value(); }
    bool amHost() const { return currentRoom && me && currentRoom->hostId == me->id; }
};

/** 한 번 알리고 끝나는 일 (토스트·화면 전환용) */
struct Notice
{
    enum class Kind
    {
        kicked,          // room.kicked: 홈 + "방에서 내보내졌어요" (Q14)
        roomClosed,      // room.closed: 홈 + "방장이 방을 닫았어요"
        joinAccepted,    // join_request.accepted: 바로 합주 화면
        inviteReceived,  // invite.received: 토스트
        movedToOtherRoom, // 다시 연결했더니(hello) 다른 방에 있음: 같은 계정의 다른 기기가 들어간 방. 송출 정지로 들어가고 토스트
        roomLostWhileDisconnected // 끊겼다 다시 연결했더니(hello) 방이 없음: 끊긴 채 30초가 지나 서버가 방에서 내보냄. 홈 + 토스트
    };
    Kind kind;
    juce::String roomId, roomTitle;
    std::optional<Invite> invite;
};

class AppStore : public Realtime::Listener
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void stateChanged (StateKey) {}
        virtual void notice (const Notice&) {}
    };

    AppStore() = default;

    const AppState& state() const { return s; }

    void addListener (Listener* l)    { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    //==============================================================================
    // REST 결과 반영
    void setMe (const Me&);
    void setConfig (const Config&);
    void setPublicRooms (std::vector<Room>);
    void upsertPublicRoom (const Room&);
    void setFriends (std::vector<Friend>);
    /** 친구 목록 순서 (API 7.1). setFriends·friend.updated·friend.added 때마다 */
    static void sortFriends (std::vector<Friend>&);
    void setFriendRequests (std::vector<FriendRequest>);
    void removeFriendRequest (const juce::String& requestId);
    /** 지웠던 요청을 그 자리에 되돌린다 (수락·거절 실패). 그사이 다시 받아 이미 있으면 그대로 */
    void restoreFriendRequest (const FriendRequest&, size_t index);
    void setHasUnread (bool);
    void removeInvite (const juce::String& inviteId);
    void setCurrentRoom (std::optional<RoomDetail>);   // 입장·설정 변경 응답, 나가기(nullopt)
    void setJoinRequests (std::vector<JoinRequest>);
    void removeJoinRequest (const juce::String& requestId);
    void setMyRtt (int ms);
    void setMyJoinRequestUntil (const juce::String& roomId, const juce::String& until);
    /** 로그아웃·탈퇴: 모든 상태를 비운다 */
    void clear();

    /** 지난 실행을 정상 종료하며 나간 방인데 나가기가 서버에 닿지 않았다 (MeonCore 가 켤 때 알려 준다).
        다음 hello 의 current_room 이 이 방이면 들어가지 않고 onRoomLeftOnQuit 를 부른다 (부른 쪽이 나가기를 다시 보낸다).
        첫 hello 에서 한 번만 본다 */
    void setRoomLeftOnQuit (const juce::String& roomId) { roomLeftOnQuit = roomId; }
    std::function<void (const juce::String& roomId)> onRoomLeftOnQuit;

    //==============================================================================
    // Realtime::Listener
    void linkChanged (Realtime::Link) override;
    void hello (const Hello&) override;
    void event (const RealtimeEvent&) override;

private:
    AppState s;
    juce::ListenerList<Listener> listeners;
    juce::String roomLeftOnQuit;
    bool helloAfterReconnect = false;   // 끊겼다 다시 붙은 뒤 첫 hello (linkChanged: reconnecting → online)

    void changed (StateKey k) { listeners.call ([k] (Listener& l) { l.stateChanged (k); }); }
    void notify (const Notice& n) { listeners.call ([&n] (Listener& l) { l.notice (n); }); }
    void leaveRoomState();
    void upsertFriend (const Friend&);
};

} // namespace meon::core
