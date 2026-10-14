// MEON core - 로그인 refresh 토큰 보관 (Q21). 설정 파일에는 넣지 않는다.
//   macOS: 키체인 (서비스 com.meon.MEON), Windows: 자격 증명 관리자 (MEON/refresh_token), 그 밖: 사용자 데이터 폴더의 파일(권한 600).
// 독립 앱과 플러그인이 같은 항목을 같이 쓴다.
#pragma once

#include <JuceHeader.h>

namespace meon::core
{

class TokenStore
{
public:
    virtual ~TokenStore() = default;

    virtual juce::String load() = 0;                 // 없으면 빈 문자열
    virtual bool save (const juce::String& token) = 0;
    virtual void clear() = 0;
    /** 마지막 load·save 가 실패한 이유 (진단 로그용). 없으면 빈 문자열 */
    virtual juce::String lastError() const          { return error; }

    /** 이 OS 의 기본 저장소. name 은 항목 이름 (가짜 서버 기간에는 "fake-refresh-token" 처럼 진짜와 구분) */
    static std::unique_ptr<TokenStore> createDefault (const juce::String& name);

protected:
    juce::String error;
};

/** 테스트용 */
class MemoryTokenStore : public TokenStore
{
public:
    juce::String load() override                    { return value; }
    bool save (const juce::String& t) override      { value = t; return true; }
    void clear() override                           { value = {}; }
    juce::String value;
};

} // namespace meon::core
