// MEON - 로컬 사용자 설정 (닉네임, 파트, 첫 실행 완료 여부, 입력 채널, 귀 테러 방지). 독립 앱과 플러그인이 같은 파일을 공유한다.
#pragma once

#include <JuceHeader.h>
#include "MeonEarGuard.h"

namespace meon
{

class MeonSettings
{
public:
    MeonSettings();
    ~MeonSettings();

    juce::String getNickname() const;
    void setNickname (const juce::String& name);

    /** 파트 키 ("vocal" "guitar" … parts::key). 고른 적 없으면 빈 문자열 */
    juce::String getPartKey() const;
    void setPartKey (const juce::String& key);

    /** 첫 실행 흐름을 끝냈는지. 독립 앱과 플러그인은 따로 기록한다 (헤드폰 확인 단계가 다르므로). */
    bool isOnboardingDone (bool plugin) const;
    void setOnboardingDone (bool plugin, bool done);

    /** 독립 앱 입력 채널 (0 기반 시작 채널, 채널 수 1 또는 2) */
    int getInputChannelStart() const;
    int getInputChannelCount() const;
    void setInputChannels (int start, int count);

    /** 귀 테러 방지 단계 (끔 · 보통 · 강하게). 기본 보통 */
    EarGuard::Level getEarGuardLevel() const;
    void setEarGuardLevel (EarGuard::Level level);

    bool isChatOpen() const;
    void setChatOpen (bool open);

    /** 자동 업데이트 채널 "stable" | "beta" (배포 설계 D6). 처음 이 값을 정할 때 설정 파일이 이미 있었으면(브리지 전부터 쓰던 앱,
        출시 전 테스터) beta, 새로 설치했으면 stable. 바꾸는 화면은 아직 없다 (설정 파일의 updateChannel) */
    juce::String getUpdateChannel() const;

    void save();

    /** 리메이크 core 가 쓰는 키 (sendMuted, sendGainDb, bufferMode, selfMonitorApp/Plugin, keepMasterVolume, masterVolumeDb …) */
    juce::PropertiesFile& properties() { return *props; }

    static juce::File getSettingsFolder();
    static juce::File getSettingsFile();
    static juce::File getLogFolder();

    /** 닉네임 규칙: 한글·영문·숫자·공백·특수문자(키보드 기호, 한글 입력기 특수문자) 2–12자. 이모지·제어 문자 제외 */
    static bool isValidNickname (const juce::String& name);

private:
    std::unique_ptr<juce::PropertiesFile> props;
};

} // namespace meon
