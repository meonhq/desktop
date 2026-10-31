#include "OnboardingScreen.h"
#include "../theme/Hairline.h"
#include "../theme/Icons.h"
#include "../Messages.h"
#include "../Links.h"

namespace meon::ui
{

using Step = core::AuthController::Step;
namespace v = core::validation;

namespace
{
    constexpr int columnW = 360;
    constexpr int cardPad = 18;
    constexpr int blockGap = 18;
    constexpr float descLine = 19.5f;   // 13px × 1.5

    /** 위에서 아래로 쌓는 배치 도우미 */
    struct Flow
    {
        int y = 0, w = 0;
        void put (juce::Component& c, int h) { c.setBounds (0, y, w, h); y += h; }
        void putX (juce::Component& c, int x, int cw, int h) { c.setBounds (x, y, cw, h); }
        void gap (int g) { y += g; }
    };

    /** 제목(600 18) + 설명(400 13/1.5 #767676) */
    struct TitleBlock : juce::Component
    {
        TitleBlock (const juce::String& t, const juce::String& d) : title (t, 600, 18.0f), desc (d, 400, 13.0f, col::inkSub)
        {
            addAndMakeVisible (title);
            if (d.isNotEmpty()) addAndMakeVisible (desc);
            desc.setLineHeight (descLine);
            desc.setJustification (juce::Justification::topLeft);
            setInterceptsMouseClicks (false, false);
        }
        int heightFor (int w) const { return 22 + (desc.getText().isEmpty() ? 0 : 6 + desc.getHeightForWidth (w)); }
        void resized() override
        {
            title.setBounds (0, 0, getWidth(), 22);
            desc.setBounds (0, 28, getWidth(), getHeight() - 28);
        }
        Text title, desc;
    };

    /** 칸 이름(500 12 #767676) + 오른쪽 숫자(400 12 #AAAAAA) + 입력 */
    struct Field : juce::Component
    {
        Field (const juce::String& label, const juce::String& placeholder = {})
            : name (label, 500, 12.0f, col::inkSub), counter ({}, 400, 12.0f, col::inkFaint), input (placeholder)
        {
            addAndMakeVisible (name);
            addAndMakeVisible (counter);
            addAndMakeVisible (input);
            counter.setJustification (juce::Justification::centredRight);
        }
        static constexpr int height = 15 + 6 + 32;
        void resized() override
        {
            name.setBounds (0, 0, getWidth(), 15);
            counter.setBounds (0, 0, getWidth(), 15);
            input.setBounds (0, 21, getWidth(), 32);
        }
        Text name, counter;
        TextInput input;
    };

    /** 빨간(또는 초록) 한 줄 안내 (400 12) */
    struct Note : Text
    {
        Note() : Text ({}, 400, 12.0f, col::danger) {}
        void set (const juce::String& t, juce::Colour c = col::danger) { setText (t); setColour (c); setVisible (t.isNotEmpty()); }
    };

    /** 아래 구분선 + "계정이 없나요? 회원가입" */
    struct Footer : juce::Component
    {
        Footer (const juce::String& q, const juce::String& action) : question (q, 400, 12.0f, col::inkSub), link (Button::Kind::linkUnderline, action)
        {
            addAndMakeVisible (question);
            addAndMakeVisible (link);
        }
        static constexpr int height = 12 + 17;
        void resized() override
        {
            const int qw = question.getIdealWidth(), lw = link.getIdealWidth();
            const int x = (getWidth() - (qw + 6 + lw)) / 2;
            question.setBounds (x, 12, qw, 17);
            link.setBounds (x + qw + 6, 12, lw, 17);
        }
        void paint (juce::Graphics& g) override
        {
            // margin: 0 -18px — 카드 가장자리까지 닿는 선
            g.setColour (col::borderSubtle);
            hairline::horizontal (g, *this, (float) -cardPad, 0.0f, (float) (getWidth() + cardPad * 2));
        }
        Text question;
        Button link;
    };

    /** 굵은 부분이 섞인 설명 (이메일 인증: "<b>주소</b>(으)로 보낸 …") */
    struct RichDesc : juce::Component
    {
        void set (const juce::String& bold, const juce::String& rest)
        {
            str.clear();
            str.append (bold, font (600, 13.0f), col::ink);
            str.append (rest, font (400, 13.0f), col::inkSub);
            str.setWordWrap (juce::AttributedString::byWord);
            str.setLineSpacing (descLine - font (400, 13.0f).getHeight());
            repaint();
        }
        int heightFor (int w) const
        {
            juce::TextLayout l;
            l.createLayout (str, (float) w);
            return (int) std::ceil (l.getHeight());
        }
        void paint (juce::Graphics& g) override
        {
            juce::TextLayout l;
            l.createLayout (str, (float) getWidth());
            l.draw (g, getLocalBounds().toFloat());
        }
        juce::AttributedString str;
    };

    /** 약관 동의: 체크 상자 + "이용약관과 개인정보 처리방침에 동의해요" (두 낱말은 밑줄 링크) */
    struct AgreeRow : juce::Component
    {
        AgreeRow()
        {
            addAndMakeVisible (box);
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }
        static constexpr int height = 18;
        void resized() override { box.setBounds (0, 1, 16, 16); }
        std::vector<std::pair<juce::String, juce::Colour>> parts() const
        {
            return { { TXT ("이용약관"), col::ink }, { TXT ("과 "), col::inkSub }, { TXT ("개인정보 처리방침"), col::ink }, { TXT ("에 동의해요"), col::inkSub } };
        }
        void paint (juce::Graphics& g) override
        {
            const auto f = font (400, 12.0f);
            float x = 25.0f;
            g.setFont (f);
            for (int i = 0; i < 4; ++i)
            {
                const auto all = parts();
                const auto& t = all[(size_t) i].first;
                const auto& c = all[(size_t) i].second;
                const float w = textWidth (f, t);
                g.setColour (c);
                g.drawText (t, juce::Rectangle<float> (x, 0.0f, w + 1.0f, 18.0f), juce::Justification::centredLeft, false);
                if (i == 0 || i == 2) g.fillRect (x, 15.0f, w, 1.0f);
                x += w;
            }
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            const auto f = font (400, 12.0f);
            float x = 25.0f;
            for (int i = 0; i < 4; ++i)
            {
                const float w = textWidth (f, parts()[(size_t) i].first);
                if (e.position.x >= x && e.position.x < x + w)
                {
                    if (i == 0) { links::open (links::terms()); return; }
                    if (i == 2) { links::open (links::privacy()); return; }
                }
                x += w;
            }
            box.setToggleState (! box.getToggleState(), juce::sendNotification);
        }
        Checkbox box;
    };

