// MEON core - 앱 진단 로그 (logs/app.log, 플러그인은 logs/plugin.log).
// 시작·종료, 로그인 단계, 토큰 갱신·세션 끊김의 이유, 실시간 연결, 저장 실패처럼 가끔 생기는 일을 한 줄씩 남긴다.
// 앱이 죽어도 직전 줄까지 남도록 줄마다 바로 파일에 쓴다 (앱 안에 모아 두지 않는다). 토큰·비밀번호는 쓰지 않는다.
// 열지 않았으면(단위 테스트) 아무것도 쓰지 않는다.
#pragma once

#include <JuceHeader.h>

/** 로그 문구의 한국어 리터럴 (소스는 UTF-8. juce::String ("…") 은 ASCII 로 읽는다) */
#define LOGTXT(s) juce::String (juce::CharPointer_UTF8 (s))

namespace meon::core
{

class AppLog
{
public:
    /** 이 프로세스에서 처음 부를 때 연다. 그 뒤로는 횟수만 센다 (DAW 안의 플러그인 인스턴스 여러 개) */
    static void open (const juce::File& file);
    /** open 한 횟수만큼 부르면 "정상 종료" 를 쓰고 닫는다 */
    static void close();

    static void write (const juce::String& area, const juce::String& message);
    static bool isOpen();
    static juce::File getFile();

    /** 지난 실행이 종료 기록 없이 끝났는지 (open 때 판단: 앱이 죽었거나 강제로 꺼졌다) */
    static bool previousRunEndedAbnormally();

    /** 크래시 처리기용: 잠금·메모리 할당 없이 이미 만든 UTF-8 줄을 그대로 붙인다 */
    static void writeRawUnsafe (const char* utf8, int numBytes);

    /** 파일이 이보다 크면 열 때 이름 뒤에 .1 을 붙여 한 번 옮겨 두고 새로 쓴다 */
    static constexpr juce::int64 maxBytes = 2 * 1024 * 1024;
};

} // namespace meon::core
