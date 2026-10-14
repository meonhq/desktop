// MEON ui - 외부 링크 한곳 모음. 약관·개인정보 주소는 임시 (사용자 결정 Q7)
#pragma once

#include <JuceHeader.h>

namespace meon::ui::links
{
    inline const char* terms()   { return "https://meon.co.kr/terms"; }
    inline const char* privacy() { return "https://meon.co.kr/privacy"; }
    /** 서버 설정(config.discord_url)이 비어 있을 때 쓰는 디스코드 초대 주소 */
    inline const char* discordFallback() { return "https://discord.gg/vtGQpnc2HQ"; }

    /** 사람이 내려받는 설치 파일 (릴리스의 meon.dmg / meon-setup.exe) */
    inline const char* download()
    {
       #if JUCE_WINDOWS
        return "https://github.com/meonhq/desktop/releases/download/windows-latest/meon-setup.exe";
       #else
        return "https://github.com/meonhq/desktop/releases/download/mac-latest/meon.dmg";
       #endif
    }

    inline void open (const juce::String& url) { juce::URL (url).launchInDefaultBrowser(); }
}
