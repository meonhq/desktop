// MEON - SonobusAudioProcessor 의 MEON 부분 (엔진 수정 E2). 엔진 파일을 덜 건드리려고 여기 따로 둔다.
#include "../SonobusPluginProcessor.h"
#include "core/MeonCore.h"
#include "core/jam/EngineLink.h"
#include "MeonSettings.h"
#include "shell/ShellHooks.h"

namespace
{
    // 설정 파일 키 (REMAKE_PLAN 7장)
    meon::core::JamPrefs loadPrefs (bool plugin)
    {
        meon::MeonSettings s;
        auto& p = s.properties();
        meon::core::JamPrefs j;
        j.sendMuted = p.getBoolValue ("sendMuted", false);
        j.sendGainDb = juce::jlimit (-30.0f, 10.0f, (float) p.getDoubleValue ("sendGainDb", 0.0));
        j.bufferMode = p.getValue ("bufferMode", "latency") == "stable" ? meon::core::Engine::BufferMode::stable
                                                                        : meon::core::Engine::BufferMode::latency;
        j.selfMonitor = p.getBoolValue (plugin ? "selfMonitorPlugin" : "selfMonitorApp", false);   // 기본 끔 (Q30)
        j.keepMasterVolume = p.getBoolValue ("keepMasterVolume", true);
        j.masterDb = juce::jlimit (-30.0f, 6.0f, (float) p.getDoubleValue ("masterVolumeDb", 0.0));
        j.inputChannelStart = s.getInputChannelStart();
        j.inputChannelCount = s.getInputChannelCount();
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
        p.setValue ("keepMasterVolume", j.keepMasterVolume);
        p.setValue ("masterVolumeDb", j.masterDb);
        if (! plugin) s.setInputChannels (j.inputChannelStart, j.inputChannelCount);
        s.save();
    }
}

meon::core::MeonCore& SonobusAudioProcessor::getMeonCore()
{
    if (mMeonCore == nullptr)
    {
        meon::core::MeonCore::Options o;
        o.plugin = ! juce::JUCEApplicationBase::isStandaloneApp();
        o.build = MEON_BUILD_ID;
        o.dataFolder = meon::MeonSettings::getSettingsFolder();
       #if MEON_DEV_OPTIONS
        meon::core::MeonCore::applyDevOptions (o, meon::devOptions());
       #endif
        mMeonCore = std::make_unique<meon::core::MeonCore> (o);

        auto link = std::make_unique<meon::core::EngineLink> (*this, o.plugin);
        link->deviceManager = [] { auto& p = meon::deviceManagerProvider(); return p ? p() : nullptr; };
        meon::core::JamSession::Options jo;
        jo.logFolder = meon::MeonSettings::getLogFolder();
        jo.prefs = loadPrefs (o.plugin);
        const bool plugin = o.plugin;
        jo.savePrefs = [plugin] (const meon::core::JamPrefs& j) { savePrefs (j, plugin); };
        mMeonCore->attachEngine (std::move (link), std::move (jo));
    }
    return *mMeonCore;
}