    /** 진행 막대 4칸 (높이 3, 사이 4) + "n / 4" */
    struct Progress : juce::Component
    {
        int step = 1;
        static constexpr int height = 15;
        void paint (juce::Graphics& g) override
        {
            const auto f = font (500, 12.0f);
            const juce::String label = juce::String (step) + " / 4";
            const float lw = textWidth (f, "4 / 4") + 1.0f;
            const float barsW = (float) getWidth() - lw - 9.0f;
            const float each = (barsW - 12.0f) / 4.0f;
            for (int i = 0; i < 4; ++i)
            {
                g.setColour (i < step ? col::accent : col::line);
                g.fillRoundedRectangle ((float) i * (each + 4.0f), (float) getHeight() * 0.5f - 1.5f, each, 3.0f, 1.5f);
            }
            g.setColour (col::inkFaint);
            g.setFont (f);
            g.drawText (label, juce::Rectangle<float> ((float) getWidth() - lw, 0.0f, lw, (float) getHeight()), juce::Justification::centredRight, false);
        }
    };

    int progressFor (Step s)
    {
        switch (s)
        {
            case Step::signup:   return 1;
            case Step::verify:   return 2;
            case Step::nickname: return 3;
            case Step::part:     return 4;
            default:             return 0;   // 디스코드에서는 숨김 (Q4)
        }
    }
}

//==============================================================================
struct OnboardingScreen::StepView : juce::Component
{
    StepView (OnboardingScreen& o) : owner (o) {}
    /** 폭 w 에 맞춰 자식을 놓고 높이를 돌려준다 */
    virtual int layoutFor (int w) = 0;
    virtual void tick() {}
    virtual void focusFirst() {}
    core::MeonCore& meonCore() { return owner.core; }
    core::AuthController& auth() { return owner.core.auth(); }
    const core::AppState& state() { return owner.core.store().state(); }
    void relayout() { owner.layout(); }
    OnboardingScreen& owner;
};

namespace
{
using StepView = OnboardingScreen::StepView;

//==============================================================================
struct LoginView : StepView
{
    TitleBlock title { TXT ("로그인"), {} };
    Field email { TXT ("이메일"), "you@example.com" }, pw { TXT ("비밀번호") };
    Note error;
    Button submit { Button::Kind::large, TXT ("로그인") };
    Button forgot { Button::Kind::linkSmall, TXT ("비밀번호를 잊었나요?") };
    struct ForgotBox : juce::Component
    {
        Text head { TXT ("비밀번호 찾기는 디스코드에서 도와드려요"), 600, 13.0f };
        Text body { TXT ("#문의하기 채널에 가입한 이메일을 남겨주세요. 본인 확인 후 비밀번호를 다시 설정할 수 있게 도와드려요"), 400, 12.0f, col::inkSub };
        DiscordButton open { TXT ("디스코드 #문의하기 열기"), 32 };
        ForgotBox() { addAndMakeVisible (head); addAndMakeVisible (body); addAndMakeVisible (open); body.setLineHeight (18.0f); body.setJustification (juce::Justification::topLeft); }
        int heightFor (int w) { return 12 + 16 + 3 + body.getHeightForWidth (w - 24) + 12 + 32 + 12; }
        void resized() override
        {
            const int w = getWidth() - 24;
            head.setBounds (12, 12, w, 16);
            const int bh = body.getHeightForWidth (w);
            body.setBounds (12, 31, w, bh);
            open.setBounds (12, 31 + bh + 12, w, 32);
        }
        void paint (juce::Graphics& g) override
        {
            g.setColour (col::softBg);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), metric::radius);
            g.setColour (col::borderSubtle);
            hairline::roundedBorder (g, *this, getLocalBounds().toFloat(), metric::radius);
        }
    } forgotBox;
    Footer footer { TXT ("계정이 없나요?"), TXT ("회원가입") };
    bool busy = false;

    LoginView (OnboardingScreen& o) : StepView (o)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &title, &email, &pw, &error, &submit, &forgot, &footer })
            addAndMakeVisible (c);
        addChildComponent (forgotBox);
        error.set ({});
        pw.input.setPassword (true);
        email.input.onChange = pw.input.onChange = [this] { error.set ({}); update(); };
        email.input.onReturn = [this] { pw.input.focus(); };
        pw.input.onReturn = [this] { submitNow(); };
        submit.onPress = [this] { submitNow(); };
        forgot.onPress = [this] { forgotBox.setVisible (! forgotBox.isVisible()); relayout(); };
        forgotBox.open.onClick = [this]
        {
            const auto url = state().config.discordUrl;
            links::open (url.isNotEmpty() ? url : juce::String (links::discordFallback()));
        };
        footer.link.onPress = [this] { auth().showSignup(); };
        update();
    }

    bool ok() const { return v::email (email.input.getText().trim()) == v::Problem::none && pw.input.getText().isNotEmpty(); }
    void update() { submit.setDimmed (! ok() || busy); submit.setButtonText (busy ? TXT ("로그인 중…") : TXT ("로그인")); }
    void focusFirst() override { email.input.focus(); }

    void submitNow()
    {
        if (! ok() || busy) return;
        busy = true;
        update();
        juce::Component::SafePointer<LoginView> safe (this);
        auth().login (email.input.getText(), pw.input.getText(), [safe] (core::Result<core::Done> r)
        {
            if (safe == nullptr) return;
            safe->busy = false;
            safe->update();
            if (! r.ok())
            {
                safe->error.set (r.code() == core::ErrorCode::invalidCredentials ? TXT ("이메일 또는 비밀번호가 맞지 않아요")
                                                                                : msg::general (r.err()));
                safe->relayout();
            }
        });
    }

    int layoutFor (int w) override
    {
        Flow f { 0, w };
        f.put (title, title.heightFor (w));
        f.gap (blockGap);
        f.put (email, Field::height);
        f.gap (12);
        f.put (pw, Field::height);
        if (error.isVisible()) { f.gap (blockGap - 6); f.put (error, 15); }
        f.gap (blockGap);
        f.put (submit, metric::bigButtonH);
        f.gap (12);
        const int fw = forgot.getIdealWidth();
        forgot.setBounds ((w - fw) / 2, f.y, fw, 17);
        f.gap (17);
        if (forgotBox.isVisible()) { f.gap (blockGap); f.put (forgotBox, forgotBox.heightFor (w)); }
        f.gap (blockGap);
        f.put (footer, Footer::height);
        return f.y;
    }
};

