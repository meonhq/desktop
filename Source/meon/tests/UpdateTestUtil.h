// 업데이트 시험 공용 (UpdateManifestTests·UpdateCheckTests): 시험용 키(고정 씨앗), 부록 A 모양의 payload, 봉투 만들기.
#pragma once

#include <JuceHeader.h>

#include "../MeonUpdateManifest.h"
#include "monocypher-ed25519.h"

#include <algorithm>
#include <cstring>
#include <utility>

#define U8(s) juce::String (juce::CharPointer_UTF8 (s))

namespace meon::updatetest
{

using namespace meon::update;
using juce::String;
using juce::var;

inline juce::MemoryBlock hexBytes (const String& hex)
{
    juce::MemoryBlock b;
    fromHex (hex, (size_t) hex.length() / 2, b);
    return b;
}

/** 시험용 키 (고정 씨앗). 앱의 키가 아니다 */
struct TestSigner
{
    explicit TestSigner (juce::uint8 seedByte)
    {
        uint8_t seed[32];
        std::fill (seed, seed + 32, seedByte);
        crypto_ed25519_key_pair (secret, pub, seed);
    }

    juce::MemoryBlock sign (const juce::MemoryBlock& msg) const
    {
        juce::MemoryBlock sig (64);
        crypto_ed25519_sign (static_cast<uint8_t*> (sig.getData()), secret, static_cast<const uint8_t*> (msg.getData()), msg.getSize());
        return sig;
    }

    TrustedKey key (const String& id) const
    {
        TrustedKey k;
        k.id = id;
        k.publicKey = juce::MemoryBlock (pub, 32);
        return k;
    }

    uint8_t secret[64] {};
    uint8_t pub[32] {};
};

inline juce::DynamicObject* fileEntry (const String& url, juce::int64 size)
{
    auto* f = new juce::DynamicObject();
    f->setProperty ("url", url);
    f->setProperty ("sha256", String::repeatedString ("ab", 32));
    f->setProperty ("size", size);
    return f;
}

/** 부록 A 모양의 맞는 payload (desktop, mac, beta, 980) */
inline var payload (const String& channel = "beta", const String& platform = "mac", int build = 980)
{
    auto* p = new juce::DynamicObject();
    p->setProperty ("schema", 1);
    p->setProperty ("product", "meon");
    if (channel.isNotEmpty())
        p->setProperty ("channel", channel);
    p->setProperty ("platform", platform);
    p->setProperty ("build", build);
    p->setProperty ("version", "1.0.0");
    p->setProperty ("commit", "0123456789abcdef0123456789abcdef01234567");
    p->setProperty ("released_at", "2026-10-23T00:00:00Z");
    p->setProperty ("min_os", platform == "mac" ? "10.13" : "6.1");
    const String dir = "https://dl.meon.co.kr/meon/" + platform + "/" + String (build) + "/";
    auto* pkg = fileEntry (dir + (platform == "mac" ? "meon-1.0.0-b980-mac.zip" : "meon-1.0.0-b980-setup.exe"), 49039278);
    pkg->setProperty ("kind", platform == "mac" ? "mac-app-zip" : "win-inno");
    p->setProperty ("packages", juce::Array<var> { var (pkg) });
    p->setProperty ("installer", var (fileEntry (dir + (platform == "mac" ? "meon-1.0.0-b980.dmg" : "meon-1.0.0-b980-setup.exe"), 84089575)));
    p->setProperty ("license", "GPL-3.0");
    p->setProperty ("source_url", "https://github.com/meonhq/desktop/tree/0123456789abcdef0123456789abcdef01234567");
    return var (p);
}

inline juce::MemoryBlock bytesOf (const String& s)
{
    return juce::MemoryBlock (s.toRawUTF8(), s.getNumBytesAsUTF8());
}

inline juce::MemoryBlock bytesOf (const var& json)
{
    return bytesOf (juce::JSON::toString (json, true));
}

inline juce::MemoryBlock signedEnvelope (const juce::MemoryBlock& body, const TestSigner& s, const String& keyId = "test-1")
{
    return bytesOf (makeEnvelope (body, keyId, s.sign (body)));
}


} // namespace meon::updatetest
