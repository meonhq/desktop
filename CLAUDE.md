# MEON — 에이전트 작업 규칙

SonoBus 1.7.2 기반 합주 앱 (JUCE 7, C++). 우리 코드는 `Source/meon/`, 원본 SonoBus 코드 구조는 `doc/dev/`, 진행 기록은 `doc/MEON_PROGRESS.md`.
macOS / Windows 독립 앱 + 플러그인(AU / VST3). UI 문구는 한국어 존댓말 (`TXT ("…")`).

## 배포 구조: 릴리스는 태그 또는 main 수동 실행 때만

배포 설계는 ensemble-rust `docs/DISTRIBUTION_PLAN.md` (결정 D1–D9 확정, 2026-10-23). 지금은 브리지 단계: GitHub 릴리스와 dl.meon.co.kr 에 같이 올린다.

- `.github/workflows/mac.yml`, `windows.yml` 는 main push 와 PR 에서 **빌드만** 한다. 릴리스는 건드리지 않고, 앱 빌드 번호는 `0`(로컬 빌드와 같음, 자동 업데이트 꺼짐).
- **릴리스**는 `v*` 태그를 push 하거나 main 에서 `workflow_dispatch` 로 수동 실행할 때만 한다. main 이 아닌 브랜치에서 수동 실행하면 빌드만 한다.
  - 빌드 번호 `MEON_BUILD_ID` = 그 커밋까지의 **커밋 수** (`git rev-list --count HEAD`, D3). 같은 커밋이면 mac·windows 가 같은 번호다.
    이미 낸 번호보다 커야 하고(같은 커밋을 두 번 릴리스할 수 없다), 10000 미만이다 (10000 이상은 app). '빌드 번호' 단계가 막는다.
    브리지 전에는 워크플로마다 `run_number` 였다 (마지막 mac 200, windows 208).
  - build 작업이 GitHub 프리릴리스를 갱신한다 (`build.txt` 마지막):
    - macOS: `mac-latest` 에 `meon.dmg`(사용자 다운로드), `MEON-mac.zip`, `build.txt`
    - Windows: `windows-latest` 에 `meon-setup.exe`(사용자 다운로드), `MEON-Installer.exe`(같은 파일, 자동 업데이트용), `build.txt`
  - 이어서 **publish-dl** 작업이 dl.meon.co.kr (Cloudflare R2 버킷 `meon-dl`) 에 올린다 (`scripts/dl_publish.sh`):
    빌드 파일 `meon/{mac,windows}/<빌드>/…` (덮어쓰지 않음) → CDN 으로 다시 받아 sha256 확인 → `release.json` →
    **채널 매니페스트 `meon/v1/{beta,stable}/{mac,windows}.json` 마지막**. 출시 전이라 beta·stable 둘 다 올린다 (출시 뒤 stable 은 승격으로만).
  - 매니페스트는 Ed25519 로 서명한다 (`gpl-1`). 올리기 전에 앱과 같은 코드(`MEON_ManifestTool`)·같은 키 헤더로 검증한다.
  - R2 토큰·서명 키는 Environment `dl-release` (main·`v*` 태그만) 의 비밀값이다. 빌드 작업과 PR 은 읽지 못한다. 출력하거나 로그에 남기지 않는다.
  - `dl-test.yml` (main 수동 실행): 시험 경로 `meon/v1/test/`·`meon/test/` 에만 쓰고 R2·CDN·키를 확인한다 (`scripts/dl_test.sh`).
- 독립 앱 자동 업데이트 (`Source/meon/MeonUpdater.*`, `MeonUpdateCheck.*`, `MeonUpdateManifest.*`): 켤 때·6시간마다·서버 `latest_build` 가 오를 때(0–60초 무작위 지연) 확인한다.
  - dl 매니페스트(설정 파일의 `updateChannel`. beta 는 stable 도 읽어 큰 번호)를 앱에 넣은 공개 키로 검증하고, 받은 파일은 크기·sha256 이 맞을 때만 설치한다.
  - 검증된 매니페스트가 하나도 없으면(없음·막힘·서명 틀림) 지금까지처럼 GitHub `build.txt` 로 대체한다.
  - 기록: 로그 폴더의 `update.log` 와 `app.log` ("확인: 출처 dl …" 또는 "… → GitHub").
  - 채널: 브리지 전부터 쓰던 앱(테스터)은 beta, 새로 설치한 앱은 stable. 바꾸는 화면은 없다.
- 서버 `min_build`·`latest_build` 는 한 숫자다. 두 플랫폼 stable 이 모두 그 번호 이상일 때만 올린다 (server `docs/DEPLOY.md` 5.1).
- `docs/**` 만 바뀐 push·PR 은 빌드하지 않는다.
- 리메이크(R0–R10)는 `main` 에 합쳤다 (R10, 태그 없음). 이후 PR 은 모두 `main` 으로 올린다. 테스터 빌드는 `main` 에서 `workflow_dispatch` 로 만든다 (이미 설치된 앱도 그 빌드로 자동 업데이트된다).
- 운영 서버 가동 중(2026-10-17~). main 수동 실행이 테스터 빌드다.

## 작업 흐름

