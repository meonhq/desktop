#include "HttpTransport.h"

#if JUCE_LINUX
 #include "SystemCerts.h"
 #include <ixwebsocket/IXHttpClient.h>
#endif

namespace meon::core
{

HttpTransport::HttpTransport (juce::String restBase, Deliver d)
    : base (std::move (restBase)), deliver (std::move (d)), alive (std::make_shared<std::atomic<bool>> (true))
{
}

HttpTransport::~HttpTransport()
{
    *alive = false;   // 이미 넘긴 결과는 실행하지 않는다
    pool.removeAllJobs (true, 3000);
}

void HttpTransport::send (const HttpRequest& req, std::function<void (HttpResponse)> done)
{
    pool.addJob ([req, done = std::move (done), restBase = base, timeout = timeoutMs, flag = alive, d = deliver]
    {
        if (! *flag) return;
        auto res = perform (restBase, req, timeout);
        d ([flag, done, res]
        {
            if (*flag && done) done (res);
        });
    });
}

[[maybe_unused]] static int parseRetryAfter (const juce::StringPairArray& headers)
{
    for (const auto& k : headers.getAllKeys())
        if (k.equalsIgnoreCase ("Retry-After"))
            return juce::jmax (0, headers[k].getIntValue());
    return 0;
}

static void parseBody (HttpResponse& res, const juce::String& text)
{
    if (text.isEmpty()) return;
    juce::var parsed;
    if (juce::JSON::parse (text, parsed).wasOk())
        res.body = parsed;
}

#if JUCE_LINUX
// Linux: JUCE 7 의 소켓 HTTP 는 본문이 있는 요청을 깨뜨린다 (헤더 뒤에 빈 줄이 하나 더 들어가고 Content-Length 가 둘).
// 그래서 IXWebSocket 의 HTTP 클라이언트로 보낸다 (http·https 둘 다). Linux 앱은 배포하지 않고 테스트·로컬 확인용이다
HttpResponse HttpTransport::perform (const juce::String& restBase, const HttpRequest& req, int timeoutMs)
{
    HttpResponse res;
    const bool hasBody = ! req.body.isVoid() && ! req.body.isUndefined();
    const auto method = req.method.toUpperCase();
    const auto url = (restBase + req.path).toStdString();

    ix::HttpClient client (false);
    if (restBase.startsWithIgnoreCase ("https://"))
    {
        ix::SocketTLSOptions tls;
        tls.caFile = certs::caFileForTls();
        client.setTLSOptions (tls);
    }
    auto args = client.createRequest (url, method.toStdString());
    args->extraHeaders["Accept"] = "application/json";
    args->extraHeaders["X-Meon-Build"] = juce::String (req.build).toStdString();
    if (hasBody) args->extraHeaders["Content-Type"] = "application/json";
    if (req.accessToken.isNotEmpty()) args->extraHeaders["Authorization"] = ("Bearer " + req.accessToken).toStdString();
    args->connectTimeout = juce::jmax (1, timeoutMs / 1000);
    args->transferTimeout = juce::jmax (1, timeoutMs / 1000);
    args->followRedirects = false;
    args->compress = false;

    const auto body = hasBody ? juce::JSON::toString (req.body, true).toStdString() : std::string();
    auto r = client.request (url, method.toStdString(), body, args);
    if (r == nullptr || r->errorCode != ix::HttpErrorCode::Ok)
    {
        res.status = 0;
        res.timedOut = r != nullptr && r->errorCode == ix::HttpErrorCode::Timeout;
        return res;
    }
    res.status = r->statusCode;
    for (const auto& h : r->headers)
        if (juce::String (h.first).equalsIgnoreCase ("Retry-After"))
            res.retryAfterSec = juce::jmax (0, juce::String (h.second).getIntValue());
    parseBody (res, juce::String::fromUTF8 (r->body.data(), (int) r->body.size()));
    return res;
}
#else
HttpResponse HttpTransport::perform (const juce::String& restBase, const HttpRequest& req, int timeoutMs)
{
    HttpResponse res;
    const bool hasBody = ! req.body.isVoid() && ! req.body.isUndefined();
    const auto method = req.method.toUpperCase();

    juce::URL url (restBase + req.path);
    if (hasBody)
        url = url.withPOSTData (juce::JSON::toString (req.body, true));

    // GET 은 주소만, 나머지는 본문이 있으면 그대로, 없으면 빈 본문으로 보낸다
    const bool usePost = method != "GET";
    juce::WebInputStream stream (url, usePost);

    // 줄 사이에만 줄바꿈을 넣는다 (JUCE 가 끝에 줄바꿈을 붙이므로 끝에 넣으면 빈 줄이 생겨 헤더가 거기서 끝난다)
    juce::StringArray headerLines { "Accept: application/json", "X-Meon-Build: " + juce::String (req.build) };
    if (hasBody) headerLines.add ("Content-Type: application/json");
    if (req.accessToken.isNotEmpty()) headerLines.add ("Authorization: Bearer " + req.accessToken);

    stream.withExtraHeaders (headerLines.joinIntoString ("\r\n"))
          .withConnectionTimeout (timeoutMs)
          .withNumRedirectsToFollow (0);
    if (method != "GET" && method != "POST")
        stream.withCustomRequestCommand (method);

    const auto started = juce::Time::getMillisecondCounter();
    if (! stream.connect (nullptr) || stream.isError())
    {
        res.status = 0;
        res.timedOut = juce::Time::getMillisecondCounter() - started >= (juce::uint32) juce::jmax (0, timeoutMs - 100);
        return res;
    }

    res.status = stream.getStatusCode();
    res.retryAfterSec = parseRetryAfter (stream.getResponseHeaders());
    parseBody (res, stream.readEntireStreamAsString());
    return res;
}
#endif

} // namespace meon::core
