# MEON 데스크톱 앱 기능 현황 (AS-IS)

이 문서는 "있어야 할 것"이 아니라 **지금 코드에 있는 것**만 적는다. 디자인·API 문서와 대조하는 용도.

**표기**
- 확인 방법: **[실행 확인]** = Linux 빌드를 가상 화면(Xvfb)에서 실제로 켜서 확인 / **[코드 확인]** = 코드만 읽고 판단
- 출처: **[SonoBus 원본]** = SonoBus 1.7.2 코드 그대로 / **[MEON 추가]** = MEON 이 새로 만든 것 / **[SonoBus 수정]** = SonoBus 엔진 기능을 MEON 이 고치거나 새 화면으로 감싼 것
- 상태: 동작함 / 일부 동작 / UI만 있음 / 코드만 있음 / 가짜 데이터
- 근거의 경로는 저장소 루트 기준. `Source/meon/` 은 `meon/` 으로 줄여 쓴다.

> **실행 확인의 범위 (중요)**
> - 실행 확인은 **Linux(Ubuntu 24.04) 독립 앱**에서만 했다. 배포 대상인 macOS·Windows 앱과 AU·VST3 플러그인은 이 컨테이너에서 실행할 수 없다.
> - 이 컨테이너에는 **오디오 장치가 없고 MEON 서버(`aoo.sonobus.net`)에 접속할 수도 없다**(DNS 조회 실패). 그래서 서버 연결, 방 만들기·입장, 멤버, 채팅 송수신, 소리는 실행으로 확인하지 못했다.
> - 실행 확인은 앱에 들어 있는 개발용 실행 옵션(`--screen`, `--nickname`, `--no-input`, `--snapshot`)으로 화면을 직접 열고 `xdotool` 로 키·마우스를 넣어 찍은 것이다. 따라서 합주 화면처럼 **실제 방에 들어가지 않은 상태로 그린 화면**도 있다(초대 코드가 비어 있는 등).

---

## 0. 기본 정보

