// MEON - 맥 독립 앱 창의 제목 줄. 설명은 MeonWindowChrome.h
//   - 제목 줄을 투명하게 하고 제목 글자를 숨긴 뒤(창 이름은 남는다) 내용 영역을 창 맨 위까지 넓힌다 (fullSizeContentView).
//   - 빈 NSToolbar 를 붙이고 unified 스타일로 둔다 (macOS 11+). 그러면 제목 줄 + 툴바가 한 줄(52pt)이 되고,
//     macOS 가 신호등을 그 세로 가운데에 시스템 간격으로 놓는다. 버튼을 직접 옮기지 않는다:
//     창 크기·키 창·전체 화면·다른 모니터·화면 배율이 바뀔 때 AppKit 이 다시 배치해도 늘 같은 자리다.
//     (예전 방식의 버그: 처음 켤 때 버튼을 옮긴 뒤 AppKit 이 다음 화면 갱신에서 제목 줄을 다시 배치해 버튼이 기본 자리(모서리)로 돌아갔고,
//      그 뒤 알림이 없어 창 크기를 바꿀 때까지 그대로였다)
//   - macOS 10.13–10.15 에는 툴바 스타일이 없어 윗줄 높이를 보장할 수 없다: 붙이지 않는다 (회색 제목 줄 그대로).
//   - 전체 화면에서는 툴바를 뗀다 (신호등이 없는 배치. 툴바가 있으면 전체 화면 위에 빈 줄이 남는다). 나오면 다시 붙인다.
//     JUCE 는 전체 화면에서 나올 때 창 모양(styleMask)을 처음 값으로 되돌린다 (resetWindowPresentation): 그 뒤에 다시 붙인다.
//   - 맨 윗줄 누름: 앱 화면(JUCE)이 받아서 빈 곳이면 titleBarMouseDown 을 부른다 (사이드바 접기 버튼·제목 줄 버튼은 각자 눌린다).
//     그 줄에서 툴바·제목 줄이 누름을 가로채면(앱 화면이 아닌 뷰가 받으면) 그 누름을 앱 화면으로 넘긴다 (신호등은 그대로 신호등이 받는다).
//   - 창 틀 뷰(NSThemeFrame) 자신이 받는 자리 = macOS 의 크기 조절 자리 (위 가장자리·위 모서리, 폭은 시스템 기본): 넘기지 않고 macOS 에 맡긴다.
//     (예전 버그: 이 누름도 앱 화면으로 넘겨 끌기가 되어, 크기 조절 커서인데 창이 옮겨졌다) 확대(zoom)한 창·전체 화면은 크기 조절 없이 예전대로
//   - 끌기는 지금 처리 중인 마우스 누름으로 performWindowDragWithEvent (macOS 가 옮긴다: 화면 가장자리 붙이기 등 그대로).
//     두 번 누름은 시스템 설정 "제목 막대를 이중 클릭할 때" (AppleActionOnDoubleClick) 를 따른다.
#import <AppKit/AppKit.h>
#include <objc/runtime.h>
#include <algorithm>
#include "MeonWindowChrome.h"
#include <cmath>
#include <vector>

namespace meon::chrome
{

namespace
{
    struct State
    {
        NSWindow* window = nil;          // 붙인 창 (앱 창 하나). 에디터가 사라질 때 detach 가 불린다
        std::vector<id> observers;
        id monitor = nil;                // 맨 윗줄 누름을 앱 화면으로 넘기는 이벤트 감시
        std::function<void()> onChange;
        bool applied = false;
        bool forwarding = false;         // 넘긴 누름이 떼어질 때까지 끌기·뗌도 넘긴다
        NSEvent* forwardingDown = nil;   // 넘기는 중인 누름 (titleBarMouseDown 이 쓴다. 감시 안에서는 currentEvent 가 다를 수 있다)
        int forwarded = 0, titleBarPresses = 0;
        int edgePasses = 0;              // 크기 조절 자리 누름을 macOS 에 맡긴 횟수
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

    bool isFullScreen (NSWindow* w) { return w != nil && ([w styleMask] & NSWindowStyleMaskFullScreen) != 0; }

