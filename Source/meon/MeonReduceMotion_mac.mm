// MEON - "동작 줄이기" (macOS). 설명은 MeonReduceMotion.h
#import <AppKit/AppKit.h>
#include "MeonReduceMotion.h"
#include <cstdlib>

namespace meon
{

bool systemPrefersReducedMotion()
{
   #if MEON_DEV_OPTIONS
    if (const char* v = std::getenv ("MEON_REDUCE_MOTION"); v != nullptr && v[0] == '1') return true;
   #endif
    if (@available (macOS 10.12, *))
        return [[NSWorkspace sharedWorkspace] accessibilityDisplayShouldReduceMotion];
    return false;
}

} // namespace meon
