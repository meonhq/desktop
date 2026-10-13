// MEON core - 프로세서(독립 앱 1개, 플러그인 인스턴스 1개)당 하나. 서버 연결·상태·로그인을 묶어 소유한다.
// 에디터(창)를 닫아도 살아 있어야 하므로 SonobusAudioProcessor 가 소유한다 (엔진 수정 E2).
// 가짜 기간: 서버는 FakeMeonServer. 진짜 서버 단계(R11)에서 transport 만 바꾼다.
#pragma once

#include "api/MeonApi.h"
#include "api/Realtime.h"
#include "api/fake/FakeMeonServer.h"
#include "state/AppStore.h"
#include "auth/AuthController.h"
#include "auth/TokenStore.h"
#include "util/Scheduler.h"

namespace meon::core
{

class MeonCore
{
public:
    struct Options
    {
        bool plugin = false;
        int build = 0;
        juce::File dataFolder;                  // 가짜 서버 상태 파일을 둘 곳 (설정 폴더)
        FakeMeonServer::Options fake;
        bool resetFakeState = false;            // 개발용: 가짜 계정을 지우고 시작
    };

    explicit MeonCore (Options);
    ~MeonCore();

    /** 개발용 실행 옵션 (--fake-latency= --fake-speed= --fake-script=0 --fake-aoo=host:port --fake-reset) */
    static void applyDevOptions (Options&, const juce::StringPairArray&);

    /** 시작 (설정·로그인 복원). 메시지 스레드에서 한 번 */
    void start();
    bool isStarted() const { return started; }

    bool isPlugin() const               { return options.plugin; }
    int buildNumber() const             { return options.build; }
    Scheduler& scheduler()              { return sched; }
    MeonApi& api()                      { return *meonApi; }
    Realtime& realtime()                { return *rt; }
    AppStore& store()                   { return appStore; }
    AuthController& auth()              { return *authController; }
    FakeMeonServer* fakeServer()        { return fake.get(); }

private:
    Options options;
    RealScheduler sched;
    std::unique_ptr<FakeMeonServer> fake;
    std::unique_ptr<MeonApi> meonApi;
    std::unique_ptr<Realtime> rt;
    AppStore appStore;
    std::unique_ptr<TokenStore> tokenStore;
    std::unique_ptr<AuthController> authController;
    bool started = false;
    juce::File fakeStateFile;

    JUCE_DECLARE_NON_COPYABLE (MeonCore)
};

} // namespace meon::core
