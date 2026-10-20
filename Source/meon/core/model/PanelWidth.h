// MEON core - 합주 화면 오른쪽 패널(채팅·친구·파일) 너비 (합주 최종 디자인 6). 화면 없이 숫자만 다룬다.
//   범위: 앱 320 ~ min(창 너비의 50%, 창 너비 − 54 − 566), 플러그인 240 ~ 같은 식 (1280 → 640, 900 → 280, 1024 → 404).
//   54 = 왼쪽 여백 18 + 멤버·패널 사이 18 + 오른쪽 여백 18, 566 = 멤버 쪽 최소 너비: 볼륨 60·미터 84 로 줄였을 때
//   멤버 행·내 오디오·모니터(둘 다 펼침) 어디도 겹치지 않는 폭 (화면 자가 테스트로 잰 값). 창을 키우면 최대도 커진다.
//   저장은 앱·플러그인 따로. 창이 작아서 줄어 보여도 저장값은 그대로 둔다 (창을 다시 키우면 돌아온다).
#pragma once

#include <JuceHeader.h>

namespace meon::core::panel
{

constexpr int defaultApp = 320, defaultPlugin = 240;
constexpr int chrome = 54, membersMin = 566;

inline int defaultWidth (bool plugin) { return plugin ? defaultPlugin : defaultApp; }

/** 창 너비에서 고를 수 있는 가장 큰 너비 (작은 창에서도 기본보다 작아지지 않는다) */
inline int maxWidth (bool plugin, int windowW)
{
    const int half = (int) std::lround (windowW / 2.0);
    return juce::jmax (defaultWidth (plugin), juce::jmin (half, windowW - chrome - membersMin));
}

inline int clampWidth (bool plugin, int windowW, int w)
{
    return juce::jlimit (defaultWidth (plugin), maxWidth (plugin, windowW), w);
}

/** 화면에 쓸 너비: 저장값(없으면 0 → 기본)을 지금 창에 맞춘다 */
inline int shownWidth (bool plugin, int windowW, int saved)
{
    return clampWidth (plugin, windowW, saved > 0 ? saved : defaultWidth (plugin));
}

inline juce::String settingsKey (bool plugin) { return plugin ? "jamPanelWidthPlugin" : "jamPanelWidthApp"; }

/** 저장된 너비 (없으면 0) */
inline int load (const juce::PropertySet& p, bool plugin) { return juce::jmax (0, p.getIntValue (settingsKey (plugin), 0)); }
inline void save (juce::PropertySet& p, bool plugin, int w) { p.setValue (settingsKey (plugin), w); }

/** 내 말풍선 최대 폭: 채팅 목록 안쪽 폭의 75% 를 220..420 으로 (안쪽보다 넓지 않게, 디자인 min(max(220px,75%),420px)) */
inline int myBubbleMax (int inner)
{
    return juce::jmin (inner, juce::jlimit (220, 420, (int) std::lround (inner * 0.75)));
}

/** 멤버 행·내 오디오·모니터 줄: 슬라이더와 미터가 나눠 쓸 폭(space)이 모자라면 슬라이더가 먼저 60 까지,
    그다음 미터가 150 에서 84 까지 줄어든다. 슬라이더 최대는 sliderMax (멤버 행 150, 내 오디오·모니터는 남는 만큼).
    60·84 아래로는 줄이지 않는다: 멤버 쪽이 566 이상이라 늘 들어간다 (membersMin) */
struct Split { int slider = 0, meter = 0; };
inline Split shrink (int space, int sliderMax = 150)
{
    constexpr int sliderMin = 60, meterMin = 84, meterMax = 150;
    Split s;
    s.meter = juce::jlimit (meterMin, meterMax, space - sliderMin);
    s.slider = juce::jlimit (sliderMin, juce::jmax (sliderMin, sliderMax), space - s.meter);
    return s;
}

} // namespace meon::core::panel
