#include "AppRoot.h"
#include "Messages.h"
#include "../MeonSettings.h"
#include "screens/JamScreen.h"
#include "overlays/JamOverlays.h"
#include "../core/model/PanelWidth.h"
#include "../MeonWindowChrome.h"

namespace meon::ui
{

AppRoot::AppRoot (core::MeonCore& c, HomeScreen::UpdateHooks u) : core (c), update (std::move (u))
{
    setOpaque (true);
    // 사이드바를 먼저 넣는다 (홈 화면 아래). 홈 화면은 사이드바 자리를 그리지도 받지도 않고, 모달만 창 전체를 덮는다
    sidebar = std::make_unique<Sidebar> (core);
    addAndMakeVisible (*sidebar);
    sidebar->onLayoutChanged = [this] { resized(); };
    sidebar->onTitleBarMouseDown = [this] { titleBarMouseDown(); };
    sidebar->onHome = [this] { showPage (Sidebar::homeMenu ? Sidebar::Page::home : Sidebar::Page::rooms); };
    sidebar->onRooms = [this] { showPage (Sidebar::Page::rooms); };
    sidebar->onShowRoom = [this] { setMinimized (false); };
    sidebar->onJoinCode = [this]
    {
        // 코드로 입장은 홈의 모달 (기존 화면). 방 안이면 방은 그대로 두고 홈 위에 띄운다 (들어가기 전에 나가기 확인)
        if (core.store().state().currentRoom && ! minimized) setMinimized (true);
        home->openJoinCode();
    };
    sidebar->onCreateRoom = [this] { home->openCreateRoom(); };
    sidebar->onAddFriend = [this]
    {
        if (core.store().state().currentRoom && ! minimized) setMinimized (true);
        home->openAddFriend();
    };
    sidebar->onOpenFriend = [this] (const core::UserPublic& u)
    {
        if (auto* screen = getJamScreen(); screen != nullptr && screen->isVisible()) screen->openProfile (u.id);
        else home->openProfile (u);
    };
    sidebar->onProfileMenu = [this] { if (profileMenu != nullptr) closeProfileMenu(); else openProfileMenu(); };
    sidebar->onAcceptInvite = [this] (const juce::String& id) { acceptInvite (id); };
    sidebar->onDeclineInvite = [this] (const juce::String& id)
    {
        // 누른 카드(버튼)가 목록에서 지워지므로 다음 메시지에서
        juce::Component::SafePointer<AppRoot> safe (this);
        juce::MessageManager::callAsync ([safe, id] { if (safe != nullptr) safe->core.home().dismissInvite (id); });
    };
    home = std::make_unique<HomeScreen> (core, update);
    home->setShell (sidebar->currentWidth(), page == Sidebar::Page::rooms ? TXT ("공개 방") : TXT ("홈"), page != Sidebar::Page::rooms);
    home->onTitleBarMouseDown = [this] { titleBarMouseDown(); };
    home->onOverlayChanged = [this] { syncToggle(); };
    home->onOpenSettingsTab = [this] (int tab) { if (onOpenSettings) onOpenSettings (tab); };
    home->onShowRooms = [this] { showPage (Sidebar::Page::rooms); };
    home->onAcceptInvite = [this] (const juce::String& id) { acceptInvite (id); };
    home->onOpenSettings = [this] { if (onOpenSettings) onOpenSettings (0); };
    home->onJoinFailure = [this] (const core::ApiError& e) { showJoinFailure (e); };
    home->onReturnToRoom = [this] { setMinimized (false); };
    addChildComponent (*home);
    toggle = std::make_unique<SidebarToggle>();
    toggle->onPress = [this] { sidebar->toggleCollapsed(); syncToggle(); };
    addChildComponent (*toggle);
    core.store().addListener (this);
    core.connection().addListener (this);
    core.room().onActionFailed = [this] (core::ErrorCode code)
    {
        const auto text = msg::hostActionFailure (code);
        if (text.isNotEmpty()) addInfoToast (text, {});
    };
    core.home().onInviteFailed = [this] (core::ErrorCode code)
    {
        const auto text = msg::inviteFailure (code);
        if (text.isNotEmpty()) addInfoToast (text, {});
    };
    core.home().onFriendRequestFailed = [this] (core::ErrorCode)
    {
        addInfoToast (TXT ("친구 요청을 처리하지 못했어요. 다시 시도해 주세요"), {});
    };
    updateScreen();
    syncInviteToasts();
}

AppRoot::~AppRoot()
{
    if (home != nullptr) home->onOverlayChanged = nullptr;
    if (auto* screen = getJamScreen()) screen->onOverlayChanged = nullptr;
    core.store().removeListener (this);
    core.connection().removeListener (this);
    core.room().onActionFailed = nullptr;
    core.home().onInviteFailed = nullptr;
    core.home().onFriendRequestFailed = nullptr;
    core.home().setHomeVisible (false);
    profileMenu.reset();
    toasts.clear();
    fail.reset();
    jam.reset();
    home.reset();
}

void AppRoot::paint (juce::Graphics& g)
{
    g.fillAll (col::white);
}

void AppRoot::paintOverChildren (juce::Graphics& g)
{
    if (hasForcedChrome && forcedChrome == Sidebar::Chrome::macWindow)
        drawMockLights (g);
}

void AppRoot::drawMockLights (juce::Graphics& g)
{
    // 52px 윗줄의 세로 가운데, 원 12. 자리는 macOS 가 정한다 (빈 툴바 + unified): 여기 값은 미리 보기용 근사치
    const juce::uint32 fills[] = { 0xFFFF5F57, 0xFFFEBC2E, 0xFF28C840 }, rims[] = { 0xFFE2463F, 0xFFE1A116, 0xFF1AAB29 };
    for (int i = 0; i < 3; ++i)
    {
        const juce::Rectangle<float> c (20.0f + 20.0f * (float) i, 20.0f, 12.0f, 12.0f);
        g.setColour (juce::Colour (fills[i]));
        g.fillEllipse (c);
        g.setColour (juce::Colour (rims[i]));
        g.drawEllipse (c.reduced (0.25f), 0.5f);
    }
}

Sidebar::Chrome AppRoot::chromeMode() const
{
    if (hasForcedChrome) return forcedChrome;
    if (core.isPlugin()) return Sidebar::Chrome::legacy;   // 플러그인은 이번에 바꾸지 않는다
    // 맥 독립 앱 창 모드: 신호등이 있다. 전체 화면이면 신호등이 사라지므로 디자인 fullscreen 켜짐 배치.
    // 윈도우·리눅스는 제목 줄을 그대로 두고 사이드바 윗부분은 신호등이 없는 배치 (사용자 결정 2026-10-30)
    if (auto* peer = getPeer(); peer != nullptr && meon::chrome::installedOn (peer->getNativeHandle()) && ! peer->isFullScreen())
        return Sidebar::Chrome::macWindow;
    return Sidebar::Chrome::flat;
}

void AppRoot::titleBarMouseDown()
{
    if (chromeMode() == Sidebar::Chrome::macWindow)
        if (auto* peer = getPeer())
            meon::chrome::titleBarMouseDown (peer->getNativeHandle());
}

void AppRoot::syncToggle()
{
    if (toggle == nullptr || home == nullptr || sidebar == nullptr) return;
    const auto ta = sidebar->toggleArea();
    const bool show = sidebar->isVisible() && ! ta.isEmpty();
    toggle->setVisible (show);
    if (! show) return;
    toggle->setBounds (ta);
    toggle->setOpen (! sidebar->isCollapsed());
    // 모달 덮개 아래 (홈 모달은 창 전체, 방 화면 모달은 방 화면만 덮는다)
    auto* screen = getJamScreen();
    const bool covered = roomClosed != nullptr
                      || (home->isVisible() && home->dimsWindow())
                      || (screen != nullptr && screen->isVisible() && screen->dimsScreen() && screen->getBounds().contains (ta));
    toggle->setCovered (covered);
}

//==============================================================================
void AppRoot::setMinimized (bool b)
{
    if (! core.store().state().currentRoom) b = false;
    if (b == minimized) return;
    minimized = b;
    // 새 채팅 수는 최소화한 뒤부터 센다. 돌아오면 읽은 것으로
    if (auto* j = core.jam()) j->chat().markRead();
    applyScreen();
}

void AppRoot::applyScreen()
{
    // 화면만 바꾼다. 방 연결·오디오·채팅·세션 로그는 JamSession 이 그대로 이어 간다.
    // 셀프 모니터는 방 안이면 prefs.selfMonitor 만 본다 (JamSession::isSelfMonitorAudible). HomeController 의
    // setHomeVisible 은 공개 방 구독·새로 고침만 하므로 최소화해도 내 소리는 그대로다
    if (minimized)
    {
        if (jam != nullptr) jam->setVisible (false);   // 지우지 않는다: 채팅 스크롤·입력 중인 글·접기 상태가 남는다
        // 최소화 막대는 쓰지 않는다: 사이드바 "참여 중" 항목이 대신한다 (사용자 결정 2026-10-29)
        home->setRoomBar (false);
        home->setVisible (true);
        core.home().setHomeVisible (true);
        home->grabKeyboardFocus();
    }
    else
    {
        home->closeOverlays();
        home->setRoomBar (false);
        home->setVisible (false);
        core.home().setHomeVisible (false);
        if (jam != nullptr) jam->setVisible (true);
    }
    syncSidebar();
    resized();
    for (auto* t : toasts) t->toFront (false);
    if (fail != nullptr) fail->toFront (false);
}

//==============================================================================
void AppRoot::syncSidebar()
{
    const bool inRoomView = core.store().state().currentRoom && ! minimized;
    sidebar->setPage (inRoomView ? Sidebar::Page::room : page);
    home->setShell (sidebar->currentWidth(), page == Sidebar::Page::rooms ? TXT ("공개 방") : TXT ("홈"), page != Sidebar::Page::rooms, sidebar->headerInset());
}

void AppRoot::showPage (Sidebar::Page p)
{
    page = p;
    // 방 안이면 방은 그대로 두고 화면만 홈으로 (방 최소화와 같다)
    if (core.store().state().currentRoom && ! minimized) setMinimized (true);
    syncSidebar();
}

void AppRoot::openProfileMenu()
{
    const auto& s = core.store().state();
    profileMenu = std::make_unique<ProfileMenu> ((int) (s.friendRequests.size() + s.invites.size()));
    profileMenu->onDismiss = [this] { closeProfileMenu(); };
    profileMenu->onNotifications = [this]
    {
        closeProfileMenu();
        if (core.store().state().currentRoom && ! minimized) setMinimized (true);
        home->toggleNotifications();
    };
    profileMenu->onSettings = [this] { closeProfileMenu(); if (onOpenSettings) onOpenSettings (0); };
    addAndMakeVisible (*profileMenu);
    sidebar->setProfileMenuOpen (true);
    resized();
    profileMenu->toFront (false);
}

void AppRoot::closeProfileMenu()
{
    profileMenu.reset();
    sidebar->setProfileMenuOpen (false);
}

void AppRoot::updateScreen()
{
    const auto& room = core.store().state().currentRoom;
    if (room)
    {
        if (jam == nullptr && core.jam() != nullptr)
        {
            auto screen = std::make_unique<JamScreen> (core);
            screen->onOpenSettings = [this] { if (onOpenSettings) onOpenSettings (0); };
            screen->onJoinFailure = [this] (const core::ApiError& e) { showJoinFailure (e); };
            screen->onMinimize = [this]
            {
                // 로비 버튼 콜백 안이므로 다음 메시지에서 (합주 화면을 숨기는 동안 버튼이 눌린 상태로 남지 않게)
                juce::Component::SafePointer<AppRoot> safe (this);
                juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->setMinimized (true); });
            };
            screen->onTitleBarMouseDown = [this] { titleBarMouseDown(); };
            screen->onOverlayChanged = [this] { syncToggle(); };
            jam = std::move (screen);
            addAndMakeVisible (*jam);
            toggle->toFront (false);   // 접기 버튼은 방 화면 위 (토스트·겹침은 그 뒤에 다시 앞으로)
            minimized = false;   // 새로 들어간 방은 합주 화면부터
        }
        // 다른 기기에서 들어간 방으로 옮겨져도(movedToOtherRoom) 최소화 상태는 그대로 둔다 (막대가 새 방을 보여 준다)
        shownRoomId = room->id;
        // 방에 들어가면 받은 초대는 모두 사라진다 (API 7.11)
        for (int i = toasts.size(); --i >= 0;)
            if (toasts[i]->key.isNotEmpty() && ! toasts[i]->key.startsWith ("jr:")) toasts.remove (i);
        fail.reset();
        applyScreen();
        return;
    }
    else
    {
        // 최소화 중에 내보내지거나 방이 닫혀도 여기로 온다: 막대가 사라지고 보통 홈 + 알림 토스트
        minimized = false;
        home->setRoomBar (false);
        jam.reset();
        shownRoomId = {};
        home->setVisible (true);
        if (core.room().takeClosedByMe())
        {
            auto overlay = std::make_unique<RoomClosedOverlay>();
            overlay->onHome = [this]
            {
                juce::Component::SafePointer<AppRoot> safe (this);
                juce::MessageManager::callAsync ([safe] { if (safe != nullptr) { safe->roomClosed.reset(); safe->resized(); } });
            };
            roomClosed = std::move (overlay);
            addAndMakeVisible (*roomClosed);
        }
        core.home().setHomeVisible (true);
    }
    syncSidebar();
    resized();
    for (auto* t : toasts) t->toFront (false);
    if (fail != nullptr) fail->toFront (false);
}

