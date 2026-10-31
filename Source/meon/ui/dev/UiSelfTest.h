// MEON ui - 개발용 화면 자가 테스트 (--uitest=all|leave|minimize|resize|chat|aoodrop|rooms|friends|sidebar|roomsidebar|chrome|popover|winchrome|overlay|dock|modalanim|reducedmotion). 빌드 0 (MEON_DEV_OPTIONS) 전용.
// 실제 창에 마우스·키 이벤트를 넣고(ComponentPeer) 화면·store·엔진 상태를 확인한다. 가짜 서버로 자동 가입한 홈에서 시작한다.
// 결과는 표준 오류에 "UITEST PASS|FAIL <구역> · <확인>" 한 줄씩, 끝에 "UITEST DONE <통과> passed, <실패> failed".
// 끝나면 앱을 닫고 실패 수를 종료 코드로 돌려준다 (linux.yml 이 Xvfb 에서 돌린다).
// --fake-script=0 과 함께 돌린다: 각본 사용자가 움직이면 친구의 '참여' 버튼처럼 시각에 따라 달라지는 것이 있다.
#pragma once

#include <JuceHeader.h>
#include "../../core/MeonCore.h"
#include "../../MeonWindowChrome.h"

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
    /** 맥 창 제목 줄 상태 한 줄 (UITEST INFO chrome …) */
    static juce::String describe (const meon::chrome::Probe&);

    /** 윈도우 창 틀 확인 한 번 (--uitest=winstates): 상태별 내용 자리(보통 = 창 - 틀, 최대화 = 작업 영역, 전체 화면 = 모니터),
        JUCE 가 아는 내용 자리·크기, 화면에 보이는 창과 앱이 그린 그림을 비교 (네 가장자리 줄·창 버튼 자리: 여백·운영체제 창 버튼이 있으면 다르다).
        key 는 캡처 파일 이름 (환경 변수 MEON_WINCAPS 폴더에 PNG). 실패 내용은 detail 에 */
    static bool checkWindowFrame (juce::Component& editor, const juce::String& label, const juce::String& key, juce::String& detail);
    /** 켠 직후(온보딩 화면) 확인 결과: 에디터가 창이 뜰 때 한 번 (실패 수, 설명) */
    static inline int launchFrameFailures = 0, launchFrameChecks = 0;
    static inline juce::String launchFrameDetail;
    /** checkWindowFrame 가 가장자리 비교에서 뺄 화면 물리 사각형 (다른 창이 덮은 자리) */
    static inline juce::Rectangle<int> frameCheckIgnore;

private:
    core::MeonCore& core;
    AppRoot& root;
    const juce::String which;

    struct Step
    {
        juce::String label;
        std::function<void()> run;
        std::function<bool()> until;   // 있으면 참이 될 때까지 기다린다 (timeoutMs 넘으면 실패)
        std::function<void()> onTimeout;   // 있으면 시간 초과를 실패로 세지 않고 이것을 부른다 (러너에서 안 될 수 있는 것)
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
    void waitUntilOr (const juce::String& label, std::function<bool()> cond, int timeoutMs, std::function<void()> onTimeout);
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
    juce::StringArray kickedRooms;   // 자가 테스트 중 내보내진 방 (다시 들어갈 수 없다)
    static juce::Button* findButton (juce::Component&, const juce::String& text);
    juce::Button* confirmButton (const juce::String& text) const;
    juce::String dockOverlaps() const;   // 내 오디오·모니터 카드 묶음 안 겹침 (미터 상자는 형제)
    static juce::Button* firstButton (juce::Component&);

    // 방에 들어가는 단계 (approval: 방장인 승인 방)
    void enterNewRoom (bool approval = false);
    void minimize();
    void clickSidebar (const juce::String& text);   // 사이드바 줄(메뉴·참여 중 방)을 누른다
    void closeToasts();   // 앞 섹션의 토스트(오른쪽 아래)를 닫는다. 방 화면 채팅 입력·방 카드를 가릴 수 있다

    // 시나리오
    void addLeaveTests();
    void addMinimizeTests();
    void addResizeTests();
    void addChatInputTests();
    void addAooDropTests();
    void addRoomListTests();
    void addFriendRequestTests();
    void addRoomSidebarTests();   // 방 화면에 들어갈 때 사이드바 상태 (창 폭별)
    void addSidebarTests();
    void addChromeTests();        // 맥 창 신호등 (--uitest=chrome, mac.yml)
    void addPopoverTests();       // 팝오버 그림자 자리·마우스·그림자 다시 그리기 (--uitest=popover)
    void addWinChromeTests();     // 윈도우 창 윗부분: 창 버튼·제목 줄 영역 판정·최대화·끌기 (--uitest=winchrome, windows.yml)
    void addOverlayTests();       // 팝오버·모달·토스트를 열고 닫아도 화면 그대로, 모달 층(창 전체), 방 안 코드로 입장 숨김 (--uitest=overlay)
    void addDockTests();          // 내 오디오·모니터 카드 묶음: 자리·틈 누름·목록 여백·다시 그리기·방 화면 ↔ 공개 방 (--uitest=dock)
    void addModalAnimTests();     // 모달 열고 닫는 애니메이션 (--uitest=modalanim)
    void addReducedMotionTests(); // 동작 줄이기 (--uitest=reducedmotion, MEON_REDUCE_MOTION=1)
    void addWinEdgeTests();       // 윈도우 창 왼쪽·오른쪽·아래 가장자리 픽셀 (--uitest=winedge, windows.yml)
    void addFullscreenTests();    // 전체 화면 단축키 (--uitest=fullscreen, 실제 창: windows.yml·mac.yml)
    void addWinStatesTests();     // 윈도우 창 상태별 가장자리·창 버튼·크기 조절 (--uitest=winstates, windows.yml)
    void addProfileFooterTests(); // 친구 프로필 아래 줄: 보이는 버튼이 없으면 아래 줄(구분선 포함)도 없다 (--uitest=profilefooter)
    void addCodeInputTests();
    void addFriendAddTests();     // 친구 추가 창 (합주 최종 v2): 크기·결과 문구·색·Enter(한글 조합 중 제외)·취소·받은 요청 (--uitest=friendadd)     // 6칸 코드 입력: 위 숫자 줄·숫자 키패드·붙여넣기·한글 자판 (방 코드 / 이메일 인증) (--uitest=codeinput)
    /** parent 의 보이는 자식끼리 겹치지 않고 parent 안에 있는지 (겹치면 그 이름들) */
    static juce::String overlaps (juce::Component& parent);
};

} // namespace meon::ui
