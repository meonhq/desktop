#include "MeonUpdater.h"
#include "MeonSettings.h"
#include "core/log/AppLog.h"

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
    // GitHub 릴리스 (브리지 전 앱도 보는 곳, 이름을 바꾸지 않는다: CLAUDE.md). dl 에 검증된 매니페스트가 없을 때만 쓴다
   #if JUCE_MAC
    const char* const releaseBase = "https://github.com/meonhq/desktop/releases/download/mac-latest/";
    const char* const packageName = "MEON-mac.zip";
    const char* const platformName = "mac";
   #else
    const char* const releaseBase = "https://github.com/meonhq/desktop/releases/download/windows-latest/";
    const char* const packageName = "MEON-Installer.exe";
    const char* const platformName = "windows";
   #endif

    // 기록: 로그 폴더의 update.log (두 플랫폼) 와 app.log. 1 MB 를 넘으면 update.log.1 로 한 번 옮긴다
    juce::File updateLog()
    {
        return MeonSettings::getLogFolder().getChildFile ("update.log");
    }

    void log (const juce::String& text)
    {
        static juce::CriticalSection logLock;
        const juce::ScopedLock sl (logLock);
        auto f = updateLog();
        f.getParentDirectory().createDirectory();
        if (f.getSize() > 1024 * 1024)
        {
            const auto old = f.getSiblingFile ("update.log.1");
            old.deleteFile();
            f.moveFileTo (old);
        }
        f.appendText (juce::Time::getCurrentTime().toString (true, true) + " " + text + "\n");
        core::AppLog::write ("update", text);
    }

    // 설치마다 한 번 만드는 ID (단계적 배포 rollout 의 나누기에만 쓴다. 기기 밖으로 보내지 않는다)
    juce::String installId()
    {
        const auto f = MeonSettings::getSettingsFolder().getChildFile ("update-id.txt");
        auto id = f.loadFileAsString().trim();
        if (id.length() < 16)
        {
            id = juce::Uuid().toString();
            f.getParentDirectory().createDirectory();
            f.replaceWithText (id);
        }
        return id;
    }

    // 믿는 키로 서명된 매니페스트가 알린 폐기 키 (한 줄에 하나). 이 키로 서명된 매니페스트는 다시 믿지 않는다
    juce::File revokedKeysFile()
    {
        return MeonSettings::getSettingsFolder().getChildFile ("update-revoked-keys.txt");
    }

    juce::StringArray loadRevokedKeys()
    {
        juce::StringArray keys;
        keys.addLines (revokedKeysFile().loadFileAsString());
        keys.trim();
        keys.removeEmptyStrings();
        return keys;
    }

    std::unique_ptr<juce::InputStream> open (const juce::String& name, int timeoutMs, int* status)
    {
        return juce::URL (juce::String (releaseBase) + name)
                   .createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                           .withConnectionTimeoutMs (timeoutMs)
                                           .withNumRedirectsToFollow (5)
                                           .withStatusCode (status));
    }

    // dl 의 파일: 리디렉션을 따르지 않는다 (주소는 서명한 매니페스트에 있고 https://dl.meon.co.kr/ 아래만 믿는다)
    std::unique_ptr<juce::InputStream> openDl (const juce::String& url, int timeoutMs, int* status)
    {
        return juce::URL (url).createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                      .withConnectionTimeoutMs (timeoutMs)
                                                      .withNumRedirectsToFollow (0)
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

void MeonUpdater::setChannel (const juce::String& c)
{
    const juce::ScopedLock sl (lock);
    channel = c == "beta" ? "beta" : "stable";
}

void MeonUpdater::setForced (bool f)
{
    const juce::ScopedLock sl (lock);
    forced = f;
}

update::Source MeonUpdater::getSource() const
{
    const juce::ScopedLock sl (lock);
    return source;
}

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
    const auto s = getState();
    progress = 0.0f;
    if (s == State::Available)
        start (Job::Download, State::Downloading);
    else if (s == State::Failed)
        start (Job::CheckThenDownload, State::Checking);   // 다시 시도: 출처(dl·GitHub)부터 다시 고른다
}

