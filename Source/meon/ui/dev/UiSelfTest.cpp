#include "UiSelfTest.h"
#include "../AppRoot.h"
#include "../screens/HomeScreen.h"
#include "../screens/JamScreen.h"
#include "../overlays/JamOverlays.h"
#include "../../core/api/fake/FakeMeonServer.h"
#include "../../core/model/RoomFilter.h"
#include "../../core/model/PanelWidth.h"
#include "../../MeonSettings.h"
#include "../../core/util/ChatInput.h"
#include "../../MeonWindowFrame.h"
#include "../shell/WindowChromeView.h"
#include "../overlays/SettingsModal.h"
#include "../../MeonReduceMotion.h"
#include "RepaintProbe.h"
#include "../../shell/ShellHooks.h"
#include "../../MeonEditor.h"
#include <climits>

namespace meon::ui
{

UiSelfTest::UiSelfTest (core::MeonCore& c, AppRoot& r, const juce::String& w)
    : core (c), root (r), which (w.isEmpty() ? juce::String ("all") : w)
{
    clock = juce::Time::currentTimeMillis();
}

UiSelfTest::~UiSelfTest() { stopTimer(); }

void UiSelfTest::start()
{
    const auto want = [this] (const char* name) { return which == "all" || which == name; };
    if (want ("leave"))    addLeaveTests();
    if (want ("minimize")) addMinimizeTests();
    if (want ("resize"))   addResizeTests();
    if (want ("chat"))     addChatInputTests();
    if (want ("aoodrop"))  addAooDropTests();
    if (want ("rooms"))    addRoomListTests();
    if (want ("sidebar") || which == "roomsidebar") addRoomSidebarTests();   // roomsidebar: 이 구역만 (CI 창 폭별 확인)
    if (want ("sidebar"))  addSidebarTests();
    if (want ("chrome"))   addChromeTests();
    if (want ("popover"))  addPopoverTests();
    if (want ("winchrome")) addWinChromeTests();
    if (which == "winedge") addWinEdgeTests();         // 윈도우 창 가장자리 (windows.yml, all 에 없음: 바탕 창을 띄운다)
    if (which == "fullscreen") addFullscreenTests();   // 실제 창 (windows.yml·mac.yml), all 에 없음
    if (which == "winstates") addWinStatesTests();     // 윈도우 창 상태별 (windows.yml, all 에 없음: 실제 창을 여러 상태로)
    if (want ("overlay"))  addOverlayTests();
    if (want ("dock"))     addDockTests();
    if (want ("modalanim")) addModalAnimTests();
    if (which == "reducedmotion") addReducedMotionTests();   // MEON_REDUCE_MOTION=1 로 돌린다 (all 에 없음)
    if (want ("friends"))  addFriendRequestTests();   // 친구가 늘어 친구 패널이 바뀌므로 마지막
    std::cerr << "UITEST START " << which << " (" << steps.size() << " steps)" << std::endl;
    startTimer (20);
}

//==============================================================================
void UiSelfTest::add (std::function<void()> run, int waitAfterMs)
{
    Step s;
    s.run = std::move (run);
    s.waitMs = waitAfterMs;
    steps.push_back (std::move (s));
}

void UiSelfTest::waitUntil (const juce::String& label, std::function<bool()> cond, int timeoutMs)
{
    Step s;
    s.label = label;
    s.until = std::move (cond);
    s.timeoutMs = timeoutMs;
    s.waitMs = 0;
    steps.push_back (std::move (s));
}

void UiSelfTest::waitUntilOr (const juce::String& label, std::function<bool()> cond, int timeoutMs, std::function<void()> onTimeout)
{
    waitUntil (label, std::move (cond), timeoutMs);
    steps.back().onTimeout = std::move (onTimeout);
}

void UiSelfTest::begin (const juce::String& name)
{
    add ([this, name] { section = name; std::cerr << "UITEST SECTION " << name << std::endl; }, 0);
}

void UiSelfTest::timerCallback()
{
    const auto now = juce::Time::currentTimeMillis();
    if (index >= steps.size()) { finish(); return; }
    auto& s = steps[index];
    if (s.until)
    {
        if (waitStart == 0) waitStart = now;
        if (s.until())
        {
            waitStart = 0;
            ++index;
            return;
        }
        if (now - waitStart > s.timeoutMs)
        {
            if (s.onTimeout) s.onTimeout();
            else expect (false, TXT ("시간 초과: ") + s.label);
            waitStart = 0;
            ++index;
        }
        return;
    }
    if (waitStart == 0)
    {
        if (s.run) s.run();
        waitStart = now;
    }
    if (now - waitStart >= s.waitMs)
    {
        waitStart = 0;
        ++index;
    }
}

void UiSelfTest::finish()
{
    stopTimer();
    std::cerr << "UITEST DONE " << passes << " passed, " << failures << " failed" << std::endl;
    if (onDone) onDone (failures);
}

void UiSelfTest::expect (bool ok, const juce::String& what)
{
    if (ok) ++passes; else ++failures;
    std::cerr << (ok ? "UITEST PASS " : "UITEST FAIL ") << section << " · " << what << std::endl;
}

//==============================================================================
juce::ComponentPeer* UiSelfTest::peer() const
{
    return root.getPeer();
}

juce::Point<float> UiSelfTest::toPeer (juce::Component& c, juce::Point<int> local) const
{
    auto* p = peer();
    if (p == nullptr) return {};
    if (local == centre()) local = c.getLocalBounds().getCentre();
    return p->getComponent().getLocalPoint (&c, local).toFloat();
}

void UiSelfTest::mouse (juce::Point<float> pos, juce::ModifierKeys mods)
{
    if (auto* p = peer())
    {
        // 호버 판정(isMouseOver)은 OS 포인터 위치를 보므로 포인터도 같은 자리로 옮긴다 (Xvfb)
        juce::Desktop::setMousePosition (p->localToGlobal (pos).roundToInt());
        p->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, pos, mods,
                             juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation, ++clock);
    }
}

void UiSelfTest::moveTo (juce::Component& c, juce::Point<int> local)
{
    mouse (toPeer (c, local), {});
}

void UiSelfTest::click (juce::Component& c, juce::Point<int> local)
{
    clock += 1000;   // 앞 클릭과 이어진 더블클릭으로 보이지 않게
    const auto pos = toPeer (c, local);
    mouse (pos, {});
    mouse (pos, juce::ModifierKeys::leftButtonModifier);
    mouse (pos, {});
}

void UiSelfTest::doubleClick (juce::Component& c, juce::Point<int> local)
{
    clock += 1000;
    const auto pos = toPeer (c, local);
    mouse (pos, {});
    for (int i = 0; i < 2; ++i)
    {
        mouse (pos, juce::ModifierKeys::leftButtonModifier);
        mouse (pos, {});
    }
}

void UiSelfTest::drag (juce::Component& c, juce::Point<int> from, juce::Point<int> to, int n)
{
    clock += 1000;
    const auto a = toPeer (c, from), b = toPeer (c, to);
    mouse (a, juce::ModifierKeys::leftButtonModifier);
    for (int i = 1; i <= n; ++i)
        mouse (a + (b - a) * ((float) i / (float) n), juce::ModifierKeys::leftButtonModifier);
    mouse (b, {});
}

void UiSelfTest::key (const juce::KeyPress& k)
{
    if (auto* p = peer()) p->handleKeyPress (k);
}

void UiSelfTest::type (const juce::String& text)
{
    for (auto ch : text)
        key (juce::KeyPress (0, juce::ModifierKeys(), ch));
}

//==============================================================================
HomeScreen& UiSelfTest::home() const { return *root.home; }
JamScreen* UiSelfTest::jam() const   { return root.getJamScreen(); }
bool UiSelfTest::inRoom() const      { return core.store().state().currentRoom.has_value(); }
juce::String UiSelfTest::roomId() const
{
    const auto& r = core.store().state().currentRoom;
    return r ? r->id : juce::String();
}

juce::Button* UiSelfTest::findButton (juce::Component& c, const juce::String& text)
{
    for (auto* child : c.getChildren())
    {
        if (auto* b = dynamic_cast<juce::Button*> (child); b != nullptr && b->isShowing() && b->getButtonText() == text)
            return b;
        if (auto* found = findButton (*child, text)) return found;
    }
    return nullptr;
}

juce::String UiSelfTest::dockOverlaps() const
{
    // 카드 묶음: 카드 안 버튼·슬라이더와 (형제인) 미터 상자가 겹치지 않고 카드 안에 있는지
    juce::String bad;
    auto* d = root.dock.get();
    if (d == nullptr) return TXT ("카드 묶음 없음");
    auto one = [&] (Card& card, juce::Component& meter, const juce::String& name)
    {
        juce::Array<juce::Rectangle<int>> rs;
        juce::StringArray names;
        for (auto* c : card.getChildren())
            if (c->isVisible() && ! c->getBounds().isEmpty() && c->getY() > 40) { rs.add (c->getBounds()); names.add (c->getName()); }
        const auto m = meter.getBounds() - d->getPosition() - card.getPosition();
        if (meter.isVisible()) { rs.add (m); names.add ("meter"); }
        for (int i = 0; i < rs.size(); ++i)
        {
            if (! card.getLocalBounds().contains (rs[i])) bad << name << " " << names[i] << TXT (" 카드 밖 ");
            for (int k = i + 1; k < rs.size(); ++k)
                if (rs[i].intersects (rs[k])) bad << name << " " << names[i] << "/" << names[k] << " ";
        }
    };
    one (d->sendCard, d->inputMeter, TXT ("내 오디오"));
    one (d->monCard, d->outputMeter, TXT ("모니터"));
    return bad;
}

juce::Button* UiSelfTest::confirmButton (const juce::String& text) const
{
    // 홈의 확인 모달 (모달 층: AppRoot 의 자식)
    return home().confirm != nullptr ? findButton (*home().confirm, text) : nullptr;
}

juce::Button* UiSelfTest::firstButton (juce::Component& c)
{
    for (auto* child : c.getChildren())
    {
        if (auto* b = dynamic_cast<juce::Button*> (child); b != nullptr && b->isShowing()) return b;
        if (auto* found = firstButton (*child)) return found;
    }
    return nullptr;
}

void UiSelfTest::enterNewRoom (bool approval)
{
    add ([this, approval]
    {
        core::CreateRoom c;
        c.type = approval ? "public" : "private";
        if (approval) c.entryMode = juce::String ("approval");
        c.title = TXT ("자가 테스트 방");
        core.home().createRoom (c, [] (core::Result<core::RoomDetail>) {});
    }, 0);
    waitUntil (TXT ("방에 들어가 합주 화면"), [this] { return inRoom() && jam() != nullptr && jam()->isShowing(); });
    wait (300);
}

void UiSelfTest::closeToasts()
{
    add ([this] { for (auto* t : root.toasts) root.removeToast (t); }, 0);
    waitUntil (TXT ("토스트 닫힘"), [this] { return root.toasts.isEmpty(); });
}

void UiSelfTest::minimize()
{
    // 홈·사이드바 디자인 v2: 방 화면 윗줄의 로비 보기 버튼은 없다. 사이드바 "공개 방" 을 누르면 방은 그대로 두고 공개 방으로 (최소화.
    // v2.1 최신: 메뉴에 홈이 없다). 창이 좁아 방 화면에서 사이드바가 숨으면(플러그인 900) 예전 윗줄의 로비 보기
    add ([this]
    {
        if (auto* j = jam(); j != nullptr && j->lobby.isVisible()) { click (j->lobby); return; }
        const auto r = root.sidebar->devRow (TXT ("공개 방"));
        expect (root.sidebar->isVisible() && ! r.isEmpty(), TXT ("사이드바 \"공개 방\""));
        click (*root.sidebar, r.getCentre());
    }, 0);
    // 홈·사이드바 디자인: 최소화 막대 없이 사이드바 "참여 중" 항목이 방을 보여 준다
    waitUntil (TXT ("최소화 (홈 + 사이드바 참여 중)"), [this]
    {
        const auto& s = core.store().state();
        return root.isMinimized() && home().isShowing() && ! home().hasRoomBar()
            && s.currentRoom && ! root.sidebar->devRow (s.currentRoom->title).isEmpty();
    });
    wait (200);
}

void UiSelfTest::clickSidebar (const juce::String& text)
{
    add ([this, text]
    {
        const auto r = root.sidebar->devRow (text);
        expect (! r.isEmpty(), TXT ("사이드바에 \"") + text + "\"");
        if (! r.isEmpty()) click (*root.sidebar, r.getCentre());
    }, 0);
    wait (200);
}

//==============================================================================
// 4. 방 나가기: 두 번 눌러 확정 (팝업 없음)
void UiSelfTest::addLeaveTests()
{
    begin (TXT ("4 방 나가기"));
    enterNewRoom (true);
    add ([this] { click (jam()->leave); });
    add ([this]
    {
        auto* j = jam();
        expect (j->leave.isArmed() && inRoom(), TXT ("첫 클릭: 나가지 않고 확인 상태"));
        expect (j->leave.getWidth() == 32 && j->leave.getHeight() == 32, TXT ("방 나가기: 아이콘 버튼 32×32"));
        expect (j->tip.isVisible() && j->tip.getTarget() == &j->leave, TXT ("방장: 확인 상태에서 말풍선"));
        expect (j->tip.getText() == TXT ("한 번 더 누르면 나가요") && j->tip.getSubText() == TXT ("나가면 방이 닫혀요"),
                TXT ("말풍선: 방장이고 혼자면 \"나가면 방이 닫혀요\" (") + j->tip.getSubText() + ")");
        expect (j->leave.getTooltip().isEmpty(), TXT ("확인 상태: 툴팁 대신 말풍선"));
        if (j->tip.isVisible())
            expect (j->tip.getY() == j->leave.getBottom() + 6 && j->tip.getRight() == j->leave.getRight(), TXT ("툴팁: 버튼 아래 6, 오른쪽 끝 맞춤"));
        click (j->membersCard, { 40, 120 });   // 바깥
    });
    add ([this]
    {
        auto* j = jam();
        expect (! j->leave.isArmed() && inRoom(), TXT ("바깥 클릭: 원래대로"));
        expect (! j->tip.isVisible(), TXT ("바깥 클릭: 말풍선 사라짐"));
        expect (j->leave.getTooltip() == TXT ("방 나가기"), TXT ("평소 툴팁 \"방 나가기\""));
        click (j->leave);
    }, 3300);
    add ([this] { expect (! jam()->leave.isArmed() && ! jam()->tip.isVisible() && inRoom(), TXT ("3초가 지나면 원래대로 (말풍선도 사라짐)")); });

    // 방장이고 다른 사람이 있으면 "돌아올 때까지 방장 자리가 비어요" (가짜 사용자의 입장 요청을 받아 들인다)
    add ([this] { if (auto* f = core.fakeServer()) f->devJoinRequest(); }, 0);
    waitUntil (TXT ("입장 요청"), [this] { return ! core.store().state().joinRequests.empty(); });
    add ([this] { core.room().acceptJoinRequest (core.store().state().joinRequests.front().id); }, 0);
    waitUntil (TXT ("두 사람"), [this]
    {
        const auto& r = core.store().state().currentRoom;
        return r && juce::jmax (r->memberCount, (int) r->members.size()) >= 2;
    });
    add ([this] { click (jam()->leave); });
    add ([this]
    {
        auto* j = jam();
        expect (j->leave.isArmed() && j->tip.getSubText() == TXT ("돌아올 때까지 방장 자리가 비어요"),
                TXT ("말풍선: 방장이고 다른 사람이 있으면 \"돌아올 때까지 방장 자리가 비어요\" (") + j->tip.getSubText() + ")");
        click (j->leave);   // 두 번째: 나간다
    }, 0);
    waitUntil (TXT ("두 번째 클릭으로 나감"), [this] { return ! inRoom() && jam() == nullptr && home().isShowing(); });
    add ([this] { expect (! inRoom() && ! home().hasRoomBar(), TXT ("두 번째 클릭: 방에서 나가 홈")); });

    // 방장이 아니면 둘째 줄 없음 (가짜 방장의 공개 방)
    add ([this]
    {
        const auto& s = core.store().state();
        for (auto& r : s.publicRooms)
            if (! r.isApproval() && core::rooms::hasRoomFor (r, s.me ? s.me->id : juce::String()) && ! kickedRooms.contains (r.id))
            {
                core.home().joinRoom (r.id, [] (core::Result<core::JoinResult>) {});
                return;
            }
    }, 0);
    waitUntil (TXT ("가짜 방장의 방에 들어감"), [this] { return inRoom() && jam() != nullptr && jam()->isShowing(); }, 6000);
    closeToasts();
    add ([this] { click (jam()->leave); }, 300);
    add ([this]
    {
        auto* j = jam();
        expect (! core.store().state().amHost() && j->leave.isArmed() && j->tip.isVisible() && j->tip.getText() == TXT ("한 번 더 누르면 나가요")
                    && j->tip.getSubText().isEmpty(),
                TXT ("방장이 아니면 둘째 줄 없음 (") + j->tip.getSubText() + ")");
        click (j->leave);
    }, 0);
    waitUntil (TXT ("나감"), [this] { return ! inRoom() && jam() == nullptr && home().isShowing(); });
}

//==============================================================================
// 5. 방 최소화
void UiSelfTest::addMinimizeTests()
{
    begin (TXT ("5 최소화: 상태 유지"));
    enterNewRoom (true);
    auto state = std::make_shared<std::map<juce::String, juce::var>>();
    add ([this, state]
    {
        auto* j = jam();
        auto* session = core.jam();
        // 채팅 40줄 (혼자라 내 목록에만) → 위로 조금 올려 둔다
        for (int i = 1; i <= 40; ++i) session->chat().sendText (TXT ("자가 테스트 메시지 ") + juce::String (i));
        click (j->chatInput);
    }, 300);
    add ([this] { type (TXT ("draft 쓰는 중")); });
    add ([this]
    {
        if (auto* b = firstButton (root.dock->monCard)) click (*b);   // 모니터 패널 접기 (v2.1: 방 화면에 친구 패널 없음)
    }, 700);
    add ([this] { jam()->chatViewport.setViewPosition (0, 120); });   // 채팅을 위로 올려 둔다 (맨 아래가 아니면 새 메시지를 따라가지 않는다)
    add ([this, state]
    {
        auto* j = jam();
        auto* session = core.jam();
        (*state)["room"] = roomId();
        (*state)["text"] = j->chatInput.getText();
        (*state)["scroll"] = j->chatViewport.getViewPositionY();
        (*state)["monOpen"] = root.dock->monOpen;
        (*state)["phase"] = (int) session->getPhase();
        (*state)["msgs"] = (int) session->chat().getMessages().size();
        expect ((*state)["text"].toString() == TXT ("draft 쓰는 중"), TXT ("채팅 입력에 글을 써 둠"));
        expect (! root.dock->monOpen, TXT ("모니터 패널을 접어 둠"));
        expect (! j->friendsCard.isVisible(), TXT ("방 화면에 친구 패널 없음 (v2.1)"));
        expect (! j->chatOpen || j->chatCard.getHeight() == j->sideAvail, TXT ("채팅이 반주 위까지 세로 전체 (") + juce::String (j->chatCard.getHeight()) + " / " + juce::String (j->sideAvail) + ")");
        // 셀프 모니터: 끈 채로, 입장 전 듣기는 켠 채로 (최소화한 홈에서 이것으로 바뀌면 안 된다)
        session->setSelfMonitor (false);
        session->setHomeMonitor (true);
        (*state)["selfMon"] = session->isSelfMonitorAudible();
    });
    minimize();
    add ([this, state]
    {
        auto* session = core.jam();
        expect (jam() != nullptr && ! jam()->isVisible(), TXT ("합주 화면은 지우지 않고 숨김"));
        expect (roomId() == (*state)["room"].toString(), TXT ("방은 그대로"));
        // 엔진은 방에서 나오지 않는다 (나오면 idle). 가짜 서버 + 닿지 않는 AOO 에서는 connecting 일 수 있다
        expect (session->getPhase() != core::JamSession::Phase::idle && (int) session->getPhase() >= (int) (*state)["phase"],
                TXT ("오디오 연결 그대로 (단계 ") + juce::String ((int) (*state)["phase"]) + " -> " + juce::String ((int) session->getPhase()) + ")");
        expect (session->isSelfMonitorAudible() == (bool) (*state)["selfMon"] && ! session->isHomeMonitorAudible(), TXT ("셀프 모니터 같음 (끔)"));
        expect (! home().monitorTag.isVisible(), TXT ("홈의 \"내 소리 듣는 중\" 표시 없음"));
        // 최소화 중 받은 채팅 2개
        auto& chat = session->chat();
        chat.received ({ chat.group(), "u_left_preview", "id:uitest1", TXT ("최소화 중 1") });
        chat.received ({ chat.group(), "u_left_preview", "id:uitest2", TXT ("최소화 중 2") });
        root.addInfoToast (TXT ("자가 테스트 토스트"), {});
    }, 300);
    add ([this]
    {
        bool found = false;
        for (auto* t : root.toasts)
            if (t->getTitle() == TXT ("자가 테스트 토스트"))
            {
                found = true;
                expect (t->getPanelBounds().getBottom() == root.getHeight() - 12, TXT ("토스트 아래 여백 12 (막대 없음)"));
            }
        expect (found, TXT ("토스트가 뜸"));
        // 셀프 모니터를 켜도 같은 값
        core.jam()->setSelfMonitor (true);
        expect (core.jam()->isSelfMonitorAudible(), TXT ("최소화 중 셀프 모니터 켬 → 들림"));
    }, 0);
    clickSidebar (TXT ("자가 테스트 방"));   // 사이드바 참여 중 항목
    waitUntil (TXT ("참여 중 항목을 누르면 합주 화면"), [this] { return ! root.isMinimized() && jam() != nullptr && jam()->isShowing(); });
    add ([this, state]
    {
        auto* j = jam();
        auto* session = core.jam();
        expect (! home().isVisible() && ! home().hasRoomBar(), TXT ("돌아오면 홈 숨김"));
        expect (session->chat().getUnread() == 0, TXT ("돌아오면 새 채팅 읽음"));
        expect (j->chatInput.getText() == (*state)["text"].toString(), TXT ("입력 중인 글 그대로"));
        expect (std::abs (j->chatViewport.getViewPositionY() - (int) (*state)["scroll"]) <= 1,
                TXT ("채팅 스크롤 그대로 (") + (*state)["scroll"].toString() + " -> " + juce::String (j->chatViewport.getViewPositionY()) + ")");
        expect (root.dock->monOpen == (bool) (*state)["monOpen"], TXT ("접기 상태 그대로"));
        expect ((int) session->chat().getMessages().size() == (int) (*state)["msgs"] + 2, TXT ("최소화 중 받은 채팅이 목록에"));
        expect (session->isSelfMonitorAudible(), TXT ("돌아와도 셀프 모니터 같음 (켬)"));
        session->setSelfMonitor (false);
        session->setHomeMonitor (false);
    });

    // 막대의 나가기는 없다 (최소화 막대를 뺐다): 합주 화면에서 나간다
    add ([this] { click (jam()->leave); });
    add ([this] { click (jam()->leave); }, 0);
    waitUntil (TXT ("합주 화면에서 두 번 눌러 나감"), [this] { return ! inRoom() && jam() == nullptr && home().isShowing(); });

    begin (TXT ("5 최소화: 다른 방 확인 모달"));
    enterNewRoom();
    minimize();
    clickSidebar (TXT ("공개 방"));   // 방 목록은 공개 방 쪽에
    add ([this] { expect (root.page == Sidebar::Page::rooms && home().roomViewport.isShowing(), TXT ("공개 방 쪽")); });
    auto target = std::make_shared<juce::String>();
    auto pickCard = [this, target] () -> juce::Button*
    {
        const auto& s = core.store().state();
        for (auto& [room, button] : home().devRoomCards())
            if (! room.isApproval() && core::rooms::hasRoomFor (room, s.me ? s.me->id : juce::String()) && room.id != roomId())
            {
                *target = room.id;
                return button;
            }
        return nullptr;
    };
    add ([this, pickCard]
    {
        auto* b = pickCard();
        expect (b != nullptr, TXT ("바로 입장할 공개 방"));
        if (b != nullptr) click (*b);
    });
    add ([this]
    {
        expect (home().confirm != nullptr, TXT ("'바로 입장' → 확인 모달"));
        expect (confirmButton (TXT ("나가고 들어가기")) != nullptr && confirmButton (TXT ("취소")) != nullptr, TXT ("[취소] [나가고 들어가기]"));
        key (juce::KeyPress (juce::KeyPress::escapeKey));
    }, 300);
    add ([this, pickCard]
    {
        expect (home().confirm == nullptr && inRoom() && root.isMinimized(), TXT ("Esc = 취소 (방 그대로)"));
        if (auto* b = pickCard()) click (*b);
    }, 300);
    add ([this]
    {
        if (home().confirm != nullptr) click (*home().confirm, { 8, 8 });   // 바깥 (덮개)
    }, 300);
    add ([this, pickCard]
    {
        expect (home().confirm == nullptr && inRoom(), TXT ("바깥 클릭 = 취소"));
        if (auto* b = pickCard()) click (*b);
    }, 300);
    add ([this] { if (auto* ok = confirmButton (TXT ("나가고 들어가기"))) click (*ok); }, 0);
    waitUntil (TXT ("나간 뒤 다른 방으로"), [this, target] { return inRoom() && roomId() == *target && jam() != nullptr && jam()->isShowing(); }, 6000);
    add ([this] { expect (! root.isMinimized() && ! home().hasRoomBar(), TXT ("새 방은 합주 화면으로 (최소화 아님)")); });

    begin (TXT ("5 최소화: 친구 참여·코드로 입장"));
    minimize();
    add ([this]
    {
        // 다른 방에서 합주 중인 친구: 사이드바 친구 줄 → 프로필 창의 참여·요청 (홈의 "합주 중인 친구" 카드는 홈을 꺼 둬서 없다)
        const auto& s = core.store().state();
        for (auto& f : s.friends)
            if (f.status == "in_room" && f.room && (! s.currentRoom || f.room->id != s.currentRoom->id))
            {
                const auto r = root.sidebar->devRow (f.user.nickname.value_or (f.user.username));
                if (! r.isEmpty()) click (*root.sidebar, r.getCentre());
                else home().openProfile (f.user);
                return;
            }
        expect (false, TXT ("다른 방에서 합주 중인 친구"));
    }, 0);
    waitUntil (TXT ("프로필 창에 참여·요청"), [this]
    {
        auto* m = dynamic_cast<ProfileModal*> (home().modal.get());
        return m != nullptr && m->joinRoom.isVisible();
    });
    add ([this] { if (auto* m = dynamic_cast<ProfileModal*> (home().modal.get())) click (m->joinRoom); }, 300);
    add ([this]
    {
        expect (home().confirm != nullptr && confirmButton (TXT ("나가고 들어가기")) != nullptr, TXT ("친구 참여 → 확인 모달"));
        if (auto* c = confirmButton (TXT ("취소"))) click (*c);
    }, 300);
    add ([this]
    {
        expect (home().confirm == nullptr && inRoom() && root.isMinimized(), TXT ("취소: 방 그대로"));
        home().openJoinCode();
    }, 300);
    add ([this] { type ("Q7K2MX"); }, 200);
    add ([this] { if (home().modal != nullptr) if (auto* b = findButton (*home().modal, TXT ("입장"))) click (*b); }, 300);
    add ([this]
    {
        expect (home().confirm != nullptr && dynamic_cast<JoinCodeModal*> (home().modal.get()) != nullptr, TXT ("코드로 입장 → 코드 창 위에 확인 모달"));
        key (juce::KeyPress (juce::KeyPress::escapeKey));
    }, 300);
    add ([this]
    {
        expect (home().confirm == nullptr && dynamic_cast<JoinCodeModal*> (home().modal.get()) != nullptr, TXT ("Esc: 확인 모달만 닫힘 (코드 창은 남음)"));
        key (juce::KeyPress (juce::KeyPress::escapeKey));
    }, 300);
    add ([this] { expect (home().modal == nullptr && inRoom() && root.isMinimized(), TXT ("한 번 더 Esc: 코드 창 닫힘, 방 그대로")); });

    // v2.1 최신: 공개 방 머리말의 방 만들기(+ 아이콘)는 방에 있으면 숨는다 (사이드바 방 만들기와 같은 규칙)
    begin (TXT ("5 최소화: 머리말 방 만들기 숨김"));
    minimize();
    clickSidebar (TXT ("공개 방"));
    add ([this]
    {
        expect (inRoom() && root.isMinimized() && ! home().createIcon.isVisible(), TXT ("방에 있으면 머리말 방 만들기 없음"));
        core.home().leaveRoom();
    }, 300);
    waitUntil (TXT ("방에서 나감"), [this] { return ! inRoom() && home().isShowing(); }, 6000);
    add ([this]
    {
        auto& c = home().createIcon;
        expect (c.isVisible() && c.getTooltip() == TXT ("방 만들기") && c.getWidth() == 32 && c.getHeight() == 32
                    && c.getRight() == home().getWidth() - metric::padX,
                TXT ("방 밖: 머리말 방 만들기 아이콘 32×32, 오른쪽 18, 툴팁 (") + c.getBounds().toString() + ")");
        click (c);
    }, 300);
    add ([this]
    {
        expect (dynamic_cast<CreateRoomModal*> (home().modal.get()) != nullptr, TXT ("+ 아이콘 → 방 만들기 창"));
        home().closeOverlays();
    }, 0);

    begin (TXT ("5 최소화: 내보내짐·방 닫힘"));
    auto joinFakeRoom = [this, target]
    {
        add ([this, target]
        {
            const auto& s = core.store().state();
            for (auto& r : s.publicRooms)
                if (! r.isApproval() && core::rooms::hasRoomFor (r, s.me ? s.me->id : juce::String()) && r.id != *target)
                {
                    *target = r.id;
                    core.home().joinRoom (r.id, [] (core::Result<core::JoinResult>) {});
                    return;
                }
        }, 0);
        waitUntil (TXT ("가짜 방장의 방에 들어감"), [this] { return inRoom() && jam() != nullptr && jam()->isShowing(); }, 6000);
        wait (300);
    };
    joinFakeRoom();
    minimize();
    add ([this] { kickedRooms.add (roomId()); if (auto* f = core.fakeServer()) f->devKickMe(); }, 0);   // 내보내진 방은 다시 못 들어간다 (403 kicked)
    waitUntil (TXT ("최소화 중 내보내짐"), [this] { return ! inRoom(); });
    add ([this]
    {
        expect (! home().hasRoomBar() && ! root.isMinimized() && home().isShowing(), TXT ("내보내지면 막대 사라짐"));
        bool toast = false;
        for (auto* t : root.toasts) toast = toast || t->getTitle() == TXT ("방에서 내보내졌어요");
        expect (toast, TXT ("\"방에서 내보내졌어요\" 토스트"));
    }, 300);
    joinFakeRoom();
    minimize();
    add ([this] { if (auto* f = core.fakeServer()) f->devCloseMyRoom(); }, 0);
    waitUntil (TXT ("최소화 중 방 닫힘"), [this] { return ! inRoom(); });
    add ([this]
    {
        expect (! home().hasRoomBar() && ! root.isMinimized() && home().isShowing(), TXT ("방이 닫히면 막대 사라짐"));
        bool toast = false;
        for (auto* t : root.toasts) toast = toast || t->getTitle() == TXT ("방장이 방을 닫았어요");
        expect (toast, TXT ("\"방장이 방을 닫았어요\" 토스트"));
    }, 300);
}

