// MEON ui - 디자인 토큰 (docs/design/MEON_Lab.dc.html 의 값 그대로).
// 포인트 핑크(#FFD8E4)는 면(배경·채움·테두리)에만 쓰고 글자에는 쓰지 않는다.
// 상태·알림 표시(알림 점, 친구 상태 점 등)에 검은 원(ink)을 쓰지 않는다. 툴팁·아이콘은 해당 없음 (합주 최종 디자인).
#pragma once

#include <JuceHeader.h>
#include "../../MeonTheme.h"   // 글꼴(Pretendard)·문단·이모지·로고 그리기 (R6 에서 ui/theme 로 옮긴다)

namespace meon::ui
{

namespace col
{
    const juce::Colour ink          { 0xFF1A1A1Au };   // 제목·주요 글자
    const juce::Colour inkSub       { 0xFF767676u };   // 본문·보조
    const juce::Colour inkFaint     { 0xFFAAAAAAu };   // 비활성·안내·placeholder
    const juce::Colour inkGhost     { 0xFFCCCCCCu };   // 초기화 버튼(바꾼 값 없음)
    const juce::Colour white        { 0xFFFFFFFFu };
    const juce::Colour accent       { 0xFFFFD8E4u };   // 포인트 핑크 면
    const juce::Colour accentHover  { 0xFFF5C2D2u };
    const juce::Colour accentSoft   { 0xFFFFF0F4u };   // 연한 핑크: 선택 칩·경고 면·내 말풍선
    const juce::Colour line         { 0xFFE0E0E0u };   // 테두리가 아닌 회색 면: 토글 끔, 슬라이더·진행 막대 트랙, 온보딩 단계 막대, 미터 상자 테두리
    const juce::Colour miniBorder   { 0xFFE0E0E0u };   // 19px 미니 버튼 테두리 (관리·거절·복사…, 작은 선택 버튼). 이것만 #E0E0E0 (합주 최종 v2)
    const juce::Colour borderSubtle { 0xFFEBEBEBu };   // 선 하나로 쓰는 테두리: 구분선·카드·팝오버·패널, 입력 칸, 32·40px 버튼(흰 보조·선택·비활성 주 버튼), 아바타
                                                       // (예전 #E0E0E0·#D5D5D5 가 진해 보여 연하게. 그릴 때는 ui::hairline 으로 물리 픽셀에 맞춘다)
    const juce::Colour focusBorder  { 0xFFAAAAAAu };   // 입력 칸 포커스 테두리 (코드 칸은 지금 칸·숫자가 들어간 칸)
    const juce::Colour field        { 0xFFD5D5D5u };   // 체크박스 테두리, 스크롤 막대, 오프라인 점 (입력·버튼 테두리는 borderSubtle)
    const juce::Colour softBg       { 0xFFFCFCFCu };   // 작은 버튼·미터 상자 배경
    const juce::Colour hoverBg      { 0xFFF5F5F5u };   // 호버·비활성 면
    const juce::Colour badgeBg      { 0xFFF5F5F5u };   // 핑 배지·작은 태그(친구·방장·비공개) 면, 글자는 inkSub. 테두리 없음
    const juce::Colour rowHover     { 0xFFF8F8F8u };   // 목록 행 호버
    const juce::Colour tabOn        { 0xFFF0F0F0u };   // 설정 탭 선택
    const juce::Colour danger       { 0xFFD92D20u };   // 오류·삭제
    const juce::Colour dangerLine   { 0xFFF2C4C0u };   // 방 닫기 버튼 테두리
    const juce::Colour dangerSoft   { 0xFFFDF2F1u };
    const juce::Colour success      { 0xFF1F9D55u };   // "쓸 수 있는 아이디예요"
    const juce::Colour discord      { 0xFF5865F2u };
    const juce::Colour discordHover { 0xFF4752C4u };
    const juce::Colour overlay      { 0x661A1A1Au };   // rgba(26,26,26,0.4)
    const juce::Colour statusOff    { 0xFFD5D5D5u };   // 친구 상태 점: 오프라인
    const juce::Colour notify       { 0xFFF2545Bu };   // 알림 점 전용 (알림·입장 요청 버튼)
    const juce::Colour live         { 0xFF34C759u };   // 친구 상태 점: 온라인·합주 중 (meterGreen 과 값은 같지만 의미가 달라 따로 둔다)
    const juce::Colour pageBg       { 0xFFFFFFFFu };
    // 홈·합주 화면의 패널들을 감싸는 작업 영역 바탕 (디자인 파일은 흰색, REMAKE_PLAN 3.6 의 결정이 우선).
    // 안쪽 카드(흰 면 + #E0E0E0 테두리)는 그대로, 상단 바·배너·모달·설정 화면은 흰색 그대로
    const juce::Colour surfaceCanvas { 0xFFFCFCFCu };