void AppRoot::stateChanged (core::StateKey k)
{
    if (k == core::StateKey::currentRoom)
    {
        const auto& room = core.store().state().currentRoom;
        if ((room ? room->id : juce::String()) != shownRoomId || (room.has_value() == (jam == nullptr)))
            updateScreen();
    }
    else if (k == core::StateKey::invites)
    {
        syncInviteToasts();
    }
    if (k == core::StateKey::joinRequests || k == core::StateKey::currentRoom)
        syncJoinRequestToasts();
}

void AppRoot::notice (const core::Notice& n)
{
    using K = core::Notice::Kind;
    switch (n.kind)
    {
        case K::kicked:     addInfoToast (TXT ("방에서 내보내졌어요"), n.roomTitle); break;
        case K::roomClosed: addInfoToast (TXT ("방장이 방을 닫았어요"), n.roomTitle); break;
        case K::movedToOtherRoom: addInfoToast (TXT ("다른 기기에서 들어간 방으로 연결됐어요"), n.roomTitle); break;
        case K::roomLostWhileDisconnected:
            addInfoToast (TXT ("방에서 나왔어요"), TXT ("연결이 오래 끊겨서 방에서 나왔어요"), Toast::Kind::warning);
            break;
        default: break;   // 초대는 store.invites 로, 승인 수락은 currentRoom 으로 처리된다
    }
}

