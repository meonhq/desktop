// MEON 브리지 실험 (MEON_BRIDGE_SPIKE): 독립 앱 쪽. BridgeAppSide.h 설명 참고.
#include <JuceHeader.h>
#include "BridgeAppSide.h"
#include "BridgeCore.h"
#include "../shell/ShellHooks.h"
#include "../core/log/AppLog.h"

namespace meon::bridge
{
namespace
{
const char* const kTypeName = "MEON Bridge (test)";
const char* const kDeviceName = "MEON Bridge";

juce::String T (const char* utf8) { return juce::String::fromUTF8 (utf8); }

std::string bridgeName()
{
    auto n = meon::devOptions()["bridge-name"];   // 시험용 (두 앱을 한 컴퓨터에서 돌릴 때)
    if (n.isEmpty()) n = juce::SystemStats::getEnvironmentVariable ("MEON_BRIDGE_NAME", "meonbrg");
    return n.toStdString();
}

/** true = 받은 소리를 그대로 돌려준다 (1단계: 통로만 잰다), false = 합주 엔진이 처리한다 (2단계) */
std::atomic<bool> loopback { true };

class BridgeDevice;
BridgeDevice* liveDevice = nullptr;   // 상태 창이 읽는다 (메시지 스레드)

//==============================================================================
class BridgeDevice : public juce::AudioIODevice
{
public:
    BridgeDevice() : juce::AudioIODevice (kDeviceName, kTypeName) { liveDevice = this; }
    ~BridgeDevice() override
    {
        close();
        if (liveDevice == this) liveDevice = nullptr;
    }

    juce::StringArray getOutputChannelNames() override { return { "L", "R" }; }
    juce::StringArray getInputChannelNames() override { return { "L", "R" }; }
    juce::Array<double> getAvailableSampleRates() override { return { 44100.0, 48000.0, 88200.0, 96000.0 }; }
    juce::Array<int> getAvailableBufferSizes() override { return { 32, 64, 128, 256, 512, 1024, 2048 }; }
    int getDefaultBufferSize() override { return 256; }

    juce::String open (const juce::BigInteger& ins, const juce::BigInteger& outs, double sr, int bs) override
    {
        activeIns = ins;
        activeOuts = outs;
        if (rate <= 0) rate = sr > 0 ? sr : 48000.0;   // 플러그인이 붙으면 그 값으로 바뀐다
        if (block <= 0) block = bs > 0 ? bs : 256;
        opened = true;
        return {};
    }

    void close() override
    {
        stop();
        opened = false;
    }

    bool isOpen() override { return opened; }

    void start (juce::AudioIODeviceCallback* cb) override
    {
        if (cb == nullptr || playing) return;
        {
            const juce::ScopedLock sl (lock);
            callback = cb;
        }
        cb->audioDeviceAboutToStart (this);
        const bool ok = endpoint.start (bridgeName(),
            [this] (const float* const* in, float* const* out, int ch, int n) { processBridgeBlock (in, out, ch, n); },
            [this] (double sr, int bs, int ch) { formatChanged (sr, bs, ch); },
            true);   // 플러그인이 없을 때도 자기 시계로 돌아 합주 엔진이 멈추지 않게
        lastError = ok ? juce::String() : juce::String::fromUTF8 (endpoint.channel.errorText.c_str());
        playing = ok;
        meon::core::AppLog::write ("bridge", ok ? T ("브리지 장치 시작 (통로 ") + juce::String (bridgeName()) + ")"
                                                : T ("브리지 장치 시작 실패: ") + lastError);
    }

    void stop() override
    {
        if (! playing) return;
        playing = false;
        endpoint.stop();   // 처리 스레드를 먼저 끝낸다 (잠금을 쥔 채 기다리지 않는다)
        juce::AudioIODeviceCallback* cb = nullptr;
        {
            const juce::ScopedLock sl (lock);
            std::swap (cb, callback);
        }
        if (cb != nullptr) cb->audioDeviceStopped();
    }

    bool isPlaying() override { return playing; }
    juce::String getLastError() override { return lastError; }
    int getCurrentBufferSizeSamples() override { return block; }
    double getCurrentSampleRate() override { return rate; }
    int getCurrentBitDepth() override { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { return activeOuts; }
    juce::BigInteger getActiveInputChannels() const override { return activeIns; }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }

