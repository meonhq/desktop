// MEON core - 접을 수 있는 패널의 접기·펼치기 움직임 (화면 없이 계산만. 화면은 ui/widgets/FoldAnimator).
//   CubicBezier : CSS cubic-bezier(x1, y1, x2, y2) 와 같은 값 (x 로 t 를 구할 때 뉴턴법, 안 되면 이분법)
//   FoldProgress: 패널 하나의 진행값 e (접힘 0 ~ 펼침 1). e = 시작값 + (목표값 − 시작값) × ease(경과 / 길이).
//                 움직이는 중에 반대로 누르면 지금 e 에서 이어서 가고, 길이 = 전체 길이 × 남은 거리
//   FoldLayout  : 접기 전 기존 레이아웃 규칙(끝 상태)과, 움직이는 동안 두 패널의 e 로 네 끝 상태를 이중 선형 보간한 크기
#pragma once

#include <cmath>
#include <utility>

namespace meon::core
{

class CubicBezier
{
public:
    CubicBezier (double x1, double y1, double x2, double y2)
    {
        cx = 3.0 * x1; bx = 3.0 * (x2 - x1) - cx; ax = 1.0 - cx - bx;
        cy = 3.0 * y1; by = 3.0 * (y2 - y1) - cy; ay = 1.0 - cy - by;
    }

    /** x(0~1) → y. 0 과 1 은 정확히 0 과 1 */
    double operator() (double x) const
    {
        if (x <= 0.0) return 0.0;
        if (x >= 1.0) return 1.0;
        return sampleY (solveT (x));
    }

    double sampleX (double t) const { return ((ax * t + bx) * t + cx) * t; }
    double sampleY (double t) const { return ((ay * t + by) * t + cy) * t; }

    /** x(t) = x 인 t. 뉴턴법 8번, 기울기가 너무 작거나 덜 맞으면 [0, 1] 이분법으로 보정 */
    double solveT (double x, double eps = 1.0e-7) const
    {
        double t = x;
        for (int i = 0; i < 8; ++i)
        {
            const double err = sampleX (t) - x;
            if (std::abs (err) < eps) return t;
            const double d = (3.0 * ax * t + 2.0 * bx) * t + cx;
            if (std::abs (d) < 1.0e-6) break;
            t -= err / d;
        }
        double lo = 0.0, hi = 1.0;
        t = x;
        if (t < lo) t = lo;
        if (t > hi) t = hi;
        for (int i = 0; i < 60; ++i)
        {
            const double v = sampleX (t);
            if (std::abs (v - x) < eps) return t;
            if (x > v) lo = t; else hi = t;
            t = (lo + hi) * 0.5;
        }
        return t;
    }

private:
    double ax, bx, cx, ay, by, cy;
};

class FoldProgress
{
public:
    FoldProgress (bool open, double fullMs, CubicBezier ease) : full (fullMs), curve (ease)
    {
        from = to = open ? 1.0 : 0.0;
    }

    /** 목표를 바꾼다. animate 가 false 면(동작 줄이기) 바로 끝 값 */
    void setTarget (bool open, double nowMs, bool animate)
    {
        const double target = open ? 1.0 : 0.0;
        const double now = value (nowMs);
        frozen = false;
        if (! animate)
        {
            from = to = target;
            moving = false;
            return;
        }
        if (target == to && (moving || now == target)) return;
        from = now;
        to = target;
        start = nowMs;
        length = full * std::abs (to - from);
        moving = length > 0.0;
        if (! moving) from = to;
    }

    double value (double nowMs) const
    {
        if (frozen) return frozenValue;
        if (! moving) return to;
        const double t = (nowMs - start) / length;
        if (t >= 1.0) return to;
        return from + (to - from) * curve (t < 0.0 ? 0.0 : t);
    }

    /** 매 프레임: 길이가 다 지났으면 멈춘다. 아직 움직이면 true */
    bool update (double nowMs)
    {
        if (frozen) return true;
        if (moving && nowMs - start >= length) { moving = false; from = to; }
        return moving;
    }

