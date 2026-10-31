// MEON ui - 프로필 사진(아바타). 설명은 Avatars.h
#include "Avatars.h"
#include "Tokens.h"
#include <BinaryData.h>
#include <map>

namespace meon::ui::avatars
{

namespace
{
    struct Store
    {
        juce::String mine;
        juce::Image minePhoto;
        std::map<juce::String, juce::String> remote;   // 사용자 id → 사진 주소 (나중에)
        // 크기별로 줄인 그림: (그림 id, 물리 px) → 그림
        std::map<std::pair<juce::String, int>, juce::Image> scaled;
        juce::ChangeBroadcaster changes;
        int photoGeneration = 0;
    };
    Store& store()
    {
        static Store s;
        return s;
    }

    /** 반씩 줄여 가며 (한 번에 크게 줄이면 거칠다) 정사각형 px 크기로 */
    juce::Image scaleTo (const juce::Image& src, int px)
    {
        juce::Image cur = src;
        while (cur.getWidth() >= px * 2)
            cur = cur.rescaled (cur.getWidth() / 2, cur.getHeight() / 2, juce::Graphics::highResamplingQuality);
        if (cur.getWidth() != px || cur.getHeight() != px)
            cur = cur.rescaled (px, px, juce::Graphics::highResamplingQuality);
        return cur;
    }
}

const juce::Image& defaultImage()
{
    static const juce::Image img = juce::ImageCache::getFromMemory (BinaryData::avatardefault_png, BinaryData::avatardefault_pngSize);
    return img;
}

juce::Image photoFor (const juce::String& userId)
{
    auto& s = store();
    if (userId.isNotEmpty() && userId == s.mine && s.minePhoto.isValid()) return s.minePhoto;
    return {};
}

void paint (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& userId, float opacity)
{
    if (r.isEmpty()) return;
    auto& s = store();
    const auto photo = photoFor (userId);
    const bool isPhoto = photo.isValid();
    // 물리 px 크기로 한 번 줄여 둔 그림을 붙인다
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const int px = juce::jmax (1, juce::roundToInt (r.getWidth() * scale));
    const juce::String key = isPhoto ? "mine#" + juce::String (s.photoGeneration) : juce::String ("default");
    auto& cached = s.scaled[{ key, px }];
    if (! cached.isValid()) cached = scaleTo (isPhoto ? photo : defaultImage(), px);

    juce::Graphics::ScopedSaveState ss (g);
    juce::Path circle;
    circle.addEllipse (r);
    g.reduceClipRegion (circle);
    g.setOpacity (opacity);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (cached, r, juce::RectanglePlacement::fillDestination);
    if (! isPhoto)
    {
        g.setColour (col::borderSubtle.withMultipliedAlpha (opacity));   // 기본 이미지 테두리 #EBEBEB 1px (원 안쪽)
        g.drawEllipse (r.reduced (0.5f), 1.0f);
    }
}

void setMine (const juce::String& userId, const juce::Image& photo)
{
    auto& s = store();
    const bool same = s.mine == userId && s.minePhoto == photo;
    if (same) return;
    s.mine = userId;
    s.minePhoto = photo;
    ++s.photoGeneration;
    for (auto it = s.scaled.begin(); it != s.scaled.end();)
        it = it->first.first == "default" ? std::next (it) : s.scaled.erase (it);   // 예전 내 사진 그림은 버린다
    s.changes.sendChangeMessage();
}

juce::String myId() { return store().mine; }
bool hasMyPhoto() { return store().minePhoto.isValid(); }

void setRemoteUrl (const juce::String& userId, const juce::String& url)
{
    if (userId.isEmpty()) return;
    auto& r = store().remote;
    if (url.isEmpty()) r.erase (userId);
    else r[userId] = url;
}

juce::String remoteUrl (const juce::String& userId)
{
    const auto& r = store().remote;
    const auto it = r.find (userId);
    return it != r.end() ? it->second : juce::String();
}

juce::ChangeBroadcaster& changes() { return store().changes; }

} // namespace meon::ui::avatars
