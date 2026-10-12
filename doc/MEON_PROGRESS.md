# MEON 진행 보고

SonoBus 1.7.2(upstream `sonosaurus/sonobus`, 커밋 35f1062d)를 포크한 MEON 합주 MVP 작업 기록.
각 단계마다 **한 것 / 막힌 것 / 판단이 필요한 것**을 남긴다.

---

## 1단계: 원본 빌드 (2026-09-22)

### 환경
- macOS 26.6.2 (Apple Silicon), Xcode 27.0 설치됨(단, 라이선스 미동의 상태), Command Line Tools clang 21, CMake 4.4.0
- Logic Pro 설치됨, 이 Mac에는 SonoBus가 설치되어 있지 않음

### 한 것
- `upstream` 리모트(sonosaurus/sonobus) 확인. `origin/main == upstream/main` (1.7.2 그대로).
- 원본 그대로 빌드 성공: **독립 앱, AU, VST3** (LV2도 함께 생성됨). arm64 단독 빌드와 기본 설정인 유니버설(x86_64+arm64) 빌드 둘 다 확인.
  ```bash
  export DEVELOPER_DIR=/Library/Developer/CommandLineTools   # Xcode 라이선스 미동의 상태일 때만 필요
  cmake -DCMAKE_BUILD_TYPE=Release -B build && cmake --build build --config Release -j 8
  # 결과: build/SonoBus_artefacts/Release/{Standalone,AU,VST3}
  ```
- **빌드 오류 1건을 빌드 설정으로 해결** (JUCE 소스는 손대지 않음):
  macOS 26/27 SDK에서 `CGWindowListCreateImage`가 obsoleted(15.0) 처리되어 JUCE의 헬퍼 도구 `juceaide` 컴파일이 실패했다.
  원인은 JUCE가 juceaide를 별도 cmake 프로세스로 빌드하면서 deployment target을 넘기지 않아 호스트 OS 버전(26.x)이 기본값이 되는 것.
  `CMakeLists.txt`에서 deployment target을 10.13으로 올리고 `MACOSX_DEPLOYMENT_TARGET` 환경변수를 하위 cmake에 전달하도록 했다.
- 독립 앱 실행 확인(창이 정상적으로 뜸, 한국어 UI 포함).
- AU 검증 통과: `auval -v aumf NBus Sono` → `AU VALIDATION SUCCEEDED` (Logic이 쓰는 검증과 동일한 도구).
- VST3: 번들을 직접 로드해 `GetPluginFactory`로 클래스 3개(Audio Module / Controller / Compatibility) 열거 확인.
- JUCE 버전 정정: 실제 빌드에 쓰이는 `deps/juce`는 **JUCE 7.0.8**(essej/JUCE `sono7good` 브랜치)이다. 최상위 `JUCE/` 폴더(6.0.8)는 빌드에 쓰이지 않는 잔재. JUCE 7도 GPLv3/상용 이중 라이선스라 조건은 같다. 올리지 않는다.
- **Windows ASIO 라이선스 조사 결과**:
  - 2025년 10월 Steinberg가 ASIO SDK **2.3.4**(2025-10-15)를 **proprietary / GPLv3 이중 라이선스**로 변경했다. (VST3 SDK 3.8은 MIT.)
  - SDK 동봉 README FAQ: "Can I switch to GPLv3 License? — Yes, if your product is released under GPLv3 License terms (including source code disclosure)."
  - 따라서 GPLv3로 전체 소스를 공개하는 MEON은 **ASIO를 포함한 Windows 빌드를 배포할 수 있다.** (Audacity가 과거에 ASIO 없이 배포한 문제는 해소됨.)
  - 다운로드: https://www.steinberg.net/asiosdk (→ `ASIO-SDK_2.3.4_2025-10-15.zip`). 원본 CMake는 `../asiosdk/common`을 참조하므로 Windows PC에서는 저장소 폴더 옆에 `asiosdk/`로 풀어둔다.
  - 근거: KVR/heise/Libre Arts 보도(2025-10-29), SDK 동봉 LICENSE.txt·README.md(미러 github.com/audiosdk/asio).

