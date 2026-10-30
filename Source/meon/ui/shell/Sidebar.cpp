#include "Sidebar.h"
#include "../../MeonTheme.h"
#include <map>

namespace meon::ui
{

namespace
{
    // 디자인 값 (홈·사이드바, activeStyle 회색)
    const juce::Colour sideBg     { 0xFFFFFFFFu };   // v2: #FFFFFF (예전 #FCFCFC)
    const juce::Colour activeBg   { 0xFFEFEFEFu };   // 선택된 메뉴·방
    const juce::Colour buttonHover { 0xFFF0F0F0u };  // 접기·+ 버튼 호버
    constexpr int profileH = 1 + 9 + 40 + 9, padX = 9, padY = 12, sectionGap = 12, rowGap = 2;

    juce::String nameOf (const core::UserPublic& u) { return u.nickname.value_or (u.username); }

    void drawIcon (juce::Graphics& g, icons::Id id, juce::Rectangle<float> box, float px, juce::Colour c)
    {
        icons::draw (g, id, box.withSizeKeepingCentre (px, px), c);
    }

    /** 빨간 점 (흰 테두리 1.5): 아이콘 오른쪽 위 */
    void drawDot (juce::Graphics& g, juce::Point<float> centre, float d, juce::Colour border)
    {
        const auto r = juce::Rectangle<float> (d, d).withCentre (centre);
        g.setColour (border);
        g.fillEllipse (r.expanded (1.5f));
        g.setColour (col::notify);
        g.fillEllipse (r);
    }
}

//==============================================================================
struct Sidebar::Body : public juce::Component
{
    explicit Body (Sidebar& s) : owner (s) { setRepaintsOnMouseActivity (false); }

    enum class Kind { label, nav, header, separator, room, empty, friendOn, friendOff, offHeader, iconButton, invite };
    struct Row
    {
        Kind kind;
        juce::Rectangle<int> r;
        juce::String text, sub;
        icons::Id icon = icons::Id::navHome;
        bool active = false, dot = false, open = true, plus = false;
        juce::String part;
        juce::Rectangle<int> plusR;
        std::function<void()> act, plusAct;
        float angle = 0.0f;               // 머리말 화살표 (펼침 0, 접힘 -90°): 섹션 높이와 같이 움직인다
        bool clipped = false;             // 접는 섹션 안: clip 밖은 그리지도 받지도 않는다
        juce::Rectangle<int> clip;
        int trimRight = 0;                // 오른쪽 "초대" 버튼 자리 (이름이 그 앞에서 줄어든다)
    };
    std::vector<Row> rows;
    int hotRow = -1;
    bool hotPlus = false;
    bool roomsOpen = true, friendsOpen = true, offlineOpen = false;   // 오프라인은 처음에 접힘

    Sidebar& owner;
    struct InviteButtons { std::unique_ptr<Button> no, ok; };
    std::map<juce::String, InviteButtons> inviteButtons;              // 받은 초대 카드 (거절·수락)
    std::map<juce::String, std::unique_ptr<Button>> friendInvites;    // 친구 줄 "초대" (방에 있을 때, 온라인 친구)
    SectionInfo secRooms, secFriends, secOffline;

    /** 접는 섹션 (디자인 grid-template-rows 1fr ↔ 0fr): 내용은 제자리, 보이는 높이 = 다 펼친 높이 × 진행값. 그 밖은 잘린다 */
    int clipSection (size_t first, int top, int full, int panel, SectionInfo& info)
    {
        const float e = owner.fold.value (panel);
        const int vis = juce::jlimit (0, juce::jmax (0, full), juce::roundToInt ((float) full * e));
        const juce::Rectangle<int> c (0, top, juce::jmax (1, getWidth() > 0 ? getWidth() : owner.currentWidth()), vis);
        for (size_t i = first; i < rows.size(); ++i)
        {
            auto& r = rows[i];
            r.clip = r.clipped ? r.clip.getIntersection (c) : c;
            r.clipped = true;
        }
        info = { vis, full, -juce::MathConstants<float>::halfPi * (1.0f - e) };
        return vis;
    }

