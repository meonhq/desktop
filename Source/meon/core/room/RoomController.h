// MEON core - 방장 명령 (API 5.7, 6장): 방 설정 바꾸기, 방장 넘기기, 내보내기, 방 닫기, 입장 요청 수락·거절.
// 응답의 RoomDetail 로 store 의 현재 방을 덮어쓴다 (API 10.7). 문구는 만들지 않는다.
#pragma once

#include "../api/MeonApi.h"
#include "../state/AppStore.h"

namespace meon::core
{

class RoomController : private AppStore::Listener
{
public:
    RoomController (MeonApi&, AppStore&);
    ~RoomController() override;

    void patchRoom (const RoomPatch&, Callback<RoomDetail> = {});
    void transferHost (const juce::String& userId, Callback<RoomDetail> = {});
    void kick (const juce::String& userId, Callback<Done> = {});
    /** 성공하면 현재 방을 비운다 (화면은 "방을 닫았어요" 덮개) */
    void closeRoom (Callback<Done> = {});
    void acceptJoinRequest (const juce::String& requestId, Callback<RoomDetail> = {});
    void declineJoinRequest (const juce::String& requestId);
    /** 내가 방장이 되면(입장·넘겨받기) 대기 중인 요청을 받는다 */
    void refreshJoinRequests();
    /** 방금 내가 방을 닫아서 방에서 나왔는지 (한 번 읽으면 지운다). 화면은 "방을 닫았어요" 덮개를 띄운다 */
    bool takeClosedByMe() { const bool b = closedByMe; closedByMe = false; return b; }

    /** 방장 기능이 화면에 알려야 할 이유로 실패했을 때 (R11 PR3): 내보내기·방장 넘기기 target_not_in_room,
        요청 수락 room_full(요청은 남음)·request_unavailable(요청 삭제). 화면이 토스트로 알린다 */
    std::function<void (ErrorCode)> onActionFailed;

private:
    MeonApi& api;
    AppStore& store;
    bool wasHost = false, closedByMe = false;
    juce::String roomId;
    std::shared_ptr<bool> alive { std::make_shared<bool> (true) };

    juce::String currentId() const;
    void stateChanged (StateKey) override;
};

} // namespace meon::core
