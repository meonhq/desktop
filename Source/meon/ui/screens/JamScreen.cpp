#include "JamScreen.h"
#include "../Messages.h"
#include "../../core/api/ApiJson.h"

namespace meon::ui
{

namespace
{
    constexpr int headerLineY = metric::headerH;
    constexpr int bannerH = 37;
    constexpr int bottomCardH = 48 + 1 + 43;
    constexpr int bubbleMaxW = 220;

    juce::String nameOf (const core::UserPublic& u) { return u.nickname.value_or (u.username); }

    juce::String peerStateText (core::PeerState::State s)
    {
        using S = core::PeerState::State;
        switch (s)
        {
            case S::connecting:  return TXT ("연결 중");
            case S::interrupted: return TXT ("소리 끊김");
            case S::failed:      return TXT ("연결 실패");
            default:             return {};
        }
    }

    juce::String clockText (juce::int64 ms)
    {
        const auto t = juce::Time (ms);
        const int h = t.getHours();
        return (h < 12 ? TXT ("오전 ") : TXT ("오후 ")) + juce::String (h % 12 == 0 ? 12 : h % 12) + ":" + juce::String (t.getMinutes()).paddedLeft ('0', 2);
    }

    juce::String mmss (double seconds)
    {
        const int s = (int) std::floor (juce::jmax (0.0, seconds));
        return juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
    }

    struct Separator : juce::Component
    {
        Separator() { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics& g) override { g.fillAll (col::line); }
    };
}

//==============================================================================
/** 무음 경고 줄 (노란 면 #FFF8D6, 위 구분선): "입력 신호가 없어요 · 설정에서 입력 채널을 확인해 주세요" + 설정 열기.
    입력 장치는 있는데 소리가 안 들어올 때만 (5초 −60 dBFS). 장치 없음 안내는 띄우지 않는다 (2026-10-15 사용자 결정) */
struct JamScreen::SilenceRow : juce::Component
{
    SilenceRow() { addAndMakeVisible (open); }

    // 넓으면 한 줄 (제목 · 설명 · 버튼, 높이 37). 설명이 다 안 들어가면 설명을 제목 아래로 내린다 (플러그인 900px 등)
    static constexpr int oneLineH = 37, padY = 10, titleH = 17, descLineH = 16;
    juce::String title() const { return TXT ("입력 신호가 없어요"); }
    juce::String desc() const  { return TXT ("설정에서 입력 채널을 확인해 주세요"); }
    static juce::Font titleFont() { return font (500, 13.0f); }
    static juce::Font descFont()  { return font (400, 12.0f); }
    int titleW() const { return (int) std::ceil (textWidth (titleFont(), title())) + 1; }
    bool fitsOneLine (int w) const
    {
        return metric::padX + titleW() + 9 + (int) std::ceil (textWidth (descFont(), desc())) + 1 + 9 + open.getIdealWidth() + metric::padX <= w;
    }
    int descHeight (int w) const
    {
        return (int) std::ceil (meon::paragraphHeightWithEmoji (desc(), descFont(), (float) (w - metric::padX * 2), (float) descLineH));
    }
    int heightFor (int w) const { return fitsOneLine (w) ? oneLineH : 1 + padY + titleH + 3 + descHeight (w) + padY; }

    void paint (juce::Graphics& g) override
    {
        g.setColour (col::accentSoft);
        g.fillRect (getLocalBounds());
        g.setColour (col::line);
        g.fillRect (0, 0, getWidth(), 1);
        const int w1 = titleW();
        g.setColour (col::ink);
        g.setFont (titleFont());
        if (fitsOneLine (getWidth()))
        {
            g.drawText (title(), metric::padX, 1, w1, getHeight() - 1, juce::Justification::centredLeft, false);
            g.setColour (col::inkSub);
            g.setFont (descFont());
            g.drawText (desc(), metric::padX + w1 + 9, 1, open.getX() - 9 - (metric::padX + w1 + 9), getHeight() - 1,
                        juce::Justification::centredLeft, false);
            return;
        }
        g.drawText (title(), metric::padX, 1 + padY, w1, titleH, juce::Justification::centredLeft, false);
        meon::drawParagraphWithEmoji (g, desc(), descFont(), col::inkSub,
                                      juce::Rectangle<float> ((float) metric::padX, (float) (1 + padY + titleH + 3),
                                                              (float) (getWidth() - metric::padX * 2), (float) descHeight (getWidth())),
                                      (float) descLineH, juce::Justification::topLeft);
    }
    void resized() override
    {
        const int w = open.getIdealWidth();
        const int y = fitsOneLine (getWidth()) ? (getHeight() - metric::smallH) / 2 + 1 : 1 + padY + (titleH - metric::smallH) / 2;
        open.setBounds (getWidth() - metric::padX - w, y, w, metric::smallH);
    }
    Button open { Button::Kind::small, TXT ("설정 열기") };
};

int JamScreen::silenceRowH (int width) const
{
    return jam.isInputSilent() ? silenceRow->heightFor (width) : 0;
}

//==============================================================================
/** 입장 코드 상자 (높이 32, 테두리 #E0E0E0, 면 #FCFCFC): "입장 코드" + 코드(600 14, 자간 2) + 복사 */
struct JamScreen::CodeBox : juce::Component
{
    CodeBox()
    {
        addAndMakeVisible (copy);
        // 복사하고 2초 동안 "복사됨". 버튼이 넓어지는 만큼 상자도 넓어진다 (코드와 버튼 사이 9px 유지, 오른쪽 고정)
        copy.onPress = [this]
        {
            juce::SystemClipboard::copyTextToClipboard (code);
            copy.flashText (TXT ("복사됨"), copiedMs);
        };
        copy.onStateChange = [this] { if (onWidthChange) onWidthChange(); };
    }
    static constexpr int copiedMs = 2000;
    std::function<void()> onWidthChange;   // 버튼 글자가 바뀌어 idealWidth 가 달라짐
    void setCode (const juce::String& c) { code = c; resized(); repaint(); }
    int idealWidth() const
    {
        return 12 + (int) std::ceil (textWidth (font (400, 12.0f), TXT ("입장 코드"))) + 9 + codeWidth() + 9 + copy.getIdealWidth() + 6;
    }
    int codeWidth() const { return (int) std::ceil (textWidth (font (600, 14.0f), code) + 2.0f * (float) code.length()); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (col::softBg);
        g.fillRoundedRectangle (r, metric::radius);
        g.setColour (col::line);
        g.drawRoundedRectangle (r.reduced (0.5f), metric::radius, 1.0f);
        const auto lf = font (400, 12.0f);
        const int lw = (int) std::ceil (textWidth (lf, TXT ("입장 코드")));
        g.setColour (col::inkSub);
        g.setFont (lf);
        g.drawText (TXT ("입장 코드"), 12, 0, lw + 1, getHeight(), juce::Justification::centredLeft, false);
        meon::drawSpacedText (g, code, font (600, 14.0f), 14.0f, 2.0f / 14.0f, col::ink,
                              juce::Rectangle<float> ((float) (12 + lw + 9), 0.0f, (float) codeWidth() + 2.0f, (float) getHeight()),
                              juce::Justification::centredLeft);
    }
    void resized() override
    {
        const int w = copy.getIdealWidth();
        copy.setBounds (getWidth() - 6 - w, (getHeight() - metric::smallH) / 2, w, metric::smallH);
    }

