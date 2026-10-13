#include "MeonUpdater.h"

#if JUCE_MAC
 #include <unistd.h>
 #include <dlfcn.h>
 #include <string>
 #include <vector>
#endif

#ifndef MEON_BUILD_ID
 #define MEON_BUILD_ID 0
#endif

namespace meon
{

namespace
{
   #if JUCE_MAC
    const char* const releaseBase = "https://github.com/meonhq/desktop/releases/download/mac-latest/";
    const char* const packageName = "MEON-mac.zip";
   #else
    const char* const releaseBase = "https://github.com/meonhq/desktop/releases/download/windows-latest/";
    const char* const packageName = "MEON-Installer.exe";
   #endif

    std::unique_ptr<juce::InputStream> open (const juce::String& name, int timeoutMs, int* status)
    {
        return juce::URL (juce::String (releaseBase) + name)
                   .createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                           .withConnectionTimeoutMs (timeoutMs)
                                           .withNumRedirectsToFollow (5)
                                           .withStatusCode (status));
    }

    juce::File downloadFolder()
    {
        return juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("MEON-Update");
    }

   #if JUCE_MAC
    // 브라우저로 받은 dmg 에서 끌어다 놓은 앱은 격리 속성 때문에 macOS 가 읽기 전용 임시 위치
    // (…/AppTranslocation/…)로 옮겨서 실행한다 (App Translocation). 그때는 원래 자리(/Applications/Meon.app 등)를 찾는다.
    // CoreFoundation/Security 헤더는 JUCE 이름과 부딪혀서 함수만 dlsym 으로 가져온다.
    juce::File originalPathIfTranslocated (const juce::File& app)
    {
        if (! app.getFullPathName().contains ("/AppTranslocation/"))
            return app;

        using CFURLCreateFn  = void* (*) (void*, const unsigned char*, long, unsigned char);
        using CFURLGetPathFn = unsigned char (*) (void*, unsigned char, unsigned char*, long);
        using CFReleaseFn    = void (*) (void*);
        using OriginalFn     = void* (*) (void*, void**);

        static void* const security = dlopen ("/System/Library/Frameworks/Security.framework/Security", RTLD_LAZY);
        auto create   = (CFURLCreateFn)  dlsym (RTLD_DEFAULT, "CFURLCreateFromFileSystemRepresentation");
        auto getPath  = (CFURLGetPathFn) dlsym (RTLD_DEFAULT, "CFURLGetFileSystemRepresentation");
        auto release  = (CFReleaseFn)    dlsym (RTLD_DEFAULT, "CFRelease");
        auto original = security != nullptr ? (OriginalFn) dlsym (security, "SecTranslocateCreateOriginalPathForURL") : nullptr;
        if (create == nullptr || getPath == nullptr || release == nullptr || original == nullptr)
            return {};

        const auto path = app.getFullPathName().toStdString();
        juce::File result;
        if (auto* url = create (nullptr, (const unsigned char*) path.data(), (long) path.size(), 1))
        {
            if (auto* orig = original (url, nullptr))
            {
                char buffer[4096] = {};
                if (getPath (orig, 1, (unsigned char*) buffer, (long) sizeof (buffer)))
                    result = juce::File (juce::CharPointer_UTF8 (buffer));
                release (orig);
            }
            release (url);
        }
        return result;
    }

    // 실행 중인 Meon.app 의 원래 자리
    juce::File currentAppBundle()
    {
        return originalPathIfTranslocated (juce::File::getSpecialLocation (juce::File::currentApplicationFile));
    }

    bool canReplaceApp()
    {
        const auto app = currentAppBundle();
        return app.getFileExtension() == ".app" && app.isDirectory()
               && app.hasWriteAccess() && app.getParentDirectory().hasWriteAccess();
    }

    bool runTool (const juce::StringArray& args, int timeoutMs)
    {
        juce::ChildProcess p;
        return p.start (args, 0) && p.waitForProcessToFinish (timeoutMs) && p.getExitCode() == 0;
    }

