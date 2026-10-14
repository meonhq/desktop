#include "JamSession.h"
#include "../log/AppLog.h"

namespace meon::core
{

using juce::String;
using juce::var;

static float toDb (float lin) { return juce::Decibels::gainToDecibels (lin, -100.0f); }

JamSession::JamSession (Engine& e, AppStore& s, Scheduler& sc, Options o)
    : engine (e), store (s), scheduler (sc), options (std::move (o)), prefs (options.prefs),
      chatChannel (sc, [this] (const EngineChat& c) { return engine.sendChat (c); }),
      sessionLog (options.logFolder)
{
    masterDb = prefs.keepMasterVolume ? prefs.masterDb : 0.0f;
    engine.addListener (this);
    store.addListener (this);
    chatChannel.addListener (this);

    // 방 밖(홈)의 엔진 값도 여기서 맞춘다. 앱·플러그인은 지난 실행의 엔진 상태(셀프 모니터 dry 등)를 다시 읽어 오므로,
    // 그대로 두면 홈에서도 내 소리가 스피커로 나왔다 (버그 21·22). 방 밖에서는 셀프 모니터를 끈다 (설정값은 그대로)
    engine.applyDefaults();
    if (! options.plugin) engine.setSendMuted (prefs.sendMuted);   // 앱: 홈의 송출 정지 버튼도 설정 파일 값으로 (버그 #1 과 같은 규칙)
    engine.setSendGainDb (prefs.sendGainDb);                        // 홈의 송출 레벨·입력 미터도 설정값대로
    applyMonitor();

    engine.setEarGuardLevel (prefs.earGuard);
    schedulePoll();

    // 플러그인 창을 다시 연 게 아니라 프로세서 단위라서, 이미 방에 있으면 이어받는다
    if (store.state().currentRoom) enter (*store.state().currentRoom);
}

void JamSession::schedulePoll()
{
    std::weak_ptr<bool> weak = alive;
    pollTask = scheduler.post (1000, [this, weak]
    {
        if (weak.expired()) return;
        pollTask = 0;
        pollSharedPrefs();
        schedulePoll();
    });
}

JamSession::~JamSession()
{
    *alive = false;
    scheduler.cancel (tickTask);
    scheduler.cancel (retryTask);
    scheduler.cancel (pollTask);
    if (phase != Phase::idle) exit (options.plugin ? "pluginClosed" : "quit");
    chatChannel.removeListener (this);
    store.removeListener (this);
    engine.removeListener (this);
}

void JamSession::savePrefs()
{
    if (options.savePrefs) options.savePrefs (prefs);
}

void JamSession::setPhase (Phase p)
{
    if (phase == p) return;
    phase = p;
    listeners.call ([p] (Listener& l) { l.jamPhaseChanged (p); });
}

//==============================================================================
// 방 따라가기

void JamSession::stateChanged (StateKey k)
{
    // 가짜 기간: 방 안에서 닉네임·파트를 바꾸면 다른 컴퓨터에도 바로 알린다 (버그 24. 예전에는 다시 연결될 때만 보냈다)
    if (k == StateKey::me && phase == Phase::inRoom) { sendMyProfile(); return; }
    if (k != StateKey::currentRoom) return;
    const auto& room = store.state().currentRoom;
    if (! room)
    {
        if (phase != Phase::idle) exit ("left");
        return;
    }
    if (phase == Phase::idle || room->id != currentRoomId)
    {
        if (phase != Phase::idle) exit ("switch");
        enter (*room);
        return;
    }
    roomUpdated (*room);
}

void JamSession::enter (const RoomDetail& room)
{
    AppLog::write ("jam", LOGTXT ("방 입장: ") + room.id + (room.type == "private" ? LOGTXT (" (비공개)") : ""));
    currentRoomId = room.id;
    connection = room.connection;
    myId = connection.aooUsername;
    peerStates.clear();
    mix.clear();
    serverLost = false;
    reconnectAttempts = 0;

    engine.applyDefaults();
    // 앱: 송출 정지는 설정 파일 값 (버그 #1). 플러그인: 프로젝트에 남은 엔진 값을 그대로 쓴다
    if (! options.plugin)
    {
        engine.setSendMuted (prefs.sendMuted);
        engine.setInputChannels (prefs.inputChannelStart, prefs.inputChannelCount);
    }
    else
    {
        engine.setInputChannels (0, prefs.pluginSendChannels);   // 플러그인: 보내는 채널만 고정 (버그 #13)
    }
    engine.setBufferMode (prefs.bufferMode);
    engine.setSendGainDb (prefs.sendGainDb);
    applyMonitor();
    applyAllowed();

    connectEngine();
    scheduleTick();
}

void JamSession::connectEngine()
{
    setPhase (Phase::connecting);
    if (engine.isConnected() && engine.connectedUser() == connection.aooUsername)
    {
        joinGroup();
        return;
    }
    if (engine.isConnected()) engine.disconnect();
    if (! engine.connect (connection.aooHost, connection.aooPort, connection.aooUsername, connection.aooUserPassword))
    {
        std::weak_ptr<bool> weak = alive;
        scheduler.cancel (retryTask);
        retryTask = scheduler.post (3000, [this, weak] { if (! weak.expired() && phase == Phase::connecting) connectEngine(); });
    }
}

void JamSession::joinGroup()
{
    setPhase (Phase::joining);
    if (engine.joinedGroup() == connection.groupName)
    {
        engineGroupJoined (true, connection.groupName, {});
        return;
    }
    engine.joinGroup (connection.groupName, connection.groupPassword);
}

void JamSession::exit (const String& reason)
{
    AppLog::write ("jam", LOGTXT ("방 나감: ") + currentRoomId + " (" + reason + ")");
    // 방을 나가면(직접 퇴장·방 닫힘·추방·다른 방으로 옮김) 틀어 둔 파일을 닫는다 (옛 앱 동작)
    closeFile();
    scheduler.cancel (tickTask);
    scheduler.cancel (retryTask);
    tickTask = retryTask = 0;
    if (connection.groupName.isNotEmpty()) engine.leaveGroup (connection.groupName);
    engine.disconnect();
    engine.setAllowedPeers ({}, false);
    if (sessionLog.isActive())
    {
        sessionLog.addEvent ("leave", reason);
        sessionLog.end (reason);
    }
    if (silence.isWarning()) silence.update (scheduler.nowMs(), false, false, 0, 0.0f);
    chatChannel.end();
    peerStates.clear();
    mix.clear();
    currentRoomId = {};
    connection = {};
    applyMonitor();   // 방 밖: 셀프 모니터 끔 (다시 들어가면 설정값대로)
    setPhase (Phase::idle);
    listeners.call ([] (Listener& l) { l.jamMembersChanged(); l.jamSilenceChanged (false); });
}

void JamSession::roomUpdated (const RoomDetail& room)
{
    connection.members = room.connection.members;
    applyAllowed();
    listeners.call ([] (Listener& l) { l.jamMembersChanged(); });
}

void JamSession::applyAllowed()
{
    juce::StringArray others;
    for (auto& m : connection.members)
        if (m != myId) others.add (m);
    // 엔진 수정 E1: 멤버 목록 밖의 상대와는 연결하지 않는다 (빠진 상대는 엔진이 끊는다)
    engine.setAllowedPeers (others, true);
    chatChannel.setOthers (others);

    const auto now = scheduler.nowMs();
    for (auto& u : others)
        if (peerStates.count (u) == 0) peerStates.emplace (u, PeerState (now));
    for (auto it = peerStates.begin(); it != peerStates.end();)
        it = others.contains (it->first) ? std::next (it) : peerStates.erase (it);
}

//==============================================================================
// 엔진 이벤트

void JamSession::engineConnected (bool ok, const String& message)
{
    if (phase == Phase::idle) return;
    if (! ok)
    {
        if (phase == Phase::connecting)
        {
            std::weak_ptr<bool> weak = alive;
            scheduler.cancel (retryTask);
            retryTask = scheduler.post (3000, [this, weak] { if (! weak.expired() && phase == Phase::connecting) connectEngine(); });
        }
        if (serverLost) ++reconnectAttempts;
        juce::ignoreUnused (message);
        return;
    }
    if (serverLost && sessionLog.isActive())
    {
        sessionLog.addEvent ("serverReconnected", String (reconnectAttempts) + " attempts, "
                                                  + String ((scheduler.nowMs() - lostAt) / 1000.0, 1) + " s");
    }
    serverLost = false;
    if (phase == Phase::connecting) joinGroup();
}

void JamSession::engineDisconnected (const String& message)
{
    if (phase == Phase::idle) return;
    if (! serverLost)
    {
        serverLost = true;
        lostAt = scheduler.nowMs();
        reconnectAttempts = 0;
        if (sessionLog.isActive()) sessionLog.addEvent ("serverLost", message);
    }
    // 방 안에서는 엔진의 자동 재접속을 쓴다 (소리는 직접 연결이라 계속 들린다)
}

void JamSession::engineGroupJoined (bool ok, const String& group, const String& message)
{
    if (phase == Phase::idle || group != connection.groupName) return;
    if (! ok)
    {
        if (sessionLog.isActive()) sessionLog.addEvent ("groupJoinFailed", message);
        std::weak_ptr<bool> weak = alive;
        scheduler.cancel (retryTask);
        retryTask = scheduler.post (3000, [this, weak] { if (! weak.expired() && phase == Phase::joining) joinGroup(); });
        return;
    }
    if (phase == Phase::inRoom) return;   // 서버 재접속 뒤 다시 들어온 경우

    SessionLog::AudioInfo audio;
    audio.host = connection.aooHost + ":" + String (connection.aooPort);
    // 장치 정보는 칸별로 (버그 18: 예전에는 deviceType 한 줄에 몰아 넣음). 없으면 빈칸 대신 "없음"
    const auto dev = engine.deviceInfo();
    const String none (juce::CharPointer_UTF8 ("없음"));
    audio.deviceType = dev.type.isNotEmpty() ? dev.type : String ("unknown");
    audio.inputDevice = dev.input.isNotEmpty() ? dev.input : none;
    audio.outputDevice = dev.output.isNotEmpty() ? dev.output : none;
    audio.sampleRate = dev.sampleRate;
    audio.bufferSize = dev.bufferSize;
    sessionLog.begin (currentRoomId, myId, options.plugin, audio);
    sessionLog.addEvent ("joined", connection.groupName);
    lastSampleAt = scheduler.nowMs();
    chatChannel.begin (connection.groupName, myId);
    applyAllowed();
    setPhase (Phase::inRoom);
    sendMyProfile();
    listeners.call ([] (Listener& l) { l.jamMembersChanged(); });
}

void JamSession::engineGroupLeft (const String& group)
{
    if (phase == Phase::inRoom && group == connection.groupName && sessionLog.isActive())
        sessionLog.addEvent ("groupLeft", group);
}

void JamSession::enginePeerJoined (const String& user)
{
    if (phase == Phase::idle) return;
    if (sessionLog.isActive()) sessionLog.addEvent ("peerJoined", user);
    // 가짜 기간: 그룹(비밀번호로 막힌)에 들어온 실제 상대를 방 멤버로 올린다
    if (options.fake != nullptr && ! connection.members.contains (user))
        options.fake->remotePeerJoined (user, {}, {}, false);
}

void JamSession::enginePeerJoinFailed (const String& user)
{
    if (sessionLog.isActive()) sessionLog.addEvent ("peerJoinFailed", user);
}

void JamSession::enginePeerLeft (const String& user)
{
    if (phase == Phase::idle) return;
    if (sessionLog.isActive()) sessionLog.addEvent ("peerLeft", user);
    if (options.fake != nullptr) options.fake->remotePeerLeft (user);
}

void JamSession::engineChat (const EngineChat& c)
{
    // 멤버 목록 밖의 상대가 보낸 채팅은 버린다 (API 2.8)
    if (phase != Phase::inRoom || ! connection.members.contains (c.from)) return;
    chatChannel.received (c);
}

void JamSession::chatProfile (const String& user, const var& profile)
{
    if (options.fake == nullptr) return;
    options.fake->remotePeerJoined (user, profile.getProperty ("nickname", "").toString(),
                                    profile.getProperty ("part", "").toString(), (bool) profile.getProperty ("host", false));
}

void JamSession::chatAckMissing (const String& id, const juce::StringArray& missing)
{
    if (sessionLog.isActive()) sessionLog.addEvent ("chatAckMissing", id + " " + missing.joinIntoString (","));
}

void JamSession::sendMyProfile()
{
    if (options.fake == nullptr || ! store.state().me) return;
    const auto& me = *store.state().me;
    const auto& room = store.state().currentRoom;
    auto* o = new juce::DynamicObject();
    o->setProperty ("nickname", me.nickname.value_or (""));
    o->setProperty ("part", me.part.value_or ("other"));
    o->setProperty ("host", room && room->hostId == me.id);
    chatChannel.sendProfile (var (o));
}

//==============================================================================
// 주기 (0.4초): 멤버 상태, 무음, 로그

void JamSession::scheduleTick()
{
    scheduler.cancel (tickTask);
    std::weak_ptr<bool> weak = alive;
    tickTask = scheduler.post (tickMs, [this, weak]
    {
        if (weak.expired()) return;
        tickTask = 0;
        tick();
        if (phase != Phase::idle) scheduleTick();
    });
}

void JamSession::tick()
{
    const auto now = scheduler.nowMs();
    const auto enginePeers = engine.peers();
    bool membersChangedNow = false;
    bool needProfile = false;

    for (auto& [user, ps] : peerStates)
    {
        std::optional<EnginePeer> ep;
        for (auto& p : enginePeers)
            if (p.user == user) { ep = p; break; }
        auto& m = mix[user];
        if (ep && ep->connected && ! m.defaultsApplied)
        {
            engine.applyPeerDefaults (user, prefs.bufferMode);
            engine.setPeerReceive (user, ! m.muted);
            engine.setPeerGainDb (user, m.gainDb);
            engine.setPeerPan (user, m.pan);
            m.defaultsApplied = true;
        }
        const auto change = ps.update (now, ep, m.muted);
        switch (change)
        {
            case PeerState::Change::connected:
                membersChangedNow = needProfile = true;
                if (sessionLog.isActive()) sessionLog.addEvent ("peerConnected", user);
                if (phase == Phase::inRoom) chatChannel.addSystem (nameOf (user) + juce::String (juce::CharPointer_UTF8 ("님이 입장했어요")), user);
                break;
            case PeerState::Change::interrupted:
                membersChangedNow = true;
                if (sessionLog.isActive()) sessionLog.addEvent ("peerInterrupted", user);
                break;
            case PeerState::Change::resumed:
                membersChangedNow = true;
                if (sessionLog.isActive()) sessionLog.addEvent ("peerResumed", user);
                break;
            case PeerState::Change::failed:
                membersChangedNow = true;
                if (sessionLog.isActive()) sessionLog.addEvent ("peerJoinTimeout", user);
                break;
            case PeerState::Change::none:
                break;
        }
    }
    if (needProfile) sendMyProfile();

    // 송신·입력 레벨 (5초 최댓값)
    float inPeak = 0.0f, sendPeak = 0.0f;
    for (int c = 0; c < 2; ++c)
    {
        inPeak = juce::jmax (inPeak, engine.inputLevel (c));
        sendPeak = juce::jmax (sendPeak, engine.sendLevel (c));
    }
    inputPeakDb = juce::jmax (inputPeakDb, toDb (inPeak));
    sendPeakDb = juce::jmax (sendPeakDb, toDb (sendPeak));

    const auto sChange = silence.update (now, phase == Phase::inRoom, engine.isSendMuted(), engine.inputChannelCount(), toDb (inPeak));
    if (sChange == SilenceDetector::Change::started)
    {
        if (sessionLog.isActive())
            sessionLog.addEvent (isInputMissing() ? "inputMissingStart" : "inputSilenceStart",
                                 "channels " + engine.inputChannelText() + ", " + engine.audioDescription()
                                     + ", sendGainDb " + String (prefs.sendGainDb, 1));
        listeners.call ([] (Listener& l) { l.jamSilenceChanged (true); });
    }
    else if (sChange == SilenceDetector::Change::ended)
    {
        if (sessionLog.isActive())
            sessionLog.addEvent (silence.lastKind() == SilenceDetector::Kind::noInput ? "inputMissingEnd" : "inputSilenceEnd",
                                 String (silence.lastDurationMs() / 1000.0, 1) + " s");
        listeners.call ([] (Listener& l) { l.jamSilenceChanged (false); });
    }

    if (phase == Phase::inRoom && now - lastSampleAt >= sampleMs)
    {
        lastSampleAt = now;
        writeSample (now);
    }

    if (membersChangedNow) listeners.call ([] (Listener& l) { l.jamMembersChanged(); });
    listeners.call ([] (Listener& l) { l.jamStatsChanged(); });
}

void JamSession::writeSample (juce::int64)
{
    if (! sessionLog.isActive()) return;
    const auto enginePeers = engine.peers();
    juce::Array<var> arr;
    int sendingTo = 0;
    for (auto& [user, ps] : peerStates)
    {
        std::optional<EnginePeer> ep;
        for (auto& p : enginePeers)
            if (p.user == user) ep = p;
        const auto& m = mix[user];
        const char* st = ps.get() == PeerState::State::connected ? "connected"
                        : ps.get() == PeerState::State::interrupted ? "interrupted"
                        : ps.get() == PeerState::State::failed ? "failed" : "connecting";
        arr.add (SessionLog::makeObject ({
            { "user", user }, { "state", st }, { "dropCount", ps.dropCount() },
            { "pingMs", ep ? ep->pingMs : 0.0f }, { "roundtripMs", ep ? ep->roundtripMs : 0.0f },
            { "jitterBufferMs", ep ? ep->bufferMs : 0.0f },
            { "packetsReceived", ep ? ep->packetsReceived : (juce::int64) 0 },
            { "packetsDropped", ep ? ep->packetsDropped : (juce::int64) 0 },
            { "packetsResent", ep ? ep->packetsResent : (juce::int64) 0 },
            // 송출 정지 확인용: 엔진이 이 상대에게 보내는 중인지 (정지면 false), 보낸 패킷 수 (참고값)
            { "sendActive", ep ? ep->sendActive : false },
            { "packetsSent", ep ? ep->packetsSent : (juce::int64) 0 },
            { "gainDb", m.gainDb }, { "pan", m.pan }, { "mutedByMe", m.muted } }));
        if (ep && ep->sendActive) ++sendingTo;
    }
    sessionLog.addSample (SessionLog::makeObject ({
        { "t", juce::Time::getCurrentTime().toISO8601 (true) },
        { "serverConnected", engine.isConnected() },
        // 추가 반영 4: 송신 레벨 (5초 동안 최댓값), 입력 처리 뒤 레벨 (파일 소리 제외)
        { "sendPeakDb", sendPeakDb },
        { "inputPeakDb", inputPeakDb },
        { "sendMuted", engine.isSendMuted() },
        { "sendingTo", sendingTo },   // 지금 보내고 있는 상대 수 (송출 정지면 0 이어야 한다)
        { "sendGainDb", prefs.sendGainDb },
        { "inputChannels", engine.inputChannelText() },
        { "inputSilent", isInputSilent() },
        { "inputMissing", isInputMissing() },
        { "sendChannels", engine.sendChannelCount() },
        { "masterDb", masterDb }, { "monitorMuted", monitorMuted }, { "selfMonitor", prefs.selfMonitor },
        { "selfMonitorAudible", isSelfMonitorAudible() }, { "selfMonitorDb", prefs.selfMonitorDb },
        { "selfMonitorPan", prefs.selfMonitorPan },
        { "bufferMode", prefs.bufferMode == Engine::BufferMode::latency ? "latency" : "stable" },
        { "memberCount", (int) connection.members.size() },
        { "members", arr } }));
    sendPeakDb = inputPeakDb = -100.0f;
}

//==============================================================================
// 멤버 조작

std::vector<JamSession::Member> JamSession::members() const
{
    std::vector<Member> out;
    const auto& room = store.state().currentRoom;
    if (! room) return out;
    const auto enginePeers = engine.peers();
    for (auto& rm : room->members)
    {
        Member m;
        m.userId = rm.user.id;
        m.nickname = rm.user.nickname.value_or (juce::String());
        m.part = rm.user.part.value_or ("other");
        m.isHost = rm.isHost;
        m.isMe = rm.user.id == myId;
        if (! m.isMe)
        {
            if (auto it = peerStates.find (m.userId); it != peerStates.end())
            {
                m.state = it->second.get();
                m.dropCount = it->second.dropCount();
            }
            for (auto& p : enginePeers)
                if (p.user == m.userId && p.connected)
                {
                    m.pingMs = (int) std::lround (p.pingMs);
                    m.channels = juce::jmax (1, p.recvChannels);
                }
            if (auto it = mix.find (m.userId); it != mix.end())
            {
                m.muted = it->second.muted;
                m.gainDb = it->second.gainDb;
                m.pan = it->second.pan;
            }
        }
        else
        {
            m.state = PeerState::State::connected;
            m.channels = engine.sendChannelCount();
        }
        out.push_back (m);
    }
    return out;
}

String JamSession::nameOf (const String& userId) const
{
    if (const auto& room = store.state().currentRoom)
        for (auto& m : room->members)
            if (m.user.id == userId) return m.user.nickname.value_or (juce::String());
    return {};
}

float JamSession::memberLevel (const String& userId, int ch) const
{
    if (auto it = mix.find (userId); it != mix.end() && it->second.muted) return 0.0f;
    return engine.peerLevel (userId, ch);
}

void JamSession::setMemberMuted (const String& u, bool b)
{
    mix[u].muted = b;
    engine.setPeerReceive (u, ! b);
    listeners.call ([] (Listener& l) { l.jamStatsChanged(); });
}

void JamSession::setMemberGainDb (const String& u, float db)
{
    mix[u].gainDb = juce::jlimit (-60.0f, 6.0f, db);
    engine.setPeerGainDb (u, mix[u].gainDb);
}

void JamSession::setMemberPan (const String& u, int pan)
{
    mix[u].pan = juce::jlimit (-100, 100, pan);
    engine.setPeerPan (u, mix[u].pan);
}

void JamSession::resetMember (const String& u)
{
    setMemberGainDb (u, 0.0f);
    setMemberPan (u, 0);
}

//==============================================================================
// 나

bool JamSession::loadFile (const juce::File& f)
{
    if (! engine.loadFile (f)) return false;
    if (sessionLog.isActive()) sessionLog.addEvent ("fileLoaded", f.getFileName() + " (" + String (engine.fileLength(), 1) + " s)");
    listeners.call ([] (Listener& l) { l.jamFileChanged(); });
    return true;
}

void JamSession::closeFile()
{
    if (! engine.hasFile()) return;
    engine.closeFile();
    if (sessionLog.isActive()) sessionLog.addEvent ("fileClosed", {});
    listeners.call ([] (Listener& l) { l.jamFileChanged(); });
}

void JamSession::setFilePlaying (bool play)
{
    if (! engine.hasFile() || play == engine.isFilePlaying()) return;
    engine.setFilePlaying (play);
    if (sessionLog.isActive()) sessionLog.addEvent (play ? "filePlay" : "fileStop", String (engine.filePosition(), 1) + " s");
    listeners.call ([] (Listener& l) { l.jamFileChanged(); });
}

void JamSession::setSendMuted (bool b)
{
    engine.setSendMuted (b);
    prefs.sendMuted = b;
    if (! options.plugin) savePrefs();
    applyMonitor();   // 송출 정지 중에는 셀프 모니터도 들리지 않게 (설정값은 그대로, 버그 23)
    if (sessionLog.isActive())
    {
        // 바꾼 순간 엔진이 보내는 상대 수 (정지면 0)
        int sending = 0, total = 0;
        for (auto& p : engine.peers())
            if (p.connected) { ++total; if (p.sendActive) ++sending; }
        sessionLog.addEvent (b ? "sendMuted" : "sendUnmuted", "sendingTo " + String (sending) + "/" + String (total));
    }
    // 송출 정지를 켜면 무음 경고는 지우고 판정을 처음부터
    tick();
}

void JamSession::setSendGainDb (float db)
{
    prefs.sendGainDb = juce::jlimit (-30.0f, 10.0f, db);
    engine.setSendGainDb (prefs.sendGainDb);
    savePrefs();
}

void JamSession::setSelfMonitor (bool on)
{
    prefs.selfMonitor = on;
    applyMonitor();
    savePrefs();
}

void JamSession::setEarGuardLevel (int l)
{
    l = juce::jlimit (0, 2, l);
    prefs.earGuard = l;
    engine.setEarGuardLevel (l);
    savePrefs();
    if (sessionLog.isActive()) sessionLog.addEvent ("earGuard", String (l));
}

void JamSession::pollSharedPrefs()
{
    // 플러그인: DAW 가 저장된 상태를 나중에 다시 넣으면(프로젝트 불러오기 등) 셀프 모니터 값이 덮인다. 1초마다 맞춰 둔다
    engine.reapplyMonitor();
    if (! options.pollEarGuard) return;
    const int l = options.pollEarGuard();
    if (l < 0 || l == prefs.earGuard) return;
    prefs.earGuard = l;
    engine.setEarGuardLevel (l);
    if (sessionLog.isActive()) sessionLog.addEvent ("earGuard", String (l) + " (shared)");
}

void JamSession::setSelfMonitorDb (float db)
{
    prefs.selfMonitorDb = juce::jlimit (-60.0f, 6.0f, db);
    applyMonitor();
    savePrefs();
}

void JamSession::setSelfMonitorPan (int pan)
{
    prefs.selfMonitorPan = juce::jlimit (-100, 100, pan);
    applyMonitor();
    savePrefs();
}

void JamSession::setMasterDb (float db)
{
    masterDb = juce::jlimit (-30.0f, 6.0f, db);
    applyMonitor();
    if (prefs.keepMasterVolume) { prefs.masterDb = masterDb; savePrefs(); }
}

void JamSession::setMonitorMuted (bool b)
{
    monitorMuted = b;
    applyMonitor();
    if (sessionLog.isActive()) sessionLog.addEvent (b ? "monitorMuted" : "monitorUnmuted", {});
}

bool JamSession::isSelfMonitorAudible() const
{
    if (engine.isSendMuted()) return false;   // 송출 정지 중에는 홈에서도 방에서도 안 들린다 (버그 23)
    if (inRoomForAudio()) return prefs.selfMonitor;
    return ! options.plugin && prefs.homeMonitor;   // 홈: 설정을 켠 사람만 (버그 22·28. 플러그인은 설정이 없어 늘 끔)
}

void JamSession::setHomeMonitor (bool on)
{
    prefs.homeMonitor = on;
    applyMonitor();
    savePrefs();
    AppLog::write ("audio", on ? LOGTXT ("입장 전에도 내 소리 듣기: 켬") : LOGTXT ("입장 전에도 내 소리 듣기: 끔"));
}

void JamSession::applyMonitor()
{
    engine.setMasterDb (masterDb);
    engine.setSelfMonitor (isSelfMonitorAudible());
    engine.setSelfMonitorDb (prefs.selfMonitorDb);
    engine.setSelfMonitorPan (prefs.selfMonitorPan);
    engine.setMonitorMuted (monitorMuted);
}

void JamSession::setPluginSendChannels (int count)
{
    prefs.pluginSendChannels = juce::jlimit (1, 2, count);
    if (phase != Phase::idle) engine.setInputChannels (0, prefs.pluginSendChannels);
    savePrefs();
}

void JamSession::setInputChannels (int start, int count)
{
    prefs.inputChannelStart = juce::jmax (0, start);
    prefs.inputChannelCount = juce::jlimit (1, 2, count);
    engine.setInputChannels (prefs.inputChannelStart, prefs.inputChannelCount);
    if (sessionLog.isActive()) sessionLog.addEvent ("inputChannels", engine.inputChannelText());
    savePrefs();
}

void JamSession::setBufferMode (Engine::BufferMode m)
{
    prefs.bufferMode = m;
    engine.setBufferMode (m);
    for (auto& [user, x] : mix)
        if (x.defaultsApplied) engine.applyPeerDefaults (user, m);   // 합주 중에 바꾸면 바로 모든 피어에
    savePrefs();
}

} // namespace meon::core
