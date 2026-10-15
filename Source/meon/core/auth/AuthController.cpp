#include "AuthController.h"
#include "../log/AppLog.h"

namespace meon::core
{

AuthController::AuthController (MeonApi& a, Realtime& r, AppStore& s, TokenStore& t, Scheduler& sc)
    : api (a), realtime (r), store (s), tokens (t), scheduler (sc)
{
    realtime.addListener (this);
    store.addListener (this);
    api.onClientOutdated = [this] { markOutdated (LOGTXT ("서버가 426 을 돌려줌")); };
    api.onTokensChanged = [this] (const Tokens& tk)
    {
        if (tk.refreshToken.isEmpty())
        {
            tokens.clear();
            AppLog::write ("auth", LOGTXT ("저장된 로그인 토큰을 지움"));
        }
        else if (tokens.save (tk.refreshToken))
            AppLog::write ("auth", LOGTXT ("로그인 토큰 저장함"));
        else
            AppLog::write ("auth", LOGTXT ("로그인 토큰 저장 실패: ") + tokens.lastError() + LOGTXT (" → 다음 실행 때 로그아웃된 상태로 시작합니다"));
    };
    api.onSessionLost = [this] { sessionLost (LOGTXT ("요청 중 토큰 갱신 실패")); };
}

AuthController::~AuthController()
{
    *alive = false;
    realtime.removeListener (this);
    store.removeListener (this);
    api.onClientOutdated = nullptr;
    api.onTokensChanged = nullptr;
    api.onSessionLost = nullptr;
}

static juce::String stepName (AuthController::Step s)
{
    using S = AuthController::Step;
    switch (s)
    {
        case S::loading:  return LOGTXT ("불러오는 중");
        case S::login:    return LOGTXT ("로그인");
        case S::signup:   return LOGTXT ("가입");
        case S::verify:   return LOGTXT ("이메일 인증");
        case S::nickname: return LOGTXT ("닉네임");
        case S::part:     return LOGTXT ("파트");
        case S::discord:  return LOGTXT ("디스코드 안내");
        case S::done:     return LOGTXT ("완료");
        case S::ready:    return LOGTXT ("홈");
        case S::outdated: return LOGTXT ("강제 업데이트");
        case S::replaced: return LOGTXT ("다른 곳에서 접속");
    }
    return LOGTXT ("?");
}

void AuthController::go (Step s)
{
    if (current == s) return;
    if (current == Step::outdated) return;   // 강제 업데이트 화면에서는 업데이트 말고는 나갈 곳이 없다
    AppLog::write ("auth", juce::String (LOGTXT ("화면 단계: ")) + stepName (current) + LOGTXT (" → ") + stepName (s));
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
            AppLog::write ("auth", LOGTXT ("시작: 설정 받기 실패 (") + errorKey (c.code()) + LOGTXT (") → 오프라인 표시"));
            offline = true;
            listeners.call ([this] (Listener& l) { l.authStepChanged (current); });
            return;
        }
        store.setConfig (c.get());
        if (outdatedPending) return;   // 설정의 min_build 가 내 빌드보다 높다 (setConfig → checkMinBuild). 로그인은 그대로 둔다

        const auto saved = tokens.load();
        if (saved.isEmpty())
        {
            const auto err = tokens.lastError();
            AppLog::write ("auth", LOGTXT ("시작: 저장된 로그인 토큰 없음") + (err.isNotEmpty() ? LOGTXT (" (읽기 오류: ") + err + ")" : juce::String()) + LOGTXT (" → 로그인 화면"));
            go (Step::login);
            return;
        }

        AppLog::write ("auth", LOGTXT ("시작: 저장된 로그인 토큰 있음"));
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
                if (t.code() == ErrorCode::clientOutdated) return;   // 강제 업데이트 화면 (onClientOutdated). 로그인은 남긴다
                AppLog::write ("auth", LOGTXT ("시작: 저장된 로그인으로 토큰을 받지 못함 (") + errorKey (t.code()) + LOGTXT (") → 로그인 화면"));
                api.setTokens ({});
                go (Step::login);
                return;
            }
            api.getMe ([this, weak] (Result<Me> m)
            {
                if (weak.expired()) return;
                if (! m.ok())
                {
                    if (m.code() == ErrorCode::clientOutdated) return;
                    AppLog::write ("auth", LOGTXT ("시작: 내 정보 받기 실패 (") + errorKey (m.code()) + LOGTXT (") → 로그인 화면"));
                    go (Step::login);
                    return;
                }
                afterMe (m.get(), false);
            });
        }, LOGTXT ("앱 시작"));
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
    const auto link = realtime.getLink();
    if (link != Realtime::Link::offline && link != Realtime::Link::replaced) return;
    realtime.connect ([this] { return api.getTokens().accessToken; });
}

