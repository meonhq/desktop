// MEON ui - 팝오버 겉모양 그리기 (값은 Tokens.h popoverStyle).
// 판(panel) 바깥에 그림자 두 겹 → 흰 면 → 1px 테두리 (#EBEBEB, 물리 픽셀에 맞춤).
// 그림자는 판보다 shadowMargin() 만큼 바깥까지 번진다. 그리는 구성 요소가 그만큼 커야(또는 창 전체를 덮어야) 잘리지 않는다.
// 흐림 계산은 판 크기·화면 배율이 바뀔 때만 한다. 그 밖에는(창 크기·자리 변경, 뒤에서 미터가 움직여 다시 그릴 때) 만들어 둔 그림을 붙이기만 한다.
#pragma once

#include <JuceHeader.h>

namespace meon::ui
{

class PopoverSurface
{
public:
    /** 판 바깥으로 그림자가 번지는 폭 (논리 px, 사방 같음) */
    static int shadowMargin();

    /** panel: 구성 요소 c 안의 판 자리. 그림자·면·테두리를 그린다 (안쪽 내용은 부르는 쪽이 이어서 그린다) */
    void paint (juce::Graphics&, const juce::Component& c, juce::Rectangle<int> panel);

    /** 그림자 그림을 새로 만든 횟수 (개발용 확인: 크기가 그대로면 늘지 않아야 한다) */
    int getBuildCount() const { return builds; }

    /** 그림자 그림 (판 크기 w×h, 배율 scale). 바깥 여백 포함, 물리 픽셀 크기. 검정 + 알파 */
    static juce::Image renderShadow (int w, int h, float scale);

private:
    juce::Image shadow;
    int cachedW = 0, cachedH = 0, builds = 0;
    float cachedScale = 0.0f;
};

} // namespace meon::ui
