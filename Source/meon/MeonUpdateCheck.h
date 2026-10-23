// MEON - 업데이트 확인 순서와 받은 파일 확인 (배포 설계 DISTRIBUTION_PLAN 3.4, 5.1, 부록 C).
//
// 브리지(desktop) 업데이터의 판단만 모았다. 네트워크는 Fetch 로 받으므로 단위 테스트가 가짜 응답으로 돌린다.
//   1. https://dl.meon.co.kr/meon/v1/<channel>/<platform>.json 을 받아 검증한다 (beta 면 stable 도). MeonUpdateManifest
//      - 리디렉션을 따르지 않는다, 64 KB 이하, 200 만
//   2. 검증된 매니페스트의 폐기 키(revoked_keys)를 기억하고, 그 키로 서명된 것은 뺀다.
//   3. 남은 것 가운데 가장 큰 build 를 쓴다 (출처 dl). build > 내 빌드일 때만 새 버전. min_os·rollout 을 본다.
//   4. 검증된 매니페스트가 하나도 없을 때만 지금처럼 GitHub build.txt (출처 github, 해시 없음). 이유를 기록한다.
// 받은 파일(출처 dl): 쓰면서 크기를 세고 매니페스트의 size 를 넘으면 멈춘다. 다 받으면 크기·sha256 을 비교한다.
#pragma once

#include "MeonUpdateManifest.h"

#include <functional>

namespace meon::update
{

/** 응답 하나 */
struct Fetched
{
    int status = 0;            // 0: 연결하지 못함
    juce::MemoryBlock body;
    bool tooLarge = false;     // maxBytes 를 넘어서 읽다 멈췄다
};

/** url 을 받는다. maxRedirects 0 이면 리디렉션을 따르지 않는다 (3xx 가 그대로 온다) */
using Fetch = std::function<Fetched (const juce::String& url, int maxRedirects, size_t maxBytes)>;

/** juce::URL 로 받기 (앱). timeoutMs: 연결 시간 한도 */
Fetched fetchUrl (const juce::String& url, int maxRedirects, size_t maxBytes, int timeoutMs);

enum class Source { none, dl, github };
juce::String sourceName (Source);

struct CheckInput
{
    juce::String platform;              // "mac" | "windows"
    juce::String channel;               // "stable" | "beta" (그 밖은 stable)
    int currentBuild = 0;
    juce::String osVersion;             // currentOsVersion()
    juce::String installId;             // rollout 용 (설치마다 한 번 만든 값)
    bool forced = false;                // 강제 업데이트 화면: rollout 을 무시한다
    juce::StringArray revokedKeys;      // 지난번까지 기억한 폐기 키
    juce::String manifestBase { meon::update::dlBase };   // 매니페스트 머리 (시험에서만 바꾼다)
    juce::String githubBuildTxt;        // GitHub 대체: build.txt 주소 (비우면 대체하지 않는다)
};

struct CheckResult
{
    bool ok = false;                    // 확인을 끝냈다 (false: dl 도 GitHub 도 실패)
    Source source = Source::none;
    int latestBuild = 0;                // 찾은 가장 큰 번호
    bool available = false;             // latestBuild > 내 빌드이고 받을 수 있다
    bool osTooOld = false;              // 새 빌드가 있지만 이 OS 버전이 min_os 보다 낮다
    bool heldBack = false;              // 새 빌드가 있지만 단계적 배포(rollout)에 아직 들지 않았다
    Manifest manifest;                  // 출처 dl: 고른 매니페스트 (package 를 받는다)
    juce::StringArray newlyRevoked;     // 이번에 알게 된 폐기 키 (기억할 것)
    juce::StringArray log;              // 기록할 줄
};

CheckResult check (const CheckInput&, const juce::Array<TrustedKey>&, const Fetch&);

/** 받기: in 을 target 에 쓴다. expectedSize > 0 이면 넘는 순간 멈춘다. shouldStop() 이 true 면 멈춘다.
    progress(받은 바이트) 를 가끔 부른다. 성공하면 빈 문자열, 아니면 이유 */
juce::String receive (juce::InputStream& in, const juce::File& target, juce::int64 expectedSize,
                      const std::function<bool()>& shouldStop, const std::function<void (juce::int64)>& progress);

/** 받은 파일이 매니페스트의 package 와 같은지 (크기·sha256). 맞으면 빈 문자열, 아니면 이유 */
juce::String checkReceived (const juce::File&, const Package&);

/** 이 컴퓨터의 OS 버전 (macOS "26.0.1", Windows 10·11 "10.0", 8.1 "6.3", 7 "6.1"). 모르면 빈 문자열 */
juce::String currentOsVersion();

} // namespace meon::update