//==============================================================================
juce::String UiSelfTest::overlaps (juce::Component& parent)
{
    juce::Array<juce::Component*> kids;
    for (auto* c : parent.getChildren())
        if (c->isVisible() && ! c->getBounds().isEmpty()) kids.add (c);
    juce::String out;
    auto name = [] (juce::Component* c)
    {
        if (auto* b = dynamic_cast<juce::Button*> (c); b != nullptr && b->getButtonText().isNotEmpty()) return b->getButtonText();
        return juce::String (typeid (*c).name()).fromLastOccurrenceOf (":", false, false).retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
    };
    for (int i = 0; i < kids.size(); ++i)
    {
        if (! parent.getLocalBounds().contains (kids[i]->getBounds()))
            out << name (kids[i]) << " 밖으로; ";
        for (int k = i + 1; k < kids.size(); ++k)
            if (kids[i]->getBounds().intersects (kids[k]->getBounds()))
                out << name (kids[i]) << "×" << name (kids[k]) << "; ";
    }
    return out;
}

// 6. 오른쪽 패널 너비 조절
void UiSelfTest::addResizeTests()
{
    begin (TXT ("6 패널 너비"));
    enterNewRoom (true);
    const bool plugin = core.isPlugin();
    const int def = core::panel::defaultWidth (plugin);
    // 끌어서 바꾼 너비는 지금 창의 최대에서 멈춘다 (플러그인 900: 240 ~ 280)
    auto widened = [this, plugin] (int w) { return core::panel::clampWidth (plugin, jam()->getWidth(), w); };
    // 멤버 행·내 오디오·모니터에서 볼륨이 60, 미터가 84 아래로 줄어든 곳 (없으면 빈 문자열)
    auto tooSmall = [this] (JamScreen& j)
    {
        juce::String small;
        auto check = [&small] (juce::Component& c, const juce::String& where)
        {
            for (auto* child : c.getChildren())
            {
                if (auto* v = dynamic_cast<DbSlider*> (child); v != nullptr && v->isVisible() && v->getWidth() < 60)
                    small << where << TXT (" 볼륨 ") << v->getWidth() << " ";
                if (auto* m = dynamic_cast<MeterBox*> (child); m != nullptr && m->isVisible() && m->getWidth() < 84)
                    small << where << TXT (" 미터 ") << m->getWidth() << " ";
            }
        };
        if (auto* list = j.memberViewport.getViewedComponent())
            for (auto* row : list->getChildren()) check (*row, TXT ("멤버 행"));
        // 내 오디오·모니터: 카드 묶음 (미터 상자는 카드 묶음의 형제)
        if (auto* d = root.dock.get())
        {
            check (d->sendCard, TXT ("내 오디오"));
            check (d->monCard, TXT ("모니터"));
            for (auto* m : std::initializer_list<juce::Component*> { &d->inputMeter, &d->outputMeter })
                if (m->isVisible() && m->getWidth() < 84) small << TXT ("미터 ") << m->getWidth() << " ";
        }
        return small;
    };
    add ([this, def]
    {
        auto* j = jam();
        auto* rz = j->devSideResizer();
        expect (j->sidePanelWidth() == def, TXT ("처음: 기본 너비 ") + juce::String (j->sidePanelWidth()));
        expect (rz->getWidth() == 18 && rz->getRight() == j->chatCard.getX() && rz->getX() == j->membersCard.getRight(), TXT ("끄는 자리: 멤버·패널 사이 18 전체"));
        expect (rz->getY() == j->headerLine() + 1 && rz->getBottom() == j->getHeight(), TXT ("끄는 자리: 본문 높이 전체"));
        expect (rz->getMouseCursor() == juce::MouseCursor (juce::MouseCursor::LeftRightResizeCursor), TXT ("커서 col-resize"));
        expect (j->devSideLineColour().isTransparent(), TXT ("선: 평소 안 보임"));
        moveTo (*rz, { 9, rz->getHeight() / 2 });
    });
    add ([this, def, widened]
    {
        auto* j = jam();
        expect (j->devSideLineColour() == col::field, TXT ("선: 호버 #D5D5D5"));
        auto* rz = j->devSideResizer();
        // 누른 채 왼쪽으로 100 (넓어진다)
        const auto p = toPeer (*rz, { 9, rz->getHeight() / 2 });
        clock += 1000;
        mouse (p, juce::ModifierKeys::leftButtonModifier);
        for (int i = 1; i <= 5; ++i) mouse (p - juce::Point<float> (20.0f * (float) i, 0.0f), juce::ModifierKeys::leftButtonModifier);
        expect (j->devSideLineColour() == col::ink, TXT ("선: 끄는 중 #1A1A1A"));
        expect (j->sidePanelWidth() == widened (def + 100), TXT ("왼쪽으로 100 끌면 +100 (최대에서 멈춤) ") + juce::String (j->sidePanelWidth()));
        mouse (p - juce::Point<float> (100.0f, 0.0f), {});
    });
    add ([this, plugin, def, widened]
    {
        auto* j = jam();
        MeonSettings settings;
        expect (core::panel::load (settings.properties(), plugin) == widened (def + 100), TXT ("놓으면 저장 (앱·플러그인 따로)"));
        auto* rz = j->devSideResizer();
        drag (*rz, { 9, rz->getHeight() / 2 }, { 9 - 2000, rz->getHeight() / 2 });   // 아주 멀리
    });
    add ([this, plugin, tooSmall]
    {
        auto* j = jam();
        const int max = core::panel::maxWidth (plugin, j->getWidth());
        expect (j->sidePanelWidth() == max, TXT ("최대에서 멈춤 ") + juce::String (j->sidePanelWidth()) + " = " + juce::String (max));
        expect (j->getWidth() - 54 - j->sidePanelWidth() >= 566, TXT ("멤버 쪽 566 이상"));
        // 가장 넓을 때 멤버 행·내 오디오·모니터가 겹치지 않고, 볼륨 60·미터 84 아래로 줄지 않는다
        juce::String bad;
        if (auto* list = j->memberViewport.getViewedComponent()) for (auto* row : list->getChildren()) bad << overlaps (*row);
        expect (bad.isEmpty(), TXT ("최대 너비: 멤버 행 겹침 없음 ") + bad);
        bad = dockOverlaps();
        expect (bad.isEmpty(), TXT ("최대 너비: 내 오디오·모니터 겹침 없음 ") + bad);
        const auto small = tooSmall (*j);
        expect (small.isEmpty(), TXT ("최대 너비: 볼륨 60·미터 84 이상 ") + small);
        auto* rz = j->devSideResizer();
        drag (*rz, { 9, rz->getHeight() / 2 }, { 9 + 2000, rz->getHeight() / 2 });
    });
    add ([this, def]
    {
        auto* j = jam();
        expect (j->sidePanelWidth() == def, TXT ("최소에서 멈춤 ") + juce::String (j->sidePanelWidth()));
        auto* rz = j->devSideResizer();
        drag (*rz, { 9, rz->getHeight() / 2 }, { 9 - 150, rz->getHeight() / 2 });
    });
    add ([this, def, widened]
    {
        auto* j = jam();
        expect (j->sidePanelWidth() == widened (def + 150), TXT ("다시 넓힘 +150 (최대에서 멈춤)"));
        doubleClick (*j->devSideResizer(), { 9, j->devSideResizer()->getHeight() / 2 });
    });
    add ([this, plugin, def]
    {
        auto* j = jam();
        MeonSettings settings;
        expect (j->sidePanelWidth() == def && core::panel::load (settings.properties(), plugin) == def, TXT ("더블클릭: 기본 너비로, 저장도 기본"));
        auto* rz = j->devSideResizer();
        drag (*rz, { 9, rz->getHeight() / 2 }, { 9 - 80, rz->getHeight() / 2 });
    });
    // 나갔다 다시 들어와도 (새 합주 화면 = 다시 켠 것과 같은 길) 저장한 너비
    add ([this] { core.home().leaveRoom(); }, 0);
    waitUntil (TXT ("방에서 나옴"), [this] { return ! inRoom() && jam() == nullptr; });
    enterNewRoom();
    add ([this, plugin, def, widened, tooSmall]
    {
        auto* j = jam();
        expect (j->sidePanelWidth() == widened (def + 80), TXT ("다시 들어와도 저장한 너비 ") + juce::String (j->sidePanelWidth()));
        // 작은 창에서는 최대로 보이고 저장값은 그대로
        j->setSideWidth (100000, true);
        MeonSettings settings;
        const int saved = core::panel::load (settings.properties(), plugin);
        expect (saved == core::panel::maxWidth (plugin, j->getWidth()), TXT ("가장 넓게 저장"));
        const auto old = j->getBounds();
        j->setBounds (old.withWidth (old.getWidth() - 200));
        expect (j->sidePanelWidth() == core::panel::maxWidth (plugin, j->getWidth()) && j->sidePanelWidth() < saved, TXT ("창이 작으면 최대로 보임"));
        // 창 최소 크기(앱 1024, 플러그인 900) 이상일 때만: 여기서는 시험을 위해 그보다 작게 줄일 수도 있다
        if (j->getWidth() >= (plugin ? 900 : 1024))
        {
            const auto small = tooSmall (*j);
            expect (small.isEmpty(), TXT ("작은 창의 최대 너비: 볼륨 60·미터 84 이상 ") + small);
        }
        MeonSettings again;
        expect (core::panel::load (again.properties(), plugin) == saved, TXT ("저장값은 그대로"));
        j->setBounds (old);
        expect (j->sidePanelWidth() == saved, TXT ("창을 다시 키우면 저장값"));
        j->setSideWidth (def, true);   // 다음 시험을 위해 기본으로
    });
}

//==============================================================================
// 7. 채팅 입력 여러 줄
void UiSelfTest::addChatInputTests()
{
    namespace ci = core::chatinput;
    begin (TXT ("7 채팅 입력"));
    enterNewRoom();
    closeToasts();   // 친구 패널이 빠져 채팅 입력이 아래로 내려왔다. 앞 섹션 토스트 두 개가 쌓이면 입력 칸을 가린다
    // 입력 줄 높이 = 카드 안쪽 아래 − (목록 아래 + 구분선 1)
    auto rowH = [this] { auto* j = jam(); return j->chatCard.contentBounds().getBottom() - (j->chatViewport.getBottom() + 1); };
    auto lastText = [this] { const auto& m = core.jam()->chat().getMessages(); return m.empty() ? juce::String() : m.back().text; };
    auto msgCount = [this] { return (int) core.jam()->chat().getMessages().size(); };
    auto bytesOf = [] (int n) { juce::String s; while ((int) s.getNumBytesAsUTF8() + 3 <= n) s << TXT ("가"); while ((int) s.getNumBytesAsUTF8() < n) s << "a"; return s; };
    auto atBottom = [this] { auto* j = jam(); return j->chatViewport.getViewPositionY() + j->chatViewport.getHeight() >= j->chatViewport.getViewedComponent()->getHeight() - 4; };
    auto shiftEnter = juce::KeyPress (juce::KeyPress::returnKey, juce::ModifierKeys::shiftModifier, 0);

    add ([this, rowH]
    {
        for (int i = 1; i <= 40; ++i) core.jam()->chat().sendText (TXT ("채우기 ") + juce::String (i));   // 목록을 채운다 (맨 아래)
        expect (rowH() == 44 && jam()->chatLines == 1, TXT ("처음: 한 줄 44 (") + juce::String (rowH()) + ")");
        click (jam()->chatInput);
    }, 300);
    add ([this] { type (TXT ("첫 줄")); });
    add ([this, shiftEnter] { key (shiftEnter); });
    add ([this] { type (TXT ("둘째 줄")); }, 300);
    add ([this, rowH, atBottom]
    {
        auto* j = jam();
        expect (j->chatInput.getText() == TXT ("첫 줄\n둘째 줄"), TXT ("Shift+Enter: 줄바꿈 (보내지 않음)"));
        expect (j->chatLines == 2 && rowH() == 64, TXT ("두 줄 64 (") + juce::String (rowH()) + ")");
        expect (j->chatSend.getBottom() == j->chatInput.getBottom() && j->chatSend.getHeight() == 20, TXT ("전송: 마지막 줄에 맞춤 (아래 정렬)"));
        expect (atBottom(), TXT ("입력이 늘어도 최신 메시지는 아래에"));
    });
    add ([this, shiftEnter] { key (shiftEnter); type (TXT ("셋째")); key (shiftEnter); type (TXT ("넷째")); key (shiftEnter); type (TXT ("다섯째")); }, 400);
    add ([this, rowH, atBottom]
    {
        auto* j = jam();
        expect (j->chatLines == 3 && rowH() == 84, TXT ("세 줄 넘으면 84 에서 멈춤 (") + juce::String (rowH()) + ")");
        juce::Viewport* vp = nullptr;
        for (auto* c : j->chatInput.editor().getChildren()) if (auto* v = dynamic_cast<juce::Viewport*> (c)) vp = v;
        expect (vp != nullptr && vp->isVerticalScrollBarShown() && vp->getViewPositionY() > 0, TXT ("칸 안에서 스크롤 (마지막 줄이 보임)"));
        if (vp != nullptr)
        {
            auto& bar = vp->getVerticalScrollBar();
            expect ((bool) bar.getProperties()["meonChatBar"] && bar.getWidth() == 10
                    && j->chatInput.getRight() == j->chatSend.getX() - 8, TXT ("스크롤바: 4px 막대 + 글자와 8 (칸 10), 전송과 8"));
        }
        expect (atBottom(), TXT ("세 줄이어도 최신 메시지는 아래에"));
        key (juce::KeyPress (juce::KeyPress::returnKey));
    }, 300);
    add ([this, rowH, lastText]
    {
        auto* j = jam();
        expect (lastText() == TXT ("첫 줄\n둘째 줄\n셋째\n넷째\n다섯째"), TXT ("Enter: 보냄 (안쪽 줄바꿈 그대로)"));
        expect (j->chatInput.getText().isEmpty() && j->chatLines == 1 && rowH() == 44, TXT ("보내면 다시 한 줄 44"));
        expect (j->devLastChatLines() == 5, TXT ("여러 줄 메시지가 줄바꿈대로 보임 (") + juce::String (j->devLastChatLines()) + ")");
        j->chatInput.setText (TXT ("  \n  안녕  \n\n"), true);
        key (juce::KeyPress (juce::KeyPress::returnKey));
    }, 300);
    add ([this, lastText]
    {
        expect (lastText() == TXT ("안녕"), TXT ("앞뒤 공백·빈 줄 지움"));
        jam()->chatInput.setText (TXT ("\n\n위\n\n아래\n\n"), true);
        key (juce::KeyPress (juce::KeyPress::returnKey));
    }, 300);
    add ([this, lastText]
    {
        expect (lastText() == TXT ("위\n\n아래"), TXT ("안쪽 빈 줄은 그대로"));
        jam()->chatInput.editor().insertTextAtCaret (TXT ("\r\n  붙여넣은\r\n글  \r\n"));   // 붙여넣기 (CRLF)
        key (juce::KeyPress (juce::KeyPress::returnKey));
    }, 300);
    add ([this, lastText, bytesOf]
    {
        expect (lastText() == TXT ("붙여넣은\n글"), TXT ("붙여넣은 글도 같은 규칙 (CRLF → 줄바꿈)"));
        jam()->chatInput.setText (bytesOf (899), true);
    }, 300);
    add ([this, bytesOf]
    {
        expect (jam()->chatCounterArea.isEmpty(), TXT ("899 바이트: 숫자 없음"));
        jam()->chatInput.setText (bytesOf (900), true);
    }, 300);
    add ([this, bytesOf]
    {
        auto* j = jam();
        expect (! j->chatCounterArea.isEmpty() && j->chatCounterArea.getRight() == j->chatSend.getX() - 8
                && j->chatCounterArea.getBottom() == j->chatSend.getBottom(), TXT ("900 바이트: \"900 / 1000\" (전송 왼쪽 8, 마지막 줄)"));
        j->chatInput.setText (bytesOf (1000), true);
        key (juce::KeyPress (juce::KeyPress::returnKey));
    }, 300);
    add ([this, bytesOf, lastText]
    {
        expect (lastText().getNumBytesAsUTF8() == 1000 && jam()->chatInput.getText().isEmpty(), TXT ("1000 바이트: 보냄"));
        jam()->chatInput.setText (bytesOf (1001), true);
    }, 300);
    auto countBefore = std::make_shared<int> (0);
    add ([this, msgCount, countBefore]
    {
        expect (ci::overLimit (ci::bytes (jam()->chatInput.getText())) && ! jam()->chatCounterArea.isEmpty(), TXT ("1001 바이트: 빨간 숫자"));
        *countBefore = msgCount();
        key (juce::KeyPress (juce::KeyPress::returnKey));
        click (jam()->chatSend);
    }, 300);
    add ([this, msgCount, countBefore]
    {
        expect (msgCount() == *countBefore && jam()->chatInput.getText().getNumBytesAsUTF8() == 1001, TXT ("1000 바이트를 넘으면 보내지 않음 (글은 그대로)"));
        jam()->chatInput.setText ({}, true);
        // 받은 여러 줄 메시지·긴 단어 (받는 쪽 표시)
        auto& chat = core.jam()->chat();
        chat.received ({ chat.group(), "u_left_preview", "id:ml1", TXT ("받은 첫 줄\n받은 둘째 줄\n받은 셋째 줄") });
    }, 300);
    add ([this]
    {
        expect (jam()->devLastChatLines() == 3, TXT ("받은 여러 줄 메시지도 줄바꿈대로"));
        core.jam()->chat().sendText (juce::String::repeatedString ("A", 200));
    }, 300);
    add ([this]
    {
        expect (jam()->devLastChatLines() > 1, TXT ("긴 단어는 줄 끝에서 끊김 (") + juce::String (jam()->devLastChatLines()) + TXT ("줄)"));
        auto& chat = core.jam()->chat();
        chat.received ({ chat.group(), "u_left_preview", "id:ml2", TXT ("받은긴단어") + juce::String::repeatedString ("B", 200) });
    }, 300);
    add ([this] { expect (jam()->devLastChatLines() > 1, TXT ("받은 긴 단어도 끊김")); });
}

//==============================================================================
// 공개 방 목록: 혼자 있던 공개 방에서 나가면 목록에 남지 않는다 (0명 방도 보이지 않는다). 닫힌 방 카드를 누르면 빠지고 토스트
void UiSelfTest::addRoomListTests()
{
    auto saved = std::make_shared<core::Room>();
    auto listed = [this] (const juce::String& id) { return core.store().state().findPublicRoom (id) != nullptr; };
    auto cardButton = [this] (const juce::String& id) -> juce::Button*
    {
        for (auto& [room, b] : home().devRoomCards()) if (room.id == id) return b;
        return nullptr;
    };
    auto hasToast = [this] (const juce::String& title)
    {
        for (auto* t : root.toasts) if (t->getTitle() == title) return true;
        return false;
    };

    begin (TXT ("방 목록: 혼자 있던 공개 방에서 나가기"));
    add ([this] { if (inRoom()) core.home().leaveRoom(); }, 0);
    waitUntil (TXT ("홈 (방 밖)"), [this] { return ! inRoom() && jam() == nullptr && home().isShowing(); });
    add ([this]
    {
        core::CreateRoom c;
        c.type = "public";
        c.entryMode = juce::String ("instant");
        c.title = TXT ("혼자 공개 방");
        core.home().createRoom (c, [] (core::Result<core::RoomDetail>) {});
    }, 0);
    waitUntil (TXT ("공개 방에 들어감"), [this] { return inRoom() && jam() != nullptr && jam()->isShowing(); });
    add ([this, saved] { *saved = *core.store().state().currentRoom; click (jam()->leave); }, 300);
    add ([this] { click (jam()->leave); }, 0);
    waitUntil (TXT ("나가서 홈"), [this] { return ! inRoom() && jam() == nullptr && home().isShowing(); });
    add ([this, saved, listed] { expect (! listed (saved->id), TXT ("나가자마자 목록에 없음")); }, 1500);
    add ([this, saved, listed, cardButton]
    {
        expect (! listed (saved->id) && cardButton (saved->id) == nullptr, TXT ("다시 받은 목록에도 없음 (카드 없음)"));
        auto zero = *saved;
        zero.id = "r_zero_selftest";
        zero.memberCount = 0;
        zero.members.clear();
        core.store().upsertPublicRoom (zero);
    }, 300);
    add ([this, listed, cardButton] { expect (! listed ("r_zero_selftest") && cardButton ("r_zero_selftest") == nullptr, TXT ("0명 방은 보이지 않음")); });

    begin (TXT ("방 목록: 닫힌 방 카드를 누르면"));
    clickSidebar (TXT ("공개 방"));   // 방 카드는 공개 방 쪽에 (홈·사이드바 디자인)
    add ([this, saved] { core.store().upsertPublicRoom (*saved); }, 300);   // rooms.removed 를 놓친 목록
    waitUntil (TXT ("닫힌 방 카드가 보임"), [saved, cardButton] { return cardButton (saved->id) != nullptr; });
    add ([this, saved, cardButton] { if (auto* b = cardButton (saved->id)) click (*b); }, 0);
    waitUntil (TXT ("\"이미 닫힌 방이에요\" 토스트"), [hasToast] { return hasToast (TXT ("이미 닫힌 방이에요")); });
    add ([this, saved, listed, cardButton]
    {
        expect (! listed (saved->id) && cardButton (saved->id) == nullptr, TXT ("목록에서 빠짐"));
        expect (root.fail == nullptr, TXT ("입장 실패 창은 뜨지 않음"));
        expect (! inRoom(), TXT ("들어가지 않음"));
    }, 300);
}

//==============================================================================
// 알림 목록의 친구 요청 수락·거절: 누르면 목록이 다시 만들어지며 그 줄이 지워진다. 그래도 그 줄의 요청 id 가 서버로 가야 한다.
// 실패하면 요청이 되돌아오고 토스트. 홈과 합주 화면이 같은 목록(NotificationList)이다
void UiSelfTest::addFriendRequestTests()
{
    auto sent = std::make_shared<juce::StringArray>();          // 가짜 서버가 받은 수락·거절 요청 ("POST /friends/requests/<id>/accept")
    auto reqs = std::make_shared<std::vector<core::FriendRequest>>();
    auto requestIds = [this]
    {
        juce::StringArray a;
        for (auto& f : core.store().state().friendRequests) a.add (f.id);
        return a;
    };
    auto isFriend = [this] (const juce::String& userId)
    {
        for (auto& f : core.store().state().friends) if (f.user.id == userId) return true;
        return false;
    };
    auto hasToast = [this] (const juce::String& title)
    {
        for (auto* t : root.toasts) if (t->getTitle() == title) return true;
        return false;
    };
    auto list = [this]() -> juce::Component*
    {
        // 방 화면 윗줄 알림 버튼(사이드바가 숨은 좁은 창)의 목록, 아니면 프로필 › 알림 (홈이 띄우는 모달 층 팝오버: 방 화면 위에도 뜬다)
        if (auto* j = jam(); j != nullptr && j->isShowing() && j->notifications != nullptr) return j->notifications.get();
        return home().notifications.get();
    };
    auto press = [this, list] (const juce::String& text) { if (auto* l = list()) if (auto* b = findButton (*l, text)) click (*b); };
    const auto failText = TXT ("친구 요청을 처리하지 못했어요. 다시 시도해 주세요");

    begin (TXT ("친구 요청: 홈 알림 목록"));
    add ([this] { if (inRoom()) core.home().leaveRoom(); }, 0);
    waitUntil (TXT ("홈 (방 밖)"), [this] { return ! inRoom() && jam() == nullptr && home().isShowing(); });
    add ([this, sent]
    {
        if (auto* f = core.fakeServer())
            f->onRequestForTest = [sent] (const core::HttpRequest& q) { if (q.path.startsWith ("/friends/requests/")) sent->add (q.method + " " + q.path); };
    }, 0);
    waitUntil (TXT ("받은 친구 요청 2개"), [this] { return core.store().state().friendRequests.size() == 2; });
    add ([this, reqs] { *reqs = core.store().state().friendRequests; home().toggleNotifications(); });
    waitUntil (TXT ("알림 목록 열림"), [list] { return list() != nullptr && list()->isShowing(); });
    add ([press] { press (TXT ("수락")); }, 0);
    waitUntil (TXT ("수락이 서버로"), [sent] { return sent->size() == 1; });
    waitUntil (TXT ("수락한 사람이 친구 목록에"), [reqs, isFriend] { return isFriend ((*reqs)[0].from.id); });
    add ([this, sent, reqs, requestIds, list]
    {
        expect ((*sent)[0] == "POST /friends/requests/" + (*reqs)[0].id + "/accept", TXT ("첫 줄 수락: 그 줄의 id (") + (*sent)[0] + ")");
        expect (requestIds() == juce::StringArray ((*reqs)[1].id), TXT ("수락한 요청만 목록에서 빠짐"));
        expect (list() != nullptr && findButton (*list(), TXT ("수락")) != nullptr, TXT ("남은 줄"));
    }, 0);
    add ([press] { press (TXT ("거절")); }, 0);
    waitUntil (TXT ("거절이 서버로"), [sent] { return sent->size() == 2; });
    add ([this, sent, reqs, requestIds, list]
    {
        expect ((*sent)[1] == "POST /friends/requests/" + (*reqs)[1].id + "/decline", TXT ("남은 줄 거절: 그 줄의 id (") + (*sent)[1] + ")");
        expect (requestIds().isEmpty() && list() != nullptr && findButton (*list(), TXT ("수락")) == nullptr, TXT ("목록 비움"));
    }, 300);

    begin (TXT ("친구 요청: 실패하면 되돌리고 토스트"));
    add ([this] { if (auto* f = core.fakeServer()) f->devFriendRequest(); }, 0);
    waitUntil (TXT ("새 친구 요청"), [this, list] { return core.store().state().friendRequests.size() == 1 && list() != nullptr && findButton (*list(), TXT ("수락")) != nullptr; });
    add ([this, reqs, press]
    {
        *reqs = core.store().state().friendRequests;
        if (auto* f = core.fakeServer())
            f->replyForTest = [] (const core::HttpRequest& q) -> std::optional<core::HttpResponse>
            {
                if (q.path.startsWith ("/friends/requests/")) return core::HttpResponse {};   // 네트워크 오류
                return std::nullopt;
            };
        press (TXT ("수락"));
    }, 0);
    waitUntil (TXT ("실패 토스트"), [hasToast, failText] { return hasToast (failText); });
    add ([this, reqs, requestIds, list, isFriend]
    {
        expect (requestIds() == juce::StringArray ((*reqs)[0].id), TXT ("요청이 목록에 되돌아옴"));
        expect (list() != nullptr && findButton (*list(), TXT ("수락")) != nullptr, TXT ("알림 목록에 줄이 다시 보임"));
        expect (! isFriend ((*reqs)[0].from.id), TXT ("친구가 되지 않음"));
        if (auto* f = core.fakeServer()) f->replyForTest = nullptr;
    }, 300);
    add ([press] { press (TXT ("수락")); }, 0);
    waitUntil (TXT ("다시 누르면 수락됨"), [reqs, isFriend] { return isFriend ((*reqs)[0].from.id); });
    add ([this, sent, reqs, requestIds]
    {
        expect (sent->size() == 4 && (*sent)[3] == "POST /friends/requests/" + (*reqs)[0].id + "/accept", TXT ("재시도: 같은 id"));
        expect (requestIds().isEmpty(), TXT ("목록 비움"));
        home().toggleNotifications();   // 닫기
    }, 300);

    // 홈·사이드바 디자인 v2: 방 화면 윗줄에 알림 버튼이 없다. 내 프로필 줄 → 메뉴 "알림" → (방은 그대로 두고) 홈 위 알림 목록
    begin (TXT ("친구 요청: 방 안에서 내 프로필 › 알림"));
    enterNewRoom();
    add ([this]
    {
        auto* j = jam();
        if (root.sidebar->isVisible())
        {
            expect (! j->lobby.isVisible() && ! j->bell.isVisible() && ! j->settings.isVisible(), TXT ("방 화면 윗줄: 로비·알림·설정 버튼 없음"));
            expect (j->leave.isVisible() && j->roomTitle.getX() == metric::padX, TXT ("방 이름은 왼쪽 18, 방 나가기는 그대로"));
        }
        else   // 창이 좁아 사이드바가 숨으면 예전 윗줄 (갈 곳이 없어지지 않게)
            expect (j->lobby.isVisible() && j->bell.isVisible() && j->settings.isVisible(), TXT ("사이드바가 숨으면 로비·알림·설정 버튼"));
        if (auto* f = core.fakeServer()) f->devFriendRequest();
    }, 0);
    waitUntil (TXT ("방 안에서 새 친구 요청"), [this] { return core.store().state().friendRequests.size() == 1; });
    add ([this, reqs]
    {
        *reqs = core.store().state().friendRequests;
        if (root.sidebar->isVisible()) click (*root.sidebar, root.sidebar->devProfileRow().getCentre());
        else click (jam()->bell);
    });
    add ([this]
    {
        if (root.profileMenu == nullptr) return;   // 알림 버튼으로 연 경우
        const auto b = root.profileMenu->box;
        click (*root.profileMenu, { b.getX() + b.getWidth() / 2, b.getY() + 6 + 16 });   // 첫 줄 "알림"
    }, 300);
    waitUntil (TXT ("알림 목록 열림 (방 화면 그대로)"), [this, list]
    {
        return list() != nullptr && list()->isShowing() && inRoom() && ! root.isMinimized() && jam() != nullptr && jam()->isShowing();
    });
    add ([press] { press (TXT ("수락")); }, 0);
    waitUntil (TXT ("방 안에서 수락"), [reqs, isFriend] { return isFriend ((*reqs)[0].from.id); });
    add ([this, sent, reqs, requestIds]
    {
        expect (sent->size() == 5 && (*sent)[4] == "POST /friends/requests/" + (*reqs)[0].id + "/accept", TXT ("알림 목록 줄 수락: 그 줄의 id"));
        expect (requestIds().isEmpty(), TXT ("목록 비움"));
        if (auto* f = core.fakeServer()) f->onRequestForTest = nullptr;
    }, 300);
}

