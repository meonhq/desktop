#include "MeonSession.h"

namespace meon
{

// 미터 소스의 피크 유지 시간. 한 화면 프레임(~17 ms)보다 조금 길게 해서 프레임 사이에 지나간 피크도 막대에 잡히게 한다.
// 길면(예: 100 ms) 값이 계단처럼 바뀌어 막대가 덜컥거린다. 하강은 LevelBar 가 부드럽게 처리한다.
static constexpr int kMeterHoldMs = 25;

static const char* kCodeChars = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";   // 혼동 문자(O,0,I,1) 제외

//==============================================================================
// 파일 재생 위치를 옮기거나 재생을 시작할 때 소리가 튀지 않게 한다.
// AudioTransportSource 의 gain 은 엔진이 쓰지 않으므로(엔진은 채널 그룹 gain 을 쓴다) 여기서 페이드용으로 쓴다.
// 5 ms 마다 gain 을 조금씩 바꾸고, 엔진은 바뀔 때마다 그 블록 안에서 램프한다.
// 위치 이동: 약 35 ms 에 걸쳐 줄임 → 위치 이동 → 80 ms 기다림(읽기 버퍼가 차는 시간) → 약 50 ms 에 걸쳐 키움.
class PlaybackFader : private juce::Timer
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
        if (! pendingSeek)
            return false;
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
            if (--holdTicks == 0)
                desired = 1.0f;
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

//==============================================================================
/** 서버까지의 왕복 시간을 TCP 접속 시간으로 어림한다 (참고용). */
class ServerPinger : public juce::Thread
{
public:
    ServerPinger (const juce::String& h, int p) : juce::Thread ("MEON server ping"), host (h), port (p) {}
    ~ServerPinger() override { stopThread (5000); }

    void run() override
    {
        while (! threadShouldExit())
        {
            measure();
            wait (intervalMs.load());
        }
    }

    std::atomic<float> lastMs { -1.0f };
    std::atomic<int> intervalMs { 5000 };

private:
    void measure()
    {
        juce::StreamingSocket sock;
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        if (sock.connect (host, port, 3000))
            lastMs = (float) (juce::Time::getMillisecondCounterHiRes() - t0);
        else
            lastMs = -1.0f;
        sock.close();
    }

