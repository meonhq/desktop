// MEON ui - 개발용 합주 확인 화면 (--dev-jam, 개발 빌드만). R4 의 새 합주 로직(JamSession)을 두 컴퓨터로 확인한다.
// 비공개 방 만들기 / 코드로 입장 / 나가기, 멤버 상태(연결 중·연결됨·끊김·연결 실패)·핑·끊김 횟수·미터·음소거·볼륨,
// 내 입력 미터·송출 정지·입력 채널, 간단한 채팅(전달 실패 표시), 세션 로그 위치.
// 디자인 화면이 아니다 (R6 에서 진짜 합주 화면이 들어온다).
#pragma once

#include "../widgets/Button.h"
#include "../widgets/Inputs.h"
#include "../widgets/Controls.h"
#include "../widgets/Meter.h"
#include "../widgets/Display.h"
#include "../../core/MeonCore.h"

namespace meon::ui
{

class DevJamScreen : public juce::Component,
                     private core::JamSession::Listener,
                     private core::AppStore::Listener,
                     private core::ChatChannel::Listener,
                     private juce::Timer
{
public:
    explicit DevJamScreen (core::MeonCore&);
    ~DevJamScreen() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    core::MeonCore& core;
    core::JamSession& jam;

    Text title { TXT ("개발용 합주 확인 (--dev-jam)"), 600, 18.0f };
    Text account { {}, 400, 12.0f, col::inkSub };
    Text status { {}, 400, 12.0f, col::inkSub };
    Button create { Button::Kind::primary, TXT ("비공개 방 만들기") };
    OtpInput code { OtpInput::Charset::roomCode, OtpInput::Size::small };
    Button join { Button::Kind::secondary, TXT ("코드로 입장") };
    Button leave { Button::Kind::secondary, TXT ("방 나가기") };
    Text joinNote { {}, 400, 12.0f, col::inkFaint };
    Text roomInfo { {}, 600, 15.0f };

    // 나
    Text meTitle { TXT ("내 오디오"), 600, 15.0f };
    Button sendMute { Button::Kind::small, TXT ("송출 정지") };
    Meter inputMeter, sendMeter, monitorMeter;
    Text inputLabel { TXT ("입력 (처리 뒤)"), 400, 12.0f, col::inkSub }, sendLabel { TXT ("송신"), 400, 12.0f, col::inkSub },
         monitorLabel { TXT ("모니터"), 400, 12.0f, col::inkSub };
    Select inputStart;
    Choice inputMode { Choice::Style::small };
    Button audioSettings { Button::Kind::small, TXT ("오디오 장치…") };
    Choice bufferMode { Choice::Style::small };
    Text selfMonLabel { TXT ("셀프 모니터"), 400, 12.0f, col::inkSub };
    Toggle selfMonitor;
    DbSlider master { -30.0f, 6.0f };
    Text masterLabel { TXT ("마스터"), 400, 12.0f, col::inkSub };

    // 멤버
    struct Row;
    juce::OwnedArray<Row> rows;

    // 채팅
    TextInput chatInput { TXT ("메시지 입력 (Enter)") };
    Text chatNote { {}, 400, 12.0f, col::inkFaint };
    juce::StringArray chatLines;
    juce::String failedId;
    Button resend { Button::Kind::small, TXT ("다시 보내기") };

    juce::String lastDevLine;
    bool devChatSent = false;
    Text logPath { {}, 400, 12.0f, col::inkFaint };
    Button openLogs { Button::Kind::small, TXT ("로그 폴더 열기") };

    void createRoom();
    void joinCode();
    void leaveRoom();
    void rebuildRows();
    void refresh();
    void refreshChat();
    void openAudioSettings();

    void jamPhaseChanged (core::JamSession::Phase) override { refresh(); }
    void jamMembersChanged() override { rebuildRows(); refresh(); }
    void jamStatsChanged() override;
    void stateChanged (core::StateKey) override { refresh(); }
    void chatChanged() override { refreshChat(); }
    void timerCallback() override { refresh(); }
};

} // namespace meon::ui
