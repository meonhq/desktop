// MEON - 업데이트 매니페스트 확인 (배포 설계 DISTRIBUTION_PLAN 3.3·3.4, 부록 A). 네트워크는 쓰지 않는다.
//
// dl.meon.co.kr 의 서명한 매니페스트(봉투)를 풀어 서명과 값을 확인한다.
// 앱의 업데이터(MeonUpdater)와 CI 의 검증 도구(MEON_ManifestTool, Source/meon/tools)가 이 코드를 같이 쓴다:
// CI 는 매니페스트를 올리기 전에 앱과 같은 코드·같은 키 헤더(MeonUpdateKeys.h)로 다시 검증한다.
//
// 봉투     { "signed": "<payload 바이트의 표준 base64>",
//            "signatures": [ { "keyid": "gpl-1", "sig": "<Ed25519 서명 64바이트의 표준 base64>" } ] }
// payload  { "schema": 1, "product": "meon", "channel": "beta", "platform": "mac", "build": 980,
//            "version": "1.0.0", "commit": "…", "released_at": "…", "min_os": "10.13",
//            "packages": [ { "kind": "mac-app-zip", "url": "https://dl.meon.co.kr/…", "sha256": "<hex 64>", "size": 49039278 } ],
//            "installer": { "url": …, "sha256": …, "size": … }, "license": "GPL-3.0", "source_url": "…" }
// 서명한 바이트를 base64 로 싸 두므로 JSON 을 다시 써도 서명이 깨지지 않는다.
// release.json (빌드 폴더마다 하나, 승격·되돌리기용) 은 같은 형식에 channel 만 없다 → 채널 자리에 옮겨 놓아도 거절된다.
#pragma once

#include <JuceHeader.h>

namespace meon::update
{

/** 매니페스트 주소의 고정 머리. 받을 파일(packages·installer 의 url)도 이 아래에 있어야 한다 */
inline constexpr const char* dlBase = "https://dl.meon.co.kr/";

/** 크기 한도: 매니페스트 64 KB, 받을 파일 1 MiB – 1 GiB */
inline constexpr size_t maxEnvelopeBytes = 64 * 1024;
inline constexpr juce::int64 minPackageBytes = (juce::int64) 1 << 20;
inline constexpr juce::int64 maxPackageBytes = (juce::int64) 1 << 30;

struct TrustedKey
{
    juce::String id;
    juce::MemoryBlock publicKey;   // 32바이트
};

/** MeonUpdateKeys.h 의 키 */
juce::Array<TrustedKey> builtInKeys();

/** hex → 바이트. 길이가 expectedBytes 가 아니거나 hex 가 아니면 false */
bool fromHex (const juce::String& hex, size_t expectedBytes, juce::MemoryBlock& out);
juce::String toHex (const void* data, size_t size);

/** Ed25519 (RFC 8032, SHA-512) 서명 확인 (Monocypher crypto_ed25519_check) */
bool ed25519Check (const void* signature64, const void* publicKey32, const void* message, size_t messageSize);

/** 파일의 sha256 (소문자 hex 64자, JUCE SHA256). 파일이 없으면 빈 문자열 */
juce::String sha256OfFile (const juce::File&);

struct Package
{
    juce::String kind, url, sha256;   // sha256: 소문자 hex 64자
    juce::int64 size = 0;
};

struct Manifest
{
    int build = 0;
    juce::String product, channel, platform;   // channel: release.json 이면 비어 있다
    juce::String version, commit, releasedAt, minOs, notes, license, sourceUrl;
    Package package;            // 앱 안 업데이트용: packages[] 에서 기대한 kind 의 첫 것
    Package installer;          // 사람용 dmg·setup.exe (없으면 url 이 비어 있다)
    int rollout = 100;          // 0–100 (없으면 100)
    juce::StringArray revokedKeys;
    juce::String keyId;         // 서명을 확인한 키
};

/** 앱이 요청한 것. payload 의 값이 이와 같아야 한다 */
struct Expect
{
    juce::String product { "meon" };
    juce::String channel;            // 비우면 release.json: payload 에 channel 이 없어야 한다
    juce::String platform;           // "mac" | "windows"
    juce::StringArray packageKinds;  // 앱이 설치할 수 있는 kind
};

/** 플랫폼의 기대값: product meon, kind 는 mac "mac-app-zip" / windows "win-inno" */
Expect expectFor (const juce::String& platform, const juce::String& channel);

struct Result
{
    bool ok = false;
    Manifest manifest;
    juce::String reason;   // 거절한 이유 (기록용 한 줄)
};

/** 봉투를 풀고 서명(믿는 키 − 폐기한 키 가운데 하나)을 확인한 뒤 payload 값을 확인한다. 하나라도 틀리면 ok = false */
Result verify (const juce::MemoryBlock& envelope, const Expect&, const juce::Array<TrustedKey>& trusted,
               const juce::StringArray& revoked = {});

/** 봉투 만들기 (CI 도구·시험). signature 는 64바이트 */
juce::String makeEnvelope (const juce::MemoryBlock& payload, const juce::String& keyId, const juce::MemoryBlock& signature);

/** "10.13" 처럼 점으로 나눈 숫자 (1–4개, 각 1–5자리) */
bool isVersionString (const juce::String&);
/** have ≥ need (점으로 나눈 숫자 비교, 모자란 자리는 0). 형식이 틀리면 false */
bool versionAtLeast (const juce::String& have, const juce::String& need);

/** 단계적 배포: 설치마다 한 번 만든 ID 와 build 로 0–99 를 정해 rollout 보다 작으면 받는다 (100 이면 모두) */
bool rolloutAllows (const juce::String& installId, int build, int rollout);

} // namespace meon::update
