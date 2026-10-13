#include "EngineLink.h"
#include "../util/Hangul.h"

namespace meon::core
{

using juce::String;

namespace
{
    // 미터 소스의 피크 유지 시간: 화면 프레임보다 조금 길게 (옛 MeonSession 과 같은 값)
    constexpr int meterHoldMs = 25;
}

//==============================================================================
// 파일 재생 위치를 옮기거나 재생을 시작할 때 소리가 튀지 않게 한다 (옛 MeonSession 에서 옮김).
// AudioTransportSource 의 gain 은 엔진이 쓰지 않으므로(엔진은 채널 그룹 gain 을 쓴다) 여기서 페이드용으로 쓴다.
// 5 ms 마다 gain 을 조금씩 바꾸고, 엔진은 바뀔 때마다 그 블록 안에서 램프한다.
// 위치 이동: 약 35 ms 에 걸쳐 줄임 → 위치 이동 → 80 ms 기다림(읽기 버퍼가 차는 시간) → 약 50 ms 에 걸쳐 키움.
class EngineLink::PlaybackFader : private juce::Timer
{
public:
    explicit PlaybackFader (juce::AudioTransportSource& t) : transport (t) {}
    ~PlaybackFader() override { finish(); }

    void seek (double seconds)
    {
        if (! transport.isPlaying())
        {
            finish();
            transport.setPosition (seconds);
            return;
        }
        pendingSeek = true;
        target = seconds;
        desired = 0.0f;
        holdTicks = 0;
        startTimer (tickMs);
    }

    /** 0 에서 시작해서 버퍼가 찬 뒤 키운다 */
    void start()
    {
        finish();
        setGain (0.0f);
        transport.start();
        desired = 0.0f;
        holdTicks = 80 / tickMs;
        startTimer (tickMs);
    }

    /** 남은 위치 이동은 바로 적용하고 소리를 원래대로 */
    void finish()
    {
        stopTimer();
        if (pendingSeek)
            transport.setPosition (target);
        pendingSeek = false;
        holdTicks = 0;
        desired = 1.0f;
        setGain (1.0f);
    }

    bool hasPendingSeek (double& seconds) const
    {
        if (! pendingSeek) return false;
        seconds = target;
        return true;
    }

private:
    static constexpr int tickMs = 5;
    juce::AudioTransportSource& transport;
    bool pendingSeek = false;
    double target = 0.0;
    float current = 1.0f, desired = 1.0f;
    int holdTicks = 0, silentTicks = 0;

    void setGain (float g)
    {
        current = g;
        transport.setGain (g);
    }

