// 공개 방 카드 메이슨리 배치 (디자인 hColsFor)
#include "../ui/widgets/Masonry.h"

namespace meon::ui
{

class MasonryTests : public juce::UnitTest
{
public:
    MasonryTests() : juce::UnitTest ("Masonry", "Ui") {}

    void runTest() override
    {
        beginTest ("card height formula");
        expectEquals (Masonry::cardHeight (4, 2), 2 + 24 + 19 + 9 + 34 + 9 + 77 + 9 + 19);
        expectEquals (Masonry::cardHeight (1, 1), 2 + 24 + 19 + 9 + 17 + 9 + 17 + 9 + 19);

        beginTest ("columns by width");
        expectEquals (Masonry::columnsForWidth (900), 2);
        expectEquals (Masonry::columnsForWidth (1099), 2);
        expectEquals (Masonry::columnsForWidth (1100), 3);
        expectEquals (Masonry::columnsForWidth (1449), 3);
        expectEquals (Masonry::columnsForWidth (1512), 4);

        beginTest ("shortest column, stable placement, empty columns last");
        Masonry m;
        std::vector<Masonry::Item> items { { "a", 200 }, { "b", 100 }, { "c", 100 }, { "d", 50 } };
        auto cols = m.place (items, 3);
        expectEquals ((int) cols.size(), 3);
        expect (cols[0] == std::vector<juce::String> { "a" });
        expect (cols[1] == std::vector<juce::String> { "b", "d" });
        expect (cols[2] == std::vector<juce::String> { "c" });

        // b 가 커져도 다른 열로 옮기지 않는다
        items[1].height = 400;
        cols = m.place (items, 3);
        expect (cols[1] == std::vector<juce::String> { "b", "d" });

        // 새 카드는 지금 가장 짧은 열 (c 열: 100)
        items.push_back ({ "e", 10 });
        cols = m.place (items, 3);
        expect (cols[2] == std::vector<juce::String> { "c", "e" });

        // a 가 사라지면 빈 열은 뒤로
        items.erase (items.begin());
        cols = m.place (items, 3);
        expect (cols[0] == std::vector<juce::String> { "b", "d" });
        expect (cols[2].empty());

        // 열 수가 바뀌면 다시 배치
        cols = m.place (items, 2);
        expectEquals ((int) cols.size(), 2);
    }
};

static MasonryTests masonryTests;

} // namespace meon::ui
