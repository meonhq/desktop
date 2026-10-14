#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <dbghelp.h>
 #include <shlobj.h>
 #include <knownfolders.h>
 #include <signal.h>
 #include <stdlib.h>
 #include <stdio.h>
 #include <wchar.h>
 #include <atomic>
 #if defined (_MSC_VER)
  #pragma comment (lib, "shell32.lib")
  #pragma comment (lib, "ole32.lib")
 #endif
#endif

// 시스템 헤더를 JuceHeader 보다 먼저 넣는다 (TokenStore.cpp 와 같은 이유)
#include "MeonCrashDump.h"
#include "core/log/AppLog.h"

namespace meon::crashdump
{

#if defined (_WIN32)
namespace
{
    using WriteDumpFn = BOOL (WINAPI*) (HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                        PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);

    // 크래시 처리 중에는 메모리 할당·잠금을 피한다: 경로와 함수는 미리 준비해 둔다
    wchar_t meonDir[MAX_PATH] {};
    wchar_t dumpDir[MAX_PATH] {};
    WriteDumpFn writeDump = nullptr;
    LPTOP_LEVEL_EXCEPTION_FILTER previousFilter = nullptr;
    std::atomic<bool> installed { false }, handling { false };

    struct Job
    {
        EXCEPTION_POINTERS* ep;
        DWORD threadId;
        wchar_t path[MAX_PATH];
        BOOL ok;
        DWORD error;
    };

    DWORD WINAPI dumpThread (LPVOID p)
    {
        auto* job = static_cast<Job*> (p);
        CreateDirectoryW (meonDir, nullptr);
        CreateDirectoryW (dumpDir, nullptr);
        HANDLE f = CreateFileW (job->path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) { job->error = GetLastError(); return 0; }

        MINIDUMP_EXCEPTION_INFORMATION info {};
        info.ThreadId = job->threadId;
        info.ExceptionPointers = job->ep;
        info.ClientPointers = FALSE;
        // 스택과 스택이 가리키는 메모리, 스레드 정보까지 (보통 수 MB)
        const auto type = (MINIDUMP_TYPE) (MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
        if (writeDump != nullptr)
            job->ok = writeDump (GetCurrentProcess(), GetCurrentProcessId(), f, type, job->ep != nullptr ? &info : nullptr, nullptr, nullptr);
        if (! job->ok) job->error = writeDump == nullptr ? (DWORD) ERROR_PROC_NOT_FOUND : GetLastError();
        CloseHandle (f);
        if (! job->ok) DeleteFileW (job->path);
        return 0;
    }

    int toUtf8 (const wchar_t* w, char* out, int size)
    {
        const int n = WideCharToMultiByte (CP_UTF8, 0, w, -1, out, size, nullptr, nullptr);
        return n > 0 ? n - 1 : 0;
    }

    /** 죽은 곳: "모듈+오프셋" (PDB 와 맞춰 보면 함수가 나온다) */
    void describeAddress (void* addr, char* out, int size)
    {
        HMODULE mod = nullptr;
        wchar_t path[MAX_PATH] {};
        if (addr != nullptr
            && GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR) addr, &mod)
            && GetModuleFileNameW (mod, path, MAX_PATH) > 0)
        {
            const wchar_t* name = wcsrchr (path, L'\\');
            char name8[MAX_PATH * 3] {};
            toUtf8 (name != nullptr ? name + 1 : path, name8, (int) sizeof (name8));
            snprintf (out, (size_t) size, "%s+0x%llX", name8, (unsigned long long) ((const char*) addr - (const char*) mod));
        }
        else
            snprintf (out, (size_t) size, "%p", addr);
    }

    void append (char* line, int size, int& n, const char* text)
    {
        if (n >= size - 1) return;
        const int w = snprintf (line + n, (size_t) (size - n), "%s", text);
        if (w > 0) n = (n + w < size - 1) ? n + w : size - 1;
    }