//==============================================================================
void AppRoot::syncInviteToasts()
{
    const auto& s = core.store().state();
    // 사라진 초대 (만료·입장·닫기)
    for (int i = toasts.size(); --i >= 0;)
    {
        auto* t = toasts[i];
        if (t->key.isEmpty() || t->key.startsWith ("jr:")) continue;   // 입장 요청 알림은 syncJoinRequestToasts 가
        bool present = false;
        for (auto& inv : s.invites) if (inv.id == t->key) present = true;
        if (! present || s.inRoom()) toasts.remove (i);
    }
    if (s.inRoom()) { layoutToasts(); return; }
    // 새 초대
    bool added = false;
    for (auto& inv : s.invites)
    {
        bool shown = false;
        for (auto* t : toasts) if (t->key == inv.id) shown = true;
        if (shown || seenInvites.contains (inv.id)) continue;
        seenInvites.add (inv.id);
        added = true;
        // 홈·사이드바 디자인: 거절·수락 (예전 "입장" + 닫기와 같은 동작: 수락 = acceptInvite, 거절 = dismissInvite)
        auto* t = toasts.add (new Toast (inv.from.nickname.value_or (inv.from.username) + TXT ("님이 초대했어요"), inv.room.title, TXT ("수락")));
        t->key = inv.id;
        t->setClosable (false);
        t->setSecondary (TXT ("거절"));
        const auto id = inv.id;
        // 버튼 콜백 안에서 토스트가 지워지지 않게 다음 메시지에서 처리한다
        t->onAction = [this, id] { acceptInvite (id); };
        t->onSecondary = [this, id]
        {
            juce::Component::SafePointer<AppRoot> safeSelf (this); juce::MessageManager::callAsync ([safe = safeSelf, id]
            {
                if (safe != nullptr) safe->core.home().dismissInvite (id);
            });
        };
        t->onClose = t->onSecondary;
        addAndMakeVisible (t);
        // 디자인 v2: 알림은 10초 뒤 닫힌다. 초대는 사이드바 카드·알림 목록에 초대가 끝날 때까지 남는다
        juce::Component::SafePointer<AppRoot> safe (this);
        juce::Component::SafePointer<Toast> sp (t);
        juce::Timer::callAfterDelay (10000, [safe, sp] { if (safe != nullptr && sp != nullptr) safe->removeToast (sp.getComponent()); });
    }
    layoutToasts();
    // 초대 토스트 소리 (설정 › 알림). 합주 중에는 울리지 않는다 (여기까지 왔으면 방 밖). 잠정: 시스템 알림음 (Q13)
    if (added)
    {
        MeonSettings settings;
        if (settings.properties().getBoolValue ("toastSound", true))
            juce::LookAndFeel::getDefaultLookAndFeel().playAlertSound();
    }
}

