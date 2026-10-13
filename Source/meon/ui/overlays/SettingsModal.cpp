#include "SettingsModal.h"
#include "../Messages.h"
#include "../Links.h"
#include "../theme/Icons.h"
#include "../../shell/ShellHooks.h"
#include "../../MeonAudio.h"
#include "../../MeonSettings.h"

namespace meon::ui
{

namespace
{
    int textHeight (const juce::String& text, int width, float px = 12.0f, int weight = 400, float lineSpacing = 6.0f)
    {
        if (text.isEmpty() || width <= 0) return 0;
        juce::AttributedString a;
        a.append (text, font (weight, px), col::inkSub);
        a.setLineSpacing (lineSpacing);
        juce::TextLayout t;
        t.createLayout (a, (float) width);
        return (int) std::ceil (t.getHeight());
    }

    void drawWrapped (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r, juce::Colour ink, float px = 12.0f, int weight = 400,
                      float lineSpacing = 6.0f)
    {
        juce::AttributedString a;
        a.append (text, font (weight, px), ink);
        a.setLineSpacing (lineSpacing);
        juce::TextLayout t;
        t.createLayout (a, (float) r.getWidth());
        t.draw (g, r.toFloat());
    }

    /** 펼침 상자 (테두리 #E0E0E0 또는 빨강 테두리, 여백 12) */
    struct Panel : juce::Component
    {
        bool danger = false, filled = true;
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            if (filled && ! danger) { g.setColour (col::softBg); g.fillRoundedRectangle (r, metric::radius); }
            g.setColour (danger ? col::dangerLine : col::line);
            g.drawRoundedRectangle (r.reduced (0.5f), metric::radius, 1.0f);
        }
    };

    /** 가로로 나란히 (오른쪽 정렬, 간격 9) */
    void layoutRight (std::initializer_list<juce::Component*> comps, juce::Rectangle<int> area, int gap = 9)
    {
        int x = area.getRight();
        std::vector<juce::Component*> v (comps);
        for (auto it = v.rbegin(); it != v.rend(); ++it)
        {
            auto* c = *it;
            if (! c->isVisible()) continue;
            int w = c->getWidth();
            if (auto* b = dynamic_cast<Button*> (c)) w = b->getIdealWidth();
            const int h = juce::jmin (area.getHeight(), c->getHeight() > 0 ? c->getHeight() : metric::smallH);
            x -= w;
            c->setBounds (x, area.getY() + (area.getHeight() - h) / 2, w, h);
            x -= gap;
        }
    }
}

//==============================================================================
struct SettingsModal::Row : juce::Component
{
    Row (const juce::String& t, const juce::String& d = {}) : title (t), desc (d) {}

    juce::String title, desc;
    juce::Colour descInk = col::inkSub;
    juce::Component* control = nullptr;   // 오른쪽 (소유하지 않음)
    int controlW = 0, controlH = metric::smallH;
    juce::Component* below = nullptr;     // 아래 펼침 (소유하지 않음)
    std::function<int (int width)> belowHeight;

    void setControl (juce::Component* c, int w, int h = metric::smallH)
    {
        control = c; controlW = w; controlH = h;
        if (c != nullptr) addAndMakeVisible (c);
    }
    void setBelow (juce::Component* c, std::function<int (int)> heightFn)
    {
        below = c; belowHeight = std::move (heightFn);
        if (c != nullptr) addChildComponent (c);
    }

    int textW (int width) const { return width - (control != nullptr && control->isVisible() ? controlW + 18 : 0); }
    int topHeight (int width) const
    {
        const int th = 17 + (desc.isNotEmpty() ? 3 + textHeight (desc, textW (width)) : 0);
        return juce::jmax (th, control != nullptr && control->isVisible() ? controlH : 0);
    }
    bool belowShown() const { return below != nullptr && below->isVisible(); }
    int heightFor (int width) const
    {
        return 9 + topHeight (width) + (belowShown() ? 12 + belowHeight (width) : 0) + 9;
    }

    void paint (juce::Graphics& g) override
    {
        const int tw = textW (getWidth());
        const int th = 17 + (desc.isNotEmpty() ? 3 + textHeight (desc, tw) : 0);
        const int top = 9 + (topHeight (getWidth()) - th) / 2;
        g.setColour (col::ink);
        g.setFont (font (500, 14.0f));
        g.drawText (title, 0, top, tw, 17, juce::Justification::centredLeft, true);
        if (desc.isNotEmpty())
            drawWrapped (g, desc, { 0, top + 17 + 3, tw, textHeight (desc, tw) }, descInk);
    }

    void resized() override
    {
        const int th = topHeight (getWidth());
        if (control != nullptr)
            control->setBounds (getWidth() - controlW, 9 + (th - controlH) / 2, controlW, controlH);
        if (below != nullptr)
            below->setBounds (0, 9 + th + 12, getWidth(), belowShown() ? belowHeight (getWidth()) : 0);
    }
};

struct SettingsModal::Page : juce::Component
{
    explicit Page (SettingsModal& o) : owner (o) {}
    SettingsModal& owner;
    std::vector<Row*> rows;   // 페이지 멤버 (소유하지 않음)
    juce::String topNote;     // 맨 위 안내 (단축키 탭)

    Row& add (Row& r) { rows.push_back (&r); addAndMakeVisible (r); return r; }
    virtual void tick() {}

    int layoutFor (int width)
    {
        int y = 9;
        if (topNote.isNotEmpty()) y += 9 + textHeight (topNote, width) + 9;
        for (auto* r : rows)
        {
            if (! r->isVisible()) continue;
            const int h = r->heightFor (width);
            r->setBounds (0, y, width, h);
            y += h;
        }
        return y + 18;
    }
    void relayout() { owner.layoutPage(); }

    void paint (juce::Graphics& g) override
    {
        if (topNote.isNotEmpty()) drawWrapped (g, topNote, { 0, 18, getWidth(), textHeight (topNote, getWidth()) }, col::inkSub);
    }
};

