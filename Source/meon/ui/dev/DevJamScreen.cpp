#include "DevJamScreen.h"
#include "../theme/Hairline.h"
#include "../theme/Icons.h"
#include "../Messages.h"
#include "../../shell/ShellHooks.h"

namespace meon::ui
{

using core::JamSession;
using core::PeerState;

namespace
{
    juce::String stateText (PeerState::State s)
    {
        switch (s)
        {
            case PeerState::State::connecting:  return TXT ("연결 중");
            case PeerState::State::connected:   return TXT ("연결됨");
            case PeerState::State::interrupted: return TXT ("끊김 · 소리 안 옴");
            case PeerState::State::failed:      return TXT ("연결 실패");
        }
        return {};
    }
}

/** 멤버 한 줄: 아바타 · 이름 · 상태 · 핑 · 끊김 횟수 · 미터 · 음소거 · 볼륨 · 패닝 */
struct DevJamScreen::Row : juce::Component
{
    Row (JamSession& j, const JamSession::Member& m) : jam (j), userId (m.userId), me (m.isMe)
    {
        addAndMakeVisible (avatar);
        addAndMakeVisible (name);
        addAndMakeVisible (state);
        addAndMakeVisible (ping);
        addAndMakeVisible (meter);
        if (! me)
        {
            addAndMakeVisible (mute);
            addAndMakeVisible (volume);
            addAndMakeVisible (pan);
            mute.onPress = [this] { jam.setMemberMuted (userId, ! mute.isOn()); };
            volume.onChange = [this] (float db) { jam.setMemberGainDb (userId, db); };
            pan.onChange = [this] (int v) { jam.setMemberPan (userId, v); };
        }
        meter.setSource (juce::jmax (1, m.channels), [this] (int ch)
        {
            return me ? jam.sendLevel (ch) : jam.memberLevel (userId, ch);
        });
        update (m);
    }

    void update (const JamSession::Member& m)
    {
        avatar.setUser (m.userId);
        avatar.setDimmed (! me && (m.muted || m.state != PeerState::State::connected));
        name.setText ((m.nickname.isNotEmpty() ? m.nickname : m.userId.substring (0, 10)) + (m.isHost ? TXT (" · 방장") : juce::String())
                      + (me ? TXT (" · 나") : juce::String()) + "  " + icons::partLabel (m.part));
        state.setText (me ? TXT ("—") : stateText (m.state) + (m.dropCount > 0 ? TXT (" · 끊김 ") + juce::String (m.dropCount) + TXT ("회") : juce::String()));
        state.setColour (m.state == PeerState::State::connected || me ? col::inkSub : col::danger);
        if (me) ping.setText (TXT ("나")); else ping.setPing (m.pingMs);
        mute.setOn (m.muted);
        mute.setButtonText (m.muted ? TXT ("음소거 해제") : TXT ("음소거"));
        if (! volume.isDragging()) volume.setValue (m.gainDb);
        pan.setValue (m.pan);
        repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 0);
        avatar.setBounds (r.removeFromLeft (41).withSizeKeepingCentre (41, 41));
        r.removeFromLeft (9);
        name.setBounds (r.removeFromLeft (220).withHeight (20).withY (8));
        state.setBounds (avatar.getRight() + 9, 30, 220, 18);
        r.removeFromLeft (9);
        ping.setBounds (r.removeFromLeft (48).withSizeKeepingCentre (48, 19));
        r.removeFromLeft (12);
        meter.setBounds (r.removeFromLeft (150).withSizeKeepingCentre (150, 19));
        r.removeFromLeft (12);
        mute.setBounds (r.removeFromLeft (76).withSizeKeepingCentre (76, 19));
        r.removeFromLeft (9);
        volume.setBounds (r.removeFromLeft (150).withSizeKeepingCentre (150, 19));
        r.removeFromLeft (9);
        pan.setBounds (r.removeFromLeft (19).withSizeKeepingCentre (19, 19));
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (col::borderSubtle);
        hairline::horizontal (g, *this, 0.0f, (float) getHeight() - 1.0f, (float) getWidth());
    }

