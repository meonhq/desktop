#include "HomeOverlays.h"
#include "../theme/Hairline.h"
#include "../Messages.h"
#include "../theme/Icons.h"
#include "FormText.h"

namespace meon::ui
{

using namespace form;

//==============================================================================
void ModalFooter::setButtons (std::initializer_list<Button*> list)
{
    buttons.assign (list.begin(), list.end());
    for (auto* b : buttons) addAndMakeVisible (b);
    resized();
}

void ModalFooter::setNote (const juce::String& n, juce::Colour c)
{
    note = n;
    noteInk = c;
    repaint();
}

void ModalFooter::paint (juce::Graphics& g)
{
    g.setColour (col::borderSubtle);
    hairline::horizontal (g, *this, 0.0f, 0.0f, (float) getWidth());
    if (note.isNotEmpty())
    {
        int right = getWidth() - metric::padX;
        for (auto* b : buttons) right -= b->getWidth() + metric::gap;
        g.setColour (noteInk);
        g.setFont (font (400, 12.0f));
        g.drawFittedText (note, juce::Rectangle<int> (metric::padX, 1, juce::jmax (0, right - metric::padX), getHeight() - 1),
                          juce::Justification::centredLeft, 2, 1.0f);
    }
}

void ModalFooter::resized()
{
    int x = getWidth() - metric::padX;
    for (auto it = buttons.rbegin(); it != buttons.rend(); ++it)
    {
        auto* b = *it;
        const int w = b->getIdealWidth();
        x -= w;
        b->setBounds (x, 1 + metric::padY, w, metric::buttonH);
        x -= metric::gap;
    }
}

//==============================================================================
JoinCodeModal::JoinCodeModal (core::MeonCore& c) : Modal (TXT ("코드로 입장"), 308), core (c)
{
    setShowClose (false);
    body.addAndMakeVisible (code);
    body.addAndMakeVisible (footer);
    footer.setButtons ({ &cancel, &join });
    cancel.onPress = [this] { if (onClose) onClose(); };
    join.onPress = [this] { submit(); };
    code.onChange = [this] { error = false; refresh(); };
    code.onSubmit = [this] { submit(); };
    setContent (&body, 18 + 15 + 6 + 48 + 6 + 18 + 18 + ModalFooter::height);
    refresh();
}

void JoinCodeModal::Body::paint (juce::Graphics& g)
{
    drawLabel (g, TXT ("입장 코드"), { metric::padX, 18, getWidth() - 36, 15 });
    const auto note = owner.error ? TXT ("이 코드로 열린 방이 없어요") : TXT ("방장에게 받은 6자리 코드를 입력하세요");
    drawHint (g, note, { metric::padX, 18 + 15 + 6 + 48 + 6, getWidth() - 36, 18 }, owner.error ? col::danger : col::inkFaint);
}

void JoinCodeModal::Body::resized()
{
    owner.code.setBounds (metric::padX, 18 + 15 + 6, owner.code.getPreferredWidth(), owner.code.getPreferredHeight());
    owner.footer.setBounds (getLocalBounds().removeFromBottom (ModalFooter::height));
}

void JoinCodeModal::refresh()
{
    const bool ok = code.isComplete();
    join.setButtonText (busy ? TXT ("확인 중…") : TXT ("입장"));
    join.setDimmed (! ok);
    code.setError (error);
    footer.resized();
    body.repaint();
}

void JoinCodeModal::submit()
{
    if (! code.isComplete() || busy) return;
    if (const auto& room = core.store().state().currentRoom; beforeJoin && room)
    {
        // 지금 방의 코드면 그 방 (나가지 않고 합주 화면으로)
        const bool same = room->inviteCode && room->inviteCode->equalsIgnoreCase (code.getCode());
        std::weak_ptr<bool> weak = alive;
        beforeJoin (false, same ? room->id : juce::String(), [this, weak] { if (! weak.expired()) joinNow(); });
        return;
    }
    joinNow();
}

void JoinCodeModal::joinNow()
{
    if (! code.isComplete() || busy) return;
    busy = true;
    refresh();
    std::weak_ptr<bool> weak = alive;
    core.home().joinByCode (code.getCode(), [this, weak] (core::Result<core::JoinResult> r)
    {
        if (weak.expired()) return;
        busy = false;
        if (r.ok()) { refresh(); return; }   // 입장 → 화면이 바뀌며 모달도 닫힌다
        if (r.code() == core::ErrorCode::invalidCode || r.code() == core::ErrorCode::roomNotFound)
        {
            error = true;
            refresh();
            return;
        }
        if (onFailure) onFailure (r.err());
    });
}

//==============================================================================
LeaveConfirmModal::LeaveConfirmModal (core::MeonCore& c, bool create)
    : Modal (create ? TXT ("지금 방에서 나가고 새 방을 만들까요?") : TXT ("지금 방에서 나가고 들어갈까요?"), 340), core (c)
{
    setShowClose (false);
    if (const auto& room = core.store().state().currentRoom) roomName = room->title;
    ok.setButtonText (create ? TXT ("나가고 만들기") : TXT ("나가고 들어가기"));
    body.addAndMakeVisible (footer);
    footer.setButtons ({ &cancel, &ok });
    cancel.onPress = [this] { if (onClose) onClose(); };
    ok.onPress = [this] { confirm(); };
    setContent (&body, 18 + textHeight() + 18 + ModalFooter::height);
}

juce::AttributedString LeaveConfirmModal::text() const
{
    juce::AttributedString a;
    a.append (roomName, font (600, 13.0f), col::ink);
    a.append (TXT ("에서 나가요."), font (400, 13.0f), col::inkSub);
    a.setLineSpacing (13.0f * 0.6f);   // 13/1.5 (입장 실패 상자와 같은 값)
    a.setWordWrap (juce::AttributedString::byWord);
    return a;
}

int LeaveConfirmModal::textHeight() const
{
    juce::TextLayout t;
    t.createLayout (text(), (float) (340 - metric::padX * 2));
    return juce::jmax (20, (int) std::ceil (t.getHeight()));
}

void LeaveConfirmModal::Body::paint (juce::Graphics& g)
{
    g.setColour (col::borderSubtle);
    hairline::horizontal (g, *this, 0.0f, 0.0f, (float) getWidth());
    juce::TextLayout t;
    t.createLayout (owner.text(), (float) (getWidth() - metric::padX * 2));
    t.draw (g, juce::Rectangle<float> ((float) metric::padX, 18.0f, (float) (getWidth() - metric::padX * 2), (float) owner.textHeight()));
}

void LeaveConfirmModal::Body::resized()
{
    owner.footer.setBounds (getLocalBounds().removeFromBottom (ModalFooter::height));
}

void LeaveConfirmModal::confirm()
{
    if (busy) return;
    busy = true;
    ok.setDimmed (true);
    footer.setNote ({}, col::danger);
    std::weak_ptr<bool> weak = alive;
    // 나가면 막대가 사라지면서 이 모달도 지워진다 (AppRoot → HomeScreen::setRoomBar). 진행할 일은 미리 받아 둔다
    auto next = onLeft;
    core.home().leaveRoomThen ([this, weak, next] (core::Result<core::Done> r)
    {
        if (! r.ok())
        {
            if (weak.expired()) return;
            busy = false;
            ok.setDimmed (false);
            // 나가지 못했으면 진행하지 않고 오류를 보여 준다 (방은 그대로)
            footer.setNote (TXT ("방에서 나가지 못했어요 · ") + msg::general (r.err()), col::danger);
            return;
        }
        if (! weak.expired() && onClose) onClose();
        if (next) next();
    });
}

//==============================================================================
FilterModal::FilterModal (core::MeonCore& c, const core::RoomFilter& applied)
    : Modal (TXT ("방 필터"), 420), core (c), draft (applied)
{
    setShowClose (false);
    reset.setSize (reset.getIdealWidth(), metric::smallH);
    setHeaderAccessory (&reset);

    // 비어 있는 파트: "그 외"는 뺀다 (Q25)
    parts.setOptions ({ "guitar", "bass", "drums", "keys", "vocal" },
                      { TXT ("기타"), TXT ("베이스"), TXT ("드럼"), TXT ("키보드"), TXT ("보컬") });
    parts.setPartIcons (true);
    ping.setOptions ({ "0", "10", "20" }, { TXT ("전체"), TXT ("10ms 이하"), TXT ("20ms 이하") });
    for (auto* comp : std::initializer_list<juce::Component*> { &parts, &ping, &onlyRoom, &onlyFriends, &footer })
        body.addAndMakeVisible (comp);
    footer.setButtons ({ &cancel, &apply });

    parts.setSelectedSet (draft.emptyParts);
    ping.setSelected (juce::String (draft.maxLatencyMs));
    onlyRoom.setOn (draft.onlyWithRoom, false);
    onlyFriends.setOn (draft.onlyWithFriends, false);

    parts.onChange = ping.onChange = [this] { readControls(); };
    onlyRoom.onChange = onlyFriends.onChange = [this] (bool) { readControls(); };
    reset.onPress = [this]
    {
        if (! draft.isActive()) return;
        draft = {};
        parts.setSelectedSet ({});
        ping.setSelected ("0");
        onlyRoom.setOn (false);
        onlyFriends.setOn (false);
        refresh();
    };
    cancel.onPress = [this] { if (onClose) onClose(); };
    apply.onPress = [this]
    {
        const auto& s = core.store().state();
        const auto myId = s.me ? s.me->id : juce::String();
        if (draft.count (s.publicRooms, myId, s.config.latencyCoefficient, s.myRttMs) == 0) return;
        if (onApply) onApply (draft);
    };
    setContent (&body, 296 + ModalFooter::height);
    refresh();
}

void FilterModal::readControls()
{
    draft.emptyParts = parts.getSelectedSet();
    draft.maxLatencyMs = ping.getSelected().getIntValue();
    draft.onlyWithRoom = onlyRoom.isOn();
    draft.onlyWithFriends = onlyFriends.isOn();
    refresh();
}

void FilterModal::refresh()
{
    const auto& s = core.store().state();
    const auto myId = s.me ? s.me->id : juce::String();
    const int n = draft.count (s.publicRooms, myId, s.config.latencyCoefficient, s.myRttMs);
    apply.setButtonText (n > 0 ? TXT ("방 ") + juce::String (n) + TXT ("개 보기") : TXT ("맞는 방이 없어요"));
    apply.setDimmed (n == 0);
    reset.setDimmed (! draft.isActive());
    footer.resized();
}

void FilterModal::Body::paint (juce::Graphics& g)
{
    const int w = getWidth() - 36;
    int y = 18;
    drawLabel (g, TXT ("비어 있는 파트"), { metric::padX, y, w, 15 }, TXT ("· 여러 개 선택"));
    y += 15 + 6 + 32 + 6;
    drawHint (g, TXT ("고른 파트를 아직 아무도 맡지 않은 방만 보여요"), { metric::padX, y, w, 18 });
    y += 18 + 18;
    drawLabel (g, TXT ("지연 시간"), { metric::padX, y, w, 15 });
    y += 15 + 6 + 32 + 18;
    auto toggleText = [&] (const juce::String& t, const juce::String& sub)
    {
        g.setColour (col::ink);
        g.setFont (font (500, 14.0f));
        g.drawText (t, metric::padX, y, w - 44, 17, juce::Justification::centredLeft, false);
        g.setColour (col::inkSub);
        g.setFont (font (400, 12.0f));
        g.drawText (sub, metric::padX, y + 17 + 3, w - 44, 18, juce::Justification::centredLeft, false);
        y += 38 + 18;
    };
    toggleText (TXT ("빈자리 있는 방만"), TXT ("가득 찬 방은 숨겨요"));
    toggleText (TXT ("친구가 있는 방만"), TXT ("친구가 들어가 있는 방만 보여요"));
}

void FilterModal::Body::resized()
{
    const int w = getWidth() - 36;
    int y = 18 + 15 + 6;
    owner.parts.setBounds (metric::padX, y, w, 32);
    y += 32 + 6 + 18 + 18 + 15 + 6;
    owner.ping.setBounds (metric::padX, y, w, 32);
    y += 32 + 18;
    owner.onlyRoom.setBounds (getWidth() - metric::padX - 26, y + 11, 26, 16);
    y += 38 + 18;
    owner.onlyFriends.setBounds (getWidth() - metric::padX - 26, y + 11, 26, 16);
    owner.footer.setBounds (getLocalBounds().removeFromBottom (ModalFooter::height));
}

//==============================================================================
CreateRoomModal::CreateRoomModal (core::MeonCore& c) : Modal (TXT ("새 합주실"), 460), core (c)
{
    setShowClose (false);
    name.setMaxLength (30);
    desc.setMaxLength (80);
    visibility.setOptions ({ "public", "private" }, { TXT ("공개"), TXT ("비공개") });
    visibility.setSelected ("public");
    entry.setOptions ({ "instant", "approval" }, { TXT ("바로 입장"), TXT ("방장 승인") });
    entry.setSelected ("instant");
    capacity.setItems ({ "2", "3", "4", "5", "6", "7" },
                       { TXT ("2명"), TXT ("3명"), TXT ("4명"), TXT ("5명"), TXT ("6명"), TXT ("7명") });
    capacity.setValue ("7");
    for (auto* comp : std::initializer_list<juce::Component*> { &name, &desc, &visibility, &entry, &capacity, &footer })
        body.addAndMakeVisible (comp);
    footer.setButtons ({ &cancel, &create });

    name.onChange = desc.onChange = [this] { footer.setNote ({}, col::danger); refresh(); };
    name.onReturn = [this] { submit(); };
    visibility.onChange = [this] { refresh(); };
    entry.onChange = [this] { refresh(); };
    cancel.onPress = [this] { if (onClose) onClose(); };
    create.onPress = [this] { submit(); };
    setContent (&body, bodyHeight() + ModalFooter::height);
    refresh();
}

int CreateRoomModal::bodyHeight() const
{
    const int w = 460 - 36;
    const auto visNote = isPublic() ? TXT ("공개 방 목록에 보이고 누구나 들어올 수 있어요")
                                    : TXT ("목록에 보이지 않아요. 방을 만들면 나오는 입장 코드로만 들어올 수 있어요");
    int h = 18 + (15 + 6 + 32) + 18 + (15 + 6 + 55) + 18 + (15 + 6 + 32 + 6 + hintHeight (visNote, w)) + 18;
    if (isPublic()) h += (15 + 6 + 32 + 6 + 18) + 18;
    h += (15 + 6 + 32) + 18;
    return h;
}

void CreateRoomModal::refresh()
{
    const bool ok = name.getText().trim().isNotEmpty();
    create.setButtonText (busy ? TXT ("만드는 중…") : TXT ("방 만들기"));
    create.setDimmed (! ok);
    entry.setVisible (isPublic());
    setContentHeight (bodyHeight() + ModalFooter::height);
    body.resized();
    body.repaint();
    footer.resized();
}

void CreateRoomModal::Body::paint (juce::Graphics& g)
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
    const auto visNote = o.isPublic() ? TXT ("공개 방 목록에 보이고 누구나 들어올 수 있어요")
                                      : TXT ("목록에 보이지 않아요. 방을 만들면 나오는 입장 코드로만 들어올 수 있어요");
    const int vh = hintHeight (visNote, w);
    drawHint (g, visNote, { metric::padX, y, w, vh });
    y += vh + 18;
    if (o.isPublic())
    {
        drawLabel (g, TXT ("입장 방식"), { metric::padX, y, w, 15 });
        y += 15 + 6 + 32 + 6;
        drawHint (g, o.entry.getSelected() == "approval" ? TXT ("들어오려는 사람이 입장 요청을 보내고, 방장이 수락하면 들어와요")
                                                         : TXT ("누구든 입장 버튼을 누르면 바로 들어와요"),
                  { metric::padX, y, w, 18 });
        y += 18 + 18;
    }
    drawLabel (g, TXT ("최대 인원"), { metric::padX, y, w, 15 });
}

