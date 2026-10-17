// MEON - 운영체제의 "동작 줄이기" 설정 (켜져 있으면 접기·펼치기를 애니메이션 없이 바로 바꾼다).
//   macOS  : NSWorkspace accessibilityDisplayShouldReduceMotion (MeonReduceMotion_mac.mm)
//   Windows: SystemParametersInfo (SPI_GETCLIENTAREAANIMATION) 가 FALSE 면 줄이기
//   그 밖  : 늘 false
#pragma once

namespace meon
{

/** 누를 때마다 부른다 (설정을 바꾸면 앱을 다시 켜지 않아도 바로 따른다) */
bool systemPrefersReducedMotion();

} // namespace meon
