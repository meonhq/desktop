// MEON ui - 개발용 화면 자가 테스트 (--uitest=all|leave|minimize|resize|chat|aoodrop|rooms|friends). 빌드 0 (MEON_DEV_OPTIONS) 전용.
// 실제 창에 마우스·키 이벤트를 넣고(ComponentPeer) 화면·store·엔진 상태를 확인한다. 가짜 서버로 자동 가입한 홈에서 시작한다.
// 결과는 표준 오류에 "UITEST PASS|FAIL <구역> · <확인>" 한 줄씩, 끝에 "UITEST DONE <통과> passed, <실패> failed".
// 끝나면 앱을 닫고 실패 수를 종료 코드로 돌려준다 (linux.yml 이 Xvfb 에서 돌린다).
// --fake-script=0 과 함께 돌린다: 각본 사용자가 움직이면 친구의 '참여' 버튼처럼 시각에 따라 달라지는 것이 있다.
#pragma once

#include <JuceHeader.h>
#include "../../core/MeonCore.h"

namespace meon::ui
{

class AppRoot;
class HomeScreen;
class JamScreen;

class UiSelfTest : private juce::Timer
{
public:
    UiSelfTest (core::MeonCore&, AppRoot&, const juce::String& which);
    ~UiSelfTest() override;

    void start();
    std::function<void (int failures)> onDone;

private:
    core::MeonCore& core;
    AppRoot& root;
    const juce::String which;

    struct Step
    {
        juce::String label;
        std::function<void()> run;
        std::function<bool()> until;   // 있으면 참이 될 때까지 기다린다 (timeoutMs 넘으면 실패)
        int waitMs = 150;
        int timeoutMs = 0;
    };
    std::vector<Step> steps;
    size_t index = 0;
    int failures = 0, passes = 0;
    juce::int64 waitStart = 0;
    juce::String section;

    void add (std::function<void()> run, int waitAfterMs = 150);
    void waitUntil (const juce::String& label, std::function<bool()> cond, int timeoutMs = 4000);
    void wait (int ms) { add ({}, ms); }
    void begin (const juce::String& name);
    void timerCallback() override;
    void finish();

    void expect (bool ok, const juce::String& what);

    // 입력 (창의 ComponentPeer 로 넣는다: 실제 마우스·키와 같은 길)
    juce::int64 clock = 0;
    juce::ComponentPeer* peer() const;
    /** 좌표를 주지 않으면 구성 요소 가운데 */
    static juce::Point<int> centre() { return { std::numeric_limits<int>::min(), 0 }; }
    juce::Point<float> toPeer (juce::Component&, juce::Point<int>) const;
    void mouse (juce::Point<float>, juce::ModifierKeys);
    void moveTo (juce::Component&, juce::Point<int> local);
    void click (juce::Component&, juce::Point<int> local = centre());
    void doubleClick (juce::Component&, juce::Point<int> local = centre());
    void drag (juce::Component&, juce::Point<int> from, juce::Point<int> to, int steps = 6);
    void key (const juce::KeyPress&);
    void type (const juce::String&);

    // 화면
    HomeScreen& home() const;
    JamScreen* jam() const;
    bool inRoom() const;
    juce::String roomId() const;
    static juce::Button* findButton (juce::Component&, const juce::String& text);
    static juce::Button* firstButton (juce::Component&);

    // 방에 들어가는 단계 (approval: 방장인 승인 방)
    void enterNewRoom (bool approval = false);
    void minimize();
    void clickSidebar (const juce::String& text);   // 사이드바 줄(메뉴·참여 중 방)을 누른다

    // 시나리오
    void addLeaveTests();
    void addMinimizeTests();
    void addResizeTests();
    void addChatInputTests();
    void addAooDropTests();
    void addRoomListTests();
    void addFriendRequestTests();
    void addSidebarTests();
    /** parent 의 보이는 자식끼리 겹치지 않고 parent 안에 있는지 (겹치면 그 이름들) */
    static juce::String overlaps (juce::Component& parent);
};

} // namespace meon::ui
