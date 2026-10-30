// MEON - 맥 독립 앱 창의 제목 줄. 설명은 MeonWindowChrome.h
//   - 제목 줄을 투명하게 하고 제목 글자를 숨긴 뒤(창 이름은 남는다) 내용 영역을 창 맨 위까지 넓힌다 (fullSizeContentView).
//   - 신호등은 macOS 기본 버튼을 그대로 쓰고 위치만 옮긴다: 제목 줄 묶음 높이를 52 로 하고 그 세로 가운데,
//     원(12px)의 왼쪽이 20, 사이 8 (디자인). 크기를 바꾸거나 전체 화면에서 나오면 AppKit 이 원래 자리로 되돌리므로 그때마다 다시 둔다.
//   - JUCE 는 전체 화면에서 나올 때 창 모양(styleMask)을 처음 값으로 되돌린다 (resetWindowPresentation): 그 뒤에 다시 붙인다.
//   - 맨 윗줄 끌기: 지금 처리 중인 마우스 누름으로 performWindowDragWithEvent (macOS 가 옮긴다: 화면 가장자리 붙이기 등 그대로).
//     두 번 누름은 시스템 설정 "제목 막대를 이중 클릭할 때" (AppleActionOnDoubleClick) 를 따른다.
#import <AppKit/AppKit.h>
#include "MeonWindowChrome.h"
#include <cmath>
#include <vector>

namespace meon::chrome
{

namespace
{
    struct State
    {
        NSWindow* window = nil;          // 붙인 창 (앱 창 하나). 창이 사라지기 전에 remove 가 불린다
        std::vector<id> observers;
        std::function<void()> onChange;
        bool applied = false;
        // 원래 자리 (remove 에서 되돌린다)
        bool saved = false;
        CGFloat containerH = 0;
        NSPoint buttonOrigins[3] {};
    };

    State& state()
    {
        static State s;
        return s;
    }

    NSWindow* windowFor (void* nsView)
    {
        NSView* view = (__bridge NSView*) nsView;
        return view != nil ? [view window] : nil;
    }

    bool buttonsOf (NSWindow* w, NSButton* out[3], NSView** container)
    {
        out[0] = [w standardWindowButton: NSWindowCloseButton];
        out[1] = [w standardWindowButton: NSWindowMiniaturizeButton];
        out[2] = [w standardWindowButton: NSWindowZoomButton];
        if (out[0] == nil || out[1] == nil || out[2] == nil) return false;
        NSView* titlebar = [out[0] superview];         // 제목 줄 (버튼이 든 곳)
        *container = titlebar != nil ? [titlebar superview] : nil;   // 제목 줄 묶음
        return *container != nil;
    }

    /** 신호등을 52px 윗줄의 세로 가운데로 (원 왼쪽 20, 원 사이 8) */
    void layoutButtons (NSWindow* w)
    {
        auto& s = state();
        if (w == nil || ! s.applied || ([w styleMask] & NSWindowStyleMaskFullScreen) != 0) return;
        NSButton* b[3];
        NSView* container = nil;
        if (! buttonsOf (w, b, &container)) return;
        if (! s.saved)
        {
            s.saved = true;
            s.containerH = NSHeight ([container frame]);
            for (int i = 0; i < 3; ++i) s.buttonOrigins[i] = [b[i] frame].origin;
        }
        const CGFloat barH = (CGFloat) barHeight;
        NSRect cf = [container frame];
        if (std::abs (NSHeight (cf) - barH) > 0.5 || std::abs (NSMaxY (cf) - NSHeight ([w frame])) > 0.5)
        {
            cf.size.height = barH;
            cf.origin.y = NSHeight ([w frame]) - barH;
            [container setFrame: cf];
        }
        const CGFloat bw = NSWidth ([b[0] frame]), bh = NSHeight ([b[0] frame]);
        const CGFloat x0 = 20.0 - (bw - 12.0) * 0.5;   // 버튼 틀은 원보다 조금 크다: 원 왼쪽이 20 이 되게
        const CGFloat y = std::round ((barH - bh) * 0.5);
        for (int i = 0; i < 3; ++i)
        {
            const NSPoint p = NSMakePoint (x0 + 20.0 * i, y);   // 원 12 + 사이 8
            if (! NSEqualPoints ([b[i] frame].origin, p))
                [b[i] setFrameOrigin: p];
        }
    }

    void apply (NSWindow* w)
    {
        if (w == nil) return;
        if (([w styleMask] & NSWindowStyleMaskFullSizeContentView) == 0)
            [w setStyleMask: [w styleMask] | NSWindowStyleMaskFullSizeContentView];
        [w setTitlebarAppearsTransparent: YES];
        [w setTitleVisibility: NSWindowTitleHidden];   // 창 이름(title)은 그대로: 미션 컨트롤·윈도우 메뉴
        if (@available (macOS 11.0, *))
            [w setTitlebarSeparatorStyle: NSTitlebarSeparatorStyleNone];
        layoutButtons (w);
    }

