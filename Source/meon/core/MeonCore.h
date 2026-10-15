// MEON core - 프로세서(독립 앱 1개, 플러그인 인스턴스 1개)당 하나. 서버 연결·상태·로그인을 묶어 소유한다.
// 에디터(창)를 닫아도 살아 있어야 하므로 SonobusAudioProcessor 가 소유한다 (엔진 수정 E2).
// 서버: Options.server (net/ServerConfig). 진짜 서버는 HttpTransport + WsTransport, 가짜는 FakeMeonServer.
#pragma once

#include "api/MeonApi.h"
#include "api/Realtime.h"
#include "api/fake/FakeMeonServer.h"
#include "api/net/ServerConfig.h"
#include "state/AppStore.h"
#include "auth/AuthController.h"
#include "auth/TokenStore.h"
#include "auth/RefreshLock.h"
#include "util/Scheduler.h"
#include "jam/JamSession.h"
#include "jam/RttReporter.h"
#include "home/HomeController.h"
#include "room/RoomController.h"
#include "account/AccountController.h"

namespace meon::core
{

class MeonCore : private AuthController::Listener,
                 private AppStore::Listener
{
public:
    struct Options
    {
        bool plugin = false;
        int build = 0;
        ServerConfig server = ServerConfig::fake();   // 앱은 ServerConfig::choose 로 고른다. 기본값(가짜)은 단위 테스트용
        juce::File dataFolder;                  // 가짜 서버 상태 파일을 둘 곳 (설정 폴더)
        FakeMeonServer::Options fake;
        bool resetFakeState = false;            // 개발용: 가짜 계정을 지우고 시작
        bool fakeAooGiven = false;              // --fake-aoo 를 줬으면 가짜 서버의 AOO 주소를 그대로 둔다
        juce::File logFile;                     // 진단 로그 (logs/app.log). 비우면 쓰지 않는다 (단위 테스트)
    };

    explicit MeonCore (Options);
    ~MeonCore();

    /** 개발용 실행 옵션 (--server=prod|local|fake|<주소> --fake-latency= --fake-speed= --fake-script=0 --fake-aoo=host:port --fake-reset) */
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
    HomeController& home()              { return *homeController; }
    RoomController& room()              { return *roomController; }
    AccountController& account()        { return *accountController; }
    FakeMeonServer* fakeServer()        { return fake.get(); }   // 가짜 서버일 때만
    const ServerConfig& server() const  { return options.server; }

    /** 프로세서가 엔진을 붙인다 (합주). 붙인 뒤에야 jam() 이 생긴다 */
    void attachEngine (std::unique_ptr<Engine>, JamSession::Options);
    JamSession* jam()                   { return jamSession.get(); }
    Engine* engine()                    { return engineImpl.get(); }

private:
    Options options;
    RealScheduler sched;
    std::unique_ptr<FakeMeonServer> fake;
    std::unique_ptr<RestTransport> restTransport;           // 진짜 서버일 때
    std::unique_ptr<RealtimeTransport> realtimeTransport;
    std::unique_ptr<MeonApi> meonApi;
    std::unique_ptr<Realtime> rt;
    AppStore appStore;
    std::unique_ptr<TokenStore> tokenStore;
    std::unique_ptr<RefreshLock> refreshLock;
    std::unique_ptr<AuthController> authController;
    std::unique_ptr<HomeController> homeController;
    std::unique_ptr<RoomController> roomController;
    std::unique_ptr<AccountController> accountController;
    bool started = false;
    juce::File fakeStateFile;
    bool fakeStateWriteFailed = false;
    std::unique_ptr<Engine> engineImpl;
    std::unique_ptr<JamSession> jamSession;
    std::unique_ptr<RttReporter> rtt;

    void authStepChanged (AuthController::Step) override;
    void stateChanged (StateKey) override;   // 방에 들어가고 나갈 때 RTT 측정 대상을 바꾼다
    RttReporter::Target defaultRttTarget() const;
    void createFake();

    JUCE_DECLARE_NON_COPYABLE (MeonCore)
};

} // namespace meon::core