void AppRoot::acceptInvite (const juce::String& id)
{
    // 버튼 콜백 안에서 토스트·사이드바 카드가 지워지지 않게 다음 메시지에서 처리한다
    juce::Component::SafePointer<AppRoot> safeSelf (this); juce::MessageManager::callAsync ([safe = safeSelf, id]
    {
        if (safe == nullptr) return;
        safe->core.home().acceptInvite (id, [safe] (core::Result<core::JoinResult> r)
        {
            if (safe == nullptr || r.ok()) return;
            // 방이 닫혔거나(410) 이미 끝난 초대(404)면 토스트로 알린다. 나머지는 입장 실패 모달 (7.9)
            const auto text = msg::inviteAcceptFailure (r.code());
            if (text.isNotEmpty()) safe->addInfoToast (text, {});
            else safe->showJoinFailure (r.err());
        });
    });
}

void AppRoot::syncJoinRequestToasts()
{
    const auto& s = core.store().state();
    const bool host = s.amHost();
    // 사라진 요청 (승인·거절·만료·방 나감)
    for (int i = toasts.size(); --i >= 0;)
    {
        auto* t = toasts[i];
        if (! t->key.startsWith ("jr:")) continue;
        bool present = false;
        for (auto& q : s.joinRequests) if ("jr:" + q.id == t->key) present = true;
        if (! present || ! host) toasts.remove (i);
    }
    if (! host) { layoutToasts(); return; }
    for (auto& q : s.joinRequests)
    {
        const auto key = "jr:" + q.id;
        bool shown = false;
        for (auto* t : toasts) if (t->key == key) shown = true;
        if (shown || seenJoinRequests.contains (q.id)) continue;   // 10초 뒤 닫힌 알림은 다시 띄우지 않는다
        seenJoinRequests.add (q.id);
        const auto name = q.user.nickname.value_or (q.user.username);
        const auto sub = icons::partLabel (q.user.part.value_or ("other")) + TXT (" · ") + (s.currentRoom ? s.currentRoom->title : juce::String());
        auto* t = toasts.add (new Toast (name + TXT ("님이 입장을 요청했어요"), sub, TXT ("승인")));
        t->key = key;
        t->setClosable (false);
        t->setSecondary (TXT ("거절"));
        const auto id = q.id;
        t->onAction = [this, id]
        {
            juce::Component::SafePointer<AppRoot> safe (this);
            juce::MessageManager::callAsync ([safe, id] { if (safe != nullptr) safe->core.room().acceptJoinRequest (id); });
        };
        t->onSecondary = [this, id]
        {
            juce::Component::SafePointer<AppRoot> safe (this);
            juce::MessageManager::callAsync ([safe, id] { if (safe != nullptr) safe->core.room().declineJoinRequest (id); });
        };
        addAndMakeVisible (t);
        // 디자인: 10초 뒤 알림만 닫힌다 (요청은 방 화면의 입장 요청 목록·사이드바 점에 남는다)
        juce::Component::SafePointer<AppRoot> safe (this);
        juce::Component::SafePointer<Toast> sp (t);
        juce::Timer::callAfterDelay (10000, [safe, sp] { if (safe != nullptr && sp != nullptr) safe->removeToast (sp.getComponent()); });
    }
    layoutToasts();
}