    juce::String code;
    Button copy { Button::Kind::small, TXT ("복사") };
};

//==============================================================================
/** 멤버 행 (높이 59, 여백 0 18 0 12). 내 행의 음소거·볼륨은 셀프 모니터 (잠정) */
struct JamScreen::MemberRow : juce::Component
{
    MemberRow (JamScreen& o, const core::JamSession::Member& m) : owner (o), member (m), meter (TXT ("레벨"))
    {
        avatar.setPart (m.part.isNotEmpty() ? m.part : juce::String ("other"));
        avatar.setInterceptsMouseClicks (false, false);
        for (auto* c : std::initializer_list<juce::Component*> { &avatar, &mute, &volume, &pan, &reset, &ping, &meter })
            addAndMakeVisible (c);
        volume.setShowTip (true);
        ping.setInterceptsMouseClicks (false, false);
        auto& jam = owner.jam;
        const auto id = m.userId;
        if (m.isMe)
        {
            mute.onPress = [this] { owner.jam.setSelfMonitor (! owner.jam.isSelfMonitor()); refresh(); };
            volume.onChange = [this] (float db) { owner.jam.setSelfMonitorDb (db); refresh(); };
            pan.onChange = [this] (int v) { owner.jam.setSelfMonitorPan (v); refresh(); };
            reset.onPress = [this] { owner.jam.setSelfMonitorDb (0.0f); owner.jam.setSelfMonitorPan (0); refresh(); };
            mute.setTooltip (TXT ("셀프 모니터 · 내 소리를 내가 듣기"));
            ping.setText (TXT ("나"));
            meter.meter.setSource (juce::jlimit (1, 2, jam.sendChannelCount()), [this] (int ch) { return owner.jam.inputLevel (ch); });
        }
        else
        {
            mute.onPress = [this, id] { owner.jam.setMemberMuted (id, ! member.muted); };
            volume.onChange = [this, id] (float db) { owner.jam.setMemberGainDb (id, db); member.gainDb = db; refresh(); };
            pan.onChange = [this, id] (int v) { owner.jam.setMemberPan (id, v); member.pan = v; refresh(); };
            reset.onPress = [this, id] { owner.jam.resetMember (id); member.gainDb = 0.0f; member.pan = 0; refresh(); };
            meter.meter.setSource (juce::jlimit (1, 2, m.channels), [this, id] (int ch) { return owner.jam.memberLevel (id, ch); });
        }
        reset.setTooltip (TXT ("볼륨 0 dB · 패닝 가운데로"));
        addChildComponent (manage);
        manage.setTooltip (TXT ("방장 전용 · 내보내기, 방장 넘기기"));
        manage.onPress = [this] { owner.openManage (member); };
        refresh();
    }

    /** 아바타·이름 자리 (누르면 프로필, 내 행 제외) */
    bool isProfileSpot (juce::Point<int> p) const
    {
        if (member.isMe) return false;
        return (p.x < 12 + 41 + 9 + 160 && p.y < 9 + 17 + 3) || avatar.getBounds().contains (p);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasClicked() && isProfileSpot (e.getPosition())) owner.openProfile (member.userId);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        setMouseCursor (isProfileSpot (e.getPosition()) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }

    void update (const core::JamSession::Member& m)
    {
        const bool chans = m.channels != member.channels;
        member = m;
        if (chans && ! m.isMe)
        {
            const auto id = m.userId;
            meter.meter.setSource (juce::jlimit (1, 2, m.channels), [this, id] (int ch) { return owner.jam.memberLevel (id, ch); });
        }
        refresh();
    }

    /** 지금 안 들림: 멤버는 내가 음소거, 내 행은 셀프 모니터가 실제로 꺼져 있음 (설정 끔 또는 송출 정지 중) */
    bool isMuted() const { return member.isMe ? ! owner.jam.isSelfMonitorAudible() : member.muted; }

    void refresh()
    {
        const bool muted = isMuted();
        // 송출 정지 중에는 내 소리가 내 귀에도 안 들린다 (버그 23). 내 행의 조절은 막아 두고, 송출을 다시 켜면 설정값대로 돌아온다
        const bool sendOff = member.isMe && owner.jam.isSendMuted();
        mute.setOn (muted);
        mute.setButtonText (muted ? TXT ("음소거 해제") : TXT ("음소거"));
        const float vol = member.isMe ? owner.jam.getSelfMonitorDb() : member.gainDb;
        const int pv = member.isMe ? owner.jam.getSelfMonitorPan() : member.pan;
        if (! volume.isDragging()) volume.setValue (vol);
        volume.setTitle (sendOff ? TXT ("송출 정지 중에는 내 소리가 들리지 않아요")
                                 : DbSlider::dbText (vol) + TXT (" · 더블클릭하면 0 dB"));
        pan.setValue (pv);
        if (member.isMe)
        {
            mute.setTooltip (sendOff ? TXT ("송출 정지 중에는 내 소리가 들리지 않아요") : TXT ("셀프 모니터 · 내 소리를 내가 듣기"));
            for (auto* c : std::initializer_list<juce::Component*> { &mute, &volume, &reset })
            {
                c->setEnabled (! sendOff);
                c->setAlpha (sendOff ? 0.4f : 1.0f);
            }
            volume.setMuted (muted);
            // 내 패닝은 셀프 모니터에만 걸리므로, 지금 안 들리면(설정 끔 · 송출 정지) 막는다
            pan.setEnabled (! muted);
            pan.setAlpha (muted ? 0.4f : 1.0f);
        }
        reset.setDimmed (vol == 0.0f && pv == 0);
        const bool host = owner.core.store().state().amHost();
        manage.setVisible (host && ! member.isMe);
        hostLayout = host;
        avatar.setDimmed (muted);
        avatar.setPart (member.part.isNotEmpty() ? member.part : juce::String ("other"));   // 가짜 기간: 프로필은 입장 뒤에 온다
        if (member.isMe) ping.setText (TXT ("나"));
        else ping.setPing (member.pingMs);
        meter.meter.setActive (! muted);
        resized();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const bool muted = isMuted();
        const int x = 12 + 41 + 9;
        int cx = x;
        const auto nf = font (600, 14.0f);
        const auto name = member.nickname;
        const int nw = juce::jmin ((int) std::ceil (textWidth (nf, name)) + 1, juce::jmax (40, mute.getRight() + 150 - x));
        g.setColour (muted ? col::inkFaint : col::ink);
        g.setFont (nf);
        g.drawText (name, cx, 9, nw, 17, juce::Justification::centredLeft, true);
        cx += nw + 5;
        g.setColour (col::inkFaint);
        g.fillEllipse ((float) cx, 9.0f + 7.5f, 2.0f, 2.0f);
        cx += 2 + 5;
        const auto pf = font (400, 12.0f);
        const auto part = icons::partLabel (member.part.isNotEmpty() ? member.part : juce::String ("other")) + (member.isMe ? TXT (" · 나") : juce::String());
        const int pw = (int) std::ceil (textWidth (pf, part)) + 1;
        g.setColour (col::inkSub);
        g.setFont (pf);
        g.drawText (part, cx, 9, pw, 17, juce::Justification::centredLeft, false);
        cx += pw;
        if (member.isHost && ! member.isMe)
        {
            cx += 8;
            const auto tf = font (500, 11.0f);
            const juce::Rectangle<float> tr ((float) cx, 10.0f, textWidth (tf, TXT ("방장")) + 10.0f, 15.0f);
            g.setColour (col::line);
            g.drawRoundedRectangle (tr.reduced (0.5f), 3.0f, 1.0f);
            g.setColour (col::inkSub);
            g.setFont (tf);
            g.drawText (TXT ("방장"), tr, juce::Justification::centred, false);
            cx = (int) tr.getRight();
        }
        // 연결 상태 (버그 #2·#3): 연결 중 / 소리 끊김 / 연결 실패
        if (! member.isMe)
        {
            const auto st = peerStateText (member.state);
            if (st.isNotEmpty())
            {
                const bool bad = member.state != core::PeerState::State::connecting;
                cx += 8;
                g.setColour (bad ? col::danger : col::inkFaint);
                g.setFont (font (500, 12.0f));
                juce::String t = st;
                if (member.dropCount > 0 && member.state == core::PeerState::State::interrupted)
                    t << " " << juce::String (member.dropCount) << TXT ("회");
                g.drawText (t, cx, 9, 120, 17, juce::Justification::centredLeft, true);
            }
        }
        if (getY() > 0)
        {
            g.setColour (col::line);
            g.fillRect (0, 0, getWidth(), 1);
        }
    }