    void rebuild()
    {
        rows.clear();
        juce::StringArray liveInvites, liveFriendInvites;
        std::vector<std::pair<juce::Component*, size_t>> clippedButtons;   // 섹션 밖으로 잘리면 숨긴다
        const auto& s = owner.core.store().state();
        const bool open = ! owner.shut();
        const int w = getWidth() > 0 ? getWidth() : owner.currentWidth();
        const int x = padX, rw = w - padX * 2;
        int y = padY;
        auto add = [&] (Row r) { rows.push_back (std::move (r)); };

        // 메뉴 (v2.1: "메뉴" 머리말 없음. 홈은 homeMenu 일 때만)
        auto nav = [&] (icons::Id id, const juce::String& t, bool active, std::function<void()> act)
        {
            Row r { Kind::nav, { x, y, rw, 32 }, t };
            r.icon = id; r.active = active; r.act = std::move (act);
            add (std::move (r));
            y += 32 + rowGap;
        };
        if (homeMenu)
            nav (icons::Id::navHome, TXT ("홈"), owner.page == Page::home, [this] { if (owner.onHome) owner.onHome(); });
        nav (icons::Id::navRooms, TXT ("공개 방"), owner.page == Page::rooms, [this] { if (owner.onRooms) owner.onRooms(); });
        nav (icons::Id::navHash, TXT ("코드로 입장"), false, [this] { if (owner.onJoinCode) owner.onJoinCode(); });
        y += sectionGap - rowGap;

        // 참여 중
        const bool joined = s.currentRoom.has_value();
        if (open)
        {
            Row h { Kind::header, { x, y, rw, 28 }, TXT ("참여 중") };
            h.open = roomsOpen;
            h.angle = -juce::MathConstants<float>::halfPi * (1.0f - owner.fold.value (owner.foldRooms));
            h.act = [this] { roomsOpen = ! roomsOpen; owner.syncSections(); rebuildAndLayout(); };
            if (! joined)
            {
                h.plus = true;
                h.plusR = { x + rw - 6 - 22, y + 3, 22, 22 };
                h.plusAct = [this] { if (owner.onCreateRoom) owner.onCreateRoom(); };
            }
            add (std::move (h));
            y += 28 + rowGap;
        }
        else
        {
            // v2.1: 구분선 margin -2px 9px 8px (좌우 9 안쪽, 위로 2 당김) + 목록 gap 2
            add ({ Kind::separator, { x + 9, y - 2, rw - 18, 1 } });
            y += -2 + 1 + 8 + rowGap;
        }
        {
            const int top = y;
            const size_t first = rows.size();
            if (joined)
            {
                const auto& room = *s.currentRoom;
                const int count = juce::jmax (room.memberCount, (int) room.members.size());
                Row r { Kind::room, { x, y, rw, 32 }, room.title, juce::String (count) + "/" + juce::String (room.capacity) };
                r.icon = icons::Id::door;
                r.active = owner.page == Page::room;
                // 입장 요청이 오면 방 아이콘에 점 (v2.1 최신: 그 방 화면을 보고 있으면 점 없음. 방 화면 윗줄 사람 아이콘에 있다)
                r.dot = s.amHost() && ! s.joinRequests.empty() && ! r.active;
                r.act = [this] { if (owner.onShowRoom) owner.onShowRoom(); };
                add (std::move (r));
                y += 32 + rowGap;
            }
            else if (open)
            {
                add ({ Kind::empty, { x, y, rw, 32 }, TXT ("참여 중인 방이 없어요") });
                y += 32 + rowGap;
            }
            else
            {
                // v2.1 접힌 상태: 방이 없으면 방 만들기 아이콘 (펼친 "참여 중 +" 와 같은 동작)
                Row b { Kind::iconButton, { x, y, rw, 32 }, TXT ("방 만들기") };   // 글자는 그리지 않는다 (devRow 로 찾는 이름)
                b.icon = icons::Id::doorPlus;
                b.act = [this] { if (owner.onCreateRoom) owner.onCreateRoom(); };
                add (std::move (b));
                y += 32 + rowGap;
            }
            y = top + clipSection (first, top, y - rowGap - top, owner.foldRooms, secRooms);
        }
        y += sectionGap;

        // 친구: 합주 중 → 온라인 → (접힌) 오프라인
        std::vector<const core::Friend*> on, off;
        for (auto& f : s.friends) if (f.status == "in_room") on.push_back (&f);
        for (auto& f : s.friends) if (f.status == "online") on.push_back (&f);
        for (auto& f : s.friends) if (f.status != "in_room" && f.status != "online") off.push_back (&f);
        if (open)
        {
            Row h { Kind::header, { x, y, rw, 28 }, TXT ("친구"), juce::String ((int) s.friends.size()) };
            h.open = friendsOpen;   // v2: 친구 요청 점은 여기가 아니라 내 프로필 아바타에
            h.angle = -juce::MathConstants<float>::halfPi * (1.0f - owner.fold.value (owner.foldFriends));
            h.act = [this] { friendsOpen = ! friendsOpen; owner.syncSections(); rebuildAndLayout(); };
            h.plus = true;
            h.plusR = { x + rw - 6 - 22, y + 3, 22, 22 };
            h.plusAct = [this] { if (owner.onAddFriend) owner.onAddFriend(); };
            add (std::move (h));
            y += 28 + rowGap;
        }
        else
        {
            // v2.1 접힌 상태: 친구 목록 대신 친구 추가 아이콘 (펼친 "친구 +" 와 같은 친구 추가 창)
            add ({ Kind::separator, { x + 9, y - 2, rw - 18, 1 } });
            y += -2 + 1 + 8 + rowGap;
            Row b { Kind::iconButton, { x, y, rw, 32 }, TXT ("친구 추가") };
            b.icon = icons::Id::userPlus;
            b.act = [this] { if (owner.onAddFriend) owner.onAddFriend(); };
            add (std::move (b));
            y += 32 + rowGap;
        }
        bool anyPending = false;
        {
            // 친구 목록 (디자인: 접힌 사이드바에서는 높이 0 으로 접힌다. 접는 동안은 줄이 남아 잘려 보인다)
            const int top = y;
            const size_t first = rows.size();
            if (open)
            {
                // 받은 초대: 친구 목록 위 카드 (홈·사이드바 디자인)
                for (auto& inv : s.invites)
                {
                    const int h = 1 + 9 + 34 + 9 + metric::smallH + 9 + 1;   // 82
                    Row r { Kind::invite, { x, y, rw, h }, inv.from.nickname.value_or (inv.from.username) + TXT ("님이 초대했어요"), inv.room.title };
                    r.part = inv.from.part.value_or ("other");
                    add (std::move (r));
                    liveInvites.add (inv.id);
                    auto& b = inviteButtons[inv.id];
                    if (b.ok == nullptr)
                    {
                        b.no = std::make_unique<Button> (Button::Kind::small, TXT ("거절"));
                        b.ok = std::make_unique<Button> (Button::Kind::smallPrimary, TXT ("수락"));
                        const auto id = inv.id;
                        b.no->onPress = [this, id] { if (owner.onDeclineInvite) owner.onDeclineInvite (id); };
                        b.ok->onPress = [this, id] { if (owner.onAcceptInvite) owner.onAcceptInvite (id); };
                        addChildComponent (*b.no);
                        addChildComponent (*b.ok);
                    }
                    const int by = y + 1 + 9 + 34 + 9;
                    const int ow = b.ok->getIdealWidth(), nw = b.no->getIdealWidth();
                    b.ok->setBounds (x + rw - 10 - ow, by, ow, metric::smallH);
                    b.no->setBounds (x + rw - 10 - ow - 6 - nw, by, nw, metric::smallH);
                    clippedButtons.push_back ({ b.ok.get(), rows.size() - 1 });
                    clippedButtons.push_back ({ b.no.get(), rows.size() - 1 });
                    y += h + 4 + rowGap;
                }
            }
            const juce::String myRoom = joined ? s.currentRoom->id : juce::String();
            for (auto* f : on)
            {
                Row r { Kind::friendOn, { x, y, rw, 32 }, nameOf (f->user) };
                r.part = f->user.part.value_or ("other");
                const bool jam = f->status == "in_room";
                const bool here = jam && f->room && f->room->id == myRoom;
                if (jam && ! here) r.sub = TXT ("합주 중");
                const auto user = f->user;
                r.act = [this, user] { if (owner.onOpenFriend) owner.onOpenFriend (user); };
                // 방에 있을 때 온라인(다른 방에서 합주 중이 아닌) 친구: 줄 오른쪽 "초대" (방 화면에서 뺀 친구 패널의 초대).
                // 누르면 3초 동안 "취소", 그 뒤 "초대됨" (디자인 screen=방). 서버가 초대할 수 있다고 한 친구만 (can_invite)
                if (open && joined && f->status == "online" && f->canInvite)
                {
                    const auto id = f->user.id;
                    liveFriendInvites.add (id);
                    auto& b = friendInvites[id];
                    if (b == nullptr)
                    {
                        b = std::make_unique<Button> (Button::Kind::small, TXT ("초대"));
                        b->onPress = [this, id]
                        {
                            auto& home = owner.core.home();
                            using P = core::HomeController::InviteState::Phase;
                            const auto st = home.inviteState (id).phase;
                            if (st == P::pending) home.cancelInvite (id);
                            else if (st == P::none)
                            {
                                juce::Component::SafePointer<Sidebar> safe (&owner);
                                home.invite (id, [safe] (core::Result<core::InviteSent>) { if (safe != nullptr) safe->timerCallback(); });   // 실패 토스트는 AppRoot
                            }
                            juce::Component::SafePointer<Sidebar> safe (&owner);
                            juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->timerCallback(); });
                        };
                        addChildComponent (*b);
                    }
                    using P = core::HomeController::InviteState::Phase;
                    const auto st = owner.core.home().inviteState (id);
                    b->setPending (st.phase == P::pending);
                    b->setDimmed (st.phase == P::sent);
                    b->setButtonText (st.phase == P::pending ? TXT ("취소") : (st.phase == P::sent ? TXT ("초대됨") : TXT ("초대")));
                    if (st.phase == P::pending)
                    {
                        anyPending = true;
                        const auto left = std::max<juce::int64> (1, (st.deliverAtMs - owner.core.scheduler().nowMs() + 999) / 1000);
                        b->setTooltip (juce::String (left) + TXT ("초 뒤 초대가 가요"));
                    }
                    else
                        b->setTooltip (st.phase == P::sent ? juce::String() : TXT ("방에 초대"));
                    const int bw = b->getIdealWidth();
                    b->setBounds (x + rw - 9 - bw, y + (32 - metric::smallH) / 2, bw, metric::smallH);
                    r.trimRight = bw + 9;
                    r.sub = {};
                    clippedButtons.push_back ({ b.get(), rows.size() });
                }
                add (std::move (r));
                y += 32 + rowGap;
            }
            if (open)
            {
                y += 4;   // 오프라인 머리말 margin-top 4
                Row oh { Kind::offHeader, { x, y, rw, 28 }, TXT ("오프라인"), juce::String ((int) off.size()) };
                oh.open = offlineOpen;
                oh.angle = -juce::MathConstants<float>::halfPi * (1.0f - owner.fold.value (owner.foldOffline));
                oh.act = [this] { offlineOpen = ! offlineOpen; owner.syncSections(); rebuildAndLayout(); };
                add (std::move (oh));
                y += 28 + rowGap;
            }
            {
                const int otop = y;
                const size_t ofirst = rows.size();
                for (auto* f : off)
                {
                    Row r { Kind::friendOff, { x, y, rw, 32 }, nameOf (f->user) };
                    r.part = f->user.part.value_or ("other");
                    const auto user = f->user;
                    r.act = [this, user] { if (owner.onOpenFriend) owner.onOpenFriend (user); };
                    add (std::move (r));
                    y += 32 + rowGap;
                }
                const int ofull = off.empty() ? 0 : y - rowGap - otop;
                y = otop + clipSection (ofirst, otop, ofull, owner.foldOffline, secOffline);
            }
            y = top + clipSection (first, top, y - top, owner.foldFriends, secFriends);
        }