1. 작업 브랜치에서 수정, 커밋, push.
2. **빌드 확인은 Actions 로 한다.** 이 컨테이너는 Linux 라 macOS/Windows 코드를 컴파일할 수 없다.
   PR 을 열면 `mac.yml` 과 `windows.yml` 이 자동으로 돈다. `linux.yml` 은 Linux 빌드·단위 테스트(`MEON_Tests`)·화면 스냅숏을 확인한다 (배포 없음). PR 전에 확인하려면 작업 브랜치에서 `workflow_dispatch` 로 둘 다 돌린다 (빌드만 한다).
3. 둘 다 성공해야 머지한다. 실패하면 로그를 보고 고쳐서 다시 돌린다. 실패한 채로 머지하지 않는다.
4. 사용자가 "머지까지" 하라고 했을 때만 머지한다.
5. 빌드 성공은 컴파일이 된다는 뜻일 뿐이다. 실제 동작은 사용자가 앱에서 확인하므로, 보고할 때 **무엇을 어떻게 확인하면 되는지** 함께 적는다.

## 플랫폼

- `JUCE_MAC` / `JUCE_WINDOWS` 로 갈리는 코드가 있다 (오디오 장치, 업데이트, 글꼴 등). 한쪽을 고치면 다른 쪽도 확인한다.
- 화면(UI)은 두 플랫폼이 같은 코드를 쓴다. 플랫폼 차이는 화면 코드가 아니라 아래 계층에서 처리하는 것이 기본.

## 엔진 코드 수정은 먼저 묻는다

- SonoBus 엔진 코드(`Source/meon/` 밖: `SonobusPluginProcessor.*`, `ChannelGroup.*`, AOO·코덱·네트워크 등)는 **고치기 전에 사용자에게 먼저 묻는다.** 버그를 고치는 데 필요해 보여도 마찬가지다.
- 승인받은 수정은 `docs/REMAKE_PLAN.md` 8.2 에 E 번호로 적는다: 바꾼 파일·함수, 이유, 보내는 소리 등 기존 동작에 영향이 없는 근거.
- E 번호는 다시 쓰지 않는다. 하지 않기로 한 수정도 번호를 그대로 두고 "안 함" 으로 표시한다. 지금까지 E1–E12 (E3 은 안 함, E5·E6·E7 은 승인 후 R11 에서 구현, E8 은 SIGPIPE 소켓 단위 차단, E9 는 AOO 붙기 실패 판정, E10 은 오디오 스레드 관문, E11 은 화면 미터를 잠금 안에서 값으로 읽기, E12 는 지연 우선 자동 버퍼가 실제로 줄어들게).

## 건드리면 자동 업데이트가 깨지는 것 (바꿔야 하면 먼저 사용자에게 묻는다)

- **워크플로 파일 이름 변경·삭제 후 재생성 금지.** 빌드 번호는 이제 커밋 수라 이름과 무관하지만, 브리지 전 앱이 기대는 GitHub 릴리스를 만드는 곳이라 그대로 둔다.
- **main 강제 push·이력 재작성 금지.** 커밋 수가 줄면 빌드 번호가 거꾸로 가서 새 빌드가 옛 빌드로 보인다. 릴리스한 커밋은 공개돼 있어야 한다 (GPL 소스 위치, 매니페스트 `source_url`).
- **dl 의 약속** (브리지 이후 모든 앱이 기대): 주소 `https://dl.meon.co.kr/meon/v1/{stable,beta}/{mac,windows}.json`, 매니페스트 v1 형식 (`Source/meon/MeonUpdateManifest.h`),
  믿는 공개 키 (`Source/meon/MeonUpdateKeys.h`: 빼거나 바꾸면 이미 설치된 앱이 그 키로 서명한 매니페스트를 믿지 않는다), 채널 매니페스트를 마지막에 올리는 순서, 올린 빌드 파일은 덮어쓰지 않기.
- 릴리스 태그(`mac-latest`, `windows-latest`)와 자동 업데이트용 파일 이름(`MEON-mac.zip`, `MEON-Installer.exe`, `build.txt`) — 이미 설치된 앱 코드에 하드코딩돼 있다. (`meon.dmg`, `meon-setup.exe` 는 사람이 받는 파일이라 앱과 무관)
- 릴리스에 `build.txt` 를 **마지막에** 올리는 순서. 먼저 올리면 앱이 반쯤 올라간 파일을 받는다.
- 앱 이름 `Meon.app` / `Meon.exe` (CMake 타깃 이름은 `MEON`), 번들 ID `com.meon.MEON`, 제조사·플러그인 코드 (`Meon`, `Mjam`), Inno Setup `AppId`.
  - 예전 이름은 `MEON.app` / `MEON.exe`. 옛 앱은 대소문자를 가리지 않는 파일 시스템 덕분에 새 파일을 찾는다. 새 업데이터는 `MEON.app` 을 `Meon.app` 으로 바꿔 넣는다.
- macOS 업데이트는 `MEON-mac.zip` 안에 `Meon.app` 이 최상위로 들어 있다고 가정한다 (`ditto -c -k --keepParent`).
- Windows 업데이트는 `release/wininstaller.iss` 의 `/UPDATE=1` 처리에 의존한다.

## 서명

- macOS 는 Apple 개발자 ID 서명·공증 없이 ad-hoc 서명만 한다. 처음 설치할 때 경고가 뜨는 것은 정상 (`doc/INSTALL.md`).
- ad-hoc 서명이라 업데이트할 때마다 마이크 권한을 다시 물을 수 있다.
