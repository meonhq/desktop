#include "UiSelfTest.h"
#include "../AppRoot.h"
#include "../screens/HomeScreen.h"
#include "../screens/JamScreen.h"
#include "../../core/api/fake/FakeMeonServer.h"
#include "../../core/model/RoomFilter.h"
#include "../../core/model/PanelWidth.h"
#include "../../MeonSettings.h"
#include "../../core/util/ChatInput.h"

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
            expect (false, TXT ("시간 초과: ") + s.label);
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

void UiSelfTest::minimize()
{
    // 홈·사이드바 디자인 v2: 방 화면 윗줄의 로비 보기 버튼은 없다. 사이드바 "홈" 을 누르면 방은 그대로 두고 홈으로 (최소화).
    // 창이 좁아 방 화면에서 사이드바가 숨으면(플러그인 900) 예전 윗줄의 로비 보기
    add ([this]
    {
        if (auto* j = jam(); j != nullptr && j->lobby.isVisible()) { click (j->lobby); return; }
        const auto r = root.sidebar->devRow (TXT ("홈"));
        expect (root.sidebar->isVisible() && ! r.isEmpty(), TXT ("사이드바 \"홈\""));
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
        expect (j->leave.getButtonText() == TXT ("한 번 더 누르면 나가요"), TXT ("확인 문구"));
        expect (j->tip.isVisible() && j->tip.getTarget() == &j->leave, TXT ("방장: 확인 상태에서 툴팁"));
        if (j->tip.isVisible())
            expect (j->tip.getY() == j->leave.getBottom() + 6 && j->tip.getRight() == j->leave.getRight(), TXT ("툴팁: 버튼 아래 6, 오른쪽 끝 맞춤"));
        click (j->membersCard, { 40, 120 });   // 바깥
    });
    add ([this]
    {
        auto* j = jam();
        expect (! j->leave.isArmed() && inRoom(), TXT ("바깥 클릭: 원래대로"));
        expect (! j->tip.isVisible(), TXT ("바깥 클릭: 툴팁 사라짐"));
        click (j->leave);
    }, 3300);
    add ([this] { expect (! jam()->leave.isArmed() && inRoom(), TXT ("3초가 지나면 원래대로")); });
    add ([this] { click (jam()->leave); });
    add ([this] { click (jam()->leave); }, 0);
    waitUntil (TXT ("두 번째 클릭으로 나감"), [this] { return ! inRoom() && jam() == nullptr && home().isShowing(); });
    add ([this] { expect (! inRoom() && ! home().hasRoomBar(), TXT ("두 번째 클릭: 방에서 나가 홈")); });
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
        if (auto* b = firstButton (jam()->friendsCard)) click (*b);   // 친구 패널 접기
    }, 700);
    add ([this] { jam()->chatViewport.setViewPosition (0, 120); });   // 채팅을 위로 올려 둔다 (맨 아래가 아니면 새 메시지를 따라가지 않는다)
    add ([this, state]
    {
        auto* j = jam();
        auto* session = core.jam();
        (*state)["room"] = roomId();
        (*state)["text"] = j->chatInput.getText();
        (*state)["scroll"] = j->chatViewport.getViewPositionY();
        (*state)["friendsOpen"] = j->friendsOpen;
        (*state)["phase"] = (int) session->getPhase();
        (*state)["msgs"] = (int) session->chat().getMessages().size();
        expect ((*state)["text"].toString() == TXT ("draft 쓰는 중"), TXT ("채팅 입력에 글을 써 둠"));
        expect (! j->friendsOpen, TXT ("친구 패널을 접어 둠"));
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
                expect (t->getBottom() == root.getHeight() - 12, TXT ("토스트 아래 여백 12 (막대 없음)"));
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
        expect (j->friendsOpen == (bool) (*state)["friendsOpen"], TXT ("접기 상태 그대로"));
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
        expect (findButton (home(), TXT ("나가고 들어가기")) != nullptr && findButton (home(), TXT ("취소")) != nullptr, TXT ("[취소] [나가고 들어가기]"));
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
    add ([this] { if (auto* ok = findButton (home(), TXT ("나가고 들어가기"))) click (*ok); }, 0);
    waitUntil (TXT ("나간 뒤 다른 방으로"), [this, target] { return inRoom() && roomId() == *target && jam() != nullptr && jam()->isShowing(); }, 6000);
    add ([this] { expect (! root.isMinimized() && ! home().hasRoomBar(), TXT ("새 방은 합주 화면으로 (최소화 아님)")); });

    begin (TXT ("5 최소화: 친구 참여·코드로 입장"));
    minimize();
    clickSidebar (TXT ("홈"));
    add ([this]
    {
        // 홈 "합주 중인 친구" 카드의 참여·입장 요청 (승인 방에 있는 친구는 "입장 요청")
        auto* b = findButton (home(), TXT ("입장 요청"));
        if (b == nullptr) b = findButton (home(), TXT ("참여"));
        expect (b != nullptr, TXT ("합주 중인 친구에 참여·입장 요청 버튼"));
        if (b != nullptr) click (*b);
    }, 300);
    add ([this]
    {
        expect (home().confirm != nullptr && findButton (home(), TXT ("나가고 들어가기")) != nullptr, TXT ("친구 참여 → 확인 모달"));
        if (auto* c = findButton (home(), TXT ("취소"))) click (*c);
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

    begin (TXT ("5 최소화: 새 방 확인 모달"));
    minimize();
    clickSidebar (TXT ("공개 방"));   // 방 만들기는 공개 방 쪽 머리말에
    add ([this] { click (home().createRoom); }, 300);
    add ([this]
    {
        expect (home().confirm != nullptr && findButton (home(), TXT ("나가고 만들기")) != nullptr, TXT ("'방 만들기' → [취소] [나가고 만들기]"));
        if (auto* ok = findButton (home(), TXT ("나가고 만들기"))) click (*ok);
    }, 0);
    waitUntil (TXT ("나간 뒤 방 만들기 모달"), [this] { return ! inRoom() && dynamic_cast<CreateRoomModal*> (home().modal.get()) != nullptr; }, 6000);
    add ([this]
    {
        expect (! home().hasRoomBar() && home().confirm == nullptr, TXT ("막대·확인 모달 닫힘, 방 만들기 모달 열림"));
        home().closeOverlays();
    });

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
    add ([this] { if (auto* f = core.fakeServer()) f->devKickMe(); }, 0);
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
    auto tooSmall = [] (JamScreen& j)
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
        check (j.sendCard, TXT ("내 오디오"));
        check (j.monCard, TXT ("모니터"));
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
        bad = overlaps (j->sendCard) + overlaps (j->monCard);
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
        if (auto* j = jam(); j != nullptr && j->isShowing()) return j->notifications.get();
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
    waitUntil (TXT ("알림 목록 열림 (방은 그대로)"), [this, list] { return list() != nullptr && list()->isShowing() && inRoom(); });
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
// AOO 서버에 닿지 못하는 곳에서 방을 빠르게 나갔다 들어가기 (E8·E9). 이름을 찾을 수 없는 AOO 서버로 보낸다.
// E9 전에는 AOO 가 이름 조회 실패를 연결된 것으로 보고 연결되지 않은 소켓에 썼다 (E8 없이는 SIGPIPE 로 앱이 끝났다).
// 이제는 붙기 실패로 오고, 앱은 끊김("연결이 끊겼어요")을 보이고 3초마다 다시 시도한다
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

} // namespace meon::ui
