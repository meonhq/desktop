// MEON ui - 공개 방 카드의 메이슨리 배치 (디자인의 hColsFor). 화면 없이 계산만 한다 (단위 테스트 대상).
//   - 카드는 가장 짧은 열에 넣고, 한 번 들어간 열은 유지한다 (카드가 튀지 않게)
//   - 열 수가 바뀌면 처음부터 다시 배치
//   - 빈 열은 뒤로 모은다
#pragma once

#include <JuceHeader.h>
#include <map>
#include <vector>

namespace meon::ui
{

struct Masonry
{
    static constexpr int gap = 9;

    /** 디자인 cardH: 2+24+19+9+17×(소개 줄 수)+9+(인원×20−3)+9+19 */
    static int cardHeight (int members, int descLines)
    {
        return 2 + 24 + 19 + 9 + 17 * (descLines > 0 ? descLines : 2) + 9 + (members * 20 - 3) + 9 + 19;
    }

    /** 창(목록 영역) 폭 → 열 수: 1100 미만 2, 1450 미만 3, 그 이상 4 */
    static int columnsForWidth (int width)
    {
        return width < 1100 ? 2 : (width < 1450 ? 3 : 4);
    }

    struct Item { juce::String id; int height; };

    /** items 를 n 열로 나눈다. 열마다 id 목록 (위에서 아래) */
    std::vector<std::vector<juce::String>> place (const std::vector<Item>& items, int n)
    {
        if (n != columns) { columns = n; map.clear(); }
        std::vector<int> heights ((size_t) n, 0);
        for (auto& it : items)
            if (auto f = map.find (it.id); f != map.end() && f->second < n)
                heights[(size_t) f->second] += it.height + gap;
        for (auto& it : items)
        {
            if (map.count (it.id) > 0) continue;
            int c = 0;
            for (int i = 1; i < n; ++i)
                if (heights[(size_t) i] < heights[(size_t) c]) c = i;
            map[it.id] = c;
            heights[(size_t) c] += it.height + gap;
        }
        std::vector<std::vector<juce::String>> cols ((size_t) n);
        for (auto& it : items) cols[(size_t) map[it.id]].push_back (it.id);

        std::vector<std::vector<juce::String>> packed;
        for (auto& c : cols) if (! c.empty()) packed.push_back (c);
        for (auto& c : cols) if (c.empty()) packed.push_back (c);
        for (size_t i = 0; i < packed.size(); ++i)
            for (auto& id : packed[i]) map[id] = (int) i;

        // 목록에서 사라진 카드는 잊는다
        for (auto it = map.begin(); it != map.end();)
        {
            bool present = false;
            for (auto& x : items) if (x.id == it->first) { present = true; break; }
            it = present ? std::next (it) : map.erase (it);
        }
        return packed;
    }

    void reset() { map.clear(); columns = 0; }

private:
    int columns = 0;
    std::map<juce::String, int> map;
};

} // namespace meon::ui