        // 사라진 초대·친구의 버튼은 지우고, 섹션 밖으로 잘리면 숨긴다 (움직이는 동안 반쯤 걸친 버튼도 숨긴다)
        for (auto it = inviteButtons.begin(); it != inviteButtons.end();)
        {
            if (! liveInvites.contains (it->first)) { it = inviteButtons.erase (it); continue; }
            it->second.no->setVisible (false);
            it->second.ok->setVisible (false);
            ++it;
        }
        for (auto it = friendInvites.begin(); it != friendInvites.end();)
        {
            if (! liveFriendInvites.contains (it->first)) { it = friendInvites.erase (it); continue; }
            it->second->setVisible (false);
            ++it;
        }
        for (auto& [c, rowIndex] : clippedButtons)
        {
            const auto& row = rows[rowIndex];
            c->setVisible (! row.clipped || row.clip.contains (c->getBounds()));
        }
        if (anyPending) { if (! owner.isTimerRunning()) owner.startTimer (250); }
        else owner.stopTimer();
        preferredH = y + padY;
    }

    int preferredH = 0;

    // (목록이 다시 만들어질 때 지운 버튼의 콜백이 남지 않게 버튼은 map 이 소유한다)
    void rebuildAndLayout()
    {
        rebuild();
        owner.resized();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        // 메뉴·방·친구 글자는 접기·펼치기 동안 같이 흐려지고 진해진다 (디자인 labelOp). 움직이는 중에는 말줄임 없이 잘린다
        const float alpha = owner.labelAlpha();
        const bool ellipsis = ! owner.animating();
        for (int i = 0; i < (int) rows.size(); ++i)
        {
            const auto& r = rows[(size_t) i];
            if (r.clipped && r.clip.getIntersection (r.r).isEmpty()) continue;
            juce::Graphics::ScopedSaveState ss (g);
            if (r.clipped) g.reduceClipRegion (r.clip);
            const auto rf = r.r.toFloat();
            const bool hot = i == hotRow && ! hotPlus;
            switch (r.kind)
            {
                case Kind::label:
                    g.setColour (col::inkSub);
                    g.setFont (font (500, 12.0f));
                    g.drawText (r.text, r.r.withTrimmedLeft (9), juce::Justification::centredLeft, false);
                    break;
                case Kind::nav: case Kind::room: case Kind::iconButton:
                {
                    if (r.active) { g.setColour (activeBg); g.fillRoundedRectangle (rf, 6.0f); }
                    else if (hot) { g.setColour (col::hoverBg); g.fillRoundedRectangle (rf, 6.0f); }
                    const auto ib = juce::Rectangle<float> (rf.getX() + 9.0f, rf.getCentreY() - 10.0f, 20.0f, 20.0f);
                    const auto ink = r.kind == Kind::nav && r.active ? col::ink : col::inkSub;
                    drawIcon (g, r.icon, ib, 18.0f, ink);
                    if (r.dot) drawDot (g, { ib.getRight() - 3.0f, ib.getY() + 4.0f }, 5.0f, r.active ? activeBg : (hot ? col::hoverBg : sideBg));   // 빨강 5, 테두리 포함 8
                    if (alpha > 0.001f && r.kind != Kind::iconButton)
                    {
                        auto t = r.r.withTrimmedLeft (9 + 20 + 9).withTrimmedRight (9);
                        if (t.getWidth() <= 0) break;
                        if (r.kind == Kind::room && r.sub.isNotEmpty())
                        {
                            const auto cf = font (400, 12.0f);
                            const int cw = (int) std::ceil (tabularWidth (cf, r.sub)) + 1;
                            g.setColour (col::inkSub.withMultipliedAlpha (alpha));
                            g.setFont (cf);
                            g.drawText (r.sub, t.removeFromRight (cw), juce::Justification::centredRight, false);
                            t.removeFromRight (9);
                        }
                        g.setColour (col::ink.withMultipliedAlpha (alpha));
                        g.setFont (font (r.active ? 600 : 400, 14.0f));
                        g.drawText (r.text, t, juce::Justification::centredLeft, ellipsis);
                    }
                    break;
                }
                case Kind::header: case Kind::offHeader:
                {
                    const bool headerHot = i == hotRow && ! hotPlus;
                    const auto ink = headerHot ? col::ink : col::inkSub;
                    const auto f5 = font (500, 12.0f), f4 = font (400, 12.0f);
                    float tx = (float) r.r.getX() + 9.0f;
                    const float cy = rf.getCentreY();
                    g.setColour (ink);
                    g.setFont (f5);
                    const float w1 = textWidth (f5, r.text);
                    g.drawText (r.text, juce::Rectangle<float> (tx, rf.getY(), w1 + 2.0f, rf.getHeight()), juce::Justification::centredLeft, false);
                    tx += w1;
                    if (r.sub.isNotEmpty())
                    {
                        tx += 3.0f;
                        g.setFont (f4);
                        const float w2 = tabularWidth (f4, r.sub);
                        drawTabular (g, f4, r.sub, tx, rf.getY(), rf.getHeight());
                        tx += w2;
                    }
                    if (r.dot)
                    {
                        tx += 2.0f;
                        g.setColour (col::notify);
                        g.fillEllipse (tx, cy - 3.0f, 6.0f, 6.0f);
                        tx += 6.0f + 1.0f;
                    }
                    tx += 3.0f;
                    // 접힘 화살표 14 (24 그리드 선 1.6): 펼침 아래(0), 접힘 오른쪽(-90°). 섹션 높이와 같은 시간·곡선으로 돈다
                    icons::draw (g, icons::Id::chevronDown24, { tx, cy - 7.0f, 14.0f, 14.0f }, ink, r.angle);
                    if (r.plus)
                    {
                        const bool ph = i == hotRow && hotPlus;
                        if (ph) { g.setColour (buttonHover); g.fillRoundedRectangle (r.plusR.toFloat(), 5.0f); }
                        drawIcon (g, icons::Id::plus, r.plusR.toFloat(), 16.0f, ph ? col::ink : col::inkSub);
                    }
                    break;
                }
                case Kind::separator:
                    g.setColour (col::line);
                    g.fillRect (r.r);
                    break;
                case Kind::invite:
                {
                    g.setColour (col::white);
                    g.fillRoundedRectangle (rf, 6.0f);
                    g.setColour (col::line);
                    g.drawRoundedRectangle (rf.reduced (0.5f), 6.0f, 1.0f);
                    // v2.1: 안쪽 10 (테두리 1 + 9), 아바타 줄 34 (아바타 32 가운데), 제목 17 · 간격 2 · 보조 16
                    const auto av = juce::Rectangle<float> (rf.getX() + 10.0f, rf.getY() + 11.0f, 32.0f, 32.0f);
                    Avatar::paintCircle (g, av, r.part, 16.0f, col::ink, true);
                    const auto t = juce::Rectangle<float> (av.getRight() + 9.0f, rf.getY() + 10.0f, rf.getRight() - 10.0f - av.getRight() - 9.0f, 34.0f);
                    g.setColour (col::ink);
                    g.setFont (font (600, 13.0f));
                    g.drawText (r.text, t.withHeight (17.0f), juce::Justification::centredLeft, true);
                    g.setColour (col::inkSub);
                    g.setFont (font (400, 12.0f));
                    g.drawText (r.sub, t.withTrimmedTop (17.0f + 2.0f).withHeight (16.0f), juce::Justification::centredLeft, true);
                    break;
                }
                case Kind::empty:
                    g.setColour (col::inkFaint);
                    g.setFont (font (400, 13.0f));
                    g.drawText (r.text, r.r.withTrimmedLeft (9), juce::Justification::centredLeft, true);
                    break;
                case Kind::friendOn: case Kind::friendOff:
                {
                    if (hot) { g.setColour (col::hoverBg); g.fillRoundedRectangle (rf, 6.0f); }
                    const bool off = r.kind == Kind::friendOff;
                    const auto av = juce::Rectangle<float> (rf.getX() + 9.0f, rf.getCentreY() - 10.0f, 20.0f, 20.0f);
                    {
                        juce::Graphics::ScopedSaveState s2 (g);
                        if (off) g.beginTransparencyLayer (0.6f);
                        Avatar::paintCircle (g, av, r.part, 12.0f, off ? col::inkSub : col::ink, true);
                        if (off) g.endTransparencyLayer();
                    }
                    if (! off)
                    {
                        // 상태 점 8 (테두리 1.5, 배경색), 아바타 오른쪽·아래로 1 (v2.1)
                        const auto d = juce::Rectangle<float> (av.getRight() + 1.0f - 8.0f, av.getBottom() + 1.0f - 8.0f, 8.0f, 8.0f);
                        g.setColour (hot ? col::hoverBg : sideBg);
                        g.fillEllipse (d);
                        g.setColour (col::live);
                        g.fillEllipse (d.reduced (1.5f));
                    }
                    auto t = r.r.withTrimmedLeft (9 + 20 + 9).withTrimmedRight (9);
                    if (r.trimRight > 0) t = t.withTrimmedRight (r.trimRight);
                    if (r.sub.isNotEmpty())
                    {
                        const auto sf = font (400, 12.0f);
                        const int sw = (int) std::ceil (textWidth (sf, r.sub)) + 1;
                        g.setColour (col::inkSub.withMultipliedAlpha (alpha));
                        g.setFont (sf);
                        g.drawText (r.sub, t.removeFromRight (sw), juce::Justification::centredRight, false);
                        t.removeFromRight (6);
                    }
                    g.setColour ((off ? col::inkSub : col::ink).withMultipliedAlpha (alpha));
                    g.setFont (font (400, 14.0f));
                    g.drawText (r.text, t, juce::Justification::centredLeft, ellipsis);
                    break;
                }
            }
        }
    }

    int rowAt (juce::Point<int> p, bool& onPlus) const
    {
        onPlus = false;
        for (int i = 0; i < (int) rows.size(); ++i)
        {
            const auto& r = rows[(size_t) i];
            if (r.clipped && ! r.clip.contains (p)) continue;
            if (r.plus && r.plusR.contains (p)) { onPlus = true; return i; }
            if (! r.act) continue;
            if (r.kind == Kind::header || r.kind == Kind::offHeader)
            {
                // 머리말은 글자·화살표 자리만 누를 수 있다 (디자인: 버튼이 글자 폭)
                if (r.r.withWidth (juce::jmin (r.r.getWidth(), 140)).contains (p)) return i;
                continue;
            }
            if (r.r.contains (p)) return i;
        }
        return -1;
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        bool plus = false;
        const int i = rowAt (e.getPosition(), plus);
        if (i != hotRow || plus != hotPlus) { hotRow = i; hotPlus = plus; repaint(); }
        setMouseCursor (i >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }
    void mouseExit (const juce::MouseEvent&) override { hotRow = -1; hotPlus = false; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mouseWasClicked()) return;
        bool plus = false;
        const int i = rowAt (e.getPosition(), plus);
        if (i < 0) return;
        auto act = plus ? rows[(size_t) i].plusAct : rows[(size_t) i].act;
        // 누른 행이 다시 만들어질 수 있으므로 다음 메시지에서
        juce::Component::SafePointer<Body> safe (this);
        juce::MessageManager::callAsync ([safe, act] { if (safe != nullptr && act) act(); });
    }
};

