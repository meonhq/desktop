// MEON ui - 표시용: 핑 배지, 아바타(파트 아이콘 원 + 상태 점), 작은 태그, 카드 틀, 모달·팝오버 틀, 덮개.
#pragma once

#include "../theme/Tokens.h"
#include "Button.h"

namespace meon::ui
{

/** 핑 배지 (폭 48, 높이 19). 18ms 를 넘으면 노란 면 + 진한 글자. text 가 "나" 같은 글자면 그대로 */
class PingBadge : public juce::Component
{
public:
    PingBadge();
    void setPing (std::optional<int> ms);   // 없으면 "—"
    void setText (const juce::String& t) { text = t; warn = false; repaint(); }
    void paint (juce::Graphics&) override;
private:
    juce::String text { "—" };
    bool warn = false;
};

/** 파트 아이콘 원. size 41(멤버 행)·32(친구 목록)·…, 테두리 #E0E0E0.
    상태 점(9px, 흰 테두리 2px): 온라인 #FFD700, 방에 있음 #1A1A1A, 오프라인 #D5D5D5 */
class Avatar : public juce::Component
{
public:
    enum class Status { none, online, inRoom, offline };
    Avatar (float size, float iconPx);
    void setPart (const juce::String& partKey) { part = partKey; repaint(); }
    void setDimmed (bool d) { dimmed = d; repaint(); }   // 아이콘 투명도 0.4
    void setStatus (Status s) { status = s; repaint(); }
    void setClickable (bool c) { setMouseCursor (c ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor); }
    std::function<void()> onClick;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    juce::MouseCursor getMouseCursor() override   // onClick 이 있으면 손가락
    {
        return onClick ? juce::MouseCursor (juce::MouseCursor::PointingHandCursor) : juce::Component::getMouseCursor();
    }

    static juce::Colour statusColour (Status);

private:
    float size, iconPx;
    juce::String part { "other" };
    bool dimmed = false;
    Status status = Status::none;
};

/** "비공개" 같은 작은 태그 (500 11px #767676, 여백 2·4, 테두리 #E0E0E0, 모서리 3) */
class Tag : public juce::Component
{
public:
    explicit Tag (const juce::String& text);
    int getIdealWidth() const;
    void paint (juce::Graphics&) override;
private:
    juce::String text;
};

/** 카드 (테두리 #E0E0E0, 모서리 6, 흰 면). 머리말이 있으면 12·18 여백 + 제목 600 15px + 접기 버튼, 아래 구분선 */
class Card : public juce::Component
{
public:
    Card();
    void setTitle (const juce::String&, bool collapsible = false, bool sidewaysArrow = false);
    void setOpen (bool);
    bool isOpen() const { return open; }
    int headerHeight() const { return title.isEmpty() ? 0 : 48; }
    /** 머리말 아래 내용 영역 (구분선 아래) */
    juce::Rectangle<int> contentBounds() const;
    std::function<void (bool open)> onToggle;
    /** 머리말 오른쪽 (접기 버튼 왼쪽)에 붙는 구성 요소 */
    void setHeaderAccessory (juce::Component*);

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

private:
    juce::String title;
    bool open = true, collapsible = false;
    std::unique_ptr<CollapseButton> collapse;
    juce::Component* accessory = nullptr;
};

/** 모달: 덮개(rgba 26,26,26,0.4) + 가운데 흰 상자(모서리 6). 머리말 12·18, 제목 600 15/24px, 닫기 24px.
    덮개를 누르거나 Esc 를 누르면 onClose. 본문은 content 로 넣는다 (머리말 아래 구분선부터) */
class Modal : public juce::Component
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
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    juce::String title;
    int width, contentH = 100;
    bool showHeader = true, showClose = true;
    juce::Component* content = nullptr;
    juce::Component* accessory = nullptr;
    IconButton closeButton;
    juce::BorderSize<int> insets;
};

/** 팝오버 (테두리 #D5D5D5, 모서리 6, 흰 면, 폭 340). 머리말: 제목 600 15px + 숫자 500 13px #767676 + 닫기 */
class Popover : public juce::Component
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