//==============================================================================
// 홈·사이드바 디자인 v2.1: 접은 사이드바의 아이콘 순서와 동작. 방 칸은 방이 없으면 방 만들기, 있으면 그 방 (입장 요청 점).
// 친구 칸은 친구 추가 (펼친 "친구 +" 와 같은 창). 접고 펴는 폭은 애니메이션 (180ms)
void UiSelfTest::addRoomSidebarTests()
{
    // 방 화면에 들어가도 사이드바는 사용자가 둔 상태 그대로 (펼침/접힘). 창 폭이 좁을 때만 잠시 접힌다
    // (독립 앱 1180 미만). 독립 앱에서는 사이드바가 통째로 숨지 않는다 (숨김은 좁은 플러그인 창 900~915 만)
    auto expectRoomSidebar = [this] (bool userCollapsed, const juce::String& how)
    {
        const int w = root.getWidth();
        const bool plugin = core.isPlugin();
        const int need = core::panel::chrome + core::panel::membersMin + core::panel::defaultWidth (plugin);
        const bool hidden = plugin && w - Sidebar::closedW < need;
        const bool narrow = w - Sidebar::openW < need;
        const int want = hidden ? 0 : ((userCollapsed || narrow) ? Sidebar::closedW : Sidebar::openW);
        std::cerr << "UITEST INFO room sidebar (" << how << ") window " << w << (plugin ? " plugin" : " app")
                  << " visible=" << root.sidebar->isVisible() << " width=" << root.sidebar->currentWidth() << " want=" << want << std::endl;
        expect (root.sidebar->isVisible() == ! hidden, how + TXT (": 사이드바 ") + (hidden ? TXT ("숨김 (좁은 플러그인 창)") : TXT ("보임")));
        if (! hidden)
            expect (root.sidebar->currentWidth() == want, how + TXT (": 폭 ") + juce::String (want) + TXT (" (지금 ") + juce::String (root.sidebar->currentWidth()) + ")");
        if (! hidden && want == Sidebar::closedW)
            expect (inRoom() && ! root.sidebar->devRow (TXT ("공개 방")).isEmpty() && ! root.sidebar->devRow (core.store().state().currentRoom->title).isEmpty(),
                    how + TXT (": 접혀도 아이콘 줄 (공개 방·방)"));
        expect (jam() != nullptr && jam()->getX() == (hidden ? 0 : root.sidebar->currentWidth()), how + TXT (": 방 화면은 사이드바 오른쪽"));
    };

    begin (TXT ("사이드바: 방 화면에서 상태 유지"));
    add ([this] { if (inRoom()) core.home().leaveRoom(); }, 0);
    waitUntil (TXT ("홈 (방 밖)"), [this] { return ! inRoom() && jam() == nullptr && home().isShowing(); });
    add ([this] { root.sidebar->setCollapsed (false); }, 0);
    waitUntil (TXT ("펼침 (240)"), [this] { return root.sidebar->currentWidth() == Sidebar::openW; });
    // 마우스 대신 공개 방 카드의 "바로 입장" 과 같은 기능을 부른다 (HomeScreen::enterRoom → joinRoom). macOS 러너에서는
    // 가짜 마우스 클릭이 먹지 않는 곳이 있어, 창 폭별 확인(CI)이 클릭에 기대지 않게 한다. 카드 클릭은 방 목록 시험이 본다
    auto joinable = [this]
    {
        const auto& s = core.store().state();
        for (auto& r : s.publicRooms)
            if (! r.isApproval() && core::rooms::hasRoomFor (r, s.me ? s.me->id : juce::String()) && ! kickedRooms.contains (r.id)) return r.id;
        return juce::String();
    };
    waitUntil (TXT ("바로 입장할 공개 방"), [joinable] { return joinable().isNotEmpty(); });
    add ([this, joinable] { const auto id = joinable(); if (id.isNotEmpty()) core.home().joinRoom (id, [] (core::Result<core::JoinResult>) {}); }, 0);
    waitUntil (TXT ("공개 방에서 바로 입장 → 방 화면"), [this] { return inRoom() && jam() != nullptr && jam()->isShowing(); }, 6000);
    wait (600);   // 접히는 애니메이션(180ms)이 끝난 뒤
    add ([this, expectRoomSidebar] { expectRoomSidebar (false, TXT ("펼친 채 입장")); }, 0);
    add ([this] { if (inRoom()) core.home().leaveRoom(); }, 0);
    waitUntil (TXT ("나감"), [this] { return ! inRoom() && home().isShowing(); });
    add ([this] { root.sidebar->setCollapsed (true); }, 0);
    waitUntil (TXT ("접힘 (56)"), [this] { return root.sidebar->currentWidth() == Sidebar::closedW; });
    enterNewRoom();
    wait (600);
    add ([this, expectRoomSidebar] { expectRoomSidebar (true, TXT ("접은 채 입장")); }, 0);
    add ([this] { if (inRoom()) core.home().leaveRoom(); }, 0);
    waitUntil (TXT ("나감"), [this] { return ! inRoom() && home().isShowing(); });
    add ([this] { root.sidebar->setCollapsed (false); }, 0);
    waitUntil (TXT ("다시 펼침"), [this] { return root.sidebar->currentWidth() == Sidebar::openW; });
}

void UiSelfTest::addSidebarTests()
{
    // 방 화면에 친구 패널이 없으므로(v2.1) 방 안 초대는 프로필 창의 "방 초대" 로 한다.
    // 친구 패널의 "초대" 와 같은 동작: 누르면 3초 동안 "취소", 그 뒤 "초대됨"
    begin (TXT ("프로필: 방 초대 (v2.1)"));
    auto friendId = std::make_shared<juce::String>();
    enterNewRoom();
    waitUntil (TXT ("초대할 수 있는 친구 (온라인·방 밖)"), [this, friendId]
    {
        for (auto& f : core.store().state().friends)
            if (f.canInvite) { *friendId = f.user.id; return true; }
        return false;
    }, 6000);
    add ([this, friendId]
    {
        auto* j = jam();
        expect (j != nullptr && ! j->friendsCard.isVisible(), TXT ("방 화면에 친구 패널 없음"));
        const auto* f = core.store().state().findFriend (*friendId);
        if (j == nullptr || f == nullptr) return;
        // 사이드바 친구 줄을 누른다 (좁은 플러그인 창에서 사이드바가 숨으면 같은 길인 openProfile)
        const auto row = root.sidebar->isVisible() ? root.sidebar->devRow (f->user.nickname.value_or (f->user.username)) : juce::Rectangle<int>();
        if (! row.isEmpty()) click (*root.sidebar, row.getCentre());
        else j->openProfile (*friendId);
    }, 0);
    auto profileModal = [this] { auto* j = jam(); return j != nullptr ? dynamic_cast<ProfileModal*> (j->modal.get()) : nullptr; };
    waitUntil (TXT ("프로필 창에 \"방 초대\""), [profileModal] { auto* m = profileModal(); return m != nullptr && m->invite.isVisible(); });
    add ([this, profileModal]
    {
        auto* m = profileModal();
        if (m == nullptr) return;
        expect (m->invite.getButtonText() == TXT ("방 초대"), TXT ("버튼 글자 \"방 초대\""));
        expect (! m->request.isVisible() && ! m->joinRoom.isVisible(), TXT ("아래 버튼은 \"방 초대\" 하나"));
        click (m->invite);
    }, 200);
    add ([this, profileModal, friendId]
    {
        auto* m = profileModal();
        if (m == nullptr) { expect (false, TXT ("프로필 창이 그대로")); return; }
        using P = core::HomeController::InviteState::Phase;
        expect (core.home().inviteState (*friendId).phase == P::pending && m->invite.getButtonText() == TXT ("취소"), TXT ("누르면 3초 동안 \"취소\""));
    });
    waitUntil (TXT ("3초 뒤 \"초대됨\""), [this, profileModal, friendId]
    {
        auto* m = profileModal();
        using P = core::HomeController::InviteState::Phase;
        return m != nullptr && core.home().inviteState (*friendId).phase == P::sent && m->invite.getButtonText() == TXT ("초대됨");
    }, 6000);
    add ([this] { if (auto* j = jam()) j->closeOverlays(); }, 200);
    add ([this] { if (inRoom()) core.home().leaveRoom(); }, 0);
    waitUntil (TXT ("홈 (방 밖)"), [this] { return ! inRoom() && jam() == nullptr && home().isShowing(); });

    // v2.1 최신: 창 윗부분 배치 (chromeVals A안). 맥 창 모드 = 신호등 오른쪽 접기 버튼(신호등 끝 + 12, 디자인 84)·로고 아래 줄(32)·
    // 접히면 제목 줄 왼쪽 = 접기 버튼 - 12 (디자인 72). 미리 보기(--chrome=mac)의 신호등은 macOS 26 자리라 91·79,
    // 신호등 없음(맥 전체 화면·윈도우·리눅스) = 펼침 접기 버튼 196·맨 윗줄 42, 접힘 12·52, 제목 줄 왼쪽 18. 접는 동안 같이 움직인다
    begin (TXT ("사이드바: 창 배치 (v2.1 최신)"));
    add ([this] { if (inRoom()) core.home().leaveRoom(); }, 0);
    waitUntil (TXT ("홈 (방 밖)"), [this] { return ! inRoom() && jam() == nullptr && home().isShowing(); });
    add ([this] { root.sidebar->setCollapsed (false); }, 0);
    waitUntil (TXT ("펼침 (240)"), [this] { return root.sidebar->currentWidth() == Sidebar::openW; });
    auto chromeCheck = [this] (const juce::String& how, int toggleX, int bodyTop, int titleInset)
    {
        auto* sb = root.sidebar.get();
        const auto ta = root.toggle->getBounds();
        expect (root.toggle->isVisible() && ta == juce::Rectangle<int> (toggleX, 10, 32, 32),
                how + TXT (": 접기 버튼 (") + juce::String (toggleX) + TXT (", 10) (지금 ") + ta.toString() + ")");
        expect (sb->viewport.getY() == bodyTop, how + TXT (": 메뉴 시작 ") + juce::String (bodyTop) + TXT (" (지금 ") + juce::String (sb->viewport.getY()) + ")");
        expect (sb->headerInset() == titleInset && home().titleInset == titleInset,
                how + TXT (": 제목 줄 왼쪽 ") + juce::String (titleInset) + TXT (" (지금 ") + juce::String (home().titleInset) + ")");
        expect (sb->devRow (TXT ("홈")).isEmpty(), how + TXT (": 메뉴에 홈 없음"));
    };
    add ([this, chromeCheck]
    {
       #if JUCE_MAC
        const auto native = Sidebar::Chrome::macWindow;   // 맥 독립 앱 창 모드 (전체 화면이 아니면)
       #else
        const auto native = Sidebar::Chrome::flat;        // 윈도우·리눅스: 제목 줄은 그대로, 신호등 없는 배치
       #endif
        expect (root.chromeMode() == (core.isPlugin() ? Sidebar::Chrome::flat : native), TXT ("이 플랫폼의 창 배치 (플러그인은 신호등 없는 배치)"));
        root.devForceChrome (Sidebar::Chrome::macWindow);
        chromeCheck (TXT ("맥 창 모드 펼침"), AppRoot::mockLightsRight + 12, 52 + 32, 18);
        root.sidebar->setCollapsed (true);
    }, 0);
    add ([this]
    {
        // 접는 중: 로고 줄·제목 줄 여백이 끝 값 사이이고, 마지막 프레임의 폭과 같은 진행값으로 움직인다
        auto* sb = root.sidebar.get();
        const float e = (float) (sb->getWidth() - Sidebar::closedW) / (float) (Sidebar::openW - Sidebar::closedW);   // 마지막 프레임
        const int logoRow = sb->viewport.getY() - 52, inset = home().titleInset;
        expect (e > 0.02f && e < 0.98f, TXT ("접는 중 (진행값 ") + juce::String (e, 2) + ")");
        expect (std::abs (logoRow - juce::roundToInt (32.0f * e)) <= 1 && std::abs (inset - juce::roundToInt (18.0f + (float) (AppRoot::mockLightsRight - 18) * (1.0f - e))) <= 1,
                TXT ("접는 중: 폭 ") + juce::String (sb->getWidth()) + TXT (", 로고 줄 ") + juce::String (logoRow) + TXT (", 제목 줄 왼쪽 ") + juce::String (inset));
    }, 60);
    waitUntil (TXT ("접힘 (56)"), [this] { return root.sidebar->currentWidth() == Sidebar::closedW; });
    add ([this, chromeCheck]
    {
        chromeCheck (TXT ("맥 창 모드 접힘"), AppRoot::mockLightsRight + 12, 52, AppRoot::mockLightsRight);
        root.devForceChrome (Sidebar::Chrome::flat);
        chromeCheck (TXT ("신호등 없음 접힘"), 12, 52, 18);
        root.sidebar->setCollapsed (false);
    }, 0);
    add ([this]
    {
        // 펼치는 중 (신호등 없음): 접기 버튼 12 → 196, 맨 윗줄 52 → 42 가 폭과 같은 진행값으로 움직인다
        auto* sb = root.sidebar.get();
        const float e = (float) (sb->getWidth() - Sidebar::closedW) / (float) (Sidebar::openW - Sidebar::closedW);
        const int tx = root.toggle->getX(), top = sb->viewport.getY();
        expect (e > 0.02f && e < 0.98f, TXT ("펼치는 중 (진행값 ") + juce::String (e, 2) + ")");
        expect (std::abs (tx - juce::roundToInt (12.0f + 184.0f * e)) <= 1 && std::abs (top - juce::roundToInt (52.0f - 10.0f * e)) <= 1,
                TXT ("펼치는 중: 폭 ") + juce::String (sb->getWidth()) + TXT (", 접기 버튼 x ") + juce::String (tx) + TXT (", 맨 윗줄 ") + juce::String (top));
    }, 60);
    waitUntil (TXT ("펼침 (240)"), [this] { return root.sidebar->currentWidth() == Sidebar::openW; });
    add ([this, chromeCheck]
    {
        chromeCheck (TXT ("신호등 없음 펼침"), 196, 42, 18);
        root.devResetChrome();
    }, 0);

    // 참여 중·친구 섹션 접기 (디자인: 내용 높이 grid-template-rows 1fr ↔ 0fr, 화살표 0° ↔ -90°, 180ms 같은 곡선)
    begin (TXT ("사이드바: 섹션 접기 애니메이션"));
    add ([this]
    {
        auto* sb = root.sidebar.get();
        const auto f = sb->devSection ("friends");
        expect (f.full > 0 && f.visible == f.full && std::abs (f.angle) < 0.001f, TXT ("친구: 펼침 (") + juce::String (f.visible) + "/" + juce::String (f.full) + ")");
        // 친구 머리말 누름
        const auto h = sb->devSectionHeader (TXT ("친구"));
        expect (! h.isEmpty(), TXT ("친구 머리말"));
        if (! h.isEmpty()) click (*sb, h.getPosition().translated (12, 14));
    }, 0);
    // 접는 중 (180ms): 높이와 화살표가 끝 값 사이 (같은 진행값). 프레임 사이에서 한 번은 보인다
    waitUntil (TXT ("접는 중: 높이·화살표가 끝 값 사이"), [this]
    {
        const auto f = root.sidebar->devSection ("friends");
        const float e = (float) f.visible / (float) juce::jmax (1, f.full);
        const float a = -juce::MathConstants<float>::halfPi * (1.0f - e);
        return f.visible > 0 && f.visible < f.full && f.angle < -0.0001f && f.angle > -juce::MathConstants<float>::halfPi + 0.0001f
            && std::abs (f.angle - a) < 0.05f;
    }, 1000);
    wait (400);
    add ([this]
    {
        const auto f = root.sidebar->devSection ("friends");
        expect (f.visible == 0 && std::abs (f.angle + juce::MathConstants<float>::halfPi) < 0.001f, TXT ("접힘: 높이 0, 화살표 -90°"));
        if (const auto h = root.sidebar->devSectionHeader (TXT ("친구")); ! h.isEmpty()) click (*root.sidebar, h.getPosition().translated (12, 14));
    }, 400);
    add ([this]
    {
        const auto f = root.sidebar->devSection ("friends");
        expect (f.visible == f.full && std::abs (f.angle) < 0.001f, TXT ("다시 펼침"));
        const auto r = root.sidebar->devSection ("rooms");
        expect (r.visible == r.full, TXT ("참여 중: 펼침"));
    });

    // 방에 있을 때 사이드바 친구 줄 "초대" (방 화면에서 뺀 친구 패널의 초대를 대신한다. 디자인 screen=방)
    begin (TXT ("사이드바: 친구 줄 초대 (v2.1 최신)"));
    auto invitee = std::make_shared<juce::String>();
    enterNewRoom();
    waitUntil (TXT ("초대할 수 있는 친구 (온라인·방 밖)"), [this, invitee]
    {
        for (auto& f : core.store().state().friends)
            if (f.canInvite && f.status == "online" && core.home().inviteState (f.user.id).phase == core::HomeController::InviteState::Phase::none)
            { *invitee = f.user.id; return true; }
        return false;
    }, 6000);
    add ([this, invitee]
    {
        auto* sb = root.sidebar.get();
        auto* b = sb->devInviteButton (*invitee);
        const bool sidebarOpen = sb->isVisible() && ! sb->isCollapsed();
        if (! sidebarOpen) { expect (b == nullptr || ! b->isVisible(), TXT ("접힌 사이드바에는 초대 버튼 없음")); return; }
        expect (b != nullptr && b->isVisible() && b->getButtonText() == TXT ("초대"), TXT ("온라인 친구 줄 오른쪽 \"초대\""));
        // 합주 중인 친구(같은 방·다른 방)는 버튼 없음
        for (auto& f : core.store().state().friends)
            if (f.status == "in_room")
                expect (sb->devInviteButton (f.user.id) == nullptr, TXT ("합주 중인 친구는 초대 버튼 없음 (") + f.user.nickname.value_or (f.user.username) + ")");
        if (b != nullptr) click (*b);
    }, 200);
    add ([this, invitee]
    {
        auto* b = root.sidebar->devInviteButton (*invitee);
        if (b == nullptr) return;
        using P = core::HomeController::InviteState::Phase;
        expect (core.home().inviteState (*invitee).phase == P::pending && b->getButtonText() == TXT ("취소") && b->getTooltip().contains (TXT ("초 뒤 초대가 가요")),
                TXT ("누르면 3초 동안 \"취소\" (") + b->getTooltip() + ")");
    }, 300);
    waitUntil (TXT ("3초 뒤 \"초대됨\""), [this, invitee]
    {
        auto* b = root.sidebar->devInviteButton (*invitee);
        using P = core::HomeController::InviteState::Phase;
        return b == nullptr || (core.home().inviteState (*invitee).phase == P::sent && b->getButtonText() == TXT ("초대됨"));
    }, 6000);
    add ([this] { if (inRoom()) core.home().leaveRoom(); }, 0);
    waitUntil (TXT ("홈 (방 밖)"), [this] { return ! inRoom() && jam() == nullptr && home().isShowing(); });

    begin (TXT ("사이드바: 접은 상태 (v2.1)"));
    add ([this] { if (inRoom()) core.home().leaveRoom(); }, 0);
    waitUntil (TXT ("홈 (방 밖)"), [this] { return ! inRoom() && jam() == nullptr && home().isShowing(); });
    add ([this] { root.sidebar->setCollapsed (true); }, 0);
    waitUntil (TXT ("접힘 (56)"), [this] { return root.sidebar->currentWidth() == Sidebar::closedW; });
    add ([this]
    {
        auto* sb = root.sidebar.get();
        const auto rooms = sb->devRow (TXT ("공개 방")), code = sb->devRow (TXT ("코드로 입장"));
        const auto make = sb->devRow (TXT ("방 만들기")), add_ = sb->devRow (TXT ("친구 추가"));
        expect (sb->devRow (TXT ("홈")).isEmpty(), TXT ("메뉴에 홈 없음 (v2.1 최신 homeMenu 숨김)"));
        expect (! rooms.isEmpty() && ! make.isEmpty() && ! add_.isEmpty(), TXT ("아이콘: 공개 방·코드로 입장·방 만들기·친구 추가"));
        expect (rooms.getY() < code.getY() && code.getY() < make.getY() && make.getY() < add_.getY(), TXT ("순서: 메뉴 → 방 칸 → 친구 추가"));
        expect (code.getBottom() + 2 + 10 + 9 == make.getY() && make.getBottom() + 2 + 10 + 9 == add_.getY(), TXT ("간격: 구분선 위 -2, 아래 8 (v2.1)"));
        click (*sb, make.getCentre());
    });
    add ([this]
    {
        expect (dynamic_cast<CreateRoomModal*> (home().modal.get()) != nullptr, TXT ("방 만들기 아이콘 → 방 만들기 창"));
        home().closeOverlays();   // (방 이름 칸에 포커스가 있으면 Esc 가 창을 닫지 않는다: main 과 같은 기존 동작)
    }, 400);
    add ([this] { click (*root.sidebar, root.sidebar->devRow (TXT ("친구 추가")).getCentre()); }, 300);
    add ([this]
    {
        expect (home().modal != nullptr && dynamic_cast<CreateRoomModal*> (home().modal.get()) == nullptr
                && findButton (*home().modal, TXT ("요청 보내기")) != nullptr, TXT ("친구 추가 아이콘 → 친구 추가 창"));
        home().closeOverlays();
    }, 400);
    enterNewRoom (true);
    add ([this]
    {
        const auto& s = core.store().state();
        expect (root.sidebar->devRow (TXT ("방 만들기")).isEmpty() && ! root.sidebar->devRow (s.currentRoom->title).isEmpty(), TXT ("방에 있으면 방 칸 = 참여 중인 방"));
        expect (root.sidebar->currentWidth() == Sidebar::closedW, TXT ("방 화면에서도 접힌 채"));
        if (auto* f = core.fakeServer()) f->devJoinRequest();
    }, 0);
    waitUntil (TXT ("입장 요청"), [this] { return ! core.store().state().joinRequests.empty(); });
    add ([this]
    {
        expect (core.store().state().amHost(), TXT ("방장"));
        // 방은 그대로, 공개 방으로 (좁은 플러그인 창은 방 화면에서 사이드바가 숨으므로 예전 로비 보기)
        if (root.sidebar->isVisible()) click (*root.sidebar, root.sidebar->devRow (TXT ("공개 방")).getCentre());
        else click (jam()->lobby);
    });
    add ([this]
    {
        const auto& s = core.store().state();
        expect (root.isMinimized(), TXT ("공개 방 아이콘: 방은 그대로 공개 방으로"));
        click (*root.sidebar, root.sidebar->devRow (s.currentRoom->title).getCentre());
    }, 300);
    waitUntil (TXT ("방 아이콘 → 방 화면"), [this] { return ! root.isMinimized() && jam() != nullptr && jam()->isShowing(); });
    add ([this] { click (jam()->leave); }, 300);
    add ([this] { click (jam()->leave); }, 0);
    waitUntil (TXT ("나감"), [this] { return ! inRoom() && home().isShowing(); });
    add ([this] { root.sidebar->setCollapsed (false); }, 0);
    waitUntil (TXT ("다시 펼침 (240)"), [this] { return root.sidebar->currentWidth() == Sidebar::openW; });
}

//==============================================================================
// AOO 서버에 닿지 못하는 곳에서 방을 빠르게 나갔다 들어가기 (E8·E9). 이름을 찾을 수 없는 AOO 서버로 보낸다.
// E9 전에는 AOO 가 이름 조회 실패를 연결된 것으로 보고 연결되지 않은 소켓에 썼다 (E8 없이는 SIGPIPE 로 앱이 끝났다).
// 이제는 붙기 실패로 오고, 앱은 끊김("연결이 끊겼어요")을 보이고 3초마다 다시 시도한다
juce::String UiSelfTest::describe (const meon::chrome::Probe& p)
{
    const auto box = [] (const double (&b)[4]) { return "(" + juce::String (b[0], 1) + "," + juce::String (b[1], 1) + " " + juce::String (b[2], 1) + "x" + juce::String (b[3], 1) + ")"; };
    return "macOS " + juce::String (p.os) + " installed=" + juce::String ((int) p.installed) + " fullscreen=" + juce::String ((int) p.fullScreen)
         + " toolbar=" + juce::String ((int) p.toolbar) + " unified=" + juce::String ((int) p.unified) + " fullsize=" + juce::String ((int) p.fullSizeContent)
         + " bar=" + juce::String (p.barH, 1)
         + " close=" + box (p.lights[0]) + " mini=" + box (p.lights[1]) + " zoom=" + box (p.lights[2])
         + " hit[toggle]=" + juce::String (p.hitToggle) + " hit[empty]=" + juce::String (p.hitEmpty) + " hit[close]=" + juce::String (p.hitClose)
         + " forwarded=" + juce::String (p.forwarded) + " presses=" + juce::String (p.titleBarPresses);
}

