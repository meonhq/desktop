// MEON - 윈도우 독립 앱 창의 윗부분: 운영체제 창 틀은 두고 제목 줄 자리만 앱 내용으로 덮는다. 설명은 MeonWindowFrame.h
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
#include <algorithm>
#include <JuceHeader.h>

// 빌드는 WINVER 0x0601 (윈도우 7) 기준이라 윈도우 8 이후 상수는 직접 둔다
#ifndef ABM_GETAUTOHIDEBAREX
 #define ABM_GETAUTOHIDEBAREX 0x0000000b
#endif

#if defined (_MSC_VER)
 #pragma comment (lib, "comctl32.lib")
 #pragma comment (lib, "shell32.lib")
#endif

namespace meon::winframe
{

namespace
{
    constexpr UINT_PTR subclassId = 0x4D454F4E;   // 'MEON'

    struct State
    {
        HWND hwnd = nullptr;
        Hooks hooks;
        int maxState = 0;              // 최대화 버튼 표시 (0 보통, 1 올림, 2 누름)
        bool maxPressed = false, trackingNc = false, notifyPending = false;
        int ncCalc = 0, sysMenuOpened = 0, maxClicks = 0, notifies = 0;
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

    /** 크기 조절 틀 두께 (물리 px): 운영체제가 쓰는 값 그대로 (100% 에서 4 + 4 = 8) */
    int resizeBorder (HWND h)
    {
        const UINT dpi = dpiOf (h);
        return metricForDpi (SM_CYSIZEFRAME, dpi) + metricForDpi (SM_CXPADDEDBORDER, dpi);
    }

    bool zoomed (HWND h) { return IsZoomed (h) != FALSE; }

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

