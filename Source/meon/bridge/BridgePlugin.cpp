// MEON 브리지 실험 플러그인 "Meon Bridge (test)" (MEON_BRIDGE_SPIKE). 제품에 넣지 않는다.
// DAW 안에서는 소리 통로만 한다: 매 블록 입력을 독립 앱에 넘기고, 앱이 처리한 출력을 받아 내보낸다.
// 화면은 상태 한 줄과 측정값만. 기존 Meon 플러그인과 이름·ID 가 달라 같은 DAW 에 같이 설치돼도 충돌하지 않는다.
#include <JuceHeader.h>
#include "BridgeCore.h"

namespace
{
using namespace meon::bridge;

juce::String T (const char* utf8) { return juce::String::fromUTF8 (utf8); }

/** 통로 이름. 시험할 때만 환경변수 MEON_BRIDGE_NAME 으로 바꾼다 */
std::string bridgeName()
{
    auto e = juce::SystemStats::getEnvironmentVariable ("MEON_BRIDGE_NAME", "meonbrg");
    return e.toStdString();
}

juce::File logFile()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("Meon Bridge (test)").getChildFile (T ("기록.txt"));
}

juce::String ms (double v, int digits = 2) { return juce::String (v, digits) + " ms"; }

//==============================================================================
class BridgeProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    BridgeProcessor()
        : juce::AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                                 .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
        tryOpen();
        startTimerHz (4);
    }

    ~BridgeProcessor() override
    {
        stopTimer();
        writeLog ("플러그인 닫힘");
        endpoint.close();
    }

    const juce::String getName() const override { return "Meon Bridge (test)"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
        return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo()) && (in == out || in.isDisabled());
    }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override
    {
        rate = sampleRate;
        preparedBlock = samplesPerBlock;
        endpoint.prepare (sampleRate, samplesPerBlock, juce::jmax (1, getTotalNumOutputChannels()));
        resetRequested = true;
        writeLog (T ("준비: ") + juce::String (sampleRate, 0) + T (" Hz, 블록 ") + juce::String (samplesPerBlock));
    }

    void releaseResources() override {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;
        const int n = buffer.getNumSamples(), ch = buffer.getNumChannels();
        if (resetRequested.exchange (false))
        {
            endpoint.stats.reset();
            meter.reset ((int) (rate / 2));
            sessionStartNs = nowNs();
        }
        lastBlock = n;
        if (n <= 0 || ch <= 0) return;
        if (n > kMaxBlock) { buffer.clear(); return; }   // 이 실험은 4096 샘플까지

        auto* const* w = buffer.getArrayOfWritePointers();
        const bool meas = measuring.load (std::memory_order_relaxed);
        if (meas) meter.writeInput (w, ch, n);   // DAW 입력 대신 클릭
        endpoint.process (w, w, ch, n);          // 입력을 넘기고 같은 버퍼에 출력을 받는다 (앱이 없거나 늦으면 무음)
        if (meas)
        {
            meter.readOutput (w, ch, n);
            buffer.clear();   // 측정 중에는 클릭이 귀에 들리지 않게 내보내지 않는다
        }
    }

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    void getStateInformation (juce::MemoryBlock& dest) override
    {
        juce::ValueTree v ("MeonBridge");
        v.setProperty ("mode", (int) endpoint.mode.load(), nullptr);
        v.setProperty ("timeout", endpoint.timeoutFraction.load(), nullptr);
        juce::MemoryOutputStream os (dest, false);
        v.writeToStream (os);
    }

    void setStateInformation (const void* data, int size) override
    {
        auto v = juce::ValueTree::readFromData (data, (size_t) size);
        if (! v.isValid()) return;
        endpoint.mode = (Mode) (int) v.getProperty ("mode", 1);
        endpoint.timeoutFraction = (float) v.getProperty ("timeout", 0.5f);
    }

    //==========================================================================
    void setMode (Mode m) { endpoint.mode = m; resetRequested = true; writeLog (T ("방식 바꿈: ") + (m == Mode::sameBlock ? "A" : "B")); }
    void setTimeoutFraction (float f) { endpoint.timeoutFraction = f; resetRequested = true; writeLog (T ("A 기다림 한도: ") + juce::String (juce::roundToInt (f * 100)) + "%"); }
    void setMeasuring (bool on) { measuring = on; resetRequested = true; writeLog (on ? "측정 시작" : "측정 끝"); }
    void resetCounters() { resetRequested = true; writeLog ("숫자 초기화 (기록 시작)"); }

    PluginEndpoint endpoint;
    ClickMeter meter;
    std::atomic<bool> measuring { false };
    std::atomic<int> lastBlock { 0 };
    std::atomic<int64_t> sessionStartNs { 0 };
    double rate = 48000;
    int preparedBlock = 0;
    juce::String openError;

    double elapsedSeconds() const { const auto s = sessionStartNs.load(); return s == 0 ? 0.0 : (nowNs() - s) / 1e9; }

    juce::String summaryLine() const
    {
        const auto& st = endpoint.stats;
        const auto blocks = st.blocks.load();
        juce::String s;
        s << (endpoint.mode.load() == Mode::sameBlock ? "A" : "B")
          << " | " << juce::String (rate, 0) << T (" Hz | 블록 ") << lastBlock.load()
          << T (" | 경과 ") << juce::String (elapsedSeconds(), 0) << T ("초")
          << T (" | 늦은 블록 ") << (juce::int64) st.late.load() << "/" << (juce::int64) blocks
          << T (" | 앱 없음 ") << (juce::int64) st.notConnected.load()
          << T (" | A 기다림 건너뜀 ") << (juce::int64) st.waitSkipped.load()
          << T (" | 왕복 p50 ") << juce::String (st.roundTrip.percentileUs (50) / 1000.0, 2) << " ms, p99 " << juce::String (st.roundTrip.percentileUs (99) / 1000.0, 2)
          << T (" ms, 최대 ") << juce::String (st.roundTrip.maxNs.load() / 1e6, 2) << " ms"
          << T (" | 최대 기다림 ") << juce::String (st.maxWaitNs.load() / 1e6, 2) << " ms"
          << T (" | 플러그인 최대 처리 ") << juce::String (st.maxProcessNs.load() / 1e6, 2) << " ms"
          << T (" | 앱 깨우기 최대 ") << juce::String (st.maxPostNs.load() / 1e6, 3) << " ms";
        if (auto* sh = endpoint.channel.get())
            s << T (" | 앱 최대 처리 ") << juce::String (sh->appMaxProcessNs.load() / 1e6, 2) << " ms";
        if (measuring.load())
            s << T (" | 추가 지연 ") << (juce::int64) meter.lastLatency.load() << T (" 샘플 (클릭 ") << (juce::int64) meter.received.load() << "/" << (juce::int64) meter.sent.load() << ")";
        return s;
    }

