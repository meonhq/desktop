// 브리지 업데이터의 확인 순서 (배포 설계 DISTRIBUTION_PLAN 3.4·5.1, 부록 C): dl 매니페스트 → 검증 → 없으면 GitHub.
// 네트워크 대신 가짜 Fetch 를 쓴다. 받은 파일 확인(receive·checkReceived)도 여기서 본다.
//   UpdateCheck (Update): 늘 돈다
//   UpdateFetch (Transport): MEON_TEST_MOCK 이 있을 때 진짜 juce::URL 로 가짜 서버(scripts/meon_test_server.py)에 붙는다.
//     리디렉션을 따르지 않기·64 KB 한도·상태 코드. macOS·Windows CI 의 전송층 시험에서도 돈다 (플랫폼 HTTP 가 다르다)
#include <JuceHeader.h>

#include "../MeonUpdateCheck.h"
#include "UpdateTestUtil.h"

#include <map>

namespace meon
{

using namespace meon::update;
using namespace meon::updatetest;
using juce::String;
using juce::var;

namespace
{

/** 주소별 응답을 정해 두는 가짜 네트워크. 부른 주소·리디렉션·한도를 기록한다 */
struct FakeNet
{
    std::map<juce::String, Fetched> replies;
    juce::StringArray asked;
    std::vector<int> redirects;
    std::vector<size_t> limits;

    void set (const String& url, int status, const juce::MemoryBlock& body = {})
    {
        Fetched f;
        f.status = status;
        f.body = body;
        replies[url] = f;
    }

    Fetch fetch()
    {
        return [this] (const String& url, int maxRedirects, size_t maxBytes)
        {
            asked.add (url);
            redirects.push_back (maxRedirects);
            limits.push_back (maxBytes);
            const auto it = replies.find (url);
            return it != replies.end() ? it->second : Fetched {};   // 정하지 않은 주소: 연결 실패
        };
    }
};

const String base ("https://dl.meon.co.kr/");
const String github ("https://github.com/meonhq/desktop/releases/download/mac-latest/build.txt");

String manifestUrl (const String& channel, const String& platform = "mac")
{
    return base + "meon/v1/" + channel + "/" + platform + ".json";
}

CheckInput input (const String& channel = "stable", int current = 900)
{
    CheckInput in;
    in.platform = "mac";
    in.channel = channel;
    in.currentBuild = current;
    in.osVersion = "15.6";
    in.installId = "test-install";
    in.githubBuildTxt = github;
    return in;
}

} // namespace

class UpdateCheckTests : public juce::UnitTest
{
public:
    UpdateCheckTests() : juce::UnitTest ("UpdateCheck", "Update") {}

