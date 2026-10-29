#include "HomeScreen.h"
#include "HomePage.h"
#include "../Messages.h"
#include "../Links.h"
#include "../theme/Icons.h"
#include "../../core/api/ApiJson.h"
#include "../../shell/ShellHooks.h"

namespace meon::ui
{

namespace
{
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

/** 필터 버튼 24×24 (아이콘 14px). 필터가 켜져 있으면 진한 색 + 포인트 색 점 */
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
                paintTagBox (g, tr, tag, tagF);
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
            join.onPress = [this] { owner.joinFriend (fr); };
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
/** 방 최소화 중 아래 막대 (합주 최종 디자인 5). 높이 57 = 위 테두리 1 + 12 + 32 + 12, 좌우 18, 흰 면, 호버 #F8F8F8.
    왼쪽(간격 9): 방 이름 600 14 (최대 240, 말줄임) · 인원 "5 / 7" 400 12 · 배지 "새 채팅 N" · "입장 요청 N".
    오른쪽: "나가기" (두 번 눌러 확정, 방장이면 위 6 에 툴팁). 막대 어디를 눌러도 합주 화면으로 (나가기 버튼은 제외) */
struct HomeScreen::RoomBar : juce::Component,
                             juce::SettableTooltipClient,
                             private core::ChatChannel::Listener
{
    explicit RoomBar (HomeScreen& o) : owner (o), jam (o.core.jam())
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip (TXT ("합주 화면으로 돌아가기"));
        addAndMakeVisible (leave);
        leave.addMouseListener (&hover, false);
        leave.setConfirm (TXT ("한 번 더 누르면 나가요"));
        leave.onStateChange = [this]
        {
            resized();
            repaint();
            if (leave.isArmed() && owner.core.store().state().amHost())
                owner.tip.show (owner, leave, TXT ("돌아올 때까지 방장 자리가 비어요"), Tip::Place::aboveRight);
            else if (owner.tip.getTarget() == &leave)
                owner.tip.hide();
        };
        leave.onPress = [this]
        {
            juce::Component::SafePointer<HomeScreen> safe (&owner);
            juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->core.home().leaveRoom(); });   // 막대가 지워진다
        };
        if (jam != nullptr) jam->chat().addListener (this);
    }

    ~RoomBar() override
    {
        if (jam != nullptr) jam->chat().removeListener (this);
        if (owner.tip.getTarget() == &leave) owner.tip.hide();
    }

    int unreadChats() const { return jam != nullptr ? jam->chat().getUnread() : 0; }

    int joinRequests() const
    {
        const auto& s = owner.core.store().state();
        return s.amHost() ? (int) s.joinRequests.size() : 0;   // 방장이 아니면 늘 0 (디자인 nReq = host ? reqs : 0)
    }

    static float badgeWidth (const juce::String& label, int n)
    {
        return 6.0f + textWidth (font (400, 12.0f), label) + 4.0f + tabularWidth (font (600, 12.0f), juce::String (n)) + 6.0f;
    }

    /** 배지: 높이 19, 안쪽 6, 모서리 5, 글자 400 12 + 숫자 600 #1A1A1A (간격 4) */
    static float drawBadge (juce::Graphics& g, float x, float cy, const juce::String& label, int n, juce::Colour bg, juce::Colour ink)
    {
        const float w = badgeWidth (label, n);
        const juce::Rectangle<float> r (x, cy - 9.5f, w, 19.0f);
        g.setColour (bg);
        g.fillRoundedRectangle (r, 5.0f);
        const auto lf = font (400, 12.0f);
        const float lw = textWidth (lf, label);
        g.setColour (ink);
        g.setFont (lf);
        g.drawText (label, juce::Rectangle<float> (x + 6.0f, r.getY(), lw + 1.0f, r.getHeight()), juce::Justification::centredLeft, false);
        g.setColour (col::ink);
        drawTabular (g, font (600, 12.0f), juce::String (n), x + 6.0f + lw + 4.0f, r.getY(), r.getHeight());
        return w;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (isMouseOver (true) ? col::rowHover : col::white);
        g.setColour (col::line);
        g.fillRect (0, 0, getWidth(), 1);

        const auto& s = owner.core.store().state();
        if (! s.currentRoom) return;
        const auto& room = *s.currentRoom;
        const float cy = 1.0f + 12.0f + 16.0f;
        const auto countText = juce::String ((int) room.members.size()) + " / " + juce::String (room.capacity);
        const int chats = unreadChats(), reqs = joinRequests();
        const auto chatLabel = TXT ("새 채팅"), reqLabel = TXT ("입장 요청");
        const auto countF = font (400, 12.0f);

        // 방 이름만 줄어든다 (나머지는 줄지 않음, 디자인 flex-shrink:0). 오른쪽 나가기 버튼과는 18
        const float fixedW = 9.0f + tabularWidth (countF, countText) + 9.0f + badgeWidth (chatLabel, chats) + 9.0f + badgeWidth (reqLabel, reqs);
        const float leftEnd = (float) leave.getX() - 18.0f;
        const auto nameF = font (600, 14.0f);
        const float nameW = juce::jmax (0.0f, std::min ({ 240.0f, textWidth (nameF, room.title) + 1.0f, leftEnd - 18.0f - fixedW }));
        float x = 18.0f;
        g.setColour (col::ink);
        g.setFont (nameF);
        g.drawText (room.title, juce::Rectangle<float> (x, cy - 16.0f, nameW, 32.0f), juce::Justification::centredLeft, true);
        x += nameW + 9.0f;
        g.setColour (col::inkSub);
        x += drawTabular (g, countF, countText, x, cy - 16.0f, 32.0f) + 9.0f;
        x += drawBadge (g, x, cy, chatLabel, chats, col::badgeBg, col::inkSub) + 9.0f;
        drawBadge (g, x, cy, reqLabel, reqs, reqs > 0 ? col::accent : col::badgeBg, reqs > 0 ? col::ink : col::inkSub);
    }

    void resized() override
    {
        const int w = leave.getIdealWidth();
        leave.setBounds (getWidth() - 18 - w, 1 + 12, w, metric::buttonH);
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasClicked() && owner.onReturnToRoom) owner.onReturnToRoom();
    }

    void chatChanged() override { repaint(); }

    struct Hover : juce::MouseListener
    {
        explicit Hover (RoomBar& b) : bar (b) {}
        void mouseEnter (const juce::MouseEvent&) override { bar.repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { bar.repaint(); }
        RoomBar& bar;
    } hover { *this };

    HomeScreen& owner;
    core::JamSession* jam;
    Button leave { Button::Kind::secondary, TXT ("나가기") };
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
    settingsTip = std::make_unique<TipOnHover> (tip, *this, settings, TXT ("설정"), Tip::Place::belowCentre);
    settings.onClick = [this] { tip.hide(); if (onOpenSettings) onOpenSettings(); };

    addChildComponent (later);
    addChildComponent (updateNow);
    addChildComponent (directDownload);
    later.onPress = [this] { updateDismissed = true; resized(); repaint(); };
    updateNow.onPress = [this] { if (update.start) update.start(); };
    directDownload.onPress = [] { links::open (links::download()); };

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
    friendsCard.onToggle = [this] (bool open) { friendsOpen = open; fold.setOpen (foldFriends, open); };
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
    confirm.reset();
    modal.reset();
    popover.reset();
    notifications.reset();
    roomBar.reset();
}


