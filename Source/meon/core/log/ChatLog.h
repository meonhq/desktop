// MEON core - 합주방 채팅 기록 (사람이 읽는 UTF-8 .txt). 로그 폴더/chat/<세션 로그와 같은 이름>.txt
//   방에 들어갈 때 begin (파일은 아직 만들지 않음) → 첫 메시지 때 머리 줄과 함께 파일을 만든다 → 메시지마다 바로 덧붙인다
//   → 방을 나갈 때 끝 줄. 메시지가 하나도 없으면 파일이 생기지 않는다.
//   한 줄: "[21:03:15] 민준: 안녕하세요", 시스템 줄: "[21:04:01] * 서연님이 입장했어요".
//   여러 줄 메시지는 둘째 줄부터 공백 2칸 들여쓰기. 머리·끝 줄은 "# " 로 시작한다.
//   시각은 벽시계 (테스트는 clock 으로 넣는다). 쓰기에 실패해도 앱은 그대로 가고 AppLog 에 한 줄 남긴다.
#pragma once

#include <JuceHeader.h>

namespace meon::core
{

class ChatLog
{
public:
    enum class Kind { mine, other, system };
    using Clock = std::function<juce::Time()>;

    /** folder: 채팅 기록 폴더 (로그 폴더/chat). clock 이 비어 있으면 juce::Time::getCurrentTime */
    explicit ChatLog (juce::File chatFolder, Clock clock = {});

    /** 방에 들어갈 때. stamp: 같은 입장의 세션 로그 파일 이름 (확장자 없이). code 는 비공개 방만 (없으면 괄호를 쓰지 않는다) */
    void begin (const juce::String& stamp, const juce::String& roomTitle, const juce::String& roomCode);
    /** 메시지 한 줄. name 은 시스템 줄이면 쓰지 않는다 */
    void add (Kind kind, const juce::String& name, const juce::String& text);
    /** 방을 나갈 때 (파일이 있으면 끝 줄을 쓴다) */
    void end();

    bool isActive() const { return active; }
    /** 지금(또는 마지막) 기록 파일. 첫 메시지 전에는 비어 있다 */
    juce::File getFile() const { return file; }
    juce::File getFolder() const { return folder; }

    /** 메시지 한 줄 (끝 줄바꿈 포함). 여러 줄이면 둘째 줄부터 들여 쓴다 */
    static juce::String formatLine (juce::Time t, Kind kind, const juce::String& name, const juce::String& text);

private:
    juce::File folder, file;
    Clock clock;
    juce::String stamp, title, code;
    juce::Time startedAt;
    bool active = false, failed = false;

    juce::Time now() const { return clock ? clock() : juce::Time::getCurrentTime(); }
    bool openFile();
    void append (const juce::String& s);
};

} // namespace meon::core
