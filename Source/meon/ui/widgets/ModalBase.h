// MEON ui - 모든 모달의 공통 래퍼: 창 전체 덮개 + 가운데 상자, 열고 닫는 애니메이션 (홈·사이드바 디자인 v2.1 mEl).
//   열기: 덮개 투명 → 불투명 200ms ease-out / 상자 8px 아래·98%·투명 → 제자리·100%·불투명 240ms cubic-bezier(0.2, 0, 0, 1)
//   닫기: 상자 → 4px 아래·98%·투명 160ms (같은 곡선) / 덮개 → 투명 180ms ease-in
//   - 상자는 그리기(paintDialog)와 자식 구성 요소 모두에 같은 변환·투명도를 건다. 열기가 끝나면 변환을 정확히 없앤다 (100%: 글자가 흐리지 않게)
//   - 닫기는 모달이 지워지기 직전 모습을 그림으로 떠서(덮개 없이 상자만) 같은 부모에 남기는 '닫히는 그림'이 맡는다.
//     누름을 받지 않고, 끝나면 스스로 지워진다. 모달 자리(ModalSlot)가 지우기 전에 부르므로 Esc·닫기 버튼·바깥 누름·
//     다른 화면으로 가며 닫기 등 어떤 길로 닫혀도 같다
//   - 운영체제의 동작 줄이기가 켜져 있으면 둘 다 없이 바로 (MeonReduceMotion)
//   쓰는 곳: Modal (친구 추가·방 만들기·코드로 입장·프로필·필터·나가기 확인·방 설정·멤버 관리), SettingsModal (설정)
#pragma once

#include <JuceHeader.h>
#include <memory>

namespace meon::ui
{

class ModalBase : public juce::Component
{
public:
    ModalBase();
    ~ModalBase() override;

    /** 가운데 상자 자리 (이 구성 요소 좌표) */
    virtual juce::Rectangle<int> dialogBounds() const = 0;
    /** 덮개 자리 (기본: 전체) */
    virtual juce::Rectangle<int> dimBounds() const { return getLocalBounds(); }
    /** 상자 그리기 (덮개 없이). 열고 닫는 동안의 변환·투명도는 래퍼가 건다 */
    virtual void paintDialog (juce::Graphics&) = 0;

    void paint (juce::Graphics&) final;
    void parentHierarchyChanged() override;
    void visibilityChanged() override;

    /** 닫는 애니메이션을 남긴다 (곧 지울 때 부른다). 동작 줄이기·부모가 사라지는 중이면 아무것도 하지 않는다 */
    void playCloseAnimation();

    /** 열리는 중 */
    bool isOpening() const { return opening; }
    /** 개발용 (화면 자가 테스트): 지금 덮개·상자 진행 (덮개 투명도, 상자 투명도, 아래로 밀린 px, 크기 배율) */
    struct State { float dim = 1.0f, box = 1.0f, dy = 0.0f, scale = 1.0f; };
    State getAnimState() const { return state; }

    /** 부모(창 맨 위 구성 요소)가 사라지는 중이면 닫히는 그림을 남기지 않는다: 부모의 속성에 이 이름을 true 로 */
    static constexpr const char* noCloseAnimationProperty = "meonNoCloseAnimation";

private:
    // 프레임: VBlank, 없을 때(창이 가려짐 등)도 끝나도록 60Hz 타이머를 같이 (진행은 경과 시간으로 계산하므로 두 번 불려도 같다)
    struct Fallback : juce::Timer
    {
        explicit Fallback (ModalBase& o) : owner (o) {}
        void timerCallback() override { owner.frame(); }
        ModalBase& owner;
    };
    Fallback fallback { *this };
    std::unique_ptr<juce::VBlankAttachment> vblank;
    bool started = false, opening = false;
    double startMs = 0.0;
    State state;
    std::vector<std::pair<juce::Component::SafePointer<juce::Component>, float>> baseAlpha;   // 자식 원래 투명도

    void maybeStart();
    void frame();
    void applyToChildren();
    void finishOpen();
    juce::AffineTransform boxTransform() const;
};

/** 모달 자리: unique_ptr 처럼 쓰고, 지울 때(reset·nullptr·덮어쓰기·주인이 사라질 때) 닫는 애니메이션을 남긴다 */
template <typename T>
class ModalSlot
{
public:
    ModalSlot() = default;
    ~ModalSlot() { close(); }
    ModalSlot (const ModalSlot&) = delete;
    ModalSlot& operator= (const ModalSlot&) = delete;

    template <typename U>
    ModalSlot& operator= (std::unique_ptr<U> m) { close(); p = std::move (m); return *this; }
    ModalSlot& operator= (std::nullptr_t) { close(); return *this; }
    void reset() { close(); }

    T* get() const noexcept { return p.get(); }
    T* operator->() const noexcept { return p.get(); }
    T& operator*() const noexcept { return *p; }
    explicit operator bool() const noexcept { return p != nullptr; }
    bool operator== (std::nullptr_t) const noexcept { return p == nullptr; }
    bool operator!= (std::nullptr_t) const noexcept { return p != nullptr; }

private:
    std::unique_ptr<T> p;
    void close()
    {
        if (p == nullptr) return;
        auto old = std::move (p);   // 지우는 동안 자리는 이미 비어 있다 (unique_ptr::reset 과 같은 순서)
        old->playCloseAnimation();
    }
};

} // namespace meon::ui
