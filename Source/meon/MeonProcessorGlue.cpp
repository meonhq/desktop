// MEON - SonobusAudioProcessor 의 MEON 부분 (엔진 수정 E2). 엔진 파일을 덜 건드리려고 여기 따로 둔다.
#include "../SonobusPluginProcessor.h"
#include "core/MeonCore.h"
#include "core/jam/EngineLink.h"
#include "ui/dev/ShowcaseEngine.h"
#include "core/log/AppLog.h"
#include "MeonSettings.h"
#include "MeonCrashDump.h"
#include "shell/ShellHooks.h"

// 독립 앱(Meon.exe)이면 프로세스가 뜨자마자 크래시 덤프 처리기를 건다 (창·오디오 장치를 여는 중에 죽어도 남게).
// DAW 안의 플러그인에서는 걸지 않는다 (installForStandalone 이 실행 파일 이름으로 가른다)
static const bool meonCrashDumpAtStartup = meon::crashdump::installForStandalone();

namespace
{
    // 설정 파일 키 (REMAKE_PLAN 7장)
    meon::core::JamPrefs loadPrefs (bool plugin)
    {
        meon::MeonSettings s;
        auto& p = s.properties();

        // 버그 21: 예전 빌드에서 셀프 모니터가 켜진 채 저장된 사람이 있다. 이 버전에서 한 번만 끔으로 되돌린다
        // (Q30: 기본 끔). 그 뒤로는 사용자가 켠 값을 그대로 쓴다
        if (! p.getBoolValue ("selfMonitorResetQ30", false))
        {
            const bool wasOn = p.getBoolValue ("selfMonitorApp", false) || p.getBoolValue ("selfMonitorPlugin", false);
            p.setValue ("selfMonitorApp", false);
            p.setValue ("selfMonitorPlugin", false);
            p.setValue ("selfMonitorResetQ30", true);
            s.save();
            meon::core::AppLog::write ("audio", wasOn ? LOGTXT ("셀프 모니터 설정을 한 번 끔으로 되돌렸습니다 (Q30)")
                                                      : LOGTXT ("셀프 모니터 설정 확인: 이미 끔 (Q30)"));
        }

        meon::core::JamPrefs j;
        j.sendMuted = p.getBoolValue ("sendMuted", false);
        j.sendGainDb = juce::jlimit (-30.0f, 10.0f, (float) p.getDoubleValue ("sendGainDb", 0.0));
        j.bufferMode = p.getValue ("bufferMode", "latency") == "stable" ? meon::core::Engine::BufferMode::stable
                                                                        : meon::core::Engine::BufferMode::latency;
        j.selfMonitor = p.getBoolValue (plugin ? "selfMonitorPlugin" : "selfMonitorApp", false);   // 기본 끔 (Q30)
        j.selfMonitorDb = (float) p.getDoubleValue ("selfMonitorDb", 0.0);
        j.selfMonitorPan = juce::jlimit (-100, 100, p.getIntValue ("selfMonitorPan", 0));
        j.homeMonitor = ! plugin && p.getBoolValue ("homeMonitor", false);   // 앱만, 기본 끔 (버그 28)
        j.keepMasterVolume = p.getBoolValue ("keepMasterVolume", true);
        j.masterDb = juce::jlimit (-30.0f, 6.0f, (float) p.getDoubleValue ("masterVolumeDb", 0.0));
        j.inputChannelStart = s.getInputChannelStart();
        j.inputChannelCount = s.getInputChannelCount();
        j.earGuard = (int) s.getEarGuardLevel();
        j.pluginSendChannels = juce::jlimit (1, 2, p.getIntValue ("pluginSendChannels", 2));
        return j;
    }

