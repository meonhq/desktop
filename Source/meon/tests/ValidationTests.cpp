// API 3.1 입력 규칙 경계값 + 홈 필터(5.2) + 예상 지연(2.11)
#include "../core/model/Validation.h"
#include "../core/model/RoomFilter.h"
#include "../core/model/Parts.h"

namespace meon::core
{

class ValidationTests : public juce::UnitTest
{
public:
    ValidationTests() : juce::UnitTest ("Validation", "Model") {}

    static juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }

    void runTest() override
    {
        namespace v = validation;
        using P = v::Problem;

        beginTest ("email");
        expect (v::email ("me@example.com") == P::none);
        expect (v::email ("") == P::empty);
        expect (v::email ("me@example") == P::badFormat);
        expect (v::email ("@example.com") == P::badFormat);
        expect (v::email ("a@b@c.com") == P::badFormat);
        expect (v::email ("me @example.com") == P::badFormat);
        expect (v::email ("me@example.com.") == P::badFormat);
        expect (v::email (juce::String::repeatedString ("a", 243) + "@example.com") == P::tooLong);   // 255자
        expect (v::email (juce::String::repeatedString ("a", 242) + "@example.com") == P::none);      // 254자

        beginTest ("username 4-16 [a-z0-9_]");
        expect (v::username ("abcd") == P::none);
        expect (v::username ("abc") == P::tooShort);
        expect (v::username ("a234567890123456") == P::none);
        expect (v::username ("a2345678901234567") == P::tooLong);
        expect (v::username ("chan_keys") == P::none);
        expect (v::username ("Chan") == P::badCharacters);
        expect (v::username ("chan-keys") == P::badCharacters);
        expect (v::username (u8 ("한글이름")) == P::badCharacters);

        beginTest ("nickname 2-12, trim, emoji");
        expect (v::nickname (u8 ("준호")) == P::none);
        expect (v::nickname (u8 ("준")) == P::tooShort);
        expect (v::nickname (u8 ("  준호  ")) == P::none);
        expect (v::nickname ("   ") == P::empty);
        expect (v::nickname (u8 ("가나다라마바사아자차카타")) == P::none);      // 12
        expect (v::nickname (u8 ("가나다라마바사아자차카타파")) == P::tooLong); // 13
        expect (v::nickname (u8 ("준호\xF0\x9F\x8E\xB8")) == P::emoji);          // 🎸
        expect (v::nickname (u8 ("준호\xE2\x99\xAA")) == P::emoji);              // ♪ U+266A
        expect (v::nickname (u8 ("a\xE2\x80\x8D" "b")) == P::emoji);             // ZWJ
        expect (v::nickname (juce::String ("ab") + juce::String::charToString (7)) == P::emoji);
        // NFD 한글 (ㅈ+ㅜ+ㄴ, ㅎ+ㅗ) 은 2자로 센다
        expect (v::nickname (u8 ("\xE1\x84\x8C\xE1\x85\xAE\xE1\x86\xAB\xE1\x84\x92\xE1\x85\xA9")) == P::none);
        expectEquals (v::length (u8 ("\xE1\x84\x8C\xE1\x85\xAE\xE1\x86\xAB\xE1\x84\x92\xE1\x85\xA9")), 2);
        expectEquals (v::composeHangul (u8 ("\xE1\x84\x8C\xE1\x85\xAE\xE1\x86\xAB")), u8 ("준"));

        beginTest ("password 8-128, ASCII, classes");
        expect (v::password ("abcd123!") == P::none);
        expect (v::password ("abc123!") == P::tooShort);
        expect (v::password ("abcdefgh") == P::missingClasses);
        expect (v::password ("abcd1234") == P::missingClasses);
        expect (v::password ("12345678!") == P::missingClasses);
        expect (v::password ("abcd 123!") == P::badCharacters);
        expect (v::password (u8 ("abcd123!가")) == P::badCharacters);
        expect (v::password ("a1!" + juce::String::repeatedString ("x", 125)) == P::none);   // 128
        expect (v::password ("a1!" + juce::String::repeatedString ("x", 126)) == P::tooLong);
        expect (v::password ("A1~aaaaa") == P::none);

        beginTest ("room title 1-30, description 0-80");
        expect (v::roomTitle (u8 ("새벽 합주")) == P::none);
        expect (v::roomTitle ("  ") == P::empty);
        expect (v::roomTitle (juce::String::repeatedString (u8 ("가"), 30)) == P::none);
        expect (v::roomTitle (juce::String::repeatedString (u8 ("가"), 31)) == P::tooLong);
        expect (v::roomDescription ("") == P::none);
        expect (v::roomDescription (juce::String::repeatedString ("a", 80)) == P::none);
        expect (v::roomDescription (juce::String::repeatedString ("a", 81)) == P::tooLong);

        beginTest ("room code characters");
        expect (v::isRoomCode ("A7K2QX"));
        expect (v::isRoomCode ("a7k2qx"));
        expect (! v::isRoomCode ("A7K2Q"));
        expect (! v::isRoomCode ("A7K2Q0"));
        expect (! v::isRoomCode ("A7K2Q1"));
        expect (! v::isRoomCode ("A7K2QO"));
        expect (! v::isRoomCode ("A7K2QI"));
        expect (v::isRoomCodeChar ('Z') && v::isRoomCodeChar ('2') && ! v::isRoomCodeChar ('a'));

        beginTest ("latency estimate");
        expect (latency::estimate (0.7, 8, 12) == OptInt (14));
        expect (! latency::estimate (0.7, std::nullopt, 12));
        expect (! latency::estimate (0.7, 8, std::nullopt));

        beginTest ("capacity rule (5.5)");
        {
            Room r;
            r.capacity = 4;
            r.memberCount = 3;
            r.hostId = "u_h";
            r.hostPresent = true;
            expect (rooms::hasRoomFor (r, "u_me"));
            r.hostPresent = false;   // 방장 자리 예약
            expect (! rooms::hasRoomFor (r, "u_me"));
            expect (rooms::hasRoomFor (r, "u_h"));
            r.memberCount = 2;
            expect (rooms::hasRoomFor (r, "u_me"));
        }

        beginTest ("home filter (5.2)");
        {
            auto member = [] (const char* id, const char* part, bool fr)
            {
                RoomMember m;
                m.user = { id, id, juce::String (id), juce::String (part) };
                m.isFriend = fr;
                return m;
            };
            Room a;
            a.id = "a"; a.capacity = 4; a.memberCount = 2; a.hostId = "h"; a.hostPresent = true; a.hostRttMs = 6;
            a.members = { member ("h", "guitar", false), member ("x", "bass", true) };
            Room b;
            b.id = "b"; b.capacity = 2; b.memberCount = 2; b.hostId = "h2"; b.hostPresent = true; b.hostRttMs = 30;
            b.members = { member ("h2", "drums", false), member ("y", "vocal", false) };
            Room c = b;
            c.id = "c"; c.hostRttMs.reset();

            const std::vector<Room> list { a, b, c };
            RoomFilter f;
            expectEquals (f.count (list, "me", 0.7, 8), 3);

            f.emptyParts = { "vocal", "drums" };   // 둘 다 없는 방 (AND)
            expectEquals (f.count (list, "me", 0.7, 8), 1);
            f.emptyParts = { "vocal" };
            expectEquals (f.count (list, "me", 0.7, 8), 1);
            f.emptyParts.clear();

            f.maxLatencyMs = 10;   // a: 0.7*(8+6)=10 → 포함, b: 27 → 제외, c: 모름 → 제외
            expectEquals (f.count (list, "me", 0.7, 8), 1);
            f.maxLatencyMs = 20;
            expectEquals (f.count (list, "me", 0.7, 8), 1);
            expectEquals (f.count (list, "me", 0.7, std::nullopt), 0);
            f.maxLatencyMs = 0;

            f.onlyWithRoom = true;
            expectEquals (f.count (list, "me", 0.7, 8), 1);
            f.onlyWithRoom = false;
            f.onlyWithFriends = true;
            expectEquals (f.count (list, "me", 0.7, 8), 1);
        }

        beginTest ("parts");
        expectEquals (parts::keys().size(), 6);
        expect (! parts::filterKeys().contains ("other"));
        expect (parts::isValid ("keys") && ! parts::isValid ("piano"));
    }
};

static ValidationTests validationTests;

} // namespace meon::core
