// MEON - 운영체제 그림 해독기로 읽기 (JUCE 가 못 읽는 형식: WEBP).
//   맥: ImageIO (CoreImage, macOS 11 부터 WEBP), 윈도우: WIC (윈도우 10 1809 부터 WEBP 해독기 기본), 그 밖: 없음 (빈 그림)
#pragma once

#include <JuceHeader.h>

namespace meon
{

/** 실패하면 빈 그림 */
juce::Image decodeWithSystem (const void* data, size_t size);

} // namespace meon
