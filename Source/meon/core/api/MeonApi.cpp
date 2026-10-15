#include "MeonApi.h"
#include "ApiJson.h"
#include "../log/AppLog.h"

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
        refreshWaiters.push_back ([this, method, path, body, done] (Result<Tokens> t)
        {
            if (t.ok()) sendNow (method, path, body, true, false, done);
            else        done (HttpResponse { 401, json::encodeError (ApiError::of (ErrorCode::unauthorized)) });
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
    const auto usedToken = req.accessToken;
    transport.send (req, [this, weak, method, path, body, auth, mayRetry, done, usedToken] (HttpResponse r)
    {
        if (weak.expired()) return;

        if (r.status == 426)
        {
            AppLog::write ("api", LOGTXT ("426 (앱 버전이 낮음) ← ") + method + " " + path);
            if (onClientOutdated) onClientOutdated();
        }

        // 401: access 토큰 만료(token_expired) 또는 무효(unauthorized, 다른 곳에서 세션 무효화 등) → refresh 로 한 번 더.
        // refresh 도 실패하면 onSessionLost
        if (r.status == 401 && auth && mayRetry && tokens.refreshToken.isNotEmpty())
        {
            // 보내는 사이 다른 쪽이 이미 새 토큰을 받았으면 갱신하지 않고 새 토큰으로 다시 보낸다.
            // 새 토큰도 401 이면 그때는 토큰이 같으므로 아래 갱신으로 간다
            if (tokens.accessToken.isNotEmpty() && tokens.accessToken != usedToken && ! refreshing)
            {
                sendNow (method, path, body, true, true, done);
                return;
            }
            const auto why = "401 " + errorKey (errorFromResponse (r).code) + LOGTXT (" ← ") + method + " " + path.upToFirstOccurrenceOf ("?", false, false);
            doRefresh ([this, method, path, body, done] (bool ok)
            {
                if (ok) sendNow (method, path, body, true, false, done);
                else    done (HttpResponse { 401, json::encodeError (ApiError::of (ErrorCode::unauthorized)) });
            }, why);
            return;
        }
        done (r);
    });
}

void MeonApi::doRefresh (std::function<void (bool)> done, const String& why)
{
    sessionLostOnRefreshFailure = true;
    refresh ([done] (Result<Tokens> r) { done (r.ok()); }, why);
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

void MeonApi::refresh (Callback<Tokens> cb, const String& whyGiven)
{
    const auto why = whyGiven.isNotEmpty() ? whyGiven : LOGTXT ("요청");
    refreshWaiters.push_back (std::move (cb));
    if (refreshing)
    {
        AppLog::write ("auth", LOGTXT ("토큰 갱신 진행 중이라 결과를 같이 기다림 (") + why + ")");
        return;
    }
    refreshing = true;
    AppLog::write ("auth", LOGTXT ("토큰 갱신 시작 (") + why + ")");
    sendRefresh (0);
}

void MeonApi::sendRefresh (int attempt)
{
    std::weak_ptr<bool> weak = alive;
    if (coordinator != nullptr)
    {
        if (! coordinator->tryLock())
        {
            // 다른 프로세스(독립 앱·플러그인)가 갱신 중: 끝나면 그쪽이 저장한 새 토큰을 읽어서 쓴다
            if (attempt < 40)
            {
                if (attempt == 0) AppLog::write ("auth", LOGTXT ("다른 MEON(앱·플러그인)이 토큰을 갱신하는 중이라 기다림"));
                coordinator->retryLater (250, [this, weak, attempt] { if (! weak.expired()) sendRefresh (attempt + 1); });
                return;
            }
            AppLog::write ("auth", LOGTXT ("다른 MEON 의 토큰 갱신이 10초 넘게 끝나지 않아 잠금 없이 갱신"));
        }
        else
            coordinatorLocked = true;

        const auto stored = coordinator->storedRefreshToken();
        if (stored.isNotEmpty() && stored != tokens.refreshToken)
        {
            AppLog::write ("auth", LOGTXT ("다른 MEON 이 바꿔 둔 로그인 토큰으로 갱신"));
            tokens.refreshToken = stored;
        }
    }

    auto body = json::obj ({ { "refresh_token", tokens.refreshToken } });
    sendNow ("POST", "/auth/refresh", body, false, false, [this, weak] (const HttpResponse& r)
    {
        if (! weak.expired()) finishRefresh (r);
    });
}

void MeonApi::finishRefresh (const HttpResponse& r)
{
    Result<Tokens> result = ApiError::of (ErrorCode::badResponse);
    if (! isSuccess (r.status))
        result = errorFromResponse (r);
    else
    {
        auto at = r.body.getProperty ("access_token", var()), rt = r.body.getProperty ("refresh_token", var());
        if (at.isString() && rt.isString())
        {
            setTokens ({ at.toString(), rt.toString() });   // 저장(onTokensChanged)까지 끝낸 뒤 잠금을 푼다
            result = tokens;
        }
    }
    if (coordinatorLocked)
    {
        coordinatorLocked = false;
        coordinator->unlock();
    }

    refreshing = false;
    const bool loseSession = std::exchange (sessionLostOnRefreshFailure, false);
    auto waiters = std::move (refreshWaiters);
    refreshWaiters.clear();

    if (result.ok())
        AppLog::write ("auth", LOGTXT ("토큰 갱신 성공"));
    else if (result.code() == ErrorCode::network || result.code() == ErrorCode::timeout)
        AppLog::write ("auth", LOGTXT ("토큰 갱신 실패: ") + errorKey (result.code()) + LOGTXT (" (연결 문제라 로그인은 유지)"));
    else if (result.code() == ErrorCode::clientOutdated)
        AppLog::write ("auth", LOGTXT ("토큰 갱신 실패: 앱 버전이 낮음 (로그인은 유지, 업데이트 뒤 이어서 씀)"));
    else
    {
        AppLog::write ("auth", LOGTXT ("토큰 갱신 실패: ") + errorKey (result.code()) + " (HTTP " + String (r.status) + LOGTXT (") → 저장된 로그인을 지움"));
        setTokens ({});
        if (loseSession && onSessionLost) onSessionLost();
    }

    for (auto& w : waiters)
        if (w) w (result);
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

void MeonApi::deleteAccount (Callback<Done> cb)
{
    callDone ("DELETE", "/me", {}, true, [this, cb] (Result<Done> r)
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
bool MeonApi::leaveRoomBlocking (const String& id, int timeoutMs)
{
    HttpRequest req;
    req.method = "POST";
    req.path = "/rooms/" + enc (id) + "/leave";
    req.build = build;
    req.accessToken = tokens.accessToken;
    const auto r = transport.sendBlocking (req, timeoutMs);
    AppLog::write ("api", LOGTXT ("종료: 방 나가기 ") + id + " → " + (r.status == 0 ? String (LOGTXT ("응답 없음")) : String (r.status)));
    return isSuccess (r.status);
}

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
