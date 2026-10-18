// MEON core - 이모지 한 묶음(피부색·ZWJ·국기·키캡 포함)의 끝 위치. 화면 없이 글자만 본다.
//   그리기(MeonEmoji)와 채팅 링크 찾기(ChatLinks)가 같이 쓴다.
#pragma once

#include <JuceHeader.h>

namespace meon::core
{

namespace emojidetail
{
    inline bool isModifier (juce::juce_wchar c)
    {
        return c == 0xFE0E || c == 0xFE0F                 // 텍스트/이모지 표시 선택자
            || (c >= 0x1F3FB && c <= 0x1F3FF)             // 피부색
            || c == 0x20E3                                // 키캡
            || (c >= 0xE0020 && c <= 0xE007F);            // 태그 (지역 깃발)
    }

    inline bool isRegionalIndicator (juce::juce_wchar c) { return c >= 0x1F1E6 && c <= 0x1F1FF; }

    inline bool isEmojiBase (juce::juce_wchar c)
    {
        return c >= 0x10000                               // 보조 평면 (이모지 대부분. Pretendard 에는 없다)
            || (c >= 0x2300 && c <= 0x23FF) || (c >= 0x2600 && c <= 0x27BF) || (c >= 0x2B00 && c <= 0x2BFF)
            || c == 0x200D || isModifier (c);
    }
}

/** text[start] 에서 이모지 묶음이 시작하면 그 끝 위치, 아니면 start */
inline int emojiClusterEnd (const juce::String& text, int start)
{
    using namespace emojidetail;
    const int n = text.length();
    if (start >= n)
        return start;

    const auto c = text[start];
    const bool followedBySelector = start + 1 < n && (text[start + 1] == 0xFE0F || text[start + 1] == 0x20E3);   // ❤️, #️⃣
    if (! isEmojiBase (c) && ! followedBySelector)
        return start;

    int j = start + 1;
    if (isRegionalIndicator (c) && j < n && isRegionalIndicator (text[j]))   // 국기는 두 글자
        ++j;

    while (j < n)
    {
        if (isModifier (text[j]))
            ++j;
        else if (text[j] == 0x200D && j + 1 < n)   // ZWJ 로 이어진 다음 글자까지 (👨‍👩‍👧)
            j += 2;
        else
            break;
    }
    return j;
}

} // namespace meon::core
