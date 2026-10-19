// MEON core - 합주 채팅 입력 칸 규칙 (합주 최종 디자인 7). 화면 없이 글자·숫자만 본다.
//   글 400 14 / 줄 높이 20. 칸은 1줄 44 ~ 3줄 84 (위아래 12), 그 뒤로는 칸 안에서 스크롤.
//   Enter 보냄, Shift+Enter 줄바꿈. 보낼 때 앞뒤 공백·빈 줄을 지우고 안쪽 줄바꿈은 둔다 (붙여넣은 글도 같다).
//   900 바이트부터 "962 / 1000" 표시, 1000 을 넘으면 빨간색이고 보내지 않는다.
#pragma once

#include <JuceHeader.h>

namespace meon::core::chatinput
{

constexpr int limit = 1000;          // ChatChannel::maxBytes 와 같다
constexpr int counterFrom = 900;
constexpr int lineH = 20, padY = 12, maxLines = 3;

/** 보낼 글: 줄바꿈을 \n 으로 맞추고 앞뒤 공백·빈 줄을 지운다. 안쪽 줄바꿈·빈 줄은 그대로 */
inline juce::String prepare (const juce::String& raw)
{
    return raw.replace ("\r\n", "\n").replaceCharacter ('\r', '\n').trim();
}

/** UTF-8 바이트 수 (입력 칸에 보이는 글 그대로, 디자인과 같다) */
inline int bytes (const juce::String& s) { return (int) s.getNumBytesAsUTF8(); }

inline bool showCounter (int b) { return b >= counterFrom; }
inline bool overLimit (int b)   { return b > limit; }
inline juce::String counterText (int b) { return juce::String (b) + " / " + juce::String (limit); }

/** 보낼 수 있는지 (비었거나 1000 바이트를 넘으면 아니다) */
inline bool canSend (const juce::String& raw)
{
    const auto t = prepare (raw);
    return t.isNotEmpty() && ! overLimit (bytes (t));
}

/** 글 높이(px)로 칸에 보일 줄 수 (1..3) */
inline int visibleLines (int textHeightPx)
{
    return juce::jlimit (1, maxLines, (textHeightPx + lineH - 1) / lineH);
}

/** 입력 줄 높이: 위아래 12 + 줄 20 × 1..3 → 44..84 */
inline int boxHeight (int lines) { return padY * 2 + lineH * juce::jlimit (1, maxLines, lines); }

/** Enter: Shift 와 함께면 줄바꿈, 아니면 보냄 */
enum class Enter { send, newline };
inline Enter onEnter (bool shiftDown) { return shiftDown ? Enter::newline : Enter::send; }

/** 한글 조합 중 Enter 를 누를 때 확정 글자가 보낸 뒤에야 들어오는 입력기가 있으면, 보낸 글의 마지막 글자가
    빈 칸에 다시 남는다 (다음 Enter 에 두 번 보내짐). 보낸 뒤 짧은 시간 안에 칸이 보낸 글의 끝 1~2 글자와 같으면 그것이다 */
inline bool isLateImeLeftover (const juce::String& sent, const juce::String& now, juce::int64 elapsedMs)
{
    return elapsedMs >= 0 && elapsedMs < 300 && now.isNotEmpty() && now.length() <= 2 && sent.endsWith (now);
}

} // namespace meon::core::chatinput
