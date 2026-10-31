// MEON - 운영체제 그림 해독기. 설명은 MeonImageDecode.h (맥 MeonImageDecode_mac.mm, 윈도우 MeonImageDecode_win.cpp)
#include "MeonImageDecode.h"

#if ! defined (_WIN32) && ! defined (__APPLE__)

namespace meon
{

juce::Image decodeWithSystem (const void*, size_t) { return {}; }   // 리눅스 (개발용 빌드): WEBP 해독기 없음

} // namespace meon

#endif
