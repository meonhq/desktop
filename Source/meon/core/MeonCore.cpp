#include "MeonCore.h"
#include "MeonFixtures.h"
#include "log/AppLog.h"
#include "api/ApiJson.h"
#include "api/net/HttpTransport.h"
#include "api/net/WsTransport.h"

#ifndef MEON_GIT_COMMIT
 #define MEON_GIT_COMMIT "unknown"
#endif

namespace meon::core
{

static void logEnvironment (const MeonCore::Options& o)
{
    using juce::SystemStats;
    AppLog::write ("app", juce::String ("Meon ") + MEON_BUILD_VERSION + LOGTXT (" (빌드 ") + juce::String (o.build) + LOGTXT (", 커밋 ") + MEON_GIT_COMMIT + "), "
                          + (o.plugin ? LOGTXT ("플러그인 (호스트 ") + juce::File::getSpecialLocation (juce::File::hostApplicationPath).getFileName() + ")" : juce::String (LOGTXT ("독립 앱"))));
    AppLog::write ("app", SystemStats::getOperatingSystemName() + ", CPU " + SystemStats::getCpuModel().trim()
                          + " (" + juce::String (SystemStats::getNumCpus()) + LOGTXT ("개), 메모리 ") + juce::String (SystemStats::getMemorySizeInMegabytes()) + " MB, "
                          + SystemStats::getUserLanguage() + "-" + SystemStats::getUserRegion());
    AppLog::write ("app", LOGTXT ("설정 폴더: ") + o.dataFolder.getFullPathName());
}

MeonCore::MeonCore (Options o) : options (std::move (o))
{
    if (options.logFile != juce::File())
    {
        AppLog::open (options.logFile);
        logEnvironment (options);
    }

    AppLog::write ("app", LOGTXT ("서버: ") + options.server.describe());

    RestTransport* rest = nullptr;
    RealtimeTransport* realtimeLink = nullptr;
    if (options.server.isFake())
    {
        if (! options.fakeAooGiven)
        {
            options.fake.aooHost = options.server.aooHost;
            options.fake.aooPort = options.server.aooPort;
        }
        createFake();
        rest = fake.get();
        realtimeLink = fake.get();
    }
    else
    {
        restTransport = std::make_unique<HttpTransport> (options.server.restBase);
        realtimeTransport = std::make_unique<WsTransport> (options.server.wsUrl);
        rest = restTransport.get();
        realtimeLink = realtimeTransport.get();
    }

    meonApi = std::make_unique<MeonApi> (*rest, options.build);
    rt = std::make_unique<Realtime> (*realtimeLink, sched, options.build);
    rt->addListener (&appStore);
    // 로그인 토큰은 서버마다 다른 항목에 둔다 (가짜·로컬·운영 토큰이 섞이지 않게)
    tokenStore = TokenStore::createDefault (options.server.tokenItem);
    if (options.resetFakeState && options.server.isFake()) tokenStore->clear();
    // 같은 컴퓨터의 다른 MEON(앱·플러그인)과 토큰 갱신을 나눠 쓴다 (가짜 서버는 프로세스마다 따로라 필요 없다)
    if (! options.server.isFake())
    {
        refreshLock = std::make_unique<RefreshLock> (options.server.tokenItem, *tokenStore, sched);
        meonApi->setRefreshCoordinator (refreshLock.get());
    }
    authController = std::make_unique<AuthController> (*meonApi, *rt, appStore, *tokenStore, sched);
    homeController = std::make_unique<HomeController> (*meonApi, *rt, appStore, sched);
    roomController = std::make_unique<RoomController> (*meonApi, appStore);
    accountController = std::make_unique<AccountController> (*meonApi, appStore, *authController);
    authController->addListener (this);
    appStore.addListener (this);
    readLeftOnQuit();
}

void MeonCore::readLeftOnQuit()
{
    if (options.dataFolder == juce::File()) return;   // 단위 테스트
    leftOnQuitFile = options.dataFolder.getChildFile ("MEONLeftOnQuit.json");
    appStore.onRoomLeftOnQuit = [this] (const juce::String& roomId)
    {
        AppLog::write ("room", LOGTXT ("지난번 종료 때 나간 방이 남아 있어 들어가지 않고 다시 나가기: ") + roomId);
        meonApi->leaveRoom (roomId, [] (Result<Done>) {});
    };
    if (! leftOnQuitFile.existsAsFile()) return;
    const auto v = juce::JSON::parse (leftOnQuitFile);
    leftOnQuitFile.deleteFile();   // 한 번만 쓴다 (같은 컴퓨터의 다른 MEON 이 먼저 켜졌으면 그쪽이 쓴다)
    const auto roomId = v.getProperty ("room", "").toString();
    const auto age = juce::Time::currentTimeMillis() - (juce::int64) v.getProperty ("at", 0);
    if (roomId.isEmpty() || v.getProperty ("server", "").toString() != options.server.tokenItem || age < 0 || age > leftOnQuitValidMs)
        return;
    AppLog::write ("room", LOGTXT ("지난번 정상 종료 때 나가기가 서버에 닿지 않은 방: ") + roomId + LOGTXT (" (다시 들어가지 않음)"));
    appStore.setRoomLeftOnQuit (roomId);
}

void MeonCore::leaveOnQuit()
{
    // 정상 종료(창 닫기, 메뉴 종료, 플러그인 제거·DAW 종료)에서만 여기를 지난다. 강제 종료·크래시는 지나지 않으므로
    // 30초 안에 다시 켜면 hello 로 같은 방에 돌아간다 (서버의 끊김 유예, API 5.6)
    if (homeController == nullptr) return;
    const auto roomId = appStore.state().currentRoom ? appStore.state().currentRoom->id : juce::String();
    using Q = HomeController::QuitLeave;
    const auto r = homeController->leaveOnQuit (quitLeaveTimeoutMs);
    if (r == Q::skipped)
        AppLog::write ("room", LOGTXT ("종료: 다른 곳에서 접속 중이라 방 나가기를 보내지 않음"));
    if (r != Q::failed || leftOnQuitFile == juce::File()) return;
    const bool ok = leftOnQuitFile.replaceWithText (juce::JSON::toString (json::obj ({
        { "server", options.server.tokenItem }, { "room", roomId }, { "at", juce::Time::currentTimeMillis() } })));
    AppLog::write ("room", LOGTXT ("종료: 방 나가기가 서버에 닿지 않음 → 다음 실행 때 이 방에 돌아가지 않게 표시") + (ok ? juce::String() : juce::String (LOGTXT (" (표시 저장 실패)"))));
}

void MeonCore::createFake()
{
    fake = std::make_unique<FakeMeonServer> (sched, options.fake);
    fake->loadWorld (juce::JSON::parse (juce::String::fromUTF8 (MeonFixtures::world_json, MeonFixtures::world_jsonSize)));

    // 가짜 계정(이 설치의 계정 ID·AOO 비밀번호·refresh 토큰)은 설정 폴더의 파일에 둔다 (빌드 0 의 기본·--server=fake 전용)
    fakeStateFile = options.dataFolder.getChildFile ("MEONFakeServer.json");
    if (options.resetFakeState)
    {
        fakeStateFile.deleteFile();
        AppLog::write ("fake", LOGTXT ("개발 옵션으로 가짜 서버 계정을 지우고 시작"));
    }
    if (fakeStateFile.existsAsFile())
    {
        const auto saved = juce::JSON::parse (fakeStateFile);
        fake->loadState (saved);
        AppLog::write ("fake", LOGTXT ("가짜 서버 상태 읽음: ") + juce::String (fakeStateFile.getSize()) + LOGTXT (" 바이트, 수정 ")
                               + fakeStateFile.getLastModificationTime().formatted ("%Y-%m-%d %H:%M:%S")
                               + (saved.isObject() ? juce::String() : juce::String (LOGTXT (" (내용을 읽지 못함: 계정 없이 시작)"))));
    }
    else
        AppLog::write ("fake", LOGTXT ("가짜 서버 상태 파일 없음 (처음 실행이거나 지워짐)"));

    fake->onStateChanged = [this] (const juce::var& state)
    {
        fakeStateFile.getParentDirectory().createDirectory();
        const bool ok = fakeStateFile.replaceWithText (juce::JSON::toString (state));
        if (! ok)
            AppLog::write ("fake", LOGTXT ("가짜 서버 상태 저장 실패: ") + fakeStateFile.getFullPathName() + LOGTXT (" → 다음 실행 때 로그인이 풀릴 수 있습니다"));
        else if (fakeStateWriteFailed)
            AppLog::write ("fake", LOGTXT ("가짜 서버 상태 저장이 다시 됨"));
        fakeStateWriteFailed = ! ok;
    };
}

void MeonCore::attachEngine (std::unique_ptr<Engine> e, JamSession::Options o)
{
    homeMonitorGate.reset();
    if (jamSession != nullptr) jamSession->removeListener (this);
    jamSession.reset();
    engineImpl = std::move (e);
    o.plugin = options.plugin;
    o.fake = fake.get();
    o.sendLatency = [this] (const LatencyProbe::Report& r)
    {
        meonApi->sendLatencyTelemetry (r.roomId, r.estimatedMs, r.myRttMs, r.hostRttMs, r.actualP2pRttMs, [] (Result<Done> res)
        {
            if (! res.ok()) AppLog::write ("api", LOGTXT ("지연 보정 자료 보내기 실패: ") + errorKey (res.code()));
        });
    };
    // 상대 소리가 모두 끊기면 내 인터넷을 의심한다: 서버 실시간 연결을 바로 확인하고(Realtime::checkNow),
    // 합주 서버에 TCP 로 닿는지 본다 (와이파이가 조용히 끊기면 엔진은 한참 동안 끊김을 모른다)
    o.peersAllInterrupted = [this] { rt->checkNow(); };
    o.checkReachable = [] (const juce::String& host, int port, std::function<void (bool)> done)
    {
        juce::Thread::launch ([host, port, done]
        {
            juce::StreamingSocket sock;
            const bool ok = sock.connect (host, port, 3000);
            sock.close();
            juce::MessageManager::callAsync ([done, ok] { done (ok); });
        });
    };
    jamSession = std::make_unique<JamSession> (*engineImpl, appStore, sched, std::move (o));
    jamSession->addListener (this);
    homeMonitorGate = std::make_unique<HomeMonitorGate> (*authController, *jamSession);
}

void MeonCore::updateConnection()
{
    connWatch.update (ConnectionWatch::inputFor (appStore.state().link, jamSession != nullptr && jamSession->isJamServerDown()));
}

void MeonCore::jamServerLinkChanged()
{
    updateConnection();
}

void MeonCore::notice (const Notice& n)
{
    if (n.kind == Notice::Kind::roomLostWhileDisconnected) connWatch.roomLost();
}

void MeonCore::authStepChanged (AuthController::Step s)
{
    // RTT 는 프로필까지 마친 계정만 보고할 수 있다 (API 3.4). 그 뒤로는 1분마다
    if (s != AuthController::Step::ready && s != AuthController::Step::done && s != AuthController::Step::discord)
    {
        if (s == AuthController::Step::login) rtt.reset();
        return;
    }
    if (rtt != nullptr) return;
    rtt = std::make_unique<RttReporter> ([this] (int ms)
    {
        appStore.setMyRtt (ms);
        meonApi->reportRtt (ms, [] (Result<Done>) {});
    });
    // 방에 있으면 그 방 Connection 의 AOO 서버, 아니면 서버 설정의 AOO 주소. 켤 때 한 번 재고 그 뒤 1분마다
    const auto t = RttReporter::targetFor (appStore.state().currentRoom, defaultRttTarget());
    rtt->start (t.host, t.port);
}

RttReporter::Target MeonCore::defaultRttTarget() const
{
    if (options.server.isFake()) return { options.fake.aooHost, options.fake.aooPort };
    return { options.server.aooHost, options.server.aooPort };
}

void MeonCore::stateChanged (StateKey k)
{
    if (k == StateKey::link) updateConnection();
    if (k != StateKey::currentRoom || rtt == nullptr) return;
    const auto t = RttReporter::targetFor (appStore.state().currentRoom, defaultRttTarget());
    if (! (rtt->target() == t)) rtt->setTarget (t);
}

MeonCore::~MeonCore()
{
    AppLog::write ("app", LOGTXT ("종료 중"));
    leaveOnQuit();
    appStore.onRoomLeftOnQuit = nullptr;
    rtt.reset();
    homeMonitorGate.reset();
    if (jamSession != nullptr) jamSession->removeListener (this);
    jamSession.reset();
    engineImpl.reset();
    accountController.reset();
    roomController.reset();
    homeController.reset();
    authController->removeListener (this);
    appStore.removeListener (this);
    authController.reset();
    rt->removeListener (&appStore);
    rt->disconnect();
    rt.reset();
    if (meonApi != nullptr) meonApi->setRefreshCoordinator (nullptr);
    refreshLock.reset();
    meonApi.reset();
    realtimeTransport.reset();
    restTransport.reset();
    if (fake != nullptr) fake->onStateChanged = nullptr;
    fake.reset();
    if (options.logFile != juce::File()) AppLog::close();
}

void MeonCore::start()
{
    if (started) return;
    started = true;
    authController->start();
}

void MeonCore::applyDevOptions (Options& o, const juce::StringPairArray& dev)
{
    const auto& keys = dev.getAllKeys();
    if (keys.contains ("fake-latency")) o.fake.latencyMs = dev["fake-latency"].getIntValue();
    if (keys.contains ("fake-speed"))   o.fake.scriptSpeed = juce::jmax (0.1, dev["fake-speed"].getDoubleValue());
    if (keys.contains ("fake-script"))  o.fake.scripts = dev["fake-script"] != "0";
    if (keys.contains ("fake-aoo"))
    {
        o.fakeAooGiven = true;
        const auto v = dev["fake-aoo"];
        o.fake.aooHost = v.upToLastOccurrenceOf (":", false, false);
        if (v.containsChar (':')) o.fake.aooPort = v.fromLastOccurrenceOf (":", false, false).getIntValue();
    }
    if (keys.contains ("fake-reset")) o.resetFakeState = true;
}

} // namespace meon::core