//==============================================================================
Sidebar::Sidebar (core::MeonCore& c) : core (c)
{
    setOpaque (true);
    body = std::make_unique<Body> (*this);
    viewport.setViewedComponent (body.get(), false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (6);
    addAndMakeVisible (viewport);
    core.store().addListener (this);
}

Sidebar::~Sidebar()
{
    stopTimer();
    core.store().removeListener (this);
}

int Sidebar::currentWidth() const
{
    return juce::roundToInt ((float) closedW + (float) (openW - closedW) * fold.value (foldPanel));
}

void Sidebar::foldFrame()
{
    // 폭·제목 줄 여백이 바뀌면 AppRoot 가 다시 배치한다 (섹션만 움직일 때는 사이드바 안만)
    const float e = openProgress();
    if (! laying && onLayoutChanged && e != notifiedE)
    {
        notifiedE = e;
        onLayoutChanged();
    }
    resized();
    repaint();
}

void Sidebar::syncSections()
{
    // 디자인: 참여 중 = (펼친 사이드바 && 접은 섹션) 일 때만 0fr, 친구 = 펼친 사이드바 && 펼친 섹션, 오프라인 = 펼친 사이드바 && 펼친 섹션
    fold.setOpen (foldRooms, shut() || body->roomsOpen);
    fold.setOpen (foldFriends, ! shut() && body->friendsOpen);
    fold.setOpen (foldOffline, ! shut() && body->offlineOpen);
}

void Sidebar::setCollapsed (bool b)
{
    if (b == collapsed) return;
    collapsed = b;
    body->hotRow = -1;
    // 배치(펼친 줄·접힌 줄)는 바로 바뀌고, 폭·글자 불투명도·섹션 높이가 움직인다 (디자인: open 은 바로, width·opacity·rows 는 transition)
    fold.setOpen (foldPanel, ! shut());   // 프레임마다 onLayoutChanged → AppRoot::resized
    syncSections();
}

void Sidebar::setForcedCollapsed (bool b)
{
    // AppRoot::resized 가 부른다: 그 안에서는 다시 배치하지 않는다 (부른 쪽이 곧 currentWidth 를 읽는다). 다음 프레임부터는 보통대로
    if (b == forced) return;
    forced = b;
    body->hotRow = -1;
    hot = 0;
    const juce::ScopedValueSetter<bool> guard (laying, true);
    fold.setOpen (foldPanel, ! shut());
    syncSections();
}

void Sidebar::setChrome (Chrome c)
{
    if (c == chrome) return;
    chrome = c;
    resized();
    repaint();
}

void Sidebar::previewFold (bool open, float e)
{
    collapsed = ! open;
    fold.freeze (foldPanel, open, e);
    // 사이드바와 같이 움직이는 섹션 (목표가 바뀌는 것만): 같은 진행값에 멈춘다
    if (! body->roomsOpen) fold.freeze (foldRooms, ! open, 1.0f - e); else fold.snap (foldRooms, true);
    if (body->friendsOpen) fold.freeze (foldFriends, open, e); else fold.snap (foldFriends, false);
    if (body->offlineOpen) fold.freeze (foldOffline, open, e); else fold.snap (foldOffline, false);
    if (onLayoutChanged) onLayoutChanged();
    resized();
    repaint();
}

juce::Rectangle<int> Sidebar::devRow (const juce::String& text) const
{
    for (auto& r : body->rows)
        if ((r.kind == Body::Kind::nav || r.kind == Body::Kind::room || r.kind == Body::Kind::iconButton
             || r.kind == Body::Kind::friendOn || r.kind == Body::Kind::friendOff) && r.text == text
            && ! (r.clipped && ! r.clip.contains (r.r)))
            return getLocalArea (body.get(), r.r);
    return {};
}

Button* Sidebar::devInviteButton (const juce::String& userId) const
{
    auto it = body->friendInvites.find (userId);
    return it != body->friendInvites.end() ? it->second.get() : nullptr;
}

Sidebar::SectionInfo Sidebar::devSection (const juce::String& name) const
{
    if (name == "rooms") return body->secRooms;
    if (name == "friends") return body->secFriends;
    return body->secOffline;
}

juce::Rectangle<int> Sidebar::devSectionHeader (const juce::String& text) const
{
    for (auto& r : body->rows)
        if ((r.kind == Body::Kind::header || r.kind == Body::Kind::offHeader) && r.text == text)
            return getLocalArea (body.get(), r.r);
    return {};
}

void Sidebar::setPage (Page p)
{
    if (p == page) return;
    page = p;
    body->rebuild();
    body->repaint();
}

void Sidebar::setProfileMenuOpen (bool b)
{
    profileMenuOpen = b;
    repaint (profileBounds());
}

void Sidebar::stateChanged (core::StateKey k)
{
    using K = core::StateKey;
    switch (k)
    {
        case K::friends: case K::friendRequests: case K::currentRoom: case K::joinRequests: case K::me: case K::invites:
            body->rebuild();
            resized();
            body->repaint();
            repaint();
            break;
        default: break;
    }
}

void Sidebar::timerCallback()
{
    // 초대 "취소" 3초 → "초대됨", 남은 초 (디자인: 250ms 마다)
    body->rebuild();
    body->repaint();
}

juce::Rectangle<int> Sidebar::toggleArea() const
{
    if (forced) return {};   // 잠시 접힌 동안은 펼칠 자리가 없다
    switch (chrome)
    {
        case Chrome::macWindow: return { 84, 10, 32, 32 };   // 신호등(20~72) 오른쪽 (디자인 toggleL 84, 펼침·접힘 같음)
        case Chrome::flat:      return { 12, 10, 32, 32 };   // 디자인 fullscreen 켜짐: toggleL 12
        case Chrome::legacy:    break;
    }
    return shut() ? juce::Rectangle<int> (12, 10, 32, 32) : juce::Rectangle<int> (getWidth() - 1 - 12 - 32, 10, 32, 32);   // 오른쪽 선 1 안쪽에서 12
}

int Sidebar::headerInset() const
{
    // 디자인 hdPL: 맥 창 모드에서 접히면 72 (신호등·접기 버튼이 본문 제목 줄 위로 온다), 그 밖은 18
    if (chrome != Chrome::macWindow) return metric::padX;
    return juce::roundToInt (18.0f + (72.0f - 18.0f) * (1.0f - openProgress()));
}

int Sidebar::logoRowH() const
{
    // 디자인 logoRowH: 맥 창 모드 + 펼침이면 32 (로고가 신호등 아래 줄), 접히면 0. 같은 시간·곡선으로 줄어든다
    return chrome == Chrome::macWindow ? juce::roundToInt (32.0f * openProgress()) : 0;
}

juce::Rectangle<int> Sidebar::profileBounds() const
{
    return { padX, getHeight() - profileH + 1 + 9, getWidth() - 1 - padX * 2, 40 };   // 오른쪽 선 1 안쪽 (펼침 221, 접힘 37)
}

void Sidebar::resized()
{
    auto area = getLocalBounds().withTrimmedRight (1);
    area.removeFromTop (headerH + logoRowH());
    area.removeFromBottom (profileH);
    viewport.setBounds (area);
    body->setSize (area.getWidth(), 10);
    body->rebuild();
    body->setSize (area.getWidth(), juce::jmax (area.getHeight(), body->preferredH));
}

void Sidebar::paint (juce::Graphics& g)
{
    g.fillAll (sideBg);
    g.setColour (col::line);
    // 오른쪽 선: 맥 창 모드에서 접히면 52 부터 (맨 윗줄이 본문 제목 줄과 이어진다, 디자인 divTop)
    const int divTop = chrome == Chrome::macWindow && shut() ? headerH : 0;
    g.fillRect (getWidth() - 1, divTop, 1, getHeight() - divTop);
    // 맨 윗줄 아래 선: 플러그인은 늘, 그 밖은 접혔을 때만 (펼치면 로고·메뉴와 이어진다, 디자인 topLine)
    if (chrome == Chrome::legacy || shut())
        g.fillRect (0, headerH - 1, divTop > 0 ? getWidth() : getWidth() - 1, 1);
    g.fillRect (0, getHeight() - profileH, getWidth() - 1, 1);

    // 내 프로필 줄
    const auto pb = profileBounds().toFloat();
    if (profileMenuOpen) { g.setColour (activeBg); g.fillRoundedRectangle (pb, 6.0f); }
    else if (hot == 2) { g.setColour (col::hoverBg); g.fillRoundedRectangle (pb, 6.0f); }
    const auto& me = core.store().state().me;
    const juce::String part = me ? me->part.value_or ("other") : juce::String ("other");
    const auto av = juce::Rectangle<float> (pb.getX() + 5.0f, pb.getCentreY() - 14.0f, 28.0f, 28.0f);
    Avatar::paintCircle (g, av, part, 16.0f, col::ink, true);
    // v2: 친구 요청·초대가 있으면 내 프로필 아바타에 점 (지름 8, 흰 테두리 1.5, 아바타 오른쪽 위 top -1 right -2)
    {
        const auto& st = core.store().state();
        if (! st.friendRequests.empty() || ! st.invites.empty())
            drawDot (g, { av.getX() + 24.0f, av.getY() + 5.0f }, 6.5f, profileMenuOpen ? activeBg : (hot == 2 ? col::hoverBg : sideBg));
    }
    if (const float alpha = labelAlpha(); alpha > 0.001f && pb.getRight() - av.getRight() > 30.0f)
    {
        // v2.1: 안쪽 5, 아바타 28 · 9 · 글자(이름 15, 간격 1, 파트 14) · 9 · 화살표 20 · 오른쪽 2
        const float tx = av.getRight() + 9.0f;
        const float cx = pb.getRight() - 5.0f - 2.0f - 20.0f;
        const float tw = cx - 9.0f - tx;
        const bool ellipsis = ! animating();
        g.setColour (col::ink.withMultipliedAlpha (alpha));
        g.setFont (font (600, 13.0f));
        g.drawText (me ? me->nickname.value_or (me->username) : juce::String(), juce::Rectangle<float> (tx, pb.getCentreY() - 15.0f, tw, 15.0f),
                    juce::Justification::centredLeft, ellipsis);
        g.setColour (col::inkSub.withMultipliedAlpha (alpha));
        g.setFont (font (400, 12.0f));
        g.drawText (icons::partLabel (part), juce::Rectangle<float> (tx, pb.getCentreY() + 1.0f, tw, 14.0f), juce::Justification::centredLeft, ellipsis);
        drawIcon (g, icons::Id::chevronUpDown, juce::Rectangle<float> (cx, pb.getCentreY() - 10.0f, 20.0f, 20.0f), 14.0f,
                  col::inkSub.withMultipliedAlpha (alpha));
    }
}

void Sidebar::paintOverChildren (juce::Graphics& g)
{
    // 로고 (높이 24). 디자인 A안: 맥 창 모드 (18, 56) 신호등 아래 줄, 신호등 없음 (56, 14) 접기 버튼 오른쪽. 접는 동안 흐려진다 (labelOp).
    // 디자인처럼 메뉴 위에 그린다 (접는 동안 메뉴가 로고 줄로 올라온다)
    juce::Point<float> at { 18.0f, 14.0f };
    float alpha = labelAlpha();
    switch (chrome)
    {
        case Chrome::macWindow: at = { 18.0f, 56.0f }; break;
        case Chrome::flat:      at = { 56.0f, 14.0f }; break;
        case Chrome::legacy:    alpha = shut() ? 0.0f : 1.0f; break;   // 플러그인: 예전대로 펼쳤을 때만 바로
    }
    if (alpha <= 0.001f) return;
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (0, 0, getWidth() - 1, getHeight());
    meon::drawLogo (g, juce::Rectangle<float> (at.x, at.y, 140.0f, 24.0f), 24.0f, col::ink.withMultipliedAlpha (alpha), juce::Justification::centredLeft);
}

void Sidebar::mouseMove (const juce::MouseEvent& e)
{
    const int h = profileBounds().contains (e.getPosition()) ? 2 : 0;
    if (h != hot) { hot = h; repaint(); }
    setMouseCursor (h != 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void Sidebar::mouseExit (const juce::MouseEvent&)
{
    if (hot != 0) { hot = 0; repaint(); }
}

void Sidebar::mouseDown (const juce::MouseEvent& e)
{
    // 맨 윗줄 (맥 창 모드: 끌면 창이 옮겨지고 더블클릭은 시스템 설정대로. AppRoot 가 모드를 본다)
    if (e.y < headerH && onTitleBarMouseDown) onTitleBarMouseDown();
}

void Sidebar::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mouseWasClicked()) return;
    if (profileBounds().contains (e.getPosition()) && onProfileMenu)
        onProfileMenu();
}

//==============================================================================
SidebarToggle::SidebarToggle()
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setOpen (true);
}

