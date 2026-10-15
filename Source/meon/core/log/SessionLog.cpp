#include "SessionLog.h"

namespace meon::core
{

juce::var SessionLog::makeObject (std::initializer_list<std::pair<juce::Identifier, juce::var>> props)
{
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    for (auto& p : props)
        obj->setProperty (p.first, p.second);
    return juce::var (obj.get());
}

static juce::String nowIso()
{
    return juce::Time::getCurrentTime().toISO8601 (true);
}

void SessionLog::begin (const juce::String& roomCode, const juce::String& userName, bool isPlugin, const AudioInfo& audio)
{
    if (active)
        end ("restart");

    folder.createDirectory();
    // 같은 초에 세션이 또 시작되면 (다른 방으로 옮김 등) 앞 세션 파일을 덮지 않게 번호를 붙인다
    const auto stamp = "meon-session-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    file = folder.getChildFile (stamp + ".json");
    for (int n = 2; file.existsAsFile(); ++n)
        file = folder.getChildFile (stamp + "-" + juce::String (n) + ".json");

    root = makeObject ({
        { "app", "MEON" },
        { "version", juce::String (MEON_BUILD_VERSION) },
        { "base", "SonoBus 1.7.2" },
        { "mode", isPlugin ? "plugin" : "standalone" },
        { "os", juce::SystemStats::getOperatingSystemName() },
        { "cpu", juce::SystemStats::getCpuModel() },
        { "computer", juce::SystemStats::getComputerName() },
        { "roomCode", roomCode },
        { "userName", userName },
        { "sessionStart", nowIso() },
        { "sessionEnd", juce::var() },
        { "endReason", juce::var() },
        { "audio", makeObject ({
            { "deviceType", audio.deviceType },
            { "inputDevice", audio.inputDevice },
            { "outputDevice", audio.outputDevice },
            { "sampleRate", audio.sampleRate },
            { "bufferSize", audio.bufferSize },
            { "host", audio.host } }) },
        { "events", juce::Array<juce::var>() },
        { "samples", juce::Array<juce::var>() }
    });

    active = true;
    write();
}

void SessionLog::addEvent (const juce::String& type, const juce::String& detail)
{
    if (! active)
        return;
    if (auto* arr = root.getProperty ("events", juce::var()).getArray())
        arr->add (makeObject ({ { "t", nowIso() }, { "type", type }, { "detail", detail } }));
    write();
}

void SessionLog::addSample (juce::var sample)
{
    if (! active)
        return;
    if (auto* arr = root.getProperty ("samples", juce::var()).getArray())
        arr->add (sample);
    if (++samplesSinceWrite >= 2)
    {
        samplesSinceWrite = 0;
        write();
    }
}

void SessionLog::end (const juce::String& reason)
{
    if (! active)
        return;
    if (auto* obj = root.getDynamicObject())
    {
        obj->setProperty ("sessionEnd", nowIso());
        obj->setProperty ("endReason", reason);
    }
    write();
    active = false;
}

void SessionLog::write()
{
    if (file == juce::File())
        return;
    juce::TemporaryFile temp (file);
    {
        juce::FileOutputStream out (temp.getFile());
        if (out.openedOk())
            juce::JSON::writeToStream (out, root, false);
    }
    temp.overwriteTargetFileWithTemporary();
}

} // namespace meon::core
