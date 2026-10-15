#include "HomeScreen.h"
#include "../Messages.h"
#include "../theme/Icons.h"
#include "../../core/api/ApiJson.h"
#include "../../shell/ShellHooks.h"

namespace meon::ui
{

namespace
{
    constexpr int headerLineY = metric::headerH;        // 헤더 아래 구분선
    constexpr int roomsHeaderH = 56;                     // 12 + 32 + 12
    constexpr int audioCardH = 48 + 1 + 43;
    constexpr int addRowH = 44;

    int descLines (const juce::String& text, int width)
    {
        if (text.isEmpty() || width <= 0) return 1;
        juce::AttributedString a;
        a.append (text, font (400, 12.0f), col::inkSub);
        juce::TextLayout t;
        t.createLayout (a, (float) width);
        return juce::jlimit (1, 2, t.getNumLines());
    }

    juce::String nameOf (const core::UserPublic& u) { return u.nickname.value_or (u.username); }
}

//==============================================================================
struct HomeScreen::Separator : juce::Component
{
    Separator() { setInterceptsMouseClicks (false, false); }
    void paint (juce::Graphics& g) override { g.fillAll (col::line); }
};

/** 필터 버튼 24×24 (아이콘 14px). 필터가 켜져 있으면 진한 색 + 노란 점 */
struct HomeScreen::FilterButton : juce::Button
{
    FilterButton() : juce::Button ("filter") { setMouseCursor (juce::MouseCursor::PointingHandCursor); setTooltip (TXT ("필터")); }
    bool active = false;
    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto r = getLocalBounds().toFloat();
        if (over)
        {
            g.setColour (col::hoverBg);
            g.fillRoundedRectangle (r, metric::smallRadius);
        }
        icons::draw (g, icons::Id::filter, r.withSizeKeepingCentre (14.0f, 14.0f), (over || active) ? col::ink : col::inkFaint);
        if (active)
        {
            g.setColour (col::accent);
            g.fillEllipse (r.getRight() - 3.0f - 6.0f, 3.0f, 6.0f, 6.0f);
        }
    }
};

//==============================================================================
/** 공개 방 카드 (디자인 cardH 식의 높이, 여백 12·18, 간격 9) */
struct HomeScreen::RoomCard : juce::Component
{
    RoomCard (HomeScreen& o, bool animateIn) : owner (o),
        vblank (this, [this]
        {
            if (! fade.tick (juce::Time::getMillisecondCounterHiRes())) return;
            setAlpha (fade.get() * (full ? 0.45f : 1.0f));
        })
    {
        // 새로 생긴 방은 0.3초에 걸쳐 나타난다 (디자인 hCardIn)
        if (animateIn) { fade.jumpTo (0.0f); fade.setTarget (1.0f, 300.0); }
        addAndMakeVisible (ping);
        addAndMakeVisible (join);
        ping.setInterceptsMouseClicks (false, false);
        join.onPress = [this] { owner.enterRoom (room); };
    }

