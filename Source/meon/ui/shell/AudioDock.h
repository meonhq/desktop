// MEON ui - 내 오디오 · 모니터 카드 묶음 (방 화면 아래쪽, 방에 있으면 공개 방 목록 위에도. 홈·사이드바 디자인 v2.1).
//   카드 하나(이 구성 요소)를 두 화면이 같이 쓴다: AppRoot 가 갖고, 방 화면에서는 방 화면이 비워 둔 자리(dockSlot)에,
//   공개 방에서는 목록 영역 안 왼쪽·오른쪽·아래 18 에 둔다. 오갈 때는 AppRoot 가 이 카드를 옮기며 폭·그림자를 잇는다.
//   오디오 상태는 화면 밖 JamSession 에 있다 (송출 정지·송출 레벨 / 전체 음소거·마스터 볼륨). 이 카드는 읽고 쓰기만 한다.
//   - 카드 사이 9 와 카드 바깥(그림자 자리)은 누름을 받지 않는다: 뒤의 방 카드가 눌린다
//   - 다시 그리기: 계속 바뀌는 것은 미터뿐이다. 미터 상자는 이 구성 요소의 형제(부모의 자식, 바로 위)로 두고 불투명으로
//     자기 배경까지 칠한다. 그래서 미터가 다시 그려져도 카드·뒤의 방 목록·방 화면은 그리지 않는다 (JUCE 는 위의 불투명 형제
//     자리를 아래 구성 요소 그리기에서 뺀다. 자식이면 부모가 같이 그려진다)
//   - 그림자 (공개 방): 팝오버 그림자. 높이·배율이 그대로면 다시 계산하지 않는다 (StretchShadow).
//     그림자는 카드 바깥까지 그리므로, 카드 크기·자리가 바뀌는 프레임마다 이전·새 자리를 합친 영역 + 그림자 폭을 다시 그린다
//     (JUCE 는 카드 자리만 다시 그린다: 그것만으로는 이전 그림자 끝이 잔상으로 남았다)
//   - 접기·펼치기 (옆으로, 방 화면과 같은 규칙·애니메이션)
#pragma once

#include "../widgets/Button.h"
#include "../widgets/Controls.h"
#include "../widgets/Display.h"
#include "../widgets/Meter.h"
#include "../widgets/FoldAnimator.h"
#include "../theme/PopoverSurface.h"
#include "../../core/MeonCore.h"

namespace meon::ui
{

class AudioDock : public juce::Component,
                  private core::JamSession::Listener
{
public:
    AudioDock (core::MeonCore&, core::JamSession&);
    ~AudioDock() override;

    /** 카드 높이 (머리말 48 + 구분선 1 + 줄 43): 방 화면 자리·공개 방 목록 아래 여백이 이 값을 잰다 */
    static constexpr int cardsHeight = 48 + 1 + 43;
    int getCardsHeight() const { return cardsHeight; }

    /** 카드 두 장이 놓일 자리 (부모 좌표). 구성 요소는 그림자 폭만큼 사방으로 크다 */
    void setCardsBounds (juce::Rectangle<int> cardsInParent);
    juce::Rectangle<int> getCardsBounds() const { return getBounds().reduced (PopoverSurface::shadowMargin()); }
    /** 그림자 진하기 0 (방 화면) ~ 1 (공개 방) */
    void setShadow (float);
    float getShadow() const { return shadow; }

    /** JamSession 값을 다시 읽어 보인다 (단축키 M·N·⌘↑↓ 등 바깥에서 바꾼 뒤) */
    void refresh();
    /** 미터 상자를 이 구성 요소 바로 위로 (부모가 쌓는 순서를 바꾼 뒤) */
    void raiseMeters();

    // 접기 (방 화면과 같은 규칙)
    bool isSendOpen() const { return sendOpen; }
    bool isMonOpen() const { return monOpen; }
    void devToggleFold (const juce::String& panel);   // "send" · "mon"
    void previewFold (const juce::String& panel, float e);
    bool isFoldMoving() const { return fold.anyMoving(); }

    // 개발용 (자가 테스트·다시 그리기 확인)
    Card& devSendCard() { return sendCard; }
    Card& devMonCard() { return monCard; }
    MeterBox& devInputMeter() { return inputMeter; }
    MeterBox& devOutputMeter() { return outputMeter; }
    const StretchShadow& devShadow(int i) const { return shadows[(size_t) i]; }
    /** 소리 없이도 미터가 움직이게 (가짜 레벨: 다시 그리기 확인용) */
    void devDriveMeters (bool);
    static int devPaintCount;   // 이 구성 요소의 paint (그림자) 횟수

    bool hitTest (int x, int y) override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void moved() override;
    void parentHierarchyChanged() override;
    void visibilityChanged() override;

private:
    friend class UiSelfTest;
    core::MeonCore& core;
    core::JamSession& jam;

    Card sendCard, monCard;
    Button sendMute { Button::Kind::small, TXT ("송출 정지") }, monMute { Button::Kind::small, TXT ("전체 음소거") };
    DbSlider sendGain { -30.0f, 10.0f }, master { -30.0f, 6.0f };
    /** 미터 상자: 불투명 (카드 면 #FFFFFF 까지 칠한다). 이 구성 요소의 부모에 붙는다 */
    struct OpaqueMeterBox : MeterBox
    {
        explicit OpaqueMeterBox (const juce::String& tip) : MeterBox (tip) { setOpaque (true); }
        void paint (juce::Graphics& g) override;
    };
    OpaqueMeterBox inputMeter { TXT ("입력 미터") }, outputMeter { TXT ("출력 미터") };
    bool sendOpen = true, monOpen = true;
    float shadow = 0.0f;
    std::array<StretchShadow, 2> shadows;
    bool devDrive = false;

    FoldAnimator fold { *this, [this] { resized(); } };
    const int foldSend = fold.add ("send", true), foldMon = fold.add ("mon", true);
    int foldSendW = 0, foldMonW = 0, bottomAvail = 0;
    bool foldFillH = true;
    std::array<juce::Rectangle<int>, 2> meterRects;   // 이 구성 요소 좌표

    juce::Rectangle<int> cards() const { return getLocalBounds().reduced (PopoverSurface::shadowMargin()); }
    void toggleFold (int panel, bool open);
    void placeMeters();
    void setMeterSources();
    void jamStatsChanged() override;
};

} // namespace meon::ui
