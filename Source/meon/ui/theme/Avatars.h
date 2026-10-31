// MEON ui - 프로필 사진(아바타) 한 곳.
//   - 사진이 없으면 디자인 기본 이미지 (합주 최종 v2 avatar-default.png, 256×256, images/brand/avatar). 파트 아이콘 아바타는 쓰지 않는다
//   - 원으로 잘라 그린다. 기본 이미지는 #EBEBEB 1px 테두리, 사진은 원을 다 덮는다 (디자인: 사진 inset -1px)
//   - 내 사진: 이 컴퓨터에만 (설정 › 프로필에서 고른 사진, 서버에 올리지 않는다). 내 화면의 내 자리에만 보인다
//   - 다른 사람 사진: 서버가 사진 주소를 주게 되면 setRemoteUrl 로 넣는 자리 (지금은 늘 기본 이미지)
//   - 크기별로 줄인 그림을 기억해 두고 붙인다 (그릴 때마다 줄이지 않는다)
#pragma once

#include <JuceHeader.h>

namespace meon::ui::avatars
{

/** 디자인 기본 이미지 (256×256) */
const juce::Image& defaultImage();

/** 이 사용자의 사진. 없으면 빈 그림 (그릴 때 기본 이미지) */
juce::Image photoFor (const juce::String& userId);

/** 원 안에 그 사용자의 사진(없으면 기본 이미지)을 그린다. opacity: 오프라인 등 흐리게 */
void paint (juce::Graphics&, juce::Rectangle<float> circle, const juce::String& userId, float opacity = 1.0f);

/** 내 id 와 내 사진 (빈 그림 = 사진 없음). 바뀌면 changes() 로 알린다 */
void setMine (const juce::String& userId, const juce::Image& photo);
juce::String myId();
bool hasMyPhoto();

/** 다른 사람 사진 주소 (서버가 주게 되면). 지금은 주소만 기억하고 그림은 받지 않는다 */
void setRemoteUrl (const juce::String& userId, const juce::String& url);
juce::String remoteUrl (const juce::String& userId);

/** 사진이 바뀌면 (내 사진 올리기·바꾸기·지우기) 알린다: 아바타를 그리는 화면이 다시 그린다 */
juce::ChangeBroadcaster& changes();

} // namespace meon::ui::avatars