    void setRoom (const core::Room& r)
    {
        room = r;
        const auto& s = owner.core.store().state();
        const auto myId = s.me ? s.me->id : juce::String();
        full = ! core::rooms::hasRoomFor (room, myId);
        ping.setPing (core::latency::estimate (s.config.latencyCoefficient, s.myRttMs, room.hostRttMs));
        juce::String label;
        if (full)                                     label = TXT ("가득 참");
        else if (room.isApproval())                   label = owner.core.home().isRequestPending (room) ? TXT ("요청 보냄") : TXT ("요청 보내기");
        else                                          label = TXT ("바로 입장");
        join.setButtonText (label);
        join.setEnabled (! full);
        // "요청 보냄" 은 눌러도 아무 일이 없으므로 화살표 커서 (모양은 그대로)
        join.setMouseCursor (label == TXT ("요청 보냄") ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
        setAlpha (fade.get() * (full ? 0.45f : 1.0f));
        resized();
        repaint();
    }

    int lines = 2;
    int preferredHeight() const { return Masonry::cardHeight (juce::jmax (1, (int) room.members.size()), lines); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (col::white);
        g.fillRoundedRectangle (r, metric::radius);
        g.setColour (col::line);
        g.drawRoundedRectangle (r.reduced (0.5f), metric::radius, 1.0f);

        const int x = 18, w = getWidth() - 36;
        int y = 13;
        g.setColour (col::ink);
        g.setFont (font (600, 14.0f));
        g.drawText (room.title, x, y, w - 48 - 9, 19, juce::Justification::centredLeft, true);
        y += 19 + 9;

        g.setColour (col::inkSub);
        g.setFont (font (400, 12.0f));
        g.drawFittedText (room.description, x, y, w, 17 * lines, juce::Justification::topLeft, lines, 1.0f);
        y += 17 * lines + 9;

        for (auto& m : room.members)
        {
            const auto nameF = font (400, 13.0f);
            const auto name = nameOf (m.user);
            const auto part = icons::partLabel (m.user.part.value_or ("other"));
            const auto tag = m.isHost ? TXT ("방장") : (m.isFriend ? TXT ("친구") : juce::String());
            // 파트: 아이콘(얇은 버전 16px, 파트 글자와 같은 색) + 이름. 디자인은 글자만 있다 (REMAKE_PLAN 3.6)
            // 14px 에서는 1px 선이 흐려 기타·베이스가 잘 구별되지 않아 16px
            constexpr float iconPx = 16.0f, iconGap = 3.0f;
            const float partW = iconPx + iconGap + textWidth (nameF, part) + 1.0f;
            const auto tagF = font (500, 11.0f);
            const float tagW = tag.isEmpty() ? 0.0f : textWidth (tagF, tag) + 8.0f + 2.0f + 3.0f;
            const float nameMax = (float) w - 5.0f - 2.0f - 5.0f - partW - tagW - (tag.isEmpty() ? 0.0f : 5.0f);
            const float nameW = juce::jmin (textWidth (nameF, name) + 1.0f, juce::jmax (10.0f, nameMax));
            float cx = (float) x;
            g.setFont (nameF);
            g.setColour (col::ink);
            g.drawText (name, juce::Rectangle<float> (cx, (float) y, nameW, 17.0f), juce::Justification::centredLeft, true);
            cx += nameW + 5.0f;
            g.setColour (col::inkFaint);
            g.fillEllipse (cx, (float) y + 7.5f, 2.0f, 2.0f);
            cx += 2.0f + 5.0f;
            icons::drawPart (g, m.user.part.value_or ("other"), { cx, (float) y, iconPx, 17.0f }, iconPx, col::inkSub, icons::PartWeight::thin);
            g.setColour (col::inkSub);
            g.drawText (part, juce::Rectangle<float> (cx + iconPx + iconGap, (float) y, partW - iconPx - iconGap, 17.0f),
                        juce::Justification::centredLeft, false);
            cx += partW + 5.0f;
            if (tag.isNotEmpty())
            {
                cx += 3.0f;
                const juce::Rectangle<float> tr (cx, (float) y + 1.5f, textWidth (tagF, tag) + 8.0f + 2.0f, 15.0f);
                g.setColour (col::line);
                g.drawRoundedRectangle (tr.reduced (0.5f), 3.0f, 1.0f);
                g.setColour (col::inkSub);
                g.setFont (tagF);
                g.drawText (tag, tr, juce::Justification::centred, false);
            }
            y += 20;
        }

        g.setColour (col::inkSub);
        g.setFont (font (400, 12.0f));
        g.drawText (juce::String (room.memberCount) + " / " + juce::String (room.capacity),
                    x, getHeight() - 13 - 19, 80, 19, juce::Justification::centredLeft, false);
    }

    void resized() override
    {
        ping.setBounds (getWidth() - 18 - 48, 13, 48, 19);
        const int jw = join.getIdealWidth();
        join.setBounds (getWidth() - 18 - jw, getHeight() - 13 - 19, jw, 19);
    }

    // 카드 자체는 누를 수 없다. 입장·참가 요청은 join 버튼으로만 (호버 표시도 없음)

    HomeScreen& owner;
    core::Room room;
    bool full = false;
    AnimatedValue fade { 1.0f };
    juce::VBlankAttachment vblank;
    PingBadge ping;
    Button join { Button::Kind::small };
};

/** 메이슨리 목록 (뷰포트 안). 비면 빈 상태 문구 */
struct HomeScreen::RoomList : juce::Component
{
    explicit RoomList (HomeScreen& o) : owner (o) {}

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::softBg);
        if (! cards.isEmpty()) return;
        const bool filtered = owner.filter.isActive() && ! owner.core.store().state().publicRooms.empty();
        const auto title = filtered ? TXT ("필터에 맞는 방이 없어요") : TXT ("지금 열린 공개 방이 없어요");
        const auto sub = filtered ? TXT ("필터를 바꾸거나 초기화해 보세요") : TXT ("직접 방을 열면 다른 사람들이 찾아 들어올 수 있어요");
        auto r = getLocalBounds().reduced (18);
        const int cy = r.getCentreY();
        g.setColour (col::ink);
        g.setFont (font (600, 15.0f));
        g.drawText (title, r.withY (cy - 9 - 19).withHeight (19), juce::Justification::centred, true);
        g.setColour (col::inkSub);
        g.setFont (font (400, 13.0f));
        g.drawText (sub, r.withY (cy).withHeight (17), juce::Justification::centred, true);
    }

    HomeScreen& owner;
    juce::OwnedArray<RoomCard> cards;
};

//==============================================================================
/** 친구 목록 (뷰포트 안). 행: 아바타 32 + 상태 점, 이름 600 13, 상태 400 12, 참여 */
struct HomeScreen::FriendList : juce::Component
{
    struct Row : juce::Component, juce::SettableTooltipClient   // 상태가 말줄임으로 잘리면 툴팁으로 전체 문구
    {
        Row (HomeScreen& o, const core::Friend& f) : owner (o), fr (f)
        {
            using S = Avatar::Status;
            const auto st = fr.status == "in_room" ? S::inRoom : (fr.status == "online" ? S::online : S::offline);
            avatar.setPart (fr.user.part.value_or ("other"));
            avatar.setStatus (st);
            avatar.setDimmed (st == S::offline);
            avatar.setInterceptsMouseClicks (false, false);
            addAndMakeVisible (avatar);
            setMouseCursor (juce::MouseCursor::PointingHandCursor);   // 누르면 프로필
            addChildComponent (join);
            join.onPress = [this]
            {
                juce::Component::SafePointer<HomeScreen> safe (&owner);
                juce::Component::SafePointer<Row> self (this);
                owner.core.home().joinFriend (fr, [safe, self] (core::Result<core::JoinResult> r)
                {
                    if (self != nullptr) self->refreshJoin();
                    if (safe != nullptr && ! r.ok() && safe->onJoinFailure) safe->onJoinFailure (r.err());
                });
            };
            refreshJoin();
        }

