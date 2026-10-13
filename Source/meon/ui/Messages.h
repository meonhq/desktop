// MEON ui - core 의 오류 코드·입력 규칙 → 화면 문구. core 는 문구를 만들지 않는다 (REMAKE_PLAN 0-2).
#pragma once

#include "theme/Tokens.h"
#include "../core/api/ApiError.h"
#include "../core/model/Validation.h"

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

} // namespace meon::ui::msg
