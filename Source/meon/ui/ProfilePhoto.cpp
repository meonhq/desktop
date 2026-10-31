// MEON ui - 내 프로필 사진. 설명은 ProfilePhoto.h
#include "ProfilePhoto.h"
#include "theme/Avatars.h"
#include "theme/Tokens.h"
#include "../MeonImageDecode.h"
#include "../MeonSettings.h"
#include "../core/log/AppLog.h"

namespace meon::ui::profilephoto
{

namespace
{
    /** JPEG 의 EXIF Orientation (1~8, 없으면 1) */
    int exifOrientation (const juce::uint8* d, size_t n)
    {
        if (n < 4 || d[0] != 0xFF || d[1] != 0xD8) return 1;
        size_t i = 2;
        while (i + 4 <= n && d[i] == 0xFF)
        {
            const int marker = d[i + 1];
            if (marker == 0xD9 || marker == 0xDA) break;   // 그림 끝·그림 자료 시작: 더 앞에서 찾지 못함
            const size_t len = ((size_t) d[i + 2] << 8) | d[i + 3];
            if (len < 2 || i + 2 + len > n) break;
            if (marker == 0xE1 && len >= 16 && std::memcmp (d + i + 4, "Exif\0\0", 6) == 0)
            {
                const juce::uint8* t = d + i + 10;              // TIFF 머리
                const size_t tn = len - 8;
                const bool le = t[0] == 'I' && t[1] == 'I';
                auto u16 = [&] (size_t o) -> unsigned { if (o + 2 > tn) return 0; return le ? (unsigned) (t[o] | (t[o + 1] << 8)) : (unsigned) ((t[o] << 8) | t[o + 1]); };
                auto u32 = [&] (size_t o) -> size_t
                {
                    if (o + 4 > tn) return 0;
                    return le ? ((size_t) t[o] | ((size_t) t[o + 1] << 8) | ((size_t) t[o + 2] << 16) | ((size_t) t[o + 3] << 24))
                              : (((size_t) t[o] << 24) | ((size_t) t[o + 1] << 16) | ((size_t) t[o + 2] << 8) | (size_t) t[o + 3]);
                };
                if (u16 (2) != 0x2A) return 1;
                const size_t ifd = u32 (4);
                const unsigned count = u16 (ifd);
                for (unsigned e = 0; e < count; ++e)
                {
                    const size_t at = ifd + 2 + (size_t) e * 12;
                    if (at + 12 > tn) break;
                    if (u16 (at) == 0x0112)
                    {
                        const unsigned v = u16 (at + 8);
                        return v >= 1 && v <= 8 ? (int) v : 1;
                    }
                }
                return 1;
            }
            i += 2 + len;
        }
        return 1;
    }

    /** 회전 정보대로 바로 세운 그림 */
    juce::Image applyOrientation (const juce::Image& src, int o)
    {
        if (o <= 1 || o > 8 || ! src.isValid()) return src;
        const float w = (float) src.getWidth(), h = (float) src.getHeight();
        const bool swap = o >= 5;
        juce::AffineTransform t;
        switch (o)
        {
            case 2: t = juce::AffineTransform::scale (-1.0f, 1.0f).translated (w, 0.0f); break;
            case 3: t = juce::AffineTransform::rotation (juce::MathConstants<float>::pi).translated (w, h); break;
            case 4: t = juce::AffineTransform::scale (1.0f, -1.0f).translated (0.0f, h); break;
            case 5: t = juce::AffineTransform (0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f); break;           // 대각선 뒤집기
            case 6: t = juce::AffineTransform::rotation (juce::MathConstants<float>::halfPi).translated (h, 0.0f); break;   // 시계 90°
            case 7: t = juce::AffineTransform (0.0f, -1.0f, h, -1.0f, 0.0f, w); break;
            case 8: t = juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi).translated (0.0f, w); break;  // 반시계 90°
            default: break;
        }
        juce::Image out (juce::Image::ARGB, swap ? src.getHeight() : src.getWidth(), swap ? src.getWidth() : src.getHeight(), true);
        juce::Graphics g (out);
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);   // 90° 단위라 픽셀 그대로
        g.drawImageTransformed (src, t);
        return out;
    }

    /** 가운데 정사각형 → size×size, 투명은 흰색 */
    juce::Image squareWhite (const juce::Image& src)
    {
        const int n = juce::jmin (src.getWidth(), src.getHeight());
        juce::Image sq = src.getClippedImage ({ (src.getWidth() - n) / 2, (src.getHeight() - n) / 2, n, n }).createCopy();
        while (sq.getWidth() >= size * 2)   // 한 번에 크게 줄이면 거칠다: 반씩
            sq = sq.rescaled (sq.getWidth() / 2, sq.getHeight() / 2, juce::Graphics::highResamplingQuality);
        juce::Image out (juce::Image::RGB, size, size, true);
        juce::Graphics g (out);
        g.fillAll (juce::Colours::white);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (sq, juce::Rectangle<float> (0.0f, 0.0f, (float) size, (float) size));
        return out;
    }
}

