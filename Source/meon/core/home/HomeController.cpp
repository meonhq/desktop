#include "HomeController.h"
#include "../api/ApiJson.h"

namespace meon::core
{

HomeController::HomeController (MeonApi& a, Realtime& r, AppStore& s, Scheduler& sc)
    : api (a), realtime (r), store (s), scheduler (sc)
{
    store.addListener (this);
}

HomeController::~HomeController()
{
    *alive = false;
    store.removeListener (this);
}

void HomeController::setHomeVisible (bool on)
{
    if (homeVisible == on) return;
    homeVisible = on;
    realtime.subscribePublicRooms (on);
    if (on)
    {
        refreshRooms();
        if (! friendRequestsLoaded) refreshFriendRequests();
    }
}

void HomeController::refreshRooms()
{
    const int gen = ++roomsGeneration;
    std::weak_ptr<bool> weak = alive;
    api.listPublicRooms ([this, weak, gen] (Result<std::vector<Room>> r)
    {
        if (weak.expired() || gen != roomsGeneration || ! r.ok()) return;
        store.setPublicRooms (std::move (r.get()));
    });
}

void HomeController::refreshFriendRequests()
{
    std::weak_ptr<bool> weak = alive;
    api.listFriendRequests ([this, weak] (Result<std::vector<FriendRequest>> r)
    {
        if (weak.expired() || ! r.ok()) return;
        friendRequestsLoaded = true;
        store.setFriendRequests (std::move (r.get()));
    });
}

void HomeController::stateChanged (StateKey k)
{
    // 재연결하면(hello) 그동안 놓친 목록을 다시 받는다. 로그아웃하면 다시 받을 수 있게 표시를 지운다
    if (k == StateKey::link && store.state().link == Realtime::Link::online)
    {
        if (homeVisible) refreshRooms();
        refreshFriendRequests();
    }
    if (k == StateKey::me && ! store.state().me)
        friendRequestsLoaded = false;
    if (k == StateKey::currentRoom)
    {
        const auto& room = store.state().currentRoom;
        const auto id = room ? room->id : juce::String();
        if (id != invitesRoomId) { invites.clear(); cancelOnArrival.clear(); invitesRoomId = id; }
    }
}

//==============================================================================
HomeController::JoinFailure HomeController::classify (const ApiError& e)
{
    using E = ErrorCode;
    switch (e.code)
    {
        case E::roomFull:                               return JoinFailure::full;
        case E::roomClosed: case E::roomNotFound:
        case E::invalidCode: case E::notFound:          return JoinFailure::closed;
        case E::network: case E::timeout:
        case E::internal: case E::badResponse:          return JoinFailure::server;
        case E::clientOutdated:                         return JoinFailure::version;
        case E::hostAbsent:                             return JoinFailure::hostAbsent;
        case E::kicked:                                 return JoinFailure::kicked;
        case E::alreadyInRoom:                          return JoinFailure::alreadyInRoom;
        default:                                        return JoinFailure::other;
    }
}

void HomeController::handleJoin (const juce::String& roomId, Result<JoinResult> r, Callback<JoinResult> cb)
{
    if (r.ok())
    {
        if (r.get().room)
            store.setCurrentRoom (r.get().room);
        else if (r.get().requestUntil && roomId.isNotEmpty())
        {
            requestUntilByRoom[roomId] = json::parseIsoTime (*r.get().requestUntil);
            store.setMyJoinRequestUntil (roomId, *r.get().requestUntil);
        }
    }
    else if (classify (r.err()) == JoinFailure::closed && roomId.isNotEmpty())
    {
        // 목록에 남아 있던 닫힌 방은 지운다 (rooms.removed 를 놓쳤을 수 있다)
        refreshRooms();
    }
    if (cb) cb (std::move (r));
}

void HomeController::joinRoom (const juce::String& roomId, Callback<JoinResult> cb)
{
    std::weak_ptr<bool> weak = alive;
    api.joinRoom (roomId, [this, weak, roomId, cb] (Result<JoinResult> r)
    {
        if (weak.expired()) return;
        handleJoin (roomId, std::move (r), cb);
    });
}

void HomeController::joinByCode (const juce::String& code, Callback<JoinResult> cb)
{
    std::weak_ptr<bool> weak = alive;
    api.joinByCode (code.trim().toUpperCase(), [this, weak, cb] (Result<JoinResult> r)
    {
        if (weak.expired()) return;
        handleJoin ({}, std::move (r), cb);
    });
}

void HomeController::joinFriend (const Friend& f, Callback<JoinResult> cb)
{
    if (! f.room)
    {
        if (cb) cb (ApiError::of (ErrorCode::roomNotFound));
        return;
    }
    joinFriendRoom (*f.room, std::move (cb));
}

void HomeController::createRoom (const CreateRoom& c, Callback<RoomDetail> cb)
{
    std::weak_ptr<bool> weak = alive;
    api.createRoom (c, [this, weak, cb] (Result<RoomDetail> r)
    {
        if (weak.expired()) return;
        if (r.ok()) store.setCurrentRoom (r.get());
        if (cb) cb (std::move (r));
    });
}

void HomeController::leaveRoom()
{
    const auto& room = store.state().currentRoom;
    if (! room) return;
    const auto id = room->id;
    store.setCurrentRoom (std::nullopt);
    api.leaveRoom (id, [] (Result<Done>) {});
}

HomeController::QuitLeave HomeController::leaveOnQuit (int timeoutMs)
{
    const auto& st = store.state();
    if (! st.currentRoom) return QuitLeave::notInRoom;
    if (st.link == Realtime::Link::replaced) return QuitLeave::skipped;
    return api.leaveRoomBlocking (st.currentRoom->id, timeoutMs) ? QuitLeave::left : QuitLeave::failed;
}

bool HomeController::isRequestPending (const Room& r) const
{
    if (r.myJoinRequestUntil && json::parseIsoTime (*r.myJoinRequestUntil) > scheduler.nowMs()) return true;
    auto it = requestUntilByRoom.find (r.id);
    return it != requestUntilByRoom.end() && it->second > scheduler.nowMs();
}

bool HomeController::isRequestPending (const juce::String& roomId) const
{
    if (const auto* r = store.state().findPublicRoom (roomId)) return isRequestPending (*r);
    auto it = requestUntilByRoom.find (roomId);
    return it != requestUntilByRoom.end() && it->second > scheduler.nowMs();
}

HomeController::FriendJoin HomeController::friendJoinState (const Friend& f) const
{
    // 비공개 방이면 room 이 비어 있다 (위치 비공개, API 2.10) → 버튼 없음
    if (f.status != "in_room" || ! f.room) return FriendJoin::none;
    return roomJoinState (*f.room);
}

HomeController::FriendJoin HomeController::roomJoinState (const RoomRef& ref) const
{
    if (! ref.joinable) return FriendJoin::none;   // 정원이 찼거나 차단
    // 입장 방식은 공개 방 목록의 그 방을 따른다 (rooms.updated 로 바뀌면 바로 따라간다). 카드와 같은 순서:
    // 바로 입장이면 "참여", 승인이 필요하면 요청을 보낸 뒤 request_until 까지 "요청 보냄"
    const auto* room = store.state().findPublicRoom (ref.id);
    if (room != nullptr && ! room->isApproval()) return FriendJoin::join;
    if (isRequestPending (ref.id)) return FriendJoin::requested;
    return room != nullptr ? FriendJoin::request : FriendJoin::join;   // 목록에 없으면 모른다 → "참여" (승인 방이면 서버가 요청으로 받는다)
}

void HomeController::joinFriendRoom (const RoomRef& ref, Callback<JoinResult> cb)
{
    const auto st = roomJoinState (ref);
    if (st == FriendJoin::requested) return;   // "요청 보냄" 동안은 다시 보내지 않는다 (방 목록 카드와 같은 규칙)
    joinRoom (ref.id, std::move (cb));
}

//==============================================================================
void HomeController::acceptInvite (const juce::String& inviteId, Callback<JoinResult> cb)
{
    juce::String roomId;
    for (auto& i : store.state().invites)
        if (i.id == inviteId) roomId = i.room.id;
    store.removeInvite (inviteId);
    std::weak_ptr<bool> weak = alive;
    api.acceptInvite (inviteId, [this, weak, roomId, cb] (Result<JoinResult> r)
    {
        if (weak.expired()) return;
        handleJoin (roomId, std::move (r), cb);
    });
}

void HomeController::dismissInvite (const juce::String& inviteId)
{
    store.removeInvite (inviteId);
    api.dismissInvite (inviteId, [] (Result<Done>) {});
}

//==============================================================================
void HomeController::sendFriendRequest (const juce::String& username, Callback<juce::String> cb)
{
    api.sendFriendRequest (username.trim().trimCharactersAtStart ("@").toLowerCase(), std::move (cb));
}

void HomeController::acceptFriendRequest (const juce::String& requestId, Callback<Done> cb)
{
    store.removeFriendRequest (requestId);
    api.acceptFriendRequest (requestId, [cb] (Result<Done> r) { if (cb) cb (std::move (r)); });
}

void HomeController::declineFriendRequest (const juce::String& requestId, Callback<Done> cb)
{
    store.removeFriendRequest (requestId);
    api.declineFriendRequest (requestId, [cb] (Result<Done> r) { if (cb) cb (std::move (r)); });
}

void HomeController::markNotificationsRead()
{
    if (! store.state().hasUnreadNotifications) return;
    store.setHasUnread (false);
    api.markNotificationsRead ([] (Result<Done>) {});
}

//==============================================================================
HomeController::InviteState HomeController::inviteState (const juce::String& userId) const
{
    auto it = invites.find (userId);
    if (it == invites.end()) return {};
    auto st = it->second.state;
    if (st.phase == InviteState::Phase::pending && st.deliverAtMs > 0 && scheduler.nowMs() >= st.deliverAtMs)
        st.phase = InviteState::Phase::sent;
    return st;
}

void HomeController::invite (const juce::String& userId, Callback<InviteSent> cb)
{
    if (inviteState (userId).phase != InviteState::Phase::none) return;
    auto& rec = invites[userId];
    rec = {};
    rec.state.phase = InviteState::Phase::pending;
    rec.state.deliverAtMs = scheduler.nowMs() + 3000;   // 응답이 오면 서버의 deliver_at 으로 바꾼다
    std::weak_ptr<bool> weak = alive;
    api.sendInvite (userId, [this, weak, userId, cb] (Result<InviteSent> r)
    {
        if (weak.expired()) return;
        if (cancelOnArrival.erase (userId) > 0)
        {
            if (r.ok()) api.cancelInvite (r.get().id, [] (Result<Done>) {});
            if (cb) cb (std::move (r));
            return;
        }
        auto it = invites.find (userId);
        if (it == invites.end()) { if (cb) cb (std::move (r)); return; }
        auto& rec = it->second;
        if (r.ok())
        {
            rec.inviteId = r.get().id;
            if (const auto at = json::parseIsoTime (r.get().deliverAt); at > 0) rec.state.deliverAtMs = at;
        }
        else if (r.code() == ErrorCode::alreadyInvited)
        {
            rec.state.phase = InviteState::Phase::sent;
            rec.state.deliverAtMs = 0;
        }
        else
        {
            invites.erase (it);
        }
        if (cb) cb (std::move (r));
    });
}

void HomeController::cancelInvite (const juce::String& userId)
{
    auto it = invites.find (userId);
    if (it == invites.end() || inviteState (userId).phase != InviteState::Phase::pending) return;
    auto& rec = it->second;
    if (rec.inviteId.isEmpty()) { cancelOnArrival.insert (userId); invites.erase (it); return; }   // 응답 전: 응답이 오면 취소
    const auto id = rec.inviteId;
    invites.erase (it);   // 화면은 바로 "초대"로
    std::weak_ptr<bool> weak = alive;
    api.cancelInvite (id, [this, weak, userId] (Result<Done> r)
    {
        if (weak.expired() || r.ok()) return;
        if (r.code() == ErrorCode::alreadyDelivered)
        {
            auto& rec = invites[userId];
            rec.state.phase = InviteState::Phase::sent;
            rec.state.deliverAtMs = 0;
        }
    });
}

} // namespace meon::core
