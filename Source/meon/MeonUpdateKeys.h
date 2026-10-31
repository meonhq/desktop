// MEON - 업데이트 매니페스트를 믿는 공개 키 (Ed25519, raw 32바이트 hex). 배포 설계 DISTRIBUTION_PLAN 3.5 (결정 D5).
//
//   gpl-1  desktop(이 저장소) CI 가 서명한다. 개인 키는 GitHub Environment "dl-release" 의 DL_SIGNING_KEY_GPL1
//   app-1  app CI 가 서명한다
//   app-2  예비 (오프라인 보관). 키를 바꿀 때 쓴다
//
// 앱(MeonUpdater)과 CI 의 검증 도구(MEON_ManifestTool)가 이 파일 하나를 같이 쓴다. CI 는 서명한 매니페스트를 올리기 전에
// 이 키로 다시 검증하므로, 개인 키와 여기 값이 어긋나면 올리지 않고 실패한다.
// 키를 빼거나 바꾸면 이미 설치된 앱은 그 키로 서명한 매니페스트를 믿지 않는다. 바꾸기 전에 사용자에게 묻는다 (CLAUDE.md).
#pragma once

namespace meon::update
{

struct KeyHex
{
    const char* id;
    const char* hex;
};

inline constexpr KeyHex builtInKeyHex[] = {
    { "gpl-1", "58945d58ea1f8b5fcf580b0d467bef3a954b256fa2e95a1651f88460227bacbe" },
    { "app-1", "df75108cbe22347cd09ead44475c937ac95ea0a52367d90fcfcc6ad3e96222b0" },
    { "app-2", "18bc470dc65088a987abade10c60115b2cd7e54dcd7696e19e40dc7f6ebd2228" },
};

} // namespace meon::update