    void runTest() override
    {
        const TestSigner signer (7), other (9);
        const juce::Array<TrustedKey> trusted { signer.key ("test-1") };
        auto signedFor = [&] (const String& channel, int build, const String& platform = "mac")
        {
            return signedEnvelope (bytesOf (payload (channel, platform, build)), signer);
        };

        beginTest ("stable: a verified dl manifest wins; no redirects, 64 KB limit, GitHub not asked");
        {
            FakeNet net;
            net.set (manifestUrl ("stable"), 200, signedFor ("stable", 980));
            net.set (github, 200, bytesOf (String ("999\n")));
            const auto r = check (input(), trusted, net.fetch());
            expect (r.ok);
            expect (r.source == Source::dl);
            expectEquals (r.latestBuild, 980);
            expect (r.available);
            expectEquals (r.manifest.channel, String ("stable"));
            expectEquals (r.manifest.package.size, (juce::int64) 49039278);
            expectEquals (net.asked.size(), 1);
            expectEquals (net.redirects[0], 0);
            expectEquals ((int) net.limits[0], (int) maxEnvelopeBytes);
            expect (r.log.joinIntoString ("\n").contains (U8 ("출처 dl")), r.log.joinIntoString ("\n"));
        }

        beginTest ("beta reads stable too and takes the larger build");
        {
            FakeNet net;
            net.set (manifestUrl ("beta"), 200, signedFor ("beta", 985));
            net.set (manifestUrl ("stable"), 200, signedFor ("stable", 980));
            auto r = check (input ("beta"), trusted, net.fetch());
            expect (r.source == Source::dl);
            expectEquals (r.latestBuild, 985);
            expectEquals (r.manifest.channel, String ("beta"));
            expectEquals (net.asked[0], manifestUrl ("beta"));
            expectEquals (net.asked[1], manifestUrl ("stable"));

            net.set (manifestUrl ("beta"), 200, signedFor ("beta", 970));
            r = check (input ("beta"), trusted, net.fetch());
            expectEquals (r.latestBuild, 980);
            expectEquals (r.manifest.channel, String ("stable"));

            // beta 가 없으면 stable 로 (GitHub 까지 가지 않는다)
            net.set (manifestUrl ("beta"), 404);
            r = check (input ("beta"), trusted, net.fetch());
            expect (r.source == Source::dl);
            expectEquals (r.latestBuild, 980);
            expect (r.log.joinIntoString ("\n").contains ("beta: HTTP 404"), r.log.joinIntoString ("\n"));

            // 모르는 채널은 stable
            FakeNet net2;
            net2.set (manifestUrl ("stable"), 200, signedFor ("stable", 980));
            r = check (input ("nightly"), trusted, net2.fetch());
            expectEquals (net2.asked[0], manifestUrl ("stable"));
            expect (r.source == Source::dl);
        }

        beginTest ("Same or lower build is not new; GitHub is not asked when dl verified");
        {
            FakeNet net;
            net.set (manifestUrl ("stable"), 200, signedFor ("stable", 900));
            net.set (github, 200, bytesOf (String ("999")));
            auto r = check (input ("stable", 900), trusted, net.fetch());
            expect (r.ok && r.source == Source::dl && ! r.available);
            expectEquals (r.latestBuild, 900);
            r = check (input ("stable", 950), trusted, net.fetch());
            expect (r.ok && ! r.available);
            expect (! net.asked.contains (github));
        }

        beginTest ("No verified manifest -> GitHub build.txt (404, HTML, forged, wrong platform, too large, unreachable)");
        {
            auto htmlPage = bytesOf (String ("<!DOCTYPE html><html><body>Wi-Fi login</body></html>"));
            auto forged = [&]
            {
                const auto body = bytesOf (payload ("stable", "mac", 990));
                auto changed = bytesOf (payload ("stable", "mac", 999));
                return bytesOf (makeEnvelope (changed, "test-1", signer.sign (body)));
            }();
            struct Case { const char* name; int status; juce::MemoryBlock body; bool tooLarge; };
            const Case cases[] = {
                { "404", 404, {}, false },
                { "html", 200, htmlPage, false },
                { "forged", 200, forged, false },
                { "other key", 200, signedEnvelope (bytesOf (payload ("stable", "mac", 990)), other), false },
                { "windows manifest at mac path", 200, signedFor ("stable", 990, "windows"), false },
                { "redirect not followed", 302, {}, false },
                { "too large", 200, signedFor ("stable", 990), true },
                { "unreachable", 0, {}, false },
            };
            for (const auto& c : cases)
            {
                FakeNet net;
                if (c.status != 0)
                {
                    net.set (manifestUrl ("stable"), c.status, c.body);
                    net.replies[manifestUrl ("stable")].tooLarge = c.tooLarge;
                }
                net.set (github, 200, bytesOf (String ("990\n")));
                const auto r = check (input(), trusted, net.fetch());
                expect (r.ok, c.name);
                expect (r.source == Source::github, c.name);
                expectEquals (r.latestBuild, 990, c.name);
                expect (r.available, c.name);
                expectEquals (net.asked[net.asked.size() - 1], github, c.name);
                expectEquals (net.redirects.back(), 5, c.name);   // GitHub 은 objects.githubusercontent.com 으로 리디렉션한다
                expect (r.log.joinIntoString ("\n").contains ("GitHub"), c.name);
            }
        }

        beginTest ("Both dl and GitHub failing is a failed check; bad build.txt too");
        {
            FakeNet net;
            auto r = check (input(), trusted, net.fetch());
            expect (! r.ok);
            expect (r.source == Source::none);

            for (auto text : { "", "abc", "12a", "<html>", "9999999999" })
            {
                FakeNet n2;
                n2.set (github, 200, bytesOf (String (text)));
                r = check (input(), trusted, n2.fetch());
                expect (! r.ok, text);
            }
            FakeNet n3;
            n3.set (github, 404);
            expect (! check (input(), trusted, n3.fetch()).ok);

            // GitHub 대체가 없는 설정 (meon-app 의 엄격 모드)
            auto in = input();
            in.githubBuildTxt = {};
            FakeNet n4;
            n4.set (github, 200, bytesOf (String ("990")));
            expect (! check (in, trusted, n4.fetch()).ok);
            expect (n4.asked.size() == 1);
        }

        beginTest ("min_os: an older OS does not get the build; unknown OS version does not block");
        {
            FakeNet net;
            net.set (manifestUrl ("stable"), 200, signedFor ("stable", 980));   // min_os 10.13
            auto in = input();
            in.osVersion = "10.12.6";
            auto r = check (in, trusted, net.fetch());
            expect (r.ok && r.source == Source::dl);
            expect (! r.available);
            expect (r.osTooOld);
            in.osVersion = "10.13";
            r = check (in, trusted, net.fetch());
            expect (r.available && ! r.osTooOld);
            in.osVersion = {};
            r = check (in, trusted, net.fetch());
            expect (r.available);
        }

        beginTest ("rollout: held back unless the forced-update screen is up");
        {
            auto p = payload ("stable", "mac", 980);
            p.getDynamicObject()->setProperty ("rollout", 0);
            FakeNet net;
            net.set (manifestUrl ("stable"), 200, signedEnvelope (bytesOf (p), signer));
            auto in = input();
            auto r = check (in, trusted, net.fetch());
            expect (r.ok && ! r.available && r.heldBack);
            in.forced = true;
            r = check (in, trusted, net.fetch());
            expect (r.available && ! r.heldBack);
        }

        beginTest ("revoked_keys: remembered, and manifests signed by a revoked key are dropped");
        {
            const juce::Array<TrustedKey> both { signer.key ("test-1"), other.key ("test-2") };
            // beta (test-1) 가 test-2 를 폐기 → test-2 로 서명된 stable 은 이번 확인부터 뺀다
            auto beta = payload ("beta", "mac", 970);
            beta.getDynamicObject()->setProperty ("revoked_keys", juce::Array<var> { "test-2" });
            FakeNet net;
            net.set (manifestUrl ("beta"), 200, signedEnvelope (bytesOf (beta), signer, "test-1"));
            net.set (manifestUrl ("stable"), 200, signedEnvelope (bytesOf (payload ("stable", "mac", 990)), other, "test-2"));
            auto r = check (input ("beta"), both, net.fetch());
            expect (r.source == Source::dl);
            expectEquals (r.latestBuild, 970);
            expect (r.newlyRevoked.contains ("test-2"));

            // 기억한 폐기 키는 처음부터 믿지 않는다 → 남는 것이 없으면 GitHub
            auto in = input();
            in.revokedKeys.add ("test-1");
            FakeNet n2;
            n2.set (manifestUrl ("stable"), 200, signedFor ("stable", 980));
            n2.set (github, 200, bytesOf (String ("975")));
            r = check (in, trusted, n2.fetch());
            expect (r.source == Source::github);
            expectEquals (r.latestBuild, 975);

            // 자기 키를 폐기하는 매니페스트는 자기도 빠진다
            auto self = payload ("stable", "mac", 980);
            self.getDynamicObject()->setProperty ("revoked_keys", juce::Array<var> { "test-1" });
            FakeNet n3;
            n3.set (manifestUrl ("stable"), 200, signedEnvelope (bytesOf (self), signer));
            n3.set (github, 200, bytesOf (String ("975")));
            r = check (input(), trusted, n3.fetch());
            expect (r.source == Source::github);
            expect (r.newlyRevoked.contains ("test-1"));
        }

        beginTest ("receive: stops past the manifest size, can be stopped, replaces an old file");
        {
            juce::TemporaryFile tmp;
            const auto target = tmp.getFile();
            juce::MemoryBlock data (300000);
            for (size_t i = 0; i < data.getSize(); ++i)
                static_cast<char*> (data.getData())[i] = (char) (i * 7);

            target.replaceWithText ("old content that must go away");
            juce::int64 lastProgress = 0;
            {
                juce::MemoryInputStream in (data, false);
                const auto problem = receive (in, target, (juce::int64) data.getSize(), nullptr, [&] (juce::int64 d) { lastProgress = d; });
                expect (problem.isEmpty(), problem);
                expectEquals (target.getSize(), (juce::int64) data.getSize());
                expectEquals (lastProgress, (juce::int64) data.getSize());
                juce::MemoryBlock back;
                target.loadFileAsData (back);
                expect (back == data);
            }
            {
                juce::MemoryInputStream in (data, false);
                const auto problem = receive (in, target, (juce::int64) data.getSize() - 1, nullptr, nullptr);
                expect (problem.isNotEmpty());
                expect (! target.existsAsFile(), "넘친 파일은 지운다");
            }
            {
                juce::MemoryInputStream in (data, false);
                const auto problem = receive (in, target, 0, [] { return true; }, nullptr);
                expect (problem.isNotEmpty());
                expect (! target.existsAsFile());
            }
            {
                juce::MemoryInputStream in (data, false);
                expect (receive (in, target, 0, nullptr, nullptr).isEmpty(), "크기를 모르면 다 받는다 (GitHub)");
                expectEquals (target.getSize(), (juce::int64) data.getSize());
            }
        }

        beginTest ("checkReceived: size and sha256 must both match the manifest");
        {
            juce::TemporaryFile tmp;
            const auto f = tmp.getFile();
            juce::MemoryBlock data (2 * 1024 * 1024 + 3);
            data.fillWith (0x5a);
            f.replaceWithData (data.getData(), data.getSize());
            Package p;
            p.size = (juce::int64) data.getSize();
            p.sha256 = juce::SHA256 (data).toHexString().toLowerCase();
            expect (checkReceived (f, p).isEmpty());
            auto wrongSize = p;
            wrongSize.size += 1;
            expect (checkReceived (f, wrongSize).contains (U8 ("크기")));
            auto wrongHash = p;
            wrongHash.sha256 = String::repeatedString ("0", 64);
            expect (checkReceived (f, wrongHash).contains ("sha256"));
            // 1 바이트만 바뀌어도 다르다
            static_cast<char*> (data.getData())[12345] ^= 1;
            f.replaceWithData (data.getData(), data.getSize());
            expect (checkReceived (f, p).contains ("sha256"));
        }

        beginTest ("currentOsVersion has the version form on macOS / Windows");
        {
           #if JUCE_MAC || JUCE_WINDOWS
            const auto v = currentOsVersion();
            expect (isVersionString (v), v);
           #else
            expect (currentOsVersion().isEmpty());
           #endif
        }
    }
};

static UpdateCheckTests updateCheckTests;

/** 진짜 juce::URL 로 받기 (플랫폼 HTTP: macOS NSURLSession, Windows WinINet, Linux JUCE 소켓) */
class UpdateFetchTests : public juce::UnitTest
{
public:
    UpdateFetchTests() : juce::UnitTest ("UpdateFetch", "Transport") {}