    void resized() override
    {
        const int gap = owner.plugin ? 24 : 18;
        // 오른쪽: 핑 48 + 12 + 미터
        auto r = getLocalBounds().withTrimmedLeft (12).withTrimmedRight (18);
        const int meterW = owner.plugin ? juce::jlimit (84, 150, r.getWidth() / 4) : 150;
        meter.setBounds (r.removeFromRight (meterW).withSizeKeepingCentre (meterW, metric::smallH));
        r.removeFromRight (12);
        ping.setBounds (r.removeFromRight (48).withSizeKeepingCentre (48, metric::smallH));
        r.removeFromRight (gap);
        avatar.setBounds (r.removeFromLeft (41).withSizeKeepingCentre (41, 41));
        r.removeFromLeft (9);
        // 둘째 줄: 음소거 · 볼륨 150 · 패닝 19 · 초기화 (간격 9)
        const int y = 9 + 17 + 5;
        int x = r.getX();
        const int mw = mute.getIdealWidth();
        mute.setBounds (x, y, mw, metric::smallH);
        x += mw + 9;
        const int vw = juce::jlimit (60, 150, r.getRight() - x - 9 - 19 - 9 - reset.getIdealWidth() - (hostLayout ? 9 + manage.getIdealWidth() : 0));
        volume.setBounds (x, y, vw, metric::smallH);
        x += vw + 9;
        pan.setBounds (x, y, 19, 19);
        x += 19 + 9;
        reset.setBounds (x, y, reset.getIdealWidth(), metric::smallH);
        x += reset.getIdealWidth() + 9;
        manage.setBounds (x, y, manage.getIdealWidth(), metric::smallH);
    }

    JamScreen& owner;
    core::JamSession::Member member;
    bool hostLayout = false;   // 방장이면 내 행에도 "관리" 자리만큼 비운다 (디자인 manageGhost)
    Avatar avatar { 41.0f, 20.0f };
    Button mute { Button::Kind::small, TXT ("음소거") }, reset { Button::Kind::small, TXT ("초기화") };
    DbSlider volume { -60.0f, 6.0f };
    PanKnob pan;
    PingBadge ping;
    MeterBox meter;
    Button manage { Button::Kind::small, TXT ("관리") };
};

struct JamScreen::MemberList : juce::Component
{
    juce::OwnedArray<MemberRow> rows;
    void resized() override
    {
        int y = 0;
        for (auto* r : rows) { r->setBounds (0, y, getWidth(), rowH); y += rowH; }
    }
};

//==============================================================================
/** 채팅 목록. 시스템(가운데 회색 "오후 9:02 · …"), 남(이름 + 본문 15px), 나(오른쪽 노란 말풍선 220, 실패 줄) */
struct JamScreen::ChatView : juce::Component
{
    explicit ChatView (JamScreen& o) : owner (o) {}

    struct Item
    {
        core::ChatChannel::Message msg;
        juce::Rectangle<int> bounds, resendArea;
        juce::TextLayout layout;
    };
    std::vector<Item> items;

    static juce::TextLayout makeLayout (const juce::String& text, int width, juce::Colour ink, float px, int weight)
    {
        juce::AttributedString a;
        a.append (text, font (weight, px), ink);
        a.setLineSpacing (px * 0.5f);
        a.setWordWrap (juce::AttributedString::byChar);
        juce::TextLayout t;
        t.createLayout (a, (float) width);
        return t;
    }

    int layoutFor (int width)
    {
        const auto& msgs = owner.jam.chat().getMessages();
        items.clear();
        int y = 12;
        const int inner = width - 36;
        for (auto& m : msgs)
        {
            Item it;
            it.msg = m;
            using K = core::ChatChannel::Message::Kind;
            if (m.kind == K::system)
            {
                if (m.fromUser.isNotEmpty()) it.msg.text = owner.memberName (m.fromUser) + TXT ("님이 입장했어요");
                it.layout = makeLayout (clockText (m.time) + " · " + it.msg.text, inner, col::inkFaint, 12.0f, 400);
                const int h = (int) std::ceil (it.layout.getHeight());
                it.bounds = { 18, y, inner, h };
                y += h + 14;
            }
            else if (m.kind == K::mine)
            {
                const int maxText = juce::jmin (bubbleMaxW, inner) - 24;
                auto probe = makeLayout (m.text, maxText, col::ink, 15.0f, 400);
                float tw = 0.0f;
                for (int i = 0; i < probe.getNumLines(); ++i)
                    tw = juce::jmax (tw, probe.getLine (i).getLineBoundsX().getLength());
                const int bw = juce::jmin (maxText, (int) std::ceil (tw) + 1);
                it.layout = makeLayout (m.text, bw, col::ink, 15.0f, 400);
                const int h = (int) std::ceil (it.layout.getHeight()) + 16;
                it.bounds = { 18 + inner - (bw + 24), y, bw + 24, h };
                y += h;
                if (m.delivery == core::ChatChannel::Message::Delivery::failed)
                {
                    it.resendArea = { 18, y + 4, inner, 16 };
                    y += 4 + 16;
                }
                y += 14;
            }
            else
            {
                it.layout = makeLayout (m.text, inner, col::ink, 15.0f, 400);
                const int h = 16 + 3 + (int) std::ceil (it.layout.getHeight());
                it.bounds = { 18, y, inner, h };
                y += h + 14;
            }
            items.push_back (std::move (it));
        }
        return y - 14 + 12;
    }

    void paint (juce::Graphics& g) override
    {
        using K = core::ChatChannel::Message::Kind;
        for (auto& it : items)
        {
                const auto& m = it.msg;
            if (m.kind == K::system)
            {
                juce::AttributedString a;   // 가운데 정렬
                a.append (clockText (m.time) + " · " + m.text, font (400, 12.0f), col::inkFaint);
                a.setJustification (juce::Justification::centredTop);
                juce::TextLayout t;
                t.createLayout (a, (float) it.bounds.getWidth());
                t.draw (g, it.bounds.toFloat());
            }
            else if (m.kind == K::mine)
            {
                auto b = it.bounds.toFloat();
                juce::Path p;
                p.addRoundedRectangle (b.getX(), b.getY(), b.getWidth(), b.getHeight(), 10.0f, 10.0f, true, true, true, false);
                // 오른쪽 아래만 모서리 2
                juce::Path corner;
                corner.addRoundedRectangle (b.getRight() - 12.0f, b.getBottom() - 12.0f, 12.0f, 12.0f, 2.0f);
                g.setColour (col::accentSoft);
                g.fillPath (p);
                g.fillPath (corner);
                it.layout.draw (g, b.reduced (12.0f, 8.0f));
                if (m.delivery == core::ChatChannel::Message::Delivery::failed)
                {
                    const auto f = font (400, 12.0f);
                    const auto t1 = TXT ("일부 멤버에게 전달되지 않았어요 · ");
                    const auto t2 = TXT ("다시 보내기");
                    const float w1 = textWidth (f, t1), w2 = textWidth (f, t2);
                    const float x0 = (float) it.resendArea.getRight() - w1 - w2 - 2.0f;
                    g.setFont (f);
                    g.setColour (col::danger);
                    g.drawText (t1, juce::Rectangle<float> (x0, (float) it.resendArea.getY(), w1 + 1.0f, 16.0f), juce::Justification::centredLeft, false);
                    g.setColour (col::ink);
                    g.drawText (t2, juce::Rectangle<float> (x0 + w1, (float) it.resendArea.getY(), w2 + 2.0f, 16.0f), juce::Justification::centredLeft, false);
                    g.fillRect (x0 + w1, (float) it.resendArea.getY() + 14.0f, w2, 1.0f);
                }
            }
            else
            {
                g.setColour (col::inkSub);
                g.setFont (font (600, 13.0f));
                g.drawText (owner.memberName (m.fromUser), it.bounds.withHeight (16), juce::Justification::centredLeft, true);
                it.layout.draw (g, it.bounds.withTrimmedTop (19).toFloat());
            }
        }
        if (items.empty())
        {
            g.setColour (col::inkFaint);
            g.setFont (font (400, 12.0f));
            g.drawText (TXT ("방 멤버끼리만 보이는 채팅이에요"), getLocalBounds().reduced (18), juce::Justification::centred, true);
        }
    }