    juce::String host;
    int port;
};

//==============================================================================
MeonSession::MeonSession (SonobusAudioProcessor& p, MeonSettings& s, bool plugin)
    : processor (p), settings (s), isPlugin (plugin)
{
    applyProcessorDefaults();
    processor.getEarGuard().setLevel (settings.getEarGuardLevel());   // 다른 인스턴스·앱에서 바꾼 값을 따라간다
    processor.addClientListener (this);
    pinger = std::make_unique<ServerPinger> (DEFAULT_SERVER_HOST, DEFAULT_SERVER_PORT);
    pinger->startThread();

    // 플러그인 창을 닫았다 다시 열면 엔진은 이미 서버·방에 붙어 있다. 그 상태를 이어받는다.
    if (processor.isConnectedToServer())
    {
        serverState = ServerState::Connected;
        everConnected = true;
        wantConnected = true;
        userName = processor.getCurrentUsername();
        const auto group = processor.getCurrentJoinedGroup();
        if (group.isNotEmpty())
        {
            roomCode = group;
            pendingCode = group;
            roomState = RoomState::InRoom;
            joinedAtMs = juce::Time::getMillisecondCounterHiRes() - 60000.0;
            reattached = true;
        }
    }
    fader = std::make_unique<PlaybackFader> (processor.getTransportSource());
    startTimer (200);
}

MeonSession::~MeonSession()
{
    stopTimer();
    fader = nullptr;   // 줄여 둔 소리가 있으면 원래대로 (플러그인은 창을 닫아도 재생이 이어진다)
    processor.removeClientListener (this);
    cancelPendingUpdate();
    if (roomState != RoomState::None)
    {
        if (log.isActive())
            log.end (isPlugin ? "editorClosed" : "quit");
        // 플러그인은 창을 닫아도 합주가 이어진다. 독립 앱은 종료이므로 방을 나간다.
        if (! isPlugin)
            processor.leaveServerGroup (roomCode.isNotEmpty() ? roomCode : pendingCode);
    }
    pinger = nullptr;
}

//==============================================================================
juce::String MeonSession::generateRoomCode()
{
    auto& rng = juce::Random::getSystemRandom();
    juce::String code;
    const int n = (int) strlen (kCodeChars);
    for (int i = 0; i < 6; ++i)
        code += juce::String::charToString ((juce::juce_wchar) kCodeChars[rng.nextInt (n)]);
    return code;
}

bool MeonSession::isValidRoomCode (const juce::String& code)
{
    if (code.length() != 6)
        return false;
    for (int i = 0; i < 6; ++i)
    {
        const juce::juce_wchar c = code[i];
        if (! ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
            return false;
    }
    return true;
}

// 사용자 이름 = 닉네임#XXXX(+파트 한 글자). 파트는 서버·엔진을 거치지 않고 이름에 실어 보낸다.
juce::String MeonSession::displayNameFor (const juce::String& user)
{
    const int hash = user.lastIndexOfChar ('#');
    const int suffixLen = user.length() - hash - 1;
    if (hash > 0 && (suffixLen == 4 || suffixLen == 5))
        return user.substring (0, hash);
    return user;
}

Part MeonSession::partFor (const juce::String& user)
{
    const int hash = user.lastIndexOfChar ('#');
    if (hash > 0 && user.length() - hash - 1 == 5)
        return parts::fromCode (user.getLastCharacter());
    return Part::None;
}

juce::String MeonSession::makeUserName() const
{
    auto& rng = juce::Random::getSystemRandom();
    juce::String suffix;
    const int n = (int) strlen (kCodeChars);
    for (int i = 0; i < 4; ++i)
        suffix += juce::String::charToString ((juce::juce_wchar) kCodeChars[rng.nextInt (n)]);
    const auto code = parts::code (getMyPart());
    if (code != 0)
        suffix += juce::String::charToString (code);
    return settings.getNickname().trim() + "#" + suffix;
}

Part MeonSession::getMyPart() const
{
    return parts::fromKey (settings.getPartKey());
}

juce::String MeonSession::getDisplayName() const
{
    return settings.getNickname().trim();
}

//==============================================================================
void MeonSession::applyProcessorDefaults()
{
    // 코덱: 무압축 PCM 16 bit 고정
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

    // 지터버퍼: 자동 (양방향)
    processor.setDefaultAutoresizeBufferMode (SonobusAudioProcessor::AutoNetBufferModeAutoFull);

    // 이펙트 전부 끔 (엔진 코드는 그대로, 사용만 하지 않음)
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

    auto& state = processor.getValueTreeState();
    auto setParam = [&state] (const juce::String& id, float value)
    {
        if (auto* param = state.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    };
    setParam (SonobusAudioProcessor::paramMainReverbEnabled, 0.0f);
    setParam (SonobusAudioProcessor::paramMainRecvMute, 0.0f);
    setParam (SonobusAudioProcessor::paramMainInMute, 0.0f);
    setParam (SonobusAudioProcessor::paramMetEnabled, 0.0f);
    setParam (SonobusAudioProcessor::paramSendMetAudio, 0.0f);
    setParam (SonobusAudioProcessor::paramSendFileAudio, 1.0f);   // 파일 재생(MR)은 송신에 섞어 보낸다
    setParam (SonobusAudioProcessor::paramSendSoundboardAudio, 0.0f);
    setParam (SonobusAudioProcessor::paramHearLatencyTest, 0.0f);
    setParam (SonobusAudioProcessor::paramMainSendMute, 0.0f);

    // 그룹 피어에 자동 연결, 서버 끊김 시 자동 재접속(엔진 내장)
    processor.setAutoconnectToGroupPeers (true);
    processor.setReconnectAfterServerLoss (true);
    processor.setWatchPublicGroups (false);

    if (! isPlugin)
        setInputChannels (settings.getInputChannelStart(), settings.getInputChannelCount());

    processor.getInputMeterSource().setMaxHoldMS (kMeterHoldMs);
    processor.getSendMeterSource().setMaxHoldMS (kMeterHoldMs);
}

void MeonSession::setInputChannels (int start, int count)
{
    if (isPlugin)
        return;
    count = juce::jlimit (1, 2, count);
    start = juce::jmax (0, start);
    processor.setInputGroupCount (1);
    processor.setInputGroupChannelStartAndCount (0, start, count);
    processor.setInputGroupChannelDestStartAndCount (0, 0, 2);
    processor.setInputGroupMuted (0, false);

    if (auto* param = processor.getValueTreeState().getParameter (SonobusAudioProcessor::paramSendChannels))
        param->setValueNotifyingHost (param->convertTo0to1 ((float) count));   // 1 = mono, 2 = stereo
}

void MeonSession::applyPeerDefaults (int peerIndex, Member& m)
{
    if (pcmFormatIndex >= 0)
        processor.setRemotePeerAudioCodecFormat (peerIndex, pcmFormatIndex);
    processor.setRemotePeerAutoresizeBufferMode (peerIndex, SonobusAudioProcessor::AutoNetBufferModeAutoFull);
    if (auto* src = processor.getRemotePeerRecvMeterSource (peerIndex))
        src->setMaxHoldMS (kMeterHoldMs);
    applyPeerPan (peerIndex, m.pan, false);
    m.defaultsApplied = true;
}

// 엔진의 채널 그룹 패닝을 쓴다 (엔진 고유 팬 법칙: 가운데 -4.5 dB). 모노는 위치 하나, 스테레오는 L/R 위치를 함께 민다.
// 상대 스트림의 채널 수가 바뀌면 엔진이 패닝을 다시 잡을 수 있어 refreshStats 에서 어긋나면 다시 넣는다.
void MeonSession::applyPeerPan (int peerIndex, int pan, bool onlyIfChanged)
{
    const float p = juce::jlimit (-1.0f, 1.0f, (float) pan / 100.0f);
    const int groups = processor.getRemotePeerChannelGroupCount (peerIndex);
    for (int g = 0; g < groups; ++g)
    {
        int start = 0, count = 0;
        if (! processor.getRemotePeerChannelGroupStartAndCount (peerIndex, g, start, count))
            continue;
        const bool stereo = count == 2;
        for (int c = 0; c < count; ++c)
        {
            const float want = stereo ? juce::jlimit (-1.0f, 1.0f, (c == 0 ? -1.0f : 1.0f) + 2.0f * p) : p;
            if (onlyIfChanged && std::abs (processor.getRemotePeerChannelPan (peerIndex, g, c) - want) < 0.0001f)
                continue;
            processor.setRemotePeerChannelPan (peerIndex, g, c, want);
        }
    }
}

//==============================================================================
void MeonSession::start()
{
    if (reattached && roomState == RoomState::InRoom && ! log.isActive())
    {
        beginLog ("editorReopened");
        reconcilePeers (true);
        listeners.call ([] (Listener& l) { l.sessionStateChanged(); l.membersChanged(); });
    }
    if (settings.getNickname().trim().isEmpty())
        return;
    wantConnected = true;
    if (serverState == ServerState::Disconnected)
        connectNow();
}

void MeonSession::applyNicknameChange()
{
    if (roomState != RoomState::None)
        return;   // 합주 중에 바꾸면 다음 입장부터 적용
    if (serverState != ServerState::Disconnected)
    {
        processor.disconnectFromServer();
        serverState = ServerState::Disconnected;
    }
    everConnected = false;
    nextConnectAttemptMs = 0.0;
    start();
}

void MeonSession::connectNow()
{
    if (settings.getNickname().trim().isEmpty())
        return;
    userName = makeUserName();
    processor.setCurrentUsername (userName);
    serverState = ServerState::Connecting;
    connectDeadlineMs = juce::Time::getMillisecondCounterHiRes() + 12000.0;
    if (! processor.connectToServer (DEFAULT_SERVER_HOST, DEFAULT_SERVER_PORT, userName))
    {
        serverState = ServerState::Disconnected;
        nextConnectAttemptMs = juce::Time::getMillisecondCounterHiRes() + 3000.0;
    }
    listeners.call ([] (Listener& l) { l.sessionStateChanged(); });
}

juce::String MeonSession::getServerStatusText (bool home) const
{
    // 서버는 멤버를 찾아 주기만 한다 (소리는 멤버끼리 직접). 왕복 시간은 합주 지연으로 오해되므로 표시하지 않는다.
    if (isServerConnected())
        return TXT ("서버 연결됨");
    if (isServerLost())
        return home ? TXT ("서버 연결 끊김 · 재연결 중…") : TXT ("서버 재연결 중…");
    return TXT ("서버에 연결 중…");
}

float MeonSession::getServerPingMs() const
{
    return pinger != nullptr ? pinger->lastMs.load() : -1.0f;
}

void MeonSession::setServerPingInterval (int ms)
{
    if (pinger != nullptr)
    {
        pinger->intervalMs = juce::jmax (1000, ms);
        pinger->notify();
    }
}

//==============================================================================
void MeonSession::createRoom()
{
    if (roomState != RoomState::None)
        return;
    lastJoinError.clear();
    pendingCode = generateRoomCode();
    creatingRoom = true;
    joinRequested = false;
    roomState = RoomState::Joining;
    listeners.call ([] (Listener& l) { l.sessionStateChanged(); });
    performPendingRoomAction();
}

void MeonSession::joinRoom (const juce::String& codeIn)
{
    if (roomState != RoomState::None)
        return;
    auto code = codeIn.trim().toUpperCase();
    if (! isValidRoomCode (code))
    {
        listeners.call ([] (Listener& l) { l.joinFailed (JoinFailure::InvalidCode); });
        return;
    }
    lastJoinError.clear();
    pendingCode = code;
    creatingRoom = false;
    joinRequested = false;
    roomState = RoomState::Joining;
    listeners.call ([] (Listener& l) { l.sessionStateChanged(); });
    performPendingRoomAction();
}

void MeonSession::performPendingRoomAction()
{
    if (roomState != RoomState::Joining || pendingCode.isEmpty() || joinRequested)
        return;
    if (serverState != ServerState::Connected)
    {
        start();
        return;   // 연결되면 aooClientConnected 에서 다시 시도
    }
    joinRequested = true;
    if (! processor.joinServerGroup (pendingCode, "", false))
        finishJoin (false, JoinFailure::Error, "join request failed");
}

void MeonSession::finishJoin (bool success, JoinFailure failure, const juce::String& message)
{
    if (success)
    {
        roomCode = pendingCode;
        roomState = RoomState::InRoom;
        joinedAtMs = juce::Time::getMillisecondCounterHiRes();

        AooServerConnectionInfo info;
        info.userName = userName;
        info.groupName = roomCode;
        info.groupIsPublic = false;
        info.serverHost = DEFAULT_SERVER_HOST;
        info.serverPort = DEFAULT_SERVER_PORT;
        info.timestamp = juce::Time::getCurrentTime().toMilliseconds();
        processor.addRecentServerConnectionInfo (info);

        beginLog (creatingRoom ? "roomCreated" : "roomJoined");
        lastSampleMs = 0.0;

        listeners.call ([] (Listener& l) { l.sessionStateChanged(); l.roomJoined(); });
    }
    else
    {
        lastJoinError = message;
        if (roomState != RoomState::None && (roomState == RoomState::Verifying || roomState == RoomState::InRoom))
            processor.leaveServerGroup (pendingCode);
        roomState = RoomState::None;
        pendingCode.clear();
        roomCode.clear();
        members.clear();
        listeners.call ([failure] (Listener& l) { l.sessionStateChanged(); l.membersChanged(); l.joinFailed (failure); });
    }
}

void MeonSession::leaveRoom()
{
    if (roomState == RoomState::None)
        return;
    const auto code = roomCode.isNotEmpty() ? roomCode : pendingCode;
    processor.leaveServerGroup (code);
    clearRoom ("leave");
}

void MeonSession::clearRoom (const juce::String& reason)
{
    closePlaybackFile();   // 방을 나가면 MR 도 닫는다 (다음 방에서 바로 소리가 나가지 않게)
    if (log.isActive())
    {
        log.addEvent ("leave", reason);
        log.end (reason);
    }
    roomState = RoomState::None;
    roomCode.clear();
    pendingCode.clear();
    creatingRoom = false;
    members.clear();
    chat.clear();
    unread = 0;
    listeners.call ([] (Listener& l) { l.sessionStateChanged(); l.membersChanged(); l.chatChanged(); l.roomLeft(); });
}

//==============================================================================
int MeonSession::getMemberCount() const
{
    return 1 + (int) members.size();
}

std::vector<MeonSession::Member> MeonSession::getMembers() const
{
    std::vector<Member> out;
    for (auto& m : members)
        if (m.slot >= 0)
            out.push_back (m);
    std::sort (out.begin(), out.end(), [] (const Member& a, const Member& b) { return a.slot < b.slot; });
    return out;
}

const MeonSession::Member* MeonSession::getMemberInSlot (int slot) const
{
    for (auto& m : members)
        if (m.slot == slot)
            return &m;
    return nullptr;
}

MeonSession::Member* MeonSession::findMember (const juce::String& user)
{
    for (auto& m : members)
        if (m.userName == user)
            return &m;
    return nullptr;
}

int MeonSession::findPeerIndex (const juce::String& user) const
{
    const int n = processor.getNumberRemotePeers();
    for (int i = 0; i < n; ++i)
        if (processor.getRemotePeerUserName (i) == user)
            return i;
    return -1;
}

int MeonSession::firstFreeSlot() const
{
    for (int s = 0; s < metric::maxOthers; ++s)
        if (getMemberInSlot (s) == nullptr)
            return s;
    return -1;
}

void MeonSession::setMemberMuted (int slot, bool muted)
{
    if (auto* m = const_cast<Member*> (getMemberInSlot (slot)))
    {
        const int idx = findPeerIndex (m->userName);
        if (idx >= 0)
            processor.setRemotePeerRecvActive (idx, ! muted);
        m->muted = muted;
        listeners.call ([] (Listener& l) { l.memberStatsChanged(); });
    }
}

void MeonSession::setMemberGain (int slot, float gain)
{
    if (auto* m = const_cast<Member*> (getMemberInSlot (slot)))
    {
        const int idx = findPeerIndex (m->userName);
        if (idx >= 0)
            processor.setRemotePeerLevelGain (idx, gain);
        m->gain = gain;
    }
}

int MeonSession::readPeerPan (int peerIndex) const
{
    int start = 0, count = 0;
    if (processor.getRemotePeerChannelGroupCount (peerIndex) < 1
        || ! processor.getRemotePeerChannelGroupStartAndCount (peerIndex, 0, start, count))
        return 0;
    float p = processor.getRemotePeerChannelPan (peerIndex, 0, 0);
    if (count == 2)
    {
        // applyPeerPan 의 역: L = -1 + 2p (p > 0), R = 1 + 2p (p < 0)
        const float r = processor.getRemotePeerChannelPan (peerIndex, 0, 1);
        p = p > -1.0f + 0.0001f ? (p + 1.0f) * 0.5f : (r - 1.0f) * 0.5f;
    }
    return juce::jlimit (-100, 100, (int) std::lround (p * 100.0f));
}

void MeonSession::setMemberPan (int slot, int pan)
{
    if (auto* m = const_cast<Member*> (getMemberInSlot (slot)))
    {
        m->pan = juce::jlimit (-100, 100, pan);
        const int idx = findPeerIndex (m->userName);
        if (idx >= 0)
            applyPeerPan (idx, m->pan, false);
    }
}

float MeonSession::meterDb (foleys::LevelMeterSource* src, int channel) const
{
    if (src == nullptr || src->getNumChannels() == 0)
        return -100.0f;
    float lin = 0.0f;
    if (channel >= 0 && channel < src->getNumChannels())
        lin = src->getMaxLevel (channel);
    else
        for (int c = 0; c < src->getNumChannels(); ++c)
            lin = juce::jmax (lin, src->getMaxLevel (c));
    return juce::Decibels::gainToDecibels (lin, -100.0f);
}

float MeonSession::getMemberLevelDb (int slot) const
{
    if (auto* m = getMemberInSlot (slot))
    {
        if (! m->connected || m->muted)
            return -100.0f;
        const int idx = findPeerIndex (m->userName);
        if (idx >= 0)
            return meterDb (processor.getRemotePeerRecvMeterSource (idx));
    }
    return -100.0f;
}

float MeonSession::getMemberPeakLevelDb (int slot) const
{
    return getMemberLevelDb (slot);
}

//==============================================================================
bool MeonSession::isMyMuted() const
{
    if (auto* param = processor.getValueTreeState().getParameter (SonobusAudioProcessor::paramMainSendMute))
        return param->getValue() > 0.5f;
    return false;
}

void MeonSession::beginLog (const juce::String& firstEvent)
{
    MeonSessionLog::AudioInfo audio;
    if (audioInfoProvider)
        audio = audioInfoProvider();
    log.begin (roomCode, userName, isPlugin, audio);
    log.addEvent (firstEvent, roomCode);
    auto& guard = processor.getEarGuard();
    earGuardBandBase = guard.getBandTriggerCount();
    earGuardFullBase = guard.getFullTriggerCount();
    log.addEvent ("earGuard", EarGuard::levelKey (guard.getLevel()));
}

EarGuard::Level MeonSession::getEarGuardLevel() const
{
    return processor.getEarGuard().getLevel();
}

void MeonSession::setEarGuardLevel (EarGuard::Level level)
{
    if (level == processor.getEarGuard().getLevel() && level == settings.getEarGuardLevel())
        return;
    settings.setEarGuardLevel (level);
    processor.getEarGuard().setLevel (level);
    if (log.isActive())
        log.addEvent ("earGuard", EarGuard::levelKey (level));
}

void MeonSession::setMyMuted (bool muted)
{
    if (auto* param = processor.getValueTreeState().getParameter (SonobusAudioProcessor::paramMainSendMute))
        param->setValueNotifyingHost (muted ? 1.0f : 0.0f);
    if (log.isActive())
        log.addEvent (muted ? "selfMute" : "selfUnmute", "");
    listeners.call ([] (Listener& l) { l.memberStatsChanged(); });
}

//==============================================================================
// 한글 자모 조합 (유니코드 표준 알고리즘): 초성 U+1100..1112 + 중성 U+1161..1175 [+ 종성 U+11A8..11C2] → U+AC00..D7A3.
// 완성형 음절 + 종성도 합친다. 그 밖의 글자는 그대로.
juce::String MeonSession::composeHangul (const juce::String& text)
{
    constexpr juce::juce_wchar sBase = 0xAC00, lBase = 0x1100, vBase = 0x1161, tBase = 0x11A7;
    constexpr int lCount = 19, vCount = 21, tCount = 28, nCount = vCount * tCount, sCount = lCount * nCount;
    juce::Array<juce::juce_wchar> out;
    for (auto p = text.getCharPointer(); ! p.isEmpty();)
    {
        const juce::juce_wchar c = p.getAndAdvance();
        if (! out.isEmpty())
        {
            const juce::juce_wchar last = out.getLast();
            const int l = (int) (last - lBase), v = (int) (c - vBase);
            if (l >= 0 && l < lCount && v >= 0 && v < vCount)
            {
                out.set (out.size() - 1, (juce::juce_wchar) (sBase + (l * vCount + v) * tCount));
                continue;
            }
            const int si = (int) (last - sBase), t = (int) (c - tBase);
            if (si >= 0 && si < sCount && si % tCount == 0 && t > 0 && t < tCount)
            {
                out.set (out.size() - 1, last + (juce::juce_wchar) t);
                continue;
            }
        }
        out.add (c);
    }
    out.add (0);
    return juce::String (juce::CharPointer_UTF32 ((const juce::CharPointer_UTF32::CharType*) out.getRawDataPointer()));
}

//==============================================================================
bool MeonSession::loadPlaybackFile (const juce::File& file)
{
    fader->finish();
    if (! processor.loadURLIntoTransport (juce::URL (file)))
        return false;
    playbackFile = file;
    if (log.isActive())
        log.addEvent ("fileLoaded", file.getFileName() + " (" + juce::String (getPlaybackLength(), 1) + " s)");
    listeners.call ([] (Listener& l) { l.playbackChanged(); });
    return true;
}

void MeonSession::closePlaybackFile()
{
    if (! hasPlaybackFile())
        return;
    fader->finish();
    processor.clearTransportURL();
    playbackFile = juce::File();
    if (log.isActive())
        log.addEvent ("fileClosed", "");
    listeners.call ([] (Listener& l) { l.playbackChanged(); });
}

bool MeonSession::hasPlaybackFile() const
{
    return ! processor.getCurrentLoadedTransportURL().isEmpty();
}

juce::String MeonSession::getPlaybackFileName() const
{
    auto url = processor.getCurrentLoadedTransportURL();
    if (url.isEmpty())
        return {};
    juce::String name;
    if (playbackFile != juce::File() && url == juce::URL (playbackFile))
        name = playbackFile.getFileName();
    else
        name = url.isLocalFile() ? url.getLocalFile().getFileName() : juce::URL::removeEscapeChars (url.getFileName());
    return composeHangul (name);
}

bool MeonSession::isPlaybackPlaying() const
{
    return processor.getTransportSource().isPlaying();
}

void MeonSession::setPlaybackPlaying (bool play)
{
    if (! hasPlaybackFile() || play == isPlaybackPlaying())
        return;
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
    if (log.isActive())
        log.addEvent (play ? "filePlay" : "fileStop", juce::String (getPlaybackPosition(), 1) + " s");
    listeners.call ([] (Listener& l) { l.playbackChanged(); });
}

double MeonSession::getPlaybackPosition() const
{
    double pending = 0.0;
    if (fader->hasPendingSeek (pending))
        return pending;
    return juce::jmax (0.0, processor.getTransportSource().getCurrentPosition());
}

double MeonSession::getPlaybackLength() const
{
    return hasPlaybackFile() ? juce::jmax (0.0, processor.getTransportSource().getLengthInSeconds()) : 0.0;
}

void MeonSession::setPlaybackPosition (double seconds)
{
    if (hasPlaybackFile())
        fader->seek (juce::jlimit (0.0, getPlaybackLength(), seconds));
}

// 볼륨은 엔진(파일 재생 채널 그룹 gain)에만 둔다. 플러그인 창을 다시 열어도 그대로.
float MeonSession::getPlaybackGainDb() const
{
    const float gain = processor.getFilePlaybackGain();
    return gain <= 0.0f ? playbackMinDb : juce::jlimit (playbackMinDb, playbackMaxDb, juce::Decibels::gainToDecibels (gain));
}

void MeonSession::setPlaybackGainDb (float db)
{
    db = juce::jlimit (playbackMinDb, playbackMaxDb, db);
    processor.setFilePlaybackGain (db <= playbackMinDb ? 0.0f : juce::Decibels::decibelsToGain (db));
}

float MeonSession::getMyInputLevelDb (int channel) const
{
    return meterDb (&processor.getInputMeterSource(), channel);
}

float MeonSession::getMySendLevelDb() const
{
    if (isMyMuted())
        return -100.0f;
    return meterDb (&processor.getSendMeterSource());
}

//==============================================================================
void MeonSession::sendChat (const juce::String& textIn)
{
    auto text = textIn.trim();
    if (text.isEmpty() || roomState != RoomState::InRoom)
        return;
    SBChatEvent ev;
    ev.type = SBChatEvent::UserType;
    ev.from = userName;
    ev.group = roomCode;
    ev.message = text;
    processor.sendChatEvent (ev);

    ChatMessage m;
    m.kind = ChatMessage::Mine;
    m.from = getDisplayName();
    m.text = text;
    m.time = juce::Time::getCurrentTime();
    chat.push_back (m);
    listeners.call ([] (Listener& l) { l.chatChanged(); });
}

void MeonSession::addSystemChat (const juce::String& text)
{
    ChatMessage m;
    m.kind = ChatMessage::System;
    m.text = text;
    m.time = juce::Time::getCurrentTime();
    chat.push_back (m);
    listeners.call ([] (Listener& l) { l.chatChanged(); });
}

//==============================================================================
// ClientListener (네트워크 스레드) → 큐에 넣고 메시지 스레드에서 처리
void MeonSession::pushEvent (Event e)
{
    {
        const juce::ScopedLock sl (eventLock);
        pendingEvents.push_back (std::move (e));
    }
    triggerAsyncUpdate();
}

void MeonSession::aooClientConnected (SonobusAudioProcessor*, bool success, const juce::String& errmesg)
{
    Event e; e.type = Event::Connected; e.success = success; e.message = errmesg; pushEvent (e);
}
void MeonSession::aooClientDisconnected (SonobusAudioProcessor*, bool success, const juce::String& errmesg)
{
    Event e; e.type = Event::Disconnected; e.success = success; e.message = errmesg; pushEvent (e);
}
void MeonSession::aooClientGroupJoined (SonobusAudioProcessor*, bool success, const juce::String& group, const juce::String& errmesg)
{
    Event e; e.type = Event::GroupJoined; e.success = success; e.group = group; e.message = errmesg; pushEvent (e);
}
void MeonSession::aooClientGroupLeft (SonobusAudioProcessor*, bool success, const juce::String& group, const juce::String& errmesg)
{
    Event e; e.type = Event::GroupLeft; e.success = success; e.group = group; e.message = errmesg; pushEvent (e);
}
void MeonSession::aooClientPeerPendingJoin (SonobusAudioProcessor*, const juce::String& group, const juce::String& user)
{
    Event e; e.type = Event::PeerPending; e.group = group; e.user = user; pushEvent (e);
}
void MeonSession::aooClientPeerJoined (SonobusAudioProcessor*, const juce::String& group, const juce::String& user)
{
    Event e; e.type = Event::PeerJoined; e.group = group; e.user = user; pushEvent (e);
}
void MeonSession::aooClientPeerJoinFailed (SonobusAudioProcessor*, const juce::String& group, const juce::String& user)
{
    Event e; e.type = Event::PeerJoinFailed; e.group = group; e.user = user; pushEvent (e);
}
void MeonSession::aooClientPeerLeft (SonobusAudioProcessor*, const juce::String& group, const juce::String& user)
{
    Event e; e.type = Event::PeerLeft; e.group = group; e.user = user; pushEvent (e);
}
void MeonSession::aooClientError (SonobusAudioProcessor*, const juce::String& errmesg)
{
    Event e; e.type = Event::Error; e.message = errmesg; pushEvent (e);
}
void MeonSession::aooClientPeerChangedState (SonobusAudioProcessor*, const juce::String& mesg)
{
    Event e; e.type = Event::PeerState; e.message = mesg; pushEvent (e);
}
void MeonSession::sbChatEventReceived (SonobusAudioProcessor*, const SBChatEvent& chatevent)
{
    Event e; e.type = Event::Chat; e.chat = chatevent; pushEvent (e);
}

void MeonSession::handleAsyncUpdate()
{
    std::vector<Event> events;
    {
        const juce::ScopedLock sl (eventLock);
        events.swap (pendingEvents);
    }
    for (auto& e : events)
        handleEvent (e);
}

void MeonSession::handleEvent (const Event& e)
{
    const double now = juce::Time::getMillisecondCounterHiRes();

    switch (e.type)
    {
        case Event::Connected:
        {
            if (e.success)
            {
                const bool wasInRoom = roomState == RoomState::InRoom;
                serverState = ServerState::Connected;
                everConnected = true;
                if (wasInRoom && log.isActive())
                    log.addEvent ("serverReconnected", "");
                listeners.call ([] (Listener& l) { l.sessionStateChanged(); });
                performPendingRoomAction();
            }
            else
            {
                serverState = ServerState::Disconnected;
                nextConnectAttemptMs = now + (e.message.containsIgnoreCase ("denied") ? 500.0 : 3000.0);
                if (roomState == RoomState::Joining)
                    finishJoin (false, JoinFailure::Error, e.message);
                listeners.call ([] (Listener& l) { l.sessionStateChanged(); });
            }
            break;
        }

        case Event::Disconnected:
        {
            serverState = ServerState::Disconnected;
            nextConnectAttemptMs = now + 3000.0;
            if (roomState == RoomState::InRoom && log.isActive())
                log.addEvent ("serverLost", e.message);
            if (roomState == RoomState::Joining || roomState == RoomState::Verifying)
                finishJoin (false, JoinFailure::Error, e.message);
            listeners.call ([] (Listener& l) { l.sessionStateChanged(); });
            break;
        }

        case Event::GroupJoined:
        {
            if (e.success)
            {
                if (roomState == RoomState::Joining && e.group == pendingCode)
                {
                    if (creatingRoom)
                    {
                        finishJoin (true, JoinFailure::None, {});
                    }
                    else
                    {
                        roomState = RoomState::Verifying;
                        verifyDeadlineMs = now + 4000.0;
                        listeners.call ([] (Listener& l) { l.sessionStateChanged(); });
                    }
                }
                // 서버 재접속 후 같은 방에 다시 들어온 경우는 그대로 둔다
            }
            else if (roomState == RoomState::Joining && e.group == pendingCode)
            {
                finishJoin (false, JoinFailure::Error, e.message);
            }
            break;
        }

        case Event::GroupLeft:
        {
            if (roomState != RoomState::None && (e.group == roomCode || e.group == pendingCode))
            {
                // 우리가 leaveRoom 을 부른 경우는 이미 정리됐다. 그 외(서버가 내보냄)는 여기서 정리.
                clearRoom ("groupLeft");
            }
            break;
        }

        case Event::PeerPending:
        case Event::PeerJoined:
        case Event::PeerJoinFailed:
        {
            if (roomState == RoomState::None || (e.group != pendingCode && e.group != roomCode))
                break;

            auto* m = findMember (e.user);
            if (m == nullptr)
            {
                Member nm;
                nm.userName = e.user;
                nm.displayName = displayNameFor (e.user);
                nm.part = partFor (e.user);
                nm.slot = firstFreeSlot();
                nm.joinedAtMs = now;
                members.push_back (nm);
                m = &members.back();
                if (log.isActive())
                    log.addEvent ("peerPending", e.user);
            }

            if (e.type == Event::PeerJoined)
            {
                const bool wasConnected = m->connected;
                m->pending = false;
                m->connected = true;
                m->joinFailed = false;
                m->disconnectedSinceMs = 0.0;
                if (! wasConnected && roomState == RoomState::InRoom && now - joinedAtMs > 2000.0)
                    addSystemChat (m->displayName + "님이 입장했습니다");
                if (log.isActive())
                    log.addEvent ("peerJoined", e.user);
                const int idx = findPeerIndex (e.user);
                if (idx >= 0)
                    applyPeerDefaults (idx, *m);
            }
            else if (e.type == Event::PeerJoinFailed)
            {
                m->pending = false;
                m->connected = false;
                m->joinFailed = true;
                m->disconnectedSinceMs = now;
                if (log.isActive())
                    log.addEvent ("peerJoinFailed_NAT", e.user);
            }

            if (roomState == RoomState::Verifying)
            {
                if ((int) members.size() >= metric::maxOthers + 1)
                    finishJoin (false, JoinFailure::RoomFull, "room full");
                else
                    finishJoin (true, JoinFailure::None, {});
            }
            else if (roomState == RoomState::InRoom && (int) members.size() >= metric::maxOthers + 1
                     && now - joinedAtMs < 3000.0)
            {
                // 내가 6번째로 들어온 경우
                finishJoin (false, JoinFailure::RoomFull, "room full");
            }
            listeners.call ([] (Listener& l) { l.membersChanged(); });
            break;
        }

        case Event::PeerLeft:
        {
            if (auto* m = findMember (e.user))
            {
                const auto name = m->displayName;
                members.erase (std::remove_if (members.begin(), members.end(),
                                               [&e] (const Member& mm) { return mm.userName == e.user; }), members.end());
                if (roomState == RoomState::InRoom)
                    addSystemChat (name + "님이 나갔습니다");
                if (log.isActive())
                    log.addEvent ("peerLeft", e.user);
                listeners.call ([] (Listener& l) { l.membersChanged(); });
            }
            break;
        }

        case Event::Chat:
        {
            if (roomState != RoomState::InRoom || e.chat.group != roomCode)
                break;
            if (e.chat.from == userName)
                break;
            ChatMessage m;
            m.kind = ChatMessage::Other;
            m.from = displayNameFor (e.chat.from);
            m.text = e.chat.message;
            m.time = juce::Time::getCurrentTime();
            chat.push_back (m);
            ++unread;
            listeners.call ([] (Listener& l) { l.chatChanged(); });
            break;
        }

        case Event::PeerState:
            refreshStats();
            break;

        case Event::Error:
            if (log.isActive())
                log.addEvent ("error", e.message);
            break;
    }
}

//==============================================================================
void MeonSession::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    ++tickCount;

    // 서버 연결 관리 (방 밖에서만 직접 재시도; 방 안에서는 엔진의 자동 재접속을 쓴다)
    if (wantConnected && serverState == ServerState::Disconnected && roomState == RoomState::None
        && now >= nextConnectAttemptMs)
    {
        connectNow();
    }
    else if (serverState == ServerState::Connecting && now > connectDeadlineMs)
    {
        processor.disconnectFromServer();
        serverState = ServerState::Disconnected;
        nextConnectAttemptMs = now + 3000.0;
        if (roomState == RoomState::Joining)
            finishJoin (false, JoinFailure::Error, "connect timeout");
        listeners.call ([] (Listener& l) { l.sessionStateChanged(); });
    }

    // 코드 확인 시간 초과 → 잘못된 코드
    if (roomState == RoomState::Verifying && now > verifyDeadlineMs)
        finishJoin (false, JoinFailure::InvalidCode, "no peers");

    if (roomState == RoomState::InRoom)
    {
        reconcilePeers();
        if ((tickCount % 2) == 0)
            refreshStats();
        if (now - lastSampleMs >= 5000.0)
        {
            lastSampleMs = now;
            writeLogSample();
        }
    }
}

void MeonSession::reconcilePeers (bool adoptEnginePan)
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    bool changed = false;

    // 엔진에는 있는데 목록에 없는 피어 (끊겼다 다시 붙은 경우 등)
    const int n = processor.getNumberRemotePeers();
    for (int i = 0; i < n; ++i)
    {
        auto user = processor.getRemotePeerUserName (i);
        if (user.isEmpty() || findMember (user) != nullptr)
            continue;
        Member nm;
        nm.userName = user;
        nm.displayName = displayNameFor (user);
        nm.part = partFor (user);
        nm.slot = firstFreeSlot();
        nm.pending = false;
        nm.connected = processor.getRemotePeerConnected (i);
        nm.joinedAtMs = now;
        if (adoptEnginePan)
            nm.pan = readPeerPan (i);
        members.push_back (nm);
        applyPeerDefaults (i, members.back());
        changed = true;
    }

    // 연결 상태 변화, 30초 이상 끊긴 멤버 정리
    for (auto it = members.begin(); it != members.end();)
    {
        auto& m = *it;
        const int idx = findPeerIndex (m.userName);
        if (idx >= 0)
        {
            // 엔진의 connected 는 '어느 쪽으로든 소리가 흐르는지'다 (aoo 는 받던 소리가 끊기면 STOP 을 낸다).
            // 그래서 양쪽 다 마이크를 끄거나, 내가 마이크를 끈 채 상대를 뮤트하면 false 가 된다.
            // 상대가 정말 나간 것은 서버의 PeerLeft 로 오므로, 엔진 값으로는 끊김 처리하지 않고 연결만 반영한다.
            if (processor.getRemotePeerConnected (idx) && ! m.connected && ! m.pending)
            {
                m.disconnectedSinceMs = 0.0;
                if (log.isActive()) log.addEvent ("peerReconnected", m.userName);
                m.connected = true;
                changed = true;
            }
            if (! m.defaultsApplied && m.connected)
                applyPeerDefaults (idx, m);
        }

        if (! m.connected && ! m.pending && m.disconnectedSinceMs > 0.0 && now - m.disconnectedSinceMs > 30000.0)
        {
            if (log.isActive()) log.addEvent ("peerRemovedAfterTimeout", m.userName);
            it = members.erase (it);
            changed = true;
            continue;
        }
        ++it;
    }

    if (changed)
        listeners.call ([] (Listener& l) { l.membersChanged(); });
}

void MeonSession::refreshStats()
{
    for (auto& m : members)
    {
        const int idx = findPeerIndex (m.userName);
        if (idx < 0)
            continue;
        SonobusAudioProcessor::LatencyInfo li;
        if (processor.getRemotePeerLatencyInfo (idx, li))
        {
            m.pingMs = li.pingMs;
            m.roundtripMs = li.totalRoundtripMs;
        }
        m.jitterBufferMs = processor.getRemotePeerBufferTime (idx);
        m.dropped = processor.getRemotePeerPacketsDropped (idx);
        m.resent = processor.getRemotePeerPacketsResent (idx);
        m.received = processor.getRemotePeerPacketsReceived (idx);
        m.gain = processor.getRemotePeerLevelGain (idx);
        applyPeerPan (idx, m.pan, true);
        m.hasStats = true;
    }
    listeners.call ([] (Listener& l) { l.memberStatsChanged(); });
}

void MeonSession::writeLogSample()
{
    if (! log.isActive())
        return;
    juce::Array<juce::var> arr;
    for (auto& m : members)
    {
        const int idx = findPeerIndex (m.userName);
        arr.add (MeonSessionLog::makeObject ({
            { "user", m.userName },
            { "connected", m.connected },
            { "pending", m.pending },
            { "natFailed", m.joinFailed },
            { "pingMs", m.pingMs },
            { "roundtripMs", m.roundtripMs },
            { "jitterBufferMs", m.jitterBufferMs },
            { "packetsDropped", (juce::int64) m.dropped },
            { "packetsResent", (juce::int64) m.resent },
            { "packetsReceived", (juce::int64) m.received },
            { "dropCount", m.dropCount },
            { "packetsSent", idx >= 0 ? (juce::int64) processor.getRemotePeerPacketsSent (idx) : (juce::int64) 0 },
            { "bytesSent", idx >= 0 ? (juce::int64) processor.getRemotePeerBytesSent (idx) : (juce::int64) 0 },
            { "sendChannels", idx >= 0 ? processor.getRemotePeerActualSendChannelCount (idx) : 0 },
            { "gain", m.gain },
            { "mutedByMe", m.muted } }));
    }
    log.addSample (MeonSessionLog::makeObject ({
        { "t", juce::Time::getCurrentTime().toISO8601 (true) },
        { "serverConnected", serverState == ServerState::Connected },
        { "serverPingMs", getServerPingMs() },
        { "selfMuted", isMyMuted() },
        // 파일 재생(MR): 재생 중이면 그 소리도 송신에 섞인다 (송신 채널 수는 그대로)
        { "fileLoaded", hasPlaybackFile() },
        { "filePlaying", isPlaybackPlaying() },
        { "fileGainDb", getPlaybackGainDb() },
        // 귀 테러 방지: 지금 단계, 방에 들어온 뒤 대역·전체 리미터가 줄이기 시작한 횟수
        { "earGuard", EarGuard::levelKey (processor.getEarGuard().getLevel()) },
        { "earGuardBandCount", processor.getEarGuard().getBandTriggerCount() - earGuardBandBase },
        { "earGuardFullCount", processor.getEarGuard().getFullTriggerCount() - earGuardFullBase },
        { "memberCount", getMemberCount() },
        { "members", arr } }));
}

} // namespace meon