void AuthController::reconnectHere()
{
    if (current != Step::replaced) return;
    AppLog::write ("auth", LOGTXT ("여기서 다시 연결 (다른 곳의 연결은 끊긴다)"));
    connectRealtime();
    go (Step::ready);
}

void AuthController::markOutdated (const juce::String& why)
{
    if (! outdatedPending)
        AppLog::write ("auth", LOGTXT ("앱 버전이 낮음: ") + why + (store.state().inRoom() ? LOGTXT (" → 방을 나가면 강제 업데이트 화면") : LOGTXT (" → 강제 업데이트 화면")));
    outdatedPending = true;
    // 합주 중이면 방을 나갈 때까지 기다린다 (API 13장). 소리는 서버와 따로 오가므로 합주는 이어진다
    if (! store.state().inRoom())
        go (Step::outdated);
}

void AuthController::checkMinBuild()
{
    // 빌드 0(로컬 빌드)은 서버가 ALLOW_DEV_BUILD 로 정하므로 여기서 판정하지 않는다 (막히면 426 으로 안다)
    const int build = api.buildNumber();
    const auto& cfg = store.state().config;
    if (build > 0 && build < cfg.minBuild)
        markOutdated (LOGTXT ("최소 빌드 ") + juce::String (cfg.minBuild) + LOGTXT (", 내 빌드 ") + juce::String (build));
}

void AuthController::stateChanged (StateKey k)
{
    if (k == StateKey::config) checkMinBuild();
    else if (k == StateKey::currentRoom && outdatedPending && ! store.state().inRoom()) go (Step::outdated);
}

void AuthController::linkChanged (Realtime::Link l)
{
    if (l != Realtime::Link::replaced) return;
    if (current == Step::outdated) return;
    go (Step::replaced);
}

void AuthController::sessionRevoked()
{
    sessionLost (LOGTXT ("서버가 세션을 폐기함 (닫는 코드 4001)"));
}

void AuthController::authRejected (const juce::String& code)
{
    // access 토큰이 만료돼 거절됐으면 새로 받아 다시 붙는다. 그 밖(인증 전·버전)은 그대로 둔다
    AppLog::write ("auth", LOGTXT ("실시간 연결이 거절됨: ") + code);
    if (code == "client_outdated") { markOutdated (LOGTXT ("실시간 연결이 client_outdated 로 거절됨")); return; }
    if (code == "email_not_verified") { go (Step::verify); return; }
    if (code != "unauthorized") return;
    std::weak_ptr<bool> weak = alive;
    api.refresh ([this, weak] (Result<Tokens> t)
    {
        if (weak.expired()) return;
        if (t.ok()) realtime.connect ([this] { return api.getTokens().accessToken; });
        else if (t.code() != ErrorCode::network && t.code() != ErrorCode::timeout)
            sessionLost (LOGTXT ("실시간 연결 거절 뒤 토큰 갱신 실패 (") + errorKey (t.code()) + ")");
    }, LOGTXT ("실시간 연결 거절"));
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
    AppLog::write ("auth", LOGTXT ("로그아웃 (사용자가 누름)"));
    api.logout ([] (Result<Done>) {});
    realtime.disconnect();
    store.clear();
    go (Step::login);
}

void AuthController::sessionLost (const juce::String& why)
{
    // 같은 갱신 실패를 여러 쪽(요청 재시도, 실시간 연결)이 같이 받으면 한 번만 처리한다
    if (current == Step::login) return;
    AppLog::write ("auth", LOGTXT ("세션 끊김: ") + why + LOGTXT (" → 로그인 화면"));
    if (api.hasSession()) api.setTokens ({});   // 서버가 폐기한 세션(4001)의 토큰은 다시 쓸 수 없다
    realtime.disconnect();
    store.clear();
    go (Step::login);
}

} // namespace meon::core