    /** 최대화했을 때 작업 표시줄이 자동 숨김이면 그 쪽 가장자리를 1px 비운다 (꽉 채우면 작업 표시줄이 다시 나오지 않는다) */
    void leaveRoomForAutoHideTaskbar (HWND h, RECT& r)
    {
        APPBARDATA abd {};
        abd.cbSize = sizeof (abd);
        if ((SHAppBarMessage (ABM_GETSTATE, &abd) & ABS_AUTOHIDE) == 0) return;
        HMONITOR mon = MonitorFromWindow (h, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi {};
        mi.cbSize = sizeof (mi);
        if (! GetMonitorInfoW (mon, &mi)) return;
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

    LRESULT hitTest (HWND h, WPARAM wParam, LPARAM lParam)
    {
        // 왼쪽·오른쪽·아래 틀은 운영체제 판정 그대로 (HTLEFT·HTBOTTOMRIGHT …)
        const LRESULT def = DefSubclassProc (h, WM_NCHITTEST, wParam, lParam);
        if (def != HTCLIENT) return def;
        const int x = GET_X_LPARAM (lParam), y = GET_Y_LPARAM (lParam);
        RECT wr;
        GetWindowRect (h, &wr);
        // 맨 위 틀 두께: 위쪽 크기 조절 (최대화하면 없음). 양 끝은 모서리
        if (! zoomed (h))
        {
            const int b = resizeBorder (h);
            if (y < wr.top + b)
            {
                if (x < wr.left + b * 2) return HTTOPLEFT;
                if (x >= wr.right - b * 2) return HTTOPRIGHT;
                return HTTOP;
            }
        }
        if (st.hooks.hitTest)
        {
            switch (st.hooks.hitTest (x, y))
            {
                case Region::caption:   return HTCAPTION;
                case Region::maxButton: return HTMAXBUTTON;
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
                if (wParam == TRUE)
                {
                    auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*> (lParam);
                    const LONG top = p->rgrc[0].top;
                    // 운영체제가 표준 틀을 계산하게 두고 (왼쪽·오른쪽·아래), 위(제목 줄 + 위 틀)만 내용으로 쓴다.
                    // 위 틀은 최대화해도 0 으로 둔다: JUCE 는 틀 두께를 한 번 읽어 두고 좌표 계산에 계속 쓰므로 상태마다 달라지면 마우스가 어긋난다.
                    // 최대화하면 창 위가 화면 밖으로 틀 두께만큼 나가므로 그만큼은 앱이 비워 둔다 (hiddenTop)
                    const LRESULT r = DefSubclassProc (h, msg, wParam, lParam);
                    p->rgrc[0].top = top;
                    if (zoomed (h)) leaveRoomForAutoHideTaskbar (h, p->rgrc[0]);
                    ++st.ncCalc;
                    return r;
                }
                break;

            case WM_NCHITTEST:
                return hitTest (h, wParam, lParam);

            // 최대화 버튼 (HTMAXBUTTON): 운영체제가 그리지 않게 누름·뗌은 여기서 끝내고, 표시는 앱이 그린다
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

            case WM_SYSCOMMAND:
                // Alt+Space (시스템 메뉴): JUCE 는 키보드로 연 창 메뉴를 막는다 (Alt 를 수정 키로 쓰는 앱 때문). 스페이스만 운영체제에 넘긴다
                if ((wParam & 0xFFF0) == SC_KEYMENU && LOWORD (lParam) == VK_SPACE)
                    return DefWindowProcW (h, msg, wParam, lParam);
                break;
            case WM_INITMENUPOPUP:
                if (HIWORD (lParam) != 0) ++st.sysMenuOpened;   // 창 메뉴 (개발용 확인)
                break;

            case WM_ACTIVATE:
            case WM_ACTIVATEAPP:
            case WM_NCACTIVATE:
            case WM_SIZE:
                notifyLater();
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

    // 내용 자리를 그대로 두고 틀만 바꾼다: 위 틀(제목 줄 포함) 높이만큼 창 위를 내린다 (창 크기·위치를 저장해 다시 열 때 늘거나 밀리지 않게)
    RECT wr {}, cr {};
    GetWindowRect (h, &wr);
    GetClientRect (h, &cr);
    POINT tl { cr.left, cr.top };
    ClientToScreen (h, &tl);
    const int topFrame = tl.y - wr.top;

    st = State();
    st.hwnd = h;
    st.hooks = std::move (hooks);
    if (! SetWindowSubclass (h, proc, subclassId, 0)) { st.hwnd = nullptr; return false; }

    if (zoomed (h) || IsIconic (h))
        SetWindowPos (h, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    else
        SetWindowPos (h, nullptr, wr.left, wr.top + topFrame, wr.right - wr.left, (wr.bottom - wr.top) - topFrame,
                      SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE);
    return true;
}

void detach()
{
    if (st.hwnd == nullptr) return;
    HWND h = st.hwnd;
    st.hooks = {};
    st.hwnd = nullptr;
    if (IsWindow (h))
    {
        RemoveWindowSubclass (h, proc, subclassId);
        SetWindowPos (h, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

bool installedOn (void* handle) { return handle != nullptr && st.hwnd == static_cast<HWND> (handle); }

int hiddenTop (void* handle)
{
    auto h = static_cast<HWND> (handle);
    if (h == nullptr || ! zoomed (h)) return 0;
    MONITORINFO mi {};
    mi.cbSize = sizeof (mi);
    if (! GetMonitorInfoW (MonitorFromWindow (h, MONITOR_DEFAULTTONEAREST), &mi)) return 0;
    POINT tl { 0, 0 };
    ClientToScreen (h, &tl);
    return std::max (0, (int) (mi.rcWork.top - tl.y));
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
    MONITORINFO mi {};
    mi.cbSize = sizeof (mi);
    if (GetMonitorInfoW (MonitorFromWindow (h, MONITOR_DEFAULTTONEAREST), &mi)) { rectToInts (mi.rcMonitor, p.monitor); rectToInts (mi.rcWork, p.work); }
    p.dpi = (int) dpiOf (h);
    p.resizeBorder = resizeBorder (h);
    p.ncCalc = st.ncCalc;
    p.sysMenuOpened = st.sysMenuOpened;
    p.maxClicks = st.maxClicks;
    p.notifies = st.notifies;
    p.os = juce::SystemStats::getOperatingSystemName().toStdString();
    return p;
}

int devHitTest (void* handle, int x, int y)
{
    auto h = static_cast<HWND> (handle);
    return h != nullptr ? (int) SendMessageW (h, WM_NCHITTEST, 0, MAKELPARAM ((short) x, (short) y)) : 0;
}

std::string devScreenPixels (int x, int y, int w, int h)
{
    std::string out;
    HDC dc = GetDC (nullptr);
    if (dc == nullptr) return out;
    char buf[16];
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i)
        {
            const COLORREF c = GetPixel (dc, x + i, y + j);
            std::snprintf (buf, sizeof (buf), "%02X%02X%02X ", GetRValue (c), GetGValue (c), GetBValue (c));
            out += buf;
        }
    ReleaseDC (nullptr, dc);
    return out;
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

void devEscape()
{
    INPUT ins[2] = { keyInput (VK_ESCAPE, false), keyInput (VK_ESCAPE, true) };
    SendInput (2, ins, sizeof (INPUT));
}

} // namespace meon::winframe

#endif