    AppEndpoint endpoint;
    std::atomic<uint64_t> engineBlocks { 0 }, loopbackBlocks { 0 };

private:
    // 브리지 처리 스레드 (앱 쪽 실시간 스레드. DAW 의 오디오 스레드가 아니다)
    void processBridgeBlock (const float* const* in, float* const* out, int ch, int n)
    {
        if (loopback.load (std::memory_order_relaxed))
        {
            for (int c = 0; c < ch; ++c)
                std::memcpy (out[c], in[c], sizeof (float) * (size_t) n);
            loopbackBlocks++;
            return;
        }
        const juce::ScopedLock sl (lock);
        if (callback == nullptr)
        {
            for (int c = 0; c < ch; ++c) std::memset (out[c], 0, sizeof (float) * (size_t) n);
            return;
        }
        const float* ins[kMaxChannels] {};
        float* outs[kMaxChannels] {};
        int ni = 0, no = 0;
        for (int c = 0; c < kMaxChannels; ++c)
        {
            if (activeIns[c]) ins[ni++] = in[c];
            if (activeOuts[c]) outs[no++] = out[c];
        }
        callback->audioDeviceIOCallbackWithContext (ins, ni, outs, no, n, {});
        // 쓰지 않은 출력 채널(모노 출력 등)은 무음
        for (int c = 0; c < kMaxChannels; ++c)
            if (! activeOuts[c]) std::memset (out[c], 0, sizeof (float) * (size_t) n);
        engineBlocks++;
    }

    void formatChanged (double sr, int bs, int)
    {
        if (sr == rate && bs == block) return;
        juce::AudioIODeviceCallback* cb = nullptr;
        {
            const juce::ScopedLock sl (lock);
            cb = callback;
            if (cb != nullptr) cb->audioDeviceStopped();
            rate = sr;
            block = bs;
            if (cb != nullptr) cb->audioDeviceAboutToStart (this);   // 프로세서 prepareToPlay (DAW 의 형식으로)
        }
        meon::core::AppLog::write ("bridge", T ("DAW 형식으로 맞춤: ") + juce::String (sr, 0) + " Hz, " + juce::String (bs) + T (" 샘플"));
    }

    juce::CriticalSection lock;
    juce::AudioIODeviceCallback* callback = nullptr;
    juce::BigInteger activeIns, activeOuts;
    std::atomic<double> rate { 0 };
    std::atomic<int> block { 0 };
    std::atomic<bool> opened { false }, playing { false };
    juce::String lastError;
};

class BridgeDeviceType : public juce::AudioIODeviceType
{
public:
    BridgeDeviceType() : juce::AudioIODeviceType (kTypeName) {}
    void scanForDevices() override {}
    juce::StringArray getDeviceNames (bool) const override { return { kDeviceName }; }
    int getDefaultDeviceIndex (bool) const override { return 0; }
    int getIndexOfDevice (juce::AudioIODevice* d, bool) const override { return d != nullptr ? 0 : -1; }
    bool hasSeparateInputsAndOutputs() const override { return false; }
    juce::AudioIODevice* createDevice (const juce::String&, const juce::String&) override { return new BridgeDevice(); }
};

//==============================================================================
class StatusPanel : public juce::Component, private juce::Timer
{
public:
    StatusPanel()
    {
        for (auto* b : { &loop, &engine })
        {
            b->setRadioGroupId (7);
            b->setClickingTogglesState (true);
            addAndMakeVisible (b);
        }
        (loopback.load() ? loop : engine).setToggleState (true, juce::dontSendNotification);
        loop.onClick = [] { loopback = true; meon::core::AppLog::write ("bridge", T ("처리: 되돌려 보내기")); };
        engine.onClick = [] { loopback = false; meon::core::AppLog::write ("bridge", T ("처리: 합주 엔진")); };
        setSize (460, 250);
        startTimerHz (4);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xfff7f7f5));
        auto r = getLocalBounds().reduced (14);
        g.setColour (juce::Colour (0xff1d1d1b));
        g.setFont (juce::Font (16.0f, juce::Font::bold));
        g.drawText (T ("Meon Bridge App (test): 브리지 모드"), r.removeFromTop (22), juce::Justification::left);
        g.setFont (juce::Font (14.0f));
        auto* d = liveDevice;
        juce::String status;
        if (d == nullptr || ! d->isPlaying())
            status = T ("브리지 장치가 꺼져 있어요") + (d != nullptr && d->getLastError().isNotEmpty() ? ": " + d->getLastError() : juce::String());
        else if (d->endpoint.receivingBlocks())
            status = T ("플러그인 연결됨: ") + juce::String (d->endpoint.currentRate.load(), 0) + " Hz, " + juce::String (d->endpoint.currentBlock.load()) + T (" 샘플");
        else if (d->endpoint.pluginConnected())
            status = T ("플러그인은 있지만 DAW 가 소리를 보내지 않아요 (재생 멈춤?)");
        else
            status = T ("플러그인을 기다리는 중: DAW 트랙에 Meon Bridge (test) 를 꽂아 주세요");
        g.drawFittedText (status, r.removeFromTop (40), juce::Justification::topLeft, 2);
        r.removeFromTop (38);   // 버튼
        if (d != nullptr)
            if (auto* s = d->endpoint.channel.get())
            {
                auto line = [&] (const juce::String& k, const juce::String& v)
                {
                    auto row = r.removeFromTop (20);
                    g.setColour (juce::Colour (0xff6b6b66));
                    g.drawText (k, row.removeFromLeft (200), juce::Justification::left);
                    g.setColour (juce::Colour (0xff1d1d1b));
                    g.drawText (v, row, juce::Justification::left);
                };
                line (T ("처리한 블록 (플러그인 박자)"), juce::String ((juce::int64) s->appProcessed.load()));
                line (T ("혼자 돈 블록 (플러그인 없을 때)"), juce::String ((juce::int64) s->appFreeRun.load()));
                line (T ("건너뛴 블록 (앱이 늦어서)"), juce::String ((juce::int64) s->appSkipped.load()));
                line (T ("앱 최대 처리 시간"), juce::String (s->appMaxProcessNs.load() / 1e6, 3) + " ms");
                line (T ("처리 스레드 실시간 우선순위"), d->endpoint.realtime.load() ? T ("예") : T ("아니오"));
            }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14);
        r.removeFromTop (22 + 40);
        auto row = r.removeFromTop (32);
        loop.setBounds (row.removeFromLeft (210));
        row.removeFromLeft (8);
        engine.setBounds (row.removeFromLeft (210));
    }