void CreateRoomModal::Body::resized()
{
    auto& o = owner;
    const int w = getWidth() - 36;
    int y = 18 + 15 + 6;
    o.name.setBounds (metric::padX, y, w, 32);
    y += 32 + 18 + 15 + 6;
    o.desc.setBounds (metric::padX, y, w, 55);
    y += 55 + 18 + 15 + 6;
    o.visibility.setBounds (metric::padX, y, w, 32);
    const auto visNote = o.isPublic() ? TXT ("공개 방 목록에 보이고 누구나 들어올 수 있어요")
                                      : TXT ("목록에 보이지 않아요. 방을 만들면 나오는 입장 코드로만 들어올 수 있어요");
    y += 32 + 6 + hintHeight (visNote, w) + 18;
    if (o.isPublic())
    {
        o.entry.setBounds (metric::padX, y + 15 + 6, w, 32);
        y += 15 + 6 + 32 + 6 + 18 + 18;
    }
    o.capacity.setBounds (metric::padX, y + 15 + 6, 120, 32);
    o.footer.setBounds (getLocalBounds().removeFromBottom (ModalFooter::height));
}

void CreateRoomModal::submit()
{
    if (busy || name.getText().trim().isEmpty()) return;
    core::CreateRoom c;
    c.type = isPublic() ? "public" : "private";
    if (isPublic()) c.entryMode = entry.getSelected();
    c.title = name.getText().trim();
    c.description = desc.getText().trim();
    c.capacity = capacity.getValue().getIntValue();
    busy = true;
    refresh();
    std::weak_ptr<bool> weak = alive;
    core.home().createRoom (c, [this, weak] (core::Result<core::RoomDetail> r)
    {
        if (weak.expired()) return;
        busy = false;
        if (! r.ok())
            footer.setNote (r.code() == core::ErrorCode::alreadyInRoom ? TXT ("이미 다른 방에 있어요") : msg::general (r.err()), col::danger);
        refresh();
    });
}

