// MEON - 업데이트 매니페스트 도구 (CI 용, 배포 설계 DISTRIBUTION_PLAN 3.5·3.9).
// 앱과 같은 확인 코드(MeonUpdateManifest.cpp)와 같은 키 헤더(MeonUpdateKeys.h)를 쓴다.
// 비밀값은 다루지 않는다: 서명은 openssl 이 하고, 이 도구는 봉투를 만들고 검증만 한다.
//
//   MEON_ManifestTool verify <봉투.json> --platform mac|windows (--channel <이름> | --release)
//        앱과 같은 방법으로 확인한다. 맞으면 0 과 key=value 줄 (build·keyid·package.* …), 틀리면 1 과 이유
//   MEON_ManifestTool envelope <payload.json> <keyid> <서명 64바이트 파일>
//        봉투 JSON 을 표준 출력으로 (서명 길이만 본다. 맞는지는 verify 로)
//   MEON_ManifestTool keyhex <keyid>       앱이 믿는 공개 키 hex (없으면 1)
//   MEON_ManifestTool sha256 <파일>        앱이 받은 파일에 쓰는 sha256 (JUCE SHA256)
//   MEON_ManifestTool selftest             RFC 8032 시험 벡터로 Ed25519 확인 코드가 맞게 빌드됐는지
#include <JuceHeader.h>

#include "../MeonUpdateManifest.h"

#include <iostream>

using namespace meon::update;

namespace
{

void out (const juce::String& s)  { std::cout << s.toStdString() << std::endl; }
void err (const juce::String& s)  { std::cerr << s.toStdString() << std::endl; }

int usage()
{
    err ("usage: MEON_ManifestTool verify <envelope.json> --platform mac|windows (--channel <name> | --release)\n"
         "       MEON_ManifestTool envelope <payload.json> <keyid> <sig.bin>\n"
         "       MEON_ManifestTool keyhex <keyid>\n"
         "       MEON_ManifestTool sha256 <file>\n"
         "       MEON_ManifestTool selftest");
    return 2;
}

juce::String option (const juce::StringArray& args, const juce::String& name)
{
    const int i = args.indexOf (name);
    return i >= 0 && i + 1 < args.size() ? args[i + 1] : juce::String();
}

bool readFile (const juce::String& path, juce::MemoryBlock& data)
{
    const juce::File f = juce::File::getCurrentWorkingDirectory().getChildFile (path);
    if (! f.existsAsFile() || ! f.loadFileAsData (data))
    {
        err ("cannot read " + path);
        return false;
    }
    return true;
}

int verifyCommand (const juce::StringArray& args)
{
    if (args.size() < 2)
        return usage();
    const auto platform = option (args, "--platform");
    const bool release = args.contains ("--release");
    const auto channel = option (args, "--channel");
    if (platform.isEmpty() || (release == channel.isNotEmpty()))
        return usage();

    juce::MemoryBlock data;
    if (! readFile (args[1], data))
        return 1;

    const auto r = verify (data, expectFor (platform, release ? juce::String() : channel), builtInKeys());
    if (! r.ok)
    {
        out ("rejected: " + r.reason);
        return 1;
    }
    const auto& m = r.manifest;
    out ("ok");
    out ("keyid=" + m.keyId);
    out ("build=" + juce::String (m.build));
    out ("channel=" + m.channel);
    out ("platform=" + m.platform);
    out ("version=" + m.version);
    out ("commit=" + m.commit);
    out ("min_os=" + m.minOs);
    out ("rollout=" + juce::String (m.rollout));
    out ("package.kind=" + m.package.kind);
    out ("package.url=" + m.package.url);
    out ("package.sha256=" + m.package.sha256);
    out ("package.size=" + juce::String (m.package.size));
    out ("installer.url=" + m.installer.url);
    out ("installer.sha256=" + m.installer.sha256);
    out ("installer.size=" + juce::String (m.installer.size));
    out ("license=" + m.license);
    out ("source_url=" + m.sourceUrl);
    out ("revoked_keys=" + m.revokedKeys.joinIntoString (","));
    return 0;
}

int envelopeCommand (const juce::StringArray& args)
{
    if (args.size() != 4)
        return usage();
    juce::MemoryBlock payload, sig;
    if (! readFile (args[1], payload) || ! readFile (args[3], sig))
        return 1;
    if (sig.getSize() != 64)
    {
        err ("signature must be 64 bytes, got " + juce::String ((int) sig.getSize()));
        return 1;
    }
    out (makeEnvelope (payload, args[2], sig));
    return 0;
}

int keyhexCommand (const juce::StringArray& args)
{
    if (args.size() != 2)
        return usage();
    for (const auto& k : builtInKeys())
    {
        if (k.id == args[1])
        {
            out (toHex (k.publicKey.getData(), k.publicKey.getSize()));
            return 0;
        }
    }
    err ("unknown keyid " + args[1]);
    return 1;
}

int sha256Command (const juce::StringArray& args)
{
    if (args.size() != 2)
        return usage();
    const auto h = sha256OfFile (juce::File::getCurrentWorkingDirectory().getChildFile (args[1]));
    if (h.isEmpty())
    {
        err ("cannot read " + args[1]);
        return 1;
    }
    out (h);
    return 0;
}

int selftestCommand()
{
    // RFC 8032 7.1 TEST 1 (빈 메시지)
    juce::MemoryBlock pub, sig;
    fromHex ("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", 32, pub);
    fromHex ("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
             "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b", 64, sig);
    const bool good = ed25519Check (sig.getData(), pub.getData(), "", 0);
    static_cast<juce::uint8*> (sig.getData())[0] ^= 1;
    const bool tampered = ed25519Check (sig.getData(), pub.getData(), "", 0);
    if (! good || tampered)
    {
        out ("selftest failed");
        return 1;
    }
    out ("selftest ok");
    return 0;
}

} // namespace

int main (int argc, char* argv[])
{
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (juce::String (juce::CharPointer_UTF8 (argv[i])));
    if (args.isEmpty())
        return usage();

    const auto cmd = args[0];
    if (cmd == "verify")   return verifyCommand (args);
    if (cmd == "envelope") return envelopeCommand (args);
    if (cmd == "keyhex")   return keyhexCommand (args);
    if (cmd == "sha256")   return sha256Command (args);
    if (cmd == "selftest") return selftestCommand();
    return usage();
}
