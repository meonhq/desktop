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
    check,          // 체크
    gearHeader,     // 헤더 설정 버튼 (선 1.8, 합주 최종 디자인)
    lobby,          // 로비 보기(방 최소화): 아래 화살표 + 바닥 선
    // 사이드바·홈 (홈·사이드바 디자인, 24 그리드 선 1.6)
    navHome,        // 홈
    navRooms,       // 공개 방 (네 칸)
    navHash,        // 코드로 입장 (#)
    sidebar,        // 사이드바 접기·펼치기
    plus,           // 방 만들기·친구 추가 (+)
    door,           // 참여 중인 방
    users,          // 친구 (예전 접힌 사이드바)
    doorPlus,       // 방 만들기 (접힌 사이드바, v2.1)
    userPlus,       // 친구 추가 (접힌 사이드바, v2.1: 선 1.6)
    chevronDown24,  // 사이드바 머리말 접기 화살표 (24 그리드 선 1.6, 14px)
    chevronUpDown,  // 내 프로필 줄 (위아래 화살표)
    chevronLeft,    // 달력 이전 달 (선 2)
    chevronRight,   // 달력 다음 달 (선 2)
    // 헤더 아이콘 (홈·사이드바 디자인 v2.1 최신: 24 그리드 선 1.8, 18px)
    plusHeader,     // 공개 방 머리말 방 만들기 (+)
    logout          // 방 나가기 (문 + 오른쪽 화살표)
};

/** area 안에 비율을 지켜 그린다. angleRad 만큼 가운데 기준 회전 */
void draw (juce::Graphics&, Id, juce::Rectangle<float> area, juce::Colour, float angleRad = 0.0f);

/** 디스코드 로고 (공식 경로, 비율 유지) */
void drawDiscord (juce::Graphics&, juce::Rectangle<float> area, juce::Colour);

/** 파트 아이콘 (API 의 part 키). size×size 를 area 가운데에. colour 는 주변 글자색을 준다.
    모든 자리에서 얇은 버전(선 1px)을 쓴다 (디자인과 같음. 디자인에 없던 방 카드·필터 칩도, REMAKE_PLAN 3.6) */
enum class PartWeight { regular, thin };
void drawPart (juce::Graphics&, const juce::String& partKey, juce::Rectangle<float> area, float size, juce::Colour,
               PartWeight weight = PartWeight::thin);

/** 파트 이름 (화면 문구): 보컬 / 기타 / 베이스 / 드럼 / 키보드 / 그 외 */
juce::String partLabel (const juce::String& partKey);

} // namespace meon::ui::icons
