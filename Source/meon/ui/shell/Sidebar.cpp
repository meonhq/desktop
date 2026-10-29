#include "Sidebar.h"
#include "../../MeonTheme.h"

namespace meon::ui
{

namespace
{
    // 디자인 값 (홈·사이드바, activeStyle 회색)
    const juce::Colour sideBg     { 0xFFFCFCFCu };
    const juce::Colour activeBg   { 0xFFEFEFEFu };   // 선택된 메뉴·방
    const juce::Colour buttonHover { 0xFFF0F0F0u };  // 접기·+ 버튼 호버
    constexpr int headerH = 52, profileH = 1 + 9 + 40 + 9, padX = 9, padY = 12, sectionGap = 12, rowGap = 2;

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

    enum class Kind { label, nav, header, separator, room, empty, friendOn, friendOff, offHeader, iconButton };
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
    };
    std::vector<Row> rows;
    int hotRow = -1;
    bool hotPlus = false;
    bool roomsOpen = true, friendsOpen = true, offlineOpen = false;   // 오프라인은 처음에 접힘

    Sidebar& owner;

    void rebuild()
    {
        rows.clear();
        const auto& s = owner.core.store().state();
        const bool open = ! owner.collapsed;
        const int w = getWidth() > 0 ? getWidth() : owner.currentWidth();
        const int x = padX, rw = w - padX * 2;
        int y = padY;
        auto add = [&] (Row r) { rows.push_back (std::move (r)); };

        // 메뉴
        if (open) { add ({ Kind::label, { x, y, rw, 28 }, TXT ("메뉴") }); y += 28 + rowGap; }
        auto nav = [&] (icons::Id id, const juce::String& t, bool active, std::function<void()> act)
        {
            Row r { Kind::nav, { x, y, rw, 32 }, t };
            r.icon = id; r.active = active; r.act = std::move (act);
            add (std::move (r));
            y += 32 + rowGap;
        };
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
            h.act = [this] { roomsOpen = ! roomsOpen; rebuildAndLayout(); };
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
            add ({ Kind::separator, { x, y + 6, rw, 1 } });
            y += 6 + 1 + 8;
        }
        if (! open || roomsOpen)
        {
            if (joined)
            {
                const auto& room = *s.currentRoom;
                const int count = juce::jmax (room.memberCount, (int) room.members.size());
                Row r { Kind::room, { x, y, rw, 32 }, room.title, juce::String (count) + "/" + juce::String (room.capacity) };
                r.icon = icons::Id::door;
                r.active = owner.page == Page::room;
                r.dot = s.amHost() && ! s.joinRequests.empty();   // 입장 요청이 오면 방 아이콘에 점
                r.act = [this] { if (owner.onShowRoom) owner.onShowRoom(); };
                add (std::move (r));
                y += 32 + rowGap;
            }
            else if (open)
            {
                add ({ Kind::empty, { x, y, rw, 32 }, TXT ("참여 중인 방이 없어요") });
                y += 32 + rowGap;
            }
        }
        y += sectionGap - rowGap;