    JamSession& jam;
    juce::String userId;
    bool me;
    Avatar avatar { 41.0f, 20.0f };
    Text name { {}, 600, 14.0f }, state { {}, 400, 12.0f, col::inkSub };
    PingBadge ping;
    Meter meter;
    Button mute { Button::Kind::small, TXT ("음소거") };
    DbSlider volume { -60.0f, 6.0f };
    PanKnob pan;
};

//==============================================================================
DevJamScreen::DevJamScreen (core::MeonCore& c) : core (c), jam (*c.jam())
{
    setOpaque (true);
    for (auto* x : std::initializer_list<juce::Component*> { &title, &account, &status, &create, &code, &join, &leave, &joinNote, &roomInfo,
                                                             &meTitle, &sendMute, &inputMeter, &sendMeter, &monitorMeter, &inputLabel,
                                                             &sendLabel, &monitorLabel, &inputStart, &inputMode, &audioSettings,
                                                             &bufferMode, &selfMonLabel, &selfMonitor, &master, &masterLabel,
                                                             &chatInput, &chatNote, &logPath, &openLogs })
        addAndMakeVisible (x);
    addChildComponent (resend);

    create.onPress = [this] { createRoom(); };
    join.onPress = [this] { joinCode(); };
    code.onSubmit = [this] { joinCode(); };
    code.onRejected = [this] (juce::juce_wchar) { joinNote.setText (TXT ("0·1·O·I는 코드에 쓰지 않아요")); };
    leave.onPress = [this] { leaveRoom(); };

    sendMute.onPress = [this] { jam.setSendMuted (! jam.isSendMuted()); refresh(); };
    inputMeter.setSource (2, [this] (int ch) { return jam.inputLevel (ch); });
    sendMeter.setSource (2, [this] (int ch) { return jam.sendLevel (ch); });
    monitorMeter.setSource (2, [this] (int ch) { return jam.monitorLevel (ch); });

    // 입력 채널 위치 (Q8 잠정안) · 모노/스테레오
    juce::StringArray starts, labels;
    for (int i = 0; i < 16; ++i) { starts.add (juce::String (i)); labels.add (TXT ("입력 ") + juce::String (i + 1)); }
    inputStart.setItems (starts, labels);
    inputStart.setValue (juce::String (jam.getPrefs().inputChannelStart));
    inputMode.setOptions ({ "1", "2" }, { TXT ("모노"), TXT ("스테레오") });
    inputMode.setSelected (juce::String (jam.getPrefs().inputChannelCount));
    auto applyInput = [this] { jam.setInputChannels (inputStart.getValue().getIntValue(), inputMode.getSelected().getIntValue()); };
    inputStart.onValueChange = [applyInput] (const juce::String&) { applyInput(); };
    inputMode.onChange = applyInput;
    audioSettings.onPress = [this] { openAudioSettings(); };
    audioSettings.setVisible (! core.isPlugin());
    inputStart.setVisible (! core.isPlugin());
    inputMode.setVisible (! core.isPlugin());

    bufferMode.setOptions ({ "latency", "stable" }, { TXT ("지연 우선"), TXT ("안정 우선") });
    bufferMode.setSelected (jam.getBufferMode() == core::Engine::BufferMode::latency ? "latency" : "stable");
    bufferMode.onChange = [this] { jam.setBufferMode (bufferMode.getSelected() == "stable" ? core::Engine::BufferMode::stable : core::Engine::BufferMode::latency); };
    selfMonitor.setOn (jam.isSelfMonitor(), false);
    selfMonitor.onChange = [this] (bool on) { jam.setSelfMonitor (on); };
    master.setValue (jam.getMasterDb());
    master.onChange = [this] (float db) { jam.setMasterDb (db); };

    chatInput.setMaxBytes (core::ChatChannel::maxBytes);
    chatInput.onReturn = [this]
    {
        using R = core::ChatChannel::SendResult;
        const auto r = jam.chat().sendText (chatInput.getText());
        if (r == R::sent || r == R::localOnly) chatInput.setText ({});
        chatNote.setText (r == R::failed ? TXT ("보내지 못했어요") : juce::String());
    };
    resend.onPress = [this] { if (failedId.isNotEmpty()) jam.chat().resend (failedId); };
    openLogs.onPress = [this] { jam.log().getFolder().createDirectory(); jam.log().getFolder().startAsProcess(); };

    jam.addListener (this);
    core.store().addListener (this);
    jam.chat().addListener (this);
    rebuildRows();
    refresh();
    refreshChat();
    startTimer (1000);

   #if MEON_DEV_OPTIONS
    // 자동 시험: --dev-create (방 만들고 코드를 표준 오류로), --dev-join=코드, --dev-chat=글 (입장 8초 뒤 보냄)
    const auto& dev = meon::devOptions();
    if (! core.store().state().inRoom())   // 새 홈(R5~)에서 임시 합주 화면으로 열렸으면 이미 방 안이다
    {
        if (dev.getAllKeys().contains ("dev-create")) createRoom();
        if (dev["dev-join"].isNotEmpty()) { code.setCode (dev["dev-join"].toUpperCase()); joinCode(); }
    }
   #endif
}

