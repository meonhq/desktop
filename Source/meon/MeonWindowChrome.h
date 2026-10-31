// MEON - 맥 독립 앱 창의 제목 줄 (홈·사이드바 디자인 v2.1, 창 = 꺼짐(창 모드)). 온보딩(첫 실행·로그인)부터 앱을 끌 때까지 같은 처리.
//   회색 제목 줄과 "Meon" 글자를 없애고 내용이 창 맨 위까지 올라오게 한다. 창 이름은 그대로 둔다 (미션 컨트롤·윈도우 메뉴).
//   신호등은 macOS 가 직접 배치한다: 빈 툴바 + unified 스타일이면 윗줄이 52pt 이고 신호등이 그 세로 가운데, 간격은 시스템 기본.
//   macOS 11 이상만 (그 아래는 회색 제목 줄 그대로).
//   (예전에는 버튼을 하나씩 옮겼는데, AppKit 이 제목 줄을 다시 배치할 때마다 원래 자리로 돌아갔다)
//   맨 윗줄을 잡고 끌면 창이 옮겨지고, 더블클릭은 시스템 설정(제목 막대를 이중 클릭할 때)을 따른다.
//   macOS : MeonWindowChrome_mac.mm (NSWindow fullSizeContentView + 투명 제목 줄 + 제목 숨김 + 빈 NSToolbar(unified))
//   그 밖 : 아무 일도 하지 않는다 (MeonWindowChrome.cpp). 윈도우는 제목 줄을 그대로 둔다
#pragma once

#include <functional>
#include <string>

namespace meon::chrome
{

/** nsView: juce::ComponentPeer::getNativeHandle() (macOS NSView*). 붙였으면 true.
    macOS 11 미만(10.13–10.15)은 붙이지 않는다 (false): 툴바 스타일이 없어 윗줄 높이를 보장할 수 없다. 회색 제목 줄 그대로 (사용자 결정 2026-10-31).
    onChange: 전체 화면에 들어가거나 나온 뒤 (화면 배치를 바꿔야 한다). 같은 창에 다시 불러도 된다 */
bool install (void* nsView, std::function<void()> onChange);
/** 창을 더 지켜보지 않는다 (에디터가 사라질 때). 창 모양은 그대로 둔다: 앱을 끄는 중이거나 새 에디터가 곧 다시 붙인다 */
void detach();
/** 이 화면이 든 창에 붙어 있는지 (전체 화면이어도 true) */
bool installedOn (void* nsView);
/** 맨 윗줄을 눌렀을 때 (지금 처리 중인 마우스 누름으로): 두 번 누름이면 시스템 설정의 동작(확대·최소화·없음), 아니면 창 끌기 */
void titleBarMouseDown (void* nsView);
/** 신호등(확대 버튼 틀)의 오른쪽 끝 x (창 왼쪽 기준 pt, 올림). 붙이지 않았거나 전체 화면이면 0. 사이드바 접기 버튼이 이 + 12 에 온다 */
int lightsRight (void* nsView);
constexpr int barHeight = 52;   // 신호등이 세로 가운데 오는 윗줄 높이 (사이드바 머리말·본문 제목 줄과 같다)

/** 전체 화면 단축키 (독립 앱, 한 번): 메뉴 막대에 "보기 > 전체 화면으로 전환" (⌃⌘F) 을 넣고, ⌃⌘F 와 fn+F(지구본+F) 를
    앱 이벤트에서 먼저 받는다 (글자 입력 중에도). 모두 macOS 기본 전체 화면 (초록 버튼과 같은 toggleFullScreen) */
void installFullScreenShortcuts();
bool isFullScreen (void* nsView);
void toggleFullScreen (void* nsView);
/** 전체 화면이면 나온 뒤 done, 아니면 바로 done (앱을 끌 때: 다시 켜면 원래 창으로 열리게) */
void exitFullScreenThen (void* nsView, std::function<void()> done);
/** 개발용 (화면 자가 테스트): 키를 누른 것처럼 AppKit 이벤트 줄에 넣는다. modifiers: 1 control, 2 command, 4 fn */
void devPostKey (void* nsView, const char* chars, int modifiers);
/** 개발용: 메뉴 막대에 "보기 > 전체 화면으로 전환" (⌃⌘F) 이 있는지 */
bool devHasFullScreenMenu();

/** 개발용 (화면 자가 테스트 --uitest=chrome): 지금 창의 제목 줄 상태. 좌표는 창 왼쪽 위 기준 pt */
struct Probe
{
    bool installed = false, fullScreen = false, toolbar = false, unified = false, fullSizeContent = false;
    double barH = 0;              // 제목 줄(툴바 포함) 높이 = 창 높이 - contentLayoutRect 높이
    double lights[3][4] {};       // 닫기·최소화·확대 버튼 틀 x, y, w, h
    std::string hitToggle, hitEmpty, hitClose;   // 그 점을 누르면 받는 곳: content(앱 화면) | button(신호등) | 그 밖 클래스 이름
    int forwarded = 0;            // 제목 줄이 가로챈 누름을 앱 화면으로 넘긴 횟수
    int titleBarPresses = 0;      // titleBarMouseDown 이 불린 횟수
    int edgePasses = 0;           // 위 가장자리(창 틀 뷰가 받는 크기 조절 자리) 누름을 macOS 에 맡긴 횟수
    bool zoomed = false;          // 확대(zoom) 상태
    std::string hitEdges;         // 위 가장자리(위에서 0.5~8pt)·위 모서리·옆 끝(윗줄 안)을 누르면 받는 곳
    std::string os;
};
Probe probe (void* nsView, int toggleX, int toggleY, int emptyX, int emptyY);
/** 개발용: 창의 (x, y) 를 마우스로 누른 것처럼 AppKit 이벤트 줄에 넣는다 (누름·뗌. clickCount 2 = 더블클릭의 두 번째) */
void devPostClick (void* nsView, int x, int y, int clickCount);
/** 개발용: (x, y) 에서 눌러 (dx, dy) 만큼 끌고 뗀 것처럼 AppKit 이벤트 줄에 넣는다 (창 왼쪽 위 기준 pt) */
void devPostDrag (void* nsView, int x, int y, int dx, int dy);
/** 개발용: 창 틀 (AppKit 화면 좌표: x, 아래 y, 폭, 높이) */
void devWindowFrame (void* nsView, double out[4]);
/** 개발용: 앱을 비활성(다른 앱으로 간 것처럼)·활성으로 */
void devSetActive (bool);

} // namespace meon::chrome
