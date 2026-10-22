// MEON ui - core 의 오류 코드·입력 규칙 → 화면 문구. core 는 문구를 만들지 않는다 (REMAKE_PLAN 0-2).
#pragma once

#include <JuceHeader.h>
#ifndef TXT   // theme/Tokens.h 와 같은 정의. 문구만 다루므로 그림 모듈 없이도 쓸 수 있게 (단위 테스트)
 #define TXT(s) juce::String (juce::CharPointer_UTF8 (s))
#endif
#include "../core/api/ApiError.h"
#include "../core/model/Validation.h"
#include "../core/home/HomeController.h"
#include "../core/jam/JamSession.h"

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

/** 연결 끊김 토스트 (경고): 제목 + 설명. 설명은 문장마다 줄을 나눈다 (글자 단위로 줄바꿈하는 플랫폼에서 "중이/에요" 처럼 끊기지 않게).
    합주 서버 문제의 이유를 알면 그 안내 (UDP 막힘·무응답), 아니면 인터넷 확인 */
struct ConnectionLostText { juce::String title, body; };

inline ConnectionLostText connectionLost (core::JamSession::ServerProblem p)
{
    using P = core::JamSession::ServerProblem;
    const auto retrying = TXT ("다시 연결하는 중이에요");
    switch (p)
    {
        case P::udpBlocked:
            // 토스트 폭(280)에 한 줄 24자 안팎이라, 긴 문장은 띄어쓰기 자리에서 직접 줄을 나눈다 ("방/화벽" 처럼 끊기지 않게)
            return { TXT ("합주 서버에 연결하지 못했어요"),
                     TXT ("합주 서버와 UDP로 주고받지 못했어요.") + "\n"
                         + TXT ("회사·학교 와이파이나 방화벽이") + "\n" + TXT ("UDP를 막는 경우가 많아요.") + "\n"
                         + TXT ("다른 네트워크로 바꾸거나") + "\n" + TXT ("방화벽에서 Meon을 허용해 주세요.") + "\n"
                         + retrying };
        case P::noAnswer:
            return { TXT ("합주 서버에 연결하지 못했어요"), TXT ("합주 서버가 답하지 않아요.") + "\n" + retrying };
        case P::none:
        case P::lost:
            break;
    }
    return { TXT ("연결이 끊겼어요"), TXT ("인터넷 연결을 확인해 주세요.") + "\n" + retrying };
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

/** 방 초대 실패 (API 7.7). already_invited 는 토스트 없이 "초대됨"으로 바뀐다 */
inline juce::String inviteFailure (core::ErrorCode c)
{
    using E = core::ErrorCode;
    switch (c)
    {
        case E::targetOffline: return TXT ("친구가 지금 오프라인이에요");
        case E::targetInRoom:  return TXT ("친구가 다른 방에서 합주 중이에요");
        case E::roomFull:      return TXT ("방이 가득 차서 초대할 수 없어요");
        case E::notAccepting:  return TXT ("지금은 초대를 받을 수 없는 친구예요");
        case E::notFriend:     return TXT ("친구에게만 초대를 보낼 수 있어요");
        case E::notInRoom:     return TXT ("방에 있을 때만 초대할 수 있어요");
        default:               return {};
    }
}

/** 받은 초대 수락 실패 중 토스트로 알리는 것 (API 7.9). 나머지 입장 오류는 입장 실패 모달 */
inline juce::String inviteAcceptFailure (core::ErrorCode c)
{
    using E = core::ErrorCode;
    if (c == E::roomClosed) return TXT ("방이 닫혀서 들어갈 수 없어요");
    if (c == E::notFound)   return TXT ("이미 끝난 초대예요");
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

/** 친구 목록의 오프라인 친구: "3시간 전 접속", "방금 접속" (프로필의 "마지막 접속" 칸은 ago 그대로) */
inline juce::String lastSeen (juce::int64 thenMs, juce::int64 nowMs)
{
    const auto when = ago (thenMs, nowMs);
    return when.isEmpty() ? TXT ("오프라인") : when + TXT (" 접속");
}

} // namespace meon::ui::msg
