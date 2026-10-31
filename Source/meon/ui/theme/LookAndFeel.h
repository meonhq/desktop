// MEON ui - JUCE 기본 컴포넌트(선택 상자, 펼친 목록, 텍스트 입력, 스크롤바, 툴팁)를 디자인대로 그린다.
// 선택 상자는 디자인의 select (높이 32, 여백 12, 글자 400 13px, 테두리 #EBEBEB · 열림·포커스 #AAAAAA, 모서리 6).
#pragma once

#include "Tokens.h"
#include "Icons.h"

namespace meon::ui
{

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

    /** 커서 규칙: 누를 수 있는 것만 손가락. 비활성(자기나 부모가 setEnabled(false))이면 일반 화살표 */
    juce::MouseCursor getMouseCursorFor (juce::Component&) override;

    // 선택 상자
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&, juce::Drawable*) override;
    void drawComboBoxTextWhenNothingSelected (juce::Graphics&, juce::ComboBox&, juce::Label&) override;
    juce::PopupMenu::Options getOptionsForComboBoxPopupMenu (juce::ComboBox&, juce::Label&) override;

    // 펼친 목록 (디자인에 열린 모양이 없어 지금 앱의 둥근 흰 목록을 쓴다)
    int getMenuWindowFlags() override { return 0; }
    int getPopupMenuBorderSize() override { return 4; }
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuItemWithOptions (juce::Graphics&, const juce::Rectangle<int>& area, bool isHighlighted,
                                       const juce::PopupMenu::Item&, const juce::PopupMenu::Options&) override;
    void getIdealPopupMenuItemSizeWithOptions (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                               int& idealWidth, int& idealHeight, const juce::PopupMenu::Options&) override;
    void drawPopupMenuUpDownArrow (juce::Graphics&, int width, int height, bool isScrollUpArrow) override;
    juce::Font getPopupMenuFont() override;

    // 텍스트 입력: 테두리·배경은 TextInput 이 그린다
    void fillTextEditorBackground (juce::Graphics&, int, int, juce::TextEditor&) override {}
    void drawTextEditorOutline (juce::Graphics&, int, int, juce::TextEditor&) override {}
    juce::CaretComponent* createCaretComponent (juce::Component* keyFocusOwner) override;

    // 스크롤바
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height,
                        bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                        bool isMouseOver, bool isMouseDown) override;
    int getDefaultScrollbarWidth() override { return 8; }
    bool areScrollbarButtonsVisible() override { return false; }

    // 툴팁: 슬라이더 말풍선과 같은 모양 (검정 면, 흰 글자 500 12px, 여백 4·7, 모서리 4)
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
};

} // namespace meon::ui
