// 업데이트 매니페스트 확인 (배포 설계 DISTRIBUTION_PLAN 3.3·3.4). 앱과 CI 도구가 같이 쓰는 MeonUpdateManifest.
#include <JuceHeader.h>

#include "../MeonUpdateKeys.h"
#include "UpdateTestUtil.h"

namespace meon
{

using namespace meon::update;
using namespace meon::updatetest;
using juce::String;
using juce::var;


class UpdateManifestTests : public juce::UnitTest
{
public:
    UpdateManifestTests() : juce::UnitTest ("UpdateManifest", "Update") {}

    /** 거절되고, 이유에 why 가 들어 있어야 한다 (다른 이유로 거절돼 시험이 헛되이 통과하지 않게) */
    void rejects (const juce::MemoryBlock& envelope, const Expect& e, const juce::Array<TrustedKey>& keys, const String& why,
                  const juce::StringArray& revoked = {}, const String& label = {})
    {
        const auto r = verify (envelope, e, keys, revoked);
        expect (! r.ok, "accepted: " + label);
        expect (r.reason.contains (why), label + " -> " + r.reason);
    }

    void runTest() override
    {
        beginTest ("Ed25519: RFC 8032 7.1 test vectors 1-3, and a changed bit fails");
        {
            struct V { const char* pub; const char* msg; const char* sig; };
            const V vectors[] = {
                { "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
                  "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b" },
                { "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
                  "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00" },
                { "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", "af82",
                  "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a" },
            };
            for (const auto& v : vectors)
            {
                auto pub = hexBytes (v.pub), msg = hexBytes (v.msg), sig = hexBytes (v.sig);
                expectEquals ((int) pub.getSize(), 32);
                expectEquals ((int) sig.getSize(), 64);
                expect (ed25519Check (sig.getData(), pub.getData(), msg.getData(), msg.getSize()));
                static_cast<juce::uint8*> (sig.getData())[10] ^= 0x20;
                expect (! ed25519Check (sig.getData(), pub.getData(), msg.getData(), msg.getSize()));
            }
            // 키 만들기도 RFC 와 같다 (TEST 1 의 비밀 키 → 공개 키)
            uint8_t seed[32], secret[64], pub[32];
            auto sk = hexBytes ("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60");
            std::memcpy (seed, sk.getData(), 32);
            crypto_ed25519_key_pair (secret, pub, seed);
            expectEquals (toHex (pub, 32), String ("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"));
        }

        beginTest ("Built-in keys: gpl-1, app-1, app-2, 32 bytes each, same as the header");
        {
            const auto keys = builtInKeys();
            expectEquals (keys.size(), 3);
            expectEquals (keys[0].id, String ("gpl-1"));
            expectEquals (keys[1].id, String ("app-1"));
            expectEquals (keys[2].id, String ("app-2"));
            for (int i = 0; i < keys.size(); ++i)
            {
                expectEquals ((int) keys[i].publicKey.getSize(), 32);
                expectEquals (toHex (keys[i].publicKey.getData(), 32), String (builtInKeyHex[i].hex));
            }
            juce::MemoryBlock b;
            expect (! fromHex ("abc", 2, b));
            expect (! fromHex ("zz", 1, b));
            expect (fromHex ("A0ff", 2, b) && toHex (b.getData(), b.getSize()) == "a0ff");
        }

        const TestSigner signer (7), other (9);
        const juce::Array<TrustedKey> trusted { signer.key ("test-1") };
        const auto macBeta = expectFor ("mac", "beta");

        beginTest ("A correctly signed manifest verifies and gives its values");
        {
            const auto r = verify (signedEnvelope (bytesOf (payload()), signer), macBeta, trusted);
            expect (r.ok, r.reason);
            expectEquals (r.manifest.build, 980);
            expectEquals (r.manifest.keyId, String ("test-1"));
            expectEquals (r.manifest.channel, String ("beta"));
            expectEquals (r.manifest.package.kind, String ("mac-app-zip"));
            expectEquals (r.manifest.package.url, String ("https://dl.meon.co.kr/meon/mac/980/meon-1.0.0-b980-mac.zip"));
            expectEquals (r.manifest.package.size, (juce::int64) 49039278);
            expectEquals (r.manifest.installer.size, (juce::int64) 84089575);
            expectEquals (r.manifest.minOs, String ("10.13"));
            expectEquals (r.manifest.rollout, 100);
            expectEquals (r.manifest.license, String ("GPL-3.0"));

            const auto w = verify (signedEnvelope (bytesOf (payload ("stable", "windows")), signer), expectFor ("windows", "stable"), trusted);
            expect (w.ok, w.reason);
            expectEquals (w.manifest.package.kind, String ("win-inno"));
        }

        beginTest ("A changed payload or signature is rejected");
        {
            const auto body = bytesOf (payload());
            const auto sig = signer.sign (body);

            auto changed = body;
            static_cast<char*> (changed.getData())[changed.getSize() / 2] ^= 1;
            rejects (bytesOf (makeEnvelope (changed, "test-1", sig)), macBeta, trusted, U8 ("서명"));

            // 값을 바꾸고 옛 서명을 붙인 것 (같은 JSON 모양)
            auto p = payload();
            p.getDynamicObject()->setProperty ("build", 981);
            rejects (bytesOf (makeEnvelope (bytesOf (p), "test-1", sig)), macBeta, trusted, "test-1");

            auto badSig = sig;
            static_cast<juce::uint8*> (badSig.getData())[63] ^= 0x01;
            rejects (bytesOf (makeEnvelope (body, "test-1", badSig)), macBeta, trusted, U8 ("서명"));
        }

        beginTest ("Only trusted, not revoked keys count");
        {
            const auto body = bytesOf (payload());
            // 믿지 않는 키가 믿는 키 이름을 달고 서명
            rejects (signedEnvelope (body, other, "test-1"), macBeta, trusted, U8 ("서명"));
            // 모르는 이름
            rejects (signedEnvelope (body, signer, "nobody"), macBeta, trusted, "nobody");
            // 폐기한 키
            rejects (signedEnvelope (body, signer), macBeta, trusted, U8 ("서명"), { "test-1" });

            // 두 키로 함께 서명: 하나가 폐기돼도 다른 키로 확인된다 (키 교체 기간)
            juce::Array<TrustedKey> both { signer.key ("test-1"), other.key ("test-2") };
            juce::DynamicObject::Ptr env = new juce::DynamicObject();
            env->setProperty ("signed", juce::Base64::toBase64 (body.getData(), body.getSize()));
            juce::Array<var> sigs;
            for (auto [s, id] : { std::pair<const TestSigner*, const char*> { &signer, "test-1" }, { &other, "test-2" } })
            {
                juce::DynamicObject::Ptr e = new juce::DynamicObject();
                const auto sg = s->sign (body);
                e->setProperty ("keyid", id);
                e->setProperty ("sig", juce::Base64::toBase64 (sg.getData(), sg.getSize()));
                sigs.add (var (e.get()));
            }
            env->setProperty ("signatures", sigs);
            const auto twoSigned = bytesOf (var (env.get()));
            const auto r = verify (twoSigned, macBeta, both, { "test-1" });
            expect (r.ok, r.reason);
            expectEquals (r.manifest.keyId, String ("test-2"));
            rejects (twoSigned, macBeta, both, U8 ("서명"), { "test-1", "test-2" });

            // revoked_keys 는 읽어서 돌려준다 (기억은 업데이터가)
            auto p = payload();
            p.getDynamicObject()->setProperty ("revoked_keys", juce::Array<var> { "gpl-1" });
            const auto rv = verify (signedEnvelope (bytesOf (p), signer), macBeta, trusted);
            expect (rv.ok, rv.reason);
            expect (rv.manifest.revokedKeys.contains ("gpl-1"));
        }

        beginTest ("Product, channel and platform must be what the app asked for");
        {
            rejects (signedEnvelope (bytesOf (payload ("stable")), signer), macBeta, trusted, "channel");
            rejects (signedEnvelope (bytesOf (payload ("beta", "windows")), signer), macBeta, trusted, "platform");
            auto p = payload();
            p.getDynamicObject()->setProperty ("product", "other");
            rejects (signedEnvelope (bytesOf (p), signer), macBeta, trusted, "product");
            auto s2 = payload();
            s2.getDynamicObject()->setProperty ("schema", 2);
            rejects (signedEnvelope (bytesOf (s2), signer), macBeta, trusted, "schema");

            // 빌드 기록(release.json)은 channel 이 없다: 채널 자리에 옮겨 놓으면 거절, 기록으로 읽으면 통과
            const auto record = signedEnvelope (bytesOf (payload ("")), signer);
            rejects (record, macBeta, trusted, "channel");
            const auto asRecord = verify (record, expectFor ("mac", ""), trusted);
            expect (asRecord.ok, asRecord.reason);
            rejects (signedEnvelope (bytesOf (payload()), signer), expectFor ("mac", ""), trusted, "channel");
        }

        beginTest ("Build must be an integer 1..2^31-1");
        {
            for (auto bad : { var (0), var (-5), var ((juce::int64) 2147483648LL), var ("980"), var (980.5), var() })
            {
                auto p = payload();
                p.getDynamicObject()->setProperty ("build", bad);
                rejects (signedEnvelope (bytesOf (p), signer), macBeta, trusted, "build", {}, bad.toString());
            }
            auto p = payload();
            p.getDynamicObject()->setProperty ("build", 2147483647);
            expect (verify (signedEnvelope (bytesOf (p), signer), macBeta, trusted).ok);
        }

        beginTest ("Package URL must be under https://dl.meon.co.kr/");
        {
            const char* bad[] = {
                "https://dl.meon.co.kr.evil.example/meon/x.zip", "http://dl.meon.co.kr/meon/x.zip", "HTTPS://DL.MEON.CO.KR/meon/x.zip",
                "https://user@dl.meon.co.kr/meon/x.zip", "https://dl.meon.co.kr/", "https://dl.meon.co.kr//x.zip",
                "https://dl.meon.co.kr/meon/../x.zip", "https://dl.meon.co.kr/meon/./x.zip", "https://dl.meon.co.kr/a%2e%2e/x.zip",
                "https://dl.meon.co.kr/a b.zip", "https://dl.meon.co.kr/x.zip?y=1", "https://dl.meon.co.kr/x.zip#y",
                "https://dl.meon.co.kr/a\\b.zip", "https://dl.meon.co.kr:8443/x.zip", "https://github.com/meonhq/desktop/x.zip"
            };
            for (auto* url : bad)
            {
                auto p = payload();
                p.getDynamicObject()->getProperty ("packages")[0].getDynamicObject()->setProperty ("url", url);
                rejects (signedEnvelope (bytesOf (p), signer), macBeta, trusted, "packages", {}, url);
                rejects (signedEnvelope (bytesOf (p), signer), macBeta, trusted, String (dlBase), {}, url);

                auto q = payload();
                q.getDynamicObject()->getProperty ("installer").getDynamicObject()->setProperty ("url", url);
                rejects (signedEnvelope (bytesOf (q), signer), macBeta, trusted, "installer", {}, url);
            }
            auto ok = payload();
            ok.getDynamicObject()->getProperty ("packages")[0].getDynamicObject()->setProperty ("url", "https://dl.meon.co.kr/meon/mac/980/meon-1.0.0_b980~x.zip");
            const auto okr = verify (signedEnvelope (bytesOf (ok), signer), macBeta, trusted);
            expect (okr.ok, okr.reason);
            expectEquals (okr.manifest.package.url, String ("https://dl.meon.co.kr/meon/mac/980/meon-1.0.0_b980~x.zip"));
        }

        beginTest ("sha256 is 64 lowercase hex, size is 1 MiB..1 GiB");
        {
            for (auto sha : { String::repeatedString ("ab", 31), String::repeatedString ("AB", 32), String::repeatedString ("zz", 32), String::repeatedString ("ab", 33) })
            {
                auto p = payload();
                p.getDynamicObject()->getProperty ("packages")[0].getDynamicObject()->setProperty ("sha256", sha);
                rejects (signedEnvelope (bytesOf (p), signer), macBeta, trusted, "sha256", {}, sha);
            }
            for (auto size : { var ((juce::int64) minPackageBytes - 1), var ((juce::int64) maxPackageBytes + 1), var ("49039278"), var (1.5e7) })
            {
                auto p = payload();
                p.getDynamicObject()->getProperty ("packages")[0].getDynamicObject()->setProperty ("size", size);
                rejects (signedEnvelope (bytesOf (p), signer), macBeta, trusted, "MiB", {}, size.toString());
            }
            for (auto size : { minPackageBytes, maxPackageBytes })
            {
                auto p = payload();
                p.getDynamicObject()->getProperty ("packages")[0].getDynamicObject()->setProperty ("size", size);
                const auto r = verify (signedEnvelope (bytesOf (p), signer), macBeta, trusted);
                expect (r.ok, r.reason);
                expectEquals (r.manifest.package.size, size);
            }
        }

        beginTest ("Uses the first package of a known kind, skips unknown kinds");
        {
            auto p = payload();
            auto* future = fileEntry ("https://dl.meon.co.kr/meon/mac/980/new-format.pkg", 2000000);
            future->setProperty ("kind", "mac-pkg-v2");
            future->setProperty ("url", "not even a url");   // 모르는 kind 는 값도 보지 않는다
            auto pkgs = p.getDynamicObject()->getProperty ("packages");
            pkgs.getArray()->insert (0, var (future));
            const auto r = verify (signedEnvelope (bytesOf (p), signer), macBeta, trusted);
            expect (r.ok, r.reason);
            expectEquals (r.manifest.package.kind, String ("mac-app-zip"));

            auto only = payload();
            only.getDynamicObject()->getProperty ("packages")[0].getDynamicObject()->setProperty ("kind", "win-inno");
            rejects (signedEnvelope (bytesOf (only), signer), macBeta, trusted, "kind");
        }

        beginTest ("min_os, rollout and string fields have to be well formed");
        {
            auto p = payload();
            p.getDynamicObject()->setProperty ("min_os", "10.x");
            rejects (signedEnvelope (bytesOf (p), signer), macBeta, trusted, "min_os");
            for (auto bad : { var (-1), var (101), var ("50"), var (50.5) })
            {
                auto q = payload();
                q.getDynamicObject()->setProperty ("rollout", bad);
                rejects (signedEnvelope (bytesOf (q), signer), macBeta, trusted, "rollout", {}, bad.toString());
            }
            auto q = payload();
            q.getDynamicObject()->setProperty ("rollout", 30);
            const auto r = verify (signedEnvelope (bytesOf (q), signer), macBeta, trusted);
            expect (r.ok);
            expectEquals (r.manifest.rollout, 30);
            auto s = payload();
            s.getDynamicObject()->setProperty ("version", 1);
            rejects (signedEnvelope (bytesOf (s), signer), macBeta, trusted, U8 ("문자열"));
        }

        beginTest ("Envelope: size limit, HTML, broken JSON and base64");
        {
            const auto good = signedEnvelope (bytesOf (payload()), signer);
            rejects ({}, macBeta, trusted, U8 ("빈"));
            rejects (bytesOf (String ("<!DOCTYPE html><html><body>404 Not Found</body></html>")), macBeta, trusted, "JSON");
            rejects (bytesOf (String ("[1,2,3]")), macBeta, trusted, "JSON");
            rejects (bytesOf (String ("{\"signed\": 5, \"signatures\": []}")), macBeta, trusted, "signed");

            // 64 KB 넘으면 서명이 맞아도 거절
            auto big = payload();
            big.getDynamicObject()->setProperty ("notes", String::repeatedString ("x", 60 * 1024));
            const auto bigEnv = signedEnvelope (bytesOf (big), signer);
            expect (bigEnv.getSize() > maxEnvelopeBytes);
            rejects (bigEnv, macBeta, trusted, "64 KB");

            // signed 에 줄바꿈·공백이 끼면 표준 base64 가 아니다
            var env;
            juce::JSON::parse (good.toString(), env);
            auto text = env.getProperty ("signed", {}).toString();
            env.getDynamicObject()->setProperty ("signed", text.substring (0, 40) + "\n" + text.substring (40));
            rejects (bytesOf (env), macBeta, trusted, "base64");
            // JUCE 고유 base64 (MemoryBlock::toBase64Encoding) 는 표준이 아니다
            const auto body = bytesOf (payload());
            env.getDynamicObject()->setProperty ("signed", body.toBase64Encoding());
            rejects (bytesOf (env), macBeta, trusted, "base64");

            // 잘못된 UTF-8·0 바이트
            juce::MemoryBlock junk (good);
            junk.append ("\xff\xfe", 2);
            rejects (junk, macBeta, trusted, "JSON");
        }

        beginTest ("versionAtLeast and rolloutAllows");
        {
            expect (versionAtLeast ("10.13", "10.13"));
            expect (! versionAtLeast ("10.12.6", "10.13"));
            expect (versionAtLeast ("11.0", "10.13"));
            expect (versionAtLeast ("26.0.1", "10.13"));
            expect (versionAtLeast ("10.0", "6.1"));
            expect (! versionAtLeast ("6.1", "10.0"));
            expect (versionAtLeast ("6.3", "6.1"));
            expect (! versionAtLeast ("", "10.13"));
            expect (! versionAtLeast ("10.13", "abc"));

            expect (rolloutAllows ("anything", 980, 100));
            expect (! rolloutAllows ("anything", 980, 0));
            int allowed = 0;
            for (int i = 0; i < 2000; ++i)
                allowed += rolloutAllows ("install-" + String (i), 980, 30) ? 1 : 0;
            expect (allowed > 450 && allowed < 750, String (allowed));
            expect (rolloutAllows ("install-1", 980, 30) == rolloutAllows ("install-1", 980, 30));
        }

        beginTest ("sha256OfFile matches the known digest");
        {
            juce::TemporaryFile tmp;
            tmp.getFile().replaceWithText ("abc");
            expectEquals (sha256OfFile (tmp.getFile()), String ("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
            expect (sha256OfFile (tmp.getFile().getSiblingFile ("does-not-exist")).isEmpty());
        }
    }
};

static UpdateManifestTests updateManifestTests;

} // namespace meon
