// R11 통신 기반: 서버 선택, HTTP·WebSocket 전송.
//   ServerConfig: 늘 돈다.
//   Transport: MEON_TEST_MOCK=http://127.0.0.1:18080 일 때만 (scripts/meon_test_server.py). 없으면 건너뛴다.
//   TransportTls: MEON_TEST_TLS=1 일 때만 (바깥 인터넷: 검증되는 인증서는 붙고, 자체 서명 인증서는 거부).
#include <JuceHeader.h>
#include "../core/api/net/ServerConfig.h"
#include "../core/api/net/HttpTransport.h"
#include "../core/api/net/WsTransport.h"
#include "../core/api/net/SystemCerts.h"

using namespace meon::core;
using juce::var;

namespace
{

struct RecordingSink : RealtimeTransport::Sink
{
    int opened = 0, closed = 0, lastCode = -1;
    std::vector<var> messages;
    void transportOpened() override                      { ++opened; }
    void transportMessage (const var& m) override        { messages.push_back (m); }
    void transportClosed (int code) override             { ++closed; lastCode = code; }
};

juce::StringPairArray dev (std::initializer_list<std::pair<const char*, const char*>> kv)
{
    juce::StringPairArray a;
    for (auto& p : kv) a.set (p.first, p.second);
    return a;
}

juce::String env (const char* name) { return juce::SystemStats::getEnvironmentVariable (name, {}); }

} // namespace

class ServerConfigTests : public juce::UnitTest
{
public:
    ServerConfigTests() : juce::UnitTest ("ServerConfig", "Transport") {}

    void runTest() override
    {
        beginTest ("real build numbers always use the production server, whatever the dev options say");
        {
            const auto c = ServerConfig::choose (58, dev ({ { "server", "fake" } }));
            expect (c.kind == ServerConfig::Kind::prod);
            expectEquals (c.restBase, juce::String ("https://api.meon.co.kr/v1"));
            expectEquals (c.wsUrl, juce::String ("wss://api.meon.co.kr/v1/ws"));
            expectEquals (c.aooHost, juce::String ("aoo.meon.co.kr"));
            expectEquals (c.aooPort, 10999);
            expectEquals (c.tokenItem, juce::String ("refresh-token"));
        }

        beginTest ("build 0 defaults to the fake server (plugin too: it gets no dev options)");
        {
            expect (ServerConfig::choose (0, {}).isFake());
        }

        beginTest ("--server=local is the docker compose server");
        {
            const auto c = ServerConfig::choose (0, dev ({ { "server", "local" } }));
            expect (c.kind == ServerConfig::Kind::local);
            expectEquals (c.restBase, juce::String ("http://localhost:8080/v1"));
            expectEquals (c.wsUrl, juce::String ("ws://localhost:8080/v1/ws"));
            expectEquals (c.aooHost, juce::String ("localhost"));
            expectEquals (c.tokenItem, juce::String ("refresh-token@localhost:8080"));
        }

        beginTest ("build 0: --server=prod|local|fake|<url>");
        {
            expect (ServerConfig::choose (0, dev ({ { "server", "prod" } })).kind == ServerConfig::Kind::prod);
            expect (ServerConfig::choose (0, dev ({ { "server", "LOCAL" } })).kind == ServerConfig::Kind::local);
            expect (ServerConfig::choose (0, dev ({ { "server", "fake" } })).isFake());
            const auto c = ServerConfig::choose (0, dev ({ { "server", "https://dev.example.com/v1/" } }));
            expect (c.kind == ServerConfig::Kind::custom);
            expectEquals (c.restBase, juce::String ("https://dev.example.com/v1"));
            expectEquals (c.wsUrl, juce::String ("wss://dev.example.com/v1/ws"));
            expectEquals (c.aooHost, juce::String ("dev.example.com"));
            expectEquals (c.tokenItem, juce::String ("refresh-token@dev.example.com"));
            const auto l = ServerConfig::choose (0, dev ({ { "server", "http://192.168.0.5:8080" } }));
            expectEquals (l.wsUrl, juce::String ("ws://192.168.0.5:8080/v1/ws"));
            expectEquals (l.aooHost, juce::String ("192.168.0.5"));
            expect (ServerConfig::choose (0, dev ({ { "server", "nonsense" } })).isFake());
        }

        beginTest ("screen previews and dev sign-up stay on the fake server unless --server is given");
        {
            expect (ServerConfig::choose (0, dev ({ { "screen", "home" } })).isFake());
            expect (ServerConfig::choose (0, dev ({ { "dev-signup", "a" } })).isFake());
            expect (ServerConfig::choose (0, dev ({ { "dev-signup", "a" }, { "server", "local" } })).kind == ServerConfig::Kind::local);
            expectEquals (ServerConfig::fake().tokenItem, juce::String ("fake-refresh-token"));
        }
    }
};