//==============================================================================
struct SignupView : StepView
{
    TitleBlock title { TXT ("계정 만들기"), TXT ("이메일로 가입해요. 다음 단계에서 인증 코드를 보내드려요") };
    Text uidLabel { TXT ("아이디"), 500, 12.0f, col::inkSub };
    TextInput uid { TXT ("영문 소문자·숫자·밑줄 4–16자") };
    Button check { Button::Kind::secondary, TXT ("중복 확인") };
    Note uidNote, emailNote, pwNote, mismatch;
    Field email { TXT ("이메일"), "you@example.com" };
    Field pw { TXT ("비밀번호 · 8자 이상"), TXT ("영문·숫자·특수문자 포함 8자 이상") };
    Field pw2 { TXT ("비밀번호 확인") };
    AgreeRow agree;
    Button next { Button::Kind::large, TXT ("다음") };
    Footer footer { TXT ("이미 계정이 있나요?"), TXT ("로그인") };
    enum class Uid { idle, busy, ok, taken, invalid } uidState = Uid::idle;
    bool busy = false;

    SignupView (OnboardingScreen& o) : StepView (o)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &title, &uidLabel, &uid, &check, &uidNote, &email, &emailNote,
                                                                 &pw, &pwNote, &pw2, &mismatch, &agree, &next, &footer })
            addAndMakeVisible (c);
        for (auto* n : { &uidNote, &emailNote, &pwNote, &mismatch }) n->set ({});
        uid.setMaxLength (16);
        uid.transform = [] (const juce::String& s) { return s.toLowerCase(); };
        uid.onChange = [this] { uidState = Uid::idle; uidNote.set ({}); uid.setError (false); update(); };
        uid.onReturn = [this] { checkUid(); };
        // 디자인 버튼: 32px 흰 면 #EBEBEB 테두리, 600 13px #767676, 호버 #F5F5F5·#1A1A1A (= cancel)
        check.setKind (Button::Kind::cancel);
        check.onPress = [this] { checkUid(); };
        pw.input.setPassword (true);
        pw2.input.setPassword (true);
        email.input.onChange = [this] { emailNote.set ({}); email.input.setError (false); update(); };
        email.input.onFocusLost = [this] { showEmailProblem(); };
        pw.input.onChange = pw2.input.onChange = [this] { update(); };
        agree.box.onClick = [this] { update(); };
        next.onPress = [this] { submit(); };
        footer.link.onPress = [this] { auth().showLogin(); };
        update();
    }

    void focusFirst() override { uid.focus(); }

    void showEmailProblem()
    {
        const auto t = msg::email (v::email (email.input.getText().trim()));
        if (t.isNotEmpty()) { emailNote.set (t); email.input.setError (true); relayout(); }
    }

    void checkUid()
    {
        const auto u = uid.getText();
        if (v::username (u) != v::Problem::none) { setUid (Uid::invalid); return; }
        setUid (Uid::busy);
        juce::Component::SafePointer<SignupView> safe (this);
        auth().checkUsername (u, [safe, u] (core::Result<bool> r)
        {
            if (safe == nullptr || safe->uid.getText() != u) return;
            if (! r.ok()) { safe->setUid (Uid::idle); safe->uidNote.set (msg::general (r.err())); safe->relayout(); return; }
            safe->setUid (r.get() ? Uid::ok : Uid::taken);
        });
    }

    void setUid (Uid s)
    {
        uidState = s;
        check.setButtonText (s == Uid::busy ? TXT ("확인 중…") : TXT ("중복 확인"));
        uid.setError (s == Uid::taken || s == Uid::invalid);
        switch (s)
        {
            case Uid::ok:      uidNote.set (TXT ("쓸 수 있는 아이디예요"), col::success); break;
            case Uid::taken:   uidNote.set (TXT ("이미 쓰고 있는 아이디예요")); break;
            case Uid::invalid: uidNote.set (msg::username (v::Problem::badFormat)); break;
            default:           uidNote.set ({}); break;
        }
        update();
        relayout();
    }

    bool pwMismatch() const { return pw2.input.getText().isNotEmpty() && pw2.input.getText() != pw.input.getText(); }

    bool ok() const
    {
        return uidState == Uid::ok && v::email (email.input.getText().trim()) == v::Problem::none
               && v::password (pw.input.getText()) == v::Problem::none
               && pw2.input.getText() == pw.input.getText() && agree.box.getToggleState();
    }

    void update()
    {
        const bool mm = pwMismatch();
        pw2.input.setError (mm);
        const bool wasMm = mismatch.isVisible();
        mismatch.set (mm ? TXT ("비밀번호가 서로 달라요") : juce::String());
        const auto pwText = pw.input.getText().isEmpty() ? juce::String() : msg::password (v::password (pw.input.getText()));
        const bool wasPw = pwNote.isVisible();
        pwNote.set (pwText);
        next.setDimmed (! ok() || busy);
        if (wasMm != mismatch.isVisible() || wasPw != pwNote.isVisible()) relayout();
    }

    void submit()
    {
        if (! ok() || busy) return;
        busy = true;
        update();
        juce::Component::SafePointer<SignupView> safe (this);
        auth().signup (email.input.getText(), uid.getText(), pw.input.getText(), true, [safe] (core::Result<core::Done> r)
        {
            if (safe == nullptr) return;
            safe->busy = false;
            safe->update();
            if (r.ok()) return;
            using E = core::ErrorCode;
            if (r.code() == E::emailTaken) { safe->emailNote.set (TXT ("이미 가입한 이메일이에요")); safe->email.input.setError (true); }
            else if (r.code() == E::usernameTaken) safe->setUid (Uid::taken);
            else if (r.code() == E::invalidRequest && r.err().field == "password") safe->pwNote.set (msg::password (v::Problem::missingClasses));
            else safe->emailNote.set (msg::general (r.err()));
            safe->relayout();
        });
    }

    int layoutFor (int w) override
    {
        Flow f { 0, w };
        f.put (title, title.heightFor (w));
        f.gap (blockGap);
        f.put (uidLabel, 15);
        f.gap (6);
        const int cw = check.getIdealWidth();
        uid.setBounds (0, f.y, w - cw - 9, 32);
        check.setBounds (w - cw, f.y, cw, 32);
        f.gap (32);
        if (uidNote.isVisible()) { f.gap (6); f.put (uidNote, 15); }
        f.gap (12);
        f.put (email, Field::height);
        if (emailNote.isVisible()) { f.gap (6); f.put (emailNote, 15); }
        f.gap (12);
        f.put (pw, Field::height);
        if (pwNote.isVisible()) { f.gap (6); f.put (pwNote, 15); }
        f.gap (12);
        f.put (pw2, Field::height);
        if (mismatch.isVisible()) { f.gap (blockGap - 6); f.put (mismatch, 15); }
        f.gap (blockGap);
        f.put (agree, AgreeRow::height);
        f.gap (blockGap);
        f.put (next, metric::bigButtonH);
        f.gap (blockGap);
        f.put (footer, Footer::height);
        return f.y;
    }
};