//==============================================================================
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
    return showUpdate() ? 38 : 0;   // 연결 끊김은 배너가 아니라 토스트 (AppRoot::connectionToastChanged)
}

void HomeScreen::refreshHeader()
{
    bell.setDot (core.store().state().hasUnreadNotifications || ! core.store().state().friendRequests.empty());
    filterButton->active = filter.isActive();
    filterButton->repaint();
    const bool upd = showUpdate();
    const auto st = updateState();
    const auto& cfg = core.store().state().config;
    // 받기 실패: "다시 시도" 와 웹에서 "직접 내려받기". 앱 자리에 쓸 수 없는 실패(macOS)는 다시 시도해도 같아서 직접 받기만
    const bool failed = st == UpdateHooks::State::failed;
    const bool needsMove = failed && update.needsMove && update.needsMove();
    const bool showLater = upd && st == UpdateHooks::State::available && cfg.minBuild <= core.buildNumber();
    const bool showNow = upd && (st == UpdateHooks::State::available || (failed && ! needsMove));
    const bool showDirect = upd && failed;
    const auto nowText = failed ? TXT ("다시 시도") : TXT ("업데이트");
    // 버튼이 나타나거나 글자가 바뀌면 배너 버튼 자리를 다시 잡는다 (배너 높이는 그대로라 resized 가 저절로 불리지 않는다)
    const bool relayoutButtons = later.isVisible() != showLater || updateNow.isVisible() != showNow
                                 || directDownload.isVisible() != showDirect || updateNow.getButtonText() != nowText;
    later.setVisible (showLater);
    updateNow.setVisible (showNow);
    updateNow.setButtonText (nowText);
    directDownload.setVisible (showDirect);
    if (relayoutButtons && getWidth() > 0)
        resized();
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
    const int n = Masonry::columnsForWidth (shell ? getWidth() - shellInset : getWidth());
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
    // 방 최소화 중: 바로 입장·요청 보내기 모두 먼저 지금 방에서 나간다 (확인 모달)
    juce::Component::SafePointer<HomeScreen> safe (this);
    const auto target = room;
    confirmLeaveThen (false, room.id, [safe, target] { if (safe != nullptr) safe->joinRoomNow (target); });
}

