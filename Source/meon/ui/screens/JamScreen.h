// MEON ui - 합주 화면 (디자인 L1). 헤더(로고 · 방 이름 · 비공개 · 입장 코드+복사 · 설정 · 방 나가기),
// 서버 연결 끊김 배너(Q16), 멤버 행(상태·음소거·볼륨·패닝·초기화·핑·미터), 내 오디오(송출)·모니터,
// 채팅(전달 확인·실패 표시·남은 글자), 친구(초대 3초), 파일 플레이어.
// 방장 전용(방 설정·입장 요청·관리)은 R7 에서 붙인다.
#pragma once

#include <map>

#include "../widgets/Button.h"
#include "../widgets/Inputs.h"
#include "../widgets/Controls.h"
#include "../widgets/Display.h"
#include "../widgets/Meter.h"
#include "../widgets/FoldAnimator.h"
#include "../theme/Icons.h"
#include "../../core/MeonCore.h"
#include "../overlays/JamOverlays.h"
#include "../overlays/HomeOverlays.h"

namespace meon::ui
{

class JamScreen : public juce::Component,
                  private core::AppStore::Listener,
                  private core::JamSession::Listener,
                  private core::ChatChannel::Listener,
                  private juce::Timer
{
public:
    explicit JamScreen (core::MeonCore&);
    ~JamScreen() override;

    std::function<void()> onOpenSettings;
    /** 로비 보기 (방 최소화): 방에 그대로 있으면서 홈을 보여 준다 (AppRoot) */
    std::function<void()> onMinimize;
    /** 프로필 창에서 다른 방에 들어가거나 요청하다 실패 (입장 실패 상자, AppRoot) */
    std::function<void (const core::ApiError&)> onJoinFailure;

    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;
    /** 단축키 (설정 › 단축키, REMAKE_PLAN 4.4): M 송출, N 모니터, ⌘↑/⌘↓ 마스터 ±1 dB, Enter 채팅 입력, Esc 겹침 닫기 */
    bool handleShortcut (const juce::KeyPress&);
    void closeOverlays();
    void resized() override;

    static constexpr int rowH = 59;
    /** 오른쪽 패널 너비 (합주 최종 디자인 6). 지금 창에 맞춘 값 */
    int sidePanelWidth() const;
    /** w 로 바꾼다 (범위에 맞춘다). persist 면 설정에 저장 (앱·플러그인 따로) */
    void setSideWidth (int w, bool persist);
    /** 개발용 (화면 자가 테스트): 너비 조절 자리와 그 선 색 */
    juce::Component* devSideResizer() const;
    juce::Colour devSideLineColour() const;
    /** 개발용: 채팅 목록 마지막 메시지의 줄 수 (여러 줄 표시 확인) */
    int devLastChatLines() const;
    /** 개발용 스냅숏: 채팅 입력 칸에 글을 넣고 커서를 끝에 */
    void devSetChatInput (const juce::String&);
    /** 개발용 스냅숏: 방 나가기를 한 번 누른 상태 */
    void devArmLeave() { leave.triggerClick(); }
    /** 개발용 스냅숏: panel(chat·friends·send·mon)이 접히는 중 e 에 멈춘 화면 (--screen=jam:fold=chat:0.25) */
    void previewFold (const juce::String& panel, float e);
    /** 개발용: 누른 것처럼 접거나 펼친다 (--screen=jam:chatfill 의 프레임 확인) */
    void devToggleFold (const juce::String& panel);
    juce::String memberName (const juce::String& userId) const;
private:
    mutable std::map<juce::String, juce::String> knownNames;   // 나간 멤버의 채팅 줄도 이름으로 (마지막으로 본 이름)
public:
    /** 프로필 창 (사이드바의 친구를 눌렀을 때도 연다. 홈·사이드바 디자인) */
    void openProfile (const juce::String& userId);
    /** 홈·사이드바 디자인 v2: 사이드바가 옆에 있으면 윗줄의 옛 앱 버튼(로고·로비 보기·알림·설정)을 숨긴다
        (방 이름·방 설정·입장 요청·입장 코드·방 나가기만). 사이드바가 숨으면(좁은 플러그인 창) 예전 윗줄로 돌아간다 */
    void setSidebarShown (bool);
    /** 방 이름 왼쪽 여백 (디자인 hdPL: 보통 18, 맥 창 모드에서 사이드바를 접으면 72: 신호등·접기 버튼 자리) */
    void setHeaderInset (int);
    /** 방 화면을 어둡게 덮는 모달이 떠 있다 (사이드바는 덮지 않는다) */
    bool dimsScreen() const { return modal != nullptr; }
    /** 겹침이 생기거나 사라졌다 (AppRoot 가 접기 버튼을 덮개 아래로) */
    std::function<void()> onOverlayChanged;
    /** 윗줄의 빈 곳·방 이름을 눌렀다 (맥 창 모드: 창 끌기·더블클릭) */
    std::function<void()> onTitleBarMouseDown;
    void mouseDown (const juce::MouseEvent&) override;
    void childrenChanged() override { if (onOverlayChanged) onOverlayChanged(); }

private:
    friend class UiSelfTest;   // 개발용 화면 자가 테스트 (--uitest)
    core::MeonCore& core;
    core::JamSession& jam;
    const bool plugin;
    bool sidebarShown = false;
    int headerInset = metric::padX;
    /** 윗줄 아래 선의 y: 사이드바가 옆에 있으면 사이드바·홈 머리말과 같은 51 (높이 52), 아니면 예전 58 (로고 높이) */
    int headerLine() const { return sidebarShown ? 51 : metric::headerH; }

    // 헤더
    Text roomTitle { {}, 600, 16.0f };
    std::unique_ptr<Tag> privateTag;
    struct CodeBox;
    std::unique_ptr<CodeBox> codeBox;
    ConfirmIconButton leave { (int) icons::Id::logout };   // 방 나가기 (두 번 눌러 확정, v2.1 최신: 아이콘만)
    IconButton gear { (int) icons::Id::gear, IconButton::Style::plain24, 14.0f };              // 방장: 방 설정
    IconButton requests { (int) icons::Id::personPlus, IconButton::Style::ghost32, 18.0f };    // 방장: 입장 요청 (요청이 없어도 보임. v2.1 최신: 테두리 없음)
    IconButton bell { (int) icons::Id::bell, IconButton::Style::bordered32, 16.0f };           // 알림 (받은 친구 요청, 홈과 같은 목록)
    IconButton settings { (int) icons::Id::gearHeader, IconButton::Style::bordered32, 16.0f }; // 설정
    IconButton lobby { (int) icons::Id::lobby, IconButton::Style::bordered32, 16.0f };         // 로비 보기 (방 최소화)
    Tip tip;
    std::unique_ptr<TipOnHover> settingsTip, lobbyTip;

    // 겹침 (방 설정·멤버 관리·프로필, 입장 요청 팝오버)
    std::unique_ptr<Modal> modal;
    std::unique_ptr<Popover> popover;
    std::unique_ptr<JoinRequestList> requestList;
    std::unique_ptr<NotificationList> notifications;
    void showModal (std::unique_ptr<Modal>);
    void closePopover();
    void toggleRequests();
    void toggleNotifications();
    void showProfile (const core::UserPublic&);
    void openManage (const core::JamSession::Member&);

    // 멤버
    Card membersCard;
    struct MemberRow;
    struct MemberList;
    std::unique_ptr<MemberList> memberList;
    juce::Viewport memberViewport;

    // 내 오디오 · 모니터
    Card sendCard, monCard;
    Button sendMute { Button::Kind::small, TXT ("송출 정지") }, monMute { Button::Kind::small, TXT ("전체 음소거") };
    DbSlider sendGain { -30.0f, 10.0f }, master { -30.0f, 6.0f };
    MeterBox inputMeter { TXT ("입력 미터") }, outputMeter { TXT ("출력 미터") };
    bool sendOpen = true, monOpen = true;

    // 오른쪽 패널 너비 조절 (멤버·패널 사이 18 전체, 끌기·더블클릭하면 기본)
    struct SideResizer;
    std::unique_ptr<SideResizer> sideResizer;
    int sideSaved = 0;   // 저장값 (0 = 기본). 작은 창에서 줄어 보여도 그대로 둔다

    // 채팅
    Card chatCard;
    struct ChatView;
    std::unique_ptr<ChatView> chatView;
    juce::Viewport chatViewport;
    TextInput chatInput { TXT ("메시지 입력"), true };   // 여러 줄 (합주 최종 디자인 7): 1줄 44 ~ 3줄 84, 그다음 칸 안에서 스크롤
    Button chatSend { Button::Kind::link, TXT ("전송") };
    juce::String chatNote;
    struct ChatDecor;
    std::unique_ptr<ChatDecor> chatDecor;   // 목록·입력 사이 구분선, 남은 글자 (채팅 카드 안이라 모달 아래에 그려진다)
    bool chatOpen = true, chatAtBottom = true;
    int chatLines = 1;                     // 입력 칸에 보이는 줄 (1..3)
    juce::Rectangle<int> chatCounterArea;  // "962 / 1000" (900 바이트부터, 전송 왼쪽 8)
    juce::String lastSentChat;             // 늦게 들어온 한글 확정 글자 거르기 (core::chatinput::isLateImeLeftover)
    juce::int64 lastSentAt = 0;
    void sendChat();

    // 친구
    Card friendsCard;
    struct FriendList;
    std::unique_ptr<FriendList> friendList;
    juce::Viewport friendViewport;
    bool friendsOpen = true;

    // 파일 플레이어
    struct FilePlayer;
    std::unique_ptr<FilePlayer> filePlayer;

    void rebuildMembers();
    void rebuildFriends();
    void refreshHeader();
    void refreshAudio();
    void refreshChat();
    void fitChatView();   // 폭이 그대로면 메시지는 다시 배치하지 않고 칸 높이·스크롤만 (접기·펼치기 중)
    int chatContentH = 0;

    void stateChanged (core::StateKey) override;
    void jamPhaseChanged (core::JamSession::Phase) override { rebuildMembers(); }
    void jamMembersChanged() override { rebuildMembers(); refreshChat(); }
    void jamStatsChanged() override;
    void jamFileChanged() override;
    void chatChanged() override { refreshChat(); }
    void timerCallback() override;

    // 접기·펼치기 (오른쪽 열: 채팅·친구 세로, 아래쪽 행: 내 오디오·모니터 가로). 움직이는 동안 내용은 시작·끝 크기 중
    // 큰 쪽으로 한 번만 배치하고 카드가 잘라 보인다 (foldContent*). 끝나면 기존 레이아웃 코드로 돌아간다
    void toggleFold (int panel, bool open);
    void layoutChatContent (int cardW, int cardH);
    void layoutFriendsContent (int cardW, int cardH);
    int sideAvail = 0, bottomAvail = 0;   // 지난 resized 의 묶음 안 크기 (간격 뺀 값)
    int foldChatH = 0, foldFriendsH = 0, foldSendW = 0, foldMonW = 0;
    bool foldFillV = true, foldFillH = true;   // 이번 움직임의 시작·끝이 모두 묶음을 채우는지 (FoldLayout::blend)
    juce::Point<int> laidChat, laidFriends;   // 움직이는 동안 배치해 둔 크기 (같으면 다시 배치하지 않는다)
    FoldAnimator fold { *this, [this] { resized(); } };
    const int foldChat = fold.add ("chat", true), foldFriends = fold.add ("friends", true),
              foldSend = fold.add ("send", true), foldMon = fold.add ("mon", true);
};

} // namespace meon::ui