//==============================================================================
struct VerifyView : StepView
{
    Text title { TXT ("이메일 인증"), 600, 18.0f };
    RichDesc desc;
    OtpInput code { OtpInput::Charset::digits, OtpInput::Size::large };
    Note error;
    Button submit { Button::Kind::large, TXT ("인증하기") };
    Button resend { Button::Kind::linkUnderline, TXT ("코드 다시 받기") };
    Text wait { {}, 400, 12.0f, col::inkSub };
    Text bar { "|", 400, 12.0f, col::field };
    Button changeEmail { Button::Kind::linkSmall, TXT ("이메일 바꾸기") };
    // 이메일 바꾸기 (Q3 잠정): 인증 화면 안에서 새 주소를 받아 바로 바꾸고 코드를 다시 보낸다
    TextInput newEmail { "you@example.com" };
    Button sendNew { Button::Kind::secondary, TXT ("보내기") };
    Note emailNote;
    Text devCode { {}, 400, 12.0f, col::inkFaint };
    bool busy = false, editing = false;

    VerifyView (OnboardingScreen& o) : StepView (o)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &title, &desc, &code, &error, &submit, &resend, &wait, &bar, &changeEmail })
            addAndMakeVisible (c);
        addChildComponent (newEmail);
        addChildComponent (sendNew);
        addChildComponent (emailNote);
       #if MEON_DEV_OPTIONS
        addAndMakeVisible (devCode);
        devCode.setJustification (juce::Justification::centred);
       #endif
        error.set ({});
        emailNote.set ({});
        code.onChange = [this] { error.set ({}); update(); };
        code.onSubmit = [this] { submitNow(); };
        submit.onPress = [this] { submitNow(); };
        resend.onPress = [this]
        {
            juce::Component::SafePointer<VerifyView> safe (this);
            auth().resendCode ([safe] (core::Result<core::Done> r)
            {
                if (safe == nullptr) return;
                if (! r.ok()) safe->error.set (msg::general (r.err()));
                safe->code.clear();
                safe->tick();
                safe->relayout();
            });
        };
        changeEmail.onPress = [this]
        {
            editing = ! editing;
            newEmail.setVisible (editing);
            sendNew.setVisible (editing);
            if (editing) newEmail.focus();
            relayout();
        };
        newEmail.onReturn = [this] { sendNewEmail(); };
        newEmail.onChange = [this] { emailNote.set ({}); newEmail.setError (false); };
        sendNew.onPress = [this] { sendNewEmail(); };
        refreshEmail();
        update();
        tick();
    }

    void refreshEmail()
    {
        const auto e = state().me ? state().me->email : juce::String ("you@example.com");
        desc.set (e, TXT ("(으)로 보낸 6자리 코드를 입력하세요"));
    }

    void focusFirst() override { code.focusFirst(); }

    void sendNewEmail()
    {
        const auto e = newEmail.getText().trim();
        if (v::email (e) != v::Problem::none) { emailNote.set (msg::email (v::email (e))); newEmail.setError (true); relayout(); return; }
        juce::Component::SafePointer<VerifyView> safe (this);
        auth().changeUnverifiedEmail (e, [safe] (core::Result<core::Done> r)
        {
            if (safe == nullptr) return;
            if (! r.ok())
            {
                safe->emailNote.set (r.code() == core::ErrorCode::emailTaken ? TXT ("이미 가입한 이메일이에요") : msg::general (r.err()));
                safe->newEmail.setError (true);
                safe->relayout();
                return;
            }
            safe->editing = false;
            safe->newEmail.setVisible (false);
            safe->sendNew.setVisible (false);
            safe->emailNote.set ({});
            safe->refreshEmail();
            safe->code.clear();
            safe->tick();
            safe->relayout();
        });
    }

    void update()
    {
        submit.setDimmed (! code.isComplete() || busy);
        submit.setButtonText (busy ? TXT ("확인 중…") : TXT ("인증하기"));
    }

    void tick() override
    {
        const auto now = meonCore().scheduler().nowMs();
        const int left = juce::jmax (0, (int) std::ceil ((double) (core::AuthController::resendGapMs - (now - auth().lastCodeSentAt())) / 1000.0));
        const bool canResend = left == 0;
        const bool changed = resend.isVisible() != canResend;
        resend.setVisible (canResend);
        wait.setVisible (! canResend);
        wait.setText (juce::String (left) + TXT ("초 뒤 다시 받을 수 있어요"));
       #if MEON_DEV_OPTIONS
        if (auto* fake = meonCore().fakeServer())
            devCode.setText (TXT ("개발용 · 받은 코드 ") + fake->getLastMailedCode());
       #endif
        if (changed) relayout();
    }

    void submitNow()
    {
        if (! code.isComplete() || busy) return;
        busy = true;
        update();
        juce::Component::SafePointer<VerifyView> safe (this);
        auth().verify (code.getCode(), [safe] (core::Result<core::Done> r)
        {
            if (safe == nullptr) return;
            safe->busy = false;
            safe->update();
            if (r.ok()) return;
            using E = core::ErrorCode;
            safe->code.setError (true);
            safe->error.set (r.code() == E::codeInvalid ? TXT ("코드가 맞지 않아요. 다시 확인해 주세요")
                             : r.code() == E::codeExpired ? TXT ("코드가 만료됐어요. 코드를 다시 받아 주세요")
                             : msg::general (r.err()));
            safe->relayout();
        });
    }

    int layoutFor (int w) override
    {
        Flow f { 0, w };
        f.put (title, 22);
        f.gap (6);
        f.put (desc, desc.heightFor (w));
        f.gap (blockGap);
        f.put (code, code.getPreferredHeight());
        if (error.isVisible()) { f.gap (6); f.put (error, 15); }
        f.gap (blockGap);
        f.put (submit, metric::bigButtonH);
        f.gap (blockGap);

        // "코드 다시 받기 | 이메일 바꾸기" 가운데 정렬 (사이 12)
        auto& left = resend.isVisible() ? static_cast<juce::Component&> (resend) : static_cast<juce::Component&> (wait);
        const int lw = resend.isVisible() ? resend.getIdealWidth() : wait.getIdealWidth();
        const int cw = changeEmail.getIdealWidth(), bw = 4;
        const int total = lw + 12 + bw + 12 + cw;
        int x = (w - total) / 2;
        left.setBounds (x, f.y, lw, 17); x += lw + 12;
        bar.setBounds (x, f.y, bw, 17); x += bw + 12;
        changeEmail.setBounds (x, f.y, cw, 17);
        f.gap (17);
        if (editing)
        {
            f.gap (12);
            const int sw = sendNew.getIdealWidth();
            newEmail.setBounds (0, f.y, w - sw - 9, 32);
            sendNew.setBounds (w - sw, f.y, sw, 32);
            f.gap (32);
            if (emailNote.isVisible()) { f.gap (6); f.put (emailNote, 15); }
        }
       #if MEON_DEV_OPTIONS
        f.gap (12);
        f.put (devCode, 15);
       #endif
        return f.y;
    }
};