void HomeScreen::joinRoomNow (const core::Room& room)
{
    juce::Component::SafePointer<HomeScreen> safe (this);
    core.home().joinRoom (room.id, [safe] (core::Result<core::JoinResult> r)
    {
        if (safe != nullptr && ! r.ok() && safe->onJoinFailure) safe->onJoinFailure (r.err());
    });
}

void HomeScreen::joinFriend (const core::Friend& f)
{
    juce::Component::SafePointer<HomeScreen> safe (this);
    const auto fr = f;
    // 방 최소화 중이면 먼저 확인 (나가는 동안 친구 목록이 다시 만들어지므로 행이 아니라 화면이 이어받는다)
    confirmLeaveThen (false, fr.room ? fr.room->id : juce::String(), [safe, fr]
    {
        if (safe == nullptr) return;
        safe->core.home().joinFriend (fr, [safe] (core::Result<core::JoinResult> r)
        {
            if (safe == nullptr) return;
            for (auto* row : safe->friendList->rows) row->refreshJoin();
            if (! r.ok() && safe->onJoinFailure) safe->onJoinFailure (r.err());
        });
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
int HomeScreen::headerLine() const { return shell ? 51 : metric::headerH; }

void HomeScreen::setShell (int leftInset, const juce::String& title, bool homePageShown)
{
    const bool changed = ! shell || leftInset != shellInset || title != pageTitle || homePageShown != shellHome;
    if (! shell)
    {
        // 공개 방 쪽 (디자인): 카드 없이 쪽 머리말에 필터·방 만들기, 그 아래 방 목록. 코드로 입장은 사이드바 메뉴에 있다
        addChildComponent (roomViewport);
        addChildComponent (*filterButton);
        addChildComponent (createRoom);
        roomsCard.setVisible (false);
        audioCard.setVisible (false);   // 내 오디오는 방 안에만 (사용자 결정 2026-10-29). 홈은 "내 준비 상태"
        auto page = std::make_unique<HomePage> (core);
        page->onOpenSettings = [this] (int tab) { if (onOpenSettingsTab) onOpenSettingsTab (tab); };
        page->onGoRooms = [this] { if (onShowRooms) onShowRooms(); };
        homePage = std::move (page);
        addChildComponent (*homePage);
    }
    shell = true;
    shellHome = homePageShown;
    shellInset = leftInset;
    pageTitle = title;
    setOpaque (false);   // 왼쪽 사이드바 자리는 비워 둔다
    if (changed && getWidth() > 0) { resized(); repaint(); }
}

bool HomeScreen::hitTest (int x, int y)
{
    // 사이드바 자리는 사이드바가 받는다. 모달·확인 창·알림이 떠 있으면 창 전체를 덮는다
    if (shell && x < shellInset && ! hasOverlay()) return false;
    return juce::Component::hitTest (x, y);
}

namespace
{
    /** 친구 추가 (사이드바 +): 아이디 입력 + 요청. 결과는 입력 아래 한 줄 (홈 친구 추가 카드와 같은 문구) */
    class AddFriendModal : public Modal
    {
    public:
        explicit AddFriendModal (core::MeonCore& c) : Modal (TXT ("친구 추가"), 400), core (c)
        {
            body.owner = this;
            body.addAndMakeVisible (input);
            body.addAndMakeVisible (send);
            body.addChildComponent (note);
            input.setFontPx (14.0f);
            input.setMaxLength (16);
            input.transform = [] (const juce::String& t) { return t.toLowerCase(); };
            input.onReturn = [this] { submit(); };
            input.onChange = [this] { if (note.isVisible()) { note.setVisible (false); layoutHeight(); } };
            send.onPress = [this] { submit(); };
            note.setLineHeight (15.0f);
            layoutHeight();
        }
        void focus() { input.grabKeyboardFocus(); }

    private:
        struct Body : juce::Component
        {
            AddFriendModal* owner = nullptr;
            void resized() override
            {
                if (owner == nullptr) return;
                auto r = getLocalBounds().reduced (metric::padX);
                auto row = r.removeFromTop (metric::buttonH);
                const int sw = owner->send.getIdealWidth();
                owner->send.setBounds (row.removeFromRight (sw));
                row.removeFromRight (metric::gap);
                owner->input.setBounds (row);
                r.removeFromTop (6);
                owner->note.setBounds (r.withHeight (juce::jmax (15, owner->note.getHeightForWidth (r.getWidth()))));
            }
        } body;
        core::MeonCore& core;
        TextInput input { TXT ("친구 아이디 입력") };
        Button send { Button::Kind::primary, TXT ("요청 보내기") };
        Text note { {}, 400, 12.0f, col::inkSub };
        std::shared_ptr<bool> alive { std::make_shared<bool> (true) };

        void layoutHeight()
        {
            const int w = 400 - metric::padX * 2;
            const int nh = note.isVisible() ? 6 + juce::jmax (15, note.getHeightForWidth (w)) : 0;
            setContent (&body, metric::padX * 2 + metric::buttonH + nh);
        }
        void show (const juce::String& t)
        {
            note.setText (t);
            note.setVisible (t.isNotEmpty());
            layoutHeight();
            body.resized();
        }
        void submit()
        {
            const auto id = input.getText().trim().trimCharactersAtStart ("@");
            if (id.isEmpty()) { show (TXT ("아이디를 입력해 주세요")); return; }
            std::weak_ptr<bool> weak = alive;
            core.home().sendFriendRequest (id, [this, weak, id] (core::Result<juce::String> r)
            {
                if (weak.expired()) return;
                if (r.ok())
                {
                    show (msg::friendRequestResult (r.get(), id.toLowerCase()));
                    if (r.get() == "sent" || r.get() == "accepted") input.setText ({});
                }
                else
                    show (msg::friendRequestError (r.err()));
            });
        }
    };
}

void HomeScreen::openAddFriend()
{
    auto m = std::make_unique<AddFriendModal> (core);
    auto* raw = m.get();
    showModal (std::move (m));
    raw->focus();
}

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
        m->beforeJoin = safe->leaveGuard();
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
    m->beforeJoin = leaveGuard();
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
    // 방 최소화 중이면 먼저 "지금 방에서 나가고 새 방을 만들까요?" → 나간 뒤 방 만들기 모달
    juce::Component::SafePointer<HomeScreen> safe (this);
    confirmLeaveThen (true, {}, [safe] { if (safe != nullptr) safe->openCreateRoomNow(); });
}

void HomeScreen::openCreateRoomNow()
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
    confirm.reset();
    modal.reset();
    popover.reset();
    notifications.reset();
}

bool HomeScreen::closeTopOverlay()
{
    // 최소화 중 확인 모달은 그 아래 모달(코드로 입장·프로필)보다 먼저 닫힌다 (Esc = 취소)
    if (confirm != nullptr) { confirm.reset(); grabKeyboardFocus(); return true; }
    if (! hasOverlay()) return false;
    closeOverlays();
    return true;
}

bool HomeScreen::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) return closeTopOverlay();
    return false;
}

