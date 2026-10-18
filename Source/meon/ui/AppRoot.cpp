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
    home = std::make_unique<HomeScreen> (core, update);
    home->onOpenSettings = [this] { if (onOpenSettings) onOpenSettings (0); };
    home->onJoinFailure = [this] (const core::ApiError& e) { showJoinFailure (e); };
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
    updateScreen();
    syncInviteToasts();
}

AppRoot::~AppRoot()
{
    core.store().removeListener (this);
    core.connection().removeListener (this);
    core.room().onActionFailed = nullptr;
    core.home().onInviteFailed = nullptr;
    core.home().setHomeVisible (false);
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
            jam = std::move (screen);
            addAndMakeVisible (*jam);
        }
        home->closeOverlays();
        home->setVisible (false);
        core.home().setHomeVisible (false);
        shownRoomId = room->id;
        // 방에 들어가면 받은 초대는 모두 사라진다 (API 7.11)
        for (int i = toasts.size(); --i >= 0;)
            if (toasts[i]->key.isNotEmpty()) toasts.remove (i);
        fail.reset();
    }
    else
    {
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
    // 끊긴 동안은 경고 토스트, 다시 붙으면 같은 자리에서 일반 토스트 한 줄로 바뀐다 (높이가 달라서 다시 쌓는다)
    // 설명은 문장마다 줄을 나눈다 (글자 단위로 줄바꿈하는 플랫폼에서 "중이/에요" 처럼 끊기지 않게)
    if (state == T::lost)
        connectionToast->setContent (Toast::Kind::warning, TXT ("연결이 끊겼어요"),
                                     TXT ("인터넷 연결을 확인해 주세요.") + "\n" + TXT ("다시 연결하는 중이에요"));
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
    int y = getHeight() - 12;
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
        if (fail != nullptr) { fail.reset(); return true; }
        if (home->isVisible() && home->hasOverlay()) { home->closeOverlays(); return true; }
    }
    if (auto* screen = dynamic_cast<JamScreen*> (jam.get()))
        return screen->handleShortcut (k);
    return false;
}

void AppRoot::resized()
{
    home->setBounds (getLocalBounds());
    if (jam != nullptr) jam->setBounds (getLocalBounds());
    if (roomClosed != nullptr) { roomClosed->setBounds (getLocalBounds()); roomClosed->toFront (false); }
    if (fail != nullptr)
        fail->setBounds (juce::Rectangle<int> (380, fail->getPreferredHeight()).withCentre (getLocalBounds().getCentre()));
    layoutToasts();
}

} // namespace meon::ui
