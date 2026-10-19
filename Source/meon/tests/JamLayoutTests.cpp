// 합주 화면 배치 규칙 (합주 최종 디자인): 오른쪽 패널 너비 범위·저장 (6)
#include <JuceHeader.h>
#include "../core/model/PanelWidth.h"

namespace meon::core
{

class JamLayoutTests : public juce::UnitTest
{
public:
    JamLayoutTests() : juce::UnitTest ("Jam layout", "UI") {}

    void runTest() override
    {
        beginTest ("panel width: app 320 ~ min(50%, W - 54 - 440), plugin 240 ~ same");
        {
            expectEquals (panel::defaultWidth (false), 320);
            expectEquals (panel::defaultWidth (true), 240);
            expectEquals (panel::maxWidth (false, 1280), 640);     // min(640, 786)
            expectEquals (panel::maxWidth (true, 900), 406);       // min(450, 406)
            expectEquals (panel::maxWidth (false, 1024), 512);     // 앱 최소 창
            expectEquals (panel::maxWidth (false, 1512), 756);     // MacBook Pro 14
            expectEquals (panel::maxWidth (true, 700), 240);       // 아주 작은 창에서도 기본보다 작아지지 않는다
            // 최소·최대에서 멈춘다
            expectEquals (panel::clampWidth (false, 1280, 100), 320);
            expectEquals (panel::clampWidth (false, 1280, 5000), 640);
            expectEquals (panel::clampWidth (false, 1280, 500), 500);
            expectEquals (panel::clampWidth (true, 900, 100), 240);
            expectEquals (panel::clampWidth (true, 900, 5000), 406);
            // 멤버 쪽은 440 아래로 내려가지 않는다 (창 − 54 − 패널)
            for (int w : { 1024, 1280, 1512, 1920 })
                expect (w - 54 - panel::maxWidth (false, w) >= 440, juce::String (w));
            for (int w : { 900, 1000, 1280 })
                expect (w - 54 - panel::maxWidth (true, w) >= 440, juce::String (w));
        }

        beginTest ("panel width: shown value follows the window, saved value stays");
        {
            expectEquals (panel::shownWidth (false, 1280, 0), 320);     // 저장 없음 → 기본
            expectEquals (panel::shownWidth (true, 900, 0), 240);
            expectEquals (panel::shownWidth (false, 1280, 600), 600);
            expectEquals (panel::shownWidth (false, 1024, 600), 512);   // 작은 창: 최대로 보인다
            expectEquals (panel::shownWidth (false, 1280, 600), 600);   // 다시 키우면 저장값 그대로
        }

        beginTest ("panel width: persisted across restart, app and plugin separately; double-click default");
        {
            auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("meon-panel-width-test.settings");
            file.deleteFile();
            juce::PropertiesFile::Options o;
            {
                juce::PropertiesFile p (file, o);
                expectEquals (panel::load (p, false), 0);
                expectEquals (panel::load (p, true), 0);
                panel::save (p, false, 512);
                panel::save (p, true, 300);
                expect (p.save());
            }
            {
                juce::PropertiesFile p (file, o);   // 다시 켬
                expectEquals (panel::load (p, false), 512);
                expectEquals (panel::load (p, true), 300);
                panel::save (p, true, panel::defaultWidth (true));   // 플러그인에서 더블클릭 → 기본
                expect (p.save());
            }
            {
                juce::PropertiesFile p (file, o);
                expectEquals (panel::load (p, false), 512);   // 앱 값은 그대로
                expectEquals (panel::load (p, true), 240);
            }
            file.deleteFile();
        }

        beginTest ("wider panel: my bubble max 75% of the list inner width, 220..420");
        {
            expectEquals (panel::myBubbleMax (284), 220);   // 기본 패널 (320 − 36): 75% = 213 → 220
            expectEquals (panel::myBubbleMax (204), 204);   // 플러그인 240: 안쪽보다 넓지 않게
            expectEquals (panel::myBubbleMax (400), 300);
            expectEquals (panel::myBubbleMax (604), 420);   // 640 패널: 453 → 420
        }

        beginTest ("narrower members: slider 150 -> 60 first, then meter 150 -> 84");
        {
            auto s = panel::shrink (300);
            expect (s.slider == 150 && s.meter == 150);
            s = panel::shrink (500);
            expect (s.slider == 150 && s.meter == 150, "멤버 행 볼륨 최대 150");
            s = panel::shrink (250);
            expect (s.slider == 100 && s.meter == 150, "볼륨이 먼저 줄어든다");
            s = panel::shrink (210);
            expect (s.slider == 60 && s.meter == 150);
            s = panel::shrink (180);
            expect (s.slider == 60 && s.meter == 120, "볼륨이 60 이면 미터가 줄어든다");
            s = panel::shrink (144);
            expect (s.slider == 60 && s.meter == 84, "미터 최소 84");
            s = panel::shrink (100);
            expect (s.slider == 60 && s.meter == 40, "60 + 84 도 안 들어가면 겹치지 않게 미터가 더 줄어든다");
            s = panel::shrink (40);
            expect (s.slider == 40 && s.meter == 0);
            // 내 오디오·모니터: 슬라이더는 남는 만큼
            s = panel::shrink (400, std::numeric_limits<int>::max());
            expect (s.slider == 250 && s.meter == 150);
            s = panel::shrink (166, std::numeric_limits<int>::max());
            expect (s.slider == 60 && s.meter == 106);
        }
    }
};

static JamLayoutTests jamLayoutTests;

} // namespace meon::core
