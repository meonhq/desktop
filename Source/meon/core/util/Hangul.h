// MEON core - 한글 자모 조합 (유니코드 표준 알고리즘). macOS 파일 이름은 자모가 풀린 NFD 로 오므로 화면에 쓰기 전에 합친다.
// 초성 U+1100..1112 + 중성 U+1161..1175 [+ 종성 U+11A8..11C2] → U+AC00..D7A3. 완성형 음절 + 종성도 합친다. 그 밖의 글자는 그대로.
#pragma once

#include <JuceHeader.h>

namespace meon::core
{

inline juce::String composeHangul (const juce::String& text)
{
    constexpr juce::juce_wchar sBase = 0xAC00, lBase = 0x1100, vBase = 0x1161, tBase = 0x11A7;
    constexpr int lCount = 19, vCount = 21, tCount = 28, nCount = vCount * tCount, sCount = lCount * nCount;
    juce::Array<juce::juce_wchar> out;
    for (auto p = text.getCharPointer(); ! p.isEmpty();)
    {
        const juce::juce_wchar c = p.getAndAdvance();
        if (! out.isEmpty())
        {
            const juce::juce_wchar last = out.getLast();
            const int l = (int) (last - lBase), v = (int) (c - vBase);
            if (l >= 0 && l < lCount && v >= 0 && v < vCount)
            {
                out.set (out.size() - 1, (juce::juce_wchar) (sBase + (l * vCount + v) * tCount));
                continue;
            }
            const int si = (int) (last - sBase), t = (int) (c - tBase);
            if (si >= 0 && si < sCount && si % tCount == 0 && t > 0 && t < tCount)
            {
                out.set (out.size() - 1, last + (juce::juce_wchar) t);
                continue;
            }
        }
        out.add (c);
    }
    out.add (0);
    return juce::String (juce::CharPointer_UTF32 ((const juce::CharPointer_UTF32::CharType*) out.getRawDataPointer()));
}

} // namespace meon::core