        /** 참여 버튼 (버그 25): 바로 입장 방 "참여", 승인 방 "요청" → 보낸 뒤 5분 "요청 보냄" (방 목록 카드와 같은 상태) */
        void refreshJoin()
        {
            using J = core::HomeController::FriendJoin;
            const auto st = owner.core.home().friendJoinState (fr);
            const auto label = st == J::request ? TXT ("요청") : st == J::requested ? TXT ("요청 보냄") : TXT ("참여");
            const bool show = st != J::none;
            // "요청 보냄" 은 방 목록 카드와 같은 모양으로 두고 누르면 아무것도 하지 않는다 (joinFriend 가 막는다)
            if (show == join.isVisible() && label == join.getButtonText()) return;
            join.setButtonText (label);
            join.setMouseCursor (st == J::requested ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
            join.setVisible (show);
            resized();
            repaint();
        }

        juce::String statusText() const
        {
            if (fr.status == "in_room")
                return fr.room ? fr.room->title + TXT ("에서 합주 중") : TXT ("합주 중");   // 비공개 방이면 위치를 숨긴다 (API 2.10)
            if (fr.status == "online") return TXT ("온라인");
            return msg::lastSeen (fr.lastSeenAt ? core::json::parseIsoTime (*fr.lastSeenAt) : 0, owner.core.scheduler().nowMs());
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
            const int right = (join.isVisible() ? join.getX() : r.getRight() - 9) - 9;
            g.setColour (fr.status == "offline" ? col::inkFaint : col::ink);
            g.setFont (font (600, 13.0f));
            g.drawText (nameOf (fr.user), x, 6, right - x, 16, juce::Justification::centredLeft, true);
            g.setColour (col::inkFaint);
            g.setFont (font (400, 12.0f));
            const auto st = statusText();
            g.drawText (st, x, 6 + 16 + 3, right - x, 15, juce::Justification::centredLeft, true);
            // 말줄임으로 잘렸을 때만 마우스를 올리면 전체 문구 (방 이름이 길거나 폭이 좁을 때)
            const auto tip = textWidth (font (400, 12.0f), st) > (float) (right - x) ? st : juce::String();
            if (getTooltip() != tip) setTooltip (tip);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (18, 0);
            avatar.setBounds (r.removeFromLeft (32).withSizeKeepingCentre (32, 32));
            const int jw = join.getIdealWidth();
            join.setBounds (r.removeFromRight (jw).withSizeKeepingCentre (jw, metric::smallH));
        }

        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { repaint(); }
        void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked()) owner.openProfile (fr.user); }

        HomeScreen& owner;
        core::Friend fr;
        Avatar avatar { 32.0f, 16.0f };
        Button join { Button::Kind::small, TXT ("참여") };
    };

    explicit FriendList (HomeScreen& o) : owner (o) {}

    void paint (juce::Graphics& g) override
    {
        if (! rows.isEmpty()) return;
        auto r = getLocalBounds().reduced (18);
        const int cy = r.getCentreY();
        g.setColour (col::inkSub);
        g.setFont (font (500, 13.0f));
        g.drawText (TXT ("아직 친구가 없어요"), r.withY (cy - 3 - 16).withHeight (16), juce::Justification::centred, true);
        g.setColour (col::inkFaint);
        g.setFont (font (400, 12.0f));
        g.drawFittedText (TXT ("합주실에서 만난 사람에게 친구 요청을 보내보세요"), r.withY (cy).withHeight (36),
                          juce::Justification::centredTop, 2, 1.0f);
    }

    void resized() override
    {
        int y = 6;
        for (auto* r : rows) { r->setBounds (0, y, getWidth(), 44); y += 44; }
    }

    int preferredHeight() const { return rows.isEmpty() ? 0 : 6 + rows.size() * 44 + 6; }

    HomeScreen& owner;
    juce::OwnedArray<Row> rows;
};