void UiSelfTest::addChromeTests()
{
    // 맥 독립 앱 창: 빈 툴바 + unified 스타일이면 macOS 가 신호등을 52pt 윗줄의 세로 가운데에 놓는다 (버튼을 직접 옮기지 않는다).
    // 창 크기·위치·사이드바 접기·앱 전환·더블클릭 확대·전체 화면 전후에 신호등이 같은 자리인지, 맨 윗줄 누름이 앱 화면으로 오는지 본다.
    // 마우스 누름은 AppKit 이벤트 줄에 넣는다 (실제 클릭과 같은 길: 창 → 누를 뷰 찾기 → 앱 화면)
    begin (TXT ("맥 창: 신호등 (빈 툴바 + unified)"));
   #if ! JUCE_MAC
    add ([this]
    {
        // 맥에서 "빈 곳"으로 누르는 점이 공개 방 제목 줄의 빈 곳인지만 본다 (JUCE 쪽 판정은 플랫폼과 같다)
        const int x = root.sidebar->currentWidth() + (root.getWidth() - root.sidebar->currentWidth()) / 2;
        auto* hit = root.getComponentAt (juce::Point<int> (x, 26));
        expect (hit == root.home.get(), TXT ("제목 줄 가운데는 빈 곳 (") + (hit != nullptr ? juce::String (typeid (*hit).name()) : TXT ("없음")) + ")");
        std::cerr << "UITEST INFO chrome: macOS 가 아니라 나머지는 건너뜀" << std::endl;
    }, 0);
   #else
    if (core.isPlugin()) { add ([] { std::cerr << "UITEST INFO chrome: 플러그인이라 건너뜀" << std::endl; }, 0); return; }
    struct Shared { meon::chrome::Probe first; juce::Rectangle<int> bounds; int presses = 0; bool wasCollapsed = false, fullScreen = false; };
    auto sh = std::make_shared<Shared>();
    auto handle = [this]() -> void* { auto* p = peer(); return p != nullptr ? p->getNativeHandle() : nullptr; };
    // 창 왼쪽 위 기준 점 (peer 좌표 = 창 좌표: 내용이 창 맨 위까지)
    auto togglePoint = [this]() -> juce::Point<int> { return toPeer (*root.toggle, centre()).roundToInt(); };
    auto emptyPoint = [this]() -> juce::Point<int>
    {
        // 공개 방 제목 줄의 빈 곳 (제목과 오른쪽 버튼 사이)
        const int x = root.sidebar->currentWidth() + (root.getWidth() - root.sidebar->currentWidth()) / 2;
        return root.getPeer() != nullptr ? root.getPeer()->getComponent().getLocalPoint (&root, juce::Point<int> (x, 26)) : juce::Point<int>();
    };
    auto probeNow = [this, handle, togglePoint, emptyPoint]
    {
        const auto t = togglePoint();
        const auto e = emptyPoint();
        return meon::chrome::probe (handle(), t.x, t.y, e.x, e.y);
    };
    auto log = [] (const juce::String& when, const meon::chrome::Probe& p) { std::cerr << "UITEST INFO chrome " << when << ": " << describe (p) << std::endl; };
    auto same = [this, sh, log] (const juce::String& when, const meon::chrome::Probe& p)
    {
        log (when, p);
        bool ok = true;
        for (int i = 0; i < 3; ++i)
            for (int k = 0; k < 4; ++k)
                ok = ok && std::abs (p.lights[i][k] - sh->first.lights[i][k]) < 0.5;
        expect (ok, when + TXT (": 신호등이 처음과 같은 자리"));
    };
    auto peerBounds = [this] { return peer() != nullptr ? peer()->getBounds() : juce::Rectangle<int>(); };

    closeToasts();
    add ([this, sh, probeNow, log, togglePoint]
    {
        if (root.sidebar->isCollapsed()) root.sidebar->setCollapsed (false);
        const auto p = probeNow();
        sh->first = p;
        log (TXT ("시작"), p);
        expect (p.installed && p.fullSizeContent && ! p.fullScreen, TXT ("제목 줄 없음 (내용이 창 맨 위까지)"));
        expect (p.toolbar, TXT ("빈 툴바"));
        const bool modern = juce::SystemStats::getOperatingSystemType() >= juce::SystemStats::MacOS_11;
        if (modern)
        {
            expect (p.unified, TXT ("툴바 스타일 unified"));
            expect (std::abs (p.barH - 52.0) < 0.5, TXT ("윗줄 높이 52 (지금 ") + juce::String (p.barH, 1) + ")");
        }
        bool centred = true;
        for (auto& b : p.lights) centred = centred && std::abs ((b[1] + b[3] * 0.5) - p.barH * 0.5) <= 1.0;
        expect (centred, TXT ("신호등이 윗줄 세로 가운데"));
        expect (p.lights[0][0] < p.lights[1][0] && p.lights[1][0] < p.lights[2][0], TXT ("닫기·최소화·확대 순서"));
        expect (root.chromeMode() == Sidebar::Chrome::macWindow, TXT ("사이드바: 맥 창 배치"));
        const auto t = root.toggle->getBounds();
        const int right = (int) std::ceil (p.lights[2][0] + p.lights[2][2]);
        expect (t.getX() == right + 12, TXT ("접기 버튼 = 신호등 끝 ") + juce::String (right) + " + 12 (" + juce::String (t.getX()) + ")");
        std::cerr << "UITEST INFO chrome 신호등 간격: 버튼 틀 x " << p.lights[0][0] << " / " << p.lights[1][0] << " / " << p.lights[2][0]
                  << " (간격 " << (p.lights[1][0] - p.lights[0][0]) << "), 확대 버튼 오른쪽 끝 " << (p.lights[2][0] + p.lights[2][2])
                  << ", 접기 버튼 x " << t.getX() << " (사이 " << (t.getX() - (p.lights[2][0] + p.lights[2][2])) << ")"
                  << ", 접기 버튼 가운데 y " << togglePoint().y << std::endl;
        expect (p.hitClose == "button", TXT ("신호등 누름은 신호등이 받음 (") + juce::String (p.hitClose) + ")");
    }, 0);

    // 창 크기·위치
    add ([this, sh, peerBounds] { sh->bounds = peerBounds(); peer()->setBounds (sh->bounds.withSizeKeepingCentre (sh->bounds.getWidth() - 160, sh->bounds.getHeight() - 80), false); }, 700);
    add ([probeNow, same] { same (TXT ("창 크기 줄임"), probeNow()); }, 0);
    add ([this, sh] { peer()->setBounds (sh->bounds.translated (60, 40), false); }, 700);
    add ([probeNow, same] { same (TXT ("창 옮김"), probeNow()); }, 0);
    add ([this, sh] { peer()->setBounds (sh->bounds, false); }, 700);
    add ([probeNow, same] { same (TXT ("창 원래대로"), probeNow()); }, 0);

    // 사이드바 접기·펼치기
    add ([this] { root.sidebar->setCollapsed (true); }, 0);
    waitUntil (TXT ("접힘"), [this] { return root.sidebar->currentWidth() == Sidebar::closedW; });
    add ([this, probeNow, same]
    {
        same (TXT ("사이드바 접음"), probeNow());
        const int tx = root.toggle->getX();
        expect (home().titleInset == tx - 12, TXT ("접힌 제목 줄 왼쪽 = 접기 버튼 - 12 (") + juce::String (home().titleInset) + " / " + juce::String (tx) + ")");
    }, 100);
    add ([this] { root.sidebar->setCollapsed (false); }, 0);
    waitUntil (TXT ("펼침"), [this] { return root.sidebar->currentWidth() == Sidebar::openW; });
    add ([probeNow, same] { same (TXT ("사이드바 펼침"), probeNow()); }, 100);

    // 다른 앱으로 갔다가 돌아옴 (키 창이 바뀐다)
    add ([] { meon::chrome::devSetActive (false); }, 900);
    add ([probeNow, same] { same (TXT ("앱 비활성"), probeNow()); }, 0);
    add ([] { meon::chrome::devSetActive (true); }, 900);
    add ([probeNow, same] { same (TXT ("앱 다시 활성"), probeNow()); }, 0);

    // 맨 윗줄 누름 (AppKit 이벤트): 접기 버튼은 버튼으로, 빈 곳은 창 끌기로
    add ([this, sh, handle, togglePoint]
    {
        sh->wasCollapsed = root.sidebar->isCollapsed();
        const auto t = togglePoint();
        meon::chrome::devPostClick (handle(), t.x, t.y, 1);
    }, 0);
    waitUntilOr (TXT ("윗줄 접기 버튼 누름"), [this, sh] { return root.sidebar->isCollapsed() != sh->wasCollapsed; }, 3000,
                 [this] { expect (false, TXT ("윗줄의 접기 버튼을 눌러도 사이드바가 접히지 않음")); });
    add ([this, sh, handle, togglePoint, probeNow, log]
    {
        log (TXT ("접기 버튼 누른 뒤"), probeNow());
        if (root.sidebar->isCollapsed() != sh->wasCollapsed)
        {
            expect (true, TXT ("윗줄의 접기 버튼이 눌림"));
            const auto t = togglePoint();
            meon::chrome::devPostClick (handle(), t.x, t.y, 1);   // 다시 펼친다
        }
    }, 0);
    waitUntilOr (TXT ("다시 펼침"), [this, sh] { return root.sidebar->isCollapsed() == sh->wasCollapsed; }, 3000,
                 [this, sh] { root.sidebar->setCollapsed (sh->wasCollapsed); });
    waitUntil (TXT ("펼침 (240)"), [this] { return root.sidebar->currentWidth() == Sidebar::openW; });
    add ([this, sh, handle, emptyPoint, probeNow]
    {
        auto* hit = root.getComponentAt (root.getLocalPoint (&root.getPeer()->getComponent(), emptyPoint()));
        expect (hit == root.home.get(), TXT ("빈 곳 점이 제목 줄 빈 곳 (") + (hit != nullptr ? hit->getName() + " " + juce::String (typeid (*hit).name()) : TXT ("없음")) + ")");
        sh->presses = probeNow().titleBarPresses;
        sh->bounds = peer()->getBounds();
        const auto e = emptyPoint();
        meon::chrome::devPostClick (handle(), e.x, e.y, 1);
    }, 800);
    add ([this, sh, probeNow, same]
    {
        const auto p = probeNow();
        expect (p.titleBarPresses > sh->presses, TXT ("빈 곳 누름 → 창 끌기 (titleBarMouseDown ") + juce::String (p.titleBarPresses - sh->presses) + TXT ("번)"));
        std::cerr << "UITEST INFO chrome 빈 곳 누른 뒤 창 " << sh->bounds.toString() << " → " << peer()->getBounds().toString() << std::endl;
        same (TXT ("빈 곳 누른 뒤"), p);
    }, 0);
    // 더블클릭 → 시스템 설정(기본: 확대)
    add ([this, sh, handle, emptyPoint]
    {
        sh->bounds = peer()->getBounds();
        const auto e = emptyPoint();
        meon::chrome::devPostClick (handle(), e.x, e.y, 1);
        meon::chrome::devPostClick (handle(), e.x, e.y, 2);
    }, 0);
    waitUntilOr (TXT ("더블클릭 확대"), [this, sh] { return peer()->getBounds() != sh->bounds; }, 3000,
                 [this] { std::cerr << "UITEST INFO chrome 더블클릭 뒤 창 크기 그대로 (시스템 설정이 '아무것도 안 함'이거나 이미 최대)" << std::endl; });
    add ({}, 600);
    add ([this, sh, handle, emptyPoint, probeNow, same]
    {
        std::cerr << "UITEST INFO chrome 더블클릭 뒤 창 " << sh->bounds.toString() << " → " << peer()->getBounds().toString() << std::endl;
        same (TXT ("더블클릭 확대 뒤"), probeNow());
        if (peer()->getBounds() != sh->bounds)
        {
            const auto e = emptyPoint();
            meon::chrome::devPostClick (handle(), e.x, e.y, 1);
            meon::chrome::devPostClick (handle(), e.x, e.y, 2);
        }
    }, 0);
    waitUntilOr (TXT ("더블클릭 되돌림"), [this, sh] { return peer()->getBounds() == sh->bounds; }, 3000,
                 [this, sh] { std::cerr << "UITEST INFO chrome 두 번째 더블클릭 뒤 창 " << peer()->getBounds().toString() << " (처음 " << sh->bounds.toString() << ")" << std::endl;
                              peer()->setBounds (sh->bounds, false); });
    add ({}, 600);
    add ([probeNow, same] { same (TXT ("더블클릭 되돌린 뒤"), probeNow()); }, 0);

    // 전체 화면: 툴바를 떼고 신호등 없는 배치 → 나오면 다시 같은 자리
    add ([this, sh] { sh->bounds = peer()->getBounds(); peer()->setFullScreen (true); }, 0);
    waitUntilOr (TXT ("전체 화면"), [this] { return peer()->isFullScreen(); }, 8000,
                 [] { std::cerr << "UITEST INFO chrome 전체 화면에 들어가지 못함 (러너)" << std::endl; });
    add ({}, 1200);
    add ([this, sh, probeNow, log]
    {
        sh->fullScreen = peer()->isFullScreen();
        if (! sh->fullScreen) return;
        const auto p = probeNow();
        log (TXT ("전체 화면"), p);
        expect (! p.toolbar, TXT ("전체 화면: 툴바 뗌"));
        expect (root.chromeMode() == Sidebar::Chrome::flat, TXT ("전체 화면: 신호등 없는 배치"));
        peer()->setFullScreen (false);
    }, 0);
    waitUntilOr (TXT ("전체 화면 나옴"), [this] { return ! peer()->isFullScreen(); }, 8000,
                 [] { std::cerr << "UITEST INFO chrome 전체 화면에서 나오지 못함" << std::endl; });
    add ({}, 1500);
    add ([this, sh, probeNow, same]
    {
        if (! sh->fullScreen) return;
        const auto p = probeNow();
        same (TXT ("전체 화면에서 나온 뒤"), p);
        expect (p.toolbar && p.fullSizeContent, TXT ("나온 뒤: 툴바·제목 줄 없음 다시"));
        expect (root.chromeMode() == Sidebar::Chrome::macWindow, TXT ("나온 뒤: 맥 창 배치"));
        const auto b = peer()->getBounds();
        expect (std::abs (b.getWidth() - sh->bounds.getWidth()) <= 1 && std::abs (b.getHeight() - sh->bounds.getHeight()) <= 1,
                TXT ("나온 뒤 창 크기 그대로 (") + sh->bounds.toString() + " → " + b.toString() + ")");
    }, 0);
   #endif
}

void UiSelfTest::addAooDropTests()
{
    begin (TXT ("AOO 끊김: 빠르게 나갔다 들어가기"));
    auto oldAoo = std::make_shared<std::pair<juce::String, int>>();
    add ([this, oldAoo]
    {
        if (inRoom()) core.home().leaveRoom();
        if (auto* f = core.fakeServer())
        {
            *oldAoo = f->devAoo();
            f->devSetAoo ("aoo-selftest.invalid", 10998);   // .invalid: 이름 조회가 늘 실패한다 (RFC 2606)
        }
    }, 0);
    waitUntil (TXT ("홈 (방 밖)"), [this] { return ! inRoom() && jam() == nullptr && home().isShowing(); });
    for (int i = 0; i < 10; ++i)
    {
        enterNewRoom();
        add ([this] { core.home().leaveRoom(); }, 0);
        waitUntil (TXT ("나감"), [this] { return ! inRoom() && jam() == nullptr; });
        wait (100);
    }
    enterNewRoom();
    wait (4000);   // 끊김 표시는 2초 뒤에 뜬다 (ConnectionWatch)
    add ([this, oldAoo]
    {
        auto* s = core.jam();
        const auto toast = core.connection().toast();
        std::cerr << "UITEST INFO aoodrop phase=" << (s != nullptr ? (int) s->getPhase() : -1)
                  << " serverDown=" << (s != nullptr && s->isJamServerDown()) << " toast=" << (int) toast << std::endl;
        expect (s != nullptr && inRoom() && s->getPhase() != core::JamSession::Phase::inRoom && ! s->isEngineConnected(),
                TXT ("앱이 살아 있고 합주 서버에 붙지 못한 상태"));
        expect (s != nullptr && s->isJamServerDown() && toast == core::ConnectionWatch::Toast::lost,
                TXT ("붙지 못하면 끊김으로 보인다 (\"연결이 끊겼어요\")"));
        core.home().leaveRoom();
        if (auto* f = core.fakeServer()) f->devSetAoo (oldAoo->first, oldAoo->second);
    }, 300);
}

//==============================================================================
// 팝오버 겉모양 (PopoverSurface): 판 둘레의 그림자 자리까지 그리는 구성 요소가 덮고(잘리지 않게), 그 자리는 마우스를 받지 않는다.
// 그림자 흐림은 판 크기가 바뀔 때만 새로 계산한다 (창 크기를 바꿔 자리가 옮겨지거나 다시 그려도 그대로)
void UiSelfTest::addPopoverTests()
{
    begin (TXT ("팝오버: 그림자 (프로필 메뉴·알림)"));
    const int m = PopoverSurface::shadowMargin();
    struct Shared { juce::Rectangle<int> bounds; };
    auto sh = std::make_shared<Shared>();
    closeToasts();
    add ([this] { if (root.profileMenu == nullptr) click (*root.sidebar, root.sidebar->devProfileRow().getCentre()); }, 400);
    waitUntil (TXT ("프로필 메뉴 열림"), [this] { return root.profileMenu != nullptr; });
    add ([this, m]
    {
        auto& pm = *root.profileMenu;
        expect (pm.getBounds() == root.getLocalBounds(), TXT ("프로필 메뉴: 창 전체를 덮는 투명 판 (창 맨 위 AppRoot 의 자식)"));
        expect (pm.box.getWidth() == ProfileMenu::boxW && pm.box.getX() == 9, TXT ("프로필 메뉴: 폭 222, 왼쪽 9 (그대로)"));
        expect (root.getIndexOfChildComponent (&pm) > root.getIndexOfChildComponent (root.sidebar.get())
                 && root.getIndexOfChildComponent (&pm) > root.getIndexOfChildComponent (root.home.get()),
                TXT ("프로필 메뉴: 사이드바·본문보다 위"));
        if (root.sidebar->getWidth() > 0)
            expect (pm.box.expanded (m).getRight() > root.sidebar->getRight(),
                    TXT ("프로필 메뉴 그림자 자리가 사이드바 경계를 넘어 본문까지 (") + juce::String (pm.box.expanded (m).getRight()) + " > " + juce::String (root.sidebar->getRight()) + ")");
        root.repaint();
    }, 400);
    add ([this, sh]
    {
        expect (root.profileMenu->getSurface().getBuildCount() == 1,
                TXT ("프로필 메뉴 그림자: 한 번만 계산 (") + juce::String (root.profileMenu->getSurface().getBuildCount()) + ")");
        sh->bounds = peer()->getBounds();
        peer()->setBounds (sh->bounds.withHeight (sh->bounds.getHeight() - 60), false);   // 창 높이만 바꾼다: 메뉴는 아래에 붙어 자리만 옮겨진다
    }, 500);
    add ([this, sh]
    {
        expect (root.profileMenu != nullptr && root.profileMenu->box.getBottom() == root.getHeight() - 55, TXT ("창 높이를 바꾸면 메뉴도 따라 옮겨진다"));
        if (root.profileMenu != nullptr)
            expect (root.profileMenu->getSurface().getBuildCount() == 1,
                    TXT ("창 크기를 바꿔 자리만 옮겨지면 그림자를 다시 계산하지 않는다 (") + juce::String (root.profileMenu->getSurface().getBuildCount()) + ")");
        peer()->setBounds (sh->bounds, false);
    }, 500);
    // 메뉴의 "알림" 을 눌러 알림 패널 (사이드바 안, 프로필 줄 위)
    add ([this]
    {
        if (root.profileMenu == nullptr) return;
        const auto b = root.profileMenu->box;
        click (*root.profileMenu, { b.getX() + b.getWidth() / 2, b.getY() + 6 + 16 });
    }, 500);
    waitUntil (TXT ("알림 패널 열림"), [this] { return root.home->popover != nullptr && root.home->popover->isShowing(); });
    add ([this, m]
    {
        auto& p = *root.home->popover;
        const auto panel = p.getPanelBounds();
        expect (panel.getWidth() == 340 && panel.getX() == 9 && panel.getBottom() == root.home->getHeight() - 55,
                TXT ("알림 패널: 폭 340, 왼쪽 9, 아래 55 (그대로) ") + panel.toString());
        expect (p.getBounds() == panel.expanded (m), TXT ("알림 패널: 구성 요소는 판보다 그림자 폭만큼 크다 (") + juce::String (m) + ")");
        // 그림자 자리 중 창 안에 있는 부분은 부모(모달 층 = AppRoot) 안: 잘리지 않는다
        const auto inWindow = p.getBounds().getIntersection (root.getLocalBounds());
        expect (root.getLocalBounds().contains (inWindow) && inWindow.getRight() == p.getRight() && inWindow.getY() == p.getY(),
                TXT ("알림 패널 그림자: 위·오른쪽이 부모 안 (창 가장자리 쪽만 창 끝)"));
        expect (p.getParentComponent() == &root && root.getIndexOfChildComponent (&p) > root.getIndexOfChildComponent (root.sidebar.get())
                 && root.getIndexOfChildComponent (&p) > root.getIndexOfChildComponent (root.home.get()),
                TXT ("알림 패널: 모달 층 (사이드바·본문 위) 에 있어 그림자가 사이드바 위로도 번진다"));
        expect (! p.hitTest (panel.getRight() - p.getX() + 4, panel.getCentreY() - p.getY()), TXT ("알림 패널: 그림자 자리는 마우스를 받지 않는다"));
        expect (p.hitTest (panel.getCentreX() - p.getX(), panel.getCentreY() - p.getY()), TXT ("알림 패널: 판 안은 받는다"));
        root.repaint();
    }, 400);
    add ([this]
    {
        auto& p = *root.home->popover;
        expect (p.getSurface().getBuildCount() == 1, TXT ("알림 패널 그림자: 한 번만 계산 (") + juce::String (p.getSurface().getBuildCount()) + ")");
        root.sidebar->setCollapsed (! root.sidebar->isCollapsed());   // 사이드바 접기 (팝오버 자리·크기는 그대로)
    }, 600);
    add ([this]
    {
        auto& p = *root.home->popover;
        expect (p.getSurface().getBuildCount() == 1, TXT ("사이드바를 접어도 그림자를 다시 계산하지 않는다 (") + juce::String (p.getSurface().getBuildCount()) + ")");
        root.sidebar->setCollapsed (! root.sidebar->isCollapsed());
        root.home->toggleNotifications();
    }, 600);
    // 토스트: 팝오버와 같은 표면 (테두리 #EBEBEB + 그림자 두 겹). 자리·크기는 그대로 (오른쪽 12, 아래 12, 폭 280)
    closeToasts();
    add ([this] { root.addInfoToast (TXT ("표면 확인 토스트"), TXT ("둘째 줄")); }, 500);
    add ([this, m]
    {
        Toast* t = nullptr;
        for (auto* x : root.toasts) if (x->getTitle() == TXT ("표면 확인 토스트")) t = x;
        expect (t != nullptr, TXT ("토스트 뜸"));
        if (t == nullptr) return;
        const auto panel = t->getPanelBounds();
        expect (panel.getWidth() == Toast::width && panel.getRight() == root.getWidth() - 12 && panel.getBottom() == root.getHeight() - 12,
                TXT ("토스트: 폭 280, 오른쪽 12, 아래 12 (그대로) ") + panel.toString());
        expect (t->getBounds() == panel.expanded (m), TXT ("토스트: 구성 요소는 판보다 그림자 폭만큼 크다 (그림자가 잘리지 않게)"));
        expect (! t->hitTest (panel.getX() - t->getX() - 4, panel.getCentreY() - t->getY()), TXT ("토스트: 그림자 자리는 마우스를 받지 않는다"));
        root.repaint();
    }, 400);
    add ([this]
    {
        for (auto* x : root.toasts)
            if (x->getTitle() == TXT ("표면 확인 토스트"))
                expect (x->getSurface().getBuildCount() == 1, TXT ("토스트 그림자: 한 번만 계산 (") + juce::String (x->getSurface().getBuildCount()) + ")");
    }, 0);
    closeToasts();
}

