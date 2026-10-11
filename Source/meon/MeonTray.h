// MEON - 독립 앱 상태 표시 아이콘 (macOS 메뉴 막대 / Windows 알림 영역)
// 화면 디자인 없이 OS 기본 메뉴를 쓴다: 송출 정지 · 전체 음소거 | 종료.
// macOS 는 아이콘을 누르면 메뉴, Windows 는 오른쪽 클릭 메뉴 · 왼쪽 클릭은 창을 앞으로.
// JUCE 의 SystemTrayIconComponent 는 macOS 에서 메뉴 막대 모델(MenuBarModel) 없이 메뉴를 띄우면
// 메뉴 항목이 동작하지 않고 메뉴 갱신 때 죽을 수 있어서 직접 만든다.
// 메뉴 동작은 모두 메시지 스레드에서 불린다.
#pragma once

#include <functional>
#include <memory>

namespace meon
{

class TrayIcon
{
public:
    struct Callbacks
    {
        std::function<bool()> isSendMuted, isOutputMuted;
        std::function<void (bool)> setSendMuted, setOutputMuted;
        std::function<void()> showWindow, quit;
    };

    /** 지원하지 않는 플랫폼이면 nullptr */
    static std::unique_ptr<TrayIcon> create (Callbacks callbacks);
    virtual ~TrayIcon() = default;

protected:
    TrayIcon() = default;
};

#if ! defined (__APPLE__) && ! defined (_WIN32)
inline std::unique_ptr<TrayIcon> TrayIcon::create (Callbacks) { return {}; }
#endif

} // namespace meon