    /** 전송 실패 줄의 "다시 보내기" 글자 자리 (paint 와 같은 계산) */
    static juce::Rectangle<float> resendLink (const Item& it)
    {
        if (it.resendArea.isEmpty()) return {};
        const auto f = font (400, 12.0f);
        const float w2 = textWidth (f, TXT ("다시 보내기"));
        return { (float) it.resendArea.getRight() - w2 - 2.0f, (float) it.resendArea.getY(), w2 + 2.0f, 16.0f };
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        for (auto& it : items)
            if (resendLink (it).contains (e.position))
                owner.jam.chat().resend (it.msg.id);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        bool onLink = false;
        for (auto& it : items)
            onLink = onLink || resendLink (it).contains (e.position);
        setMouseCursor (onLink ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }

    JamScreen& owner;
};

//==============================================================================
/** 채팅 카드 위에 겹쳐 그리는 것: 목록과 입력 줄 사이 구분선(디자인 border-top 1px #E0E0E0),
    입력 남은 글자(100 B 미만일 때)·안내 문구. 채팅 카드의 자식이라 화면 위 모달·덮개보다 아래에 그려진다 */
struct JamScreen::ChatDecor : juce::Component
{
    explicit ChatDecor (JamScreen& o) : owner (o) { setInterceptsMouseClicks (false, false); }

    void paint (juce::Graphics& g) override
    {
        if (! owner.chatOpen) return;
        if (owner.chatViewport.isVisible())   // resized() 가 목록 아래 1px 를 비워 둔다
        {
            const auto vb = owner.chatViewport.getBounds();
            g.setColour (col::line);
            g.fillRect (vb.getX(), vb.getBottom(), vb.getWidth(), 1);
        }
        const int left = core::ChatChannel::maxBytes - core::ChatChannel::byteLength (owner.chatInput.getText());
        if (left < 100 || owner.chatNote.isNotEmpty())
        {
            const auto ib = owner.chatInput.getBounds();
            const bool note = owner.chatNote.isNotEmpty();
            g.setFont (font (400, 12.0f));
            g.setColour (note ? col::inkSub : col::inkFaint);
            g.drawText (note ? owner.chatNote : juce::String (left), ib.getX(), ib.getY() - 18, ib.getWidth(), 14,
                        note ? juce::Justification::centredLeft : juce::Justification::centredRight, true);
        }
    }

    JamScreen& owner;
};

//==============================================================================
/** 친구 목록 (합주 중): 초대 → 3초 "취소"(N초 뒤 초대가 가요) → "초대됨" */
struct JamScreen::FriendList : juce::Component
{
    struct Row : juce::Component, juce::SettableTooltipClient   // 상태가 말줄임으로 잘리면 툴팁으로 전체 문구
    {
        Row (JamScreen& o, const core::Friend& f) : owner (o), fr (f)
        {
            using S = Avatar::Status;
            const auto st = fr.status == "in_room" ? S::inRoom : (fr.status == "online" ? S::online : S::offline);
            avatar.setPart (fr.user.part.value_or ("other"));
            avatar.setStatus (st);
            avatar.setDimmed (st == S::offline);
            avatar.setInterceptsMouseClicks (false, false);
            addAndMakeVisible (avatar);
            setMouseCursor (juce::MouseCursor::PointingHandCursor);   // 누르면 프로필
            if (fr.canInvite)
            {
                addAndMakeVisible (invite);
                invite.onPress = [this]
                {
                    auto& home = owner.core.home();
                    const auto id = fr.user.id;
                    const auto st = home.inviteState (id).phase;
                    using P = core::HomeController::InviteState::Phase;
                    if (st == P::pending) home.cancelInvite (id);
                    else if (st == P::none)
                    {
                        juce::Component::SafePointer<JamScreen> safe (&owner);
                        home.invite (id, [safe] (core::Result<core::InviteSent>) { if (safe != nullptr) safe->rebuildFriends(); });
                    }
                    refresh();
                };
            }
            refresh();
        }

        juce::String statusText() const
        {
            using P = core::HomeController::InviteState::Phase;
            const auto st = owner.core.home().inviteState (fr.user.id);
            if (st.phase == P::pending)
            {
                const auto left = std::max<juce::int64> (1, (st.deliverAtMs - owner.core.scheduler().nowMs() + 999) / 1000);
                return juce::String (left) + TXT ("초 뒤 초대가 가요");
            }
            if (st.phase == P::sent) return TXT ("초대 보냄");
            if (fr.status == "in_room") return fr.room ? fr.room->title + TXT ("에서 합주 중") : TXT ("합주 중");   // 비공개 방이면 위치를 숨긴다 (API 2.10)
            if (fr.status == "online") return TXT ("온라인");
            return msg::lastSeen (fr.lastSeenAt ? core::json::parseIsoTime (*fr.lastSeenAt) : 0, owner.core.scheduler().nowMs());
        }

        void refresh()
        {
            using P = core::HomeController::InviteState::Phase;
            const auto st = owner.core.home().inviteState (fr.user.id).phase;
            invite.setPending (st == P::pending);
            invite.setDimmed (st == P::sent);
            invite.setButtonText (st == P::pending ? TXT ("취소") : (st == P::sent ? TXT ("초대됨") : TXT ("초대")));
            resized();
            repaint();
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
            const int right = (invite.isVisible() ? invite.getX() : r.getRight() - 9) - 9;
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
            const int w = invite.getIdealWidth();
            invite.setBounds (r.removeFromRight (w).withSizeKeepingCentre (w, metric::smallH));
        }

        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { repaint(); }
        void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked()) owner.openProfile (fr.user.id); }

        JamScreen& owner;
        core::Friend fr;
        Avatar avatar { 32.0f, 16.0f };
        Button invite { Button::Kind::small, TXT ("초대") };
    };

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
        g.drawFittedText (TXT ("합주실에서 만난 사람에게 친구 요청을 보내보세요"), r.withY (cy).withHeight (36), juce::Justification::centredTop, 2, 1.0f);
    }

    void resized() override
    {
        int y = 6;
        for (auto* r : rows) { r->setBounds (0, y, getWidth(), 44); y += 44; }
    }

    int preferredHeight() const { return rows.isEmpty() ? 0 : 12 + rows.size() * 44; }
    juce::OwnedArray<Row> rows;
};

