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
    const juce::Colour line         { 0xFFE0E0E0u };   // 컨트롤 테두리: 작은 버튼(19)·비활성 버튼·입력·호버, 미터 상자·팬 노브·선택 칩, 막대 트랙
    const juce::Colour borderSubtle { 0xFFEBEBEBu };   // 영역을 나누거나 감싸는 선: 사이드바·제목 줄·카드·패널 테두리, 목록 구분선, 아바타 테두리
                                                       // (예전 #E0E0E0 이 진해 보여 연하게. 그릴 때는 ui::hairline 으로 물리 픽셀에 맞춘다)
    const juce::Colour field        { 0xFFD5D5D5u };   // 입력·버튼 테두리
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

/** 접기·펼치기 (채팅·친구·내 오디오·모니터·홈 친구가 모두 이 값을 읽는다. ui/widgets/FoldAnimator) */
namespace motion
{
    constexpr int foldDurationMs = 180;
    constexpr double foldEase[4] = { 0.2, 0.0, 0.0, 1.0 };   // cubic-bezier(0.2, 0, 0, 1)
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