//==============================================================================
HomeScreen::HomeScreen (core::MeonCore& c, UpdateHooks u)
    : core (c), update (std::move (u)), plugin (c.isPlugin())
{
    setOpaque (true);
    setWantsKeyboardFocus (true);

    addAndMakeVisible (bell);
    bell.setTooltip (TXT ("알림"));
    bell.onClick = [this] { toggleNotifications(); };
    addAndMakeVisible (settings);
    settings.onPress = [this] { if (onOpenSettings) onOpenSettings(); };

    addChildComponent (later);
    addChildComponent (updateNow);
    later.onPress = [this] { updateDismissed = true; resized(); repaint(); };
    updateNow.onPress = [this] { if (update.start) update.start(); };

    // 공개 방
    addAndMakeVisible (roomsCard);
    filterButton = std::make_unique<FilterButton>();
    roomsCard.addAndMakeVisible (*filterButton);
    filterButton->onClick = [this] { openFilter(); };
    roomsCard.addAndMakeVisible (joinCode);
    roomsCard.addAndMakeVisible (createRoom);
    joinCode.onPress = [this] { openJoinCode(); };
    createRoom.onPress = [this] { openCreateRoom(); };
    roomList = std::make_unique<RoomList> (*this);
    roomViewport.setViewedComponent (roomList.get(), false);
    roomViewport.setScrollBarsShown (true, false);
    roomViewport.setScrollBarThickness (8);
    roomsCard.addAndMakeVisible (roomViewport);
    roomsCard.addAndMakeVisible (roomsTitle);
    roomsLine = std::make_unique<Separator>();
    roomsCard.addAndMakeVisible (*roomsLine);

    // 내 오디오
    addAndMakeVisible (audioCard);
    audioCard.setTitle (TXT ("내 오디오"));
    monitorTag.setSize (monitorTag.getIdealWidth(), 19);
    audioCard.setHeaderAccessory (&monitorTag);
    monitorTag.setVisible (false);
    audioCard.addAndMakeVisible (deviceName);
    audioCard.addAndMakeVisible (sendMute);
    audioCard.addAndMakeVisible (sendGain);
    inputMeter = std::make_unique<MeterBox> (TXT ("입력 미터"));
    audioCard.addAndMakeVisible (*inputMeter);
    sendMute.setTooltip (TXT ("다른 사람에게 보내는 내 소리"));
    sendGain.setTitle (TXT ("송출 레벨 · 더블클릭하면 0 dB"));
    sendMute.onPress = [this]
    {
        if (auto* jam = core.jam()) jam->setSendMuted (! jam->isSendMuted());
        refreshAudio();
    };
    sendGain.onChange = [this] (float db) { if (auto* jam = core.jam()) jam->setSendGainDb (db); };
    if (auto* jam = core.jam())
    {
        const int chans = plugin ? 2 : juce::jlimit (1, 2, jam->getPrefs().inputChannelCount);
        inputMeter->meter.setSource (chans, [this] (int ch) { auto* j = core.jam(); return j != nullptr ? j->inputLevel (ch) : 0.0f; });
    }

    // 친구
    addAndMakeVisible (friendsCard);
    friendsCard.setTitle (TXT ("친구"), true);
    friendsCard.onToggle = [this] (bool open) { friendsOpen = open; resized(); };
    friendList = std::make_unique<FriendList> (*this);
    friendViewport.setViewedComponent (friendList.get(), false);
    friendViewport.setScrollBarsShown (true, false);
    friendViewport.setScrollBarThickness (8);
    friendsCard.addAndMakeVisible (friendViewport);

    // 친구 추가 (테두리 없는 입력)
    addAndMakeVisible (addCard);
    addCard.setTitle (TXT ("친구 추가"));
    addCard.addAndMakeVisible (addInput);
    addCard.addAndMakeVisible (addSend);
    addCard.addChildComponent (addNote);
    addNote.setLineHeight (15.0f);   // 긴 오류 문구는 줄을 바꾼다
    addInput.setFontPx (14.0f);
    addInput.setBorderless (true);
    addInput.setMaxLength (16);
    addInput.transform = [] (const juce::String& s) { return s.toLowerCase(); };
    addInput.onReturn = [this] { sendFriendRequest(); };
    addInput.onChange = [this] { if (addMessage.isNotEmpty()) { addMessage = {}; resized(); repaint(); } };
    addSend.onPress = [this] { sendFriendRequest(); };

    core.store().addListener (this);
    core.connection().addListener (this);
    rebuildRooms();
    rebuildFriends();
    refreshHeader();
    refreshAudio();
    startTimer (500);
}

HomeScreen::~HomeScreen()
{
    *alive = false;
    core.store().removeListener (this);
    core.connection().removeListener (this);
    modal.reset();
    popover.reset();
    notifications.reset();
}


//==============================================================================
bool HomeScreen::showDisconnected() const
{
    return core.connection().isShown();   // 서버 실시간 연결이 끊긴 지 2초 (MeonCore::connection)
}

void HomeScreen::connectionShownChanged (bool)
{
    repaint (0, headerLineY + 1 + bannersHeight(), getWidth(), metric::padX);
}

HomeScreen::UpdateHooks::State HomeScreen::updateState() const
{
    return update.state ? update.state() : UpdateHooks::State::none;
}

bool HomeScreen::showUpdate() const
{
    const auto st = updateState();
    if (st == UpdateHooks::State::none) return false;
    if (st == UpdateHooks::State::available && updateDismissed)
    {
        // 최소 버전보다 낮으면 "나중에"로 숨길 수 없다 (기획 결정)
        const auto& cfg = core.store().state().config;
        return cfg.minBuild > core.buildNumber();
    }
    return true;
}

int HomeScreen::bannersHeight() const
{
    return showUpdate() ? 38 : 0;   // 연결 끊김은 배너가 아니라 패널 위 여백에 그린다 (배치를 밀지 않게)
}

