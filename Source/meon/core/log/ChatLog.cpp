#include "ChatLog.h"
#include "AppLog.h"

namespace meon::core
{

ChatLog::ChatLog (juce::File chatFolder, Clock c) : folder (std::move (chatFolder)), clock (std::move (c)) {}

void ChatLog::begin (const juce::String& s, const juce::String& roomTitle, const juce::String& roomCode)
{
    if (active) end();
    stamp = s.isNotEmpty() ? s : "meon-session-" + now().formatted ("%Y%m%d-%H%M%S");
    title = roomTitle;
    code = roomCode;
    startedAt = now();
    file = juce::File();
    failed = false;
    active = true;
}

juce::String ChatLog::formatLine (juce::Time t, Kind kind, const juce::String& name, const juce::String& text)
{
    juce::String out ("[" + t.formatted ("%H:%M:%S") + "] ");
    out << (kind == Kind::system ? juce::String ("* ") : name + ": ");
    const auto lines = juce::StringArray::fromLines (text);
    for (int i = 0; i < lines.size(); ++i)
        out << (i > 0 ? "\n  " : "") << lines[i];
    return out + "\n";
}

bool ChatLog::openFile()
{
    if (file != juce::File()) return true;
    if (failed || folder == juce::File()) return false;   // 로그 폴더가 없는 설정(일부 테스트)은 기록하지 않는다
    if (! folder.createDirectory())
    {
        failed = true;
        AppLog::write ("chat", LOGTXT ("채팅 기록 폴더를 만들지 못함: ") + folder.getFullPathName());
        return false;
    }
    auto f = folder.getChildFile (stamp + ".txt");
    for (int n = 2; f.exists(); ++n)
        f = folder.getChildFile (stamp + "-" + juce::String (n) + ".txt");
    file = f;
    juce::String head (LOGTXT ("# Meon 채팅 기록\n"));
    head << LOGTXT ("# 방: ") << title << (code.isNotEmpty() ? " (" + code + ")" : juce::String()) << "\n";
    head << LOGTXT ("# 시작: ") << startedAt.formatted ("%Y-%m-%d %H:%M:%S") << "\n";
    append (head);
    return ! failed;
}

void ChatLog::append (const juce::String& s)
{
    if (failed || file == juce::File()) return;
    // 줄마다 열고 닫는다: 앱이 강제 종료돼도 그때까지 쓴 줄은 남는다
    juce::FileOutputStream out (file);
    if (out.failedToOpen() || ! out.writeText (s, false, false, nullptr))
    {
        failed = true;
        AppLog::write ("chat", LOGTXT ("채팅 기록을 쓰지 못함: ") + file.getFullPathName() + " (" + out.getStatus().getErrorMessage() + ")");
        return;
    }
    out.flush();
}

void ChatLog::add (Kind kind, const juce::String& name, const juce::String& text)
{
    if (! active || ! openFile()) return;
    append (formatLine (now(), kind, name, text));
}

void ChatLog::end()
{
    if (! active) return;
    if (file != juce::File())
        append (LOGTXT ("# 끝: ") + now().formatted ("%Y-%m-%d %H:%M:%S") + "\n");
    active = false;
}

} // namespace meon::core
