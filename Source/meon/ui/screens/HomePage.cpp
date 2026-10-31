#include "HomePage.h"
#include "../theme/Hairline.h"
#include "../../shell/ShellHooks.h"

namespace meon::ui
{

namespace
{
    const juce::Colour badBg { 0xFFFEF3F2u };   // 준비 상태 문제 줄
    constexpr int pad = 18, gap = 9, cardHeadH = 48, readyH = 1 + cardHeadH + 9 + 36 + 2 + 58 + 2 + 36 + 2 + 36 + 9;
    constexpr int recentRowH = 48, recentGap = 2;

    void paintCard (juce::Graphics& g, const juce::Component& c, juce::Rectangle<int> r, const juce::String& title, const juce::String& count = {})
    {
        const auto rf = r.toFloat();
        g.setColour (col::white);
        g.fillRoundedRectangle (rf, 6.0f);
        g.setColour (col::borderSubtle);
        hairline::roundedBorder (g, c, rf, 6.0f);
        hairline::horizontal (g, c, (float) r.getX() + 1.0f, (float) (r.getY() + cardHeadH), (float) r.getWidth() - 2.0f);
        const auto tf = font (600, 15.0f);
        g.setColour (col::ink);
        g.setFont (tf);
        const int tw = (int) std::ceil (textWidth (tf, title)) + 1;
        g.drawText (title, r.getX() + pad, r.getY() + 12, tw, 24, juce::Justification::centredLeft, false);
        if (count.isNotEmpty())
        {
            const auto cf = font (400, 12.0f);
            g.setColour (col::inkSub);
            g.setFont (cf);
            drawTabular (g, cf, count, (float) (r.getX() + pad + tw + 6), (float) r.getY() + 12.0f, 24.0f);
        }
    }

    void label (juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, int weight, float px, juce::Colour c,
                juce::Justification j = juce::Justification::centredLeft)
    {
        g.setColour (c);
        g.setFont (font (weight, px));
        g.drawText (t, r, j, true);
    }