    void runTest() override
    {
        const auto mock = juce::SystemStats::getEnvironmentVariable ("MEON_TEST_MOCK", {});
        beginTest ("fetchUrl: status, no redirects, size limit");
        if (mock.isEmpty())
        {
            logMessage ("MEON_TEST_MOCK 이 없어 건너뜀");
            return;
        }
        auto f = fetchUrl (mock + "/dl/bytes/100", 0, maxEnvelopeBytes, 5000);
        expectEquals (f.status, 200);
        expectEquals ((int) f.body.getSize(), 100);
        expect (! f.tooLarge);

        f = fetchUrl (mock + "/dl/redirect", 0, maxEnvelopeBytes, 5000);
        expectEquals (f.status, 302, "리디렉션을 따르지 않아야 한다");
        expectEquals ((int) f.body.getSize(), 0);

        f = fetchUrl (mock + "/dl/redirect", 5, maxEnvelopeBytes, 5000);
        expectEquals (f.status, 200, "GitHub 처럼 따라가면 받는다");
        expectEquals ((int) f.body.getSize(), 10);

        f = fetchUrl (mock + "/dl/bytes/70000", 0, maxEnvelopeBytes, 5000);
        expectEquals (f.status, 200);
        expect (f.tooLarge, "64 KB 를 넘으면 멈춘다");
        expect (f.body.getSize() <= maxEnvelopeBytes);

        f = fetchUrl (mock + "/dl/status/404", 0, maxEnvelopeBytes, 5000);
        expectEquals (f.status, 404);
        expectEquals ((int) f.body.getSize(), 0);

        f = fetchUrl ("http://127.0.0.1:9/nothing.json", 0, maxEnvelopeBytes, 3000);
        expect (f.status != 200);
        expectEquals ((int) f.body.getSize(), 0);
    }
};

static UpdateFetchTests updateFetchTests;

} // namespace meon