    void savePrefs (const meon::core::JamPrefs& j, bool plugin)
    {
        meon::MeonSettings s;
        auto& p = s.properties();
        if (! plugin) p.setValue ("sendMuted", j.sendMuted);   // 플러그인은 프로젝트 상태(엔진 값)에 남는다
        p.setValue ("sendGainDb", j.sendGainDb);
        p.setValue ("bufferMode", j.bufferMode == meon::core::Engine::BufferMode::stable ? "stable" : "latency");
        p.setValue (plugin ? "selfMonitorPlugin" : "selfMonitorApp", j.selfMonitor);
        p.setValue ("selfMonitorDb", j.selfMonitorDb);
        p.setValue ("selfMonitorPan", j.selfMonitorPan);
        if (! plugin) p.setValue ("homeMonitor", j.homeMonitor);
        p.setValue ("keepMasterVolume", j.keepMasterVolume);
        p.setValue ("masterVolumeDb", j.masterDb);
        if (! plugin) s.setInputChannels (j.inputChannelStart, j.inputChannelCount);
        s.setEarGuardLevel ((meon::EarGuard::Level) j.earGuard);
        if (plugin) p.setValue ("pluginSendChannels", j.pluginSendChannels);
        s.save();
    }
}

meon::core::MeonCore& SonobusAudioProcessor::getMeonCore()
{
    if (mMeonCore == nullptr)
    {
        meon::core::MeonCore::Options o;
        o.plugin = ! juce::JUCEApplicationBase::isStandaloneApp();
       #if MEON_DEV_OPTIONS
        if (meon::devOptions().getAllKeys().contains ("as-plugin")) o.plugin = true;   // 개발용: 독립 앱에서 플러그인 화면 보기
       #endif
        o.build = MEON_BUILD_ID;
        // 실제 번호 빌드는 운영 서버 고정. 빌드 0 은 가짜 서버가 기본이고 --server=prod|local|fake|<주소> 로 바꾼다
       #if MEON_DEV_OPTIONS
        o.server = meon::core::ServerConfig::choose (MEON_BUILD_ID, meon::devOptions());
       #else
        o.server = meon::core::ServerConfig::choose (MEON_BUILD_ID, {});
       #endif
        o.dataFolder = meon::MeonSettings::getSettingsFolder();
        o.logFile = meon::MeonSettings::getLogFolder().getChildFile (juce::JUCEApplicationBase::isStandaloneApp() ? "app.log" : "plugin.log");
       #if MEON_DEV_OPTIONS
        meon::core::MeonCore::applyDevOptions (o, meon::devOptions());
       #endif
        mMeonCore = std::make_unique<meon::core::MeonCore> (o);
        juce::ignoreUnused (meonCrashDumpAtStartup);
        meon::crashdump::installForStandalone();   // 장치를 연 뒤 드라이버가 처리기를 바꿔 놓았으면 다시 건다
        meon::crashdump::tidyAndLog();

        std::unique_ptr<meon::core::Engine> engine;
       #if MEON_DEV_OPTIONS
        if (o.showcaseScene.isNotEmpty())   // 소개 페이지 캡처: 소리 없이 정해 둔 멤버가 연주하는 것처럼 보이는 엔진
            engine = std::make_unique<meon::ui::dev::ShowcaseEngine>();
       #endif
        if (engine == nullptr)
        {
            auto link = std::make_unique<meon::core::EngineLink> (*this, o.plugin);
            link->deviceManager = [] { auto& p = meon::deviceManagerProvider(); return p ? p() : nullptr; };
            engine = std::move (link);
        }
        meon::core::JamSession::Options jo;
        jo.logFolder = meon::MeonSettings::getLogFolder();
        jo.prefs = loadPrefs (o.plugin);
        const bool plugin = o.plugin;
        jo.savePrefs = [plugin] (const meon::core::JamPrefs& j) { savePrefs (j, plugin); };
        // 버그 #18: 다른 인스턴스가 설정 파일을 바꿨으면(수정 시각) 귀 테러 방지 단계를 다시 읽는다
        jo.pollEarGuard = [lastStamp = juce::Time()] () mutable
        {
            const auto stamp = meon::MeonSettings::getSettingsFile().getLastModificationTime();
            if (stamp == lastStamp) return -1;
            lastStamp = stamp;
            meon::MeonSettings s;
            return (int) s.getEarGuardLevel();
        };
        mMeonCore->attachEngine (std::move (engine), std::move (jo));
    }
    return *mMeonCore;
}