    void attachToolbar (NSWindow* w)
    {
        if ([w toolbar] == nil)
        {
            NSToolbar* t = [[NSToolbar alloc] initWithIdentifier: @"com.meon.MEON.titlebar"];
            [t setAllowsUserCustomization: NO];
            [t setAutosavesConfiguration: NO];
            [t setDisplayMode: NSToolbarDisplayModeIconOnly];
            [w setToolbar: t];
            [t release];
        }
        if (@available (macOS 11.0, *))
            [w setToolbarStyle: NSWindowToolbarStyleUnified];
    }

    void apply (NSWindow* w)
    {
        if (w == nil) return;
        // 내용 영역을 먼저 넓힌다: 그 뒤에 툴바를 붙여야 내용 크기가 툴바만큼 줄지 않는다
        if (([w styleMask] & NSWindowStyleMaskFullSizeContentView) == 0)
            [w setStyleMask: [w styleMask] | NSWindowStyleMaskFullSizeContentView];
        [w setTitlebarAppearsTransparent: YES];
        [w setTitleVisibility: NSWindowTitleHidden];   // 창 이름(title)은 그대로: 미션 컨트롤·윈도우 메뉴
        if (@available (macOS 11.0, *))
            [w setTitlebarSeparatorStyle: NSTitlebarSeparatorStyleNone];
        if (! isFullScreen (w))
            attachToolbar (w);
    }

    void observe (NSWindow* w, NSNotificationName name, void (^block) (NSNotification*))
    {
        id o = [[NSNotificationCenter defaultCenter] addObserverForName: name object: w queue: [NSOperationQueue mainQueue] usingBlock: block];
        state().observers.push_back (o);
    }

    bool isWindowButton (NSWindow* w, NSView* v)
    {
        for (NSWindowButton kind : { NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton })
        {
            NSButton* b = [w standardWindowButton: kind];
            if (b != nil && (v == b || [v isDescendantOf: b])) return true;
        }
        return false;
    }

    /** 창 좌표(AppKit: 창 왼쪽 아래 기준)의 점을 누르면 받는 뷰 */
    NSView* hitAt (NSWindow* w, NSPoint windowPoint)
    {
        NSView* frameView = [[w contentView] superview];
        if (frameView == nil) return nil;
        NSView* above = [frameView superview];
        return [frameView hitTest: above != nil ? [above convertPoint: windowPoint fromView: nil] : windowPoint];
    }

    /** 위 가장자리에서 크기를 바꿀 수 있는 창인지: 크기 조절 가능, 전체 화면·확대(zoom) 아님 */
    bool resizeFromTopAllowed (NSWindow* w)
    {
        return w != nil && ([w styleMask] & NSWindowStyleMaskResizable) != 0 && ! isFullScreen (w) && ! [w isZoomed];
    }

    /** 맨 윗줄에서 앱 화면이 아닌 뷰(툴바·제목 줄)가 받을 누름을 앱 화면으로 넘긴다. 그 밖은 그대로.
        창 틀 뷰 자신이 받는 누름(크기 조절 자리)은 넘기지 않는다: macOS 가 크기를 바꾼다 */
    NSEvent* route (NSEvent* ev)
    {
        auto& s = state();
        if (! s.applied || s.window == nil || [ev window] != s.window) return ev;
        NSView* content = [s.window contentView];
        if (content == nil) return ev;
        const NSEventType type = [ev type];
        if (type == NSEventTypeLeftMouseDown || type == NSEventTypeRightMouseDown)
        {
            s.forwarding = false;
            if (isFullScreen (s.window)) return ev;
            const NSPoint p = [ev locationInWindow];
            if (p.y < NSHeight ([s.window frame]) - (CGFloat) barHeight) return ev;   // 맨 윗줄(52)만
            NSView* hit = hitAt (s.window, p);
            if (hit == nil || hit == content || [hit isDescendantOf: content] || isWindowButton (s.window, hit)) return ev;
            if (hit == [content superview] && type == NSEventTypeLeftMouseDown && resizeFromTopAllowed (s.window))
            {
                ++s.edgePasses;   // 위 가장자리·모서리 크기 조절 (macOS 가 처리)
                return ev;
            }
            s.forwarding = true;
            ++s.forwarded;
            s.forwardingDown = ev;
            if (type == NSEventTypeLeftMouseDown) [content mouseDown: ev];
            else                                  [content rightMouseDown: ev];
            s.forwardingDown = nil;
            return nil;
        }
        if (! s.forwarding) return ev;
        switch (type)
        {
            case NSEventTypeLeftMouseDragged:  [content mouseDragged: ev]; break;
            case NSEventTypeRightMouseDragged: [content rightMouseDragged: ev]; break;
            case NSEventTypeLeftMouseUp:       s.forwarding = false; [content mouseUp: ev]; break;
            case NSEventTypeRightMouseUp:      s.forwarding = false; [content rightMouseUp: ev]; break;
            default: return ev;
        }
        return nil;
    }

