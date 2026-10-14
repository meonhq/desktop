#include "JamOverlays.h"
#include "FormText.h"
#include "../Messages.h"
#include "../theme/Icons.h"
#include "../../core/api/ApiJson.h"

namespace meon::ui
{

using namespace form;

namespace
{
    juce::String nameOf (const core::UserPublic& u) { return u.nickname.value_or (u.username); }
}

//==============================================================================
RoomSettingsModal::RoomSettingsModal (core::MeonCore& c) : Modal (TXT ("방 설정"), 460), core (c)
{
    if (core.store().state().currentRoom) room = *core.store().state().currentRoom;
    name.setMaxLength (30);
    desc.setMaxLength (80);
    name.setText (room.title);
    desc.setText (room.description);
    visibility.setOptions ({ "public", "private" }, { TXT ("공개"), TXT ("비공개") });
    visibility.setSelected (room.type);
    entry.setOptions ({ "instant", "approval" }, { TXT ("바로 입장"), TXT ("방장 승인") });
    entry.setSelected (room.entryMode.value_or ("instant"));
    capacity.setItems ({ "2", "3", "4", "5", "6", "7" }, { TXT ("2명"), TXT ("3명"), TXT ("4명"), TXT ("5명"), TXT ("6명"), TXT ("7명") });
    for (int n = 2; n <= 7; ++n) capacity.setItemDisabled (juce::String (n), n < room.memberCount);
    capacity.setValue (juce::String (room.capacity));
    for (auto* comp : std::initializer_list<juce::Component*> { &name, &desc, &visibility, &entry, &capacity, &closeRoom, &footer })
        body.addAndMakeVisible (comp);
    footer.setButtons ({ &cancel, &save });
    closeRoom.setConfirm (TXT ("한 번 더 누르면 방을 닫아요"));
    // 확정 대기 문구로 바뀌거나 되돌아가면 글자 폭에 맞춰 다시 배치 (Body::resized 가 getIdealWidth 를 쓴다)
    closeRoom.onStateChange = [this] { body.resized(); body.repaint(); };

    name.onChange = desc.onChange = [this] { footer.setNote ({}, col::danger); refresh(); };
    visibility.onChange = entry.onChange = [this] { refresh(); };
    capacity.onValueChange = [this] (const juce::String&) { refresh(); };
    cancel.onPress = [this] { if (onClose) onClose(); };
    save.onPress = [this] { submit(); };
    closeRoom.onPress = [this]
    {
        std::weak_ptr<bool> weak = alive;
        core.room().closeRoom ([this, weak] (core::Result<core::Done> r)
        {
            if (weak.expired() || r.ok()) return;   // 성공하면 현재 방이 비고 AppRoot 가 덮개를 띄운다
            footer.setNote (msg::general (r.err()), col::danger);
        });
    };
    setContent (&body, bodyHeight() + ModalFooter::height);
    refresh();
}

juce::String RoomSettingsModal::visNote() const
{
    if (isPublic()) return TXT ("홈의 공개 방 목록에 보이고 누구나 찾아 들어올 수 있어요");
    if (room.inviteCode) return TXT ("목록에서 사라져요. 입장 코드 ") + *room.inviteCode + TXT ("로만 들어올 수 있어요");
    return TXT ("목록에서 사라져요. 저장하면 나오는 입장 코드로만 들어올 수 있어요");
}

bool RoomSettingsModal::isDirty() const
{
    const auto entryNow = room.entryMode.value_or ("instant");
    return name.getText().trim() != room.title || desc.getText().trim() != room.description
        || visibility.getSelected() != room.type || (isPublic() && entry.getSelected() != entryNow)
        || capacity.getValue().getIntValue() != room.capacity;
}

int RoomSettingsModal::bodyHeight() const
{
    const int w = 460 - 36;
    int h = 18 + (15 + 6 + 32) + 18 + (15 + 6 + 55) + 18 + (15 + 6 + 32 + 6 + hintHeight (visNote(), w)) + 18;
    if (isPublic()) h += (15 + 6 + 32 + 6 + hintHeight (TXT ("요청이 오면 방장이 수락해야 들어와요. 거절해도 상대에게는 알리지 않아요"), w)) + 18;
    h += 15 + 6 + 32;
    if (room.memberCount > 2) h += 6 + 18;
    h += 18;
    h += 1 + 12 + 38 + 12;   // 방 닫기 줄
    return h;
}

void RoomSettingsModal::refresh()
{
    const bool ok = isDirty() && name.getText().trim().isNotEmpty() && ! busy;
    save.setDimmed (! ok);
    entry.setVisible (isPublic());
    setContentHeight (bodyHeight() + ModalFooter::height);
    body.resized();
    body.repaint();
    footer.resized();
}

void RoomSettingsModal::Body::paint (juce::Graphics& g)
{
    auto& o = owner;
    const int w = getWidth() - 36;
    int y = 18;
    drawLabel (g, TXT ("방 이름"), { metric::padX, y, w, 15 });
    drawCounter (g, juce::String (countChars (o.name.getText())) + " / 30", { metric::padX, y, w, 15 });
    y += 15 + 6 + 32 + 18;
    drawLabel (g, TXT ("소개"), { metric::padX, y, w, 15 }, TXT ("· 선택"));
    drawCounter (g, juce::String (countChars (o.desc.getText())) + " / 80", { metric::padX, y, w, 15 });
    y += 15 + 6 + 55 + 18;
    drawLabel (g, TXT ("공개 설정"), { metric::padX, y, w, 15 });
    y += 15 + 6 + 32 + 6;
    const int vh = hintHeight (o.visNote(), w);
    drawHint (g, o.visNote(), { metric::padX, y, w, vh });
    y += vh + 18;
    if (o.isPublic())
    {
        drawLabel (g, TXT ("입장 방식"), { metric::padX, y, w, 15 });
        y += 15 + 6 + 32 + 6;
        const auto note = o.entry.getSelected() == "approval" ? TXT ("요청이 오면 방장이 수락해야 들어와요. 거절해도 상대에게는 알리지 않아요")
                                                              : TXT ("누구든 입장 버튼을 누르면 바로 들어와요");
        const int eh = hintHeight (TXT ("요청이 오면 방장이 수락해야 들어와요. 거절해도 상대에게는 알리지 않아요"), w);
        drawHint (g, note, { metric::padX, y, w, eh });
        y += eh + 18;
    }
    drawLabel (g, TXT ("최대 인원"), { metric::padX, y, w, 15 });
    y += 15 + 6 + 32;
    if (o.room.memberCount > 2)
    {
        drawHint (g, TXT ("지금 ") + juce::String (o.room.memberCount) + TXT ("명이 있어서 그보다 적게는 줄일 수 없어요"), { metric::padX, y + 6, w, 18 });
        y += 6 + 18;
    }
    y += 18;
    // 방 닫기 줄
    g.setColour (col::line);
    g.fillRect (0, y, getWidth(), 1);
    g.setColour (col::ink);
    g.setFont (font (500, 14.0f));
    g.drawText (TXT ("방 닫기"), metric::padX, y + 12, w - 150, 17, juce::Justification::centredLeft, false);
    g.setColour (col::inkSub);
    g.setFont (font (400, 12.0f));
    g.drawText (TXT ("멤버 모두 홈으로 돌아가고 방이 목록에서 사라져요"), metric::padX, y + 12 + 17 + 3, o.closeRoom.getX() - 18 - metric::padX, 18,
                juce::Justification::centredLeft, true);
}

void RoomSettingsModal::Body::resized()
{
    auto& o = owner;
    const int w = getWidth() - 36;
    int y = 18 + 15 + 6;
    o.name.setBounds (metric::padX, y, w, 32);
    y += 32 + 18 + 15 + 6;
    o.desc.setBounds (metric::padX, y, w, 55);
    y += 55 + 18 + 15 + 6;
    o.visibility.setBounds (metric::padX, y, w, 32);
    y += 32 + 6 + hintHeight (o.visNote(), w) + 18;
    if (o.isPublic())
    {
        o.entry.setBounds (metric::padX, y + 15 + 6, w, 32);
        y += 15 + 6 + 32 + 6 + hintHeight (TXT ("요청이 오면 방장이 수락해야 들어와요. 거절해도 상대에게는 알리지 않아요"), w) + 18;
    }
    o.capacity.setBounds (metric::padX, y + 15 + 6, 120, 32);
    y += 15 + 6 + 32 + (o.room.memberCount > 2 ? 6 + 18 : 0) + 18;
    const int cw = o.closeRoom.getIdealWidth();
    o.closeRoom.setBounds (getWidth() - metric::padX - cw, y + 1 + 12 + 3, cw, metric::buttonH);
    o.footer.setBounds (getLocalBounds().removeFromBottom (ModalFooter::height));
}

void RoomSettingsModal::submit()
{
    if (busy || ! isDirty() || name.getText().trim().isEmpty()) return;
    core::RoomPatch p;
    if (name.getText().trim() != room.title) p.title = name.getText().trim();
    if (desc.getText().trim() != room.description) p.description = desc.getText().trim();
    if (visibility.getSelected() != room.type)
    {
        p.type = visibility.getSelected();
        p.setEntryMode = true;
        if (isPublic()) p.entryMode = entry.getSelected();   // 비공개로 바꾸면 null
    }
    else if (isPublic() && entry.getSelected() != room.entryMode.value_or ("instant"))
    {
        p.setEntryMode = true;
        p.entryMode = entry.getSelected();
    }
    if (capacity.getValue().getIntValue() != room.capacity) p.capacity = capacity.getValue().getIntValue();
    busy = true;
    refresh();
    std::weak_ptr<bool> weak = alive;
    core.room().patchRoom (p, [this, weak] (core::Result<core::RoomDetail> r)
    {
        if (weak.expired()) return;
        busy = false;
        if (r.ok()) { if (onClose) onClose(); return; }
        footer.setNote (r.code() == core::ErrorCode::capacityBelowMembers ? TXT ("지금 인원보다 적게는 줄일 수 없어요") : msg::general (r.err()), col::danger);
        refresh();
    });
}

//==============================================================================
ManageMemberModal::ManageMemberModal (core::MeonCore& c, const core::JamSession::Member& m)
    : Modal (TXT ("멤버 관리"), 340), core (c), member (m)
{
    avatar.setPart (m.part.isNotEmpty() ? m.part : juce::String ("other"));
    for (auto* comp : std::initializer_list<juce::Component*> { &avatar, &hand, &kick })
        body.addAndMakeVisible (comp);
    hand.setConfirm (TXT ("한 번 더 누르면 넘겨요"), true);
    kick.setConfirm (TXT ("한 번 더 누르면 내보내요"));
    const auto id = m.userId;
    hand.onPress = [this, id] { core.room().transferHost (id); if (onClose) onClose(); };
    kick.onPress = [this, id] { core.room().kick (id); if (onClose) onClose(); };
    hand.onStateChange = [this] { body.resized(); };
    kick.onStateChange = [this] { body.resized(); };
    setContent (&body, 18 + 48 + 18);
}

void ManageMemberModal::Body::paint (juce::Graphics& g)
{
    auto& o = owner;
    const int x = metric::padX + 48 + 12;
    g.setColour (col::ink);
    g.setFont (font (600, 16.0f));
    g.drawText (o.member.nickname, x, 18 + 6, o.hand.getX() - 6 - x, 19, juce::Justification::centredLeft, true);
    g.setColour (col::inkFaint);
    g.setFont (font (400, 12.0f));
    g.drawText (icons::partLabel (o.member.part.isNotEmpty() ? o.member.part : juce::String ("other")), x, 18 + 6 + 19 + 3, getWidth() - x - 18, 15,
                juce::Justification::centredLeft, true);
}

void ManageMemberModal::Body::resized()
{
    auto& o = owner;
    o.avatar.setBounds (metric::padX, 18, 48, 48);
    const int nameW = juce::jmin (120, (int) std::ceil (textWidth (font (600, 16.0f), o.member.nickname)) + 2);
    int x = metric::padX + 48 + 12 + nameW + 6;
    const int hw = o.hand.getIdealWidth(), kw = o.kick.getIdealWidth();
    o.hand.setBounds (x, 18 + 6, hw, metric::smallH);
    x += hw + 6;
    o.kick.setBounds (x, 18 + 6, kw, metric::smallH);
}

//==============================================================================
struct JoinRequestList::Row : juce::Component
{
    Row (JoinRequestList& o, const core::JoinRequest& r) : owner (o), req (r)
    {
        avatar.setPart (r.user.part.value_or ("other"));
        avatar.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (avatar);
        addAndMakeVisible (decline);
        addAndMakeVisible (accept);
        accept.setEnabled (! owner.isFull());
        decline.onPress = [this] { owner.core.room().declineJoinRequest (req.id); };
        accept.onPress = [this] { owner.core.room().acceptJoinRequest (req.id); };
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().reduced (9, 0);
        if (isMouseOver (true)) { g.setColour (col::rowHover); g.fillRoundedRectangle (r.toFloat(), metric::radius); }
        const int x = r.getX() + 9 + 32 + 9, right = decline.getX() - 9;
        g.setColour (col::ink);
        g.setFont (font (600, 13.0f));
        g.drawText (nameOf (req.user), x, 6, right - x, 17, juce::Justification::centredLeft, true);
        g.setColour (col::inkSub);
        g.setFont (font (400, 12.0f));
        g.drawText (icons::partLabel (req.user.part.value_or ("other")) + (req.viaInvite ? TXT (" · 초대받음") : juce::String()),
                    x, 6 + 17 + 3, right - x, 16, juce::Justification::centredLeft, true);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (18, 0);
        avatar.setBounds (r.removeFromLeft (32).withSizeKeepingCentre (32, 32));
        const int aw = accept.getIdealWidth(), dw = decline.getIdealWidth();
        accept.setBounds (r.removeFromRight (aw).withSizeKeepingCentre (aw, metric::smallH));
        r.removeFromRight (9);
        decline.setBounds (r.removeFromRight (dw).withSizeKeepingCentre (dw, metric::smallH));
    }
    void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked() && owner.onOpenProfile) owner.onOpenProfile (req.user); }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }

    JoinRequestList& owner;
    core::JoinRequest req;
    Avatar avatar { 32.0f, 16.0f };
    Button decline { Button::Kind::small, TXT ("거절") }, accept { Button::Kind::smallPrimary, TXT ("수락") };
};