//==============================================================================
/** 파일 플레이어 카드 (여백 12·18, 간격 12). 비면 "재생할 파일 없음" + 파일 열기 */
struct JamScreen::FilePlayer : juce::Component
{
    explicit FilePlayer (JamScreen& o) : owner (o), position (0.0f, 1.0f, true), volume (core::Engine::fileMinDb, core::Engine::fileMaxDb)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &openButton, &play, &position, &close, &volume })
            addChildComponent (c);
        openButton.onPress = [this] { chooseFile(); };
        play.onClick = [this] { owner.jam.setFilePlaying (! owner.jam.isFilePlaying()); };
        close.onClick = [this] { owner.jam.closeFile(); };
        close.setTooltip (TXT ("파일 닫기"));
        position.setTitle (TXT ("드래그해서 위치 이동"));
        position.onChange = [this] (float f) { owner.jam.setFilePosition (f * owner.jam.fileLength()); };
        volume.setTitle (TXT ("더블클릭하면 0 dB"));
        volume.onChange = [this] (float db) { owner.jam.setFileGainDb (db); repaint(); };
        refresh();
    }

    bool loaded() const { return owner.jam.hasFile(); }
    // 파일이 없을 때 설명: 한 줄에 안 들어가면 줄을 바꾼다 (플러그인 폭 등). 높이도 따라 늘어난다
    static juce::String emptyDesc() { return TXT ("파일을 열면 내 소리와 섞여 모두에게 들려요"); }
    static constexpr int descLineH = 17;
    int emptyTextW (int width) const { return width - 18 - openButton.getIdealWidth() - 9 - 18; }
    int emptyDescH (int width) const
    {
        return juce::jmax (descLineH, (int) std::ceil (meon::paragraphHeightWithEmoji (emptyDesc(), font (400, 12.0f),
                                                                                       (float) emptyTextW (width), (float) descLineH)));
    }
    int preferredHeight (int width) const { return loaded() ? 12 + 19 + 12 + 19 + 12 + 19 + 12 : 12 + 17 + 5 + emptyDescH (width) + 12; }

    void chooseFile()
    {
        chooser = std::make_unique<juce::FileChooser> (TXT ("재생할 파일"), juce::File(), "*.wav;*.aif;*.aiff;*.mp3;*.flac;*.ogg;*.m4a");
        juce::Component::SafePointer<FilePlayer> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
        {
            if (safe == nullptr) return;
            const auto f = fc.getResult();
            if (f.existsAsFile()) safe->owner.jam.loadFile (f);
        });
    }

    void refresh()
    {
        const bool l = loaded();
        openButton.setButtonText (l ? TXT ("파일 변경") : TXT ("파일 열기"));
        openButton.setVisible (true);
        for (auto* c : std::initializer_list<juce::Component*> { &play, &position, &close, &volume })
            c->setVisible (l);
        if (l)
        {
            if (! position.isDragging())
                position.setValue ((float) (owner.jam.fileLength() > 0.0 ? owner.jam.filePosition() / owner.jam.fileLength() : 0.0));
            if (! volume.isDragging()) volume.setValue (owner.jam.fileGainDb());
            play.setTooltip (owner.jam.isFilePlaying() ? TXT ("정지") : TXT ("재생"));
        }
        resized();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (col::white);
        g.fillRoundedRectangle (r, metric::radius);
        g.setColour (col::line);
        g.drawRoundedRectangle (r.reduced (0.5f), metric::radius, 1.0f);
        if (! loaded())
        {
            const int w = emptyTextW (getWidth());
            g.setColour (col::inkFaint);
            g.setFont (font (600, 14.0f));
            g.drawText (TXT ("재생할 파일 없음"), 18, 12, w, 17, juce::Justification::centredLeft, true);
            meon::drawParagraphWithEmoji (g, emptyDesc(), font (400, 12.0f), col::inkFaint,
                                          juce::Rectangle<float> (18.0f, (float) (12 + 17 + 5), (float) w, (float) emptyDescH (getWidth())),
                                          (float) descLineH, juce::Justification::topLeft);
            return;
        }
        const auto tf = font (400, 12.0f);
        const auto time = mmss (owner.jam.filePosition()) + " / " + mmss (owner.jam.fileLength());
        const int tw = (int) std::ceil (textWidth (tf, "00:00 / 00:00"));
        const int timeX = openButton.getX() - 9 - tw;
        g.setColour (col::ink);
        g.setFont (font (600, 14.0f));
        g.drawText (owner.jam.fileName(), 18, 12, timeX - 9 - 18, 19, juce::Justification::centredLeft, true);
        g.setColour (col::inkSub);
        g.setFont (tf);
        g.drawText (time, timeX, 12, tw, 19, juce::Justification::centredRight, false);
        // 재생 / 정지 모양
        auto pb = play.getBounds().toFloat();
        g.setColour (col::ink);
        if (owner.jam.isFilePlaying())
            g.fillRoundedRectangle (pb.withSizeKeepingCentre (11.0f, 11.0f), 2.0f);
        else
            icons::draw (g, icons::Id::play, pb.withSizeKeepingCentre (12.0f, 14.0f).translated (1.0f, 0.0f), col::ink);
        // 볼륨 아이콘과 값
        const int vy = getHeight() - 12 - 19;
        icons::draw (g, icons::Id::speaker, juce::Rectangle<float> (18.0f, (float) vy, 19.0f, 19.0f).withSizeKeepingCentre (14.0f, 14.0f), col::inkSub);
        g.setColour (col::inkSub);
        g.setFont (tf);
        g.drawText (DbSlider::dbText (owner.jam.fileGainDb()), getWidth() - 18 - 48, vy, 48, 19, juce::Justification::centredRight, false);
    }

    void resized() override
    {
        const int w = openButton.getIdealWidth();
        openButton.setBounds (getWidth() - 18 - w, loaded() ? 12 : (getHeight() - metric::smallH) / 2, w, metric::smallH);
        int y = 12 + 19 + 12;
        play.setBounds (18, y, 19, 19);
        close.setBounds (getWidth() - 18 - 19, y, 19, 19);
        position.setBounds (18 + 19 + 9, y, getWidth() - 36 - 19 - 9 - 19 - 9, 19);
        y += 19 + 12;
        volume.setBounds (18 + 19 + 9, y, getWidth() - 36 - 19 - 9 - 48 - 9, 19);
    }

    JamScreen& owner;
    Button openButton { Button::Kind::small, TXT ("파일 열기") };
    struct PlainButton : juce::Button
    {
        PlainButton() : juce::Button ({}) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
        void paintButton (juce::Graphics&, bool, bool) override {}   // 모양은 FilePlayer::paint 가 그린다
    } play;
    DbSlider position, volume;
    IconButton close { (int) icons::Id::close12, IconButton::Style::bare19, 10.0f };
    std::unique_ptr<juce::FileChooser> chooser;
};

