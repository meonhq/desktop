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

    /** panel: 구성 요소 c 안의 판 자리. 그림자·면·테두리를 그린다 (안쪽 내용은 부르는 쪽이 이어서 그린다).
        fill: 면 색 (기본 popoverStyle::background. 경고 토스트는 연한 핑크) */
    void paint (juce::Graphics&, const juce::Component& c, juce::Rectangle<int> panel, juce::Colour fill);
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

/** 폭이 바뀌는 판의 그림자만 (공개 방 위 내 오디오·모니터 카드: 방 화면과 오가며 폭이 바뀐다). 진하기 opacity (0~1).
    흐림은 판 높이·화면 배율이 바뀔 때만 한 번 계산하고, 폭은 왼쪽·오른쪽 끝을 그대로 두고 가운데 한 줄을 타일로 이어 붙인다
    (판 가장자리에서 모서리 + 그림자 폭보다 안쪽은 가로로 같은 그림자라서 같은 그림이 된다) */
class StretchShadow
{
public:
    void paint (juce::Graphics&, const juce::Component& c, juce::Rectangle<int> panel, float opacity);
    int getBuildCount() const { return builds; }
private:
    juce::Image shadow, midTile;   // midTile: 가운데 한 줄을 가로로 32 번 (타일로 붙인다)
    int cachedH = 0, builds = 0;
    float cachedScale = 0.0f;
    PopoverSurface narrow;   // 끝끼리 겹칠 만큼 좁으면 (거의 없음) 보통 방식
};

/** 판 둘레에 그림자를 그리는 떠 있는 판 (팝오버·토스트, 앞으로 생길 팝오버도 이것을 쓴다).
    구성 요소는 판보다 그림자 폭만큼 사방으로 크고(그림자가 잘리지 않게), 그 자리는 마우스를 받지 않는다.
    자리는 setPanelBounds 로 판 기준으로 정하고, 안쪽은 panel() 기준으로 배치·그리기 */
class SurfaceComponent : public juce::Component
{
public:
    void setPanelBounds (juce::Rectangle<int> panelInParent) { setBounds (panelInParent.expanded (PopoverSurface::shadowMargin())); }
    void setPanelBounds (int x, int y, int w, int h) { setPanelBounds ({ x, y, w, h }); }
    juce::Rectangle<int> getPanelBounds() const { return getBounds().reduced (PopoverSurface::shadowMargin()); }
    const PopoverSurface& getSurface() const { return surface; }
    bool hitTest (int x, int y) override { return panel().contains (x, y); }
protected:
    juce::Rectangle<int> panel() const { return getLocalBounds().reduced (PopoverSurface::shadowMargin()); }
    void paintSurface (juce::Graphics& g, juce::Colour fill);
    void paintSurface (juce::Graphics& g);
private:
    PopoverSurface surface;
};

} // namespace meon::ui
