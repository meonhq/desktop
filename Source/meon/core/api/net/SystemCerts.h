// MEON core - TLS 인증서 검증에 쓸 루트 인증서 (R11, WebSocket 전용).
// mbedTLS 는 Windows 에서만 시스템 인증서를 스스로 읽는다. macOS 는 키체인의 시스템 루트 인증서를 PEM 으로 바꿔 넘긴다.
#pragma once

#include <JuceHeader.h>

namespace meon::core::certs
{

/** IXWebSocket SocketTLSOptions::caFile 에 넣을 값.
    Windows: "SYSTEM" (IXWebSocket 이 시스템 저장소를 읽는다)
    macOS:   키체인 시스템 루트 인증서들의 PEM 묶음 (처음 한 번 만들고 기억한다)
    Linux:   /etc/ssl/certs/ca-certificates.crt 등 있는 파일 경로
    못 구하면 빈 문자열 (wss 연결이 실패한다) */
std::string caFileForTls();

/** macOS: 키체인에서 읽은 루트 인증서 수 (진단 로그·테스트용). 다른 OS 는 -1 */
int systemRootCount();

} // namespace meon::core::certs
