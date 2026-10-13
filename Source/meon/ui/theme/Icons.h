// MEON ui - 아이콘. 디자인의 SVG 경로를 그대로 쓰고, 색만 바꿔 그린다 (색별로 한 번 읽어 캐시).
#pragma once

#include "Tokens.h"

namespace meon::ui::icons
{

enum class Id
{
    chevronDown,    // 12 그리드, 접기 화살표 (회전해서 씀)
    close12,        // 12 그리드 ×
    close24,        // 24 그리드 × (모달 닫기)
    gear,           // 방 설정
    bell,           // 알림
    filter,         // 필터
    personPlus,     // 입장 요청·친구 추가
    external,       // 외부 링크
    speaker,        // 볼륨
    play,           // 재생 (12×14)
    check           // 체크
};

/** area 안에 비율을 지켜 그린다. angleRad 만큼 가운데 기준 회전 */
void draw (juce::Graphics&, Id, juce::Rectangle<float> area, juce::Colour, float angleRad = 0.0f);

/** 디스코드 로고 (공식 경로, 비율 유지) */
void drawDiscord (juce::Graphics&, juce::Rectangle<float> area, juce::Colour);

/** 파트 아이콘 (API 의 part 키). 굵기 1.5px, size×size 를 area 가운데에 */
void drawPart (juce::Graphics&, const juce::String& partKey, juce::Rectangle<float> area, float size, juce::Colour);

/** 파트 이름 (화면 문구): 보컬 / 기타 / 베이스 / 드럼 / 키보드 / 그 외 */
juce::String partLabel (const juce::String& partKey);

} // namespace meon::ui::icons
