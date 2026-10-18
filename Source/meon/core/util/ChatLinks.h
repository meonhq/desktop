// MEON core - 채팅 글 속 링크 찾기와 브라우저에 넘길 주소 (리메이크 전 JamScreen 의 findLinks·browserUrl 규칙 그대로).
//   링크: http://, https://, www. 로 시작해서 공백·< > "·이모지 전까지. 끝에 붙은 문장부호는 뺀다 (괄호는 짝이 안 맞을 때만).
//         www. 는 앞 글자가 영문·숫자·. · / 이면 링크로 보지 않고, 뒤에 . 이 있어야 한다.
#pragma once

#include <JuceHeader.h>
#include <cstring>
#include <vector>
#include "EmojiCluster.h"

namespace meon::core
{

/** 링크 범위들 (글자 위치, 끝 미포함, 앞에서부터 겹치지 않게) */
inline std::vector<juce::Range<int>> findLinks (const juce::String& text)
{
    std::vector<juce::Range<int>> links;
    const int n = text.length();
    // www. 바로 앞이 영문·숫자·. · / 이면 단어 속 글자로 보고 링크로 치지 않는다 ("awww.b.com").
    // 리메이크 전에는 isLetterOrDigit(iswalnum) 이었는데 한글 판정이 플랫폼마다 달라서(macOS·Linux 는 글자 아님, Windows 는 글자)
    // ASCII 로 고정했다. 한글 바로 뒤의 www. 는 링크다 (리메이크 전 macOS 와 같음).
    auto isAsciiWordChar = [] (juce::juce_wchar c)
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '/';
    };
    auto startsWith = [&text] (int i, const char* prefix)
    {
        return text.substring (i, i + (int) std::strlen (prefix)).equalsIgnoreCase (prefix);
    };

    for (int i = 0; i < n;)
    {
        int prefix = 0;
        if (startsWith (i, "https://"))     prefix = 8;
        else if (startsWith (i, "http://")) prefix = 7;
        else if (startsWith (i, "www.") && (i == 0 || ! isAsciiWordChar (text[i - 1])))
            prefix = 4;
        if (prefix == 0)
        {
            ++i;
            continue;
        }

        int end = i + prefix;
        while (end < n && ! juce::CharacterFunctions::isWhitespace (text[end])
               && text[end] != '<' && text[end] != '>' && text[end] != '"' && emojiClusterEnd (text, end) == end)
            ++end;

        // 끝에 붙은 문장부호 ("…봐요 https://a.com." 의 마침표 등). 괄호는 짝이 안 맞을 때만 뺀다.
        const juce::String trailing (juce::CharPointer_UTF8 (".,!?;:'\")]}\xe2\x80\xa6"));
        while (end > i + prefix && trailing.containsChar (text[end - 1]))
        {
            if (text[end - 1] == ')')
            {
                const auto body = text.substring (i, end);
                if (body.retainCharacters ("(").length() >= body.retainCharacters (")").length())
                    break;
            }
            --end;
        }

        const auto rest = text.substring (i + prefix, end);
        if (rest.isNotEmpty() && (prefix != 4 || rest.containsChar ('.')))
            links.push_back ({ i, end });
        i = juce::jmax (end, i + 1);
    }
    return links;
}

/** 브라우저에 넘길 주소. www. 처럼 http(s):// 가 없으면 https:// 를 붙이고, 한글 등은 퍼센트 인코딩한다 (macOS 는 ASCII 주소만 연다). */
inline juce::String browserUrl (const juce::String& link)
{
    const bool hasScheme = link.startsWithIgnoreCase ("http://") || link.startsWithIgnoreCase ("https://");
    const auto full = hasScheme ? link : "https://" + link;
    const juce::String safe ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._~:/?#[]@!$&'()*+,;=%");
    juce::String out;
    for (auto p = full.getCharPointer(); ! p.isEmpty();)
    {
        const auto c = p.getAndAdvance();
        if (c < 0x80 && safe.containsChar (c))
        {
            out += juce::String::charToString (c);
            continue;
        }
        const auto ch = juce::String::charToString (c);
        for (auto* b = ch.toRawUTF8(); *b != 0; ++b)
            out += "%" + juce::String::toHexString ((int) (juce::uint8) *b).paddedLeft ('0', 2).toUpperCase();
    }
    return out;
}

} // namespace meon::core