class TransportTests : public juce::UnitTest
{
public:
    TransportTests() : juce::UnitTest ("Transport (mock server)", "Transport") {}

    CallbackQueue queue;

    HttpResponse http (const juce::String& base, HttpRequest req)
    {
        HttpTransport t (base, queue.deliver());
        t.timeoutMs = 5000;
        HttpResponse out;
        bool done = false;
        t.send (req, [&] (HttpResponse r) { out = r; done = true; });
        expect (queue.runUntil ([&] { return done; }, 10000), "HTTP 응답이 오지 않음");
        return out;
    }

    void runTest() override
    {
        const auto mock = env ("MEON_TEST_MOCK");
        beginTest ("HTTP: method, headers, body");
        if (mock.isEmpty())
        {
            logMessage ("MEON_TEST_MOCK 이 없어 건너뜀");
            return;
        }
        const auto base = mock + "/v1";
        {
            HttpRequest r { "GET", "/echo/a?x=1&y=%ED%95%9C", {}, "tok", 42 };
            auto res = http (base, r);
            expectEquals (res.status, 200);
            expectEquals (res.body["method"].toString(), juce::String ("GET"));
            expectEquals (res.body["path"].toString(), juce::String ("/v1/echo/a"));
            expectEquals (res.body["query"]["x"].toString(), juce::String ("1"));
            expectEquals (res.body["query"]["y"].toString(), juce::String (juce::CharPointer_UTF8 ("\xed\x95\x9c")));
            expectEquals (res.body["headers"]["authorization"].toString(), juce::String ("Bearer tok"));
            expectEquals (res.body["headers"]["x-meon-build"].toString(), juce::String ("42"));
            expectEquals ((int) res.body["body_length"], 0);
        }
        {
            juce::DynamicObject::Ptr o = new juce::DynamicObject();
            o->setProperty ("a", 1);
            o->setProperty ("name", juce::String (juce::CharPointer_UTF8 ("\xec\x8a\xb9\xec\xb0\xac")));
            auto res = http (base, { "POST", "/echo", var (o.get()), {}, 0 });
            expectEquals (res.body["method"].toString(), juce::String ("POST"));
            expectEquals ((int) res.body["body"]["a"], 1);
            expectEquals (res.body["body"]["name"].toString(), juce::String (juce::CharPointer_UTF8 ("\xec\x8a\xb9\xec\xb0\xac")));
            expect (res.body["headers"]["content-type"].toString().startsWith ("application/json"));
            expect (! res.body["headers"].hasProperty ("authorization"), "토큰이 없으면 Authorization 도 없다");

            auto patch = http (base, { "PATCH", "/echo", var (o.get()), "t", 1 });
            expectEquals (patch.body["method"].toString(), juce::String ("PATCH"));
            expectEquals ((int) patch.body["body"]["a"], 1);
        }
        {
            auto post = http (base, { "POST", "/echo/empty", {}, "t", 1 });
            expectEquals (post.body["method"].toString(), juce::String ("POST"));
            expectEquals ((int) post.body["body_length"], 0);
            auto del = http (base, { "DELETE", "/echo/x", {}, "t", 1 });
            expectEquals (del.body["method"].toString(), juce::String ("DELETE"));
        }

        beginTest ("HTTP: error status keeps the JSON body; 429 Retry-After; 204");
        {
            auto e = http (base, { "POST", "/status/409", {}, {}, 1 });
            expectEquals (e.status, 409);
            expectEquals (e.body["error"]["code"].toString(), juce::String ("x_409"));
            auto u = http (base, { "GET", "/status/401", {}, {}, 1 });
            expectEquals (u.status, 401);
            expectEquals (u.body["error"]["code"].toString(), juce::String ("x_401"));
            auto o = http (base, { "GET", "/status/426", {}, {}, 1 });
            expectEquals (o.status, 426);
            auto r = http (base, { "GET", "/status/429", {}, {}, 1 });
            expectEquals (r.status, 429);
            expectEquals (r.retryAfterSec, 7);
            auto n = http (base, { "POST", "/status/204", {}, {}, 1 });
            expectEquals (n.status, 204);
            expect (n.body.isVoid());
        }

        beginTest ("HTTP: unreachable server is status 0");
        {
            auto res = http ("http://127.0.0.1:1/v1", { "GET", "/x", {}, {}, 1 });
            expectEquals (res.status, 0);
        }

        const auto wsUrl = "ws" + mock.fromFirstOccurrenceOf ("http", false, false) + "/v1/ws";

        beginTest ("WebSocket: open, echo, server close code");
        {
            RecordingSink sink;
            WsTransport ws (wsUrl, queue.deliver());
            ws.open (&sink);
            expect (queue.runUntil ([&] { return sink.opened == 1; }, 10000), "열리지 않음");
            juce::DynamicObject::Ptr o = new juce::DynamicObject();
            o->setProperty ("type", "ping");
            o->setProperty ("n", 7);
            ws.send (var (o.get()));
            expect (queue.runUntil ([&] { return sink.messages.size() == 1; }, 10000), "메아리가 오지 않음");
            if (! sink.messages.empty())
                expectEquals ((int) sink.messages[0]["n"], 7);

            for (int code : { 4000, 4001, 4002, 1008 })
            {
                if (code != 4000)
                {
                    const int openedBefore = sink.opened;
                    ws.open (&sink);
                    expect (queue.runUntil ([&] { return sink.opened == openedBefore + 1; }, 10000), "다시 열리지 않음");
                }
                const int closedBefore = sink.closed;
                juce::DynamicObject::Ptr c = new juce::DynamicObject();
                c->setProperty ("close", code);
                ws.send (var (c.get()));
                expect (queue.runUntil ([&] { return sink.closed == closedBefore + 1; }, 10000), "닫히지 않음");
                expectEquals (sink.lastCode, code);
            }
        }

        beginTest ("WebSocket: dropped connection and unreachable server report code 0");
        {
            RecordingSink sink;
            WsTransport ws (wsUrl, queue.deliver());
            ws.open (&sink);
            expect (queue.runUntil ([&] { return sink.opened == 1; }, 10000));
            juce::DynamicObject::Ptr d = new juce::DynamicObject();
            d->setProperty ("drop", true);
            ws.send (var (d.get()));
            expect (queue.runUntil ([&] { return sink.closed == 1; }, 10000), "끊김을 알리지 않음");
            expectEquals (sink.lastCode, 0);

            RecordingSink bad;
            WsTransport none ("ws://127.0.0.1:1/v1/ws", queue.deliver());
            none.handshakeTimeoutSecs = 3;
            none.open (&bad);
            expect (queue.runUntil ([&] { return bad.closed == 1; }, 10000), "연결 실패를 알리지 않음");
            expectEquals (bad.opened, 0);
            expectEquals (bad.lastCode, 0);
        }

        beginTest ("WebSocket: nothing is delivered after close()");
        {
            RecordingSink sink;
            {
                WsTransport ws (wsUrl, queue.deliver());
                ws.open (&sink);
                expect (queue.runUntil ([&] { return sink.opened == 1; }, 10000));
                juce::DynamicObject::Ptr o = new juce::DynamicObject();
                o->setProperty ("n", 1);
                ws.send (var (o.get()));
                ws.close();
            }
            queue.runUntil ([] { return false; }, 300);
            expectEquals (sink.closed, 0);
        }
    }
};

