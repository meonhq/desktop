// MEON core - 서버와 바이트를 주고받는 층. 위의 MeonApi / Realtime 은 이 인터페이스만 안다.
//   진짜 서버: net/HttpTransport (REST), net/WsTransport (WebSocket). 어느 서버인지는 net/ServerConfig.
//   가짜 서버(개발 옵션 --server=fake, 단위 테스트): FakeMeonServer 가 두 인터페이스를 다 구현한다.
#pragma once

#include <JuceHeader.h>

namespace meon::core
{

struct HttpRequest
{
    juce::String method;        // GET POST PATCH DELETE
    juce::String path;          // "/rooms/r_1/join" (/v1 뒤). 쿼리 포함 가능
    juce::var body;             // 없으면 void
    juce::String accessToken;   // 비어 있으면 Authorization 헤더 없음
    int build = 0;              // X-Meon-Build
};

struct HttpResponse
{
    int status = 0;             // 0 = 네트워크 오류 (응답 없음)
    juce::var body;
    int retryAfterSec = 0;      // 429 의 Retry-After
    bool timedOut = false;
};

class RestTransport
{
public:
    virtual ~RestTransport() = default;
    /** 메시지 스레드에서 부르고, 결과도 메시지 스레드로 온다 */
    virtual void send (const HttpRequest&, std::function<void (HttpResponse)> done) = 0;
    /** 응답을 받을 때까지 기다린다 (최대 timeoutMs). 부른 스레드를 막으므로 종료할 때(방 나가기)만 쓴다.
        기본: 보내지 못함 (status 0) */
    virtual HttpResponse sendBlocking (const HttpRequest&, int timeoutMs) { juce::ignoreUnused (timeoutMs); return {}; }
};

class RealtimeTransport
{
public:
    struct Sink
    {
        virtual ~Sink() = default;
        virtual void transportOpened() = 0;
        virtual void transportMessage (const juce::var& message) = 0;
        /** 닫힘. code 는 서버가 보낸 WebSocket 닫는 코드 (API 10.1: 4000 다른 곳에서 접속, 4001 세션 폐기,
            4002 느린 클라이언트, 1008 인증 실패 …). 연결 실패·네트워크 끊김처럼 닫는 코드가 없으면 0 */
        virtual void transportClosed (int code) = 0;
    };

    virtual ~RealtimeTransport() = default;
    /** 연결을 연다. 결과는 sink 로 (메시지 스레드). 이미 열려 있으면 닫고 다시 연다. */
    virtual void open (Sink* sink) = 0;
    virtual void send (const juce::var& message) = 0;
    virtual void close() = 0;
};

} // namespace meon::core