void HomeScreen::refreshHeader()
{
    bell.setDot (core.store().state().hasUnreadNotifications || ! core.store().state().friendRequests.empty());
    filterButton->active = filter.isActive();
    filterButton->repaint();
    const bool upd = showUpdate();
    const auto st = updateState();
    const auto& cfg = core.store().state().config;
    later.setVisible (upd && st == UpdateHooks::State::available && cfg.minBuild <= core.buildNumber());
    updateNow.setVisible (upd && (st == UpdateHooks::State::available || st == UpdateHooks::State::failed));
    updateNow.setButtonText (st == UpdateHooks::State::failed ? TXT ("다시 시도") : TXT ("업데이트"));
}

void HomeScreen::refreshAudio()
{
    auto* jam = core.jam();
    const bool muted = jam != nullptr && jam->isSendMuted();
    sendMute.setOn (muted);
    sendMute.setButtonText (muted ? TXT ("송출 재개") : TXT ("송출 정지"));
    if (jam != nullptr && ! sendGain.isDragging()) sendGain.setValue (jam->getSendGainDb());
    sendGain.setMuted (muted);
    // 송출 정지 중: 미터는 그대로 움직이되 회색으로 (지금 안 보내는 중, 합주 화면 내 오디오와 같은 표시)
    inputMeter->meter.setGreyed (muted);
    // 홈에서 내 소리가 스피커로 나가는 중이면 머리말 오른쪽에 표시 (송출 정지 중에는 들리지 않으므로 사라진다)
    const bool hearing = jam != nullptr && jam->isHomeMonitorAudible();
    if (monitorTag.isVisible() != hearing) { monitorTag.setVisible (hearing); audioCard.resized(); }
    deviceName.setText (deviceText());
    resized();
}

juce::String HomeScreen::deviceText() const
{
    if (plugin) return TXT ("DAW 입력");
    auto& provider = meon::deviceManagerProvider();
    auto* dm = provider ? provider() : nullptr;
    auto* dev = dm != nullptr ? dm->getCurrentAudioDevice() : nullptr;
    if (dev == nullptr) return {};   // 장치 없음은 표시하지 않는다 (2026-10-15 사용자 결정)
    juce::String text = dev->getName();
    if (auto* jam = core.jam())
    {
        const auto& p = jam->getPrefs();
        text << TXT (" · 입력 ") << juce::String (p.inputChannelStart + 1);
        if (p.inputChannelCount >= 2) text << juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")) << juce::String (p.inputChannelStart + 2);
    }
    return text;
}

//==============================================================================
void HomeScreen::rebuildRooms()
{
    const auto& s = core.store().state();
    const auto myId = s.me ? s.me->id : juce::String();
    std::vector<const core::Room*> shown;
    for (auto& r : s.publicRooms)
        if (! filter.isActive() || filter.matches (r, myId, s.config.latencyCoefficient, s.myRttMs))
            shown.push_back (&r);

    // 있던 카드는 다시 쓰고(같은 ID), 없어진 방은 지운다
    juce::OwnedArray<RoomCard> next;
    for (auto* r : shown)
    {
        RoomCard* card = nullptr;
        for (int i = 0; i < roomList->cards.size(); ++i)
            if (roomList->cards[i]->room.id == r->id) { card = roomList->cards.removeAndReturn (i); break; }
        if (card == nullptr)
        {
            card = new RoomCard (*this, roomsShownOnce);
            roomList->addAndMakeVisible (card);
        }
        card->setRoom (*r);
        next.add (card);
    }
    roomList->cards.clear();
    roomList->cards.swapWith (next);
    if (s.publicRoomsLoaded || ! roomList->cards.isEmpty()) roomsShownOnce = true;
    layoutRooms();
    roomList->repaint();
}

void HomeScreen::layoutRooms()
{
    const int viewW = roomViewport.getWidth();
    if (viewW <= 0) return;
    const int n = Masonry::columnsForWidth (getWidth());
    const int pad = 12, gap = Masonry::gap;
    // 스크롤 막대가 필요할 때만 그 폭을 뺀다
    int contentW = viewW, colW = 0;
    std::vector<std::vector<juce::String>> cols;
    for (int pass = 0; pass < 2; ++pass)
    {
        colW = (contentW - pad * 2 - gap * (n - 1)) / n;
        std::vector<Masonry::Item> items;
        for (auto* c : roomList->cards)
        {
            c->lines = descLines (c->room.description, colW - 36);
            items.push_back ({ c->room.id, c->preferredHeight() });
        }
        cols = masonry.place (items, n);
        int tallest = 0;
        for (auto& cl : cols)
        {
            int h = pad;
            for (auto& id : cl)
                for (auto* c : roomList->cards)
                    if (c->room.id == id) h += c->preferredHeight() + gap;
            tallest = juce::jmax (tallest, h - gap + pad);
        }
        if (tallest <= roomViewport.getHeight() || pass == 1) break;
        contentW = viewW - roomViewport.getScrollBarThickness();
    }
    int bottom = 0;
    for (size_t ci = 0; ci < cols.size(); ++ci)
    {
        int y = pad;
        for (auto& id : cols[ci])
            for (auto* c : roomList->cards)
                if (c->room.id == id)
                {
                    c->setBounds (pad + (int) ci * (colW + gap), y, colW, c->preferredHeight());
                    y += c->preferredHeight() + gap;
                }
        bottom = juce::jmax (bottom, y - gap + pad);
    }
    roomList->setSize (contentW, juce::jmax (roomViewport.getHeight(), bottom));
}