DevJamScreen::~DevJamScreen()
{
    jam.chat().removeListener (this);
    core.store().removeListener (this);
    jam.removeListener (this);
}

void DevJamScreen::createRoom()
{
    core::CreateRoom c;
    c.type = "private";
    c.title = TXT ("개발용 방");
    c.capacity = 7;
    joinNote.setText (TXT ("만드는 중…"));
    juce::Component::SafePointer<DevJamScreen> safe (this);
    core.api().createRoom (c, [safe] (core::Result<core::RoomDetail> r)
    {
        if (safe == nullptr) return;
        if (! r.ok()) { safe->joinNote.setText (msg::general (r.err()) + " (" + core::errorKey (r.code()) + ")"); return; }
        safe->joinNote.setText ({});
        safe->core.store().setCurrentRoom (r.get());
        std::cerr << "MEON dev: room code " << r.get().inviteCode.value_or ("").toStdString() << std::endl;
    });
}

void DevJamScreen::joinCode()
{
    if (! code.isComplete()) { joinNote.setText (TXT ("방장에게 받은 6자리 코드를 입력하세요")); return; }
    joinNote.setText (TXT ("확인 중…"));
    juce::Component::SafePointer<DevJamScreen> safe (this);
    core.api().joinByCode (code.getCode(), [safe] (core::Result<core::JoinResult> r)
    {
        if (safe == nullptr) return;
        if (! r.ok()) { safe->joinNote.setText (TXT ("들어가지 못했어요: ") + core::errorKey (r.code())); return; }
        safe->joinNote.setText ({});
        if (r.get().room) safe->core.store().setCurrentRoom (r.get().room);
    });
}

void DevJamScreen::leaveRoom()
{
    const auto& room = core.store().state().currentRoom;
    if (! room) return;
    const auto id = room->id;
    core.store().setCurrentRoom (std::nullopt);   // 엔진은 바로 나온다
    core.api().leaveRoom (id, [] (core::Result<core::Done>) {});
}

