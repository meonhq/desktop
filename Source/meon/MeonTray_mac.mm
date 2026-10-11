// MEON - 독립 앱 상태 표시 아이콘 (macOS 메뉴 막대). 설명은 MeonTray.h
#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include "MeonTray.h"
#include "BinaryData.h"

namespace meon
{

namespace
{
    class TrayMac;
    TrayMac* activeTray = nullptr;   // 독립 앱에 하나뿐

    class TrayMac final : public TrayIcon
    {
    public:
        explicit TrayMac (Callbacks c) : callbacks (std::move (c))
        {
            activeTray = this;
            target = [[targetClass() alloc] init];

            menu = [[NSMenu alloc] initWithTitle: @"MEON"];
            [menu setAutoenablesItems: NO];
            [menu setDelegate: (id<NSMenuDelegate>) target];
            sendItem = addItem (@"송출 정지", @selector (meonToggleSend:));
            outputItem = addItem (@"전체 음소거", @selector (meonToggleOutput:));
            [menu addItem: [NSMenuItem separatorItem]];
            addItem (@"종료", @selector (meonQuit:));

            statusItem = [[[NSStatusBar systemStatusBar] statusItemWithLength: NSVariableStatusItemLength] retain];
            statusItem.button.image = makeIcon();
            statusItem.button.toolTip = @"MEON";
            statusItem.menu = menu;
        }

        ~TrayMac() override
        {
            [[NSStatusBar systemStatusBar] removeStatusItem: statusItem];
            [statusItem release];
            [menu setDelegate: nil];
            [menu release];
            [target release];
            if (activeTray == this)
                activeTray = nullptr;
        }

        // 메뉴가 열릴 때마다 체크 표시를 현재 상태로 맞춘다 (앱 화면에서 바꿨을 수도 있으니)
        void refresh()
        {
            sendItem.state = callbacks.isSendMuted() ? NSControlStateValueOn : NSControlStateValueOff;
            outputItem.state = callbacks.isOutputMuted() ? NSControlStateValueOn : NSControlStateValueOff;
        }

        void toggleSend()   { callbacks.setSendMuted (! callbacks.isSendMuted()); }
        void toggleOutput() { callbacks.setOutputMuted (! callbacks.isOutputMuted()); }

        void quit()
        {
            // 메뉴 처리가 끝난 뒤에 (종료 과정에서 이 아이콘이 지워진다)
            auto f = callbacks.quit;
            dispatch_async (dispatch_get_main_queue(), ^{ if (f) f(); });
        }

    private:
        Callbacks callbacks;
        id target = nil;
        NSMenu* menu = nil;
        NSMenuItem* sendItem = nil;
        NSMenuItem* outputItem = nil;
        NSStatusItem* statusItem = nil;

        NSMenuItem* addItem (NSString* title, SEL action)
        {
            NSMenuItem* item = [menu addItemWithTitle: title action: action keyEquivalent: @""];
            item.target = target;
            return item;
        }

        // 배경 없는 검은 M (scripts/make_icons.py). 템플릿 이미지라 OS 가 메뉴 막대 색에 맞춰 칠한다.
        static NSImage* makeIcon()
        {
            const NSSize size = NSMakeSize (20, 16);
            NSImage* image = [[[NSImage alloc] initWithSize: size] autorelease];
            const struct { const char* data; int bytes; } pngs[] = {
                { BinaryData::meon_tray_mac_png,    BinaryData::meon_tray_mac_pngSize },
                { BinaryData::meon_tray_mac_2x_png, BinaryData::meon_tray_mac_2x_pngSize },
            };
            for (auto& p : pngs)
            {
                if (NSBitmapImageRep* rep = [NSBitmapImageRep imageRepWithData: [NSData dataWithBytes: p.data length: (NSUInteger) p.bytes]])
                {
                    rep.size = size;
                    [image addRepresentation: rep];
                }
            }
            [image setTemplate: YES];   // (.template 은 C++ 키워드)
            return image;
        }

        // 메뉴 항목 target. 같은 코드가 플러그인(AU·VST3)에도 들어가므로 ObjC 클래스 이름이 겹치지 않게 런타임에 만든다.
        static Class targetClass()
        {
            static Class cls = []
            {
                NSString* name = [NSString stringWithFormat: @"MeonTrayTarget_%08x", arc4random()];
                Class c = objc_allocateClassPair ([NSObject class], name.UTF8String, 0);
                auto add = [c] (SEL sel, void (^block) (id, id))
                {
                    class_addMethod (c, sel, imp_implementationWithBlock (block), "v@:@");
                };
                add (@selector (meonToggleSend:),   ^(id, id) { if (activeTray != nullptr) activeTray->toggleSend(); });
                add (@selector (meonToggleOutput:), ^(id, id) { if (activeTray != nullptr) activeTray->toggleOutput(); });
                add (@selector (meonQuit:),         ^(id, id) { if (activeTray != nullptr) activeTray->quit(); });
                add (@selector (menuNeedsUpdate:),  ^(id, id) { if (activeTray != nullptr) activeTray->refresh(); });
                class_addProtocol (c, @protocol (NSMenuDelegate));
                objc_registerClassPair (c);
                return c;
            }();
            return cls;
        }
    };
}

std::unique_ptr<TrayIcon> TrayIcon::create (Callbacks callbacks)
{
    return std::make_unique<TrayMac> (std::move (callbacks));
}

} // namespace meon