### 막힌 것
- **Xcode 라이선스 미동의**: `/usr/bin/git`, `xcodebuild`, `lipo` 등 Xcode 셔임이 모두 "You have not agreed to the Xcode license" 로 실패한다.
  Command Line Tools(`DEVELOPER_DIR=/Library/Developer/CommandLineTools`)로 우회해 빌드·git을 진행했다.
  Xcode 프로젝트 생성(`setupcmakexcode.sh`)이나 Xcode 빌드가 필요하면 터미널에서 한 번 실행해야 한다:
  ```bash
  sudo xcodebuild -license accept
  ```
- **Logic에서 실제 로드**: auval은 통과했지만 Logic 앱을 직접 조작해 확인하지는 못했다(화면 제어 권한 필요). Logic > 설정 > 플러그인 관리자에서 "Sonosaurus: SonoBus"가 성공으로 표시되는지 한 번 확인 필요.
- **Windows 빌드: 미확인** (Windows PC에서 확인 예정).

### 판단이 필요한 것
- "VST"는 **VST3**로 진행한다. VST2 SDK는 Steinberg가 배포를 중단했고 원본도 `VST2_SDK_PATH`가 없으면 VST2를 만들지 않는다.
- ASIO SDK를 저장소에 넣을지: GPLv3 이중 라이선스라 재배포는 가능해 보이지만, 동봉 LICENSE.txt에 "prior written agreement" 문구가 남아 있어 확답이 어렵다. 우선은 저장소에 넣지 않고 Windows 빌드 시 다운로드해 쓰는 방식으로 간다.
- 최소 macOS를 10.10 → **10.13**으로 올렸다 (최신 SDK가 10.13 미만을 지원하지 않음).

---

## 2단계: 이름과 식별자 (2026-09-22)

### 한 것
- 앱·플러그인 이름 **MEON**, CMake 타깃 `MEON` (SonoBus 의 VSTi 변형 타깃은 제외).
- 식별자 새로 지정: 번들 ID `com.meon.MEON`, 제조사 코드 `Meon`, 플러그인 코드 `Mjam`, AU 타입은 SonoBus 와 같은 MusicEffect(`aumf`).
  → `auval -a` 에 `aumf Mjam Meon - MEON: MEON` 으로 등록되고 SonoBus(`aumf NBus Sono`)와 겹치지 않음.
- 설정 파일 위치도 분리: `~/Library/Application Support/MEON/` (SonoBus 는 `.../SonoBus/`). `sonobus://` URL 스킴은 제거.
- 앱 아이콘: 디자인에 없어서 로고 규칙(MEON 텍스트, O 만 포인트 색)으로 임시 제작 (`images/meon_icon_mac_*.png`).
- 한국어 UI: 새 화면은 모두 한국어 문구를 직접 사용 (SonoBus 의 다국어 번역 파일은 쓰지 않음). MSVC 는 `/utf-8` 옵션 추가.
- README 와 설정 > 앱 정보에 "SonoBus 기반 수정판 · GPLv3" 표기.

### 판단이 필요한 것
- AU 타입: SonoBus 처럼 MusicEffect 로 두었다. Logic 에서는 "AU MIDI 제어 이펙트" 메뉴에 나타난다. 오디오 트랙 인서트(Audio FX)로 쓰게 하려면 `kAudioUnitType_Effect` 로 바꾸면 되고 빌드 설정 한 줄이다.
- 번들 ID `com.meon.MEON` 은 임의로 정했다. 도메인이 정해지면 바꿀 것 (설정 파일 위치와는 무관).

---

## 3단계: 기본값과 숨김 (2026-09-22)

