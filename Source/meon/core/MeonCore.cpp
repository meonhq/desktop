#include "MeonCore.h"
#include "MeonFixtures.h"
#include "log/AppLog.h"

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

    fake = std::make_unique<FakeMeonServer> (sched, options.fake);
    fake->loadWorld (juce::JSON::parse (juce::String::fromUTF8 (MeonFixtures::world_json, MeonFixtures::world_jsonSize)));

    // 가짜 계정(이 설치의 계정 ID·AOO 비밀번호·refresh 토큰)은 설정 폴더의 파일에 둔다. 진짜 서버 단계에서 지운다
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

    meonApi = std::make_unique<MeonApi> (*fake, options.build);
    rt = std::make_unique<Realtime> (*fake, sched, options.build);
    rt->addListener (&appStore);
    // 가짜 기간에는 진짜 토큰과 섞이지 않게 항목 이름을 따로 쓴다
    tokenStore = TokenStore::createDefault ("fake-refresh-token");
    if (options.resetFakeState) tokenStore->clear();
    authController = std::make_unique<AuthController> (*meonApi, *rt, appStore, *tokenStore, sched);
    homeController = std::make_unique<HomeController> (*meonApi, *rt, appStore, sched);
    roomController = std::make_unique<RoomController> (*meonApi, appStore);
    accountController = std::make_unique<AccountController> (*meonApi, appStore, *authController);
    authController->addListener (this);
}

void MeonCore::attachEngine (std::unique_ptr<Engine> e, JamSession::Options o)
{
    jamSession.reset();
    engineImpl = std::move (e);
    o.plugin = options.plugin;
    o.fake = fake.get();
    jamSession = std::make_unique<JamSession> (*engineImpl, appStore, sched, std::move (o));
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
    // 가짜 기간: 지금 쓰는 AOO 서버 (진짜 서버 단계에서는 마지막 Connection 이나 config 의 주소)
    rtt->start (options.fake.aooHost, options.fake.aooPort);
}

MeonCore::~MeonCore()
{
    AppLog::write ("app", LOGTXT ("종료 중"));
    rtt.reset();
    jamSession.reset();
    engineImpl.reset();
    accountController.reset();
    roomController.reset();
    homeController.reset();
    authController->removeListener (this);
    authController.reset();
    rt->removeListener (&appStore);
    rt->disconnect();
    rt.reset();
    meonApi.reset();
    fake->onStateChanged = nullptr;
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
        const auto v = dev["fake-aoo"];
        o.fake.aooHost = v.upToLastOccurrenceOf (":", false, false);
        if (v.containsChar (':')) o.fake.aooPort = v.fromLastOccurrenceOf (":", false, false).getIntValue();
    }
    if (keys.contains ("fake-reset")) o.resetFakeState = true;
}

} // namespace meon::core
