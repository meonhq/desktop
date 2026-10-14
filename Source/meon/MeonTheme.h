// MEON - 디자인 토큰 (색, 글꼴, 치수)
// 값은 디자인 명세(MEON Yellow.dc.html 공통 스펙)에서 그대로 가져온다. 임의로 추가한 값은 주석에 [임의] 표시.
#pragma once

#include <JuceHeader.h>

/** 한국어 등 UTF-8 리터럴을 juce::String 으로 (MSVC 는 /utf-8 옵션 필요) */
#define TXT(s) juce::String (juce::CharPointer_UTF8 (s))

namespace meon
{

namespace col
{
    // 노랑(accent, accentDown, warnBg, warnBorder)은 배경·채우기에만 쓴다. 글자에는 쓰지 않는다.
    // 글자 회색은 세 단계뿐: ink #1A1A1A(제목·주요) / inkSub #767676(본문·보조·링크) / disabled #AAAAAA(비활성)
    // (#8F7200 포인트 글자, #4A4A4A 본문 글자는 폐기 → inkSub)
    const juce::Colour accent      { 0xFFFFD700u };  // 포인트 (유일한 컬러, 노랑 시안)
    const juce::Colour accentDown  { 0xFFEBC600u };  // 포인트 눌림
    const juce::Colour onAccent    { 0xFF1A1A1Au };  // 노랑 면 위 글자·아이콘
    const juce::Colour warnBg      { 0xFFFFF8D6u };  // 경고 배경
    const juce::Colour warnBorder  { 0xFFF0DE8Au };  // 경고 테두리
    const juce::Colour ink         { 0xFF1A1A1Au };  // 제목·주요 텍스트
    const juce::Colour inkSub      { 0xFF767676u };  // 본문·보조 텍스트, 링크 (흰 바탕 대비 4.5:1)
    const juce::Colour disabled    { 0xFFAAAAAAu };  // 비활성
    const juce::Colour panel       { 0xFFF5F5F5u };  // 패널 / 비활성 카드
    const juce::Colour border      { 0xFFD5D5D5u };  // 테두리 · 입력 필드
    const juce::Colour cardBorder  { 0xFFE0E0E0u };  // 카드 테두리 · 구분선
    const juce::Colour meterTrack  { 0xFFECECECu };  // 레벨 미터 트랙
    const juce::Colour white       { 0xFFFFFFFFu };
    const juce::Colour badgeBg     { 0xFFF7F7F7u };  // 단축키 배지 배경
    const juce::Colour progressOff { 0xFFDDDDDDu };  // 진행 막대 (이후 단계)
    const juce::Colour titleBar    { 0xFFF0F0F0u };
    const juce::Colour titleInk    { 0xFF8A8A8Au };
}

namespace metric
{
    constexpr int   radiusButton = 6;    // 버튼·입력
    constexpr int   radiusCard   = 8;    // 카드
    constexpr int   radiusWindow = 10;   // 창
    constexpr int   fadeMs       = 120;  // 전환 페이드
    constexpr float pingWarnMs   = 18.0f;
    constexpr int   maxMembers   = 5;    // 나 포함
    constexpr int   maxOthers    = 4;    // 멤버 카드 수

    // 드롭다운 목록 (디자인에 열린 상태가 없어 임의로 정함)
    constexpr int   popupPad        = 4;   // 목록 창 안쪽 여백
    constexpr int   popupGap        = 4;   // 필드와 목록 사이 간격
    constexpr int   popupItemRadius = 4;   // 항목 호버 배경 모서리
}

/** Pretendard 글꼴 캐시. 에디터가 SharedResourcePointer 로 소유한다. */
class FontCache
{
public:
    FontCache();
    ~FontCache();

    juce::Typeface::Ptr get (int weight) const;
    juce::Typeface::Ptr getForInput (int weight) const;
    static FontCache* instance();

private:
    juce::Typeface::Ptr regular, medium, semiBold, bold;
    juce::Typeface::Ptr inputRegular, inputMedium, inputSemiBold, inputBold;   // Windows: 이모지를 한 글자로 재는 입력창용
    juce::Array<juce::Typeface::Ptr> memoryFonts;   // Windows: 등록 유지용
};

struct Fonts
{
    /** weight: 400 / 500 / 600 / 700,  px: CSS font-size (em 높이) */
    static juce::Font get (int weight, float px);

