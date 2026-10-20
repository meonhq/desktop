// 합주 화면 배치 규칙 (합주 최종 디자인): 오른쪽 패널 너비 범위·저장 (6), 채팅 입력 칸 (7)
#include <JuceHeader.h>
#include "../core/model/PanelWidth.h"
#include "../core/util/ChatInput.h"
#include "../core/jam/ChatChannel.h"

namespace meon::core
{

class JamLayoutTests : public juce::UnitTest
{
public:
    JamLayoutTests() : juce::UnitTest ("Jam layout", "UI") {}

    void runTest() override
    {
        beginTest ("panel width: app 320 ~ min(50%, W - 54 - 566), plugin 240 ~ same");
        {
            expectEquals (panel::defaultWidth (false), 320);
            expectEquals (panel::defaultWidth (true), 240);
            expectEquals (panel::maxWidth (false, 1280), 640);     // min(640, 660)
            expectEquals (panel::maxWidth (true, 900), 280);       // min(450, 280)
            expectEquals (panel::maxWidth (true, 1200), 580);      // 플러그인을 키우면 최대도 커진다: min(600, 580)
            expectEquals (panel::maxWidth (false, 1024), 404);     // 앱 최소 창: min(512, 404)
            expectEquals (panel::maxWidth (false, 1512), 756);     // MacBook Pro 14: min(756, 892)
            expectEquals (panel::maxWidth (true, 700), 240);       // 아주 작은 창에서도 기본보다 작아지지 않는다
            // 최소·최대에서 멈춘다
            expectEquals (panel::clampWidth (false, 1280, 100), 320);
            expectEquals (panel::clampWidth (false, 1280, 5000), 640);
            expectEquals (panel::clampWidth (false, 1280, 500), 500);
            expectEquals (panel::clampWidth (true, 900, 100), 240);
            expectEquals (panel::clampWidth (true, 900, 5000), 280);
            // 멤버 쪽은 566 아래로 내려가지 않는다 (창 − 54 − 패널. 앱 최소 1024, 플러그인 최소 900)
            for (int w : { 1024, 1280, 1512, 1920 })
                expect (w - 54 - panel::maxWidth (false, w) >= 566, juce::String (w));
            for (int w : { 900, 1000, 1280 })
                expect (w - 54 - panel::maxWidth (true, w) >= 566, juce::String (w));
        }

        beginTest ("panel width: shown value follows the window, saved value stays");
        {
            expectEquals (panel::shownWidth (false, 1280, 0), 320);     // 저장 없음 → 기본
            expectEquals (panel::shownWidth (true, 900, 0), 240);
            expectEquals (panel::shownWidth (false, 1280, 600), 600);
            expectEquals (panel::shownWidth (false, 1024, 600), 404);   // 작은 창: 최대로 보인다
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
            expect (s.slider == 60 && s.meter == 84, "60·84 아래로는 줄이지 않는다 (멤버 쪽 566 이상이라 늘 들어간다)");
            // 내 오디오·모니터: 슬라이더는 남는 만큼
            s = panel::shrink (400, std::numeric_limits<int>::max());
            expect (s.slider == 250 && s.meter == 150);
            s = panel::shrink (166, std::numeric_limits<int>::max());
            expect (s.slider == 60 && s.meter == 106);
        }

        auto u8 = [] (const char* t) { return juce::String (juce::CharPointer_UTF8 (t)); };
        auto bytesOf = [&] (int n) { juce::String s; while ((int) s.getNumBytesAsUTF8() + 3 <= n) s << u8 ("가"); while ((int) s.getNumBytesAsUTF8() < n) s << "a"; return s; };

        beginTest ("chat input: trim ends and blank lines, keep inner newlines, CRLF from paste");
        {
            expectEquals (chatinput::prepare (u8 ("  안녕  ")), u8 ("안녕"));
            expectEquals (chatinput::prepare (u8 ("\n\n  첫 줄\n둘째 줄\n\n\n")), u8 ("첫 줄\n둘째 줄"));
            expectEquals (chatinput::prepare (u8 ("위\n\n아래")), u8 ("위\n\n아래"));   // 안쪽 빈 줄은 그대로
            expectEquals (chatinput::prepare (u8 ("\r\n붙여\r\n넣기\r")), u8 ("붙여\n넣기"));
            expectEquals (chatinput::prepare (" \t\n "), juce::String());
            expect (! chatinput::canSend (" \n\n "));
            expect (chatinput::canSend (u8 ("가")));
        }

        beginTest ("chat input: 900 / 1000 byte boundaries (same limit as ChatChannel)");
        {
            expectEquals (chatinput::limit, ChatChannel::maxBytes);
            expect (! chatinput::showCounter (chatinput::bytes (bytesOf (899))));
            expect (chatinput::showCounter (chatinput::bytes (bytesOf (900))));
            expectEquals (chatinput::counterText (962), juce::String ("962 / 1000"));
            expect (! chatinput::overLimit (1000) && chatinput::canSend (bytesOf (1000)));
            expect (chatinput::overLimit (1001) && ! chatinput::canSend (bytesOf (1001)));
            // 앞뒤 공백은 지운 뒤에 센다 (보낼 수 있는지)
            expect (chatinput::canSend (bytesOf (1000) + "   \n"));
            expectEquals (chatinput::bytes (u8 ("한")), 3);
        }

        beginTest ("chat input: height 44 (1 line) .. 84 (3 lines), then scrolls; Enter / Shift+Enter");
        {
            expectEquals (chatinput::boxHeight (1), 44);
            expectEquals (chatinput::boxHeight (2), 64);
            expectEquals (chatinput::boxHeight (3), 84);
            expectEquals (chatinput::boxHeight (7), 84);
            expectEquals (chatinput::visibleLines (0), 1);    // 빈 칸
            expectEquals (chatinput::visibleLines (20), 1);
            expectEquals (chatinput::visibleLines (40), 2);
            expectEquals (chatinput::visibleLines (60), 3);
            expectEquals (chatinput::visibleLines (200), 3);
            expect (chatinput::onEnter (false) == chatinput::Enter::send);
            expect (chatinput::onEnter (true) == chatinput::Enter::newline);
        }

        beginTest ("chat input: Korean IME late commit after Enter is dropped (not sent twice), normal typing is not");
        {
            const auto sent = u8 ("안녕하세요");
            expect (chatinput::isLateImeLeftover (sent, u8 ("요"), 20));       // 확정 글자가 보낸 뒤 늦게 들어옴
            expect (chatinput::isLateImeLeftover (sent, u8 ("세요"), 120));
            expect (! chatinput::isLateImeLeftover (sent, u8 ("요"), 800));     // 시간이 지난 뒤 새로 쓴 글
            expect (! chatinput::isLateImeLeftover (sent, u8 ("네"), 20));      // 보낸 글의 끝이 아니다
            expect (! chatinput::isLateImeLeftover (sent, u8 ("하세요"), 20));  // 3 글자 이상은 새로 쓴 것으로 본다
            expect (! chatinput::isLateImeLeftover (sent, {}, 20));
            expect (! chatinput::isLateImeLeftover ({}, u8 ("요"), 20));
        }
    }
};

static JamLayoutTests jamLayoutTests;

} // namespace meon::core
