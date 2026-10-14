// MEON ui - 모달 본문의 글자 (작은 제목 500 12px, 안내 400 12px/1.5, 글자 수). 모달들이 같이 쓴다.
#pragma once

#include "../theme/Tokens.h"

namespace meon::ui::form
{
    // 디자인의 작은 제목 (font: 500 12px, #767676)
    inline void drawLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r, const juce::String& faint = {})
    {
        const auto f = font (500, 12.0f);
        g.setFont (f);
        g.setColour (col::inkSub);
        g.drawText (text, r, juce::Justification::centredLeft, false);
        if (faint.isNotEmpty())
        {
            const int w = (int) std::ceil (textWidth (f, text + " "));
            g.setColour (col::inkFaint);
            g.drawText (faint, r.withTrimmedLeft (w), juce::Justification::centredLeft, false);
        }
    }

    // 안내 (font: 400 12px/1.5)
    inline void drawHint (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r, juce::Colour ink = col::inkFaint)
    {
        g.setFont (font (400, 12.0f));
        g.setColour (ink);
        g.drawFittedText (text, r, juce::Justification::topLeft, 3, 1.0f);
    }

    inline int hintHeight (const juce::String& text, int width)
    {
        juce::AttributedString a;
        a.append (text, font (400, 12.0f), col::inkFaint);
        a.setLineSpacing (6.0f);
        juce::TextLayout t;
        t.createLayout (a, (float) width);
        return juce::jmax (18, (int) std::ceil (t.getHeight()));
    }

    inline void drawCounter (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r)
    {
        g.setFont (font (400, 12.0f));
        g.setColour (col::inkFaint);
        g.drawText (text, r, juce::Justification::centredRight, false);
    }

    inline int countChars (const juce::String& s) { return s.length(); }

} // namespace meon::ui::form
