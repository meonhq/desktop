// MEON ui - 합주 화면 (디자인 L1). 헤더(로고 · 방 이름 · 비공개 · 입장 코드+복사 · 설정 · 방 나가기),
// 서버 연결 끊김 배너(Q16), 멤버 행(상태·음소거·볼륨·패닝·초기화·핑·미터), 내 오디오(송출·무음 경고)·모니터,
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

    std::function<void()> onOpenSettings, onOpenAudioSettings;
    /** 프로필 창에서 다른 방에 들어가거나 요청하다 실패 (입장 실패 상자, AppRoot) */
    std::function<void (const core::ApiError&)> onJoinFailure;

    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;
    /** 단축키 (설정 › 단축키, REMAKE_PLAN 4.4): M 송출, N 모니터, ⌘↑/⌘↓ 마스터 ±1 dB, Enter 채팅 입력, Esc 겹침 닫기 */
    bool handleShortcut (const juce::KeyPress&);
    void closeOverlays();
    void resized() override;

    static constexpr int rowH = 59;
    /** 개발용 스냅숏: panel(chat·friends·send·mon)이 접히는 중 e 에 멈춘 화면 (--screen=jam:fold=chat:0.25) */
    void previewFold (const juce::String& panel, float e);
    /** 개발용: 누른 것처럼 접거나 펼친다 (--screen=jam:chatfill 의 프레임 확인) */
    void devToggleFold (const juce::String& panel);
    juce::String memberName (const juce::String& userId) const;
private:
    mutable std::map<juce::String, juce::String> knownNames;   // 나간 멤버의 채팅 줄도 이름으로 (마지막으로 본 이름)
public:

private:
    core::MeonCore& core;
    core::JamSession& jam;
    const bool plugin;

    // 헤더
    Text roomTitle { {}, 600, 16.0f };
    std::unique_ptr<Tag> privateTag;
    struct CodeBox;
    std::unique_ptr<CodeBox> codeBox;
    Button settings { Button::Kind::quiet, TXT ("설정") }, leave { Button::Kind::secondary, TXT ("방 나가기") };
    IconButton gear { (int) icons::Id::gear, IconButton::Style::plain24, 14.0f };              // 방장: 방 설정
    IconButton requests { (int) icons::Id::personPlus, IconButton::Style::bordered32, 16.0f }; // 방장 + 승인 방: 입장 요청

    // 겹침 (방 설정·멤버 관리·프로필, 입장 요청 팝오버)
    std::unique_ptr<Modal> modal;
    std::unique_ptr<Popover> popover;
    std::unique_ptr<JoinRequestList> requestList;
    void showModal (std::unique_ptr<Modal>);
    void toggleRequests();
    void openProfile (const juce::String& userId);
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
    struct SilenceRow;
    std::unique_ptr<SilenceRow> silenceRow;   // 입력 신호 없음 경고 (내 오디오 카드 안, 미터 아래)
    int silenceRowH (int width) const;   // 경고가 없으면 0. 좁으면 설명을 두 줄로 내려 높아진다

    // 채팅
    Card chatCard;
    struct ChatView;
    std::unique_ptr<ChatView> chatView;
    juce::Viewport chatViewport;
    TextInput chatInput { TXT ("메시지 입력") };
    Button chatSend { Button::Kind::link, TXT ("전송") };
    juce::String chatNote;
    struct ChatDecor;
    std::unique_ptr<ChatDecor> chatDecor;   // 목록·입력 사이 구분선, 남은 글자 (채팅 카드 안이라 모달 아래에 그려진다)
    bool chatOpen = true, chatAtBottom = true;
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
    void jamSilenceChanged (bool) override { resized(); repaint(); }
    void jamFileChanged() override;
    void chatChanged() override { refreshChat(); }
    void timerCallback() override;

    // 접기·펼치기 (오른쪽 열: 채팅·친구 세로, 아래쪽 행: 내 오디오·모니터 가로). 움직이는 동안 내용은 시작·끝 크기 중
    // 큰 쪽으로 한 번만 배치하고 카드가 잘라 보인다 (foldContent*). 끝나면 기존 레이아웃 코드로 돌아간다
    void toggleFold (int panel, bool open);
    void layoutChatContent (int cardW, int cardH);
    void layoutFriendsContent (int cardW, int cardH);
    int sideAvail = 0, bottomAvail = 0;   // 지난 resized 의 묶음 안 크기 (간격 뺀 값)
    int foldChatH = 0, foldFriendsH = 0, foldSendW = 0, foldMonW = 0, foldWarnW = 0;
    bool foldFillV = true, foldFillH = true;   // 이번 움직임의 시작·끝이 모두 묶음을 채우는지 (FoldLayout::blend)
    juce::Point<int> laidChat, laidFriends;   // 움직이는 동안 배치해 둔 크기 (같으면 다시 배치하지 않는다)
    FoldAnimator fold { *this, [this] { resized(); } };
    const int foldChat = fold.add ("chat", true), foldFriends = fold.add ("friends", true),
              foldSend = fold.add ("send", true), foldMon = fold.add ("mon", true);
};

} // namespace meon::ui