//==============================================================================
struct NicknameView : StepView
{
    TitleBlock title { TXT ("닉네임을 정해주세요"), TXT ("합주실과 친구 목록에서 이 이름으로 보여요. 나중에 설정에서 바꿀 수 있어요") };
    Field nick { TXT ("닉네임"), TXT ("예: 준호") };
    Note note;
    Button next { Button::Kind::large, TXT ("다음") };
    bool busy = false;

    NicknameView (OnboardingScreen& o) : StepView (o)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &title, &nick, &note, &next }) addAndMakeVisible (c);
        note.set ({});
        nick.input.setMaxLength (v::nicknameMax);
        if (state().me && state().me->nickname) nick.input.setText (*state().me->nickname);
        nick.input.onChange = [this] { update(); };
        nick.input.onReturn = [this] { submit(); };
        next.onPress = [this] { submit(); };
        update();
    }

    void focusFirst() override { nick.input.focus(); }

    void update()
    {
        const auto t = nick.input.getText();
        nick.counter.setText (juce::String (v::length (t)) + " / 12");
        const auto p = v::nickname (t);
        const bool wasVisible = note.isVisible();
        note.set (t.trim().isEmpty() ? juce::String() : msg::nickname (p));
        nick.input.setError (note.isVisible());
        next.setDimmed (p != v::Problem::none || busy);
        if (wasVisible != note.isVisible()) relayout();
    }

    void submit()
    {
        if (v::nickname (nick.input.getText()) != v::Problem::none || busy) return;
        busy = true;
        update();
        juce::Component::SafePointer<NicknameView> safe (this);
        auth().setNickname (v::normalise (nick.input.getText()), [safe] (core::Result<core::Done> r)
        {
            if (safe == nullptr) return;
            safe->busy = false;
            safe->update();
            if (! r.ok()) { safe->note.set (msg::general (r.err())); safe->relayout(); }
        });
    }

    int layoutFor (int w) override
    {
        Flow f { 0, w };
        f.put (title, title.heightFor (w));
        f.gap (blockGap);
        f.put (nick, Field::height);
        if (note.isVisible()) { f.gap (6); f.put (note, 15); }
        f.gap (blockGap);
        f.put (next, metric::bigButtonH);
        return f.y;
    }
};

//==============================================================================
struct PartTile : juce::Button
{
    PartTile (const juce::String& k) : juce::Button (k), key (k) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paintButton (juce::Graphics& g, bool, bool) override
    {
        const bool on = getToggleState();
        auto r = getLocalBounds().toFloat();
        g.setColour (on ? col::accentSoft : col::white);
        g.fillRoundedRectangle (r, metric::radius);
        g.setColour (on ? col::accent : col::borderSubtle);   // 꺼진 파트 카드 #EBEBEB, 고른 카드 #FFD8E4
        g.drawRoundedRectangle (r.reduced (0.5f), metric::radius, 1.0f);
        // 아이콘 28 + 간격 9 + 글자 500 13
        const float top = r.getCentreY() - (28.0f + 9.0f + 16.0f) * 0.5f;
        // 아이콘은 라벨과 같은 색 (디자인: 얇은 버전 28px). 비활성(가입 처리 중)이면 둘 다 흐리게
        const auto ink = isEnabled() ? col::ink : col::inkFaint;
        icons::drawPart (g, key, { r.getCentreX() - 14.0f, top, 28.0f, 28.0f }, 28.0f, ink, icons::PartWeight::thin);
        g.setColour (ink);
        g.setFont (font (500, 13.0f));
        g.drawText (icons::partLabel (key), juce::Rectangle<float> (r.getX(), top + 37.0f, r.getWidth(), 16.0f), juce::Justification::centred, false);
    }
    juce::String key;
};

struct PartView : StepView
{
    TitleBlock title { TXT ("주로 맡는 파트는요?"), TXT ("이름 옆에 표시돼요. 나중에 설정에서 바꿀 수 있어요") };
    juce::OwnedArray<PartTile> tiles;
    Button back { Button::Kind::largeSecondary, TXT ("이전") };
    Button start { Button::Kind::large, TXT ("시작하기") };
    Note note;
    juce::String selected;
    bool busy = false;