void MeonUpdater::run()
{
    Job j;
    {
        const juce::ScopedLock sl (lock);
        j = job;
    }
    failedOnDownload = (j == Job::Download || j == Job::CheckThenDownload);
    failedOnPermission = false;
    if (j == Job::Download)
        problem = (int) Problem::none;
    if (j == Job::Check || j == Job::CheckThenDownload)
    {
        if (! runCheck())
        {
            setState (State::Failed);
            return;
        }
        if (j == Job::Check || getState() != State::Available)
            return;
        setState (State::Downloading);
    }
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

bool MeonUpdater::runCheck()
{
    update::CheckInput in;
    {
        const juce::ScopedLock sl (lock);
        in.channel = channel;
        in.forced = forced;
    }
    in.platform = platformName;
    in.currentBuild = currentBuild();
    in.osVersion = update::currentOsVersion();
    in.installId = installId();
    in.revokedKeys = loadRevokedKeys();
    in.githubBuildTxt = juce::String (releaseBase) + "build.txt";

    const auto r = update::check (in, update::builtInKeys(), [] (const juce::String& url, int redirects, size_t maxBytes)
    {
        return update::fetchUrl (url, redirects, maxBytes, 10000);
    });

    log (LOGTXT ("확인 (빌드 ") + juce::String (in.currentBuild) + ", " + in.platform + ", " + in.channel
         + (in.osVersion.isNotEmpty() ? ", OS " + in.osVersion : juce::String()) + (in.forced ? LOGTXT (", 강제") : juce::String()) + ")");
    for (const auto& line : r.log)
        log (LOGTXT ("확인: ") + line);
    if (! r.newlyRevoked.isEmpty())
    {
        auto all = in.revokedKeys;
        all.addArray (r.newlyRevoked);
        revokedKeysFile().replaceWithText (all.joinIntoString ("\n") + "\n");
    }

    {
        const juce::ScopedLock sl (lock);
        source = r.source;
        manifest = r.manifest;
    }
    problem = (int) (r.osTooOld ? Problem::osTooOld : Problem::none);
    if (! r.ok)
        return false;
    latestBuild = r.latestBuild;
    log (r.available ? LOGTXT ("확인: 새 빌드 ") + juce::String (r.latestBuild) + LOGTXT (" 받을 수 있음")
                     : LOGTXT ("확인: 받을 새 빌드 없음"));
    setState (r.available ? State::Available : State::UpToDate);
    return true;
}

bool MeonUpdater::runDownload()
{
   #if JUCE_MAC
    // dmg 안에서 바로 켰거나 쓰기 권한이 없는 곳이면 앱을 바꿔 넣을 수 없다
    if (! canReplaceApp())
    {
        failedOnPermission = true;
        problem = (int) Problem::needsMove;
        log (LOGTXT ("받기: 앱 자리에 쓸 수 없음 (") + currentAppBundle().getFullPathName() + LOGTXT (") → 응용 프로그램 폴더로 옮겨야 함"));
        return false;
    }
   #endif
    auto folder = downloadFolder();
    folder.deleteRecursively();   // 지난번에 받은 설치 프로그램 정리
    if (! folder.createDirectory())
    {
        log (LOGTXT ("받기: 임시 폴더를 만들 수 없음 ") + folder.getFullPathName());
        return false;
    }

    update::Source src;
    update::Manifest m;
    {
        const juce::ScopedLock sl (lock);
        src = source;
        m = manifest;
    }
   #if JUCE_MAC
    auto target = folder.getChildFile ("MEON-mac-" + juce::String (latestBuild.load()) + ".zip");
   #else
    auto target = folder.getChildFile ("MEON-Installer-" + juce::String (latestBuild.load()) + ".exe");
   #endif

    auto onProgress = [this] (juce::int64 total)
    {
        return [this, total] (juce::int64 done)
        {
            const float p = total > 0 ? (float) ((double) done / (double) total) : -1.0f;
            if (std::abs (p - progress.load()) >= 0.01f || p < 0.0f)
            {
                progress = p;
                sendChangeMessage();
            }
        };
    };
    auto stop = [this] { return threadShouldExit(); };

    if (src == update::Source::dl)
    {
        // 서명한 매니페스트의 파일: 쓰면서 크기를 세고(넘으면 멈춤), 다 받으면 크기·sha256 비교. 틀리면 지운다
        log (LOGTXT ("받기: 출처 dl ") + m.package.url);
        int status = 0;
        auto in = openDl (m.package.url, 15000, &status);
        if (in == nullptr || status != 200)
        {
            log (LOGTXT ("받기 실패: HTTP ") + juce::String (status));
            return false;
        }
        const auto problemText = update::receive (*in, target, m.package.size, stop, onProgress (m.package.size));
        if (problemText.isNotEmpty())
        {
            log (LOGTXT ("받기 실패: ") + problemText);
            return false;
        }
        const auto mismatch = update::checkReceived (target, m.package);
        if (mismatch.isNotEmpty())
        {
            target.deleteFile();
            log (LOGTXT ("받은 파일이 매니페스트와 달라 지움: ") + mismatch);
            return false;
        }
        log (LOGTXT ("받기: 크기·sha256 이 매니페스트와 같음 (") + juce::String (m.package.size) + LOGTXT (" 바이트, ") + m.package.sha256.substring (0, 16) + LOGTXT ("…)"));
    }
    else
    {
        // GitHub (브리지 전과 같은 수준: 해시 없이 길이·파일 머리만 본다)
        log (LOGTXT ("받기: 출처 github ") + juce::String (releaseBase) + packageName);
        int status = 0;
        auto in = open (packageName, 15000, &status);
        if (in == nullptr || status != 200)
        {
            log (LOGTXT ("받기 실패: HTTP ") + juce::String (status));
            return false;
        }
        const auto total = in->getTotalLength();
        const auto problemText = update::receive (*in, target, 0, stop, onProgress (total));
        if (problemText.isNotEmpty())
        {
            log (LOGTXT ("받기 실패: ") + problemText);
            return false;
        }
        if (total > 0 && target.getSize() != total)
        {
            target.deleteFile();
            log (LOGTXT ("받기 실패: 길이가 다름 (") + juce::String (target.getSize()) + " / " + juce::String (total) + ")");
            return false;
        }
    }

    // 잘린 파일이나 오류 페이지를 실행하지 않도록: 길이와 파일 머리(Windows MZ, macOS zip 의 PK)를 확인한다
    {
        bool ok = target.getSize() >= 1024 * 1024;
        juce::FileInputStream check (target);
       #if JUCE_MAC
        ok = ok && check.openedOk() && check.readByte() == 'P' && check.readByte() == 'K';
       #else
        ok = ok && check.openedOk() && check.readByte() == 'M' && check.readByte() == 'Z';
       #endif
        if (! ok)
        {
            log (LOGTXT ("받기 실패: 1 MiB 보다 작거나 파일 머리가 다름"));
            return false;
        }
    }

   #if JUCE_MAC
    // zip 을 풀고 (ditto 는 서명·심볼릭 링크를 그대로 살린다) 서명이 온전한지 확인한다
    auto unpacked = folder.getChildFile ("unpacked");
    if (threadShouldExit()
        || ! runTool ({ "/usr/bin/ditto", "-x", "-k", target.getFullPathName(), unpacked.getFullPathName() }, 120000))
    {
        log (LOGTXT ("받기 실패: zip 을 풀지 못함"));
        return false;
    }
    auto app = unpacked.getChildFile ("Meon.app");
    if (! app.getChildFile ("Contents/MacOS/Meon").existsAsFile()
        || ! runTool ({ "/usr/bin/codesign", "--verify", "--deep", app.getFullPathName() }, 60000))
    {
        log (LOGTXT ("받기 실패: zip 안에 Meon.app 이 없거나 서명이 깨짐"));
        return false;
    }
    target.deleteFile();
    target = app;
   #endif

    log (LOGTXT ("받기 끝: build ") + juce::String (latestBuild.load()) + LOGTXT (", 설치 준비됨"));
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
    {
        log (LOGTXT ("설치 프로그램 실행 (/SILENT /UPDATE=1), 앱을 끈다"));
        return true;
    }
    log (LOGTXT ("설치 프로그램을 실행하지 못함 (관리자 권한 창에서 취소 등)"));
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