    juce::String partKey (const juce::String& s)
    {
        if (s == TXT ("기타")) return "guitar";
        if (s == TXT ("보컬")) return "vocal";
        if (s == TXT ("베이스")) return "bass";
        if (s == TXT ("드럼")) return "drums";
        if (s == TXT ("키보드")) return "keys";
        return "other";
    }
}

//==============================================================================
HomePage::HomePage (core::MeonCore& c) : core (c), plugin (c.isPlugin())
{
    setOpaque (false);
    for (auto* b : { &devBtn, &fixBtn, &monBtn, &retryBtn })
        addChildComponent (*b);
    addChildComponent (inputMeter);
    addChildComponent (browseRooms);
    devBtn.onPress = [this] { if (onOpenSettings) onOpenSettings (2); };
    fixBtn.onPress = [this] { if (onOpenSettings) onOpenSettings (2); };
    monBtn.onPress = [this]
    {
        // 홈에서 내 소리 듣기 (설정 › 오디오 "입장 전에도 내 소리 듣기" 와 같은 값)
        if (auto* jam = core.jam()) jam->setHomeMonitor (! jam->isHomeMonitor());
        refresh();
    };
    retryBtn.onPress = [this]
    {
        // 서버는 저절로 다시 붙는다. 디자인처럼 잠깐 "연결 중…" 을 보인 뒤 실제 상태를 다시 본다
        if (retryBusy) return;
        retryBusy = true;
        refresh();
        juce::Component::SafePointer<HomePage> safe (this);
        juce::Timer::callAfterDelay (1200, [safe] { if (safe != nullptr) { safe->retryBusy = false; safe->refresh(); } });
    };
    browseRooms.onPress = [this] { if (onGoRooms) onGoRooms(); };
    if (auto* jam = core.jam())
    {
        const int chans = plugin ? 2 : juce::jlimit (1, 2, jam->getPrefs().inputChannelCount);
        inputMeter.meter.setSource (chans, [this] (int ch) { auto* j = core.jam(); return j != nullptr ? j->inputLevel (ch) : 0.0f; });
    }

    // 최근 같이 합주한 사람: 서버에 아직 없는 자료라 디자인의 목 데이터 (확인용 빌드).
    // 비어 있는 화면을 보려면 환경 변수 MEON_MOCK_RECENT=none 으로 켠다
    if (juce::SystemStats::getEnvironmentVariable ("MEON_MOCK_RECENT", {}) != "none")
    {
        struct M { juce::String name, part; bool fr, on; const char* last; int n; };
        const M mock[] = {
            { TXT ("민호"), TXT ("기타"), true, true, "10/29", 5 }, { TXT ("서준"), TXT ("보컬"), true, true, "10/27", 3 },
            { TXT ("민준"), TXT ("기타"), false, false, "10/26", 1 }, { TXT ("다은"), TXT ("베이스"), true, false, "10/25", 2 },
            { TXT ("유나"), TXT ("보컬"), true, true, "10/24", 4 }, { TXT ("서연"), TXT ("베이스"), false, false, "10/22", 1 },
            { TXT ("은우"), TXT ("드럼"), true, false, "10/20", 3 }, { TXT ("재민"), TXT ("베이스"), false, false, "10/18", 2 },
            { TXT ("예린"), TXT ("키보드"), true, true, "10/15", 1 }, { TXT ("하준"), TXT ("드럼"), false, false, "10/12", 1 } };
        for (auto& m : mock)
        {
            Recent r;
            r.name = m.name;
            r.part = partKey (m.part);
            r.friend_ = m.fr;
            r.online = m.on;
            r.last = m.last;
            r.count = m.n;
            r.button = std::make_unique<Button> (Button::Kind::small);
            addChildComponent (*r.button);
            recent.push_back (std::move (r));
        }
        for (size_t i = 0; i < recent.size(); ++i)
        {
            recent[i].button->onPress = [this, i]
            {
                auto& r = recent[i];
                if (! r.friend_) { if (r.state == 0) r.state = 1; }          // 친구 추가 → 요청 보냄 (목)
                else if (r.state == 0) { r.state = 1; r.pendingAt = juce::Time::getMillisecondCounter(); }   // 초대 → 3초 뒤 감
                else if (r.state == 1) r.state = 0;                          // 취소
                refreshRecentButtons();
                resized();
                repaint();
            };
        }
    }

    // 내 프로필
    addAndMakeVisible (editBtn);
    addAndMakeVisible (copyBtn);
    editBtn.onPress = [this] { if (onOpenSettings) onOpenSettings (1); };
    copyBtn.onPress = [this]
    {
        if (const auto& me = core.store().state().me) juce::SystemClipboard::copyTextToClipboard ("@" + me->username);
        copiedAt = juce::Time::getMillisecondCounter();
        copyBtn.setButtonText (TXT ("복사됨"));
        resized();
    };

    core.store().addListener (this);
    rebuildGroups();
    refresh();
    startTimer (250);
}

//==============================================================================
std::vector<int> HomePage::jamDays (int year, int month) const
{
    // 합주한 날: 서버에 아직 없는 자료라 디자인의 목 데이터 (2026년 8~10월). 최근 같이 합주한 사람이 없으면 비움
    if (! hasRecent() || year != 2026) return {};
    if (month == 10) return { 3, 5, 9, 12, 15, 18, 20, 22, 24, 25, 26, 27, 29 };
    if (month == 9) return { 6, 12, 19, 21, 26, 28 };
    if (month == 8) return { 8, 15, 22, 29 };
    return {};
}

int HomePage::calRows() const
{
    const auto now = juce::Time::getCurrentTime();
    int y = now.getYear(), m = now.getMonth() + calOff;   // getMonth 0~11
    while (m < 0) { m += 12; --y; }
    const juce::Time first (y, m, 1, 0, 0);
    const int lead = first.getDayOfWeek();
    const int days = juce::Time (m == 11 ? y + 1 : y, (m + 1) % 12, 1, 0, 0).toMilliseconds() > first.toMilliseconds()
                         ? (int) ((juce::Time (m == 11 ? y + 1 : y, (m + 1) % 12, 1, 0, 0) - first).inDays() + 0.5) : 30;
    return (lead + days + 6) / 7;
}

void HomePage::rebuildGroups()
{
    for (auto& g : groups) if (g.button != nullptr) removeChildComponent (g.button.get());
    groups.clear();
    const auto& s = core.store().state();
    const juce::String myRoom = s.currentRoom ? s.currentRoom->id : juce::String();
    for (auto& f : s.friends)
    {
        if (f.status != "in_room" || ! f.room || f.room->id == myRoom) continue;
        auto it = std::find_if (groups.begin(), groups.end(), [&f] (const JamGroup& g) { return g.roomId == f.room->id; });
        if (it == groups.end())
        {
            JamGroup g;
            g.roomId = f.room->id;
            g.title = f.room->title;
            g.button = std::make_unique<Button> (Button::Kind::small);
            addChildComponent (*g.button);
            groups.push_back (std::move (g));
            it = groups.end() - 1;
        }
        it->friends.push_back (f);
    }
    for (size_t i = 0; i < groups.size(); ++i)
    {
        const auto first = groups[i].friends.front();
        groups[i].button->onPress = [this, first]
        {
            using J = core::HomeController::FriendJoin;
            if (core.home().friendJoinState (first) == J::requested) return;
            if (onJoinFriend) onJoinFriend (first);
        };
    }
    refreshGroupButtons();
}

void HomePage::refreshGroupButtons()
{
    using J = core::HomeController::FriendJoin;
    for (auto& g : groups)
    {
        const auto st = core.home().friendJoinState (g.friends.front());
        g.button->setVisible (st != J::none);
        g.button->setButtonText (st == J::request ? TXT ("입장 요청") : st == J::requested ? TXT ("요청 보냄") : TXT ("참여"));
        g.button->setEnabled (st != J::requested);
    }
}

int HomePage::jamFriendCount() const
{
    int n = 0;
    for (auto& g : groups) n += (int) g.friends.size();
    return n;
}

HomePage::~HomePage()
{
    core.store().removeListener (this);
}

//==============================================================================
juce::String HomePage::deviceName() const
{
    if (plugin) return TXT ("DAW");
    auto& provider = meon::deviceManagerProvider();
    auto* dm = provider ? provider() : nullptr;
    auto* dev = dm != nullptr ? dm->getCurrentAudioDevice() : nullptr;
    return dev != nullptr ? dev->getName() : juce::String();
}

juce::String HomePage::inputText() const
{
    auto* jam = core.jam();
    if (jam == nullptr) return {};
    const auto& p = jam->getPrefs();
    const bool stereo = p.inputChannelCount >= 2;
    if (plugin) return TXT ("DAW 입력 · ") + (stereo ? TXT ("스테레오") : TXT ("모노"));
    juce::String t = TXT ("입력 ") + juce::String (p.inputChannelStart + 1);
    if (stereo) t << "-" << juce::String (p.inputChannelStart + 2);
    return t + TXT (" · ") + (stereo ? TXT ("스테레오") : TXT ("모노"));
}

void HomePage::refresh()
{
    const bool noDev = deviceName().isEmpty();
    auto* jam = core.jam();
    const bool noIn = ! noDev && jam != nullptr && (jam->isInputMissing() || jam->isInputSilent());
    const bool noSrv = core.store().state().link != core::Realtime::Link::online;

    devBtn.setVisible (! plugin);
    devBtn.setKind (noDev ? Button::Kind::smallPrimary : Button::Kind::small);
    devBtn.setButtonText (noDev ? TXT ("장치 선택") : TXT ("변경"));
    fixBtn.setVisible (noIn && ! plugin);
    const bool mon = jam != nullptr && jam->isHomeMonitor();
    monBtn.setVisible (! noIn && ! plugin);
    monBtn.setEnabled (! noDev);
    monBtn.setOn (mon && ! noDev);
    monBtn.setButtonText (mon && ! noDev ? TXT ("모니터링 중지") : TXT ("모니터링 테스트"));
    retryBtn.setVisible (noSrv);
    retryBtn.setEnabled (! retryBusy);
    retryBtn.setButtonText (retryBusy ? TXT ("연결 중…") : TXT ("다시 시도"));
    inputMeter.setVisible (true);
    inputMeter.meter.setGreyed (noDev);
    browseRooms.setVisible (! hasRecent());
    refreshRecentButtons();
    resized();
    repaint();
}

void HomePage::refreshRecentButtons()
{
    const bool joined = core.store().state().currentRoom.has_value();
    const int shown = recentShown();
    for (int i = 0; i < (int) recent.size(); ++i)
    {
        auto& r = recent[(size_t) i];
        if (r.friend_ && r.state == 1 && juce::Time::getMillisecondCounter() - r.pendingAt >= 3000) r.state = 2;
        const bool add = ! r.friend_, canInvite = r.friend_ && r.online && joined;
        auto& b = *r.button;
        b.setVisible (i < shown && (add || canInvite));
        if (add)
        {
            b.setButtonText (r.state == 1 ? TXT ("요청 보냄") : TXT ("친구 추가"));
            b.setEnabled (r.state == 0);
            b.setOn (false);
        }
        else
        {
            b.setButtonText (r.state == 1 ? TXT ("취소") : (r.state == 2 ? TXT ("초대됨") : TXT ("초대")));
            b.setEnabled (r.state != 2);
            b.setOn (r.state == 1);
        }
    }
}

//==============================================================================
HomePage::Layout HomePage::layout() const
{
    Layout l;
    auto area = getLocalBounds().reduced (metric::pagePad);   // 공개 방 쪽과 같은 바깥 여백
    const int cw = (area.getWidth() - gap * 2) / 3;
    for (int i = 0; i < 3; ++i)
    {
        l.col[i] = area.removeFromLeft (i == 2 ? area.getWidth() : cw);
        area.removeFromLeft (gap);
    }
    auto c0 = l.col[0];
    l.readyCard = c0.removeFromTop (readyH);
    c0.removeFromTop (gap);
    l.profileCard = c0;
    l.recentCard = l.col[1];
    auto c2 = l.col[2];
    l.calCard = c2.removeFromTop (1 + cardHeadH + 9 + 28 + 2 + calRows() * 34 - 2 + 12 + 1 + 34);
    c2.removeFromTop (gap);
    l.jamCard = c2;

    auto b = l.readyCard.withTrimmedTop (cardHeadH + 1).reduced (9);
    l.rowDev = b.removeFromTop (36); b.removeFromTop (2);
    l.rowIn = b.removeFromTop (58); b.removeFromTop (2);
    l.rowSrv = b.removeFromTop (36); b.removeFromTop (2);
    l.rowNet = b.removeFromTop (36);
    return l;
}

void HomePage::resized()
{
    const auto l = layout();
    // 장치 줄 버튼
    auto placeRight = [] (Button& btn, juce::Rectangle<int> row)
    {
        const int w = btn.getIdealWidth();
        btn.setBounds (row.getRight() - 9 - w, row.getCentreY() - metric::smallH / 2, w, metric::smallH);
    };
    placeRight (devBtn, l.rowDev);
    placeRight (retryBtn, l.rowSrv);
    // 입력 줄: 미터 + (입력 확인 방법) + 모니터링 테스트
    {
        auto line = juce::Rectangle<int> (l.rowIn.getX() + 9 + 40 + 12, l.rowIn.getY() + 9, l.rowIn.getWidth() - 9 - 40 - 12 - 9, metric::smallH);
        if (monBtn.isVisible())
        {
            const int w = monBtn.getIdealWidth();
            monBtn.setBounds (line.removeFromRight (w));
            line.removeFromRight (9);
        }
        if (fixBtn.isVisible())
        {
            const int w = fixBtn.getIdealWidth();
            fixBtn.setBounds (line.removeFromRight (w));
            line.removeFromRight (9);
        }
        inputMeter.setBounds (line.withWidth (juce::jmax (60, line.getWidth())));
    }

    // 최근 같이 합주한 사람
    {
        auto body = l.recentCard.withTrimmedTop (cardHeadH + 1);
        moreBounds = {};
        if (hasRecent())
        {
            auto rows = body.withTrimmedTop (6).reduced (9, 0);
            const int shown = recentShown();
            for (int i = 0; i < (int) recent.size(); ++i)
            {
                auto& b = *recent[(size_t) i].button;
                if (i >= shown) continue;
                const auto row = rows.removeFromTop (recentRowH);
                rows.removeFromTop (recentGap);
                const int w = b.getIdealWidth();
                b.setBounds (row.getRight() - 9 - w, row.getCentreY() - metric::smallH / 2, w, metric::smallH);
            }
            if ((int) recent.size() > 8) moreBounds = rows.removeFromTop (32);
        }
        else
        {
            const int w = browseRooms.getIdealWidth();
            browseRooms.setBounds (body.getCentreX() - w / 2, body.getCentreY() + 6, w, metric::buttonH);
        }
    }

    // 내 프로필: 편집(머리말 오른쪽 12), 복사(이름 줄 오른쪽)
    {
        const int ew = editBtn.getIdealWidth();
        editBtn.setBounds (l.profileCard.getRight() - 12 - ew, l.profileCard.getY() + (cardHeadH - metric::smallH) / 2, ew, metric::smallH);
        const int cw = copyBtn.getIdealWidth();
        const int rowY = l.profileCard.getY() + cardHeadH + 1 + 18;
        copyBtn.setBounds (l.profileCard.getRight() - 18 - cw, rowY + 24 - metric::smallH / 2, cw, metric::smallH);
    }

    // 합주 기록: 이전·다음 달
    {
        const int y = l.calCard.getY() + 12;
        calNext = { l.calCard.getRight() - 12 - 24, y, 24, 24 };
        calPrev = { calNext.getX() - 2 - 76 - 2 - 24, y, 24, 24 };
    }

    // 합주 중인 친구: 줄마다 버튼
    {
        auto rows = l.jamCard.withTrimmedTop (cardHeadH + 1 + 6).reduced (9, 0);
        for (auto& g : groups)
        {
            const auto row = rows.removeFromTop (recentRowH);
            rows.removeFromTop (recentGap);
            const int w = g.button->getIdealWidth();
            g.button->setBounds (row.getRight() - 9 - w, row.getCentreY() - metric::smallH / 2, w, metric::smallH);
        }
    }
}

void HomePage::paint (juce::Graphics& g)
{
    const auto l = layout();
    const auto& s = core.store().state();
    const juce::String dev = deviceName();
    const bool noDev = dev.isEmpty();
    auto* jam = core.jam();
    const bool noIn = ! noDev && jam != nullptr && (jam->isInputMissing() || jam->isInputSilent());
    const bool noSrv = s.link != core::Realtime::Link::online;

    // 내 준비 상태
    paintCard (g, *this, l.readyCard, TXT ("내 준비 상태"));
    auto rowBg = [&] (juce::Rectangle<int> r, bool bad) { if (bad) { g.setColour (badBg); g.fillRoundedRectangle (r.toFloat(), 6.0f); } };
    auto rowLabel = [&] (juce::Rectangle<int> r, const juce::String& t, bool top)
    {
        label (g, t, top ? juce::Rectangle<int> (r.getX() + 9, r.getY() + 9, 40, 19) : juce::Rectangle<int> (r.getX() + 9, r.getY(), 40, r.getHeight()),
               400, 13.0f, col::inkSub);
    };
    rowBg (l.rowDev, noDev);
    rowLabel (l.rowDev, TXT ("장치"), false);
    {
        const int x = l.rowDev.getX() + 9 + 40 + 12;
        const int right = devBtn.isVisible() ? devBtn.getX() - 9 : l.rowDev.getRight() - 9;
        label (g, noDev ? TXT ("오디오 장치를 찾을 수 없어요") : dev, { x, l.rowDev.getY(), right - x, l.rowDev.getHeight() }, 500, 13.0f,
               noDev ? col::danger : col::ink);
    }
    rowBg (l.rowIn, noIn);
    rowLabel (l.rowIn, TXT ("입력"), true);
    {
        const int x = l.rowIn.getX() + 9 + 40 + 12;
        const auto t = noDev ? TXT ("장치 없음") : (noIn ? TXT ("소리가 안 들어와요") : inputText());
        label (g, t, { x, l.rowIn.getY() + 9 + 19 + 6, l.rowIn.getRight() - 9 - x, 15 }, 500, 12.0f,
               noIn ? col::danger : (noDev ? col::inkFaint : col::inkSub));
    }
    rowBg (l.rowSrv, noSrv);
    rowLabel (l.rowSrv, TXT ("서버"), false);
    {
        const int x = l.rowSrv.getX() + 9 + 40 + 12;
        if (noSrv)
        {
            const int right = retryBtn.getX() - 9;
            label (g, TXT ("서버에 연결할 수 없어요"), { x, l.rowSrv.getY(), right - x, l.rowSrv.getHeight() }, 500, 13.0f, col::danger);
        }
        else
        {
            const auto t = s.myRttMs ? juce::String (*s.myRttMs) + "ms" : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94"));
            const auto f = font (500, 13.0f);
            g.setColour (col::ink);
            g.setFont (f);
            drawTabular (g, f, t, (float) x, (float) l.rowSrv.getY(), (float) l.rowSrv.getHeight());
        }
    }
    rowLabel (l.rowNet, TXT ("연결"), false);
    {
        const int x = l.rowNet.getX() + 9 + 40 + 12;
        // 직접 연결 가능 여부는 아직 앱이 모른다 (목 데이터). 서버에 못 붙으면 "확인할 수 없어요"
        label (g, noSrv ? TXT ("확인할 수 없어요") : TXT ("직접 연결 가능"), { x, l.rowNet.getY(), l.rowNet.getRight() - 9 - x, l.rowNet.getHeight() },
               500, 13.0f, noSrv ? col::inkFaint : col::ink);
    }

    // 최근 같이 합주한 사람 (목 데이터)
    paintCard (g, *this, l.recentCard, TXT ("최근 같이 합주한 사람"));
    {
        auto body = l.recentCard.withTrimmedTop (cardHeadH + 1);
        if (hasRecent())
        {
            auto rows = body.withTrimmedTop (6).reduced (9, 0);
            const int shown = recentShown();
            const auto now = juce::Time::getMillisecondCounter();
            for (int i = 0; i < shown; ++i)
            {
                const auto& r = recent[(size_t) i];
                const auto row = rows.removeFromTop (recentRowH);
                rows.removeFromTop (recentGap);
                const auto av = juce::Rectangle<float> ((float) row.getX() + 9.0f, (float) row.getCentreY() - 16.0f, 32.0f, 32.0f);
                Avatar::paintPhoto (g, av, {});   // 목 데이터 (사용자 id 없음): 기본 이미지
                if (r.friend_)
                {
                    const auto d = juce::Rectangle<float> (av.getRight() + 1.0f - 10.0f, av.getBottom() + 1.0f - 10.0f, 10.0f, 10.0f);
                    g.setColour (col::white);
                    g.fillEllipse (d);
                    g.setColour (r.online ? col::live : col::statusOff);
                    g.fillEllipse (d.reduced (2.0f));
                }
                const int tx = (int) av.getRight() + 9;
                const int right = r.button->isVisible() ? r.button->getX() - 9 : row.getRight() - 9;
                const int ty = row.getCentreY() - 18;
                const auto nf = font (600, 13.0f), pf = font (400, 12.0f);
                const int nw = juce::jmin ((int) std::ceil (textWidth (nf, r.name)) + 1, right - tx);
                label (g, r.name, { tx, ty, nw, 17 }, 600, 13.0f, col::ink);
                g.setColour (col::inkFaint);
                g.fillEllipse ((float) (tx + nw + 4), (float) ty + 7.5f, 2.0f, 2.0f);
                label (g, icons::partLabel (r.part), { tx + nw + 10, ty, juce::jmax (0, right - tx - nw - 10), 17 }, 400, 12.0f, col::inkSub);
                juce::String meta = TXT ("마지막 ") + r.last + TXT (" · ") + juce::String (r.count) + TXT ("회");
                if (r.friend_ && r.state == 1)
                {
                    const int left = juce::jmax (1, (int) std::ceil ((3000.0 - (double) (now - r.pendingAt)) / 1000.0));
                    meta = juce::String (left) + TXT ("초 뒤 초대가 가요");
                }
                label (g, meta, { tx, ty + 17 + 3, right - tx, 16 }, 400, 12.0f, col::inkFaint);
            }
            if (! moreBounds.isEmpty())
            {
                if (moreHot) { g.setColour (col::hoverBg); g.fillRoundedRectangle (moreBounds.toFloat(), 6.0f); }
                label (g, recentAll ? TXT ("접기") : TXT ("더 보기 · ") + juce::String ((int) recent.size() - 8) + TXT ("명"), moreBounds, 500, 12.0f,
                       moreHot ? col::ink : col::inkSub, juce::Justification::centred);
            }
        }
        else
        {
            label (g, TXT ("아직 같이 합주한 사람이 없어요"), { body.getX(), body.getCentreY() - 6 - 18, body.getWidth(), 18 }, 400, 13.0f, col::inkSub,
                   juce::Justification::centred);
        }
    }

    // 내 프로필 (이름·파트·아이디는 실제, 합주한 사람·이번 달 합주는 목 데이터)
    paintCard (g, *this, l.profileCard, TXT ("내 프로필"));
    {
        const auto& me = s.me;
        const juce::String part = me ? me->part.value_or ("other") : juce::String ("other");
        const int x0 = l.profileCard.getX() + pad;
        const int y0 = l.profileCard.getY() + cardHeadH + 1 + pad;
        const auto av = juce::Rectangle<float> ((float) x0, (float) y0, 48.0f, 48.0f);
        Avatar::paintPhoto (g, av, me ? me->id : juce::String());
        const int tx = x0 + 48 + 12, right = copyBtn.getX() - 9;
        label (g, me ? me->nickname.value_or (me->username) : juce::String(), { tx, y0 + 4, right - tx, 20 }, 600, 16.0f, col::ink);
        const auto pf = font (400, 12.0f);
        const auto pl = icons::partLabel (part);
        const int pw = (int) std::ceil (textWidth (pf, pl)) + 1;
        label (g, pl, { tx, y0 + 28, pw, 16 }, 400, 12.0f, col::inkSub);
        g.setColour (col::inkFaint);
        g.fillEllipse ((float) (tx + pw + 4), (float) y0 + 35.0f, 2.0f, 2.0f);
        label (g, me ? "@" + me->username : juce::String(), { tx + pw + 10, y0 + 28, juce::jmax (0, right - tx - pw - 10), 16 }, 400, 12.0f, col::inkFaint);

        const int sy = y0 + 48 + 18, sw = (l.profileCard.getWidth() - pad * 2 - 12) / 3;
        const juce::String vals[3] = { juce::String ((int) s.friends.size()), juce::String ((int) recent.size()),
                                       juce::String ((int) jamDays (juce::Time::getCurrentTime().getYear(), juce::Time::getCurrentTime().getMonth() + 1).size()) + TXT ("회") };
        const juce::String names[3] = { TXT ("친구"), TXT ("합주한 사람"), TXT ("이번 달 합주") };
        for (int i = 0; i < 3; ++i)
        {
            const juce::Rectangle<int> box (x0 + i * (sw + 6), sy, i == 2 ? l.profileCard.getRight() - pad - (x0 + i * (sw + 6)) : sw, 9 + 22 + 3 + 15 + 9);
            g.setColour (col::rowHover);
            g.fillRoundedRectangle (box.toFloat(), 6.0f);
            const auto vf = font (600, 18.0f);
            g.setColour (col::ink);
            g.setFont (vf);
            drawTabular (g, vf, vals[i], (float) box.getX() + 12.0f, (float) box.getY() + 9.0f, 22.0f);
            label (g, names[i], { box.getX() + 12, box.getY() + 9 + 22 + 3, box.getWidth() - 12, 15 }, 400, 12.0f, col::inkSub);
        }
    }

    // 합주 기록 (달력: 오늘은 실제 날짜, 합주한 날은 목 데이터)
    paintCard (g, *this, l.calCard, TXT ("합주 기록"));
    {
        const auto now = juce::Time::getCurrentTime();
        int y = now.getYear(), m = now.getMonth() + calOff;
        while (m < 0) { m += 12; --y; }
        const juce::Time first (y, m, 1, 0, 0);
        const int lead = first.getDayOfWeek();
        const juce::Time next (m == 11 ? y + 1 : y, (m + 1) % 12, 1, 0, 0);
        const int days = (int) ((next - first).inDays() + 0.5);
        const juce::Time prevFirst (m == 0 ? y - 1 : y, (m + 11) % 12, 1, 0, 0);
        const int prevDays = (int) ((first - prevFirst).inDays() + 0.5);
        const auto jam = jamDays (y, m + 1);

        // 머리말 오른쪽: < 2026년 10월 >
        if (calHot == 1) { g.setColour (col::hoverBg); g.fillRoundedRectangle (calPrev.toFloat(), 5.0f); }
        icons::draw (g, icons::Id::chevronLeft, calPrev.toFloat().withSizeKeepingCentre (14.0f, 14.0f), calHot == 1 ? col::ink : col::inkSub);
        label (g, juce::String (y) + TXT ("년 ") + juce::String (m + 1) + TXT ("월"), { calPrev.getRight() + 2, calPrev.getY(), 76, 24 }, 500, 13.0f, col::ink,
               juce::Justification::centred);
        const bool canNext = calOff < 0;
        if (calHot == 2 && canNext) { g.setColour (col::hoverBg); g.fillRoundedRectangle (calNext.toFloat(), 5.0f); }
        icons::draw (g, icons::Id::chevronRight, calNext.toFloat().withSizeKeepingCentre (14.0f, 14.0f), canNext ? col::inkSub : col::statusOff);

        const auto body = l.calCard.withTrimmedTop (cardHeadH + 1).withTrimmedLeft (12).withTrimmedRight (12);
        const int cellW = body.getWidth() / 7;
        const juce::String week[7] = { TXT ("일"), TXT ("월"), TXT ("화"), TXT ("수"), TXT ("목"), TXT ("금"), TXT ("토") };
        for (int i = 0; i < 7; ++i)
            label (g, week[i], { body.getX() + i * cellW, body.getY() + 9, cellW, 28 }, 500, 11.0f, col::inkFaint, juce::Justification::centred);
        const int rows = (lead + days + 6) / 7;
        for (int cell = 0; cell < rows * 7; ++cell)
        {
            const int d = cell - lead + 1;
            const int cx = body.getX() + (cell % 7) * cellW, cy = body.getY() + 9 + 28 + 2 + (cell / 7) * 34;
            const auto box = juce::Rectangle<float> ((float) cx, (float) cy, (float) cellW, 32.0f);
            const float sz = juce::jmin (28.0f, (float) cellW);
            const auto circ = box.withSizeKeepingCentre (sz, sz);
            juce::String t;
            juce::Colour ink = col::ink;
            int fw = 400;
            if (d < 1) { t = juce::String (prevDays + d); ink = col::statusOff; }
            else if (d > days) { t = juce::String (d - days); ink = col::statusOff; }
            else
            {
                t = juce::String (d);
                const bool isJam = std::find (jam.begin(), jam.end(), d) != jam.end();
                const bool today = calOff == 0 && d == now.getDayOfMonth();
                if (calOff == 0 && d > now.getDayOfMonth()) ink = col::inkFaint;
                if (isJam) { g.setColour (col::accent); g.fillEllipse (circ); fw = 600; }
                if (today)
                {
                    g.setColour (col::ink);
                    g.drawEllipse (circ.withSizeKeepingCentre (26.5f, 26.5f), 1.5f);
                    fw = 600;
                }
            }
            g.setColour (ink);
            g.setFont (font (fw, 12.0f));
            g.drawText (t, box, juce::Justification::centred, false);
        }
        // 아래: 합주한 날 N일
        const int fy = body.getY() + 9 + 28 + 2 + rows * 34 - 2 + 12;
        g.setColour (col::borderSubtle);
        hairline::horizontal (g, *this, (float) l.calCard.getX() + 1.0f, (float) fy, (float) l.calCard.getWidth() - 2.0f);
        label (g, TXT ("합주한 날"), { l.calCard.getX() + pad, fy + 1, 120, 33 }, 400, 12.0f, col::inkSub);
        label (g, juce::String ((int) jam.size()) + TXT ("일"), { l.calCard.getRight() - pad - 80, fy + 1, 80, 33 }, 600, 12.0f, col::ink,
               juce::Justification::centredRight);
    }

    // 합주 중인 친구 (실제 데이터)
    paintCard (g, *this, l.jamCard, TXT ("합주 중인 친구"), juce::String (jamFriendCount()));
    {
        if (groups.empty())
        {
            label (g, TXT ("지금 합주 중인 친구가 없어요"), l.jamCard.withTrimmedTop (cardHeadH + 1), 400, 13.0f, col::inkFaint, juce::Justification::centred);
        }
        else
        {
            auto rows = l.jamCard.withTrimmedTop (cardHeadH + 1 + 6).reduced (9, 0);
            for (auto& gr : groups)
            {
                const auto row = rows.removeFromTop (recentRowH);
                rows.removeFromTop (recentGap);
                const int n = juce::jmin (3, (int) gr.friends.size());
                float ax = (float) row.getX() + 9.0f;
                for (int i = 0; i < n; ++i)
                {
                    const auto av = juce::Rectangle<float> (ax, (float) row.getCentreY() - 14.0f, 28.0f, 28.0f);
                    g.setColour (col::white);
                    g.fillEllipse (av.expanded (2.0f));   // 흰 테두리 2 (겹친 아바타 사이)
                    Avatar::paintPhoto (g, av, gr.friends[(size_t) i].user.id);
                    ax += 28.0f - 8.0f;
                }
                const int tx = (int) (ax + 8.0f) + 9;
                const int right = gr.button->isVisible() ? gr.button->getX() - 9 : row.getRight() - 9;
                const int ty = row.getCentreY() - 18;
                label (g, gr.title, { tx, ty, right - tx, 17 }, 600, 13.0f, col::ink);
                juce::StringArray names;
                for (auto& f : gr.friends) names.add (f.user.nickname.value_or (f.user.username));
                label (g, names.joinIntoString (", ") + TXT (" 합주 중"), { tx, ty + 17 + 3, right - tx, 16 }, 400, 12.0f, col::inkSub);
            }
        }
    }
}

//==============================================================================
void HomePage::stateChanged (core::StateKey k)
{
    using K = core::StateKey;
    if (k == K::friends || k == K::currentRoom || k == K::publicRooms) rebuildGroups();
    if (k == K::link || k == K::myRtt || k == K::currentRoom || k == K::me || k == K::friends || k == K::publicRooms)
        refresh();
}

void HomePage::timerCallback()
{
    // 장치·무음 상태와 초대 카운트다운을 따라간다
    bool pending = false;
    for (auto& r : recent) if (r.friend_ && r.state == 1) pending = true;
    if (pending) { refreshRecentButtons(); repaint (layout().recentCard); }
    if (copiedAt != 0 && juce::Time::getMillisecondCounter() - copiedAt >= 1500)
    {
        copiedAt = 0;
        copyBtn.setButtonText (TXT ("복사"));
        resized();
    }
    if (++ticks % 4 == 0) { refreshGroupButtons(); refresh(); }
}

void HomePage::mouseMove (const juce::MouseEvent& e)
{
    const bool h = moreBounds.contains (e.getPosition());
    if (h != moreHot) { moreHot = h; repaint (moreBounds); }
    const int ch = calPrev.contains (e.getPosition()) ? 1 : (calNext.contains (e.getPosition()) && calOff < 0 ? 2 : 0);
    if (ch != calHot) { calHot = ch; repaint (layout().calCard); }
    setMouseCursor (h || ch != 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void HomePage::mouseExit (const juce::MouseEvent&)
{
    if (moreHot) { moreHot = false; repaint (moreBounds); }
}

void HomePage::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasClicked() && (calPrev.contains (e.getPosition()) || calNext.contains (e.getPosition())))
    {
        if (calPrev.contains (e.getPosition())) calOff = juce::jmax (-12, calOff - 1);
        else if (calOff < 0) ++calOff;
        resized();
        repaint();
        return;
    }
    if (e.mouseWasClicked() && moreBounds.contains (e.getPosition()))
    {
        recentAll = ! recentAll;
        refreshRecentButtons();
        resized();
        repaint();
    }
}

} // namespace meon::ui