//==============================================================================
JoinFailDialog::JoinFailDialog() : close ((int) icons::Id::close24, IconButton::Style::plain24, 14.0f)
{
    addAndMakeVisible (close);
    addChildComponent (update);
    close.setTooltip (TXT ("닫기"));
    close.onClick = [this] { if (onClose) onClose(); };
    update.onPress = [this] { if (onUpdate) onUpdate(); };
}

void JoinFailDialog::show (core::HomeController::JoinFailure f)
{
    const auto t = msg::joinFailure (f);
    title = t.title;
    body = t.body;
    showUpdate = t.update;
    update.setVisible (showUpdate);
    setSize (380, getPreferredHeight());
    resized();
    repaint();
}

int JoinFailDialog::bodyHeight() const
{
    juce::AttributedString a;
    a.append (body, font (400, 13.0f), col::inkSub);
    a.setLineSpacing (13.0f * 0.6f);
    juce::TextLayout t;
    t.createLayout (a, 380.0f - 36.0f);
    return (int) std::ceil (t.getHeight());
}

int JoinFailDialog::getPreferredHeight() const
{
    return 48 + 1 + 12 + bodyHeight() + (showUpdate ? 12 + metric::smallH : 0) + 12;
}

void JoinFailDialog::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::white);
    g.fillRoundedRectangle (r, metric::radius);
    g.setColour (col::field);
    g.drawRoundedRectangle (r.reduced (0.5f), metric::radius, 1.0f);
    g.setColour (col::ink);
    g.setFont (font (600, 15.0f));
    g.drawText (title, metric::padX, 0, getWidth() - 36 - 30, 48, juce::Justification::centredLeft, true);
    g.setColour (col::borderSubtle);
    hairline::horizontal (g, *this, 1.0f, 48.0f, (float) getWidth() - 2.0f);
    juce::AttributedString a;
    a.append (body, font (400, 13.0f), col::inkSub);
    a.setLineSpacing (13.0f * 0.6f);
    juce::TextLayout t;
    t.createLayout (a, (float) getWidth() - 36.0f);
    t.draw (g, juce::Rectangle<float> ((float) metric::padX, 61.0f, (float) getWidth() - 36.0f, (float) bodyHeight()));
}