//==============================================================================
// 계정
namespace
{
    struct AccountPage : SettingsModal::Page
    {
        explicit AccountPage (SettingsModal& o, core::MeonCore& c) : Page (o), core (c)
        {
            // 아이디
            idBox.addAndMakeVisible (copy);
            copy.onPress = [this]
            {
                juce::SystemClipboard::copyTextToClipboard ("@" + username());
                copy.setButtonText (TXT ("복사됨"));
                juce::Component::SafePointer<AccountPage> safe (this);
                juce::Timer::callAfterDelay (1500, [safe] { if (safe != nullptr) { safe->copy.setButtonText (TXT ("복사")); safe->idBox.resized(); } });
                idBox.resized();
            };
            idBox.owner = this;
            add (idRow).setControl (&idBox, 200);

            // 이메일
            add (emailRow).setControl (&emailEdit, emailEdit.getIdealWidth());
            emailRow.setBelow (&emailPanel, [this] (int) { return emailPanelHeight(); });
            emailEdit.onPress = [this] { emailMode = 1; newEmail.setText ({}); emailError = {}; refreshEmail(); newEmail.focus(); };
            for (auto* comp : std::initializer_list<juce::Component*> { &newEmail, &emailCancel, &emailSend, &code, &codeCancel, &codeConfirm, &resend })
                emailPanel.addChildComponent (comp);
            newEmail.setPlaceholder (TXT ("새 이메일"));
            newEmail.onReturn = [this] { sendEmailCode(); };
            newEmail.onChange = [this] { emailError = {}; emailPanel.repaint(); };
            emailCancel.onPress = codeCancel.onPress = [this] { emailMode = 0; emailError = {}; refreshEmail(); };
            emailSend.onPress = [this] { sendEmailCode(); };
            code.onChange = [this] { codeError = false; code.setError (false); emailPanel.repaint(); codeConfirm.setDimmed (! code.isComplete()); };
            code.onSubmit = codeConfirm.onPress = [this] { confirmEmailCode(); };
            resend.onPress = [this]
            {
                if (resendLeft() > 0) return;
                sentAt = juce::Time::currentTimeMillis();
                core.account().resendEmailChange ([] (core::Result<core::Done>) {});
                emailPanel.resized();
            };

            // 비밀번호
            add (pwRow).setControl (&pwEdit, pwEdit.getIdealWidth());
            pwRow.setBelow (&pwPanel, [this] (int) { return 32 * 3 + 9 * 3 + metric::smallH + (pwError.isNotEmpty() ? 18 + 9 : 0); });
            for (auto* comp : std::initializer_list<juce::Component*> { &pwCurrent, &pwNew, &pwConfirm, &pwCancel, &pwSave })
                pwPanel.addAndMakeVisible (comp);
            pwCurrent.setPassword (true); pwNew.setPassword (true); pwConfirm.setPassword (true);
            pwEdit.onPress = [this] { pwOpen = true; pwError = {}; for (auto* i : { &pwCurrent, &pwNew, &pwConfirm }) i->setText ({}); refreshPw(); pwCurrent.focus(); };
            pwCancel.onPress = [this] { pwOpen = false; pwError = {}; refreshPw(); };
            pwSave.onPress = [this] { savePassword(); };
            pwPanel.paintFn = [this] (juce::Graphics& g)
            {
                if (pwError.isNotEmpty())
                {
                    g.setColour (col::danger);
                    g.setFont (font (400, 12.0f));
                    g.drawText (pwError, 0, 32 * 3 + 9 * 3, pwPanel.getWidth(), 18, juce::Justification::centredLeft, true);
                }
            };
            pwPanel.layoutFn = [this]
            {
                const int w = pwPanel.getWidth();
                int y = 0;
                for (auto* i : { &pwCurrent, &pwNew, &pwConfirm }) { i->setBounds (0, y, w, 32); y += 32 + 9; }
                if (pwError.isNotEmpty()) y += 18 + 9;
                layoutRight ({ &pwCancel, &pwSave }, { 0, y, w, metric::smallH });
            };

            // 로그아웃
            add (logoutRow).setControl (&logout, logout.getIdealWidth());
            logout.onPress = [this] { core.account().logout(); };

            // 회원 탈퇴 (3단계)
            add (deleteRow).setControl (&deleteStart, deleteStart.getIdealWidth());
            deleteRow.setBelow (&deletePanel, [this] (int w) { return deletePanelHeight (w); });
            for (auto* comp : std::initializer_list<juce::Component*> { &delCancel, &delNext, &delInput, &delFinal })
                deletePanel.addChildComponent (comp);
            deletePanel.danger = true;
            deleteStart.onPress = [this] { delStep = 1; refreshDelete(); };
            delCancel.onPress = [this] { delStep = 0; delInput.setText ({}); refreshDelete(); };
            delNext.onPress = [this] { delStep = 2; delInput.setText ({}); refreshDelete(); delInput.focus(); };
            delInput.onChange = [this] { delFinal.setDimmed (! deleteOk()); };
            delFinal.onPress = [this]
            {
                if (! deleteOk()) return;
                juce::Component::SafePointer<AccountPage> safe (this);
                core.account().deleteAccount ([safe] (core::Result<core::Done> r)
                {
                    if (safe == nullptr || r.ok()) return;
                    safe->deleteError = msg::general (r.err());
                    safe->deletePanel.repaint();
                });
            };
            deletePanel.paintFn = [this] (juce::Graphics& g) { paintDelete (g); };
            deletePanel.layoutFn = [this] { layoutDelete(); };

            emailPanel.paintFn = [this] (juce::Graphics& g) { paintEmail (g); };
            emailPanel.layoutFn = [this] { layoutEmail(); };

            refreshEmail();
            refreshPw();
            refreshDelete();
        }

        struct Box : juce::Component
        {
            std::function<void (juce::Graphics&)> paintFn;
            std::function<void()> layoutFn;
            bool danger = false, framed = false;
            void paint (juce::Graphics& g) override
            {
                if (framed)
                {
                    auto r = getLocalBounds().toFloat();
                    if (! danger) { g.setColour (col::softBg); g.fillRoundedRectangle (r, metric::radius); }
                    g.setColour (danger ? col::dangerLine : col::line);
                    g.drawRoundedRectangle (r.reduced (0.5f), metric::radius, 1.0f);
                }
                if (paintFn) paintFn (g);
            }
            void resized() override { if (layoutFn) layoutFn(); }
        };

        struct IdBox : juce::Component
        {
            AccountPage* owner = nullptr;
            void paint (juce::Graphics& g) override
            {
                g.setColour (col::ink);
                g.setFont (font (600, 13.0f));
                g.drawText ("@" + owner->username(), 0, 0, owner->copy.getX() - 9, getHeight(), juce::Justification::centredRight, true);
            }
            void resized() override
            {
                const int w = owner->copy.getIdealWidth();
                owner->copy.setBounds (getWidth() - w, (getHeight() - metric::smallH) / 2, w, metric::smallH);
            }
        };

        juce::String username() const { return core.store().state().me ? core.store().state().me->username : juce::String(); }

        //==========================================================================
        // 이메일 변경: 0 보통 / 1 새 이메일 입력 / 2 코드 입력
        int emailMode = 0;
        juce::String emailError, sentTo;
        bool codeError = false, busy = false;
        juce::int64 sentAt = 0;

        int resendLeft() const { return (int) std::max<juce::int64> (0, 30 - (juce::Time::currentTimeMillis() - sentAt) / 1000); }

        int emailPanelHeight() const
        {
            if (emailMode == 1) return 32 + (emailError.isNotEmpty() ? 6 + 18 : 0);
            if (emailMode == 2) return 12 + 36 + 9 + 44 + (codeError ? 9 + 16 : 0) + 9 + metric::smallH + 12;
            return 0;
        }

        void refreshEmail()
        {
            const auto& me = core.store().state().me;
            emailRow.desc = me ? me->email : juce::String();
            emailEdit.setVisible (emailMode == 0);
            emailPanel.setVisible (emailMode != 0);
            emailPanel.framed = emailMode == 2;
            for (auto* comp : std::initializer_list<juce::Component*> { &newEmail, &emailCancel, &emailSend })
                comp->setVisible (emailMode == 1);
            for (auto* comp : std::initializer_list<juce::Component*> { &code, &codeCancel, &codeConfirm, &resend })
                comp->setVisible (emailMode == 2);
            codeConfirm.setDimmed (! code.isComplete());
            emailSend.setButtonText (busy ? TXT ("보내는 중…") : TXT ("인증 코드 받기"));
            codeConfirm.setButtonText (busy ? TXT ("확인 중…") : TXT ("확인"));
            relayout();
            emailPanel.resized();
            emailPanel.repaint();
        }

        void sendEmailCode()
        {
            const auto e = newEmail.getText().trim();
            const auto p = core::validation::email (e);
            if (p != core::validation::Problem::none) { emailError = msg::email (p).isNotEmpty() ? msg::email (p) : TXT ("이메일을 입력해 주세요"); refreshEmail(); return; }
            if (busy) return;
            busy = true;
            refreshEmail();
            juce::Component::SafePointer<AccountPage> safe (this);
            core.account().requestEmailChange (e, [safe, e] (core::Result<core::Done> r)
            {
                if (safe == nullptr) return;
                safe->busy = false;
                if (r.ok()) { safe->emailMode = 2; safe->sentTo = e; safe->sentAt = juce::Time::currentTimeMillis(); safe->code.clear(); safe->codeError = false; }
                else safe->emailError = r.code() == core::ErrorCode::emailTaken ? TXT ("이미 가입된 이메일이에요") : msg::general (r.err());
                safe->refreshEmail();
                if (r.ok()) safe->code.focusFirst();
            });
        }

        void confirmEmailCode()
        {
            if (! code.isComplete() || busy) return;
            busy = true;
            refreshEmail();
            juce::Component::SafePointer<AccountPage> safe (this);
            core.account().verifyEmailChange (code.getCode(), [safe] (core::Result<core::Me> r)
            {
                if (safe == nullptr) return;
                safe->busy = false;
                if (r.ok()) safe->emailMode = 0;
                else { safe->codeError = true; safe->code.setError (true); }
                safe->refreshEmail();
            });
        }

