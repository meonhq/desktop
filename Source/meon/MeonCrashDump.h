// MEON - Windows 독립 앱이 죽으면 미니덤프를 %APPDATA%\MEON\crashes\ 에 스스로 남긴다.
// 레지스트리(WER LocalDumps)나 관리자 권한 없이 앱 안에서 한다. 죽은 이유(예외 코드·주소)는 logs\app.log 에도 한 줄 남긴다.
// DAW 안의 플러그인에서는 켜지 않는다 (호스트 프로그램의 크래시까지 가로채면 안 된다).
// macOS 는 시스템이 ~/Library/Logs/DiagnosticReports 에 크래시 보고서를 남기므로 따로 하지 않는다.
#pragma once

#include <JuceHeader.h>

namespace meon::crashdump
{
    /** 프로세스가 Meon.exe 이면 처리기를 건다 (여러 번 불러도 한 번). 걸었으면 true. Windows 밖에서는 아무것도 안 한다 */
    bool installForStandalone();

    /** 처리기가 걸려 있는지 */
    bool isInstalled();

    /** 덤프를 두는 폴더 (%APPDATA%\MEON\crashes) */
    juce::File getFolder();

    /** 오래된 덤프를 지워 최근 keep 개만 남기고, 남은 덤프 수와 가장 최근 파일을 진단 로그에 적는다 */
    void tidyAndLog (int keep = 10);
}
