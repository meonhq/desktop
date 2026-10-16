// 접기·펼치기: cubic-bezier 값(CSS 와 같은지), 진행값 e, 중간에 다시 눌렀을 때 이어지는지와 남은 시간,
// e = 0·1 에서 기존 레이아웃과 같은지, 매 프레임 크기 합.
#include <JuceHeader.h>
#include "../core/util/Fold.h"

namespace meon::core
{

namespace
{
    // 기존 레이아웃 코드(애니메이션 넣기 전 JamScreen::resized)를 그대로 옮긴 것. FoldLayout 과 따로 둔다
    FoldLayout::Pair oldVertical (int avail, bool chatOpen, bool friendsOpen)
    {
        const int closedH = 48;
        int chatH, frH;
        if (chatOpen && friendsOpen) { chatH = avail - avail / 2; frH = avail / 2; }
        else if (chatOpen)           { frH = closedH; chatH = avail - frH; }
        else if (friendsOpen)        { chatH = closedH; frH = avail - chatH; }
        else                         { chatH = frH = closedH; }
        return { chatH, frH };
    }

    FoldLayout::Pair oldHorizontal (int availW, bool sendOpen, bool monOpen)
    {
        int sendW, monW;
        if (sendOpen && monOpen)  { sendW = availW - availW / 2; monW = availW / 2; }
        else if (sendOpen)        { monW = juce::jmin (availW / 2, 84 + 36 + 120); sendW = availW - monW; }
        else if (monOpen)         { sendW = juce::jmin (availW / 2, 84 + 36 + 120); monW = availW - sendW; }
        else                      { sendW = monW = juce::jmin (availW / 2, 84 + 36 + 120); }
        return { sendW, monW };
    }

    CubicBezier foldEase() { return { 0.2, 0.0, 0.0, 1.0 }; }   // motion::foldEase 와 같은 값
}

class FoldTests : public juce::UnitTest
{
public:
    FoldTests() : juce::UnitTest ("Fold animation", "UI") {}