    PartView (OnboardingScreen& o) : StepView (o)
    {
        addAndMakeVisible (title);
        // 디자인 순서: 기타 · 베이스 · 드럼 · 키보드 · 보컬 · 그 외
        for (auto k : { "guitar", "bass", "drums", "keys", "vocal", "other" })
        {
            auto* t = tiles.add (new PartTile (k));
            addAndMakeVisible (t);
            t->onClick = [this, t] { select (t->key); };
        }
        if (state().me && state().me->part) selected = *state().me->part;
        addAndMakeVisible (back);
        addAndMakeVisible (start);
        addChildComponent (note);
        back.onPress = [this] { auth().backToNickname(); };
        start.onPress = [this] { submit(); };
        select (selected);
    }

    void select (const juce::String& k)
    {
        selected = k;
        for (auto* t : tiles) t->setToggleState (t->key == k, juce::dontSendNotification);
        start.setDimmed (selected.isEmpty() || busy);
    }

    void submit()
    {
        if (selected.isEmpty() || busy) return;
        busy = true;
        select (selected);
        juce::Component::SafePointer<PartView> safe (this);
        auth().setPart (selected, [safe] (core::Result<core::Done> r)
        {
            if (safe == nullptr) return;
            safe->busy = false;
            safe->select (safe->selected);
            if (! r.ok()) { safe->note.set (msg::general (r.err())); safe->relayout(); }
        });
    }

    int layoutFor (int w) override
    {
        Flow f { 0, w };
        f.put (title, title.heightFor (w));
        f.gap (blockGap);
        const int tw = (w - 18) / 3;
        for (int i = 0; i < tiles.size(); ++i)
            tiles[i]->setBounds ((i % 3) * (tw + 9), f.y + (i / 3) * (84 + 9), tw, 84);
        f.gap (84 * 2 + 9);
        if (note.isVisible()) { f.gap (6); f.put (note, 15); }
        f.gap (blockGap);
        const int bw = back.getIdealWidth();
        back.setBounds (0, f.y, bw, metric::bigButtonH);
        start.setBounds (bw + 9, f.y, w - bw - 9, metric::bigButtonH);
        f.gap (metric::bigButtonH);
        return f.y;
    }
};

//==============================================================================
struct DiscordView : StepView
{
    TitleBlock title { TXT ("아 맞다! 하나만 더요"), TXT ("디스코드에 같이 합주할 사람들이 모여 있어요. 들어오면 이런 걸 할 수 있어요") };
    struct Perks : juce::Component
    {
        juce::StringArray items { TXT ("같이 합주할 사람 찾기"), TXT ("새 기능과 업데이트 소식 먼저 받기"), TXT ("막히는 게 있으면 #문의하기에서 바로 물어보기") };
        int heightFor (int w) const
        {
            int h = 24;
            for (auto& s : items) h += (int) std::ceil (meon::paragraphHeightWithEmoji (s, font (400, 13.0f), (float) (w - 24 - 21), descLine));
            return h + 9 * (items.size() - 1);
        }
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (col::softBg);
            g.fillRoundedRectangle (r, metric::radius);
            g.setColour (col::borderSubtle);
            hairline::roundedBorder (g, *this, r, metric::radius);
            float y = 12.0f;
            const float tw = (float) getWidth() - 24.0f - 21.0f;
            for (auto& s : items)
            {
                icons::draw (g, icons::Id::check, { 12.0f, y + 3.0f, 12.0f, 12.0f }, col::ink);
                const float h = meon::paragraphHeightWithEmoji (s, font (400, 13.0f), tw, descLine);
                meon::drawParagraphWithEmoji (g, s, font (400, 13.0f), col::ink, { 33.0f, y, tw, h }, descLine);
                y += std::ceil (h) + 9.0f;
            }
        }
    } perks;
    DiscordButton join { TXT ("디스코드 참여하기"), 40 };
    Button skip { Button::Kind::linkSmall, TXT ("이미 참여해 있어요") };

    DiscordView (OnboardingScreen& o) : StepView (o)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &title, &perks, &join, &skip }) addAndMakeVisible (c);
        join.onClick = [this]
        {
            const auto url = state().config.discordUrl;
            links::open (url.isNotEmpty() ? url : juce::String (links::discordFallback()));
            auth().finishDiscord();
        };
        skip.onPress = [this] { auth().finishDiscord(); };
    }

    int layoutFor (int w) override
    {
        Flow f { 0, w };
        f.put (title, title.heightFor (w));
        f.gap (blockGap);
        f.put (perks, perks.heightFor (w));
        f.gap (blockGap);
        f.put (join, metric::bigButtonH);
        f.gap (12);
        const int sw = skip.getIdealWidth();
        skip.setBounds ((w - sw) / 2, f.y, sw, 17);
        f.gap (17);
        return f.y;
    }
};

//==============================================================================
struct DoneView : StepView
{
    Avatar icon { 48.0f, 24.0f };
    Text title { {}, 600, 18.0f }, desc { {}, 400, 13.0f, col::inkSub };
    Button home { Button::Kind::large, TXT ("홈으로") };
    Text note { TXT ("오디오 장치는 설정 › 오디오에서 바꿀 수 있어요"), 400, 12.0f, col::inkFaint };

    DoneView (OnboardingScreen& o) : StepView (o)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &icon, &title, &desc, &home }) addAndMakeVisible (c);
        // 플러그인은 오디오 탭이 없어 안내를 숨긴다
        if (! meonCore().isPlugin()) addAndMakeVisible (note);
        const bool login = auth().cameFromLogin();
        const auto& me = state().me;
        icon.setUser (me ? me->id : juce::String());
        title.setText (login ? TXT ("다시 오셨네요") : TXT ("준비됐어요, ") + (me && me->nickname ? *me->nickname : juce::String()) + TXT ("님"));
        desc.setText (login ? TXT ("마지막으로 쓰던 설정 그대로 이어서 합주할 수 있어요")
                            : TXT ("공개 방에 들어가거나 직접 방을 만들어 합주를 시작해 보세요"));
        for (auto* t : { &title, &desc, &note }) t->setJustification (juce::Justification::centredTop);
        desc.setLineHeight (descLine);
        home.onPress = [this] { auth().finishDone(); };
    }

    int layoutFor (int w) override
    {
        Flow f { 0, w };
        f.gap (12);
        icon.setBounds ((w - 48) / 2, f.y, 48, 48);
        f.gap (48 + 12);
        f.put (title, 22);
        f.gap (6);
        f.put (desc, desc.getHeightForWidth (w));
        f.gap (blockGap);
        f.put (home, metric::bigButtonH);
        if (note.isVisible()) { f.gap (blockGap - 6); f.put (note, 18); }
        return f.y;
    }
};

