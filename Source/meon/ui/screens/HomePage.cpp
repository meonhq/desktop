#include "HomePage.h"
#include "../../shell/ShellHooks.h"

namespace meon::ui
{

namespace
{
    const juce::Colour badBg { 0xFFFEF3F2u };   // 준비 상태 문제 줄
    constexpr int pad = 18, gap = 9, cardHeadH = 48, readyH = 1 + cardHeadH + 9 + 36 + 2 + 58 + 2 + 36 + 2 + 36 + 9;
    constexpr int recentRowH = 48, recentGap = 2;

    void paintCard (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title, const juce::String& count = {})
    {
        const auto rf = r.toFloat();
        g.setColour (col::white);
        g.fillRoundedRectangle (rf, 6.0f);
        g.setColour (col::line);
        g.drawRoundedRectangle (rf.reduced (0.5f), 6.0f, 1.0f);
        g.fillRect (r.getX() + 1, r.getY() + cardHeadH, r.getWidth() - 2, 1);
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

    core.store().addListener (this);
    refresh();
    startTimer (250);
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
    auto area = getLocalBounds().reduced (pad);
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
    l.calCard = c2.removeFromTop (juce::jmin (c2.getHeight() / 2, 290));
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
    paintCard (g, l.readyCard, TXT ("내 준비 상태"));
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
    paintCard (g, l.recentCard, TXT ("최근 같이 합주한 사람"));
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
                Avatar::paintCircle (g, av, r.part, 16.0f, col::ink, true);
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
}

//==============================================================================
void HomePage::stateChanged (core::StateKey k)
{
    using K = core::StateKey;
    if (k == K::link || k == K::myRtt || k == K::currentRoom || k == K::me || k == K::friends)
        refresh();
}

void HomePage::timerCallback()
{
    // 장치·무음 상태와 초대 카운트다운을 따라간다
    bool pending = false;
    for (auto& r : recent) if (r.friend_ && r.state == 1) pending = true;
    if (pending) { refreshRecentButtons(); repaint (layout().recentCard); }
    if (++ticks % 4 == 0) refresh();
}

void HomePage::mouseMove (const juce::MouseEvent& e)
{
    const bool h = moreBounds.contains (e.getPosition());
    if (h != moreHot) { moreHot = h; repaint (moreBounds); }
    setMouseCursor (h ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void HomePage::mouseExit (const juce::MouseEvent&)
{
    if (moreHot) { moreHot = false; repaint (moreBounds); }
}

void HomePage::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasClicked() && moreBounds.contains (e.getPosition()))
    {
        recentAll = ! recentAll;
        refreshRecentButtons();
        resized();
        repaint();
    }
}

} // namespace meon::ui
