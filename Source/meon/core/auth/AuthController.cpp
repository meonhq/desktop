#include "AuthController.h"

namespace meon::core
{

AuthController::AuthController (MeonApi& a, Realtime& r, AppStore& s, TokenStore& t, Scheduler& sc)
    : api (a), realtime (r), store (s), tokens (t), scheduler (sc)
{
    realtime.addListener (this);
    api.onTokensChanged = [this] (const Tokens& tk)
    {
        if (tk.refreshToken.isEmpty()) tokens.clear();
        else tokens.save (tk.refreshToken);
    };
    api.onSessionLost = [this] { sessionLost(); };
}

AuthController::~AuthController()
{
    *alive = false;
    realtime.removeListener (this);
    api.onTokensChanged = nullptr;
    api.onSessionLost = nullptr;
}

void AuthController::go (Step s)
{
    if (current == s) return;
    current = s;
    listeners.call ([s] (Listener& l) { l.authStepChanged (s); });
}

void AuthController::start()
{
    offline = false;
    go (Step::loading);
    std::weak_ptr<bool> weak = alive;
    api.getConfig ([this, weak] (Result<Config> c)
    {
        if (weak.expired()) return;
        if (! c.ok())
        {
            offline = true;
            listeners.call ([this] (Listener& l) { l.authStepChanged (current); });
            return;
        }
        store.setConfig (c.get());

        const auto saved = tokens.load();
        if (saved.isEmpty()) { go (Step::login); return; }

        api.setTokens ({ {}, saved });
        api.refresh ([this, weak] (Result<Tokens> t)
        {
            if (weak.expired()) return;
            if (! t.ok())
            {
                if (t.code() == ErrorCode::network || t.code() == ErrorCode::timeout)
                {
                    offline = true;
                    listeners.call ([this] (Listener& l) { l.authStepChanged (current); });
                    return;
                }
                api.setTokens ({});
                go (Step::login);
                return;
            }
            api.getMe ([this, weak] (Result<Me> m)
            {
                if (weak.expired()) return;
                if (! m.ok()) { go (Step::login); return; }
                afterMe (m.get(), false);
            });
        });
    });
}

void AuthController::afterMe (const Me& me, bool afterLogin)
{
    store.setMe (me);
    fromLogin = afterLogin;
    if (me.emailVerified) connectRealtime();

    if (! me.emailVerified)         go (Step::verify);
    else if (! me.nickname)         go (Step::nickname);
    else if (! me.part)             go (Step::part);
    else if (afterLogin)            go (Step::done);
    else                            go (Step::ready);
}

void AuthController::connectRealtime()
{
    if (realtime.getLink() != Realtime::Link::offline) return;
    realtime.connect ([this] { return api.getTokens().accessToken; });
}

void AuthController::authRejected (const juce::String& code)
{
    // access 토큰이 만료돼 거절됐으면 새로 받아 다시 붙는다. 그 밖(인증 전·버전)은 그대로 둔다
    if (code != "unauthorized") return;
    std::weak_ptr<bool> weak = alive;
    api.refresh ([this, weak] (Result<Tokens> t)
    {
        if (weak.expired()) return;
        if (t.ok()) realtime.connect ([this] { return api.getTokens().accessToken; });
        else if (t.code() != ErrorCode::network && t.code() != ErrorCode::timeout) sessionLost();
    });
}

void AuthController::showLogin()  { go (Step::login); }
void AuthController::showSignup() { go (Step::signup); }

void AuthController::login (const juce::String& email, const juce::String& pw, std::function<void (Result<Done>)> cb)
{
    std::weak_ptr<bool> weak = alive;
    api.login (email.trim(), pw, [this, weak, cb] (Result<AuthResult> r)
    {
        if (weak.expired()) return;
        if (! r.ok()) { cb (r.err()); return; }
        signedUp = false;
        cb (Done {});
        afterMe (r.get().me, true);
    });
}

void AuthController::checkUsername (const juce::String& u, std::function<void (Result<bool>)> cb)
{
    api.usernameAvailable (u, cb);
}

void AuthController::signup (const juce::String& email, const juce::String& username, const juce::String& pw, bool agreed,
                             std::function<void (Result<Done>)> cb)
{
    std::weak_ptr<bool> weak = alive;
    api.signup (email.trim(), username, pw, agreed, [this, weak, cb] (Result<AuthResult> r)
    {
        if (weak.expired()) return;
        if (! r.ok()) { cb (r.err()); return; }
        signedUp = true;
        codeSentAt = scheduler.nowMs();
        cb (Done {});
        afterMe (r.get().me, false);
    });
}

void AuthController::verify (const juce::String& code, std::function<void (Result<Done>)> cb)
{
    std::weak_ptr<bool> weak = alive;
    api.verifyEmail (code, [this, weak, cb] (Result<Me> r)
    {
        if (weak.expired()) return;
        if (! r.ok()) { cb (r.err()); return; }
        cb (Done {});
        afterMe (r.get(), fromLogin);
    });
}

void AuthController::resendCode (std::function<void (Result<Done>)> cb)
{
    std::weak_ptr<bool> weak = alive;
    api.resendVerification ([this, weak, cb] (Result<Done> r)
    {
        if (weak.expired()) return;
        if (r.ok()) codeSentAt = scheduler.nowMs();
        cb (r);
    });
}

void AuthController::changeUnverifiedEmail (const juce::String& email, std::function<void (Result<Done>)> cb)
{
    std::weak_ptr<bool> weak = alive;
    api.requestEmailChange (email.trim(), {}, [this, weak, cb] (Result<Done> r)
    {
        if (weak.expired()) return;
        if (! r.ok()) { cb (r); return; }
        codeSentAt = scheduler.nowMs();
        api.getMe ([this, weak, cb] (Result<Me> m)
        {
            if (weak.expired()) return;
            if (m.ok()) store.setMe (m.get());
            cb (Done {});
        });
    });
}

void AuthController::setNickname (const juce::String& nickname, std::function<void (Result<Done>)> cb)
{
    MePatch p;
    p.nickname = nickname;
    std::weak_ptr<bool> weak = alive;
    api.patchMe (p, [this, weak, cb] (Result<Me> r)
    {
        if (weak.expired()) return;
        if (! r.ok()) { cb (r.err()); return; }
        store.setMe (r.get());
        cb (Done {});
        go (Step::part);
    });
}

void AuthController::setPart (const juce::String& part, std::function<void (Result<Done>)> cb)
{
    MePatch p;
    p.part = part;
    std::weak_ptr<bool> weak = alive;
    api.patchMe (p, [this, weak, cb] (Result<Me> r)
    {
        if (weak.expired()) return;
        if (! r.ok()) { cb (r.err()); return; }
        store.setMe (r.get());
        cb (Done {});
        // 가입 흐름이면 디스코드 안내, 로그인 뒤 빠진 단계를 채운 것이면 완료
        go (fromLogin ? Step::done : Step::discord);
    });
}

void AuthController::backToNickname() { go (Step::nickname); }
void AuthController::finishDiscord()  { go (Step::done); }
void AuthController::finishDone()     { go (Step::ready); }

void AuthController::logout()
{
    api.logout ([] (Result<Done>) {});
    realtime.disconnect();
    store.clear();
    go (Step::login);
}

void AuthController::sessionLost()
{
    realtime.disconnect();
    store.clear();
    go (Step::login);
}

} // namespace meon::core
