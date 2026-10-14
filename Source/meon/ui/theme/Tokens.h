// MEON ui - 디자인 토큰 (docs/design/MEON_Lab.dc.html 의 값 그대로).
// 노랑(#FFD700)은 면(배경·채움·테두리)에만 쓰고 글자에는 쓰지 않는다.
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
    const juce::Colour accent       { 0xFFFFD700u };   // 노랑 면
    const juce::Colour accentHover  { 0xFFEBC600u };
    const juce::Colour accentSoft   { 0xFFFFF8D6u };   // 선택 칩·경고 면·내 말풍선
    const juce::Colour line         { 0xFFE0E0E0u };   // 카드 테두리·구분선
    const juce::Colour field        { 0xFFD5D5D5u };   // 입력·버튼 테두리
    const juce::Colour softBg       { 0xFFFCFCFCu };   // 작은 버튼·미터 상자 배경
    const juce::Colour hoverBg      { 0xFFF5F5F5u };   // 호버·비활성 면
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
    const juce::Colour pageBg       { 0xFFFFFFFFu };

    // 미터 (Logic 스타일)
    const juce::Colour meterGreen   { 0xFF34C759u };
    const juce::Colour meterYellow  { 0xFFFFCC00u };
    const juce::Colour meterOrange  { 0xFFFF9500u };
    const juce::Colour meterRed     { 0xFFFF3B30u };
    const juce::Colour meterCover   { 0xFFE5E5EAu };
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
    constexpr float radius = 6.0f;      // 카드·버튼·입력
    constexpr float smallRadius = 5.0f; // 작은 버튼·미터 상자
    constexpr float pingWarnMs = 18.0f;

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
