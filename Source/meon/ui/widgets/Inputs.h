// MEON ui - 입력: 한 줄 입력, 두 줄 입력, 6칸 코드, 선택 상자, 체크 상자.
#pragma once

#include "../theme/Tokens.h"

namespace meon::ui
{

/** 한 줄 입력 (높이 32, 여백 0 12, 글자 400 13px, 테두리 #D5D5D5 / 오류 #D92D20, 모서리 6).
    TextEditor 를 담고 테두리·placeholder 를 직접 그린다. 한글 입력기는 TextEditor 가 처리한다. */
class TextInput : public juce::Component, private juce::TextEditor::Listener
{
public:
    explicit TextInput (const juce::String& placeholder = {}, bool multiLine = false);

    void setText (const juce::String&, bool notify = false);
    juce::String getText() const;
    void setPlaceholder (const juce::String&);
    void setError (bool);
    bool hasError() const { return error; }
    void setPassword (bool);
    /** 글자 수 제한 (코드포인트). 0 이면 없음 */
    void setMaxLength (int);
    /** 바이트 제한 (UTF-8, 채팅 1,000 B). 0 이면 없음 */
    void setMaxBytes (int);
    /** 입력할 때 바꿔 넣기 (예: 아이디 소문자) */
    std::function<juce::String (const juce::String&)> transform;
    void setFontPx (float px, int weight = 400);
    void setReadOnly (bool);
    /** 테두리·여백 없는 입력 (홈의 친구 추가) */
    void setBorderless (bool);
    void focus();

    juce::TextEditor& editor() { return ed; }

    std::function<void()> onChange, onReturn, onEscape, onFocusLost;

    void paint (juce::Graphics&) override;
    void resized() override;
    void enablementChanged() override;

private:
    juce::TextEditor ed;
    juce::String placeholder;
    bool error = false, multi, borderless = false;
    int maxLength = 0, maxBytes = 0;
    float fontPx = 13.0f;
    int weight = 400;
    bool applying = false;

    void textEditorTextChanged (juce::TextEditor&) override;
    void textEditorReturnKeyPressed (juce::TextEditor&) override;
    void textEditorEscapeKeyPressed (juce::TextEditor&) override;
    void textEditorFocusLost (juce::TextEditor&) override;
};

/** 6칸 코드 입력 (방 입장 코드, 이메일 인증 코드). 디자인의 칸별 입력 동작을 그대로:
    한 칸 입력 → 다음 칸, Backspace(빈 칸이면 앞 칸 지우고 이동), ←/→ 이동, 붙여넣기 6자, Enter 제출.
    한글 자판이 켜져 있어도 두벌식 자모를 영문 키로 바꿔 받는다. */
class OtpInput : public juce::Component
{
public:
    enum class Charset { roomCode, digits };
    enum class Size { large, medium, small };   // 44×52 (온보딩) / 40×48 (코드로 입장) / 36×44 (설정)

    OtpInput (Charset, Size);

    juce::String getCode() const;               // 끝의 빈 칸은 뺀다
    bool isComplete() const;
    void setCode (const juce::String&);
    void clear();
    void setError (bool);
    void focusFirst();

    int getPreferredWidth() const;
    int getPreferredHeight() const;

    std::function<void()> onChange, onSubmit;
    /** 받지 않는 글자를 눌렀을 때 (방 코드의 0·1·O·I 안내용) */
    std::function<void (juce::juce_wchar)> onRejected;

    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void focusGained (FocusChangeType) override;
    void focusLost (FocusChangeType) override;

    /** 키 → 받을 글자 (대문자). 받지 않으면 0. 테스트용으로 공개 */
    static juce::juce_wchar mapKey (juce::juce_wchar typed, Charset);

private:
    Charset charset;
    int cellW, cellH, gap = 8;
    juce::juce_wchar cells[6] {};
    int active = 0;
    bool error = false, caretOn = true;
    juce::VBlankAttachment blink;
    double lastBlink = 0.0;

    void put (int index, juce::juce_wchar c);
    void changed();
    juce::Rectangle<float> cellBounds (int i) const;
};

/** 선택 상자 (디자인 select: 높이 32, 글자 400 13px). 항목 ID 는 1부터 */
class Select : public juce::ComboBox
{
public:
    Select();
    /** 값(문자열) 목록으로 채운다. labels 가 비면 values 를 그대로 보여 준다 */
    void setItems (const juce::StringArray& values, const juce::StringArray& labels = {});
    void setValue (const juce::String&, juce::NotificationType = juce::dontSendNotification);
    juce::String getValue() const;
    void setItemDisabled (const juce::String& value, bool disabled);
    std::function<void (const juce::String&)> onValueChange;

private:
    juce::StringArray values;
};

/** 체크 상자 16×16 (약관 동의). 켬: 포인트 면 + 체크 */
class Checkbox : public juce::Button
{
public:
    Checkbox();
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

} // namespace meon::ui