void JoinFailDialog::resized()
{
    close.setBounds (getWidth() - metric::padX - 24, 12, 24, 24);
    const int w = update.getIdealWidth();
    update.setBounds (getWidth() - metric::padX - w, getHeight() - 12 - metric::smallH, w, metric::smallH);
}

//==============================================================================
struct NotificationList::Row : juce::Component
{
    /** 받은 초대 (디자인: "OO님이 초대했어요" · 방 이름, 거절·입장). 줄을 눌러도 아무 일 없다 */
    Row (NotificationList& o, const core::Invite& inv) : owner (o), invite (true)
    {
        inviteId = inv.id;
        title = inv.from.nickname.value_or (inv.from.username) + TXT ("님이 초대했어요");
        sub = inv.room.title;
        avatar.setPart (inv.from.part.value_or ("other"));
        avatar.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (avatar);
        addAndMakeVisible (decline);
        addAndMakeVisible (accept);
        // 누른 줄이 목록에서 지워지므로 다음 메시지에서 (id 는 먼저 복사)
        juce::Component::SafePointer<NotificationList> safe (&owner);
        const auto id = inviteId;
        decline.onPress = [safe, id] { juce::MessageManager::callAsync ([safe, id] { if (safe != nullptr) safe->core.home().dismissInvite (id); }); };
        accept.onPress = [safe, id]
        {
            juce::MessageManager::callAsync ([safe, id]
            {
                if (safe == nullptr) return;
                if (safe->onAcceptInvite) safe->onAcceptInvite (id);
                else safe->core.home().acceptInvite (id, [] (core::Result<core::JoinResult>) {});
            });
        };
    }

