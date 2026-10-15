// MEON ui - core 의 오류 코드·입력 규칙 → 화면 문구. core 는 문구를 만들지 않는다 (REMAKE_PLAN 0-2).
#pragma once

#include "theme/Tokens.h"
#include "../core/api/ApiError.h"
#include "../core/model/Validation.h"
#include "../core/home/HomeController.h"

namespace meon::ui::msg
{

/** 어느 화면에서나 쓰는 일반 오류 */
inline juce::String general (const core::ApiError& e)
{
    using E = core::ErrorCode;
    switch (e.code)
    {
        case E::network:
        case E::timeout:        return TXT ("서버에 연결하지 못했어요. 인터넷 연결을 확인해 주세요");
        case E::rateLimited:    return TXT ("잠시 뒤 다시 시도해 주세요");
        case E::clientOutdated: return TXT ("앱을 업데이트해 주세요");
        default:                return TXT ("문제가 생겼어요. 다시 시도해 주세요");
    }
}

// 입력 규칙 (Q6 잠정 문구)
inline juce::String email (core::validation::Problem p)
{
    using P = core::validation::Problem;
    if (p == P::none || p == P::empty) return {};
    if (p == P::tooLong) return TXT ("이메일이 너무 길어요");
    return TXT ("이메일 형식이 맞지 않아요");
}

inline juce::String password (core::validation::Problem p)
{
    using P = core::validation::Problem;
    switch (p)
    {
        case P::tooShort:       return TXT ("8자 이상 입력해 주세요");
        case P::tooLong:        return TXT ("128자까지 쓸 수 있어요");
        case P::badCharacters:  return TXT ("영문, 숫자, 특수문자만 쓸 수 있어요 (공백 제외)");
        case P::missingClasses: return TXT ("영문, 숫자, 특수문자를 하나씩 넣어 주세요");
        default:                return {};
    }
}

inline juce::String nickname (core::validation::Problem p)
{
    using P = core::validation::Problem;
    switch (p)
    {
        case P::tooShort: return TXT ("2자 이상 입력해 주세요");
        case P::tooLong:  return TXT ("12자까지 쓸 수 있어요");
        case P::emoji:    return TXT ("이모지는 쓸 수 없어요");
        default:          return {};
    }
}

inline juce::String username (core::validation::Problem p)
{
    using P = core::validation::Problem;
    if (p == P::none || p == P::empty) return {};
    return TXT ("영문 소문자, 숫자, 밑줄(_)로 4–16자를 입력해 주세요");
}

// 입장 실패 모달 (디자인 H_FAIL + 5.5 표, 방장 부재 Q15, 추방은 잠정)
struct JoinFailText { juce::String title, body; bool update = false; };

inline JoinFailText joinFailure (core::HomeController::JoinFailure f)
{
    using F = core::HomeController::JoinFailure;
    switch (f)
    {
        case F::full:       return { TXT ("방이 가득 찼어요"), TXT ("들어가는 사이에 마지막 자리가 찼어요. 자리가 나면 다시 시도해 주세요.") };
        case F::closed:     return { TXT ("방이 닫혔어요"), TXT ("방이 닫혀서 들어갈 수 없어요") };
        case F::network:    return { TXT ("방에 연결하지 못했어요"), TXT ("멤버들과 직접 연결되지 않아요. 방화벽이나 공유기 설정을 확인해 주세요.") };
        case F::server:     return { TXT ("방에 연결하지 못했어요"), TXT ("서버에 연결하지 못했어요. 인터넷 연결을 확인해 주세요") };
        case F::version:    return { TXT ("앱 버전이 달라요"), TXT ("새 버전이 나왔어요. 업데이트한 뒤 들어갈 수 있어요"), true };
        case F::hostAbsent: return { TXT ("방장이 자리에 없어요"), TXT ("방장이 돌아오면 다시 요청해 주세요") };
        case F::kicked:     return { TXT ("들어갈 수 없어요"), TXT ("이 방에서 내보내져서 다시 들어갈 수 없어요") };
        case F::alreadyInRoom: return { TXT ("이미 방에 있어요"), TXT ("지금 있는 방에서 나온 뒤 다시 시도해 주세요") };
        case F::other:      break;
    }
    return { TXT ("들어가지 못했어요"), TXT ("문제가 생겼어요. 다시 시도해 주세요") };
}

/** 방장 기능 실패 토스트 (R11 PR3, 2026-10-15 문구 승인) */
inline juce::String hostActionFailure (core::ErrorCode c)
{
    using E = core::ErrorCode;
    switch (c)
    {
        case E::targetNotInRoom:    return TXT ("이미 방에서 나간 멤버예요");
        case E::roomFull:           return TXT ("방이 가득 차서 수락하지 못했어요");
        case E::requestUnavailable: return TXT ("요청한 사람이 지금 들어올 수 없어요");
        default:                    return {};
    }
}

/** 친구 추가 결과 (API 7.2 result, 잠정 문구) */
inline juce::String friendRequestResult (const juce::String& result, const juce::String& username)
{
    const auto at = "@" + username;
    if (result == "sent")              return at + TXT ("님에게 친구 요청을 보냈어요");
    if (result == "accepted")          return at + TXT ("님과 친구가 됐어요");
    if (result == "not_found")         return TXT ("그런 아이디를 찾지 못했어요");
    if (result == "already_friends")   return TXT ("이미 친구예요");
    if (result == "already_requested") return TXT ("이미 친구 요청을 보냈어요");
    if (result == "self")              return TXT ("내 아이디예요");
    return {};
}

inline juce::String friendRequestError (const core::ApiError& e)
{
    if (e.code == core::ErrorCode::blockedByMe)
        return TXT ("차단한 사람이에요. 설정 › 친구 · 개인정보에서 차단을 풀 수 있어요");
    return general (e);
}

} // namespace meon::ui::msg

namespace meon::ui::msg
{

/** 마지막 접속 (친구 목록·프로필): "방금", "N분 전", "N시간 전", "어제", "N일 전" */
inline juce::String ago (juce::int64 thenMs, juce::int64 nowMs)
{
    if (thenMs <= 0) return {};
    const auto mins = std::max<juce::int64> (0, (nowMs - thenMs) / 60000);
    if (mins < 1)  return TXT ("방금");
    if (mins < 60) return juce::String (mins) + TXT ("분 전");
    const auto hours = mins / 60;
    if (hours < 24) return juce::String (hours) + TXT ("시간 전");
    const auto days = hours / 24;
    if (days < 2) return TXT ("어제");
    return juce::String (days) + TXT ("일 전");
}

} // namespace meon::ui::msg