    void timerCallback() override
    {
        if (current > desired)
        {
            setGain (juce::jmax (desired, current - 0.15f));
            silentTicks = 0;
            return;
        }
        if (pendingSeek)
        {
            if (++silentTicks < 2)   // 0 이 된 블록이 실제로 나간 뒤에 옮긴다
                return;
            transport.setPosition (target);
            pendingSeek = false;
            holdTicks = 80 / tickMs;
            return;
        }
        if (holdTicks > 0)
        {
            if (--holdTicks == 0) desired = 1.0f;
            return;
        }
        if (current < desired)
        {
            setGain (juce::jmin (desired, current + 0.1f));
            return;
        }
        stopTimer();
    }
};

EngineLink::EngineLink (SonobusAudioProcessor& p, bool isPlugin) : processor (p), plugin (isPlugin)
{
    processor.addClientListener (this);
    fader = std::make_unique<PlaybackFader> (processor.getTransportSource());
}

EngineLink::~EngineLink()
{
    processor.removeClientListener (this);
    cancelPendingUpdate();
    fader.reset();   // 줄여 둔 소리가 있으면 원래대로 (플러그인은 창을 닫아도 재생이 이어진다)
}

//==============================================================================
bool EngineLink::loadFile (const juce::File& file)
{
    fader->finish();
    if (! processor.loadURLIntoTransport (juce::URL (file)))
        return false;
    loadedFile = file;
    return true;
}

void EngineLink::closeFile()
{
    if (! hasFile()) return;
    fader->finish();
    processor.clearTransportURL();
    loadedFile = juce::File();
}

bool EngineLink::hasFile() const
{
    return ! processor.getCurrentLoadedTransportURL().isEmpty();
}

String EngineLink::fileName() const
{
    auto url = processor.getCurrentLoadedTransportURL();
    if (url.isEmpty()) return {};
    String name;
    if (loadedFile != juce::File() && url == juce::URL (loadedFile))
        name = loadedFile.getFileName();
    else
        name = url.isLocalFile() ? url.getLocalFile().getFileName() : juce::URL::removeEscapeChars (url.getFileName());
    return composeHangul (name);
}

bool EngineLink::isFilePlaying() const
{
    return const_cast<SonobusAudioProcessor&> (processor).getTransportSource().isPlaying();
}

void EngineLink::setFilePlaying (bool play)
{
    if (! hasFile() || play == isFilePlaying()) return;
    auto& transport = processor.getTransportSource();
    if (play)
    {
        if (transport.getCurrentPosition() >= transport.getLengthInSeconds())
            transport.setPosition (0.0);
        fader->start();
    }
    else
    {
        fader->finish();
        transport.stop();   // 엔진 쪽에서 마지막 블록을 짧게 줄여 끝낸다
    }
}

double EngineLink::filePosition() const
{
    double pending = 0.0;
    if (fader->hasPendingSeek (pending)) return pending;
    return juce::jmax (0.0, const_cast<SonobusAudioProcessor&> (processor).getTransportSource().getCurrentPosition());
}

double EngineLink::fileLength() const
{
    return hasFile() ? juce::jmax (0.0, const_cast<SonobusAudioProcessor&> (processor).getTransportSource().getLengthInSeconds()) : 0.0;
}

void EngineLink::setFilePosition (double seconds)
{
    if (hasFile()) fader->seek (juce::jlimit (0.0, fileLength(), seconds));
}

// 볼륨은 엔진(파일 재생 채널 그룹 gain)에만 둔다. 플러그인 창을 다시 열어도 그대로.
float EngineLink::fileGainDb() const
{
    const float gain = processor.getFilePlaybackGain();
    return gain <= 0.0f ? fileMinDb : juce::jlimit (fileMinDb, fileMaxDb, juce::Decibels::gainToDecibels (gain));
}

void EngineLink::setFileGainDb (float db)
{
    db = juce::jlimit (fileMinDb, fileMaxDb, db);
    processor.setFilePlaybackGain (juce::Decibels::decibelsToGain (db));
}

void EngineLink::setParam (const String& id, float value)
{
    if (auto* param = processor.getValueTreeState().getParameter (id))
        param->setValueNotifyingHost (param->convertTo0to1 (value));
}

void EngineLink::applyDefaults()
{
    // 코덱: 무압축 PCM 16bit 고정 (기획 결정)
    pcmFormatIndex = -1;
    for (int i = 0; i < processor.getNumberAudioCodecFormats(); ++i)
    {
        SonobusAudioProcessor::AudioCodecFormatInfo info;
        if (processor.getAudioCodeFormatInfo (i, info) && info.codec == SonobusAudioProcessor::CodecPCM && info.bitdepth == 2)
        {
            pcmFormatIndex = i;
            break;
        }
    }
    if (pcmFormatIndex >= 0)
    {
        processor.setChangingDefaultAudioCodecSetsExisting (true);
        processor.setDefaultAudioCodecFormat (pcmFormatIndex);
    }

    // 이펙트 전부 끔
    for (int g = 0; g < processor.getInputGroupCount(); ++g)
    {
        SonoAudio::CompressorParams cp;
        if (processor.getInputCompressorParams (g, cp)) { cp.enabled = false; processor.setInputCompressorParams (g, cp); }
        SonoAudio::CompressorParams ep;
        if (processor.getInputExpanderParams (g, ep)) { ep.enabled = false; processor.setInputExpanderParams (g, ep); }
        SonoAudio::CompressorParams lp;
        if (processor.getInputLimiterParams (g, lp)) { lp.enabled = false; processor.setInputLimiterParams (g, lp); }
        SonoAudio::ParametricEqParams eq;
        if (processor.getInputEqParams (g, eq)) { eq.enabled = false; processor.setInputEqParams (g, eq); }
    }

    setParam (SonobusAudioProcessor::paramMainReverbEnabled, 0.0f);
    setParam (SonobusAudioProcessor::paramMainRecvMute, 0.0f);
    setParam (SonobusAudioProcessor::paramMainInMute, 0.0f);
    setParam (SonobusAudioProcessor::paramMetEnabled, 0.0f);
    setParam (SonobusAudioProcessor::paramSendMetAudio, 0.0f);
    setParam (SonobusAudioProcessor::paramSendFileAudio, 1.0f);   // 파일 재생(MR)은 송신에 섞는다
    setParam (SonobusAudioProcessor::paramSendSoundboardAudio, 0.0f);
    setParam (SonobusAudioProcessor::paramHearLatencyTest, 0.0f);
    // 송출 정지(paramMainSendMute)는 덮지 않는다 (버그 #1)

    processor.setAutoconnectToGroupPeers (true);
    processor.setReconnectAfterServerLoss (true);
    processor.setWatchPublicGroups (false);

    processor.getInputMeterSource().setMaxHoldMS (meterHoldMs);
    processor.getPostInputMeterSource().setMaxHoldMS (meterHoldMs);
    processor.getSendMeterSource().setMaxHoldMS (meterHoldMs);
    processor.getOutputMeterSource().setMaxHoldMS (meterHoldMs);
}

//==============================================================================
bool EngineLink::connect (const String& host, int port, const String& user, const String& password)
{
    processor.setCurrentUsername (user);
    return processor.connectToServer (host, port, user, password);
}

void EngineLink::disconnect()          { if (processor.isConnectedToServer()) processor.disconnectFromServer(); }
bool EngineLink::isConnected() const   { return processor.isConnectedToServer(); }
String EngineLink::connectedUser() const { return processor.getCurrentUsername(); }
String EngineLink::joinedGroup() const { return processor.getCurrentJoinedGroup(); }

bool EngineLink::joinGroup (const String& name, const String& password)
{
    return processor.joinServerGroup (name, password, false);
}

void EngineLink::leaveGroup (const String& name)
{
    if (processor.getCurrentJoinedGroup() == name) processor.leaveServerGroup (name);
}

void EngineLink::setAllowedPeers (const juce::StringArray& users, bool enabled)
{
    processor.setAllowedPeerNames (users, enabled);
}

//==============================================================================
int EngineLink::indexOf (const String& user) const
{
    const int n = processor.getNumberRemotePeers();
    for (int i = 0; i < n; ++i)
        if (processor.getRemotePeerUserName (i) == user) return i;
    return -1;
}

std::vector<EnginePeer> EngineLink::peers() const
{
    std::vector<EnginePeer> out;
    const int n = processor.getNumberRemotePeers();
    for (int i = 0; i < n; ++i)
    {
        EnginePeer p;
        p.user = processor.getRemotePeerUserName (i);
        p.connected = processor.getRemotePeerConnected (i);
        p.recvActive = processor.getRemotePeerRecvActive (i);
        p.packetsReceived = processor.getRemotePeerPacketsReceived (i);
        p.packetsDropped = processor.getRemotePeerPacketsDropped (i);
        p.packetsResent = processor.getRemotePeerPacketsResent (i);
        SonobusAudioProcessor::LatencyInfo li;
        if (processor.getRemotePeerLatencyInfo (i, li))
        {
            p.pingMs = li.pingMs;
            p.roundtripMs = li.totalRoundtripMs;
        }
        p.bufferMs = processor.getRemotePeerBufferTime (i);
        if (auto* src = processor.getRemotePeerRecvMeterSource (i)) p.recvChannels = src->getNumChannels();
        out.push_back (p);
    }
    return out;
}

void EngineLink::removePeer (const String& user)
{
    const int i = indexOf (user);
    if (i >= 0) processor.removeRemotePeer (i);
}

void EngineLink::setPeerReceive (const String& user, bool on)
{
    const int i = indexOf (user);
    if (i >= 0) processor.setRemotePeerRecvActive (i, on);
}

void EngineLink::setPeerGainDb (const String& user, float db)
{
    const int i = indexOf (user);
    if (i >= 0) processor.setRemotePeerLevelGain (i, juce::Decibels::decibelsToGain (db, -60.0f));
}

void EngineLink::setPeerPan (const String& user, int pan)
{
    // 엔진의 채널 그룹 패닝 (옛 MeonSession 과 같은 방식: 모노는 위치 하나, 스테레오는 L/R 을 함께 민다)
    const int i = indexOf (user);
    if (i < 0) return;
    const float p = juce::jlimit (-1.0f, 1.0f, (float) pan / 100.0f);
    const int groups = processor.getRemotePeerChannelGroupCount (i);
    for (int g = 0; g < groups; ++g)
    {
        int start = 0, count = 0;
        if (! processor.getRemotePeerChannelGroupStartAndCount (i, g, start, count)) continue;
        const bool stereo = count == 2;
        for (int c = 0; c < count; ++c)
            processor.setRemotePeerChannelPan (i, g, c, stereo ? juce::jlimit (-1.0f, 1.0f, (c == 0 ? -1.0f : 1.0f) + 2.0f * p) : p);
    }
}

float EngineLink::meter (foleys::LevelMeterSource& src, int channel)
{
    if (src.getNumChannels() == 0) return 0.0f;
    if (channel >= 0 && channel < src.getNumChannels()) return src.getMaxLevel (channel);
    float m = 0.0f;
    for (int c = 0; c < src.getNumChannels(); ++c) m = juce::jmax (m, src.getMaxLevel (c));
    return m;
}

float EngineLink::peerLevel (const String& user, int channel) const
{
    const int i = indexOf (user);
    if (i < 0) return 0.0f;
    if (auto* src = processor.getRemotePeerRecvMeterSource (i))
    {
        if (channel >= src->getNumChannels()) return 0.0f;
        return meter (*src, channel);
    }
    return 0.0f;
}

void EngineLink::applyPeerDefaults (const String& user, BufferMode mode)
{
    const int i = indexOf (user);
    if (i < 0) return;
    if (pcmFormatIndex >= 0) processor.setRemotePeerAudioCodecFormat (i, pcmFormatIndex);
    processor.setRemotePeerAutoresizeBufferMode (i, mode == BufferMode::latency ? SonobusAudioProcessor::AutoNetBufferModeAutoFull
                                                                                : SonobusAudioProcessor::AutoNetBufferModeAutoIncreaseOnly);
    if (auto* src = processor.getRemotePeerRecvMeterSource (i)) src->setMaxHoldMS (meterHoldMs);
}

//==============================================================================
bool EngineLink::isSendMuted() const
{
    if (auto* param = processor.getValueTreeState().getParameter (SonobusAudioProcessor::paramMainSendMute))
        return param->getValue() > 0.5f;
    return false;
}

void EngineLink::setSendMuted (bool b)               { setParam (SonobusAudioProcessor::paramMainSendMute, b ? 1.0f : 0.0f); }
void EngineLink::setSendGainDb (float db)            { setParam (SonobusAudioProcessor::paramInGain, juce::Decibels::decibelsToGain (db)); }
void EngineLink::setSelfMonitor (bool on)            { selfMonitor = on; applyMonitorParams(); }
void EngineLink::setSelfMonitorDb (float db)         { selfMonitorDb = db; applyMonitorParams(); }
void EngineLink::setEarGuardLevel (int l)            { processor.getEarGuard().setLevel ((meon::EarGuard::Level) juce::jlimit (0, 2, l)); }
void EngineLink::setMasterDb (float db)              { masterDb = db; applyMonitorParams(); }
void EngineLink::setMonitorMuted (bool m)            { monitorMuted = m; applyMonitorParams(); }

void EngineLink::applyMonitorParams()
{
    // 모니터 전체 음소거 = 출력(wet) 0 + 셀프 모니터(dry) 0 (Q12). 마스터는 −30 ~ +6 dB (Q10, wet 최대 2.0)
    const float wet = monitorMuted ? 0.0f : juce::jlimit (0.0f, 2.0f, juce::Decibels::decibelsToGain (masterDb));
    setParam (SonobusAudioProcessor::paramWet, wet);
    setParam (SonobusAudioProcessor::paramDry, (monitorMuted || ! selfMonitor) ? 0.0f
                                                                               : juce::jlimit (0.0f, 2.0f, juce::Decibels::decibelsToGain (selfMonitorDb)));
}

void EngineLink::setBufferMode (BufferMode m)
{
    processor.setDefaultAutoresizeBufferMode (m == BufferMode::latency ? SonobusAudioProcessor::AutoNetBufferModeAutoFull
                                                                       : SonobusAudioProcessor::AutoNetBufferModeAutoIncreaseOnly);
}

void EngineLink::setInputChannels (int start, int count)
{
    count = juce::jlimit (1, 2, count);
    if (plugin)
    {
        // 플러그인: 입력은 DAW 버스 그대로, 보내는 채널만 모노/스테레오로 고정 (버그 #13, "Match # Inputs" 를 쓰지 않음)
        setParam (SonobusAudioProcessor::paramSendChannels, (float) count);
        return;
    }
    processor.setInputGroupCount (1);
    processor.setInputGroupChannelStartAndCount (0, juce::jmax (0, start), count);
    processor.setInputGroupChannelDestStartAndCount (0, 0, 2);
    processor.setInputGroupMuted (0, false);
    setParam (SonobusAudioProcessor::paramSendChannels, (float) count);   // 1 = 모노, 2 = 스테레오
}

int EngineLink::inputChannelCount() const
{
    if (plugin) return processor.getMainBusNumInputChannels();
    int start = 0, count = 0;
    if (processor.getInputGroupCount() < 1 || ! const_cast<SonobusAudioProcessor&> (processor).getInputGroupChannelStartAndCount (0, start, count))
        return 0;
    if (deviceManager)
        if (auto* dm = deviceManager())
            if (auto* dev = dm->getCurrentAudioDevice())
                if (dev->getActiveInputChannels().countNumberOfSetBits() == 0) return 0;
    return count;
}

String EngineLink::inputChannelText() const
{
    if (plugin) return String (processor.getMainBusNumInputChannels()) + " (bus)";
    int start = 0, count = 0;
    if (processor.getInputGroupCount() < 1 || ! const_cast<SonobusAudioProcessor&> (processor).getInputGroupChannelStartAndCount (0, start, count))
        return "none";
    return count >= 2 ? String (start + 1) + "-" + String (start + count) : String (start + 1);
}

float EngineLink::inputLevel (int ch) const   { return meter (processor.getPostInputMeterSource(), ch); }
float EngineLink::sendLevel (int ch) const    { return isSendMuted() ? 0.0f : meter (processor.getSendMeterSource(), ch); }
float EngineLink::monitorLevel (int ch) const { return meter (processor.getOutputMeterSource(), ch); }
int EngineLink::sendChannelCount() const      { return juce::jmax (1, processor.getSendChannels()); }

String EngineLink::audioDescription() const
{
    if (plugin) return "plugin";
    if (deviceManager)
        if (auto* dm = deviceManager())
            if (auto* dev = dm->getCurrentAudioDevice())
                return dev->getTypeName() + " / " + dev->getName() + " / " + String (dev->getCurrentSampleRate(), 0) + " Hz / "
                       + String (dev->getCurrentBufferSizeSamples());
    return "unknown";
}

bool EngineLink::sendChat (const EngineChat& c)
{
    SBChatEvent ev;
    ev.type = SBChatEvent::UserType;
    ev.group = c.group;
    ev.from = c.from;
    ev.tags = c.tags;
    ev.message = c.message;
    return processor.sendChatEvent (ev);
}

//==============================================================================
// 네트워크 스레드 → 메시지 스레드

void EngineLink::push (Event e)
{
    {
        const juce::ScopedLock sl (lock);
        pending.push_back (std::move (e));
    }
    triggerAsyncUpdate();
}

void EngineLink::handleAsyncUpdate()
{
    std::vector<Event> events;
    {
        const juce::ScopedLock sl (lock);
        events.swap (pending);
    }
    for (auto& e : events)
    {
        switch (e.type)
        {
            case Event::connected:      listeners.call ([&] (Listener& l) { l.engineConnected (e.ok, e.message); }); break;
            case Event::disconnected:   listeners.call ([&] (Listener& l) { l.engineDisconnected (e.message); }); break;
            case Event::groupJoined:    listeners.call ([&] (Listener& l) { l.engineGroupJoined (e.ok, e.group, e.message); }); break;
            case Event::groupLeft:      listeners.call ([&] (Listener& l) { l.engineGroupLeft (e.group); }); break;
            case Event::peerJoined:     listeners.call ([&] (Listener& l) { l.enginePeerJoined (e.user); }); break;
            case Event::peerJoinFailed: listeners.call ([&] (Listener& l) { l.enginePeerJoinFailed (e.user); }); break;
            case Event::peerLeft:       listeners.call ([&] (Listener& l) { l.enginePeerLeft (e.user); }); break;
            case Event::chat:           listeners.call ([&] (Listener& l) { l.engineChat (e.chatEvent); }); break;
        }
    }
}

void EngineLink::aooClientConnected (SonobusAudioProcessor*, bool ok, const String& m)       { push ({ Event::connected, ok, {}, {}, m, {} }); }
void EngineLink::aooClientDisconnected (SonobusAudioProcessor*, bool ok, const String& m)    { push ({ Event::disconnected, ok, {}, {}, m, {} }); }
void EngineLink::aooClientGroupJoined (SonobusAudioProcessor*, bool ok, const String& g, const String& m) { push ({ Event::groupJoined, ok, g, {}, m, {} }); }
void EngineLink::aooClientGroupLeft (SonobusAudioProcessor*, bool ok, const String& g, const String& m)   { push ({ Event::groupLeft, ok, g, {}, m, {} }); }
void EngineLink::aooClientPeerJoined (SonobusAudioProcessor*, const String& g, const String& u)     { push ({ Event::peerJoined, true, g, u, {}, {} }); }
void EngineLink::aooClientPeerJoinFailed (SonobusAudioProcessor*, const String& g, const String& u) { push ({ Event::peerJoinFailed, false, g, u, {}, {} }); }
void EngineLink::aooClientPeerLeft (SonobusAudioProcessor*, const String& g, const String& u)       { push ({ Event::peerLeft, true, g, u, {}, {} }); }

void EngineLink::sbChatEventReceived (SonobusAudioProcessor*, const SBChatEvent& c)
{
    Event e { Event::chat, true, c.group, c.from, {}, { c.group, c.from, c.tags, c.message } };
    push (e);
}

} // namespace meon::core
