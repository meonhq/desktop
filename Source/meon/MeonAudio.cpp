#include "MeonAudio.h"
#include "MeonTheme.h"
#if MEON_BRIDGE_SPIKE
 #include "bridge/BridgeAppSide.h"
#endif

namespace meon
{
namespace audio
{

static juce::AudioIODeviceType* findType (juce::AudioDeviceManager& dm, const juce::String& name)
{
    for (auto* t : dm.getAvailableDeviceTypes())
        if (t->getTypeName() == name)
            return t;
    return nullptr;
}

juce::StringArray driverTypes (juce::AudioDeviceManager& dm)
{
    juce::StringArray result;
#if JUCE_WINDOWS
    for (auto name : { "ASIO", "Windows Audio (Low Latency Mode)", "Windows Audio (Exclusive Mode)", "Windows Audio" })
        if (findType (dm, name) != nullptr)
            result.add (name);
#else
    juce::ignoreUnused (dm);
#endif
    return result;
}

juce::String driverLabel (const juce::String& typeName)
{
    if (typeName == "ASIO")                             return TXT ("ASIO (오디오 인터페이스)");
    if (typeName == "Windows Audio (Low Latency Mode)") return TXT ("Windows 오디오 · 저지연");
    if (typeName == "Windows Audio (Exclusive Mode)")   return TXT ("Windows 오디오 · 단독 사용 (다른 앱 소리 꺼짐)");
    if (typeName == "Windows Audio")                    return TXT ("Windows 오디오 · 기본 (지연 큼)");
    return typeName;
}

void setDriverType (juce::AudioDeviceManager& dm, const juce::String& typeName)
{
    if (dm.getCurrentAudioDeviceType() == typeName)
        return;
    if (auto* t = findType (dm, typeName))
        t->scanForDevices();
    // treatAsChosenDevice=true: 새 드라이버의 기본 입력·출력 장치를 연다
    dm.setCurrentAudioDeviceType (typeName, true);
}

juce::String preferredDriverType (juce::AudioDeviceManager& dm)
{
#if MEON_BRIDGE_SPIKE
    // 브리지 실험 앱 (Windows 셸이 처음 장치를 열기 전에 부른다): 진짜 장치(ASIO 등)를 잠깐이라도 열지 않는다.
    // ASIO 드라이버는 보통 한 프로그램만 쓸 수 있어서, 앱이 열면 DAW 와 부딪힌다
    return meon::bridge::registerDeviceType (dm);
#endif
#if JUCE_WINDOWS
    if (auto* asio = findType (dm, "ASIO"))
    {
        asio->scanForDevices();
        if (asio->getDeviceNames (false).size() > 0)
            return "ASIO";
    }
    if (findType (dm, "Windows Audio (Low Latency Mode)") != nullptr)
        return "Windows Audio (Low Latency Mode)";
    return "Windows Audio";
#else
    return dm.getCurrentAudioDeviceType();
#endif
}

void setDevices (juce::AudioDeviceManager& dm, const juce::String& input, const juce::String& output)
{
    auto setup = dm.getAudioDeviceSetup();
    auto* type = dm.getCurrentDeviceTypeObject();
    const bool separate = type == nullptr || type->hasSeparateInputsAndOutputs();

    if (separate)
    {
        if (input.isNotEmpty())  setup.inputDeviceName = input;
        if (output.isNotEmpty()) setup.outputDeviceName = output;
    }
    else
    {
        // ASIO: 한 드라이버가 입력과 출력을 같이 맡는다. 바뀐 쪽을 따라 둘 다 맞춘다.
        const auto chosen = (output.isNotEmpty() && output != setup.outputDeviceName) ? output
                          : (input.isNotEmpty() ? input : output);
        setup.inputDeviceName = setup.outputDeviceName = chosen;
    }
    setup.useDefaultInputChannels = true;
    setup.useDefaultOutputChannels = true;
    if (setup.sampleRate <= 0.0)
        setup.sampleRate = 48000.0;
    dm.setAudioDeviceSetup (setup, true);
}

juce::Array<int> bufferChoices (juce::AudioDeviceManager& dm)
{
    const int targets[] = { 64, 128, 256, 512 };
    juce::Array<int> result;
    auto* dev = dm.getCurrentAudioDevice();
    const auto available = dev != nullptr ? dev->getAvailableBufferSizes() : juce::Array<int>();

    for (int t : targets)
    {
        int best = t;
        if (! available.isEmpty())
        {
            best = available.getFirst();
            for (int s : available)
                if (std::abs (s - t) < std::abs (best - t))
                    best = s;
        }
        result.addIfNotAlreadyThere (best);
    }
    result.sort();
    return result;
}

int currentBufferSize (juce::AudioDeviceManager& dm)
{
    if (auto* dev = dm.getCurrentAudioDevice())
        return dev->getCurrentBufferSizeSamples();
    return dm.getAudioDeviceSetup().bufferSize;
}

void setBufferSize (juce::AudioDeviceManager& dm, int size)
{
    auto setup = dm.getAudioDeviceSetup();
    setup.bufferSize = size;
    dm.setAudioDeviceSetup (setup, true);
}

bool hasDriverPanel (juce::AudioDeviceManager& dm)
{
    auto* dev = dm.getCurrentAudioDevice();
    return dev != nullptr && dev->hasControlPanel();
}

void showDriverPanel (juce::AudioDeviceManager& dm)
{
    if (auto* dev = dm.getCurrentAudioDevice())
    {
        if (dev->showControlPanel())
        {
            // ASIO 설정 창에서 버퍼 등을 바꾸면 장치를 다시 열어야 적용된다
            dm.closeAudioDevice();
            dm.restartLastAudioDevice();
        }
    }
}

}
}