//==============================================================================
// 윈도우 창 윗부분 (디자인 v2.1 os = 윈도우): 창 버튼 셋은 에디터 맨 위 오른쪽 끝 46×51, 제목 줄 오른쪽 여백 150.
// 윈도우에서는 실제 창(MeonWindowFrame)으로: 영역 판정(WM_NCHITTEST), 맨 위 줄 픽셀, 최대화(화면 안에 딱), 버튼 마우스 올림 색,
// 더블클릭 최대화·복원, Alt+Space 창 메뉴, 제목 줄 끌기, 최소화. 입력은 SendInput (실제 입력과 같은 길)
void UiSelfTest::addWinChromeTests()
{
    begin (TXT ("윈도우 창 윗부분 (창 버튼 + 제목 줄)"));
    if (core.isPlugin()) { add ([] { std::cerr << "UITEST INFO winchrome: 플러그인이라 건너뜀 (창 주인이 DAW)" << std::endl; }, 0); return; }
    struct Shared
    {
        bool on = false, real = false;
        meon::winframe::Probe before;
        int sysMenus = 0;
        juce::Rectangle<int> windowBefore;
    };
    auto sh = std::make_shared<Shared>();
    auto caption = [this]() -> WinCaptionButtons*
    {
        if (auto* e = root.getParentComponent())
            for (auto* c : e->getChildren())
                if (auto* w = dynamic_cast<WinCaptionButtons*> (c)) return w;
        return nullptr;
    };
    auto handle = [this]() -> void* { auto* p = peer(); return p != nullptr ? p->getNativeHandle() : nullptr; };
    // 구성 요소 좌표 → 화면 물리 좌표 (WM_NCHITTEST·SendInput 이 쓰는 좌표)
    auto phys = [] (juce::Component& c, juce::Point<int> local)
    {
        return juce::Desktop::getInstance().getDisplays().logicalToPhysical (c.localPointToGlobal (local));
    };
    auto emptyTitle = [this]() { return juce::Point<int> (root.sidebar->getRight() + 260, 26); };   // 공개 방 제목 오른쪽 빈 곳
    auto ht = [handle] (juce::Point<int> s) { return meon::winframe::devHitTest (handle(), s.x, s.y); };
    auto rectStr = [] (const int r[4]) { return juce::String (r[0]) + "," + juce::String (r[1]) + " " + juce::String (r[2]) + "x" + juce::String (r[3]); };

    add ([this, sh, caption, handle]
    {
        sh->on = caption() != nullptr;
        if (! sh->on) { std::cerr << "UITEST INFO winchrome: 창 버튼 없음 (윈도우가 아니고 --chrome=win 도 아님). 건너뜀" << std::endl; return; }
        sh->real = meon::winframe::installedOn (handle());
        if (root.page != Sidebar::Page::rooms) root.showPage (Sidebar::Page::rooms);
    }, 600);
    closeToasts();
    // 배치 (모든 플랫폼: 윈도우는 실제 창, 그 밖은 --chrome=win 미리 보기)
    add ([this, sh, caption, emptyTitle]
    {
        if (! sh->on) return;
        auto* cb = caption();
        auto* editor = root.getParentComponent();
        const int W = editor->getWidth();
        expect (cb->getBounds() == juce::Rectangle<int> (W - 138, 0, 138, 51), TXT ("창 버튼: 오른쪽 위 끝 138×51 (버튼 46×51 셋) ") + cb->getBounds().toString());
        const int top = editor->getNumChildComponents() - 1;
        const int idx = editor->getIndexOfChildComponent (cb);
        expect (idx == top || (idx == top - 1 && dynamic_cast<juce::TooltipWindow*> (editor->getChildComponent (top)) != nullptr),
                TXT ("창 버튼은 에디터 맨 위 (툴팁 창만 그 위)"));
        expect (root.getTitleRightInset() == 150, TXT ("제목 줄 오른쪽 여백 150 (138 + 12): ") + juce::String (root.getTitleRightInset()));
        auto& icon = root.home->createIcon;
        const auto iconRight = editor->getLocalPoint (root.home.get(), juce::Point<int> (icon.getRight(), 0)).x;
        expect (icon.isVisible() && iconRight == cb->getX() - 12, TXT ("공개 방 + 아이콘: 창 버튼 왼쪽 12 (오른쪽 끝 ") + juce::String (iconRight) + " / " + juce::String (cb->getX() - 12) + ")");
        expect (cb->buttonAt ({ 23, 25 }) == WinCaptionButtons::minimise && cb->buttonAt ({ 69, 25 }) == WinCaptionButtons::maximise
                 && cb->buttonAt ({ 115, 25 }) == WinCaptionButtons::closeButton, TXT ("버튼 순서: 최소화 · 최대화 · 닫기"));
        // 제목 줄 판정 (맥 창 끌기와 같은 조건): 빈 곳·사이드바 로고 줄은 끌기, 버튼·아이콘·본문은 아니다
        expect (root.titleBarDragAt (emptyTitle()), TXT ("제목 줄 빈 곳 = 끌기 자리"));
        expect (! root.titleBarDragAt (root.getLocalPoint (&icon, icon.getLocalBounds().getCentre())), TXT ("+ 아이콘 = 앱 버튼"));
        expect (root.titleBarDragAt ({ 120, 26 }), TXT ("사이드바 로고 줄 빈 곳 = 끌기 자리"));
        if (root.toggle->isVisible()) expect (! root.titleBarDragAt (root.toggle->getBounds().getCentre()), TXT ("사이드바 접기 버튼 = 앱 버튼"));
        expect (! root.titleBarDragAt ({ emptyTitle().x, 100 }), TXT ("제목 줄 아래 = 앱 화면"));
    }, 200);
    // 설정 팝업이 떠 있어도 창 버튼은 맨 위에서 눌러진다
    add ([this, sh] { if (sh->on && root.onOpenSettings) root.onOpenSettings (0); }, 700);
    add ([this, sh, caption, handle, phys, emptyTitle, ht]
    {
        if (! sh->on) return;
        auto* cb = caption();
        auto* editor = root.getParentComponent();
        const auto closeC = editor->getLocalPoint (cb, cb->buttonBounds (WinCaptionButtons::closeButton).getCentre());
        expect (editor->getComponentAt (closeC) == cb, TXT ("설정 팝업 위에서도 닫기 버튼이 맨 위 (눌러진다)"));
        if (sh->real)
        {
            expect (ht (phys (*cb, cb->buttonBounds (WinCaptionButtons::maximise).getCentre())) == (meon::winframe::probe (handle()).win11 ? 9 : 1),
                    TXT ("설정 팝업 위: 최대화 버튼 (윈도우 11 HTMAXBUTTON, 윈도우 10 앱 화면)"));
            const int t = ht (phys (root, emptyTitle()));
            expect (t == 1, TXT ("설정 팝업이 떠 있으면 제목 줄은 앱 화면 (맥과 같다): ") + juce::String (t));
        }
        editor->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
    }, 600);
    // 홈·방 화면의 모달(모달 층: 창 전체)이 떠 있어도 창 버튼은 맨 위에서 눌러진다 (설정 창과 같다)
    add ([this, sh] { if (sh->on) home().openAddFriend(); }, 500);
    add ([this, sh, caption, handle, phys, emptyTitle, ht]
    {
        if (! sh->on) return;
        auto* cb = caption();
        auto* editor = root.getParentComponent();
        const auto closeC = editor->getLocalPoint (cb, cb->buttonBounds (WinCaptionButtons::closeButton).getCentre());
        expect (home().modal != nullptr && home().modal->getParentComponent() == &root && home().modal->getBounds() == root.getLocalBounds(),
                TXT ("친구 추가: 모달 층 (창 전체)"));
        expect (editor->getComponentAt (closeC) == cb, TXT ("친구 추가 모달 위에서도 닫기 버튼이 맨 위 (눌러진다)"));
        if (sh->real)
        {
            expect (ht (phys (*cb, cb->buttonBounds (WinCaptionButtons::maximise).getCentre())) == (meon::winframe::probe (handle()).win11 ? 9 : 1),
                    TXT ("모달 위: 최대화 버튼 (윈도우 11 HTMAXBUTTON, 윈도우 10 앱 화면)"));
            const int t = ht (phys (root, emptyTitle()));
            expect (t == 1, TXT ("모달이 떠 있으면 제목 줄은 앱 화면 (설정 창과 같다): ") + juce::String (t));
        }
        home().closeOverlays();
    }, 300);
    if (! juce::SystemStats::getOperatingSystemName().startsWithIgnoreCase ("Windows"))
    {
        add ([sh] { if (sh->on) std::cerr << "UITEST INFO winchrome: 윈도우가 아니라 실제 창 확인은 건너뜀" << std::endl; }, 0);
        return;
    }
    // ── 윈도우: 실제 창 ──
    add ([this, sh, caption, handle, phys, emptyTitle, ht, rectStr]
    {
        if (! sh->on) return;
        expect (sh->real, TXT ("창 틀 붙음 (MeonWindowFrame)"));
        if (! sh->real) return;
        const auto p = meon::winframe::probe (handle());
        sh->before = p;
        std::cerr << "UITEST INFO winchrome: os " << p.os << " dpi " << p.dpi << " resizeBorder " << p.resizeBorder
                  << " window " << rectStr (p.window) << " client " << rectStr (p.client) << " monitor " << rectStr (p.monitor)
                  << " work " << rectStr (p.work) << " ncCalc " << p.ncCalc << std::endl;
        expect (p.client[1] == p.window[1], TXT ("제목 줄 없음: 내용 위 = 창 위 (") + juce::String (p.client[1]) + " / " + juce::String (p.window[1]) + ")");
        const int left = p.client[0] - p.window[0], right = (p.window[0] + p.window[2]) - (p.client[0] + p.client[2]);
        const int bottom = (p.window[1] + p.window[3]) - (p.client[1] + p.client[3]);
        expect (left == right && left == bottom && left == p.insets[0] && p.insets[1] == 0 && left > 0,
                TXT ("왼쪽·오른쪽·아래 틀 = 크기 조절 자리 (윈도우 10 은 운영체제 1px 테두리 자리까지 내용) (") + juce::String (left) + "/" + juce::String (right) + "/" + juce::String (bottom) + ")");
        auto* cb = caption();
        const int midX = p.window[0] + p.window[2] / 2;
        struct H { const char* name; juce::Point<int> at; int want; };
        const H checks[] = {
            { "위 가장자리 → HTTOP 12", { midX, p.window[1] + 2 }, 12 },
            { "제목 줄 빈 곳 → HTCAPTION 2", phys (root, emptyTitle()), 2 },
            { "사이드바 로고 줄 빈 곳 → HTCAPTION 2", phys (root, { 120, 26 }), 2 },
            { p.win11 ? "최대화 버튼 → HTMAXBUTTON 9 (윈도우 11 스냅 레이아웃)" : "최대화 버튼 → HTCLIENT 1 (윈도우 10: 운영체제 버튼이 그려지지 않게 앱이 받음)",
              phys (*cb, cb->buttonBounds (WinCaptionButtons::maximise).getCentre()), p.win11 ? 9 : 1 },
            { "최소화 버튼 → HTCLIENT 1 (앱이 받음)", phys (*cb, cb->buttonBounds (WinCaptionButtons::minimise).getCentre()), 1 },
            { "닫기 버튼 → HTCLIENT 1 (앱이 받음)", phys (*cb, cb->buttonBounds (WinCaptionButtons::closeButton).getCentre()), 1 },
            { "+ 아이콘 → HTCLIENT 1", phys (root.home->createIcon, root.home->createIcon.getLocalBounds().getCentre()), 1 },
            { "제목 줄 아래 → HTCLIENT 1", phys (root, { emptyTitle().x, 100 }), 1 },
            { "왼쪽 가장자리 → HTLEFT 10", { p.window[0] + 2, p.window[1] + p.window[3] / 2 }, 10 },
            { "오른쪽 가장자리 → HTRIGHT 11", { p.window[0] + p.window[2] - 2, p.window[1] + p.window[3] / 2 }, 11 },
            { "아래 가장자리 → HTBOTTOM 15", { midX, p.window[1] + p.window[3] - 2 }, 15 },
            { "내용 왼쪽 끝 줄 → 크기 조절 (윈도우 10) / 앱 (윈도우 11)", { p.client[0], p.client[1] + p.client[3] / 2 }, p.win11 ? 1 : 10 },
            { "내용 아래 끝 줄 → 크기 조절 (윈도우 10) / 앱 (윈도우 11)", { midX, p.client[1] + p.client[3] - 1 }, p.win11 ? 1 : 15 },
            { "왼쪽 아래 모서리 → HTBOTTOMLEFT 16", { p.window[0] + 2, p.window[1] + p.window[3] - 2 }, 16 },
            { "오른쪽 위 모서리 → HTTOPRIGHT 14 (창 버튼 위 2px)", { p.window[0] + p.window[2] - 2, p.window[1] + 1 }, 14 },
        };
        for (auto& c : checks)
        {
            const int got = ht (c.at);
            expect (got == c.want, juce::String (juce::CharPointer_UTF8 (c.name)) + " (" + juce::String (got) + ")");
        }
        // 맨 위 줄 픽셀 (윈도우 10 의 1px 색 선 확인): 내용 맨 위 3줄 · 창 바로 위 1줄
        for (int x : { p.client[0] + 120, midX, p.client[0] + p.client[2] - 200 })
            std::cerr << "UITEST INFO winchrome top pixels x=" << x << " above: " << meon::winframe::devScreenPixels (x, p.client[1] - 1, 1, 1)
                      << "| rows 0-2: " << meon::winframe::devScreenPixels (x, p.client[1], 1, 3) << std::endl;
        // 창 테두리 #EBEBEB 1px: 윈도우 11 은 운영체제가 (위는 내용 맨 위 줄 위에, 나머지는 내용 바깥), 윈도우 10 은 앱이 내용 네 변 끝 줄에
        const auto row0 = meon::winframe::devScreenPixels (midX, p.client[1], 1, 1).substr (0, 6);
        expect (row0 == "EBEBEB", TXT ("위 테두리 #EBEBEB 1px (") + juce::String (row0) + ")");
        const int cy = p.client[1] + p.client[3] / 2;
        const auto edge = [&] (int x, int y) { return juce::String (meon::winframe::devScreenPixels (x, y, 1, 1).substr (0, 6)); };
        const auto l = p.win11 ? edge (p.client[0] - 1, cy) : edge (p.client[0], cy);
        const auto r = p.win11 ? edge (p.client[0] + p.client[2], cy) : edge (p.client[0] + p.client[2] - 1, cy);
        const auto b = p.win11 ? edge (midX, p.client[1] + p.client[3]) : edge (midX, p.client[1] + p.client[3] - 1);
        expect (l == "EBEBEB" && r == "EBEBEB" && b == "EBEBEB", TXT ("왼쪽·오른쪽·아래 테두리 #EBEBEB 1px (") + l + " " + r + " " + b + ")");
        if (! p.win11)
        {
            // 윈도우 10: 운영체제 테두리(#707070)는 내용이 덮어 보이지 않는다 → 테두리 바로 안쪽은 앱 화면 (선이 두 줄이 아니다)
            const auto in1 = edge (p.client[0] + 1, cy);
            std::cerr << "UITEST INFO winchrome win10 edge: left " << l << " next " << in1 << " outside " << edge (p.client[0] - 1, cy) << std::endl;
        }
    }, 300);
    // 마우스 올림 색: 최대화(운영체제 영역에서 알려 줌) #F5F5F5, 닫기 #C42B1C
    add ([sh, caption, phys] { if (sh->real) { auto* cb = caption(); const auto c = phys (*cb, cb->buttonBounds (WinCaptionButtons::maximise).getCentre()); meon::winframe::devMouse (c.x, c.y, false, false); } }, 600);
    add ([this, sh, caption, phys]
    {
        if (! sh->real) return;
        auto* cb = caption();
        const auto corner = phys (*cb, cb->buttonBounds (WinCaptionButtons::maximise).getTopLeft() + juce::Point<int> (3, 3));
        const auto px = meon::winframe::devScreenPixels (corner.x, corner.y, 1, 1).substr (0, 6);
        expect (cb->devFill (WinCaptionButtons::maximise) == juce::Colour (0xFFF5F5F5u), TXT ("최대화 버튼에 올리면 #F5F5F5 (그리는 색)"));
        std::cerr << "UITEST INFO winchrome max hover pixel " << px << std::endl;
        const auto c = phys (*cb, cb->buttonBounds (WinCaptionButtons::closeButton).getCentre());
        meon::winframe::devMouse (c.x, c.y, false, false);
    }, 600);
    add ([this, sh, caption, phys, emptyTitle]
    {
        if (! sh->real) return;
        auto* cb = caption();
        const auto corner = phys (*cb, cb->buttonBounds (WinCaptionButtons::closeButton).getTopLeft() + juce::Point<int> (3, 3));
        const auto px = meon::winframe::devScreenPixels (corner.x, corner.y, 1, 1).substr (0, 6);
        expect (cb->devFill (WinCaptionButtons::maximise).isTransparent(), TXT ("최대화 버튼에서 나가면 보통"));
        expect (cb->devFill (WinCaptionButtons::closeButton) == juce::Colour (0xFFC42B1Cu) && px == "C42B1C", TXT ("닫기 버튼에 올리면 #C42B1C (화면 ") + juce::String (px) + ")");
        const auto e = phys (root, { emptyTitle().x, 300 });
        meon::winframe::devMouse (e.x, e.y, false, false);
    }, 400);
    // 최대화 버튼 → 화면(작업 영역) 안에 딱, 닫기 버튼이 오른쪽 위 모서리
    add ([sh, caption, phys] { if (sh->real) { auto* cb = caption(); const auto c = phys (*cb, cb->buttonBounds (WinCaptionButtons::maximise).getCentre()); meon::winframe::devMouse (c.x, c.y, true, true); } }, 0);
    waitUntilOr (TXT ("최대화"), [sh, handle] { return ! sh->real || meon::winframe::isMaximised (handle()); }, 5000, [this] { expect (false, TXT ("최대화 버튼을 눌러도 최대화되지 않음")); });
    add ([this, sh, caption, handle, phys, ht, rectStr]
    {
        if (! sh->real) return;
        const auto p = meon::winframe::probe (handle());
        std::cerr << "UITEST INFO winchrome maximised: window " << rectStr (p.window) << " client " << rectStr (p.client) << " work " << rectStr (p.work) << std::endl;
        // 최대화: 내용 = 작업 영역 (위·왼쪽·오른쪽·아래 여백·잘림 0)
        expect (std::equal (p.client, p.client + 4, p.work), TXT ("최대화: 내용 = 작업 영역 (") + rectStr (p.client) + " / " + rectStr (p.work) + ")");
        auto* cb = caption();
        expect (cb->isMaximised(), TXT ("최대화 버튼이 '이전 크기로' 아이콘 (상태 알림 ") + juce::String (p.notifies) + ")");
        const auto tr = phys (*cb, { cb->getWidth() - 1, 0 });
        expect (tr.x == p.work[0] + p.work[2] - 1 && tr.y == p.work[1], TXT ("닫기 버튼이 화면 오른쪽 위 모서리에 딱 (") + tr.toString() + ")");
        const int corner = ht ({ p.work[0] + p.work[2] - 1, p.work[1] });
        expect (corner == 1, TXT ("최대화: 오른쪽 위 모서리 픽셀 = 닫기 버튼 (HTCLIENT, ") + juce::String (corner) + ")");
        const int topEdge = ht ({ p.work[0] + p.work[2] / 2, p.work[1] });
        expect (topEdge != 12, TXT ("최대화: 위 가장자리 크기 조절 없음 (") + juce::String (topEdge) + ")");
        const auto c = phys (*cb, cb->buttonBounds (WinCaptionButtons::maximise).getCentre());
        meon::winframe::devMouse (c.x, c.y, true, true);   // 이전 크기로
    }, 0);
    waitUntilOr (TXT ("이전 크기로"), [sh, handle] { return ! sh->real || ! meon::winframe::isMaximised (handle()); }, 5000, [this] { expect (false, TXT ("이전 크기로 버튼을 눌러도 돌아오지 않음")); });
    add ([this, sh, handle, caption]
    {
        if (! sh->real) return;
        const auto p = meon::winframe::probe (handle());
        expect (std::equal (p.client, p.client + 4, sh->before.client), TXT ("이전 크기로: 내용 자리·크기가 처음과 같다"));
        expect (! caption()->isMaximised(), TXT ("이전 크기로: 최대화 아이콘으로 돌아옴"));
    }, 400);
    // 다른 창이 앞에 오면 아이콘 #AAAAAA (비활성), 돌아오면 #1A1A1A
    struct Other { std::unique_ptr<juce::DocumentWindow> w; };
    auto other = std::make_shared<Other>();
    add ([sh, other]
    {
        if (! sh->real) return;
        other->w = std::make_unique<juce::DocumentWindow> ("other", juce::Colours::grey, juce::DocumentWindow::closeButton);
        other->w->setUsingNativeTitleBar (true);
        other->w->setBounds (200, 200, 300, 200);
        other->w->setVisible (true);
        other->w->toFront (true);
    }, 0);
    waitUntilOr (TXT ("비활성"), [sh, caption] { return ! sh->real || ! caption()->isActiveLook(); }, 3000,
                 [this] { expect (false, TXT ("다른 창이 앞에 와도 창 버튼이 비활성 모양으로 바뀌지 않음")); });
    add ([this, sh, other, caption, handle]
    {
        if (! sh->real) return;
        if (! caption()->isActiveLook()) expect (true, TXT ("다른 창이 앞에 오면 창 버튼 아이콘 #AAAAAA (비활성)"));
        other->w.reset();
        if (auto* p = peer()) p->toFront (true);
    }, 0);
    waitUntilOr (TXT ("다시 활성"), [sh, caption] { return ! sh->real || caption()->isActiveLook(); }, 3000,
                 [this] { expect (false, TXT ("창이 다시 앞에 와도 창 버튼이 활성 모양으로 돌아오지 않음")); });
    add ([this, sh] { if (sh->real) expect (true, TXT ("다시 앞에 오면 아이콘 #1A1A1A (활성)")); }, 0);
    // 제목 줄 빈 곳 더블클릭 → 최대화, 다시 → 이전 크기로 (운영체제 HTCAPTION 처리)
    auto doubleClick = [sh, phys, this, emptyTitle]
    {
        if (! sh->real) return;
        const auto e = phys (root, emptyTitle());
        meon::winframe::devMouse (e.x, e.y, true, true);
        meon::winframe::devMouse (e.x, e.y, true, true);
    };
    add (doubleClick, 0);
    waitUntilOr (TXT ("더블클릭 최대화"), [sh, handle] { return ! sh->real || meon::winframe::isMaximised (handle()); }, 5000, [this] { expect (false, TXT ("제목 줄 더블클릭으로 최대화되지 않음")); });
    add ([this, sh] { if (sh->real) expect (true, TXT ("제목 줄 더블클릭 → 최대화")); }, 600);
    add (doubleClick, 0);
    waitUntilOr (TXT ("더블클릭 복원"), [sh, handle] { return ! sh->real || ! meon::winframe::isMaximised (handle()); }, 5000, [this] { expect (false, TXT ("제목 줄 더블클릭으로 돌아오지 않음")); });
    add ([this, sh] { if (sh->real) expect (true, TXT ("제목 줄 더블클릭 → 이전 크기로")); }, 600);
    // Alt+Space → 창 메뉴 (JUCE 가 막던 것을 운영체제에 넘긴다)
    add ([sh, handle] { if (sh->real) { sh->sysMenus = meon::winframe::probe (handle()).sysMenuOpened; meon::winframe::devAltSpace(); } }, 800);
    add ([this, sh, handle]
    {
        if (! sh->real) return;
        const int n = meon::winframe::probe (handle()).sysMenuOpened;
        expect (n > sh->sysMenus, TXT ("Alt+Space → 창 메뉴 (") + juce::String (n - sh->sysMenus) + ")");
        meon::winframe::devEscape();
    }, 500);
    // 제목 줄을 잡고 끌기 (운영체제 창 옮기기)
    add ([this, sh, handle, phys, emptyTitle]
    {
        if (! sh->real) return;
        const auto p = meon::winframe::probe (handle());
        sh->windowBefore = { p.window[0], p.window[1], p.window[2], p.window[3] };
        const auto e = phys (root, emptyTitle());
        meon::winframe::devMouse (e.x, e.y, true, false);
    }, 300);
    add ([this, sh, phys, emptyTitle] { if (sh->real) { const auto e = phys (root, emptyTitle()); meon::winframe::devMouse (e.x + 30, e.y + 20, false, false); } }, 200);
    add ([this, sh, phys, emptyTitle] { if (sh->real) { const auto e = phys (root, emptyTitle()); meon::winframe::devMouse (e.x + 60, e.y + 40, false, false); } }, 200);
    add ([this, sh, handle]
    {
        if (! sh->real) return;
        const auto p = meon::winframe::probe (handle());
        meon::winframe::devMouse (p.window[0] + 400, p.window[1] + 26, false, true);   // 뗌 (지금 커서 자리)
    }, 600);
    add ([this, sh, handle]
    {
        if (! sh->real) return;
        const auto p = meon::winframe::probe (handle());
        const int dx = p.window[0] - sh->windowBefore.getX(), dy = p.window[1] - sh->windowBefore.getY();
        std::cerr << "UITEST INFO winchrome drag moved " << dx << "," << dy << std::endl;
        expect (dx != 0 || dy != 0, TXT ("제목 줄을 잡고 끌면 창이 옮겨진다 (") + juce::String (dx) + "," + juce::String (dy) + ")");
    }, 300);
    // 최소화 버튼 → 최소화, 다시 띄운다
    add ([sh, caption, phys] { if (sh->real) { auto* cb = caption(); const auto c = phys (*cb, cb->buttonBounds (WinCaptionButtons::minimise).getCentre()); meon::winframe::devMouse (c.x, c.y, true, true); } }, 0);
    waitUntilOr (TXT ("최소화"), [this, sh] { return ! sh->real || (peer() != nullptr && peer()->isMinimised()); }, 5000, [this] { expect (false, TXT ("최소화 버튼을 눌러도 최소화되지 않음")); });
    add ([this, sh] { if (sh->real) { expect (true, TXT ("최소화 버튼 → 최소화")); peer()->setMinimised (false); } }, 1000);
    add ([this, sh, handle, rectStr]
    {
        if (! sh->real) return;
        const auto p = meon::winframe::probe (handle());
        std::cerr << "UITEST INFO winchrome end: window " << rectStr (p.window) << " client " << rectStr (p.client) << " active " << (p.active ? 1 : 0) << std::endl;
    }, 0);
}

//==============================================================================
// 팝오버·모달·토스트를 열고 닫아도 보던 화면(공개 방·방 화면)은 그대로. 예전에는 홈이 띄우는 창(알림·친구 추가·코드로 입장)이 홈 안에 떠서
// 방 화면이면 먼저 방을 최소화했다 (= 홈 메뉴를 끈 뒤로 공개 방으로 바뀌었다). 이제 모달·팝오버는 모달 층(AppRoot: 창 전체,
// 사이드바까지 어둡게 덮고 창 가운데)에 뜬다. 방에 있으면 사이드바 "코드로 입장" 은 없다 (방 만들기와 같은 규칙)
void UiSelfTest::addOverlayTests()
{
    struct Shared
    {
        juce::String room, chatText;
        int phase = 0;
        bool sendMuted = false, monMuted = false, selfMon = false, homeVisible = false;
        float master = 0.0f;
        bool side = true;   // 방 화면에서 사이드바가 보인다 (좁은 플러그인 창은 숨는다: 방 화면 윗줄의 알림·설정·로비 버튼)
    };
    auto sh = std::make_shared<Shared>();
    auto roomView = [this]
    {
        return inRoom() && ! root.isMinimized() && jam() != nullptr && jam()->isShowing() && ! home().isVisible()
            && root.sidebar->getPage() == Sidebar::Page::room;
    };
    auto snap = [this, sh]
    {
        auto* s = core.jam();
        if (s == nullptr) return;
        sh->room = roomId();
        sh->phase = (int) s->getPhase();
        sh->sendMuted = s->isSendMuted();
        sh->monMuted = s->isMonitorMuted();
        sh->selfMon = s->isSelfMonitorAudible();
        sh->master = s->getMasterDb();
        sh->homeVisible = core.home().isHomeVisible();
        if (auto* j = jam()) sh->chatText = j->chatInput.getText();
    };
    auto same = [this, sh, roomView] (const juce::String& what)
    {
        auto* s = core.jam();
        expect (roomView(), what + TXT (": 방 화면 그대로 (공개 방으로 가지 않음)"));
        expect (s != nullptr && roomId() == sh->room && (int) s->getPhase() == sh->phase, what + TXT (": 같은 방, 연결 단계 그대로"));
        if (s != nullptr)
            expect (s->isSendMuted() == sh->sendMuted && s->isMonitorMuted() == sh->monMuted && s->isSelfMonitorAudible() == sh->selfMon
                        && s->getMasterDb() == sh->master && core.home().isHomeVisible() == sh->homeVisible,
                    what + TXT (": 송출·모니터·셀프 모니터·마스터 볼륨 그대로, 공개 방 구독 없음"));
        if (auto* j = jam()) expect (j->chatInput.getText() == sh->chatText, what + TXT (": 채팅 입력 중인 글 그대로"));
    };
    auto fullModal = [this] (juce::Component* m, const juce::String& what)
    {
        expect (m != nullptr && m->isShowing(), what + TXT (": 떴다"));
        if (m == nullptr) return;
        expect (m->getParentComponent() == &root && m->getBounds() == root.getLocalBounds(), what + TXT (": 모달 층 (창 전체 ") + m->getBounds().toString() + ")");
        const int idx = root.getIndexOfChildComponent (m);
        bool above = idx > root.getIndexOfChildComponent (root.sidebar.get()) && idx > root.getIndexOfChildComponent (root.home.get())
                  && idx > root.getIndexOfChildComponent (root.toggle.get());
        if (root.jam != nullptr) above = above && idx > root.getIndexOfChildComponent (root.jam.get());
        expect (above, what + TXT (": 사이드바·본문·방 화면·접기 버튼 위"));
        if (root.sidebar->isVisible())
        {
            auto* hit = root.getComponentAt (juce::Point<int> (root.sidebar->getWidth() / 2, root.getHeight() / 2));
            expect (hit == m || m->isParentOf (hit), what + TXT (": 사이드바 자리도 덮는다 (누르면 모달 바깥)"));
        }
        if (auto* md = dynamic_cast<Modal*> (m))
        {
            const auto box = md->boxBounds();
            expect (std::abs (box.getCentreX() - root.getWidth() / 2) <= 1 && std::abs (box.getCentreY() - root.getHeight() / 2) <= 1,
                    what + TXT (": 창 가운데 (") + box.toString() + ")");
        }
    };
    auto outside = [this] { click (root, { juce::jmax (8, root.sidebar->getWidth() / 2), root.getHeight() / 2 }); };   // 사이드바 위 덮개 (모달 바깥)
    auto profileMenuItem = [this] (int i)
    {
        if (root.profileMenu == nullptr) return;
        const auto b = root.profileMenu->box;
        click (*root.profileMenu, { b.getX() + b.getWidth() / 2, b.getY() + 6 + i * 34 + 16 });
    };
    auto settingsOpen = [this]
    {
        if (auto* e = root.getParentComponent())
            for (auto* c : e->getChildren())
                if (dynamic_cast<SettingsModal*> (c) != nullptr) return true;
        return false;
    };
    auto friendPlus = [this]
    {
        const auto h = root.sidebar->devSectionHeader (TXT ("친구"));
        expect (! h.isEmpty(), TXT ("사이드바 친구 머리말"));
        if (! h.isEmpty()) click (*root.sidebar, { h.getRight() - 6 - 11, h.getCentreY() });   // 머리말 오른쪽 + (22×22, 오른쪽 6)
    };

    begin (TXT ("겹침: 방 화면에서 열고 닫아도 방 화면 그대로"));
    closeToasts();
    enterNewRoom();
    add ([this, sh, snap]
    {
        if (root.sidebar->isCollapsed()) root.sidebar->setCollapsed (false);
        sh->side = root.sidebar->isVisible();
        if (! sh->side) std::cerr << "UITEST INFO overlay: 방 화면에서 사이드바가 숨음 (좁은 창). 사이드바 항목은 건너뛰고 방 화면 윗줄 알림으로" << std::endl;
        jam()->devSetChatInput (TXT ("입력 중인 글"));
        jam()->grabKeyboardFocus();
        snap();
        expect (root.sidebar->devRow (TXT ("코드로 입장")).isEmpty(), TXT ("방에 있으면 사이드바 코드로 입장 없음 (펼침)"));
        expect (! root.sidebar->devRow (TXT ("공개 방")).isEmpty(), TXT ("공개 방 메뉴는 그대로"));
    }, 600);
    // 프로필 › 알림: 방 화면 위에 팝오버 (사이드바가 숨는 좁은 창은 방 화면 윗줄 알림 버튼: 방 화면 안 팝오버)
    add ([this, sh] { if (sh->side) click (*root.sidebar, root.sidebar->devProfileRow().getCentre()); else click (jam()->bell); }, 300);
    add ([sh, profileMenuItem] { if (sh->side) profileMenuItem (0); }, 400);
    waitUntil (TXT ("알림 팝오버 열림"), [this, sh]
    {
        return sh->side ? (home().popover != nullptr && home().popover->isShowing()) : (jam()->notifications != nullptr && jam()->notifications->isShowing());
    });
    add ([this, sh, same]
    {
        same (TXT ("알림 열기"));
        if (! sh->side) { jam()->devOpenPopover ("notif"); return; }   // 다시 누르면 닫힌다
        auto* p = home().popover.get();
        if (p == nullptr) return;
        expect (p->getParentComponent() == &root && root.getIndexOfChildComponent (p) > root.getIndexOfChildComponent (root.jam.get()),
                TXT ("알림 팝오버: 모달 층 (방 화면 위)"));
        expect (p->getPanelBounds().getX() == 9 && p->getPanelBounds().getBottom() == root.getHeight() - 55,
                TXT ("알림 팝오버: 프로필 줄 위 (왼쪽 9, 아래 55) ") + p->getPanelBounds().toString());
        if (auto* x = firstButton (*p)) click (*x);   // 머리말 닫기
    }, 300);
    add ([this, same]
    {
        expect (home().popover == nullptr && (jam()->notifications == nullptr), TXT ("알림 닫기 → 닫힘"));
        same (TXT ("알림 닫기"));
    }, 0);
    add ([this, sh] { if (sh->side) click (*root.sidebar, root.sidebar->devProfileRow().getCentre()); else click (jam()->bell); }, 300);
    add ([sh, profileMenuItem] { if (sh->side) profileMenuItem (0); }, 400);
    waitUntil (TXT ("알림 팝오버 다시 열림"), [this] { return home().popover != nullptr || jam()->notifications != nullptr; });
    add ([this] { jam()->grabKeyboardFocus(); key (juce::KeyPress (juce::KeyPress::escapeKey)); }, 300);
    add ([this, same]
    {
        expect (home().popover == nullptr && jam()->notifications == nullptr, TXT ("Esc → 알림 닫힘"));
        same (TXT ("알림 Esc"));
        jam()->devOpenPopover ("requests");   // 방 화면의 입장 요청 팝오버
    }, 300);
    add ([this, sh] { if (sh->side) click (*root.sidebar, root.sidebar->devProfileRow().getCentre()); }, 300);
    add ([sh, profileMenuItem] { if (sh->side) profileMenuItem (0); }, 400);
    add ([this, sh, same]
    {
        if (sh->side)
            expect (home().popover != nullptr && jam()->popover == nullptr, TXT ("팝오버는 하나만: 알림을 열면 방 화면의 입장 요청 팝오버는 닫힘"));
        same (TXT ("입장 요청 → 알림"));
        home().closeOverlays();
        jam()->closeOverlays();
    }, 300);
    // 프로필 › 설정 (설정 창은 에디터의 모달 층). 좁은 창은 방 화면 윗줄 설정 버튼
    add ([this, sh] { if (sh->side) click (*root.sidebar, root.sidebar->devProfileRow().getCentre()); else click (jam()->settings); }, 300);
    add ([sh, profileMenuItem] { if (sh->side) profileMenuItem (1); }, 700);
    waitUntil (TXT ("설정 열림"), settingsOpen);
    add ([same] { same (TXT ("프로필 › 설정 열기")); }, 0);
    add ([this] { if (auto* e = root.getParentComponent()) e->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)); }, 0);
    waitUntil (TXT ("설정 닫힘"), [settingsOpen] { return ! settingsOpen(); });
    add ([same] { same (TXT ("설정 닫기")); }, 300);
    // 사이드바 친구 + → 친구 추가 (좁은 창은 사이드바가 없어 홈의 창을 직접: 같은 모달 층인지만)
    add ([sh, friendPlus, this] { if (sh->side) friendPlus(); else home().openAddFriend(); }, 400);
    add ([this, same, fullModal]
    {
        fullModal (home().modal.get(), TXT ("친구 추가 (방 화면)"));
        same (TXT ("친구 추가 열기"));
    }, 0);
    add ([outside] { outside(); }, 400);
    add ([this, same]
    {
        expect (home().modal == nullptr, TXT ("덮개를 누르면 친구 추가 닫힘"));
        same (TXT ("친구 추가 닫기"));
        expect (jam()->hasKeyboardFocus (true), TXT ("닫으면 방 화면이 글자 입력을 받는다 (단축키 M·N)"));
    }, 0);
    // 방 화면의 모달 (방 설정: 방장)
    add ([this] { expect (jam()->gear.isShowing(), TXT ("방장: 방 설정 버튼")); click (jam()->gear); }, 400);
    add ([this, same, fullModal]
    {
        fullModal (jam()->modal.get(), TXT ("방 설정 (방 화면)"));
        same (TXT ("방 설정 열기"));
    }, 0);
    add ([outside] { outside(); }, 400);
    add ([this, same]
    {
        expect (jam()->modal == nullptr, TXT ("사이드바 위 덮개를 누르면 방 설정 닫힘"));
        same (TXT ("방 설정 닫기"));
    }, 0);
    // 사이드바 친구 줄 → 방 화면의 프로필 (좁은 창은 방 화면 멤버·친구 목록이 같은 창을 연다)
    add ([this, sh]
    {
        if (! sh->side)
        {
            for (auto& f : core.store().state().friends) { jam()->openProfile (f.user); return; }
            expect (false, TXT ("친구"));
            return;
        }
        for (auto& f : core.store().state().friends)
            if (f.status == "online" || f.status == "in_room")
            {
                const auto r = root.sidebar->devRow (f.user.nickname.value_or (f.user.username));
                if (! r.isEmpty()) { click (*root.sidebar, { r.getX() + 48, r.getCentreY() }); return; }
            }
        expect (false, TXT ("사이드바 친구 줄"));
    }, 0);
    waitUntil (TXT ("친구 프로필 열림"), [this] { return jam()->modal != nullptr; });
    add ([this, same, fullModal]
    {
        fullModal (jam()->modal.get(), TXT ("친구 프로필 (방 화면)"));
        expect (dynamic_cast<ProfileModal*> (jam()->modal.get()) != nullptr, TXT ("방 화면의 프로필 창"));
        same (TXT ("친구 프로필 열기"));
        key (juce::KeyPress (juce::KeyPress::escapeKey));
    }, 400);
    add ([this, same]
    {
        expect (jam()->modal == nullptr, TXT ("Esc → 프로필 닫힘"));
        same (TXT ("친구 프로필 닫기"));
    }, 0);
    // 토스트 닫기
    add ([this] { root.addInfoToast (TXT ("겹침 확인 토스트"), {}); }, 400);
    add ([this]
    {
        for (auto* t : root.toasts)
            if (t->getTitle() == TXT ("겹침 확인 토스트"))
                if (auto* b = firstButton (*t)) click (*b);
    }, 400);
    add ([this, same]
    {
        bool gone = true;
        for (auto* t : root.toasts) gone = gone && t->getTitle() != TXT ("겹침 확인 토스트");
        expect (gone, TXT ("토스트 닫기 → 닫힘"));
        same (TXT ("토스트 닫기"));
    }, 0);
    // 접힌 사이드바 (좁은 창은 사이드바가 없어 같은 확인을 한 번 더)
    add ([this, sh] { if (sh->side) root.sidebar->setCollapsed (true); }, 600);
    add ([this, sh]
    {
        if (! sh->side) { home().openAddFriend(); return; }
        expect (root.sidebar->devRow (TXT ("코드로 입장")).isEmpty(), TXT ("방에 있으면 사이드바 코드로 입장 없음 (접힘)"));
        const auto r = root.sidebar->devRow (TXT ("친구 추가"));
        expect (! r.isEmpty(), TXT ("접힌 사이드바: 친구 추가 아이콘"));
        if (! r.isEmpty()) click (*root.sidebar, r.getCentre());
    }, 400);
    add ([this, same, fullModal]
    {
        fullModal (home().modal.get(), TXT ("친구 추가 (접힌 사이드바)"));
        same (TXT ("접힌 사이드바: 친구 추가 열기"));
    }, 0);
    add ([outside] { outside(); }, 400);
    add ([this] { expect (home().modal == nullptr, TXT ("접힌 사이드바: 덮개 → 친구 추가 닫힘")); click (jam()->gear); }, 400);
    add ([this, same, fullModal]
    {
        fullModal (jam()->modal.get(), TXT ("방 설정 (접힌 사이드바)"));
        same (TXT ("접힌 사이드바: 방 설정 열기"));
    }, 0);
    add ([outside] { outside(); }, 400);
    add ([this, same]
    {
        expect (jam()->modal == nullptr, TXT ("접힌 사이드바: 덮개 → 방 설정 닫힘"));
        same (TXT ("접힌 사이드바: 닫기"));
        if (root.sidebar->isCollapsed()) root.sidebar->setCollapsed (false);
    }, 600);

    begin (TXT ("겹침: 공개 방에서 열고 닫아도 공개 방 그대로"));
    add ([this] { click (jam()->leave); });
    add ([this] { click (jam()->leave); }, 0);
    waitUntil (TXT ("방에서 나감"), [this] { return ! inRoom() && jam() == nullptr && home().isShowing(); }, 6000);
    closeToasts();
    add ([this]
    {
        expect (root.page == Sidebar::Page::rooms && home().roomViewport.isShowing(), TXT ("공개 방"));
        expect (! root.sidebar->devRow (TXT ("코드로 입장")).isEmpty(), TXT ("방 밖: 사이드바 코드로 입장 있음"));
    }, 0);
    add ([friendPlus] { friendPlus(); }, 400);
    add ([this, fullModal]
    {
        fullModal (home().modal.get(), TXT ("친구 추가 (공개 방)"));
        expect (! inRoom() && root.page == Sidebar::Page::rooms && home().roomViewport.isShowing(), TXT ("친구 추가 열기: 공개 방 그대로"));
    }, 0);
    add ([outside] { outside(); }, 400);
    add ([this] { expect (home().modal == nullptr && root.page == Sidebar::Page::rooms && home().roomViewport.isShowing(), TXT ("친구 추가 닫기: 공개 방 그대로")); }, 0);
    add ([this] { click (*root.sidebar, root.sidebar->devProfileRow().getCentre()); }, 300);
    add ([profileMenuItem] { profileMenuItem (0); }, 400);
    add ([this]
    {
        expect (home().popover != nullptr && root.page == Sidebar::Page::rooms && home().roomViewport.isShowing(), TXT ("알림 열기: 공개 방 그대로"));
        if (home().popover != nullptr) if (auto* x = firstButton (*home().popover)) click (*x);
    }, 300);
    add ([this] { expect (home().popover == nullptr && root.page == Sidebar::Page::rooms && home().roomViewport.isShowing(), TXT ("알림 닫기: 공개 방 그대로")); }, 0);
    // 코드로 입장 창이 열린 채로 초대를 수락해 방에 들어가면 그 창은 닫힌다
    clickSidebar (TXT ("코드로 입장"));
    add ([this, fullModal]
    {
        fullModal (home().modal.get(), TXT ("코드로 입장 (공개 방)"));
        expect (dynamic_cast<JoinCodeModal*> (home().modal.get()) != nullptr, TXT ("코드로 입장 창"));
        if (auto* f = core.fakeServer()) f->devInviteMe();
    }, 0);
    auto inviteToast = [this]() -> Toast*
    {
        for (auto* t : root.toasts) if (t->key.isNotEmpty() && ! t->key.startsWith ("jr:")) return t;
        return nullptr;
    };
    waitUntil (TXT ("초대 토스트 (코드로 입장 창 위)"), [inviteToast] { return inviteToast() != nullptr; });
    add ([this, inviteToast]
    {
        auto* t = inviteToast();
        expect (t != nullptr && root.getIndexOfChildComponent (t) > root.getIndexOfChildComponent (home().modal.get()), TXT ("토스트는 모달 층 위"));
        if (t != nullptr) if (auto* b = findButton (*t, TXT ("수락"))) click (*b);
    }, 0);
    waitUntil (TXT ("초대 수락 → 그 방 화면"), [this] { return inRoom() && jam() != nullptr && jam()->isShowing() && ! root.isMinimized(); }, 6000);
    add ([this]
    {
        expect (home().modal == nullptr && ! root.hasWindowModal(), TXT ("방에 들어가면 코드로 입장 창은 닫힘"));
        expect (root.sidebar->devRow (TXT ("코드로 입장")).isEmpty(), TXT ("방에 들어가면 사이드바 코드로 입장 없음"));
    }, 300);
}