    // 미터 (Logic 스타일)
    const juce::Colour meterGreen   { 0xFF34C759u };
    const juce::Colour meterYellow  { 0xFFFFCC00u };
    const juce::Colour meterOrange  { 0xFFFF9500u };
    const juce::Colour meterRed     { 0xFFFF3B30u };
    const juce::Colour meterCover   { 0xFFE5E5EAu };
}

/** 팝오버 겉모양: 프로필 메뉴·알림·입장 요청 패널. 앞으로 생길 팝오버도 이 값만 쓴다.
    그리기는 ui::PopoverSurface (theme/PopoverSurface): 그림자 → 흰 면 → 1px 테두리. 토스트·모달·버튼은 해당 없음 */
namespace popoverStyle
{
    const juce::Colour background { 0xFFFFFFFFu };
    const juce::Colour border     { 0xFFEBEBEBu };   // popoverBorder: 1px (col::borderSubtle 과 같은 값)
    constexpr float radius = 6.0f;

    /** popoverShadow: 디자인 CSS box-shadow 한 겹 = "0 <dy>px <blur>px rgba(0,0,0,<alpha>)". 앞에 적은 겹이 위 */
    struct ShadowLayer { float dy, blur, alpha; };
    constexpr ShadowLayer shadow[] = { { 4.0f, 20.0f, 0.07f },    // 넓고 옅게
                                       { 1.0f,  3.0f, 0.04f } };  // 가장자리에 붙는 얇은 그림자
    /** CSS 값을 JUCE 로 옮기는 비율 (브라우저 캡처와 나란히 놓고 가장자리 밝기를 픽셀로 비교해 맞춘 값, PR #90 보고서):
        흐림은 가우스 표준편차 σ = blur × sigmaPerBlur (논리 px, CSS 명세와 같은 B/2),
        진하기는 alpha × opacityScale (크롬은 옅은 그림자를 8비트로 깎아 같은 수치보다 조금 옅게 그린다) */
    constexpr float sigmaPerBlur = 0.5f;
    constexpr float opacityScale = 0.92f;
}

/** 접기·펼치기 (채팅·친구·내 오디오·모니터·홈 친구가 모두 이 값을 읽는다. ui/widgets/FoldAnimator) */
namespace motion
{
    constexpr int foldDurationMs = 180;
    constexpr double foldEase[4] = { 0.2, 0.0, 0.0, 1.0 };   // cubic-bezier(0.2, 0, 0, 1)
    // 모달 (디자인 v2.1 mEl): 열기 덮개 200ms ease-out · 상자 240ms foldEase (8px 아래·98%·투명 → 제자리), 닫기 상자 160ms foldEase (→ 4px 아래·98%·투명) · 덮개 180ms ease-in
    constexpr double modalOpenDimMs = 200.0, modalOpenBoxMs = 240.0, modalCloseBoxMs = 160.0, modalCloseDimMs = 180.0;
    constexpr float modalOpenDy = 8.0f, modalCloseDy = 4.0f, modalScale = 0.98f;
    constexpr double easeOut[4] = { 0.0, 0.0, 0.58, 1.0 };   // CSS ease-out
    constexpr double easeIn[4] = { 0.42, 0.0, 1.0, 1.0 };    // CSS ease-in
    // 방 화면 ↔ 공개 방: 내 오디오·모니터 카드 묶음 (디자인 v2.1 barFlip) 320ms foldEase, 그림자 0 ↔ 팝오버 그림자
    constexpr double dockFlipMs = 320.0;
}

namespace metric
{
    constexpr int padX = 18;            // 좌우 여백
    constexpr int padY = 12;            // 상하 여백
    constexpr int headerH = 58;         // 로고 34 + 12 × 2
    constexpr int logoH = 34;           // 로고 자리 높이 (헤더·온보딩 배치)
    constexpr float logoDrawH = 31.0f;  // 그리는 높이: 새 로고 파일은 안쪽 여백이 없어 예전 로고(34, 여백 포함)와 같은 크기로 (REMAKE_PLAN 3.6)
    constexpr int buttonH = 32;
    constexpr int smallH = 19;          // 작은 버튼·슬라이더·미터 상자
    constexpr int bigButtonH = 40;      // 온보딩 버튼
    constexpr int gap = 9;              // 나란히 붙는 카드·헤더 버튼 사이
    constexpr int pagePad = 18;         // 사이드바 오른쪽 쪽(홈·공개 방)의 바깥 여백: 머리말 아래 본문 가장자리 ~ 카드 (홈·사이드바 디자인 padding 18)
    constexpr float radius = 6.0f;      // 카드·버튼·입력
    constexpr float smallRadius = 5.0f; // 작은 버튼·미터 상자

    // 창 크기 (6.1, Q19)
    constexpr int appMinW = 1024, appMinH = 680, appDefaultW = 1280, appDefaultH = 800;
    constexpr int pluginMinW = 900, pluginMinH = 600, pluginDefaultW = 900, pluginDefaultH = 600;
}

/** 디자인의 font: <weight> <px> Pretendard */
inline juce::Font font (int weight, float px) { return meon::Fonts::get (weight, px); }

inline float textWidth (const juce::Font& f, const juce::String& s) { return meon::textWidth (f, s); }

} // namespace meon::ui

#ifndef TXT
 #define TXT(s) juce::String (juce::CharPointer_UTF8 (s))
#endif