private:
    void tryOpen()
    {
        if (endpoint.open (bridgeName())) { openError.clear(); writeLog ("통로 열림"); }
        else openError = juce::String::fromUTF8 (endpoint.channel.errorText.c_str());
    }

    void timerCallback() override
    {
        if (! endpoint.channel.isOpen())
        {
            if (++retry % 8 == 0) tryOpen();   // 2초마다 다시
            return;
        }
        endpoint.heartbeat();
        const auto st = endpoint.state();
        if (st != lastState)
        {
            lastState = st;
            writeLog (st == PluginEndpoint::State::connected ? "앱 연결됨"
                      : st == PluginEndpoint::State::waitingForApp ? "앱 기다림"
                      : st == PluginEndpoint::State::otherInstance ? "다른 Meon Bridge 가 이미 연결됨" : "닫힘");
        }
        if (++ticks % 40 == 0)   // 10초마다 누적 숫자
            writeLog (summaryLine());
        const auto e = elapsedSeconds();
        if (! tenMinuteLogged && e >= 600) { tenMinuteLogged = true; writeLog (T ("10분 결과: ") + summaryLine()); }
        if (e < 600) tenMinuteLogged = false;
    }

    void writeLog (const juce::String& line) const
    {
        auto f = logFile();
        f.getParentDirectory().createDirectory();
        f.appendText (juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S") + " [" + juce::String::toHexString ((juce::pointer_sized_int) this) + "] "
                          + line + "\n",
                      false, false, "\n");
    }
    void writeLog (const char* utf8) const { writeLog (juce::String::fromUTF8 (utf8)); }

    std::atomic<bool> resetRequested { true };
    PluginEndpoint::State lastState = PluginEndpoint::State::closed;
    int ticks = 0, retry = 0;
    bool tenMinuteLogged = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BridgeProcessor)
};

