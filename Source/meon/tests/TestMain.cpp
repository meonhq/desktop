// MEON 단위 테스트 실행기. 등록된 juce::UnitTest 를 모두 돌리고, 하나라도 실패하면 1 로 끝난다.
//   MEON_Tests            모두 실행
//   MEON_Tests <분류>     그 분류만 실행 (예: EarGuard)
#include <JuceHeader.h>

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;   // MessageManager 가 필요한 테스트(타이머·비동기 콜백)용

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);

    if (argc > 1)
        runner.runTestsInCategory (juce::String (argv[1]));
    else
        runner.runAllTests();

    int failures = 0, passes = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        const auto* r = runner.getResult (i);
        failures += r->failures;
        passes += r->passes;
    }

    std::cout << "MEON_Tests: " << passes << " passed, " << failures << " failed" << std::endl;
    return failures > 0 ? 1 : 0;
}