    void observe (NSWindow* w, NSNotificationName name, void (^block) (NSNotification*))
    {
        id o = [[NSNotificationCenter defaultCenter] addObserverForName: name object: w queue: [NSOperationQueue mainQueue] usingBlock: block];
        state().observers.push_back (o);
    }
}

bool install (void* nsView, std::function<void()> onFullScreenChange)
{
    NSWindow* w = windowFor (nsView);
    if (w == nil) return false;
    auto& s = state();
    s.onChange = std::move (onFullScreenChange);
    if (s.applied && s.window == w) { apply (w); return true; }
    if (s.applied) remove ((__bridge void*) [s.window contentView]);
    s.window = w;
    s.applied = true;
    s.saved = false;
    // 내용이 화면에서 제자리에 있게 창 틀을 제목 줄만큼 줄인다 (내용 크기 그대로). 그래야 창을 닫을 때 저장하는 크기(내용 크기)가
    // 다음 실행에서도 같다 (틀을 그대로 두면 내용이 28 커진 채 저장돼 켤 때마다 창이 28 씩 커진다)
    const bool fullScreen = ([w styleMask] & NSWindowStyleMaskFullScreen) != 0;
    const NSRect content = [w contentRectForFrameRect: [w frame]];
    apply (w);
    if (! fullScreen && ! NSEqualRects ([w frame], content))
        [w setFrame: content display: YES];
    layoutButtons (w);

    // AppKit 이 제목 줄을 다시 배치하면 신호등이 원래 자리로 돌아간다: 크기·키 창·화면 배율이 바뀔 때마다 다시 둔다
    NSNotificationName relayoutOn[] = { NSWindowDidResizeNotification, NSWindowDidEndLiveResizeNotification, NSWindowDidBecomeKeyNotification,
                                        NSWindowDidResignKeyNotification, NSWindowDidBecomeMainNotification, NSWindowDidChangeBackingPropertiesNotification };
    for (NSNotificationName n : relayoutOn)
        observe (w, n, ^(NSNotification*) { auto& st = state(); if (st.applied) layoutButtons (st.window); });
    observe (w, NSWindowDidEnterFullScreenNotification, ^(NSNotification*)
    {
        auto& st = state();
        if (st.applied && st.onChange) st.onChange();
    });
    observe (w, NSWindowDidExitFullScreenNotification, ^(NSNotification*)
    {
        // JUCE 가 같은 알림에서 창 모양을 처음 값으로 되돌린다: 그 뒤(다음 차례)에 다시 붙인다
        dispatch_async (dispatch_get_main_queue(), ^{
            auto& st = state();
            if (! st.applied) return;
            apply (st.window);
            if (st.onChange) st.onChange();
        });
    });
    return true;
}

void remove (void* nsView)
{
    auto& s = state();
    for (id o : s.observers) [[NSNotificationCenter defaultCenter] removeObserver: o];
    s.observers.clear();
    s.onChange = nullptr;
    // 넘겨받은 화면이 아직 그 창 안에 있을 때만 창을 건드린다 (앱을 끝내는 중이면 창이 먼저 사라질 수 있다)
    NSWindow* w = windowFor (nsView);
    if (w != nil && w == s.window && s.applied)
    {
        NSButton* b[3];
        NSView* container = nil;
        if (s.saved && ([w styleMask] & NSWindowStyleMaskFullScreen) == 0 && buttonsOf (w, b, &container))
        {
            NSRect cf = [container frame];
            cf.size.height = s.containerH;
            cf.origin.y = NSHeight ([w frame]) - s.containerH;
            [container setFrame: cf];
            for (int i = 0; i < 3; ++i) [b[i] setFrameOrigin: s.buttonOrigins[i]];
        }
        const bool fullScreen = ([w styleMask] & NSWindowStyleMaskFullScreen) != 0;
        const NSRect content = [w frame];   // 지금은 틀 = 내용
        [w setTitlebarAppearsTransparent: NO];
        [w setTitleVisibility: NSWindowTitleVisible];
        if (@available (macOS 11.0, *))
            [w setTitlebarSeparatorStyle: NSTitlebarSeparatorStyleAutomatic];
        [w setStyleMask: [w styleMask] & ~NSWindowStyleMaskFullSizeContentView];
        // 붙일 때의 반대: 내용은 제자리, 제목 줄만큼 틀을 위로 늘린다
        if (! fullScreen)
            [w setFrame: [w frameRectForContentRect: content] display: YES];
    }
    s.window = nil;
    s.applied = false;
    s.saved = false;
}

void titleBarMouseDown (void* nsView)
{
    NSWindow* w = windowFor (nsView);
    NSEvent* ev = [NSApp currentEvent];
    if (w == nil || ev == nil || [ev type] != NSEventTypeLeftMouseDown || [ev window] != w) return;
    if (([w styleMask] & NSWindowStyleMaskFullScreen) != 0) return;   // 전체 화면 창은 옮기지 않는다

    if ([ev clickCount] >= 2)
    {
        // 시스템 설정 › 데스크탑 및 Dock › "제목 막대를 이중 클릭할 때": Maximize(확대)·Fill(채우기)·Minimize(최소화)·None(아무것도 안 함)
        NSUserDefaults* d = [NSUserDefaults standardUserDefaults];
        NSString* action = [d stringForKey: @"AppleActionOnDoubleClick"];
        if (action == nil)
            action = [d boolForKey: @"AppleMiniaturizeOnDoubleClick"] ? @"Minimize" : @"Maximize";
        if ([action isEqualToString: @"Minimize"]) [w performMiniaturize: nil];
        else if (! [action isEqualToString: @"None"]) [w performZoom: nil];   // 확대·채우기
        return;
    }
    [w performWindowDragWithEvent: ev];
}

} // namespace meon::chrome
