// MEON ui - 개발용: 다시 그리기 확인 (화면 자가 테스트 --uitest=dock).
//   지켜볼 자리(화면 좌표, 미터 칸)를 정하면, 그리기 영역이 그 자리 안에만 있는 paint 를 센다.
//   = 미터가 다시 그려질 때 같이 그려진 구성 요소 (카드·방 목록·방 카드·화면 바탕). 0 이어야 한다.
//   (방 목록이 새로 오거나 토스트가 생기는 등 다른 이유로 넓게 다시 그리는 것은 세지 않는다)
#pragma once

#include <JuceHeader.h>

namespace meon::ui::devprobe
{

inline juce::RectangleList<int> watch;
inline int hits = 0;
inline juce::StringArray hitNames;

// 이름이 unionName 인 구성 요소가 그린 영역을 모두 합친다 (화면 좌표): 접는 동안 그림자 자리까지 다시 그렸는지 (--uitest=dock)
inline juce::String unionName;
inline juce::Rectangle<int> unionClip;

inline void note (const juce::Component& c, const juce::Graphics& g, const char* name)
{
    if (unionName.isNotEmpty() && unionName == name)
    {
        const auto area = c.localAreaToGlobal (g.getClipBounds());
        unionClip = unionClip.isEmpty() ? area : unionClip.getUnion (area);
    }
    if (watch.isEmpty()) return;
    const auto clip = c.localAreaToGlobal (g.getClipBounds());
    if (! clip.isEmpty() && watch.containsRectangle (clip)) { ++hits; hitNames.addIfNotAlreadyThere (name); }
}

} // namespace meon::ui::devprobe
