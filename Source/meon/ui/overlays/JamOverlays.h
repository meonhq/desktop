// MEON ui - 합주 화면의 겹침 요소 (REMAKE_PLAN 3.3) + 프로필(홈·합주 공용).
// 방 설정(460) · 멤버 관리(340) · 입장 요청 목록(팝오버 340) · 프로필(340) · "방을 닫았어요" 덮개.
#pragma once

#include "HomeOverlays.h"

namespace meon::ui
{

//==============================================================================
/** 방 설정 (방장). 이름·소개·공개 설정·입장 방식(공개만)·최대 인원(지금 인원보다 작은 값 비활성), 방 닫기(두 번), 저장(바뀐 게 있을 때만) */
class RoomSettingsModal : public Modal
{
public:
    explicit RoomSettingsModal (core::MeonCore&);

private:
    core::MeonCore& core;
    core::RoomDetail room;
    struct Body : juce::Component
    {
        explicit Body (RoomSettingsModal& m) : owner (m) {}
        void paint (juce::Graphics&) override;
        void resized() override;
        RoomSettingsModal& owner;
    } body { *this };
    TextInput name, desc { TXT ("어떤 곡을, 어떤 분위기로 하는지 적어주세요"), true };
    Choice visibility { Choice::Style::chip, false, 80 }, entry { Choice::Style::chip, false, 80 };
    Select capacity;
    Button closeRoom { Button::Kind::dangerOutline, TXT ("방 닫기") };
    Button cancel { Button::Kind::cancel, TXT ("취소") }, save { Button::Kind::primary, TXT ("저장") };
    ModalFooter footer;
    bool busy = false;
    std::shared_ptr<bool> alive { std::make_shared<bool> (true) };

    bool isPublic() const { return visibility.getSelected() == "public"; }
    bool isDirty() const;
    juce::String visNote() const;
    int bodyHeight() const;
    void refresh();
    void submit();
};

//==============================================================================
/** 멤버 관리 (방장): 아바타·이름·파트, 방장 넘기기(두 번), 내보내기(두 번) */
class ManageMemberModal : public Modal
{
public:
    ManageMemberModal (core::MeonCore&, const core::JamSession::Member&);

private:
    core::MeonCore& core;
    core::JamSession::Member member;
    struct Body : juce::Component
    {
        explicit Body (ManageMemberModal& m) : owner (m) {}
        void paint (juce::Graphics&) override;
        void resized() override;
        ManageMemberModal& owner;
    } body { *this };
    Avatar avatar { 48.0f, 24.0f };
    Button hand { Button::Kind::small, TXT ("방장 넘기기") }, kick { Button::Kind::smallDanger, TXT ("내보내기") };
};

//==============================================================================
/** 입장 요청 팝오버 내용 (방장 + 승인 방). 가득 차면 수락 비활성 + 안내 */
class JoinRequestList : public juce::Component, private core::AppStore::Listener
{
public:
    explicit JoinRequestList (core::MeonCore&);
    ~JoinRequestList() override;
    int getPreferredHeight() const;
    std::function<void()> onSizeChanged;
    std::function<void (const core::UserPublic&)> onOpenProfile;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    core::MeonCore& core;
    struct Row;
    juce::OwnedArray<Row> rows;
    bool isFull() const;
    void rebuild();
    void stateChanged (core::StateKey) override;
};

//==============================================================================
/** 프로필 (340). 관계별 버튼은 API 2.4 (can_send_friend_request, can_invite, relation), Q28 */
class ProfileModal : public Modal, private core::AppStore::Listener
{
public:
    ProfileModal (core::MeonCore&, const core::UserPublic&, bool inJam);
    ~ProfileModal() override;

private:
    core::MeonCore& core;
    core::UserPublic user;
    const bool inJam;
    std::optional<core::UserProfile> profile;
    struct Body : juce::Component
    {
        explicit Body (ProfileModal& m) : owner (m) {}
        void paint (juce::Graphics&) override;
        void resized() override;
        ProfileModal& owner;
    } body { *this };
    Avatar avatar { 48.0f, 24.0f };
    Tag friendTag { TXT ("친구") };
    Button remove { Button::Kind::smallDanger, TXT ("친구 삭제") }, block { Button::Kind::smallDanger, TXT ("차단") };
    Button unblock { Button::Kind::small, TXT ("차단 해제") };
    Button request { Button::Kind::primary, TXT ("친구 요청") }, joinRoom { Button::Kind::primary, TXT ("같은 방 참여") };
    Button decline { Button::Kind::cancel, TXT ("거절") }, accept { Button::Kind::primary, TXT ("수락") };
    ModalFooter footer;
    juce::String note;
    std::shared_ptr<bool> alive { std::make_shared<bool> (true) };

    void load();
    core::Callback<core::Done> reloader();
    void refresh();
    juce::String statusText() const;
    juce::String lastSeenText() const;
    juce::String incomingRequestId() const;
    bool hasFooter() const;
    void stateChanged (core::StateKey) override;
};

//==============================================================================
/** 방장이 방을 닫은 뒤 전체 덮개 (#FCFCFC): "방을 닫았어요 / 멤버 모두 홈으로 돌아갔어요 / 홈으로" */
class RoomClosedOverlay : public juce::Component
{
public:
    RoomClosedOverlay();
    std::function<void()> onHome;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    Button home { Button::Kind::primary, TXT ("홈으로") };
};

} // namespace meon::ui
