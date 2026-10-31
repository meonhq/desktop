// MEON ui - 1px 선을 물리 픽셀에 맞춰 그린다 (구분선·카드 테두리).
// 화면 배율이 정수가 아니면(윈도우 125%·150%) 정수 좌표의 1px 선이 물리 픽셀 두 줄에 걸친다:
//   채운 선(fillRect)은 두 줄을 다 칠해 두꺼워 보이고 (125%: 63.75~65 → 63·64 두 줄), 둥근 테두리(stroke)는 두 줄로 번진다.
// 선을 가장 가까운 물리 픽셀 경계에 두고, 두께는 물리 픽셀 floor(배율) 줄 (최소 1): 100%·125%·150% 1줄, 200%(레티나) 2줄 = 1px.
// 브라우저가 1px 테두리를 그리는 방식과 같다. 정수 배율(100%·200%)에서는 예전과 똑같다.
#pragma once

#include <JuceHeader.h>

namespace meon::ui::hairline
{

/** 가로선: (x, y) 에서 너비 w, 두께 1px. comp 는 지금 그리는 구성 요소 (창 안의 자리로 물리 좌표를 셈한다) */
void horizontal (juce::Graphics&, const juce::Component& comp, float x, float y, float w);
/** 세로선: (x, y) 에서 높이 h, 두께 1px */
void vertical (juce::Graphics&, const juce::Component& comp, float x, float y, float h);
/** 1px 두께 사각형 (가로선이든 세로선이든: 짧은 쪽이 두께) */
void line (juce::Graphics&, const juce::Component& comp, juce::Rectangle<float> r);
/** outer 안쪽에 그리는 1px 둥근 테두리 (예전 drawRoundedRectangle (outer.reduced (0.5f), radius, 1.0f) 자리) */
void roundedBorder (juce::Graphics&, const juce::Component& comp, juce::Rectangle<float> outer, float radius);

} // namespace meon::ui::hairline
