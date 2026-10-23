// MEON - 독립 앱 자동 업데이트 (Windows / macOS). 브리지 업데이터 (배포 설계 DISTRIBUTION_PLAN 5.1).
//
// 확인 (MeonUpdateCheck):
//   1. https://dl.meon.co.kr/meon/v1/<채널>/<mac|windows>.json — Ed25519 로 서명한 매니페스트 (beta 채널은 stable 도 읽는다).
//      앱에 넣은 공개 키(MeonUpdateKeys.h)로 검증하고, 받은 파일은 매니페스트의 크기·sha256 이 맞을 때만 설치한다.
//   2. 검증된 매니페스트가 하나도 없으면(404, 막힘, 서명 틀림 …) 지금까지처럼 GitHub 릴리스
//      (windows-latest / mac-latest 의 build.txt → MEON-mac.zip / MEON-Installer.exe). 이유를 기록한다.
// 내 빌드 번호(MEON_BUILD_ID)보다 큰 번호만 새 버전이다. 받기·설치는 사용자가 "업데이트" 를 눌렀을 때만.
// - Windows: 설치 프로그램을 조용한 설치(/SILENT)로 실행한 뒤 종료. 설치가 끝나면 설치 프로그램이 앱을 다시 켠다.
// - macOS: zip 을 받아 풀어 두고, 설치 때 Meon.app 을 바꿔 넣은 뒤 종료. 종료 직전에 open -n 으로 새 앱을 켠다.
//   앱 안에 AU / VST3 도 들어 있어서, 켤 때 사용자 폴더에 설치된 옛 플러그인을 바꿔 넣는다 (syncPlugins).
// 기록: 로그 폴더의 update.log (macOS ~/Library/Logs/MEON, Windows %APPDATA%\MEON\logs) 와 app.log.
//   "확인: 출처 dl …" / "확인: … → GitHub" 줄에서 어디서 받았는지 볼 수 있다.
#pragma once

#include "MeonTheme.h"
#include "MeonUpdateCheck.h"

namespace meon
{

class MeonUpdater : public juce::ChangeBroadcaster,
                    private juce::Thread,
                    private juce::AsyncUpdater
{
public:
    enum class State
    {
        Idle,          // 아직 확인 안 함
        Checking,
        UpToDate,
        Available,     // 새 빌드가 있음
        Downloading,
        Ready,         // 받기 끝, 설치 대기 (합주 중이라 바로 설치하지 않은 경우)
        Failed
    };

    /** 다시 시도해도 풀리지 않는 이유 (화면 문구) */
    enum class Problem
    {
        none,
        needsMove,     // macOS: 앱 자리에 쓸 수 없다 (dmg 안에서 실행 등). 응용 프로그램 폴더로 옮겨야 한다
        osTooOld       // 새 빌드가 이 OS 버전을 지원하지 않는다 (매니페스트 min_os)
    };

    MeonUpdater();
    ~MeonUpdater() override;

    /** Windows / macOS 독립 앱이고 CI 빌드(빌드 번호 있음)일 때만 true. 아니면 화면에 업데이트 항목을 두지 않는다. */
    static bool isSupported();
    static int currentBuild();

    /** 확인하기 전에 메시지 스레드에서 정한다. channel: "stable" | "beta" (MeonSettings::getUpdateChannel) */
    void setChannel (const juce::String& channel);
    /** 강제 업데이트 화면이 떠 있다 (단계적 배포 rollout 을 무시한다) */
    void setForced (bool forced);

    void check();
    /** Available 일 때 새 빌드 받기 시작. Failed 면 다시 확인하고 새 빌드가 있으면 이어서 받는다 ("다시 시도").
        끝나면 onDownloaded 를 메시지 스레드에서 부른다. */
    void download();
    /** Ready 일 때 설치를 시작한다 (Windows: 설치 프로그램 실행, macOS: 앱 교체 후 다시 켜기 예약). 성공하면 true (호출한 쪽이 앱을 종료한다). */
    bool launchInstaller();
    /** launchInstaller() 가 성공하고 설정을 저장한 뒤, 종료 직전에 부른다. macOS: 새 앱을 켠다. Windows: 할 일 없음 (설치 프로그램이 켠다). */
    void relaunchAfterInstall();

    State getState() const;
    int getLatestBuild() const      { return latestBuild.load(); }
    float getProgress() const       { return progress.load(); }   // 0~1, 길이를 모르면 음수
    bool lastFailureWasDownload() const { return failedOnDownload.load(); }
    /** macOS: 앱 자리에 쓸 수 없어서(dmg 안에서 실행 등) 받지 못했다. 다시 시도해도 소용없다. */
    bool lastFailureWasPermission() const { return failedOnPermission.load(); }
    Problem getProblem() const      { return (Problem) problem.load(); }
    /** 마지막 확인의 출처 (dl: 서명한 매니페스트, github: build.txt 대체) */
    update::Source getSource() const;

    std::function<void()> onDownloaded;

private:
    enum class Job { None, Check, Download, CheckThenDownload };
    mutable juce::CriticalSection lock;
    State state = State::Idle;
    Job job = Job::None;
    juce::String channel { "stable" };
    bool forced = false;
    update::Source source = update::Source::none;
    update::Manifest manifest;          // 출처 dl 일 때 받을 것
    std::atomic<int> latestBuild { 0 };
    std::atomic<float> progress { 0.0f };
    std::atomic<bool> failedOnDownload { false };
    std::atomic<bool> failedOnPermission { false };
    std::atomic<int> problem { (int) Problem::none };
    juce::File installer;
    juce::File installedApp;   // macOS: 바꿔 넣은 앱 (다시 켤 대상)

    void setState (State s);
    void start (Job j, State s);
    void run() override;
    bool runCheck();
    bool runDownload();
    void handleAsyncUpdate() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeonUpdater)
};

} // namespace meon
