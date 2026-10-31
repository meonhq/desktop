// MEON ui - 표시용: 핑 배지, 아바타(파트 아이콘 원 + 상태 점), 작은 태그, 카드 틀, 모달·팝오버 틀, 덮개.
#pragma once

#include "../theme/Tokens.h"
#include "../theme/PopoverSurface.h"
#include "Button.h"
#include "ModalBase.h"

namespace meon::ui
{

/** 핑 배지 (폭 48, 높이 19, 모서리 5, 400 12px). 연한 회색 면(badgeBg) + #767676 글자, 테두리 없음.
    핑 값과 상관없이 같은 모양 (예전의 18ms 초과 포인트 색 면은 쓰지 않는다, 2026-10-16). text 가 "나" 같은 글자면 그대로 */
class PingBadge : public juce::Component
{
public:
    PingBadge();
    void setPing (std::optional<int> ms);   // 없으면 "—"
    void setText (const juce::String& t) { text = t; repaint(); }
    const juce::String& getText() const { return text; }   // 보이는 글자 (소개 페이지 캡처의 핑 검사용)
    void paint (juce::Graphics&) override;
private:
    juce::String text { "—" };
};

/** 파트 아이콘 원. size 41(멤버 행)·32(친구 목록)·…, 테두리 #EBEBEB.
    상태 점(9px, 흰 테두리 2px): 온라인 포인트 #FFD8E4, 방에 있음 #1A1A1A, 오프라인 #D5D5D5 */
class Avatar : public juce::Component
{
public:
    enum class Status { none, online, inRoom, offline };
    Avatar (float size, float iconPx);
    void setPart (const juce::String& partKey) { part = partKey; repaint(); }
    void setDimmed (bool d) { dimmed = d; repaint(); }   // 오프라인·소리 꺼짐: 아이콘을 흐린 글자색(#AAAAAA)으로, 옆 이름과 같게
    void setInk (juce::Colour c) { ink = c; repaint(); } // 아이콘 색 = 주변 글자색 (기본 #1A1A1A)
    void setStatus (Status s) { status = s; repaint(); }
    /** 프로필 창: 상태 점 12×12 (아바타 오른쪽 아래에 붙임) */
    void setLargeStatusDot (bool b) { largeDot = b; repaint(); }
    void setClickable (bool c) { setMouseCursor (c ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor); }
    std::function<void()> onClick;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    juce::MouseCursor getMouseCursor() override   // onClick 이 있으면 손가락
    {
        return onClick ? juce::MouseCursor (juce::MouseCursor::PointingHandCursor) : juce::Component::getMouseCursor();
    }

