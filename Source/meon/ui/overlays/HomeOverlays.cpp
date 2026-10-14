#include "HomeOverlays.h"
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
    g.setColour (col::line);
    g.fillRect (0, 0, getWidth(), 1);
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
FilterModal::FilterModal (core::MeonCore& c, const core::RoomFilter& applied)
    : Modal (TXT ("방 필터"), 420), core (c), draft (applied)
{
    setShowClose (false);
    reset.setSize (reset.getIdealWidth(), metric::smallH);
    setHeaderAccessory (&reset);

    // 비어 있는 파트: "그 외"는 뺀다 (Q25)
    parts.setOptions ({ "guitar", "bass", "drums", "keys", "vocal" },
                      { TXT ("기타"), TXT ("베이스"), TXT ("드럼"), TXT ("키보드"), TXT ("보컬") });
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
    const auto visNote = isPublic() ? TXT ("홈의 공개 방 목록에 보이고 누구나 들어올 수 있어요")
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
    const auto visNote = o.isPublic() ? TXT ("홈의 공개 방 목록에 보이고 누구나 들어올 수 있어요")
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
    const auto visNote = o.isPublic() ? TXT ("홈의 공개 방 목록에 보이고 누구나 들어올 수 있어요")
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
    g.setColour (col::line);
    g.fillRect (1, 48, getWidth() - 2, 1);
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
    Row (NotificationList& o, const core::FriendRequest& r) : owner (o), request (r)
    {
        avatar.setPart (r.from.part.value_or ("other"));
        avatar.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (avatar);
        addAndMakeVisible (decline);
        addAndMakeVisible (accept);
        decline.onPress = [this] { owner.core.home().declineFriendRequest (request.id); };
        accept.onPress = [this] { owner.core.home().acceptFriendRequest (request.id); };
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
        g.drawText (request.from.nickname.value_or (request.from.username), x, 6, right - x, 17, juce::Justification::centredLeft, true);
        g.setColour (col::inkSub);
        g.setFont (font (400, 12.0f));
        g.drawText (TXT ("친구 요청 · ") + icons::partLabel (request.from.part.value_or ("other")), x, 6 + 17 + 3, right - x, 16,
                    juce::Justification::centredLeft, true);
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
    void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked() && owner.onOpenProfile) owner.onOpenProfile (request.from); }

    NotificationList& owner;
    core::FriendRequest request;
    Avatar avatar { 32.0f, 16.0f };
    Button decline { Button::Kind::small, TXT ("거절") }, accept { Button::Kind::smallPrimary, TXT ("수락") };
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
    if (k == core::StateKey::friendRequests) rebuild();
}

void NotificationList::rebuild()
{
    rows.clear();
    for (auto& r : core.store().state().friendRequests)
        addAndMakeVisible (rows.add (new Row (*this, r)));
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
    setSize (width, height);
}

void Toast::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::white);
    g.fillRoundedRectangle (r, metric::radius);
    g.setColour (col::field);
    g.drawRoundedRectangle (r.reduced (0.5f), metric::radius, 1.0f);
    const int right = (action != nullptr ? action->getX() : close.getX()) - 9;
    g.setColour (col::ink);
    g.setFont (font (600, 13.0f));
    g.drawText (title, 18, 12, right - 18, 17, juce::Justification::centredLeft, true);
    g.setColour (col::inkSub);
    g.setFont (font (400, 12.0f));
    g.drawText (sub, 18, 12 + 17 + 3, right - 18, 16, juce::Justification::centredLeft, true);
}

void Toast::resized()
{
    auto r = getLocalBounds().reduced (12, 0);
    close.setBounds (r.removeFromRight (19).withSizeKeepingCentre (19, 19));
    if (action != nullptr)
    {
        r.removeFromRight (9);
        const int w = action->getIdealWidth();
        action->setBounds (r.removeFromRight (w).withSizeKeepingCentre (w, metric::smallH));
    }
}

} // namespace meon::ui
