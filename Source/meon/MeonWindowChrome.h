// MEON - 맥 독립 앱 창의 제목 줄 (홈·사이드바 디자인 v2.1, 창 = 꺼짐(창 모드)).
//   회색 제목 줄과 "Meon" 글자를 없애고 내용이 창 맨 위까지 올라오게 한다. 창 이름은 그대로 둔다 (미션 컨트롤·윈도우 메뉴).
//   신호등은 macOS 기본 버튼 그대로, 52px 윗줄의 세로 가운데 (디자인: 12px 원, 왼쪽 20, 사이 8).
//   맨 윗줄을 잡고 끌면 창이 옮겨지고, 더블클릭은 시스템 설정(제목 막대를 이중 클릭할 때)을 따른다.
//   macOS : MeonWindowChrome_mac.mm (NSWindow fullSizeContentView + 투명 제목 줄)
//   그 밖 : 아무 일도 하지 않는다 (MeonWindowChrome.cpp). 윈도우는 이번에 제목 줄을 그대로 둔다
#pragma once

#include <functional>

namespace meon::chrome
{

/** nsView: juce::ComponentPeer::getNativeHandle() (macOS NSView*). 붙였으면 true.
    onFullScreenChange: 전체 화면에 들어가거나 나온 뒤 (화면 배치를 바꿔야 한다). 같은 창에 다시 불러도 된다 */
bool install (void* nsView, std::function<void()> onFullScreenChange);
/** 제목 줄을 원래대로 (로그아웃해서 앱 화면이 사라질 때) */
void remove (void* nsView);
/** 맨 윗줄을 눌렀을 때 (지금 처리 중인 마우스 누름으로): 두 번 누름이면 시스템 설정의 동작(확대·최소화·없음), 아니면 창 끌기 */
void titleBarMouseDown (void* nsView);
/** 신호등 자리 (창 왼쪽 위 기준, 논리 px): 원 3개를 감싸는 사각형. 붙이지 않았으면 빈 값 */
struct Lights { int x = 20, y = 20, w = 52, h = 12; };
constexpr int barHeight = 52;   // 신호등이 세로 가운데 오는 윗줄 높이 (사이드바 머리말·본문 제목 줄과 같다)

} // namespace meon::chrome