void AppRoot::connectionToastChanged (core::ConnectionWatch::Toast state)
{
    using T = core::ConnectionWatch::Toast;
    if (state == T::none)
    {
        if (connectionToast != nullptr) removeToast (connectionToast.getComponent());
        connectionToast = nullptr;
        return;
    }
    if (connectionToast == nullptr)
    {
        // 다른 토스트와 같은 자리 (아래에서 위로 쌓기). 닫기 버튼 없이, 상태가 바뀔 때만 바뀌고 닫힌다
        auto* t = toasts.insert (0, new Toast ({}, {}));
        t->setClosable (false);
        connectionToast = t;
        addAndMakeVisible (t);
    }
    // 끊긴 동안은 경고 토스트, 다시 붙으면 같은 자리에서 일반 토스트 한 줄로 바뀐다 (높이가 달라서 다시 쌓는다).
    // 끊긴 이유가 바뀌면(합주 서버 UDP 막힘 등) lost 가 다시 와서 문구만 바꾼다
    if (state == T::lost)
    {
        const auto text = msg::connectionLost (core.jamServerProblem());
        connectionToast->setContent (Toast::Kind::warning, text.title, text.body);
    }
    else
        connectionToast->setContent (Toast::Kind::normal, TXT ("다시 연결됐어요"), {});
    layoutToasts();
}