    // 교체할 때 옛 앱을 잠시 옮겨 두는 곳 (숨김). 실행 중인 앱은 지우지 않고 다음에 켤 때 지운다.
    juce::File oldAppBundle (const juce::File& app)
    {
        return app.getSiblingFile (".MEON-old.app");
    }

    juce::File updateLog()
    {
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Logs/MEON/update.log");
    }

    void log (const juce::String& text)
    {
        auto f = updateLog();
        f.getParentDirectory().createDirectory();
        f.appendText (juce::Time::getCurrentTime().toString (true, true) + " " + text + "\n");
    }

    // 새 앱을 넣을 자리. 이름을 MEON.app 에서 Meon.app 으로 바꿨으므로, 옛 이름 그대로면 새 이름으로 넣는다.
    // (옛 앱은 먼저 옮겨 두므로 대소문자를 가리지 않는 볼륨에서도 겹치지 않는다. 사용자가 바꾼 이름은 그대로 둔다.)
    juce::File newAppBundle (const juce::File& app)
    {
        return app.getFileName() == "MEON.app" ? app.getSiblingFile ("Meon.app") : app;
    }

    // 실행 중인 앱을 옮겨 두고 dest 에 새 앱을 넣는다. 실행 중인 앱은 이미 메모리에 올라와 있어서 바꿔도 된다.
    // 실패하면 원래 앱을 되돌려 놓는다.
    bool replaceApp (const juce::File& newApp, const juce::File& app, const juce::File& dest)
    {
        const auto old = oldAppBundle (app);
        old.deleteRecursively();
        if (! app.moveFileTo (old))
        {
            log ("move old app failed: " + app.getFullPathName());
            return false;
        }
        // 같은 볼륨이면 이름만 바꾸고, 아니면 ditto 로 복사한다 (서명·심볼릭 링크 보존)
        if (! newApp.moveFileTo (dest)
            && ! runTool ({ "/usr/bin/ditto", newApp.getFullPathName(), dest.getFullPathName() }, 120000))
        {
            log ("put new app failed, restoring");
            dest.deleteRecursively();
            old.moveFileTo (app);
            return false;
        }
        runTool ({ "/usr/bin/xattr", "-dr", "com.apple.quarantine", dest.getFullPathName() }, 30000);
        log ("replaced " + app.getFullPathName() + " -> " + dest.getFullPathName());
        return true;
    }

    // 플러그인 따라 바꾸기: CI 가 AU / VST3 를 Meon.app/Contents/Resources/MeonPlugins.zip 에 넣고,
    // 각 플러그인 Info.plist 에 MeonBuildID(빌드 번호)를 적는다 (mac.yml).
    // 앱은 켤 때 사용자 폴더(~/Library/Audio/Plug-Ins)에 설치된 Meon 플러그인이 자기보다 옛 빌드면 앱 안의 것으로 바꾼다.
    // 설치돼 있지 않은 플러그인은 새로 깔지 않는다. MeonBuildID 가 없는 플러그인은 이 기능 이전 빌드라 0 으로 본다.
    int pluginBuild (const juce::File& bundle)
    {
        auto xml = juce::XmlDocument::parse (bundle.getChildFile ("Contents/Info.plist"));
        if (xml == nullptr)
            return 0;
        if (auto* dict = xml->getChildByName ("dict"))
            for (auto* e = dict->getFirstChildElement(); e != nullptr; e = e->getNextElement())
                if (e->hasTagName ("key") && e->getAllSubText() == "MeonBuildID")
                    if (auto* v = e->getNextElement())
                        return v->getAllSubText().trim().getIntValue();
        return 0;
    }