//==============================================================================
JamScreen::JamScreen (core::MeonCore& c) : core (c), jam (*c.jam()), plugin (c.isPlugin())
{
    setOpaque (true);
    setWantsKeyboardFocus (true);

    // 헤더
    addAndMakeVisible (roomTitle);
    privateTag = std::make_unique<Tag> (TXT ("비공개"));
    addChildComponent (*privateTag);
    codeBox = std::make_unique<CodeBox>();
    addChildComponent (*codeBox);
    codeBox->onWidthChange = [this] { resized(); };
    addAndMakeVisible (settings);
    addAndMakeVisible (leave);
    addChildComponent (gear);
    addChildComponent (requests);
    gear.setTooltip (TXT ("방 설정"));
    requests.setTooltip (TXT ("입장 요청"));
    gear.setInk (col::inkFaint, col::ink);
    gear.onClick = [this] { showModal (std::make_unique<RoomSettingsModal> (core)); };
    requests.onClick = [this] { toggleRequests(); };
    settings.onPress = [this] { if (onOpenSettings) onOpenSettings(); };
    leave.onPress = [this]
    {
        juce::Component::SafePointer<JamScreen> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->core.home().leaveRoom(); });   // 이 화면이 지워진다
    };

    // 멤버
    addAndMakeVisible (membersCard);
    memberList = std::make_unique<MemberList>();
    memberViewport.setViewedComponent (memberList.get(), false);
    memberViewport.setScrollBarsShown (true, false);
    memberViewport.setScrollBarThickness (8);
    membersCard.addAndMakeVisible (memberViewport);

    // 내 오디오 · 모니터 (옆으로 접기)
    addAndMakeVisible (sendCard);
    addAndMakeVisible (monCard);
    sendCard.setTitle (TXT ("내 오디오"), true, true);
    monCard.setTitle (TXT ("모니터"), true, true);
    sendCard.onToggle = [this] (bool o) { sendOpen = o; resized(); };
    monCard.onToggle = [this] (bool o) { monOpen = o; resized(); };
    for (auto* comp : std::initializer_list<juce::Component*> { &sendMute, &sendGain, &inputMeter })
        sendCard.addAndMakeVisible (comp);
    for (auto* comp : std::initializer_list<juce::Component*> { &monMute, &master, &outputMeter })
        monCard.addAndMakeVisible (comp);
    silenceRow = std::make_unique<SilenceRow>();
    sendCard.addChildComponent (*silenceRow);
    sendMute.setTooltip (TXT ("다른 사람에게 보내는 내 소리"));
    monMute.setTooltip (TXT ("내가 듣는 모든 소리"));
    sendGain.setTitle (TXT ("송출 레벨 · 더블클릭하면 0 dB"));
    master.setTitle (TXT ("마스터 볼륨 · 더블클릭하면 0 dB"));
    sendMute.onPress = [this] { jam.setSendMuted (! jam.isSendMuted()); refreshAudio(); };
    monMute.onPress = [this] { jam.setMonitorMuted (! jam.isMonitorMuted()); refreshAudio(); };
    sendGain.onChange = [this] (float db) { jam.setSendGainDb (db); };
    master.onChange = [this] (float db) { jam.setMasterDb (db); };
    silenceRow->open.onPress = [this] { if (onOpenAudioSettings) onOpenAudioSettings(); };
    // 송출 정지 중에는 보내는 신호가 0 이므로 입력 레벨을 회색으로 보여 준다 (refreshAudio)
    inputMeter.meter.setSource (juce::jlimit (1, 2, jam.sendChannelCount()),
                                [this] (int ch) { return jam.isSendMuted() ? jam.inputLevel (ch) : jam.sendLevel (ch); });
    outputMeter.meter.setSource (2, [this] (int ch) { return jam.monitorLevel (ch); });

    // 채팅
    addAndMakeVisible (chatCard);
    chatCard.setTitle (TXT ("채팅"), true);
    chatCard.onToggle = [this] (bool o) { chatOpen = o; resized(); };
    chatView = std::make_unique<ChatView> (*this);
    chatViewport.setViewedComponent (chatView.get(), false);
    chatViewport.setScrollBarsShown (true, false);
    chatViewport.setScrollBarThickness (8);
    chatCard.addAndMakeVisible (chatViewport);
    chatCard.addAndMakeVisible (chatInput);
    chatCard.addAndMakeVisible (chatSend);
    chatDecor = std::make_unique<ChatDecor> (*this);
    chatCard.addAndMakeVisible (*chatDecor);
    chatInput.setBorderless (true);
    chatInput.setFontPx (14.0f);
    chatInput.setMaxBytes (core::ChatChannel::maxBytes);
    chatInput.onReturn = [this] { sendChat(); };
    chatInput.onChange = [this] { chatNote = {}; chatCard.repaint(); resized(); };
    chatSend.onPress = [this] { sendChat(); };

    // 친구
    addAndMakeVisible (friendsCard);
    friendsCard.setTitle (TXT ("친구"), true);
    friendsCard.onToggle = [this] (bool o) { friendsOpen = o; resized(); };
    friendList = std::make_unique<FriendList>();
    friendViewport.setViewedComponent (friendList.get(), false);
    friendViewport.setScrollBarsShown (true, false);
    friendViewport.setScrollBarThickness (8);
    friendsCard.addAndMakeVisible (friendViewport);

    // 파일
    filePlayer = std::make_unique<FilePlayer> (*this);
    addAndMakeVisible (*filePlayer);

    core.store().addListener (this);
    jam.addListener (this);
    jam.chat().addListener (this);
    refreshHeader();
    rebuildMembers();
    rebuildFriends();
    refreshAudio();
    refreshChat();
    startTimer (250);
}

JamScreen::~JamScreen()
{
    stopTimer();
    jam.chat().removeListener (this);
    jam.removeListener (this);
    core.store().removeListener (this);
}

juce::String JamScreen::memberName (const juce::String& userId) const
{
    for (auto& m : jam.members())
        if (m.userId == userId && m.nickname.isNotEmpty()) { knownNames[userId] = m.nickname; return m.nickname; }
    if (auto it = knownNames.find (userId); it != knownNames.end()) return it->second;
    return TXT ("멤버");
}

bool JamScreen::showBanner() const
{
    return core.store().state().link == core::Realtime::Link::reconnecting;
}

//==============================================================================
void JamScreen::refreshHeader()
{
    const auto& room = core.store().state().currentRoom;
    if (! room) return;
    const bool host = core.store().state().amHost();
    gear.setVisible (host);
    requests.setVisible (host && room->isApproval());
    requests.setDot (! core.store().state().joinRequests.empty());
    if (! requests.isVisible() && popover != nullptr) { popover.reset(); requestList.reset(); }
    roomTitle.setText (room->title);
    privateTag->setVisible (! room->isPublic());
    codeBox->setVisible (! room->isPublic() && room->inviteCode.has_value());
    codeBox->setCode (room->inviteCode.value_or (""));
    resized();
    repaint();
}

void JamScreen::rebuildMembers()
{
    const auto list = jam.members();
    for (auto& m : list)
        if (m.nickname.isNotEmpty()) knownNames[m.userId] = m.nickname;
    bool same = (int) list.size() == memberList->rows.size();
    for (size_t i = 0; same && i < list.size(); ++i)
        same = memberList->rows[(int) i]->member.userId == list[i].userId && memberList->rows[(int) i]->member.isMe == list[i].isMe;
    if (same)
    {
        for (size_t i = 0; i < list.size(); ++i) memberList->rows[(int) i]->update (list[i]);
    }
    else
    {
        memberList->rows.clear();
        for (auto& m : list)
            memberList->addAndMakeVisible (memberList->rows.add (new MemberRow (*this, m)));
    }
    resized();
}

void JamScreen::jamStatsChanged()
{
    // 송출 정지·전체 음소거가 이 화면 밖에서 바뀌면(홈, 단축키 처리 전, 플러그인의 DAW 상태) 0.4초 안에 따라간다
    if (sendMute.isOn() != jam.isSendMuted() || monMute.isOn() != jam.isMonitorMuted()) refreshAudio();
    const auto list = jam.members();
    if ((int) list.size() != memberList->rows.size()) { rebuildMembers(); return; }
    for (size_t i = 0; i < list.size(); ++i)
    {
        auto* row = memberList->rows[(int) i];
        if (row->member.userId != list[i].userId || row->member.isMe != list[i].isMe) { rebuildMembers(); return; }
        row->update (list[i]);
    }
}

void JamScreen::rebuildFriends()
{
    auto list = core.store().state().friends;
    auto rank = [] (const core::Friend& f) { return f.status == "online" ? 0 : (f.status == "in_room" ? 1 : 2); };
    std::stable_sort (list.begin(), list.end(), [&] (const core::Friend& a, const core::Friend& b) { return rank (a) < rank (b); });
    friendList->rows.clear();
    for (auto& f : list)
        friendList->addAndMakeVisible (friendList->rows.add (new FriendList::Row (*this, f)));
    resized();
    friendList->repaint();
}

void JamScreen::refreshAudio()
{
    const bool sm = jam.isSendMuted(), mm = jam.isMonitorMuted();
    sendMute.setOn (sm);
    sendMute.setButtonText (sm ? TXT ("송출 재개") : TXT ("송출 정지"));
    sendGain.setMuted (sm);
    inputMeter.meter.setGreyed (sm);
    if (! sendGain.isDragging()) sendGain.setValue (jam.getSendGainDb());
    monMute.setOn (mm);
    monMute.setButtonText (mm ? TXT ("음소거 해제") : TXT ("전체 음소거"));
    master.setMuted (mm);
    if (! master.isDragging()) master.setValue (jam.getMasterDb());
    outputMeter.meter.setActive (! mm);
    resized();
}

