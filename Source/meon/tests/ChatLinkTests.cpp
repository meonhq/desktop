// 채팅 링크 찾기 (core::findLinks, 리메이크 전 규칙)와 브라우저 주소 (core::browserUrl).
#include <JuceHeader.h>
#include "../core/util/ChatLinks.h"

namespace meon::core
{

namespace
{
    juce::String u8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

    /** 찾은 링크 글들 */
    juce::StringArray linkTexts (const juce::String& text)
    {
        juce::StringArray out;
        for (auto r : findLinks (text))
            out.add (text.substring (r.getStart(), r.getEnd()));
        return out;
    }
}

class ChatLinkTests : public juce::UnitTest
{
public:
    ChatLinkTests() : juce::UnitTest ("Chat links", "UI") {}

    void expectLinks (const juce::String& text, const juce::StringArray& expected)
    {
        const auto got = linkTexts (text);
        expect (got == expected, "\"" + text + "\" -> [" + got.joinIntoString (" | ") + "], expected [" + expected.joinIntoString (" | ") + "]");
    }

    void runTest() override
    {
        beginTest ("link inside a normal sentence");
        {
            expectLinks (u8 ("이거 봐요 https://example.com/path?q=1 좋아요"), { "https://example.com/path?q=1" });
            expectLinks (u8 ("악보는 http://a.b/c 에 있어요"), { "http://a.b/c" });
            expectLinks (u8 ("www.example.com 들어가 보세요"), { "www.example.com" });
            expectLinks (u8 ("HTTPS://EXAMPLE.COM 대문자"), { "HTTPS://EXAMPLE.COM" });
            expectLinks (u8 ("링크 없음. http:// 만 있음, www.abc 도 아님"), {});
            // 위치: 글자 단위 (끝 미포함)
            const auto r = findLinks (u8 ("가 https://a.com 나"));
            expect (r.size() == 1 && r[0] == juce::Range<int> (2, 15));
        }

        beginTest ("trailing period and comma are not part of the link");
        {
            expectLinks (u8 ("여기 보세요 https://a.com."), { "https://a.com" });
            expectLinks (u8 ("https://a.com, https://b.com/x. 끝"), { "https://a.com", "https://b.com/x" });
            expectLinks (u8 ("정말? https://a.com/?! 그래요"), { "https://a.com/" });
            expectLinks (u8 ("말줄임 https://a.com/b\xe2\x80\xa6"), { "https://a.com/b" });
            expectLinks (u8 ("\"https://a.com\" 따옴표"), { "https://a.com" });
        }

        beginTest ("links in parentheses");
        {
            expectLinks (u8 ("(https://a.com)"), { "https://a.com" });
            expectLinks (u8 ("설명(https://a.com/x) 참고"), { "https://a.com/x" });
            // 링크 안의 짝 맞는 괄호는 남긴다
            expectLinks (u8 ("https://en.wikipedia.org/wiki/Foo_(bar) 문서"), { "https://en.wikipedia.org/wiki/Foo_(bar)" });
            expectLinks (u8 ("(https://en.wikipedia.org/wiki/Foo_(bar))"), { "https://en.wikipedia.org/wiki/Foo_(bar)" });
            expectLinks (u8 ("(www.a.com)"), { "www.a.com" });
        }

        beginTest ("link right after Korean text");
        {
            expectLinks (u8 ("여기https://a.com 봐요"), { "https://a.com" });
            expectLinks (u8 ("주소는:http://a.com"), { "http://a.com" });
            expectLinks (u8 ("여기www.a.com 봐요"), { "www.a.com" });
            // www. 앞이 영문·숫자면 단어 속 글자로 보고 링크로 치지 않는다
            expectLinks (u8 ("awww.b.com 1www.c.com"), {});
            // 리메이크 전 규칙: 링크는 공백까지 이어지므로 바로 뒤에 붙은 한글도 링크에 들어간다
            expectLinks (u8 ("https://a.com에서 봐요"), { u8 ("https://a.com에서") });
        }

        beginTest ("several links, emoji ends a link");
        {
            expectLinks (u8 ("a https://a.com b http://b.org/x c www.c.net d"), { "https://a.com", "http://b.org/x", "www.c.net" });
            expectLinks (u8 ("https://a.com https://a.com"), { "https://a.com", "https://a.com" });
            expectLinks (u8 ("https://a.com\xf0\x9f\x98\x80 웃음"), { "https://a.com" });
            expectLinks (u8 ("줄바꿈\nhttps://a.com\nwww.b.com"), { "https://a.com", "www.b.com" });
            // 겹치지 않고 앞에서부터
            const auto r = findLinks (u8 ("https://a.com www.b.com"));
            expect (r.size() == 2 && r[0].getEnd() <= r[1].getStart());
        }

        beginTest ("browser url: https:// for www., percent-encode non-ASCII");
        {
            expectEquals (browserUrl ("www.a.com"), juce::String ("https://www.a.com"));
            expectEquals (browserUrl ("WWW.A.COM/x"), juce::String ("https://WWW.A.COM/x"));
            expectEquals (browserUrl ("http://a.com/b?c=1&d=2#e"), juce::String ("http://a.com/b?c=1&d=2#e"));
            expectEquals (browserUrl ("https://a.com/b"), juce::String ("https://a.com/b"));
            expectEquals (browserUrl (u8 ("https://a.com/한")), juce::String ("https://a.com/%ED%95%9C"));
            expectEquals (browserUrl ("https://a.com/a b"), juce::String ("https://a.com/a%20b"));
        }
    }
};

static ChatLinkTests chatLinkTests;

} // namespace meon::core
