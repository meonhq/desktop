// MEON - "동작 줄이기" (macOS). 설명은 MeonReduceMotion.h
#import <AppKit/AppKit.h>
#include "MeonReduceMotion.h"

namespace meon
{

bool systemPrefersReducedMotion()
{
    if (@available (macOS 10.12, *))
        return [[NSWorkspace sharedWorkspace] accessibilityDisplayShouldReduceMotion];
    return false;
}

} // namespace meon
