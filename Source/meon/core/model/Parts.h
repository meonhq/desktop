// MEON core - 파트 데이터 (API 2.1). 그리기·아이콘은 ui 쪽에 있다.
#pragma once

#include <JuceHeader.h>

namespace meon::core::parts
{

/** API 값 순서 = 화면 순서 */
inline const juce::StringArray& keys()
{
    static const juce::StringArray k { "vocal", "guitar", "bass", "drums", "keys", "other" };
    return k;
}

inline bool isValid (const juce::String& key) { return keys().contains (key); }

/** 필터 모달의 "비어 있는 파트" 목록: 디자인대로 "그 외"를 뺀 5개 (Q25) */
inline juce::StringArray filterKeys()
{
    return { "vocal", "guitar", "bass", "drums", "keys" };
}

} // namespace meon::core::parts
