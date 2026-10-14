// MEON core - 서버와 바이트를 주고받는 층. 위의 MeonApi / Realtime 은 이 인터페이스만 안다.
//   지금: FakeMeonServer 가 두 인터페이스를 다 구현한다 (메모리 안에서 JSON 을 주고받음).
//   진짜 서버 단계(R11): HTTP 구현과 WebSocket 구현으로 바꾼다. 위층 코드는 그대로.
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
};

class RealtimeTransport
{
public:
    struct Sink
    {
        virtual ~Sink() = default;
        virtual void transportOpened() = 0;
        virtual void transportMessage (const juce::var& message) = 0;
        virtual void transportClosed() = 0;   // 연결 실패도 여기로
    };

    virtual ~RealtimeTransport() = default;
    /** 연결을 연다. 결과는 sink 로 (메시지 스레드). 이미 열려 있으면 닫고 다시 연다. */
    virtual void open (Sink* sink) = 0;
    virtual void send (const juce::var& message) = 0;
    virtual void close() = 0;
};

} // namespace meon::core