JoinRequestList::JoinRequestList (core::MeonCore& c) : core (c)
{
    core.store().addListener (this);
    rebuild();
}

JoinRequestList::~JoinRequestList() { core.store().removeListener (this); }

bool JoinRequestList::isFull() const
{
    const auto& r = core.store().state().currentRoom;
    return r && r->memberCount >= r->capacity;
}

void JoinRequestList::stateChanged (core::StateKey k)
{
    if (k == core::StateKey::joinRequests || k == core::StateKey::currentRoom) rebuild();
}

void JoinRequestList::rebuild()
{
    rows.clear();
    for (auto& r : core.store().state().joinRequests)
        addAndMakeVisible (rows.add (new Row (*this, r)));
    resized();
    repaint();
    if (onSizeChanged) onSizeChanged();
}

int JoinRequestList::getPreferredHeight() const
{
    return 6 + (rows.isEmpty() ? 41 : rows.size() * 44) + 6 + 1 + 37;
}

void JoinRequestList::paint (juce::Graphics& g)
{
    if (rows.isEmpty())
    {
        g.setColour (col::inkFaint);
        g.setFont (font (400, 13.0f));
        g.drawText (TXT ("새 입장 요청이 없어요"), metric::padX, 6, getWidth() - 36, 41, juce::Justification::centredLeft, true);
    }
    const int y = getHeight() - 38;
    g.setColour (col::line);
    g.fillRect (0, y, getWidth(), 1);
    g.setColour (col::inkSub);
    g.setFont (font (400, 12.0f));
    g.drawText (isFull() ? TXT ("방이 가득 찼어요. 자리가 나야 수락할 수 있어요") : TXT ("거절해도 상대에게는 알리지 않아요"),
                metric::padX, y + 1, getWidth() - 36, 37, juce::Justification::centredLeft, true);
}

