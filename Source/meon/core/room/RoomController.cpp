#include "RoomController.h"

namespace meon::core
{

RoomController::RoomController (MeonApi& a, AppStore& s) : api (a), store (s)
{
    store.addListener (this);
}

RoomController::~RoomController()
{
    *alive = false;
    store.removeListener (this);
}

juce::String RoomController::currentId() const
{
    const auto& r = store.state().currentRoom;
    return r ? r->id : juce::String();
}

void RoomController::stateChanged (StateKey k)
{
    if (k != StateKey::currentRoom && k != StateKey::me) return;
    const bool host = store.state().amHost();
    const auto id = currentId();
    if (host && (! wasHost || id != roomId)) refreshJoinRequests();
    wasHost = host;
    roomId = id;
}

void RoomController::refreshJoinRequests()
{
    const auto id = currentId();
    const auto& room = store.state().currentRoom;
    if (id.isEmpty() || ! room || ! room->isApproval()) return;
    std::weak_ptr<bool> weak = alive;
    api.listJoinRequests (id, [this, weak, id] (Result<std::vector<JoinRequest>> r)
    {
        if (weak.expired() || ! r.ok() || id != currentId()) return;
        store.setJoinRequests (std::move (r.get()));
    });
}

void RoomController::patchRoom (const RoomPatch& p, Callback<RoomDetail> cb)
{
    const auto id = currentId();
    if (id.isEmpty()) return;
    std::weak_ptr<bool> weak = alive;
    api.patchRoom (id, p, [this, weak, id, cb] (Result<RoomDetail> r)
    {
        if (weak.expired()) return;
        if (r.ok() && id == currentId()) store.setCurrentRoom (r.get());
        if (cb) cb (std::move (r));
    });
}

void RoomController::transferHost (const juce::String& userId, Callback<RoomDetail> cb)
{
    const auto id = currentId();
    if (id.isEmpty()) return;
    std::weak_ptr<bool> weak = alive;
    api.transferHost (id, userId, [this, weak, id, cb] (Result<RoomDetail> r)
    {
        if (weak.expired()) return;
        if (r.ok() && id == currentId()) store.setCurrentRoom (r.get());
        if (cb) cb (std::move (r));
    });
}

void RoomController::kick (const juce::String& userId, Callback<Done> cb)
{
    const auto id = currentId();
    if (id.isEmpty()) return;
    std::weak_ptr<bool> weak = alive;
    api.kick (id, userId, [weak, cb] (Result<Done> r)
    {
        if (weak.expired()) return;
        if (cb) cb (std::move (r));
    });
}

void RoomController::closeRoom (Callback<Done> cb)
{
    const auto id = currentId();
    if (id.isEmpty()) return;
    std::weak_ptr<bool> weak = alive;
    api.closeRoom (id, [this, weak, id, cb] (Result<Done> r)
    {
        if (weak.expired()) return;
        if (r.ok() && id == currentId()) { closedByMe = true; store.setCurrentRoom (std::nullopt); }
        if (cb) cb (std::move (r));
    });
}

void RoomController::acceptJoinRequest (const juce::String& requestId, Callback<RoomDetail> cb)
{
    const auto id = currentId();
    if (id.isEmpty()) return;
    std::weak_ptr<bool> weak = alive;
    api.acceptJoinRequest (id, requestId, [this, weak, id, requestId, cb] (Result<RoomDetail> r)
    {
        if (weak.expired()) return;
        if (r.ok() && id == currentId())
        {
            store.removeJoinRequest (requestId);
            store.setCurrentRoom (r.get());
        }
        else if (! r.ok() && r.code() == ErrorCode::requestUnavailable)
        {
            store.removeJoinRequest (requestId);   // 신청자가 다른 방에 들어갔거나 오프라인 (요청 삭제됨)
        }
        if (cb) cb (std::move (r));
    });
}

void RoomController::declineJoinRequest (const juce::String& requestId)
{
    const auto id = currentId();
    if (id.isEmpty()) return;
    store.removeJoinRequest (requestId);
    api.declineJoinRequest (id, requestId, [] (Result<Done>) {});
}

} // namespace meon::core
