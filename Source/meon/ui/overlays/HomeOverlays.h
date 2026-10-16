// MEON ui - 홈의 겹침 요소 (REMAKE_PLAN 3.2): 코드로 입장·방 필터·새 합주실 모달, 입장 실패 상자, 알림 목록, 토스트.
// 명령은 core::HomeController 로 보내고, 결과 문구는 ui::msg 에서 고른다.
#pragma once

#include "../widgets/Button.h"
#include "../widgets/Inputs.h"
#include "../widgets/Controls.h"
#include "../widgets/Display.h"
#include "../../core/MeonCore.h"
#include "../../core/model/RoomFilter.h"

namespace meon::ui
{

/** 모달 본문 아래의 버튼 줄 (구분선 + 12·18 여백, 오른쪽 정렬, 간격 9) */
class ModalFooter : public juce::Component
{
public:
    static constexpr int height = 57;
    void setButtons (std::initializer_list<Button*>);
    /** 왼쪽 글자 (오류 등) */
    void setNote (const juce::String&, juce::Colour);
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    std::vector<Button*> buttons;
    juce::String note;
    juce::Colour noteInk;
};

//==============================================================================
/** 코드로 입장 (308). 6칸, "방장에게 받은 6자리 코드를 입력하세요" / 오류 "이 코드로 열린 방이 없어요", 취소·입장("확인 중…") */
class JoinCodeModal : public Modal
{
public:
    explicit JoinCodeModal (core::MeonCore&);
    /** 칸 아래 오류가 아닌 실패 (가득 참·닫힘·연결·버전 …): 모달을 닫고 입장 실패 상자로 */
    std::function<void (const core::ApiError&)> onFailure;
    void focusCode() { code.focusFirst(); }

private:
    core::MeonCore& core;
    struct Body : juce::Component
    {
        explicit Body (JoinCodeModal& m) : owner (m) {}
        void paint (juce::Graphics&) override;
        void resized() override;
        JoinCodeModal& owner;
    } body { *this };
    OtpInput code { OtpInput::Charset::roomCode, OtpInput::Size::medium };
    Button cancel { Button::Kind::cancel, TXT ("취소") }, join { Button::Kind::primary, TXT ("입장") };
    ModalFooter footer;
    bool busy = false, error = false;
    std::shared_ptr<bool> alive { std::make_shared<bool> (true) };

    void submit();
    void refresh();
};

//==============================================================================
/** 방 필터 (420). 비어 있는 파트(다중), 지연(전체/10/20ms), 빈자리만, 친구 있는 방만, 초기화, "방 N개 보기" */
class FilterModal : public Modal
{
public:
    FilterModal (core::MeonCore&, const core::RoomFilter& applied);
    std::function<void (const core::RoomFilter&)> onApply;

private:
    core::MeonCore& core;
    core::RoomFilter draft;
    struct Body : juce::Component
    {
        explicit Body (FilterModal& m) : owner (m) {}
        void paint (juce::Graphics&) override;
        void resized() override;
        FilterModal& owner;
    } body { *this };
    Choice parts { Choice::Style::chip, true }, ping { Choice::Style::chip };
    Toggle onlyRoom, onlyFriends;
    Button reset { Button::Kind::small, TXT ("초기화") };
    Button cancel { Button::Kind::cancel, TXT ("취소") }, apply { Button::Kind::primary };
    ModalFooter footer;

    void readControls();
    void refresh();
};

//==============================================================================
/** 새 합주실 (460). 방 이름(30), 소개(80, 2줄), 공개/비공개, 입장 방식(공개만), 최대 인원 2~7, 방 만들기("만드는 중…") */
class CreateRoomModal : public Modal
{
public:
    explicit CreateRoomModal (core::MeonCore&);
    void focusName() { name.focus(); }

private:
    core::MeonCore& core;
    struct Body : juce::Component
    {
        explicit Body (CreateRoomModal& m) : owner (m) {}
        void paint (juce::Graphics&) override;
        void resized() override;
        CreateRoomModal& owner;
    } body { *this };
    TextInput name { TXT ("예: 금요일 밤 홍대 밴드 합주") };
    TextInput desc { TXT ("어떤 곡을, 어떤 분위기로 하는지 적어주세요"), true };
    Choice visibility { Choice::Style::chip, false, 80 }, entry { Choice::Style::chip, false, 80 };
    Select capacity;
    Button cancel { Button::Kind::cancel, TXT ("취소") }, create { Button::Kind::primary, TXT ("방 만들기") };
    ModalFooter footer;
    bool busy = false;
    std::shared_ptr<bool> alive { std::make_shared<bool> (true) };

    bool isPublic() const { return visibility.getSelected() == "public"; }
    int bodyHeight() const;
    void submit();
    void refresh();
};

//==============================================================================
/** 입장 실패 (380, 덮개 없음, 테두리 #D5D5D5). 제목 + 닫기, 본문, 버전 다름이면 "업데이트" */
class JoinFailDialog : public juce::Component
{
public:
    JoinFailDialog();
    void show (core::HomeController::JoinFailure);
    std::function<void()> onClose, onUpdate;
    int getPreferredHeight() const;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::String title, body;
    bool showUpdate = false;
    IconButton close;
    Button update { Button::Kind::smallPrimary, TXT ("업데이트") };
    int bodyHeight() const;
};

//==============================================================================
/** 알림 팝오버의 내용: 받은 친구 요청 (아이콘, 이름, "친구 요청 · 파트", 거절·수락). 비면 "새 알림이 없어요" */
class NotificationList : public juce::Component, private core::AppStore::Listener
{
public:
    explicit NotificationList (core::MeonCore&);
    ~NotificationList() override;
    int getPreferredHeight() const;
    std::function<void()> onSizeChanged;
    std::function<void (const core::UserPublic&)> onOpenProfile;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    core::MeonCore& core;
    struct Row;
    juce::OwnedArray<Row> rows;
    void rebuild();
    void stateChanged (core::StateKey) override;
};

//==============================================================================
/** 토스트 (280, 테두리 #D5D5D5, 여백 12 12 12 18). 굵은 줄 + 보조 줄, 선택 버튼(입장), 닫기 */
class Toast : public juce::Component
{
public:
    /** normal: 흰 면 (초대·알림). warning: 무음 경고 줄과 같은 연한 노란 면(col::accentSoft), 제목(굵게) + 설명, 설명은 줄바꿈 */
    enum class Kind { normal, warning };

    Toast (const juce::String& title, const juce::String& sub, const juce::String& action = {});
    static constexpr int width = 280, height = 60;   // height: 한 줄 설명까지의 높이 (경고 설명이 길면 heightForWidth 로 늘어난다)
    std::function<void()> onAction, onClose;
    /** 종류와 문구를 바꾼다 (연결 끊김 토스트가 같은 자리에서 "다시 연결됐어요" 로 바뀔 때) */
    void setContent (Kind k, const juce::String& t, const juce::String& s) { kind = k; title = t; sub = s; resized(); repaint(); }
    int heightForWidth (int w) const;
    /** 닫기 버튼을 숨긴다 (연결이 끊긴 동안의 토스트: 다시 붙으면 저절로 바뀐다) */
    void setClosable (bool c) { close.setVisible (c); repaint(); }
    juce::String key;   // 초대 ID 등 (지울 때 찾는 용도)
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Kind kind = Kind::normal;
    juce::String title, sub;
    std::unique_ptr<Button> action;
    int textRight (int w) const;
    float subHeight (int w) const;
    IconButton close;
};

} // namespace meon::ui