//==============================================================================
// 내 오디오·모니터 카드 묶음 (AudioDock): 방 화면·공개 방이 같이 쓰는 하나. 공개 방에서는 목록 위에 떠 있고(그림자),
// 틈·바깥은 뒤의 방 카드가 받는다. 목록 아래 여백 = 카드 높이 + 36. 미터만 다시 그린다. 오갈 때 폭·그림자가 이어진다 (320ms)
void UiSelfTest::addDockTests()
{
    struct Shared
    {
        juce::Rectangle<int> roomCards, roomsCards, mid;
        float midShadow = -1.0f;
        int toasts = 0;
        bool retry = false;
        juce::uint32 countFrom = 0;
        bool gapCard = false;
        bool sendMuted = false;
        int phase = 0;
    };
    auto sh = std::make_shared<Shared>();
    auto dockRoomSlot = [this]() -> juce::Rectangle<int>
    {
        auto* j = jam();
        return j != nullptr ? j->dockSlot() + j->getPosition() : juce::Rectangle<int>();
    };
    auto dockRoomsSlot = [this]() -> juce::Rectangle<int> { return home().dockSlot() + home().getPosition(); };
    auto roomsView = [this] { return inRoom() && root.isMinimized() && home().isShowing(); };
    auto listBottomPad = [this]() -> int
    {
        int maxBottom = 0;
        for (auto* c : home().devRoomCardComps()) maxBottom = juce::jmax (maxBottom, c->getBottom());
        return home().devRoomList()->getHeight() - maxBottom;
    };
    auto scrollable = [this] { return home().devRoomList()->getHeight() > home().roomViewport.getHeight(); };
    auto checkRoomsLayout = [this, dockRoomsSlot, listBottomPad, scrollable] (const juce::String& where)
    {
        auto* d = root.dock.get();
        expect (d != nullptr && d->isVisible(), where + TXT (": 카드 묶음 보임"));
        if (d == nullptr) return;
        const auto list = home().roomViewport.getBounds() + home().getPosition();
        const auto cards = d->getCardsBounds();
        expect (cards == dockRoomsSlot(), where + TXT (": 카드 자리 = 공개 방 자리 ") + cards.toString());
        expect (cards.getX() == list.getX() + 18 && cards.getRight() == list.getRight() - 18 && cards.getBottom() == list.getBottom() - 18,
                where + TXT (": 목록 영역 기준 왼쪽·오른쪽·아래 18 (") + cards.toString() + " / " + list.toString() + ")");
        expect (cards.getHeight() == d->getCardsHeight(), where + TXT (": 높이 = 카드 높이"));
        const auto s = d->sendCard.getBounds(), m = d->monCard.getBounds();
        if (d->isSendOpen() && d->isMonOpen())
            expect (std::abs (s.getWidth() - m.getWidth()) <= 1, where + TXT (": 두 카드 같은 폭 (") + juce::String (s.getWidth()) + " / " + juce::String (m.getWidth()) + ")");
        expect (m.getX() - s.getRight() == 9, where + TXT (": 카드 사이 9"));
        expect (d->getShadow() == 1.0f, where + TXT (": 팝오버 그림자"));
        if (scrollable())
            expect (listBottomPad() == d->getCardsHeight() + 36, where + TXT (": 목록 아래 여백 = 카드 높이 + 위아래 18 (") + juce::String (listBottomPad()) + ")");
    };
    auto checkRoomLayout = [this, dockRoomSlot] (const juce::String& where)
    {
        auto* d = root.dock.get();
        auto* j = jam();
        expect (d != nullptr && d->isVisible() && j != nullptr, where + TXT (": 방 화면 카드 묶음"));
        if (d == nullptr || j == nullptr) return;
        const auto cards = d->getCardsBounds();
        expect (cards == dockRoomSlot(), where + TXT (": 카드 자리 = 방 화면 자리 ") + cards.toString());
        expect (cards.getX() == j->getX() + 18 && cards.getBottom() == root.getHeight() - 18, where + TXT (": 왼쪽 18 · 아래 18"));
        expect (cards.getRight() + 18 == j->getX() + j->chatCard.getX(), where + TXT (": 오른쪽 = 채팅 영역 왼쪽 - 18"));
        expect (d->getShadow() == 0.0f, where + TXT (": 그림자 없음"));
        expect (dockOverlaps().isEmpty(), where + TXT (": 카드 안 겹침 없음 ") + dockOverlaps());
    };
    auto clickRooms = [this]
    {
        if (root.sidebar->isVisible() && ! root.sidebar->devRow (TXT ("공개 방")).isEmpty()) click (*root.sidebar, root.sidebar->devRow (TXT ("공개 방")).getCentre());
        else if (auto* j = jam()) click (j->lobby);   // 좁은 플러그인 창: 방 화면 윗줄 로비 보기
    };
    auto clickRoom = [this]
    {
        const auto& s = core.store().state();
        const auto r = s.currentRoom ? root.sidebar->devRow (s.currentRoom->title) : juce::Rectangle<int>();
        if (! r.isEmpty()) click (*root.sidebar, r.getCentre());
        else root.setMinimized (false);
    };

    begin (TXT ("내 오디오·모니터: 방 밖에는 없다"));
    add ([this] { if (inRoom()) core.home().leaveRoom(); }, 0);
    waitUntil (TXT ("방 밖 공개 방"), [this] { return ! inRoom() && jam() == nullptr && home().isShowing(); }, 6000);
    closeToasts();
    add ([this, listBottomPad, scrollable]
    {
        expect (root.dock == nullptr, TXT ("방 밖: 카드 묶음 없음"));
        expect (home().dockSlot().isEmpty() && home().dockH == 0, TXT ("방 밖: 카드 자리 없음"));
        if (scrollable()) expect (listBottomPad() == metric::pagePad, TXT ("방 밖: 목록 아래 여백 18 그대로 (") + juce::String (listBottomPad()) + ")");
        else std::cerr << "UITEST INFO dock: 목록이 짧아 스크롤 없음 (아래 여백 확인 생략)" << std::endl;
    }, 0);

    begin (TXT ("내 오디오·모니터: 방 화면"));
    enterNewRoom();
    closeToasts();
    add ([this, sh, checkRoomLayout]
    {
        if (root.sidebar->isCollapsed()) root.sidebar->setCollapsed (false);
        checkRoomLayout (TXT ("방 화면"));
        auto* d = root.dock.get();
        if (d == nullptr) return;
        for (auto* m : std::initializer_list<juce::Component*> { &d->inputMeter, &d->outputMeter })
            expect (m->getParentComponent() == &root && m->isOpaque() && root.getIndexOfChildComponent (m) > root.getIndexOfChildComponent (d),
                    TXT ("미터 상자: 카드 묶음 바로 위 형제, 불투명"));
        expect (root.getIndexOfChildComponent (d) > root.getIndexOfChildComponent (root.jam.get())
                 && root.getIndexOfChildComponent (d) > root.getIndexOfChildComponent (root.home.get()), TXT ("카드 묶음: 방 화면·홈 위"));
        sh->roomCards = d->getCardsBounds();
        sh->phase = (int) core.jam()->getPhase();
    }, 300);
    // 단축키 M (방 화면) → 카드 묶음에 바로
    add ([this, sh]
    {
        sh->sendMuted = core.jam()->isSendMuted();
        jam()->grabKeyboardFocus();
        key (juce::KeyPress ('m', juce::ModifierKeys(), 'm'));
    }, 200);
    add ([this, sh]
    {
        auto* d = root.dock.get();
        expect (core.jam()->isSendMuted() != sh->sendMuted && d->sendMute.isOn() == core.jam()->isSendMuted()
                 && d->sendMute.getButtonText() == (core.jam()->isSendMuted() ? TXT ("송출 재개") : TXT ("송출 정지")),
                TXT ("단축키 M → 카드의 송출 정지 그대로 반영"));
        core.jam()->setSendGainDb (-3.0f);   // 바깥에서 바꾼 값 (설정·DAW 등)
    }, 900);
    add ([this]
    {
        expect (std::abs (root.dock->sendGain.getValue() - (-3.0f)) < 0.01f, TXT ("바깥에서 바꾼 송출 레벨 → 카드에 0.4초 안에 반영"));
    }, 0);

    // 방 화면 → 공개 방: 같은 카드가 폭만 늘고 그림자가 생긴다
    begin (TXT ("내 오디오·모니터: 방 화면 ↔ 공개 방"));
    add ([clickRooms] { clickRooms(); }, 110);
    add ([this, sh]
    {
        auto* d = root.dock.get();
        sh->mid = d->getCardsBounds();
        sh->midShadow = d->getShadow();
    }, 500);
    waitUntil (TXT ("공개 방"), roomsView);
    add ([this, sh, checkRoomsLayout]
    {
        checkRoomsLayout (TXT ("공개 방"));
        auto* d = root.dock.get();
        sh->roomsCards = d->getCardsBounds();
        const auto a = sh->roomCards, b = sh->roomsCards, m = sh->mid;
        std::cerr << "UITEST INFO dock flip to rooms: room " << a.toString() << " mid " << m.toString() << " shadow " << sh->midShadow << " rooms " << b.toString() << std::endl;
        expect (m.getBottom() == a.getBottom() && b.getBottom() == a.getBottom(), TXT ("전환 중: 바닥 그대로"));
        if (a.getX() == b.getX())
        {
            expect (m.getX() == a.getX(), TXT ("전환 중: 왼쪽 끝 그대로"));
            expect (m.getWidth() > juce::jmin (a.getWidth(), b.getWidth()) && m.getWidth() < juce::jmax (a.getWidth(), b.getWidth()),
                    TXT ("전환 중: 폭만 두 화면 사이 (") + juce::String (m.getWidth()) + ")");
        }
        else   // 방 화면에서 사이드바가 숨거나(좁은 플러그인 창) 잠시 접히면(좁은 창) 왼쪽 끝이 두 화면에서 다르다
            std::cerr << "UITEST INFO dock: 두 화면의 왼쪽 끝이 다름 (방 화면 " << a.getX() << ", 공개 방 " << b.getX() << "): 왼쪽 끝도 같이 옮긴다" << std::endl;
        expect (sh->midShadow > 0.0f && sh->midShadow < 1.0f, TXT ("전환 중: 그림자가 서서히 생긴다 (") + juce::String (sh->midShadow, 2) + ")");
    }, 0);
    // 공개 방: 틈·바깥은 뒤의 방 목록이 받는다
    add ([this]
    {
        auto* d = root.dock.get();
        const auto s = d->sendCard.getBounds() + d->getPosition(), m = d->monCard.getBounds() + d->getPosition();
        const juce::Point<int> gap ((s.getRight() + m.getX()) / 2, s.getCentreY());
        const juce::Point<int> outsideLeft (s.getX() - 8, s.getCentreY()), outsideTop (s.getCentreX(), s.getY() - 8);
        for (auto p : { gap, outsideLeft, outsideTop })
        {
            auto* hit = root.getComponentAt (p);
            expect (hit != nullptr && hit != d && ! d->isParentOf (hit) && (hit == root.home.get() || root.home->isParentOf (hit)),
                    TXT ("카드 틈·바깥 ") + p.toString() + TXT (" → 뒤의 방 목록이 받는다"));
        }
        auto* hitCard = root.getComponentAt (s.getCentre());
        expect (hitCard == &d->sendCard || d->sendCard.isParentOf (hitCard) || hitCard == &d->inputMeter || d->inputMeter.isParentOf (hitCard),
                TXT ("카드 위는 카드가 받는다"));
    }, 0);
    // 목록을 내려 카드 틈(9) 뒤에 방 카드가 오게 한 뒤: 틈을 누르면 그 방 카드가 받는다.
    // 2열이면 카드 틈이 방 목록의 열 사이(9)와 겹쳐 방 카드가 오지 않으므로 사이드바를 접어 3열로
    add ([this] { if (root.sidebar->isVisible()) root.sidebar->setCollapsed (true); }, 500);
    add ([this, sh]
    {
        sh->gapCard = false;
        auto* d = root.dock.get();
        const auto s = d->sendCard.getBounds() + d->getPosition(), m = d->monCard.getBounds() + d->getPosition();
        const juce::Point<int> gap ((s.getRight() + m.getX()) / 2, s.getCentreY());
        juce::Component* under = nullptr;
        for (int y = 0; y < 4000 && under == nullptr; y += 12)
        {
            home().roomViewport.setViewPosition (0, y);
            for (auto* c : home().devRoomCardComps())
                if (root.getLocalArea (c, c->getLocalBounds()).contains (gap)) under = c;
            if (home().roomViewport.getViewPositionY() < y) break;   // 끝
        }
        if (under == nullptr) { std::cerr << "UITEST INFO dock: 카드 틈 뒤에 오는 방 카드가 없음 (목록이 짧거나 열 사이와 겹침)" << std::endl; return; }
        sh->gapCard = true;
        auto* hit = root.getComponentAt (gap);
        expect (hit == under || under->isParentOf (hit), TXT ("카드 틈 뒤의 방 카드가 보이고 누름을 받는다"));
        moveTo (root, gap);
    }, 200);
    add ([this, sh]
    {
        if (! sh->gapCard) return;
        auto* d = root.dock.get();
        const auto s = d->sendCard.getBounds() + d->getPosition(), m = d->monCard.getBounds() + d->getPosition();
        const juce::Point<int> gap ((s.getRight() + m.getX()) / 2, s.getCentreY());
        if (auto* hit = root.getComponentAt (gap); hit != nullptr && hit != d)
            expect (hit->isMouseOver (true) || hit->getParentComponent()->isMouseOver (true), TXT ("카드 틈에 마우스를 올리면 뒤의 방 카드 위"));
        home().roomViewport.setViewPosition (0, 0);
        if (root.sidebar->isCollapsed()) root.sidebar->setCollapsed (false);
    }, 500);
    // 끝까지 내리면 마지막 방이 카드 위로 다 올라온다
    add ([this] { home().roomViewport.setViewPosition (0, 1 << 20); }, 300);
    add ([this, scrollable]
    {
        if (! scrollable()) { std::cerr << "UITEST INFO dock: 목록이 짧아 스크롤 없음" << std::endl; return; }
        auto* d = root.dock.get();
        int lowest = 0;
        for (auto* c : home().devRoomCardComps()) lowest = juce::jmax (lowest, root.getLocalArea (c, c->getLocalBounds()).getBottom());
        expect (lowest <= d->getCardsBounds().getY() - 18, TXT ("끝까지 내리면 마지막 방 아래 = 카드 위 18 (") + juce::String (lowest) + " / " + juce::String (d->getCardsBounds().getY()) + ")");
        home().roomViewport.setViewPosition (0, 0);
    }, 200);
    // 다시 그리기: 미터만 (카드·뒤의 방 카드·화면은 그리지 않는다)
    closeToasts();
    add ([this, sh]
    {
        root.dock->devDriveMeters (true);
        moveTo (*root.sidebar, { 120, 400 });
    }, 400);
    auto resetCounts = [this, sh]
    {
        auto* d = root.dock.get();
        MeterBox::devPaints = 0; AudioDock::devPaintCount = 0; HomeScreen::devRoomCardPaints = 0; HomeScreen::devPaints = 0; JamScreen::devPaints = 0;
        d->sendCard.devPaintCount = 0; d->monCard.devPaintCount = 0;
        devprobe::watch.clear();
        devprobe::watch.add (d->inputMeter.getScreenBounds());
        devprobe::watch.add (d->outputMeter.getScreenBounds());
        devprobe::hits = 0;
        devprobe::hitNames.clear();
        sh->toasts = root.toasts.size();
    };
    auto countsLine = [this] (const juce::String& where)
    {
        auto* d = root.dock.get();
        std::cerr << "UITEST INFO repaint " << where << ": meter " << MeterBox::devPaints << " dock " << AudioDock::devPaintCount
                  << " cards " << d->sendCard.devPaintCount << "+" << d->monCard.devPaintCount << " roomcards " << HomeScreen::devRoomCardPaints
                  << " home " << HomeScreen::devPaints << " jam " << JamScreen::devPaints
                  << " | 미터 칸 안에서만 같이 그려진 것 " << devprobe::hits << " " << devprobe::hitNames.joinIntoString (",") << std::endl;
    };
    add ([resetCounts] { resetCounts(); }, 1000);
    add ([this, sh, resetCounts]
    {
        // 그동안 토스트가 생기거나 사라졌으면(뒤가 다시 그려진다) 한 번 더 센다
        sh->retry = root.toasts.size() != sh->toasts;
        if (sh->retry) { std::cerr << "UITEST INFO repaint: 토스트가 바뀌어 한 번 더 센다" << std::endl; resetCounts(); }
        sh->countFrom = juce::Time::getMillisecondCounter();
    }, 0);
    waitUntil (TXT ("다시 셈"), [sh] { return ! sh->retry || juce::Time::getMillisecondCounter() - sh->countFrom >= 1000; }, 3000);
    add ([this, sh, countsLine]
    {
        countsLine (TXT ("공개 방"));
        auto* d = root.dock.get();
        expect (MeterBox::devPaints >= 10, TXT ("공개 방: 미터는 다시 그린다 (") + juce::String (MeterBox::devPaints) + ")");
        // 미터가 다시 그려질 때 같이 그려진 것이 없다 (카드·방 목록·방 카드·화면). 방 목록이 새로 오는 등 다른 이유의 넓은 그리기는 세지 않는다
        expect (devprobe::hits == 0, TXT ("공개 방: 미터 칸만 다시 그린다 (같이 그려진 것 ") + devprobe::hitNames.joinIntoString (",") + ")");
        devprobe::watch.clear();
        expect (d->devShadow (0).getBuildCount() == 1 && d->devShadow (1).getBuildCount() == 1, TXT ("그림자: 한 번만 계산 (") + juce::String (d->devShadow (0).getBuildCount()) + ")");
    }, 0);
    // 방 화면도
    add ([clickRoom] { clickRoom(); }, 110);
    add ([this, sh]
    {
        auto* d = root.dock.get();
        sh->mid = d->getCardsBounds();
        sh->midShadow = d->getShadow();
        expect (root.getIndexOfChildComponent (d) > root.getIndexOfChildComponent (root.jam.get()), TXT ("돌아가는 중: 카드는 방 화면 위"));
        if (auto* j = jam()) expect (sh->mid.getRight() > j->getX() + j->chatCard.getX(), TXT ("돌아가는 중: 카드가 오른쪽 채팅 영역 위에 겹쳐 보인다"));
    }, 500);
    add ([this, sh, checkRoomLayout, countsLine]
    {
        checkRoomLayout (TXT ("방 화면으로 돌아옴"));
        expect (sh->midShadow > 0.0f && sh->midShadow < 1.0f, TXT ("돌아가는 중: 그림자가 서서히 사라진다 (") + juce::String (sh->midShadow, 2) + ")");
        moveTo (*root.sidebar, { 120, 400 });
    }, 400);
    add ([resetCounts] { resetCounts(); }, 1000);
    add ([this, countsLine]
    {
        countsLine (TXT ("방 화면"));
        auto* d = root.dock.get();
        expect (MeterBox::devPaints >= 10, TXT ("방 화면: 미터는 다시 그린다"));
        expect (devprobe::hits == 0 && AudioDock::devPaintCount == 0 && d->sendCard.devPaintCount == 0 && d->monCard.devPaintCount == 0,
                TXT ("방 화면: 미터 칸만 다시 그린다 (같이 그려진 것 ") + devprobe::hitNames.joinIntoString (",") + ")");
        devprobe::watch.clear();
        root.dock->devDriveMeters (false);
    }, 0);

    // 여러 번 오가도 어긋나지 않는다 (사이드바 펼침·접힘)
    for (int round = 0; round < 4; ++round)
    {
        const bool collapse = round >= 2;
        add ([this, collapse] { if (root.sidebar->isVisible() && root.sidebar->isCollapsed() != collapse) root.sidebar->setCollapsed (collapse); }, 400);
        add ([clickRooms] { clickRooms(); }, 450);
        add ([checkRoomsLayout, round] { checkRoomsLayout (TXT ("공개 방 ") + juce::String (round + 1) + TXT ("번째")); }, 0);
        add ([clickRoom] { clickRoom(); }, 450);
        add ([checkRoomLayout, round] { checkRoomLayout (TXT ("방 화면 ") + juce::String (round + 1) + TXT ("번째")); }, 0);
    }
    add ([this] { if (root.sidebar->isCollapsed()) root.sidebar->setCollapsed (false); }, 400);
    // 전환 중에도 송출 조작은 그대로 (소리·방 상태)
    add ([clickRooms] { clickRooms(); }, 90);
    add ([this, sh]
    {
        sh->sendMuted = core.jam()->isSendMuted();
        click (root.dock->sendMute);
    }, 450);
    add ([this, sh]
    {
        expect (core.jam()->isSendMuted() != sh->sendMuted && (int) core.jam()->getPhase() == sh->phase, TXT ("전환 중 송출 정지 누름 → 바로 반영, 연결 그대로"));
        click (root.dock->sendMute);   // 되돌림
    }, 300);
    add ([this, sh] { expect (core.jam()->isSendMuted() == sh->sendMuted, TXT ("공개 방에서 누름 → 같은 상태 (화면 밖 공통 상태)")); }, 0);
    // 창 크기를 바꿔도 자리를 따라간다
    add ([this] { if (auto* p = peer()) { auto b = p->getBounds(); p->setBounds (b.withSize (juce::jmax (1000, b.getWidth() - 180), juce::jmax (600, b.getHeight() - 80)), false); } }, 600);
    add ([checkRoomsLayout] { checkRoomsLayout (TXT ("작은 창 공개 방")); }, 0);
    add ([clickRoom] { clickRoom(); }, 450);
    add ([checkRoomLayout] { checkRoomLayout (TXT ("작은 창 방 화면")); }, 0);
    add ([this] { if (auto* p = peer()) { auto b = p->getBounds(); p->setBounds (b.withSize (b.getWidth() + 180, b.getHeight() + 80), false); } }, 600);
    add ([checkRoomLayout] { checkRoomLayout (TXT ("창 되돌림")); }, 0);

    begin (TXT ("내 오디오·모니터: 방에서 나가면 없어진다"));
    add ([this] { click (jam()->leave); });
    add ([this] { click (jam()->leave); }, 0);
    waitUntil (TXT ("방에서 나감"), [this] { return ! inRoom() && jam() == nullptr && home().isShowing(); }, 6000);
    add ([this, listBottomPad, scrollable]
    {
        expect (root.dock == nullptr && home().dockSlot().isEmpty(), TXT ("나가면 카드 묶음·자리 없음"));
        if (scrollable()) expect (listBottomPad() == metric::pagePad, TXT ("나가면 목록 아래 여백 18"));
        // 미터 상자도 함께 빠졌다
        for (auto* c : root.getChildren()) expect (dynamic_cast<MeterBox*> (c) == nullptr, TXT ("미터 상자 남지 않음"));
    }, 0);
}