void JamScreen::refreshChat()
{
    const auto& view = chatViewport;
    chatAtBottom = view.getViewPositionY() + view.getHeight() >= chatView->getHeight() - 4;
    const int w = chatViewport.getWidth() - (chatViewport.isVerticalScrollBarShown() ? chatViewport.getScrollBarThickness() : 0);
    if (w <= 0) return;
    const int h = chatView->layoutFor (w);
    chatView->setSize (w, juce::jmax (chatViewport.getHeight(), h));
    if (chatAtBottom) chatViewport.setViewPosition (0, chatView->getHeight() - chatViewport.getHeight());
    chatView->repaint();
}

void JamScreen::sendChat()
{
    using R = core::ChatChannel::SendResult;
    const auto text = chatInput.getText();
    const auto r = jam.chat().sendText (text);
    if (r == R::sent || r == R::failed || r == R::localOnly)   // localOnly: 받을 사람이 없어 내 목록에만
    {
        chatInput.setText ({});
        chatNote = {};
        chatAtBottom = true;
    }
    resized();
    repaint();
}

void JamScreen::jamFileChanged()
{
    filePlayer->refresh();
    resized();
}

void JamScreen::stateChanged (core::StateKey k)
{
    using K = core::StateKey;
    if (k == K::currentRoom || k == K::joinRequests || k == K::me) { refreshHeader(); jamStatsChanged(); }
    else if (k == K::friends) rebuildFriends();
    else if (k == K::link) { resized(); repaint(); }
}

void JamScreen::timerCallback()
{
    // 초대 카운트다운·파일 위치·남은 글자
    for (auto* r : friendList->rows) r->refresh();
    if (jam.hasFile()) filePlayer->refresh();
    if (silenceRow->isVisible() != jam.isInputSilent()) resized();
}

//==============================================================================
void JamScreen::paint (juce::Graphics& g)
{
    g.fillAll (col::white);
    // 상단 바(·연결 끊김 배너) 아래 작업 영역 (패널들을 감싸는 곳)
    const int canvasTop = headerLineY + 1 + (showBanner() ? bannerH : 0);
    g.setColour (col::surfaceCanvas);
    g.fillRect (0, canvasTop, getWidth(), getHeight() - canvasTop);
    const float logoW = meon::logoWidth (metric::logoDrawH);
    meon::drawLogo (g, juce::Rectangle<float> ((float) metric::padX, 12.0f, logoW, (float) metric::logoH), metric::logoDrawH, col::ink,
                    juce::Justification::centredLeft);
    // 로고 오른쪽 세로 구분선은 없다 (디자인 파일과 다른 결정, REMAKE_PLAN 3.6). 로고 끝에서 18px 뒤에 방 제목
    g.setColour (col::line);
    g.fillRect (0, headerLineY, getWidth(), 1);

    if (showBanner())
    {
        const int y = headerLineY + 1;
        g.setColour (col::accentSoft);
        g.fillRect (0, y, getWidth(), bannerH - 1);
        g.setColour (juce::Colour (0xFFF0DE8Au));
        g.fillRect (0, y + bannerH - 1, getWidth(), 1);
        const auto f1 = font (500, 13.0f);
        const auto t1 = TXT ("서버 연결 끊김 · 재연결 중");
        const int w1 = (int) std::ceil (textWidth (f1, t1)) + 1;
        g.setColour (col::ink);
        g.setFont (f1);
        g.drawText (t1, metric::padX, y, w1, bannerH - 1, juce::Justification::centredLeft, false);
        g.setColour (col::inkSub);
        g.setFont (font (400, 12.0f));
        g.drawText (TXT ("합주 소리는 계속 들려요"), metric::padX + w1 + 9, y, getWidth() - w1 - 50, bannerH - 1, juce::Justification::centredLeft, true);
    }
}

void JamScreen::resized()
{
    auto area = getLocalBounds();
    // 헤더
    {
        const float logoW = meon::logoWidth (metric::logoDrawH);
        auto h = area.withHeight (metric::headerH).reduced (metric::padX, 0);
        const int lw = leave.getIdealWidth(), sw = settings.getIdealWidth();
        leave.setBounds (h.removeFromRight (lw).withSizeKeepingCentre (lw, metric::buttonH));
        h.removeFromRight (metric::gap);
        settings.setBounds (h.removeFromRight (sw).withSizeKeepingCentre (sw, metric::buttonH));
        if (requests.isVisible())
        {
            h.removeFromRight (metric::gap);
            requests.setBounds (h.removeFromRight (32).withSizeKeepingCentre (32, 32));
        }
        if (codeBox->isVisible())
        {
            h.removeFromRight (metric::gap);
            const int cw = codeBox->idealWidth();
            codeBox->setBounds (h.removeFromRight (cw).withSizeKeepingCentre (cw, metric::buttonH));
        }
        const int tx = (int) (metric::padX + logoW + 18.0f);
        const int tagW = (privateTag->isVisible() ? privateTag->getIdealWidth() + 6 : 0) + (gear.isVisible() ? 24 + 6 : 0);
        const int titleMax = juce::jmax (40, h.getRight() - 18 - tx - tagW);
        const int tw = juce::jmin (titleMax, roomTitle.getIdealWidth() + 2);
        roomTitle.setBounds (tx, 0, tw, metric::headerH);
        int gx = tx + tw + 6;
        if (privateTag->isVisible())
        {
            privateTag->setBounds (gx, (metric::headerH - 17) / 2, privateTag->getIdealWidth(), 17);
            gx += privateTag->getIdealWidth() + 6;
        }
        gear.setBounds (gx, (metric::headerH - 24) / 2, 24, 24);
    }
    area.removeFromTop (headerLineY + 1 + (showBanner() ? bannerH : 0));
    area.reduce (metric::padX, metric::padX);

    // 오른쪽: 채팅 · 친구 · 파일
    const int sideW = plugin ? 240 : 320;
    auto side = area.removeFromRight (sideW);
    area.removeFromRight (metric::padX);
    {
        filePlayer->setBounds (side.removeFromBottom (filePlayer->preferredHeight (side.getWidth())));
        side.removeFromBottom (metric::gap);
        const int closedH = 48;
        int chatH, frH;
        const int avail = side.getHeight() - metric::gap;
        if (chatOpen && friendsOpen) { chatH = avail - avail / 2; frH = avail / 2; }
        else if (chatOpen)           { frH = closedH; chatH = avail - frH; }
        else if (friendsOpen)        { chatH = closedH; frH = avail - chatH; }
        else                         { chatH = frH = closedH; }
        chatCard.setBounds (side.removeFromTop (chatH));
        side.removeFromTop (metric::gap);
        friendsCard.setBounds (side.removeFromTop (frH));
        chatCard.setOpen (chatOpen);
        friendsCard.setOpen (friendsOpen);

        // 채팅 안: 목록 + 입력 줄 (12·18, 높이 44)
        auto cb = chatCard.contentBounds();
        chatViewport.setVisible (chatOpen);
        chatInput.setVisible (chatOpen);
        chatSend.setVisible (chatOpen);
        auto inputRow = cb.removeFromBottom (44);
        chatViewport.setBounds (cb.withTrimmedBottom (1));
        chatDecor->setBounds (chatCard.getLocalBounds());
        chatDecor->toFront (false);
        chatDecor->repaint();
        auto ir = inputRow.reduced (metric::padX, 12);
        const int sw = chatSend.getIdealWidth();
        chatSend.setBounds (ir.removeFromRight (sw));
        ir.removeFromRight (8);
        chatInput.setBounds (ir);
        refreshChat();

        auto fb = friendsCard.contentBounds();
        friendViewport.setVisible (friendsOpen);
        friendViewport.setBounds (fb);
        friendList->setSize (fb.getWidth(), juce::jmax (fb.getHeight(), friendList->preferredHeight()));
        friendList->resized();
    }

    // 왼쪽: 멤버 (위) · 내 오디오 + 모니터 (아래)
    {
        const int closedW = 150;
        int sendW, monW;
        const int availW = area.getWidth() - metric::gap;
        if (sendOpen && monOpen)  { sendW = availW - availW / 2; monW = availW / 2; }
        else if (sendOpen)        { monW = juce::jmin (availW / 2, 84 + 36 + 120); sendW = availW - monW; }
        else if (monOpen)         { sendW = juce::jmin (availW / 2, 84 + 36 + 120); monW = availW - sendW; }
        else                      { sendW = monW = juce::jmin (availW / 2, 84 + 36 + 120); juce::ignoreUnused (closedW); }
        const int warnH = silenceRowH (sendW - 2);   // 카드 테두리 안쪽 폭
        auto bottom = area.removeFromBottom (bottomCardH + warnH);
        area.removeFromBottom (metric::padX);
        sendCard.setBounds (bottom.removeFromLeft (sendW));
        bottom.removeFromLeft (metric::gap);
        monCard.setBounds (bottom.removeFromLeft (monW).withHeight (bottomCardH));
        sendCard.setOpen (sendOpen);
        monCard.setOpen (monOpen);

        auto layoutRow = [] (juce::Rectangle<int> row, bool open, Button& b, DbSlider& s, MeterBox& m, bool pluginSize)
        {
            row = row.reduced (metric::padX, 12);
            const int mw = open ? ((pluginSize || row.getWidth() < 400) ? juce::jlimit (84, 150, row.getWidth() / 3) : 150) : row.getWidth();
            m.setBounds (row.removeFromRight (mw).withSizeKeepingCentre (mw, metric::smallH));
            b.setVisible (open);
            s.setVisible (open);
            if (! open) return;
            row.removeFromRight (12);
            const int bw = b.getIdealWidth();
            b.setBounds (row.removeFromLeft (bw).withSizeKeepingCentre (bw, metric::smallH));
            row.removeFromLeft (12);
            s.setBounds (row.withSizeKeepingCentre (row.getWidth(), metric::smallH));
        };
        auto sc = sendCard.contentBounds();
        layoutRow (sc.removeFromTop (43), sendOpen, sendMute, sendGain, inputMeter, plugin);
        const bool warn = jam.isInputSilent();
        silenceRow->setVisible (warn);
        silenceRow->repaint();
        if (warn) silenceRow->setBounds (sc.withHeight (warnH));
        layoutRow (monCard.contentBounds().removeFromTop (43), monOpen, monMute, master, outputMeter, plugin);

        const int listH = memberList->rows.size() * rowH;
        membersCard.setBounds (area.withHeight (juce::jmin (area.getHeight(), listH + 2)));
        memberViewport.setBounds (membersCard.getLocalBounds().reduced (1));
        memberList->setSize (memberViewport.getWidth() - (listH + 2 > area.getHeight() ? memberViewport.getScrollBarThickness() : 0), listH);
        memberList->resized();
    }
    if (modal != nullptr) modal->setBounds (getLocalBounds());
    if (popover != nullptr && requestList != nullptr)
        popover->setBounds (getWidth() - 12 - 340, headerLineY + 10, 340, 44 + requestList->getPreferredHeight());
    repaint();
}

