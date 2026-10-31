// MEON - 창 제목 줄 (macOS 밖). 설명은 MeonWindowChrome.h. macOS 는 MeonWindowChrome_mac.mm
#include "MeonWindowChrome.h"

#if ! defined (__APPLE__)

namespace meon::chrome
{

bool install (void*, std::function<void()>) { return false; }
void detach() {}
bool installedOn (void*) { return false; }
void titleBarMouseDown (void*) {}
int lightsRight (void*) { return 0; }
Probe probe (void*, int, int, int, int) { return {}; }
void devPostClick (void*, int, int, int) {}
void devSetActive (bool) {}
void installFullScreenShortcuts() {}
bool isFullScreen (void*) { return false; }
void toggleFullScreen (void*) {}
void exitFullScreenThen (void*, std::function<void()> done) { if (done) done(); }
void devPostKey (void*, const char*, int) {}
bool devHasFullScreenMenu() { return false; }

} // namespace meon::chrome

#endif
