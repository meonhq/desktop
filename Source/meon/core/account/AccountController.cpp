#include "AccountController.h"

namespace meon::core
{

AccountController::AccountController (MeonApi& a, AppStore& s, AuthController& au) : api (a), store (s), auth (au) {}

AccountController::~AccountController() { *alive = false; }

void AccountController::loadProfile (const juce::String& userId, Callback<UserProfile> cb)
{
    std::weak_ptr<bool> weak = alive;
    api.getUser (userId, [weak, cb] (Result<UserProfile> r) { if (! weak.expired() && cb) cb (std::move (r)); });
}

void AccountController::patch (const MePatch& p, Callback<Me> cb)
{
    std::weak_ptr<bool> weak = alive;
    api.patchMe (p, [this, weak, cb] (Result<Me> r)
    {
        if (weak.expired()) return;
        if (r.ok()) store.setMe (r.get());
        if (cb) cb (std::move (r));
    });
}

void AccountController::setNickname (const juce::String& n, Callback<Me> cb)       { MePatch p; p.nickname = n.trim(); patch (p, std::move (cb)); }
void AccountController::setPart (const juce::String& v, Callback<Me> cb)           { MePatch p; p.part = v; patch (p, std::move (cb)); }
void AccountController::setFriendRequests (const juce::String& v, Callback<Me> cb) { MePatch p; p.friendRequests = v; patch (p, std::move (cb)); }
void AccountController::setRoomInvites (const juce::String& v, Callback<Me> cb)    { MePatch p; p.roomInvites = v; patch (p, std::move (cb)); }

void AccountController::changePassword (const juce::String& current, const juce::String& next, Callback<Done> cb)
{
    std::weak_ptr<bool> weak = alive;
    api.changePassword (current, next, [weak, cb] (Result<Done> r) { if (! weak.expired() && cb) cb (std::move (r)); });
}

void AccountController::requestEmailChange (const juce::String& newEmail, Callback<Done> cb)
{
    pendingEmail = newEmail.trim();
    std::weak_ptr<bool> weak = alive;
    api.requestEmailChange (pendingEmail, {}, [weak, cb] (Result<Done> r) { if (! weak.expired() && cb) cb (std::move (r)); });
}

void AccountController::resendEmailChange (Callback<Done> cb)
{
    if (pendingEmail.isEmpty()) return;
    requestEmailChange (pendingEmail, std::move (cb));
}

void AccountController::verifyEmailChange (const juce::String& code, Callback<Me> cb)
{
    std::weak_ptr<bool> weak = alive;
    api.verifyEmailChange (code, [this, weak, cb] (Result<Me> r)
    {
        if (weak.expired()) return;
        if (r.ok()) { store.setMe (r.get()); pendingEmail = {}; }
        if (cb) cb (std::move (r));
    });
}

void AccountController::removeFriend (const juce::String& userId, Callback<Done> cb)
{
    std::weak_ptr<bool> weak = alive;
    api.removeFriend (userId, [this, weak, userId, cb] (Result<Done> r)
    {
        if (weak.expired()) return;
        if (r.ok())
        {
            auto list = store.state().friends;   // friend.removed 이벤트를 기다리지 않고 바로 뺀다
            list.erase (std::remove_if (list.begin(), list.end(), [&] (const Friend& f) { return f.user.id == userId; }), list.end());
            store.setFriends (std::move (list));
        }
        if (cb) cb (std::move (r));
    });
}

void AccountController::block (const juce::String& userId, Callback<Done> cb)
{
    std::weak_ptr<bool> weak = alive;
    api.block (userId, [weak, cb] (Result<Done> r) { if (! weak.expired() && cb) cb (std::move (r)); });   // 친구는 차단할 수 없다 (API 8.1)
}

void AccountController::unblock (const juce::String& userId, Callback<Done> cb)
{
    std::weak_ptr<bool> weak = alive;
    api.unblock (userId, [weak, cb] (Result<Done> r) { if (! weak.expired() && cb) cb (std::move (r)); });
}

void AccountController::listBlocks (Callback<std::vector<Block>> cb)
{
    std::weak_ptr<bool> weak = alive;
    api.listBlocks ([weak, cb] (Result<std::vector<Block>> r) { if (! weak.expired() && cb) cb (std::move (r)); });
}

void AccountController::logout()
{
    auth.logout();
}

void AccountController::deleteAccount (Callback<Done> cb)
{
    std::weak_ptr<bool> weak = alive;
    api.deleteAccount ({}, [this, weak, cb] (Result<Done> r)
    {
        if (weak.expired()) return;
        if (cb) cb (r);
        if (r.ok()) auth.logout();
    });
}

} // namespace meon::core
