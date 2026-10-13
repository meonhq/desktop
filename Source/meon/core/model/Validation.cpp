#include "Validation.h"

namespace meon::core::validation
{

using juce::String;
using juce::juce_wchar;

String composeHangul (const String& s)
{
    // 유니코드 표준의 한글 조합 규칙: L(1100–1112) + V(1161–1175) [+ T(11A8–11C2)] → AC00 + (L*21+V)*28 + T
    String out;
    out.preallocateBytes (s.getNumBytesAsUTF8());
    auto p = s.getCharPointer();
    juce_wchar pending = 0;   // 조합 중인 음절 (LV 또는 LVT) 또는 홀로 있는 L

    auto flush = [&] { if (pending != 0) { out += String::charToString (pending); pending = 0; } };

    while (! p.isEmpty())
    {
        const juce_wchar c = p.getAndAdvance();

        if (pending >= 0x1100 && pending <= 0x1112 && c >= 0x1161 && c <= 0x1175)
        {
            pending = 0xAC00 + ((pending - 0x1100) * 21 + (c - 0x1161)) * 28;
            continue;
        }
        if (pending >= 0xAC00 && pending <= 0xD7A3 && (pending - 0xAC00) % 28 == 0 && c >= 0x11A8 && c <= 0x11C2)
        {
            pending += (c - 0x11A7);
            continue;
        }
        flush();
        if ((c >= 0x1100 && c <= 0x1112) || (c >= 0xAC00 && c <= 0xD7A3))
            pending = c;
        else
            out += String::charToString (c);
    }
    flush();
    return out;
}

int length (const String& s)
{
    return composeHangul (s).length();
}

String normalise (const String& s)
{
    return composeHangul (s.trim());
}

bool isEmojiOrControl (juce_wchar c)
{
    // API 3.1: 유니코드 So 범주, U+1F000 이상, U+2600–27BF, 변형 선택자, ZWJ + 제어 문자(Cc).
    // So 는 범주 표 전체 대신 이모지·기호가 모인 구역으로 근사한다. 경계가 다른 글자는 서버가 최종 판정한다.
    if (c < 0x20 || (c >= 0x7F && c <= 0x9F)) return true;                     // Cc
    if (c == 0x200D) return true;                                               // ZWJ
    if ((c >= 0xFE00 && c <= 0xFE0F) || (c >= 0xE0100 && c <= 0xE01EF)) return true;   // 변형 선택자
    if (c >= 0x1F000) return true;
    if (c >= 0x2600 && c <= 0x27BF) return true;
    if (c == 0x00A9 || c == 0x00AE || c == 0x2122 || c == 0x2139) return true;   // © ® ™ ℹ
    if (c >= 0x2190 && c <= 0x21FF) return true;   // 화살표
    if (c >= 0x2300 && c <= 0x23FF) return true;   // 기타 기술 기호 (⌚ ⏰ …)
    if (c >= 0x2500 && c <= 0x25FF) return true;   // 상자·도형 (▶ ◀ ■ …)
    if (c >= 0x2B00 && c <= 0x2BFF) return true;   // ⬆ ⭐ …
    if (c == 0x3030 || c == 0x303D || c == 0x3297 || c == 0x3299) return true;
    return false;
}

Problem email (const String& s)
{
    if (s.isEmpty()) return Problem::empty;
    if (s.length() > 254) return Problem::tooLong;
    // 일반 형식: 공백 없음, @ 하나, 앞뒤가 비어 있지 않음, 도메인에 점이 있고 끝이 점이 아님
    if (s.containsAnyOf (" \t\r\n")) return Problem::badFormat;
    const int at = s.indexOfChar ('@');
    if (at <= 0 || at != s.lastIndexOfChar ('@')) return Problem::badFormat;
    const auto domain = s.substring (at + 1);
    if (domain.isEmpty() || ! domain.containsChar ('.') || domain.startsWithChar ('.') || domain.endsWithChar ('.')
        || domain.contains (".."))
        return Problem::badFormat;
    return Problem::none;
}

Problem username (const String& s)
{
    if (s.isEmpty()) return Problem::empty;
    for (auto p = s.getCharPointer(); ! p.isEmpty();)
    {
        const auto c = p.getAndAdvance();
        if (! ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
            return Problem::badCharacters;
    }
    if (s.length() < usernameMin) return Problem::tooShort;
    if (s.length() > usernameMax) return Problem::tooLong;
    return Problem::none;
}

Problem nickname (const String& raw)
{
    const auto s = normalise (raw);
    if (s.isEmpty()) return Problem::empty;
    for (auto p = s.getCharPointer(); ! p.isEmpty();)
        if (isEmojiOrControl (p.getAndAdvance()))
            return Problem::emoji;
    const int n = s.length();
    if (n < 2) return Problem::tooShort;
    if (n > nicknameMax) return Problem::tooLong;
    return Problem::none;
}

Problem password (const String& s)
{
    if (s.isEmpty()) return Problem::empty;
    bool letter = false, digit = false, special = false;
    for (auto p = s.getCharPointer(); ! p.isEmpty();)
    {
        const auto c = p.getAndAdvance();
        if (c < 0x21 || c > 0x7E) return Problem::badCharacters;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) letter = true;
        else if (c >= '0' && c <= '9') digit = true;
        else special = true;   // 남은 출력 가능한 ASCII 는 모두 구두점
    }
    if (s.length() < 8) return Problem::tooShort;
    if (s.length() > 128) return Problem::tooLong;
    if (! (letter && digit && special)) return Problem::missingClasses;
    return Problem::none;
}

Problem roomTitle (const String& raw)
{
    const auto s = normalise (raw);
    if (s.isEmpty()) return Problem::empty;
    if (s.length() > titleMax) return Problem::tooLong;
    return Problem::none;
}

Problem roomDescription (const String& raw)
{
    if (normalise (raw).length() > descriptionMax) return Problem::tooLong;
    return Problem::none;
}

bool isRoomCodeChar (juce_wchar c)
{
    if (c >= '2' && c <= '9') return true;
    if (c >= 'A' && c <= 'Z') return c != 'I' && c != 'O';
    return false;
}

bool isRoomCode (const String& s)
{
    if (s.length() != 6) return false;
    for (auto p = s.toUpperCase().getCharPointer(); ! p.isEmpty();)
        if (! isRoomCodeChar (p.getAndAdvance())) return false;
    return true;
}

} // namespace meon::core::validation