//==============================================================================
// 모달 애니메이션 (공통 래퍼 ModalBase): 열기 240ms 끝나면 정확히 100%, 닫기는 누름을 받지 않는 그림이 180ms 뒤 사라진다.
// 다섯 모달(설정·방 만들기·친구 추가·코드로 입장·친구 프로필)을 Esc·닫기(또는 취소) 버튼·바깥 누름으로 닫는다
void UiSelfTest::addModalAnimTests()
{
    enum class Kind { settings, create, friendAdd, joinCode, profile };
    enum class How { esc, button, outside };
    auto current = [this] (Kind k) -> ModalBase*
    {
        if (k == Kind::settings)
        {
            if (auto* e = root.getParentComponent())
                for (auto* c : e->getChildren()) if (auto* s = dynamic_cast<SettingsModal*> (c)) return s;
            return nullptr;
        }
        return home().modal.get();
    };
    auto ghost = [this] (Kind k) -> juce::Component*
    {
        auto* parent = k == Kind::settings ? root.getParentComponent() : static_cast<juce::Component*> (&root);
        if (parent == nullptr) return nullptr;
        for (auto* c : parent->getChildren()) if (c->getComponentID() == "modalCloseGhost") return c;
        return nullptr;
    };
    auto open = [this] (Kind k)
    {
        switch (k)
        {
            case Kind::settings:  if (root.onOpenSettings) root.onOpenSettings (0); break;
            case Kind::create:    home().openCreateRoom(); break;
            case Kind::friendAdd: home().openAddFriend(); break;
            case Kind::joinCode:  home().openJoinCode(); break;
            case Kind::profile:
                for (auto& f : core.store().state().friends) { home().openProfile (f.user); break; }
                break;
        }
    };
    const char* names[] = { "설정", "방 만들기", "친구 추가", "코드로 입장", "친구 프로필" };
    const char* hows[] = { "Esc", "닫기 버튼", "바깥 누름" };

    begin (TXT ("모달 애니메이션"));
    add ([this] { if (inRoom()) core.home().leaveRoom(); }, 0);
    waitUntil (TXT ("공개 방"), [this] { return ! inRoom() && home().isShowing(); }, 6000);
    closeToasts();
    for (int ki = 0; ki < 5; ++ki)
        for (int hi = 0; hi < 3; ++hi)
        {
            const auto k = (Kind) ki;
            const auto how = (How) hi;
            const juce::String label = juce::String (juce::CharPointer_UTF8 (names[ki])) + " · " + juce::String (juce::CharPointer_UTF8 (hows[hi]));
            add ([open, k] { open (k); }, 40);
            waitUntil (label + TXT (": 열림"), [current, k] { return current (k) != nullptr; }, 2000);
            add ([this, current, k, label]
            {
                auto* m = current (k);
                if (m == nullptr) return;
                const auto st = m->getAnimState();
                expect (m->isOpening() && st.box < 1.0f && st.dim < 1.0f, label + TXT (": 여는 중 (덮개 ") + juce::String (st.dim, 2) + TXT (", 상자 ") + juce::String (st.box, 2) + ")");
            }, 350);
            add ([this, current, k, label, how]
            {
                auto* m = current (k);
                if (m == nullptr) return;
                const auto st = m->getAnimState();
                bool identity = true;
                for (auto* c : m->getChildren()) identity = identity && ! c->isTransformed() && c->getAlpha() == 1.0f;
                expect (! m->isOpening() && st.dim == 1.0f && st.box == 1.0f && st.dy == 0.0f && st.scale == 1.0f && identity,
                        label + TXT (": 열기 끝 → 변환 없음 (정확히 100%), 불투명"));
                switch (how)
                {
                    case How::esc: key (juce::KeyPress (juce::KeyPress::escapeKey)); break;
                    case How::button:
                    {
                        juce::Button* b = nullptr;
                        if (auto* s = dynamic_cast<SettingsModal*> (m)) b = &s->close;
                        else if (auto* md = dynamic_cast<Modal*> (m); md != nullptr && md->closeButton.isShowing()) b = &md->closeButton;
                        else b = findButton (*m, TXT ("취소"));   // 닫기 버튼이 없는 창 (방 만들기·코드로 입장): 취소
                        expect (b != nullptr, label + TXT (": 닫기(취소) 버튼"));
                        if (b != nullptr) click (*b);
                        break;
                    }
                    case How::outside: click (*m, { 6, m->getHeight() / 2 }); break;
                }
            }, 60);
            add ([this, current, ghost, k, label]
            {
                expect (current (k) == nullptr, label + TXT (": 닫힘 (모달은 바로 빠진다)"));
                auto* g = ghost (k);
                expect (g != nullptr, label + TXT (": 닫히는 그림이 남아 있다"));
                if (g != nullptr)
                {
                    auto* parent = g->getParentComponent();
                    auto* hit = parent->getComponentAt (g->getBounds().getCentre());
                    bool self = true, kids = true;
                    g->getInterceptsMouseClicks (self, kids);
                    expect (hit != g && ! self, label + TXT (": 닫히는 동안 누름을 받지 않는다"));
                }
            }, 300);
            add ([ghost, k, label, this] { expect (ghost (k) == nullptr, label + TXT (": 끝나면 화면에서 빠진다")); }, 100);
        }
}

//==============================================================================
// 동작 줄이기 (MEON_REDUCE_MOTION=1 로 켜서 돌린다): 모달·카드 전환·사이드바 접기 애니메이션 없이 바로
void UiSelfTest::addReducedMotionTests()
{
    begin (TXT ("동작 줄이기"));
    add ([this] { expect (systemPrefersReducedMotion(), TXT ("동작 줄이기 켜짐 (MEON_REDUCE_MOTION=1)")); if (inRoom()) core.home().leaveRoom(); }, 0);
    waitUntil (TXT ("공개 방"), [this] { return ! inRoom() && home().isShowing(); }, 6000);
    closeToasts();
    add ([this] { home().openAddFriend(); }, 30);
    add ([this]
    {
        auto* m = home().modal.get();
        expect (m != nullptr && ! m->isOpening() && m->getAnimState().box == 1.0f, TXT ("모달: 열기 애니메이션 없이 바로"));
        key (juce::KeyPress (juce::KeyPress::escapeKey));
    }, 60);
    add ([this]
    {
        bool g = false;
        for (auto* c : root.getChildren()) g = g || c->getComponentID() == "modalCloseGhost";
        expect (home().modal == nullptr && ! g, TXT ("모달: 닫기 애니메이션 없이 바로"));
        root.sidebar->toggleCollapsed();
    }, 30);
    add ([this]
    {
        expect (root.sidebar->getWidth() == Sidebar::closedW, TXT ("사이드바 접기: 애니메이션 없이 바로 (") + juce::String (root.sidebar->getWidth()) + ")");
        root.sidebar->toggleCollapsed();
    }, 30);
    add ([this] { expect (root.sidebar->getWidth() == Sidebar::openW, TXT ("사이드바 펼치기: 바로")); }, 0);
    enterNewRoom();
    add ([this]
    {
        const auto r = root.sidebar->devRow (TXT ("공개 방"));
        if (! r.isEmpty()) click (*root.sidebar, r.getCentre()); else root.setMinimized (true);
    }, 30);
    add ([this]
    {
        auto* d = root.dock.get();
        expect (d != nullptr && d->getCardsBounds() == home().dockSlot() + home().getPosition() && d->getShadow() == 1.0f,
                TXT ("카드 전환: 애니메이션 없이 바로 공개 방 자리·그림자"));
        root.setMinimized (false);
    }, 30);
    add ([this]
    {
        auto* d = root.dock.get();
        auto* j = jam();
        expect (d != nullptr && j != nullptr && d->getCardsBounds() == j->dockSlot() + j->getPosition() && d->getShadow() == 0.0f,
                TXT ("카드 전환: 바로 방 화면 자리"));
    }, 0);
}

//==============================================================================
// 윈도우 창 가장자리 (왼쪽·오른쪽·아래 진한 선 조사·확인): 흰 바탕 창을 뒤에 깔고(투명한 크기 조절 틀 자리가 흰색으로 보이게)
// 가장자리 픽셀 줄을 읽는다. --no-winframe 이면 운영체제 제목 줄 그대로인 창(비교 기준). windows.yml (윈도우 10·11 러너)
void UiSelfTest::addWinEdgeTests()
{
    begin (TXT ("윈도우 창 가장자리"));
    if (! juce::SystemStats::getOperatingSystemName().startsWithIgnoreCase ("Windows"))
    {
        add ([] { std::cerr << "UITEST INFO winedge: 윈도우가 아니라 건너뜀" << std::endl; }, 0);
        return;
    }
    struct Plain : juce::Component
    {
        explicit Plain (juce::Colour c) : colour (c) {}
        void paint (juce::Graphics& g) override { g.fillAll (colour); }
        juce::Colour colour;
    };
    struct Shared { std::unique_ptr<Plain> backdrop, other; };
    auto sh = std::make_shared<Shared>();
    auto handle = [this]() -> void* { auto* p = peer(); return p != nullptr ? p->getNativeHandle() : nullptr; };
    auto rect = [] (const int r[4]) { return juce::String (r[0]) + "," + juce::String (r[1]) + " " + juce::String (r[2]) + "x" + juce::String (r[3]); };
    // 픽셀 문자열 "RRGGBB RRGGBB …" 에서 가장 어두운 밝기 (0~255)
    auto darkest = [] (const std::string& px)
    {
        int best = 255;
        juce::StringArray t;
        t.addTokens (juce::String (px), " ", "");
        t.removeEmptyStrings();
        for (auto& s : t)
        {
            const auto v = (unsigned) s.getHexValue32();
            const int r = (int) ((v >> 16) & 0xFF), g = (int) ((v >> 8) & 0xFF), b = (int) (v & 0xFF);
            best = juce::jmin (best, (r * 299 + g * 587 + b * 114) / 1000);
        }
        return best;
    };
    auto sample = [this, handle, rect, darkest] (const juce::String& label)
    {
        const auto p = meon::winframe::probe (handle());
        const auto f = meon::winframe::frameInfo (handle());
        std::cerr << "UITEST INFO winedge " << label << ": os " << p.os << " build " << f.build << " installed " << (p.installed ? 1 : 0)
                  << " max " << (p.maximised ? 1 : 0) << " active " << (p.active ? 1 : 0) << " dpi " << p.dpi
                  << " window " << rect (p.window) << " client " << rect (p.client) << " visibleFrame " << rect (f.frameBounds)
                  << " composition " << (f.composition ? 1 : 0) << " ncRendering " << (f.ncRendering ? 1 : 0)
                  << " style " << juce::String::toHexString ((int) f.style) << " exStyle " << juce::String::toHexString ((int) f.exStyle)
                  << " classStyle " << juce::String::toHexString ((int) f.classStyle)
                  << " colorization " << juce::String::toHexString ((int) f.colorization) << " accentOnBorders " << f.colorPrevalence
                  << " borderColour(DWMWA_BORDER_COLOR) " << f.borderColourResult << std::endl;
        const int cx = p.client[0] + p.client[2] / 2, cy = p.client[1] + p.client[3] / 2;
        const int cl = p.client[0], cr = p.client[0] + p.client[2], cb = p.client[1] + p.client[3];
        const int wl = p.window[0], wr = p.window[0] + p.window[2], wb = p.window[1] + p.window[3];
        auto line = [&] (const char* name, int x, int y, int w, int h)
        {
            const auto px = meon::winframe::devScreenPixels (x, y, w, h);
            std::cerr << "UITEST INFO winedge " << label << " " << name << " @" << x << "," << y << " " << w << "x" << h << ": " << px << std::endl;
            return px;
        };
        if (p.maximised)
        {
            // 최대화: 틀은 화면 밖. 작업 영역 가장자리(내용 끝)에 선이 없는지
            const int ml = p.work[0], mr = p.work[0] + p.work[2], mb = p.work[1] + p.work[3];
            const auto a = line ("workLeft", ml, cy, 4, 1), b = line ("workRight", mr - 4, cy, 4, 1), c = line ("workBottom", cx, mb - 4, 1, 4);
            const int d = juce::jmin (darkest (a), darkest (b), darkest (c));
            std::cerr << "UITEST INFO winedge " << label << " darkest at work-area edges " << d << std::endl;
            expect (d >= 200, label + TXT (": 최대화한 내용 가장자리에 진한 선 없음 (가장 어두운 밝기 ") + juce::String (d) + ")");
            return;
        }
        // 보통: 창 틀(내용 밖) 줄과 내용 안쪽 3px. 화면(모니터) 밖으로 나간 쪽은 건너뛴다
        const int ml = p.monitor[0], mr = p.monitor[0] + p.monitor[2], mb = p.monitor[1] + p.monitor[3];
        juce::String dark;
        if (wl - 2 >= ml)
        {
            const auto left = line ("left", wl - 2, cy, (cl + 3) - (wl - 2), 1);
            line ("bottomLeftCorner", cl - 9, cb - 2, 12, 1);
            dark << " left " << darkest (left.substr (0, (size_t) (cl - (wl - 2)) * 7));
        }
        if (wr + 2 <= mr)
        {
            const auto right = line ("right", cr - 3, cy, (wr + 2) - (cr - 3), 1);
            dark << " right " << darkest (right.substr ((size_t) 3 * 7));
        }
        if (wb + 2 <= mb)
        {
            const auto bottom = line ("bottom", cx, cb - 3, 1, (wb + 2) - (cb - 3));
            dark << " bottom " << darkest (bottom.substr ((size_t) 3 * 7));
        }
        line ("topEdge", cx, p.client[1] - 2, 1, 5);
        // 틀 자리(내용 밖)에서 가장 어두운 밝기: 흰 바탕 + 운영체제 그림자·1px 테두리만이면 밝다
        std::cerr << "UITEST INFO winedge " << label << " darkest outside content:" << dark << std::endl;
        if (! p.installed) return;   // 비교용 (운영체제 제목 줄 그대로)
        // 테두리는 네 변 모두 #EBEBEB 1px 한 줄: 윈도우 11 은 운영체제 (위는 내용 맨 위 줄, 나머지는 내용 바로 바깥), 윈도우 10 은 앱 (내용 끝 줄)
        const bool w11 = p.win11;
        auto px = [] (int x, int y) { return juce::String (meon::winframe::devScreenPixels (x, y, 1, 1).substr (0, 6)); };
        const auto top = px (cx, p.client[1]);
        const auto lft = w11 ? px (cl - 1, cy) : px (cl, cy);
        const auto rgt = w11 ? px (cr, cy) : px (cr - 1, cy);
        const auto bot = w11 ? px (cx, cb) : px (cx, cb - 1);
        expect (top == "EBEBEB" && (wl - 2 < ml || lft == "EBEBEB") && (wr + 2 > mr || rgt == "EBEBEB") && (wb + 2 > mb || bot == "EBEBEB"),
                label + TXT (": 네 변 테두리 #EBEBEB (위 ") + top + TXT (" 왼쪽 ") + lft + TXT (" 오른쪽 ") + rgt + TXT (" 아래 ") + bot + ")");
        // 테두리 바깥 한 줄에 진한 선 없음 (흰 바탕 위: 운영체제 테두리가 보이면 #707070·강조 색)
        const int o = w11 ? 1 : 0;
        int worst = 255;
        if (wl - 2 >= ml) worst = juce::jmin (worst, darkest (meon::winframe::devScreenPixels (cl - 1 - o, cy, 1, 1)));
        if (wr + 2 <= mr) worst = juce::jmin (worst, darkest (meon::winframe::devScreenPixels (cr + o, cy, 1, 1)));
        if (wb + 2 <= mb) worst = juce::jmin (worst, darkest (meon::winframe::devScreenPixels (cx, cb + o, 1, 1)));
        expect (worst >= 200, label + TXT (": 테두리 바깥에 진한 선 없음 (가장 어두운 밝기 ") + juce::String (worst) + ")");
    };

    add ([this, sh]
    {
        auto* top = root.getTopLevelComponent();
        sh->backdrop = std::make_unique<Plain> (juce::Colours::white);
        sh->backdrop->setBounds (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->totalArea);
        sh->backdrop->addToDesktop (juce::ComponentPeer::windowIgnoresKeyPresses);
        sh->backdrop->setVisible (true);
        sh->backdrop->toBehind (top);
        top->toFront (true);
    }, 1500);
    add ([sample] { sample (TXT ("보통·활성")); }, 0);
    // 화면이 좁아(러너 1024) 오른쪽 틀이 화면 밖이면 창을 왼쪽으로 옮겨 오른쪽을 본다
    add ([this, handle]
    {
        const auto p = meon::winframe::probe (handle());
        if (p.window[0] + p.window[2] + 2 <= p.monitor[0] + p.monitor[2]) return;
        if (auto* pr = peer())
        {
            const float scale = (float) p.dpi / 96.0f;
            const auto b = pr->getBounds();
            const int targetRight = juce::roundToInt ((float) (p.monitor[0] + p.monitor[2] - 40) / scale);
            pr->setBounds (b.withX (targetRight - b.getWidth()), false);
        }
    }, 900);
    add ([sample] { sample (TXT ("보통·활성 (오른쪽 보기)")); }, 0);
    add ([this]
    {
        if (auto* pr = peer()) pr->setBounds (pr->getBounds().withX (100), false);
    }, 900);
    add ([this, sh]
    {
        // 다른 창을 앞으로 (비활성)
        sh->other = std::make_unique<Plain> (juce::Colours::lightgrey);
        const auto area = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
        sh->other->setBounds (area.getRight() - 40, area.getBottom() - 40, 30, 30);
        sh->other->addToDesktop (juce::ComponentPeer::windowHasDropShadow);
        sh->other->setVisible (true);
        sh->other->toFront (true);
    }, 1000);
    add ([sample] { sample (TXT ("보통·비활성")); }, 0);
    add ([this, sh] { sh->other.reset(); root.getTopLevelComponent()->toFront (true); }, 800);
    add ([handle] { meon::winframe::toggleMaximise (handle()); }, 1200);
    add ([sample] { sample (TXT ("최대화")); }, 0);
    add ([handle] { meon::winframe::toggleMaximise (handle()); }, 1200);
    add ([sample] { sample (TXT ("다시 보통")); }, 0);
    add ([sh] { sh->backdrop.reset(); }, 0);
}

//==============================================================================
// 전체 화면 단축키 (독립 앱): 맥 ⌃⌘F·fn+F·보기 메뉴 (macOS 기본 전체 화면), 윈도우 F11 (창 틀·작업 표시줄까지).
// 방 안에서 채팅 입력 중에 켜고 끈다 (합주 소리·방 상태 그대로). 윈도우는 창 버튼이 숨고 맨 위에 대면 나타난다.
// 끌 때(requestedQuit) 원래 창으로 되돌아오는지. 실제 창이 필요하다 (windows.yml·mac.yml)
void UiSelfTest::addFullscreenTests()
{
    begin (TXT ("전체 화면 단축키"));
    const auto os = juce::SystemStats::getOperatingSystemName();
    const bool win = os.startsWithIgnoreCase ("Windows"), mac = os.startsWithIgnoreCase ("Mac");
    if (! win && ! mac)
    {
        add ([] { std::cerr << "UITEST INFO fullscreen: 맥·윈도우가 아니라 건너뜀" << std::endl; }, 0);
        return;
    }
    struct Shared { juce::Rectangle<int> before; int phase = 0; bool sendMuted = false; juce::String room; };
    auto sh = std::make_shared<Shared>();
    auto handle = [this]() -> void* { auto* p = peer(); return p != nullptr ? p->getNativeHandle() : nullptr; };
    auto isFs = [win, handle] { return win ? meon::winframe::isFullscreen (handle()) : meon::chrome::isFullScreen (handle()); };
    auto shortcut = [win, handle] (int macMods)
    {
        if (win) meon::winframe::devKey (0x7A);   // VK_F11
        else meon::chrome::devPostKey (handle(), "f", macMods);
    };
    auto caption = [this]() -> WinCaptionButtons*
    {
        if (auto* e = root.getParentComponent())
            for (auto* c : e->getChildren())
                if (auto* w = dynamic_cast<WinCaptionButtons*> (c)) return w;
        return nullptr;
    };
    auto sameRoom = [this, sh] (const juce::String& what)
    {
        auto* s = core.jam();
        expect (inRoom() && roomId() == sh->room && s != nullptr && (int) s->getPhase() == sh->phase && s->isSendMuted() == sh->sendMuted,
                what + TXT (": 방·연결·송출 그대로"));
    };
    const int waitMs = mac ? 2200 : 900;   // 맥 전체 화면은 애니메이션

    enterNewRoom();
    closeToasts();
    add ([this, sh, mac]
    {
        if (auto* p = peer()) sh->before = p->getBounds();
        sh->room = roomId();
        sh->phase = (int) core.jam()->getPhase();
        sh->sendMuted = core.jam()->isSendMuted();
        if (mac) meon::chrome::devSetActive (true);   // 창이 앞에 있어야 입력 칸이 포커스를 받는다
    }, 600);
    add ([this, mac]
    {
        jam()->chatInput.focus();   // 채팅 입력 중
        type ("hi");
        expect (jam()->chatInput.getText() == "hi", TXT ("채팅 입력 칸에 글 넣음 (") + jam()->chatInput.getText()
                + TXT (", 포커스 ") + juce::String (jam()->chatInput.editor().hasKeyboardFocus (true) ? 1 : 0) + ")");
        if (mac) expect (meon::chrome::devHasFullScreenMenu(), TXT ("메뉴 막대: 보기 > 전체 화면으로 전환 (⌃⌘F)"));
    }, 300);
    add ([shortcut] { shortcut (1 | 2); }, waitMs);
    add ([this, sh, win, isFs, caption, sameRoom, handle]
    {
        expect (isFs(), win ? TXT ("F11 (채팅 입력 중) → 전체 화면") : TXT ("⌃⌘F (채팅 입력 중) → 전체 화면"));
        sameRoom (TXT ("켤 때"));
        expect (jam()->chatInput.getText() == "hi", TXT ("채팅 입력 중인 글 그대로 (") + jam()->chatInput.getText() + ")");
        expect (root.chromeMode() == Sidebar::Chrome::flat, TXT ("전체 화면 배치 (fullscreen=켜짐)"));
        if (win)
        {
            const auto p = meon::winframe::probe (handle());
            expect (std::equal (p.client, p.client + 4, p.monitor),
                    TXT ("윈도우 전체 화면: 내용 = 모니터 전체 (작업 표시줄까지) ") + juce::String (p.client[2]) + "x" + juce::String (p.client[3]));
            auto* cb = caption();
            expect (cb == nullptr || ! cb->isVisible(), TXT ("전체 화면: 창 버튼 숨김"));
            expect (root.getTitleRightInset() == metric::padX, TXT ("전체 화면: 제목 줄 오른쪽 여백 18 (창 버튼 없음)"));
        }
    }, 0);
    if (win)
    {
        // 마우스를 화면 맨 위에 대면 창 버튼이 나타나고, "이전 크기로" 로 빠져나간다
        add ([handle]
        {
            const auto p = meon::winframe::probe (handle());
            meon::winframe::devMouse (p.monitor[0] + p.monitor[2] / 2, p.monitor[1], false, false);
        }, 500);
        add ([this, caption]
        {
            auto* cb = caption();
            expect (cb != nullptr && cb->isVisible(), TXT ("맨 위에 마우스 → 창 버튼이 보인다"));
        }, 0);
        add ([caption]
        {
            if (auto* cb = caption())
            {
                const auto c = juce::Desktop::getInstance().getDisplays().logicalToPhysical (cb->localPointToGlobal (cb->buttonBounds (WinCaptionButtons::maximise).getCentre()));
                meon::winframe::devMouse (c.x, c.y, false, false);
            }
        }, 300);
        add ([caption]
        {
            if (auto* cb = caption())
            {
                const auto c = juce::Desktop::getInstance().getDisplays().logicalToPhysical (cb->localPointToGlobal (cb->buttonBounds (WinCaptionButtons::maximise).getCentre()));
                meon::winframe::devMouse (c.x, c.y, true, true);
            }
        }, waitMs);
        add ([this, sh, isFs, sameRoom]
        {
            expect (! isFs(), TXT ("창 버튼 '이전 크기로' → 전체 화면 끝"));
            if (auto* p = peer()) expect (p->getBounds() == sh->before, TXT ("원래 창 자리·크기 ") + p->getBounds().toString());
            sameRoom (TXT ("끌 때"));
            jam()->chatInput.focus();
        }, 300);
    }
    else
    {
        add ([shortcut] { shortcut (1 | 2); }, waitMs);
        add ([this, sh, isFs, sameRoom]
        {
            expect (! isFs(), TXT ("⌃⌘F 다시 → 전체 화면 끝"));
            sameRoom (TXT ("끌 때"));
            jam()->chatInput.focus();
        }, 300);
        add ([shortcut] { shortcut (4); }, waitMs);   // fn+F (지구본+F)
        add ([isFs] { if (! isFs()) std::cerr << "UITEST INFO fullscreen: fn+F 로 전체 화면이 되지 않음 (가짜 키 이벤트)" << std::endl; }, 0);
        add ([this, isFs, handle]
        {
            expect (isFs(), TXT ("fn+F → 전체 화면"));
            if (isFs()) meon::chrome::toggleFullScreen (handle());
        }, waitMs);
    }
    // 단축키로 다시 켠 채 앱을 끄면 원래 창으로 (끄는 길: requestedQuit). 맥은 나온 뒤 끄므로 여기서는 윈도우만
    if (win)
    {
        add ([shortcut] { shortcut (0); }, waitMs);
        add ([this, sh, isFs]
        {
            expect (isFs(), TXT ("F11 다시 → 전체 화면"));
            if (auto* hooks = dynamic_cast<meon::ShellHooks*> (root.getParentComponent()))
                expect (hooks->requestedQuit(), TXT ("끄기 요청"));
        }, 600);
        add ([this, sh, isFs]
        {
            expect (! isFs(), TXT ("끌 때: 전체 화면을 풀고 끈다 (다음에 켜면 원래 창)"));
            if (auto* p = peer()) expect (p->getBounds() == sh->before, TXT ("끌 때 저장될 창 크기 = 원래 창 ") + p->getBounds().toString());
        }, 0);
    }
}