    Row (NotificationList& o, const core::FriendRequest& r) : owner (o), request (r)
    {
        title = r.from.nickname.value_or (r.from.username);
        sub = TXT ("친구 요청 · ") + icons::partLabel (r.from.part.value_or ("other"));
        avatar.setPart (r.from.part.value_or ("other"));
        avatar.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (avatar);
        addAndMakeVisible (decline);
        addAndMakeVisible (accept);
        // id 를 먼저 복사한다: 수락·거절하면 목록이 다시 만들어지며 이 줄이 지워진다 (예전에는 지워진 줄의 id 를 보냈다)
        decline.onPress = [this] { const auto id = request.id; owner.core.home().declineFriendRequest (id); };
        accept.onPress = [this] { const auto id = request.id; owner.core.home().acceptFriendRequest (id); };
        setMouseCursor (juce::MouseCursor::PointingHandCursor);   // 누르면 프로필
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().reduced (9, 0);
        if (isMouseOver (true))
        {
            g.setColour (col::rowHover);
            g.fillRoundedRectangle (r.toFloat(), metric::radius);
        }
        const int x = r.getX() + 9 + 32 + 9;
        const int right = decline.getX() - 9;
        g.setColour (col::ink);
        g.setFont (font (600, 13.0f));
        g.drawText (title, x, 6, right - x, 17, juce::Justification::centredLeft, true);
        g.setColour (col::inkSub);
        g.setFont (font (400, 12.0f));
        g.drawText (sub, x, 6 + 17 + 3, right - x, 16, juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (9, 0).reduced (9, 0);
        avatar.setBounds (r.removeFromLeft (32).withSizeKeepingCentre (32, 32));
        const int aw = accept.getIdealWidth(), dw = decline.getIdealWidth();
        accept.setBounds (r.removeFromRight (aw).withSizeKeepingCentre (aw, metric::smallH));
        r.removeFromRight (9);
        decline.setBounds (r.removeFromRight (dw).withSizeKeepingCentre (dw, metric::smallH));
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override { if (! invite && e.mouseWasClicked() && owner.onOpenProfile) owner.onOpenProfile (request.from); }

    NotificationList& owner;
    const bool invite = false;
    core::FriendRequest request;
    juce::String inviteId, title, sub;
    Avatar avatar { 32.0f, 16.0f };
    Button decline { Button::Kind::small, TXT ("거절") }, accept { Button::Kind::smallPrimary, invite ? TXT ("입장") : TXT ("수락") };   // 디자인: 초대는 입장
};

NotificationList::NotificationList (core::MeonCore& c) : core (c)
{
    core.store().addListener (this);
    rebuild();
}

NotificationList::~NotificationList()
{
    core.store().removeListener (this);
}

void NotificationList::stateChanged (core::StateKey k)
{
    if (k == core::StateKey::friendRequests || k == core::StateKey::invites) rebuild();
}

void NotificationList::rebuild()
{
    rows.clear();
    // 디자인 순서: 받은 친구 요청, 그다음 받은 초대
    for (auto& r : core.store().state().friendRequests)
        addAndMakeVisible (rows.add (new Row (*this, r)));
    for (auto& inv : core.store().state().invites)
        addAndMakeVisible (rows.add (new Row (*this, inv)));
    resized();
    repaint();
    if (onSizeChanged) onSizeChanged();
}

int NotificationList::getPreferredHeight() const
{
    return 6 + (rows.isEmpty() ? 41 : rows.size() * 44) + 6;
}

void NotificationList::paint (juce::Graphics& g)
{
    if (! rows.isEmpty()) return;
    g.setColour (col::inkFaint);
    g.setFont (font (400, 13.0f));
    g.drawText (TXT ("새 알림이 없어요"), metric::padX, 6, getWidth() - 36, 41, juce::Justification::centredLeft, true);
}

void NotificationList::resized()
{
    int y = 6;
    for (auto* r : rows)
    {
        r->setBounds (0, y, getWidth(), 44);
        y += 44;
    }
}

//==============================================================================
Toast::Toast (const juce::String& t, const juce::String& s, const juce::String& a)
    : title (t), sub (s), close ((int) icons::Id::close12, IconButton::Style::bare19, 10.0f)
{
    if (a.isNotEmpty())
    {
        action = std::make_unique<Button> (Button::Kind::smallPrimary, a);
        action->onPress = [this] { if (onAction) onAction(); };
        addAndMakeVisible (*action);
    }
    addAndMakeVisible (close);
    close.setTooltip (TXT ("닫기"));
    close.onClick = [this] { if (onClose) onClose(); };
    setPanelBounds (0, 0, width, height);
}

namespace
{
    constexpr int toastPadX = 18, toastTitleY = 12, toastTitleH = 17, toastSubGap = 3, toastSubLineH = 16;
    juce::Font toastTitleFont() { return font (600, 13.0f); }
    juce::Font toastSubFont()   { return font (400, 12.0f); }
}

void Toast::setSecondary (const juce::String& label)
{
    secondary = std::make_unique<Button> (Button::Kind::small, label);
    secondary->onPress = [this] { if (onSecondary) onSecondary(); };
    addAndMakeVisible (*secondary);
    resized();
}

int Toast::textRight (int w) const
{
    // 판 기준 x (버튼 자리는 구성 요소 기준이라 판 왼쪽만큼 뺀다)
    const int px = panel().getX();
    if (secondary != nullptr) return secondary->getX() - px - 9;
    if (action != nullptr) return action->getX() - px - 9;
    return (close.isVisible() ? w - 12 - 19 : w - 12) - 9;
}

float Toast::subHeight (int w) const
{
    if (kind == Kind::normal) return (float) toastSubLineH;
    return paragraphHeightWithEmoji (sub, toastSubFont(), (float) (textRight (w) - toastPadX), (float) toastSubLineH);
}

int Toast::heightForWidth (int w) const
{
    // 한 줄 토스트 ("다시 연결됐어요", 초대 수락 실패 등): 글자 높이에 위아래 여백만
    if (sub.isEmpty() && action == nullptr) return toastTitleY + toastTitleH + toastTitleY;
    if (kind == Kind::normal || sub.isEmpty()) return height;
    return juce::jmax (height, toastTitleY + toastTitleH + toastSubGap + (int) std::ceil (subHeight (w)) + toastTitleY);
}

void Toast::paint (juce::Graphics& g)
{
    // 면 색은 종류대로 (경고는 연한 핑크), 테두리·그림자는 팝오버와 같은 표면
    paintSurface (g, kind == Kind::warning ? col::accentSoft : popoverStyle::background);
    const auto p = panel();
    g.setOrigin (p.getPosition());   // 아래는 판 기준 좌표
    const int right = textRight (p.getWidth());
    g.setColour (col::ink);
    g.setFont (toastTitleFont());
    if (sub.isEmpty())   // 보조 줄이 없으면 한 줄로 가운데
    {
        g.drawText (title, toastPadX, 0, right - toastPadX, p.getHeight(), juce::Justification::centredLeft, true);
        return;
    }
    g.drawText (title, toastPadX, toastTitleY, right - toastPadX, toastTitleH, juce::Justification::centredLeft, true);
    const int subY = toastTitleY + toastTitleH + toastSubGap;
    if (kind == Kind::warning)   // 설명은 자르지 않고 줄바꿈
    {
        drawParagraphWithEmoji (g, sub, toastSubFont(), col::inkSub,
                                juce::Rectangle<float> ((float) toastPadX, (float) subY, (float) (right - toastPadX), subHeight (p.getWidth())),
                                (float) toastSubLineH, juce::Justification::topLeft);
        return;
    }
    g.setColour (col::inkSub);
    g.setFont (toastSubFont());
    g.drawText (sub, toastPadX, subY, right - toastPadX, toastSubLineH, juce::Justification::centredLeft, true);
}

void Toast::resized()
{
    const auto p = panel();
    auto r = p.reduced (12, 0);
    // 경고는 설명이 여러 줄일 수 있어서 닫기 버튼을 제목 줄에 맞춘다
    auto closeArea = r.removeFromRight (19);
    close.setBounds (kind == Kind::warning && sub.isNotEmpty()
                         ? juce::Rectangle<int> (closeArea.getX(), p.getY() + toastTitleY + (toastTitleH - 19) / 2, 19, 19)
                         : closeArea.withSizeKeepingCentre (19, 19));
    // 닫기 없이 두 버튼 (입장 요청, 디자인: 오른쪽 여백 12, 사이 9)
    const bool twoButtons = ! close.isVisible() && secondary != nullptr;
    if (twoButtons) r = p.withTrimmedLeft (toastPadX).withTrimmedRight (12);
    if (action != nullptr)
    {
        if (! twoButtons) r.removeFromRight (9);
        const int w = action->getIdealWidth();
        action->setBounds (r.removeFromRight (w).withSizeKeepingCentre (w, metric::smallH));
    }
    if (secondary != nullptr)
    {
        r.removeFromRight (twoButtons ? 9 : 6);
        const int w = secondary->getIdealWidth();
        secondary->setBounds (r.removeFromRight (w).withSizeKeepingCentre (w, metric::smallH));
    }
}

} // namespace meon::ui