    bool replaceBundle (const juce::File& newBundle, const juce::File& dest, const juce::File& staging)
    {
        const auto old = staging.getChildFile ("old-" + dest.getFileName());
        old.deleteRecursively();
        if (! dest.moveFileTo (old))
        {
            log ("plugin: move old failed: " + dest.getFullPathName());
            return false;
        }
        if (! newBundle.moveFileTo (dest)
            && ! runTool ({ "/usr/bin/ditto", newBundle.getFullPathName(), dest.getFullPathName() }, 120000))
        {
            log ("plugin: put new failed, restoring: " + dest.getFullPathName());
            dest.deleteRecursively();
            old.moveFileTo (dest);
            return false;
        }
        old.deleteRecursively();
        runTool ({ "/usr/bin/xattr", "-dr", "com.apple.quarantine", dest.getFullPathName() }, 30000);
        log ("plugin: replaced " + dest.getFullPathName());
        return true;
    }

    void syncPlugins()
    {
        const auto zip = juce::File::getSpecialLocation (juce::File::currentApplicationFile)
                             .getChildFile ("Contents/Resources/MeonPlugins.zip");
        if (! zip.existsAsFile())
            return;

        const auto plugIns = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Audio/Plug-Ins");
        const juce::File targets[] { plugIns.getChildFile ("Components/Meon.component"), plugIns.getChildFile ("VST3/Meon.vst3") };

        juce::Array<juce::File> stale;
        for (auto& t : targets)
            if (t.isDirectory() && pluginBuild (t) < MEON_BUILD_ID)
                stale.add (t);
        if (stale.isEmpty())
            return;

        // 같은 볼륨(사용자 폴더)에 풀어서 바꿔 넣기가 이름 바꾸기로 끝나게 한다
        const auto staging = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                 .getChildFile ("Application Support/MEON/PluginUpdate");
        staging.deleteRecursively();
        const auto unpacked = staging.getChildFile ("unpacked");
        if (! unpacked.createDirectory()
            || ! runTool ({ "/usr/bin/ditto", "-x", "-k", zip.getFullPathName(), unpacked.getFullPathName() }, 120000))
        {
            log ("plugin: unpack failed");
            return;
        }

        bool componentReplaced = false;
        for (auto& t : stale)
        {
            // 옛 이름(MEON.component 등)도 대소문자를 가리지 않는 볼륨이라 여기서 찾아지고, 새 이름으로 바뀐다
            const auto src = unpacked.getChildFile (t.getFileName());
            if (! src.isDirectory() || ! runTool ({ "/usr/bin/codesign", "--verify", "--deep", src.getFullPathName() }, 60000))
            {
                log ("plugin: bad package for " + t.getFileName());
                continue;
            }
            if (! t.getParentDirectory().hasWriteAccess())
            {
                log ("plugin: no write access: " + t.getFullPathName());
                continue;
            }
            if (replaceBundle (src, t, staging) && t.getFileExtension() == ".component")
                componentReplaced = true;
        }
        staging.deleteRecursively();

        // AU 목록 캐시를 새로 읽게 한다 (필요할 때 macOS 가 다시 띄운다). 열려 있는 DAW 는 다시 켜야 새 플러그인을 쓴다.
        if (componentReplaced)
            runTool ({ "/usr/bin/killall", "-9", "AudioComponentRegistrar" }, 10000);
    }
   #endif
}

MeonUpdater::MeonUpdater() : juce::Thread ("MEON updater")
{
   #if JUCE_MAC
    if (isSupported())
    {
        // 지난 업데이트에서 남은 옛 앱 정리. 업데이트 직후에는 옛 앱이 아직 꺼지는 중일 수 있어서 잠시 기다린다.
        const auto old = oldAppBundle (currentAppBundle());
        juce::Thread::launch ([old]
        {
            syncPlugins();
            juce::Thread::sleep (10000);
            old.deleteRecursively();
        });
    }
   #endif
}

MeonUpdater::~MeonUpdater()
{
    cancelPendingUpdate();
    stopThread (5000);
}

