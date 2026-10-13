// MEON ui - 온보딩 (디자인 L3): 로그인 · 회원가입 · 이메일 인증 · 닉네임 · 파트 · 디스코드 · 완료.
// 단계는 core::AuthController 가 정한다. 화면은 그 단계를 그리고 입력을 넘길 뿐이다.
// 배경 #FCFCFC, 가운데 폭 360 열: 로고 34 · 간격 18 · 카드(여백 18, 블록 사이 18).
#pragma once

#include "../widgets/Button.h"
#include "../widgets/Inputs.h"
#include "../widgets/Display.h"
#include "../../core/MeonCore.h"

namespace meon::ui
{

class OnboardingScreen : public juce::Component,
                         private core::AuthController::Listener,
                         private juce::Timer
{
public:
    explicit OnboardingScreen (core::MeonCore&);
    ~OnboardingScreen() override;

    /** 개발용: 특정 단계를 바로 보여 준다 (스냅숏) */
    void showStepForPreview (core::AuthController::Step);

    void paint (juce::Graphics&) override;
    void resized() override;

    struct StepView;

private:
    core::MeonCore& core;
    std::unique_ptr<StepView> view;
    core::AuthController::Step shown = core::AuthController::Step::loading;
    bool previewOnly = false;
    juce::Rectangle<int> cardBounds, logoBounds;
    juce::Viewport viewport;
    struct Canvas : juce::Component
    {
        std::function<void (juce::Graphics&)> painter;
        void paint (juce::Graphics& g) override { if (painter) painter (g); }
    } content;
    std::unique_ptr<juce::Component> progress;

    void authStepChanged (core::AuthController::Step) override;
    void show (core::AuthController::Step);
    void timerCallback() override;
    void layout();
    void paintContent (juce::Graphics&);
    friend struct StepView;
};

} // namespace meon::ui
