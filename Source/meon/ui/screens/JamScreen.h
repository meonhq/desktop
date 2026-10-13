// MEON ui - 합주 화면 (디자인 L1). 헤더(로고 · 방 이름 · 비공개 · 입장 코드+복사 · 설정 · 방 나가기),
// 서버 연결 끊김 배너(Q16), 멤버 행(상태·음소거·볼륨·패닝·초기화·핑·미터), 내 오디오(송출·무음 경고)·모니터,
// 채팅(전달 확인·실패 표시·남은 글자), 친구(초대 3초), 파일 플레이어.
// 방장 전용(방 설정·입장 요청·관리)은 R7 에서 붙인다.
#pragma once

#include "../widgets/Button.h"
#include "../widgets/Inputs.h"
#include "../widgets/Controls.h"
#include "../widgets/Display.h"
#include "../widgets/Meter.h"
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

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void resized() override;

    static constexpr int rowH = 59;
    juce::String memberName (const juce::String& userId) const;

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
    void openManage (const core::JamSession::Member&);
    bool showBanner() const;

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
    int silenceRowH() const { return jam.isInputSilent() ? 37 : 0; }

    // 채팅
    Card chatCard;
    struct ChatView;
    std::unique_ptr<ChatView> chatView;
    juce::Viewport chatViewport;
    TextInput chatInput { TXT ("메시지 입력") };
    Button chatSend { Button::Kind::link, TXT ("전송") };
    juce::String chatNote;
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

    void stateChanged (core::StateKey) override;
    void jamPhaseChanged (core::JamSession::Phase) override { rebuildMembers(); }
    void jamMembersChanged() override { rebuildMembers(); refreshChat(); }
    void jamStatsChanged() override;
    void jamSilenceChanged (bool) override { resized(); repaint(); }
    void jamFileChanged() override;
    void chatChanged() override { refreshChat(); }
    void timerCallback() override;
};

} // namespace meon::ui
