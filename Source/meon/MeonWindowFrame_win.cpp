// MEON - 윈도우 독립 앱 창 틀: 운영체제 창은 두고 제목 줄 자리만 앱 내용으로 덮는다. 설명은 MeonWindowFrame.h
// 창 틀 계산(상태별 내용 자리·틀 두께·크기 조절 판정·그림자)은 이 파일 맨 위 "창 틀 계산" 한 곳에만 있다.
// 참고한 공개 구현 (같은 방식으로 맞췄다):
//  - Windows Terminal (microsoft/terminal, NonClientIslandWindow::_OnNcCalcSize · _GetResizeHandleHeight):
//    최대화한 창은 크기 조절 두께만큼 화면 밖으로 크다. 내용 = 창 - (SM_CXFRAME/SM_CYSIZEFRAME + SM_CXPADDEDBORDER, 창 DPI) = 작업 영역
//  - melak47/BorderlessWindow (adjust_maximized_client_rect · set_shadow · hit_test):
//    보통 창은 내용이 창 전체, 최대화는 모니터 작업 영역, 그림자는 DwmExtendFrameIntoClientArea (1,1,1,1), 크기 조절 판정은 내용 안쪽
#include "MeonWindowFrame.h"

#if defined (_WIN32)

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <cstdio>
#include <climits>
#include <algorithm>
#include <JuceHeader.h>

// 빌드는 WINVER 0x0601 (윈도우 7) 기준이라 윈도우 8 이후 상수는 직접 둔다
#ifndef ABM_GETAUTOHIDEBAREX
 #define ABM_GETAUTOHIDEBAREX 0x0000000b
#endif
#ifndef WM_DPICHANGED
 #define WM_DPICHANGED 0x02E0
#endif

#if defined (_MSC_VER)
 #pragma comment (lib, "comctl32.lib")
 #pragma comment (lib, "shell32.lib")
 #pragma comment (lib, "ole32.lib")
#endif

namespace meon::winframe
{

namespace
{
    constexpr UINT_PTR subclassId = 0x4D454F4E;   // 'MEON'
    // 테마(uxtheme)가 제목 줄·틀을 직접 그리는 숨은 메시지. 제목 줄 자리가 내용이므로 그리면 내용 위에 운영체제 제목 줄·창 버튼이 나타난다
    constexpr UINT wmNcUahDrawCaption = 0x00AE, wmNcUahDrawFrame = 0x00AF;

    struct State
    {
        HWND hwnd = nullptr;
        Hooks hooks;
        int maxState = 0;              // 최대화 버튼 표시 (0 보통, 1 올림, 2 누름). 윈도우 11 (HTMAXBUTTON) 에서만
        bool maxPressed = false, trackingNc = false, notifyPending = false;
        int ncCalc = 0, sysMenuOpened = 0, maxClicks = 0, notifies = 0, captionPaintsBlocked = 0;
        long borderColourResult = 1;
        bool fullscreen = false;            // F11 전체 화면
        bool holdPosition = false;          // JUCE 가 틀 두께를 다시 읽는 동안 창을 움직이지 않는다 (holdPosition)
        long shadowResult = 1;              // DwmExtendFrameIntoClientArea 결과 (윈도우 10 그림자)
        int bandPaints = 0;                 // 윈도우 10 가장자리 1px 다시 놓은 횟수 (개발용)
        WINDOWPLACEMENT savedPlacement {};  // 켜기 전 자리·크기·최대화
        LONG savedStyle = 0;
    };
    State st;

    template <typename Fn> Fn user32 (const char* name)
    {
        return reinterpret_cast<Fn> (reinterpret_cast<void*> (GetProcAddress (GetModuleHandleW (L"user32.dll"), name)));
    }

    UINT dpiOf (HWND h)
    {
        static auto fn = user32<UINT (WINAPI*) (HWND)> ("GetDpiForWindow");   // 윈도우 10 1607+
        const UINT d = fn != nullptr ? fn (h) : 0;
        return d != 0 ? d : 96;
    }

    int metricForDpi (int index, UINT dpi)
    {
        static auto fn = user32<int (WINAPI*) (int, UINT)> ("GetSystemMetricsForDpi");
        return fn != nullptr ? fn (index, dpi) : MulDiv (GetSystemMetrics (index), (int) dpi, 96);
    }

    int windowsBuild()
    {
        static const int build = []
        {
            wchar_t text[32] {};
            DWORD size = sizeof (text);
            if (RegGetValueW (HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"CurrentBuildNumber",
                              RRF_RT_REG_SZ, nullptr, text, &size) != ERROR_SUCCESS)
                return 0;
            return _wtoi (text);
        }();
        return build;
    }

    bool isWin11() { return windowsBuild() >= 22000; }

    bool zoomed (HWND h) { return IsZoomed (h) != FALSE; }

    MONITORINFO monitorInfo (HMONITOR mon)
    {
        MONITORINFO mi {};
        mi.cbSize = sizeof (mi);
        GetMonitorInfoW (mon, &mi);
        return mi;
    }

    //==============================================================================
    // 창 틀 계산 (여기 한 곳)

    /** 크기 조절 두께 (물리 px): 운영체제가 쓰는 값 (100% 에서 4 + 4 = 8, 배율에 비례) */
    int resizeBorder (HWND h)
    {
        const UINT dpi = dpiOf (h);
        return metricForDpi (SM_CYSIZEFRAME, dpi) + metricForDpi (SM_CXPADDEDBORDER, dpi);
    }

    /** 보통 창(과 전체 화면)의 틀 두께 (물리 px).
        윈도우 11: 옆·아래 = 크기 조절 두께 (그 안쪽 끝 1px 이 운영체제 테두리, 색 #EBEBEB), 위 = 0.
        윈도우 10: 0 (내용이 창 전체. 운영체제 1px 테두리 #707070 은 색을 바꿀 수 없어 없애고 앱이 #EBEBEB 를 그린다, 사용자 결정).
        윈도우 11 의 두께는 논리 8px (100% 8, 125% 10, 150% 12, 175% 14, 200% 16) 이라 JUCE 가 논리 정수로 정확히 기억한다 */
    RECT normalInsets (HWND h)
    {
        if (! isWin11()) return { 0, 0, 0, 0 };
        const int b = resizeBorder (h);
        return { b, 0, b, b };
    }