void JamScreen::showModal (std::unique_ptr<Modal> m)
{
    popover.reset();
    requestList.reset();
    modal = std::move (m);
    juce::Component::SafePointer<JamScreen> safe (this);
    modal->onClose = [safe] { juce::MessageManager::callAsync ([safe] { if (safe != nullptr) { safe->modal.reset(); safe->grabKeyboardFocus(); } }); };
    addAndMakeVisible (*modal);
    modal->setBounds (getLocalBounds());
    modal->toFront (true);
}

void JamScreen::toggleRequests()
{
    if (popover != nullptr) { popover.reset(); requestList.reset(); return; }
    requestList = std::make_unique<JoinRequestList> (core);
    popover = std::make_unique<Popover> (TXT ("입장 요청"));
    juce::Component::SafePointer<JamScreen> safe (this);
    popover->onClose = [safe] { juce::MessageManager::callAsync ([safe] { if (safe != nullptr) { safe->popover.reset(); safe->requestList.reset(); } }); };
    requestList->onSizeChanged = [this]
    {
        if (popover == nullptr) return;
        const auto n = core.store().state().joinRequests.size();
        popover->setCount (n > 0 ? juce::String ((int) n) : juce::String());
        popover->setContent (requestList.get(), requestList->getPreferredHeight());
        resized();
    };
    requestList->onOpenProfile = [this] (const core::UserPublic& u)
    {
        juce::Component::SafePointer<JamScreen> s2 (this);
        auto user = u;
        juce::MessageManager::callAsync ([s2, user] { if (s2 != nullptr) s2->showProfile (user); });
    };
    const auto n = core.store().state().joinRequests.size();
    popover->setCount (n > 0 ? juce::String ((int) n) : juce::String());
    popover->setContent (requestList.get(), requestList->getPreferredHeight());
    addAndMakeVisible (*popover);
    resized();
    popover->toFront (false);
}

void JamScreen::openProfile (const juce::String& userId)
{
    core::UserPublic u;
    u.id = userId;
    if (const auto& room = core.store().state().currentRoom)
        for (auto& m : room->members)
            if (m.user.id == userId) u = m.user;
    for (auto& f : core.store().state().friends)
        if (f.user.id == userId) u = f.user;
    if (u.username.isEmpty()) u.nickname = memberName (userId);
    showProfile (u);
}

void JamScreen::showProfile (const core::UserPublic& u)
{
    auto m = std::make_unique<ProfileModal> (core, u, true);
    juce::Component::SafePointer<JamScreen> safe (this);
    m->onJoinFailure = [safe] (const core::ApiError& e)
    {
        juce::MessageManager::callAsync ([safe, e] { if (safe != nullptr && safe->onJoinFailure) safe->onJoinFailure (e); });
    };
    showModal (std::move (m));
}

void JamScreen::openManage (const core::JamSession::Member& m)
{
    showModal (std::make_unique<ManageMemberModal> (core, m));
}

void JamScreen::closeOverlays()
{
    modal.reset();
    popover.reset();
    requestList.reset();
}

bool JamScreen::handleShortcut (const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();
    const auto c = juce::CharacterFunctions::toLowerCase (k.getTextCharacter());
    if (k == juce::KeyPress::escapeKey && (modal != nullptr || popover != nullptr)) { closeOverlays(); return true; }
    if (modal != nullptr) return false;   // 모달이 떠 있으면 합주 단축키는 쉰다
    if (mods.isCommandDown() && (k.getKeyCode() == juce::KeyPress::upKey || k.getKeyCode() == juce::KeyPress::downKey))
    {
        jam.setMasterDb (jam.getMasterDb() + (k.getKeyCode() == juce::KeyPress::upKey ? 1.0f : -1.0f));
        refreshAudio();
        return true;
    }
    if (mods.isAnyModifierKeyDown() && ! mods.isShiftDown()) return false;
    if (c == 'm') { jam.setSendMuted (! jam.isSendMuted()); refreshAudio(); return true; }
    if (c == 'n') { jam.setMonitorMuted (! jam.isMonitorMuted()); refreshAudio(); return true; }
    if (k.getKeyCode() == juce::KeyPress::returnKey)
    {
        if (! chatOpen) { chatOpen = true; resized(); }
        chatInput.focus();
        return true;
    }
    return false;
}

bool JamScreen::keyPressed (const juce::KeyPress& k)
{
    return handleShortcut (k);
}

} // namespace meon::ui