void JoinRequestList::resized()
{
    int y = 6;
    for (auto* r : rows) { r->setBounds (0, y, getWidth(), 44); y += 44; }
}

//==============================================================================
ProfileModal::ProfileModal (core::MeonCore& c, const core::UserPublic& u, bool jam)
    : Modal (TXT ("프로필"), 340), core (c), user (u), inJam (jam)
{
    avatar.setPart (u.part.value_or ("other"));
    for (auto* comp : std::initializer_list<juce::Component*> { &avatar, &friendTag, &remove, &block, &unblock, &footer })
        body.addChildComponent (comp);
    avatar.setVisible (true);
    footer.setVisible (true);
    remove.setConfirm (TXT ("한 번 더 누르면 삭제돼요"));
    block.setConfirm (TXT ("한 번 더 누르면 차단돼요"));
    remove.onStateChange = block.onStateChange = [this] { body.resized(); };

    const auto id = u.id;
    remove.onPress = [this, id] { core.account().removeFriend (id, reloader()); };
    block.onPress = [this, id] { core.account().block (id, reloader()); };
    unblock.onPress = [this, id] { core.account().unblock (id, reloader()); };
    request.onPress = [this]
    {
        std::weak_ptr<bool> weak = alive;
        core.home().sendFriendRequest (user.username, [this, weak] (core::Result<juce::String> r)
        {
            if (weak.expired()) return;
            note = r.ok() ? msg::friendRequestResult (r.get(), user.username) : msg::friendRequestError (r.err());
            load();
        });
    };
    joinRoom.onPress = [this]
    {
        // 버그 27: 친구 목록·방 목록 카드와 같은 상태·같은 규칙 ("요청 보냄" 동안은 아무것도 하지 않는다)
        if (! profile || ! profile->room || joinState() == core::HomeController::FriendJoin::requested) return;
        std::weak_ptr<bool> weak = alive;
        core.home().joinFriendRoom (*profile->room, [this, weak] (core::Result<core::JoinResult> r)
        {
            if (weak.expired()) return;
            if (! r.ok())
            {
                // 다른 입장 버튼과 같이 입장 실패 상자로 (이 창은 닫는다)
                auto failed = onJoinFailure;
                const auto err = r.err();
                if (onClose) onClose();
                if (failed) failed (err);
                return;
            }
            refresh();   // 승인 방이면 "요청 보냄". 바로 입장이면 합주 화면으로 바뀌며 이 창도 닫힌다
        });
    };
    accept.onPress = [this] { const auto rid = incomingRequestId(); if (rid.isNotEmpty()) core.home().acceptFriendRequest (rid, reloader()); };
    decline.onPress = [this] { const auto rid = incomingRequestId(); if (rid.isNotEmpty()) core.home().declineFriendRequest (rid, reloader()); };

    core.store().addListener (this);
    setContent (&body, 18 + 48 + 18 + 22 + 18 + ModalFooter::height);
    refresh();
    load();
    startTimer (1000);
}

