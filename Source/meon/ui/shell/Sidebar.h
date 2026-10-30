// MEON ui - 왼쪽 사이드바 (홈·사이드바 디자인). 메뉴(홈·공개 방·코드로 입장), 참여 중, 친구(온라인·합주 중,
// 오프라인은 처음에 접힘), 맨 아래 내 프로필 줄. 접으면 아이콘만 (240 → 56).
// 데이터는 모두 AppStore (친구·상태·참여 중인 방·친구 요청·입장 요청). 누르면 AppRoot 가 화면을 바꾼다.
#pragma once

#include "../widgets/Display.h"
#include "../widgets/Button.h"
#include "../theme/Icons.h"
#include "../widgets/FoldAnimator.h"
#include "../../core/MeonCore.h"

namespace meon::ui
{

class Sidebar : public juce::Component,
                private core::AppStore::Listener
{
public:
    enum class Page { none, home, rooms, room };

    explicit Sidebar (core::MeonCore&);
    ~Sidebar() override;

    static constexpr int openW = 240, closedW = 56;
    /** 지금 폭: 접기·펼치기 중이면 56 ~ 240 사이 (FoldAnimator, 180ms). 프레임마다 onLayoutChanged 로 알린다 */
    int currentWidth() const;
    bool isCollapsed() const { return shut(); }
    void setCollapsed (bool);   // 사용자가 접기 버튼으로 정한 상태
    /** 창이 좁아 방 화면이 모자랄 때 잠시 접는다 (사용자가 정한 상태는 그대로, 넓어지면 돌아간다). 그동안 접기 버튼은 숨긴다 */
    void setForcedCollapsed (bool);
    void setPage (Page);
    Page getPage() const { return page; }
    /** 프로필 메뉴가 열려 있으면 프로필 줄을 선택된 색으로 */
    void setProfileMenuOpen (bool);
    /** 개발용 (화면 자가 테스트): 글자가 text 인 줄(메뉴·참여 중 방·친구·접힌 상태의 방 만들기·친구 추가)의 사이드바 안 위치. 없으면 빈 사각형 */
    juce::Rectangle<int> devRow (const juce::String& text) const;
    juce::Rectangle<int> devProfileRow() const { return profileBounds(); }
    /** 개발용 스냅숏: 접히는(open=false)·펼쳐지는 중 e 에 멈춘다 (--screen=home:sidebar=0.5) */
    void previewFold (bool open, float e);

    std::function<void()> onLayoutChanged;   // 접기·펼치기 (폭이 바뀐다)
    std::function<void()> onHome, onRooms, onJoinCode, onShowRoom, onCreateRoom, onAddFriend, onProfileMenu;
    std::function<void (const core::UserPublic&)> onOpenFriend;
    /** 받은 초대 카드 (친구 목록 위): 수락·거절 */
    std::function<void (const juce::String& inviteId)> onAcceptInvite, onDeclineInvite;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    core::MeonCore& core;
    bool collapsed = false, forced = false, profileMenuOpen = false;
    bool shut() const { return collapsed || forced; }
    // 폭 애니메이션 (디자인: width 180ms cubic-bezier(0.2, 0, 0, 1), 글자는 같은 시간 동안 opacity).
    // 화면 쪽 타이머(VBlank)만 쓴다: 오디오 처리와 무관
    bool laying = false;   // setForcedCollapsed 를 AppRoot::resized 가 부르는 중 (그 안에서 다시 배치하지 않는다)
    FoldAnimator fold { *this, [this] { if (! laying && onLayoutChanged) onLayoutChanged(); resized(); repaint(); } };
    const int foldPanel = fold.add ("sidebar", true);
    /** 메뉴·방·친구·내 프로필 글자의 불투명도 (펼침 1, 접힘 0) */
    float labelAlpha() const { return fold.value (foldPanel); }
    bool animating() const { return fold.isMoving (foldPanel); }
    Page page = Page::home;

    struct Body;
    std::unique_ptr<Body> body;
    juce::Viewport viewport;
    int hot = 0;   // 1 = 접기 버튼, 2 = 프로필 줄

    juce::Rectangle<int> toggleBounds() const;
    juce::Rectangle<int> profileBounds() const;
    void stateChanged (core::StateKey) override;
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
