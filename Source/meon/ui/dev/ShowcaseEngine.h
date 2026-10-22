// MEON dev - 소개 페이지 캡처 전용 합주 엔진 (--showcase, 빌드 0 전용). 실제 소리·네트워크 없이
// 정해 둔 멤버가 붙어 있는 것처럼 보이게 한다: 모두 연결됨, 핑은 거의 고정(2~4 ms), 레벨은 120 BPM 연주처럼 오르내린다.
// 레벨·핑은 시계(clockMs)의 함수이고 12초마다 똑같이 반복한다 (첫 화면 영상이 끊김 없이 이어지도록).
// 캡처 장치(Showcase)가 시계를 프레임 단위로 넘긴다. 시계를 넘기지 않으면 실제 시간으로 움직인다.
#pragma once

#include "../../core/jam/Engine.h"
#include <map>

namespace meon::ui::dev
{

class ShowcaseEngine : public core::Engine
{
public:
    static constexpr double loopMs = 12000.0;   // 120 BPM × 24박 (4/4 6마디)

    /** 캡처 시계 (ms). 음수면 실제 시간을 쓴다. 메시지 스레드에서만 */
    static double& clockMs() { static double t = -1.0; return t; }
    /** 지금 시각 (반복 주기 안, 0 ≤ t < loopMs) */
    static double now();

    /** 멤버의 파트 (레벨 모양을 고른다). 모르는 멤버는 기타처럼 */
    static void setPart (const juce::String& user, const juce::String& part) { parts()[user] = part; }
    /** 멤버의 핑 (기본값과 1 ms 차이가 나는 구간) */
    static void setPing (const juce::String& user, int baseMs, double bumpFromMs, double bumpToMs, int bumpMs)
    {
        pings()[user] = { baseMs, bumpFromMs, bumpToMs, bumpMs };
    }
    /** 레벨 (dBFS) — t 는 반복 주기 안의 ms. 화면에 쓰는 값은 지난 1/30초 동안의 최대값 */
    static float levelDb (const juce::String& part, int variant, int channel, double t);

    void addListener (Listener* l) override    { listeners.add (l); }
    void removeListener (Listener* l) override { listeners.remove (l); }
    void applyDefaults() override {}

    bool connect (const juce::String&, int, const juce::String& user, const juce::String&) override;
    void disconnect() override { connected = false; group = {}; }
    bool isConnected() const override { return connected; }
    juce::String connectedUser() const override { return me; }
    juce::String joinedGroup() const override { return group; }
    bool joinGroup (const juce::String& name, const juce::String&) override;
    void leaveGroup (const juce::String& name) override;
    void setAllowedPeers (const juce::StringArray& users, bool) override;

    std::vector<core::EnginePeer> peers() const override;
    void removePeer (const juce::String&) override {}
    void setPeerReceive (const juce::String&, bool) override {}
    void setPeerGainDb (const juce::String&, float) override {}
    void setPeerPan (const juce::String&, int) override {}
    float peerLevel (const juce::String& user, int channel) const override;
    void applyPeerDefaults (const juce::String&, BufferMode) override {}

    bool isSendMuted() const override { return false; }
    void setSendMuted (bool) override {}
    void setSendGainDb (float) override {}
    void setSelfMonitor (bool) override {}
    void setMasterDb (float) override {}
    void setMonitorMuted (bool) override {}
    void setBufferMode (BufferMode) override {}
    void setInputChannels (int, int) override {}
    int inputChannelCount() const override { return 2; }
    juce::String inputChannelText() const override { return "1-2"; }
    float inputLevel (int channel) const override  { return sendLevel (channel); }
    float sendLevel (int channel) const override;
    float monitorLevel (int channel) const override;
    int sendChannelCount() const override { return 2; }
    juce::String audioDescription() const override { return "showcase"; }

    // 파일 플레이어: 이름만 기억하는 가짜 파일 (소리 없음). 위치 1:24 / 길이 3:52 고정
    bool loadFile (const juce::File& f) override  { file = f.getFileName(); playing = false; return true; }
    void closeFile() override                     { file = {}; playing = false; }
    bool hasFile() const override                 { return file.isNotEmpty(); }
    juce::String fileName() const override        { return file; }
    bool isFilePlaying() const override           { return playing; }
    void setFilePlaying (bool b) override         { playing = b; }
    double filePosition() const override          { return 84.0; }
    double fileLength() const override            { return 232.0; }

    /** 내 메시지에는 멤버마다 바로 "ack:<id>" 를 돌려준다 (전달 확인) */
    bool sendChat (const core::EngineChat&) override;

private:
    struct Ping { int baseMs; double fromMs, toMs; int bumpMs; };
    static std::map<juce::String, juce::String>& parts() { static std::map<juce::String, juce::String> m; return m; }
    static std::map<juce::String, Ping>& pings()         { static std::map<juce::String, Ping> m; return m; }
    static float frameMax (const juce::String& part, int variant, int channel);

    juce::ListenerList<Listener> listeners;
    bool connected = false;
    juce::String me, group;
    juce::StringArray allowed;
    juce::String file;
    bool playing = false;
    void later (std::function<void()>);
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

public:
    ~ShowcaseEngine() override { *alive = false; }
};

} // namespace meon::ui::dev
