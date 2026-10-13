// MEON - 독립 앱 셸(SonoStandaloneFilterApp / SonoStandaloneFilterWindow)이 에디터에 바라는 것.
// 셸은 구체 에디터 클래스를 모르고 이 인터페이스로만 부른다. 에디터를 바꿔도 셸은 그대로 둔다.
#pragma once

#include <JuceHeader.h>

namespace meon
{

class ShellHooks
{
public:
    virtual ~ShellHooks() = default;

    /** 셸이 채운다: 에디터가 오디오 장치 관리자에 접근할 때 */
    std::function<juce::AudioDeviceManager*()> getAudioDeviceManager;
    /** 셸이 채운다: 장치·엔진 상태·앱 설정을 지금 저장 */
    std::function<void()> saveSettingsIfNeeded;

    /** 독립 앱 종료 요청. true 면 셸이 바로 종료한다. false 면 에디터가 확인한 뒤 스스로 종료한다. */
    virtual bool requestedQuit() = 0;

    /** 개발용 실행 옵션(--key=value, --flag). MEON_DEV_OPTIONS 빌드에서만 불린다. */
    virtual void applyDevOptions (const juce::StringPairArray& options) = 0;
};

/** 개발용 실행 옵션을 해석한다. "--key=value" → key:value, "--flag" → flag:"1". 해석한 인자는 목록에서 뺀다.
    셸이 쓰는 키(window, snapshot, no-input)도 같은 표에 들어 있다. */
/** 셸이 해석한 개발용 실행 옵션 (프로세서가 MeonCore 를 만들 때 읽는다). 배포 빌드에서는 늘 비어 있다 */
inline juce::StringPairArray& devOptions()
{
    static juce::StringPairArray options;
    return options;
}

inline juce::StringPairArray parseDevOptions (juce::StringArray& args)
{
    juce::StringPairArray out;
    for (int i = args.size(); --i >= 0;)
    {
        const auto a = args[i];
        if (! a.startsWith ("--") || a.length() < 3)
            continue;
        const auto body = a.substring (2);
        const auto key = body.upToFirstOccurrenceOf ("=", false, false);
        const auto value = body.containsChar ('=') ? body.fromFirstOccurrenceOf ("=", false, false) : juce::String ("1");
        out.set (key, value);
        args.remove (i);
    }
    return out;
}

} // namespace meon