void SidebarToggle::setOpen (bool open)
{
    setTooltip (open ? TXT ("사이드바 접기") : TXT ("사이드바 펼치기"));
}

void SidebarToggle::setCovered (bool c)
{
    if (c == covered) return;
    covered = c;
    // 덮개 아래면 누름은 덮개(모달 바깥 누름)로 간다
    setInterceptsMouseClicks (! c, false);
    if (c) hot = false;
    repaint();
}

void SidebarToggle::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    if (hot && ! covered) { g.setColour (buttonHover); g.fillRoundedRectangle (r, 6.0f); }
    // 덮개 아래: 아래 판은 이미 덮개 색이 칠해져 있다. 아이콘만 덮개를 겹친 색으로
    auto ink = hot ? col::ink : col::inkSub;
    if (covered) ink = ink.overlaidWith (col::overlay);
    drawIcon (g, icons::Id::sidebar, r, 18.0f, ink);
}

void SidebarToggle::mouseEnter (const juce::MouseEvent&) { if (! covered) { hot = true; repaint(); } }
void SidebarToggle::mouseExit (const juce::MouseEvent&)  { if (hot) { hot = false; repaint(); } }

void SidebarToggle::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mouseWasClicked() || covered || ! onPress) return;
    juce::MessageManager::callAsync (onPress);
}

