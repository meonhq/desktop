// MEON ui - 사이드바 디자인의 홈 (홈·사이드바 디자인). 3열 카드:
//   1열: 내 준비 상태(장치·입력·서버·연결) / 내 프로필
//   2열: 최근 같이 합주한 사람
//   3열: 합주 기록(달력) / 합주 중인 친구
// 실제 데이터가 있는 칸은 AppStore·JamSession 을 쓰고, 아직 서버에 없는 칸(최근 같이 합주한 사람·합주 기록 등)은
// 디자인의 목 데이터로 채운다 (확인용 빌드). HomeScreen 이 사이드바 안 "홈" 쪽일 때 보인다.
#pragma once

#include "../widgets/Button.h"
#include "../widgets/Display.h"
#include "../widgets/Meter.h"
#include "../theme/Icons.h"
#include "../../core/MeonCore.h"

namespace meon::ui
{

class HomePage : public juce::Component,
                 private core::AppStore::Listener,
                 private juce::Timer
{
public:
    explicit HomePage (core::MeonCore&);
    ~HomePage() override;

    std::function<void (int tab)> onOpenSettings;   // 0 계정, 1 프로필, 2 오디오 …
    std::function<void()> onGoRooms;                 // "공개 방 둘러보기"
    std::function<void (const core::Friend&)> onJoinFriend;   // 합주 중인 친구의 참여·입장 요청 (HomeScreen::joinFriend)

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    core::MeonCore& core;
    const bool plugin;

    // 내 준비 상태
    Button devBtn { Button::Kind::small, TXT ("변경") };
    Button fixBtn { Button::Kind::smallPrimary, TXT ("입력 확인 방법") };
    Button monBtn { Button::Kind::small, TXT ("모니터링 테스트") };
    Button retryBtn { Button::Kind::smallPrimary, TXT ("다시 시도") };
    MeterBox inputMeter { TXT ("입력 레벨") };
    bool retryBusy = false;
    int ticks = 0;
    juce::String deviceName() const;
    juce::String inputText() const;

    // 최근 같이 합주한 사람 (목 데이터)
    struct Recent
    {
        juce::String name, part;
        bool friend_ = false, online = false;
        juce::String last;
        int count = 0;
        int state = 0;   // 친구 추가: 1 요청 보냄 / 초대: 1 보내는 중(3초) 2 초대됨
        juce::uint32 pendingAt = 0;
        std::unique_ptr<Button> button;
    };
    std::vector<Recent> recent;
    bool recentAll = false;
    juce::Rectangle<int> moreBounds;
    bool moreHot = false;
    Button browseRooms { Button::Kind::secondary, TXT ("공개 방 둘러보기") };
    bool hasRecent() const { return ! recent.empty(); }
    int recentShown() const { return recentAll ? (int) recent.size() : juce::jmin (8, (int) recent.size()); }
    void refreshRecentButtons();

    // 내 프로필
    Button editBtn { Button::Kind::small, TXT ("편집") };
    Button copyBtn { Button::Kind::small, TXT ("복사") };
    juce::uint32 copiedAt = 0;

    // 합주 기록 (목 데이터: 합주한 날)
    int calOff = 0;             // 0 = 이번 달, -1 = 지난달 … (-12 까지)
    int calHot = 0;             // 1 이전 달, 2 다음 달
    std::vector<int> jamDays (int year, int month) const;   // month 1~12
    int calRows() const;
    juce::Rectangle<int> calPrev, calNext;

    // 합주 중인 친구 (실제: 친구 상태 in_room, 방마다 묶음. 내가 있는 방은 뺀다)
    struct JamGroup
    {
        juce::String roomId, title;
        std::vector<core::Friend> friends;
        std::unique_ptr<Button> button;
    };
    std::vector<JamGroup> groups;
    void rebuildGroups();
    void refreshGroupButtons();
    int jamFriendCount() const;

    // 배치 (paint·resized 가 같이 쓴다)
    struct Layout
    {
        juce::Rectangle<int> col[3];
        juce::Rectangle<int> readyCard, profileCard, recentCard, calCard, jamCard;
        juce::Rectangle<int> rowDev, rowIn, rowSrv, rowNet;
    };
    Layout layout() const;

    void refresh();
    void stateChanged (core::StateKey) override;
    void timerCallback() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
};

} // namespace meon::ui
