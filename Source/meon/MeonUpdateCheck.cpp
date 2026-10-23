#include "MeonUpdateCheck.h"

namespace meon::update
{

using juce::String;

#define CTXT(s) juce::String (juce::CharPointer_UTF8 (s))

Fetched fetchUrl (const String& url, int maxRedirects, size_t maxBytes, int timeoutMs)
{
    Fetched f;
    int status = 0;
    auto in = juce::URL (url).createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                     .withConnectionTimeoutMs (timeoutMs)
                                                     .withNumRedirectsToFollow (maxRedirects)
                                                     .withStatusCode (&status));
    f.status = status;
    if (in == nullptr || status != 200)
        return f;

    juce::MemoryOutputStream out;
    char buffer[8192];
    size_t total = 0;
    while (! in->isExhausted())
    {
        const int n = in->read (buffer, (int) sizeof (buffer));
        if (n <= 0)
            break;
        total += (size_t) n;
        if (total > maxBytes)
        {
            f.tooLarge = true;
            break;
        }
        out.write (buffer, (size_t) n);
    }
    f.body = out.getMemoryBlock();
    return f;
}

String sourceName (Source s)
{
    switch (s)
    {
        case Source::dl:     return "dl";
        case Source::github: return "github";
        case Source::none:   break;
    }
    return "-";
}

CheckResult check (const CheckInput& in, const juce::Array<TrustedKey>& keys, const Fetch& fetch)
{
    CheckResult res;
    const bool beta = in.channel == "beta";
    juce::StringArray channels;
    channels.add (beta ? "beta" : "stable");
    if (beta)
        channels.add ("stable");   // beta 앱은 stable 도 읽어 큰 번호를 쓴다 (stable 보다 뒤처지지 않게)

    // 1. 채널 매니페스트를 받아 검증한다 (하나라도 틀리면 그 매니페스트는 없는 것)
    juce::Array<Manifest> found;
    juce::StringArray why;
    for (const auto& ch : channels)
    {
        const auto url = in.manifestBase + "meon/v1/" + ch + "/" + in.platform + ".json";
        const auto f = fetch (url, 0, maxEnvelopeBytes);
        if (f.status == 0)   { why.add (ch + CTXT (": 연결 실패")); continue; }
        if (f.status != 200) { why.add (ch + ": HTTP " + String (f.status)); continue; }
        if (f.tooLarge)      { why.add (ch + CTXT (": 64 KB 보다 큼")); continue; }
        const auto r = verify (f.body, expectFor (in.platform, ch), keys, in.revokedKeys);
        if (! r.ok)          { why.add (ch + ": " + r.reason); continue; }
        found.add (r.manifest);
    }

    // 2. 믿는 키로 확인된 매니페스트가 알린 폐기 키: 기억하고, 이번 확인부터 그 키로 서명된 것은 뺀다
    juce::StringArray revoked (in.revokedKeys);
    for (const auto& m : found)
        for (const auto& k : m.revokedKeys)
            if (! revoked.contains (k))
            {
                revoked.add (k);
                res.newlyRevoked.add (k);
            }
    if (! res.newlyRevoked.isEmpty())
        res.log.add (CTXT ("폐기된 키를 기억함: ") + res.newlyRevoked.joinIntoString (", "));
    for (int i = found.size(); --i >= 0;)
    {
        if (revoked.contains (found.getReference (i).keyId))
        {
            why.add (found.getReference (i).channel + CTXT (": 폐기된 키 ") + found.getReference (i).keyId);
            found.remove (i);
        }
    }

    // 3. 가장 큰 번호 (출처 dl)
    if (! found.isEmpty())
    {
        int best = 0;
        for (int i = 1; i < found.size(); ++i)
            if (found.getReference (i).build > found.getReference (best).build)
                best = i;
        res.manifest = found[best];
        res.ok = true;
        res.source = Source::dl;
        res.latestBuild = res.manifest.build;
        const auto& m = res.manifest;
        String line = CTXT ("출처 dl: ") + m.channel + " build " + String (m.build) + CTXT (" (키 ") + m.keyId + ")";
        if (! why.isEmpty())
            line += " / " + why.joinIntoString ("; ");
        res.log.add (line);

        if (m.build > in.currentBuild)
        {
            if (m.minOs.isNotEmpty() && in.osVersion.isNotEmpty() && ! versionAtLeast (in.osVersion, m.minOs))
            {
                res.osTooOld = true;
                res.log.add (CTXT ("새 빌드는 OS ") + m.minOs + CTXT (" 이상이 필요함 (이 컴퓨터 ") + in.osVersion + ")");
            }
            else if (! in.forced && ! rolloutAllows (in.installId, m.build, m.rollout))
            {
                res.heldBack = true;
                res.log.add (CTXT ("단계적 배포 ") + String (m.rollout) + CTXT ("% 에 아직 들지 않음"));
            }
            else
            {
                res.available = true;
                if (m.minOs.isNotEmpty() && in.osVersion.isEmpty())
                    res.log.add (CTXT ("OS 버전을 몰라 min_os ") + m.minOs + CTXT (" 은 보지 않음"));
            }
        }
        return res;
    }

    // 4. 검증된 매니페스트가 하나도 없을 때만 GitHub (지금 방식 그대로, 해시 없음)
    res.log.add (CTXT ("dl 에 검증된 매니페스트 없음 (") + why.joinIntoString ("; ") + CTXT (") → GitHub"));
    if (in.githubBuildTxt.isEmpty())
        return res;

    const auto f = fetch (in.githubBuildTxt, 5, 64);
    const auto text = f.status == 200 && ! f.tooLarge ? f.body.toString().trim() : String();
    if (text.isEmpty() || text.length() > 9 || ! text.containsOnly ("0123456789"))
    {
        res.log.add (CTXT ("GitHub build.txt 실패 (") + (f.status == 0 ? CTXT ("연결 실패") : "HTTP " + String (f.status)) + ")");
        return res;
    }
    res.ok = true;
    res.source = Source::github;
    res.latestBuild = text.getIntValue();
    res.available = res.latestBuild > in.currentBuild;
    res.log.add (CTXT ("출처 github: build ") + text);
    return res;
}

