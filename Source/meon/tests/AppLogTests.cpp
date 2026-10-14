// AppLog: 줄마다 바로 파일에 쓰는지, 정상 종료 표시, 지난 실행이 죽었는지 알아채는지
#include "../core/log/AppLog.h"

namespace meon::core
{

class AppLogTests : public juce::UnitTest
{
public:
    AppLogTests() : juce::UnitTest ("AppLog", "Log") {}

    void runTest() override
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("meon-applog", "");
        dir.createDirectory();
        const auto file = dir.getChildFile ("app.log");
        const auto marker = dir.getChildFile ("app.running");

        beginTest ("lines reach the file before close; clean exit removes the marker");
        {
            AppLog::open (file);
            expect (AppLog::isOpen());
            expect (! AppLog::previousRunEndedAbnormally());
            AppLog::write ("test", LOGTXT ("한국어 한 줄"));
            expect (file.loadFileAsString().contains (LOGTXT ("[test] 한국어 한 줄")), "written before close");
            expect (marker.existsAsFile());
            AppLog::close();
            expect (! AppLog::isOpen());
            expect (! marker.existsAsFile());
            expect (file.loadFileAsString().contains (LOGTXT ("==== 정상 종료 ====")));
            AppLog::write ("test", "after close");   // 닫힌 뒤에는 쓰지 않는다
            expect (! file.loadFileAsString().contains ("after close"));
        }

        beginTest ("marker left behind means the last run ended abnormally");
        {
            marker.replaceWithText ("PID 1");
            AppLog::open (file);
            expect (AppLog::previousRunEndedAbnormally());
            expect (file.loadFileAsString().contains (LOGTXT ("종료 기록 없이")));
            AppLog::close();
        }

        beginTest ("nested open/close (plugin instances)");
        {
            AppLog::open (file);
            AppLog::open (file);
            AppLog::close();
            expect (AppLog::isOpen());
            AppLog::close();
            expect (! AppLog::isOpen());
        }

        beginTest ("raw write for the crash handler");
        {
            AppLog::open (file);
            const char raw[] = "raw crash line\n";
            AppLog::writeRawUnsafe (raw, (int) sizeof (raw) - 1);
            expect (file.loadFileAsString().contains ("raw crash line"));
            AppLog::close();
        }

        dir.deleteRecursively();
    }
};

static AppLogTests appLogTests;

} // namespace meon::core
