// MEON core - 채팅 말풍선 줄바꿈 (화면 없이 글자 폭만 본다).
//   띄어쓰기(공백) 자리에서 줄을 바꾼다. 한 단어가 한 줄보다 길 때(긴 링크, 띄어쓰기 없는 문장)만 글자 단위로 끊는다.
//   링크 시작·끝(cuts)도 줄을 바꿀 수 있는 자리로 본다 ("자료:https://…" 에서 링크를 다음 줄에서 시작).
//   결과는 줄을 바꿀 자리에 \n 을 넣은 글이다. 화면은 이 글을 줄바꿈 없이 그대로 그린다
//   (플랫폼 배치의 줄바꿈 규칙이 macOS·Windows·Linux 마다 달라서, 특히 macOS 는 한글을 글자 단위로 끊었다).
#pragma once

#include <JuceHeader.h>
#include "EmojiCluster.h"

#include <algorithm>
#include <functional>
#include <vector>

namespace meon::core::chatwrap
{

/** 글 → 폭 (화면 글꼴로 잰다. 시험은 글자 수로) */
using Measure = std::function<float (const juce::String&)>;

/** 줄을 바꾼 글. map[원래 글자 위치] = 바꾼 글의 위치 (크기 = 글자 수 + 1, 링크 범위를 옮길 때 쓴다) */
struct Wrapped
{
    juce::String text;
    std::vector<int> map;
};

/** maxW: 줄 폭. 줄마다 maxW − 1 까지 채운다 (글자 폭 합과 화면 배치의 차이 여유).
    줄을 바꾸는 자리의 공백은 \n 하나로 바뀐다 (줄 끝·줄 앞에 공백이 남지 않게). 사용자가 넣은 줄바꿈과 줄 앞 공백은 그대로 둔다. */
inline Wrapped wrap (const juce::String& text, const Measure& measure, float maxW, const std::vector<int>& cuts = {})
{
    auto isCut = [&cuts] (int k) { return std::find (cuts.begin(), cuts.end(), k) != cuts.end(); };
    auto isBreak = [] (juce::juce_wchar c) { return c == '\n' || c == '\r'; };
    auto isSpace = [&isBreak] (juce::juce_wchar c) { return ! isBreak (c) && juce::CharacterFunctions::isWhitespace (c); };

    const int n = text.length();
    Wrapped w;
    w.map.assign ((size_t) n + 1, 0);
    auto& out = w.text;
    const float limit = maxW - 1.0f;
    float x = 0.0f;                 // 지금 줄에 놓은 폭
    int spaceFrom = -1, spaceTo = -1;   // 아직 놓지 않은 공백 (다음 단어가 같은 줄에 오면 놓는다)
    float spaceW = 0.0f;

    auto put = [&] (int from, int to)
    {
        for (int k = from; k < to; ++k) { w.map[(size_t) k] = out.length(); out += juce::String::charToString (text[k]); }
    };
    auto newLine = [&]
    {
        // 기다리던 공백은 \n 하나로 바꾼다 (공백 글자는 모두 그 \n 자리를 가리킨다)
        if (spaceFrom >= 0)
            for (int k = spaceFrom; k < spaceTo; ++k) w.map[(size_t) k] = out.length();
        out += "\n";
        x = 0.0f;
        spaceFrom = spaceTo = -1;
        spaceW = 0.0f;
    };
    auto flushSpace = [&]
    {
        if (spaceFrom >= 0) { put (spaceFrom, spaceTo); x += spaceW; }
        spaceFrom = spaceTo = -1;
        spaceW = 0.0f;
    };

    int i = 0;
    while (i < n)
    {
        if (isBreak (text[i]))
        {
            flushSpace();   // 사용자가 넣은 줄바꿈 앞 공백은 그대로 (보이지 않는다)
            put (i, i + 1);
            x = 0.0f;
            ++i;
            continue;
        }
        if (isSpace (text[i]))
        {
            int j = i;
            while (j < n && isSpace (text[j])) ++j;
            if (spaceFrom >= 0) flushSpace();   // (cut 자리로 나뉜 공백 없음. 방어)
            spaceFrom = i; spaceTo = j; spaceW = measure (text.substring (i, j));
            i = j;
            continue;
        }
        // 단어: 공백·줄바꿈·링크 경계(cut)까지
        int j = i + 1;
        while (j < n && ! isBreak (text[j]) && ! isSpace (text[j]) && ! isCut (j)) ++j;
        const float wordW = measure (text.substring (i, j));
        const bool lineHasText = x > 0.0f;

        if (x + spaceW + wordW <= limit)           // 이 줄에 들어간다
        {
            flushSpace();
            put (i, j);
            x += wordW;
        }
        else if (wordW <= limit)                    // 다음 줄에 통째로
        {
            if (lineHasText)
                newLine();
            else if (spaceFrom >= 0)   // 줄 앞 공백 때문에 넘친다: 그 공백은 버린다
            {
                for (int k = spaceFrom; k < spaceTo; ++k) w.map[(size_t) k] = out.length();
                spaceFrom = spaceTo = -1;
                spaceW = 0.0f;
            }
            put (i, j);
            x += wordW;
        }
        else                                        // 한 줄보다 길다: 지금 줄을 채우고 넘치는 글자 앞에서 끊는다 (이모지 묶음은 쪼개지 않는다)
        {
            flushSpace();
            for (int k = i; k < j;)
            {
                const int e = juce::jmax (k + 1, juce::jmin (j, emojiClusterEnd (text, k)));
                const float cw = measure (text.substring (k, e));
                if (x + cw > limit && x > 0.0f) newLine();
                put (k, e);
                x += cw;
                k = e;
            }
        }
        i = j;
    }
    flushSpace();   // 글 끝 공백 (보이지 않는다)
    w.map[(size_t) n] = out.length();
    return w;
}

} // namespace meon::core::chatwrap