### 한 것 (`Source/meon/MeonSession.cpp` `applyProcessorDefaults`)
- 코덱: **PCM 16 bit** 고정 (새 피어에도 강제 적용). 엔진 코드는 그대로.
- 지터버퍼: 자동(Auto-Full, 양방향 자동 조절).
- 이펙트(컴프레서·게이트·EQ·리버브·리미터) 모두 끔. 메트로놈·사운드보드 송출 끔. UI 에서 접근 불가.
- 녹음: UI 없음.
- 파일 재생 (2026-10-09 숨김 해제): 합주 화면 내 영역 아래 파일 플레이어 (독립 앱·플러그인). SonoBus 엔진의 파일 재생을 그대로 쓰고
  `sendfileaudio` 를 켠다. MEON 은 송신 채널을 1(모노)/2(스테레오)로 고정하므로 엔진이 파일 소리를 **내 송신에 섞어** 보낸다
  (채널 수·대역폭은 그대로, 받는 쪽에 따로 표시 없음). 지원 형식은 엔진 그대로: macOS CoreAudio(WAV·AIFF·MP3·M4A/AAC 등) + FLAC·Ogg,
  Windows WAV·AIFF·FLAC·Ogg + Windows Media(MP3·WMA, OS 코덱이라 라이선스 문제 없음). 방을 나가면 파일을 닫는다.
  내 뮤트(`mainsendmute`)는 송신 전체를 끄므로 MR 도 함께 안 나간다.
- 서버: `aoo.sonobus.net:10998` (SonoBus 기본 서버). 서버 끊김 시 엔진의 자동 재접속 사용.
- 그룹: 항상 비공개(`joinServerGroup(code, "", false)`), 공개 그룹 감시 끔, 공개 목록 UI 없음.
- 독립 앱 입력: 기본 **입력 1 채널 모노 송출** (첫 실행 입력 레벨 화면에서 채널 변경 가능).

### 판단이 필요한 것
- PCM 비트 수: 16 bit 로 두었다 (48 kHz 모노 기준 상향 약 0.8 Mbps/피어). 24 bit 로 올리면 대역폭 1.5배.
- 지터버퍼 자동 모드: Auto-Full 은 네트워크가 좋아지면 버퍼를 줄여 지연을 낮추지만 순간 끊김에 조금 더 민감하다. SonoBus 기본은 Initial-Auto(처음만 자동).

---

## 4단계: 화면과 기능 (2026-09-22)

### 한 것
- SonoBus 에디터·뷰 코드(약 30개 파일)를 빌드에서 제외하고 삭제. 새 UI 는 `Source/meon/` 에 새로 작성.
  - `MeonTheme` 색·글꼴(Pretendard 4종 내장, OFL)·치수, `MeonLookAndFeel` 콤보박스·입력·슬라이더·메뉴·스크롤바
  - `MeonWidgets` 버튼(단축키 배지)·레벨 미터·볼륨 슬라이더·체크박스·경고 상자·코드 6칸 입력·진행 막대
  - `MeonSession` 엔진 연결(서버·방·멤버·채팅·뮤트·통계·로그), `MeonSettings` 닉네임/첫 실행/입력 채널
  - 화면: 첫 실행 4단계(플러그인 2단계) · 홈 · 방 만들기 · 코드 입장 · 합주 · 설정
