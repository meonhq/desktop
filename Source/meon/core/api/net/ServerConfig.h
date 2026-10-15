// MEON core - 어느 서버에 붙을지 (R11).
//   실제 빌드 번호가 있는 빌드(CI 릴리스): 운영 서버 고정 (https://api.meon.co.kr)
//   빌드 0(로컬·PR 빌드): 기본은 가짜 서버 (더블클릭만으로 시험, 플러그인도). 개발 옵션 --server=local(docker compose,
//   http://localhost:8080)|prod|fake|<주소>
#pragma once

#include <JuceHeader.h>

namespace meon::core
{

struct ServerConfig
{
    enum class Kind { prod, local, custom, fake };

    Kind kind = Kind::prod;
    juce::String restBase;          // "https://api.meon.co.kr/v1" (끝에 / 없음)
    juce::String wsUrl;             // "wss://api.meon.co.kr/v1/ws"
    juce::String aooHost;           // 방에 들어가기 전 RTT 를 잴 AOO 서버 (방 안에서는 Connection 의 값)
    int aooPort = 10999;
    juce::String tokenItem;         // 로그인 토큰 저장 항목 이름. 서버마다 따로 (운영 토큰과 로컬 토큰이 섞이지 않게)

    bool isFake() const { return kind == Kind::fake; }
    juce::String describe() const;  // 로그용: "prod https://api.meon.co.kr/v1"

    static ServerConfig prod();
    static ServerConfig local();
    static ServerConfig fake();
    /** "http://host:port" 또는 "https://host" (뒤의 /v1 은 있어도 없어도 된다). 알아볼 수 없으면 restBase 가 비어 있다 */
    static ServerConfig fromUrl (const juce::String& base);

    /** 빌드 번호와 개발 옵션으로 고른다. build > 0 이면 개발 옵션과 상관없이 운영 */
    static ServerConfig choose (int build, const juce::StringPairArray& devOptions);
};

} // namespace meon::core
