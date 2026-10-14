#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#else
 #include <fcntl.h>
 #include <unistd.h>
#endif
#include <atomic>

// 시스템 헤더를 JuceHeader 보다 먼저 넣는다 (TokenStore.cpp 와 같은 이유: macOS 의 Point 가 juce::Point 와 겹친다)
#include "AppLog.h"

namespace meon::core
{

using juce::String;

namespace
{
    // 크래시 처리기가 잠금 없이 쓸 수 있게 파일은 OS 핸들로 직접 연다 (덧붙이기 모드, 줄마다 바로 OS 에 넘긴다)
   #if defined (_WIN32)
    using Handle = HANDLE;
    const Handle noHandle = INVALID_HANDLE_VALUE;

    Handle openAppend (const juce::File& f)
    {
        return CreateFileW (f.getFullPathName().toWideCharPointer(), FILE_APPEND_DATA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    void writeAll (Handle h, const char* p, int n)  { DWORD done = 0; WriteFile (h, p, (DWORD) n, &done, nullptr); }
    void closeHandle (Handle h)                     { CloseHandle (h); }
    juce::int64 processId()                         { return (juce::int64) GetCurrentProcessId(); }
    const char* const newline = "\r\n";
   #else
    using Handle = int;
    const Handle noHandle = -1;

    Handle openAppend (const juce::File& f)
    {
        return ::open (f.getFullPathName().toRawUTF8(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    }
    void writeAll (Handle h, const char* p, int n)
    {
        while (n > 0)
        {
            const auto w = ::write (h, p, (size_t) n);
            if (w <= 0) return;
            p += w;
            n -= (int) w;
        }
    }
    void closeHandle (Handle h)                     { ::close (h); }
    juce::int64 processId()                         { return (juce::int64) ::getpid(); }
    const char* const newline = "\n";
   #endif

    struct State
    {
        juce::CriticalSection lock;
        std::atomic<Handle> handle { noHandle };
        juce::File file, marker;
        int opens = 0;
        juce::uint32 startCounter = 0;
        bool previousAbnormal = false;
    };

    State& state()
    {
        static auto* s = new State();   // 일부러 지우지 않는다: 종료 중·크래시 처리기에서도 남아 있게
        return *s;
    }

    String timestamp()
    {
        const auto t = juce::Time::getCurrentTime();
        return t.formatted ("%Y-%m-%d %H:%M:%S.") + String (t.getMilliseconds()).paddedLeft ('0', 3);
    }

    void writeLine (State& s, const String& line)
    {
        const auto h = s.handle.load();
        if (h == noHandle) return;
        const auto text = line + newline;
        writeAll (h, text.toRawUTF8(), (int) text.getNumBytesAsUTF8());
    }
}

void AppLog::open (const juce::File& file)
{
    auto& s = state();
    const juce::ScopedLock sl (s.lock);
    if (s.opens++ > 0) return;

    file.getParentDirectory().createDirectory();
    if (file.getSize() > maxBytes)
        file.moveFileTo (file.getSiblingFile (file.getFileNameWithoutExtension() + ".1" + file.getFileExtension()));

    // 실행 중 표시 파일: 정상 종료 때 지운다. 다음에 열 때 남아 있으면 지난 실행이 죽었거나 강제로 꺼진 것
    s.file = file;
    s.marker = file.getSiblingFile (file.getFileNameWithoutExtension() + ".running");
    s.previousAbnormal = s.marker.existsAsFile();
    const auto previous = s.previousAbnormal ? s.marker.loadFileAsString().trim() : String();
    s.marker.replaceWithText ("PID " + String (processId()) + LOGTXT (", 시작 ") + timestamp());

    s.handle = openAppend (file);
    s.startCounter = juce::Time::getMillisecondCounter();

    writeLine (s, "");
    writeLine (s, timestamp() + LOGTXT (" ==== 시작 ===="));
    if (s.previousAbnormal)
        writeLine (s, timestamp() + LOGTXT (" +0.0s [app] 지난 실행(") + previous + LOGTXT (")이 종료 기록 없이 끝났습니다. 앱이 죽었거나 강제로 꺼졌을 수 있습니다"));
}

void AppLog::close()
{
    auto& s = state();
    const juce::ScopedLock sl (s.lock);
    if (s.opens == 0 || --s.opens > 0) return;

    writeLine (s, timestamp() + LOGTXT (" ==== 정상 종료 ===="));
    const auto h = s.handle.exchange (noHandle);
    if (h != noHandle) closeHandle (h);
    s.marker.deleteFile();
}

void AppLog::write (const String& area, const String& message)
{
    auto& s = state();
    const juce::ScopedLock sl (s.lock);
    if (s.handle.load() == noHandle) return;
    // 실행한 뒤 지난 시간은 시계(NTP 등)가 바뀌어도 영향이 없는 카운터로 잰다
    const auto up = (double) (juce::Time::getMillisecondCounter() - s.startCounter) / 1000.0;
    writeLine (s, timestamp() + " +" + String (up, 1) + "s [" + area + "] " + message.replace ("\r", " ").replace ("\n", " "));
}

bool AppLog::isOpen()                       { return state().handle.load() != noHandle; }
juce::File AppLog::getFile()                { return state().file; }
bool AppLog::previousRunEndedAbnormally()   { return state().previousAbnormal; }

void AppLog::writeRawUnsafe (const char* utf8, int numBytes)
{
    const auto h = state().handle.load();
    if (h != noHandle && utf8 != nullptr && numBytes > 0) writeAll (h, utf8, numBytes);
}

} // namespace meon::core