class TransportTlsTests : public juce::UnitTest
{
public:
    TransportTlsTests() : juce::UnitTest ("Transport (TLS, internet)", "TransportTls") {}

    CallbackQueue queue;

    void runTest() override
    {
        beginTest ("TLS: system root certificates");
        if (env ("MEON_TEST_TLS") != "1")
        {
            logMessage ("MEON_TEST_TLS=1 이 아니라 건너뜀");
            return;
        }
       #if JUCE_MAC
        logMessage ("macOS 키체인 루트 인증서: " + juce::String (certs::systemRootCount()));
        expectGreaterThan (certs::systemRootCount(), 50);
       #endif
        expect (! certs::caFileForTls().empty());

        beginTest ("TLS: HTTPS through the OS");
        {
            auto res = HttpTransport::perform ("https://github.com", { "GET", "/", {}, {}, 1 }, 15000);
            logMessage ("https://github.com → " + juce::String (res.status));
            expect (res.status >= 200 && res.status < 500);
        }

        beginTest ("TLS: wss with a valid certificate connects");
        {
            RecordingSink sink;
            WsTransport ws (env ("MEON_TEST_WSS").isNotEmpty() ? env ("MEON_TEST_WSS") : juce::String ("wss://echo.websocket.org"), queue.deliver());
            ws.open (&sink);
            expect (queue.runUntil ([&] { return sink.opened == 1 || sink.closed > 0; }, 20000), "응답 없음");
            expectEquals (sink.opened, 1, "검증된 인증서로 열려야 함");
            juce::DynamicObject::Ptr o = new juce::DynamicObject();
            o->setProperty ("meon", 1);
            ws.send (var (o.get()));
            expect (queue.runUntil ([&] { for (auto& m : sink.messages) if ((int) m["meon"] == 1) return true; return false; }, 20000), "메아리 없음");
        }

        beginTest ("TLS: wss with a self-signed certificate is refused");
        {
            RecordingSink sink;
            WsTransport ws ("wss://self-signed.badssl.com/", queue.deliver());
            ws.open (&sink);
            expect (queue.runUntil ([&] { return sink.closed > 0 || sink.opened > 0; }, 20000), "응답 없음");
            expectEquals (sink.opened, 0, "자체 서명 인증서는 거부해야 함");
        }
    }
};

static ServerConfigTests serverConfigTests;
static TransportTests transportTests;
static TransportTlsTests transportTlsTests;
