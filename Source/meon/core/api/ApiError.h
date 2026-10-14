// MEON core - API 1.5 오류 코드. 서버의 apperr 표와 같은 목록 + 클라이언트에서만 생기는 오류(네트워크 등).
// 화면 문구는 여기서 만들지 않는다 (ui 가 코드별로 정한다). message 는 서버가 준 문구를 그대로 담을 뿐이다.
#pragma once

#include <JuceHeader.h>
#include <optional>

namespace meon::core
{

enum class ErrorCode
{
    // 공통 (1.5)
    invalidRequest, unauthorized, tokenExpired, forbidden, notFound, conflict,
    clientOutdated, rateLimited, internal,
    // 개별
    codeInvalid, codeExpired, invalidCredentials,
    emailNotVerified, profileIncomplete, notHost, kicked, notFriend, notAccepting,
    roomNotFound, invalidCode,
    emailTaken, usernameTaken, alreadyInRoom, roomFull, hostAbsent, requestUnavailable,
    capacityBelowMembers, targetNotInRoom, notInRoom, targetOffline, targetInRoom,
    alreadyInvited, alreadyDelivered, blockedByMe, isFriend,
    roomClosed,
    // 클라이언트 전용 (서버가 보내지 않음)
    network,      // 연결 실패
    timeout,      // 응답 시간 초과
    badResponse,  // 응답을 해석하지 못함
    unknown       // 목록에 없는 서버 코드
};

struct ErrorInfo { ErrorCode code; const char* key; int httpStatus; };

inline const ErrorInfo* errorTable (int& count)
{
    static const ErrorInfo table[] = {
        { ErrorCode::invalidRequest, "invalid_request", 400 },
        { ErrorCode::unauthorized, "unauthorized", 401 },
        { ErrorCode::tokenExpired, "token_expired", 401 },
        { ErrorCode::forbidden, "forbidden", 403 },
        { ErrorCode::notFound, "not_found", 404 },
        { ErrorCode::conflict, "conflict", 409 },
        { ErrorCode::clientOutdated, "client_outdated", 426 },
        { ErrorCode::rateLimited, "rate_limited", 429 },
        { ErrorCode::internal, "internal", 500 },
        { ErrorCode::codeInvalid, "code_invalid", 400 },
        { ErrorCode::codeExpired, "code_expired", 400 },
        { ErrorCode::invalidCredentials, "invalid_credentials", 401 },
        { ErrorCode::emailNotVerified, "email_not_verified", 403 },
        { ErrorCode::profileIncomplete, "profile_incomplete", 403 },
        { ErrorCode::notHost, "not_host", 403 },
        { ErrorCode::kicked, "kicked", 403 },
        { ErrorCode::notFriend, "not_friend", 403 },
        { ErrorCode::notAccepting, "not_accepting", 403 },
        { ErrorCode::roomNotFound, "room_not_found", 404 },
        { ErrorCode::invalidCode, "invalid_code", 404 },
        { ErrorCode::emailTaken, "email_taken", 409 },
        { ErrorCode::usernameTaken, "username_taken", 409 },
        { ErrorCode::alreadyInRoom, "already_in_room", 409 },
        { ErrorCode::roomFull, "room_full", 409 },
        { ErrorCode::hostAbsent, "host_absent", 409 },
        { ErrorCode::requestUnavailable, "request_unavailable", 409 },
        { ErrorCode::capacityBelowMembers, "capacity_below_members", 409 },
        { ErrorCode::targetNotInRoom, "target_not_in_room", 409 },
        { ErrorCode::notInRoom, "not_in_room", 409 },
        { ErrorCode::targetOffline, "target_offline", 409 },
        { ErrorCode::targetInRoom, "target_in_room", 409 },
        { ErrorCode::alreadyInvited, "already_invited", 409 },
        { ErrorCode::alreadyDelivered, "already_delivered", 409 },
        { ErrorCode::blockedByMe, "blocked_by_me", 409 },
        { ErrorCode::isFriend, "is_friend", 409 },
        { ErrorCode::roomClosed, "room_closed", 410 },
        { ErrorCode::network, "network", 0 },
        { ErrorCode::timeout, "timeout", 0 },
        { ErrorCode::badResponse, "bad_response", 0 },
        { ErrorCode::unknown, "unknown", 0 },
    };
    count = (int) (sizeof (table) / sizeof (table[0]));
    return table;
}

inline juce::String errorKey (ErrorCode c)
{
    int n = 0;
    auto* t = errorTable (n);
    for (int i = 0; i < n; ++i)
        if (t[i].code == c) return t[i].key;
    return "unknown";
}

inline ErrorCode errorFromKey (const juce::String& key)
{
    int n = 0;
    auto* t = errorTable (n);
    for (int i = 0; i < n; ++i)
        if (key == t[i].key) return t[i].code;
    return ErrorCode::unknown;
}

inline int errorHttpStatus (ErrorCode c)
{
    int n = 0;
    auto* t = errorTable (n);
    for (int i = 0; i < n; ++i)
        if (t[i].code == c) return t[i].httpStatus;
    return 0;
}

struct ApiError
{
    ErrorCode code = ErrorCode::unknown;
    int httpStatus = 0;
    juce::String message;   // 서버 문구 (분기에 쓰지 않는다)
    juce::String field;     // invalid_request 의 입력 칸 이름
    int retryAfterSec = 0;  // 429

    static ApiError of (ErrorCode c, juce::String msg = {}, juce::String fieldName = {})
    {
        return { c, errorHttpStatus (c), std::move (msg), std::move (fieldName), 0 };
    }
};

/** 값 또는 오류. 콜백 하나로 결과를 돌려줄 때 쓴다. */
template <typename T>
class Result
{
public:
    Result (T v) : value (std::move (v)) {}
    Result (ApiError e) : error (std::move (e)) {}

    bool ok() const                 { return value.has_value(); }
    const T& get() const            { jassert (ok()); return *value; }
    T& get()                        { jassert (ok()); return *value; }
    const ApiError& err() const     { return error; }
    ErrorCode code() const          { return ok() ? ErrorCode::unknown : error.code; }

private:
    std::optional<T> value;
    ApiError error;
};

/** 본문 없는 성공 (204) */
struct Done {};

} // namespace meon::core