void HomeScreen::rebuildFriends()
{
    auto list = core.store().state().friends;
    auto rank = [] (const core::Friend& f) { return f.status == "in_room" ? 0 : (f.status == "online" ? 1 : 2); };
    std::stable_sort (list.begin(), list.end(), [&] (const core::Friend& a, const core::Friend& b) { return rank (a) < rank (b); });
    friendList->rows.clear();
    for (auto& f : list)
        friendList->addAndMakeVisible (friendList->rows.add (new FriendList::Row (*this, f)));
    resized();
    friendList->repaint();
}

void HomeScreen::enterRoom (const core::Room& room)
{
    const auto& s = core.store().state();
    if (! core::rooms::hasRoomFor (room, s.me ? s.me->id : juce::String())) return;
    if (room.isApproval() && core.home().isRequestPending (room)) return;
    juce::Component::SafePointer<HomeScreen> safe (this);
    core.home().joinRoom (room.id, [safe] (core::Result<core::JoinResult> r)
    {
        if (safe != nullptr && ! r.ok() && safe->onJoinFailure) safe->onJoinFailure (r.err());
    });
}

void HomeScreen::sendFriendRequest()
{
    const auto id = addInput.getText().trim().trimCharactersAtStart ("@");
    if (id.isEmpty())
    {
        addMessage = TXT ("아이디를 입력해 주세요");
        resized();
        repaint();
        return;
    }
    std::weak_ptr<bool> weak = alive;
    core.home().sendFriendRequest (id, [this, weak, id] (core::Result<juce::String> r)
    {
        if (weak.expired()) return;
        if (r.ok())
        {
            addMessage = msg::friendRequestResult (r.get(), id.toLowerCase());
            if (r.get() == "sent" || r.get() == "accepted") addInput.setText ({});
        }
        else
        {
            addMessage = msg::friendRequestError (r.err());
        }
        resized();
        repaint();
    });
}

//==============================================================================
void HomeScreen::showModal (std::unique_ptr<Modal> m)
{
    popover.reset();
    notifications.reset();
    modal = std::move (m);
    modal->onClose = [this] { juce::Component::SafePointer<HomeScreen> safeSelf (this); juce::MessageManager::callAsync ([safe = safeSelf]
                                                               { if (safe != nullptr) { safe->modal.reset(); safe->grabKeyboardFocus(); } }); };
    addAndMakeVisible (*modal);
    modal->setBounds (getLocalBounds());
    modal->toFront (true);
}

void HomeScreen::openProfile (const core::UserPublic& u)
{
    juce::Component::SafePointer<HomeScreen> safe (this);
    auto user = u;
    juce::MessageManager::callAsync ([safe, user]
    {
        if (safe == nullptr) return;
        auto m = std::make_unique<ProfileModal> (safe->core, user, false);
        m->onJoinFailure = [safe] (const core::ApiError& e)
        {
            juce::MessageManager::callAsync ([safe, e] { if (safe != nullptr && safe->onJoinFailure) safe->onJoinFailure (e); });
        };
        safe->showModal (std::move (m));
    });
}

void HomeScreen::openJoinCode()
{
    auto m = std::make_unique<JoinCodeModal> (core);
    auto* raw = m.get();
    m->onFailure = [this] (const core::ApiError& e)
    {
        juce::Component::SafePointer<HomeScreen> safeSelf (this); juce::MessageManager::callAsync ([safe = safeSelf, e]
        {
            if (safe == nullptr) return;
            safe->modal.reset();
            if (safe->onJoinFailure) safe->onJoinFailure (e);
        });
    };
    showModal (std::move (m));
    raw->focusCode();
}

void HomeScreen::openCreateRoom()
{
    auto m = std::make_unique<CreateRoomModal> (core);
    auto* raw = m.get();
    showModal (std::move (m));
    raw->focusName();
}

void HomeScreen::openFilter()
{
    auto m = std::make_unique<FilterModal> (core, filter);
    m->onApply = [this] (const core::RoomFilter& f)
    {
        filter = f;
        juce::Component::SafePointer<HomeScreen> safeSelf (this); juce::MessageManager::callAsync ([safe = safeSelf]
        {
            if (safe == nullptr) return;
            safe->modal.reset();
            safe->masonry.reset();
            safe->rebuildRooms();
            safe->refreshHeader();
        });
    };
    showModal (std::move (m));
}

void HomeScreen::toggleNotifications()
{
    if (popover != nullptr)
    {
        popover.reset();
        notifications.reset();
        return;
    }
    core.home().markNotificationsRead();
    notifications = std::make_unique<NotificationList> (core);
    popover = std::make_unique<Popover> (TXT ("알림"));
    popover->onClose = [this] { juce::Component::SafePointer<HomeScreen> safeSelf (this); juce::MessageManager::callAsync ([safe = safeSelf]
                                                                 { if (safe != nullptr) { safe->popover.reset(); safe->notifications.reset(); } }); };
    notifications->onSizeChanged = [this] { resized(); };
    notifications->onOpenProfile = [this] (const core::UserPublic& u) { openProfile (u); };
    popover->setContent (notifications.get(), notifications->getPreferredHeight());
    addAndMakeVisible (*popover);
    resized();
    popover->toFront (false);
}