        void paintEmail (juce::Graphics& g)
        {
            const int w = emailPanel.getWidth();
            if (emailMode == 1 && emailError.isNotEmpty())
            {
                g.setColour (col::danger);
                g.setFont (font (400, 12.0f));
                g.drawText (emailError, 0, 32 + 6, w, 18, juce::Justification::centredLeft, true);
            }
            if (emailMode == 2)
            {
                juce::AttributedString a;
                a.append (sentTo, font (600, 12.0f), col::ink);
                a.append (TXT ("(으)로 보낸 6자리 코드를 입력하세요. 확인하면 이메일이 바뀌어요"), font (400, 12.0f), col::inkSub);
                a.setLineSpacing (6.0f);
                juce::TextLayout t;
                t.createLayout (a, (float) w - 24.0f);
                t.draw (g, juce::Rectangle<float> (12.0f, 12.0f, (float) w - 24.0f, 36.0f));
                int y = 12 + 36 + 9 + 44;
                if (codeError)
                {
                    g.setColour (col::danger);
                    g.setFont (font (400, 12.0f));
                    g.drawText (TXT ("코드가 맞지 않아요. 다시 확인해 주세요"), 12, y + 9, w - 24, 16, juce::Justification::centredLeft, true);
                    y += 9 + 16;
                }
                if (resendLeft() > 0)
                {
                    g.setColour (col::inkSub);
                    g.setFont (font (400, 12.0f));
                    g.drawText (juce::String (resendLeft()) + TXT ("초 뒤 다시 받을 수 있어요"), 12, y + 9, w / 2, metric::smallH, juce::Justification::centredLeft, false);
                }
            }
        }

        void layoutEmail()
        {
            const int w = emailPanel.getWidth();
            if (emailMode == 1)
            {
                const int bw = emailCancel.getIdealWidth() + 9 + emailSend.getIdealWidth();
                newEmail.setBounds (0, 0, w - bw - 9, 32);
                layoutRight ({ &emailCancel, &emailSend }, { 0, 0, w, 32 });
            }
            else if (emailMode == 2)
            {
                code.setBounds (12, 12 + 36 + 9, code.getPreferredWidth(), code.getPreferredHeight());
                const int y = 12 + 36 + 9 + 44 + (codeError ? 9 + 16 : 0) + 9;
                resend.setVisible (resendLeft() == 0);
                resend.setBounds (12, y + 1, resend.getIdealWidth(), 17);
                layoutRight ({ &codeCancel, &codeConfirm }, { 12, y, w - 24, metric::smallH });
            }
        }

        //==========================================================================
        bool pwOpen = false, pwDone = false;
        juce::String pwError;

        void refreshPw()
        {
            pwRow.desc = pwDone ? TXT ("비밀번호를 바꿨어요") : TXT ("바꾸면 다른 기기에서는 로그아웃돼요");
            pwEdit.setVisible (! pwOpen);
            pwPanel.setVisible (pwOpen);
            relayout();
            pwPanel.resized();
            pwPanel.repaint();
        }

        void savePassword()
        {
            const auto next = pwNew.getText();
            if (pwCurrent.getText().isEmpty()) { pwError = TXT ("현재 비밀번호를 입력해 주세요"); refreshPw(); return; }
            const auto p = core::validation::password (next);
            if (p != core::validation::Problem::none) { pwError = msg::password (p); refreshPw(); return; }
            if (pwConfirm.getText() != next) { pwError = TXT ("새 비밀번호가 서로 달라요"); refreshPw(); return; }
            juce::Component::SafePointer<AccountPage> safe (this);
            core.account().changePassword (pwCurrent.getText(), next, [safe] (core::Result<core::Done> r)
            {
                if (safe == nullptr) return;
                if (r.ok()) { safe->pwOpen = false; safe->pwDone = true; safe->pwError = {}; }
                else safe->pwError = r.code() == core::ErrorCode::invalidCredentials ? TXT ("현재 비밀번호가 맞지 않아요") : msg::general (r.err());
                safe->refreshPw();
            });
        }

        //==========================================================================
        int delStep = 0;
        juce::String deleteError;

        bool deleteOk() const { return delInput.getText().trim().trimCharactersAtStart ("@") == username() && username().isNotEmpty(); }

        int deletePanelHeight (int w) const
        {
            if (delStep == 1) return 12 + 17 + 9 + textHeight (step1Text(), w - 24) + 9 + metric::smallH + 12;
            if (delStep == 2) return 12 + 17 + 9 + 18 + 9 + 32 + 9 + metric::smallH + (deleteError.isNotEmpty() ? 9 + 16 : 0) + 12;
            return 0;
        }

        juce::String step1Text() const
        {
            return TXT ("30일 안에 다시 로그인하면 탈퇴가 취소되고 친구 ") + juce::String ((int) core.store().state().friends.size())
                   + TXT ("명과 설정이 그대로 돌아와요. 30일이 지나면 되돌릴 수 없어요");
        }

        void refreshDelete()
        {
            deleteStart.setVisible (delStep == 0);
            deletePanel.setVisible (delStep != 0);
            deletePanel.framed = true;
            delCancel.setVisible (delStep != 0);
            delNext.setVisible (delStep == 1);
            delInput.setVisible (delStep == 2);
            delFinal.setVisible (delStep == 2);
            delFinal.setDimmed (! deleteOk());
            delInput.setPlaceholder ("@" + username());
            relayout();
            deletePanel.resized();
            deletePanel.repaint();
        }

        void paintDelete (juce::Graphics& g)
        {
            const int w = deletePanel.getWidth() - 24;
            g.setColour (col::ink);
            g.setFont (font (600, 13.0f));
            g.drawText (delStep == 1 ? TXT ("정말 탈퇴할까요?") : TXT ("마지막 확인"), 12, 12, w, 17, juce::Justification::centredLeft, false);
            if (delStep == 1)
                drawWrapped (g, step1Text(), { 12, 12 + 17 + 9, w, textHeight (step1Text(), w) }, col::inkSub);
            else if (delStep == 2)
            {
                juce::AttributedString a;
                a.append (TXT ("확인을 위해 내 아이디 "), font (400, 12.0f), col::inkSub);
                a.append ("@" + username(), font (600, 12.0f), col::ink);
                a.append (TXT ("을(를) 입력해 주세요"), font (400, 12.0f), col::inkSub);
                juce::TextLayout t;
                t.createLayout (a, (float) w);
                t.draw (g, juce::Rectangle<float> (12.0f, 12.0f + 17.0f + 9.0f, (float) w, 18.0f));
                if (deleteError.isNotEmpty())
                {
                    g.setColour (col::danger);
                    g.setFont (font (400, 12.0f));
                    g.drawText (deleteError, 12, deletePanel.getHeight() - 12 - 16, w, 16, juce::Justification::centredLeft, true);
                }
            }
        }

        void layoutDelete()
        {
            const int w = deletePanel.getWidth() - 24;
            if (delStep == 1)
            {
                const int y = 12 + 17 + 9 + textHeight (step1Text(), w) + 9;
                layoutRight ({ &delCancel, &delNext }, { 12, y, w, metric::smallH });
            }
            else if (delStep == 2)
            {
                int y = 12 + 17 + 9 + 18 + 9;
                delInput.setBounds (12, y, w, 32);
                y += 32 + 9;
                layoutRight ({ &delCancel, &delFinal }, { 12, y, w, metric::smallH });
            }
        }

        void tick() override
        {
            if (emailMode == 2) { emailPanel.resized(); emailPanel.repaint(); }
            const auto& me = core.store().state().me;
            if (me && emailRow.desc != me->email && emailMode == 0) { emailRow.desc = me->email; emailRow.repaint(); }
            idBox.repaint();
        }

        core::MeonCore& core;
        SettingsModal::Row idRow { TXT ("아이디"), TXT ("친구가 나를 추가할 때 이 아이디를 입력해요") };
        IdBox idBox;
        Button copy { Button::Kind::small, TXT ("복사") };
        SettingsModal::Row emailRow { TXT ("이메일") };
        Button emailEdit { Button::Kind::small, TXT ("변경") };
        Box emailPanel;
        TextInput newEmail;
        Button emailCancel { Button::Kind::small, TXT ("취소") }, emailSend { Button::Kind::smallPrimary, TXT ("인증 코드 받기") };
        OtpInput code { OtpInput::Charset::digits, OtpInput::Size::small };
        Button codeCancel { Button::Kind::small, TXT ("취소") }, codeConfirm { Button::Kind::smallPrimary, TXT ("확인") };
        Button resend { Button::Kind::linkUnderline, TXT ("코드 다시 받기") };
        SettingsModal::Row pwRow { TXT ("비밀번호") };
        Button pwEdit { Button::Kind::small, TXT ("변경") };
        Box pwPanel;
        TextInput pwCurrent { TXT ("현재 비밀번호") }, pwNew { TXT ("영문·숫자·특수문자 포함 8자 이상") }, pwConfirm { TXT ("새 비밀번호 확인") };
        Button pwCancel { Button::Kind::small, TXT ("취소") }, pwSave { Button::Kind::smallPrimary, TXT ("저장") };
        SettingsModal::Row logoutRow { TXT ("로그아웃"), TXT ("이 기기에서 로그아웃해요") };
        Button logout { Button::Kind::small, TXT ("로그아웃") };
        SettingsModal::Row deleteRow { TXT ("회원 탈퇴"), TXT ("신청하고 30일이 지나면 계정과 친구, 차단 목록, 설정이 모두 지워져요. 30일 안에 다시 로그인하면 탈퇴가 취소돼요") };
        Button deleteStart { Button::Kind::smallDanger, TXT ("탈퇴") };
        Box deletePanel;
        Button delCancel { Button::Kind::small, TXT ("취소") }, delNext { Button::Kind::smallDanger, TXT ("계속") };
        TextInput delInput;
        Button delFinal { Button::Kind::smallDanger, TXT ("탈퇴하기") };
    };