core::HomeController::FriendJoin ProfileModal::joinState() const
{
    using J = core::HomeController::FriendJoin;
    if (! profile || profile->relation != "friend" || profile->status.value_or ("offline") != "in_room" || ! profile->room) return J::none;
    const auto& cur = core.store().state().currentRoom;
    if (cur && cur->id == profile->room->id) return J::none;   // 지금 내가 있는 방
    return core.home().roomJoinState (*profile->room);
}

void ProfileModal::timerCallback()
{
    if (! joinRoom.isVisible()) return;
    using J = core::HomeController::FriendJoin;
    const auto st = joinState();
    const auto label = st == J::request ? TXT ("요청") : st == J::requested ? TXT ("요청 보냄") : TXT ("참여");
    if (label != joinRoom.getButtonText() || st == J::none) refresh();
}

ProfileModal::~ProfileModal()
{
    *alive = false;
    core.store().removeListener (this);
}

void ProfileModal::stateChanged (core::StateKey k)
{
    if (k == core::StateKey::friends || k == core::StateKey::friendRequests) load();
    else if (k == core::StateKey::publicRooms) refresh();   // 입장 방식·요청 상태 (카드·친구 목록에서 요청해도 같이)
}

core::Callback<core::Done> ProfileModal::reloader()
{
    std::weak_ptr<bool> weak = alive;
    return [this, weak] (core::Result<core::Done>) { if (! weak.expired()) load(); };
}

