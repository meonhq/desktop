// MEON ui - 로그인 뒤의 최상위 화면. 현재 방(store.currentRoom)을 따라 홈과 합주 화면을 바꾸고,
// 토스트(초대·방 닫힘·추방)와 입장 실패 상자를 띄운다 (REMAKE_PLAN 1.1 AppRoot).
#pragma once

#include "screens/HomeScreen.h"
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
    /** 방 안이면 합주 화면 (개발용 미리 보기) */
    JamScreen* getJamScreen();
    void showJoinFailure (const core::ApiError&);
    void showJoinFailure (core::HomeController::JoinFailure);
    void addInfoToast (const juce::String& title, const juce::String& sub, Toast::Kind kind = Toast::Kind::normal);
    /** 개발용 미리 보기 (--screen=home:roomlost): 서버 알림이 온 것처럼 토스트를 띄운다 */
    void previewNotice (const core::Notice& n) { notice (n); }

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
    /** 연결 끊김 토스트: 경고 "연결이 끊겼어요" → 같은 자리에서 일반 토스트 "다시 연결됐어요" → 3초 뒤 닫힘 */
    void connectionToastChanged (core::ConnectionWatch::Toast) override;
    juce::Component::SafePointer<Toast> connectionToast;
};

} // namespace meon::ui
