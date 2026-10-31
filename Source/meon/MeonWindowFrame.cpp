// MEON - 윈도우 독립 앱 창의 윗부분 (윈도우 밖). 설명은 MeonWindowFrame.h. 윈도우는 MeonWindowFrame_win.cpp
#include "MeonWindowFrame.h"

#if ! defined (_WIN32)

namespace meon::winframe
{

bool install (void*, Hooks) { return false; }
void detach() {}
bool installedOn (void*) { return false; }
bool isMaximised (void*) { return false; }
bool isActive (void*) { return true; }
void minimise (void*) {}
void toggleMaximise (void*) {}
void close (void*) {}
void restore (void*) {}
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
bool appDrawsBorder (void*) { return false; }
bool clientPixelSize (void*, int&, int&) { return false; }
void frameInsets (void*, int out[4]) { out[0] = out[1] = out[2] = out[3] = 0; }
void normalFrameInsets (void*, int out[4]) { out[0] = out[1] = out[2] = out[3] = 0; }
void holdPosition (void*, bool) {}
void devPostKey (void*, int) {}
void devRedrawNow (void*) {}
std::vector<int> devWindowsAbove (void*) { return {}; }
bool unmaximiseForSave (void*) { return false; }
std::vector<unsigned> devCaptureScreen (int, int, int, int) { return {}; }
int devScaleSteps (int, bool) { return 0; }

} // namespace meon::winframe

#endif