void DevJamScreen::openAudioSettings()
{
    auto& provider = meon::deviceManagerProvider();
    auto* dm = provider ? provider() : nullptr;
    if (dm == nullptr) return;
    auto* sel = new juce::AudioDeviceSelectorComponent (*dm, 0, 16, 0, 2, false, false, false, false);
    sel->setSize (520, 420);
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (sel);
    o.dialogTitle = TXT ("오디오 장치");
    o.dialogBackgroundColour = col::white;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

void DevJamScreen::rebuildRows()
{
    const auto members = jam.members();
    bool same = (int) members.size() == rows.size();
    for (int i = 0; same && i < rows.size(); ++i) same = rows[i]->userId == members[(size_t) i].userId;
    if (! same)
    {
        rows.clear();
        for (auto& m : members) addAndMakeVisible (rows.add (new Row (jam, m)));
        resized();
    }
    for (int i = 0; i < rows.size(); ++i) rows[i]->update (members[(size_t) i]);
}

void DevJamScreen::jamStatsChanged()
{
    const auto members = jam.members();
    for (int i = 0; i < rows.size() && i < (int) members.size(); ++i)
        if (rows[i]->userId == members[(size_t) i].userId) rows[i]->update (members[(size_t) i]);
}

void DevJamScreen::refresh()
{
   #if MEON_DEV_OPTIONS
    // 자동 시험용 상태 출력 (바뀔 때만)
    {
        juce::String line = "phase=" + juce::String ((int) jam.getPhase());
        for (auto& m : jam.members())
            line << " | " << m.nickname << (m.isHost ? "(host)" : "") << (m.isMe ? "(me)" : "") << " state=" << (int) m.state;
        for (auto& msg : jam.chat().getMessages()) line << " | chat:" << msg.text << ":" << (int) msg.delivery;
        if (line != lastDevLine) { lastDevLine = line; std::cerr << "MEON dev: " << line << std::endl; }
        const auto chatText = meon::devOptions()["dev-chat"];
        if (chatText.isNotEmpty() && ! devChatSent && jam.isInRoom() && jam.members().size() >= 2)
        {
            devChatSent = true;
            juce::Component::SafePointer<DevJamScreen> safe (this);
            juce::Timer::callAfterDelay (8000, [safe, chatText] { if (safe != nullptr) safe->jam.chat().sendText (chatText); });
        }
    }
   #endif
    const auto& st = core.store().state();
    if (st.me)
        account.setText (TXT ("계정 @") + st.me->username + " · " + st.me->nickname.value_or (juce::String()) + " · " + icons::partLabel (st.me->part.value_or ("other"))
                         + " · AOO " + st.me->id);
    const auto phase = jam.getPhase();
    const char* ph = phase == JamSession::Phase::idle ? "방 밖" : phase == JamSession::Phase::connecting ? "AOO 서버 접속 중"
                   : phase == JamSession::Phase::joining ? "그룹 입장 중" : "방 안";
    status.setText (TXT ("상태: ") + juce::String::fromUTF8 (ph) + TXT (" · AOO 서버 ") + (jam.isEngineConnected() ? TXT ("연결됨") : TXT ("끊김"))
                    + TXT (" · 내 RTT ") + (st.myRttMs ? juce::String (*st.myRttMs) + "ms" : TXT ("—"))
                    + TXT (" · 실시간 ") + (st.link == core::Realtime::Link::online ? TXT ("연결됨") : TXT ("끊김")));
    if (st.currentRoom)
        roomInfo.setText (TXT ("입장 코드 ") + st.currentRoom->inviteCode.value_or ("—") + "   " + st.currentRoom->title + " · "
                          + juce::String (st.currentRoom->memberCount) + " / " + juce::String (st.currentRoom->capacity)
                          + TXT ("   그룹 ") + st.currentRoom->connection.groupName + " @ " + st.currentRoom->connection.aooHost + ":"
                          + juce::String (st.currentRoom->connection.aooPort));
    else
        roomInfo.setText (TXT ("방에 들어가 있지 않아요"));

    create.setVisible (! st.currentRoom);
    code.setVisible (! st.currentRoom);
    join.setVisible (! st.currentRoom);
    leave.setVisible (st.currentRoom.has_value());

    const bool muted = jam.isSendMuted();
    sendMute.setOn (muted);
    sendMute.setButtonText (muted ? TXT ("송출 재개") : TXT ("송출 정지"));
    auto f = jam.log().getFile();
    logPath.setText (TXT ("세션 로그: ") + (f != juce::File() ? f.getFullPathName() : jam.log().getFolder().getFullPathName()));
    repaint();
}

void DevJamScreen::refreshChat()
{
    chatLines.clear();
    failedId = {};
    const auto members = jam.members();
    auto nameOf = [&] (const juce::String& id)
    {
        for (auto& m : members) if (m.userId == id) return m.nickname;
        return id.substring (0, 8);
    };
    using M = core::ChatChannel::Message;
    for (auto& m : jam.chat().getMessages())
    {
        juce::String line;
        if (m.kind == M::Kind::system) line = TXT ("· ") + m.text;
        else if (m.kind == M::Kind::mine)
        {
            line = TXT ("나: ") + m.text;
            if (m.delivery == M::Delivery::failed) { line += TXT ("   ⚠ 일부 멤버에게 전달되지 않았어요"); failedId = m.id; }
            else if (m.delivery == M::Delivery::delivered) line += TXT ("   ✓");
        }
        else line = nameOf (m.fromUser) + ": " + m.text;
        chatLines.add (line);
    }
    while (chatLines.size() > 8) chatLines.remove (0);
    resend.setVisible (failedId.isNotEmpty());
    repaint();
}

void DevJamScreen::resized()
{
    auto r = getLocalBounds().reduced (metric::padX, metric::padY);
    title.setBounds (r.removeFromTop (24));
    account.setBounds (r.removeFromTop (18));
    status.setBounds (r.removeFromTop (18));
    r.removeFromTop (12);

    auto row = r.removeFromTop (44);
    int x = row.getX();
    create.setBounds (x, row.getY() + 6, create.getIdealWidth(), 32);
    x = create.getRight() + 18;
    code.setBounds (x, row.getY(), code.getPreferredWidth(), 44);
    join.setBounds (code.getRight() + 9, row.getY() + 6, join.getIdealWidth(), 32);
    leave.setBounds (row.getX(), row.getY() + 6, leave.getIdealWidth(), 32);
    joinNote.setBounds (join.getRight() + 12, row.getY() + 12, 400, 20);
    r.removeFromTop (9);
    roomInfo.setBounds (r.removeFromTop (22));
    r.removeFromTop (9);

    // 오른쪽: 내 오디오 + 채팅
    auto side = r.removeFromRight (380);
    r.removeFromRight (18);
    meTitle.setBounds (side.removeFromTop (22));
    side.removeFromTop (6);
    auto line = [&] (Text& label, juce::Component& c)
    {
        auto l = side.removeFromTop (24);
        label.setBounds (l.removeFromLeft (100));
        c.setBounds (l.removeFromLeft (180).withSizeKeepingCentre (180, 19));
    };
    line (inputLabel, inputMeter);
    line (sendLabel, sendMeter);
    line (monitorLabel, monitorMeter);
    auto l2 = side.removeFromTop (28);
    sendMute.setBounds (l2.removeFromLeft (80).withSizeKeepingCentre (80, 19));
    auto l3 = side.removeFromTop (36);
    inputStart.setBounds (l3.removeFromLeft (100).withSizeKeepingCentre (100, 32));
    l3.removeFromLeft (9);
    inputMode.setBounds (l3.removeFromLeft (inputMode.getPreferredWidth()).withSizeKeepingCentre (inputMode.getPreferredWidth(), 19));
    l3.removeFromLeft (9);
    audioSettings.setBounds (l3.removeFromLeft (audioSettings.getIdealWidth()).withSizeKeepingCentre (audioSettings.getIdealWidth(), 19));
    auto l4 = side.removeFromTop (28);
    bufferMode.setBounds (l4.removeFromLeft (bufferMode.getPreferredWidth()).withSizeKeepingCentre (bufferMode.getPreferredWidth(), 19));
    l4.removeFromLeft (18);
    selfMonLabel.setBounds (l4.removeFromLeft (70));
    selfMonitor.setBounds (l4.removeFromLeft (26).withSizeKeepingCentre (26, 16));
    auto l5 = side.removeFromTop (28);
    masterLabel.setBounds (l5.removeFromLeft (100));
    master.setBounds (l5.removeFromLeft (180).withSizeKeepingCentre (180, 19));
    side.removeFromTop (12);
    auto bottom = side.removeFromBottom (48);
    logPath.setBounds (bottom.removeFromTop (20));
    openLogs.setBounds (bottom.getX(), bottom.getY() + 4, openLogs.getIdealWidth(), 19);
    auto chatBox = side;
    chatInput.setBounds (chatBox.removeFromBottom (32));
    chatNote.setBounds (chatBox.removeFromBottom (20));
    resend.setBounds (chatBox.getRight() - 90, chatBox.getBottom() - 20, 90, 19);

    int y = r.getY();
    for (auto* rw : rows)
    {
        rw->setBounds (r.getX(), y, r.getWidth(), 59);
        y += 59;
    }
}

void DevJamScreen::paint (juce::Graphics& g)
{
    g.fillAll (col::white);
    // 채팅 줄
    auto chatArea = chatInput.getBounds().withY (monitorMeter.getBottom() + 150).withBottom (chatNote.getY() - 4);
    g.setFont (font (400, 13.0f));
    float y = (float) chatArea.getBottom() - 18.0f * (float) chatLines.size();
    for (auto& l : chatLines)
    {
        g.setColour (l.contains (TXT ("⚠")) ? col::danger : col::ink);
        g.drawText (l, juce::Rectangle<float> ((float) chatArea.getX(), y, (float) chatArea.getWidth(), 18.0f), juce::Justification::centredLeft, true);
        y += 18.0f;
    }
}

} // namespace meon::ui