bool MeonUpdater::isSupported()
{
   #if JUCE_WINDOWS
    return MEON_BUILD_ID > 0 && juce::JUCEApplicationBase::isStandaloneApp();
   #elif JUCE_MAC
    // 앱을 바꿔 넣을 수 없는 곳(dmg 안 등)이어도 확인은 하고, 받을 때 안내한다 (runDownload)
    return MEON_BUILD_ID > 0 && juce::JUCEApplicationBase::isStandaloneApp();
   #else
    return false;
   #endif
}

int MeonUpdater::currentBuild() { return MEON_BUILD_ID; }

MeonUpdater::State MeonUpdater::getState() const
{
    const juce::ScopedLock sl (lock);
    return state;
}

void MeonUpdater::setState (State s)
{
    {
        const juce::ScopedLock sl (lock);
        state = s;
    }
    sendChangeMessage();
}

void MeonUpdater::start (Job j, State s)
{
    if (! isSupported() || isThreadRunning())
        return;
    {
        const juce::ScopedLock sl (lock);
        job = j;
    }
    setState (s);
    startThread();
}

void MeonUpdater::check()
{
    const auto s = getState();
    if (s == State::Checking || s == State::Downloading || s == State::Ready)
        return;
    start (Job::Check, State::Checking);
}

void MeonUpdater::download()
{
    if (getState() != State::Available)
        return;
    progress = 0.0f;
    start (Job::Download, State::Downloading);
}

void MeonUpdater::run()
{
    Job j;
    {
        const juce::ScopedLock sl (lock);
        j = job;
    }
    failedOnDownload = (j == Job::Download);
    failedOnPermission = false;
    if (j == Job::Check)
    {
        if (! runCheck())
            setState (State::Failed);
    }
    else if (j == Job::Download)
    {
        if (runDownload())
        {
            setState (State::Ready);
            triggerAsyncUpdate();
        }
        else if (! threadShouldExit())
        {
            setState (State::Failed);
        }
    }
}

bool MeonUpdater::runCheck()
{
    int status = 0;
    auto in = open ("build.txt", 10000, &status);
    if (in == nullptr || status != 200)
        return false;
    const auto text = in->readEntireStreamAsString().trim();
    if (! text.containsOnly ("0123456789") || text.isEmpty())
        return false;
    latestBuild = text.getIntValue();
    setState (latestBuild.load() > currentBuild() ? State::Available : State::UpToDate);
    return true;
}

