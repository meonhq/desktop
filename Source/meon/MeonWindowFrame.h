// MEON - 윈도우 독립 앱 창 틀 (홈·사이드바 디자인 v2.1, os = 윈도우). 맥은 MeonWindowChrome (빈 툴바), 플러그인은 쓰지 않는다 (창 주인이 DAW).
//   운영체제 창(크기 조절·그림자·화면 끝 붙이기·창 메뉴)은 그대로 두고, 제목 줄 자리만 앱 내용으로 덮는다. 창 버튼 셋은 앱이 그린다
//   (ui/shell/WindowChromeView: 윈도우 11 모양). 창 프로시저는 서브클래스로 JUCE 앞에 끼운다. 맥·리눅스는 아무 일도 하지 않는다.
//
//   창 틀 계산은 MeonWindowFrame_win.cpp 맨 위 한 곳에서만 한다 (clientFromWindow · normalInsets · 상태별 내용 자리).
//   참고한 공개 구현: Windows Terminal (NonClientIslandWindow::_OnNcCalcSize), melak47/BorderlessWindow
//     내용 자리  보통 = 창 - 틀 / 최대화 = 그 모니터의 작업 영역과 정확히 같게 (운영체제가 창을 크기 조절 두께
//                (SM_CXFRAME + SM_CXPADDEDBORDER, 창 DPI)만큼 화면 밖으로 크게 두므로 내용 = 창 - 그 두께) / 전체 화면(F11) = 모니터
//     틀 두께    보통·전체 화면: 윈도우 11 = 옆·아래 크기 조절 두께, 위 0 (운영체제 1px 테두리 = #EBEBEB, DWMWA_BORDER_COLOR).
//                윈도우 10 = 0 (내용이 창 전체, 운영체제 테두리 #707070 은 없고 앱이 네 변 1px #EBEBEB, 그림자는 DwmExtendFrameIntoClientArea 1px).
//                최대화: 화면 밖으로 나간 크기 조절 두께. JUCE 는 틀 두께를 자기 크기 변경 때만 다시 읽으므로, 상태가 바뀌면
//                앱이 창을 움직이지 않고 다시 읽게 한다 (holdPosition, MeonEditor::checkJuceFrame)
//     상태 바뀜  WM_WINDOWPOSCHANGED (JUCE 처리 뒤)에서 본다. JUCE 가 이 메시지에서 DefWindowProc 를 부르지 않아 WM_SIZE 는 오지 않는다
//     크기       보통 창의 내용 크기는 배율에서 논리 px 정수로 떨어지는 물리 px 의 배수 (125% 5, 150% 3): JUCE 가 아는 크기 = 실제
//     크기 조절  창 바깥 끝에서 크기 조절 두께 안쪽 (윈도우 10 은 내용 끝, 위는 내용 맨 위 줄). 최대화·전체 화면은 없음
//     창 버튼    운영체제 창 버튼은 어떤 상태에서도 그려지지 않는다: 최소화·닫기는 늘 앱 화면(HTCLIENT), 최대화는 윈도우 11 에서만
//                HTMAXBUTTON (스냅 레이아웃), 윈도우 10 은 앱 화면. 제목 줄을 그리는 숨은 메시지(WM_NCUAHDRAW*)는 막는다
//     최대화     운영체제 기본 동작 (WM_SYSCOMMAND 를 JUCE 의 '전체 화면' 처리에 넘기지 않는다: 두 곳이 자리를 기억하면 서로 덮어쓴다)
//     전체 화면  F11 한 가지 방식만: 창 모양(틀·제목 줄 없음)으로 바꾸고 내용 = 모니터. 이 상태의 자리는 이 파일이 지킨다
#pragma once

#include <functional>
#include <string>
#include <vector>

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
bool isMaximised (void* hwnd);
/** 창이 활성인지 (우리 앱의 메뉴·툴팁 같은 임시 창이 앞에 있어도 활성) */
bool isActive (void* hwnd);
/** F11 전체 화면 (독립 앱): 창 틀·작업 표시줄까지 덮는다 (모니터 전체, 창 틀 없음). 끌 때 켜기 전 자리·크기·최대화로 되돌린다.
    F11 은 창 프로시저에서 받는다 (글자 입력 중에도). 상태가 바뀌면 onStateChange */
void setFullscreen (void* hwnd, bool);
bool isFullscreen (void* hwnd);
/** 윈도우 10 보통 창: 앱이 내용 네 변 끝 1px 에 #EBEBEB 를 그린다 (운영체제 테두리 대신). 최대화·전체 화면·윈도우 11 은 false */
bool appDrawsBorder (void* hwnd);
/** 내용 크기 (물리 px). 창이 없으면 false */
bool clientPixelSize (void* hwnd, int& w, int& h);
/** 지금 창의 실제 틀 두께 (창 - 내용, 물리 px): 왼쪽, 위, 오른쪽, 아래. 앱이 JUCE 가 기억한 두께와 맞는지 확인할 때 */
void frameInsets (void* hwnd, int out[4]);
/** 보통 창(과 전체 화면)의 틀 두께 (물리 px). 윈도우 10 은 0, 윈도우 11 은 옆·아래 크기 조절 두께 */
void normalFrameInsets (void* hwnd, int out[4]);
/** true 인 동안 창 자리·크기 바꾸기를 막는다: JUCE 에게 틀 두께를 다시 읽게 할 때 (JUCE 는 자기 크기 변경 때만 읽는다) */
void holdPosition (void* hwnd, bool);
/** 앱을 끌 때: 최대화였으면 보이지 않게 한 뒤 보통 자리로 되돌린다 (창 크기는 창이 사라질 때 저장되므로). 최대화였으면 true */
bool unmaximiseForSave (void* hwnd);

