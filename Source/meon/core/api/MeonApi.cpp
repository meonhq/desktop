#include "MeonApi.h"
#include "ApiJson.h"

namespace meon::core
{

using juce::String;
using juce::var;

ApiError errorFromResponse (const HttpResponse& r)
{
    if (r.status == 0)
        return ApiError::of (r.timedOut ? ErrorCode::timeout : ErrorCode::network);
    auto e = json::decodeError (r.status, r.body);
    e.retryAfterSec = r.retryAfterSec;
    return e;
}

static bool isSuccess (int status) { return status >= 200 && status < 300; }

static String enc (const String& s) { return juce::URL::addEscapeChars (s, true); }

//==============================================================================
MeonApi::MeonApi (RestTransport& t, int buildNumber) : transport (t), build (buildNumber) {}

MeonApi::~MeonApi() { *alive = false; }

void MeonApi::setTokens (const Tokens& t)
{
    tokens = t;
    if (onTokensChanged) onTokensChanged (tokens);
}

void MeonApi::request (const String& method, const String& path, const var& body, bool auth, Raw done)
{
    if (auth && refreshing)
    {
        // 토큰을 새로 받는 중이면 끝난 뒤 보낸다
        waitingForRefresh.push_back ([this, method, path, body, done] (bool ok)
        {
            if (ok) sendNow (method, path, body, true, false, done);
            else    done (HttpResponse { 401, json::encodeError (ApiError::of (ErrorCode::unauthorized)) });
        });
        return;
    }
    sendNow (method, path, body, auth, auth, done);
}

void MeonApi::sendNow (const String& method, const String& path, const var& body, bool auth, bool mayRetry, Raw done)
{
    HttpRequest req;
    req.method = method;
    req.path = path;
    req.body = body;
    req.build = build;
    if (auth) req.accessToken = tokens.accessToken;

    std::weak_ptr<bool> weak = alive;
    transport.send (req, [this, weak, method, path, body, auth, mayRetry, done] (HttpResponse r)
    {
        if (weak.expired()) return;

        if (r.status == 426 && onClientOutdated)
            onClientOutdated();

        // 401: access 토큰 만료(token_expired) 또는 무효(unauthorized, 다른 곳에서 세션 무효화 등) → refresh 로 한 번 더.
        // refresh 도 실패하면 onSessionLost
        if (r.status == 401 && auth && mayRetry && tokens.refreshToken.isNotEmpty())
        {
            doRefresh ([this, method, path, body, done] (bool ok)
            {
                if (ok) sendNow (method, path, body, true, false, done);
                else    done (HttpResponse { 401, json::encodeError (ApiError::of (ErrorCode::unauthorized)) });
            });
            return;
        }
        done (r);
    });
}

void MeonApi::doRefresh (std::function<void (bool)> done)
{
    waitingForRefresh.push_back (std::move (done));
    if (refreshing) return;
    refreshing = true;

    refresh ([this] (Result<Tokens> r)
    {
        refreshing = false;
        auto waiting = std::move (waitingForRefresh);
        waitingForRefresh.clear();
        if (! r.ok() && r.code() != ErrorCode::network && r.code() != ErrorCode::timeout)
        {
            setTokens ({});
            if (onSessionLost) onSessionLost();
        }
        for (auto& w : waiting) w (r.ok());
    });
}

template <typename T>
void MeonApi::call (const String& method, const String& path, const var& body, bool auth, Callback<T> cb)
{
    request (method, path, body, auth, [cb] (const HttpResponse& r)
    {
        if (! isSuccess (r.status)) { cb (errorFromResponse (r)); return; }
        T value;
        if (! json::decode (r.body, value)) { cb (ApiError::of (ErrorCode::badResponse)); return; }
        cb (std::move (value));
    });
}

void MeonApi::callDone (const String& method, const String& path, const var& body, bool auth, Callback<Done> cb)
{
    request (method, path, body, auth, [cb] (const HttpResponse& r)
    {
        if (! isSuccess (r.status)) { cb (errorFromResponse (r)); return; }
        cb (Done {});
    });
}

void MeonApi::callJoin (const String& path, const var& body, Callback<JoinResult> cb)
{
    request ("POST", path, body, true, [cb] (const HttpResponse& r)
    {
        if (! isSuccess (r.status)) { cb (errorFromResponse (r)); return; }
        JoinResult jr;
        if (r.status == 202)
        {
            auto until = r.body.getProperty ("request_until", var());
            if (! until.isString()) { cb (ApiError::of (ErrorCode::badResponse)); return; }
            jr.requestUntil = until.toString();
        }
        else
        {
            RoomDetail d;
            if (! json::decode (r.body, d)) { cb (ApiError::of (ErrorCode::badResponse)); return; }
            jr.room = std::move (d);
        }
        cb (std::move (jr));
    });
}

void MeonApi::storeAuth (const HttpResponse& r, Callback<AuthResult> cb)
{
    if (! isSuccess (r.status)) { cb (errorFromResponse (r)); return; }
    AuthResult ar;
    auto at = r.body.getProperty ("access_token", var()), rt = r.body.getProperty ("refresh_token", var());
    if (! at.isString() || ! rt.isString() || ! json::decode (r.body.getProperty ("me", var()), ar.me))
    {
        cb (ApiError::of (ErrorCode::badResponse));
        return;
    }
    ar.tokens = { at.toString(), rt.toString() };
    setTokens (ar.tokens);
    cb (std::move (ar));
}

//==============================================================================
void MeonApi::getConfig (Callback<Config> cb)              { call ("GET", "/config", {}, false, cb); }

void MeonApi::usernameAvailable (const String& u, Callback<bool> cb)
{
    request ("GET", "/auth/username-available?username=" + enc (u), {}, false, [cb] (const HttpResponse& r)
    {
        if (! isSuccess (r.status)) { cb (errorFromResponse (r)); return; }
        auto v = r.body.getProperty ("available", var());
        if (! v.isBool()) { cb (ApiError::of (ErrorCode::badResponse)); return; }
        cb ((bool) v);
    });
}

void MeonApi::signup (const String& email, const String& username, const String& password, bool agreed, Callback<AuthResult> cb)
{
    auto body = json::obj ({ { "email", email }, { "username", username }, { "password", password }, { "agreed_terms", agreed } });
    request ("POST", "/auth/signup", body, false, [this, cb] (const HttpResponse& r) { storeAuth (r, cb); });
}

void MeonApi::login (const String& email, const String& password, Callback<AuthResult> cb)
{
    auto body = json::obj ({ { "email", email }, { "password", password } });
    request ("POST", "/auth/login", body, false, [this, cb] (const HttpResponse& r) { storeAuth (r, cb); });
}

void MeonApi::refresh (Callback<Tokens> cb)
{
    auto body = json::obj ({ { "refresh_token", tokens.refreshToken } });
    std::weak_ptr<bool> weak = alive;
    sendNow ("POST", "/auth/refresh", body, false, false, [this, weak, cb] (const HttpResponse& r)
    {
        if (weak.expired()) return;
        if (! isSuccess (r.status)) { cb (errorFromResponse (r)); return; }
        auto at = r.body.getProperty ("access_token", var()), rt = r.body.getProperty ("refresh_token", var());
        if (! at.isString() || ! rt.isString()) { cb (ApiError::of (ErrorCode::badResponse)); return; }
        setTokens ({ at.toString(), rt.toString() });
        cb (tokens);
    });
}

void MeonApi::logout (Callback<Done> cb)
{
    auto body = json::obj ({ { "refresh_token", tokens.refreshToken } });
    callDone ("POST", "/auth/logout", body, true, [this, cb] (Result<Done> r)
    {
        setTokens ({});   // 서버 응답과 관계없이 이 기기에서는 끝
        cb (r);
    });
}

void MeonApi::verifyEmail (const String& code, Callback<Me> cb)
{
    call ("POST", "/auth/verify-email", json::obj ({ { "code", code } }), true, cb);
}

void MeonApi::resendVerification (Callback<Done> cb)        { callDone ("POST", "/auth/resend-verification", {}, true, cb); }

void MeonApi::getMe (Callback<Me> cb)                       { call ("GET", "/me", {}, true, cb); }
void MeonApi::patchMe (const MePatch& p, Callback<Me> cb)   { call ("PATCH", "/me", json::encode (p), true, cb); }

void MeonApi::changePassword (const String& current, const String& next, Callback<Done> cb)
{
    callDone ("POST", "/me/password", json::obj ({ { "current_password", current }, { "new_password", next } }), true, cb);
}

void MeonApi::requestEmailChange (const String& newEmail, const String& password, Callback<Done> cb)
{
    auto body = password.isNotEmpty() ? json::obj ({ { "new_email", newEmail }, { "password", password } })
                                      : json::obj ({ { "new_email", newEmail } });
    callDone ("POST", "/me/email", body, true, cb);
}

void MeonApi::verifyEmailChange (const String& code, Callback<Me> cb)
{
    call ("POST", "/me/email/verify", json::obj ({ { "code", code } }), true, cb);
}

void MeonApi::deleteAccount (const String& password, Callback<Done> cb)
{
    callDone ("DELETE", "/me", json::obj ({ { "password", password } }), true, [this, cb] (Result<Done> r)
    {
        if (r.ok()) setTokens ({});
        cb (r);
    });
}

void MeonApi::getUser (const String& id, Callback<UserProfile> cb)   { call ("GET", "/users/" + enc (id), {}, true, cb); }

void MeonApi::listPublicRooms (Callback<std::vector<Room>> cb)
{
    request ("GET", "/rooms", {}, true, [cb] (const HttpResponse& r)
    {
        if (! isSuccess (r.status)) { cb (errorFromResponse (r)); return; }
        std::vector<Room> rooms;
        if (! json::decodeList (r.body.getProperty ("rooms", var()), rooms)) { cb (ApiError::of (ErrorCode::badResponse)); return; }
        cb (std::move (rooms));
    });
}

void MeonApi::createRoom (const CreateRoom& c, Callback<RoomDetail> cb)   { call ("POST", "/rooms", json::encode (c), true, cb); }

void MeonApi::getRoom (const String& id, Callback<RoomDetail> cb)
{
    request ("GET", "/rooms/" + enc (id), {}, true, [cb] (const HttpResponse& r)
    {
        if (! isSuccess (r.status)) { cb (errorFromResponse (r)); return; }
        RoomDetail d;
        if (r.body.hasProperty ("connection"))
        {
            if (! json::decode (r.body, d)) { cb (ApiError::of (ErrorCode::badResponse)); return; }
        }
        else if (! json::decode (r.body, static_cast<Room&> (d)))
        {
            cb (ApiError::of (ErrorCode::badResponse));
            return;
        }
        cb (std::move (d));
    });
}

void MeonApi::joinRoom (const String& id, Callback<JoinResult> cb)     { callJoin ("/rooms/" + enc (id) + "/join", {}, cb); }
void MeonApi::joinByCode (const String& code, Callback<JoinResult> cb) { callJoin ("/rooms/join-by-code", json::obj ({ { "code", code } }), cb); }
void MeonApi::leaveRoom (const String& id, Callback<Done> cb)          { callDone ("POST", "/rooms/" + enc (id) + "/leave", {}, true, cb); }
void MeonApi::reportRtt (int ms, Callback<Done> cb)                    { callDone ("POST", "/me/rtt", json::obj ({ { "aoo_rtt_ms", ms } }), true, cb); }

void MeonApi::sendLatencyTelemetry (const String& roomId, int est, int mine, int host, int actual, Callback<Done> cb)
{
    callDone ("POST", "/telemetry/latency",
              json::obj ({ { "room_id", roomId }, { "estimated_ms", est }, { "my_rtt_ms", mine }, { "host_rtt_ms", host }, { "actual_p2p_rtt_ms", actual } }),
              true, cb);
}

void MeonApi::listJoinRequests (const String& roomId, Callback<std::vector<JoinRequest>> cb)
{
    request ("GET", "/rooms/" + enc (roomId) + "/join-requests", {}, true, [cb] (const HttpResponse& r)
    {
        if (! isSuccess (r.status)) { cb (errorFromResponse (r)); return; }
        std::vector<JoinRequest> list;
        if (! json::decodeList (r.body.getProperty ("requests", var()), list)) { cb (ApiError::of (ErrorCode::badResponse)); return; }
        cb (std::move (list));
    });
}

void MeonApi::acceptJoinRequest (const String& roomId, const String& rid, Callback<RoomDetail> cb)
{
    call ("POST", "/rooms/" + enc (roomId) + "/join-requests/" + enc (rid) + "/accept", {}, true, cb);
}

void MeonApi::declineJoinRequest (const String& roomId, const String& rid, Callback<Done> cb)
{
    callDone ("POST", "/rooms/" + enc (roomId) + "/join-requests/" + enc (rid) + "/decline", {}, true, cb);
}

void MeonApi::patchRoom (const String& id, const RoomPatch& p, Callback<RoomDetail> cb) { call ("PATCH", "/rooms/" + enc (id), json::encode (p), true, cb); }

void MeonApi::transferHost (const String& id, const String& userId, Callback<RoomDetail> cb)
{
    call ("POST", "/rooms/" + enc (id) + "/transfer-host", json::obj ({ { "user_id", userId } }), true, cb);
}

void MeonApi::kick (const String& id, const String& userId, Callback<Done> cb)
{
    callDone ("POST", "/rooms/" + enc (id) + "/kick", json::obj ({ { "user_id", userId } }), true, cb);
}

void MeonApi::closeRoom (const String& id, Callback<Done> cb) { callDone ("POST", "/rooms/" + enc (id) + "/close", {}, true, cb); }

void MeonApi::listFriends (Callback<std::vector<Friend>> cb)
{
    request ("GET", "/friends", {}, true, [cb] (const HttpResponse& r)
    {
        if (! isSuccess (r.status)) { cb (errorFromResponse (r)); return; }
        std::vector<Friend> list;
        if (! json::decodeList (r.body.getProperty ("friends", var()), list)) { cb (ApiError::of (ErrorCode::badResponse)); return; }
        cb (std::move (list));
    });
}

void MeonApi::sendFriendRequest (const String& username, Callback<String> cb)
{
    request ("POST", "/friends/requests", json::obj ({ { "username", username } }), true, [cb] (const HttpResponse& r)
    {
        if (! isSuccess (r.status)) { cb (errorFromResponse (r)); return; }
        auto v = r.body.getProperty ("result", var());
        if (! v.isString()) { cb (ApiError::of (ErrorCode::badResponse)); return; }
        cb (v.toString());
    });
}

void MeonApi::listFriendRequests (Callback<std::vector<FriendRequest>> cb)
{
    request ("GET", "/friends/requests", {}, true, [cb] (const HttpResponse& r)
    {
        if (! isSuccess (r.status)) { cb (errorFromResponse (r)); return; }
        std::vector<FriendRequest> list;
        if (! json::decodeList (r.body.getProperty ("requests", var()), list)) { cb (ApiError::of (ErrorCode::badResponse)); return; }
        cb (std::move (list));
    });
}

void MeonApi::acceptFriendRequest (const String& id, Callback<Done> cb)  { callDone ("POST", "/friends/requests/" + enc (id) + "/accept", {}, true, cb); }
void MeonApi::declineFriendRequest (const String& id, Callback<Done> cb) { callDone ("POST", "/friends/requests/" + enc (id) + "/decline", {}, true, cb); }
void MeonApi::removeFriend (const String& userId, Callback<Done> cb)     { callDone ("DELETE", "/friends/" + enc (userId), {}, true, cb); }

void MeonApi::sendInvite (const String& userId, Callback<InviteSent> cb)
{
    request ("POST", "/invites", json::obj ({ { "user_id", userId } }), true, [cb] (const HttpResponse& r)
    {
        if (! isSuccess (r.status)) { cb (errorFromResponse (r)); return; }
        auto id = r.body.getProperty ("id", var()), at = r.body.getProperty ("deliver_at", var());
        if (! id.isString() || ! at.isString()) { cb (ApiError::of (ErrorCode::badResponse)); return; }
        cb (InviteSent { id.toString(), at.toString() });
    });
}

void MeonApi::cancelInvite (const String& id, Callback<Done> cb)        { callDone ("DELETE", "/invites/" + enc (id), {}, true, cb); }
void MeonApi::acceptInvite (const String& id, Callback<JoinResult> cb)  { callJoin ("/invites/" + enc (id) + "/accept", {}, cb); }
void MeonApi::dismissInvite (const String& id, Callback<Done> cb)       { callDone ("POST", "/invites/" + enc (id) + "/dismiss", {}, true, cb); }

void MeonApi::listBlocks (Callback<std::vector<Block>> cb)
{
    request ("GET", "/blocks", {}, true, [cb] (const HttpResponse& r)
    {
        if (! isSuccess (r.status)) { cb (errorFromResponse (r)); return; }
        std::vector<Block> list;
        if (! json::decodeList (r.body.getProperty ("blocks", var()), list)) { cb (ApiError::of (ErrorCode::badResponse)); return; }
        cb (std::move (list));
    });
}

void MeonApi::block (const String& userId, Callback<Done> cb)   { callDone ("POST", "/blocks", json::obj ({ { "user_id", userId } }), true, cb); }
void MeonApi::unblock (const String& userId, Callback<Done> cb) { callDone ("DELETE", "/blocks/" + enc (userId), {}, true, cb); }

void MeonApi::notificationState (Callback<bool> cb)
{
    request ("GET", "/notifications/state", {}, true, [cb] (const HttpResponse& r)
    {
        if (! isSuccess (r.status)) { cb (errorFromResponse (r)); return; }
        auto v = r.body.getProperty ("has_unread", var());
        if (! v.isBool()) { cb (ApiError::of (ErrorCode::badResponse)); return; }
        cb ((bool) v);
    });
}

void MeonApi::markNotificationsRead (Callback<Done> cb) { callDone ("POST", "/notifications/read", {}, true, cb); }

} // namespace meon::core