//==============================================================================
void HomeScreen::setRoomBar (bool show)
{
    if (show == (roomBar != nullptr)) return;
    if (show)
    {
        roomBar = std::make_unique<RoomBar> (*this);
        addAndMakeVisible (*roomBar);
    }
    else
    {
        confirm.reset();   // 방이 없어졌으면 확인할 것도 없다
        roomBar.reset();
    }
    resized();
    repaint();
}

juce::Component* HomeScreen::devRoomBar() const { return roomBar.get(); }
Button* HomeScreen::devRoomBarLeave() const     { return roomBar != nullptr ? &roomBar->leave : nullptr; }
int HomeScreen::devRoomBarUnread() const        { return roomBar != nullptr ? roomBar->unreadChats() : -1; }

std::vector<std::pair<core::Room, juce::Button*>> HomeScreen::devRoomCards() const
{
    std::vector<std::pair<core::Room, juce::Button*>> out;
    for (auto* c : roomList->cards) out.emplace_back (c->room, &c->join);
    return out;
}

void HomeScreen::devPreviewRoomBar (const juce::String& what)
{
    if (roomBar == nullptr) return;
    if (what == "arm") roomBar->leave.triggerClick();
    if (what == "create") openCreateRoom();
    if (what == "confirm")
    {
        const auto& s = core.store().state();
        for (auto& r : s.publicRooms)
            if ((! s.currentRoom || r.id != s.currentRoom->id) && core::rooms::hasRoomFor (r, s.me ? s.me->id : juce::String()) && ! r.isApproval()) { enterRoom (r); break; }
    }
}

