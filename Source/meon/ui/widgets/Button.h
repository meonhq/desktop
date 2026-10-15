// MEON ui - 버튼. 디자인의 버튼 모양을 종류(Kind)로 묶었다.
//   primary      32px 노랑 (방 만들기·입장·저장)          600 13px
//   secondary    32px 흰 면 + #D5D5D5 (방 나가기)         600 13px #1A1A1A
//   quiet        32px 투명 + #D5D5D5 (설정)               500 13px #767676
//   large        40px 노랑, 폭 가득 (온보딩)               600 13px
//   dangerOutline 32px 흰 면 + #F2C4C0, 빨간 글자 (방 닫기)
//   small        19px #FCFCFC + #E0E0E0 (관리·거절·복사…)  400 12px #767676
//   smallPrimary 19px 노랑 (수락·확인)                     500 12px
//   smallDanger  19px 빨간 글자 (친구 삭제·차단·내보내기)
//   largeSecondary 40px 흰 면 + #D5D5D5 (온보딩 "이전")      600 13px #767676
//   cancel       32px 흰 면 + #D5D5D5 (취소·거절·중복 확인)  600 13px #767676 → 호버 #1A1A1A
//   link         글자만 (전송)                             500 13px #767676 → 호버 #1A1A1A
//   linkSmall    글자만 (비밀번호를 잊었나요?)             500 12px #767676 → 호버 #1A1A1A
//   linkUnderline 밑줄 글자 (회원가입·로그인·코드 다시 받기) 500 12px #1A1A1A
// 상태: 켬(toggled)·대기(pending, 노란 연한 면)·비활성. 두 번 눌러 확정(setConfirm)도 여기서 한다.
#pragma once

#include "../theme/Tokens.h"

namespace meon::ui
{

class Button : public juce::Button, private juce::Timer
{
public:
    enum class Kind { primary, secondary, quiet, large, largeSecondary, dangerOutline, small, smallPrimary, smallDanger, link, linkSmall, linkUnderline, cancel };

    struct Look
    {
        juce::Colour fill, border, ink;
        int weight = 400;
    };

    Button (Kind, const juce::String& text = {});
    ~Button() override;

    void setKind (Kind);
    Kind getKind() const { return kind; }

    /** 작은 버튼의 켬 상태 (음소거·송출 정지): 노란 면 */
    void setOn (bool);
    bool isOn() const { return on; }

    /** 대기 상태 (초대 3초): 연한 노랑 면 + 노란 테두리 */
    void setPending (bool);

    /** 비활성 모양 (클릭은 막지 않는다. 막으려면 setEnabled) */
    void setDimmed (bool);

    /** 두 번 눌러 확정: 첫 클릭에 armedText 와 빨간(또는 노란) 면으로 바뀌고, 다시 누르면 onClick.
        다른 곳을 누르거나 3초가 지나면 되돌아간다. */
    void setConfirm (const juce::String& armedText, bool yellow = false);
    bool isArmed() const { return armed; }
    void disarm();

    /** 글자를 잠깐 바꿨다가 ms 뒤 되돌린다 (복사 → "복사됨"). 그 사이 다시 부르면 시간을 처음부터 센다.
        글자 폭이 바뀌므로 바뀔 때와 되돌아갈 때 onStateChange 를 부른다 (부모가 다시 배치) */
    void flashText (const juce::String& text, int ms);
    bool isFlashing() const { return flashing; }

    /** 내용에 맞는 폭 (여백 포함) */
    int getIdealWidth() const;

    /** 고정 폭 글자 (가운데 정렬, 숫자 칸) */
    void setFixedWidth (int w) { fixedWidth = w; }

    Look currentLook() const;

    /** 눌렀을 때 (두 번 눌러 확정이면 두 번째에). juce::Button::onClick 대신 이것을 쓴다 */
    std::function<void()> onPress;
    /** 확정 대기로 바뀌거나 되돌아갈 때 (글자 폭이 바뀌므로 다시 배치) */
    std::function<void()> onStateChange;

protected:
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void clicked() override;

private:
    Kind kind;
    bool on = false, pending = false, dimmed = false, armed = false, confirm = false, confirmYellow = false, flashing = false;
    juce::String normalText, armedText;
    int fixedWidth = 0;

    void timerCallback() override;

    struct Outside : juce::MouseListener   // 확정 대기 중 다른 곳을 누르면 해제
    {
        explicit Outside (Button& b) : owner (b) {}
        void mouseDown (const juce::MouseEvent&) override;
        Button& owner;
    };
    Outside outside { *this };
};

/** 디스코드 버튼 (#5865F2, 흰 로고·글자, 바깥 링크 표시). 높이 32(로고 16) 또는 40(로고 18) */
class DiscordButton : public juce::Button
{
public:
    DiscordButton (const juce::String& text, int height);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
private:
    int height;
};

/** 접기 화살표 (19×19, 12px 화살표). open 이면 아래, 아니면 왼쪽(-90°). 송출·모니터 카드는 90°/-90° */
class CollapseButton : public juce::Button
{
public:
    explicit CollapseButton (bool sideways = false);
    void setOpen (bool);
    bool isOpen() const { return open; }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    bool open = true, sideways;
    float angle = 0.0f, target = 0.0f;
    juce::VBlankAttachment vblank;
};

/** 아이콘 버튼. 24×24 (톱니·닫기, 호버 면 #F5F5F5) 또는 32×32 테두리 (알림·입장 요청, 점 표시) */
class IconButton : public juce::Button
{
public:
    enum class Style { plain24, bordered32, bare19 };
    IconButton (int iconId, Style, float iconPx);
    void setDot (bool);
    void setInk (juce::Colour normal, juce::Colour hover) { ink = normal; inkHover = hover; repaint(); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    int icon;
    Style style;
    float iconPx;
    bool dot = false;
    juce::Colour ink, inkHover;
};

} // namespace meon::ui