void AppRoot::addInfoToast (const juce::String& title, const juce::String& sub, Toast::Kind kind)
{
    auto* t = toasts.insert (0, new Toast (title, sub));
    t->setContent (kind, title, sub);
    juce::Component::SafePointer<Toast> sp (t);
    t->onClose = [this, sp] { if (sp != nullptr) removeToast (sp.getComponent()); };
    addAndMakeVisible (t);
    // 6초 뒤 자동으로 닫힘
    juce::Component::SafePointer<AppRoot> safe (this);
    juce::Timer::callAfterDelay (6000, [safe, sp]
    {
        if (safe != nullptr && sp != nullptr) safe->removeToast (sp.getComponent());
    });
    layoutToasts();
}

void AppRoot::removeToast (Toast* t)
{
    // 버튼 콜백 안에서 지워질 수 있으므로 다음 메시지에서
    juce::Component::SafePointer<AppRoot> safe (this);
    juce::Component::SafePointer<Toast> sp (t);
    juce::MessageManager::callAsync ([safe, sp]
    {
        if (safe == nullptr || sp == nullptr) return;
        safe->toasts.removeObject (sp.getComponent());
        safe->layoutToasts();
    });
}

void AppRoot::layoutToasts()
{
    // 방 최소화 중 막대(57) 위로: 아래 여백 12 → 69 (디자인 hToastBottom)
    int y = getHeight() - (home->isVisible() && home->hasRoomBar() ? 12 + HomeScreen::roomBarH : 12);
    for (auto* t : toasts)
    {
        const int h = t->heightForWidth (Toast::width);
        y -= h;
        t->setBounds (getWidth() - 12 - Toast::width, y, Toast::width, h);
        t->toFront (false);
        y -= metric::gap;
    }
}

//==============================================================================
JamScreen* AppRoot::getJamScreen() { return dynamic_cast<JamScreen*> (jam.get()); }

void AppRoot::showJoinFailure (const core::ApiError& e)
{
    showJoinFailure (core::HomeController::classify (e));
}

void AppRoot::showJoinFailure (core::HomeController::JoinFailure f)
{
    // 426: 입장 실패 창 대신 강제 업데이트 화면 (PR2). MeonApi 가 426 을 받으면 onClientOutdated 로 그 화면이 뜬다
    if (f == core::HomeController::JoinFailure::version) return;
    // 닫힌 방: 창 대신 토스트. HomeController 가 목록에서 바로 빼고 목록을 다시 받는다
    if (f == core::HomeController::JoinFailure::closed) { addInfoToast (TXT ("이미 닫힌 방이에요"), {}); return; }
    fail = std::make_unique<JoinFailDialog>();
    fail->onClose = [this] { juce::Component::SafePointer<AppRoot> safeSelf (this); juce::MessageManager::callAsync ([safe = safeSelf] { if (safe != nullptr) safe->fail.reset(); }); };
    fail->onUpdate = [this]
    {
        if (update.start) update.start();
        juce::Component::SafePointer<AppRoot> safeSelf (this); juce::MessageManager::callAsync ([safe = safeSelf] { if (safe != nullptr) safe->fail.reset(); });
    };
    fail->show (f);
    addAndMakeVisible (*fail);
    resized();
    fail->toFront (false);
}

