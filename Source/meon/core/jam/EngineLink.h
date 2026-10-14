// MEON core - Engine 인터페이스의 실제 구현 (SonobusAudioProcessor). 엔진을 직접 부르는 곳은 여기뿐.
// 엔진 콜백(네트워크 스레드)은 큐에 넣어 메시지 스레드에서 Engine::Listener 로 넘긴다.
// 앱·플러그인 타깃에만 들어간다 (단위 테스트는 가짜 엔진).
#pragma once

#include "Engine.h"
#include "../../../SonobusPluginProcessor.h"

namespace meon::core
{

class EngineLink : public Engine,
                   private SonobusAudioProcessor::ClientListener,
                   private juce::AsyncUpdater
{
public:
    EngineLink (SonobusAudioProcessor&, bool plugin);
    ~EngineLink() override;

    void addListener (Listener* l) override    { listeners.add (l); }
    void removeListener (Listener* l) override { listeners.remove (l); }

    void applyDefaults() override;

    bool connect (const juce::String& host, int port, const juce::String& user, const juce::String& password) override;
    void disconnect() override;
    bool isConnected() const override;
    juce::String connectedUser() const override;
    juce::String joinedGroup() const override;
    bool joinGroup (const juce::String& name, const juce::String& password) override;
    void leaveGroup (const juce::String& name) override;
    void setAllowedPeers (const juce::StringArray& users, bool enabled) override;

    std::vector<EnginePeer> peers() const override;
    void removePeer (const juce::String& user) override;
    void setPeerReceive (const juce::String& user, bool on) override;
    void setPeerGainDb (const juce::String& user, float db) override;
    void setPeerPan (const juce::String& user, int pan) override;
    float peerLevel (const juce::String& user, int channel) const override;
    void applyPeerDefaults (const juce::String& user, BufferMode) override;

    bool isSendMuted() const override;
    void setSendMuted (bool) override;
    void setSendGainDb (float) override;
    void setSelfMonitor (bool) override;
    void setSelfMonitorDb (float) override;
    void setSelfMonitorPan (int) override;
    void setEarGuardLevel (int) override;
    void setMasterDb (float) override;
    void setMonitorMuted (bool) override;
    void setBufferMode (BufferMode) override;
    void setInputChannels (int start, int count) override;
    int inputChannelCount() const override;
    juce::String inputChannelText() const override;
    float inputLevel (int channel) const override;
    float sendLevel (int channel) const override;
    float monitorLevel (int channel) const override;
    int sendChannelCount() const override;
    juce::String audioDescription() const override;
    DeviceInfo deviceInfo() const override;

    bool sendChat (const EngineChat&) override;

    bool loadFile (const juce::File&) override;
    void closeFile() override;
    bool hasFile() const override;
    juce::String fileName() const override;
    bool isFilePlaying() const override;
    void setFilePlaying (bool) override;
    double filePosition() const override;
    double fileLength() const override;
    void setFilePosition (double) override;
    float fileGainDb() const override;
    void setFileGainDb (float) override;

    /** 앱이 장치 관리자를 알려 준다 (입력 채널 수·장치 이름 로그용). 플러그인은 없음 */
    std::function<juce::AudioDeviceManager*()> deviceManager;

private:
    SonobusAudioProcessor& processor;
    const bool plugin;
    juce::ListenerList<Listener> listeners;
    int pcmFormatIndex = -1;
    bool selfMonitor = false, monitorMuted = false;
    float masterDb = 0.0f, selfMonitorDb = 0.0f;
    int selfMonitorPan = 0;
    class PlaybackFader;
    std::unique_ptr<PlaybackFader> fader;
    juce::File loadedFile;

    struct Event
    {
        enum Type { connected, disconnected, groupJoined, groupLeft, peerJoined, peerJoinFailed, peerLeft, chat } type;
        bool ok = false;
        juce::String group, user, message;
        EngineChat chatEvent;
    };
    juce::CriticalSection lock;
    std::vector<Event> pending;
    void push (Event e);
    void handleAsyncUpdate() override;

    int indexOf (const juce::String& user) const;
    void setParam (const juce::String& id, float value);
    void applyMonitorParams();
    static float meter (foleys::LevelMeterSource&, int channel);

    void aooClientConnected (SonobusAudioProcessor*, bool success, const juce::String& errmesg) override;
    void aooClientDisconnected (SonobusAudioProcessor*, bool success, const juce::String& errmesg) override;
    void aooClientLoginResult (SonobusAudioProcessor*, bool, const juce::String&) override {}
    void aooClientGroupJoined (SonobusAudioProcessor*, bool success, const juce::String& group, const juce::String& errmesg) override;
    void aooClientGroupLeft (SonobusAudioProcessor*, bool success, const juce::String& group, const juce::String& errmesg) override;
    void aooClientPublicGroupModified (SonobusAudioProcessor*, const juce::String&, int, const juce::String&) override {}
    void aooClientPublicGroupDeleted (SonobusAudioProcessor*, const juce::String&, const juce::String&) override {}
    void aooClientPeerPendingJoin (SonobusAudioProcessor*, const juce::String&, const juce::String&) override {}
    void aooClientPeerJoined (SonobusAudioProcessor*, const juce::String& group, const juce::String& user) override;
    void aooClientPeerJoinFailed (SonobusAudioProcessor*, const juce::String& group, const juce::String& user) override;
    void aooClientPeerJoinBlocked (SonobusAudioProcessor*, const juce::String&, const juce::String&, const juce::String&, int) override {}
    void aooClientPeerLeft (SonobusAudioProcessor*, const juce::String& group, const juce::String& user) override;
    void aooClientError (SonobusAudioProcessor*, const juce::String&) override {}
    void aooClientPeerChangedState (SonobusAudioProcessor*, const juce::String&) override {}
    void sbChatEventReceived (SonobusAudioProcessor*, const SBChatEvent&) override;

    JUCE_DECLARE_NON_COPYABLE (EngineLink)
};

} // namespace meon::core
