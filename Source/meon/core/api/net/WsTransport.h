// MEON core - 진짜 서버 WebSocket 전송 (R11). IXWebSocket + mbedTLS (Q23).
//   wss:// 의 인증서 검증: Windows 는 시스템 인증서 저장소, macOS 는 키체인의 시스템 루트 인증서(SystemCerts),
//   Linux 는 /etc/ssl/certs 묶음.
//   자동 재연결은 끈다. 다시 연결하는 시점은 위층(Realtime)이 정한다 (API 10.4).
// 네트워크 스레드에서 받은 것은 Deliver 로 메시지 스레드에 넘기고, 이미 닫았거나 다시 연 연결의 것은 버린다.
#pragma once

#include "../Transport.h"
#include "Deliver.h"

namespace meon::core
{

class WsTransport : public RealtimeTransport
{
public:
    /** url: "wss://api.meon.co.kr/v1/ws" */
    explicit WsTransport (juce::String url, Deliver deliver = messageThreadDeliver());
    ~WsTransport() override;

    void open (Sink*) override;
    void send (const juce::var& message) override;
    void close() override;

    /** 연결·핸드셰이크를 기다리는 최대 시간 (초) */
    int handshakeTimeoutSecs = 10;

private:
    struct Connection;
    const juce::String url;
    Deliver deliver;
    std::shared_ptr<Connection> current;

    void retire (std::shared_ptr<Connection>);

    JUCE_DECLARE_NON_COPYABLE (WsTransport)
};

} // namespace meon::core
