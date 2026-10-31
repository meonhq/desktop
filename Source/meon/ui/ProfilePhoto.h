// MEON ui - 내 프로필 사진 (합주 최종 v2: 설정 › 프로필 "프로필 사진").
//   - 이 컴퓨터에만 둔다. 서버에 올리지 않는다 (2026-10-31 사용자 결정): 내 화면의 내 자리(내 프로필·멤버 목록·채팅의 나)에만 보인다
//   - 받는 형식: JPG·PNG·WEBP (파일 내용으로 판단), 5MB 이하. WEBP 는 운영체제 해독기 (MeonImageDecode)
//   - 처리 (디자인 photoChange 와 같다): 가운데 정사각형으로 자르고 → 256×256 → 투명한 곳은 흰색 → JPEG 품질 0.88.
//     다시 저장하므로 위치(GPS) 같은 메타데이터는 남지 않는다. JPEG 의 회전 정보(EXIF Orientation)는 먼저 적용한다 (브라우저와 같게)
//   - 계정마다 따로: 설정 폴더/profile-photo-<사용자 id>.jpg
#pragma once

#include <JuceHeader.h>

namespace meon::ui::profilephoto
{

enum class Error { none, format, tooLarge, unreadable };

struct Result
{
    Error error = Error::none;
    juce::Image image;          // 256×256 (RGB)
    juce::MemoryBlock jpeg;     // 저장할 JPEG
};

constexpr juce::int64 maxBytes = 5 * 1024 * 1024;
constexpr int size = 256;
constexpr float jpegQuality = 0.88f;

/** 파일 → 처리한 사진. 실패하면 error */
Result process (const juce::File&);
/** 내용 (파일 크기 확인 없이) */
Result processBytes (const void* data, size_t size);

/** 오류 문구 (빨강 #D92D20) */
juce::String message (Error);

/** 파일 내용의 형식: "jpeg"·"png"·"webp", 아니면 빈 값 */
juce::String sniff (const void* data, size_t size);

/** 이 사용자의 사진 파일 (설정 폴더) */
juce::File fileFor (const juce::String& userId);
/** 로그인한 사람이 바뀔 때: 그 사람의 사진을 읽어 아바타에 (없거나 로그아웃이면 기본 이미지) */
void loadFor (const juce::String& userId);
/** 저장하고 아바타에 바로 보인다. 실패하면 false */
bool save (const juce::String& userId, const Result&);
/** 기본 이미지로 (파일을 지운다) */
void remove (const juce::String& userId);

} // namespace meon::ui::profilephoto
