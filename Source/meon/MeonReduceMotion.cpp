// MEON - "동작 줄이기" (Windows·그 밖). macOS 는 MeonReduceMotion_mac.mm
#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #ifndef SPI_GETCLIENTAREAANIMATION
  #define SPI_GETCLIENTAREAANIMATION 0x1042   // Windows Vista 부터 (WINVER 를 낮게 잡은 빌드에서도 쓰도록)
 #endif
#endif
#include "MeonReduceMotion.h"

#if ! defined (__APPLE__)

namespace meon
{

bool systemPrefersReducedMotion()
{
   #if defined (_WIN32)
    BOOL animate = TRUE;
    if (SystemParametersInfoW (SPI_GETCLIENTAREAANIMATION, 0, &animate, 0))
        return animate == FALSE;
    return false;
   #else
    return false;
   #endif
}

} // namespace meon

#endif
