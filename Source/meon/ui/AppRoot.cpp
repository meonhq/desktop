#include "AppRoot.h"
#include "Messages.h"
#include "../MeonSettings.h"
#include "screens/JamScreen.h"
#include "overlays/JamOverlays.h"

namespace meon::ui
{

AppRoot::AppRoot (core::MeonCore& c, HomeScreen::UpdateHooks u) : core (c), update (std::move (u))
{
    setOpaque (true);
    // 사이드바를 먼저 넣는다 (홈 화면 아래). 홈 화면은 사이드바 자리를 그리지도 받지도 않고, 모달만 창 전체를 덮는다
    sidebar = std::make_unique<Sidebar> (core);
    addAndMakeVisible (*sidebar);
    sidebar->onLayoutChanged = [this] { resized(); };
    sidebar->onHome = [this] { showPage (Sidebar::Page::home); };
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
    home = std::make_unique<HomeScreen> (core, update);
    home->setShell (sidebar->currentWidth(), TXT ("홈"));
    home->onOpenSettings = [this] { if (onOpenSettings) onOpenSettings (0); };
    home->onJoinFailure = [this] (const core::ApiError& e) { showJoinFailure (e); };
    home->onReturnToRoom = [this] { setMinimized (false); };
    addChildComponent (*home);
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
        home->setRoomBar (true);
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
    home->setShell (sidebar->currentWidth(), page == Sidebar::Page::rooms ? TXT ("공개 방") : TXT ("홈"));
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
            jam = std::move (screen);
            addAndMakeVisible (*jam);
            minimized = false;   // 새로 들어간 방은 합주 화면부터
        }
        // 다른 기기에서 들어간 방으로 옮겨져도(movedToOtherRoom) 최소화 상태는 그대로 둔다 (막대가 새 방을 보여 준다)
        shownRoomId = room->id;
        // 방에 들어가면 받은 초대는 모두 사라진다 (API 7.11)
        for (int i = toasts.size(); --i >= 0;)
            if (toasts[i]->key.isNotEmpty()) toasts.remove (i);
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
        if (t->key.isEmpty()) continue;
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
        if (shown) continue;
        added = true;
        auto* t = toasts.add (new Toast (inv.from.nickname.value_or (inv.from.username) + TXT ("님이 초대했어요"), inv.room.title, TXT ("입장")));
        t->key = inv.id;
        const auto id = inv.id;
        // 버튼 콜백 안에서 토스트가 지워지지 않게 다음 메시지에서 처리한다
        t->onAction = [this, id]
        {
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
        };
        t->onClose = [this, id]
        {
            juce::Component::SafePointer<AppRoot> safeSelf (this); juce::MessageManager::callAsync ([safe = safeSelf, id]
            {
                if (safe != nullptr) safe->core.home().dismissInvite (id);
            });
        };
        addAndMakeVisible (t);
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
    const int sw = sidebar->currentWidth();
    sidebar->setBounds (0, 0, sw, getHeight());
    home->setShell (sw, page == Sidebar::Page::rooms ? TXT ("공개 방") : TXT ("홈"));
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
    layoutToasts();
}

} // namespace meon::ui
