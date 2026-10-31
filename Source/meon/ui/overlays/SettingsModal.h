// MEON ui - 설정 (디자인 860×620, 왼쪽 탭 8개, 플러그인은 오디오 탭 없음). REMAKE_PLAN 3.4.
// 계정 · 프로필 · 오디오(앱만) · 합주 · 친구·개인정보 · 알림 · 단축키 · 정보
#pragma once

#include "../widgets/Button.h"
#include "../widgets/Inputs.h"
#include "../widgets/Controls.h"
#include "../widgets/Display.h"
#include "../widgets/Meter.h"
#include "../theme/Icons.h"
#include "../../core/MeonCore.h"

namespace meon::ui
{

class SettingsModal : public ModalBase, private juce::Timer
{
public:
    /** 업데이트 (독립 앱의 지금 업데이터). 플러그인은 비워 둔다 (Q18: 버튼 없이 안내만) */
    struct UpdateHooks
    {
        enum class State { idle, checking, upToDate, available, downloading, installing, failed };
        /** 다시 시도해도 풀리지 않는 이유: needsMove (macOS: 앱 자리에 쓸 수 없음), osTooOld (새 빌드의 최소 OS 보다 낮음) */
        enum class Problem { none, needsMove, osTooOld };
        std::function<State()> state;
        std::function<float()> progress;
        std::function<int()> latestBuild;
        std::function<Problem()> problem;
        std::function<void()> check, start;
    };

    enum class Tab { account, profile, audio, jam, privacy, notif, keys, about };

    SettingsModal (core::MeonCore&, UpdateHooks, Tab first = Tab::account);
    ~SettingsModal() override;

    std::function<void()> onClose;
    void showTab (Tab);
    Tab currentTab() const { return tab; }
    /** 개발용 (화면 자가 테스트): 프로필 탭에서 파일을 고른 것처럼 (파일 선택 창 없이) */
    void devPickPhoto (const juce::File&);
    /** 개발용: 프로필 사진 줄의 설명·버튼 글자 ("설명|올리기|기본 이미지로 보임 0/1") */
    juce::String devPhotoRow() const;

    juce::Rectangle<int> dialogBounds() const override { return box(); }
    void paintDialog (juce::Graphics&) override;   // 덮개·애니메이션은 공통 래퍼 (ModalBase)
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    static juce::String tabTitle (Tab);
    static constexpr int boxW = 860, boxH = 620, tabsW = 200;

    /** 설정 행 (제목 500 14px + 설명 400 12px/1.5, 오른쪽에 조작, 아래에 펼침 영역). 페이지들이 쓴다 */
    struct Row;
    struct Page;

private:
    friend class UiSelfTest;   // 개발용 화면 자가 테스트 (닫기 버튼)
    core::MeonCore& core;
    UpdateHooks update;
    const bool plugin;
    Tab tab = Tab::account;
    std::vector<Tab> tabs;
    int hoverTab = -1;
    IconButton close { (int) icons::Id::close12, IconButton::Style::plain24, 10.0f };
    juce::Viewport viewport;
    std::unique_ptr<Page> page;

    juce::Rectangle<int> box() const;
    juce::Rectangle<int> tabBounds (int index) const;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void timerCallback() override;
    void layoutPage();
};

} // namespace meon::ui
