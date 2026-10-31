#include "WidgetGallery.h"
#include "../theme/Icons.h"

namespace meon::ui
{

namespace
{
    /** 아이콘 하나를 그리는 칸 */
    struct IconCell : juce::Component
    {
        IconCell (int id, juce::String part = {}) : icon (id), partKey (part) {}
        int icon;
        juce::String partKey;
        void paint (juce::Graphics& g) override
        {
            if (partKey.isNotEmpty()) icons::drawPart (g, partKey, getLocalBounds().toFloat(), 20.0f, col::ink);
            else icons::draw (g, (icons::Id) icon, getLocalBounds().toFloat().reduced (6.0f), col::ink);
        }
    };

    /** 가짜 레벨 (사인파로 흔들림) */
    float fakeLevel (int ch, float base)
    {
        const double t = juce::Time::getMillisecondCounterHiRes() / 1000.0;
        const double db = base + 5.0 * std::sin (t * 1.7 + ch) + 3.0 * std::sin (t * 4.1 + ch * 1.3);
        return (float) juce::Decibels::decibelsToGain (db);
    }
}

WidgetGallery::WidgetGallery()
{
    setLookAndFeel (&lnf);
    setOpaque (true);

    // 버튼
    {
        auto* p = make<Button> (Button::Kind::primary, TXT ("방 만들기"));
        auto* pd = make<Button> (Button::Kind::primary, TXT ("입장"));
        pd->setDimmed (true);
        auto* s = make<Button> (Button::Kind::secondary, TXT ("방 나가기"));
        auto* q = make<Button> (Button::Kind::quiet, TXT ("설정"));
        auto* d = make<Button> (Button::Kind::dangerOutline, TXT ("방 닫기"));
        d->setConfirm (TXT ("한 번 더 누르면 방을 닫아요"));
        auto* l = make<Button> (Button::Kind::link, TXT ("전송"));
        addRow (TXT ("버튼 32"), { p, pd, s, q, d, l });
    }
    {
        auto* a = make<Button> (Button::Kind::small, TXT ("음소거"));
        auto* b = make<Button> (Button::Kind::small, TXT ("음소거 해제"));
        b->setOn (true);
        auto* c = make<Button> (Button::Kind::small, TXT ("취소"));
        c->setPending (true);
        auto* d = make<Button> (Button::Kind::small, TXT ("가득 참"));
        d->setDimmed (true);
        auto* e = make<Button> (Button::Kind::smallPrimary, TXT ("수락"));
        auto* f = make<Button> (Button::Kind::smallDanger, TXT ("친구 삭제"));
        f->setConfirm (TXT ("한 번 더 누르면 삭제돼요"));
        auto* h = make<Button> (Button::Kind::small, TXT ("방장 넘기기"));
        h->setConfirm (TXT ("한 번 더 누르면 넘겨요"), true);
        addRow (TXT ("작은 버튼 19"), { a, b, c, d, e, f, h }, 19);
    }
    {
        auto* big = make<Button> (Button::Kind::large, TXT ("로그인"));
        big->setFixedWidth (320);
        auto* bigOff = make<Button> (Button::Kind::large, TXT ("다음"));
        bigOff->setDimmed (true);
        bigOff->setFixedWidth (320);
        addRow (TXT ("버튼 40"), { big, bigOff }, 40);
    }
    {
        auto* c1 = make<CollapseButton>();
        auto* c2 = make<CollapseButton>();
        c2->setOpen (false);
        auto* c3 = make<CollapseButton> (true);
        auto* gear = make<IconButton> ((int) icons::Id::gear, IconButton::Style::plain24, 14.0f);
        auto* close = make<IconButton> ((int) icons::Id::close24, IconButton::Style::plain24, 14.0f);
        auto* bell = make<IconButton> ((int) icons::Id::bell, IconButton::Style::bordered32, 16.0f);
        bell->setDot (true);
        auto* jr = make<IconButton> ((int) icons::Id::personPlus, IconButton::Style::bordered32, 16.0f);
        std::vector<juce::Component*> items { c1, c2, c3, gear, close, bell, jr };
        for (int id = 0; id <= (int) icons::Id::check; ++id) items.push_back (make<IconCell> (id));
        for (auto k : { "vocal", "guitar", "bass", "drums", "keys", "other" }) items.push_back (make<IconCell> (0, k));
        addRow (TXT ("아이콘"), items, 32);
    }
    // 입력
    {
        auto* a = make<TextInput> (TXT ("you@example.com"));
        auto* b = make<TextInput> (TXT ("아이디"));
        b->setText ("junho");
        b->setError (true);
        auto* c = make<TextInput> (TXT ("비밀번호"));
        c->setPassword (true);
        c->setText ("abcd123!");
        auto* sel = make<Select>();
        sel->setItems ({ "2", "3", "4", "5", "6", "7" }, { TXT ("2명"), TXT ("3명"), TXT ("4명"), TXT ("5명"), TXT ("6명"), TXT ("7명") });
        sel->setValue ("7");
        auto* chk = make<Checkbox>();
        chk->setToggleState (true, juce::dontSendNotification);
        auto* chk2 = make<Checkbox>();
        addRow (TXT ("입력"), { a, b, c, sel, chk, chk2 });
    }
    {
        auto* area = make<TextInput> (TXT ("어떤 곡을, 어떤 분위기로 하는지 적어주세요"), true);
        auto* o1 = make<OtpInput> (OtpInput::Charset::digits, OtpInput::Size::large);
        o1->setCode ("4829");
        auto* o2 = make<OtpInput> (OtpInput::Charset::roomCode, OtpInput::Size::medium);
        o2->setCode ("A7K2QX");
        o2->setError (true);
        auto* o3 = make<OtpInput> (OtpInput::Charset::digits, OtpInput::Size::small);
        addRow (TXT ("소개 · 6칸"), { area, o1, o2, o3 }, 52);
    }
    // 조작
    {
        auto* t1 = make<Toggle>();
        t1->setOn (true, false);
        auto* t2 = make<Toggle>();
        auto* s1 = make<Choice> (Choice::Style::small);
        s1->setOptions ({ "mono", "stereo" }, { TXT ("모노"), TXT ("스테레오") });
        s1->setSelected ("mono");
        auto* s2 = make<Choice> (Choice::Style::small);
        s2->setOptions ({ "off", "mid", "strong" }, { TXT ("끔"), TXT ("보통"), TXT ("강하게") });
        s2->setSelected ("mid");
        auto* c1 = make<Choice> (Choice::Style::chip, false, 80);
        c1->setOptions ({ "public", "private" }, { TXT ("공개"), TXT ("비공개") });
        c1->setSelected ("public");
        auto* c2 = make<Choice> (Choice::Style::chip, true);
        c2->setOptions ({ "guitar", "bass", "drums", "keys", "vocal" }, { TXT ("기타"), TXT ("베이스"), TXT ("드럼"), TXT ("키보드"), TXT ("보컬") });
        c2->setSelectedSet ({ "bass", "vocal" });
        addRow (TXT ("토글 · 고르기"), { t1, t2, s1, s2, c1, c2 });
    }
    {
        auto* v1 = make<DbSlider> (-60.0f, 6.0f);
        v1->setValue (-2.5f);
        auto* v2 = make<DbSlider> (-30.0f, 10.0f);
        v2->setValue (3.0f);
        v2->setMuted (true);
        auto* pos = make<DbSlider> (0.0f, 1.0f, true);
        pos->setValue (0.18f);
        auto* pan = make<PanKnob>();
        pan->setValue (-30);
        auto* pan2 = make<PanKnob>();
        pan2->setValue (40);
        auto* reset = make<Button> (Button::Kind::small, TXT ("초기화"));
        addRow (TXT ("슬라이더 · 패닝"), { v1, v2, pos, pan, pan2, reset }, 19);
    }
    {
        auto* m1 = make<Meter>();
        m1->setSource (1, [] (int ch) { return fakeLevel (ch, -14.0f); });
        auto* m2 = make<Meter>();
        m2->setSource (2, [] (int ch) { return fakeLevel (ch, -9.0f); });
        auto* m3 = make<Meter>();
        m3->setSource (2, [] (int ch) { return fakeLevel (ch, -2.0f); });
        auto* p1 = make<PingBadge>();
        p1->setPing (9);
        auto* p2 = make<PingBadge>();
        p2->setPing (26);
        auto* p3 = make<PingBadge>();
        p3->setText (TXT ("나"));
        auto* tag = make<Tag> (TXT ("비공개"));
        addRow (TXT ("미터 · 핑 · 태그"), { m1, m2, m3, p1, p2, p3, tag }, 19);
    }
    {
        auto* a1 = make<Avatar> (41.0f, 20.0f);
        a1->setPart ("guitar");
        auto* a2 = make<Avatar> (41.0f, 20.0f);
        a2->setPart ("drums");
        a2->setDimmed (true);
        auto* a3 = make<Avatar> (32.0f, 16.0f);
        a3->setPart ("keys");
        a3->setStatus (Avatar::Status::online);
        auto* a4 = make<Avatar> (32.0f, 16.0f);
        a4->setPart ("vocal");
        a4->setStatus (Avatar::Status::inRoom);
        auto* a5 = make<Avatar> (32.0f, 16.0f);
        a5->setPart ("bass");
        a5->setStatus (Avatar::Status::offline);
        a5->setDimmed (true);
        auto* t = make<Text> (TXT ("서연 · 베이스"), 600, 14.0f);
        auto* sub = make<Text> (TXT ("마지막 접속 3시간 전"), 400, 12.0f, col::inkFaint);
        addRow (TXT ("아바타 · 글자"), { a1, a2, a3, a4, a5, t, sub }, 41);
    }

    // 카드·팝오버·모달 (오른쪽 아래)
    addAndMakeVisible (card);
    card.setTitle (TXT ("내 오디오"), true, true);
    addAndMakeVisible (chatCard);
    chatCard.setTitle (TXT ("채팅"), true);
    chatCard.setOpen (false);
    addAndMakeVisible (popover);
    popover.setCount ("3");
    addAndMakeVisible (modal);
    modal.setContent (nullptr, 120);
}

WidgetGallery::~WidgetGallery()
{
    setLookAndFeel (nullptr);
}

void WidgetGallery::addRow (const juce::String& label, std::vector<juce::Component*> items, int height)
{
    rows.push_back ({ label, std::move (items), height });
}

void WidgetGallery::resized()
{
    labels.clear();
    int y = 18;
    for (auto& row : rows)
    {
        labels.push_back ({ row.label, { 18, y, 120, juce::jmax (row.height, 19) } });
        int x = 150;
        const int rowH = juce::jmax (row.height, 19);
        for (auto* c : row.items)
        {
            int w = 120, h = row.height;
            if (auto* b = dynamic_cast<Button*> (c)) w = b->getIdealWidth();
            else if (dynamic_cast<CollapseButton*> (c)) w = 19;
            else if (auto* ib = dynamic_cast<IconButton*> (c)) w = ib->getHeight() > 0 ? 32 : 32;
            else if (dynamic_cast<IconCell*> (c)) w = 28;
            else if (auto* o = dynamic_cast<OtpInput*> (c)) { w = o->getPreferredWidth(); h = o->getPreferredHeight(); }
            else if (auto* ch = dynamic_cast<Choice*> (c)) w = ch->getPreferredWidth();
            else if (dynamic_cast<Checkbox*> (c)) w = 16;
            else if (dynamic_cast<Toggle*> (c)) w = 26;
            else if (dynamic_cast<PanKnob*> (c)) w = 19;
            else if (dynamic_cast<PingBadge*> (c)) w = 48;
            else if (auto* tg = dynamic_cast<Tag*> (c)) w = tg->getIdealWidth();
            else if (auto* av = dynamic_cast<Avatar*> (c)) w = av->getHeight() > 0 ? 41 : 41;
            else if (auto* tx = dynamic_cast<Text*> (c)) w = tx->getIdealWidth();
            else if (dynamic_cast<Meter*> (c)) w = 150;
            else if (dynamic_cast<DbSlider*> (c)) w = 150;
            else if (dynamic_cast<TextInput*> (c)) w = 200;
            else if (dynamic_cast<Select*> (c)) w = 120;
            c->setBounds (x, y + (rowH - h) / 2, w, h);
            x += w + 9;
        }
        y += rowH + 18;
    }
    card.setBounds (18, y, 360, 120);
    chatCard.setBounds (18 + 369, y, 320, 48);
    popover.setPanelBounds (getWidth() - 18 - 340, y, 340, popover.getPreferredHeight() + 60);
    popover.setContent (nullptr, 60);
    modal.setBounds (getWidth() - 18 - 340 - 18 - 400, y - 20, 400, 220);
}

void WidgetGallery::paint (juce::Graphics& g)
{
    g.fillAll (col::white);
    g.setFont (font (500, 12.0f));
    g.setColour (col::inkFaint);
    for (auto& l : labels)
        g.drawText (l.first, l.second, juce::Justification::centredLeft, true);
}

} // namespace meon::ui
