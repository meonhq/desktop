// MEON core - 홈 화면의 명령: 공개 방 목록(구독), 입장(바로·승인 요청·코드), 방 만들기·나가기,
// 친구 요청 보내기·수락·거절, 알림 읽음, 받은 초대 입장·닫기.
// 결과는 AppStore 에 반영하고(입장 → setCurrentRoom 등), 화면에는 Result 하나로 돌려준다. 문구는 만들지 않는다.
#pragma once

#include "../api/MeonApi.h"
#include "../api/Realtime.h"
#include "../state/AppStore.h"
#include "../util/Scheduler.h"
#include <map>
#include <set>

namespace meon::core
{

class HomeController : private AppStore::Listener
{
public:
    HomeController (MeonApi&, Realtime&, AppStore&, Scheduler&);
    ~HomeController() override;

    /** 홈이 보일 때 true: 공개 방 목록을 받고 실시간 구독. 숨으면 구독을 끊는다 (API 10) */
    void setHomeVisible (bool);
    bool isHomeVisible() const { return homeVisible; }
    void refreshRooms();
    /** 받은 친구 요청 목록 (hello 에는 없다). 로그인 뒤 한 번, 재연결마다 */
    void refreshFriendRequests();

    //==============================================================================
    // 입장 (API 5.5, 5.7)
    /** 입장 실패 모달의 종류 (5.5 표). other 는 모달 대신 일반 오류 */
    enum class JoinFailure { full, closed, network, version, hostAbsent, kicked, alreadyInRoom, other };
    static JoinFailure classify (const ApiError&);

    /** 공개 방. 200 이면 store 의 현재 방이 바뀌고, 202 면 그 방의 myJoinRequestUntil 이 바뀐다 */
    void joinRoom (const juce::String& roomId, Callback<JoinResult>);
    /** 비공개 방 코드 (대소문자 구분 없음) */
    void joinByCode (const juce::String& code, Callback<JoinResult>);
    void createRoom (const CreateRoom&, Callback<RoomDetail>);
    /** 지금 방에서 나간다. 엔진은 store 를 따라 바로 나온다 */
    void leaveRoom();
    /** 이 방에 보낸 승인 요청이 아직 유효한지 (request_until 전) */
    bool isRequestPending (const Room&) const;
    /** 친구가 있는 방 (친구 목록의 "참여"). 공개 방이면 입장, 아니면 실패 */
    void joinFriend (const Friend&, Callback<JoinResult>);

    //==============================================================================
    // 받은 초대 (토스트)
    void acceptInvite (const juce::String& inviteId, Callback<JoinResult>);
    void dismissInvite (const juce::String& inviteId);

    //==============================================================================
    // 친구 (API 7)
    /** result: sent | not_found | already_friends | already_requested | accepted | self (오류는 blocked_by_me 등) */
    void sendFriendRequest (const juce::String& username, Callback<juce::String>);
    void acceptFriendRequest (const juce::String& requestId, Callback<Done> = {});
    void declineFriendRequest (const juce::String& requestId, Callback<Done> = {});

    // 알림 (API 9): 팝오버를 열면 읽음
    void markNotificationsRead();

    //==============================================================================
    // 방 초대 (API 7.7·7.8): 친구 행의 "초대" → 3초 동안 "취소"(N초 뒤 초대가 가요) → "초대됨". 방이 바뀌면 지운다
    struct InviteState
    {
        enum class Phase { none, pending, sent };
        Phase phase = Phase::none;
        juce::int64 deliverAtMs = 0;
    };
    InviteState inviteState (const juce::String& userId) const;
    /** 실패하면 상태를 지우고 오류를 돌려준다 (already_invited 는 "초대됨") */
    void invite (const juce::String& userId, Callback<InviteSent> = {});
    void cancelInvite (const juce::String& userId);

private:
    MeonApi& api;
    Realtime& realtime;
    AppStore& store;
    Scheduler& scheduler;
    bool homeVisible = false, friendRequestsLoaded = false;
    int roomsGeneration = 0;
    struct InviteRec { InviteState state; juce::String inviteId; };
    std::map<juce::String, InviteRec> invites;
    std::set<juce::String> cancelOnArrival;   // 응답 전에 취소한 초대
    juce::String invitesRoomId;
    std::shared_ptr<bool> alive { std::make_shared<bool> (true) };

    void handleJoin (const juce::String& roomId, Result<JoinResult>, Callback<JoinResult>);
    void stateChanged (StateKey) override;
};

} // namespace meon::core
