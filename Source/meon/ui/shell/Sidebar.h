// MEON ui - 왼쪽 사이드바 (홈·사이드바 디자인 v2.1). 메뉴(공개 방·코드로 입장. 홈은 꺼 둠), 참여 중, 친구(온라인·합주 중,
// 오프라인은 처음에 접힘), 맨 아래 내 프로필 줄. 접으면 아이콘만 (240 → 56).
// 데이터는 모두 AppStore (친구·상태·참여 중인 방·친구 요청·입장 요청). 누르면 AppRoot 가 화면을 바꾼다.
// 접기 버튼은 창 기준 자리라(맥 창 모드에서는 접혀도 사이드바 밖) AppRoot 가 따로 그린다 (toggleArea).
#pragma once

#include "../widgets/Display.h"
#include "../widgets/Button.h"
#include "../theme/Icons.h"
#include "../widgets/FoldAnimator.h"
#include "../../core/MeonCore.h"

namespace meon::ui
{

class Sidebar : public juce::Component,
                private core::AppStore::Listener,
                private juce::Timer
{
public:
    enum class Page { none, home, rooms, room };
    /** 창 윗부분 배치 (디자인 chromeVals, 로고 위치 A안).
        macWindow: 맥 독립 앱 창 모드 (신호등 있음). 접기 버튼은 신호등 오른쪽: 확대 버튼 오른쪽 끝 + 12 (setMacToggleX.
                   디자인은 84 = 원 끝 72 + 12. 신호등은 macOS 기본 간격이라 버전마다 끝이 다르다: macOS 26 은 79 → 91, 사용자 결정 2026-10-31).
                   로고는 그 아래 줄 (18, 56), 로고 줄 높이 펼침 32 / 접힘 0.
                   접히면 맨 윗줄이 본문 제목 줄과 이어진다 (오른쪽 선은 52 부터, 제목 줄 왼쪽 여백 = 접기 버튼 x - 12. 디자인 72).
        flat     : 신호등이 없을 때 (맥 전체 화면·윈도우·리눅스 독립 앱) = 디자인 fullscreen 켜짐 (v2.1 최신).
                   펼침: 로고 (18, 14), 접기 버튼 오른쪽 끝 x 196, 맨 윗줄 42 (로고와 메뉴 사이 16, 창 모드와 같은 간격).
                   접힘: 접기 버튼 x 12, 맨 윗줄 52. 위치·높이는 접기 애니메이션과 같이 움직인다 (180ms).
        로고 위치는 늘 A안 (디자인에서 logoPos B안이 빠졌다).
        legacy   : 플러그인 (이번에 바꾸지 않음): 로고 (18, 14), 접기 버튼은 펼치면 오른쪽 끝, 접으면 12 */
    enum class Chrome { legacy, macWindow, flat };

    explicit Sidebar (core::MeonCore&);
    ~Sidebar() override;

    static constexpr int openW = 240, closedW = 56;
    /** 디자인 homeMenu (기본 숨김): 메뉴의 "홈". 홈 화면 코드는 그대로 두고 메뉴에서만 뺀다 */
    static constexpr bool homeMenu = false;
    /** 지금 폭: 접기·펼치기 중이면 56 ~ 240 사이 (FoldAnimator, 180ms). 프레임마다 onLayoutChanged 로 알린다 */
    int currentWidth() const;
    bool isCollapsed() const { return shut(); }
    void setCollapsed (bool);   // 사용자가 접기 버튼으로 정한 상태
    void toggleCollapsed() { setCollapsed (! collapsed); }
    /** 창이 좁아 방 화면이 모자랄 때 잠시 접는다 (사용자가 정한 상태는 그대로, 넓어지면 돌아간다). 그동안 접기 버튼은 숨긴다 */
    void setForcedCollapsed (bool);
    void setPage (Page);
    Page getPage() const { return page; }
    void setChrome (Chrome);
    Chrome getChrome() const { return chrome; }
    /** 맥 창 모드의 접기 버튼 x (확대 버튼 오른쪽 끝 + 12). 접힌 제목 줄 왼쪽 여백도 이것을 따른다 (x - 12) */
    void setMacToggleX (int x);
    static constexpr int designMacToggleX = 84;
    /** 펼침 진행값 (접힘 0 ~ 펼침 1, 180ms cubic-bezier(0.2, 0, 0, 1)). 폭·글자와 로고 불투명도·로고 줄 높이·제목 줄 왼쪽 여백이 이 값으로 같이 움직인다 */
    float openProgress() const { return fold.value (foldPanel); }
    /** 접기 버튼 자리 (사이드바 = 창 왼쪽 위 기준). 잠시 접힌 동안은 빈 사각형 */
    juce::Rectangle<int> toggleArea() const;
    /** 본문(공개 방·방 화면) 제목 줄의 왼쪽 여백: 맥 창 모드에서 접히면 접기 버튼 x - 12 (디자인 72: 신호등·접기 버튼 자리), 아니면 18. 접는 동안 같이 움직인다 */
    int headerInset() const;
    /** 프로필 메뉴가 열려 있으면 프로필 줄을 선택된 색으로 */
    void setProfileMenuOpen (bool);
    /** 개발용 (화면 자가 테스트): 글자가 text 인 줄(메뉴·참여 중 방·친구·접힌 상태의 방 만들기·친구 추가)의 사이드바 안 위치. 없으면 빈 사각형 */
    juce::Rectangle<int> devRow (const juce::String& text) const;
    juce::Rectangle<int> devProfileRow() const { return profileBounds(); }
    /** 개발용: 친구 줄의 "초대" 버튼 (없으면 nullptr) */
    Button* devInviteButton (const juce::String& userId) const;
    /** 개발용: 섹션("rooms"·"friends"·"offline") 내용이 보이는 높이와 다 펼친 높이, 화살표 각도(라디안) */
    struct SectionInfo { int visible = 0, full = 0; float angle = 0.0f; };
    SectionInfo devSection (const juce::String& name) const;
    /** 개발용: 섹션 머리말("참여 중"·"친구"·"오프라인")의 사이드바 안 위치 (펼쳤을 때만) */
    juce::Rectangle<int> devSectionHeader (const juce::String& text) const;
    /** 개발용 스냅숏: 접히는(open=false)·펼쳐지는 중 e 에 멈춘다 (--screen=home:sidebar=0.5) */
    void previewFold (bool open, float e);

    std::function<void()> onLayoutChanged;   // 접기·펼치기 (폭·제목 줄 여백이 바뀐다)
    std::function<void()> onHome, onRooms, onJoinCode, onShowRoom, onCreateRoom, onAddFriend, onProfileMenu;
    std::function<void (const core::UserPublic&)> onOpenFriend;
    /** 받은 초대 카드 (친구 목록 위): 수락·거절 */
    std::function<void (const juce::String& inviteId)> onAcceptInvite, onDeclineInvite;
    /** 맨 윗줄(52)을 눌렀다 (맥 창 모드: 창 끌기·더블클릭) */
    std::function<void()> onTitleBarMouseDown;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    static constexpr int headerH = 52;

private:
    friend class UiSelfTest;   // 개발용 화면 자가 테스트 (--uitest)
    core::MeonCore& core;
    bool collapsed = false, forced = false, profileMenuOpen = false;
    bool shut() const { return collapsed || forced; }
    Chrome chrome = Chrome::flat;
    int macToggleX = designMacToggleX;
    // 접기 애니메이션 (디자인: 180ms cubic-bezier(0.2, 0, 0, 1)). 화면 쪽 타이머(VBlank)만 쓴다: 오디오 처리와 무관.
    // 사이드바 폭과 참여 중·친구·오프라인 섹션 높이가 각각 움직인다
    bool laying = false;   // setForcedCollapsed 를 AppRoot::resized 가 부르는 중 (그 안에서 다시 배치하지 않는다)
    float notifiedE = -1.0f;
    void foldFrame();
    FoldAnimator fold { *this, [this] { foldFrame(); } };
    const int foldPanel = fold.add ("sidebar", true);
    const int foldRooms = fold.add ("sidebarRooms", true);
    const int foldFriends = fold.add ("sidebarFriends", true);
    const int foldOffline = fold.add ("sidebarOffline", false);
    /** 섹션 목표: 참여 중 = 접힌 사이드바이거나 펼친 섹션, 친구·오프라인 = 펼친 사이드바이면서 펼친 섹션 (디자인 fold) */
    void syncSections();
    /** 메뉴·방·친구·내 프로필 글자의 불투명도 (펼침 1, 접힘 0) */
    float labelAlpha() const { return fold.value (foldPanel); }
    bool animating() const { return fold.isMoving (foldPanel); }
    int logoRowH() const;
    /** 맨 윗줄 높이 (디자인 topRowH): 신호등 없음 + 펼침이면 42, 그 밖은 52. 접는 동안 같이 움직인다 */
    int topRowH() const;
    Page page = homeMenu ? Page::home : Page::rooms;

    struct Body;
    std::unique_ptr<Body> body;
    juce::Viewport viewport;
    int hot = 0;   // 2 = 프로필 줄

    juce::Rectangle<int> profileBounds() const;
    void stateChanged (core::StateKey) override;
    void timerCallback() override;   // 초대 버튼 "N초 뒤 초대가 가요" → "초대됨"
};

/** 접기 버튼 (사이드바 머리말의 아이콘 32). 창 기준 자리라 AppRoot 가 사이드바·본문 위에 둔다.
    모달 덮개 아래에 있어야 할 때(covered)는 덮개 색을 덧칠하고 누름을 아래로 넘긴다 */
class SidebarToggle : public juce::Component, public juce::SettableTooltipClient
{
public:
    SidebarToggle();
    std::function<void()> onPress;
    void setOpen (bool open);
    void setCovered (bool);
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    bool hot = false, covered = false;
};

/** 프로필 줄 위에 뜨는 메뉴 (알림·설정). 창 전체를 덮는 투명 판: 바깥을 누르면 닫힌다 */
class ProfileMenu : public juce::Component
{
public:
    explicit ProfileMenu (int notificationCount);
    std::function<void()> onNotifications, onSettings, onDismiss;
    static constexpr int boxW = 222, boxH = 6 + 32 + 2 + 32 + 6;
    juce::Rectangle<int> box;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    int count = 0, hot = -1;
    juce::Rectangle<int> item (int i) const { return { box.getX() + 6, box.getY() + 6 + i * 34, box.getWidth() - 12, 32 }; }
};

} // namespace meon::ui