bool MeonUpdater::runDownload()
{
   #if JUCE_MAC
    // dmg 안에서 바로 켰거나 쓰기 권한이 없는 곳이면 앱을 바꿔 넣을 수 없다
    if (! canReplaceApp())
    {
        failedOnPermission = true;
        return false;
    }
   #endif
    auto folder = downloadFolder();
    folder.deleteRecursively();   // 지난번에 받은 설치 프로그램 정리
    if (! folder.createDirectory())
        return false;
   #if JUCE_MAC
    auto target = folder.getChildFile ("MEON-mac-" + juce::String (latestBuild.load()) + ".zip");
   #else
    auto target = folder.getChildFile ("MEON-Installer-" + juce::String (latestBuild.load()) + ".exe");
   #endif

    int status = 0;
    auto in = open (packageName, 15000, &status);
    if (in == nullptr || status != 200)
        return false;

    const auto total = in->getTotalLength();
    juce::int64 done = 0;
    {
        juce::FileOutputStream out (target);
        if (! out.openedOk())
            return false;
        juce::HeapBlock<char> buffer (64 * 1024);
        while (! in->isExhausted())
        {
            if (threadShouldExit())
                return false;
            const int n = in->read (buffer, 64 * 1024);
            if (n < 0)
                return false;
            if (n == 0)
                break;
            if (! out.write (buffer, (size_t) n))
                return false;
            done += n;
            const float p = total > 0 ? (float) ((double) done / (double) total) : -1.0f;
            if (std::abs (p - progress.load()) >= 0.01f || p < 0.0f)
            {
                progress = p;
                sendChangeMessage();
            }
        }
        out.flush();
    }

    // 잘린 파일이나 오류 페이지를 실행하지 않도록: 길이와 파일 머리(Windows MZ, macOS zip 의 PK)를 확인한다
    if ((total > 0 && done != total) || done < 1024 * 1024)
        return false;
    {
        juce::FileInputStream check (target);
       #if JUCE_MAC
        if (! check.openedOk() || check.readByte() != 'P' || check.readByte() != 'K')
            return false;
       #else
        if (! check.openedOk() || check.readByte() != 'M' || check.readByte() != 'Z')
            return false;
       #endif
    }

   #if JUCE_MAC
    // zip 을 풀고 (ditto 는 서명·심볼릭 링크를 그대로 살린다) 서명이 온전한지 확인한다
    auto unpacked = folder.getChildFile ("unpacked");
    if (threadShouldExit()
        || ! runTool ({ "/usr/bin/ditto", "-x", "-k", target.getFullPathName(), unpacked.getFullPathName() }, 120000))
        return false;
    auto app = unpacked.getChildFile ("Meon.app");
    if (! app.getChildFile ("Contents/MacOS/Meon").existsAsFile()
        || ! runTool ({ "/usr/bin/codesign", "--verify", "--deep", app.getFullPathName() }, 60000))
        return false;
    target.deleteFile();
    target = app;
   #endif

    const juce::ScopedLock sl (lock);
    installer = target;
    return true;
}

void MeonUpdater::handleAsyncUpdate()
{
    if (onDownloaded)
        onDownloaded();
}

bool MeonUpdater::launchInstaller()
{
    juce::File file;
    {
        const juce::ScopedLock sl (lock);
        if (state != State::Ready)
            return false;
        file = installer;
    }
   #if JUCE_MAC
    // 앱을 먼저 바꿔 넣는다. 다시 켜기는 설정을 저장한 뒤 relaunchAfterInstall() 이 한다.
    const auto current = currentAppBundle();
    const auto app = newAppBundle (current);
    if (! replaceApp (file, current, app))
    {
        setState (State::Failed);
        return false;
    }
    {
        const juce::ScopedLock sl (lock);
        installedApp = app;
    }
    return true;   // 이미 바꿔 넣었으니 종료한다
   #else
    // /SILENT: 묻는 것 없이 진행 막대만 보여 준다. /UPDATE=1: 설치가 끝나면 앱을 다시 켠다 (wininstaller.iss).
    // ShellExecute 라서 관리자 권한 확인 창을 거친다. 거기서 취소하면 false.
    if (juce::Process::openDocument (file.getFullPathName(), "/SILENT /SUPPRESSMSGBOXES /NORESTART /UPDATE=1"))
        return true;
    setState (State::Ready);   // 다시 누를 수 있게 그대로 둔다
    return false;
   #endif
}

void MeonUpdater::relaunchAfterInstall()
{
   #if JUCE_MAC
    // 꺼지기 직전에 LaunchServices 에 새 앱을 새 인스턴스로 켜 달라고 한다 (open -n).
    // 앱이 띄운 대기 프로세스(셸, setsid, launchd 작업)는 모두 앱이 꺼질 때 함께 사라지거나 돌지 않았다 (build 9~28).
    // LaunchServices 가 띄운 앱은 이 앱의 자식이 아니라서 남는다. 옛 앱은 곧바로 꺼진다.
    juce::File app;
    {
        const juce::ScopedLock sl (lock);
        app = installedApp;
    }
    if (app == juce::File())
        return;
    if (runTool ({ "/usr/bin/open", "-n", app.getFullPathName() }, 15000))
        log ("relaunch requested (open -n)");
    else
        log ("relaunch failed (open -n)");
   #endif
}

} // namespace meon