LeaveGuard HomeScreen::leaveGuard()
{
    juce::Component::SafePointer<HomeScreen> safe (this);
    return [safe] (bool create, const juce::String& roomId, std::function<void()> proceed)
    {
        if (safe != nullptr) safe->confirmLeaveThen (create, roomId, std::move (proceed));
    };
}

void HomeScreen::confirmLeaveThen (bool create, const juce::String& roomId, std::function<void()> proceed)
{
    const auto& room = core.store().state().currentRoom;
    if (! room) { if (proceed) proceed(); return; }
    if (! create && roomId.isNotEmpty() && roomId == room->id)
    {
        // 지금 방이면 나가지 않고 합주 화면으로 돌아간다
        juce::Component::SafePointer<HomeScreen> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr && safe->onReturnToRoom) safe->onReturnToRoom(); });
        return;
    }
    auto m = std::make_unique<LeaveConfirmModal> (core, create);
    m->onLeft = std::move (proceed);
    m->onClose = [this]
    {
        juce::Component::SafePointer<HomeScreen> safeSelf (this);
        juce::MessageManager::callAsync ([safe = safeSelf] { if (safe != nullptr) { safe->confirm.reset(); safe->grabKeyboardFocus(); } });
    };
    confirm = std::move (m);
    addAndMakeVisible (*confirm);
    confirm->setBounds (getLocalBounds());
    confirm->toFront (true);
}