| 항목 | 값 | 근거 |
|---|---|---|
| 저장소 / 브랜치 | `meonhq/desktop`. 분석한 작업 트리는 `claude/tender-keller-b60s9o` 의 `a689e93` (2026-10-12 23:18:08 UTC). `origin/main` 최신 `2cfdf5f` (2026-10-13 09:16:37 +0900, PR #25 머지)와 **파일 내용이 같다**(`git diff a689e93 origin/main` 결과 없음) | `git log`, `git diff` |
| 기반 | SonoBus 1.7.2 (upstream 마지막 커밋 `35f1062`, 2023-12-11, essej). 이후 MEON 커밋 78개 | `git log 35f1062..HEAD`, `doc/MEON_PROGRESS.md:3` |
| 앱 버전 표기 | `Meon 1.0.0` + 빌드 번호 (CI `run_number`, 로컬 빌드는 0 → 화면에는 `build 1`) | `CMakeLists.txt:44-48`, `meon/MeonEditor.cpp:270-278` |
| JUCE 버전 | 7.0.8 (`deps/juce`) | `deps/juce/modules/juce_core/system/juce_StandardHeader.h:30-32` |
| 빌드 대상 | 하나의 CMake 타깃 `MEON`. macOS: 독립 앱(`Meon.app`) + AU + VST3. Windows: 독립 앱(`Meon.exe`) + VST3. Linux: 독립 앱 + VST3 + LV2(배포 안 함) | `CMakeLists.txt:92-102`, `533`, `.github/workflows/mac.yml:40`, `.github/workflows/windows.yml:47` |
| 플러그인 식별자 | 제조사 코드 `Meon`, 플러그인 코드 `Mjam`, AU 타입 MusicEffect(`aumf`), 번들 ID `com.meon.MEON` | `CMakeLists.txt:154-176` |
| 빌드 성공 여부 | **Linux 독립 앱: 성공** (이 컨테이너, g++ 13.3, CMake 3.28, Release, 경고 13개 중 `Source/meon` 경고 0개) [실행 확인]<br>**macOS: 성공**. 같은 트리로 Actions `macOS build` #50(작업 브랜치)과 #51(main) 이 돌았고, #50 은 성공, #51 은 릴리스 `mac-latest/build.txt`=`51` 로 마지막 단계까지 끝났다. [실행 확인 = CI 결과 확인]<br>**Windows: 성공**. `Windows build` #58 성공, #59 는 `windows-latest/build.txt`=`59`. [실행 확인 = CI 결과 확인] | 빌드 로그(scratchpad), GitHub Actions run 38322370676 / 38322372771, 릴리스 build.txt |
| 확인한 OS | 실행: Linux(Ubuntu 24.04.5, Xvfb) 만. macOS·Windows 는 CI 컴파일만 확인 | — |

빌드 중 오류는 없다. 경고 13개는 모두 SonoBus 셸(`SonoStandaloneFilterWindow.h`, `SonoStandaloneFilterApp.cpp`)의 JUCE 폐기 예정 API 사용과 aoo·JUCE 내부 경고다.

---

## 1. 화면 목록

화면 전환은 `MeonEditor::go()` 하나로 하고 120 ms 페이드한다 (`meon/MeonEditor.cpp:133-175`, `meon/MeonTheme.h:42`). 플러그인에서 `Audio`·`Level` 화면을 부르면 홈으로 바꾼다 (`meon/MeonEditor.cpp:135-136`).

### 1-1. 전체 화면

| # | 화면 | 여는 곳 | 들어 있는 것 | 독립 앱 / 플러그인 차이 | 확인 | 근거 |
|---|---|---|---|---|---|---|
| S1 | 첫 실행 1: 이름과 파트 | 닉네임이 비어 있으면 켤 때 | 진행 막대, 닉네임 입력, 파트 6칸, 다음 | 앱 1/4, 플러그인 1/2. 앱만 입력 규칙 힌트. 하단 문구: 앱 "계정이나 로그인은 필요 없어요", 플러그인 "오디오 장치는 DAW 설정을 따릅니다" | [실행 확인] 앱 | `meon/OnboardingScreens.cpp:66-141`, `meon/MeonEditor.cpp:106-115` |
| S2 | 첫 실행 2: 오디오 장치 | S1 다음 (앱만) | (Windows) 드라이버, 입력·출력 장치, 버퍼 4버튼, (Windows) 경고 상자 | 플러그인에는 없음 | [실행 확인] Linux (장치 없음 상태) | `meon/OnboardingScreens.cpp:144-406` |
| S3 | 첫 실행 3: 헤드폰 | S2 다음 / 플러그인은 S1 다음 | 체크 행 "헤드폰을 쓰고 있어요", (macOS) 스피커 경고 | 앱 3/4, 플러그인 2/2(버튼 "시작하기"). 스피커 경고는 macOS 앱만 | [실행 확인] 앱 | `meon/OnboardingScreens.cpp:409-538` |
| S4 | 첫 실행 4: 소리 내보기 | S3 다음 (앱만) | 입력 레벨 막대·눈금, 신호 없음 경고, 시작하기 | 플러그인에는 없음 | [실행 확인] | `meon/OnboardingScreens.cpp:541-749` |
| S5 | 홈 | 첫 실행을 끝낸 뒤 켤 때 / 방을 나간 뒤 | 로고, 인사말, 방 만들기, 코드로 입장, 설정, 서버 상태, 디스코드, (앱) 새 버전 알림 | 플러그인은 버튼·글자가 작고, 하단에 호스트 이름·샘플레이트·블록 크기를 덧붙인다. 새 버전 알림은 앱만 | [실행 확인] 앱 | `meon/LobbyScreens.cpp:132-271` |
| S6 | 방 만들기 | 홈 > 방 만들기 | 초대 코드(크게), 코드 복사, 입장하기, 안내문 | 플러그인은 안내문 없음, 글자 작음 | [실행 확인] 앱 (서버 없이 열어서 코드는 빈 칸) | `meon/LobbyScreens.cpp:274-409` |
| S7 | 코드로 입장 | 홈 > 코드로 입장 | 6칸 코드 입력, 입장하기, 안내·오류 문구 | 플러그인은 부제·긴 안내 없음 | [실행 확인] 앱 | `meon/LobbyScreens.cpp:412-569` |
| S8 | 합주 | 방 입장 / 플러그인 창을 다시 열었는데 방에 있을 때 | 상단 바, 내 영역, 파일 플레이어, 멤버 카드 2×2(또는 "아직 혼자예요"), 채팅 패널(또는 접힌 한 줄) | 플러그인: 상단 바 낮음, "초대 코드" 글자 없음, 나가기 버튼 "나가기", 카드의 파트 이름이 이름 옆, 패닝 라벨 없음, 창 폭 800 미만이면 채팅 자동 접힘·서버 문구는 점+툴팁 | [실행 확인] 앱 (실제 방 아님) | `meon/JamScreen.cpp:1226-1672` |
| S9 | 설정 (덮개) | 홈·합주의 "설정" 버튼, ⌘, / Ctrl+, | 오디오, 닉네임, 파트, 문제 해결, (앱) 업데이트, 앱 정보 | 플러그인: 장치·버퍼 대신 "DAW 설정을 따릅니다" 칸 + 귀 테러 방지, 업데이트 칸 없음, "소스 코드" 링크 없음 | [실행 확인] 앱 | `meon/SettingsScreen.cpp`, `meon/MeonEditor.cpp:182-208` |

### 1-2. 모달·대화상자·배너·툴팁·팝업

| 종류 | 이름 | 뜨는 때 | 내용 | 확인 | 근거 |
|---|---|---|---|---|---|
| 모달 | 방 나가기 확인 | 합주 화면 Esc / "방 나가기" / 채팅 입력 중 Esc | "방을 나갈까요" + 머무르기 / 나가기. 화면 전체를 25% 검정으로 덮음 | [실행 확인] | `meon/JamScreen.cpp:1081-1152`, `1342-1348` |
| 모달 | 나가고 종료 확인 | 방 안에서 앱 창을 닫을 때 (앱) | 같은 창, 제목 "방을 나가고 종료할까요", 버튼 "나가고 종료" | [코드 확인] | `meon/JamScreen.cpp:1097-1103`, `1414-1423`, `meon/MeonEditor.cpp:211-224` |
| 모달 | 링크 열기 확인 | 채팅 속 링크 클릭 | "링크를 열까요" + 주소(300자 넘으면 줄임) + 취소 / 열기 | [코드 확인] | `meon/JamScreen.cpp:1156-1223`, `1350-1377` |
| 대화상자 | 이전 실행 오류 | 지난 실행이 켜지다 죽었을 때 (앱) | "이전 실행 오류 / 지난번 실행이 정상적으로 끝나지 않아 설정을 초기화했습니다." (JUCE 기본 경고 창) | [코드 확인] | `Source/SonoStandaloneFilterApp.cpp:221-226` |
| 배너 | 새 버전 알림 | 홈 상단 가운데 (macOS·Windows 앱, CI 빌드만) | "새 버전이 나왔어요 · 업데이트" / "받는 중 N%" / "설치할 준비가 됐어요 · 다시 켜서 설치" / "새 버전을 받지 못했어요 · 다시 시도" / "응용 프로그램 폴더에서 켜야 업데이트돼요" | [코드 확인] (Linux 는 업데이트 미지원이라 실행 불가) | `meon/LobbyScreens.cpp:7-107`, `163-168` |
| 경고 상자 | 오디오 장치 경고 | S2, Windows: 장치 없음 또는 ASIO 아님 | 제목·설명 + 장치 다시 검색 / ASIO 설치 안내 보기 | [코드 확인] | `meon/OnboardingScreens.cpp:266-286` |
| 경고 상자 | 스피커 경고 | S3, macOS 앱: 출력 이름에 스피커 | "지금 출력이 ‘…’예요" + 출력 장치 바꾸기 | [코드 확인] | `meon/OnboardingScreens.cpp:494-515` |
| 경고 상자 | 입력 신호 없음 | S4: 3초 동안 -55 dB 이하 | 원인 3줄 + 입력 채널 바꾸기 / 장치 다시 고르기 | [코드 확인] (아래 10장의 첫 3초 문제는 [실행 확인]) | `meon/OnboardingScreens.cpp:564-575`, `613-643` |
| 인라인 알림 | 입장 실패 | S7 | 잘못된 코드 / 방이 꽉 참 / 연결 문제 (노란 상자) | [코드 확인] | `meon/LobbyScreens.cpp:543-568` |
| 팝업 메뉴 | 입력 채널 선택 | S4 "입력 채널 바꾸기" | "입력 N", "입력 N+N+1 (스테레오)" | [코드 확인] | `meon/OnboardingScreens.cpp:645-670` |
| 팝업 목록 | 콤보박스 목록 | S2·설정의 드롭다운 | 둥근 흰 목록, 현재 값 강조 | [코드 확인] (장치가 없어 목록을 열 수 없었다) | `meon/MeonLookAndFeel.h:20-38` |
| 파일 대화상자 | 오디오 파일 열기 | 파일 플레이어 "파일 열기" | OS 파일 선택 창, 오디오 형식만 | [코드 확인] | `meon/JamScreen.cpp:442-464` |
| 툴팁 | 서버 상태 | 플러그인 합주 화면 폭 < 800 에서 점 위 | 서버 상태 문구 | [코드 확인] | `meon/JamScreen.cpp:1606-1616` |
| 툴팁 | 디스코드 / 파일 닫기 / 파일 볼륨 | 버튼 위 | "Discord" / "파일 닫기" / "더블클릭하면 0 dB" | [코드 확인] | `meon/LobbyScreens.cpp:114`, `meon/JamScreen.cpp:215`, `231` |
| 토스트 | (없음) | — | 토스트 컴포넌트는 없다. "복사됨"·"저장됨"은 버튼 글자를 1.5초 바꾸는 방식 | [코드 확인] | `meon/LobbyScreens.cpp:340-347`, `meon/SettingsScreen.cpp:318-320` |
| OS 권한 | 마이크 권한 (macOS) | 앱이 입력 장치를 열 때 | OS 대화상자 | [코드 확인] | `CMakeLists.txt:156` (`MICROPHONE_PERMISSION_ENABLED`) |

---

## 2. 기능 상세 (화면별 컨트롤)

### S1. 이름과 파트

| 이름 | 하는 일 | 값·범위·기본값·특이 동작 | 상태 | 출처 | 근거 |
|---|---|---|---|---|---|
| 진행 막대 | 단계 표시 | 앱 n/4, 플러그인 n/2 | 동작함 [실행 확인] | [MEON 추가] | `meon/MeonWidgets.cpp:647-679` |
| 닉네임 입력 | 표시 이름 입력 | 2–12자 (trim 후). 한글(완성형·자모)·영문·숫자·공백·키보드 기호·한글 입력기 특수문자 허용, 이모지·제어 문자 거부. 입력창은 12자에서 더 안 받음. 첫 표시 때 자동 포커스. Enter = 다음 | 동작함 [실행 확인: "a" 한 글자는 다음 비활성, "abc"+파트는 활성] | [MEON 추가] | `meon/OnboardingScreens.cpp:90-121`, `meon/MeonSettings.cpp:129-159` |
| 파트 6칸 | 내 악기 선택 (하나만) | 보컬·기타·베이스·드럼·키보드·그 외. 첫 실행 기본 선택 없음. 선택 칸은 노랑 #FFD700 | 동작함 [실행 확인] | [MEON 추가] | `meon/MeonParts.cpp:32-50`, `202-227`, `meon/OnboardingScreens.cpp:86-88` |
| 다음 | 닉네임·파트 저장 → 서버 연결 시작 → 앱 S2 / 플러그인 S3 | 닉네임 유효 + 파트 선택일 때만 활성 | 동작함 [실행 확인: 활성/비활성] / 이동은 [코드 확인] | [MEON 추가] | `meon/OnboardingScreens.cpp:107-121` |

### S2. 오디오 장치 (독립 앱만)

| 이름 | 하는 일 | 값·범위·기본값·특이 동작 | 상태 | 출처 | 근거 |
|---|---|---|---|---|---|
| 드라이버 (Windows) | 오디오 드라이버 종류 바꾸기 | ASIO → Windows 오디오·저지연 → 단독 사용 → 기본 순으로, 있는 것만. 처음 켤 때 기본값: ASIO 장치가 있으면 ASIO, 없으면 저지연 | 동작함 [코드 확인] | [SonoBus 수정] | `meon/MeonAudio.cpp:17-64`, `Source/SonoStandaloneFilterWindow.h:129-131` |
| 입력 장치 / 출력 장치 | 장치 바꾸기 (즉시 적용·저장) | 장치가 없으면 "장치를 찾을 수 없음" + 비활성. ASIO 는 입력·출력을 같은 장치로 맞춤. 샘플레이트 없으면 48 kHz | 동작함 [실행 확인: 장치 없음 표시] / 바꾸기는 [코드 확인] | [SonoBus 수정] | `meon/OnboardingScreens.cpp:217-319`, `meon/MeonAudio.cpp:66-89` |
| 버퍼 크기 4버튼 | 장치 버퍼 바꾸기 | 64/128/256/512 에 가장 가까운, 장치가 실제 지원하는 크기. 아래 줄에 `크기/샘플레이트` ms. 지금 값은 노란 테두리 | 동작함 [실행 확인: 장치 없을 때 64/128/256/512, 1.3/2.7/5.3/10.7 ms 표시] | [MEON 추가] | `meon/OnboardingScreens.cpp:186-193`, `321-365`, `meon/MeonAudio.cpp:91-126` |
| 경고 상자 + 장치 다시 검색 + ASIO 설치 안내 보기 (Windows) | 장치 재검색 / 브라우저로 `doc/INSTALL.md#windows-asio` | 장치 없음이거나 ASIO 가 아닐 때만 보임 | 동작함 [코드 확인] | [MEON 추가] | `meon/OnboardingScreens.cpp:160-167`, `266-286` |
| 뒤로 | S1 로 | — | 동작함 [코드 확인] | [MEON 추가] | `meon/OnboardingScreens.cpp:156-157` |
| 다음 | S3 로 | 입력·출력이 모두 없으면 비활성 | 동작함 [실행 확인: 장치 없음 → 비활성] | [MEON 추가] | `meon/OnboardingScreens.cpp:158`, `287` |

### S3. 헤드폰 확인

| 이름 | 하는 일 | 값·범위·기본값·특이 동작 | 상태 | 출처 | 근거 |
|---|---|---|---|---|---|
| "헤드폰을 쓰고 있어요" 체크 행 | 사용자가 직접 체크 | 기본 꺼짐. 행 전체가 버튼 | 동작함 [실행 확인: 꺼짐 표시] | [MEON 추가] | `meon/OnboardingScreens.cpp:409-447`, `483-484` |
| 스피커 경고 + 출력 장치 바꾸기 | (macOS 앱) 출력이 스피커면 경고, 버튼은 S2 로 | 판정은 **출력 장치 이름 문자열**뿐: `speaker`·`스피커`·`built-in output`·`내장 출력` 포함 여부. 1초마다 다시 확인. Windows·플러그인은 판정 없음 | 일부 동작 (macOS 만, 이름으로만 판정) [코드 확인] | [MEON 추가] | `meon/OnboardingScreens.cpp:486-515` |
| 뒤로 | 앱 S2 / 플러그인 S1 | — | 동작함 [코드 확인] | [MEON 추가] | `meon/OnboardingScreens.cpp:458` |
| 다음 / 시작하기(플러그인) | 앱 S4 / 플러그인은 첫 실행 완료 저장 후 홈 | 체크해야 활성. 앱은 옆에 "체크해야 넘어갈 수 있어요" | 동작함 [실행 확인: 비활성+문구] | [MEON 추가] | `meon/OnboardingScreens.cpp:461-474`, `517-522` |

### S4. 소리 내보기 (독립 앱만)

| 이름 | 하는 일 | 값·범위·기본값·특이 동작 | 상태 | 출처 | 근거 |
|---|---|---|---|---|---|
| 입력 레벨 라벨 | "입력 레벨 — 장치이름 · 입력 N(+N+1)" | — | 동작함 [실행 확인] | [MEON 추가] | `meon/OnboardingScreens.cpp:582-598` |
| 값 (dB) | 최고점 표시 | 1.5초 동안 최고값 유지. -60 이하는 "-60 dB". 신호 없으면 "신호 없음" | 동작함 [실행 확인: -60 dB] | [MEON 추가] | `meon/OnboardingScreens.cpp:600-632` |
| 레벨 막대 + 눈금 | 장치 입력 레벨 (뮤트와 무관) | -60/-40/-20/-12/-6/0 눈금을 같은 간격으로 놓은 구간 선형. 화면 주사율마다 갱신 | 동작함 [실행 확인: 모양] / 움직임은 [코드 확인] | [MEON 추가] | `meon/OnboardingScreens.cpp:613-624`, `700-731`, `meon/MeonWidgets.cpp:253-267` |
| 정상 행 | "신호가 잘 들어오고 있어요. 최고점이 -12 ~ -6 dB 사이면 알맞아요." | 신호 없음이 아니면 표시 | 일부 동작: 화면을 연 뒤 3초 동안은 신호가 없어도 표시된다 (10장) [실행 확인] | [MEON 추가] | `meon/OnboardingScreens.cpp:577-578`, `733-748` |
| 신호 없음 경고 | 3초 동안 -55 dB 를 넘지 못하면 | 버튼 2개 (아래) | 동작함 [코드 확인] | [MEON 추가] | `meon/OnboardingScreens.cpp:626-641` |
| 입력 채널 바꾸기 | 팝업에서 모노 1채널 또는 스테레오 2채널 선택 → 설정 저장 + 엔진 입력 채널 그룹 변경 | 기본 "입력 1" 모노. 목록은 지금 장치의 활성 입력 채널 수만큼 | 동작함 [코드 확인] | [SonoBus 수정] | `meon/OnboardingScreens.cpp:645-670`, `meon/MeonSession.cpp:328-341` |
| 장치 다시 고르기 | S2 로 | — | 동작함 [코드 확인] | [MEON 추가] | `meon/OnboardingScreens.cpp:572` |
| 뒤로 | S3 로 | — | 동작함 [코드 확인] | [MEON 추가] | `meon/OnboardingScreens.cpp:550-551` |
| 시작하기 | 첫 실행 완료 저장 → 홈 | **항상 활성** (신호가 없어도 넘어감, "신호가 없어도 넘어갈 수 있어요") | 동작함 [실행 확인: 활성] | [MEON 추가] | `meon/OnboardingScreens.cpp:552-557`, `610` |

### S5. 홈

| 이름 | 하는 일 | 값·범위·기본값·특이 동작 | 상태 | 출처 | 근거 |
|---|---|---|---|---|---|
| 로고 | MEON 필기체 로고 | `images/meon_logo.svg` 를 앱에 넣어 경로로 그림 | 동작함 [실행 확인] | [MEON 추가] | `meon/MeonTheme.cpp:285-325`, `CMakeLists.txt:467-473` |
| 인사말 | "{닉네임}님, 오늘도 좋은 합주 되세요" | — | 동작함 [실행 확인] | [MEON 추가] | `meon/LobbyScreens.cpp:240-243` |
| 방 만들기 | 6자리 코드를 만들고 서버 그룹에 입장 시작 → S6 | 서버 연결 중이 아니면 비활성 | 비활성 표시 [실행 확인] / 방 만들기는 확인 못 함 (서버 접속 불가) | [SonoBus 수정] | `meon/LobbyScreens.cpp:147-151`, `190-196`, `meon/MeonSession.cpp:447-458` |
| 코드로 입장 | S7 로 | 서버 연결 중이 아니면 비활성 | 비활성 표시 [실행 확인] | [MEON 추가] | `meon/LobbyScreens.cpp:153-157` |
| 설정 | 설정 덮개 열기 | — | 동작함 [실행 확인: Ctrl+, 로 열고 Esc 로 닫힘] | [MEON 추가] | `meon/LobbyScreens.cpp:139-141` |
| 서버 상태 (하단) | 노란 점 + "서버 연결됨" / "서버에 연결 중…" / "서버 연결 끊김 · 재연결 중…" | 점 색은 상태와 관계없이 항상 노랑. **핑 숫자는 표시하지 않음**. 1초마다 다시 그림. 플러그인은 `호스트 48 kHz / N 샘플` 을 덧붙임 | 동작함 [실행 확인: "서버에 연결 중…"] | [MEON 추가] | `meon/LobbyScreens.cpp:245-270`, `meon/MeonSession.cpp:422-430` |
| 디스코드 버튼 | 브라우저로 `https://discord.gg/vtGQpnc2HQ` | 오른쪽 아래 검은 원 | 동작함 [실행 확인: 표시] / 링크 열기는 [코드 확인] | [MEON 추가] | `meon/LobbyScreens.cpp:110-129` |
| 새 버전 알림 | 8장 참고 | macOS·Windows 앱, CI 빌드만 | [코드 확인] | [MEON 추가] | `meon/LobbyScreens.cpp:7-107` |

### S6. 방 만들기

| 이름 | 하는 일 | 값·범위·기본값·특이 동작 | 상태 | 출처 | 근거 |
|---|---|---|---|---|---|
| ← 홈 | **방을 나가고** 홈으로 | 입장 전에 나가면 방(그룹)이 사라짐 | 동작함 [코드 확인] | [MEON 추가] | `meon/LobbyScreens.cpp:282-286` |
| 초대 코드 | 6자, 글자 간격 0.14em, 앱 80 px / 플러그인 60 px | 문자 집합 `ABCDEFGHJKLMNPQRSTUVWXYZ23456789` (O·0·I·1 제외) | 동작함 [코드 확인] (실행 화면은 서버 없이 열어 빈 칸) | [MEON 추가] | `meon/MeonSession.cpp:10`, `200-208`, `meon/LobbyScreens.cpp:395-401` |
| 코드 복사 | 클립보드로 복사, 1.5초 "복사됨" | ⌘C / Ctrl+C 도 같음 | 동작함 [코드 확인] | [MEON 추가] | `meon/LobbyScreens.cpp:340-347`, `325-331` |
| 입장하기 | 합주 화면으로 | 서버 그룹 입장이 끝나야 활성. Enter 도 같음 | 비활성 표시 [실행 확인] / 입장은 확인 못 함 | [MEON 추가] | `meon/LobbyScreens.cpp:292-296`, `319-323`, `332-336` |
| 안내 / 실패 문구 | "나 포함 최대 5명까지 들어올 수 있어요"(앱) / "방을 만들지 못했어요. 홈으로 돌아가 다시 시도해 주세요" | 실패는 서버 오류·연결 끊김·12초 연결 시간 초과 때 | [실행 확인: 안내] / 실패는 [코드 확인] | [MEON 추가] | `meon/LobbyScreens.cpp:302`, `312-317` |

### S7. 코드로 입장

| 이름 | 하는 일 | 값·범위·기본값·특이 동작 | 상태 | 출처 | 근거 |
|---|---|---|---|---|---|
| ← 홈 | 홈으로 | — | 동작함 [코드 확인] | [MEON 추가] | `meon/LobbyScreens.cpp:420` |
| 6칸 코드 입력 | 코드 입력 | A–Z·0–9 만. **소문자는 대문자로 바뀜**. 0·1·O·I 도 입력됨(만들 때는 쓰지 않는 글자). Backspace·Delete 는 마지막 글자 삭제. ⌘V/Ctrl+V 는 클립보드에서 허용 문자만 6자까지(키 코드로 판별해서 한글 자판에서도 동작). Enter = 입장 | 동작함 [실행 확인: "ab12xy" → "AB12XY"] / 붙여넣기는 [코드 확인] | [MEON 추가] | `meon/MeonWidgets.cpp:538-644` |
| 입장하기 (Enter 배지) | 서버 그룹에 입장 시도 | 6자 + 서버 연결됨 + 확인 중 아님일 때만 활성 | 비활성 표시 [실행 확인] / 입장은 확인 못 함 | [SonoBus 수정] | `meon/LobbyScreens.cpp:462-469`, `482-488` |
| 메시지 영역 | 안내 / "코드를 확인하고 있어요…" / 실패 3종 | 높이가 고정이라 버튼이 움직이지 않음 | 안내 [실행 확인] / 나머지 [코드 확인] | [MEON 추가] | `meon/LobbyScreens.cpp:532-568` |

### S8. 합주

**상단 바**

| 이름 | 하는 일 | 값·범위·기본값·특이 동작 | 상태 | 출처 | 근거 |
|---|---|---|---|---|---|
| 로고 · "초대 코드"(앱) · 코드 | 지금 방 코드 | — | 표시 [실행 확인] | [MEON 추가] | `meon/JamScreen.cpp:1618-1644` |
| 복사 | 코드 복사, 1.5초 "복사됨" | ⌘C/Ctrl+C 와 같음 | 동작함 [코드 확인] | [MEON 추가] | `meon/JamScreen.cpp:1324-1340` |
| 서버 상태 | 점 + "서버 연결됨" / "서버 재연결 중…" / "서버에 연결 중…" | 플러그인 폭 800 미만이면 점만 + 툴팁 | 동작함 [실행 확인: "서버에 연결 중…"] | [MEON 추가] | `meon/JamScreen.cpp:1646-1671` |
| 멤버 N / 5 | 나 포함 인원 | — | 동작함 [실행 확인: 1 / 5] | [MEON 추가] | `meon/JamScreen.cpp:1648` |
| 설정 | 설정 덮개 | — | 동작함 [코드 확인] | [MEON 추가] | `meon/JamScreen.cpp:1233-1235` |
| 방 나가기 (Esc 배지) | 나가기 확인 창 | — | 동작함 [실행 확인: Esc] | [MEON 추가] | `meon/JamScreen.cpp:1236-1239` |

**내 영역**

| 이름 | 하는 일 | 값·범위·기본값·특이 동작 | 상태 | 출처 | 근거 |
|---|---|---|---|---|---|
| 파트 아이콘 + 닉네임 / "나 · 파트" | 내 표시 | 파트를 고르지 않았으면 "나" | 동작함 [실행 확인] | [MEON 추가] | `meon/JamScreen.cpp:159-178` |
| 내 입력 레벨 막대 | **실제로 보내는 신호**(송신 버퍼, 파일 재생 포함) | 마이크를 끄면 막대를 비움 | 동작함 [코드 확인] | [SonoBus 수정] | `meon/JamScreen.cpp:1513-1522`, `meon/MeonSession.cpp:869-874`, `Source/SonobusPluginProcessor.cpp:7791` |
| "최고 N dB" (앱) | 최고점 1.5초 유지 | 마이크가 꺼지면 "마이크가 꺼져 있어 아무도 못 들어요" | 동작함 [실행 확인: 두 문구 모두] | [MEON 추가] | `meon/JamScreen.cpp:180-188` |
| 마이크 켜짐 / 꺼짐 (M 배지) | 송신 전체 켜고 끄기 (엔진 `mastsendmute`) | 꺼짐은 노란 버튼 + 패널 노란 테두리. **파일 재생 소리도 함께 안 나감** | 동작함 [실행 확인: M 키로 꺼짐 표시] / 실제 송신 중단은 [코드 확인] | [SonoBus 수정] | `meon/JamScreen.cpp:97`, `103-113`, `meon/MeonSession.cpp:721-728`, `Source/SonobusPluginProcessor.cpp:6732-6760` |

**파일 플레이어** (4장에 신호 경로)

| 이름 | 하는 일 | 값·범위·기본값·특이 동작 | 상태 | 출처 | 근거 |
|---|---|---|---|---|---|
| 파일 열기 | OS 파일 선택 창 | 시작 폴더: 이번 실행에서 마지막 연 폴더, 없으면 음악 폴더. 못 열면 4초 "열 수 없는 파일이에요" | 동작함 [실행 확인: 버튼 표시] / 열기는 [코드 확인] | [SonoBus 수정] | `meon/JamScreen.cpp:442-464` |
| 파일 이름 | 표시 | macOS 의 자모 분리 한글(NFD)을 합쳐서 표시 | 동작함 [코드 확인] | [MEON 추가] | `meon/MeonSession.cpp:733-761`, `793-804` |
| × (파일 닫기) | 파일 닫기 | 방을 나가도 자동으로 닫힘 | 동작함 [코드 확인] | [MEON 추가] | `meon/JamScreen.cpp:214`, `meon/MeonSession.cpp:539` |
| 볼륨 슬라이더 | 파일 소리 크기 (보내는 소리와 내 모니터 **둘 다**) | -30 ~ +10 dB, 0.5 dB 단위. 맨 왼쪽(-30) = 소리 없음(-∞). ±1.25 dB 안은 0 dB 에 붙음. 더블클릭 0 dB. 휠 꺼짐. 값 표시 "+1.5 dB" / "-∞ dB" | 동작함 [코드 확인] | [SonoBus 수정] | `meon/JamScreen.cpp:228-242`, `388-393`, `meon/MeonSession.cpp:852-862` |
| 재생 / 정지 | 재생·정지 | 시작은 0 에서 80 ms 뒤에 키움(튐 방지). 정지는 위치 유지. 끝나면 엔진이 처음으로 되돌리고 멈춤 | 동작함 [코드 확인] | [SonoBus 수정] | `meon/MeonSession.cpp:811-830`, `39-47`, `Source/SonobusPluginProcessor.cpp:5256-5260` |
| 현재 시간 · 위치 바 · 전체 길이 | 위치 이동 | 끄는 동안은 시간만 바뀌고 **손을 뗄 때 한 번 이동**. 재생 중 이동은 35 ms 줄임 → 이동 → 80 ms 대기 → 50 ms 키움. 30 Hz 갱신. 시간 형식 `m:ss` | 동작함 [코드 확인] | [MEON 추가] | `meon/JamScreen.cpp:243-246`, `287-309`, `meon/MeonSession.cpp:13-113` |

**멤버 카드 (최대 4장, 2×2)**

| 이름 | 하는 일 | 값·범위·기본값·특이 동작 | 상태 | 출처 | 근거 |
|---|---|---|---|---|---|
| 파트 아이콘 · 닉네임 · 파트 이름 | 상대 표시 | 파트는 상대 사용자 이름 끝 글자(`닉네임#XXXX` + `v/g/b/d/k/o`)에서 읽음. 옛 버전 사용자는 파트 없음 | 동작함 [코드 확인] | [MEON 추가] | `meon/MeonSession.cpp:223-252`, `meon/JamScreen.cpp:629-651` |
| 핑 "N ms" | 상대와의 왕복 시간 | 5장 3-6. 값 없으면 "— ms" | 동작함 [코드 확인] | [SonoBus 원본] 값 / [MEON 추가] 표시 | `meon/JamScreen.cpp:653-659` |
| 카드 노란 테두리 + "지연이 조금 있어요" | 핑 18 ms 초과 경고 | 글자는 노랑을 쓰지 않음 | 동작함 [코드 확인] | [MEON 추가] | `meon/JamScreen.cpp:616-621`, `678`, `meon/MeonTheme.h:43` |
| 레벨 막대 | 그 멤버 소리 (내가 정한 볼륨이 적용된 뒤, 패닝 전) | 끊김·뮤트면 비움 | 동작함 [코드 확인] | [SonoBus 원본] 측정 / [MEON 추가] 표시 | `meon/MeonSession.cpp:667-678`, `Source/SonobusPluginProcessor.cpp:7902-7934` |
| 볼륨 슬라이더 | 내가 듣는 그 멤버 크기 (내 믹스만) | 게인 0~2 (-∞ ~ +6 dB), skew 0.69 (0 dB = 약 62% 위치). 기본 1.0 (0 dB). 더블클릭 0 dB. 스냅 없음. 휠 꺼짐. 끊긴 멤버는 비활성 | 동작함 [코드 확인] | [SonoBus 수정] | `meon/MeonWidgets.cpp:332-351`, `meon/JamScreen.cpp:477-486`, `Source/SonobusPluginProcessor.cpp:6015` |
| 값 표시 | "+1.5 dB" / "-60 dB"(0 이하) / 끊김 "—" | -60 ~ +6 dB 로 자름 | 동작함 [코드 확인] | [MEON 추가] | `meon/MeonWidgets.cpp:344-351` |
| 0 dB로 | 볼륨을 0 dB 로 | 이미 0 dB 이거나 끊긴 멤버면 비활성 | 동작함 [코드 확인] | [MEON 추가] | `meon/JamScreen.cpp:487-499`, `528-532` |
| 뮤트 / 소리 켜기 (1–4 배지) | 그 멤버 소리를 내가 안 받기 | 엔진이 상대 소스를 **초대 취소**(uninvite) → 상대가 나에게 보내는 것을 멈춤. 상대 화면에는 표시 없음. 끊긴 멤버는 조작 불가 | 동작함 [코드 확인] | [SonoBus 수정] | `meon/JamScreen.cpp:503-507`, `meon/MeonSession.cpp:604-614`, `Source/SonobusPluginProcessor.cpp:5385-5409` |
| 패닝 노브 (앱은 "패닝" 라벨) | 내가 듣는 그 멤버 좌우 위치 (전송 안 함) | -100~+100 정수, 기본 0. 회전 ±135°. 세로 드래그 1px=1(Shift 0.25배), \|값\|<3 은 0. 더블클릭 0. 휠 한 칸 ±1. 엔진 팬 법칙(가운데 -4.5 dB). 멤버가 나갔다 오면 0. 플러그인 창을 다시 열면 엔진 값을 이어받음 | 동작함 [코드 확인] | [SonoBus 수정] | `meon/MeonWidgets.cpp:354-429`, `meon/MeonSession.cpp:354-374`, `627-652` |
| 상태 문구 | "연결 중…" / "연결 실패 · 네트워크 확인 필요" / "연결 끊김 · 재연결 중" / "지연이 조금 있어요" / "소리 꺼짐" | 10장 참고 (끊김 표시가 거의 안 나오는 문제) | 일부 동작 [코드 확인] | [MEON 추가] | `meon/JamScreen.cpp:673-685` |
| 빈 자리 | 점선 + "빈 자리" | — | [코드 확인] | [MEON 추가] | `meon/JamScreen.cpp:709-729` |
| 아직 혼자예요 패널 | 혼자일 때 카드 대신: 큰 코드 + 코드 복사 + "나 포함 최대 5명"(앱) | — | 동작함 [실행 확인] | [MEON 추가] | `meon/JamScreen.cpp:732-799`, `1581-1588` |

**채팅**

| 이름 | 하는 일 | 값·범위·기본값·특이 동작 | 상태 | 출처 | 근거 |
|---|---|---|---|---|---|
| 채팅 패널 (앱 320 px / 플러그인 240 px) | 메시지 목록 | 내 글 오른쪽 정렬 "나", 상대 왼쪽, 입·퇴장 시스템 문구는 "오후 9:12 · OO님이 입장했습니다" 처럼 시각 포함. 일반 메시지에는 시각을 표시하지 않음. 이모지 표시. 새 글이 오면 맨 아래로 | 빈 목록 표시 [실행 확인] / 나머지 [코드 확인] | [SonoBus 수정] | `meon/JamScreen.cpp:802-1003` |
| 링크 | `http://`·`https://`·`www.` 를 밑줄 회색으로, 클릭하면 확인 창 | 한글 등은 퍼센트 인코딩해서 브라우저로 | 동작함 [코드 확인] | [MEON 추가] | `meon/JamScreen.cpp:10-77`, `917-933` |
| 메시지 입력 + 전송 | 보내기 | Enter = 전송, Esc = 방 나가기 확인. 길이 제한 없음(10장) | 동작함 [코드 확인] | [SonoBus 수정] | `meon/JamScreen.cpp:807-824`, `987-995`, `meon/MeonSession.cpp:877-896` |
| 접기 | 채팅 닫기 (C 키와 같음) | 열림/닫힘은 설정 파일 `chatOpen` 에 저장 (기본 열림). 플러그인 폭 800 미만에서는 자동 접힘, 직접 연 것은 저장하지 않음 | 동작함 [실행 확인: C 키로 접힘, 다시 켰을 때 접힌 채 유지] | [MEON 추가] | `meon/JamScreen.cpp:1269-1303`, `meon/MeonSettings.cpp:95-103` |
| 접힌 채팅 한 줄 | 최근 대화 한 줄 + "방금 / N분 전 · 눌러서 채팅 열기" | 누르면 열림. 15초마다 시간 갱신 | 동작함 [실행 확인: "아직 채팅이 없어요"] | [MEON 추가] | `meon/JamScreen.cpp:1007-1078` |

### S9. 설정

설정은 **탭이 없고 한 장을 세로로 스크롤**한다 (`meon/SettingsScreen.cpp:323-467`). 본문 폭은 앱 720 px, 플러그인 560 px 고정이다.

| 구역 | 이름 | 하는 일 | 값·범위·기본값·특이 동작 | 상태 | 출처 | 근거 |
|---|---|---|---|---|---|---|
| 헤더 | 닫기 | 설정 닫기 + 모든 설정 저장 | Esc, ⌘,/Ctrl+, 도 닫음 | 동작함 [실행 확인: Esc] | [MEON 추가] | `meon/SettingsScreen.cpp:558-559`, `635-643`, `meon/MeonEditor.cpp:194-208` |
| 오디오 (앱) | 드라이버 (Windows) | S2 와 같음 | — | [코드 확인] | [SonoBus 수정] | `meon/SettingsScreen.cpp:73-81`, `185-197` |
| 오디오 (앱) | 입력 / 출력 | 장치 바꾸기, **합주 중에도 즉시 적용**(스트림 재시작) | 없으면 "장치를 찾을 수 없음" | 표시 [실행 확인] / 바꾸기 [코드 확인] | [SonoBus 수정] | `meon/SettingsScreen.cpp:199-212` |
| 오디오 (앱) | 버퍼 크기 | 드롭다운 4개 ("128 샘플 · 2.7 ms"). 목록 밖 값은 글자로만 표시. 장치가 없거나 선택지가 1개면 비활성 | 합주 중 즉시 적용 | 비활성 표시 [실행 확인] / 바꾸기 [코드 확인] | [MEON 추가] | `meon/SettingsScreen.cpp:151-164`, `214-226` |
| 오디오 (앱) | 드라이버 설정 | 장치에 자체 설정 창이 있으면(주로 ASIO) 버퍼 옆에 버튼, 누르면 장치를 다시 엶 | — | [코드 확인] | [MEON 추가] | `meon/SettingsScreen.cpp:88-93`, `175-181`, `meon/MeonAudio.cpp:128-145` |
| 오디오 (앱) | 입력 레벨 | 장치 입력 레벨 막대 + 정수 dB | — | 표시 [실행 확인] | [MEON 추가] | `meon/SettingsScreen.cpp:384-387`, `611-621` |
| 오디오 (앱·플러그인) | 귀 테러 방지 | 끔 · 보통 · 강하게 탭 + 설명 한 줄 | 기본 보통. 누르는 즉시 저장·적용 | 표시 [실행 확인] / 효과 [코드 확인] | [MEON 추가] | `meon/SettingsScreen.cpp:58-64`, `228-237` |
| 오디오 (플러그인) | DAW 정보 칸 | "장치와 버퍼는 DAW 설정을 따릅니다" + `호스트 · 48 kHz · N 샘플 (x.x ms)` | 블록 크기가 바뀌면 다시 그림 | [코드 확인] | [MEON 추가] | `meon/SettingsScreen.cpp:415-422`, `512-531`, `590-601` |
| 닉네임 | 입력 + 저장 | 저장 → 방 밖이면 서버에 다시 연결(새 이름), 합주 중이면 다음 입장부터. 규칙에 안 맞으면 저장 없이 원래 이름으로 되돌림. 저장 후 1.5초 "저장됨". Enter = 저장 | — | 표시 [실행 확인] / 저장 [코드 확인] | [MEON 추가] | `meon/SettingsScreen.cpp:44-49`, `308-321` |
| 파트 | 6칸 | 누르는 즉시 저장, 방 밖이면 서버 재연결. 앱은 "닉네임과 파트는 합주 중에 바꾸면 다음 입장부터 적용돼요" | — | 표시 [실행 확인] / 저장 [코드 확인] | [MEON 추가] | `meon/SettingsScreen.cpp:50-56`, `401-412` |
| 문제 해결 | 로그 폴더 열기 | 로그 폴더를 만들고 OS 파일 관리자로 열기. 경로를 글자로 표시 | — | 표시 [실행 확인] / 열기 [코드 확인] | [MEON 추가] | `meon/SettingsScreen.cpp:65-71`, `424-431` |
| 업데이트 (macOS·Windows 앱, CI 빌드) | 상태 2줄 + 버튼 | 8장 참고 | — | [코드 확인] | [MEON 추가] | `meon/SettingsScreen.cpp:102-118`, `239-306`, `434-443` |
| 앱 정보 | 버전 / 기반 / 라이선스 | "Meon 1.0.0 (build N)" (플러그인 "(AU / VST3)"), "SonoBus 기반 수정판", "GPLv3 — … 라이선스 전문 보기 · 소스 코드" | 링크: gnu.org GPLv3, `github.com/meonhq/desktop`(앱만) | 동작함 [실행 확인: 표시, 로컬 빌드는 build 1] | [MEON 추가] | `meon/SettingsScreen.cpp:39-40`, `445-465`, `533-551` |

---

## 3. 오디오·네트워크

### 3-1. 연결 방식

| 항목 | 현재 | 확인 | 근거 |
|---|---|---|---|
| 프로토콜 | SonoBus 엔진의 **AOO(Audio over OSC)** 그대로. 서버는 멤버 찾기·그룹 관리만 하고, **소리는 멤버끼리 UDP P2P** 로 직접 주고받는다 [SonoBus 원본] | [코드 확인] | `deps/aoo/lib/src/client.cpp:604` (서버 TCP), `1369` (피어 UDP) |
| 서버 주소 | `aoo.sonobus.net:10998` (**SonoBus 공개 서버**). `#define` 으로 **하드코딩**, 설정·UI 로 바꿀 수 없음 | [코드 확인] (이 컨테이너에서는 DNS 조회 실패로 접속 불가) | `Source/SonobusPluginProcessor.h:35-36`, `meon/MeonSession.cpp:414` |
| 서버 통신 | TCP 로 OSC 메시지 (로그인·그룹 입장/퇴장·서버 핑). NAT 통과용 UDP | [코드 확인] | `deps/aoo/lib/src/client.cpp:12-60`, `905` |
| 릴레이 | **없음**. aoo 에 릴레이 코드가 없다. NAT 통과에 실패하면 그 멤버와는 소리가 오가지 않고 카드에 "연결 실패 · 네트워크 확인 필요" | [코드 확인] | `deps/aoo` 에 relay 검색 결과 없음, `meon/MeonSession.cpp:1089-1097`, `meon/JamScreen.cpp:675` |
| 방 = 그룹 | 초대 코드 6자를 **그대로 SonoBus 그룹 이름**으로 쓴다. 비밀번호 없음(`""`), 비공개 그룹(`isPublic=false`). 공개 그룹 감시 끔 | [코드 확인] | `meon/MeonSession.cpp:489`, `319` |
| 사용자 이름 | `닉네임#XXXX` + 파트 한 글자. XXXX 는 실행할 때마다 새로 뽑음 (서버 전체 이름 중복 회피) | [코드 확인] | `meon/MeonSession.cpp:241-252`, `406-420` |
| 서버 연결 관리 | 닉네임이 있으면 켤 때 바로 연결. 12초 안에 안 되면 끊고 3초 뒤 재시도(거부 응답이면 0.5초). 방 안에서 끊기면 엔진 자동 재접속에 맡김 | [코드 확인] | `meon/MeonSession.cpp:377-420`, `981-1014`, `1161-1180`, `316-318` |
| 입장 판정 | 만들기: 그룹 입장 성공 = 입장. 코드 입장: 그룹 입장 후 **4초 안에 멤버가 한 명이라도 보이면 입장**, 없으면 "그런 방이 없어요"(서버가 방 존재 여부를 알려주지 않아서 빈 그룹에 들어갔다 나옴). 5명 이상 보이면 "꽉 참" | [코드 확인] | `meon/MeonSession.cpp:1016-1040`, `1099-1111`, `1182-1184` |
| 최대 인원 | 나 포함 5명. 6번째는 입장 직후(3초 안) 스스로 나감 | [코드 확인] | `meon/MeonTheme.h:44-45`, `meon/MeonSession.cpp:1106-1111` |
| 끊긴 멤버 정리 | 연결 실패 표시된 멤버는 30초 뒤 카드에서 지움. 정상 퇴장은 서버의 "peer left" 로 바로 지움 | [코드 확인] | `meon/MeonSession.cpp:1116-1130`, `1247-1253` |

### 3-2. 코덱·비트레이트

| 항목 | 현재 | 바꿀 수 있나 | 확인 | 근거 |
|---|---|---|---|---|
| 코덱 | **무압축 PCM 16 bit 고정**. 새로 들어온 멤버에게도 강제 | UI 없음 | [코드 확인] | `meon/MeonSession.cpp:267-282`, `345-346` |
| 비트레이트 | PCM 이라 고정: 샘플레이트 × 16 bit × 송신 채널 수 (48 kHz 모노 ≈ 768 kbps/멤버, 패킷 오버헤드 제외) | UI 없음 | [코드 확인] (계산값) | 위와 같음 |
| 송신 채널 | 앱: 입력 채널 수에 맞춰 1(모노) 또는 2(스테레오). 플러그인: 엔진 기본 "입력 수에 맞춤" | 앱은 첫 실행 S4 에서만 바꿀 수 있음 | [코드 확인] | `meon/MeonSession.cpp:328-341`, `Source/SonobusPluginProcessor.cpp:633`, `781-786` |

### 3-3. 버퍼·지연 설정

| 항목 | 현재 | 바꿀 수 있는 때 | 확인 | 근거 |
|---|---|---|---|---|
| 지터 버퍼 | 엔진 자동 모드 **Auto-Full**(늘리고 줄이기 모두 자동) 고정. 모든 멤버에 적용 | UI 없음 | [코드 확인] | `meon/MeonSession.cpp:284-285`, `347` |
| 오디오 장치 버퍼 (앱) | 64/128/256/512 근사값 4개 중 선택 | 첫 실행 S2, 설정. **합주 중에도 즉시 적용** (장치 재시작 → 순간 끊김 가능) | [코드 확인] | `meon/MeonAudio.cpp:121-126`, `meon/SettingsScreen.cpp:206-226` |
| 오디오 버퍼 (플러그인) | DAW 가 정함. 화면에 표시만 | DAW 에서 | [코드 확인] | `meon/SettingsScreen.cpp:512-531` |
| 플러그인 지연 보고 | 귀 테러 방지는 지연 0, `setLatencySamples` 변경 없음 | — | [코드 확인] | `meon/MeonEarGuard.h:1-7` |

### 3-4. 미터

| 항목 | 현재 | 확인 | 근거 |
|---|---|---|---|
| 종류 | 가로 막대 하나(`LevelBar`). 내 영역 = 송신 신호, 멤버 카드 = 받은 소리(내 볼륨 적용 후), S4·설정 = 장치 입력 | [코드 확인] | `meon/MeonWidgets.h:87-111`, `meon/MeonSession.cpp:864-874` |
| 눈금 | -60 ~ 0 dB. -60/-40/-20/-12/-6/0 을 같은 간격에 놓은 구간 선형 | [실행 확인] S4 눈금 | `meon/MeonWidgets.cpp:253-267` |
| 갱신 | 화면 주사율(VBlank)마다. 오를 땐 즉시, 내릴 땐 초당 30 dB. 엔진 측정값의 피크 유지 25 ms | [코드 확인] | `meon/MeonWidgets.cpp:269-292`, `meon/MeonSession.cpp:8`, `meon/JamScreen.cpp:1513-1522` |
| 색 구간 | **없음**. 회색 트랙(#ECECEC) 위 노랑(#FFD700) 한 색. 끊긴 멤버는 회색 | [실행 확인] 색 / [코드 확인] 구간 없음 | `meon/MeonWidgets.cpp:308-319` |
| 피크 홀드 | 막대에는 피크 홀드 표시 없음. 숫자로만: 내 영역 "최고 N dB"(앱)·S4 값을 1.5초 유지 | [코드 확인] | `meon/JamScreen.cpp:115-125`, `meon/OnboardingScreens.cpp:628-632` |
| 클리핑 표시 | -0.1 dBFS 이상이면 막대 오른쪽 끝 6% 를 검정으로 1.5초 | [코드 확인] | `meon/MeonWidgets.cpp:283-286`, `321-328` |

### 3-5. 셀프 모니터·마스터 볼륨

| 항목 | 현재 | 확인 | 근거 |
|---|---|---|---|
| 셀프 모니터 (내 소리를 앱에서 듣기) | **UI 없음**. 엔진 기본값 그대로: 독립 앱 `Dry Level` = 0 (안 들림), 플러그인 = 1.0 (입력이 그대로 출력으로 지나감). 파일 재생 소리는 내 출력에서 들림 | [코드 확인] | `Source/SonobusPluginProcessor.cpp:781-789`, `8089-8104` |
| 마스터 볼륨 | **UI 없음**. 엔진 `Output Level`(wet) 기본 1.0 (0 dB). 플러그인에서는 호스트 자동화 파라미터로 노출됨 | [코드 확인] | `Source/SonobusPluginProcessor.cpp:626`, `8259-8265` |
| 내 마이크 뮤트 | 있음 (합주 화면, M). 2-S8 참고 | — | — |
| 멤버별 볼륨·뮤트·패닝 | 있음 (2-S8) | — | — |

### 3-6. 귀 테러 방지

| 항목 | 현재 | 확인 | 근거 |
|---|---|---|---|
| 있는지 | 있음. 설정 > 오디오 > 귀 테러 방지 (앱·플러그인 모두) [MEON 추가] | [실행 확인] 설정 화면 표시 | `meon/SettingsScreen.cpp:346-360` |
| 처리 위치 | **내 모니터 최종 출력**: 멤버 믹스·파일 재생·마스터 볼륨까지 적용한 뒤, 출력 미터 측정 직전, 메인 버스 채널만. 내가 보내는 소리·녹음에는 적용 안 함 | [코드 확인] | `Source/SonobusPluginProcessor.cpp:8270-8274` |
| 처리 방법 | ① 2–5 kHz 대역 리미터: 중심 3.16 kHz·Q 1.05 대역 통과 필터 출력의 RMS 가 기준을 넘으면 그 대역만 줄임(`y = x − (1−g)·BP(x)`). ② 전체 리미터: K-가중(BS.1770) 40 ms 평균 레벨이 상한을 넘으면 넘은 만큼 줄이고, 샘플 피크가 상한을 넘으면 즉시 누름. 미리보기 없음 → **지연 0**. 모든 채널 같은 게인. 기준 아래면 출력이 입력과 비트 단위로 같음 | [코드 확인] | `meon/MeonEarGuard.cpp:147-249` |
| 단계 | 끔 / 보통(기본) / 강하게. 단계 전환은 100 ms 동안 기준값을 옮겨 가며 | [코드 확인] | `meon/MeonEarGuard.cpp:11-24`, `125-144` |
| 단계별 값 | 보통: 대역 기준 -18 dBFS, 비율 4:1, 최대 감쇠 12 dB, K-가중 상한 -10 LKFS, 피크 상한 -1 dBFS. 강하게: -26 dBFS, 8:1, 18 dB, -16 LKFS, -4 dBFS | [코드 확인] | `meon/MeonEarGuard.cpp:11-13` |
| 저장·적용 | 설정 파일 `earGuard`(0/1/2). 플러그인은 창을 열지 않아도 프로세서 생성 때 설정 파일에서 읽음. 같은 PC 의 앱·플러그인이 같은 값을 공유 | [코드 확인] | `Source/SonobusPluginProcessor.cpp:851`, `meon/MeonSettings.cpp:84-93` |
| 로그 | 방 입장·단계 변경 이벤트, 5초 샘플마다 단계와 대역·전체 리미터 동작 횟수 | [코드 확인] | `meon/MeonSession.cpp:700-703`, `1320-1323` |

### 3-7. 핑·지연 측정

| 항목 | 현재 | 확인 | 근거 |
|---|---|---|---|
| 멤버 핑 (카드의 "N ms") | 엔진이 각 멤버와 **2초마다** 핑을 주고받아 왕복 시간(RTT)을 잼. 표시값은 엔진의 평활 평균(`smoothPingTime.xbar`). **편도가 아니라 왕복**이고, 오디오 버퍼·지터 버퍼 지연은 포함하지 않음. 화면 갱신은 0.4초마다 [SonoBus 원본] | [코드 확인] | `Source/SonobusPluginProcessor.cpp:43`, `3430-3450`, `5673`, `meon/MeonSession.cpp:1189-1190`, `1261-1283` |
| 총 지연 추정 | 엔진의 `totalRoundtripMs`(코덱·상대 입출력·지터 버퍼 포함 추정)를 받아 **로그에만** 기록. 화면에는 없음 | [코드 확인] | `Source/SonobusPluginProcessor.cpp:5675-5690`, `meon/MeonSession.cpp:1272`, `1299` |
| 서버 핑 | 서버에 **TCP 접속하는 데 걸린 시간**으로 어림. 홈 5초 / 방 10초 / 그 밖 15초마다. **화면에는 표시하지 않고 로그(`serverPingMs`)에만** | [코드 확인] | `meon/MeonSession.cpp:116-149`, `meon/MeonEditor.cpp:139-140`, `meon/LobbyScreens.cpp:171`, `meon/JamScreen.cpp:1257` |
| 지연 측정 테스트 (SonoBus 의 루프백 측정) | UI 삭제, 엔진의 "Hear Latency Test" 는 끔 | [코드 확인] | `meon/MeonSession.cpp:313` |

---

## 4. 파일 플레이어

| 항목 | 현재 | 확인 | 근거 |
|---|---|---|---|
| 위치 | 합주 화면 내 영역 바로 아래. 방 밖에서는 없음. 독립 앱·플러그인 모두 [SonoBus 수정] | [실행 확인] | `meon/JamScreen.cpp:1246-1247`, `1578` |
| 지원 형식 | JUCE `registerBasicFormats()` 그대로: WAV, AIFF, FLAC, Ogg Vorbis 공통 + macOS CoreAudio(MP3·M4A/AAC·CAF 등 OS 가 읽는 것) + Windows Media(MP3·WMA·ASF·WMV) + Linux MP3. 파일 선택 창도 이 형식만 보여 줌 | [코드 확인] | `Source/SonobusPluginProcessor.cpp:836`, `deps/juce/.../juce_AudioFormatManager.cpp:54-78`, `CMakeLists.txt:236-239`, `424`, `meon/JamScreen.cpp:446-447` |
| 신호 경로 (보내는 쪽) | 파일 → 엔진 transport → `fileBuffer` → (볼륨 게인 적용) → **내 송신 버퍼 `sendWorkBuffer` 에 마이크 신호와 더함** → 같은 AOO 스트림으로 각 멤버에게. 송신이 모노면 파일 채널을 1/채널수로 섞고, 스테레오면 채널 그룹 패닝으로 2채널에 섞는다. **따로 채널·스트림을 쓰지 않는다** | [코드 확인] | `meon/MeonSession.cpp:311` (`paramSendFileAudio`=켬), `Source/SonobusPluginProcessor.cpp:7622-7685` |
| 송신 미터 | 내 영역 막대는 파일 소리가 섞인 뒤의 송신 신호를 보여 준다 | [코드 확인] | `Source/SonobusPluginProcessor.cpp:7790-7791` |
| 내가 듣는 소리 | 같은 `fileBuffer` 를 같은 볼륨으로 내 출력에 더함 (마스터 볼륨·귀 테러 방지 앞) | [코드 확인] | `Source/SonobusPluginProcessor.cpp:8234-8243` |
| 마이크를 끄면 | 송신 전체가 멈추므로 파일 소리도 안 나감. 나는 계속 들음 | [코드 확인] | `Source/SonobusPluginProcessor.cpp:6732-6760` |
| 다른 사람 쪽에서 파일 소리만 조절 | **불가능**. 받는 쪽에는 내 마이크와 파일이 한 소리로 도착하고, 받는 쪽 화면에 재생 중이라는 표시도 없다 | [코드 확인] | 위 신호 경로 |
| 방을 나가면 | 파일을 닫는다 | [코드 확인] | `meon/MeonSession.cpp:539` |
| 플러그인 창을 닫으면 | 재생은 계속(엔진 상태). 다시 열면 이어서 표시 | [코드 확인] | `meon/MeonSession.h:129-130`, `meon/MeonSession.cpp:182-186` |
| 로그 | 열기·닫기·재생·정지 이벤트, 샘플마다 `fileLoaded`·`filePlaying`·`fileGainDb` | [코드 확인] | `meon/MeonSession.cpp:770-771`, `827-828`, `1316-1319` |

---

## 5. 계정·방·친구·초대·차단·알림

| 기능 | 구현 여부 | 서버 통신 | 확인 | 근거 |
|---|---|---|---|---|
| 계정 / 로그인 | **없음**. 로컬 닉네임·파트만. 첫 화면 문구 "계정이나 로그인은 필요 없어요" | 서버에는 `닉네임#XXXX(파트)` 로 로그인(비밀번호 없음) | [실행 확인] 문구 / [코드 확인] | `meon/OnboardingScreens.cpp:78`, `meon/MeonSession.cpp:414` |
| 방 | 있음. 만들기·코드 입장·나가기, 최대 5명 | **실제 서버**(SonoBus 공개 서버 AOO 그룹). 가짜 데이터 없음 | [코드 확인] (서버 접속 불가로 실행 못 함) | `meon/MeonSession.cpp:447-553` |
| 방 목록 / 공개 방 | **없음** (공개 그룹 감시 끔) | — | [코드 확인] | `meon/MeonSession.cpp:319` |
| 친구 | **없음** | — | [코드 확인] (관련 코드 없음) | `Source/meon` 전체 |
| 초대 | 6자리 코드를 **클립보드로 복사**하는 것뿐. 초대 링크·URL 스킴·알림 없음 (SonoBus 의 `sonobus://` 연결은 제거) | — | [코드 확인] | `meon/MeonEditor.cpp:252-257`, `Source/SonoStandaloneFilterApp.cpp:774-784` |
| 차단 / 강퇴 | **없음**. 멤버 뮤트는 내 쪽 수신만 끊음(상대에게 표시 없음) | — | [코드 확인] | `meon/MeonSession.cpp:604-614` |
| 알림 | OS 알림 **없음**. 새 채팅 수(`unread`)는 세지만 **어디에도 표시하지 않음**. 입·퇴장은 채팅 안 시스템 문구로만 | — | [코드 확인] | `meon/MeonSession.cpp:1144`, `meon/MeonSession.h:148` (호출하는 곳 없음) |
| 채팅 | 있음. 서버를 거치지 않고 각 멤버에게 P2P OSC 로 직접 보냄. 기록 저장 없음(방을 나가면 지움) | P2P | [코드 확인] | `meon/MeonSession.cpp:877-896`, `550`, `Source/SonobusPluginProcessor.cpp:2866-2900` |
| 서버 통신 분리(인터페이스) | **분리돼 있지 않다**. `MeonSession` 이 엔진 클래스 `SonobusAudioProcessor` 를 직접 부르고(`connectToServer`, `joinServerGroup`, `sendChatEvent` …), 엔진 콜백 `SonobusAudioProcessor::ClientListener` 를 직접 구현한다. 교체용 인터페이스·목(mock)·API 클라이언트 계층 없음 | — | [코드 확인] | `meon/MeonSession.h:18-21`, `209-220` |

**사용하는 주소·메시지 (전부)**

| 대상 | 주소 / 형식 | 용도 | 근거 |
|---|---|---|---|
| MEON 서버 | `aoo.sonobus.net:10998` TCP (+UDP) | 로그인, 그룹 입장·퇴장, 서버 핑 | `Source/SonobusPluginProcessor.h:35-36` |
| 서버 OSC 주소 | `/aoo/server/login`, `/aoo/server/request`, `/aoo/server/ping`, `/aoo/server/group/join`, `/aoo/server/group/leave`, `/aoo/server/group/public`(+`/add`·`/del`, MEON 은 쓰지 않음) / 서버→클라이언트 `/aoo/client/group/join`, `/group/leave`, `/peer/join`, `/peer/leave` 등 | AOO net 프로토콜 [SonoBus 원본] | `deps/aoo/lib/aoo/aoo_net.h:41-78`, `deps/aoo/lib/src/client.cpp:12-60` |
| 피어 OSC (UDP) | 오디오 `/aoo/sink/…`·`/aoo/src/…` (`/format`, `/data`, `/d`, `/ping`, `/invite`, `/uninvite`, `/codecchange`), 피어 핑 `/aoo/peer/ping` | 소리·초대·핑 [SonoBus 원본] | `deps/aoo/lib/aoo/aoo.h:109-128` |
| SonoBus 피어 메시지 | `/sb/chat` (`s:group s:from s:targets s:tags s:message`), `/sb/pinfo`, `/sb/clayinfo`, `/sb/ping`, `/sb/pngack`, `/sb/reqlatinfo`, `/sb/latinfo`, `/sb/suggestlat`, `/sb/blockedinfo` | 채팅·피어 정보·지연 정보 [SonoBus 원본] | `Source/SonobusPluginProcessor.cpp:2422-2460`, `2866-2880` |
| 업데이트 | `https://github.com/meonhq/desktop/releases/download/mac-latest/build.txt` (Windows 는 `windows-latest/`), `…/MEON-mac.zip`, `…/MEON-Installer.exe` | 8장 | `meon/MeonUpdater.cpp:19-34` |
| 링크 (브라우저로 열기) | `https://discord.gg/vtGQpnc2HQ`, `https://www.gnu.org/licenses/gpl-3.0.html`, `https://github.com/meonhq/desktop`, `https://github.com/meonhq/desktop/blob/main/doc/INSTALL.md#windows-asio` | 외부 링크 | `meon/LobbyScreens.cpp:117`, `meon/SettingsScreen.cpp:39-40`, `meon/OnboardingScreens.cpp:164` |

MEON 전용 백엔드·REST API·계정 서버는 없다.

---

## 6. 설정

### 6-1. 화면 구성
탭 없음. 한 장 스크롤: **오디오 → 닉네임 → 파트 → (플러그인) 오디오 → 문제 해결 → (앱) 업데이트 → 앱 정보**. 각 항목은 2장 S9 표 참고. [실행 확인] Linux 앱 (`meon/SettingsScreen.cpp:323-467`)

### 6-2. 저장 위치와 유지 여부

| 저장 대상 | 파일 / 형식 | 키 | 다시 켜면 | 확인 | 근거 |
|---|---|---|---|---|---|
| MEON 사용자 설정 (앱·플러그인 공유) | macOS `~/Library/Application Support/MEON/MEONUser.settings`, Windows `%APPDATA%\MEON\MEONUser.settings`, Linux `~/.config/meon/MEONUser.settings`. JUCE PropertiesFile **XML** | `nickname`, `part`, `onboardingDoneApp`, `onboardingDonePlugin`, `inputChannelStart`, `inputChannelCount`, `earGuard`, `chatOpen` | 유지됨 | [실행 확인] Linux: 파일 생성·`nickname`·`onboardingDoneApp`·`chatOpen` 기록과 재실행 후 채팅 접힘 유지 / 나머지 OS 경로 [코드 확인] | `meon/MeonSettings.cpp:6-22`, 각 getter/setter |
| 독립 앱 오디오 장치·엔진 상태 | 같은 폴더의 `Meon.settings` (JUCE 독립 앱 기본 저장). 장치 설정 XML + 엔진 상태 | 장치 이름·버퍼·샘플레이트, 엔진 파라미터 | 장치·버퍼 유지. **단, MEON 은 켤 때마다 엔진 기본값(뮤트 해제, 코덱, 이펙트 끔 등)을 다시 덮어씀** | Linux 에서 `~/.config/meon/Meon.settings` 생성 [실행 확인] / 내용 [코드 확인] | `Source/SonoStandaloneFilterApp.cpp:160-170`, `meon/MeonSession.cpp:155`, `265-326` |
| 플러그인 상태 | DAW 프로젝트 안 (엔진 `getStateInformation`). 마지막 창 크기 포함 | — | DAW 프로젝트를 열면 복원. MEON 기본값 덮어쓰기는 같음 | [코드 확인] | `meon/MeonEditor.cpp:69-72`, `400` |
| 세션 로그 | macOS `~/Library/Logs/MEON/`, Windows `%APPDATA%\MEON\logs\`, Linux `~/.config/meon/logs`. 방마다 `meon-session-YYYYMMDD-HHMMSS.json` | 7-2 참고 | 파일로 남음 | [실행 확인] 설정에 경로 표시 / 기록 [코드 확인] | `meon/MeonSettings.cpp:115-124`, `meon/MeonSessionLog.cpp:27-110` |
| 업데이트 기록 (macOS) | `~/Library/Logs/MEON/update.log` | — | 남음 | [코드 확인] | `meon/MeonUpdater.cpp:104-114` |
| 첫 실행 완료 | `onboardingDoneApp` / `onboardingDonePlugin` 따로 | — | 유지 | [실행 확인] 앱 | `meon/MeonSettings.cpp:56-65` |
| 저장 시점 | 대부분 바꾸는 즉시 저장. `chatOpen` 은 0.5초 뒤 자동 저장 + 설정 닫을 때·종료 때 | — | — | [실행 확인] `chatOpen` | `meon/MeonSettings.cpp:20`, `100-103`, `meon/MeonEditor.cpp:245-250` |

**세션 로그 내용** [코드 확인] (`meon/MeonSessionLog.cpp:36-58`, `meon/MeonSession.cpp:1285-1326`)
- 머리: 앱·버전·기반·모드(standalone/plugin)·OS·CPU·컴퓨터 이름·방 코드·사용자 이름·시작/끝 시각·끝난 이유·오디오 장치(또는 호스트)
- 이벤트: roomCreated/roomJoined/editorReopened, peerPending/peerJoined/peerJoinFailed_NAT/peerLeft/peerReconnected/peerRemovedAfterTimeout, serverLost/serverReconnected, selfMute/selfUnmute, fileLoaded/fileClosed/filePlay/fileStop, earGuard, error, leave
- 5초 샘플: 서버 연결·서버 핑, 내 뮤트, 파일 상태, 귀 테러 방지, 멤버별 핑·왕복 지연·지터 버퍼·패킷 수신/손실/재전송/송신·보낸 바이트·송신 채널·게인·내가 뮤트했는지
- 서버로 보내지 않는다. 사용자가 폴더를 열어 직접 보내야 한다.

---

## 7. 단축키

키 처리는 에디터 한 곳에서 한다 (`meon/MeonEditor.cpp:425-449`). macOS 는 ⌘, Windows·Linux 는 Ctrl. **텍스트 입력에 포커스가 있으면 글자 단축키는 듣지 않는다**(⌘/Ctrl 조합은 들음). 입력 밖을 클릭하면 포커스가 빠져 다시 듣는다 (`meon/MeonEditor.cpp:403-423`).

| 키 | 동작 | 동작하는 화면 | 확인 | 근거 |
|---|---|---|---|---|
| ⌘, / Ctrl+, | 설정 열기 / 닫기 (토글). 텍스트 입력 중에도 동작 | 모든 화면 | [실행 확인] 홈·합주에서 열림 | `meon/MeonEditor.cpp:427-433` |
| Esc | 설정 닫기 | 설정 | [실행 확인] | `meon/SettingsScreen.cpp:635-643` |
| Esc | 나가기 확인 창 열기 / 떠 있으면 머무르기 | 합주 | [실행 확인] 열기 | `meon/JamScreen.cpp:1434-1439` |
| Esc | 나가기 확인 창 열기 | 합주 채팅 입력 중 | [코드 확인] | `meon/JamScreen.cpp:817` |
| Esc / Enter | 링크 확인 창 취소 / 열기 (창이 떠 있는 동안 다른 키는 막음) | 합주 | [코드 확인] | `meon/JamScreen.cpp:1428-1433` |
| M | 내 마이크 켜기/끄기 | 합주 (나가기 창이 없을 때) | [실행 확인] | `meon/JamScreen.cpp:1453-1458` |
| C | 채팅 열기/접기 | 합주 | [실행 확인] | `meon/JamScreen.cpp:1459-1463` |
| 1–4 | 그 자리 멤버 뮤트/소리 켜기 (연결된 멤버만) | 합주 | [코드 확인] (멤버 없음) | `meon/JamScreen.cpp:1464-1471` |
| ⌘C / Ctrl+C | 초대 코드 복사 | 방 만들기, 합주 | [코드 확인] | `meon/LobbyScreens.cpp:327-331`, `meon/JamScreen.cpp:1440-1446` |
| Enter | 입장 (방이 준비됐을 때) | 방 만들기 | [코드 확인] | `meon/LobbyScreens.cpp:332-336` |
| Enter | 코드 제출 | 코드로 입장 | [실행 확인] (서버 미연결이라 아무 일 없음) | `meon/LobbyScreens.cpp:452-460`, `meon/MeonWidgets.cpp:580-584` |
| ⌘V / Ctrl+V | 코드 붙여넣기 | 코드로 입장 | [코드 확인] | `meon/MeonWidgets.cpp:586-600` |
| Backspace / Delete | 코드 한 글자 지우기 | 코드로 입장 | [코드 확인] | `meon/MeonWidgets.cpp:570-578` |
| Enter | 다음 / 닉네임 저장 / 채팅 전송 | S1 / 설정 / 합주 채팅 | [코드 확인] | `meon/OnboardingScreens.cpp:93`, `meon/SettingsScreen.cpp:47`, `meon/JamScreen.cpp:816` |
| Space | **쓰지 않음** (어디에도 지정 없음) | — | [실행 확인] 홈에서 눌러도 변화 없음 | `Source/meon` 전체 검색 |

**나가기 확인 창의 Enter**: 지정이 없다. Esc 로 닫기만 된다 (`meon/JamScreen.cpp:1449-1450`). [코드 확인]

**플러그인 안에서** [코드 확인] — 호스트 실행은 확인 못 함
- 키는 **플러그인 창에 키보드 포커스가 있을 때만** MEON 에 온다 (`EDITOR_WANTS_KEYBOARD_FOCUS TRUE`, `CMakeLists.txt:153`).
- MEON 이 처리하지 않은 키(`false` 반환)는 JUCE 플러그인 래퍼가 호스트로 넘긴다. **Space 는 MEON 이 쓰지 않으므로** 포커스가 버튼 등에 있을 때는 호스트로 간다. 단, **텍스트 입력(채팅·닉네임)에 포커스가 있으면 Space 는 입력창이 먹는다**(공백 입력). 실제 Logic·Ableton 등에서 재생/정지가 되는지는 확인 못 함.
- 합주 화면에서 플러그인 창이 포커스를 가지면 **M, C, 1–4, Esc, ⌘C, ⌘,** 를 MEON 이 먹는다. 호스트의 같은 키(예: Logic 의 M=트랙 뮤트, C=사이클, ⌘,=환경설정)와 겹친다.
- macOS 플러그인에서 Logic 등이 키 반복을 넘겨주지 않는 문제를 위해, 입력창에서 키를 꾹 누르면 MEON 이 반복 입력을 직접 만든다 (`meon/MeonKeyRepeat_mac.mm`).

---

## 8. 업데이트

**자동 업데이트 있음** — macOS·Windows **독립 앱**, CI 빌드(`MEON_BUILD_ID > 0`)만. 플러그인·로컬 빌드·Linux 는 없음 [MEON 추가] (`meon/MeonUpdater.cpp:267-277`)

| 항목 | 현재 | 확인 | 근거 |
|---|---|---|---|
| 확인 시점 | 에디터가 만들어지고 **3초 뒤 한 번**. 그 뒤 주기적 확인은 **없음**. 설정 > 업데이트 "다시 확인"으로 수동 확인 | [코드 확인] | `meon/MeonEditor.cpp:83-88`, `meon/SettingsScreen.cpp:105-114` |
| 확인 방법 | 릴리스의 `build.txt`(숫자)를 받아 내 빌드 번호보다 크면 "새 버전" | [코드 확인] | `meon/MeonUpdater.cpp:352-364` |
| 다운로드 | **버튼을 눌러야** 시작 (홈 알림 "업데이트" 또는 설정 "업데이트"). 합주 중이면 버튼 비활성("합주가 끝나면 업데이트할 수 있어요") | [코드 확인] | `meon/MeonEditor.cpp:226-230`, `meon/SettingsScreen.cpp:295-298` |
| 설치 | 받기가 끝나면 **자동으로 바로 설치하고 앱을 끈다** (방에 있으면 미루고 "다시 켜서 설치" 버튼). Windows: 설치 프로그램을 `/SILENT /SUPPRESSMSGBOXES /NORESTART /UPDATE=1` 로 실행(관리자 권한 확인 창 거침), 설치 프로그램이 앱을 다시 켬. macOS: 받은 zip 을 풀어 `Meon.app` 을 바꿔 넣고 `open -n` 으로 새 앱을 켬 | [코드 확인] | `meon/MeonEditor.cpp:232-243`, `meon/MeonUpdater.cpp:459-510` |
| 받은 파일 검사 | 크기(길이 일치, 1 MB 이상)와 파일 머리(Windows `MZ`, macOS zip `PK`). macOS 는 `codesign --verify --deep` 로 서명 무결성 확인 | [코드 확인] | `meon/MeonUpdater.cpp:420-446` |
| 서명·공증 | macOS: **ad-hoc 서명만, 공증 없음**. 받은 앱의 격리 속성(`com.apple.quarantine`)을 `xattr` 로 지움. Windows: **코드 서명 검증 없음**(설치 파일·앱 모두 서명 없음) | [코드 확인] | `.github/workflows/mac.yml:51-58`, `meon/MeonUpdater.cpp:143`, `CLAUDE.md` 서명 절 |
| 쓸 수 없는 위치 (macOS) | dmg 안에서 실행 등 앱을 바꿔 넣을 수 없으면 "응용 프로그램 폴더에서 켜야 업데이트돼요". App Translocation 이면 원래 경로를 찾아 씀 | [코드 확인] | `meon/MeonUpdater.cpp:41-90`, `366-374` |
| 플러그인 업데이트 (macOS) | 앱을 켤 때 앱 안 `MeonPlugins.zip` 의 AU·VST3 가 사용자 폴더(`~/Library/Audio/Plug-Ins`)에 깔린 것보다 새 빌드면 바꿔 넣음(이미 깔린 것만). AU 캐시 갱신을 위해 `AudioComponentRegistrar` 를 강제 종료 | [코드 확인] | `meon/MeonUpdater.cpp:148-240`, `244-259` |
| Windows 플러그인 | 설치 프로그램이 같이 설치 | [코드 확인] | `release/wininstaller.iss` |

---

## 9. 있는지 없는지

| 항목 | 답 | 내용 | 확인 | 근거 |
|---|---|---|---|---|
| 헤드폰 확인 기능 | **있음 (일부)** | 첫 실행에서 **사용자가 직접 체크**해야 넘어감. 자동 판정은 macOS 독립 앱만, **출력 장치 이름에 `speaker`·`스피커`·`built-in output`·`내장 출력` 이 들어 있는지**로만 판단해 경고. Windows·플러그인은 판정 없음. 합주 중 확인 없음 | [실행 확인] 체크 화면 / [코드 확인] 판정 | `meon/OnboardingScreens.cpp:494-515` |
| 멤버별 솔로 | **없음** | 엔진에는 피어 솔로 기능이 있으나 MEON UI 없음 | [코드 확인] | `Source/SonobusPluginProcessor.cpp:7908` (엔진), `Source/meon` 에 solo 없음 |
| 송출 정지 | **있음** | 합주 화면 "마이크 켜짐/꺼짐"(M). 엔진 `Main Send Mute` 로 모든 멤버에게 송신 중단 | [실행 확인] 버튼 / [코드 확인] 송신 | `meon/MeonSession.cpp:721-728` |
| 송출 레벨 (입력 게인) | **없음 (UI)** | 송신 레벨을 바꾸는 UI 없음. 엔진의 `In Gain` 파라미터(0~4, 기본값 엔진 그대로)는 남아 있어 플러그인에서는 호스트 자동화로만 바뀔 수 있음 | [코드 확인] | `Source/SonobusPluginProcessor.cpp:606` |
| 방 밖 값 → 방 안 이어받기 | **일부** (송출 정지만 해당) | 같은 실행 안에서는 이어받는다. 뮤트는 엔진 파라미터라 방을 나갔다 다른 방에 들어가도 그대로. 그러나 앱을 다시 켜거나 **플러그인 창을 다시 열면** 기본값(켜짐)으로 덮어씀. 방 밖에는 송출 컨트롤이 없음. 송출 레벨은 기능 자체가 없음 | [실행 확인] 앱 재실행 시 풀림 / [코드 확인] 나머지 | `meon/MeonSession.cpp:155`, `314`, `528-553` |
| 버퍼 모드 | **없음 (UI)** | 지터 버퍼는 Auto-Full 고정. 장치 버퍼 크기 선택(64/128/256/512)만 있음 | [코드 확인] | `meon/MeonSession.cpp:285`, `347` |
| 패닝 노브 | **있음** | 멤버 카드마다, 내 믹스에만 | [코드 확인] | `meon/MeonWidgets.cpp:354-429` |
| 접속 끊김 배너 | **없음** | 서버 끊김은 상단 바·홈 하단 글자("서버 재연결 중…")만. 멤버 끊김은 카드 상태 글자만 | [코드 확인] | `meon/MeonSession.cpp:422-430`, `meon/JamScreen.cpp:673-685` |
| 새 버전 배너 | **있음** | 홈 상단 가운데 알림 (macOS·Windows 앱, CI 빌드). 합주 화면에는 없음 | [코드 확인] | `meon/LobbyScreens.cpp:7-107` |
| 입장 실패 화면 (사유별) | **있음 (별도 화면이 아니라 코드 입장 화면 안 문구)** | ① "그런 방이 없어요. 코드를 다시 확인해 주세요"(4초 안에 멤버 없음) ② "이 방은 5명이 모두 찼어요" ③ "연결에 문제가 있어요. 잠시 후 다시 시도해 주세요"(서버 오류·끊김·시간 초과). 형식이 틀린 코드는 버튼이 비활성이라 실패 문구 없음. 방 만들기 실패는 S6 안 문구 하나 | [코드 확인] | `meon/LobbyScreens.cpp:471-480`, `543-568`, `312-317` |
| 최소 창 크기 | **있음** | 독립 앱 1024×680 (기본 1280×800, 최대 10000), 플러그인 760×520 (기본 900×600, 크기 조절 가능, 최대 4000). 앱 창에도 같은 제한 적용 | [코드 확인]. 개발용 `--window=…,800,500` 으로 지정하면 800×500 으로 그려짐 [실행 확인] → 제한은 사용자가 끌어서 줄일 때만 걸리는 것으로 보이나 창 관리자가 없어 끌기는 확인 못 함 | `meon/MeonEditor.cpp:65-75`, `Source/SonoStandaloneFilterWindow.h:1066-1074` |
| Pretendard 폰트 내장 | **있음** | Regular·Medium·SemiBold·Bold 4종을 바이너리에 넣음. macOS·Linux `.otf`, Windows `.ttf` (JUCE GDI 가 CFF 곡선을 못 그려서). 라이선스 OFL | [실행 확인] Linux 화면 글꼴 / [코드 확인] | `CMakeLists.txt:452-473`, `meon/MeonTheme.cpp:9-27`, `Source/fonts/` |
| 파트 아이콘 리소스 위치 | **파일 없음 — 코드 안 경로** | 디자인의 `icons/part-*.svg` 도형을 `meon/MeonParts.cpp` 의 `build()` 에 경로 명령으로 옮겨 적음. 선 굵기 1.5 px 고정 | [실행 확인] 그려짐 | `meon/MeonParts.cpp:70-160` |
| 로고 리소스 위치 | **파일 있음** | `images/meon_logo.svg` (앱에 넣어 경로로 그림). 앱 아이콘은 `images/icons/` (`MEON.icns`, `MEON.ico`, `mac/`, `win/`), 설치 프로그램 아이콘 `MEON-installer.*`, 생성 스크립트 `scripts/make_icons.py`. 디스코드 로고는 코드 안 SVG 경로 | [실행 확인] 로고 / [코드 확인] | `CMakeLists.txt:158-159`, `441-448`, `472`, `meon/LobbyScreens.cpp:116` |

---

## 10. 알려진 문제

| # | 문제 | 영향 | 확인 | 위치 |
|---|---|---|---|---|
| 1 | **플러그인 창을 다시 열면 내 마이크 뮤트가 풀린다.** `MeonSession` 생성자가 매번 `applyProcessorDefaults()` 를 불러 `Main Send Mute` 를 0 으로 덮는다(방에 다시 붙는 경우도 같음). 독립 앱도 켤 때마다 풀린다 | 뮤트한 줄 알았는데 소리가 나감 | 플러그인 [코드 확인] / 앱 재실행 [실행 확인] | `meon/MeonSession.cpp:155`, `314` |
| 2 | **첫 실행 '소리 내보기' 화면이 처음 3초 동안은 신호가 없어도 "신호가 잘 들어오고 있어요"를 보여 준다.** `noSignal` 초기값이 false 이고 3초가 지나야 판정 | 신호가 없는데 정상처럼 보임 | [실행 확인] (장치 없이 -60 dB 인데 정상 문구) | `meon/OnboardingScreens.cpp:577-578`, `634-635`, `733` |
| 3 | **멤버 '연결 끊김' 상태가 사실상 나오지 않는다.** `connected` 는 연결될 때만 true 로 바뀌고 다시 false 가 되는 곳은 NAT 실패뿐. 중간에 소리가 끊겨도 카드는 정상으로 보이고, 서버의 peer-left 가 와야 사라진다. 30초 정리도 NAT 실패 멤버에게만 걸림 | 끊긴 멤버를 알기 어려움 | [코드 확인] | `meon/MeonSession.cpp:1074-1097`, `1233-1253` |
| 4 | 끊김 횟수 `dropCount` 를 늘리는 코드가 없어 로그 값이 항상 0 | 로그 분석 오류 | [코드 확인] | `meon/MeonSession.h:38`, `meon/MeonSession.cpp:1304` |
| 5 | 초대 코드가 **SonoBus 공개 서버의 그룹 이름 그대로**이고 비밀번호가 없다. 코드를 아는(또는 맞힌) 누구나, 일반 SonoBus 앱 사용자도 같은 이름으로 들어올 수 있다 | 방 격리·보안 | [코드 확인] | `meon/MeonSession.cpp:489`, `doc/MEON_PROGRESS.md:152` |
| 6 | '잘못된 코드' 판정이 "4초 안에 멤버가 안 보임"이다. 방이 있어도 피어 이벤트가 4초 넘게 늦으면 "그런 방이 없어요"로 나간다 | 오판 | [코드 확인] | `meon/MeonSession.cpp:1029`, `1183-1184` |
| 7 | 5·6번째가 동시에 들어오면 둘 다 "꽉 참"으로 나갈 수 있다 (입장 후 3초 안 인원 수로 판정) | 입장 실패 | [코드 확인] | `meon/MeonSession.cpp:1099-1111` |
| 8 | 코드 입력은 생성에 쓰지 않는 0·1·O·I 도 받는다. 사용자가 O 를 0 으로 잘못 넣어도 바로잡지 않음 | "그런 방이 없어요" | [실행 확인] (`1` 입력됨) | `meon/MeonWidgets.cpp:544-547`, `meon/MeonSession.cpp:10` |
| 9 | 채팅 길이 제한이 없다. OSC 패킷 최대 4096 바이트를 넘으면 엔진이 보내지 못하는데(`sendChatEvent` false), MEON 은 결과를 보지 않고 내 화면에 글을 추가한다 | 나는 보낸 줄 알지만 상대는 못 받음 | [코드 확인] | `meon/MeonSession.cpp:887-895`, `Source/SonobusPluginProcessor.cpp:2872-2884`, `deps/aoo/lib/aoo/aoo_types.h:39` |
| 10 | 새 채팅 수 `unread` 를 세지만 표시하는 곳이 없다 | 접힌 상태에서 새 글을 놓침(한 줄 미리보기만 있음) | [코드 확인] | `meon/MeonSession.h:148`, `meon/MeonSession.cpp:1144` |
| 11 | 서버 핑을 주기적으로 TCP 접속으로 재지만 화면에 쓰지 않는다(로그만). 측정 스레드는 계속 돈다 | 불필요한 접속 | [코드 확인] | `meon/MeonSession.cpp:116-149`, `422-430` |
| 12 | 쓰이지 않는 코드: `MeonEditor::quitAfterLeave`, `SettingsScreen::Content::Row`, `FilePlayer::closeArea`, `getMemberPeakLevelDb`(=`getMemberLevelDb` 와 같고 호출하는 곳 없음) | 정리 필요 | [코드 확인] | `meon/MeonEditor.h:104`, `meon/SettingsScreen.h:71`, `meon/JamScreen.cpp:430`, `meon/MeonSession.cpp:680-683` |
| 13 | Windows 업데이트 설치 파일의 **코드 서명을 확인하지 않는다**(크기·`MZ` 머리만). macOS 는 ad-hoc 서명 무결성만 확인(누가 서명했는지는 확인 안 함). 둘 다 HTTPS·GitHub 릴리스에 의존 | 공급망 위험 | [코드 확인] | `meon/MeonUpdater.cpp:420-446` |
| 14 | 업데이트 확인은 켤 때 한 번뿐이라, 앱을 오래 켜 둔 사용자는 새 버전을 모른다 | 업데이트 지연 | [코드 확인] | `meon/MeonEditor.cpp:83-88` |
| 15 | 장치·버퍼를 합주 중에 바꾸면 장치를 다시 열어 그 순간 소리가 끊긴다(경고 없음) | 합주 중 끊김 | [코드 확인] | `meon/SettingsScreen.cpp:206-226`, `meon/MeonAudio.cpp:88`, `125` |
| 16 | 플러그인에서 SonoBus 엔진의 파라미터 37개("Output Level", "Main Send Mute", "Main Reverb Enabled", "Send Channels" 등)가 **호스트에 자동화 파라미터로 그대로 노출**된다. MEON UI 에는 없는 값을 DAW 가 바꿀 수 있다 | 숨긴 기능이 DAW 로 열림 | [코드 확인] | `Source/SonobusPluginProcessor.cpp:606-691` |
| 17 | 단축키 M·C·1–4·Esc·⌘,·⌘C 가 플러그인 창 포커스 때 호스트 단축키와 겹친다 | DAW 조작 방해 | [코드 확인] | `meon/JamScreen.cpp:1425-1473` |
| 18 | `MEON_BUILD_ID=0`(로컬 빌드)이면 앱 정보에 `build 1`(`MEON_BUILD_NUMBER`)로 보여 CI 빌드 1 과 구별되지 않는다 | 표시 혼동(개발용) | [실행 확인] | `meon/MeonEditor.cpp:276`, `CMakeLists.txt:44-48` |
| 19 | 개발용 실행 옵션(`--screen`, `--create`, `--join`, `--snapshot` 등)이 배포 빌드에도 들어 있다. `--snapshot` 은 3초 뒤 PNG 를 쓰고 앱을 끈다 | 숨은 동작 | [실행 확인] | `Source/SonoStandaloneFilterApp.cpp:498-509`, `555-580` |
| 20 | SonoBus 의 명령줄 연결 옵션(`--connect` 류)과 마지막 URL 인자는 여전히 해석하지만 쓰지 않는다 | 코드만 있음 | [코드 확인] | `Source/SonoStandaloneFilterApp.cpp:447-490`, `511-514` (해석), `548-580` (쓰는 곳 없음) |

`Source/meon` 안에 TODO·FIXME 주석은 없다 (검색 결과 0). SonoBus 엔진 쪽에는 원본 TODO 가 남아 있다(예: `Source/SonobusPluginProcessor.cpp:5398`, `7676`).

---

## 11. SonoBus 원본 기능 중 숨겼거나 끈 것

### 11-1. 코드 삭제 (UI 전체 교체)
SonoBus 에디터와 화면 코드 약 60개 파일을 지우고 `Source/meon/` 으로 새로 만들었다 (`git diff --name-status 35f1062 HEAD -- Source`). 그 결과 아래 **화면이 없다**. 엔진(`SonobusPluginProcessor`) 기능은 대부분 남아 있다.

| SonoBus 기능 | 처리 | 엔진 상태 | 근거 |
|---|---|---|---|
| 서버 주소·그룹 이름·그룹 비밀번호 입력, 공개 그룹 목록, 최근 연결, 직접 IP 연결 (`ConnectView`) | 코드 삭제 | 연결 기능 남음. 서버 고정, 공개 그룹 감시 끔(플래그) | 삭제 목록, `meon/MeonSession.cpp:319` |
| 그룹 이름 자동 제안 (`SuggestNewGroupView`, `RandomSentenceGenerator`, `wordmaker*.g`) | 코드 삭제 → 6자리 코드로 대체 | — | 삭제 목록 |
| 피어별 상세: 코덱·품질 선택, 지터 버퍼 수동·모드, 솔로, 피어 이펙트, 모니터 딜레이, 채널 그룹 (`PeersContainerView`, `JitterBufferMeter`, `LatencyMatchView`, `MonitorDelayView`) | 코드 삭제 | 기능 남음. 코덱 PCM16·지터 Auto-Full 강제(플래그) | `meon/MeonSession.cpp:267-285`, `343-352` |
| 입력 채널 그룹 여러 개, 입력 이펙트(컴프·익스팬더·리미터·EQ·리버브 보내기, 극성 반전) (`ChannelGroupsView`, `CompressorView` 등) | 코드 삭제 + 이펙트 끔 | 기능 남음, 입력 그룹 1개로 고정(앱) | `meon/MeonSession.cpp:287-298`, `334` |
| 메인 리버브 | 끔 | 남음 | `meon/MeonSession.cpp:306` |
| 메트로놈 (`BeatToggleGrid`) | 코드 삭제 + 끔, 메트로놈 송출 끔 | 남음 (클릭 소리 파일도 바이너리에 남음) | `meon/MeonSession.cpp:309-310`, `CMakeLists.txt:469-471` |
| 사운드보드 (`SoundboardView`, `SampleEditView` 등) | 코드 삭제 + 송출 끔 | 엔진 쪽 사운드보드 코드 남음 | `meon/MeonSession.cpp:312` |
| 녹음 | UI 코드 삭제 | 엔진 녹음 코드 남음 | 삭제 목록 |
| 파일 재생 원본 UI (`WaveformTransportComponent`, `SonoPlaybackProgressButton`) | 코드 삭제 → MEON 파일 플레이어로 대체 | 엔진 재사용, 파일 송출 켬 | `meon/MeonSession.cpp:311` |
| 채팅 원본 UI (`ChatView`) | 코드 삭제 → MEON 채팅으로 대체 | `/sb/chat` 그대로 | — |
| VDO.Ninja 영상 링크 (`VDONinjaView`) | 코드 삭제 | — | 삭제 목록 |
| 지연 측정(루프백) 테스트 | UI 삭제 + "Hear Latency Test" 끔 | 남음 | `meon/MeonSession.cpp:313` |
| 옵션 화면 (`OptionsView`): 언어, 샘플레이트 덮어쓰기, 블루투스 입력 허용, 동적 리샘플링, 새 버전 확인 여부, 슬라이더 스냅, 키 바인딩 등 | 코드 삭제 | 해당 값은 엔진/셸 기본값 그대로 | 삭제 목록, `Source/SonoStandaloneFilterWindow.h` diff |
| SonoBus 새 버전 확인·자동 업데이트 (`VersionInfo`, `AutoUpdater`) | 코드 삭제 → `MeonUpdater` 로 대체 | — | 삭제 목록 |
| 설정 파일 저장/불러오기(setup), 명령줄 연결 | 셸에서 호출 코드 삭제 (옵션 해석은 남음 → 10장 #20) | — | `Source/SonoStandaloneFilterApp.cpp` diff |
| `sonobus://` URL 스킴·링크로 입장 | 삭제 (URL 이 오면 창만 앞으로) | — | `Source/SonoStandaloneFilterApp.cpp:774-784` |
| 다국어 (`localization/`) | 사용 안 함. UI 는 한국어 하드코딩(`TXT`) | 폴더는 남음 | `meon/MeonTheme.h:8` |
| SonoBus 글꼴(DejaVuSans, GoNotoKurrent)·LookAndFeel | 삭제 → Pretendard·`MeonLookAndFeel` | — | 삭제 목록 |
| 여러 인스턴스 동시 실행 | **막음** (`moreThanOneInstanceAllowed=false`, Windows 는 로그인 세션 뮤텍스로 한 번 더 막고 기존 창을 앞으로) | — | `Source/SonoStandaloneFilterApp.cpp:76-136`, `182`, `521-526` |
| SonoBus VSTi(악기) 변형 타깃 | 빌드에서 제외 | — | `CMakeLists.txt:555` |

### 11-2. 플래그로 끈 것 (`applyProcessorDefaults`, 켤 때마다 적용)
`meon/MeonSession.cpp:265-326` [코드 확인]
- 코덱 PCM 16 bit 고정, 기존·새 피어 모두
- 지터 버퍼 Auto-Full
- 입력 컴프레서·익스팬더·리미터·EQ 끔, 메인 리버브 끔
- 메인 수신 뮤트·입력 뮤트·송신 뮤트 해제 (→ 10장 #1)
- 메트로놈·메트로놈 송출·사운드보드 송출·지연 테스트 듣기 끔, 파일 송출 켬
- 그룹 피어 자동 연결 켬, 서버 끊김 후 재접속 켬, 공개 그룹 감시 끔
- (앱) 입력 그룹 1개, 저장된 채널 시작·개수, 송신 채널 수 = 입력 채널 수

---

## 12. 확인 못 한 것

| # | 항목 | 이유 |
|---|---|---|
| 1 | macOS·Windows 독립 앱 실행 (모든 화면·동작) | 이 컨테이너는 Linux 다. 두 OS 는 CI 컴파일 성공만 확인 |
| 2 | AU·VST3 플러그인 실행, 호스트(Logic·Ableton 등)에서의 화면·단축키·Space 동작 | 플러그인 호스트도 macOS·Windows 도 없다 |
| 3 | 서버 연결, 방 만들기·코드 입장·나가기, 입장 실패 3종, 5명 제한 | 서버 `aoo.sonobus.net` DNS 조회 실패로 접속 불가 |
| 4 | 멤버 카드(볼륨·뮤트·패닝·핑·상태 문구), 채팅 송수신·링크 확인 창 | 같은 이유 (상대 멤버 없음) |
| 5 | 실제 소리: 송신·수신, 셀프 모니터, 파일 재생 신호 경로, 귀 테러 방지 효과, 미터 움직임 | 컨테이너에 오디오 장치가 없다 (`--no-input`, ALSA 장치 없음) |
| 6 | 장치·버퍼 바꾸기, 드라이버 선택(Windows), ASIO 경고, 드라이버 설정 창 | 오디오 장치 없음, Windows 아님 |
| 7 | 헤드폰 스피커 경고 | macOS 전용 코드 |
| 8 | 파일 열기 대화상자·지원 형식 실제 재생 | 파일 대화상자를 자동 조작하지 않았고 오디오 출력이 없다 |
| 9 | 자동 업데이트 전 과정 (확인·받기·설치·다시 켜기·플러그인 동기화) | Linux 는 업데이트 미지원(`isSupported()` false), 로컬 빌드는 빌드 번호 0 |
| 10 | 홈 새 버전 알림·설정 업데이트 칸의 모양 | 같은 이유로 화면에 나오지 않는다 |
| 11 | 사용자가 창을 끌어서 줄일 때 최소 크기 제한 | Xvfb 에 창 관리자가 없어 끌기를 못 함 (프로그램으로 지정한 크기는 제한 없이 적용됨) |
| 12 | 콤보박스 열린 목록 모양 | 장치가 없어 콤보가 비활성 |
| 13 | 한글 입력기 조합·이모지 입력·키 반복(macOS 플러그인) | 입력기·macOS 없음 |
| 14 | 나가고 종료 확인 창, 이전 실행 오류 대화상자 | 방 입장이 안 되고, 크래시 상황을 만들지 않음 |
| 15 | 세션 로그 실제 파일 내용 | 방에 들어가야 기록이 시작된다 |
