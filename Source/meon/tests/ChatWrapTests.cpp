// 채팅 말풍선 줄바꿈 (core::chatwrap): 띄어쓰기 자리에서 바꾸고, 한 줄보다 긴 단어만 글자 단위로 끊는다.
// 시험에서는 글자 하나 = 폭 1 로 잰다 (줄 폭 11 → 한 줄에 10 글자까지).
#include <JuceHeader.h>
#include "../core/util/ChatWrap.h"
#include "../core/util/ChatLinks.h"

namespace meon::core
{

namespace
{
    juce::String u8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }
    float perChar (const juce::String& s) { return (float) s.length(); }
    juce::String wrapped (const juce::String& s, float w = 11.0f, const std::vector<int>& cuts = {})
    {
        return chatwrap::wrap (s, perChar, w, cuts).text;
    }
}

class ChatWrapTests : public juce::UnitTest
{
public:
    ChatWrapTests() : juce::UnitTest ("Chat wrap", "UI") {}

    void expectWrap (const juce::String& in, const juce::String& expected, float w = 11.0f, const std::vector<int>& cuts = {})
    {
        const auto got = wrapped (in, w, cuts);
        expect (got == expected, "\"" + in + "\" -> \"" + got.replace ("\n", "|") + "\", expected \"" + expected.replace ("\n", "|") + "\"");
    }

    void runTest() override
    {
        beginTest ("English: breaks at spaces, never inside a word");
        expectWrap ("hello world foo", "hello\nworld foo");
        expectWrap ("short", "short");
        expectWrap ("aaaa bbbb cccc dddd", "aaaa bbbb\ncccc dddd");

        beginTest ("Korean: breaks between words (eojeol), not between syllables");
        expectWrap (u8 ("괜찮아요 첫 곡은 템포 120으로 갈게요"), u8 ("괜찮아요 첫 곡은\n템포 120으로\n갈게요"));
        expectWrap (u8 ("키 하나 올려도 될까요?"), u8 ("키 하나 올려도\n될까요?"));

        beginTest ("A word longer than a line is cut by characters (filling the current line)");
        expectWrap ("aaaaaaaaaaaaaaaaaaaaaaaaa", "aaaaaaaaaa\naaaaaaaaaa\naaaaa");
        expectWrap (u8 ("띄어쓰기없이아주긴문장을그대로보내면글자단위로끊어요"),
                    u8 ("띄어쓰기없이아주긴문\n장을그대로보내면글자\n단위로끊어요"));
        expectWrap ("ab cdefghijklmnopq", "ab cdefghi\njklmnopq");

        beginTest ("Long link: starts on the current line and is cut by characters; link range maps through");
        {
            const auto text = u8 ("자료 https://example.com/abcdefghij 확인");
            const auto links = findLinks (text);
            expect (links.size() == 1);
            if (links.size() != 1) return;
            std::vector<int> cuts { links[0].getStart(), links[0].getEnd() };
            const auto w = chatwrap::wrap (text, perChar, 11.0f, cuts);
            expectEquals (w.text, u8 ("자료 https:/\n/example.c\nom/abcdefg\nhij 확인"));
            // 링크 범위를 옮기면 줄바꿈(\n)을 뺀 글이 원래 링크와 같다
            const auto r = juce::Range<int> (w.map[(size_t) links[0].getStart()], w.map[(size_t) links[0].getEnd()]);
            expectEquals (w.text.substring (r.getStart(), r.getEnd()).removeCharacters ("\n"), juce::String ("https://example.com/abcdefghij"));
        }

        beginTest ("A link right after text without a space may start on the next line (link boundary)");
        {
            const auto text = u8 ("자료:https://a.io/x");
            const auto links = findLinks (text);
            expect (links.size() == 1);
            if (links.size() == 1)
                expectWrap (text, u8 ("자료:\nhttps://a.io/x"), 16.0f, { links[0].getStart(), links[0].getEnd() });
        }

        beginTest ("Spaces at a break become one line break; user line breaks stay");
        {
            const auto w = chatwrap::wrap ("aaaa    bbbbbbbb", perChar, 11.0f);
            expectEquals (w.text, juce::String ("aaaa\nbbbbbbbb"));
            for (int k = 4; k < 8; ++k)
                expectEquals (w.map[(size_t) k], 4);   // 공백은 모두 그 줄바꿈 자리
            expectEquals (w.map[8], 5);
            expectEquals (w.map.back(), w.text.length());
        }
        expectWrap ("a\nb c", "a\nb c");
        expectWrap ("aaaa\n\nbbbb", "aaaa\n\nbbbb");
        expectWrap ("aaaaaaaa\n  bbbbbbbbbb", "aaaaaaaa\nbbbbbbbbbb");   // 줄 앞 공백 때문에 넘치면 그 공백은 버린다

        beginTest ("Emoji clusters are not split at a line end");
        expectWrap (u8 ("aaaaaaaaa👍🏽"), u8 ("aaaaaaaaa\n👍🏽"));

        beginTest ("Every line fits and words that fit are never split (many widths)");
        {
            const auto text = u8 ("오늘 셋리스트는 네 곡이에요. 첫 곡은 템포 120 으로 가고 bridge 는 한 번 더 갈게요 okay? thanks everyone");
            for (int w = 6; w <= 40; ++w)
            {
                const auto out = wrapped (text, (float) w);
                for (auto& line : juce::StringArray::fromLines (out))
                    expect (line.length() <= w - 1, "width " + juce::String (w) + ": \"" + line + "\"");
                for (auto& word : juce::StringArray::fromTokens (text, " ", ""))
                    if (word.length() <= w - 1)
                        expect (out.contains (word), "width " + juce::String (w) + ": word split \"" + word + "\"");
                if (w - 1 >= 8)   // 가장 긴 단어(everyone)가 한 줄에 들어가면 바꾼 자리는 모두 공백 한 칸이었다
                    expectEquals (out.replace ("\n", " "), text);
            }
        }
    }
};

static ChatWrapTests chatWrapTests;

} // namespace meon::core