    bool isMoving() const { return moving || frozen; }
    bool isOpenTarget() const { return to > 0.5; }
    double lengthMs() const { return moving ? length : 0.0; }
    double remainingMs (double nowMs) const { return moving ? std::fmax (0.0, length - (nowMs - start)) : 0.0; }

    /** 개발용 스냅숏: target 쪽으로 움직이는 중 e 에 멈춘 상태 */
    void freeze (bool open, double e)
    {
        to = open ? 1.0 : 0.0;
        from = 1.0 - to;
        moving = false;
        frozen = true;
        frozenValue = e;
    }

private:
    double full;
    CubicBezier curve;
    double from = 1.0, to = 1.0, start = 0.0, length = 0.0;
    bool moving = false, frozen = false;
    double frozenValue = 1.0;
};

struct FoldLayout
{
    using Pair = std::pair<int, int>;   // 묶음 안 두 패널의 크기 (위·아래 또는 왼쪽·오른쪽)

    /** 세로 묶음(합주 채팅·친구) 기존 규칙. avail = 부모 높이 − 간격. 둘 다 열리면 반씩(나머지는 첫 패널) */
    static Pair verticalPair (int avail, int closed, bool openA, bool openB)
    {
        if (openA && openB) return { avail - avail / 2, avail / 2 };
        if (openA)          return { avail - closed, closed };
        if (openB)          return { closed, avail - closed };
        return { closed, closed };
    }

    /** 가로 묶음(내 오디오·모니터) 접힌 폭: min(가용 폭 / 2, 240) */
    static int horizontalClosed (int avail) { return avail / 2 < closedWideMax ? avail / 2 : closedWideMax; }
    static constexpr int closedWideMax = 84 + 36 + 120;

    static Pair horizontalPair (int avail, bool openA, bool openB)
    {
        const int c = horizontalClosed (avail);
        if (openA && openB) return { avail - avail / 2, avail / 2 };
        if (openA)          return { avail - c, c };
        if (openB)          return { c, avail - c };
        return { c, c };
    }

    /** 네 끝 상태 L[a 열림][b 열림] 를 ea·eb 로 이중 선형 보간. 둘째 크기는 누적 합을 반올림한 값에서 빼서
        반올림 오차를 둘째가 흡수한다. e 가 0·1 이면 끝 상태 그대로.
        fill: 이 움직임의 시작·끝 상태가 모두 부모를 채울 때 (둘 다 닫힘이 아닐 때). 하나를 접으며 다른 하나를 펼치면
        이중 보간에 "둘 다 닫힘" 이 섞여 합이 줄어드는데, 이때는 둘째가 나머지를 채워 매 프레임 합 = 부모 */
    static Pair blend (const Pair (&L)[2][2], double ea, double eb, bool fill = false)
    {
        double fa = 0.0, fb = 0.0;
        for (int a = 0; a < 2; ++a)
            for (int b = 0; b < 2; ++b)
            {
                const double w = (a ? ea : 1.0 - ea) * (b ? eb : 1.0 - eb);
                fa += w * L[a][b].first;
                fb += w * L[a][b].second;
            }
        const int ia = (int) std::lround (fa);
        if (fill) return { ia, L[1][1].first + L[1][1].second - ia };
        return { ia, (int) std::lround (fa + fb) - ia };
    }

    /** 네 끝 상태의 값 하나(미터 폭 등)를 같은 가중치로 보간 */
    static int blendValue (const int (&V)[2][2], double ea, double eb)
    {
        double f = 0.0;
        for (int a = 0; a < 2; ++a)
            for (int b = 0; b < 2; ++b)
                f += (a ? ea : 1.0 - ea) * (b ? eb : 1.0 - eb) * V[a][b];
        return (int) std::lround (f);
    }

    /** 패널 하나뿐인 묶음(홈 친구) */
    static int blendSingle (int closed, int open, double e) { return (int) std::lround (closed + (open - closed) * e); }
};

} // namespace meon::core