    void handleCrash (EXCEPTION_POINTERS* ep, const char* what)
    {
        if (handling.exchange (true)) return;   // 처리하다 또 죽었거나 abort 가 이어서 온 경우: 한 번만

        Job job {};
        job.ep = ep;
        job.threadId = GetCurrentThreadId();
        SYSTEMTIME t;
        GetLocalTime (&t);
        swprintf (job.path, MAX_PATH, L"%ls\\Meon-%04u%02u%02u-%02u%02u%02u-%lu.dmp", dumpDir,
                  (unsigned) t.wYear, (unsigned) t.wMonth, (unsigned) t.wDay, (unsigned) t.wHour, (unsigned) t.wMinute, (unsigned) t.wSecond,
                  (unsigned long) GetCurrentProcessId());

        // 덤프는 새 스레드에서 쓴다: 스택이 넘쳐 죽었으면(stack overflow) 이 스레드에는 남은 스택이 거의 없다
        if (HANDLE th = CreateThread (nullptr, 0, dumpThread, &job, 0, nullptr))
        {
            WaitForSingleObject (th, 120000);
            CloseHandle (th);
        }
        else
            dumpThread (&job);

        // app.log 에 한 줄 (잠금·할당 없이)
        const auto* rec = ep != nullptr ? ep->ExceptionRecord : nullptr;
        char where[MAX_PATH * 3 + 32] {};
        describeAddress (rec != nullptr ? rec->ExceptionAddress : nullptr, where, (int) sizeof (where));

        char line[2048];
        int n = 0;
        char part[1024];
        snprintf (part, sizeof (part), "%04u-%02u-%02u %02u:%02u:%02u.%03u [crash] 앱이 죽었습니다: %s, 예외 0x%08lX, 위치 %s, 스레드 %lu. ",
                  (unsigned) t.wYear, (unsigned) t.wMonth, (unsigned) t.wDay, (unsigned) t.wHour, (unsigned) t.wMinute, (unsigned) t.wSecond,
                  (unsigned) t.wMilliseconds, what, (unsigned long) (rec != nullptr ? rec->ExceptionCode : 0), where, (unsigned long) job.threadId);
        append (line, (int) sizeof (line), n, part);
        if (job.ok)
        {
            char path8[MAX_PATH * 3] {};
            toUtf8 (job.path, path8, (int) sizeof (path8));
            snprintf (part, sizeof (part), "덤프: %s\r\n", path8);
        }
        else
            snprintf (part, sizeof (part), "덤프 저장 실패 (오류 %lu)\r\n", (unsigned long) job.error);
        append (line, (int) sizeof (line), n, part);
        meon::core::AppLog::writeRawUnsafe (line, n);
    }

    LONG WINAPI onUnhandledException (EXCEPTION_POINTERS* ep)
    {
        handleCrash (ep, "처리되지 않은 예외");
        // 그 뒤는 Windows 에 맡긴다 (이벤트 뷰어의 Application Error 기록, 프로세스 종료)
        return previousFilter != nullptr ? previousFilter (ep) : EXCEPTION_CONTINUE_SEARCH;
    }

    /** 예외가 아닌 길(CRT 오류·abort)로 죽을 때: 지금 위치를 예외처럼 만들어 덤프한다 */
    void crashHere (DWORD code, const char* what)
    {
        CONTEXT ctx {};
        RtlCaptureContext (&ctx);
        EXCEPTION_RECORD rec {};
        rec.ExceptionCode = code;
       #if defined (_M_ARM64) || defined (__aarch64__)
        rec.ExceptionAddress = (PVOID) ctx.Pc;
       #elif defined (_M_X64) || defined (__x86_64__)
        rec.ExceptionAddress = (PVOID) ctx.Rip;
       #else
        rec.ExceptionAddress = (PVOID) ctx.Eip;
       #endif
        EXCEPTION_POINTERS ep { &rec, &ctx };
        handleCrash (&ep, what);
    }

    void __cdecl onPureCall()
    {
        crashHere (0xE0000001, "순수 가상 함수 호출 (지워진 객체 사용 가능성)");
        TerminateProcess (GetCurrentProcess(), 0xE0000001);
    }

