// MEON - 독립 앱 상태 표시 아이콘 (Windows 알림 영역). 설명은 MeonTray.h
#ifdef _WIN32

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include "MeonTray.h"

#pragma comment (lib, "shell32.lib")

namespace meon
{

namespace
{
    constexpr UINT trayMessage = WM_APP + 0x4d;   // 아이콘 클릭 알림
    enum MenuId : UINT { idSend = 1, idOutput, idQuit };

    class TrayWin final : public TrayIcon
    {
    public:
        explicit TrayWin (Callbacks c) : callbacks (std::move (c))
        {
            static const wchar_t* className = L"MEON_TrayWindow";
            WNDCLASSEXW wc = {};
            wc.cbSize = sizeof (wc);
            wc.lpfnWndProc = wndProc;
            wc.hInstance = GetModuleHandleW (nullptr);
            wc.lpszClassName = className;
            RegisterClassExW (&wc);   // 이미 등록돼 있으면 실패해도 그대로 쓴다

            // 보이지 않는 최상위 창 (메시지 전용 창은 Explorer 재시작 알림 TaskbarCreated 를 못 받는다)
            hwnd = CreateWindowExW (0, className, L"MEON", 0, 0, 0, 0, 0, nullptr, nullptr, wc.hInstance, nullptr);
            if (hwnd == nullptr)
                return;
            SetWindowLongPtrW (hwnd, GWLP_USERDATA, (LONG_PTR) this);

            taskbarCreated = RegisterWindowMessageW (L"TaskbarCreated");
            ChangeWindowMessageFilterEx (hwnd, taskbarCreated, MSGFLT_ALLOW, nullptr);

            // 실행 파일의 앱 아이콘(.ico, 작은 크기는 M 만 있는 그림)에서 알림 영역 크기에 맞는 것을 고른다
            icon = (HICON) LoadImageW (wc.hInstance, L"IDI_ICON1", IMAGE_ICON,
                                       GetSystemMetrics (SM_CXSMICON), GetSystemMetrics (SM_CYSMICON), LR_DEFAULTCOLOR);
            ownsIcon = icon != nullptr;
            if (icon == nullptr)
                icon = LoadIconW (nullptr, IDI_APPLICATION);   // 공유 아이콘이라 지우지 않는다

            data.cbSize = sizeof (data);
            data.hWnd = hwnd;
            data.uID = 1;
            data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
            data.uCallbackMessage = trayMessage;
            data.hIcon = icon;
            lstrcpynW (data.szTip, L"MEON", ARRAYSIZE (data.szTip));
            Shell_NotifyIconW (NIM_ADD, &data);
        }

        ~TrayWin() override
        {
            if (hwnd != nullptr)
            {
                Shell_NotifyIconW (NIM_DELETE, &data);
                SetWindowLongPtrW (hwnd, GWLP_USERDATA, 0);
                DestroyWindow (hwnd);
            }
            if (ownsIcon)
                DestroyIcon (icon);
        }

    private:
        Callbacks callbacks;
        HWND hwnd = nullptr;
        HICON icon = nullptr;
        bool ownsIcon = false;
        UINT taskbarCreated = 0;
        NOTIFYICONDATAW data = {};

        void showMenu()
        {
            HMENU menu = CreatePopupMenu();
            if (menu == nullptr)
                return;
            AppendMenuW (menu, MF_STRING | (callbacks.isSendMuted()   ? MF_CHECKED : MF_UNCHECKED), idSend,   L"송출 정지");
            AppendMenuW (menu, MF_STRING | (callbacks.isOutputMuted() ? MF_CHECKED : MF_UNCHECKED), idOutput, L"전체 음소거");
            AppendMenuW (menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW (menu, MF_STRING, idQuit, L"종료");

            POINT pt;
            GetCursorPos (&pt);
            // 메뉴 밖을 누르면 닫히도록 먼저 앞으로 가져오고, 끝나면 빈 메시지를 보낸다 (TrackPopupMenu 문서의 권장 방법)
            SetForegroundWindow (hwnd);
            const UINT flags = TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON
                             | (GetSystemMetrics (SM_MENUDROPALIGNMENT) != 0 ? TPM_RIGHTALIGN : TPM_LEFTALIGN);
            const auto chosen = (UINT) TrackPopupMenuEx (menu, flags, pt.x, pt.y, hwnd, nullptr);
            PostMessageW (hwnd, WM_NULL, 0, 0);
            DestroyMenu (menu);

            switch (chosen)
            {
                case idSend:   callbacks.setSendMuted (! callbacks.isSendMuted()); break;
                case idOutput: callbacks.setOutputMuted (! callbacks.isOutputMuted()); break;
                case idQuit:   PostMessageW (hwnd, WM_APP + 1, 0, 0); break;   // 메뉴를 다 닫은 뒤에 (종료 과정에서 이 아이콘이 지워진다)
                default: break;
            }
        }

        LRESULT handle (UINT msg, WPARAM wParam, LPARAM lParam)
        {
            if (msg == trayMessage)
            {
                switch (LOWORD (lParam))
                {
                    case WM_RBUTTONUP:
                    case WM_CONTEXTMENU: showMenu(); break;
                    case WM_LBUTTONUP:   if (callbacks.showWindow) callbacks.showWindow(); break;
                    default: break;
                }
                return 0;
            }
            if (msg == WM_APP + 1)
            {
                if (callbacks.quit)
                    callbacks.quit();
                return 0;
            }
            if (msg == taskbarCreated && taskbarCreated != 0)
            {
                // Explorer 가 다시 켜지면 아이콘을 다시 올린다
                Shell_NotifyIconW (NIM_ADD, &data);
                return 0;
            }
            return DefWindowProcW (hwnd, msg, wParam, lParam);
        }

        static LRESULT CALLBACK wndProc (HWND h, UINT msg, WPARAM wParam, LPARAM lParam)
        {
            if (auto* self = reinterpret_cast<TrayWin*> (GetWindowLongPtrW (h, GWLP_USERDATA)))
                return self->handle (msg, wParam, lParam);
            return DefWindowProcW (h, msg, wParam, lParam);
        }
    };
}

std::unique_ptr<TrayIcon> TrayIcon::create (Callbacks callbacks)
{
    return std::make_unique<TrayWin> (std::move (callbacks));
}

} // namespace meon

#endif