void HomeScreen::closeOverlays()
{
    modal.reset();
    popover.reset();
    notifications.reset();
}

bool HomeScreen::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey && hasOverlay())
    {
        closeOverlays();
        return true;
    }
    return false;
}

//==============================================================================
void HomeScreen::stateChanged (core::StateKey k)
{
    using K = core::StateKey;
    switch (k)
    {
        case K::publicRooms: case K::config: case K::myRtt: case K::me:
            rebuildRooms();
            // 친구 목록의 참여·요청 버튼도 방의 입장 방식·요청 상태를 따른다 (카드에서 요청하면 친구 쪽도 "요청 보냄")
            for (auto* r : friendList->rows) r->refreshJoin();
            break;
        case K::friends:
            rebuildFriends();
            break;
        case K::friendRequests: case K::notifications:
            refreshHeader();
            break;
        case K::link:
            refreshHeader();
            resized();
            repaint();
            break;
        default:
            break;
    }
}

void HomeScreen::timerCallback()
{
    // 승인 요청 표시(request_until)·마지막 접속·업데이트 진행을 시간에 맞춰 다시 그린다
    for (auto* c : roomList->cards)
        if (c->room.myJoinRequestUntil || core.home().isRequestPending (c->room.id)) c->setRoom (c->room);
    for (auto* r : friendList->rows) r->refreshJoin();   // "요청 보냄" 5분이 지나면 원래대로
    friendList->repaint();
    const int before = bannersHeight();
    refreshHeader();
    if (bannersHeight() != before) resized();
    if (showUpdate()) repaint (0, headerLineY, getWidth(), bannersHeight() + 1);
    if (! sendGain.isDragging()) refreshAudio();
}

//==============================================================================
void HomeScreen::paint (juce::Graphics& g)
{
    g.fillAll (col::white);
    // 배너 아래 작업 영역 (패널들을 감싸는 곳)
    const int canvasTop = headerLineY + 1 + bannersHeight();
    g.setColour (col::surfaceCanvas);
    g.fillRect (0, canvasTop, getWidth(), getHeight() - canvasTop);
    const auto logoArea = juce::Rectangle<int> (metric::padX, 12, 200, metric::logoH);
    meon::drawLogo (g, logoArea.toFloat(), metric::logoDrawH, col::ink, juce::Justification::centredLeft);
    g.setColour (col::line);
    g.fillRect (0, headerLineY, getWidth(), 1);

    int y = headerLineY + 1;
    if (showUpdate())
    {
        g.setColour (col::hoverBg);
        g.fillRect (0, y, getWidth(), 37);
        g.setColour (col::line);
        g.fillRect (0, y + 37, getWidth(), 1);
        const auto st = updateState();
        const auto f1 = font (500, 13.0f);
        const auto t1 = st == UpdateHooks::State::failed ? TXT ("새 버전을 받지 못했어요") : TXT ("새 버전이 나왔어요");
        g.setColour (col::ink);
        g.setFont (f1);
        const int w1 = (int) std::ceil (textWidth (f1, t1)) + 1;
        g.drawText (t1, metric::padX, y, w1, 37, juce::Justification::centredLeft, false);
        const int latest = update.latestBuild ? update.latestBuild() : 0;
        const auto ver = TXT ("빌드 ") + juce::String (latest);
        juce::String note = ver;
        const float p = update.progress ? update.progress() : -1.0f;
        if (st == UpdateHooks::State::downloading)
            note = ver + TXT (" 내려받는 중") + (p >= 0.0f ? TXT (" · ") + juce::String ((int) std::floor (p * 100.0f)) + "%" : juce::String());
        else if (st == UpdateHooks::State::installing)
            note = TXT ("설치를 마쳤어요 · 다시 시작하는 중…");
        g.setColour (col::inkSub);
        g.setFont (font (400, 12.0f));
        g.drawText (note, metric::padX + w1 + 9, y, getWidth() / 2, 37, juce::Justification::centredLeft, true);
        if (st == UpdateHooks::State::downloading)
        {
            const juce::Rectangle<float> bar ((float) getWidth() - metric::padX - 160.0f, (float) y + 16.5f, 160.0f, 4.0f);
            g.setColour (col::line);
            g.fillRoundedRectangle (bar, 2.0f);
            g.setColour (col::accent);
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, p)), 2.0f);
        }
    }
    // 연결 끊김: 배너 아래, 패널 위 여백 (배치를 밀지 않는다)
    if (showDisconnected())
        paintDisconnectedStrip (g, { 0, headerLineY + 1 + bannersHeight(), getWidth(), metric::padX });
}