/** 창 테두리 색: 디자인 #EBEBEB 로 늘 (강조 색 설정과 상관없이, 사용자 결정 2026-10-31).
    윈도우 11 은 운영체제 테두리 색(DWMWA_BORDER_COLOR), 윈도우 10 은 앱이 그린다 (appDrawsBorder) */
constexpr unsigned borderColourRGB = 0xEBEBEB;
/** 창 버튼: 운영체제 창 버튼과 같은 길 (WM_SYSCOMMAND) */
void minimise (void* hwnd);
void toggleMaximise (void* hwnd);
/** 최소화에서 되돌리기 (최대화였으면 최대화로) */
void restore (void* hwnd);
void close (void* hwnd);

/** 개발용 (화면 자가 테스트 --uitest=winchrome, windows.yml): 지금 창 상태. 좌표는 화면 물리 px */
struct Probe
{
    bool installed = false, maximised = false, active = false;
    int window[4] {}, client[4] {}, monitor[4] {}, work[4] {};   // x, y, w, h
    int dpi = 0, resizeBorder = 0;
    int ncCalc = 0, sysMenuOpened = 0, maxClicks = 0, notifies = 0;   // WM_NCCALCSIZE 처리 횟수, 창 메뉴가 열린 횟수, 최대화 버튼 누름, 상태 알림 횟수
    int captionPaintsBlocked = 0;     // 운영체제가 제목 줄을 그리려다 막힌 횟수 (WM_NCUAHDRAWCAPTION·FRAME)
    int insets[4] {};                 // 틀 두께 (왼쪽, 위, 오른쪽, 아래)
    bool fullscreen = false, win11 = false, appBorder = false;
    int normalInsets[4] {};           // 보통 창의 틀 두께 (기대값)
    int pixelStep = 1;                // 내용 크기 단위 (논리 px 정수로 떨어지는 물리 px)
    long shadowResult = 1;            // DwmExtendFrameIntoClientArea 결과 (0 = 됨)
    int bandPaints = 0;               // 윈도우 10 가장자리 1px 을 불투명으로 다시 놓은 횟수
    int modeChanges = 0;              // 상태(보통·최대화·전체 화면·최소화)가 바뀐 것을 본 횟수 (WM_WINDOWPOSCHANGED)
    int maxFrameMatchesWork = -1;     // 최대화: 창 - 크기 조절 두께 = 작업 영역 (1 같음, 0 다름, -1 최대화 아님)
    std::string os;
};
Probe probe (void* hwnd);
/** 개발용 (창 가장자리 조사 --uitest=winedge): 창 틀·DWM 상태 */
struct FrameInfo
{
    bool composition = false, ncRendering = false;
    int frameBounds[4] {};            // DWMWA_EXTENDED_FRAME_BOUNDS (보이는 틀, 화면 물리 px)
    unsigned style = 0, exStyle = 0, classStyle = 0;
    unsigned colorization = 0;        // DwmGetColorizationColor (0xAARRGGBB)
    int colorPrevalence = -1;         // HKCU\...\DWM ColorPrevalence (제목 표시줄·창 테두리에 강조 색 표시)
    int build = 0;                    // 윈도우 빌드 번호 (22000 이상 = 윈도우 11)
    long borderColourResult = 1;      // DWMWA_BORDER_COLOR 를 넣은 결과 (0 = 됨, 윈도우 10 은 실패)
};
FrameInfo frameInfo (void* hwnd);
/** 개발용: 화면 물리 좌표의 WM_NCHITTEST 결과 (HTCLIENT 1, HTCAPTION 2, HTMAXBUTTON 9, HTTOP 12 …) */
int devHitTest (void* hwnd, int screenX, int screenY);
/** 개발용: 화면 물리 좌표 사각형의 픽셀 (0xRRGGBB, 위→아래·왼→오른, 최대 w*h 개) */
std::string devScreenPixels (int x, int y, int w, int h);
/** 개발용: 마우스를 (x, y) 로 옮기고 왼쪽 버튼 누름(down)·뗌(up) 을 보낸다 (SendInput: 실제 입력과 같은 길) */
void devMouse (int screenX, int screenY, bool down, bool up);
/** 개발용: 화면 물리 좌표 사각형을 한 번에 찍는다 (0xFFRRGGBB, 위→아래·왼→오른). 실패하면 빈 것 */
std::vector<unsigned> devCaptureScreen (int x, int y, int w, int h);
/** 개발용: 디스플레이 배율을 권장 배율에서 steps 단계 (0 = 권장, 1 = 한 단계 위 …) 로 바꾼다. 지금 단계를 돌려준다.
    query 면 바꾸지 않고 읽기만. CI 에서 125%·150% 를 시험할 때 (운영체제가 허락하지 않으면 그대로) */
int devScaleSteps (int steps, bool query);
/** 개발용: Alt+Space 를 누른 것처럼 (SendInput), Esc, F11, Alt+F4 는 보내지 않는다 (창을 닫는다) */
void devAltSpace();
void devKey (int virtualKey);
/** 개발용: 창에 키 누름을 직접 보낸다 (앞 창이 아니어도. F11 시험) */
void devPostKey (void* hwnd, int virtualKey);
/** 개발용: 이 창보다 위에 있는 보이는 창들이 이 창을 덮는 자리 (화면 물리 px, x·y·w·h 씩). 화면 비교에서 남의 픽셀을 빼려고 */
std::vector<int> devWindowsAbove (void* hwnd);
/** 개발용: 창 전체를 WM_PAINT 로 바로 다시 그린다 (창 프로시저를 거친다: 화면 비교 전에) */
void devRedrawNow (void* hwnd);
void devEscape();

} // namespace meon::winframe
