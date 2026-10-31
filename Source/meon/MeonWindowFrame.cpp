// MEON - 윈도우 독립 앱 창의 윗부분 (윈도우 밖). 설명은 MeonWindowFrame.h. 윈도우는 MeonWindowFrame_win.cpp
#include "MeonWindowFrame.h"

#if ! defined (_WIN32)

namespace meon::winframe
{

bool install (void*, Hooks) { return false; }
void detach() {}
bool installedOn (void*) { return false; }
int hiddenTop (void*) { return 0; }
bool isMaximised (void*) { return false; }
bool isActive (void*) { return true; }
void minimise (void*) {}
void toggleMaximise (void*) {}
void close (void*) {}
Probe probe (void*) { return {}; }
FrameInfo frameInfo (void*) { return {}; }
int devHitTest (void*, int, int) { return 0; }
std::string devScreenPixels (int, int, int, int) { return {}; }
void devMouse (int, int, bool, bool) {}
void devAltSpace() {}
void devEscape() {}
void setFullscreen (void*, bool) {}
bool isFullscreen (void*) { return false; }
void devKey (int) {}

} // namespace meon::winframe

#endif
