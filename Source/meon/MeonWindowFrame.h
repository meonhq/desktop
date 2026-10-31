// MEON - 윈도우 독립 앱 창의 윗부분 (홈·사이드바 디자인 v2.1, os = 윈도우). 맥은 MeonWindowChrome (빈 툴바), 플러그인은 쓰지 않는다 (창 주인이 DAW).
//   운영체제 창 틀은 그대로 두고(JUCE 가 그리는 테두리 없는 창으로 바꾸지 않는다) 제목 줄 자리만 앱 내용으로 덮는다:
//     WM_NCCALCSIZE  제목 줄 영역을 내용으로 쓴다 (왼쪽·오른쪽·아래 틀은 운영체제 그대로 → 크기 조절·그림자·윈도우 11 둥근 모서리)
//                    위 틀은 늘 0 (JUCE 가 틀 두께를 한 번 읽어 두고 계속 쓰므로 상태마다 바뀌면 안 된다).
//                    최대화하면 창 위가 화면 밖으로 틀 두께만큼 나가므로 그만큼(hiddenTop)은 앱이 비워 두고 그 아래부터 그린다
//     WM_NCHITTEST   맨 위 틀 두께는 위쪽 크기 조절, 제목 줄 빈 곳은 HTCAPTION (끌기·더블클릭·화면 끝 붙이기·오른쪽 클릭 메뉴),
//                    최대화 버튼은 HTMAXBUTTON (윈도우 11 스냅 레이아웃), 그 밖(버튼·아이콘)은 앱 화면
//   창 프로시저는 서브클래스로 JUCE 앞에 끼운다. 창 버튼 셋은 앱이 그린다 (ui/shell/WindowChromeView: 윈도우 11 모양).
//   macOS·리눅스: 아무 일도 하지 않는다 (MeonWindowFrame.cpp).
#pragma once

#include <functional>
#include <string>

namespace meon::winframe
{

/** 맨 윗줄 판정 (앱이 정한다) */
enum class Region { client, caption, maxButton };

struct Hooks
{
    /** 화면 물리 좌표 (x, y) 가 앱의 어느 자리인지. 없으면 모두 client */
    std::function<Region (int screenX, int screenY)> hitTest;
    /** 최대화·활성 상태가 바뀐 뒤 (메시지 스레드, 비동기) */
    std::function<void()> onStateChange;
    /** 최대화 버튼 표시: 0 보통, 1 마우스 올림, 2 누름. 이 버튼은 운영체제 영역(HTMAXBUTTON)이라 앱이 마우스를 받지 못해 여기서 알려 준다 */
    std::function<void (int)> onMaxButton;
};

/** hwnd: juce::ComponentPeer::getNativeHandle(). 붙였으면 true (윈도우만). 붙일 때 내용 자리(화면 위치·크기)는 그대로 두고 창 틀만 바꾼다 */
bool install (void* hwnd, Hooks);
/** 창을 더 지켜보지 않는다 (에디터가 사라질 때). 창 프로시저를 원래대로 돌리고 틀도 되돌린다 */
void detach();
bool installedOn (void* hwnd);
/** 최대화했을 때 내용 위쪽이 화면(작업 영역) 밖으로 나간 높이 (물리 px). 보통 창은 0. 앱은 이만큼 비우고 그 아래부터 배치한다 */
int hiddenTop (void* hwnd);
bool isMaximised (void* hwnd);
/** 창이 활성인지 (우리 앱의 메뉴·툴팁 같은 임시 창이 앞에 있어도 활성) */
bool isActive (void* hwnd);
/** 창 버튼: 운영체제 창 버튼과 같은 길 (WM_SYSCOMMAND) */
void minimise (void* hwnd);
void toggleMaximise (void* hwnd);
void close (void* hwnd);

/** 개발용 (화면 자가 테스트 --uitest=winchrome, windows.yml): 지금 창 상태. 좌표는 화면 물리 px */
struct Probe
{
    bool installed = false, maximised = false, active = false;
    int window[4] {}, client[4] {}, monitor[4] {}, work[4] {};   // x, y, w, h
    int dpi = 0, resizeBorder = 0;
    int ncCalc = 0, sysMenuOpened = 0, maxClicks = 0;   // WM_NCCALCSIZE 처리 횟수, 시스템 메뉴가 열린 횟수, 최대화 버튼 누름
    std::string os;
};
Probe probe (void* hwnd);
/** 개발용: 화면 물리 좌표의 WM_NCHITTEST 결과 (HTCLIENT 1, HTCAPTION 2, HTMAXBUTTON 9, HTTOP 12 …) */
int devHitTest (void* hwnd, int screenX, int screenY);
/** 개발용: 화면 물리 좌표 사각형의 픽셀 (0xRRGGBB, 위→아래·왼→오른, 최대 w*h 개) */
std::string devScreenPixels (int x, int y, int w, int h);
/** 개발용: 마우스를 (x, y) 로 옮기고 왼쪽 버튼 누름(down)·뗌(up) 을 보낸다 (SendInput: 실제 입력과 같은 길) */
void devMouse (int screenX, int screenY, bool down, bool up);
/** 개발용: Alt+Space 를 누른 것처럼 (SendInput), Esc */
void devAltSpace();
void devEscape();

} // namespace meon::winframe
