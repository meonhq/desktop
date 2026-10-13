# Meon

**Meon**은 최대 5명이 6자리 초대 코드로 모여 실시간으로 합주하는 데스크톱 앱입니다 (macOS / Windows).
독립 앱과 DAW 플러그인(AU / VST3) 두 가지 형태로 쓸 수 있습니다.

> **이 소프트웨어는 [SonoBus](https://github.com/sonosaurus/sonobus)(Jesse Chappell, Sonosaurus LLC)의 수정판입니다.**
> 오디오 엔진, 네트워크(AOO), 코덱은 SonoBus 1.7.2 를 그대로 사용하고, 화면과 기본값만 Meon 에 맞게 새로 만들었습니다.
> SonoBus 와 마찬가지로 **GPLv3** (+ `LICENSE_EXCEPTION`)로 공개합니다. 원본 저작권 표기는 [LICENSE](LICENSE)와 앱의 설정 > 앱 정보에 있습니다.

## 무엇이 다른가

| | SonoBus | Meon |
|---|---|---|
| 방 | 그룹 이름 + 비밀번호, 공개 그룹 목록 | 6자리 초대 코드 (항상 비공개), 최대 5명 |
| 코덱 | Opus / PCM 선택 | 무압축 PCM 16 bit 고정 |
| 지터버퍼 | 수동/자동 선택 | 자동 |
| 이펙트·녹음·재생 | 있음 | 없음 (숨김) |
| UI | 영어 등 다국어 | 한국어 전용, 새 디자인 |
| 서버 | aoo.sonobus.net (기본) | 동일 (자체 서버는 다음 단계) |

식별자도 SonoBus 와 겹치지 않게 새로 정했습니다 (번들 ID `com.meon.MEON`, 제조사 코드 `Meon`, 플러그인 코드 `Mjam`).
같은 컴퓨터에 SonoBus 가 설치되어 있어도 충돌하지 않습니다.

## 빌드

CMake 3.15 이상. 의존성(JUCE 7 포크, AOO, Opus 정적 라이브러리)은 저장소에 포함되어 있습니다.

### macOS

```bash
cmake -DCMAKE_BUILD_TYPE=Release -B build
cmake --build build --config Release -j 8
```

결과물: `build/MEON_artefacts/Release/{Standalone/Meon.app, AU/Meon.component, VST3/Meon.vst3}`

- 기본은 유니버설(x86_64 + arm64) 빌드입니다. 개발 중에는 `-DUniversalBinary=OFF` 로 현재 아키텍처만 빌드하면 빠릅니다.
- Xcode 라이선스에 동의하지 않은 상태라면 `export DEVELOPER_DIR=/Library/Developer/CommandLineTools` 로 Command Line Tools 만으로 빌드할 수 있습니다.
- Xcode 프로젝트가 필요하면 `./setupcmakexcode.sh` (Xcode 라이선스 동의 필요).

### Windows

Visual Studio 2019 이상 + CMake. **ASIO SDK 2.3.4 이상**이 필요합니다 (2025년 10월부터 GPLv3 이중 라이선스).

1. https://www.steinberg.net/asiosdk 에서 SDK 를 받아 저장소 폴더 **옆**에 `asiosdk` 라는 이름으로 풉니다 (`../asiosdk/common/iasiodrv.h` 가 있어야 함). 다른 위치라면 `-DASIO_SDK_PATH=경로`.
2. ```
   cmake -G "Visual Studio 17 2022" -A x64 -B build
   cmake --build build --config Release
   ```

결과물: `build/MEON_artefacts/Release/{Standalone/Meon.exe, VST3/Meon.vst3}`

## 설치 (테스터용)

main 에 push 할 때마다 GitHub Actions 가 설치 파일을 만들어 프리릴리스에 올립니다.

- macOS (유니버설): https://github.com/meonhq/desktop/releases/download/mac-latest/meon.dmg
- Windows: https://github.com/meonhq/desktop/releases/download/windows-latest/meon-setup.exe

서명·공증을 하지 않은 빌드입니다. 경고를 넘기는 방법과 플러그인 설치 위치는 [doc/INSTALL.md](doc/INSTALL.md)를 보세요.

## 로그

방에 있는 동안 멤버별 핑·지연·지터버퍼·패킷 손실·끊김 횟수를 5초마다 JSON 으로 기록합니다.
설정 > 문제 해결 > "로그 폴더 열기" (macOS `~/Library/Logs/MEON`, Windows `%APPDATA%\MEON\logs`).

## 진행 기록

단계별 작업 내용과 판단이 필요한 항목은 [doc/MEON_PROGRESS.md](doc/MEON_PROGRESS.md)에 있습니다.
원본 SonoBus 코드 구조 참고 자료: [doc/dev/](doc/dev/).

## 라이선스와 3rd party

- Meon / SonoBus: GPLv3 — [LICENSE](LICENSE), [LICENSE_EXCEPTION](LICENSE_EXCEPTION)
- SonoBus 원저작자: Jesse Chappell (Sonosaurus LLC). 원본 저장소 https://github.com/sonosaurus/sonobus
- JUCE 7 (essej 포크, GPLv3), AOO (Christof Ressi, essej 포크), Opus (BSD), ff_meters (BSD)
- Pretendard 글꼴 (Kil Hyung-jin, SIL OFL 1.1) — `Source/fonts/LICENSE-Pretendard.txt`
- ASIO SDK (Steinberg, GPLv3 옵션) — Windows 빌드 시에만 사용, 저장소에는 포함하지 않음