        // 친구: 합주 중 → 온라인 → (접힌) 오프라인
        std::vector<const core::Friend*> on, off;
        for (auto& f : s.friends) if (f.status == "in_room") on.push_back (&f);
        for (auto& f : s.friends) if (f.status == "online") on.push_back (&f);
        for (auto& f : s.friends) if (f.status != "in_room" && f.status != "online") off.push_back (&f);
        const bool reqDot = ! s.friendRequests.empty();
        if (open)
        {
            Row h { Kind::header, { x, y, rw, 28 }, TXT ("친구"), juce::String ((int) s.friends.size()) };
            h.open = friendsOpen;
            h.dot = reqDot;
            h.act = [this] { friendsOpen = ! friendsOpen; rebuildAndLayout(); };
            h.plus = true;
            h.plusR = { x + rw - 6 - 22, y + 3, 22, 22 };
            h.plusAct = [this] { if (owner.onAddFriend) owner.onAddFriend(); };
            add (std::move (h));
            y += 28 + rowGap;
        }
        else
        {
            add ({ Kind::separator, { x, y + 6, rw, 1 } });
            y += 6 + 1 + 8;
            Row b { Kind::iconButton, { x, y, rw, 32 } };
            b.icon = icons::Id::users;
            b.dot = reqDot;
            b.act = [this] { friendsOpen = true; owner.setCollapsed (false); };
            add (std::move (b));
            y += 32 + rowGap;
        }
        if (open && friendsOpen)
        {
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
                add (std::move (r));
                y += 32 + rowGap;
            }
            y += 4 - rowGap;
            Row oh { Kind::offHeader, { x, y + rowGap, rw, 28 }, TXT ("오프라인"), juce::String ((int) off.size()) };
            oh.open = offlineOpen;
            oh.act = [this] { offlineOpen = ! offlineOpen; rebuildAndLayout(); };
            add (std::move (oh));
            y += rowGap + 28 + rowGap;
            if (offlineOpen)
            {
                for (auto* f : off)
                {
                    Row r { Kind::friendOff, { x, y, rw, 32 }, nameOf (f->user) };
                    r.part = f->user.part.value_or ("other");
                    const auto user = f->user;
                    r.act = [this, user] { if (owner.onOpenFriend) owner.onOpenFriend (user); };
                    add (std::move (r));
                    y += 32 + rowGap;
                }
            }
        }
        preferredH = y + padY;
    }

    int preferredH = 0;

    void rebuildAndLayout()
    {
        rebuild();
        owner.resized();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const bool open = ! owner.collapsed;
        for (int i = 0; i < (int) rows.size(); ++i)
        {
            const auto& r = rows[(size_t) i];
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
                    if (r.dot) drawDot (g, { ib.getRight() - 3.0f, ib.getY() + 4.0f }, 6.5f, r.active ? activeBg : sideBg);
                    if (open && r.kind != Kind::iconButton)
                    {
                        auto t = r.r.withTrimmedLeft (9 + 20 + 9).withTrimmedRight (9);
                        if (r.kind == Kind::room && r.sub.isNotEmpty())
                        {
                            const auto cf = font (400, 12.0f);
                            const int cw = (int) std::ceil (tabularWidth (cf, r.sub)) + 1;
                            g.setColour (col::inkSub);
                            g.setFont (cf);
                            g.drawText (r.sub, t.removeFromRight (cw), juce::Justification::centredRight, false);
                            t.removeFromRight (9);
                        }
                        g.setColour (col::ink);
                        g.setFont (font (r.active ? 600 : 400, 14.0f));
                        g.drawText (r.text, t, juce::Justification::centredLeft, true);
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
                    // 접힘 화살표: 펼침 아래(0), 접힘 오른쪽(-90°)
                    icons::draw (g, icons::Id::chevronDown, { tx, cy - 6.0f, 12.0f, 12.0f }, ink,
                                 r.open ? 0.0f : -juce::MathConstants<float>::halfPi);
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
                        juce::Graphics::ScopedSaveState ss (g);
                        if (off) g.beginTransparencyLayer (0.6f);
                        Avatar::paintCircle (g, av, r.part, 12.0f, off ? col::inkSub : col::ink, true);
                        if (off) g.endTransparencyLayer();
                    }
                    if (! off)
                    {
                        // 상태 점 8 (테두리 1.5 #FCFCFC), 오른쪽 아래 -2
                        const auto d = juce::Rectangle<float> (av.getRight() + 2.0f - 8.0f, av.getBottom() + 2.0f - 8.0f, 8.0f, 8.0f);
                        g.setColour (hot ? col::hoverBg : sideBg);
                        g.fillEllipse (d);
                        g.setColour (col::live);
                        g.fillEllipse (d.reduced (1.5f));
                    }
                    auto t = r.r.withTrimmedLeft (9 + 20 + 9).withTrimmedRight (9);
                    if (r.sub.isNotEmpty())
                    {
                        const auto sf = font (400, 12.0f);
                        const int sw = (int) std::ceil (textWidth (sf, r.sub)) + 1;
                        g.setColour (col::inkSub);
                        g.setFont (sf);
                        g.drawText (r.sub, t.removeFromRight (sw), juce::Justification::centredRight, false);
                        t.removeFromRight (6);
                    }
                    g.setColour (off ? col::inkSub : col::ink);
                    g.setFont (font (400, 14.0f));
                    g.drawText (r.text, t, juce::Justification::centredLeft, true);
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
    core.store().removeListener (this);
}

void Sidebar::setCollapsed (bool b)
{
    if (b == collapsed) return;
    collapsed = b;
    body->hotRow = -1;
    if (onLayoutChanged) onLayoutChanged();
    resized();
    repaint();
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
        case K::friends: case K::friendRequests: case K::currentRoom: case K::joinRequests: case K::me:
            body->rebuild();
            resized();
            body->repaint();
            repaint();
            break;
        default: break;
    }
}

