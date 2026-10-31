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
        std::function<bool()> needsMove;      // macOS: 앱 자리에 쓸 수 없어 받지 못함 (응용 프로그램 폴더로 옮겨야 한다)
        std::function<void()> start;
    };

    HomeScreen (core::MeonCore&, UpdateHooks);
    ~HomeScreen() override;

    std::function<void()> onOpenSettings;
    /** 입장 실패 → 입장 실패 상자 (AppRoot 가 띄운다) */
    std::function<void (const core::ApiError&)> onJoinFailure;

    void openJoinCode();
    void openCreateRoom();
    /** 방 최소화 (합주 최종 디자인 5): 방에 그대로 있으면서 홈을 볼 때 아래에 붙는 막대 (높이 57).
        막대를 누르면 onReturnToRoom. 다른 방에 들어가거나 새 방을 만들려면 먼저 확인 모달 */
    void setRoomBar (bool show);
    bool hasRoomBar() const { return roomBar != nullptr; }
    std::function<void()> onReturnToRoom;
    static constexpr int roomBarH = 57;   // 위 테두리 1 + 12 + 32 + 12
    /** 개발용 스냅숏 (--screen=jam:mini…): arm — 막대의 나가기를 한 번 누른 상태, confirm / create — 확인 모달 */
    void devPreviewRoomBar (const juce::String& what);
    /** 개발용 (화면 자가 테스트): 막대, 막대의 나가기 버튼, 막대의 새 채팅 수, 공개 방 카드의 입장 버튼 */
    juce::Component* devRoomBar() const;
    Button* devRoomBarLeave() const;
    int devRoomBarUnread() const;
    std::vector<std::pair<core::Room, juce::Button*>> devRoomCards() const;
    void openFilter();
    void toggleNotifications();
    void openProfile (const core::UserPublic&);
    /** 개발용 스냅숏: 친구 패널이 접히는 중 e 에 멈춘 화면 (--screen=home:fold=friends:0.5) */
    void previewFold (float e);
    void closeOverlays();
    /** Esc: 맨 위 겹침 하나만 (최소화 중 확인 모달 → 그 아래 모달·팝오버) */
    bool closeTopOverlay();
    bool hasOverlay() const { return modal != nullptr || popover != nullptr || confirm != nullptr; }
    /** 창 전체를 어둡게 덮는 겹침 (모달·확인 창). 팝오버는 덮지 않는다 */
    bool dimsWindow() const { return modal != nullptr || confirm != nullptr; }
    /** 겹침이 생기거나 사라졌다 (AppRoot 가 접기 버튼을 덮개 아래로) */
    std::function<void()> onOverlayChanged;
    /** 맨 윗줄(제목 줄)의 빈 곳을 눌렀다 (맥 창 모드: 창 끌기·더블클릭) */
    std::function<void()> onTitleBarMouseDown;
    /** 제목 줄의 빈 곳인지 (mouseDown 의 맥 창 끌기와 같은 조건. 윈도우는 이 자리를 운영체제 제목 줄로 판정한다) */
    bool titleBarDragAt (const juce::Component* hit, juce::Point<int> p) const
    {
        return hit == this && shell && ! hasOverlay() && p.y < headerLine() && p.x >= shellInset;
    }
    void mouseDown (const juce::MouseEvent&) override;
    void childrenChanged() override { if (onOverlayChanged) onOverlayChanged(); }

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool hitTest (int x, int y) override;

    /** 사이드바 안에 들어간 홈 (홈·사이드바 디자인). 창 전체를 덮되 왼쪽 leftInset(사이드바 폭)은 그리지도 받지도 않는다
        (모달·확인 창은 창 전체를 덮는다). 머리말은 52 높이의 쪽 제목, 로고·알림·설정과 오른쪽 친구 패널은 사이드바로 옮겨 숨긴다.
        titleInset: 쪽 제목 왼쪽 여백 (디자인 hdPL: 보통 18, 맥 창 모드에서 사이드바를 접으면 72) */
    void setShell (int leftInset, const juce::String& title, bool homePage = true, int titleInset = metric::padX, int titleRight = metric::padX);
    /** 사이드바 안 홈 쪽 (HomePage) 이 쓰는 연결 */
    std::function<void (int tab)> onOpenSettingsTab;
    std::function<void()> onShowRooms;
    /** 알림 목록의 초대 "입장" (AppRoot::acceptInvite: 실패하면 토스트·입장 실패 상자) */
    std::function<void (const juce::String& inviteId)> onAcceptInvite;
    bool isShell() const { return shell; }
    /** 친구 추가 (사이드바의 +) */
    void openAddFriend();

    static constexpr int sideWidthApp = 320, sideWidthPlugin = 240;

private:
    friend class UiSelfTest;   // 개발용 화면 자가 테스트 (--uitest)
    core::MeonCore& core;
    UpdateHooks update;
    const bool plugin;
    bool shell = false, shellHome = true;
    int shellInset = 0, titleInset = metric::padX, titleRightInset = metric::padX;   // 제목 줄 오른쪽 여백: 윈도우 독립 앱 150 (창 버튼 + 12)
    std::unique_ptr<juce::Component> homePage;   // 사이드바 안 "홈" (HomePage)
    juce::String pageTitle;
    int headerLine() const;   // 헤더 아래 구분선 y (사이드바 안이면 51)

    // 헤더
    IconButton bell { (int) icons::Id::bell, IconButton::Style::bordered32, 16.0f };
    IconButton settings { (int) icons::Id::gearHeader, IconButton::Style::bordered32, 16.0f };   // 설정 (툴팁 "설정")
    Tip tip;
    std::unique_ptr<TipOnHover> settingsTip;

    // 배너
    bool updateDismissed = false;
    Button later { Button::Kind::small, TXT ("나중에") }, updateNow { Button::Kind::smallPrimary, TXT ("업데이트") },
           directDownload { Button::Kind::small, TXT ("직접 내려받기") };   // 받기 실패: meon.co.kr/download
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
    // 공개 방 쪽 머리말의 방 만들기 (홈·사이드바 디자인 v2.1 최신): + 아이콘만, 툴팁 "방 만들기". 방에 있으면 숨긴다 (사이드바 방 만들기와 같은 규칙)
    IconButton createIcon { (int) icons::Id::plusHeader, IconButton::Style::ghost32, 18.0f };
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

    // 방 최소화
    struct RoomBar;
    std::unique_ptr<RoomBar> roomBar;
    std::unique_ptr<Modal> confirm;   // 최소화 중 나가기 확인 (프로필·코드로 입장 모달 위에 뜬다)
    /** 방에 없으면 바로 proceed. roomId 가 지금 방이면 합주 화면으로 돌아간다. 아니면 확인 모달 → 나간 뒤 proceed */
    void confirmLeaveThen (bool create, const juce::String& roomId, std::function<void()> proceed);
    LeaveGuard leaveGuard();
    void joinRoomNow (const core::Room&);
    void openCreateRoomNow();

    void rebuildRooms();
    void layoutRooms();
    void rebuildFriends();
    void refreshHeader();
    void refreshAudio();
    void enterRoom (const core::Room&);
    void joinFriend (const core::Friend&);
    juce::String deviceText() const;

    void stateChanged (core::StateKey) override;
    void timerCallback() override;
};

} // namespace meon::ui
