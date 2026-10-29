// MEON ui - 왼쪽 사이드바 (홈·사이드바 디자인). 메뉴(홈·공개 방·코드로 입장), 참여 중, 친구(온라인·합주 중,
// 오프라인은 처음에 접힘), 맨 아래 내 프로필 줄. 접으면 아이콘만 (240 → 56).
// 데이터는 모두 AppStore (친구·상태·참여 중인 방·친구 요청·입장 요청). 누르면 AppRoot 가 화면을 바꾼다.
#pragma once

#include "../widgets/Display.h"
#include "../theme/Icons.h"
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
    int currentWidth() const { return collapsed ? closedW : openW; }
    bool isCollapsed() const { return collapsed; }
    void setCollapsed (bool);
    void setPage (Page);
    Page getPage() const { return page; }
    /** 프로필 메뉴가 열려 있으면 프로필 줄을 선택된 색으로 */
    void setProfileMenuOpen (bool);

    std::function<void()> onLayoutChanged;   // 접기·펼치기 (폭이 바뀐다)
    std::function<void()> onHome, onRooms, onJoinCode, onShowRoom, onCreateRoom, onAddFriend, onProfileMenu;
    std::function<void (const core::UserPublic&)> onOpenFriend;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    core::MeonCore& core;
    bool collapsed = false, profileMenuOpen = false;
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