    void __cdecl onInvalidParameter (const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t)
    {
        crashHere (0xC0000417, "CRT 함수에 잘못된 인자");
        TerminateProcess (GetCurrentProcess(), 0xC0000417);
    }

    void __cdecl onAbort (int)
    {
        crashHere (0xE0000003, "abort (처리되지 않은 C++ 예외 등)");
        // 돌아가면 abort 가 이어서 프로세스를 끝낸다
    }
}

bool installForStandalone()
{
    if (! installed)
    {
        wchar_t exe[MAX_PATH] {};
        if (GetModuleFileNameW (nullptr, exe, MAX_PATH) == 0) return false;
        const wchar_t* name = wcsrchr (exe, L'\\');
        if (_wcsicmp (name != nullptr ? name + 1 : exe, L"Meon.exe") != 0) return false;   // DAW 안의 플러그인 등

        PWSTR appData = nullptr;
        if (FAILED (SHGetKnownFolderPath (FOLDERID_RoamingAppData, 0, nullptr, &appData))) return false;
        swprintf (meonDir, MAX_PATH, L"%ls\\MEON", appData);
        swprintf (dumpDir, MAX_PATH, L"%ls\\MEON\\crashes", appData);
        CoTaskMemFree (appData);

        // 죽은 뒤에 DLL 을 읽지 않게 미리 읽어 둔다 (시스템 폴더의 것만)
        if (auto lib = LoadLibraryExW (L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32))
            writeDump = (WriteDumpFn) (void*) GetProcAddress (lib, "MiniDumpWriteDump");

       #if defined (_MSC_VER)
        _set_purecall_handler (onPureCall);
        _set_invalid_parameter_handler (onInvalidParameter);
        _set_abort_behavior (0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);   // abort 가 Windows 오류 보고로 바로 가지 않고 아래 신호로 오게
       #else
        (void) onPureCall;
        (void) onInvalidParameter;
       #endif
        signal (SIGABRT, onAbort);
        installed = true;
    }

    // 다시 부르면 처리기를 다시 건다: 그사이 오디오 드라이버 등이 바꿔 놓았으면 그것은 뒤에 이어 부른다
    const auto prev = SetUnhandledExceptionFilter (onUnhandledException);
    if (prev != onUnhandledException) previousFilter = prev;
    return true;
}

bool isInstalled()   { return installed; }

#else
bool installForStandalone()   { return false; }
bool isInstalled()            { return false; }
#endif

juce::File getFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("MEON").getChildFile ("crashes");
}

void tidyAndLog (int keep)
{
    using meon::core::AppLog;
   #if JUCE_WINDOWS
    if (! isInstalled())
    {
        AppLog::write ("app", LOGTXT ("크래시 덤프: 꺼짐 (플러그인이거나 걸지 못함)"));
        return;
    }
    auto dumps = getFolder().findChildFiles (juce::File::findFiles, false, "*.dmp");
    std::sort (dumps.begin(), dumps.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getLastModificationTime() > b.getLastModificationTime(); });
    for (int i = keep; i < dumps.size(); ++i) dumps.getReference (i).deleteFile();
    dumps.removeRange (keep, dumps.size());

    AppLog::write ("app", LOGTXT ("크래시 덤프: 켜짐 (") + getFolder().getFullPathName() + LOGTXT ("), 지금 ") + juce::String (dumps.size()) + LOGTXT ("개")
                          + (dumps.isEmpty() ? juce::String() : LOGTXT (", 최근 ") + dumps.getFirst().getFileName()));
   #elif JUCE_MAC
    juce::ignoreUnused (keep);
    AppLog::write ("app", LOGTXT ("크래시 보고서: macOS 가 ~/Library/Logs/DiagnosticReports 에 남김 (Meon-*.ips)"));
   #else
    juce::ignoreUnused (keep);
   #endif
}

} // namespace meon::crashdump
