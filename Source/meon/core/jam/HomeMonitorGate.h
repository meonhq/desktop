// MEON core - 홈 모니터링("입장 전에도 내 소리 듣기")을 홈 화면에서만 켠다.
//   홈 = 로그인을 마치고 홈 화면이 보이는 단계(AuthController::Step::ready). 홈 위의 모달(설정·방 만들기 등)도 홈이다.
//   로그인 확인 중(loading)·로그인·가입·이메일 인증·온보딩(닉네임·파트·디스코드 안내·완료)·강제 업데이트(outdated)·
//   "다른 곳에서 접속했어요"(replaced) 화면에서는 설정값과 상관없이 듣지 않는다. 저장된 설정값은 건드리지 않는다.
//   합주 화면(방 안)의 셀프 모니터는 JamSession 이 방 설정대로 따로 정한다.
// MeonCore 가 소유한다 (독립 앱·플러그인 같은 규칙).
#pragma once

#include "JamSession.h"
#include "../auth/AuthController.h"

namespace meon::core
{

class HomeMonitorGate : private AuthController::Listener
{
public:
    HomeMonitorGate (AuthController& a, JamSession& j) : auth (a), jam (j)
    {
        auth.addListener (this);
        authStepChanged (auth.step());
    }
    ~HomeMonitorGate() override { auth.removeListener (this); }

    static bool isHomeScreen (AuthController::Step s) { return s == AuthController::Step::ready; }

private:
    AuthController& auth;
    JamSession& jam;

    // AuthController 가 단계를 바꾸는 즉시(같은 호출 안에서) 부른다: 홈에서 로그아웃하면 그 자리에서 꺼진다.
    // 넘어온 단계 대신 지금 단계를 본다: 다른 듣는 쪽이 알림 안에서 단계를 또 바꾸면(완료 → 홈) 옛 알림이 뒤늦게 오기 때문
    void authStepChanged (AuthController::Step) override { jam.setHomeVisible (isHomeScreen (auth.step())); }

    JUCE_DECLARE_NON_COPYABLE (HomeMonitorGate)
};

} // namespace meon::core
