// MEON 브리지 실험 (MEON_BRIDGE_SPIKE): 독립 앱 쪽.
// 앱이 자기 오디오 장치를 여는 대신, 플러그인이 넘겨주는 블록을 박자 삼아 도는 가짜 오디오 장치("MEON Bridge")를 고른다.
// 앱의 기존 오디오 경로(AudioDeviceManager → AudioProcessorPlayer → SonoBus 프로세서)는 그대로 쓴다. 엔진 코드는 고치지 않는다.
#pragma once

#include <JuceHeader.h>

namespace meon::bridge
{
/** MEON Bridge 장치 종류를 (없으면) 등록하고 그 이름을 돌려준다 */
juce::String registerDeviceType (juce::AudioDeviceManager&);

/** 독립 앱 셸이 장치 관리자를 연결한 뒤 메시지 스레드에서 부른다 (아직이면 잠시 뒤 다시 시도한다).
    MEON Bridge 장치를 등록하고 고른 뒤, 상태 창을 띄운다. 여러 번 불러도 한 번만 한다. */
void installAppSide();
}