    RECT shrink (RECT r, const RECT& in)
    {
        r.left += in.left; r.top += in.top; r.right -= in.right; r.bottom -= in.bottom;
        r.right = std::max (r.right, r.left);     // 최소화한 작은 창에서도 음수 크기가 되지 않게
        r.bottom = std::max (r.bottom, r.top);
        return r;
    }
    RECT grow (RECT r, const RECT& in)   { r.left -= in.left; r.top -= in.top; r.right += in.right; r.bottom += in.bottom; return r; }

    /** 작업 표시줄이 자동 숨김이면 그 쪽 가장자리를 1px 비운다 (꽉 채우면 작업 표시줄이 다시 나오지 않는다) */
    void leaveRoomForAutoHideTaskbar (const MONITORINFO& mi, RECT& r)
    {
        APPBARDATA abd {};
        abd.cbSize = sizeof (abd);
        if ((SHAppBarMessage (ABM_GETSTATE, &abd) & ABS_AUTOHIDE) == 0) return;
        const UINT edges[] = { ABE_BOTTOM, ABE_TOP, ABE_LEFT, ABE_RIGHT };
        for (UINT e : edges)
        {
            APPBARDATA q {};
            q.cbSize = sizeof (q);
            q.uEdge = e;
            q.rc = mi.rcMonitor;
            if (SHAppBarMessage (ABM_GETAUTOHIDEBAREX, &q) == 0) continue;
            if (e == ABE_BOTTOM) r.bottom -= 1;
            else if (e == ABE_TOP) r.top += 1;
            else if (e == ABE_LEFT) r.left += 1;
            else r.right -= 1;
            return;
        }
    }

    /** 최대화한 내용 자리 = 작업 영역 (여백·잘림 0) */
    RECT maximisedClient (HMONITOR mon)
    {
        const auto mi = monitorInfo (mon);
        RECT r = mi.rcWork;
        leaveRoomForAutoHideTaskbar (mi, r);
        return r;
    }

    /** 전체 화면 내용 자리 = 모니터 전체 (작업 표시줄 자리까지) */
    RECT fullscreenClient (HMONITOR mon) { return monitorInfo (mon).rcMonitor; }

    /** 창 상태. WM_NCCALCSIZE 때는 이미 바뀐 뒤의 상태다 (최대화 스타일이 먼저 붙는다: Windows Terminal 도 이것을 본다) */
    enum class Mode { normal, maximised, fullscreen, minimised };
    Mode modeOf (HWND h)
    {
        if (st.fullscreen) return Mode::fullscreen;
        if (IsIconic (h)) return Mode::minimised;
        if (zoomed (h)) return Mode::maximised;
        return Mode::normal;
    }

    /** 창 사각형 → 내용 사각형 (WM_NCCALCSIZE). 상태별 규칙은 여기에만 있다.
        최대화: 운영체제가 창을 크기 조절 두께만큼 화면 밖으로 크게 둔다 → 내용 = 그 모니터의 작업 영역 (= 창 - 크기 조절 두께)
        보통·전체 화면·최소화: 창 - normalInsets */
    RECT clientFromWindow (HWND h, const RECT& window)
    {
        if (modeOf (h) == Mode::maximised)
            return maximisedClient (MonitorFromRect (&window, MONITOR_DEFAULTTONEAREST));
        return shrink (window, normalInsets (h));
    }
    /** 내용 사각형 → 창 사각형 (보통·전체 화면에서 창을 놓을 때) */
    RECT windowForClient (HWND h, const RECT& client) { return grow (client, normalInsets (h)); }

    /** 화면 배율에서 논리 px 정수로 정확히 떨어지는 물리 px 단위 (100% 1, 125% 5, 150% 3, 175% 7, 200% 2).
        JUCE 는 창 크기를 논리 px 정수로 기억하므로, 보통 창의 내용 크기를 이 배수로 맞춰야 JUCE 가 아는 크기 = 실제 크기다 */
    int pixelStep (HWND h)
    {
        const int dpi = (int) dpiOf (h);
        int a = dpi, b = 96;
        while (b != 0) { const int t = a % b; a = b; b = t; }   // 최대공약수
        return a > 0 ? dpi / a : 1;
    }
    int snapDown (int v, int step) { return step <= 1 ? v : std::max (step, (v / step) * step); }

    /** 크기 조절 판정 (화면 물리 좌표, BorderlessWindow hit_test 와 같은 방식). 창 바깥 끝에서 크기 조절 두께 안쪽:
        윈도우 11 은 옆·아래 틀 자리, 윈도우 10 은 내용 끝 (틀이 없다). 위는 내용 맨 위 줄 */
    LRESULT resizeHit (HWND h, int x, int y)
    {
        if (st.fullscreen || zoomed (h) || IsIconic (h)) return HTNOWHERE;
        RECT wr;
        GetWindowRect (h, &wr);
        const int b = resizeBorder (h), corner = b * 2;
        const bool left = x < wr.left + b, right = x >= wr.right - b, top = y < wr.top + b, bottom = y >= wr.bottom - b;
        const bool nearLeft = x < wr.left + corner, nearRight = x >= wr.right - corner;
        const bool nearTop = y < wr.top + corner, nearBottom = y >= wr.bottom - corner;
        if (top)    return nearLeft ? HTTOPLEFT : nearRight ? HTTOPRIGHT : HTTOP;
        if (bottom) return nearLeft ? HTBOTTOMLEFT : nearRight ? HTBOTTOMRIGHT : HTBOTTOM;
        if (left)   return nearTop ? HTTOPLEFT : nearBottom ? HTBOTTOMLEFT : HTLEFT;
        if (right)  return nearTop ? HTTOPRIGHT : nearBottom ? HTBOTTOMRIGHT : HTRIGHT;
        return HTNOWHERE;
    }