//==============================================================================
bool UiSelfTest::checkWindowFrame (juce::Component& editor, const juce::String& label, const juce::String& key, juce::String& detail)
{
    detail.clear();
    auto* peer = editor.getPeer();
    if (peer == nullptr) { detail = TXT ("창 없음"); return false; }
    void* h = peer->getNativeHandle();
    peer->performAnyPendingRepaintsNow();
    const auto p = meon::winframe::probe (h);
    auto rs = [] (const int r[4]) { return juce::String (r[0]) + "," + juce::String (r[1]) + " " + juce::String (r[2]) + "x" + juce::String (r[3]); };
    juce::StringArray fails;
    const juce::String mode = p.fullscreen ? "fullscreen" : p.maximised ? "maximised" : "normal";

    // 1) 상태별 내용 자리
    if (p.fullscreen && ! std::equal (p.client, p.client + 4, p.monitor))
        fails.add (TXT ("내용 ≠ 모니터 (") + rs (p.client) + " / " + rs (p.monitor) + ")");
    if (! p.fullscreen && p.maximised && ! std::equal (p.client, p.client + 4, p.work))
        fails.add (TXT ("내용 ≠ 작업 영역 (") + rs (p.client) + " / " + rs (p.work) + ")");
    if (! p.fullscreen && ! p.maximised)
    {
        const int l = p.client[0] - p.window[0], t = p.client[1] - p.window[1];
        const int r = (p.window[0] + p.window[2]) - (p.client[0] + p.client[2]), b = (p.window[1] + p.window[3]) - (p.client[1] + p.client[3]);
        if (t != 0 || l != p.insets[0] || r != p.insets[2] || b != p.insets[3])
            fails.add (TXT ("틀 두께 ") + juce::String (l) + "/" + juce::String (t) + "/" + juce::String (r) + "/" + juce::String (b));
    }
    // 2) JUCE 가 아는 내용 자리·크기 = 실제 (물리 1px 안)
    const float s = (float) peer->getPlatformScaleFactor();
    const int jw = juce::roundToInt ((float) editor.getWidth() * s), jh = juce::roundToInt ((float) editor.getHeight() * s);
    const auto origin = juce::Desktop::getInstance().getDisplays().logicalToPhysical (editor.getScreenPosition());
    if (std::abs (jw - p.client[2]) > 1 || std::abs (jh - p.client[3]) > 1 || std::abs (origin.x - p.client[0]) > 1 || std::abs (origin.y - p.client[1]) > 1)
        fails.add (TXT ("JUCE 내용 ") + origin.toString() + " " + juce::String (jw) + "x" + juce::String (jh) + TXT (" / 창 내용 ") + rs (p.client));

    // 3) 화면 vs 앱 그림 (네 가장자리 줄 · 창 버튼 자리)
    const int cw = p.client[2], ch = p.client[3];
    const auto screen = meon::winframe::devCaptureScreen (p.client[0], p.client[1], cw, ch);
    int edgeTop = 0, edgeBottom = 0, edgeLeft = 0, edgeRight = 0, capDiff = 0, capArea = 0;
    if (cw <= 0 || ch <= 0 || screen.size() != (size_t) cw * (size_t) ch)
    {
        fails.add (TXT ("화면 캡처 실패"));
    }
    else
    {
        const auto snap = editor.createComponentSnapshot (editor.getLocalBounds(), true, s);
        juce::Image::BitmapData sd (snap, juce::Image::BitmapData::readOnly);
        auto differs = [&] (int x, int y)
        {
            if (x >= snap.getWidth() || y >= snap.getHeight()) return true;   // 앱 그림이 닿지 않는 자리 = 여백
            const juce::Colour a (screen[(size_t) y * (size_t) cw + (size_t) x]), b = sd.getPixelColour (x, y);
            return std::abs ((int) a.getRed() - (int) b.getRed()) > 16 || std::abs ((int) a.getGreen() - (int) b.getGreen()) > 16
                || std::abs ((int) a.getBlue() - (int) b.getBlue()) > 16;
        };
        // 윈도우 11 보통 창: 맨 위 줄은 운영체제 테두리(#EBEBEB)가 덮고, 네 모서리는 둥글다 (모서리 근처는 세지 않는다)
        const bool w11Normal = p.win11 && ! p.fullscreen && ! p.maximised;
        const int corner = w11Normal ? juce::roundToInt (10.0f * s) : 0;
        // 화면에 보이는 자리만 (전체 화면 = 모니터, 그 밖 = 작업 영역: 작업 표시줄이 위에 있다). 큰 배율에서 보통 창이 화면보다 크면 아래가 잘린다
        const auto* area = p.fullscreen ? p.monitor : p.work;
        const int vx0 = juce::jmax (0, area[0] - p.client[0]), vy0 = juce::jmax (0, area[1] - p.client[1]);
        const int vx1 = juce::jmin (cw, area[0] + area[2] - p.client[0]), vy1 = juce::jmin (ch, area[1] + area[3] - p.client[1]);
        const bool showTop = vy0 == 0, showBottom = vy1 == ch, showLeft = vx0 == 0, showRight = vx1 == cw;
        for (int x = juce::jmax (corner, vx0); x < juce::jmin (cw - corner, vx1); ++x)
        {
            if (showTop && (w11Normal ? (screen[(size_t) x] & 0xFFFFFFu) != 0xEBEBEBu : differs (x, 0))) ++edgeTop;
            if (showBottom && differs (x, ch - 1)) ++edgeBottom;
        }
        for (int y = juce::jmax (corner, vy0); y < juce::jmin (ch - corner, vy1); ++y)
        {
            if (showLeft && differs (0, y)) ++edgeLeft;
            if (showRight && differs (cw - 1, y)) ++edgeRight;
        }
        if (! (showTop && showBottom && showLeft && showRight))
            std::cerr << "UITEST INFO winstates " << label << ": 화면 밖 가장자리는 세지 않음 (위 " << (showTop ? 1 : 0) << " 오른쪽 " << (showRight ? 1 : 0)
                      << " 아래 " << (showBottom ? 1 : 0) << " 왼쪽 " << (showLeft ? 1 : 0) << ")" << std::endl;
        // 창 버튼 자리 (보이면): 운영체제 창 버튼이 그려지면 다르다
        for (auto* c : editor.getChildren())
            if (auto* cb = dynamic_cast<WinCaptionButtons*> (c); cb != nullptr && cb->isVisible())
            {
                const auto r = (cb->getBounds().toFloat() * s).getSmallestIntegerContainer().getIntersection ({ 0, 1, cw, ch - 1 });
                for (int y = r.getY(); y < r.getBottom(); ++y)
                    for (int x = r.getX(); x < r.getRight(); ++x) { ++capArea; if (differs (x, y)) ++capDiff; }
            }
        const auto pct = [] (int n, int len) { return len > 0 ? (double) n * 100.0 / (double) len : 0.0; };
        const int lenX = juce::jmax (1, cw - 2 * corner), lenY = juce::jmax (1, ch - 2 * corner);
        if (pct (edgeTop, lenX) > 2.0 || pct (edgeBottom, lenX) > 2.0 || pct (edgeLeft, lenY) > 2.0 || pct (edgeRight, lenY) > 2.0)
            fails.add (TXT ("가장자리 줄이 앱 그림과 다름 (위 ") + juce::String (edgeTop) + TXT (" 오른쪽 ") + juce::String (edgeRight)
                       + TXT (" 아래 ") + juce::String (edgeBottom) + TXT (" 왼쪽 ") + juce::String (edgeLeft) + "px)");
        if (capArea > 0 && pct (capDiff, capArea) > 2.0)
            fails.add (TXT ("창 버튼 자리가 앱 그림과 다름 (") + juce::String (capDiff) + "/" + juce::String (capArea) + "px)");
    }
    // 4) 캡처 (MEON_WINCAPS 폴더): 창 + 둘레 12px
    const auto dir = juce::SystemStats::getEnvironmentVariable ("MEON_WINCAPS", {});
    if (dir.isNotEmpty())
    {
        const int m = 12;
        const int x0 = juce::jmax (p.monitor[0], p.window[0] - m), y0 = juce::jmax (p.monitor[1], p.window[1] - m);
        const int x1 = juce::jmin (p.monitor[0] + p.monitor[2], p.window[0] + p.window[2] + m);
        const int y1 = juce::jmin (p.monitor[1] + p.monitor[3], p.window[1] + p.window[3] + m);
        const auto px = meon::winframe::devCaptureScreen (x0, y0, x1 - x0, y1 - y0);
        if (! px.empty())
        {
            juce::Image img (juce::Image::RGB, x1 - x0, y1 - y0, false);
            {
                juce::Image::BitmapData out (img, juce::Image::BitmapData::writeOnly);
                for (int y = 0; y < y1 - y0; ++y)
                    for (int x = 0; x < x1 - x0; ++x)
                        out.setPixelColour (x, y, juce::Colour (px[(size_t) y * (size_t) (x1 - x0) + (size_t) x]));
            }
            juce::File folder (dir);
            folder.createDirectory();
            auto f = folder.getChildFile ("dpi" + juce::String (p.dpi) + "_" + key + ".png");
            f.deleteFile();
            juce::FileOutputStream os (f);
            if (os.openedOk()) juce::PNGImageFormat().writeImageToStream (img, os);
        }
    }
    detail = fails.joinIntoString ("; ");
    std::cerr << "UITEST INFO winstates " << label << " [" << mode << " dpi " << p.dpi << (p.active ? " active" : " inactive")
              << (p.appBorder ? " appBorder" : "") << "] window " << rs (p.window) << " client " << rs (p.client)
              << " work " << rs (p.work) << " monitor " << rs (p.monitor) << " insets " << p.insets[0] << "/" << p.insets[1] << "/" << p.insets[2] << "/" << p.insets[3]
              << " edgeDiff T" << edgeTop << " R" << edgeRight << " B" << edgeBottom << " L" << edgeLeft << " caption " << capDiff << "/" << capArea
              << " blocked " << p.captionPaintsBlocked << (fails.isEmpty() ? "" : " FAIL: ") << detail << std::endl;
    return fails.isEmpty();
}

void UiSelfTest::addWinStatesTests()
{
    begin (TXT ("윈도우 창 상태별"));
    if (! juce::SystemStats::getOperatingSystemName().startsWithIgnoreCase ("Windows"))
    {
        add ([] { std::cerr << "UITEST INFO winstates: 윈도우가 아니라 건너뜀" << std::endl; }, 0);
        return;
    }
    struct Plain : juce::Component
    {
        void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::white); }
    };
    struct Shared
    {
        std::unique_ptr<Plain> backdrop;
        std::unique_ptr<juce::DocumentWindow> other;
        int n = 0, startSteps = INT_MIN;
        juce::Rectangle<int> normalClient;
    };
    auto sh = std::make_shared<Shared>();
    auto handle = [this]() -> void* { auto* p = peer(); return p != nullptr ? p->getNativeHandle() : nullptr; };
    auto caption = [this]() -> WinCaptionButtons*
    {
        if (auto* e = root.getParentComponent())
            for (auto* c : e->getChildren())
                if (auto* w = dynamic_cast<WinCaptionButtons*> (c)) return w;
        return nullptr;
    };
    auto phys = [] (juce::Component& c, juce::Point<int> local)
    {
        return juce::Desktop::getInstance().getDisplays().logicalToPhysical (c.localPointToGlobal (local));
    };
    // 상태 하나 확인 (화면이 다 그려진 뒤)
    auto check = [this, sh] (const juce::String& label, const juce::String& key)
    {
        return std::function<void()> ([this, sh, label, key]
        {
            juce::String detail;
            auto* editor = root.getParentComponent();
            const bool ok = editor != nullptr && checkWindowFrame (*editor, label, juce::String (++sh->n).paddedLeft ('0', 2) + "_" + key, detail);
            expect (ok, label + (ok ? juce::String() : ": " + detail));
        });
    };
    auto sysCmd = [handle] (int which)   // 0 최소화, 1 최대화·이전 크기로, 2 복원
    {
        if (which == 0) meon::winframe::minimise (handle());
        else if (which == 1) meon::winframe::toggleMaximise (handle());
        else meon::winframe::restore (handle());
    };
    auto isMax = [handle] { return meon::winframe::isMaximised (handle()); };
    auto isFs = [handle] { return meon::winframe::isFullscreen (handle()); };
    auto escape = [this] { if (auto* e = root.getParentComponent()) e->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)); };
    const int settle = 900;

    // 켠 직후 (온보딩): 에디터가 창이 뜰 때 확인해 둔 결과
    add ([this]
    {
        expect (launchFrameChecks > 0, TXT ("켠 직후(온보딩) 창 확인을 했다"));
        expect (launchFrameFailures == 0, TXT ("켠 직후(온보딩) 가장자리·창 버튼") + (launchFrameDetail.isEmpty() ? juce::String() : ": " + launchFrameDetail));
    }, 0);
    // 흰 바탕 창을 뒤에 (캡처에서 창 둘레가 보이게)
    add ([this, sh]
    {
        auto* top = root.getTopLevelComponent();
        sh->backdrop = std::make_unique<Plain>();
        sh->backdrop->setBounds (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->totalArea);
        sh->backdrop->addToDesktop (juce::ComponentPeer::windowIgnoresKeyPresses);
        sh->backdrop->setVisible (true);
        sh->backdrop->toBehind (top);
        top->toFront (true);
        closeToasts();
    }, 1500);
    add (check (TXT ("로그인 직후 (공개 방, 보통 창)"), "login"), 0);
    add ([this] { root.getSidebar()->setCollapsed (true); }, settle);
    add (check (TXT ("사이드바 접힘"), "sidebar_collapsed"), 0);
    add ([this] { root.getSidebar()->setCollapsed (false); }, settle);
    add (check (TXT ("사이드바 펼침"), "sidebar_open"), 0);
    add ([this] { home().openAddFriend(); }, settle);
    add (check (TXT ("모달 열림 (친구 추가)"), "modal"), 0);
    add (escape, settle);
    // 창 버튼에 마우스: 운영체제 창 버튼이 그려지면 화면이 앱 그림과 다르다
    for (int b : { 0, 1, 2 })
    {
        add ([caption, phys, b]
        {
            if (auto* cb = caption())
            {
                const auto c = phys (*cb, cb->buttonBounds ((WinCaptionButtons::Which) b).getCentre());
                meon::winframe::devMouse (c.x, c.y, false, false);
            }
        }, 350);
        add (check (TXT ("창 버튼에 마우스 (") + (b == 0 ? TXT ("최소화") : b == 1 ? TXT ("최대화") : TXT ("닫기")) + ")", "hover" + juce::String (b)), 0);
    }
    add ([this, phys] { const auto c = phys (root, { root.getWidth() / 2, 300 }); meon::winframe::devMouse (c.x, c.y, false, false); }, 300);
    // 비활성
    add ([sh]
    {
        sh->other = std::make_unique<juce::DocumentWindow> ("other", juce::Colours::grey, juce::DocumentWindow::closeButton);
        sh->other->setUsingNativeTitleBar (true);
        sh->other->setBounds (40, 40, 200, 120);
        sh->other->setVisible (true);
        sh->other->toFront (true);
    }, settle);
    add (check (TXT ("비활성 (다른 창이 앞)"), "inactive"), 0);
    add ([this, sh] { sh->other.reset(); root.getTopLevelComponent()->toFront (true); }, settle);
    add (check (TXT ("다시 활성"), "active"), 0);

    // 크기 조절: 오른쪽·아래·왼쪽 끝을 잡고 끈다 (윈도우 10 은 내용 끝 1px 도 크기 조절 자리)
    struct Drag { const char* name; int edge; int dx, dy; };
    for (auto d : { Drag { "오른쪽", 0, 40, 0 }, Drag { "아래", 1, 0, 30 }, Drag { "왼쪽", 2, -30, 0 } })
    {
        auto start = std::make_shared<juce::Point<int>>();
        auto before = std::make_shared<meon::winframe::Probe>();
        auto skip = std::make_shared<bool> (false);
        add ([handle, d, start, before, skip]
        {
            const auto p = meon::winframe::probe (handle());
            *before = p;
            const int b = p.resizeBorder;
            if (d.edge == 0) *start = { p.window[0] + p.window[2] - b / 2, p.window[1] + p.window[3] / 2 };
            else if (d.edge == 1) *start = { p.window[0] + p.window[2] / 2, p.window[1] + p.window[3] - b / 2 };
            else *start = { p.window[0] + b / 2, p.window[1] + p.window[3] / 2 };
            // 끌 자리와 끈 뒤 자리가 작업 영역 안이어야 한다 (큰 배율에서 창이 화면보다 크면 아래 끝이 화면 밖)
            const juce::Rectangle<int> work (p.work[0], p.work[1], p.work[2], p.work[3]);
            *skip = ! work.contains (*start) || ! work.contains (*start + juce::Point<int> (d.dx, d.dy));
            if (*skip) { std::cerr << "UITEST INFO winstates resize " << d.edge << " skipped (edge outside work area)" << std::endl; return; }
            meon::winframe::devMouse (start->x, start->y, true, false);
        }, 200);
        add ([d, start, skip] { if (! *skip) meon::winframe::devMouse (start->x + d.dx / 2, start->y + d.dy / 2, false, false); }, 150);
        add ([d, start, skip] { if (! *skip) meon::winframe::devMouse (start->x + d.dx, start->y + d.dy, false, false); }, 250);
        add ([d, start, skip] { if (! *skip) meon::winframe::devMouse (start->x + d.dx, start->y + d.dy, false, true); }, settle);
        add ([this, handle, d, before, skip]
        {
            if (*skip) return;
            const auto p = meon::winframe::probe (handle());
            const int dw = p.client[2] - before->client[2], dh = p.client[3] - before->client[3];
            const int wantW = d.edge == 2 ? -d.dx : d.dx, wantH = d.dy;
            expect (std::abs (dw - wantW) <= 2 && std::abs (dh - wantH) <= 2,
                    juce::String (juce::CharPointer_UTF8 (d.name)) + TXT (" 끝을 끌어 크기 조절 (") + juce::String (dw) + "x" + juce::String (dh) + ")");
        }, 0);
        add (check (juce::String (juce::CharPointer_UTF8 (d.name)) + TXT (" 크기 조절 뒤"), juce::String ("resize") + juce::String (d.edge)), 0);
    }
    add ([this, sh] { if (auto* p = peer()) sh->normalClient = p->getBounds(); }, 0);

    // 최대화 · 최소화 후 복원 · 이전 크기로
    add ([sysCmd] { sysCmd (1); }, 0);
    waitUntilOr (TXT ("최대화"), isMax, 4000, [this] { expect (false, TXT ("최대화되지 않음")); });
    add ([] {}, settle);
    add (check (TXT ("최대화"), "maximised"), 0);
    add ([this] { root.getSidebar()->setCollapsed (true); }, settle);
    add (check (TXT ("최대화 + 사이드바 접힘"), "max_sidebar_collapsed"), 0);
    add ([this] { root.getSidebar()->setCollapsed (false); home().openAddFriend(); }, settle);
    add (check (TXT ("최대화 + 모달"), "max_modal"), 0);
    add (escape, 400);
    add ([caption, phys]
    {
        if (auto* cb = caption())
        {
            const auto c = phys (*cb, cb->buttonBounds (WinCaptionButtons::maximise).getCentre());
            meon::winframe::devMouse (c.x, c.y, false, false);
        }
    }, 350);
    add (check (TXT ("최대화: '이전 크기로' 버튼에 마우스"), "max_hover"), 0);
    add ([this, phys] { const auto c = phys (root, { root.getWidth() / 2, 300 }); meon::winframe::devMouse (c.x, c.y, false, false); }, 200);
    add ([sysCmd] { sysCmd (0); }, 1200);
    add ([sysCmd] { sysCmd (2); }, 0);
    waitUntilOr (TXT ("최소화 뒤 복원 (최대화로)"), isMax, 4000, [this] { expect (false, TXT ("최소화 뒤 복원이 최대화로 돌아오지 않음")); });
    add ([] {}, settle);
    add (check (TXT ("최소화 후 복원 (최대화)"), "max_restored"), 0);
    add ([sysCmd] { sysCmd (1); }, 0);
    waitUntilOr (TXT ("이전 크기로"), [isMax] { return ! isMax(); }, 4000, [this] { expect (false, TXT ("이전 크기로 돌아오지 않음")); });
    add ([] {}, settle);
    add ([this, sh] { if (auto* p = peer()) expect (p->getBounds() == sh->normalClient, TXT ("이전 크기로: 최대화 전 자리·크기 ") + p->getBounds().toString()); }, 0);
    add (check (TXT ("이전 크기로 (보통)"), "normal_again"), 0);
    add ([sysCmd] { sysCmd (0); }, 1200);
    add ([sysCmd] { sysCmd (2); }, settle);
    add (check (TXT ("최소화 후 복원 (보통)"), "normal_restored"), 0);

    // 전체 화면 (F11): 내용 = 모니터, 창 버튼은 맨 위에 마우스를 댔을 때만 (앱이 그린 것)
    add ([] { meon::winframe::devKey (0x7A); }, 0);
    waitUntilOr (TXT ("전체 화면"), isFs, 4000, [this] { expect (false, TXT ("F11 로 전체 화면이 되지 않음")); });
    add ([] {}, settle);
    add (check (TXT ("전체 화면"), "fullscreen"), 0);
    add ([handle] { const auto p = meon::winframe::probe (handle()); meon::winframe::devMouse (p.monitor[0] + p.monitor[2] / 2, p.monitor[1], false, false); }, 400);
    add ([this, caption] { auto* cb = caption(); expect (cb != nullptr && cb->isVisible(), TXT ("전체 화면: 맨 위에 마우스 → 창 버튼 (앱이 그린 것)")); }, 0);
    add ([caption, phys]
    {
        if (auto* cb = caption())
        {
            const auto c = phys (*cb, cb->buttonBounds (WinCaptionButtons::maximise).getCentre());
            meon::winframe::devMouse (c.x, c.y, false, false);
        }
    }, 400);
    add ([this, caption] { auto* cb = caption(); expect (cb != nullptr && cb->isVisible(), TXT ("전체 화면: 창 버튼 위로 옮겨도 보인다")); }, 0);
    add (check (TXT ("전체 화면 + 창 버튼 보임"), "fullscreen_caption"), 0);
    add ([handle] { const auto p = meon::winframe::probe (handle()); meon::winframe::devMouse (p.monitor[0] + p.monitor[2] / 2, p.monitor[1] + p.monitor[3] / 2, false, false); }, 500);
    add ([this, caption] { auto* cb = caption(); expect (cb == nullptr || ! cb->isVisible(), TXT ("전체 화면: 아래로 내려가면 창 버튼 숨김")); }, 0);
    add ([this] { home().openAddFriend(); }, settle);
    add (check (TXT ("전체 화면 + 모달"), "fullscreen_modal"), 0);
    add (escape, 400);
    add ([] { meon::winframe::devKey (0x7A); }, 0);
    waitUntilOr (TXT ("전체 화면 끝"), [isFs] { return ! isFs(); }, 4000, [this] { expect (false, TXT ("F11 로 전체 화면이 끝나지 않음")); });
    add ([] {}, settle);
    add ([this, sh] { if (auto* p = peer()) expect (p->getBounds() == sh->normalClient, TXT ("전체 화면 끝: 원래 자리·크기 ") + p->getBounds().toString()); }, 0);
    add (check (TXT ("전체 화면 끝 (보통)"), "fullscreen_off"), 0);

    // 최대화에서 전체 화면 → 끄면 최대화 → 이전 크기로 = 처음 보통 자리 (JUCE 가 따로 기억한 자리로 가지 않는다)
    add ([sysCmd] { sysCmd (1); }, 0);
    waitUntilOr (TXT ("최대화 (전체 화면 전)"), isMax, 4000, [this] { expect (false, TXT ("최대화되지 않음")); });
    add ([] { meon::winframe::devKey (0x7A); }, settle);
    waitUntilOr (TXT ("최대화 → 전체 화면"), isFs, 4000, [this] { expect (false, TXT ("최대화에서 전체 화면이 되지 않음")); });
    add ([] {}, settle);
    add (check (TXT ("최대화 → 전체 화면"), "max_fullscreen"), 0);
    add ([] { meon::winframe::devKey (0x7A); }, 0);
    waitUntilOr (TXT ("전체 화면 → 최대화"), [isFs, isMax] { return ! isFs() && isMax(); }, 4000, [this] { expect (false, TXT ("전체 화면을 끄면 최대화로 돌아와야 함")); });
    add ([] {}, settle);
    add (check (TXT ("전체 화면 → 최대화로 돌아옴"), "fullscreen_to_max"), 0);
    add ([sysCmd] { sysCmd (1); }, 0);
    waitUntilOr (TXT ("이전 크기로 (전체 화면 뒤)"), [isMax] { return ! isMax(); }, 4000, [this] { expect (false, TXT ("이전 크기로 돌아오지 않음")); });
    add ([] {}, settle);
    add ([this, sh] { if (auto* p = peer()) expect (p->getBounds() == sh->normalClient, TXT ("전체 화면을 거친 뒤 이전 크기로 = 처음 보통 자리 ") + p->getBounds().toString()); }, 0);
    add (check (TXT ("전체 화면을 거친 뒤 보통"), "after_fs_normal"), 0);

    // 여러 번 오가기
    for (int round = 1; round <= 3; ++round)
    {
        const auto r = juce::String (round);
        add ([sysCmd] { sysCmd (1); }, 0);
        waitUntilOr (TXT ("오가기 최대화 ") + r, isMax, 4000, [this] { expect (false, TXT ("오가기: 최대화되지 않음")); });
        add ([] { meon::winframe::devKey (0x7A); }, settle);
        waitUntilOr (TXT ("오가기 전체 화면 ") + r, isFs, 4000, [this] { expect (false, TXT ("오가기: 전체 화면이 되지 않음")); });
        add ([] {}, 500);
        add (check (TXT ("오가기 ") + r + TXT (": 전체 화면"), "cycle" + r + "_fs"), 0);
        add ([] { meon::winframe::devKey (0x7A); }, 0);
        waitUntilOr (TXT ("오가기 최대화로 ") + r, [isFs, isMax] { return ! isFs() && isMax(); }, 4000, [this] { expect (false, TXT ("오가기: 최대화로 돌아오지 않음")); });
        add ([] {}, 500);
        add (check (TXT ("오가기 ") + r + TXT (": 최대화"), "cycle" + r + "_max"), 0);
        add ([sysCmd] { sysCmd (1); }, 0);
        waitUntilOr (TXT ("오가기 보통 ") + r, [isMax] { return ! isMax(); }, 4000, [this] { expect (false, TXT ("오가기: 보통으로 돌아오지 않음")); });
        add ([] {}, 500);
        add (check (TXT ("오가기 ") + r + TXT (": 보통"), "cycle" + r + "_normal"), 0);
        add ([] { meon::winframe::devKey (0x7A); }, 500);
        waitUntilOr (TXT ("오가기 보통→전체 화면 ") + r, isFs, 4000, [this] { expect (false, TXT ("오가기: 보통에서 전체 화면이 되지 않음")); });
        add ([] { meon::winframe::devKey (0x7A); }, 500);
        waitUntilOr (TXT ("오가기 전체 화면→보통 ") + r, [isFs, isMax] { return ! isFs() && ! isMax(); }, 4000,
                     [this] { expect (false, TXT ("오가기: 전체 화면에서 보통으로 돌아오지 않음")); });
        add ([] {}, 500);
        add ([this, sh, r] { if (auto* p = peer()) expect (p->getBounds() == sh->normalClient, TXT ("오가기 ") + r + TXT (": 처음 보통 자리 ") + p->getBounds().toString()); }, 0);
    }
    add (check (TXT ("오가기 뒤 보통"), "cycles_done"), 0);

    // 배율 바꾸기 (다른 배율 모니터로 옮긴 것과 같은 길: WM_DPICHANGED). 운영체제가 허락하지 않으면 건너뜀
    for (int steps : { 1, 2 })
    {
        auto changed = std::make_shared<bool> (false);
        add ([sh, steps, handle]
        {
            if (sh->startSteps == INT_MIN) sh->startSteps = meon::winframe::devScaleSteps (0, true);
            if (sh->startSteps != 0) { std::cerr << "UITEST INFO winstates in-run scale change skipped (started at steps " << sh->startSteps << ")" << std::endl; return; }
            const int before = meon::winframe::probe (handle()).dpi;
            const int now = meon::winframe::devScaleSteps (steps, false);
            std::cerr << "UITEST INFO winstates scale steps " << steps << " -> " << now << " (start " << sh->startSteps << ", dpi before " << before << ")" << std::endl;
        }, 2500);
        add ([sh, handle, steps, changed]
        {
            if (sh->startSteps != 0) return;
            const int dpi = meon::winframe::probe (handle()).dpi;
            *changed = dpi != 96;
            std::cerr << "UITEST INFO winstates dpi after scale steps " << steps << ": " << dpi
                      << (*changed ? "" : " (scale did not change: CI display does not allow it)") << std::endl;
        }, 0);
        const auto tag = "scale" + juce::String (steps);
        const auto name = TXT ("배율 바꾼 뒤 (") + juce::String (steps) + TXT ("단계) ");
        add ([check, changed, name, tag] { if (*changed) check (name + TXT ("보통"), tag + "_normal")(); }, 0);
        add ([sysCmd, changed] { if (*changed) sysCmd (1); }, settle);
        add ([check, changed, name, tag] { if (*changed) check (name + TXT ("최대화"), tag + "_max")(); }, 0);
        add ([sysCmd, changed] { if (*changed) sysCmd (1); }, settle);
        add ([changed] { if (*changed) meon::winframe::devKey (0x7A); }, settle);
        add ([check, changed, name, tag] { if (*changed) check (name + TXT ("전체 화면"), tag + "_fs")(); }, 0);
        add ([changed] { if (*changed) meon::winframe::devKey (0x7A); }, settle);
        add ([check, changed, name, tag] { if (*changed) check (name + TXT ("다시 보통"), tag + "_normal2")(); }, 0);
    }
    add ([sh] { if (sh->startSteps == 0) meon::winframe::devScaleSteps (0, false); }, 2500);
    add (check (TXT ("배율 되돌린 뒤"), "scale_back"), 0);

    add ([this]
    {
        auto* ed = dynamic_cast<meon::MeonEditor*> (root.getParentComponent());
        const int fixes = ed != nullptr ? ed->devJuceFrameFixes() : -1;
        std::cerr << "UITEST INFO winstates juce frame fixes " << fixes << std::endl;
        expect (fixes == 0, TXT ("JUCE 창 틀 두께가 한 번도 어긋나지 않음 (맞춤 ") + juce::String (fixes) + TXT ("번)"));
    }, 0);
    add ([sh] { sh->backdrop.reset(); }, 0);
}

} // namespace meon::ui
