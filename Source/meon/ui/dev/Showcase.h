// MEON dev - 소개 페이지 이미지·영상 캡처 (--showcase=jam|rooms|friends, 빌드 0 전용). 앱 동작은 바꾸지 않는다.
// 가짜 서버(showcase.json, 가상 인물)와 ShowcaseEngine 으로 장면을 꾸민 뒤, 창을 화면에서 떼고
// 화면 전체를 배율(기본 2배)로 직접 그려 PNG 로 저장한다 (늘리기 아님). 영상은 프레임마다 시계를 1/30초씩 넘긴다.
//   --capture=still|video  (기본 still)   --capture-out=폴더   --capture-at=ms (still 의 시각, 기본 0)
//   --capture-frames=360   --capture-scale=2   --capture-wait=450 (프레임 사이 기다림: 0.4초마다 오는 핑 갱신)
//   --capture-size=1512x982 (창에서 뗀 뒤 화면 크기, 논리 픽셀)   --capture-name=파일 이름
// 저장: still → <폴더>/<장면>.png, video → <폴더>/frames/0000.png…, 둘 다 <폴더>/<장면>.layout.json (크롭 위치)
#pragma once

#include <JuceHeader.h>
#include "../../core/MeonCore.h"

namespace meon::ui
{
class AppRoot;

namespace dev
{

class Showcase : private juce::Timer
{
public:
    Showcase (core::MeonCore&, AppRoot&, juce::Component& renderTarget, const juce::StringPairArray& devOptions);
    ~Showcase() override;

    void start();
    /** 끝나면 (0 = 성공) */
    std::function<void (int)> onDone;

    /** 합주 장면의 고정 인물 (나 다음 순서). 소개 페이지 문구와 맞춘다 */
    struct CastMember { const char* id; const char* nickname; const char* part; float gainDb; int pan; int pingMs; double bumpFrom, bumpTo; int bumpMs; };
    static const std::vector<CastMember>& cast();

private:
    core::MeonCore& core;
    AppRoot& root;
    juce::Component& target;
    juce::String scene, mode;
    juce::File outDir;
    double stillAtMs = 0.0;
    int frames = 360, waitMs = 450;
    juce::String captureName;   // --capture-name= 저장 파일 이름 (기본: 장면, 플러그인이면 -plugin)
    juce::String baseName() const;
    juce::Point<int> size;   // --capture-size=1512x982: 창에서 뗀 뒤 이 크기로 (화면이 작은 러너에서도 같은 크기)
    float scale = 2.0f;

    // 단계: 앞 단계가 끝나면 delay 뒤에 실행. until 이 있으면 참이 될 때까지 (최대 timeout) 기다렸다가 다음으로
    struct Step { int delayMs; std::function<void()> run; std::function<bool()> until; int timeoutMs; juce::String what; };
    std::vector<Step> steps;
    size_t next = 0;
    juce::int64 waitStarted = 0;
    bool waiting = false;
    void add (int delayMs, juce::String what, std::function<void()> run, std::function<bool()> until = {}, int timeoutMs = 20000);
    void timerCallback() override;
    void fail (const juce::String& why);

    void buildJam();
    void buildHome (bool friendsScene);
    void buildChat();          // 채팅 줄바꿈 확인 (--showcase=chat --chat-case=ko|en|link|nospace)
    juce::String chatCase;
    void addCapture();

    // 렌더링
    double baseMs = 0.0;
    int frame = 0;
    bool chat4 = false, chat8 = false;
    std::vector<juce::Component::SafePointer<juce::Component>> meters;
    void detachFromDesktop();
    void hideResizeCorner();   // 플러그인 창 크기 조절 손잡이를 캡처에서만 숨긴다
    void collectMeters (juce::Component&);
    static constexpr double fps = 30.0;
    static constexpr int loopFrames = 360;   // 12초 (ShowcaseEngine::loopMs)
    int stillFrame() const;
    void setClock (int frameIndex);
    void stepMeters (int frameIndex);
    void preroll (int untilFrame);
    bool render (const juce::File&);
    void writeLayout();
    void videoFrame();
    void videoEvents (double t);

    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
};

} // namespace dev
} // namespace meon::ui
