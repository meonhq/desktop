#include "WsTransport.h"
#include "SystemCerts.h"
#include "../../log/AppLog.h"

#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>

namespace meon::core
{

struct WsTransport::Connection
{
    ix::WebSocket ws;
    Sink* sink = nullptr;
    std::atomic<bool> live { true };          // false 면 이 연결에서 온 것은 버린다 (메시지 스레드에서 바꾸고 확인한다)
    std::atomic<bool> closedSent { false };   // 닫힘은 한 번만 알린다 (오류 뒤 닫힘이 또 올 수 있다)
};

WsTransport::WsTransport (juce::String u, Deliver d) : url (std::move (u)), deliver (std::move (d))
{
    static const bool netReady = ix::initNetSystem();   // Windows: WSAStartup (한 번)
    juce::ignoreUnused (netReady);
}

WsTransport::~WsTransport()
{
    close();
}

void WsTransport::retire (std::shared_ptr<Connection> c)
{
    if (c == nullptr) return;
    c->live = false;
    c->ws.stop();   // 닫는 프레임을 보내고 네트워크 스레드가 끝날 때까지 기다린다
}

void WsTransport::open (Sink* sink)
{
    retire (std::move (current));

    auto c = std::make_shared<Connection>();
    c->sink = sink;
    c->ws.setUrl (url.toStdString());
    c->ws.disableAutomaticReconnection();
    c->ws.disablePerMessageDeflate();
    c->ws.setHandshakeTimeout (handshakeTimeoutSecs);
    // 하트비트는 앱이 JSON ping 으로 (API 10.3). IXWebSocket 자체 ping 은 끈다 (기본값 -1 = 끔).
    // 0 을 넣으면 안 된다: 연결된 동안 이 값이 poll 대기 시간이 되어 0 이면 쉬지 않고 돌아 CPU 코어 하나를 다 쓴다 (엔진 멈춤 조사, 2026-10)
    c->ws.setPingInterval (-1);

    juce::String tlsNote;
    if (url.startsWithIgnoreCase ("wss://"))
    {
        ix::SocketTLSOptions tls;
        tls.caFile = certs::caFileForTls();
        if (tls.caFile.empty()) tlsNote = LOGTXT (" (루트 인증서를 찾지 못함: 연결이 실패할 수 있음)");
        c->ws.setTLSOptions (tls);
    }

    std::weak_ptr<Connection> weak = c;
    const Deliver d = deliver;
    auto onMain = [weak, d] (std::function<void (Connection&)> fn)
    {
        d ([weak, fn = std::move (fn)]
        {
            if (auto conn = weak.lock(); conn != nullptr && conn->live && conn->sink != nullptr)
                fn (*conn);
        });
    };

    c->ws.setOnMessageCallback ([weak, onMain] (const ix::WebSocketMessagePtr& m)
    {
        auto conn = weak.lock();
        if (conn == nullptr) return;

        switch (m->type)
        {
            case ix::WebSocketMessageType::Open:
                onMain ([] (Connection& x) { x.sink->transportOpened(); });
                break;

            case ix::WebSocketMessageType::Message:
            {
                juce::var v;
                const auto text = juce::String::fromUTF8 (m->str.data(), (int) m->str.size());
                if (juce::JSON::parse (text, v).wasOk() && v.isObject())
                    onMain ([v] (Connection& x) { x.sink->transportMessage (v); });
                break;
            }

            case ix::WebSocketMessageType::Close:
            {
                // 서버가 닫는 프레임으로 알려 준 코드만 쓴다. 네트워크가 끊겨 라이브러리가 만든 닫힘은 0
                const int code = m->closeInfo.remote ? (int) m->closeInfo.code : 0;
                if (! conn->closedSent.exchange (true))
                    onMain ([code] (Connection& x) { x.sink->transportClosed (code); });
                break;
            }

            case ix::WebSocketMessageType::Error:
            {
                const auto reason = juce::String (m->errorInfo.reason) + " (HTTP " + juce::String (m->errorInfo.http_status) + ")";
                if (! conn->closedSent.exchange (true))
                    onMain ([reason] (Connection& x)
                    {
                        AppLog::write ("realtime", LOGTXT ("WebSocket 연결 실패: ") + reason);
                        x.sink->transportClosed (0);
                    });
                break;
            }

            default:
                break;
        }
    });

    AppLog::write ("realtime", LOGTXT ("WebSocket 연결 시작: ") + url + tlsNote);
    current = c;
    c->ws.start();
}

void WsTransport::send (const juce::var& message)
{
    if (current == nullptr || current->ws.getReadyState() != ix::ReadyState::Open) return;
    current->ws.sendText (juce::JSON::toString (message, true).toStdString());
}

void WsTransport::close()
{
    retire (std::move (current));
    current.reset();
}

} // namespace meon::core
