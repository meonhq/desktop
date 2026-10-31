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
        long borderColourResult = 1;
        bool fullscreen = false;            // F11 전체 화면
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

    /** 크기 조절 틀 두께 (물리 px): 운영체제가 쓰는 값 그대로 (100% 에서 4 + 4 = 8) */
    int resizeBorder (HWND h)
    {
        const UINT dpi = dpiOf (h);
        return metricForDpi (SM_CYSIZEFRAME, dpi) + metricForDpi (SM_CXPADDEDBORDER, dpi);
    }

    bool zoomed (HWND h) { return IsZoomed (h) != FALSE; }

    // DWM (dwmapi.dll 은 직접 링크하지 않고 불러 쓴다: 빌드 기준이 윈도우 7)
    template <typename Fn> Fn dwmapi (const char* name)
    {
        static HMODULE lib = LoadLibraryW (L"dwmapi.dll");
        return lib != nullptr ? reinterpret_cast<Fn> (reinterpret_cast<void*> (GetProcAddress (lib, name))) : nullptr;
    }
    constexpr DWORD dwmNcRenderingEnabled = 1, dwmExtendedFrameBounds = 9, dwmBorderColor = 34;   // DWMWA_*

    /** 윈도우 11: 창 테두리 색을 #EBEBEB 로 (강조 색 설정과 상관없이). 윈도우 10 은 이 속성이 없어 실패하고 운영체제 테두리 그대로 */
    long applyBorderColour (HWND h)
    {
        static auto set = dwmapi<HRESULT (WINAPI*) (HWND, DWORD, LPCVOID, DWORD)> ("DwmSetWindowAttribute");
        if (set == nullptr) return -1;
        const COLORREF c = RGB ((borderColourRGB >> 16) & 0xFF, (borderColourRGB >> 8) & 0xFF, borderColourRGB & 0xFF);
        return (long) set (h, dwmBorderColor, &c, sizeof (c));
    }

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
        if (st.fullscreen) return HTCLIENT;   // 전체 화면: 끌기·크기 조절 없음. 맨 위에 나타나는 창 버튼은 앱이 받는다
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
    st.borderColourResult = applyBorderColour (h);

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
    if (st.fullscreen && IsWindow (h)) setFullscreen (h, false);   // 창 모양을 되돌린 뒤 뗀다
    st.hwnd = nullptr;
    if (IsWindow (h))
    {
        RemoveWindowSubclass (h, proc, subclassId);
        SetWindowPos (h, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
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
        if (zoomed (h)) ShowWindow (h, SW_RESTORE);   // 최대화 상태에서는 자리를 바꿀 수 없다 (끌 때 placement 가 다시 최대화한다)
        MONITORINFO mi {};
        mi.cbSize = sizeof (mi);
        GetMonitorInfoW (MonitorFromWindow (h, MONITOR_DEFAULTTONEAREST), &mi);
        st.fullscreen = true;
        // 창 틀·제목 줄 없이 모니터 전체 (작업 표시줄 위까지): 앞 창이 모니터를 덮으면 윈도우가 작업 표시줄을 내린다
        SetWindowLongW (h, GWL_STYLE, st.savedStyle & ~(WS_CAPTION | WS_THICKFRAME));
        SetWindowPos (h, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                      SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    else
    {
        st.fullscreen = false;
        SetWindowLongW (h, GWL_STYLE, st.savedStyle);
        SetWindowPlacement (h, &st.savedPlacement);   // 켜기 전 자리·크기 (최대화였으면 최대화)
        SetWindowPos (h, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    notifyLater();
}

bool isFullscreen (void* handle) { return handle != nullptr && st.hwnd == static_cast<HWND> (handle) && st.fullscreen; }

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
    p.fullscreen = st.fullscreen;
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
    wchar_t buildText[32] {};
    DWORD bsize = sizeof (buildText);
    if (RegGetValueW (HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"CurrentBuildNumber", RRF_RT_REG_SZ, nullptr, buildText, &bsize) == ERROR_SUCCESS)
        f.build = _wtoi (buildText);
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