bool AppRoot::handleShortcut (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey)
    {
        if (profileMenu != nullptr) { closeProfileMenu(); return true; }
        if (fail != nullptr) { fail.reset(); return true; }
        if (home->isVisible() && home->closeTopOverlay()) return true;
    }
    // 방 최소화 중에는 합주 화면이 숨어 있으므로 단축키(M·N·Enter …)를 넘기지 않는다
    if (auto* screen = dynamic_cast<JamScreen*> (jam.get()); screen != nullptr && screen->isVisible())
        return screen->handleShortcut (k);
    return false;
}

void AppRoot::resized()
{
    // 방 화면이 들어갈 폭 (멤버 쪽 566 + 여백 54 + 오른쪽 패널 기본폭: 앱 940, 플러그인 860). 창이 좁으면 방 화면에서만
    // 사이드바를 잠시 접는다(56): 앱 1180, 플러그인 1100 미만 (사용자 결정 2026-10-29). 홈에서는 늘 사용자가 정한 대로.
    // 통째로 숨기는 것은 플러그인만 (900~915). 독립 앱은 창이 최소 폭(1024)보다 좁아져도(작은 화면·배율, 창 나란히 놓기)
    // 접힌 아이콘 줄은 남긴다 (예전에는 이 조건에 플러그인 확인이 없어서 996 미만이면 앱에서도 숨었다)
    sidebar->setChrome (chromeMode());
    const bool roomView = jam != nullptr && jam->isVisible() && ! minimized;
    const int need = core::panel::chrome + core::panel::membersMin + core::panel::defaultWidth (core.isPlugin());
    sidebar->setForcedCollapsed (roomView && getWidth() - Sidebar::openW < need);
    const bool hideSidebar = core.isPlugin() && roomView && getWidth() - Sidebar::closedW < need;
    sidebar->setVisible (! hideSidebar);
    const int sw = hideSidebar ? 0 : sidebar->currentWidth();
    // 사이드바가 옆에 있으면 방 화면 윗줄의 옛 앱 버튼(로고·로비 보기·알림·설정)을 뺀다. 숨으면(좁은 플러그인 창) 예전 윗줄 (갈 곳이 없어지지 않게)
    // 본문 제목 줄 왼쪽 여백 (디자인 hdPL): 맥 창 모드에서 사이드바를 접으면 72 (신호등·접기 버튼 자리), 접는 동안 같이 움직인다
    const int titleInset = hideSidebar ? metric::padX : sidebar->headerInset();
    if (auto* screen = getJamScreen()) { screen->setSidebarShown (! hideSidebar); screen->setHeaderInset (titleInset); }
    sidebar->setBounds (0, 0, juce::jmax (sw, Sidebar::closedW), getHeight());
    home->setShell (sw, page == Sidebar::Page::rooms ? TXT ("공개 방") : TXT ("홈"), page != Sidebar::Page::rooms, titleInset);
    home->setBounds (getLocalBounds());
    if (jam != nullptr) jam->setBounds (getLocalBounds().withTrimmedLeft (sw));
    if (profileMenu != nullptr)
    {
        profileMenu->setBounds (getLocalBounds());
        profileMenu->box = { 9, getHeight() - 55 - ProfileMenu::boxH, ProfileMenu::boxW, ProfileMenu::boxH };
    }
    if (roomClosed != nullptr) { roomClosed->setBounds (getLocalBounds()); roomClosed->toFront (false); }
    if (fail != nullptr)
        fail->setBounds (juce::Rectangle<int> (380, fail->getPreferredHeight()).withCentre (getLocalBounds().getCentre()));
    syncToggle();
    layoutToasts();
}

} // namespace meon::ui
