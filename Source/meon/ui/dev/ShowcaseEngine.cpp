#include "ShowcaseEngine.h"

namespace meon::ui::dev
{

namespace
{
    constexpr double beat = 500.0, bar = 2000.0;   // 120 BPM, 4/4

    /** 가장 최근 타격의 레벨 (주기 period, 첫 타격 offset). 타격마다 peak 에서 decay dB/s 로 내려간다 */
    float hit (double t, double period, double offset, float peak, float decay)
    {
        double ph = std::fmod (t - offset, period);
        if (ph < 0.0) ph += period;
        return peak - decay * (float) (ph / 1000.0);
    }

    /** 정해 둔 박자 위치들 (한 마디 안 ms) 중 가장 최근 타격. peaks 는 위치마다. 마디마다 조금씩 다르게 (humanize) */
    float pattern (double t, const std::vector<std::pair<double, float>>& notes, float decay, int seed)
    {
        float best = -100.0f;
        for (int back = 0; back < 2; ++back)   // 이번 마디와 지난 마디
        {
            const double barStart = (std::floor (t / bar) - back) * bar;
            const int barIndex = ((int) std::floor (t / bar) - back + 600) % 6;   // 12초 = 6마디마다 같은 값
            for (size_t i = 0; i < notes.size(); ++i)
            {
                const double at = barStart + notes[i].first;
                if (at > t) continue;
                const auto h = (uint32_t) ((barIndex * 31 + (int) i * 17 + seed * 7) * 2654435761u);
                const float jitter = ((float) ((h >> 8) % 1000) / 1000.0f - 0.5f) * 2.4f;   // ±1.2 dB
                best = juce::jmax (best, notes[i].second + jitter - decay * (float) ((t - at) / 1000.0));
            }
        }
        return best;
    }