    //==============================================================================
    // DWM (dwmapi.dll 은 직접 링크하지 않고 불러 쓴다: 빌드 기준이 윈도우 7)
    template <typename Fn> Fn dwmapi (const char* name)
    {
        static HMODULE lib = LoadLibraryW (L"dwmapi.dll");
        return lib != nullptr ? reinterpret_cast<Fn> (reinterpret_cast<void*> (GetProcAddress (lib, name))) : nullptr;
    }
    constexpr DWORD dwmNcRenderingEnabled = 1, dwmExtendedFrameBounds = 9, dwmCornerPreference = 33, dwmBorderColor = 34;   // DWMWA_*
    constexpr DWORD dwmColourNone = 0xFFFFFFFE;                    // DWMWA_COLOR_NONE
    constexpr DWORD dwmCornerDefault = 0, dwmCornerSquare = 1;    // DWMWCP_DEFAULT, DWMWCP_DONOTROUND

    long dwmSet (HWND h, DWORD attr, DWORD value)
    {
        static auto set = dwmapi<HRESULT (WINAPI*) (HWND, DWORD, LPCVOID, DWORD)> ("DwmSetWindowAttribute");
        if (set == nullptr) return -1;
        return (long) set (h, attr, &value, sizeof (value));
    }

    /** 윈도우 11: 창 테두리 색 #EBEBEB (강조 색 설정과 상관없이). 전체 화면은 테두리 없음·모서리 각지게.
        윈도우 10 은 이 속성이 없어 실패한다 (테두리는 앱이 그린다) */
    long applyBorderColour (HWND h)
    {
        if (st.fullscreen)
        {
            dwmSet (h, dwmCornerPreference, dwmCornerSquare);
            return dwmSet (h, dwmBorderColor, dwmColourNone);
        }
        dwmSet (h, dwmCornerPreference, dwmCornerDefault);
        return dwmSet (h, dwmBorderColor, (DWORD) RGB ((borderColourRGB >> 16) & 0xFF, (borderColourRGB >> 8) & 0xFF, borderColourRGB & 0xFF));
    }

    /** 윈도우 10 보통 창 그림자: 틀이 0 이면 운영체제 그림자도 없어, 틀을 내용 쪽으로 1px 늘린다 (BorderlessWindow set_shadow 와 같은 값).
        최대화·전체 화면·윈도우 11 (틀이 있어 그림자가 있다) 은 0 */
    struct FrameMargins { int left, right, top, bottom; };   // MARGINS (uxtheme.h 없이)
    long applyShadow (HWND h)
    {
        static auto fn = dwmapi<HRESULT (WINAPI*) (HWND, const FrameMargins*)> ("DwmExtendFrameIntoClientArea");
        if (fn == nullptr) return -1;
        const bool on = ! isWin11() && modeOf (h) == Mode::normal;
        const FrameMargins m = on ? FrameMargins { 1, 1, 1, 1 } : FrameMargins { 0, 0, 0, 0 };
        return (long) fn (h, &m);
    }