juce::String sniff (const void* data, size_t n)
{
    const auto* d = static_cast<const juce::uint8*> (data);
    if (d == nullptr) return {};
    if (n >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF) return "jpeg";
    if (n >= 8 && std::memcmp (d, "\x89PNG\r\n\x1a\n", 8) == 0) return "png";
    if (n >= 12 && std::memcmp (d, "RIFF", 4) == 0 && std::memcmp (d + 8, "WEBP", 4) == 0) return "webp";
    return {};
}

juce::String message (Error e)
{
    switch (e)
    {
        case Error::format:     return TXT ("JPG, PNG, WEBP 이미지만 올릴 수 있어요");
        case Error::tooLarge:   return TXT ("5MB 이하 이미지만 올릴 수 있어요");
        case Error::unreadable: return TXT ("이미지를 읽을 수 없어요");
        case Error::none:       break;
    }
    return {};
}

Result processBytes (const void* data, size_t n)
{
    Result r;
    const auto kind = sniff (data, n);
    if (kind.isEmpty()) { r.error = Error::format; return r; }
    juce::Image img;
    if (kind == "webp")
    {
        img = meon::decodeWithSystem (data, n);
    }
    else
    {
        juce::MemoryInputStream in (data, n, false);
        img = juce::ImageFileFormat::loadFrom (in);
        if (kind == "jpeg") img = applyOrientation (img, exifOrientation (static_cast<const juce::uint8*> (data), n));
    }
    if (! img.isValid() || img.getWidth() < 1 || img.getHeight() < 1) { r.error = Error::unreadable; return r; }
    r.image = squareWhite (img);
    juce::MemoryOutputStream mos (r.jpeg, false);
    juce::JPEGImageFormat jpg;
    jpg.setQuality (jpegQuality);
    if (! jpg.writeImageToStream (r.image, mos)) { r.error = Error::unreadable; r.image = {}; }
    return r;
}

Result process (const juce::File& f)
{
    Result r;
    // 디자인 순서: 형식 → 크기 → 읽기
    juce::MemoryBlock head;
    {
        juce::FileInputStream in (f);
        if (! in.openedOk()) { r.error = Error::unreadable; return r; }
        in.readIntoMemoryBlock (head, 16);
    }
    if (sniff (head.getData(), head.getSize()).isEmpty()) { r.error = Error::format; return r; }
    if (f.getSize() > maxBytes) { r.error = Error::tooLarge; return r; }
    juce::MemoryBlock all;
    if (! f.loadFileAsData (all)) { r.error = Error::unreadable; return r; }
    return processBytes (all.getData(), all.getSize());
}

juce::File fileFor (const juce::String& userId)
{
    const auto safeId = userId.retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-");
    return MeonSettings::getSettingsFolder().getChildFile ("profile-photo-" + safeId + ".jpg");
}

void loadFor (const juce::String& userId)
{
    if (userId.isEmpty()) { avatars::setMine ({}, {}); return; }
    juce::Image img;
    const auto f = fileFor (userId);
    if (f.existsAsFile()) img = juce::ImageFileFormat::loadFrom (f);
    avatars::setMine (userId, img);
}

bool save (const juce::String& userId, const Result& r)
{
    if (userId.isEmpty() || r.error != Error::none || r.jpeg.isEmpty()) return false;
    const auto f = fileFor (userId);
    f.getParentDirectory().createDirectory();
    const auto tmp = f.getSiblingFile (f.getFileName() + ".tmp");
    if (! tmp.replaceWithData (r.jpeg.getData(), r.jpeg.getSize()) || ! tmp.moveFileTo (f))
    {
        core::AppLog::write ("profile", "프로필 사진 저장 실패: " + f.getFullPathName());
        tmp.deleteFile();
        return false;
    }
    avatars::setMine (userId, r.image);
    return true;
}

void remove (const juce::String& userId)
{
    if (userId.isEmpty()) return;
    fileFor (userId).deleteFile();
    avatars::setMine (userId, {});
}

} // namespace meon::ui::profilephoto