- 독립 앱 1280×800(최소 1024×680), 플러그인 900×600(최소 760×520, 크기 조절 가능). 전환은 120 ms 페이드.
- 초대 코드 6자리(O,0,I,1 제외) = SonoBus 그룹 이름. 비밀번호 없음.
- 최대 5명: 6번째는 입장 직후 스스로 나가며 "이 방은 5명이 모두 찼어요".
- 잘못된 코드: 서버는 방의 존재 여부를 알려주지 않는다(없는 그룹에 들어가면 새로 만들어짐). 그래서 입장 후 4초 안에 멤버가 하나도 없으면 방을 나가고 "그런 방이 없어요"로 처리한다.
- 합주 화면: 내 영역(입력 레벨·내 마이크 뮤트 M), 멤버 카드 최대 4개(닉네임·레벨·볼륨 -60~+6 dB·뮤트 1–4·핑·연결 상태), 핑 18 ms 초과 시 카드 테두리+배지 경고만, 끊긴 멤버 30초 후 정리, 채팅(SonoBus 채팅 재사용, C 로 접기), 혼자일 때 큰 코드+복사(⌘C), 나가기 확인(Esc), 설정(⌘,).
- 단축키는 플러그인에서도 창에 포커스가 있을 때만 동작 (JUCE 키 이벤트 특성상 자동으로 그렇게 됨).
- 설정: 오디오 장치(입력·출력·버퍼, 합주 중 즉시 적용)·닉네임·로그 폴더 열기·앱 정보. 플러그인은 장치 항목 대신 호스트 정보 표시.
- Windows: 첫 실행 오디오 화면에서 ASIO 장치가 0개면 "오디오 인터페이스가 필요합니다" 안내 + 다시 검색/설치 안내 버튼 (Windows 에서 실제 동작 미확인).
- Mac: 첫 실행 헤드폰 화면에서 출력이 내장 스피커면 경고 (장치 이름에 '스피커/Speakers/내장 출력' 포함 여부로 판정).
- 확인: 독립 앱 실행, 화면 캡처(첫 실행 4단계·홈·방 만들기·코드 입장·합주·설정), 같은 Mac 에서 두 인스턴스가 방 생성/코드 입장으로 서로 연결되는 것(핑 0 ms, 지터버퍼 2.7 ms 자동)까지 확인. AU 검증(`auval -v aumf Mjam Meon`) 통과, VST3 로드 확인.
- 개발·테스트용 실행 옵션(독립 앱): `--nickname=이름 --screen=home|audio|hp|level|create|join|jam|settings --create --join=코드 --enter --no-input --window=x,y,w,h`

