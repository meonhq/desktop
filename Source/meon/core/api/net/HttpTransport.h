// MEON core - 진짜 서버 REST 전송 (R11).
//   macOS·Windows: JUCE WebInputStream. NSURLSession·WinINet 이 TLS 와 시스템 인증서를 맡는다.
//   Linux: IXWebSocket 의 HTTP 클라이언트 (JUCE 7 Linux 소켓 HTTP 가 본문 있는 요청을 깨뜨려서). 테스트·로컬 확인용.
// 요청은 작업 스레드에서 보내고, 결과는 Deliver 로 메시지 스레드에 넘긴다.
#pragma once

#include "../Transport.h"
#include "Deliver.h"

namespace meon::core
{

class HttpTransport : public RestTransport
{
public:
    /** restBase: "https://api.meon.co.kr/v1" (HttpRequest.path 를 뒤에 붙인다) */
    explicit HttpTransport (juce::String restBase, Deliver deliver = messageThreadDeliver());
    ~HttpTransport() override;

    void send (const HttpRequest&, std::function<void (HttpResponse)> done) override;

    /** 연결과 응답을 기다리는 최대 시간 */
    int timeoutMs = 15000;

    /** 작업 스레드에서 바로 보낸다 (테스트·내부용) */
    static HttpResponse perform (const juce::String& restBase, const HttpRequest&, int timeoutMs);

private:
    const juce::String base;
    Deliver deliver;
    std::shared_ptr<std::atomic<bool>> alive;
    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withThreadName ("MEON http").withNumberOfThreads (4) };

    JUCE_DECLARE_NON_COPYABLE (HttpTransport)
};

} // namespace meon::core