//==============================================================================
void HomeScreen::stateChanged (core::StateKey k)
{
    using K = core::StateKey;
    switch (k)
    {
        case K::publicRooms: case K::config: case K::myRtt: case K::me:
            if (roomBar != nullptr) roomBar->repaint();
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
        case K::currentRoom: case K::joinRequests:
            if (roomBar != nullptr) { roomBar->resized(); roomBar->repaint(); }
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
    if (showUpdate()) repaint (0, headerLine(), getWidth(), bannersHeight() + 1);
    if (! sendGain.isDragging()) refreshAudio();
}

//==============================================================================
void HomeScreen::paint (juce::Graphics& g)
{
    const int x0 = shell ? shellInset : 0;   // 사이드바 안이면 왼쪽 사이드바 자리는 그리지 않는다
    g.setColour (col::white);
    g.fillRect (x0, 0, getWidth() - x0, getHeight());
    // 배너 아래 작업 영역 (패널들을 감싸는 곳)
    const int canvasTop = headerLine() + 1 + bannersHeight();
    g.setColour (col::surfaceCanvas);
    g.fillRect (x0, canvasTop, getWidth() - x0, getHeight() - canvasTop);
    if (shell)
    {
        // 쪽 제목 (디자인: 52 높이, 600 16px, 왼쪽 18)
        g.setColour (col::ink);
        g.setFont (font (600, 16.0f));
        g.drawText (pageTitle, x0 + metric::padX, 0, getWidth() - x0 - metric::padX * 2, headerLine(), juce::Justification::centredLeft, true);
    }
    else
    {
        const auto logoArea = juce::Rectangle<int> (metric::padX, 12, 200, metric::logoH);
        meon::drawLogo (g, logoArea.toFloat(), metric::logoDrawH, col::ink, juce::Justification::centredLeft);
    }
    g.setColour (col::line);
    g.fillRect (x0, headerLine(), getWidth() - x0, 1);

    int y = headerLine() + 1;
    if (showUpdate())
    {
        g.setColour (col::hoverBg);
        g.fillRect (x0, y, getWidth() - x0, 37);
        g.setColour (col::line);
        g.fillRect (x0, y + 37, getWidth() - x0, 1);
        const auto st = updateState();
        const auto f1 = font (500, 13.0f);
        const auto t1 = st == UpdateHooks::State::failed ? TXT ("새 버전을 받지 못했어요") : TXT ("새 버전이 나왔어요");
        g.setColour (col::ink);
        g.setFont (f1);
        const int w1 = (int) std::ceil (textWidth (f1, t1)) + 1;
        g.drawText (t1, x0 + metric::padX, y, w1, 37, juce::Justification::centredLeft, false);
        const int latest = update.latestBuild ? update.latestBuild() : 0;
        const auto ver = TXT ("빌드 ") + juce::String (latest);
        juce::String note = ver;
        const float p = update.progress ? update.progress() : -1.0f;
        if (st == UpdateHooks::State::downloading)
            note = ver + TXT (" 내려받는 중") + (p >= 0.0f ? TXT (" · ") + juce::String ((int) std::floor (p * 100.0f)) + "%" : juce::String());
        else if (st == UpdateHooks::State::installing)
            note = TXT ("설치를 마쳤어요 · 다시 시작하는 중…");
        else if (st == UpdateHooks::State::failed && update.needsMove && update.needsMove())
            note = TXT ("Meon을 응용 프로그램 폴더로 옮긴 뒤 다시 켜 주세요");
        g.setColour (col::inkSub);
        g.setFont (font (400, 12.0f));
        g.drawText (note, x0 + metric::padX + w1 + 9, y, (getWidth() - x0) / 2, 37, juce::Justification::centredLeft, true);
        if (st == UpdateHooks::State::downloading)
        {
            const juce::Rectangle<float> bar ((float) getWidth() - metric::padX - 160.0f, (float) y + 16.5f, 160.0f, 4.0f);
            g.setColour (col::line);
            g.fillRoundedRectangle (bar, 2.0f);
            g.setColour (col::accent);
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, p)), 2.0f);
        }
    }
}