private:
    void timerCallback() override { repaint(); }
    juce::TextButton loop { T ("1단계: 되돌려 보내기") }, engine { T ("2단계: 합주 엔진") };
};

class StatusWindow : public juce::DocumentWindow, private juce::DeletedAtShutdown
{
public:
    StatusWindow() : juce::DocumentWindow ("Meon Bridge App (test)", juce::Colours::white, juce::DocumentWindow::minimiseButton)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new StatusPanel(), true);
        setTopLeftPosition (60, 60);
        setVisible (true);
    }
};

bool installed = false;
} // namespace

juce::String registerDeviceType (juce::AudioDeviceManager& dm)
{
    for (auto* t : dm.getAvailableDeviceTypes())
        if (t->getTypeName() == kTypeName)
            return kTypeName;
    dm.addAudioDeviceType (std::make_unique<BridgeDeviceType>());
    return kTypeName;
}

void installAppSide()
{
    if (installed) return;
    auto& provider = meon::deviceManagerProvider();
    auto* dm = provider ? provider() : nullptr;
    if (dm == nullptr)
    {
        juce::Timer::callAfterDelay (200, [] { installAppSide(); });
        return;
    }
    installed = true;
    if (meon::devOptions().getAllKeys().contains ("bridge-engine")) loopback = false;   // 시험용: 처음부터 2단계

    registerDeviceType (*dm);
    if (dm->getCurrentAudioDeviceType() != kTypeName)
        dm->setCurrentAudioDeviceType (kTypeName, true);
    auto setup = dm->getAudioDeviceSetup();
    setup.inputDeviceName = setup.outputDeviceName = kDeviceName;
    setup.useDefaultInputChannels = setup.useDefaultOutputChannels = true;
    const auto err = dm->setAudioDeviceSetup (setup, true);
    meon::core::AppLog::write ("bridge", err.isEmpty() ? T ("브리지 모드: 자기 오디오 장치 대신 MEON Bridge 장치를 씀")
                                                       : T ("브리지 장치를 고르지 못함: ") + err);
    if (! meon::devOptions().getAllKeys().contains ("bridge-no-window"))
        new StatusWindow();   // 앱이 끝날 때 JUCE 가 지운다 (DeletedAtShutdown)
}

} // namespace meon::bridge