//==============================================================================
class BridgeEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BridgeEditor (BridgeProcessor& p) : juce::AudioProcessorEditor (&p), proc (p)
    {
        for (auto* b : { &modeA, &modeB })
        {
            b->setRadioGroupId (1);
            b->setClickingTogglesState (true);
            addAndMakeVisible (b);
        }
        modeA.onClick = [this] { if (modeA.getToggleState()) proc.setMode (Mode::sameBlock); };
        modeB.onClick = [this] { if (modeB.getToggleState()) proc.setMode (Mode::nextBlock); };
        (proc.endpoint.mode.load() == Mode::sameBlock ? modeA : modeB).setToggleState (true, juce::dontSendNotification);

        timeout.addItem ("25%", 25);
        timeout.addItem ("50%", 50);
        timeout.addItem ("75%", 75);
        timeout.setSelectedId (juce::jlimit (25, 75, juce::roundToInt (proc.endpoint.timeoutFraction.load() * 4) * 25), juce::dontSendNotification);
        timeout.onChange = [this] { proc.setTimeoutFraction (timeout.getSelectedId() / 100.0f); };
        addAndMakeVisible (timeout);
        timeoutLabel.setText (T ("A 기다림 한도 (블록 시간의)"), juce::dontSendNotification);
        addAndMakeVisible (timeoutLabel);

        measure.setClickingTogglesState (true);
        measure.onClick = [this] { proc.setMeasuring (measure.getToggleState()); measure.setButtonText (measure.getToggleState() ? T ("측정 끄기") : T ("지연 측정 (클릭)")); };
        addAndMakeVisible (measure);
        reset.onClick = [this] { proc.resetCounters(); };
        addAndMakeVisible (reset);
        openLog.onClick = [] { logFile().getParentDirectory().createDirectory(); logFile().revealToUser(); };
        addAndMakeVisible (openLog);

        setSize (560, 420);
        startTimerHz (10);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xfff7f7f5));
        auto r = getLocalBounds().reduced (16);
        g.setColour (juce::Colour (0xff1d1d1b));
        g.setFont (juce::Font (18.0f, juce::Font::bold));
        g.drawText ("Meon Bridge (test)", r.removeFromTop (24), juce::Justification::left);

        // 상태 한 줄
        auto statusRow = r.removeFromTop (30);
        const auto st = proc.endpoint.state();
        juce::Colour dot = juce::Colours::grey;
        juce::String text;
        if (! proc.endpoint.channel.isOpen())   { dot = juce::Colour (0xffc2541d); text = T ("통로를 열지 못했어요: ") + proc.openError; }
        else if (st == PluginEndpoint::State::connected)     { dot = juce::Colour (0xff2e9e5b); text = T ("앱 연결됨"); }
        else if (st == PluginEndpoint::State::otherInstance) { dot = juce::Colour (0xffd99a1e); text = T ("다른 Meon Bridge (test) 가 이미 연결돼 있어요. 이 플러그인은 소리를 내지 않습니다"); }
        else                                                 { dot = juce::Colour (0xffc2541d); text = T ("앱을 켜 주세요 (Meon Bridge App (test))"); }
        g.setColour (dot);
        g.fillEllipse (statusRow.removeFromLeft (14).withSizeKeepingCentre (10, 10).toFloat());
        g.setColour (juce::Colour (0xff1d1d1b));
        g.setFont (juce::Font (15.0f));
        g.drawText (text, statusRow.withTrimmedLeft (6), juce::Justification::left);

        r.removeFromTop (78);   // 버튼 자리 (resized)

        const auto& s = proc.endpoint.stats;
        const int bs = proc.lastBlock.load();
        const double blockMs = bs > 0 ? bs * 1000.0 / proc.rate : 0;
        const auto blocks = (juce::int64) s.blocks.load();
        const auto late = (juce::int64) s.late.load();
        auto line = [&] (const juce::String& k, const juce::String& v)
        {
            auto row = r.removeFromTop (22);
            g.setColour (juce::Colour (0xff6b6b66));
            g.drawText (k, row.removeFromLeft (190), juce::Justification::left);
            g.setColour (juce::Colour (0xff1d1d1b));
            g.drawText (v, row, juce::Justification::left);
        };
        g.setFont (juce::Font (14.0f));
        line (T ("샘플레이트 · 블록"), juce::String (proc.rate, 0) + T (" Hz · ") + juce::String (bs) + T (" 샘플 (") + ms (blockMs) + ")");
        line (T ("기본 지연 (DAW 블록)"), juce::String (bs) + T (" 샘플 = ") + ms (blockMs));
        if (proc.measuring.load())
        {
            const auto lat = proc.meter.lastLatency.load();
            line (T ("브리지가 더한 지연"), lat < 0 ? T ("재는 중…") : juce::String (lat) + T (" 샘플 = ") + ms (lat * 1000.0 / proc.rate)
                                                          + T ("  (클릭 ") + juce::String ((juce::int64) proc.meter.received.load()) + "/" + juce::String ((juce::int64) proc.meter.sent.load()) + ")");
        }
        else
            line (T ("브리지가 더한 지연"), T ("\"지연 측정\"을 누르면 잽니다 (A 는 0, B 는 블록 하나가 정상)"));
        line (T ("늦은 블록 (무음으로 냄)"), juce::String (late) + " / " + juce::String (blocks)
                                          + (blocks > 0 ? "  (" + juce::String (100.0 * (double) late / (double) blocks, 3) + "%)" : juce::String()));
        line (T ("앱 없던 블록"), juce::String ((juce::int64) s.notConnected.load()));
        line (T ("왕복 시간 (보통 · 99% · 최대)"), ms (s.roundTrip.percentileUs (50) / 1000.0) + T (" · ") + ms (s.roundTrip.percentileUs (99) / 1000.0) + T (" · ") + ms (s.roundTrip.maxNs.load() / 1e6));
        juce::String appMax = "-";
        if (auto* sh = proc.endpoint.channel.get()) appMax = ms (sh->appMaxProcessNs.load() / 1e6);
        line (T ("플러그인 최대 처리 · 최대 기다림 (A) · 앱 최대 처리"), ms (s.maxProcessNs.load() / 1e6) + T (" · ") + ms (s.maxWaitNs.load() / 1e6) + T (" · ") + appMax);
        const int e = (int) proc.elapsedSeconds();
        line (T ("경과 시간"), juce::String (e / 60).paddedLeft ('0', 2) + ":" + juce::String (e % 60).paddedLeft ('0', 2)
                                  + (e >= 600 ? T ("  (10분 지남: 기록 파일에 남겼어요)") : juce::String()));
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16);
        r.removeFromTop (24 + 30);
        auto row1 = r.removeFromTop (34);
        modeA.setBounds (row1.removeFromLeft (170).reduced (0, 2));
        row1.removeFromLeft (8);
        modeB.setBounds (row1.removeFromLeft (170).reduced (0, 2));
        row1.removeFromLeft (8);
        timeout.setBounds (row1.removeFromRight (80).reduced (0, 4));
        timeoutLabel.setBounds (row1);
        r.removeFromTop (6);
        auto row2 = r.removeFromTop (34);
        measure.setBounds (row2.removeFromLeft (170).reduced (0, 2));
        row2.removeFromLeft (8);
        reset.setBounds (row2.removeFromLeft (170).reduced (0, 2));
        row2.removeFromLeft (8);
        openLog.setBounds (row2.removeFromLeft (150).reduced (0, 2));
    }

private:
    void timerCallback() override { repaint(); }

    BridgeProcessor& proc;
    juce::TextButton modeA { T ("A: 같은 블록 (기다림)") }, modeB { T ("B: 한 블록 늦게") };
    juce::ComboBox timeout;
    juce::Label timeoutLabel;
    juce::TextButton measure { T ("지연 측정 (클릭)") }, reset { T ("숫자 초기화") }, openLog { T ("기록 파일 보기") };
};

juce::AudioProcessorEditor* BridgeProcessor::createEditor() { return new BridgeEditor (*this); }

} // namespace

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BridgeProcessor(); }