//==============================================================================
ProfileMenu::ProfileMenu (int n) : count (n)
{
    setInterceptsMouseClicks (true, false);
}

void ProfileMenu::paint (juce::Graphics& g)
{
    const auto b = box.toFloat();
    g.setColour (col::white);
    g.fillRoundedRectangle (b, 6.0f);
    g.setColour (col::field);
    g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.0f);
    for (int i = 0; i < 2; ++i)
    {
        const auto r = item (i).toFloat();
        if (i == hot) { g.setColour (col::hoverBg); g.fillRoundedRectangle (r, 5.0f); }
        const auto ib = juce::Rectangle<float> (r.getX() + 9.0f, r.getCentreY() - 10.0f, 20.0f, 20.0f);
        icons::draw (g, i == 0 ? icons::Id::bell : icons::Id::gearHeader, ib.withSizeKeepingCentre (18.0f, 18.0f), col::inkSub);
        g.setColour (col::ink);
        g.setFont (font (400, 14.0f));
        g.drawText (i == 0 ? TXT ("알림") : TXT ("설정"), r.withTrimmedLeft (9.0f + 20.0f + 9.0f), juce::Justification::centredLeft, false);
        if (i == 0 && count > 0)
        {
            const auto bf = font (600, 10.0f);
            const auto t = juce::String (count);
            const float w = juce::jmax (16.0f, textWidth (bf, t) + 8.0f);
            const juce::Rectangle<float> badge (r.getRight() - 9.0f + 2.0f - w, r.getCentreY() - 8.0f, w, 16.0f);
            g.setColour (col::notify);
            g.fillRoundedRectangle (badge, 4.0f);
            g.setColour (col::white);
            g.setFont (bf);
            g.drawText (t, badge, juce::Justification::centred, false);
        }
    }
}

void ProfileMenu::mouseMove (const juce::MouseEvent& e)
{
    int h = -1;
    for (int i = 0; i < 2; ++i) if (item (i).contains (e.getPosition())) h = i;
    if (h != hot) { hot = h; repaint(); }
    setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void ProfileMenu::mouseExit (const juce::MouseEvent&)
{
    if (hot != -1) { hot = -1; repaint(); }
}

void ProfileMenu::mouseUp (const juce::MouseEvent& e)
{
    std::function<void()> act;
    if (item (0).contains (e.getPosition())) act = onNotifications;
    else if (item (1).contains (e.getPosition())) act = onSettings;
    else if (! box.contains (e.getPosition())) act = onDismiss;
    if (act) juce::MessageManager::callAsync (act);
}

} // namespace meon::ui
