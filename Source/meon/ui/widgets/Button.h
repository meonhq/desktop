// MEON ui - 버튼. 디자인의 버튼 모양을 종류(Kind)로 묶었다.
//   primary      32px 포인트 (방 만들기·입장·저장)          600 13px
//   secondary    32px 흰 면 + #EBEBEB (방 나가기)         600 13px #1A1A1A
//   quiet        32px 투명 + #EBEBEB (설정)               500 13px #767676
//   large        40px 포인트, 폭 가득 (온보딩)               600 13px
//   dangerOutline 32px 흰 면 + #F2C4C0, 빨간 글자 (방 닫기)
//   small        19px #FCFCFC + #E0E0E0 (관리·거절·복사…)  400 12px #767676
//   smallPrimary 19px 포인트 (수락·확인)                     500 12px
//   smallDanger  19px 빨간 글자 (친구 삭제·차단·내보내기)
//   largeSecondary 40px 흰 면 + #EBEBEB (온보딩 "이전")      600 13px #767676
//   cancel       32px 흰 면 + #EBEBEB (취소·공개 방 보기·중복 확인) 600 13px #767676 → 호버 #1A1A1A, 면 #F5F5F5
//   link         글자만 (전송)                             500 13px #767676 → 호버 #1A1A1A
//   linkSmall    글자만 (비밀번호를 잊었나요?)             500 12px #767676 → 호버 #1A1A1A
//   linkUnderline 밑줄 글자 (회원가입·로그인·코드 다시 받기) 500 12px #1A1A1A
// 상태: 켬(toggled)·대기(pending, 연한 포인트 면)·비활성. 두 번 눌러 확정(setConfirm)도 여기서 한다.
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

    /** 작은 버튼의 켬 상태 (음소거·송출 정지): 포인트 면 */
    void setOn (bool);
    bool isOn() const { return on; }

    /** 대기 상태 (초대 3초): 연한 포인트 면 + 포인트 테두리 */
    void setPending (bool);

    /** 비활성 모양 (클릭은 막지 않는다. 막으려면 setEnabled) */
    void setDimmed (bool);
    /** 흐린 모양이어도 누르면 onPress (디자인: 친구 추가 '요청' 은 비어 있으면 흐리지만 누르면 안내 문구) */
    void setClickableWhenDimmed (bool b) { clickWhenDimmed = b; }

    /** 두 번 눌러 확정: 첫 클릭에 armedText 와 빨간(또는 포인트 색) 면으로 바뀌고, 다시 누르면 onClick.
        다른 곳을 누르거나 3초가 지나면 되돌아간다. */
    void setConfirm (const juce::String& armedText, bool accent = false);
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
    bool on = false, pending = false, dimmed = false, armed = false, confirm = false, confirmAccent = false, flashing = false;
    bool clickWhenDimmed = false;
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

/** 접기 화살표 (19×19, 12px 화살표). 각은 접기 진행값 e(접힘 0 ~ 펼침 1)로 정한다:
    세로 패널 −90° + 90° × e (펼치면 아래), 옆으로 접는 패널(내 오디오·모니터) −90° + 180° × e. 아이콘 중심 기준 회전 */
class CollapseButton : public juce::Button
{
public:
    explicit CollapseButton (bool sideways = false);
    /** 목표 상태 (툴팁). 각은 e 끝 값으로 맞춘다 (움직이는 동안은 setProgress 가 덮어쓴다) */
    void setOpen (bool);
    bool isOpen() const { return open; }
    void setProgress (float e);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    bool open = true, sideways;
    float angle = 0.0f;
};

/** 아이콘 버튼. 24×24 (톱니·닫기, 호버 면 #F5F5F5) 또는 32×32 테두리 (알림·입장 요청, 점 표시) */
class IconButton : public juce::Button
{
public:
    /** ghost32: 헤더 아이콘 (홈·사이드바 디자인 v2.1 최신: 방 만들기·입장 요청). 테두리·면 없이 아이콘만, 누르는 영역 32×32 모서리 6,
                 아이콘 #767676 → 마우스를 올리면 면 #F5F5F5·아이콘 #1A1A1A. 알림 점 테두리도 면 색을 따른다 (흰색 → #F5F5F5) */
    enum class Style { plain24, bordered32, bare19, ghost32 };
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

/** 아이콘만 있는 두 번 눌러 확정 버튼 (방 나가기, 합주 헤더. 홈·사이드바 디자인 v2.1 최신).
    누르는 영역 32×32 모서리 6, 아이콘 18. 평소: 면 없음·아이콘 #767676. 첫 클릭(확인 상태): 면 #FDECEA·아이콘 #D92D20.
    마우스를 올리면 지금 면 위에 검정 4% 를 덧칠한다 (두 상태 모두, 아이콘 색은 그대로).
    다른 곳을 누르거나 3초가 지나면 되돌아간다 (Button::setConfirm 과 같은 규칙). 확인 상태에서 다시 누르면 onConfirm */
class ConfirmIconButton : public juce::Button, private juce::Timer
{
public:
    explicit ConfirmIconButton (int iconId);
    ~ConfirmIconButton() override;
    std::function<void()> onConfirm;
    std::function<void()> onStateChange;   // 확인 상태가 바뀔 때
    bool isArmed() const { return armed; }
    void disarm();
    /** 평소 툴팁 (확인 상태에서는 말풍선이 대신하므로 비운다) */
    void setNormalTooltip (const juce::String& t) { normalTip = t; if (! armed) setTooltip (t); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void clicked() override;

private:
    int icon;
    bool armed = false;
    juce::String normalTip;
    void timerCallback() override { disarm(); }
    struct Outside : juce::MouseListener
    {
        explicit Outside (ConfirmIconButton& o) : owner (o) {}
        void mouseDown (const juce::MouseEvent&) override;
        ConfirmIconButton& owner;
    } outside { *this };
};

} // namespace meon::ui
