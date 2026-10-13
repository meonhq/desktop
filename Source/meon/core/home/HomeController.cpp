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
        case E::internal: case E::badResponse:          return JoinFailure::network;
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
            store.setMyJoinRequestUntil (roomId, *r.get().requestUntil);
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
    joinRoom (f.room->id, std::move (cb));
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

bool HomeController::isRequestPending (const Room& r) const
{
    if (! r.myJoinRequestUntil) return false;
    return json::parseIsoTime (*r.myJoinRequestUntil) > scheduler.nowMs();
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

} // namespace meon::core