void HomeScreen::resized()
{
    auto area = getLocalBounds();
    // 헤더: 오른쪽에 설정, 알림
    {
        auto h = area.withHeight (metric::headerH).reduced (metric::padX, 0);
        const int sw = settings.getIdealWidth();
        settings.setBounds (h.removeFromRight (sw).withSizeKeepingCentre (sw, metric::buttonH));
        h.removeFromRight (metric::gap);
        bell.setBounds (h.removeFromRight (32).withSizeKeepingCentre (32, 32));
    }
    area.removeFromTop (headerLineY + 1);

    // 배너 버튼
    {
        const int top = headerLineY + 1;
        int x = getWidth() - metric::padX;
        if (updateNow.isVisible())
        {
            const int w = updateNow.getIdealWidth();
            x -= w;
            updateNow.setBounds (x, top + 9, w, metric::smallH);
            x -= metric::gap;
        }
        if (later.isVisible())
        {
            const int w = later.getIdealWidth();
            x -= w;
            later.setBounds (x, top + 9, w, metric::smallH);
        }
    }
    area.removeFromTop (bannersHeight());
    area.reduce (metric::padX, metric::padX);

    // 오른쪽: 친구 + 친구 추가
    const int sideW = plugin ? sideWidthPlugin : sideWidthApp;
    auto side = area.removeFromRight (sideW);
    area.removeFromRight (metric::padX);
    {
        // 결과 문구: 입력 줄 아래 (디자인 padding 0 18px 12px, margin-top -6px)
        addNote.setText (addMessage);
        addNote.setVisible (addMessage.isNotEmpty());
        const int noteW = sideW - 2 - metric::padX * 2;
        const int noteH = addMessage.isNotEmpty() ? juce::jmax (15, addNote.getHeightForWidth (noteW)) : 0;
        const int addH = 48 + 1 + addRowH + (addMessage.isNotEmpty() ? noteH + 12 - 6 : 0);
        addCard.setBounds (side.removeFromBottom (addH));
        side.removeFromBottom (metric::gap);
        friendsCard.setBounds (friendsOpen ? side : side.withHeight (48));
        friendsCard.setOpen (friendsOpen);
        friendViewport.setVisible (friendsOpen);
        const auto fc = friendsCard.contentBounds();
        friendViewport.setBounds (fc);
        friendList->setSize (fc.getWidth(), juce::jmax (fc.getHeight(), friendList->preferredHeight()));
        friendList->resized();

        const auto ac = addCard.contentBounds();
        addNote.setBounds (ac.getX() + metric::padX, ac.getY() + addRowH - 6, ac.getWidth() - metric::padX * 2, noteH);
        auto row = ac.withHeight (addRowH).reduced (metric::padX, 12);
        const int sw = addSend.getIdealWidth();
        addSend.setBounds (row.removeFromRight (sw));
        row.removeFromRight (8);
        addInput.setBounds (row);
    }

    // 왼쪽: 공개 방 + 내 오디오
    audioCard.setBounds (area.removeFromBottom (audioCardH));
    area.removeFromBottom (metric::padX);
    roomsCard.setBounds (area);
    {
        auto h = roomsCard.getLocalBounds().withHeight (roomsHeaderH).reduced (metric::padX, 12);
        const int cw = createRoom.getIdealWidth(), jw = joinCode.getIdealWidth();
        createRoom.setBounds (h.removeFromRight (cw));
        h.removeFromRight (metric::gap);
        joinCode.setBounds (h.removeFromRight (jw));
        const int titleW = (int) std::ceil (textWidth (font (600, 16.0f), TXT ("공개 방")));
        roomsTitle.setBounds (metric::padX, 12, titleW + 2, 32);
        filterButton->setBounds (metric::padX + titleW + 6, 16, 24, 24);
        roomsLine->setBounds (1, roomsHeaderH, roomsCard.getWidth() - 2, 1);
        roomViewport.setBounds (roomsCard.getLocalBounds().withTrimmedTop (roomsHeaderH + 1).reduced (1, 0).withTrimmedBottom (1));
        layoutRooms();
    }
    {
        auto row = audioCard.contentBounds().reduced (metric::padX, 12);
        const int mw = juce::jlimit (84, 240, row.getWidth() / 3);
        inputMeter->setBounds (row.removeFromRight (mw).withSizeKeepingCentre (mw, metric::smallH));
        row.removeFromRight (12);
        if (deviceName.getText().isNotEmpty())   // 장치가 없으면 비워 두고 자리도 잡지 않는다
        {
            const int dw = juce::jmin (deviceName.getIdealWidth() + 2, row.getWidth() / 3);
            deviceName.setBounds (row.removeFromLeft (dw));
            row.removeFromLeft (12);
        }
        else
        {
            deviceName.setBounds ({});
        }
        const int bw = sendMute.getIdealWidth();
        sendMute.setBounds (row.removeFromLeft (bw).withSizeKeepingCentre (bw, metric::smallH));
        row.removeFromLeft (12);
        sendGain.setBounds (row.withSizeKeepingCentre (row.getWidth(), metric::smallH));
    }

    if (modal != nullptr) modal->setBounds (getLocalBounds());
    if (popover != nullptr && notifications != nullptr)
    {
        const int h = popover->getPreferredHeight() - 0;
        popover->setContent (notifications.get(), notifications->getPreferredHeight());
        popover->setBounds (getWidth() - 12 - 340, headerLineY + 10, 340, juce::jmax (h, popover->getPreferredHeight()));
    }
    repaint();
}

} // namespace meon::ui