### 디자인에 없어서 임의로 정한 값 (확인 요청)
| 항목 | 정한 값 |
|---|---|
| 앱 아이콘 | 흰 바탕 둥근 사각 + MEON 로고 텍스트 (임시) |
| 창 타이틀바 | 독립 앱은 OS 기본(네이티브) 타이틀바. 목업의 회색 바(28px)는 그리지 않음 |
| 멤버 카드의 '파트'(기타/베이스 등) | 엔진에 그런 정보가 없어 표시하지 않음. 내 영역은 "나 · 기타" 대신 "나" |
| 홈 하단 "서울" | 서버 위치 정보가 없어 "서버 연결됨 · 핑 N ms" 만 표시 |
| 서버 핑 | 서버까지 TCP 접속 시간으로 어림(참고용). 홈에서 5초, 방에서 10초 주기 |
| 코드 확인 중 문구 | "코드를 확인하고 있어요…" (400 14 #767676) |
| 입장 실패(네트워크) 문구 | "연결에 문제가 있어요. 잠시 후 다시 시도해 주세요" |
| 방 만들기 실패 문구 | "방을 만들지 못했어요. 홈으로 돌아가 다시 시도해 주세요" |
| 멤버 상태 문구 추가 | "연결 중…" (P2P 연결 전), "연결 실패 · 네트워크 확인 필요" (NAT 실패) |
| 종료 시 확인 | 방에 있을 때 창을 닫으면 "방을 나가고 종료할까요" / [나가고 종료] |
| 콤보박스(닫힘) | 디자인 select 필드대로: 높이 48 → 글자 16 · 안쪽 여백 14, 높이 40/38 → 글자 15 · 안쪽 여백 12. 화살표 10×6 삼각형 #767676, 오른쪽 여백 = 안쪽 여백. 목록이 열려 있는 동안 테두리 #FF5A3D |
| 드롭다운 목록(열림, 디자인 없음) | 흰 배경 · 모서리 6 · 1px #D5D5D5 테두리, 그림자 없음. 창 안쪽 여백 4, 필드와 간격 4. 항목 높이 36(글자 15) / 40(글자 16), 글자 x 위치는 필드 글자와 동일. 호버·선택 이동 배경 #F5F5F5 모서리 4, 현재 값은 #FF5A3D 세미볼드 |
| 채팅 스크롤바 | 6px, #D5D5D5, 트랙 없음 |
| 설정 화면이 창보다 길 때 | 세로 스크롤 (설정 본문 폭은 고정) |
| 볼륨 슬라이더 값 매핑 | 게인 0~2 를 skew 0.69 로 (목업의 % 위치와 일치: 0 dB = 62%) |
| 레벨 미터 눈금 | -60/-40/-20/-12/-6/0 을 균등 간격 구간 선형 보간 |
| 복사 버튼 | 누르면 1.5초 동안 "복사됨" (디자인 주석대로) |
| Windows 단축키 표기 | "Ctrl ," / "Ctrl C" |
| 채팅 시각 | "오후 9:12" 형식, 시스템 메시지 "OO님이 입장했습니다 / 나갔습니다" |
| 텍스트 입력 포커스 | 모든 입력(채팅·닉네임·초대 코드) 밖을 클릭하면 포커스가 빠져 글자 단축키가 다시 듣는다. 설정처럼 스크롤 영역(Viewport) 안의 입력도 포함. 전송 버튼으로 보낸 뒤에는 채팅 입력창에 포커스를 되돌린다 (2026-09-23 요청) |
| 채팅 링크 | `http://`·`https://`·`www.` 로 시작하는 주소(공백·이모지 전까지, 끝 문장부호 제외)를 #767676 + 밑줄로. 링크 위에서 손 커서, 누르면 "링크를 열까요" 확인 창 [취소]/[열기] (Esc/Enter) 후 기본 브라우저. 한글 등은 퍼센트 인코딩 (2026-10-09 요청) |
| 멤버별 패닝 | 멤버 카드 뮤트 줄 오른쪽 끝에 노브(앱 40 + '패닝' 라벨, 플러그인 32). -100..100 정수, 값×1.35°. 세로 드래그 1px=1 (Shift 0.25배), \|값\|<3 은 0, 더블클릭 0, 휠 ±1. 끊김은 #E0E0E0 + '—' + 조작 불가. 내 믹스에만 적용(엔진 채널 그룹 패닝, 엔진 팬 법칙 가운데 -4.5 dB 그대로), 전송 안 함. 멤버가 나갔다 오면 0, 플러그인 에디터를 다시 열면 엔진 값을 이어받음 (2026-10-09 요청) |
| 볼륨 손잡이 | 둥근 사각 → 흰 원 (앱 16, 플러그인 14), 테두리 1px 를 패닝 노브와 같은 #D5D5D5 / 끊김 #E0E0E0 로 (`MeonLookAndFeel::controlBorder` 공유). 트랙·채움은 그대로 (2026-10-09 요청) |
| 한글 조합 중 표시 | JUCE 는 입력기가 조합 중인 마지막 음절에 점선 밑줄을 그리는데, macOS 기본 입력창처럼 표시하지 않는다 (2026-09-23 요청) |

### 디자인 규칙 중 확인 못 한 것
- (해결, 2026-09-23) 드롭다운 목록이 열리면 둥근 목록 뒤에 같은 크기의 네모 배경이 보이던 문제: 팝업 배경색을 불투명 흰색으로 지정해 JUCE 가 메뉴 창을 불투명 사각 창으로 만들고 OS 그림자까지 붙인 것이 원인. 배경색을 투명으로 두고 둥근 배경을 직접 그리며 OS 그림자는 끔(`getMenuWindowFlags` = 0). macOS 에서 캡처로 확인, Windows 미확인.
- 설정의 버퍼 크기 목록은 첫 실행 화면과 같은 64/128/256/512 네 가지만 보여 준다 (2026-09-23, 사용자 요청). 장치가 다른 값을 쓰고 있으면 그 값을 선택 없이 글자로만 보여 준다. 목록이 아래 공간보다 길면 JUCE 기본 규칙대로 위쪽으로 펼쳐지고 스크롤 화살표가 붙는다.

### 막힌 것
- 개발용 실행 옵션 `--screen=settings:input|output|buffer` 로 설정 화면의 드롭다운을 열어 둔 상태를 캡처할 수 있다.
- 마우스·키보드 자동 조작 권한이 없어 버튼 클릭 흐름(닉네임 입력→다음, 뮤트, 채팅 전송)은 눈으로 확인하지 못했다. 코드 경로만 검토했고, 화면 캡처는 실행 옵션으로 각 화면을 직접 열어 찍었다.
- 첫 실행 시 macOS 마이크 권한 대화상자가 뜬다(정상). 자동화 환경에서는 답할 수 없어 `--no-input` 옵션으로 캡처했다.
- Logic 에서 실제 UI 로드는 미확인 (auval 만 통과).

### 판단이 필요한 것
- 초대 코드를 SonoBus 그룹 이름으로 그대로 쓴다. 공개 서버의 다른 SonoBus 사용자가 우연히 같은 6자 그룹 이름을 쓸 가능성은 매우 낮지만 0 은 아니다. `meon-` 접두어를 붙이면 없어진다 (한 줄).
- 서버 사용자 이름은 `닉네임#4자리` 로 만들어 서버 전체의 이름 중복을 피한다. 화면에는 닉네임만 보인다.
- 6번째 입장 판정은 "입장 후 3초 안에 멤버가 5명 이상이면 나가기". 두 사람이 동시에 5·6번째로 들어오면 둘 다 나갈 수 있다.

---

## 5단계: 세션 로그 (2026-09-22)

### 한 것
- `Source/meon/MeonSessionLog.cpp`. 방에 있는 동안 5초마다 JSON 에 기록:
  멤버별 핑·왕복 지연·지터버퍼(ms)·수신/손실/재전송 패킷·끊김 횟수·연결 상태, 서버 연결 여부·서버 핑, 내 뮤트.
- 세션 시작·종료 시각과 종료 사유, 독립 앱/플러그인 구분, OS·CPU, 오디오 장치(입력·출력·샘플레이트·버퍼) 또는 호스트 이름.
- 이벤트: 방 생성/입장, 피어 대기/입장/**P2P(NAT) 연결 실패**/퇴장/끊김/재연결, 서버 끊김/재접속, 나가기, 뮤트,
  파일 열기/닫기/재생/정지 (`fileLoaded`, `fileClosed`, `filePlay`, `fileStop`).
- 샘플마다 `fileLoaded`·`filePlaying`·`fileGainDb`, 멤버별 `packetsSent`·`bytesSent`·`sendChannels` (재생 중 업로드 변화와 끊김 비교용).
- 파일: macOS `~/Library/Logs/MEON/meon-session-YYYYMMDD-HHMMSS.json`, Windows `%APPDATA%\MEON\logs\`. 설정 > 문제 해결 > "로그 폴더 열기" 로만 접근.

---

## 6단계: 배포 (2026-09-22, 일부)

### 한 것
- 설치 안내문 `doc/INSTALL.md` (Mac 확인되지 않은 개발자 경고, Windows SmartScreen, 플러그인 설치 위치, 서명 안 된 플러그인이 안 뜰 때 해결 방법, ASIO 안내).
- macOS 배포 zip 스크립트 `scripts/meon_package_mac.sh` (앱 + AU + VST3 + 안내문, ad-hoc 서명만).

### 막힌 것 / 남은 것
- Windows 빌드·패키징: Windows PC 에서 확인 필요 (`README.md` 의 Windows 절 참고, ASIO SDK 2.3.4 필요).
- 유니버설(x86_64+arm64) 최종 빌드는 `-DUniversalBinary=ON`(기본값)으로 다시 빌드해서 zip 을 만들 것. 이번 작업은 개발 속도를 위해 arm64 로 확인했다.

---

## 귀 테러 방지 (2026-10-12)

### 기존 SonoBus 기능 조사
- SonoBus 에는 **입력(보내는 쪽) 채널 그룹별 리미터**만 있다 (`ChannelGroup` 의 `limiter`, faust 컴프레서를 threshold -1 dB · ratio 4 로 쓰는 것).
  내 모니터 최종 출력에는 리미터·컴프레서가 없다. MEON 은 3단계에서 입력 리미터도 꺼 두었다.
- 입력 리미터는 내가 **보내는** 소리에 걸리므로 듣는 쪽 보호에는 쓸 수 없다 → 얹거나 대체하지 않고 출력 직전에 새로 추가.

### 한 것
- `Source/meon/MeonEarGuard.*`: 출력 직전 처리. `SonobusAudioProcessor::processBlock` 에서 마스터 볼륨(`wetnow`)을 곱한 뒤,
  출력 미터 측정 직전에 메인 버스 채널에만 적용. 송신·녹음·네트워크(AOO)·코덱 코드는 그대로.
  1. 대역 리미터: 2–5 kHz 대역 통과 필터(중심 3.16 kHz, Q 1.05) 출력의 레벨이 기준을 넘으면 `y = x − (1−g)·BP(x)` 로 그 대역만 판다. 기준 아래면 g = 1 이라 출력이 입력과 비트 단위로 같다.
  2. 전체 리미터: 대역 처리한 소리의 K-가중 레벨(ITU-R BS.1770 필터, 40 ms 평균 파워)이 상한을 넘으면 넘은 만큼 전체를 줄이고, 샘플 피크 상한은 즉시 누른다.
  - 미리보기 없음(IIR 만) → 지연 0, `setLatencySamples` 그대로. 모든 채널을 한 게인으로 연동. 오디오 스레드에서 할당·잠금 없음.
  - 단계 전환은 기준값을 100 ms 동안 옮겨 가며 바꾼다. 끔으로 가라앉으면 처리를 건너뛴다.
- 단계별 초기값 (`MeonEarGuard.cpp` 의 `kNormal` / `kStrong`):

  | | 대역 기준 (2–5 kHz RMS) | 대역 비율 | 대역 최대 감쇠 | K-가중 상한 | 피크 상한 |
  |---|---|---|---|---|---|
  | 보통 | -18 dBFS | 4:1 | 12 dB | -10 LKFS | -1 dBFS |
  | 강하게 | -26 dBFS | 8:1 | 18 dB | -16 LKFS | -4 dBFS |

- 설정 > 오디오 > 귀 테러 방지 (끔 · 보통 · 강하게, 기본 보통, 설정 파일 `earGuard`). 독립 앱 섹션 제목을 "오디오 장치" → "오디오" 로 바꿨다.
  플러그인의 오디오 섹션(DAW 정보 칸) 아래에도 같은 줄을 둔다. 프로세서가 켜질 때 설정 파일에서 읽으므로 플러그인 창을 열지 않아도 적용된다.
- 세션 로그: 방에 들어갈 때·단계를 바꿀 때 `earGuard` 이벤트, 샘플마다 `earGuard`(off/normal/strong)·`earGuardBandCount`·`earGuardFullCount`
  (방에 들어온 뒤 1 dB 이상 줄이기 시작한 횟수).

### Linux 에서 DSP 만 따로 돌려 본 결과 (48 kHz)
- 핑크 노이즈 RMS -18 dBFS(피크 -4): 끔·보통 출력이 입력과 비트 단위로 같음. 강하게 -0.25 dB.
- 핑크 노이즈 RMS -14(피크 -0.2): 보통 -0.03 dB, 강하게 -1.6 dB.
- 풀스케일 화이트 노이즈(RMS -4.8): 보통 → -15.0, 강하게 → -21.1. 피크는 정확히 -1 / -4 dBFS.
- 3 kHz 하울링(0 dBFS): 보통 RMS -3 → -14.3, 강하게 → -19.1.
- 지연: 입력·출력 상호상관 최대점이 0 샘플 (처리 중·처리 후 모두).
- 단계 전환 중 이웃 샘플 사이 게인 변화 최대 0.025 (끔→강하게, 큰 소리 중).

### 판단이 필요한 것
- 위 기준값은 초기값. 직접 들어 보고 조정.
- 같은 PC 에서 독립 앱과 플러그인(또는 플러그인 두 개)이 설정 파일을 공유한다. 한쪽에서 바꾸면 다른 쪽은 창을 다시 열 때 따라간다.