    /** 구간 [from, to) 안에서만 소리 (노래 구절). 밖에서는 빨리 줄어든다 */
    bool inside (double t, std::initializer_list<std::pair<double, double>> spans)
    {
        for (auto& s : spans) if (t >= s.first && t < s.second) return true;
        return false;
    }
}

double ShowcaseEngine::now()
{
    const double t = clockMs() >= 0.0 ? clockMs() : juce::Time::getMillisecondCounterHiRes();
    double r = std::fmod (t, loopMs);
    return r < 0.0 ? r + loopMs : r;
}

float ShowcaseEngine::levelDb (const juce::String& part, int variant, int channel, double t)
{
    // 영상의 첫 프레임이 마디 첫 박(모두 같이 치는 순간)이 아니도록 음악 시간을 조금 민다 (주기는 그대로)
    t = std::fmod (t + 1370.0, loopMs);
    if (t < 0.0) t += loopMs;
    float l = -100.0f;
    if (part == "drums")
    {
        l = juce::jmax (hit (t, 1000.0, 0.0, -9.5f, 45.0f),                    // 킥 1·3박
                        hit (t, 1000.0, beat, -8.2f, 40.0f),                   // 스네어 2·4박
                        hit (t, 250.0, 0.0, channel == 0 ? -19.0f : -17.5f, 60.0f));   // 하이햇 8분
        l = juce::jmax (l, hit (t, 6000.0, 0.0, -6.8f, 9.0f));               // 크래시 1·4마디 첫 박
        if (t >= 11000.0)                                                       // 6마디 끝 필인 (16분)
            l = juce::jmax (l, pattern (t, { { 1000, -10.0f }, { 1125, -11.5f }, { 1250, -10.5f }, { 1375, -11.0f },
                                             { 1500, -9.5f }, { 1625, -11.0f }, { 1750, -9.8f }, { 1875, -10.5f } }, 50.0f, 3));
        if (channel == 1) l -= 0.8f;
    }
    else if (part == "bass")
    {
        l = pattern (t, { { 0, -11.5f }, { 750, -14.0f }, { 1000, -12.0f }, { 1500, -13.5f } }, 7.0f, 5);
        l += 0.6f * (float) std::sin (juce::MathConstants<double>::twoPi * t / 1500.0);
    }
    else if (part == "keys")
    {
        l = pattern (t, { { 0, -11.0f }, { 750, -14.0f }, { 1000, -12.5f }, { 1750, -15.0f } }, 5.0f, 11);
        if (channel == 1) l -= 1.0f;
    }
    else if (part == "vocal")
    {
        const bool lead = variant == 0;
        const bool singing = lead ? inside (t, { { 150, 3700 }, { 4150, 7600 }, { 8150, 11500 } })
                                  : inside (t, { { 1900, 3700 }, { 5900, 7600 }, { 9900, 11500 } });
        if (singing)
        {
            // 음절마다 올라가고 사이에 조금 내려간다
            l = pattern (t, { { 0, -10.0f }, { 375, -12.5f }, { 750, -11.0f }, { 1000, -9.5f }, { 1375, -12.0f }, { 1625, -11.5f } },
                         6.0f, lead ? 13 : 19);
            l += 0.9f * (float) std::sin (juce::MathConstants<double>::twoPi * t / 250.0);
            if (! lead) l -= 1.8f;
        }
        else
        {
            l = -60.0f;
        }
    }
    else   // guitar
    {
        if (variant == 0)   // 리듬: 8분 스트로크
        {
            l = pattern (t, { { 0, -10.5f }, { 250, -16.0f }, { 500, -13.0f }, { 750, -16.0f },
                              { 1000, -11.0f }, { 1250, -16.0f }, { 1500, -13.0f }, { 1750, -15.0f } }, 22.0f, 7);
        }
        else                // 리드: 1·2마디는 작게, 3~6마디는 멜로디
        {
            if (t < 4000.0)
                l = pattern (t, { { 0, -21.0f }, { 1000, -22.0f } }, 12.0f, 23);
            else
                l = pattern (t, { { 0, -10.0f }, { 375, -12.5f }, { 625, -11.5f }, { 1000, -9.8f }, { 1250, -13.0f }, { 1500, -10.5f } },
                             10.0f, 29);
        }
    }
    return juce::jlimit (-60.0f, -6.5f, l);   // 노랑을 넘지 않는다 (빨강 피크 없음)
}

float ShowcaseEngine::frameMax (const juce::String& part, int variant, int channel)
{
    // 미터는 지난 프레임(1/30초) 동안의 최대값을 본다 (실제 엔진의 피크 미터처럼)
    const double t = now();
    float m = -100.0f;
    for (int i = 0; i <= 16; ++i)
        m = juce::jmax (m, levelDb (part, variant, channel, t - (double) i * (1000.0 / 30.0) / 16.0));
    return m;
}

bool ShowcaseEngine::connect (const juce::String&, int, const juce::String& user, const juce::String&)
{
    me = user;
    later ([this] { connected = true; listeners.call ([] (Listener& l) { l.engineConnected (true, {}); }); });
    return true;
}

bool ShowcaseEngine::joinGroup (const juce::String& name, const juce::String&)
{
    later ([this, name] { group = name; listeners.call ([name] (Listener& l) { l.engineGroupJoined (true, name, {}); }); });
    return true;
}

void ShowcaseEngine::leaveGroup (const juce::String& name)
{
    later ([this, name] { group = {}; listeners.call ([name] (Listener& l) { l.engineGroupLeft (name); }); });
}

void ShowcaseEngine::setAllowedPeers (const juce::StringArray& users, bool)
{
    for (auto& u : users)
        if (! allowed.contains (u))
            later ([this, u] { listeners.call ([u] (Listener& l) { l.enginePeerJoined (u); }); });
    allowed = users;
}

std::vector<core::EnginePeer> ShowcaseEngine::peers() const
{
    std::vector<core::EnginePeer> out;
    const double t = now();
    for (auto& u : allowed)
    {
        core::EnginePeer p;
        p.user = u;
        p.connected = p.recvActive = p.sendActive = true;
        int ping = 8;
        if (auto it = pings().find (u); it != pings().end())
            ping = (t >= it->second.fromMs && t < it->second.toMs) ? it->second.bumpMs : it->second.baseMs;
        p.pingMs = (float) ping;
        p.roundtripMs = (float) (ping * 2 + 12);
        p.bufferMs = 10.0f;
        const auto part = parts().count (u) ? parts()[u] : juce::String ("guitar");
        p.recvChannels = part == "drums" || part == "keys" ? 2 : 1;
        // 받은 패킷 수는 실제 시간으로 늘린다 (그대로면 PeerState 가 "소리 끊김" 으로 본다)
        p.packetsReceived = p.packetsSent = (juce::int64) (juce::Time::getMillisecondCounterHiRes() / 2.6667);
        out.push_back (p);
    }
    return out;
}

float ShowcaseEngine::peerLevel (const juce::String& user, int channel) const
{
    if (! allowed.contains (user)) return 0.0f;
    const auto part = parts().count (user) ? parts()[user] : juce::String ("guitar");
    // 같은 파트가 둘이면 두 번째는 다른 모양 (기타 리듬·리드, 보컬 메인·화음)
    int variant = 0;
    for (auto& u : allowed)
    {
        if (u == user) break;
        if (parts().count (u) && parts()[u] == part) ++variant;
    }
    return juce::Decibels::decibelsToGain (frameMax (part, variant, juce::jmax (0, channel)));
}

float ShowcaseEngine::sendLevel (int channel) const
{
    return juce::Decibels::decibelsToGain (frameMax ("keys", 0, juce::jmax (0, channel)));
}

float ShowcaseEngine::monitorLevel (int channel) const
{
    // 내가 듣는 소리: 모든 멤버 + 나를 합친 크기 (조금 줄이고 노랑 안에 둔다)
    const double t = now();
    float best = -100.0f;
    for (int i = 0; i <= 16; ++i)
    {
        const double ti = t - (double) i * (1000.0 / 30.0) / 16.0;
        double power = std::pow (10.0, levelDb ("keys", 0, channel, ti) / 10.0);
        std::map<juce::String, int> seen;
        for (auto& u : allowed)
        {
            const auto part = parts().count (u) ? parts()[u] : juce::String ("guitar");
            power += std::pow (10.0, levelDb (part, seen[part]++, channel, ti) / 10.0);
        }
        float db = (float) (10.0 * std::log10 (power)) - 5.0f;
        if (db > -9.0f) db = -9.0f + (db + 9.0f) * 0.45f;
        best = juce::jmax (best, db);
    }
    return juce::Decibels::decibelsToGain (juce::jlimit (-60.0f, -6.5f, best));
}

bool ShowcaseEngine::sendChat (const core::EngineChat& c)
{
    if (! c.tags.startsWith ("id:")) return true;
    const auto id = c.tags.fromFirstOccurrenceOf ("id:", false, false);
    for (auto& u : allowed)
        later ([this, u, id, g = c.group] { listeners.call ([&] (Listener& l) { l.engineChat ({ g, u, "ack:" + id, {} }); }); });
    return true;
}

void ShowcaseEngine::later (std::function<void()> fn)
{
    std::weak_ptr<bool> weak = alive;
    juce::MessageManager::callAsync ([weak, fn] { if (! weak.expired()) fn(); });
}

} // namespace meon::ui::dev