/** 강제 업데이트 (API 13장, R11): 앱 버전이 낮아 서버가 받아 주지 않는다. 로그인은 그대로 남는다 */
struct UpdateView : StepView
{
    using S = SettingsModal::UpdateHooks::State;
    TitleBlock title { TXT ("업데이트가 필요해요"), {} };
    Text status { {}, 400, 12.0f, col::inkSub };
    Button go { Button::Kind::large, TXT ("업데이트") };
    Button download { Button::Kind::largeSecondary, TXT ("새 버전 내려받기") };   // 업데이터를 못 쓰는 독립 앱 (빌드 0 등)
    Button direct { Button::Kind::linkSmall, TXT ("meon.co.kr 에서 직접 내려받기") };   // 업데이터가 있어도 웹으로 (배포 설계 5.1)
    bool canUpdate = false;

    UpdateView (OnboardingScreen& o) : StepView (o)
    {
        auto& u = owner.update;
        canUpdate = ! meonCore().isPlugin() && u.state != nullptr && u.start != nullptr;
        title.desc.setText (meonCore().isPlugin()
            ? TXT ("이 버전으로는 서버에 연결할 수 없어요. Meon 앱에서 업데이트해 주세요")
            : TXT ("이 버전으로는 서버에 연결할 수 없어요. 새 버전으로 업데이트하면 로그인한 그대로 이어서 쓸 수 있어요"));
        addAndMakeVisible (title);
        title.addAndMakeVisible (title.desc);
        addChildComponent (status);
        status.setJustification (juce::Justification::centredTop);
        if (canUpdate)
        {
            addAndMakeVisible (go);
            addAndMakeVisible (status);
            addAndMakeVisible (direct);
            direct.onPress = [] { links::open (links::download()); };
            go.onPress = [this]
            {
                auto& h = owner.update;
                const auto st = h.state();
                if (st == S::available) { if (h.start) h.start(); }
                else if (h.check) h.check();
                tick();
            };
            if (owner.update.state() == S::idle && owner.update.check) owner.update.check();
        }
        else if (! meonCore().isPlugin())
        {
            addAndMakeVisible (download);
            download.onPress = [] { links::open (links::download()); };
        }
        tick();
    }

    void tick() override
    {
        if (! canUpdate) return;
        auto& h = owner.update;
        const auto st = h.state();
        juce::String text, label = TXT ("업데이트");
        bool enabled = true;
        switch (st)
        {
            case S::idle:
            case S::checking:    label = TXT ("새 버전 확인 중…"); enabled = false; break;
            case S::upToDate:    text = TXT ("새 버전이 아직 올라오지 않았어요. 잠시 뒤 다시 확인해 주세요"); label = TXT ("다시 확인"); break;
            case S::available:   break;
            case S::downloading:
            {
                const float p = h.progress ? h.progress() : -1.0f;
                label = p >= 0.0f ? TXT ("받는 중… ") + juce::String (juce::roundToInt (p * 100.0f)) + "%" : TXT ("받는 중…");
                enabled = false;
                break;
            }
            case S::installing:  label = TXT ("설치하는 중…"); enabled = false; break;
            case S::failed:      text = TXT ("업데이트하지 못했어요. 인터넷 연결을 확인하고 다시 시도해 주세요"); label = TXT ("다시 시도"); break;
        }
        // 다시 시도해도 풀리지 않는 이유 (MeonUpdater::Problem)
        using P = SettingsModal::UpdateHooks::Problem;
        const auto problem = h.problem ? h.problem() : P::none;
        if (st == S::failed && problem == P::needsMove)
            text = TXT ("지금 있는 곳에서는 Meon을 바꿀 수 없어요. 응용 프로그램 폴더로 옮긴 뒤 다시 켜 주세요");
        else if (st == S::upToDate && problem == P::osTooOld)
            text = TXT ("새 버전은 이 컴퓨터의 운영체제 버전을 지원하지 않아요. 운영체제를 업데이트한 뒤 다시 확인해 주세요");
        go.setButtonText (label);
        go.setDimmed (! enabled);
        go.setEnabled (enabled);
        if (status.getText() != text) { status.setText (text); relayout(); }
    }

    int layoutFor (int w) override
    {
        Flow f { 0, w };
        f.put (title, title.heightFor (w));
        if (canUpdate)
        {
            f.gap (blockGap);
            f.put (go, metric::bigButtonH);
            if (status.getText().isNotEmpty()) { f.gap (9); f.put (status, status.getHeightForWidth (w)); }
            f.gap (12);
            const int dw = direct.getIdealWidth();
            direct.setBounds ((w - dw) / 2, f.y, dw, 17);
            f.gap (17);
        }
        else if (download.isVisible())
        {
            f.gap (blockGap);
            f.put (download, metric::bigButtonH);
        }
        return f.y;
    }
};

/** 다른 곳에서 접속 (닫는 코드 4000, API 10.1): 같은 계정으로 다른 앱·플러그인이 접속해 이쪽 연결이 끊겼다.
    자동으로 다시 붙지 않는다 (서로 끊으며 반복하지 않게). 여기서 다시 연결하면 다른 쪽이 끊긴다 */
struct ReplacedView : StepView
{
    TitleBlock title { TXT ("다른 곳에서 접속했어요"),
                       TXT ("같은 계정으로 다른 곳(Meon 앱이나 플러그인)에서 접속해서 여기 연결이 끊겼어요. 여기서 계속 쓰려면 다시 연결해 주세요. 그러면 다른 곳의 연결이 끊겨요") };
    Button again { Button::Kind::large, TXT ("여기서 다시 연결") };
    Button logout { Button::Kind::linkSmall, TXT ("로그아웃") };

    ReplacedView (OnboardingScreen& o) : StepView (o)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &title, &again, &logout }) addAndMakeVisible (c);
        again.onPress = [this] { auth().reconnectHere(); };
        logout.onPress = [this] { auth().logout(); };
    }

    int layoutFor (int w) override
    {
        Flow f { 0, w };
        f.put (title, title.heightFor (w));
        f.gap (blockGap);
        f.put (again, metric::bigButtonH);
        f.gap (12);
        const int lw = logout.getIdealWidth();
        logout.setBounds ((w - lw) / 2, f.y, lw, 17);
        f.gap (17);
        return f.y;
    }
};