String receive (juce::InputStream& in, const juce::File& target, juce::int64 expectedSize,
                const std::function<bool()>& shouldStop, const std::function<void (juce::int64)>& progress)
{
    target.deleteFile();   // FileOutputStream 은 있는 파일 뒤에 이어 쓴다
    String problem;
    {
        juce::FileOutputStream out (target);
        if (! out.openedOk())
            return CTXT ("파일을 쓸 수 없음: ") + target.getFullPathName();
        juce::HeapBlock<char> buffer (64 * 1024);
        juce::int64 done = 0;
        while (problem.isEmpty() && ! in.isExhausted())
        {
            if (shouldStop && shouldStop())
            {
                problem = CTXT ("멈춤");
                break;
            }
            const int n = in.read (buffer, 64 * 1024);
            if (n < 0)  { problem = CTXT ("읽기 오류"); break; }
            if (n == 0) break;
            done += n;
            if (expectedSize > 0 && done > expectedSize)
            {
                problem = CTXT ("매니페스트의 크기(") + String (expectedSize) + CTXT (" 바이트)보다 커서 멈춤");
                break;
            }
            if (! out.write (buffer, (size_t) n))
            {
                problem = CTXT ("디스크에 쓰지 못함");
                break;
            }
            if (progress)
                progress (done);
        }
        out.flush();
        if (problem.isEmpty() && out.getStatus().failed())
            problem = CTXT ("디스크에 쓰지 못함");
    }
    if (problem.isNotEmpty())
        target.deleteFile();
    return problem;
}

String checkReceived (const juce::File& f, const Package& p)
{
    const auto size = f.getSize();
    if (size != p.size)
        return CTXT ("크기가 다름 (받은 ") + String (size) + CTXT (", 매니페스트 ") + String (p.size) + ")";
    const auto sha = sha256OfFile (f);
    if (sha != p.sha256)
        return CTXT ("sha256 이 다름 (받은 ") + sha + CTXT (", 매니페스트 ") + p.sha256 + ")";
    return {};
}

String currentOsVersion()
{
   #if JUCE_MAC
    // "Mac OSX 26.0.1" (JUCE: SystemVersion.plist 의 ProductVersion)
    const auto v = juce::SystemStats::getOperatingSystemName().fromLastOccurrenceOf (" ", false, false).trim();
    return isVersionString (v) ? v : String();
   #elif JUCE_WINDOWS
    switch (juce::SystemStats::getOperatingSystemType())
    {
        case juce::SystemStats::Windows11:
        case juce::SystemStats::Windows10:  return "10.0";
        case juce::SystemStats::Windows8_1: return "6.3";
        case juce::SystemStats::Windows8_0: return "6.2";
        case juce::SystemStats::Windows7:   return "6.1";
        case juce::SystemStats::WinVista:   return "6.0";
        default:                            return {};
    }
   #else
    return {};
   #endif
}

} // namespace meon::update
