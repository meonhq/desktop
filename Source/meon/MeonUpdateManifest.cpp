#include "MeonUpdateManifest.h"
#include "MeonUpdateKeys.h"

#include "monocypher-ed25519.h"

#include <cstring>

namespace meon::update
{

using juce::String;
using juce::var;

#define MTXT(s) juce::String (juce::CharPointer_UTF8 (s))

juce::Array<TrustedKey> builtInKeys()
{
    juce::Array<TrustedKey> keys;
    for (const auto& k : builtInKeyHex)
    {
        TrustedKey t;
        t.id = k.id;
        const bool ok = fromHex (k.hex, 32, t.publicKey);
        jassert (ok);
        if (ok)
            keys.add (std::move (t));
    }
    return keys;
}

bool fromHex (const String& hex, size_t expectedBytes, juce::MemoryBlock& out)
{
    if ((size_t) hex.length() != expectedBytes * 2)
        return false;
    out.setSize (expectedBytes);
    auto* d = static_cast<juce::uint8*> (out.getData());
    for (size_t i = 0; i < expectedBytes; ++i)
    {
        const int hi = juce::CharacterFunctions::getHexDigitValue (hex[(int) (2 * i)]);
        const int lo = juce::CharacterFunctions::getHexDigitValue (hex[(int) (2 * i + 1)]);
        if (hi < 0 || lo < 0)
            return false;
        d[i] = (juce::uint8) ((hi << 4) | lo);
    }
    return true;
}

String toHex (const void* data, size_t size)
{
    return String::toHexString (data, (int) size, 0).toLowerCase();
}

bool ed25519Check (const void* signature64, const void* publicKey32, const void* message, size_t messageSize)
{
    return crypto_ed25519_check (static_cast<const uint8_t*> (signature64), static_cast<const uint8_t*> (publicKey32),
                                 static_cast<const uint8_t*> (message), messageSize) == 0;
}

String sha256OfFile (const juce::File& f)
{
    if (! f.existsAsFile())
        return {};
    return juce::SHA256 (f).toHexString().toLowerCase();
}

Expect expectFor (const String& platform, const String& channel)
{
    Expect e;
    e.channel = channel;
    e.platform = platform;
    if (platform == "mac")          e.packageKinds.add ("mac-app-zip");   // zip 맨 위 Meon.app (ditto -c -k --keepParent)
    else if (platform == "windows") e.packageKinds.add ("win-inno");      // /SILENT /SUPPRESSMSGBOXES /NORESTART /UPDATE=1 을 받는 설치 프로그램
    return e;
}

//==============================================================================
namespace
{

bool isLowerHex (const String& s, int length)
{
    if (s.length() != length)
        return false;
    for (auto c : s)
        if (! ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    return true;
}

/** https://dl.meon.co.kr/ 아래의 경로. 조각마다 [A-Za-z0-9._~-], 빈 조각·"."·".." 은 안 된다 */
bool isDlUrl (const String& url)
{
    const String base (dlBase);
    if (! url.startsWith (base))
        return false;
    const auto rest = url.substring (base.length());
    if (rest.isEmpty())
        return false;
    for (auto c : rest)
        if (! (juce::CharacterFunctions::isLetterOrDigit (c) && c < 128) && c != '.' && c != '_' && c != '~' && c != '-' && c != '/')
            return false;
    juce::StringArray parts;
    parts.addTokens (rest, "/", "");
    for (const auto& p : parts)
        if (p.isEmpty() || p == "." || p == "..")
            return false;
    return true;
}

/** JSON 정수 (소수·문자열은 아님) */
bool getInteger (const var& v, juce::int64& out)
{
    if (v.isInt() || v.isInt64())
    {
        out = (juce::int64) v;
        return true;
    }
    return false;
}

bool optionalString (const juce::DynamicObject& o, const char* name, String& out)
{
    const auto& v = o.getProperty (name);
    if (v.isVoid() || v.isUndefined())
        return true;
    if (! v.isString())
        return false;
    out = v.toString();
    return true;
}

/** { url, sha256, size } 확인. 틀리면 이유를 돌려준다 */
String checkFile (const var& v, Package& out)
{
    auto* o = v.getDynamicObject();
    if (o == nullptr)
        return MTXT ("파일 항목이 객체가 아님");
    const auto& url = o->getProperty ("url");
    const auto& sha = o->getProperty ("sha256");
    juce::int64 size = 0;
    if (! url.isString() || ! isDlUrl (url.toString()))
        return MTXT ("주소가 ") + dlBase + MTXT (" 아래가 아님");
    if (! sha.isString() || ! isLowerHex (sha.toString(), 64))
        return MTXT ("sha256 형식이 틀림");
    if (! getInteger (o->getProperty ("size"), size) || size < minPackageBytes || size > maxPackageBytes)
        return MTXT ("크기가 1 MiB–1 GiB 밖");
    out.url = url.toString();
    out.sha256 = sha.toString();
    out.size = size;
    return {};
}

/** UTF-8 JSON 바이트를 읽는다. 0 바이트가 있거나 UTF-8 이 아니면 false */
bool parseJsonBytes (const void* data, size_t size, var& out)
{
    const auto* text = static_cast<const char*> (data);
    if (size == 0 || std::memchr (text, 0, size) != nullptr || ! juce::CharPointer_UTF8::isValidString (text, (int) size))
        return false;
    return juce::JSON::parse (String::fromUTF8 (text, (int) size), out).wasOk();
}

String checkPayload (const var& payload, const Expect& expect, Manifest& m)
{
    auto* o = payload.getDynamicObject();
    if (o == nullptr)
        return MTXT ("payload 가 JSON 객체가 아님");

    juce::int64 schema = 0;
    if (! getInteger (o->getProperty ("schema"), schema) || schema != 1)
        return MTXT ("schema 가 1 이 아님");

    const auto product = o->getProperty ("product");
    if (! product.isString() || product.toString() != expect.product)
        return MTXT ("product 가 다름 (") + product.toString() + ")";

    const auto channel = o->getProperty ("channel");
    if (expect.channel.isEmpty())
    {
        if (! (channel.isVoid() || channel.isUndefined()))
            return MTXT ("빌드 기록(release.json)인데 channel 이 있음");
    }
    else if (! channel.isString() || channel.toString() != expect.channel)
        return MTXT ("channel 이 다름 (기대 ") + expect.channel + MTXT (", 받은 ") + channel.toString() + ")";

    const auto platform = o->getProperty ("platform");
    if (! platform.isString() || platform.toString() != expect.platform)
        return MTXT ("platform 이 다름 (기대 ") + expect.platform + MTXT (", 받은 ") + platform.toString() + ")";

    juce::int64 build = 0;
    if (! getInteger (o->getProperty ("build"), build) || build < 1 || build > 2147483647)
        return MTXT ("build 가 1–2147483647 정수가 아님");

    m.product = product.toString();
    m.channel = channel.isString() ? channel.toString() : String();
    m.platform = platform.toString();
    m.build = (int) build;

    if (! optionalString (*o, "version", m.version) || ! optionalString (*o, "commit", m.commit)
        || ! optionalString (*o, "released_at", m.releasedAt) || ! optionalString (*o, "notes", m.notes)
        || ! optionalString (*o, "license", m.license) || ! optionalString (*o, "source_url", m.sourceUrl)
        || ! optionalString (*o, "min_os", m.minOs))
        return MTXT ("문자열 필드의 형식이 틀림");
    if (m.minOs.isNotEmpty() && ! isVersionString (m.minOs))
        return MTXT ("min_os 형식이 틀림 (") + m.minOs + ")";

    // packages: 아는 kind 의 첫 것만 쓴다 (모르는 kind 는 건너뛴다: 나중에 형식을 더해도 옛 앱이 깨지지 않게)
    const auto packages = o->getProperty ("packages");
    if (! packages.isArray())
        return MTXT ("packages 가 배열이 아님");
    bool found = false;
    for (const auto& p : *packages.getArray())
    {
        const auto kind = p.getProperty ("kind", var());
        if (! kind.isString() || ! expect.packageKinds.contains (kind.toString()))
            continue;
        const auto problem = checkFile (p, m.package);
        if (problem.isNotEmpty())
            return "packages (" + kind.toString() + "): " + problem;
        m.package.kind = kind.toString();
        found = true;
        break;
    }
    if (! found)
        return MTXT ("packages 에 이 플랫폼이 설치할 수 있는 kind 가 없음 (") + expect.packageKinds.joinIntoString (", ") + ")";

    const auto installer = o->getProperty ("installer");
    if (! (installer.isVoid() || installer.isUndefined()))
    {
        const auto problem = checkFile (installer, m.installer);
        if (problem.isNotEmpty())
            return "installer: " + problem;
    }

    const auto rollout = o->getProperty ("rollout");
    if (! (rollout.isVoid() || rollout.isUndefined()))
    {
        juce::int64 r = 0;
        if (! getInteger (rollout, r) || r < 0 || r > 100)
            return MTXT ("rollout 이 0–100 정수가 아님");
        m.rollout = (int) r;
    }

    const auto revoked = o->getProperty ("revoked_keys");
    if (! (revoked.isVoid() || revoked.isUndefined()))
    {
        if (! revoked.isArray())
            return MTXT ("revoked_keys 가 배열이 아님");
        for (const auto& k : *revoked.getArray())
        {
            if (! k.isString())
                return MTXT ("revoked_keys 에 문자열이 아닌 값");
            m.revokedKeys.addIfNotAlreadyThere (k.toString());
        }
    }
    return {};
}

} // namespace

//==============================================================================
Result verify (const juce::MemoryBlock& envelope, const Expect& expect, const juce::Array<TrustedKey>& trusted,
               const juce::StringArray& revoked)
{
    Result r;
    auto fail = [&r] (const String& why) { r.ok = false; r.reason = why; return r; };

    if (envelope.getSize() == 0)
        return fail (MTXT ("빈 응답"));
    if (envelope.getSize() > maxEnvelopeBytes)
        return fail (MTXT ("64 KB 보다 큼"));

    var env;
    if (! parseJsonBytes (envelope.getData(), envelope.getSize(), env) || env.getDynamicObject() == nullptr)
        return fail (MTXT ("JSON 객체가 아님"));

    const auto signedText = env.getProperty ("signed", var());
    if (! signedText.isString() || signedText.toString().isEmpty())
        return fail (MTXT ("signed 가 없음"));
    juce::MemoryOutputStream payloadOut;
    if (! juce::Base64::convertFromBase64 (payloadOut, signedText.toString()) || payloadOut.getDataSize() == 0)
        return fail (MTXT ("signed 가 표준 base64 가 아님"));
    const auto payload = payloadOut.getMemoryBlock();

    const auto sigs = env.getProperty ("signatures", var());
    if (! sigs.isArray() || sigs.getArray()->isEmpty())
        return fail (MTXT ("signatures 가 없음"));

    juce::StringArray seen;
    String keyId;
    for (const auto& s : *sigs.getArray())
    {
        const auto id = s.getProperty ("keyid", var());
        const auto sig = s.getProperty ("sig", var());
        if (! id.isString() || ! sig.isString())
            continue;
        seen.add (id.toString());
        if (revoked.contains (id.toString()))
            continue;
        for (const auto& k : trusted)
        {
            if (k.id != id.toString() || k.publicKey.getSize() != 32)
                continue;
            juce::MemoryOutputStream sigOut;
            if (! juce::Base64::convertFromBase64 (sigOut, sig.toString()) || sigOut.getDataSize() != 64)
                continue;
            if (ed25519Check (sigOut.getData(), k.publicKey.getData(), payload.getData(), payload.getSize()))
                keyId = k.id;
        }
        if (keyId.isNotEmpty())
            break;
    }
    if (keyId.isEmpty())
        return fail (MTXT ("믿는 키로 확인되는 서명이 없음 (keyid: ") + seen.joinIntoString (", ") + ")");

    var payloadJson;
    if (! parseJsonBytes (payload.getData(), payload.getSize(), payloadJson))
        return fail (MTXT ("payload 가 JSON 이 아님"));

    Manifest m;
    const auto problem = checkPayload (payloadJson, expect, m);
    if (problem.isNotEmpty())
        return fail (problem);

    m.keyId = keyId;
    r.ok = true;
    r.manifest = std::move (m);
    return r;
}

String makeEnvelope (const juce::MemoryBlock& payload, const String& keyId, const juce::MemoryBlock& signature)
{
    jassert (signature.getSize() == 64);
    juce::DynamicObject::Ptr sig = new juce::DynamicObject();
    sig->setProperty ("keyid", keyId);
    sig->setProperty ("sig", juce::Base64::toBase64 (signature.getData(), signature.getSize()));
    juce::DynamicObject::Ptr env = new juce::DynamicObject();
    env->setProperty ("signed", juce::Base64::toBase64 (payload.getData(), payload.getSize()));
    env->setProperty ("signatures", juce::Array<var> { var (sig.get()) });
    return juce::JSON::toString (var (env.get()), true);
}

bool isVersionString (const String& v)
{
    juce::StringArray parts;
    parts.addTokens (v, ".", "");
    if (parts.isEmpty() || parts.size() > 4)
        return false;
    for (const auto& p : parts)
        if (p.isEmpty() || p.length() > 5 || ! p.containsOnly ("0123456789"))
            return false;
    return true;
}

bool versionAtLeast (const String& have, const String& need)
{
    if (! isVersionString (have) || ! isVersionString (need))
        return false;
    juce::StringArray a, b;
    a.addTokens (have, ".", "");
    b.addTokens (need, ".", "");
    for (int i = 0; i < juce::jmax (a.size(), b.size()); ++i)
    {
        const int x = i < a.size() ? a[i].getIntValue() : 0;
        const int y = i < b.size() ? b[i].getIntValue() : 0;
        if (x != y)
            return x > y;
    }
    return true;
}

bool rolloutAllows (const String& installId, int build, int rollout)
{
    if (rollout >= 100) return true;
    if (rollout <= 0)   return false;
    const auto text = (installId + ":" + String (build)).toStdString();
    const auto digest = juce::SHA256 (text.data(), text.size()).getRawData();
    const auto* d = static_cast<const juce::uint8*> (digest.getData());
    const juce::uint32 n = ((juce::uint32) d[0] << 24) | ((juce::uint32) d[1] << 16) | ((juce::uint32) d[2] << 8) | (juce::uint32) d[3];
    return (int) (n % 100u) < rollout;
}

} // namespace meon::update
