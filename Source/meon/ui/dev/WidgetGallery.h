// MEON ui - 개발용 위젯 모음 화면 (--screen=widgets). 디자인과 나란히 놓고 비교한다. 배포 빌드에는 없다.
#pragma once

#include "../widgets/Button.h"
#include "../widgets/Inputs.h"
#include "../widgets/Controls.h"
#include "../widgets/Meter.h"
#include "../widgets/Display.h"
#include "../theme/LookAndFeel.h"

namespace meon::ui
{

class WidgetGallery : public juce::Component
{
public:
    WidgetGallery();
    ~WidgetGallery() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    LookAndFeel lnf;
    juce::OwnedArray<juce::Component> owned;
    struct Row { juce::String label; std::vector<juce::Component*> items; int height; };
    std::vector<Row> rows;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> labels;

    Modal modal { TXT ("방 설정"), 360 };
    Popover popover { TXT ("입장 요청") };
    Card card, chatCard;

    template <typename T, typename... Args>
    T* make (Args&&... args)
    {
        auto* c = new T (std::forward<Args> (args)...);
        owned.add (c);
        addAndMakeVisible (c);
        return c;
    }
    void addRow (const juce::String& label, std::vector<juce::Component*> items, int height = 32);
};

} // namespace meon::ui
