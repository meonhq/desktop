// MEON core - 로그인·가입·이메일 인증·온보딩 단계 (API 3.4). 화면은 step() 을 보고 해당 단계를 그린다.
//   앱 시작: 설정(config) → 저장된 refresh 토큰이 있으면 refresh → me → 빠진 단계부터, 다 있으면 바로 홈(ready)
//   로그인 뒤에도 같은 판정. 단, 다 있으면 "다시 오셨네요"(done) 화면을 한 번 보여 준다 (Q5)
//   가입: 인증 → 닉네임 → 파트 → 디스코드 → 완료 → 홈
// 인증을 마친 계정이면 실시간 연결을 시작한다.
#pragma once

#include "../api/MeonApi.h"
#include "../api/Realtime.h"
#include "../state/AppStore.h"
#include "../util/Scheduler.h"
#include "TokenStore.h"

namespace meon::core
{

class AuthController : private Realtime::Listener
{
public:
    enum class Step { loading, login, signup, verify, nickname, part, discord, done, ready };

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void authStepChanged (Step) {}
    };

    AuthController (MeonApi&, Realtime&, AppStore&, TokenStore&, Scheduler&);
    ~AuthController() override;

    void addListener (Listener* l)    { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    /** 앱을 켤 때 한 번 */
    void start();
    Step step() const { return current; }
    bool cameFromLogin() const { return fromLogin; }
    /** 서버(또는 가짜 서버)에 연결하지 못해 시작 단계에서 멈춤. retry() 로 다시 */
    bool isOffline() const { return offline; }
    void retry() { start(); }

    // 단계 이동 (화면 버튼)
    void showLogin();
    void showSignup();

    void login (const juce::String& email, const juce::String& password, std::function<void (Result<Done>)>);
    void checkUsername (const juce::String& username, std::function<void (Result<bool>)>);
    void signup (const juce::String& email, const juce::String& username, const juce::String& password, bool agreed,
                 std::function<void (Result<Done>)>);
    void verify (const juce::String& code, std::function<void (Result<Done>)>);
    void resendCode (std::function<void (Result<Done>)>);
    /** 인증 전 이메일 바꾸기 (API 4.4: 비밀번호 없이 바로 바뀌고 코드가 새 주소로) */
    void changeUnverifiedEmail (const juce::String& email, std::function<void (Result<Done>)>);
    juce::int64 lastCodeSentAt() const { return codeSentAt; }

    void setNickname (const juce::String& nickname, std::function<void (Result<Done>)>);
    void setPart (const juce::String& part, std::function<void (Result<Done>)>);
    void backToNickname();
    void finishDiscord();
    void finishDone();

    void logout();
    /** refresh 실패 등으로 세션이 끊겼을 때 (MeonApi::onSessionLost). why 는 진단 로그용 */
    void sessionLost (const juce::String& why);

    static constexpr int resendGapMs = 30000;

private:
    MeonApi& api;
    Realtime& realtime;
    AppStore& store;
    TokenStore& tokens;
    Scheduler& scheduler;
    juce::ListenerList<Listener> listeners;
    Step current = Step::loading;
    bool fromLogin = false, offline = false, signedUp = false;
    juce::int64 codeSentAt = 0;
    std::shared_ptr<bool> alive { std::make_shared<bool> (true) };

    void go (Step);
    void afterMe (const Me&, bool afterLogin);
    void connectRealtime();
    void authRejected (const juce::String& code) override;
};

} // namespace meon::core
