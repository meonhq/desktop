// MEON - 맥: ImageIO 로 읽기 (JUCE 가 PNG·GIF 에 쓰는 CoreImage 해독 함수를 그대로 쓴다). 설명은 MeonImageDecode.h
#include "MeonImageDecode.h"

namespace juce
{
    Image juce_loadWithCoreImage (InputStream&);   // juce_CoreGraphicsContext_mac.mm (JUCE_USE_COREIMAGE_LOADER)
}

namespace meon
{

juce::Image decodeWithSystem (const void* data, size_t size)
{
    if (data == nullptr || size == 0) return {};
    juce::MemoryInputStream in (data, size, false);
    return juce::juce_loadWithCoreImage (in);
}

} // namespace meon
