// MEON ui - 외부 링크 한곳 모음. 약관·개인정보 주소는 임시 (사용자 결정 Q7)
#pragma once

#include <JuceHeader.h>

namespace meon::ui::links
{
    inline const char* terms()   { return "https://meon.co.kr/terms"; }
    inline const char* privacy() { return "https://meon.co.kr/privacy"; }
    /** 서버 설정(config.discord_url)이 비어 있을 때 쓰는 디스코드 초대 주소 */
    inline const char* discordFallback() { return "https://discord.gg/vtGQpnc2HQ"; }

    /** 사람이 새 버전을 내려받는 웹 페이지 (배포 설계 3.8: OS 를 보고 dl.meon.co.kr 의 설치 파일로 잇는다).
        강제 업데이트·업데이트 실패 화면의 "직접 내려받기" 와 업데이터가 없는 빌드의 "새 버전 내려받기" */
    inline const char* download() { return "https://meon.co.kr/download"; }

    inline void open (const juce::String& url) { juce::URL (url).launchInDefaultBrowser(); }
}