juce::Rectangle<int> Sidebar::toggleBounds() const
{
    return collapsed ? juce::Rectangle<int> (12, 10, 32, 32) : juce::Rectangle<int> (getWidth() - 12 - 32, 10, 32, 32);
}

juce::Rectangle<int> Sidebar::profileBounds() const
{
    return { padX, getHeight() - profileH + 1 + 9, getWidth() - padX * 2, 40 };
}

void Sidebar::resized()
{
    auto area = getLocalBounds().withTrimmedRight (1);
    area.removeFromTop (headerH);
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
    g.fillRect (getWidth() - 1, 0, 1, getHeight());
    g.fillRect (0, headerH - 1, getWidth() - 1, 1);
    g.fillRect (0, getHeight() - profileH, getWidth() - 1, 1);

    // 머리말: 로고 (펼쳤을 때만) + 접기 버튼
    if (! collapsed)
        meon::drawLogo (g, juce::Rectangle<float> (18.0f, 14.0f, 140.0f, 24.0f), 24.0f, col::ink, juce::Justification::centredLeft);
    const auto tb = toggleBounds().toFloat();
    if (hot == 1) { g.setColour (buttonHover); g.fillRoundedRectangle (tb, 6.0f); }
    drawIcon (g, icons::Id::sidebar, tb, 18.0f, hot == 1 ? col::ink : col::inkSub);

    // 내 프로필 줄
    const auto pb = profileBounds().toFloat();
    if (profileMenuOpen) { g.setColour (activeBg); g.fillRoundedRectangle (pb, 6.0f); }
    else if (hot == 2) { g.setColour (col::hoverBg); g.fillRoundedRectangle (pb, 6.0f); }
    const auto& me = core.store().state().me;
    const juce::String part = me ? me->part.value_or ("other") : juce::String ("other");
    const auto av = juce::Rectangle<float> (pb.getX() + 5.0f, pb.getCentreY() - 14.0f, 28.0f, 28.0f);
    Avatar::paintCircle (g, av, part, 16.0f, col::ink, true);
    if (! collapsed)
    {
        const float tx = av.getRight() + 9.0f;
        const float tw = pb.getRight() - 2.0f - 20.0f - 6.0f - tx;
        g.setColour (col::ink);
        g.setFont (font (600, 13.0f));
        g.drawText (me ? me->nickname.value_or (me->username) : juce::String(), juce::Rectangle<float> (tx, pb.getCentreY() - 16.0f, tw, 16.0f),
                    juce::Justification::centredLeft, true);
        g.setColour (col::inkSub);
        g.setFont (font (400, 12.0f));
        g.drawText (icons::partLabel (part), juce::Rectangle<float> (tx, pb.getCentreY() + 1.0f, tw, 15.0f), juce::Justification::centredLeft, true);
        drawIcon (g, icons::Id::chevronUpDown, juce::Rectangle<float> (pb.getRight() - 2.0f - 20.0f, pb.getCentreY() - 10.0f, 20.0f, 20.0f), 14.0f, col::inkSub);
    }
}

void Sidebar::mouseMove (const juce::MouseEvent& e)
{
    const int h = toggleBounds().contains (e.getPosition()) ? 1 : (profileBounds().contains (e.getPosition()) ? 2 : 0);
    if (h != hot) { hot = h; repaint(); }
    setMouseCursor (h != 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void Sidebar::mouseExit (const juce::MouseEvent&)
{
    if (hot != 0) { hot = 0; repaint(); }
}

void Sidebar::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mouseWasClicked()) return;
    if (toggleBounds().contains (e.getPosition()))
    {
        juce::Component::SafePointer<Sidebar> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->setCollapsed (! safe->collapsed); });
    }
    else if (profileBounds().contains (e.getPosition()) && onProfileMenu)
        onProfileMenu();
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
