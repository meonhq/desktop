// MEON core - 합주 엔진(SonoBus) 인터페이스. JamSession 은 이것만 안다.
//   앱: EngineLink 가 SonobusAudioProcessor 로 구현 (엔진 콜백은 네트워크 스레드 → 메시지 스레드로 옮겨 부른다)
//   테스트: 가짜 엔진
// 모든 함수·콜백은 메시지 스레드. 피어는 AOO 사용자 이름(= 계정 ID)으로 가리킨다.
#pragma once

#include <JuceHeader.h>
#include <vector>

namespace meon::core
{

struct EngineChat
{
    juce::String group, from, tags, message;
};

struct EnginePeer
{
    juce::String user;
    bool connected = false;          // 엔진이 보는 연결 (어느 쪽으로든 소리가 흐름)
    bool recvActive = false;         // 상대가 보내고 있고 내가 받는 중 (상대가 송출을 끄거나 내가 음소거하면 false)
    juce::int64 packetsReceived = 0, packetsDropped = 0, packetsResent = 0;
    float pingMs = 0.0f, roundtripMs = 0.0f, bufferMs = 0.0f;
    int recvChannels = 0;
};

class Engine
{
public:
    enum class BufferMode { latency, stable };   // 지연 우선 = AutoFull, 안정 우선 = AutoIncreaseOnly

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void engineConnected (bool ok, const juce::String& message) { juce::ignoreUnused (ok, message); }
        virtual void engineDisconnected (const juce::String& message) { juce::ignoreUnused (message); }
        virtual void engineGroupJoined (bool ok, const juce::String& group, const juce::String& message) { juce::ignoreUnused (ok, group, message); }
        virtual void engineGroupLeft (const juce::String& group) { juce::ignoreUnused (group); }
        /** 그룹에 피어가 들어옴 (허용 목록 밖이면 엔진은 아직 연결하지 않았다) */
        virtual void enginePeerJoined (const juce::String& user) { juce::ignoreUnused (user); }
        virtual void enginePeerJoinFailed (const juce::String& user) { juce::ignoreUnused (user); }
        virtual void enginePeerLeft (const juce::String& user) { juce::ignoreUnused (user); }
        virtual void engineChat (const EngineChat&) {}
    };

    virtual ~Engine() = default;
    virtual void addListener (Listener*) = 0;
    virtual void removeListener (Listener*) = 0;

    /** 코덱 PCM 16bit, 이펙트 끔, 파일 소리 송신 등 MEON 기본값 */
    virtual void applyDefaults() = 0;

    // 서버·그룹
    virtual bool connect (const juce::String& host, int port, const juce::String& user, const juce::String& password) = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;
    virtual juce::String connectedUser() const = 0;
    virtual juce::String joinedGroup() const = 0;
    virtual bool joinGroup (const juce::String& name, const juce::String& password) = 0;
    virtual void leaveGroup (const juce::String& name) = 0;
    /** 엔진 수정 E1: 허용 목록 (enabled=false 면 끔) */
    virtual void setAllowedPeers (const juce::StringArray& users, bool enabled) = 0;

    // 피어
    virtual std::vector<EnginePeer> peers() const = 0;
    virtual void removePeer (const juce::String& user) = 0;
    virtual void setPeerReceive (const juce::String& user, bool on) = 0;   // 멤버 음소거
    virtual void setPeerGainDb (const juce::String& user, float db) = 0;
    virtual void setPeerPan (const juce::String& user, int pan) = 0;       // -100..100
    virtual float peerLevel (const juce::String& user, int channel) const = 0;   // 선형 피크
    virtual void applyPeerDefaults (const juce::String& user, BufferMode) = 0;

    // 나
    virtual bool isSendMuted() const = 0;
    virtual void setSendMuted (bool) = 0;
    virtual void setSendGainDb (float) = 0;
    virtual void setSelfMonitor (bool on) = 0;
    virtual void setSelfMonitorDb (float) {}            // 셀프 모니터 크기 (합주 화면 내 행의 볼륨)
    virtual void setEarGuardLevel (int) {}              // 귀 테러 방지 0 끔 / 1 보통 / 2 강하게
    virtual void setMasterDb (float) = 0;
    virtual void setMonitorMuted (bool) = 0;   // 내가 듣는 모든 소리 (Q12)
    virtual void setBufferMode (BufferMode) = 0;
    /** 독립 앱 입력 채널 (0 기반 시작, 1 = 모노 / 2 = 스테레오). 송신 채널 수도 같이 맞춘다. 플러그인은 무시 */
    virtual void setInputChannels (int start, int count) = 0;
    virtual int inputChannelCount() const = 0;          // 잡혀 있는 입력 채널 수 (0 이면 입력 없음)
    virtual juce::String inputChannelText() const = 0;  // 로그용 "1-2"
    virtual float inputLevel (int channel) const = 0;   // 입력 처리 뒤 (파일 소리 제외)
    virtual float sendLevel (int channel) const = 0;    // 실제로 보내는 신호
    virtual float monitorLevel (int channel) const = 0; // 내가 듣는 출력
    virtual int sendChannelCount() const = 0;
    virtual juce::String audioDescription() const = 0;  // 로그용 장치 정보

    virtual bool sendChat (const EngineChat&) = 0;

    // 파일 재생 (합주 화면의 파일 플레이어, 내 소리와 섞여 모두에게 간다). 기본은 없음 (테스트 엔진)
    virtual bool loadFile (const juce::File&)       { return false; }
    virtual void closeFile()                        {}
    virtual bool hasFile() const                    { return false; }
    virtual juce::String fileName() const           { return {}; }
    virtual bool isFilePlaying() const              { return false; }
    virtual void setFilePlaying (bool)              {}
    virtual double filePosition() const             { return 0.0; }   // 초
    virtual double fileLength() const               { return 0.0; }
    virtual void setFilePosition (double)           {}
    virtual float fileGainDb() const                { return 0.0f; }  // -30 ~ +10
    virtual void setFileGainDb (float)              {}
    static constexpr float fileMinDb = -30.0f, fileMaxDb = 10.0f;
};

} // namespace meon::core
