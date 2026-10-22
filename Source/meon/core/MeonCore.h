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
#include "log/EventReporter.h"
#include "jam/JamSession.h"
#include "jam/RttReporter.h"
#include "jam/HomeMonitorGate.h"
#include "home/HomeController.h"
#include "room/RoomController.h"
#include "account/AccountController.h"
#include "state/ConnectionWatch.h"

namespace meon::core
{

class MeonCore : private AuthController::Listener,
                 private AppStore::Listener,
                 private JamSession::Listener
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
        /** 개발용: 소개 페이지 캡처의 장면 (--showcase=jam|rooms|friends). 비어 있으면 보통 가짜 데이터 (world.json) */
        juce::String showcaseScene;
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
    /** 연결 끊김 표시 (홈·합주 화면 위의 줄, "다시 연결됐어요" 토스트) */
    ConnectionWatch& connection()       { return connWatch; }
    const ServerConfig& server() const  { return options.server; }

    /** 프로세서가 엔진을 붙인다 (합주). 붙인 뒤에야 jam() 이 생긴다 */
    void attachEngine (std::unique_ptr<Engine>, JamSession::Options);
    JamSession* jam()                   { return jamSession.get(); }
    Engine* engine()                    { return engineImpl.get(); }

private:
    Options options;
    RealScheduler sched;
    ConnectionWatch connWatch { sched };
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
    std::unique_ptr<HomeMonitorGate> homeMonitorGate;   // 홈 모니터링은 로그인을 마친 홈 화면에서만
    std::unique_ptr<RttReporter> rtt;
    std::unique_ptr<EventReporter> events;   // 행동 기록 (POST /v1/events). 지금은 상대와 소리 연결 성공·실패만
    void sendEventsOnQuit();
    juce::File leftOnQuitFile;   // 정상 종료 때 나가기가 서버에 닿지 않은 방 (다음 실행이 그 방에 돌아가지 않게)

    static constexpr int quitLeaveTimeoutMs = 2500;           // 종료 때 나가기 응답을 기다리는 최대 시간
    static constexpr int quitEventsTimeoutMs = 1000;          // 종료 때 남은 행동 기록을 보내고 기다리는 최대 시간
    static constexpr juce::int64 leftOnQuitValidMs = 40000;   // 서버의 끊김 유예 30초(API 5.6) + 여유. 그 뒤엔 서버가 이미 퇴장시켰다
    void readLeftOnQuit();   // 켤 때: 표시가 있으면 다음 첫 hello 에서 그 방에 들어가지 않는다
    void leaveOnQuit();      // 소멸자 (정상 종료)

    void authStepChanged (AuthController::Step) override;
    void stateChanged (StateKey) override;   // 방에 들어가고 나갈 때 RTT 측정 대상을 바꾼다. 연결 끊김 표시
    void notice (const Notice&) override;
    void jamServerLinkChanged() override;
    void updateConnection();
    RttReporter::Target defaultRttTarget() const;
    void createFake();
    static juce::var showcaseWorld (const juce::String& scene);

    JUCE_DECLARE_NON_COPYABLE (MeonCore)
};

} // namespace meon::core
