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

inline void note (const juce::Component& c, const juce::Graphics& g, const char* name)
{
    if (watch.isEmpty()) return;
    const auto clip = c.localAreaToGlobal (g.getClipBounds());
    if (! clip.isEmpty() && watch.containsRectangle (clip)) { ++hits; hitNames.addIfNotAlreadyThere (name); }
}

} // namespace meon::ui::devprobe
