// MEON - SonobusAudioProcessor 의 MEON 부분 (엔진 수정 E2). 엔진 파일을 덜 건드리려고 여기 따로 둔다.
#include "../SonobusPluginProcessor.h"
#include "core/MeonCore.h"
#include "MeonSettings.h"
#include "shell/ShellHooks.h"

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
    }
    return *mMeonCore;
}