    //==========================================================================
    // 프로필
    struct ProfilePage : SettingsModal::Page
    {
        ProfilePage (SettingsModal& o, core::MeonCore& c) : Page (o), core (c)
        {
            add (nickRow).setControl (&nick, 220, 32);
            add (partRow).setControl (&part, 220, 32);
            const auto& me = core.store().state().me;
            nick.setMaxLength (12);
            nick.setText (me && me->nickname ? *me->nickname : juce::String());
            nick.onReturn = nick.onFocusLost = [this] { saveNick(); };
            nick.onChange = [this] { nick.setError (false); nickRow.descInk = col::inkSub; nickRow.desc = TXT ("합주실과 친구 목록에 보이는 이름이에요"); nickRow.repaint(); };
            part.setItems ({ "guitar", "bass", "drums", "keys", "vocal", "other" },
                           { TXT ("기타"), TXT ("베이스"), TXT ("드럼"), TXT ("키보드"), TXT ("보컬"), TXT ("그 외") });
            part.setValue (me && me->part ? *me->part : juce::String ("other"));
            part.onValueChange = [this] (const juce::String& v) { core.account().setPart (v); };
        }

        void saveNick()
        {
            const auto n = nick.getText().trim();
            const auto& me = core.store().state().me;
            if (me && me->nickname && *me->nickname == n) return;
            const auto p = core::validation::nickname (n);
            if (p != core::validation::Problem::none)
            {
                nick.setError (true);
                nickRow.desc = msg::nickname (p).isNotEmpty() ? msg::nickname (p) : TXT ("닉네임을 입력해 주세요");
                nickRow.descInk = col::danger;
                nickRow.repaint();
                return;
            }
            juce::Component::SafePointer<ProfilePage> safe (this);
            core.account().setNickname (n, [safe] (core::Result<core::Me> r)
            {
                if (safe == nullptr) return;
                safe->nickRow.desc = r.ok() ? TXT ("저장했어요 · 합주실과 친구 목록에 보이는 이름이에요") : msg::general (r.err());
                safe->nickRow.descInk = r.ok() ? col::inkSub : col::danger;
                safe->nickRow.repaint();
            });
        }

        core::MeonCore& core;
        SettingsModal::Row nickRow { TXT ("닉네임"), TXT ("합주실과 친구 목록에 보이는 이름이에요") };
        SettingsModal::Row partRow { TXT ("파트"), TXT ("방에서 이름 옆에 표시돼요") };
        TextInput nick;
        Select part;
    };

    //==========================================================================
    // 오디오 (앱만): 드라이버(Windows) · 입력·출력 장치 · 장치 경고(Windows) · 버퍼 · 입력 채널 · 입력 테스트
    struct AudioPage : SettingsModal::Page, private juce::ChangeListener
    {
        AudioPage (SettingsModal& o, core::MeonCore& c) : Page (o), core (c)
        {
            dm = [] { auto& p = meon::deviceManagerProvider(); return p ? p() : nullptr; }();
            if (dm != nullptr && ! audio::driverTypes (*dm).isEmpty())
                add (driverRow).setControl (&driver, 220, 32);
            add (inRow).setControl (&inDev, 220, 32);
            add (outRow).setControl (&outDev, 220, 32);
            add (warnRow);
            warnRow.setBelow (&warnBox, [this] (int w) { return warnHeight (w); });
            add (bufRow).setControl (&bufBox, 220 + 9 + panelBtn.getIdealWidth(), 32);
            add (chRow).setControl (&chBox, 220, metric::smallH * 2 + 9 + 32 - 19);
            add (testRow).setControl (&testBox, 140 + 9 + test.getIdealWidth());

            driver.onValueChange = [this] (const juce::String& t) { if (dm) { audio::setDriverType (*dm, t); fill(); } };
            inDev.onValueChange = [this] (const juce::String& v) { if (dm) { audio::setDevices (*dm, v, outDev.getValue()); fill(); } };
            outDev.onValueChange = [this] (const juce::String& v) { if (dm) { audio::setDevices (*dm, inDev.getValue(), v); fill(); } };
            buffer.onValueChange = [this] (const juce::String& v) { if (dm) audio::setBufferSize (*dm, v.getIntValue()); };
            bufBox.addAndMakeVisible (buffer);
            bufBox.addChildComponent (panelBtn);
            panelBtn.onPress = [this] { if (dm) audio::showDriverPanel (*dm); };
            bufBox.layoutFn = [this]
            {
                const int pw = panelBtn.isVisible() ? panelBtn.getIdealWidth() + 9 : 0;
                buffer.setBounds (bufBox.getWidth() - 220, 0, 220, 32);
                panelBtn.setBounds (bufBox.getWidth() - 220 - pw, (32 - metric::smallH) / 2, pw - 9, metric::smallH);
            };

            // 입력 채널: 모노/스테레오 + 위치 (Q8)
            chBox.addAndMakeVisible (chMode);
            chBox.addAndMakeVisible (chStart);
            chMode.setOptions ({ "1", "2" }, { TXT ("모노"), TXT ("스테레오") });
            chMode.onChange = [this] { applyChannels(); };
            chStart.onValueChange = [this] (const juce::String&) { applyChannels(); };
            chBox.layoutFn = [this]
            {
                const int cw = chMode.getPreferredWidth();
                chMode.setBounds (chBox.getWidth() - cw, 0, cw, metric::smallH);
                chStart.setBounds (chBox.getWidth() - 220, metric::smallH + 9, 220, 32);
            };

            // 입력 테스트
            testBox.addAndMakeVisible (meter);
            testBox.addAndMakeVisible (test);
            meter.meter.setActive (false);
            if (auto* jam = core.jam())
                meter.meter.setSource (2, [this] (int ch) { auto* j = core.jam(); return j != nullptr ? j->inputLevel (ch) : 0.0f; });
            test.onPress = [this]
            {
                testing = ! testing;
                meter.meter.setActive (testing);
                test.setButtonText (testing ? TXT ("중지") : TXT ("테스트"));
                testRow.desc = testing ? TXT ("악기를 연주하거나 말해 보세요") : TXT ("선택한 입력 장치로 소리가 들어오는지 확인해요");
                testBox.resized();
                testRow.repaint();
            };
            testBox.layoutFn = [this]
            {
                const int tw = test.getIdealWidth();
                test.setBounds (testBox.getWidth() - tw, 0, tw, metric::smallH);
                meter.setBounds (testBox.getWidth() - tw - 9 - 140, 0, 140, metric::smallH);
            };

            // 장치 경고 (Windows: 장치 없음 / ASIO 아님)
            warnBox.addChildComponent (rescan);
            warnBox.addChildComponent (asioGuide);
            rescan.onPress = [this]
            {
                if (dm != nullptr)
                    if (auto* t = dm->getCurrentDeviceTypeObject()) t->scanForDevices();
                fill();
            };
            asioGuide.onPress = [] { links::open ("https://github.com/meonhq/desktop/blob/main/doc/INSTALL.md#windows-asio"); };
            warnBox.paintFn = [this] (juce::Graphics& g) { paintWarning (g); };
            warnBox.layoutFn = [this]
            {
                const int y = warnBox.getHeight() - 12 - metric::smallH;
                rescan.setBounds (12, y, rescan.getIdealWidth(), metric::smallH);
                asioGuide.setBounds (12 + rescan.getIdealWidth() + 9, y, asioGuide.getIdealWidth(), metric::smallH);
            };
            warnRow.title = {};

            if (dm != nullptr) dm->addChangeListener (this);
            fill();
        }

        ~AudioPage() override { if (dm != nullptr) dm->removeChangeListener (this); }