    /** 입력창용. Windows 에서는 이모지 묶음을 한 글자·일정한 폭으로 재서 커서 위치가 맞는다 (MeonEmoji.h). */
    static juce::Font forInput (int weight, float px);
};

/** 여러 줄 문단을 CSS line-height 처럼 그린다 (lineHeightPx = 줄 간격 픽셀). */
void drawParagraph (juce::Graphics& g, const juce::String& text, const juce::Font& font, juce::Colour colour,
                    juce::Rectangle<float> area, float lineHeightPx,
                    juce::Justification justification = juce::Justification::topLeft);

/** 문단이 차지할 높이를 계산한다. */
float paragraphHeight (const juce::String& text, const juce::Font& font, float width, float lineHeightPx);

/** drawParagraph 와 같지만 이모지 같은, Pretendard 에 없는 글자도 그리고 긴 단어·한글도 줄을 바꾼다.
    macOS 는 CoreText 로, Windows 는 직접 배치해서 그린다 (MeonEmoji.h). 높이는 paragraphHeightWithEmoji 로 잰다. */
void drawParagraphWithEmoji (juce::Graphics& g, const juce::String& text, const juce::Font& font, juce::Colour colour,
                             juce::Rectangle<float> area, float lineHeightPx,
                             juce::Justification justification = juce::Justification::topLeft);

/** 문단 속 링크 범위들 (글자 위치, 끝 미포함, 앞에서부터 겹치지 않게) */
using TextRanges = std::vector<juce::Range<int>>;

/** 위와 같고, links 범위는 linkColour 와 밑줄로 그린다. */
void drawParagraphWithEmoji (juce::Graphics& g, const juce::String& text, const juce::Font& font, juce::Colour colour,
                             juce::Rectangle<float> area, float lineHeightPx, juce::Justification justification,
                             const TextRanges& links, juce::Colour linkColour);

/** 같은 인자로 그린 문단에서 p 가 links 의 몇 번째 위에 있는지 (없으면 -1). 위쪽 정렬만 지원한다. */
int hitTestParagraphLink (const juce::String& text, const juce::Font& font, juce::Rectangle<float> area, float lineHeightPx,
                          juce::Justification justification, const TextRanges& links, juce::Point<float> p);

float paragraphHeightWithEmoji (const juce::String& text, const juce::Font& font, float width, float lineHeightPx);

/** 한 줄 텍스트를 이모지까지 그린다. 넘치면 끝을 '…' 로 줄인다. */
void drawLineWithEmoji (juce::Graphics& g, const juce::String& text, const juce::Font& font, juce::Colour colour,
                        juce::Rectangle<float> area, juce::Justification justification = juce::Justification::centredLeft);

/** 대체 글꼴까지 반영한 한 줄 텍스트 폭 */
float lineWidthWithEmoji (const juce::String& text, const juce::Font& font);

/** 텍스트 폭 */
float textWidth (const juce::Font& font, const juce::String& text);

/** 브랜드 SVG(images/brand)를 색 없이 도형으로 읽는다 (viewBox 좌표). 선과 면을 따로 모은다.
    JUCE 의 SVG 파서는 vector-effect="non-scaling-stroke" 를 모르므로(선도 같이 커진다), 선은 그리는 쪽에서
    화면 px 굵기로 긋는다. 파일에 박힌 색(#1A1A1A)은 쓰지 않고 그리는 쪽이 정한다. */
struct SvgShape
{
    juce::Path stroke, fill;
    float strokeWidth = 0.0f;           // SVG 의 stroke-width (non-scaling-stroke 이므로 화면 px)
    juce::Rectangle<float> viewBox;
};
/** BinaryData 의 원래 파일 이름(예: "part-bass.svg")으로 읽는다. 없으면 빈 도형 */
const SvgShape& brandSvg (const char* fileName);

/** 로고. 넓은 자리는 워드마크(images/brand/logo/meon_logo.svg), 좁은 자리는 M 마크(meon_mark.svg).
    heightPx 는 SVG(viewBox) 높이, 폭은 비율대로 정해진다. 모양은 그대로 두고 크기·색만 바꾼다 (밝은 바탕 col::ink). */
enum class LogoKind { wordmark, mark };
void drawLogo (juce::Graphics& g, juce::Rectangle<float> area, float heightPx, juce::Colour colour = col::ink,
               juce::Justification justification = juce::Justification::centred, LogoKind kind = LogoKind::wordmark);
float logoWidth (float heightPx, LogoKind kind = LogoKind::wordmark);

/** 자간(letter-spacing, em 단위)이 있는 한 줄 텍스트 */
void drawSpacedText (juce::Graphics& g, const juce::String& text, const juce::Font& font, float fontPx, float letterSpacingEm,
                     juce::Colour colour, juce::Rectangle<float> area, juce::Justification justification = juce::Justification::centred);
float spacedTextWidth (const juce::String& text, const juce::Font& font, float fontPx, float letterSpacingEm);

} // namespace meon