    static juce::Colour statusColour (Status);
    /** 원 + 파트 아이콘만 그린다 (채팅처럼 구성 요소 없이 직접 그리는 곳). partKey 가 비면 아이콘 없는 빈 원.
        whiteFill: 원 안을 흰색으로 (나중에 프로필 사진이 생기면 이 자리에 들어간다) */
    static void paintCircle (juce::Graphics&, juce::Rectangle<float> circle, const juce::String& partKey, float iconPx,
                             juce::Colour ink, bool whiteFill);

private:
    float size, iconPx;
    juce::String part { "other" };
    bool dimmed = false;
    juce::Colour ink { col::ink };
    Status status = Status::none;
    bool largeDot = false;
};

/** "비공개" 같은 작은 태그 (500 11px #767676, 여백 2·4, 연한 회색 면 badgeBg, 테두리 없음, 모서리 3) */
/** 작은 태그 상자를 그린다 (Tag, 방 카드 멤버의 친구·방장, 합주 멤버 행의 방장이 함께 쓴다) */
void paintTagBox (juce::Graphics&, juce::Rectangle<float> box, const juce::String& text, const juce::Font&);

class Tag : public juce::Component
{
public:
    explicit Tag (const juce::String& text);
    int getIdealWidth() const;
    void paint (juce::Graphics&) override;
private:
    juce::String text;
};

/** 카드 (테두리 #EBEBEB, 모서리 6, 흰 면). 머리말이 있으면 12·18 여백 + 제목 600 15px + 접기 버튼, 아래 구분선 */
class Card : public juce::Component
{
public:
    Card();
    void setTitle (const juce::String&, bool collapsible = false, bool sidewaysArrow = false);
    void setOpen (bool);
    bool isOpen() const { return open; }
    /** 접기·펼치기 진행값 e (접힘 0 ~ 펼침 1): 화살표 각과 머리말 아래 구분선. setOpen 뒤에 부른다 */
    void setFoldProgress (float e);
    int headerHeight() const { return title.isEmpty() ? 0 : 48; }
    /** 머리말 아래 내용 영역 (구분선 아래) */
    juce::Rectangle<int> contentBounds() const;
    /** 카드가 w×h 일 때의 내용 영역 (움직이는 동안 내용을 끝 크기로 한 번만 배치할 때) */
    juce::Rectangle<int> contentBoundsFor (int w, int h) const;
    std::function<void (bool open)> onToggle;
    /** 머리말 오른쪽 (접기 버튼 왼쪽)에 붙는 구성 요소 */
    void setHeaderAccessory (juce::Component*);

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    int devPaintCount = 0;   // 개발용 (다시 그리기 확인)

private:
    juce::String title;
    bool open = true, collapsible = false, sidewaysFold = false;
    float fold = 1.0f;
    std::unique_ptr<CollapseButton> collapse;
    juce::Component* accessory = nullptr;
};

/** 모달: 덮개(rgba 26,26,26,0.4) + 가운데 흰 상자(모서리 6). 머리말 12·18, 제목 600 15/24px, 닫기 24px.
    덮개를 누르거나 Esc 를 누르면 onClose. 본문은 content 로 넣는다 (머리말 아래 구분선부터).
    열고 닫는 애니메이션은 공통 래퍼(ModalBase) */
class Modal : public ModalBase
{
public:
    Modal (const juce::String& title, int width);
    void setContent (juce::Component* content, int contentHeight);   // 소유하지 않음
    void setContentHeight (int);
    void setTitle (const juce::String& t) { title = t; repaint(); }
    void setShowHeader (bool b) { showHeader = b; resized(); }
    /** 머리말 오른쪽 닫기 버튼 (코드로 입장·새 합주실은 없음) */
    void setShowClose (bool b) { showClose = b; resized(); }
    /** 머리말 오른쪽에 붙는 구성 요소 (방 필터의 "초기화"). 닫기 버튼이 없을 때 그 자리에 */
    void setHeaderAccessory (juce::Component*);
    std::function<void()> onClose;
    /** 덮개를 둘 영역 (창 전체에서 맨 위 막대 등을 뺀 곳). 비면 부모 전체 */
    void setOverlayInsets (juce::BorderSize<int> b) { insets = b; resized(); }

