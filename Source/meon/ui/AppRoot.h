// MEON ui - 로그인 뒤의 최상위 화면. 현재 방(store.currentRoom)을 따라 홈과 합주 화면을 바꾸고,
// 토스트(초대·방 닫힘·추방)와 입장 실패 상자를 띄운다 (REMAKE_PLAN 1.1 AppRoot).
#pragma once

#include "screens/HomeScreen.h"
#include "shell/Sidebar.h"
#include "overlays/HomeOverlays.h"
#include "../core/MeonCore.h"

namespace meon::ui
{

class JamScreen;

class AppRoot : public juce::Component,
                private core::AppStore::Listener,
                private core::ConnectionWatch::Listener
{
public:
    AppRoot (core::MeonCore&, HomeScreen::UpdateHooks);
    ~AppRoot() override;

    /** tab: ui::SettingsModal::Tab 순서 (0 계정, 2 오디오 …) */
    std::function<void (int tab)> onOpenSettings;

    HomeScreen* getHome() { return home.get(); }
    Sidebar* getSidebar() { return sidebar.get(); }   // 개발용 미리 보기
    /** 방 안이면 합주 화면 (개발용 미리 보기) */
    JamScreen* getJamScreen();
    /** 방 최소화 (합주 최종 디자인 5): 방에는 그대로 있고 화면만 홈 + 아래 막대로. 합주 화면은 지우지 않고 숨긴다 */
    void setMinimized (bool);
    bool isMinimized() const { return minimized; }
    void showJoinFailure (const core::ApiError&);
    void showJoinFailure (core::HomeController::JoinFailure);
    void addInfoToast (const juce::String& title, const juce::String& sub, Toast::Kind kind = Toast::Kind::normal);
    /** 개발용 미리 보기 (--screen=home:roomlost): 서버 알림이 온 것처럼 토스트를 띄운다 */
    void previewNotice (const core::Notice& n) { notice (n); }

    void resized() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;   // 개발용 미리 보기의 가짜 신호등
    void parentHierarchyChanged() override;
    /** 개발용 (--chrome=mac·flat·plugin): 창 윗부분 배치를 정한다. mac 은 신호등 자리에 가짜 신호등을 그린다 */
    void devForceChrome (Sidebar::Chrome c) { forcedChrome = c; hasForcedChrome = true; resized(); repaint(); }
    void devResetChrome() { hasForcedChrome = false; resized(); repaint(); }
    Sidebar::Chrome chromeMode() const;
    /** 에디터가 넘기는 단축키 (글자 입력 중이 아닐 때). Esc 는 맨 위 겹침부터 닫는다 */
    bool handleShortcut (const juce::KeyPress&);

private:
    friend class UiSelfTest;   // 개발용 화면 자가 테스트 (--uitest)
    core::MeonCore& core;
    HomeScreen::UpdateHooks update;
    // 왼쪽 사이드바 (홈·사이드바 디자인). 홈 화면은 창 전체를 덮되 사이드바 자리는 비워 둔다 (모달이 창 전체를 덮게).
    // 합주 화면은 사이드바 오른쪽에만 둔다 (방 화면 안은 그대로)
    std::unique_ptr<Sidebar> sidebar;
    std::unique_ptr<ProfileMenu> profileMenu;
    // 사이드바 접기 버튼: 창 기준 자리 (맥 창 모드에서는 접혀도 신호등 오른쪽 84). 사이드바·본문 위, 토스트·겹침 아래
    std::unique_ptr<SidebarToggle> toggle;
    void syncToggle();
    // 맥 독립 앱: 제목 줄 없이 내용이 창 맨 위까지 (MeonWindowChrome). 로그인 뒤 앱 화면에서만 (로그아웃하면 되돌린다)
    bool chromeTried = false, chromeInstalled = false;
    bool hasForcedChrome = false;
    Sidebar::Chrome forcedChrome = Sidebar::Chrome::flat;
    void ensureWindowChrome();
    void titleBarMouseDown();
    Sidebar::Page page = Sidebar::homeMenu ? Sidebar::Page::home : Sidebar::Page::rooms;   // 방 밖(또는 최소화)에서 보는 쪽: 공개 방 (홈은 꺼 둠)
    void syncSidebar();
    void showPage (Sidebar::Page);
    void openProfileMenu();
    void closeProfileMenu();
    std::unique_ptr<HomeScreen> home;
    std::unique_ptr<juce::Component> jam;
    std::unique_ptr<JoinFailDialog> fail;
    std::unique_ptr<juce::Component> roomClosed;   // 내가 방을 닫은 뒤 덮개
    juce::OwnedArray<Toast> toasts;   // 아래에서 위로 (0 = 맨 아래)
    juce::String shownRoomId;
    juce::StringArray seenJoinRequests;   // 알림을 한 번 띄운 입장 요청
    juce::StringArray seenInvites;        // 알림을 한 번 띄운 초대 (10초 뒤 닫히면 다시 띄우지 않는다. 사이드바 카드는 남는다)
    bool minimized = false;   // 방 안이면서 홈을 보는 중 (방에 있는지는 store.currentRoom)

    void updateScreen();
    void applyScreen();   // 방 안: minimized 에 따라 홈(+막대)과 합주 화면 중 하나를 보인다
    void syncInviteToasts();
    /** 방장일 때 받은 입장 요청 (홈·사이드바 디자인): 오른쪽 아래 알림이 여러 개 쌓인다. 거절·승인, 10초 뒤 알림만 닫힘 */
    void syncJoinRequestToasts();
    void acceptInvite (const juce::String& inviteId);
    void removeToast (Toast*);
    void layoutToasts();

    void stateChanged (core::StateKey) override;
    void notice (const core::Notice&) override;
    /** 연결 끊김 토스트: 경고 "연결이 끊겼어요" → 같은 자리에서 일반 토스트 "다시 연결됐어요" → 3초 뒤 닫힘 */
    void connectionToastChanged (core::ConnectionWatch::Toast) override;
    juce::Component::SafePointer<Toast> connectionToast;
};

} // namespace meon::ui