void HomeScreen::resized()
{
    auto area = getLocalBounds().withTrimmedLeft (shell ? shellInset : 0);
    // 헤더: 오른쪽에 설정, 알림 (사이드바 안이면 프로필 메뉴로 옮겨 숨긴다)
    bell.setVisible (! shell);
    settings.setVisible (! shell);
    if (! shell)
    {
        auto h = area.withHeight (metric::headerH).reduced (metric::padX, 0);
        settings.setBounds (h.removeFromRight (32).withSizeKeepingCentre (32, 32));
        h.removeFromRight (metric::gap);
        bell.setBounds (h.removeFromRight (32).withSizeKeepingCentre (32, 32));
    }
    area.removeFromTop (headerLine() + 1);

    // 배너 버튼
    {
        const int top = headerLine() + 1;
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
            x -= metric::gap;
        }
        if (directDownload.isVisible())
        {
            const int w = directDownload.getIdealWidth();
            x -= w;
            directDownload.setBounds (x, top + 9, w, metric::smallH);
        }
    }
    area.removeFromTop (bannersHeight());
    // 방 최소화 중: 아래에 막대가 붙고 위의 목록들만 57 줄어든다
    if (roomBar != nullptr) roomBar->setBounds (area.removeFromBottom (roomBarH));
    area.reduce (metric::padX, metric::padX);

    if (shell)
    {
        // 사이드바 안: 홈 쪽(HomePage) 또는 공개 방 쪽(머리말 필터·방 만들기 + 목록). 친구·친구 추가·내 오디오 카드는 숨긴다
        friendsCard.setVisible (false);
        addCard.setVisible (false);
        roomsCard.setVisible (false);
        audioCard.setVisible (false);
        homePage->setVisible (shellHome);
        roomViewport.setVisible (! shellHome);
        filterButton->setVisible (! shellHome);
        createRoom.setVisible (! shellHome);
        if (shellHome)
            homePage->setBounds (area.expanded (metric::padX));   // HomePage 가 디자인 여백 18 을 직접 둔다
        else
        {
            const int titleW = (int) std::ceil (textWidth (font (600, 16.0f), pageTitle));
            filterButton->setBounds (shellInset + metric::padX + titleW + 6, (headerLine() - 24) / 2, 24, 24);
            const int cw = createRoom.getIdealWidth();
            createRoom.setBounds (getWidth() - metric::padX - cw, (headerLine() - metric::buttonH) / 2, cw, metric::buttonH);
            roomViewport.setBounds (area.expanded (metric::padX - 12));   // 카드 목록 안쪽 여백 12 + 6 = 디자인 18
            if (! fold.isMoving (foldFriends)) layoutRooms();
        }
        if (modal != nullptr) modal->setBounds (getLocalBounds());
        if (confirm != nullptr) confirm->setBounds (getLocalBounds());
        if (popover != nullptr && notifications != nullptr)
        {
            popover->setContent (notifications.get(), notifications->getPreferredHeight());
            const int ph = popover->getPreferredHeight();
            popover->setBounds (9, getHeight() - 55 - ph, 340, ph);   // 프로필 줄 위 (디자인 left 9, bottom 55)
            popover->toFront (false);
        }
        repaint();
        return;
    }

    // 오른쪽: 친구 + 친구 추가 (사이드바 안이면 친구는 사이드바에 있어 숨긴다)
    friendsCard.setVisible (! shell);
    addCard.setVisible (! shell);
    const int sideW = plugin ? sideWidthPlugin : sideWidthApp;
    auto side = shell ? juce::Rectangle<int>() : area.removeFromRight (sideW);
    if (! shell) area.removeFromRight (metric::padX);
    if (! shell)
    {
        // 결과 문구: 입력 줄 아래 (디자인 padding 0 18px 12px, margin-top -6px)
        addNote.setText (addMessage);
        addNote.setVisible (addMessage.isNotEmpty());
        const int noteW = sideW - 2 - metric::padX * 2;
        const int noteH = addMessage.isNotEmpty() ? juce::jmax (15, addNote.getHeightForWidth (noteW)) : 0;
        const int addH = 48 + 1 + addRowH + (addMessage.isNotEmpty() ? noteH + 12 - 6 : 0);
        addCard.setBounds (side.removeFromBottom (addH));
        side.removeFromBottom (metric::gap);
        const float e = fold.value (foldFriends);
        if (! fold.isMoving (foldFriends))
        {
            laidFriends = {};
            friendsCard.setBounds (friendsOpen ? side : side.withHeight (48));
            friendsCard.setOpen (friendsOpen);
            friendsCard.setFoldProgress (e);
            friendViewport.setVisible (friendsOpen);
            friendViewport.setAlpha (1.0f);
            const auto fc = friendsCard.contentBounds();
            friendViewport.setBounds (fc);
            friendList->setSize (fc.getWidth(), juce::jmax (fc.getHeight(), friendList->preferredHeight()));
            friendList->resized();
        }
        else
        {
            // 접힌 48 과 펼친 크기 사이. 목록은 펼친 크기로 한 번만 배치하고 카드가 잘라 보인다. 완전히 접히면 숨긴다
            friendsCard.setBounds (side.withHeight (core::FoldLayout::blendSingle (48, side.getHeight(), e)));
            friendsCard.setOpen (friendsOpen);
            friendsCard.setFoldProgress (e);
            const juce::Point<int> size (side.getWidth(), side.getHeight());
            if (size != laidFriends)
            {
                laidFriends = size;
                const auto fc = friendsCard.contentBoundsFor (size.x, size.y);
                friendViewport.setBounds (fc);
                friendList->setSize (fc.getWidth(), juce::jmax (fc.getHeight(), friendList->preferredHeight()));
                friendList->resized();
            }
            friendViewport.setVisible (e > 0.0f);
            friendViewport.setAlpha (e);
        }

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
        if (! fold.isMoving (foldFriends)) layoutRooms();   // 친구 패널이 움직이는 동안 공개 방 크기는 그대로다
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
    if (confirm != nullptr) confirm->setBounds (getLocalBounds());
    if (popover != nullptr && notifications != nullptr)
    {
        const int h = popover->getPreferredHeight() - 0;
        popover->setContent (notifications.get(), notifications->getPreferredHeight());
        const int ph = juce::jmax (h, popover->getPreferredHeight());
        if (shell)   // 사이드바 안: 프로필 줄 위 (디자인 left 9, bottom 55)
            popover->setBounds (9, getHeight() - 55 - ph, 340, ph);
        else
            popover->setBounds (getWidth() - 12 - 340, headerLine() + 10, 340, ph);
    }
    if (! fold.isMoving (foldFriends)) repaint();   // 움직이는 동안은 옮겨진 카드 자리만 다시 그린다
}

void HomeScreen::previewFold (float e)
{
    friendsOpen = false;
    if (e < 0.0f) { fold.snap (foldFriends, false); return; }   // 비교용: 애니메이션 없이 접은 화면 (기존 레이아웃)
    fold.setOpen (foldFriends, false);
    fold.freeze (foldFriends, false, e);
}

} // namespace meon::ui