    juce::Rectangle<int> boxBounds() const;
    juce::Rectangle<int> dialogBounds() const override { return boxBounds(); }
    juce::Rectangle<int> dimBounds() const override { return insets.subtractedFrom (getLocalBounds()); }
    void paintDialog (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    friend class UiSelfTest;   // 개발용 화면 자가 테스트 (닫기 버튼)
    juce::String title;
    int width, contentH = 100;
    bool showHeader = true, showClose = true;
    juce::Component* content = nullptr;
    juce::Component* accessory = nullptr;
    IconButton closeButton;
    juce::BorderSize<int> insets;
};

/** 고정폭 숫자 (디자인 font-variant-numeric: tabular-nums). Pretendard 기본 숫자는 폭이 달라서,
    숫자는 '0'~'9' 중 가장 넓은 폭의 칸 가운데에 그린다 (숫자가 바뀌어도 옆 글자가 흔들리지 않는다) */
float tabularWidth (const juce::Font&, const juce::String&);
/** x 에서 시작해 왼쪽 맞춤, 높이 h 의 세로 가운데. 그린 폭을 돌려준다 */
float drawTabular (juce::Graphics&, const juce::Font&, const juce::String&, float x, float y, float h);

/** 버튼 툴팁 (합주 최종 디자인): 면 #1A1A1A, 글자 500 12px 흰색, 안쪽 4·7, 모서리 4. 마우스를 받지 않는다.
    host(그릴 부모)의 자식이고, target 의 아래(또는 위) 6 에 가운데 또는 오른쪽 끝을 맞춘다 */
class Tip : public juce::Component
{
public:
    enum class Place { belowCentre, belowRight, aboveRight };
    Tip();
    /** target 은 host 의 자손이어야 한다 */
    void show (juce::Component& host, juce::Component& target, const juce::String& text, Place);
    /** 두 줄 말풍선 (방 나가기 확인, 홈·사이드바 디자인 v2.1 최신): 안쪽 6·8, 줄 높이 1.4, 줄 사이 2.
        첫 줄 600 흰색, 둘째 줄 500 #D5D5D5. sub 가 비면 첫 줄만 */
    void showNote (juce::Component& host, juce::Component& target, const juce::String& title, const juce::String& sub, Place);
    juce::String getText() const { return text; }
    juce::String getSubText() const { return sub; }
    void hide();
    juce::Component* getTarget() const { return target.getComponent(); }
    void paint (juce::Graphics&) override;
    static int widthFor (const juce::String& text);
    static constexpr int height = 20;
private:
    juce::String text, sub;
    bool note = false;
    juce::Component::SafePointer<juce::Component> target;
    void place (juce::Component& host, juce::Component& target, int w, int h, Place);
};

/** target 에 마우스를 올리면 tip 을 띄우고 내리거나 누르면 감춘다 */
struct TipOnHover : private juce::MouseListener
{
    TipOnHover (Tip& t, juce::Component& host, juce::Component& target, const juce::String& text, Tip::Place);
    ~TipOnHover() override;
private:
    Tip& tip;
    juce::Component& host;
    juce::Component& target;
    juce::String text;
    Tip::Place place;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
};

/** 팝오버 (폭 340): 겉모양은 SurfaceComponent (흰 면, 모서리 6, 1px #EBEBEB, 그림자 두 겹).
    머리말: 제목 600 15px + 숫자 500 13px #767676 + 닫기.
    구성 요소는 판보다 그림자 폭만큼 사방으로 크다 (그림자가 잘리지 않게). 자리는 setPanelBounds 로 판 기준으로 정한다 */
class Popover : public SurfaceComponent
{
public:
    Popover (const juce::String& title, int width = 340);
    void setCount (const juce::String& c) { count = c; repaint(); }
    void setContent (juce::Component*, int height);
    std::function<void()> onClose;
    int getPreferredHeight() const { return 44 + contentH; }
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    juce::String title, count;
    int width, contentH = 0;
    juce::Component* content = nullptr;
    IconButton closeButton;
};

/** 글자를 그리는 가벼운 라벨 (글꼴·색·정렬·줄 간격). 여러 줄은 단어 단위로 접는다 */
class Text : public juce::Component
{
public:
    Text (const juce::String& text = {}, int weight = 400, float px = 13.0f, juce::Colour ink = col::ink);
    void setText (const juce::String&);
    const juce::String& getText() const { return text; }
    void setStyle (int weight, float px, juce::Colour ink);
    void setColour (juce::Colour c) { ink = c; repaint(); }
    void setJustification (juce::Justification j) { just = j; repaint(); }
    void setLineHeight (float px) { lineH = px; repaint(); }   // 0 이면 한 줄
    void setEllipsis (bool e) { ellipsis = e; repaint(); }
    int getIdealWidth() const;
    int getHeightForWidth (int width) const;
    void paint (juce::Graphics&) override;
    std::function<void()> onClick;
    void mouseUp (const juce::MouseEvent&) override;
    juce::MouseCursor getMouseCursor() override   // onClick 이 있으면 손가락
    {
        return onClick ? juce::MouseCursor (juce::MouseCursor::PointingHandCursor) : juce::Component::getMouseCursor();
    }
private:
    juce::String text;
    int weight;
    float px, lineH = 0.0f;
    juce::Colour ink;
    juce::Justification just { juce::Justification::centredLeft };
    bool ellipsis = true;
};

} // namespace meon::ui