    void runTest() override
    {
        beginTest ("cubic-bezier: same values as CSS");
        {
            const auto e = foldEase();
            expectEquals (e (0.0), 0.0);
            expectEquals (e (1.0), 1.0);
            expectEquals (e (-0.5), 0.0);
            expectEquals (e (1.5), 1.0);
            // 기준값: 정밀 이분법으로 따로 구한 값 (cubic-bezier(0.2, 0, 0, 1))
            expectWithinAbsoluteError (e (0.10), 0.15625, 1.0e-5);
            expectWithinAbsoluteError (e (0.25), 0.6072203620726822, 1.0e-5);
            expectWithinAbsoluteError (e (0.50), 0.8778336048097608, 1.0e-5);
            expectWithinAbsoluteError (e (0.75), 0.97548006787611, 1.0e-5);
            expectWithinAbsoluteError (e (0.90), 0.9964590405116713, 1.0e-5);
            // CSS 의 이름 붙은 곡선: ease(0.5) ≈ 0.8024 (브라우저 값), ease-in-out 은 0.5 에서 0.5
            const CubicBezier ease (0.25, 0.1, 0.25, 1.0), inOut (0.42, 0.0, 0.58, 1.0);
            expectWithinAbsoluteError (ease (0.5), 0.8024033875848569, 1.0e-5);
            expectWithinAbsoluteError (ease (0.25), 0.40851059135539586, 1.0e-5);
            expectWithinAbsoluteError (inOut (0.5), 0.5, 1.0e-6);
            expectWithinAbsoluteError (inOut (0.3), 0.18739590670531253, 1.0e-5);
            // 단조 증가
            double prev = 0.0;
            bool mono = true;
            for (int i = 1; i <= 1000; ++i) { const double v = e (i / 1000.0); mono = mono && v >= prev; prev = v; }
            expect (mono);
        }

        beginTest ("progress: e = start + (target - start) x ease(elapsed / length)");
        {
            FoldProgress p (true, 180.0, foldEase());
            expect (! p.isMoving());
            expectEquals (p.value (0.0), 1.0);
            p.setTarget (false, 1000.0, true);
            expect (p.isMoving());
            expectEquals (p.lengthMs(), 180.0);
            expectEquals (p.value (1000.0), 1.0);
            expectWithinAbsoluteError (p.value (1090.0), 1.0 - foldEase() (0.5), 1.0e-9);
            expectWithinAbsoluteError (p.value (1045.0), 1.0 - foldEase() (0.25), 1.0e-9);
            expect (p.update (1179.0));
            expect (! p.update (1180.0));
            expectEquals (p.value (1180.0), 0.0);
            expectEquals (p.value (5000.0), 0.0);
            // 같은 목표로 또 누르면 다시 시작하지 않는다
            p.setTarget (false, 2000.0, true);
            expect (! p.isMoving());
        }

        beginTest ("progress: pressing again mid-way continues from the current e, length = 180 x remaining");
        {
            FoldProgress p (true, 180.0, foldEase());
            p.setTarget (false, 0.0, true);
            const double at = 60.0;
            const double eNow = p.value (at);
            expect (eNow > 0.0 && eNow < 1.0);
            p.setTarget (true, at, true);   // 반대로
            expectWithinAbsoluteError (p.value (at), eNow, 1.0e-12);   // 처음 값으로 튀지 않는다
            expectWithinAbsoluteError (p.lengthMs(), 180.0 * (1.0 - eNow), 1.0e-9);
            expectWithinAbsoluteError (p.remainingMs (at), 180.0 * (1.0 - eNow), 1.0e-9);
            const double half = at + p.lengthMs() / 2;
            expectWithinAbsoluteError (p.value (half), eNow + (1.0 - eNow) * foldEase() (0.5), 1.0e-9);
            expect (p.update (at + p.lengthMs() - 1.0));
            expect (! p.update (at + p.lengthMs()));
            expectEquals (p.value (at + p.lengthMs()), 1.0);
            // 연속으로 몇 번 뒤집어도 값이 이어진다
            FoldProgress q (false, 180.0, foldEase());
            double t = 0.0;
            bool open = true, continuous = true;
            q.setTarget (open, t, true);
            for (int i = 0; i < 6; ++i)
            {
                t += 37.0;
                const double before = q.value (t);
                open = ! open;
                q.setTarget (open, t, true);
                continuous = continuous && std::abs (q.value (t) - before) < 1.0e-12;
            }
            expect (continuous);
        }

        beginTest ("progress: reduce motion jumps to the end");
        {
            FoldProgress p (true, 180.0, foldEase());
            p.setTarget (false, 0.0, false);
            expect (! p.isMoving());
            expectEquals (p.value (0.0), 0.0);
            // 움직이는 중에 바로 바꿔도 끝 값
            p.setTarget (true, 10.0, true);
            p.setTarget (false, 20.0, false);
            expect (! p.isMoving());
            expectEquals (p.value (20.0), 0.0);
        }

        beginTest ("layout: e = 0 / 1 is pixel-identical to the old layout");
        {
            bool same = true;
            for (int avail = 96; avail <= 900; avail += 7)
                for (int a = 0; a < 2; ++a)
                    for (int b = 0; b < 2; ++b)
                    {
                        FoldLayout::Pair V[2][2], H[2][2];
                        for (int i = 0; i < 2; ++i)
                            for (int j = 0; j < 2; ++j)
                            {
                                V[i][j] = FoldLayout::verticalPair (avail, 48, i == 1, j == 1);
                                H[i][j] = FoldLayout::horizontalPair (avail, i == 1, j == 1);
                            }
                        same = same && FoldLayout::verticalPair (avail, 48, a == 1, b == 1) == oldVertical (avail, a == 1, b == 1);
                        same = same && FoldLayout::horizontalPair (avail, a == 1, b == 1) == oldHorizontal (avail, a == 1, b == 1);
                        same = same && FoldLayout::blend (V, a, b) == oldVertical (avail, a == 1, b == 1);
                        same = same && FoldLayout::blend (H, a, b) == oldHorizontal (avail, a == 1, b == 1);
                    }
            expect (same);
            expectEquals (FoldLayout::blendSingle (48, 517, 0.0), 48);
            expectEquals (FoldLayout::blendSingle (48, 517, 1.0), 517);
        }

        beginTest ("layout: every frame the sizes + gap add up to the parent (when both ends fill it)");
        {
            const auto ease = foldEase();
            bool sums = true, moves = true;
            for (int avail = 96; avail <= 900; avail += 11)
            {
                FoldLayout::Pair V[2][2], H[2][2];
                for (int i = 0; i < 2; ++i)
                    for (int j = 0; j < 2; ++j)
                    {
                        V[i][j] = FoldLayout::verticalPair (avail, 48, i == 1, j == 1);
                        H[i][j] = FoldLayout::horizontalPair (avail, i == 1, j == 1);
                    }
                // 끝 상태가 모두 부모를 채우는 움직임: 둘 다 열림 ↔ 하나만 열림, 하나만 열림 ↔ 다른 하나만 열림
                for (int f = 0; f <= 60; ++f)
                {
                    const double e = ease (f / 60.0);
                    for (auto* L : { &V, &H })
                    {
                        auto p1 = FoldLayout::blend (*L, 1.0 - e, 1.0);   // 첫 패널 접기 (둘 다 열림에서)
                        auto p2 = FoldLayout::blend (*L, 1.0, 1.0 - e);   // 둘째 패널 접기
                        auto p3 = FoldLayout::blend (*L, 1.0 - e, e, true);   // 둘이 서로 바뀜 (하나 접고 하나 펼침): 시작·끝 모두 채움
                        sums = sums && p1.first + p1.second == avail && p2.first + p2.second == avail && p3.first + p3.second == avail;
                    }
                }
                // fill 이어도 끝 상태는 그대로
                sums = sums && FoldLayout::blend (V, 1.0, 0.0, true) == V[1][0] && FoldLayout::blend (H, 0.0, 1.0, true) == H[0][1];
                // 둘 다 닫힘이 끝인 움직임: 합은 두 끝 사이를 오가고 크기는 한쪽으로만 움직인다
                int prevA = V[1][0].first;
                for (int f = 0; f <= 60; ++f)
                {
                    const auto p = FoldLayout::blend (V, 1.0 - ease (f / 60.0), 0.0);
                    moves = moves && p.first <= prevA && p.second == 48;
                    prevA = p.first;
                }
            }
            expect (sums);
            expect (moves);
        }
    }
};

static FoldTests foldTests;

} // namespace meon::core