void ProfileModal::load()
{
    std::weak_ptr<bool> weak = alive;
    core.account().loadProfile (user.id, [this, weak] (core::Result<core::UserProfile> r)
    {
        if (weak.expired() || ! r.ok()) return;
        profile = r.get();
        user = profile->user;
        refresh();
    });
}

juce::String ProfileModal::incomingRequestId() const
{
    for (auto& r : core.store().state().friendRequests)
        if (r.from.id == user.id) return r.id;
    return {};
}

juce::String ProfileModal::statusText() const
{
    if (! profile) return {};
    const auto& p = *profile;
    if (p.relation != "friend")
    {
        if (p.relation == "request_received") return TXT ("나에게 친구 요청을 보냈어요");
        if (inJam)
            if (const auto& room = core.store().state().currentRoom)
                for (auto& m : room->members)
                    if (m.user.id == user.id) return TXT ("이 방에서 합주 중") + (m.isHost ? TXT (" · 방장") : juce::String());
        return TXT ("친구만 상태를 볼 수 있어요");
    }
    const auto st = p.status.value_or ("offline");
    if (st == "in_room")
    {
        const auto& room = core.store().state().currentRoom;
        if (p.room && room && p.room->id == room->id) return TXT ("이 방에서 합주 중");
        return p.room ? p.room->title + TXT ("에서 합주 중") : TXT ("다른 방에서 합주 중");
    }
    return st == "online" ? TXT ("온라인") : TXT ("오프라인");
}