/** 시작 중 / 서버에 연결하지 못함 */
struct LoadingView : StepView
{
    Text text { {}, 400, 13.0f, col::inkSub };
    Button retry { Button::Kind::secondary, TXT ("다시 시도") };
    LoadingView (OnboardingScreen& o) : StepView (o)
    {
        addAndMakeVisible (text);
        addChildComponent (retry);
        text.setJustification (juce::Justification::centred);
        retry.onPress = [this] { auth().retry(); };
        update();
    }
    void update()
    {
        const bool off = auth().isOffline();
        text.setText (off ? TXT ("서버에 연결하지 못했어요. 인터넷 연결을 확인해 주세요") : TXT ("불러오는 중…"));
        retry.setVisible (off);
    }
    int layoutFor (int w) override
    {
        Flow f { 0, w };
        f.put (text, 20);
        if (retry.isVisible()) { f.gap (12); const int rw = retry.getIdealWidth(); retry.setBounds ((w - rw) / 2, f.y, rw, 32); f.gap (32); }
        return f.y;
    }
};
} // namespace

//==============================================================================
OnboardingScreen::OnboardingScreen (core::MeonCore& c, SettingsModal::UpdateHooks u) : core (c), update (std::move (u))
{
    setOpaque (true);
    content.painter = [this] (juce::Graphics& g) { paintContent (g); };
    progress = std::make_unique<Progress>();
    content.addChildComponent (*progress);
    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    core.auth().addListener (this);
    show (core.auth().step());
    startTimer (500);
}

OnboardingScreen::~OnboardingScreen()
{
    core.auth().removeListener (this);
}

void OnboardingScreen::authStepChanged (Step s)
{
    if (previewOnly) return;
    if (s == shown && s == Step::loading)
    {
        if (auto* lv = dynamic_cast<LoadingView*> (view.get())) { lv->update(); layout(); }
        return;
    }
    show (s);
}

void OnboardingScreen::showStepForPreview (Step s) { previewOnly = true; show (s); }

void OnboardingScreen::show (Step s)
{
    shown = s;
    switch (s)
    {
        case Step::login:    view = std::make_unique<LoginView> (*this); break;
        case Step::signup:   view = std::make_unique<SignupView> (*this); break;
        case Step::verify:   view = std::make_unique<VerifyView> (*this); break;
        case Step::nickname: view = std::make_unique<NicknameView> (*this); break;
        case Step::part:     view = std::make_unique<PartView> (*this); break;
        case Step::discord:  view = std::make_unique<DiscordView> (*this); break;
        case Step::done:     view = std::make_unique<DoneView> (*this); break;
        case Step::outdated: view = std::make_unique<UpdateView> (*this); break;
        case Step::replaced: view = std::make_unique<ReplacedView> (*this); break;
        case Step::loading:
        case Step::ready:    view = std::make_unique<LoadingView> (*this); break;
    }
    content.addAndMakeVisible (*view);
    layout();
    juce::Component::SafePointer<OnboardingScreen> safe (this);
    juce::MessageManager::callAsync ([safe] { if (safe != nullptr && safe->view != nullptr && safe->isShowing()) safe->view->focusFirst(); });
}

void OnboardingScreen::timerCallback()
{
    if (view != nullptr) view->tick();
}

void OnboardingScreen::layout()
{
    if (view == nullptr) return;
    const int w = juce::jmin (columnW, getWidth() - 36);
    const int inner = w - cardPad * 2;
    const int progress = progressFor (shown);
    int viewH = view->layoutFor (inner);
    const int cardH = cardPad * 2 + (progress > 0 ? Progress::height + blockGap : 0) + viewH;
    const int columnH = metric::logoH + blockGap + cardH;

    // 스크롤 영역 안에서 세로 가운데 (margin: auto 0), 위아래 여백 18
    const int areaH = juce::jmax (viewport.getHeight(), columnH + 36);
    content.setSize (getWidth(), areaH);
    const int top = (areaH - columnH) / 2;
    const int x = (getWidth() - w) / 2;
    logoBounds = { x, top, w, metric::logoH };
    cardBounds = { x, top + metric::logoH + blockGap, w, cardH };
    view->setBounds (cardBounds.getX() + cardPad, cardBounds.getY() + cardPad + (progress > 0 ? Progress::height + blockGap : 0), inner, viewH);
    this->progress->setVisible (progress > 0);
    static_cast<Progress*> (this->progress.get())->step = progress;
    this->progress->setBounds (cardBounds.getX() + cardPad, cardBounds.getY() + cardPad, inner, Progress::height);
    this->progress->repaint();
    content.repaint();
    repaint();
}

void OnboardingScreen::resized()
{
    viewport.setBounds (getLocalBounds().withTrimmedTop (titleInset));
    layout();
}

void OnboardingScreen::setTitleBarInset (int inset)
{
    if (inset == titleInset) return;
    titleInset = inset;
    resized();
}

void OnboardingScreen::mouseDown (const juce::MouseEvent& e)
{
    // 맨 윗줄(신호등 줄)의 빈 곳: 창 끌기·더블클릭 (내용은 그 아래 viewport 안이라 여기로 오지 않는다)
    if (titleBarDragAt (e.eventComponent, e.getPosition()) && onTitleBarMouseDown)
        onTitleBarMouseDown();
}

bool OnboardingScreen::titleBarDragAt (const juce::Component* hit, juce::Point<int> p) const
{
    return hit == this && p.y < titleInset;
}

void OnboardingScreen::paint (juce::Graphics& g)
{
    g.fillAll (col::softBg);
}

void OnboardingScreen::paintContent (juce::Graphics& g)
{
    g.fillAll (col::softBg);
    meon::drawLogo (g, logoBounds.toFloat(), metric::logoDrawH, col::ink);
    g.setColour (col::white);
    g.fillRoundedRectangle (cardBounds.toFloat(), metric::radius);
    g.setColour (col::borderSubtle);
    hairline::roundedBorder (g, content, cardBounds.toFloat(), metric::radius);   // content 에 그린다 (paintContent)
}

} // namespace meon::ui