        struct Box : juce::Component
        {
            std::function<void (juce::Graphics&)> paintFn;
            std::function<void()> layoutFn;
            bool framed = false;
            void paint (juce::Graphics& g) override
            {
                if (framed)
                {
                    auto r = getLocalBounds().toFloat();
                    g.setColour (col::accentSoft);
                    g.fillRoundedRectangle (r, metric::radius);
                    g.setColour (juce::Colour (0xFFF0DE8Au));
                    g.drawRoundedRectangle (r.reduced (0.5f), metric::radius, 1.0f);
                }
                if (paintFn) paintFn (g);
            }
            void resized() override { if (layoutFn) layoutFn(); }
        };

        juce::String warnTitle, warnText;

        int warnHeight (int w) const
        {
            if (warnTitle.isEmpty()) return 0;
            return 12 + 17 + 6 + textHeight (warnText, w - 24) + 12 + metric::smallH + 12;
        }

        void paintWarning (juce::Graphics& g)
        {
            g.setColour (col::ink);
            g.setFont (font (600, 13.0f));
            g.drawText (warnTitle, 12, 12, warnBox.getWidth() - 24, 17, juce::Justification::centredLeft, true);
            drawWrapped (g, warnText, { 12, 12 + 17 + 6, warnBox.getWidth() - 24, textHeight (warnText, warnBox.getWidth() - 24) }, col::inkSub);
        }

        void changeListenerCallback (juce::ChangeBroadcaster*) override { fill(); }