    std::string describeHit (NSWindow* w, NSView* hit)
    {
        if (hit == nil) return "none";
        NSView* content = [w contentView];
        if (hit == content || [hit isDescendantOf: content]) return "content";
        if (isWindowButton (w, hit)) return "button";
        return [NSStringFromClass ([hit class]) UTF8String];
    }
}

bool install (void* nsView, std::function<void()> onChange)
{
    // macOS 10.13–10.15: 툴바 스타일(unified)이 없어 윗줄 높이(52)를 보장할 수 없다. 회색 제목 줄 그대로 둔다 (사용자 결정 2026-10-31)
    if (@available (macOS 11.0, *)) {}
    else return false;
    NSWindow* w = windowFor (nsView);
    if (w == nil) return false;
    auto& s = state();
    if (s.applied && s.window == w) { s.onChange = std::move (onChange); apply (w); return true; }
    if (s.applied) detach();
    s.onChange = std::move (onChange);   // detach 가 지웠다
    s.window = w;
    s.applied = true;
    // 내용이 화면에서 제자리에 있게 창 틀을 제목 줄만큼 줄인다 (내용 크기 그대로). 그래야 창을 닫을 때 저장하는 크기(내용 크기)가
    // 다음 실행에서도 같다 (틀을 그대로 두면 내용이 제목 줄만큼 커진 채 저장돼 켤 때마다 창이 커진다)
    const bool fullScreen = isFullScreen (w);
    const NSRect content = [w contentRectForFrameRect: [w frame]];
    apply (w);
    if (! fullScreen && ! NSEqualRects ([w frame], content))
        [w setFrame: content display: YES];

    // 전체 화면: 들어가기 전에 툴바를 떼고(신호등 없는 배치), 나온 뒤 다시 붙인다
    observe (w, NSWindowWillEnterFullScreenNotification, ^(NSNotification*)
    {
        auto& st = state();
        if (! st.applied) return;
        [st.window setToolbar: nil];
        // 전체 화면에 들어가지 못하면(실패 알림은 JUCE 가 가진 위임자로만 온다) 툴바가 떨어진 채로 남는다: 전환이 끝났을 즈음 확인한다
        dispatch_after (dispatch_time (DISPATCH_TIME_NOW, (int64_t) (3 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
            auto& s2 = state();
            if (s2.applied && ! isFullScreen (s2.window) && [s2.window toolbar] == nil)
            {
                apply (s2.window);
                if (s2.onChange) s2.onChange();
            }
        });
    });
    observe (w, NSWindowDidEnterFullScreenNotification, ^(NSNotification*)
    {
        auto& st = state();
        if (st.applied && st.onChange) st.onChange();
    });
    void (^reapply) (NSNotification*) = ^(NSNotification*)
    {
        // JUCE 가 같은 알림에서 창 모양을 처음 값으로 되돌린다: 그 뒤(다음 차례)에 다시 붙인다
        dispatch_async (dispatch_get_main_queue(), ^{
            auto& st = state();
            if (! st.applied) return;
            apply (st.window);
            if (st.onChange) st.onChange();
        });
    };
    observe (w, NSWindowDidExitFullScreenNotification, reapply);

    if (s.monitor == nil)
        s.monitor = [NSEvent addLocalMonitorForEventsMatchingMask: NSEventMaskLeftMouseDown | NSEventMaskLeftMouseDragged | NSEventMaskLeftMouseUp
                                                                   | NSEventMaskRightMouseDown | NSEventMaskRightMouseDragged | NSEventMaskRightMouseUp
                                                          handler: ^NSEvent* (NSEvent* ev) { return route (ev); }];
    return true;
}

void detach()
{
    auto& s = state();
    for (id o : s.observers) [[NSNotificationCenter defaultCenter] removeObserver: o];
    s.observers.clear();
    if (s.monitor != nil) { [NSEvent removeMonitor: s.monitor]; s.monitor = nil; }
    s.onChange = nullptr;
    s.forwarding = false;
    s.window = nil;
    s.applied = false;
}

bool installedOn (void* nsView)
{
    const auto& s = state();
    NSWindow* w = windowFor (nsView);
    return s.applied && w != nil && w == s.window;
}

void titleBarMouseDown (void* nsView)
{
    auto& s = state();
    ++s.titleBarPresses;
    NSWindow* w = windowFor (nsView);
    NSEvent* ev = s.forwardingDown != nil ? s.forwardingDown : [NSApp currentEvent];
    if (w == nil || ev == nil || [ev type] != NSEventTypeLeftMouseDown || [ev window] != w) return;
    if (isFullScreen (w)) return;   // 전체 화면 창은 옮기지 않는다

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

int lightsRight (void* nsView)
{
    const auto& s = state();
    NSWindow* w = windowFor (nsView);
    if (! s.applied || w == nil || w != s.window || isFullScreen (w)) return 0;
    NSButton* zoom = [w standardWindowButton: NSWindowZoomButton];
    if (zoom == nil || [zoom isHidden]) return 0;
    const NSRect r = [zoom convertRect: [zoom bounds] toView: nil];   // 창 좌표 (x 는 왼쪽 기준이라 그대로)
    return (int) std::ceil (NSMaxX (r));
}

Probe probe (void* nsView, int toggleX, int toggleY, int emptyX, int emptyY)
{
    Probe p;
    const auto& s = state();
    p.forwarded = s.forwarded;
    p.titleBarPresses = s.titleBarPresses;
    const auto v = [[NSProcessInfo processInfo] operatingSystemVersion];
    p.os = std::to_string ((int) v.majorVersion) + "." + std::to_string ((int) v.minorVersion) + "." + std::to_string ((int) v.patchVersion);
    NSWindow* w = windowFor (nsView);
    if (w == nil) return p;
    p.installed = s.applied && w == s.window;
    p.fullScreen = isFullScreen (w);
    p.toolbar = [w toolbar] != nil;
    if (@available (macOS 11.0, *))
        p.unified = [w toolbarStyle] == NSWindowToolbarStyleUnified;
    p.fullSizeContent = ([w styleMask] & NSWindowStyleMaskFullSizeContentView) != 0;
    const CGFloat H = NSHeight ([w frame]);
    p.barH = H - NSMaxY ([w contentLayoutRect]);
    const NSWindowButton kinds[3] = { NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton };
    for (int i = 0; i < 3; ++i)
        if (NSButton* b = [w standardWindowButton: kinds[i]])
        {
            const NSRect r = [b convertRect: [b bounds] toView: nil];   // 창 좌표 (왼쪽 아래 기준)
            p.lights[i][0] = NSMinX (r);
            p.lights[i][1] = H - NSMaxY (r);
            p.lights[i][2] = NSWidth (r);
            p.lights[i][3] = NSHeight (r);
        }
    const auto at = [&] (double x, double y) { return describeHit (w, hitAt (w, NSMakePoint ((CGFloat) x, H - (CGFloat) y))); };
    p.hitToggle = at (toggleX, toggleY);
    p.hitEmpty = at (emptyX, emptyY);
    p.hitClose = at (p.lights[0][0] + p.lights[0][2] * 0.5, p.lights[0][1] + p.lights[0][3] * 0.5);
    // 위 가장자리·모서리·옆 끝(윗줄 안)을 누르면 받는 곳 (y 는 위에서 pt)
    const CGFloat W = NSWidth ([w frame]);
    std::string edges;
    for (double y : { 0.5, 1.5, 2.5, 3.5, 5.0, 8.0 })
        edges += "top" + std::to_string ((int) (y * 10)) + "=" + at (emptyX, y) + " ";
    edges += "tl=" + at (1.5, 1.5) + " tr=" + at (W - 1.5, 1.5) + " l20=" + at (1.5, 20) + " r20=" + at (W - 1.5, 20);
    p.hitEdges = edges;
    p.edgePasses = s.edgePasses;
    p.zoomed = [w isZoomed];
    return p;
}

void devPostClick (void* nsView, int x, int y, int clickCount)
{
    NSWindow* w = windowFor (nsView);
    if (w == nil) return;
    const NSPoint at = NSMakePoint ((CGFloat) x, NSHeight ([w frame]) - (CGFloat) y);
    const NSTimeInterval t = [[NSProcessInfo processInfo] systemUptime];
    NSEvent* down = [NSEvent mouseEventWithType: NSEventTypeLeftMouseDown location: at modifierFlags: 0 timestamp: t
                                   windowNumber: [w windowNumber] context: nil eventNumber: 0 clickCount: clickCount pressure: 1.0f];
    NSEvent* up = [NSEvent mouseEventWithType: NSEventTypeLeftMouseUp location: at modifierFlags: 0 timestamp: t + 0.05
                                 windowNumber: [w windowNumber] context: nil eventNumber: 0 clickCount: clickCount pressure: 0.0f];
    [NSApp postEvent: down atStart: NO];
    [NSApp postEvent: up atStart: NO];
}

void devPostDrag (void* nsView, int x, int y, int dx, int dy)
{
    NSWindow* w = windowFor (nsView);
    if (w == nil) return;
    const CGFloat H = NSHeight ([w frame]);
    const NSTimeInterval t = [[NSProcessInfo processInfo] systemUptime];
    auto post = [&] (NSEventType type, double fx, double fy, double dt, float pressure)
    {
        // 창 좌표 (AppKit: 왼쪽 아래 기준). 끄는 동안 창이 바뀌어도 같은 기준 (누른 때의 창 높이)
        NSEvent* e = [NSEvent mouseEventWithType: type location: NSMakePoint ((CGFloat) fx, H - (CGFloat) fy) modifierFlags: 0 timestamp: t + dt
                                    windowNumber: [w windowNumber] context: nil eventNumber: 0 clickCount: 1 pressure: pressure];
        [NSApp postEvent: e atStart: NO];
    };
    post (NSEventTypeLeftMouseDown, x, y, 0.0, 1.0f);
    post (NSEventTypeLeftMouseDragged, x + dx * 0.5, y + dy * 0.5, 0.05, 1.0f);
    post (NSEventTypeLeftMouseDragged, x + dx, y + dy, 0.10, 1.0f);
    post (NSEventTypeLeftMouseUp, x + dx, y + dy, 0.15, 0.0f);
}

bool devCornerPoint (void* nsView, bool right, int& x, int& y)
{
    // 위 모서리 근처에서 창 틀 뷰(NSThemeFrame)가 받는 점 (macOS 가 크기 조절 자리로 보는 곳). 둥근 모서리 바깥은 창이 아니므로
    // 모서리에서 안쪽으로: 맨 윗줄(위에서 1pt)을 따라, 그다음 옆 끝(1pt)을 따라 찾는다. 없으면 false
    NSWindow* w = windowFor (nsView);
    if (w == nil) return false;
    NSView* frameView = [[w contentView] superview];
    const CGFloat W = NSWidth ([w frame]), H = NSHeight ([w frame]);
    // devPostDrag 이 누르는 자리와 같은 좌표로 본다 (반 점 차이로 경계 밖이 될 수 있다)
    auto isFrame = [&] (int px, int py) { return hitAt (w, NSMakePoint ((CGFloat) px, H - (CGFloat) py)) == frameView; };
    for (int d = 0; d < 40; ++d)
    {
        const int px = right ? (int) W - 1 - d : d;
        if (isFrame (px, 1)) { x = px; y = 1; return true; }
    }
    for (int d = 0; d < 40; ++d)
    {
        const int px = right ? (int) W - 1 : 0;
        if (isFrame (px, d)) { x = px; y = d; return true; }
    }
    return false;
}

void devWindowFrame (void* nsView, double out[4])
{
    out[0] = out[1] = out[2] = out[3] = 0;
    NSWindow* w = windowFor (nsView);
    if (w == nil) return;
    const NSRect f = [w frame];
    out[0] = NSMinX (f); out[1] = NSMinY (f); out[2] = NSWidth (f); out[3] = NSHeight (f);
}

namespace
{
    NSWindow* fullScreenTargetWindow()
    {
        if (NSWindow* w = [NSApp keyWindow]) return w;
        if (NSWindow* w = [NSApp mainWindow]) return w;
        for (NSWindow* w in [NSApp windows])
            if ([w isVisible] && ([w collectionBehavior] & NSWindowCollectionBehaviorFullScreenPrimary) != 0) return w;
        return nil;
    }

    void toggleFrontWindow()
    {
        if (NSWindow* w = fullScreenTargetWindow()) [w toggleFullScreen: nil];
    }

    void toggleAction (id, SEL, id) { toggleFrontWindow(); }

    /** 메뉴 항목의 대상: 이름이 겹치지 않게 실행 중에 만든다 (같은 프로세스에 다른 빌드가 있어도) */
    id menuTarget()
    {
        static id target = nil;
        if (target != nil) return target;
        NSString* name = [NSString stringWithFormat: @"MeonFullScreenTarget_%p", (void*) &menuTarget];
        Class cls = objc_allocateClassPair ([NSObject class], [name UTF8String], 0);
        if (cls == nil) return nil;
        class_addMethod (cls, @selector (meonToggleFullScreen:), (IMP) toggleAction, "v@:@");
        objc_registerClassPair (cls);
        target = [[cls alloc] init];
        return target;
    }

    /** ⌃⌘F · fn+F (다른 수정 키 없이) */
    bool isFullScreenShortcut (NSEvent* e)
    {
        if ([e type] != NSEventTypeKeyDown || [e isARepeat]) return false;
        if (! [[[e charactersIgnoringModifiers] lowercaseString] isEqualToString: @"f"]) return false;
        const NSEventModifierFlags m = [e modifierFlags] & (NSEventModifierFlagControl | NSEventModifierFlagCommand | NSEventModifierFlagOption | NSEventModifierFlagShift | NSEventModifierFlagFunction);
        return m == (NSEventModifierFlagControl | NSEventModifierFlagCommand) || m == NSEventModifierFlagFunction;
    }
}

namespace
{
    /** 메뉴 막대에 "보기 > 전체 화면으로 전환 ⌃⌘F" 가 없으면 넣는다 (앱 메뉴 다음) */
    void ensureViewMenu()
    {
        NSMenu* mainMenu = [NSApp mainMenu];
        if (mainMenu == nil) return;
        for (NSMenuItem* top in [mainMenu itemArray])
            if ([[top title] isEqualToString: @"보기"]) return;
        NSMenu* view = [[NSMenu alloc] initWithTitle: @"보기"];
        NSMenuItem* fs = [[NSMenuItem alloc] initWithTitle: @"전체 화면으로 전환" action: @selector (meonToggleFullScreen:) keyEquivalent: @"f"];
        [fs setKeyEquivalentModifierMask: NSEventModifierFlagControl | NSEventModifierFlagCommand];
        [fs setTarget: menuTarget()];
        [view addItem: fs];
        NSMenuItem* top = [[NSMenuItem alloc] initWithTitle: @"보기" action: nil keyEquivalent: @""];
        [top setSubmenu: view];
        [mainMenu insertItem: top atIndex: std::min<NSInteger> (1, [mainMenu numberOfItems])];
        [fs release];
        [view release];
        [top release];
    }
}

void installFullScreenShortcuts()
{
    static bool done = false;
    if (done) return;
    done = true;
    // 메뉴 막대: JUCE 는 앱 initialise() 가 끝난 뒤에 메뉴 막대를 새로 만든다 (JUCEApplication::initialiseApp → initialiseMacMainMenu).
    // 그 전에 넣으면 지워지므로 다음 차례에 넣고, 앱이 앞으로 올 때마다 있는지 다시 본다
    dispatch_async (dispatch_get_main_queue(), ^{ ensureViewMenu(); });
    [[NSNotificationCenter defaultCenter] addObserverForName: NSApplicationDidBecomeActiveNotification object: nil
                                                       queue: [NSOperationQueue mainQueue]
                                                  usingBlock: ^(NSNotification*) { ensureViewMenu(); }];
    // 앱 이벤트에서 먼저 받는다: 입력 칸이 키를 먹어도, 메뉴가 없어도 (fn+F 는 메뉴 단축키로 걸 수 없다)
    [NSEvent addLocalMonitorForEventsMatchingMask: NSEventMaskKeyDown handler: ^NSEvent* (NSEvent* e)
    {
        if (! isFullScreenShortcut (e)) return e;
        toggleFrontWindow();
        return nil;
    }];
}

bool isFullScreen (void* nsView) { return isFullScreen (windowFor (nsView)); }

void toggleFullScreen (void* nsView)
{
    if (NSWindow* w = windowFor (nsView)) [w toggleFullScreen: nil];
}

void exitFullScreenThen (void* nsView, std::function<void()> done)
{
    NSWindow* w = windowFor (nsView);
    if (! isFullScreen (w)) { if (done) done(); return; }
    __block id obs = nil;
    std::function<void()> fn = std::move (done);
    obs = [[NSNotificationCenter defaultCenter] addObserverForName: NSWindowDidExitFullScreenNotification object: w
                                                             queue: [NSOperationQueue mainQueue]
                                                        usingBlock: ^(NSNotification*)
    {
        [[NSNotificationCenter defaultCenter] removeObserver: obs];
        if (fn) fn();
    }];
    [w toggleFullScreen: nil];
}

void devPostKey (void* nsView, const char* chars, int modifiers)
{
    NSWindow* w = windowFor (nsView);
    if (w == nil) return;
    NSEventModifierFlags m = 0;
    if (modifiers & 1) m |= NSEventModifierFlagControl;
    if (modifiers & 2) m |= NSEventModifierFlagCommand;
    if (modifiers & 4) m |= NSEventModifierFlagFunction;
    NSString* s = [NSString stringWithUTF8String: chars];
    NSEvent* e = [NSEvent keyEventWithType: NSEventTypeKeyDown location: NSZeroPoint modifierFlags: m timestamp: [[NSProcessInfo processInfo] systemUptime]
                                windowNumber: [w windowNumber] context: nil characters: s charactersIgnoringModifiers: s isARepeat: NO keyCode: 3];
    [NSApp postEvent: e atStart: NO];
    NSEvent* up = [NSEvent keyEventWithType: NSEventTypeKeyUp location: NSZeroPoint modifierFlags: m timestamp: [[NSProcessInfo processInfo] systemUptime]
                                 windowNumber: [w windowNumber] context: nil characters: s charactersIgnoringModifiers: s isARepeat: NO keyCode: 3];
    [NSApp postEvent: up atStart: NO];
}

bool devHasFullScreenMenu()
{
    for (NSMenuItem* top in [[NSApp mainMenu] itemArray])
        if ([[top title] isEqualToString: @"보기"])
            for (NSMenuItem* it in [[top submenu] itemArray])
                if ([[it title] isEqualToString: @"전체 화면으로 전환"] && [[it keyEquivalent] isEqualToString: @"f"]
                    && ([it keyEquivalentModifierMask] & (NSEventModifierFlagControl | NSEventModifierFlagCommand)) == (NSEventModifierFlagControl | NSEventModifierFlagCommand))
                    return true;
    return false;
}

void devSetActive (bool active)
{
    if (! active) { [NSApp deactivate]; return; }
    if (@available (macOS 14.0, *))
        [NSApp activate];
    else
    {
       #pragma clang diagnostic push
       #pragma clang diagnostic ignored "-Wdeprecated-declarations"
        [NSApp activateIgnoringOtherApps: YES];
       #pragma clang diagnostic pop
    }
}

} // namespace meon::chrome