    /** 윈도우 10 보통 창: 늘린 틀 자리(내용 네 가장자리 1px)는 그림의 투명도를 쓰는데 JUCE 그림은 투명도가 0 이라 그 자리가 비친다.
        그 1px 에 불투명 #EBEBEB 를 다시 놓는다 (앱이 그리는 테두리와 같은 색·자리). 32비트 그림을 그대로 옮기면 투명도가 남는다 */
    void paintShadowBand (HWND h)
    {
        if (isWin11() || modeOf (h) != Mode::normal) return;
        RECT cr {};
        GetClientRect (h, &cr);
        const int w = cr.right - cr.left, ht = cr.bottom - cr.top;
        if (w <= 0 || ht <= 0) return;
        HDC dc = GetDC (h);
        if (dc == nullptr) return;
        HDC mem = CreateCompatibleDC (dc);
        const int n = std::max (w, ht);
        BITMAPINFO bi {};
        bi.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = n;
        bi.bmiHeader.biHeight = -1;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP bmp = CreateDIBSection (dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (mem != nullptr && bmp != nullptr && bits != nullptr)
        {
            auto* px = static_cast<DWORD*> (bits);
            for (int i = 0; i < n; ++i) px[i] = 0xFF000000u | (DWORD) borderColourRGB;   // 미리 곱한 불투명 #EBEBEB
            auto old = SelectObject (mem, bmp);
            BitBlt (dc, 0, 0, w, 1, mem, 0, 0, SRCCOPY);            // 위
            BitBlt (dc, 0, ht - 1, w, 1, mem, 0, 0, SRCCOPY);       // 아래
            for (int y = 1; y < ht - 1; ++y)                        // 왼쪽·오른쪽 (한 줄씩)
            {
                BitBlt (dc, 0, y, 1, 1, mem, 0, 0, SRCCOPY);
                BitBlt (dc, w - 1, y, 1, 1, mem, 0, 0, SRCCOPY);
            }
            SelectObject (mem, old);
            ++st.bandPaints;
        }
        if (bmp != nullptr) DeleteObject (bmp);
        if (mem != nullptr) DeleteDC (mem);
        ReleaseDC (h, dc);
    }

    /** 작업 표시줄에 전체 화면 창이라고 알린다 (모니터를 덮은 앞 창이면 작업 표시줄이 알아서 내려가지만, 놓치는 경우가 있다) */
    void markFullscreen (HWND h, bool on)
    {
        static const CLSID clsidTaskbarList = { 0x56FDF344, 0xFD6D, 0x11D0, { 0x95, 0x8A, 0x00, 0x60, 0x97, 0xC9, 0xA0, 0x90 } };
        static const IID iidTaskbarList2 = { 0x602D4995, 0xB13A, 0x429B, { 0xA6, 0x6E, 0x19, 0x35, 0xE4, 0x4F, 0x43, 0x17 } };
        struct TaskbarList2 : IUnknown   // ITaskbarList2 (shobjidl.h 없이: 빌드 기준이 윈도우 7)
        {
            virtual HRESULT STDMETHODCALLTYPE HrInit() = 0;
            virtual HRESULT STDMETHODCALLTYPE AddTab (HWND) = 0;
            virtual HRESULT STDMETHODCALLTYPE DeleteTab (HWND) = 0;
            virtual HRESULT STDMETHODCALLTYPE ActivateTab (HWND) = 0;
            virtual HRESULT STDMETHODCALLTYPE SetActiveAlt (HWND) = 0;
            virtual HRESULT STDMETHODCALLTYPE MarkFullscreenWindow (HWND, BOOL) = 0;
        };
        TaskbarList2* tb = nullptr;
        if (FAILED (CoCreateInstance (clsidTaskbarList, nullptr, CLSCTX_INPROC_SERVER, iidTaskbarList2, reinterpret_cast<void**> (&tb))) || tb == nullptr)
            return;
        if (SUCCEEDED (tb->HrInit())) tb->MarkFullscreenWindow (h, on ? TRUE : FALSE);
        tb->Release();
    }

    /** 전체 화면: 모니터 밖으로 나간 틀 자리는 잘라서 옆 모니터에 보이지 않게 (창 좌표) */
    void clipToClient (HWND h)
    {
        RECT wr;
        GetWindowRect (h, &wr);
        const RECT in = normalInsets (h);
        SetWindowRgn (h, CreateRectRgn (in.left, in.top, (wr.right - wr.left) - in.right, (wr.bottom - wr.top) - in.bottom), TRUE);   // 영역은 운영체제가 가진다
    }

    /** 전체 화면이면 창 자리를 모니터 + 틀로 맞춘다 (운영체제·JUCE 가 어떤 자리를 주든). 최대화는 운영체제 자리 그대로
        (내용 자리는 WM_NCCALCSIZE 가 작업 영역으로 맞춘다) */
    void enforceStateRect (HWND h, WINDOWPOS& wp)
    {
        if (IsIconic (h) || ! st.fullscreen) return;
        RECT cur;
        GetWindowRect (h, &cur);
        RECT proposed = cur;
        if ((wp.flags & SWP_NOMOVE) == 0) OffsetRect (&proposed, wp.x - cur.left, wp.y - cur.top);
        if ((wp.flags & SWP_NOSIZE) == 0) { proposed.right = proposed.left + wp.cx; proposed.bottom = proposed.top + wp.cy; }
        if (proposed.left <= -32000 || proposed.top <= -32000) return;   // 최소화 자리
        const HMONITOR mon = MonitorFromRect (&proposed, MONITOR_DEFAULTTONEAREST);
        const RECT want = windowForClient (h, fullscreenClient (mon));
        if (EqualRect (&want, &proposed)) return;
        wp.x = want.left;
        wp.y = want.top;
        wp.cx = want.right - want.left;
        wp.cy = want.bottom - want.top;
        wp.flags &= ~(UINT) (SWP_NOMOVE | SWP_NOSIZE);
    }

    /** 보통 창: 내용 크기를 pixelStep 배수로 (JUCE 가 아는 논리 크기 = 실제). 오른쪽·아래 끝을 옮긴다 */
    void snapNormalSize (HWND h, WINDOWPOS& wp)
    {
        if (modeOf (h) != Mode::normal || (wp.flags & SWP_NOSIZE) != 0) return;
        const int step = pixelStep (h);
        if (step <= 1) return;
        const RECT in = normalInsets (h);
        const int cw = wp.cx - in.left - in.right, ch = wp.cy - in.top - in.bottom;
        if (cw <= 0 || ch <= 0) return;
        wp.cx = snapDown (cw, step) + in.left + in.right;
        wp.cy = snapDown (ch, step) + in.top + in.bottom;
    }

    /** 틀을 다시 계산하게 한다 (자리·크기는 그대로, 최대화·전체 화면이면 상태 자리로) */
    void refreshFrame (HWND h)
    {
        SetWindowPos (h, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
    }

    //==============================================================================
    void setMaxState (int s)
    {
        if (st.maxState == s) return;
        st.maxState = s;
        if (st.hooks.onMaxButton) st.hooks.onMaxButton (s);
    }

    /** 상태 바뀜은 비동기로 한 번에 (활성 전환 중에는 앞 창이 아직 바뀌지 않았다) */
    void notifyLater()
    {
        if (st.notifyPending) return;
        st.notifyPending = true;
        juce::MessageManager::callAsync ([]
        {
            st.notifyPending = false;
            ++st.notifies;
            if (st.hwnd != nullptr && st.hooks.onStateChange) st.hooks.onStateChange();
        });
    }

    LRESULT hitTest (HWND h, LPARAM lParam)
    {
        const int x = GET_X_LPARAM (lParam), y = GET_Y_LPARAM (lParam);
        RECT wr;
        GetWindowRect (h, &wr);
        if (x < wr.left || x >= wr.right || y < wr.top || y >= wr.bottom) return HTNOWHERE;
        if (st.fullscreen) return HTCLIENT;   // 전체 화면: 끌기·크기 조절 없음. 맨 위에 나타나는 창 버튼은 앱이 받는다
        if (const LRESULT r = resizeHit (h, x, y); r != HTNOWHERE) return r;
        if (st.hooks.hitTest)
        {
            switch (st.hooks.hitTest (x, y))
            {
                case Region::caption:   return HTCAPTION;
                case Region::maxButton: return isWin11() ? HTMAXBUTTON : HTCLIENT;   // 윈도우 10 에서 HTMAXBUTTON 이면 운영체제 최대화 버튼이 그려진다
                case Region::client:    break;
            }
        }
        return HTCLIENT;
    }

    void trackNcLeave (HWND h)
    {
        if (st.trackingNc) return;
        TRACKMOUSEEVENT t {};
        t.cbSize = sizeof (t);
        t.dwFlags = TME_LEAVE | TME_NONCLIENT;
        t.hwndTrack = h;
        st.trackingNc = TrackMouseEvent (&t) != FALSE;
    }

    LRESULT CALLBACK proc (HWND h, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR)
    {
        switch (msg)
        {
            case WM_NCCALCSIZE:
            {
                // 내용 자리 = clientFromWindow (상태별 규칙 한 곳). 운영체제 계산(제목 줄·틀)은 쓰지 않는다
                ++st.ncCalc;
                if (wParam == TRUE)
                {
                    auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*> (lParam);
                    p->rgrc[0] = clientFromWindow (h, p->rgrc[0]);
                }
                else
                {
                    auto* r = reinterpret_cast<RECT*> (lParam);
                    *r = clientFromWindow (h, *r);
                }
                return 0;
            }

            case WM_NCHITTEST:
                return hitTest (h, lParam);

            case WM_WINDOWPOSCHANGING:
            {
                auto& wp = *reinterpret_cast<WINDOWPOS*> (lParam);
                if (st.holdPosition)   // JUCE 가 틀 두께를 다시 읽는 동안: 자리·크기는 그대로
                {
                    wp.flags |= SWP_NOMOVE | SWP_NOSIZE;
                    return 0;
                }
                // JUCE(크기 제한)가 먼저 고친 뒤에 맞춘다: 먼저 고치면 JUCE 가 다시 바꾼다
                const LRESULT r = DefSubclassProc (h, msg, wParam, lParam);
                enforceStateRect (h, wp);   // 전체 화면 = 모니터
                snapNormalSize (h, wp);     // 보통: 내용 크기 = 논리 px 정수
                return r;
            }
            case WM_SIZING:
            {
                // 끌어서 크기 조절: 잡은 끝 쪽을 pixelStep 배수로 (JUCE 크기 제한 뒤에)
                const LRESULT res = DefSubclassProc (h, msg, wParam, lParam);
                auto* r = reinterpret_cast<RECT*> (lParam);
                const int step = pixelStep (h);
                if (step > 1 && modeOf (h) == Mode::normal)
                {
                    const RECT in = normalInsets (h);
                    const int cw = snapDown ((r->right - r->left) - in.left - in.right, step) + in.left + in.right;
                    const int ch = snapDown ((r->bottom - r->top) - in.top - in.bottom, step) + in.top + in.bottom;
                    if (wParam == WMSZ_LEFT || wParam == WMSZ_TOPLEFT || wParam == WMSZ_BOTTOMLEFT) r->left = r->right - cw; else r->right = r->left + cw;
                    if (wParam == WMSZ_TOP || wParam == WMSZ_TOPLEFT || wParam == WMSZ_TOPRIGHT) r->top = r->bottom - ch; else r->bottom = r->top + ch;
                }
                return res;
            }
            case WM_PAINT:
            {
                const LRESULT r = DefSubclassProc (h, msg, wParam, lParam);   // JUCE 가 그린 뒤
                paintShadowBand (h);
                return r;
            }
            case WM_WINDOWPOSCHANGED:
                if (st.fullscreen && (reinterpret_cast<WINDOWPOS*> (lParam)->flags & SWP_NOSIZE) == 0) clipToClient (h);
                break;

            // 운영체제 제목 줄·창 버튼 그리기 막기
            case wmNcUahDrawCaption:
            case wmNcUahDrawFrame:
                ++st.captionPaintsBlocked;
                return 0;
            case WM_SETTEXT:
            case WM_SETICON:
            {
                // 제목·아이콘을 바꿀 때 운영체제가 제목 줄(지금은 내용 자리)에 직접 그리지 않게, 처리하는 동안만 보이지 않는 창으로 둔다
                const LONG_PTR style = GetWindowLongPtrW (h, GWL_STYLE);
                if ((style & WS_VISIBLE) != 0) SetWindowLongPtrW (h, GWL_STYLE, style & ~(LONG_PTR) WS_VISIBLE);
                const LRESULT r = DefSubclassProc (h, msg, wParam, lParam);
                if ((style & WS_VISIBLE) != 0) SetWindowLongPtrW (h, GWL_STYLE, style);
                return r;
            }

            // 최대화 버튼 (윈도우 11 HTMAXBUTTON): 누름·뗌은 여기서 끝내고, 표시는 앱이 그린다
            case WM_NCMOUSEMOVE:
                if (wParam == HTMAXBUTTON) { setMaxState (st.maxPressed ? 2 : 1); trackNcLeave (h); }
                else if (st.maxState != 0) { st.maxPressed = false; setMaxState (0); }
                break;
            case WM_NCMOUSELEAVE:
                st.trackingNc = false;
                st.maxPressed = false;
                setMaxState (0);
                break;
            case WM_NCLBUTTONDOWN:
            case WM_NCLBUTTONDBLCLK:
                if (wParam == HTMAXBUTTON)
                {
                    st.maxPressed = true;
                    setMaxState (2);
                    trackNcLeave (h);
                    return 0;
                }
                break;
            case WM_NCLBUTTONUP:
                if (wParam == HTMAXBUTTON)
                {
                    if (st.maxPressed)
                    {
                        st.maxPressed = false;
                        setMaxState (1);
                        ++st.maxClicks;
                        toggleMaximise (h);
                    }
                    return 0;
                }
                st.maxPressed = false;
                break;
            case WM_MOUSEMOVE:   // 앱 화면 쪽으로 옮겨 갔다
                if (st.maxState != 0 && ! st.maxPressed) setMaxState (0);
                break;

            case WM_KEYDOWN:
            case WM_SYSKEYDOWN:
                // F11: 전체 화면 켜기·끄기. 창이 키를 먼저 받으므로 글자 입력 중에도 된다 (JUCE 로 넘기지 않는다). Alt+F4 등 다른 키는 그대로
                if (wParam == VK_F11)
                {
                    if ((lParam & (1 << 30)) == 0) setFullscreen (h, ! st.fullscreen);   // 누르고 있을 때 되풀이는 무시
                    return 0;
                }
                break;
            case WM_KEYUP:
            case WM_SYSKEYUP:
                if (wParam == VK_F11) return 0;
                break;

            case WM_SYSCOMMAND:
            {
                const auto cmd = wParam & 0xFFF0;
                // Alt+Space (시스템 메뉴): JUCE 는 키보드로 연 창 메뉴를 막는다 (Alt 를 수정 키로 쓰는 앱 때문). 스페이스만 운영체제에 넘긴다
                if (cmd == SC_KEYMENU && LOWORD (lParam) == VK_SPACE)
                    return DefWindowProcW (h, msg, wParam, lParam);
                // 최대화·이전 크기로: 운영체제 기본 동작만. JUCE 는 이것을 자기 '전체 화면'으로 바꾸고 이전 자리를 따로 기억해 되돌릴 때
                // 한 번 더 옮긴다 (F11 중에 기억한 자리로 돌아가는 버그). 전체 화면(F11) 중이면 먼저 빠져나온다
                if (cmd == SC_MAXIMIZE || cmd == SC_RESTORE)
                {
                    if (st.fullscreen && ! IsIconic (h))   // 최소화한 전체 화면을 되살리면 전체 화면 그대로
                    {
                        setFullscreen (h, false);
                        if (cmd == SC_RESTORE) return 0;
                    }
                    return DefWindowProcW (h, msg, wParam, lParam);
                }
                break;
            }
            case WM_INITMENUPOPUP:
                if (HIWORD (lParam) != 0) ++st.sysMenuOpened;   // 창 메뉴 (개발용 확인)
                break;

            case WM_SIZE:
                st.shadowResult = applyShadow (h);   // 보통 ↔ 최대화: 그림자 틀 1px 은 보통일 때만
                notifyLater();
                break;
            case WM_ACTIVATE:
            case WM_ACTIVATEAPP:
            case WM_NCACTIVATE:
                notifyLater();
                break;

            case WM_DPICHANGED:
            case WM_DISPLAYCHANGE:
            {
                // 배율·해상도가 바뀌면 틀 두께·모니터 크기도 바뀐다: JUCE 가 새 자리를 정한 뒤 틀을 다시 계산하고 상태 자리에 맞춘다
                const LRESULT r = DefSubclassProc (h, msg, wParam, lParam);
                refreshFrame (h);
                if (st.fullscreen) clipToClient (h);
                notifyLater();
                return r;
            }

            case WM_DWMCOLORIZATIONCOLORCHANGED:
            case WM_DWMCOMPOSITIONCHANGED:
            case WM_THEMECHANGED:
                applyBorderColour (h);   // 강조 색·테마를 바꿔도 #EBEBEB 그대로
                break;

            case WM_NCDESTROY:
                RemoveWindowSubclass (h, proc, subclassId);
                if (st.hwnd == h) st.hwnd = nullptr;
                break;

            default:
                break;
        }
        return DefSubclassProc (h, msg, wParam, lParam);
    }

    void rectToInts (const RECT& r, int out[4]) { out[0] = r.left; out[1] = r.top; out[2] = r.right - r.left; out[3] = r.bottom - r.top; }

    INPUT mouseInput (DWORD flags)
    {
        INPUT in {};
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = flags;
        return in;
    }

    INPUT keyInput (WORD vk, bool up)
    {
        INPUT in {};
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = vk;
        in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
        return in;
    }
}

bool install (void* handle, Hooks hooks)
{
    auto h = static_cast<HWND> (handle);
    if (h == nullptr || ! IsWindow (h)) return false;
    if ((GetWindowLongW (h, GWL_STYLE) & WS_CAPTION) != WS_CAPTION) return false;   // 운영체제 제목 줄이 있는 창만
    if (st.hwnd == h) { st.hooks = std::move (hooks); return true; }
    if (st.hwnd != nullptr) detach();

    // 내용 자리를 그대로 두고 틀만 바꾼다 (창 크기·위치를 저장해 다시 열 때 늘거나 밀리지 않게)
    RECT cr {};
    GetClientRect (h, &cr);
    MapWindowPoints (h, nullptr, reinterpret_cast<POINT*> (&cr), 2);

    st = State();
    st.hwnd = h;
    st.hooks = std::move (hooks);
    if (! SetWindowSubclass (h, proc, subclassId, 0)) { st.hwnd = nullptr; return false; }
    st.borderColourResult = applyBorderColour (h);
    st.shadowResult = applyShadow (h);

    if (zoomed (h) || IsIconic (h))
    {
        refreshFrame (h);
    }
    else
    {
        const RECT w = windowForClient (h, cr);
        SetWindowPos (h, nullptr, w.left, w.top, w.right - w.left, w.bottom - w.top, SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    return true;
}

void detach()
{
    if (st.hwnd == nullptr) return;
    HWND h = st.hwnd;
    st.hooks = {};
    if (st.fullscreen && IsWindow (h)) setFullscreen (h, false);   // 창 모양을 되돌린 뒤 뗀다
    st.hwnd = nullptr;
    if (IsWindow (h))
    {
        RemoveWindowSubclass (h, proc, subclassId);
        refreshFrame (h);
    }
}

bool installedOn (void* handle) { return handle != nullptr && st.hwnd == static_cast<HWND> (handle); }

void setFullscreen (void* handle, bool on)
{
    auto h = static_cast<HWND> (handle);
    if (h == nullptr || st.hwnd != h || on == st.fullscreen) return;
    if (on)
    {
        st.savedPlacement = {};
        st.savedPlacement.length = sizeof (WINDOWPLACEMENT);
        GetWindowPlacement (h, &st.savedPlacement);
        st.savedStyle = GetWindowLongW (h, GWL_STYLE);
        if (zoomed (h) || IsIconic (h)) ShowWindow (h, SW_RESTORE);   // 최대화 상태에서는 자리를 바꿀 수 없다 (끌 때 placement 가 다시 최대화한다)
        const HMONITOR mon = MonitorFromWindow (h, MONITOR_DEFAULTTONEAREST);
        st.fullscreen = true;
        applyBorderColour (h);   // 테두리 없음·모서리 각지게
        applyShadow (h);
        // 창 틀·제목 줄 없는 창 모양, 내용 = 모니터 전체 (작업 표시줄 자리까지). 틀 두께는 그대로라 창은 그만큼 모니터 밖으로 크다
        SetWindowLongW (h, GWL_STYLE, st.savedStyle & ~(WS_CAPTION | WS_THICKFRAME));
        const RECT w = windowForClient (h, fullscreenClient (mon));
        SetWindowPos (h, HWND_TOP, w.left, w.top, w.right - w.left, w.bottom - w.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        clipToClient (h);
        markFullscreen (h, true);
    }
    else
    {
        st.fullscreen = false;
        markFullscreen (h, false);
        SetWindowRgn (h, nullptr, FALSE);
        SetWindowLongW (h, GWL_STYLE, st.savedStyle);
        applyBorderColour (h);   // #EBEBEB·기본 모서리
        applyShadow (h);
        SetWindowPlacement (h, &st.savedPlacement);   // 켜기 전 자리·크기 (최대화였으면 최대화: 내용 = 작업 영역)
        refreshFrame (h);
        RedrawWindow (h, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
    }
    notifyLater();
}

bool isFullscreen (void* handle) { return handle != nullptr && st.hwnd == static_cast<HWND> (handle) && st.fullscreen; }

bool appDrawsBorder (void* handle)
{
    auto h = static_cast<HWND> (handle);
    return installedOn (handle) && ! isWin11() && ! st.fullscreen && ! zoomed (h) && ! IsIconic (h);
}

bool clientPixelSize (void* handle, int& w, int& h)
{
    RECT r {};
    if (handle == nullptr || ! GetClientRect (static_cast<HWND> (handle), &r)) return false;
    w = r.right - r.left;
    h = r.bottom - r.top;
    return true;
}

void frameInsets (void* handle, int out[4])
{
    // 지금 창의 실제 틀 (창 - 내용). 보통·전체 화면 = normalInsets, 최대화 = 화면 밖으로 나간 크기 조절 두께
    out[0] = out[1] = out[2] = out[3] = 0;
    auto h = static_cast<HWND> (handle);
    if (h == nullptr || ! installedOn (handle)) return;
    RECT wr {}, cr {};
    GetWindowRect (h, &wr);
    GetClientRect (h, &cr);
    POINT tl { 0, 0 };
    ClientToScreen (h, &tl);
    out[0] = tl.x - wr.left;
    out[1] = tl.y - wr.top;
    out[2] = wr.right - (tl.x + cr.right);
    out[3] = wr.bottom - (tl.y + cr.bottom);
}

void normalFrameInsets (void* handle, int out[4])
{
    out[0] = out[1] = out[2] = out[3] = 0;
    auto h = static_cast<HWND> (handle);
    if (h == nullptr) return;
    const RECT in = normalInsets (h);
    out[0] = in.left; out[1] = in.top; out[2] = in.right; out[3] = in.bottom;
}

void holdPosition (void* handle, bool on)
{
    if (installedOn (handle)) st.holdPosition = on;
}

void devRedrawNow (void* handle)
{
    // 창 프로시저를 거치는 실제 그리기 (WM_PAINT): JUCE 그림 + 윈도우 10 가장자리 1px
    if (handle != nullptr) RedrawWindow (static_cast<HWND> (handle), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}

void devPostKey (void* handle, int vk)
{
    if (handle != nullptr) PostMessageW (static_cast<HWND> (handle), WM_KEYDOWN, (WPARAM) vk, 1);
}

bool unmaximiseForSave (void* handle)
{
    auto h = static_cast<HWND> (handle);
    if (h == nullptr || ! zoomed (h)) return false;
    WINDOWPLACEMENT wp {};
    wp.length = sizeof (wp);
    GetWindowPlacement (h, &wp);
    // 보이지 않게 한 뒤 최대화를 풀고 보통 자리로 옮긴다. 창 크기는 창이 사라질 때 이 자리로 저장된다
    ShowWindow (h, SW_HIDE);
    SetWindowLongW (h, GWL_STYLE, GetWindowLongW (h, GWL_STYLE) & ~WS_MAXIMIZE);
    // rcNormalPosition 은 작업 영역 좌표 (주 모니터 작업 영역 왼쪽 위 기준)
    const auto primary = monitorInfo (MonitorFromPoint ({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));
    RECT r = wp.rcNormalPosition;
    OffsetRect (&r, primary.rcWork.left - primary.rcMonitor.left, primary.rcWork.top - primary.rcMonitor.top);
    SetWindowPos (h, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    return true;
}

bool isMaximised (void* handle) { return handle != nullptr && zoomed (static_cast<HWND> (handle)); }

bool isActive (void* handle)
{
    auto h = static_cast<HWND> (handle);
    HWND fg = GetForegroundWindow();
    if (h == nullptr || fg == nullptr) return false;
    if (fg == h || IsChild (h, fg)) return true;
    DWORD pid = 0;
    GetWindowThreadProcessId (fg, &pid);
    if (pid != GetCurrentProcessId()) return false;
    // 우리 앱의 임시 창(메뉴·툴팁·떠 있는 목록)이 앞이면 창은 활성 그대로 (JUCE 가 제목 줄을 다루는 규칙과 같다)
    return (GetWindowLongW (fg, GWL_STYLE) & WS_CAPTION) != WS_CAPTION;
}

void minimise (void* handle)       { if (handle != nullptr) PostMessageW (static_cast<HWND> (handle), WM_SYSCOMMAND, SC_MINIMIZE, 0); }
void toggleMaximise (void* handle)
{
    auto h = static_cast<HWND> (handle);
    if (h != nullptr) PostMessageW (h, WM_SYSCOMMAND, zoomed (h) ? SC_RESTORE : SC_MAXIMIZE, 0);
}
void close (void* handle)          { if (handle != nullptr) PostMessageW (static_cast<HWND> (handle), WM_SYSCOMMAND, SC_CLOSE, 0); }
void restore (void* handle)        { if (handle != nullptr) PostMessageW (static_cast<HWND> (handle), WM_SYSCOMMAND, SC_RESTORE, 0); }

Probe probe (void* handle)
{
    Probe p;
    auto h = static_cast<HWND> (handle);
    if (h == nullptr) return p;
    p.installed = installedOn (handle);
    p.maximised = zoomed (h);
    p.active = isActive (handle);
    RECT wr {}, cr {};
    GetWindowRect (h, &wr);
    GetClientRect (h, &cr);
    POINT tl { 0, 0 };
    ClientToScreen (h, &tl);
    rectToInts (wr, p.window);
    p.client[0] = tl.x; p.client[1] = tl.y; p.client[2] = cr.right; p.client[3] = cr.bottom;
    const auto mi = monitorInfo (MonitorFromWindow (h, MONITOR_DEFAULTTONEAREST));
    rectToInts (mi.rcMonitor, p.monitor);
    rectToInts (mi.rcWork, p.work);
    p.dpi = (int) dpiOf (h);
    p.resizeBorder = resizeBorder (h);
    p.ncCalc = st.ncCalc;
    p.sysMenuOpened = st.sysMenuOpened;
    p.maxClicks = st.maxClicks;
    p.notifies = st.notifies;
    p.captionPaintsBlocked = st.captionPaintsBlocked;
    frameInsets (handle, p.insets);
    p.fullscreen = st.fullscreen;
    p.win11 = isWin11();
    p.appBorder = appDrawsBorder (handle);
    p.pixelStep = pixelStep (h);
    p.shadowResult = st.shadowResult;
    p.bandPaints = st.bandPaints;
    normalFrameInsets (handle, p.normalInsets);
    if (zoomed (h) && ! st.fullscreen)
    {
        // 최대화: 창 - 크기 조절 두께 = 작업 영역인지 (Windows Terminal 방식과 같은 결과인지)
        const int b = resizeBorder (h);
        RECT mw = shrink (wr, { b, b, b, b });
        const RECT work = maximisedClient (MonitorFromWindow (h, MONITOR_DEFAULTTONEAREST));
        p.maxFrameMatchesWork = EqualRect (&mw, &work) ? 1 : 0;
    }
    p.os = juce::SystemStats::getOperatingSystemName().toStdString();
    return p;
}

FrameInfo frameInfo (void* handle)
{
    FrameInfo f;
    auto h = static_cast<HWND> (handle);
    if (h == nullptr) return f;
    if (auto fn = dwmapi<HRESULT (WINAPI*) (BOOL*)> ("DwmIsCompositionEnabled")) { BOOL b = FALSE; if (SUCCEEDED (fn (&b))) f.composition = b != FALSE; }
    if (auto fn = dwmapi<HRESULT (WINAPI*) (HWND, DWORD, PVOID, DWORD)> ("DwmGetWindowAttribute"))
    {
        BOOL nc = FALSE;
        if (SUCCEEDED (fn (h, dwmNcRenderingEnabled, &nc, sizeof (nc)))) f.ncRendering = nc != FALSE;
        RECT r {};
        if (SUCCEEDED (fn (h, dwmExtendedFrameBounds, &r, sizeof (r)))) rectToInts (r, f.frameBounds);
    }
    if (auto fn = dwmapi<HRESULT (WINAPI*) (DWORD*, BOOL*)> ("DwmGetColorizationColor")) { DWORD c = 0; BOOL op = FALSE; if (SUCCEEDED (fn (&c, &op))) f.colorization = c; }
    DWORD v = 0, size = sizeof (v);
    if (RegGetValueW (HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", L"ColorPrevalence", RRF_RT_REG_DWORD, nullptr, &v, &size) == ERROR_SUCCESS)
        f.colorPrevalence = (int) v;
    f.build = windowsBuild();
    f.style = (unsigned) GetWindowLongW (h, GWL_STYLE);
    f.exStyle = (unsigned) GetWindowLongW (h, GWL_EXSTYLE);
    f.classStyle = (unsigned) GetClassLongW (h, GCL_STYLE);
    f.borderColourResult = st.hwnd == h ? st.borderColourResult : 1;
    return f;
}

int devHitTest (void* handle, int x, int y)
{
    auto h = static_cast<HWND> (handle);
    return h != nullptr ? (int) SendMessageW (h, WM_NCHITTEST, 0, MAKELPARAM ((short) x, (short) y)) : 0;
}

std::vector<unsigned> devCaptureScreen (int x, int y, int w, int h)
{
    std::vector<unsigned> out;
    if (w <= 0 || h <= 0) return out;
    HDC screen = GetDC (nullptr);
    if (screen == nullptr) return out;
    HDC mem = CreateCompatibleDC (screen);
    BITMAPINFO bi {};
    bi.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;   // 위에서 아래로
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection (screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bmp != nullptr && mem != nullptr && bits != nullptr)
    {
        auto old = SelectObject (mem, bmp);
        if (BitBlt (mem, 0, 0, w, h, screen, x, y, SRCCOPY | CAPTUREBLT))
        {
            GdiFlush();
            const auto* src = static_cast<const unsigned*> (bits);
            out.assign (src, src + (size_t) w * (size_t) h);
            for (auto& c : out) c = 0xFF000000u | (c & 0x00FFFFFFu);
        }
        SelectObject (mem, old);
    }
    if (bmp != nullptr) DeleteObject (bmp);
    if (mem != nullptr) DeleteDC (mem);
    ReleaseDC (nullptr, screen);
    return out;
}

std::string devScreenPixels (int x, int y, int w, int h)
{
    std::string out;
    char buf[16];
    for (auto c : devCaptureScreen (x, y, w, h))
    {
        std::snprintf (buf, sizeof (buf), "%02X%02X%02X ", (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
        out += buf;
    }
    return out;
}

int devScaleSteps (int steps, bool query)
{
    // SPI_GETLOGICALDPIOVERRIDE / SPI_SETLOGICALDPIOVERRIDE (설정 앱의 배율 목록과 같은 길, 문서에 없음): 권장 배율 기준 단계
    constexpr UINT spiGet = 0x009E, spiSet = 0x009F;
    if (! query) SystemParametersInfoW (spiSet, (UINT) steps, nullptr, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    int now = INT_MIN;
    SystemParametersInfoW (spiGet, 0, &now, 0);
    return now;
}

void devMouse (int x, int y, bool down, bool up)
{
    SetCursorPos (x, y);
    INPUT ins[2];
    int n = 0;
    if (down) ins[n++] = mouseInput (MOUSEEVENTF_LEFTDOWN);
    if (up)   ins[n++] = mouseInput (MOUSEEVENTF_LEFTUP);
    if (n == 0) { INPUT mv = mouseInput (MOUSEEVENTF_MOVE); SendInput (1, &mv, sizeof (INPUT)); return; }
    SendInput ((UINT) n, ins, sizeof (INPUT));
}

void devAltSpace()
{
    INPUT ins[4] = { keyInput (VK_MENU, false), keyInput (VK_SPACE, false), keyInput (VK_SPACE, true), keyInput (VK_MENU, true) };
    SendInput (4, ins, sizeof (INPUT));
}

void devKey (int vk)
{
    INPUT ins[2] = { keyInput ((WORD) vk, false), keyInput ((WORD) vk, true) };
    SendInput (2, ins, sizeof (INPUT));
}

void devEscape()
{
    INPUT ins[2] = { keyInput (VK_ESCAPE, false), keyInput (VK_ESCAPE, true) };
    SendInput (2, ins, sizeof (INPUT));
}

} // namespace meon::winframe

#endif
