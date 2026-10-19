// MEON ui - 홈 (디자인 L2). 헤더(로고·알림·설정), 배너(연결 끊김·새 버전), 공개 방(필터·코드로 입장·방 만들기,
// 메이슨리 카드, 빈 상태), 내 오디오, 친구(접기), 친구 추가. 모달·알림 팝오버도 홈이 띄운다.
#pragma once

#include "../widgets/Button.h"
#include "../widgets/Inputs.h"
#include "../widgets/Controls.h"
#include "../widgets/Display.h"
#include "../widgets/Meter.h"
#include "../widgets/Masonry.h"
#include "../widgets/FoldAnimator.h"
#include "../theme/Icons.h"
#include "../overlays/HomeOverlays.h"
#include "../overlays/JamOverlays.h"
#include "../../core/MeonCore.h"

namespace meon::ui
{

class HomeScreen : public juce::Component,
                   private core::AppStore::Listener,
                   private juce::Timer
{
public:
    /** 새 버전 (R7 의 UpdateController 전까지 지금 업데이터에 묻는다) */
    struct UpdateHooks
    {
        enum class State { none, available, downloading, installing, failed };
        std::function<State()> state;
        std::function<float()> progress;      // 0~1, 모르면 음수
        std::function<int()> latestBuild;
        std::function<void()> start;
    };

    HomeScreen (core::MeonCore&, UpdateHooks);
    ~HomeScreen() override;

    std::function<void()> onOpenSettings;
    /** 입장 실패 → 입장 실패 상자 (AppRoot 가 띄운다) */
    std::function<void (const core::ApiError&)> onJoinFailure;

    void openJoinCode();
    void openCreateRoom();
    void openFilter();
    void toggleNotifications();
    void openProfile (const core::UserPublic&);
    /** 개발용 스냅숏: 친구 패널이 접히는 중 e 에 멈춘 화면 (--screen=home:fold=friends:0.5) */
    void previewFold (float e);
    void closeOverlays();
    bool hasOverlay() const { return modal != nullptr || popover != nullptr; }

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    static constexpr int sideWidthApp = 320, sideWidthPlugin = 240;

private:
    core::MeonCore& core;
    UpdateHooks update;
    const bool plugin;

    // 헤더
    IconButton bell { (int) icons::Id::bell, IconButton::Style::bordered32, 16.0f };
    IconButton settings { (int) icons::Id::gearHeader, IconButton::Style::bordered32, 16.0f };   // 설정 (툴팁 "설정")
    Tip tip;
    std::unique_ptr<TipOnHover> settingsTip;

    // 배너
    bool updateDismissed = false;
    Button later { Button::Kind::small, TXT ("나중에") }, updateNow { Button::Kind::smallPrimary, TXT ("업데이트") };
    UpdateHooks::State updateState() const;
    bool showUpdate() const;
    int bannersHeight() const;

    // 공개 방
    Card roomsCard;
    Text roomsTitle { TXT ("공개 방"), 600, 16.0f };
    struct Separator;
    std::unique_ptr<Separator> roomsLine;
    struct FilterButton;
    std::unique_ptr<FilterButton> filterButton;
    Button joinCode { Button::Kind::secondary, TXT ("코드로 입장") }, createRoom { Button::Kind::primary, TXT ("방 만들기") };
    struct RoomCard;
    struct RoomList;
    std::unique_ptr<RoomList> roomList;
    juce::Viewport roomViewport;
    Masonry masonry;
    bool roomsShownOnce = false;   // 처음 목록은 애니메이션 없이
    core::RoomFilter filter;

    // 내 오디오
    Card audioCard;
    Text deviceName { {}, 400, 13.0f };
    Button sendMute { Button::Kind::small, TXT ("송출 정지") };
    DbSlider sendGain { -30.0f, 10.0f };
    std::unique_ptr<MeterBox> inputMeter;
    Tag monitorTag { TXT ("내 소리 듣는 중") };   // 홈에서 내 소리가 들릴 때 (설정 › 오디오 "입장 전에도 내 소리 듣기", 버그 28)

    // 친구
    Card friendsCard;
    struct FriendList;
    std::unique_ptr<FriendList> friendList;
    juce::Viewport friendViewport;
    bool friendsOpen = true;
    // 접기·펼치기 (패널 하나뿐인 세로 묶음: 접힌 48 과 펼친 크기 사이를 e 로). 움직이는 동안 목록은 펼친 크기로 한 번만 배치
    FoldAnimator fold { *this, [this] { resized(); } };
    const int foldFriends = fold.add ("home-friends", true);
    juce::Point<int> laidFriends;

    // 친구 추가
    Card addCard;
    TextInput addInput { TXT ("친구 아이디 입력") };
    Button addSend { Button::Kind::link, TXT ("요청") };
    juce::String addMessage;
    Text addNote { {}, 400, 12.0f, col::inkSub };   // addMessage 를 입력 줄 아래에 (디자인 400 12px #767676)
    std::shared_ptr<bool> alive { std::make_shared<bool> (true) };
    void sendFriendRequest();

    // 겹침
    std::unique_ptr<Modal> modal;
    std::unique_ptr<Popover> popover;
    std::unique_ptr<NotificationList> notifications;
    void showModal (std::unique_ptr<Modal>);

    void rebuildRooms();
    void layoutRooms();
    void rebuildFriends();
    void refreshHeader();
    void refreshAudio();
    void enterRoom (const core::Room&);
    juce::String deviceText() const;

    void stateChanged (core::StateKey) override;
    void timerCallback() override;
};

} // namespace meon::ui