juce::String ProfileModal::lastSeenText() const
{
    if (! profile || profile->relation != "friend" || profile->status.value_or ("offline") != "offline" || ! profile->lastSeenAt) return {};
    return msg::ago (core::json::parseIsoTime (*profile->lastSeenAt), core.scheduler().nowMs());
}

bool ProfileModal::hasFooter() const
{
    return request.isVisible() || joinRoom.isVisible() || accept.isVisible();
}

void ProfileModal::refresh()
{
    const juce::String rel = profile ? profile->relation : juce::String ("none");
    const bool isFriend = rel == "friend";
    friendTag.setVisible (isFriend);
    remove.setVisible (isFriend);
    block.setVisible (profile && ! isFriend && rel != "blocked_by_me");
    unblock.setVisible (rel == "blocked_by_me");

    using S = Avatar::Status;
    const auto st = profile && isFriend ? profile->status.value_or ("offline") : juce::String();
    avatar.setStatus (st == "in_room" ? S::inRoom : (st == "online" ? S::online : (st == "offline" ? S::offline : S::none)));
    avatar.setPart (user.part.value_or ("other"));

    // 아래 버튼 (Q28): 요청 보냄 = 비활성, 요청 받음 = 거절·수락, 친구가 다른 공개 방에 있으면 참여·요청 (버그 27)
    const bool canRequest = profile && (rel == "none" || rel == "request_sent");
    request.setButtonText (rel == "request_sent" ? TXT ("요청 보냄") : TXT ("친구 요청"));
    request.setDimmed (rel == "request_sent" || (profile && rel == "none" && ! profile->canSendFriendRequest));
    request.setEnabled (rel == "none" && profile && profile->canSendFriendRequest);
    using J = core::HomeController::FriendJoin;
    const auto js = joinState();
    const bool canJoin = isFriend && js != J::none;
    joinRoom.setButtonText (js == J::request ? TXT ("요청") : js == J::requested ? TXT ("요청 보냄") : TXT ("참여"));
    // "요청 보냄" 은 눌러도 아무 일이 없으므로 화살표 커서 (모양은 그대로)
    joinRoom.setMouseCursor (js == J::requested ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
    const bool incoming = rel == "request_received" && incomingRequestId().isNotEmpty();

    if (incoming)       footer.setButtons ({ &decline, &accept });
    else if (canJoin)   footer.setButtons ({ &joinRoom });
    else if (canRequest) footer.setButtons ({ &request });
    else                footer.setButtons ({});
    for (auto* b : { &request, &joinRoom, &decline, &accept })
        b->setVisible ((b == &request && canRequest && ! incoming && ! canJoin) || (b == &joinRoom && canJoin && ! incoming) || ((b == &decline || b == &accept) && incoming));
    footer.setNote (note, col::inkSub);
    footer.setVisible (hasFooter() || note.isNotEmpty());

    const int rows = lastSeenText().isNotEmpty() ? 3 : 2;
    setContentHeight (18 + 48 + 18 + (rows * 17 + (rows - 1) * 9) + 18 + (footer.isVisible() ? ModalFooter::height : 0));
    body.resized();
    body.repaint();
}

void ProfileModal::Body::paint (juce::Graphics& g)
{
    auto& o = owner;
    const int x = metric::padX + 48 + 12;
    const auto nf = font (600, 16.0f);
    const auto nm = nameOf (o.user);
    g.setColour (col::ink);
    g.setFont (nf);
    g.drawText (nm, x, 18 + 6, juce::jmin ((int) std::ceil (textWidth (nf, nm)) + 2, getWidth() - x - 18), 19, juce::Justification::centredLeft, true);
    g.setColour (col::inkFaint);
    g.setFont (font (400, 12.0f));
    g.drawText ("@" + o.user.username, x, 18 + 6 + 19 + 3, getWidth() - x - 18, 15, juce::Justification::centredLeft, true);

    int y = 18 + 48 + 18;
    auto row = [&] (const juce::String& k, const juce::String& v)
    {
        g.setFont (font (400, 13.0f));
        g.setColour (col::inkSub);
        g.drawText (k, metric::padX, y, 100, 17, juce::Justification::centredLeft, false);
        g.setColour (col::ink);
        g.drawText (v, metric::padX + 80, y, getWidth() - 36 - 80, 17, juce::Justification::centredRight, true);
        y += 17 + 9;
    };
    row (TXT ("파트"), icons::partLabel (o.user.part.value_or ("other")));
    row (TXT ("상태"), o.statusText());
    if (o.lastSeenText().isNotEmpty()) row (TXT ("마지막 접속"), o.lastSeenText());
}

void ProfileModal::Body::resized()
{
    auto& o = owner;
    o.avatar.setBounds (metric::padX, 18, 48, 48);
    const auto nm = nameOf (o.user);
    int x = metric::padX + 48 + 12 + juce::jmin (140, (int) std::ceil (textWidth (font (600, 16.0f), nm)) + 2) + 6;
    if (o.friendTag.isVisible())
    {
        const int tw = o.friendTag.getIdealWidth();
        o.friendTag.setBounds (x, 18 + 7, tw, 17);
        x += tw + 6;
    }
    for (auto* b : { &o.remove, &o.block, &o.unblock })
        if (b->isVisible())
        {
            const int w = b->getIdealWidth();
            b->setBounds (x, 18 + 6, w, metric::smallH);
            x += w + 6;
        }
    o.footer.setBounds (getLocalBounds().removeFromBottom (ModalFooter::height));
}

//==============================================================================
RoomClosedOverlay::RoomClosedOverlay()
{
    setOpaque (true);
    addAndMakeVisible (home);
    home.onPress = [this] { if (onHome) onHome(); };
}

void RoomClosedOverlay::paint (juce::Graphics& g)
{
    g.fillAll (col::softBg);
    const int cy = getHeight() / 2;
    g.setColour (col::ink);
    g.setFont (font (600, 18.0f));
    g.drawText (TXT ("방을 닫았어요"), 0, cy - 54, getWidth(), 24, juce::Justification::centred, false);
    g.setColour (col::inkSub);
    g.setFont (font (400, 13.0f));
    g.drawText (TXT ("멤버 모두 홈으로 돌아갔어요"), 0, cy - 24, getWidth(), 20, juce::Justification::centred, false);
}

void RoomClosedOverlay::resized()
{
    const int w = home.getIdealWidth();
    home.setBounds ((getWidth() - w) / 2, getHeight() / 2 + 14, w, metric::buttonH);
}

} // namespace meon::ui
