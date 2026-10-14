// MEON ui - 로그인 뒤의 최상위 화면. 현재 방(store.currentRoom)을 따라 홈과 합주 화면을 바꾸고,
// 토스트(초대·방 닫힘·추방)와 입장 실패 상자를 띄운다 (REMAKE_PLAN 1.1 AppRoot).
#pragma once

#include "screens/HomeScreen.h"
#include "overlays/HomeOverlays.h"
#include "../core/MeonCore.h"

namespace meon::ui
{

class AppRoot : public juce::Component,
                private core::AppStore::Listener
{
public:
    AppRoot (core::MeonCore&, HomeScreen::UpdateHooks);
    ~AppRoot() override;

    /** tab: ui::SettingsModal::Tab 순서 (0 계정, 2 오디오 …) */
    std::function<void (int tab)> onOpenSettings;

    HomeScreen* getHome() { return home.get(); }
    void showJoinFailure (const core::ApiError&);
    void showJoinFailure (core::HomeController::JoinFailure);
    void addInfoToast (const juce::String& title, const juce::String& sub);

    void resized() override;
    void paint (juce::Graphics&) override;
    /** 에디터가 넘기는 단축키 (글자 입력 중이 아닐 때). Esc 는 맨 위 겹침부터 닫는다 */
    bool handleShortcut (const juce::KeyPress&);

private:
    core::MeonCore& core;
    HomeScreen::UpdateHooks update;
    std::unique_ptr<HomeScreen> home;
    std::unique_ptr<juce::Component> jam;
    std::unique_ptr<JoinFailDialog> fail;
    std::unique_ptr<juce::Component> roomClosed;   // 내가 방을 닫은 뒤 덮개
    juce::OwnedArray<Toast> toasts;   // 아래에서 위로 (0 = 맨 아래)
    juce::String shownRoomId;

    void updateScreen();
    void syncInviteToasts();
    void removeToast (Toast*);
    void layoutToasts();

    void stateChanged (core::StateKey) override;
    void notice (const core::Notice&) override;
};

} // namespace meon::ui
