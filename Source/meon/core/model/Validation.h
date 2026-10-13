// MEON core - API 3.1 입력 규칙. 서버와 같은 규칙으로 미리 검사한다 (최종 판정은 서버).
// 글자 수는 NFC 정규화 뒤 코드포인트로 센다. JUCE 에 NFC 가 없어서 한글 자모 조합(가장 흔한 NFD 입력, macOS)만
// 직접 합친다. 그 밖의 결합 문자는 그대로 센다 (서버가 최종 판정).
#pragma once

#include <JuceHeader.h>

namespace meon::core::validation
{

enum class Problem
{
    none,
    empty,
    tooShort,
    tooLong,
    badCharacters,     // 아이디: 영문 소문자·숫자·밑줄 외 / 비밀번호: 출력 가능한 ASCII 외(공백 포함)
    emoji,             // 닉네임의 이모지·제어 문자
    badFormat,         // 이메일 형식
    missingClasses     // 비밀번호: 영문·숫자·특수문자 중 빠진 것
};

/** 한글 자모(초성+중성(+종성))를 완성형 음절로 합친다. 그 밖의 문자는 그대로 */
juce::String composeHangul (const juce::String&);

/** NFC(한글만) 후 코드포인트 수 */
int length (const juce::String&);

/** 앞뒤 공백 제거 + 한글 합치기 (닉네임·방 이름·소개를 보낼 때) */
juce::String normalise (const juce::String&);

bool isEmojiOrControl (juce::juce_wchar);

Problem email (const juce::String&);      // 형식, 최대 254
Problem username (const juce::String&);   // [a-z0-9_]{4,16} (대문자는 badCharacters: 화면이 소문자로 바꿔 넣는다)
Problem nickname (const juce::String&);   // 2~12, 이모지·제어 문자 불가 (앞뒤 공백 제거 후)
Problem password (const juce::String&);   // 8~128, 출력 가능한 ASCII(공백 제외), 영문·숫자·특수문자 각 1자 이상
Problem roomTitle (const juce::String&);  // 1~30 (앞뒤 공백 제거 후)
Problem roomDescription (const juce::String&);   // 0~80

constexpr int nicknameMax = 12, titleMax = 30, descriptionMax = 80, usernameMin = 4, usernameMax = 16;

/** 방 입장 코드 문자: A–H J–N P–Z 2–9 (0·1·O·I 없음, API 5.5) */
bool isRoomCodeChar (juce::juce_wchar upper);
/** 대소문자 무시하고 6자리 코드인지 */
bool isRoomCode (const juce::String&);

} // namespace meon::core::validation
