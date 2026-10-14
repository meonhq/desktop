// MEON core - 세션 로그 (로컬 JSON). 방에 있는 동안 멤버별 핑·지연·지터버퍼·패킷 손실·끊김 횟수를 주기적으로 기록한다.
#pragma once

#include <JuceHeader.h>

namespace meon::core
{

class SessionLog
{
public:
    struct AudioInfo
    {
        juce::String deviceType, inputDevice, outputDevice, host;
        double sampleRate = 0.0;
        int bufferSize = 0;
    };

    explicit SessionLog (juce::File logFolder) : folder (std::move (logFolder)) {}

    void begin (const juce::String& roomCode, const juce::String& userName, bool isPlugin, const AudioInfo& audio);
    void addEvent (const juce::String& type, const juce::String& detail);
    void addSample (juce::var sample);
    void end (const juce::String& reason);
    bool isActive() const { return active; }
    juce::File getFile() const { return file; }

    juce::File getFolder() const { return folder; }
    static juce::var makeObject (std::initializer_list<std::pair<juce::Identifier, juce::var>> props);

private:
    juce::File folder;
    juce::var root;
    juce::File file;
    bool active = false;
    int samplesSinceWrite = 0;
    void write();
};

} // namespace meon::core
