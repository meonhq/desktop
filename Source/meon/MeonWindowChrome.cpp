// MEON - 창 제목 줄 (macOS 밖). 설명은 MeonWindowChrome.h. macOS 는 MeonWindowChrome_mac.mm
#include "MeonWindowChrome.h"

#if ! defined (__APPLE__)

namespace meon::chrome
{

bool install (void*, std::function<void()>) { return false; }
void remove (void*) {}
void titleBarMouseDown (void*) {}

} // namespace meon::chrome

#endif