        void fill()
        {
            const bool inRoom = core.store().state().inRoom();
            const auto jamNote = TXT ("합주 중에 바꾸면 소리가 잠깐 끊겨요");
            inRow.desc = inRoom ? jamNote : juce::String();
            bufRow.desc = inRoom ? jamNote : TXT ("작을수록 지연이 줄지만, 컴퓨터가 버거우면 소리가 끊겨요");
            chRow.desc = inRoom ? jamNote : TXT ("기타·보컬처럼 한 줄이면 모노, 키보드처럼 두 줄이면 스테레오");
            bool noDevices = true;
            if (dm != nullptr)
            {
                const auto types = audio::driverTypes (*dm);
                juce::StringArray labels;
                for (auto& t : types) labels.add (audio::driverLabel (t));
                driver.setItems (types, labels);
                driver.setValue (dm->getCurrentAudioDeviceType());

                auto setup = dm->getAudioDeviceSetup();
                juce::StringArray ins, outs;
                if (auto* type = dm->getCurrentDeviceTypeObject())
                {
                    ins = type->getDeviceNames (true);
                    outs = type->getDeviceNames (false);
                }
                noDevices = ins.isEmpty() && outs.isEmpty();
                const auto none = TXT ("장치를 찾을 수 없음");
                inDev.setItems (ins.isEmpty() ? juce::StringArray (none) : ins);
                outDev.setItems (outs.isEmpty() ? juce::StringArray (none) : outs);
                inDev.setValue (setup.inputDeviceName);
                outDev.setValue (setup.outputDeviceName);

                const auto sizes = audio::bufferChoices (*dm);
                double sr = setup.sampleRate > 0 ? setup.sampleRate : 48000.0;
                if (auto* dev = dm->getCurrentAudioDevice()) sr = dev->getCurrentSampleRate();
                juce::StringArray vals, labs;
                for (auto s : sizes) { vals.add (juce::String (s)); labs.add (juce::String (s) + TXT (" 샘플 · ") + juce::String (s / sr * 1000.0, 1) + "ms"); }
                buffer.setItems (vals, labs);
                buffer.setValue (juce::String (audio::currentBufferSize (*dm)));
                panelBtn.setVisible (audio::hasDriverPanel (*dm));

                // 입력 채널 위치
                int chans = 0;
                if (auto* dev = dm->getCurrentAudioDevice()) chans = dev->getActiveInputChannels().getHighestBit() + 1;
                if (auto* dev = dm->getCurrentAudioDevice()) chans = juce::jmax (chans, dev->getInputChannelNames().size());
                const auto& prefs = core.jam() != nullptr ? core.jam()->getPrefs() : core::JamPrefs();
                const int count = juce::jlimit (1, 2, prefs.inputChannelCount);
                chMode.setSelected (juce::String (count));
                juce::StringArray sv, sl;
                for (int i = 0; i < juce::jmax (1, chans - (count - 1)); ++i)
                {
                    sv.add (juce::String (i));
                    sl.add (TXT ("입력 ") + juce::String (i + 1) + (count == 2 ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")) + juce::String (i + 2) : juce::String()));
                }
                chStart.setItems (sv, sl);
                chStart.setValue (juce::String (prefs.inputChannelStart));
            }

            // 장치 경고 (Windows 만, 지금 오디오 화면의 판정·문구를 옮김)
            warnTitle = warnText = {};
           #if JUCE_WINDOWS
            const bool asio = dm != nullptr && dm->getCurrentAudioDeviceType() == "ASIO";
            if (noDevices)
            {
                warnTitle = TXT ("오디오 장치를 찾지 못했어요");
                warnText = asio ? TXT ("ASIO 드라이버가 있는 장치가 없어요. 위에서 드라이버를 ‘Windows 오디오’로 바꾸거나, 오디오 인터페이스의 ASIO 드라이버를 설치해 주세요.")
                                : TXT ("장치를 연결한 뒤 ‘장치 다시 검색’을 눌러 주세요.");
            }
            else if (! asio)
            {
                warnTitle = TXT ("Windows 오디오로 연결했어요");
                warnText = TXT ("이대로도 합주할 수 있지만 지연이 조금 더 커요. 오디오 인터페이스가 있다면 ASIO 드라이버를 쓰는 것이 좋아요.");
            }
           #else
            juce::ignoreUnused (noDevices);
           #endif
            warnRow.setVisible (warnTitle.isNotEmpty());
            warnBox.setVisible (warnTitle.isNotEmpty());
            warnBox.framed = true;
            rescan.setVisible (warnTitle.isNotEmpty());
            asioGuide.setVisible (warnTitle.isNotEmpty());
            relayout();
            for (auto* b : { &bufBox, &chBox, &testBox, &warnBox }) b->resized();
            repaint();
        }

        void applyChannels()
        {
            if (auto* jam = core.jam())
                jam->setInputChannels (chStart.getValue().getIntValue(), chMode.getSelected().getIntValue());
            fill();
        }

        core::MeonCore& core;
        juce::AudioDeviceManager* dm = nullptr;
        bool testing = false;
        SettingsModal::Row driverRow { TXT ("드라이버") }, inRow { TXT ("입력 장치") }, outRow { TXT ("출력 장치") }, warnRow { {} },
                           bufRow { TXT ("버퍼 크기") }, chRow { TXT ("입력 채널") },
                           testRow { TXT ("입력 테스트"), TXT ("선택한 입력 장치로 소리가 들어오는지 확인해요") };
        Select driver, inDev, outDev, buffer, chStart;
        Box bufBox, chBox, testBox, warnBox;
        Button panelBtn { Button::Kind::small, TXT ("드라이버 설정") };
        Choice chMode { Choice::Style::small };
        MeterBox meter;
        Button test { Button::Kind::small, TXT ("테스트") };
        Button rescan { Button::Kind::small, TXT ("장치 다시 검색") }, asioGuide { Button::Kind::small, TXT ("ASIO 설치 안내 보기") };
    };

    //==========================================================================
    // 합주
    struct JamPage : SettingsModal::Page
    {
        JamPage (SettingsModal& o, core::MeonCore& c) : Page (o), core (c)
        {
            if (core.isPlugin())
            {
                add (sendRow).setControl (&sendCh, 0);
                sendCh.setOptions ({ "1", "2" }, { TXT ("모노"), TXT ("스테레오") });
                sendRow.controlW = sendCh.getPreferredWidth();
                if (auto* jam = core.jam()) sendCh.setSelected (juce::String (jam->getPrefs().pluginSendChannels));
                sendCh.onChange = [this] { if (auto* jam = core.jam()) jam->setPluginSendChannels (sendCh.getSelected().getIntValue()); };
            }
            add (bufRow).setControl (&bufMode, 0);
            add (selfRow).setControl (&selfMon, 26, 16);
            add (keepRow).setControl (&keepVol, 26, 16);
            add (earRow).setControl (&ear, 0);
            bufMode.setOptions ({ "latency", "stable" }, { TXT ("지연 우선"), TXT ("안정 우선") });
            ear.setOptions ({ "0", "1", "2" }, { TXT ("끔"), TXT ("보통"), TXT ("강하게") });
            bufRow.controlW = bufMode.getPreferredWidth();
            earRow.controlW = ear.getPreferredWidth();
            selfRow.desc = core.isPlugin() ? TXT ("플러그인으로 쓸 때는 DAW에서 이미 내 소리를 듣고 있어서 기본이 꺼짐이에요")
                                           : TXT ("내 소리를 MEON에서 함께 들을지 정해요");
            if (auto* jam = core.jam())
            {
                const auto& p = jam->getPrefs();
                bufMode.setSelected (p.bufferMode == core::Engine::BufferMode::stable ? "stable" : "latency");
                selfMon.setOn (p.selfMonitor, false);
                keepVol.setOn (p.keepMasterVolume, false);
                ear.setSelected (juce::String (p.earGuard));
            }
            refreshEar();
            bufMode.onChange = [this]
            {
                if (auto* jam = core.jam())
                {
                    const auto m = bufMode.getSelected() == "stable" ? core::Engine::BufferMode::stable : core::Engine::BufferMode::latency;
                    if (jam->isInRoom()) jam->setBufferMode (m);   // 합주 중이면 바로 (모든 피어에)
                    else jam->setDefaultBufferMode (m);
                }
            };
            selfMon.onChange = [this] (bool on) { if (auto* jam = core.jam()) jam->setSelfMonitor (on); };
            keepVol.onChange = [this] (bool on) { if (auto* jam = core.jam()) jam->setKeepMasterVolume (on); };
            ear.onChange = [this] { if (auto* jam = core.jam()) jam->setEarGuardLevel (ear.getSelected().getIntValue()); refreshEar(); };
        }

        void refreshEar()
        {
            const int l = ear.getSelected().getIntValue();
            earRow.desc = l == 0 ? TXT ("갑자기 큰 소리가 나도 줄여주지 않아요")
                        : (l == 2 ? TXT ("큰 소리를 더 일찍, 더 많이 줄여줘요. 소리가 조금 얌전해질 수 있어요.")
                                  : TXT ("귀를 찌르는 소리만 살짝 눌러줘요. 평소 소리는 그대로예요."));
            relayout();
        }

        core::MeonCore& core;
        SettingsModal::Row sendRow { TXT ("보내는 채널"), TXT ("DAW 트랙의 소리를 모노 또는 스테레오로 보내요. 파일 소리도 같은 채널에 섞여요") };
        Choice sendCh { Choice::Style::small };
        SettingsModal::Row bufRow { TXT ("버퍼 모드 기본값"), TXT ("방에 들어갈 때 처음 적용돼요. 합주 중에도 바꿀 수 있어요") },
                           selfRow { TXT ("셀프 모니터 기본값") },
                           keepRow { TXT ("마스터 볼륨 기억"), TXT ("방을 나갔다 들어와도 마지막 볼륨을 유지해요") },
                           earRow { TXT ("귀 테러 방지") };
        Choice bufMode { Choice::Style::small }, ear { Choice::Style::small };
        Toggle selfMon, keepVol;
    };

    //==========================================================================
    // 친구 · 개인정보
    struct PrivacyPage : SettingsModal::Page, private core::AppStore::Listener
    {
        PrivacyPage (SettingsModal& o, core::MeonCore& c) : Page (o), core (c)
        {
            add (reqRow).setControl (&friendReq, 0);
            add (invRow).setControl (&invites, 0);
            add (blockRow);
            blockRow.setBelow (&blockList, [this] (int) { return blocks.empty() ? 17 : (int) blocks.size() * 38; });
            blockList.setVisible (true);
            friendReq.setOptions ({ "everyone", "none" }, { TXT ("모두"), TXT ("받지 않음") });
            invites.setOptions ({ "friends", "none" }, { TXT ("친구만"), TXT ("받지 않음") });
            reqRow.controlW = friendReq.getPreferredWidth();
            invRow.controlW = invites.getPreferredWidth();
            friendReq.onChange = [this] { core.account().setFriendRequests (friendReq.getSelected()); };
            invites.onChange = [this] { core.account().setRoomInvites (invites.getSelected()); };
            blockList.owner = this;
            core.store().addListener (this);
            fromMe();
            loadBlocks();
        }
        ~PrivacyPage() override { core.store().removeListener (this); }

        void stateChanged (core::StateKey k) override { if (k == core::StateKey::me) fromMe(); }
        void fromMe()
        {
            if (const auto& me = core.store().state().me)
            {
                friendReq.setSelected (me->settings.friendRequests);
                invites.setSelected (me->settings.roomInvites);
            }
        }

        void loadBlocks()
        {
            juce::Component::SafePointer<PrivacyPage> safe (this);
            core.account().listBlocks ([safe] (core::Result<std::vector<core::Block>> r)
            {
                if (safe == nullptr || ! r.ok()) return;
                safe->blocks = r.get();
                safe->blockList.rebuild();
                safe->relayout();
            });
        }

        struct BlockList : juce::Component
        {
            PrivacyPage* owner = nullptr;
            juce::OwnedArray<Button> buttons;
            void rebuild()
            {
                buttons.clear();
                for (auto& b : owner->blocks)
                {
                    auto* btn = buttons.add (new Button (Button::Kind::small, TXT ("해제")));
                    const auto id = b.user.id;
                    btn->onPress = [this, id]
                    {
                        auto* page = owner;
                        page->core.account().unblock (id, [safe = juce::Component::SafePointer<PrivacyPage> (page)] (core::Result<core::Done>)
                        {
                            if (safe != nullptr) safe->loadBlocks();
                        });
                    };
                    addAndMakeVisible (btn);
                }
                resized();
                repaint();
            }
            void paint (juce::Graphics& g) override
            {
                if (owner->blocks.empty())
                {
                    g.setColour (col::inkFaint);
                    g.setFont (font (400, 13.0f));
                    g.drawText (TXT ("차단한 사람이 없어요"), getLocalBounds(), juce::Justification::centredLeft, false);
                    return;
                }
                auto r = getLocalBounds().toFloat();
                g.setColour (col::line);
                g.drawRoundedRectangle (r.reduced (0.5f), metric::radius, 1.0f);
                for (size_t i = 0; i < owner->blocks.size(); ++i)
                {
                    const int y = (int) i * 38;
                    if (i > 0) g.fillRect (1, y, getWidth() - 2, 1);
                    const auto& u = owner->blocks[i].user;
                    const auto nf = font (600, 13.0f);
                    const auto nm = u.nickname.value_or (u.username);
                    const int nw = (int) std::ceil (textWidth (nf, nm)) + 2;
                    g.setColour (col::ink);
                    g.setFont (nf);
                    g.drawText (nm, 12, y, nw, 38, juce::Justification::centredLeft, false);
                    g.setColour (col::inkFaint);
                    g.setFont (font (400, 12.0f));
                    g.drawText ("@" + u.username, 12 + nw + 9, y, getWidth() - nw - 120, 38, juce::Justification::centredLeft, true);
                }
            }
            void resized() override
            {
                for (int i = 0; i < buttons.size(); ++i)
                {
                    const int w = buttons[i]->getIdealWidth();
                    buttons[i]->setBounds (getWidth() - 12 - w, i * 38 + (38 - metric::smallH) / 2, w, metric::smallH);
                }
            }
        };

        core::MeonCore& core;
        std::vector<core::Block> blocks;
        SettingsModal::Row reqRow { TXT ("친구 요청 받기") }, invRow { TXT ("방 초대 받기") },
                           blockRow { TXT ("차단 목록"), TXT ("내가 방장인 방이 서로 안 보이고, 친구 요청을 보낼 수 없어요") };
        Choice friendReq { Choice::Style::small }, invites { Choice::Style::small };
        BlockList blockList;
    };

    //==========================================================================
    // 알림
    struct NotifPage : SettingsModal::Page
    {
        NotifPage (SettingsModal& o) : Page (o)
        {
            add (soundRow).setControl (&sound, 26, 16);
            MeonSettings s;
            sound.setOn (s.properties().getBoolValue ("toastSound", true), false);
            sound.onChange = [] (bool on) { MeonSettings s2; s2.properties().setValue ("toastSound", on); s2.save(); };
        }
        SettingsModal::Row soundRow { TXT ("초대 토스트 소리"), TXT ("합주 중에는 이 설정과 관계없이 소리 없이 떠요") };
        Toggle sound;
    };

    //==========================================================================
    // 단축키 (고정 목록, 4.4)
    struct KeysPage : SettingsModal::Page
    {
        struct Keys : juce::Component
        {
            juce::StringArray caps;
            int idealWidth() const
            {
                int w = 0;
                for (auto& c : caps) w += juce::jmax (24, (int) std::ceil (textWidth (font (500, 12.0f), c)) + 12) + 4;
                return w;
            }
            void paint (juce::Graphics& g) override
            {
                int x = getWidth() - idealWidth() + 4;
                for (auto& c : caps)
                {
                    const int w = juce::jmax (24, (int) std::ceil (textWidth (font (500, 12.0f), c)) + 12);
                    juce::Rectangle<float> r ((float) x, 0.0f, (float) w, 24.0f);
                    g.setColour (col::white);
                    g.fillRoundedRectangle (r, 5.0f);
                    g.setColour (col::field);
                    g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
                    g.fillRect (r.getX() + 2.0f, r.getBottom() - 1.5f, r.getWidth() - 4.0f, 1.0f);
                    g.setColour (col::ink);
                    g.setFont (font (500, 12.0f));
                    g.drawText (c, r, juce::Justification::centred, false);
                    x += w + 4;
                }
            }
        };

        explicit KeysPage (SettingsModal& o) : Page (o)
        {
            topNote = TXT ("글자를 입력하는 중에는 동작하지 않아요. 단축키 변경은 나중에 지원할 예정이에요");
           #if JUCE_MAC
            const juce::String cmd = juce::String (juce::CharPointer_UTF8 ("\xe2\x8c\x98"));
           #else
            const juce::String cmd = "Ctrl";
           #endif
            const std::pair<const char*, juce::StringArray> list[] = {
                { "내 송출 끄기 / 켜기", { "M" } }, { "모니터 끄기 / 켜기", { "N" } },
                { "마스터 볼륨 올리기", { cmd, juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x91")) } },
                { "마스터 볼륨 내리기", { cmd, juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x93")) } },
                { "채팅 입력", { "Enter" } }, { "설정 열기", { cmd, "," } }, { "창 닫기", { "Esc" } } };
            for (auto& k : list)
            {
                auto* row = rowStore.add (new SettingsModal::Row (juce::String::fromUTF8 (k.first)));
                auto* keys = keyStore.add (new Keys());
                keys->caps = k.second;
                row->setControl (keys, keys->idealWidth(), 24);
                add (*row);
            }
        }
        juce::OwnedArray<Keys> keyStore;
        juce::OwnedArray<SettingsModal::Row> rowStore;
    };

    //==========================================================================
    // 정보
    struct AboutPage : SettingsModal::Page
    {
        AboutPage (SettingsModal& o, core::MeonCore& c, SettingsModal::UpdateHooks& u) : Page (o), core (c), update (u)
        {
            add (verRow);
            if (! core.isPlugin() && update.state) verRow.setControl (&updateBtn, 0);
            add (logRow).setControl (&openLogs, openLogs.getIdealWidth());
            add (licRow).setControl (&licToggle, licToggle.getIdealWidth());
            licRow.setBelow (&licBox, [this] (int w) { return 12 + 17 + 6 + textHeight (licText(), w - 24, 12.0f, 400, 7.2f) + 6 + 17 + 12; });
            add (askRow).setControl (&discord, discord.getIdealWidth());
            openLogs.onPress = [] { MeonSettings::getLogFolder().createDirectory(); MeonSettings::getLogFolder().startAsProcess(); };
            licToggle.onPress = [this]
            {
                licBox.setVisible (! licBox.isVisible());
                licToggle.setButtonText (licBox.isVisible() ? TXT ("접기") : TXT ("보기"));
                licRow.controlW = licToggle.getIdealWidth();
                relayout();
            };
            discord.onPress = [this]
            {
                const auto url = core.store().state().config.discordUrl;
                links::open (url.isNotEmpty() ? url : juce::String (links::discordFallback()));
            };
            updateBtn.onPress = [this]
            {
                using S = SettingsModal::UpdateHooks::State;
                const auto st = update.state();
                if (st == S::available || st == S::failed) { if (update.start) update.start(); }
                else if (st != S::checking && st != S::downloading && st != S::installing) { if (update.check) update.check(); }
            };
            for (auto* l : { &licFull, &source, &sono })
                licBox.addAndMakeVisible (l);
            licFull.onPress = [] { links::open ("https://www.gnu.org/licenses/gpl-3.0.html"); };
            source.onPress = [] { links::open ("https://github.com/meonhq/desktop"); };
            sono.onPress = [] { links::open ("https://github.com/sonosaurus/sonobus"); };
            licBox.layoutFn = [this]
            {
                const int y = licBox.getHeight() - 12 - 17;
                int x = 12;
                for (auto* l : { &licFull, &source, &sono }) { const int w = l->getIdealWidth(); l->setBounds (x, y, w, 17); x += w + 12; }
            };
            licBox.paintFn = [this] (juce::Graphics& g)
            {
                g.setColour (col::ink);
                g.setFont (font (600, 13.0f));
                g.drawText (TXT ("SonoBus · GNU General Public License v3.0"), 12, 12, licBox.getWidth() - 24, 17, juce::Justification::centredLeft, true);
                drawWrapped (g, licText(), { 12, 12 + 17 + 6, licBox.getWidth() - 24, textHeight (licText(), licBox.getWidth() - 24, 12.0f, 400, 7.2f) },
                             col::inkSub, 12.0f, 400, 7.2f);
            };
            licBox.framed = true;
            tick();
        }

        static juce::String licText()
        {
            return TXT ("MEON은 SonoBus 소스 코드를 수정해 만든 프로그램으로, GPL v3에 따라 같은 라이선스로 배포되고 수정한 소스 코드를 공개해요. 이 프로그램은 어떠한 보증도 제공하지 않아요.");
        }

        void tick() override
        {
            const juce::String ver = "MEON " + juce::String (MEON_BUILD_VERSION) + " (build " + juce::String (core.buildNumber()) + ")";
            juce::String desc = ver + (core.isPlugin() ? TXT (" · 플러그인") : TXT (" · 독립 앱"));
            juce::String label;
            const int latest = juce::jmax (update.latestBuild ? update.latestBuild() : 0, core.store().state().config.latestBuild);
            if (core.isPlugin())
            {
                // Q18: 플러그인은 스스로 업데이트하지 못한다
                if (latest > core.buildNumber() && core.buildNumber() > 0)
                    desc = ver + TXT (" · 새 버전(build ") + juce::String (latest) + TXT (")이 있어요 · MEON 앱에서 업데이트해 주세요");
            }
            else if (update.state)
            {
                using S = SettingsModal::UpdateHooks::State;
                const auto st = update.state();
                const bool inRoom = core.store().state().inRoom();
                switch (st)
                {
                    case S::checking:    desc = ver + TXT (" · 새 버전을 확인하고 있어요"); label = TXT ("확인 중…"); break;
                    case S::upToDate:    desc = ver + TXT (" · 최신 버전이에요"); label = TXT ("업데이트 확인"); break;
                    case S::available:   desc = ver + TXT (" · 새 버전(build ") + juce::String (latest) + TXT (")이 있어요. ")
                                                + (inRoom ? TXT ("합주가 끝나면 업데이트할 수 있어요") : TXT ("업데이트를 누르기 전에는 설치하지 않아요"));
                                         label = TXT ("업데이트"); break;
                    case S::downloading:
                    {
                        const float p = update.progress ? update.progress() : -1.0f;
                        desc = TXT ("build ") + juce::String (latest) + TXT (" 내려받는 중") + (p >= 0.0f ? TXT (" · ") + juce::String ((int) (p * 100.0f)) + "%" : juce::String());
                        label = TXT ("업데이트 중…");
                        break;
                    }
                    case S::installing:  desc = TXT ("설치를 마쳤어요 · 다시 시작하는 중…"); label = TXT ("업데이트 중…"); break;
                    case S::failed:      desc = ver + TXT (" · 새 버전을 받지 못했어요"); label = TXT ("다시 시도"); break;
                    case S::idle:        label = TXT ("업데이트 확인"); break;
                }
                updateBtn.setButtonText (label);
                updateBtn.setDimmed (st == S::available && inRoom);
                verRow.controlW = updateBtn.getIdealWidth();
            }
            if (verRow.desc != desc) { verRow.desc = desc; relayout(); }
            verRow.resized();
            verRow.repaint();
        }

        struct Box : juce::Component
        {
            std::function<void (juce::Graphics&)> paintFn;
            std::function<void()> layoutFn;
            bool framed = false;
            void paint (juce::Graphics& g) override
            {
                auto r = getLocalBounds().toFloat();
                g.setColour (col::softBg);
                g.fillRoundedRectangle (r, metric::radius);
                g.setColour (col::line);
                g.drawRoundedRectangle (r.reduced (0.5f), metric::radius, 1.0f);
                if (paintFn) paintFn (g);
            }
            void resized() override { if (layoutFn) layoutFn(); }
        };

        core::MeonCore& core;
        SettingsModal::UpdateHooks& update;
        SettingsModal::Row verRow { TXT ("버전") },
                           logRow { TXT ("로그 폴더"), TXT ("문제가 생겼을 때 문의에 로그 파일을 함께 보내주세요") },
                           licRow { TXT ("오픈소스 라이선스"), TXT ("MEON은 SonoBus를 기반으로 수정한 프로그램이에요") },
                           askRow { TXT ("문의"), TXT ("디스코드 #문의하기 채널로 연결돼요") };
        Button updateBtn { Button::Kind::small, TXT ("업데이트 확인") }, openLogs { Button::Kind::small, TXT ("폴더 열기") },
               licToggle { Button::Kind::small, TXT ("보기") }, discord { Button::Kind::small, TXT ("디스코드 열기") };
        Box licBox;
        Button licFull { Button::Kind::linkSmall, TXT ("라이선스 전문") }, source { Button::Kind::linkSmall, TXT ("MEON 소스 코드") },
               sono { Button::Kind::linkSmall, TXT ("SonoBus 원본") };
    };
}

//==============================================================================
SettingsModal::SettingsModal (core::MeonCore& c, UpdateHooks u, Tab first)
    : core (c), update (std::move (u)), plugin (c.isPlugin())
{
    setWantsKeyboardFocus (true);
    setFocusContainerType (FocusContainerType::keyboardFocusContainer);
    juce::Component::setTitle (TXT ("설정"));
    tabs = { Tab::account, Tab::profile, Tab::audio, Tab::jam, Tab::privacy, Tab::notif, Tab::keys, Tab::about };
    if (plugin) tabs.erase (std::remove (tabs.begin(), tabs.end(), Tab::audio), tabs.end());
    addAndMakeVisible (close);
    close.setTooltip (TXT ("닫기"));
    close.setInk (col::inkFaint, col::ink);
    close.onClick = [this] { if (onClose) onClose(); };
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);
    showTab (std::find (tabs.begin(), tabs.end(), first) != tabs.end() ? first : Tab::account);
    startTimer (250);
}

SettingsModal::~SettingsModal()
{
    stopTimer();
    viewport.setViewedComponent (nullptr, false);
    page.reset();
}

juce::String SettingsModal::tabTitle (Tab t)
{
    switch (t)
    {
        case Tab::account: return TXT ("계정");
        case Tab::profile: return TXT ("프로필");
        case Tab::audio:   return TXT ("오디오");
        case Tab::jam:     return TXT ("합주");
        case Tab::privacy: return TXT ("친구 · 개인정보");
        case Tab::notif:   return TXT ("알림");
        case Tab::keys:    return TXT ("단축키");
        case Tab::about:   return TXT ("정보");
    }
    return {};
}

void SettingsModal::showTab (Tab t)
{
    tab = t;
    viewport.setViewedComponent (nullptr, false);
    switch (t)
    {
        case Tab::account: page = std::make_unique<AccountPage> (*this, core); break;
        case Tab::profile: page = std::make_unique<ProfilePage> (*this, core); break;
        case Tab::audio:   page = std::make_unique<AudioPage> (*this, core); break;
        case Tab::jam:     page = std::make_unique<JamPage> (*this, core); break;
        case Tab::privacy: page = std::make_unique<PrivacyPage> (*this, core); break;
        case Tab::notif:   page = std::make_unique<NotifPage> (*this); break;
        case Tab::keys:    page = std::make_unique<KeysPage> (*this); break;
        case Tab::about:   page = std::make_unique<AboutPage> (*this, core, update); break;
    }
    viewport.setViewedComponent (page.get(), false);
    resized();
    repaint();
}

juce::Rectangle<int> SettingsModal::box() const
{
    auto area = getLocalBounds().reduced (metric::padX);
    return juce::Rectangle<int> (juce::jmin (boxW, area.getWidth()), juce::jmin (boxH, area.getHeight())).withCentre (area.getCentre());
}

juce::Rectangle<int> SettingsModal::tabBounds (int i) const
{
    const auto b = box();
    return { b.getX() + 9, b.getY() + 12 + 32 + 6 + i * 34, tabsW - 18, 32 };
}

void SettingsModal::paint (juce::Graphics& g)
{
    g.setColour (col::overlay);
    g.fillRect (getLocalBounds());
    const auto b = box().toFloat();
    g.setColour (col::white);
    g.fillRoundedRectangle (b, metric::radius);
    // 왼쪽 탭
    {
        juce::Path left;
        left.addRoundedRectangle (b.getX(), b.getY(), (float) tabsW, b.getHeight(), metric::radius, metric::radius, true, false, true, false);
        g.setColour (col::softBg);
        g.fillPath (left);
        g.setColour (col::line);
        g.fillRect (b.getX() + (float) tabsW - 1.0f, b.getY(), 1.0f, b.getHeight());
        g.setColour (col::ink);
        g.setFont (font (600, 16.0f));
        g.drawText (TXT ("설정"), (int) b.getX() + 18, (int) b.getY() + 12, tabsW - 36, 32, juce::Justification::centredLeft, false);
        for (int i = 0; i < (int) tabs.size(); ++i)
        {
            const auto r = tabBounds (i).toFloat();
            const bool on = tabs[(size_t) i] == tab;
            if (on || hoverTab == i)
            {
                g.setColour (col::tabOn);
                g.fillRoundedRectangle (r, metric::radius);
            }
            g.setColour (on || hoverTab == i ? col::ink : col::inkSub);
            g.setFont (font (on ? 600 : 500, 13.0f));
            g.drawText (tabTitle (tabs[(size_t) i]), r.reduced (9.0f, 0.0f), juce::Justification::centredLeft, false);
        }
    }
    // 오른쪽 머리말
    const auto right = b.withTrimmedLeft ((float) tabsW);
    g.setColour (col::ink);
    g.setFont (font (600, 16.0f));
    g.drawText (tabTitle (tab), right.withHeight (57.0f).reduced (18.0f, 0.0f), juce::Justification::centredLeft, false);
    g.setColour (col::line);
    g.fillRect (right.getX(), right.getY() + 56.0f, right.getWidth(), 1.0f);
}

void SettingsModal::resized()
{
    const auto b = box();
    const auto right = b.withTrimmedLeft (tabsW);
    close.setBounds (right.getRight() - 18 - 24, right.getY() + 16, 24, 24);
    viewport.setBounds (right.withTrimmedTop (57).reduced (18, 0).withTrimmedBottom (1));
    layoutPage();
}

void SettingsModal::layoutPage()
{
    if (page == nullptr) return;
    const int w = viewport.getWidth() - 10;
    const int h = page->layoutFor (w);
    page->setSize (w, juce::jmax (h, viewport.getHeight()));
    for (auto* r : page->rows) r->resized();
    page->repaint();
}

void SettingsModal::mouseUp (const juce::MouseEvent& e)
{
    if (! box().contains (e.getPosition())) { if (onClose) onClose(); return; }
    for (int i = 0; i < (int) tabs.size(); ++i)
        if (tabBounds (i).contains (e.getPosition()) && tabs[(size_t) i] != tab) { showTab (tabs[(size_t) i]); return; }
}

void SettingsModal::mouseMove (const juce::MouseEvent& e)
{
    int h = -1;
    for (int i = 0; i < (int) tabs.size(); ++i)
        if (tabBounds (i).contains (e.getPosition())) h = i;
    if (h != hoverTab) { hoverTab = h; repaint(); }
    setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void SettingsModal::mouseExit (const juce::MouseEvent&)
{
    if (hoverTab >= 0) { hoverTab = -1; repaint(); }
}

bool SettingsModal::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
    return false;
}

void SettingsModal::timerCallback()
{
    if (page != nullptr) page->tick();
}

} // namespace meon::ui
