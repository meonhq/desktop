# MEON 엔진·오디오 현황 (리메이크 전, AS-IS)

UI 를 새로 만들 때 **가져가거나 고쳐야 하는 엔진·오디오·구조 정보**만 모았다. 화면 컨트롤 설명은 `docs/FEATURES_AS_IS.md` 에 있다.

**표기**
- **[실행 확인]**: 이 컨테이너(Linux)에서 실제로 돌려서 확인한 것. 방법은 아래 "실행 환경"을 본다.
- **[코드 확인]**: 코드만 읽고 판단한 것.
- **[계산]**: 코드에 있는 값으로 계산한 것. 실측과 맞춰 본 경우 함께 적는다.
- 경로는 저장소 루트 기준. `Source/meon/` 은 `meon/`, `Source/SonobusPluginProcessor.cpp` 는 `SPP.cpp`, `.h` 는 `SPP.h` 로 줄인다.
- 분석 기준: `origin/main` `2cfdf5f` (작업 트리는 `a689e93` 과 내용이 같다).

### 실행 환경 (실행 확인의 범위)
- Linux 독립 앱을 Release 로 빌드해서 썼다(`MEON_BUILD_ID=0`). macOS·Windows·AU·VST3 는 실행하지 못했다.
- 진짜 서버(`aoo.sonobus.net`)에는 이 컨테이너에서 접속할 수 없다(DNS 실패). 그래서 **저장소의 `deps/aoo` 서버 코드로 로컬 AOO 서버를 띄우고**, 테스트 프로세스에서만 `aoo.sonobus.net` 을 `127.0.0.1` 로 보이게 했다(마운트 네임스페이스로 hosts 파일 교체, 시스템 파일·저장소는 그대로).
- 오디오 장치는 **JACK dummy 드라이버**(48 kHz)를 썼다. 실제 소리는 무음 입력이지만 PCM 은 무음도 같은 크기로 보내므로 대역폭 측정에는 영향이 없다.
- 같은 PC 에서 2~6개 인스턴스를 띄웠다(인스턴스마다 HOME 과 `/var/tmp` 를 분리). 송수신 수치는 MEON 세션 로그(5초 샘플의 `bytesSent`·`packetsSent`·`packetsReceived`)에서 뽑았다. 같은 PC 루프백이라 **핑·손실은 실제 인터넷과 다르다**.
- 귀 테러 방지는 빌드된 엔진 라이브러리(`libMeon_SharedCode.a`)에 연결한 측정 프로그램으로 신호를 넣어 쟀다. 저장소 코드는 바꾸지 않았다.

---

## 1. 구조

### 1-1. 계층

```
[셸] Source/SonoStandaloneFilterApp.cpp / SonoStandaloneFilterWindow.h  (독립 앱 창·장치·설정 파일)   [SonoBus 수정]
        │ dynamic_cast<meon::MeonEditor*> 로 연결
[UI+조정] meon/MeonEditor.*  (화면 전환, 단축키, 업데이트 설치, 종료 처리, 테스트 옵션)          [MEON 추가]
        │ 화면들: OnboardingScreens / LobbyScreens / JamScreen / SettingsScreen
        │ 위젯·테마: MeonWidgets / MeonLookAndFeel / MeonTheme / MeonEmoji / MeonParts(그리기)
[로직] meon/MeonSession.*  (서버·방·멤버·채팅·파일 재생·뮤트·통계·로그)                         [MEON 추가]
       meon/MeonSettings.* / MeonSessionLog.* / MeonUpdater.* / MeonAudio.* / MeonEarGuard.*
        │
[엔진] Source/SonobusPluginProcessor.*  (AOO 송수신, 믹스, 코덱, 지터 버퍼, 파일 재생, 귀 테러 방지 호출)  [SonoBus 원본 + 4곳 수정]
[라이브러리] deps/aoo (AOO 프로토콜), deps/juce 7.0.8, deps/ff_meters (미터), libopus
```
[코드 확인] (`meon/MeonEditor.h:26-121`, `meon/MeonSession.h:18-247`, `SPP.cpp:8425`)

엔진(`SonobusAudioProcessor`)에 MEON 이 넣은 수정은 4곳뿐이다: 에디터를 `meon::MeonEditor` 로 바꿈, 귀 테러 방지 객체 추가·준비·호출, 설정 파일에서 귀 테러 방지 단계 읽기 (`git diff 35f1062 HEAD -- Source/SonobusPluginProcessor.*`; `SPP.cpp:851`, `6926`, `8272`, `8425`; `SPP.h:649-650`, `1083`). [코드 확인]

### 1-2. 파일별 분류 (UI 를 통째로 바꿀 때)

| 파일 | 역할 | 분류 | 이유 / 걸리는 곳 | 근거 |
|---|---|---|---|---|
| `meon/MeonEarGuard.*` | 귀 테러 방지 DSP | **그대로 재사용** | JUCE 만 의존. UI 없음 | `meon/MeonEarGuard.h:10`, include 목록 |
| `meon/MeonSettings.*` | 사용자 설정 파일 | **그대로 재사용** | `MeonEarGuard.h` 만 포함 | `meon/MeonSettings.h:4-5` |
| `meon/MeonSessionLog.*` | 세션 로그 JSON | **그대로 재사용** | `MeonSettings.h` 만 포함 | `meon/MeonSessionLog.cpp:1-2` |
| `meon/MeonAudio.*` | 장치·드라이버·버퍼 도우미 | **거의 그대로** | `driverLabel()` 이 한국어 화면 문구를 돌려준다(UI 문구가 로직에 있음) | `meon/MeonAudio.cpp:30-37` |
| `meon/MeonUpdater.*` | 자동 업데이트 (확인·받기·교체) | **거의 그대로** | 헤더가 `MeonTheme.h` 를 포함하지만 쓰지 않는다(JuceHeader 용). 단, **"받기 끝 → 바로 설치 → 앱 종료" 흐름은 `MeonEditor` 에 있다**(`installUpdate`) | `meon/MeonUpdater.h:10`, `meon/MeonEditor.cpp:226-243` |
| `meon/MeonSession.*` | 핵심 로직 | **분리 필요 (작음)** | ① `MeonTheme.h` 에 의존(`TXT` 매크로, `metric::maxOthers`=정원) ② `getServerStatusText()` 가 한국어 화면 문구를 만든다 ③ `MeonParts.h` 를 포함하는데 그 파일에 파트 **데이터와 그리기·선택 UI 가 섞여 있다** | `meon/MeonSession.h:6-7`, `meon/MeonSession.cpp:422-430`, `598`, `1101`, `1106` |
| `meon/MeonParts.*` | 파트 종류·이름·코드 + 아이콘 그리기 + 선택 칸 | **분리 필요** | `Part` enum·`key/fromKey/code/fromCode` 는 로직(사용자 이름에 실림), `drawIcon`·`PartSelector` 는 UI | `meon/MeonParts.h:10-55` |
| `meon/MeonEditor.*` | 최상위 에디터 | **분리 필요 (큼)** | 화면 전환·단축키 외에 **로직이 섞여 있다**: 업데이트 설치·재시작(`installUpdate`), 종료 요청 처리(`requestedQuit`), 오디오 정보 수집(`collectAudioInfo`), 개발용 자동 방 만들기·입장(`applyTestOptions`/`runAutoRoomAction`), 3초 뒤 업데이트 확인, 플러그인 창 크기 저장. 셸이 이 클래스를 직접 캐스팅한다 | `meon/MeonEditor.cpp:49-89`, `211-305`, `308-365`, `400`; `Source/SonoStandaloneFilterApp.cpp:555-558`, `874-879`; `Source/SonoStandaloneFilterWindow.h:1061-1063` |
| `meon/MeonKeyRepeat*` | (macOS 플러그인) 입력창 키 반복 보조 | 입력 위젯에 따라 재사용 | JUCE `TextEditor` 기준 | `meon/MeonKeyRepeat.h` |
| `meon/MeonTheme.*`, `MeonLookAndFeel.*`, `MeonWidgets.*`, `MeonEmoji.*`, 화면 4종(`OnboardingScreens`, `LobbyScreens`, `JamScreen`, `SettingsScreen`) | UI | **교체 대상** | — | — |
| `Source/SonoStandaloneFilterApp.cpp` | 독립 앱 진입점 | **수정 필요** | `meon::MeonEditor` 를 캐스팅해 `saveSettingsIfNeeded`·`applyTestOptions`·`requestedQuit` 를 부름. 단일 인스턴스, Windows 뮤텍스, 테스트 옵션 파싱 | `Source/SonoStandaloneFilterApp.cpp:76-136`, `182`, `498-509`, `548-580`, `874-879` |
| `Source/SonoStandaloneFilterWindow.h` | 독립 앱 창·장치 관리 | **수정 필요** | 에디터에 `getAudioDeviceManager` 를 꽂아 주고, 에디터 최소 크기를 창에 적용. Windows 기본 드라이버 선택 | `Source/SonoStandaloneFilterWindow.h:129-131`, `1059-1074` |
| `Source/SonobusPluginProcessor.*` | 엔진 | **재사용** (`createEditor()` 한 줄만 새 에디터로) | `meon/MeonEditor.h`, `meon/MeonSettings.h` 포함 | `SPP.cpp:7-8`, `8423-8426` |

[코드 확인] (include 관계는 `grep '#include "' Source/meon/*` 결과)

### 1-3. `MeonSession` 이 UI 에 주는 함수 (`meon/MeonSession.h`)

**스레드 규칙** [코드 확인]
- 공개 함수는 모두 **메시지(UI) 스레드에서만** 불러야 한다. 내부 `members`·`chat` 벡터에 잠금이 없다 (`meon/MeonSession.h:205-207`).
- 엔진 콜백(`ClientListener`)은 **네트워크 스레드**에서 오고, 큐(`eventLock`)에 넣은 뒤 `AsyncUpdater` 로 메시지 스레드에서 처리한다 (`meon/MeonSession.cpp:910-973`).
- 0.2초 타이머(메시지 스레드)가 재연결·코드 확인 시간 초과·피어 동기화(0.2 s)·통계(0.4 s)·로그(5 s)를 돌린다 (`meon/MeonSession.cpp:179`, `1161-1197`).
- 서버 핑은 별도 스레드 `ServerPinger` (`meon/MeonSession.cpp:117-149`).
- 미터 값(`get…LevelDb`)은 오디오 스레드가 원자 변수로 쓰는 ff_meters 값을 읽는다 (`deps/ff_meters/LevelMeter/LevelMeterSource.h:108-124`).

**콜백 (`MeonSession::Listener`, 모두 메시지 스레드)** — `meon/MeonSession.h:57-69`

| 콜백 | 언제 | 근거 |
|---|---|---|
| `sessionStateChanged()` | 서버 상태·방 상태가 바뀔 때 | `meon/MeonSession.cpp:419`, `456`, `513`, `990`, `1012` |
| `membersChanged()` | 멤버 추가·삭제·슬롯 변화 | `meon/MeonSession.cpp:1112`, `1127`, `1258` |
| `memberStatsChanged()` | 0.4초마다 통계 갱신, 뮤트 변경 | `meon/MeonSession.cpp:612`, `727`, `1282` |
| `chatChanged()` | 채팅 추가 | `meon/MeonSession.cpp:895`, `905`, `1145` |
| `roomJoined()` / `roomLeft()` | 입장 완료 / 나감(직접·서버) | `meon/MeonSession.cpp:513`, `552` |
| `joinFailed (JoinFailure)` | `InvalidCode` / `RoomFull` / `Error` | `meon/MeonSession.cpp:467`, `524` |
| `playbackChanged()` | 파일 열기·닫기·재생·정지 | `meon/MeonSession.cpp:772`, `785`, `829` |

**함수**

| 분류 | 함수 (줄) | 하는 일 |
|---|---|---|
| 서버 | `start()` (79), `applyNicknameChange()` (80) | 연결 시작(닉네임 있을 때) / 방 밖이면 끊고 새 이름으로 재연결 |
| 서버 | `getServerState()` (81), `isServerConnected()` (82), `isServerLost()` (84) | 상태 조회 |
| 서버 | `getServerStatusText(bool home)` (86) | **한국어 화면 문구** (리메이크 때 UI 로 옮길 것) |
| 서버 | `getServerPingMs()` (87), `setServerPingInterval(ms)` (88), `getServerHost()` (89) | TCP 접속 시간 어림값, 측정 주기, 서버 호스트 |
| 방 | `createRoom()` (93), `joinRoom(code)` (94), `leaveRoom()` (95) | 방 만들기·입장·나가기 |
| 방 | `getRoomState()` (96), `isInRoom()` (97), `isBusyJoining()` (98), `getRoomCode()` (99), `getDisplayRoomCode()` (100), `getLastJoinError()` (101) | 상태·코드 |
| 멤버 | `getMemberCount()` (102, 나 포함), `isAlone()` (103), `getMembers()` (104, 슬롯 순 복사본), `getMemberInSlot(slot)` (105, 포인터) | 멤버 목록 |
| 멤버 | `setMemberMuted` (107), `setMemberGain` (108, 0–2 선형), `setMemberPan` (109, -100–100) | 내 믹스 조절 |
| 멤버 | `getMemberLevelDb` (110), `getMemberPeakLevelDb` (111, 같은 값) | 받은 소리 레벨 dBFS |
| 나 | `isMyMuted` (115), `setMyMuted` (116) | 송출 정지 |
| 나 | `getMyInputLevelDb(ch)` (117), `getMySendLevelDb()` (118) | 장치 입력 / 송신 레벨 |
| 나 | `getDisplayName` (119), `getMyPart` (120), `getUserName` (121), `setInputChannels(start,count)` (122, 앱만) | |
| 귀 테러 방지 | `getEarGuardLevel` (125), `setEarGuardLevel` (126) | 저장 + 적용 + 로그 |
| 파일 | `loadPlaybackFile` (131), `closePlaybackFile` (132), `hasPlaybackFile` (133), `getPlaybackFileName` (134), `isPlaybackPlaying` (135), `setPlaybackPlaying` (136), `getPlaybackPosition`/`Length` (137-138, 초), `setPlaybackPosition` (139), `get/setPlaybackGainDb` (140-141), `playbackMinDb=-30`, `playbackMaxDb=+10` (142) | 파일 재생 |
| 채팅 | `sendChat` (146), `getChat` (147), `getUnreadCount` (148), `markChatRead` (149) | |
| 로그 | `getLog` (153), `setAudioInfoProvider` (154) | |
| 도우미 (static) | `composeHangul` (157), `generateRoomCode` (159), `displayNameFor` (160), `partFor` (161), `isValidRoomCode` (162) | |

[코드 확인]

**다른 로직 클래스의 공개 함수** [코드 확인]

| 클래스 | 함수 | 스레드 | 근거 |
|---|---|---|---|
| `MeonSettings` | `get/setNickname`, `get/setPartKey`, `isOnboardingDone/setOnboardingDone(plugin)`, `getInputChannelStart/Count`, `setInputChannels`, `get/setEarGuardLevel`, `isChatOpen/setChatOpen`, `save()`, `getSettingsFolder()`, `getLogFolder()`, `isValidNickname()` | 메시지 스레드 (엔진 생성자에서도 한 번 읽음) | `meon/MeonSettings.h:12-46` |
| `MeonUpdater` | `isSupported()`, `currentBuild()`, `check()`, `download()`, `launchInstaller()`, `relaunchAfterInstall()`, `getState()`, `getLatestBuild()`, `getProgress()`, `lastFailureWasDownload/Permission()`, `onDownloaded` 콜백, `ChangeBroadcaster` | 작업은 자체 스레드, 알림·`onDownloaded` 는 메시지 스레드 | `meon/MeonUpdater.h:15-53`, `meon/MeonUpdater.cpp:324-350`, `453-457` |
| `EarGuard` | `setLevel/getLevel` (원자 변수, 아무 스레드), `prepare()` (오디오 스레드 밖), `process()` (오디오 스레드), `getBand/FullTriggerCount()` | | `meon/MeonEarGuard.h:32-48` |
| `MeonSessionLog` | `begin`, `addEvent`, `addSample`, `end`, `isActive`, `getFile`, `makeObject` | 메시지 스레드 (파일 쓰기도 같은 스레드, 5초마다 2개 샘플에 한 번) | `meon/MeonSessionLog.h:19-29`, `meon/MeonSessionLog.cpp:73-84` |
| `audio::` (MeonAudio) | `driverTypes`, `driverLabel`, `setDriverType`, `preferredDriverType`, `setDevices`, `bufferChoices`, `currentBufferSize`, `setBufferSize`, `hasDriverPanel`, `showDriverPanel` | 메시지 스레드 | `meon/MeonAudio.h:10-37` |
| `MeonEditor` (셸이 씀) | `getAudioDeviceManager` (std::function), `saveSettingsIfNeeded` (std::function), `requestedQuit()`, `applyTestOptions(...)` | 메시지 스레드 | `meon/MeonEditor.h:36-40`, `63-65` |

---

## 2. 서버·연결

| 항목 | 현재 | 확인 | 근거 |
|---|---|---|---|
| 서버 주소·포트 | `aoo.sonobus.net` : `10998` (SonoBus 공개 서버). **`#define` 하드코딩**, 설정·UI·명령줄로 바꿀 수 없다 | [코드 확인] | `SPP.h:35-36`, `meon/MeonSession.cpp:158`, `414`, `505-506` |
| 서버 통신 | TCP (OSC, SLIP) 로그인·그룹 입장/퇴장·서버 핑 + NAT 통과용 UDP | [코드 확인] | `deps/aoo/lib/src/client.cpp:604`, `905` |
| 로그인 | 사용자 이름 `닉네임#XXXX`(+파트 한 글자 `v/g/b/d/k/o`), **비밀번호 없음**(`""`). XXXX 는 `ABCDEFGHJKLMNPQRSTUVWXYZ23456789` 에서 연결할 때마다 4자 무작위 | [실행 확인] 로그의 `userName` 예: `BBB#Q99G`, `AAA#LT9P` / [코드 확인] | `meon/MeonSession.cpp:241-252`, `406-414`, `SPP.h:298` |
| 그룹 이름 | **초대 코드 6자를 그대로 그룹 이름으로**. 그룹 비밀번호 `""`, 비공개(`isPublic=false`) | [실행 확인] 로그 `roomCode` = 그룹 / [코드 확인] | `meon/MeonSession.cpp:489`, `SPP.h:306` |
| 초대 코드 규칙 | 길이 6, 문자 집합 `ABCDEFGHJKLMNPQRSTUVWXYZ23456789`(32자, O·0·I·1 제외), `Random::getSystemRandom()` 으로 뽑음. 경우의 수 32⁶ ≈ 10.7억. **서버에 중복 확인을 하지 않는다**(이미 있는 그룹이면 그 방에 합류) | [실행 확인] 생성 예 `BHZQCH`, `E79GTB`, `RGFTXF`, `ZHMWWN`, `HRFDXR` / [코드 확인] | `meon/MeonSession.cpp:10`, `200-208` |
| 코드 검사 (입장 때) | 6자이고 A–Z·0–9 이면 통과 (0·1·O·I 도 통과) | [코드 확인] | `meon/MeonSession.cpp:210-221` |
| 정원 정의 | `metric::maxMembers = 5` (나 포함), `metric::maxOthers = 4`. **UI 테마 파일(`MeonTheme.h`)에 있다** | [코드 확인] | `meon/MeonTheme.h:44-45` |
| 정원 판정 | 서버는 인원 제한이 없다. 입장 중(Verifying)에 피어 이벤트가 오면 `members.size() >= 5` 이면 `RoomFull` 로 그룹을 나간다. 입장 후 3초 안에 5명 이상이 보여도 나간다 | [실행 확인] 6개 인스턴스: 6번째가 "이 방은 5명이 모두 찼어요"로 나가고, 나머지 5명은 `peerLeft` 를 받음 | `meon/MeonSession.cpp:1099-1111` |
| 없는 코드 판정 | 그룹 입장 성공 후 **4초 안에 피어가 하나도 안 보이면** `InvalidCode` 로 나간다(서버가 방 존재 여부를 알려주지 않는다. 없는 그룹이면 서버가 새로 만든다) | [실행 확인] `ZZZZZZ` 입장 → 약 4초 뒤 "그런 방이 없어요" | `meon/MeonSession.cpp:1028-1030`, `1182-1184` |
| P2P | 소리·채팅은 **멤버끼리 UDP 로 직접**. 서버는 상대 주소만 알려 준다. 각 멤버에게 따로 보낸다(멀티캐스트 없음 → 인원에 비례해 업로드 증가) | [실행 확인] 서버를 끈 동안에도 소리 패킷이 계속 오갔다(아래 재접속) / [코드 확인] | `deps/aoo/lib/src/source.cpp:1220-1233`, `SPP.cpp:3229-3232` |
| 릴레이 | **없다** (aoo 에 릴레이 코드 없음) | [코드 확인] | `deps/aoo` 전체 검색 |
| NAT 실패 | 엔진이 `aooClientPeerJoinFailed` 를 보내면 그 멤버를 `joinFailed` 로 표시하고 30초 뒤 목록에서 지운다. 그 멤버와는 소리가 오가지 않는다. 다른 멤버와는 정상 | [코드 확인] (같은 PC 라 NAT 실패를 만들 수 없었다) | `meon/MeonSession.cpp:1089-1097`, `1247-1253` |
| 서버 연결 시간 초과 | 12초 안에 연결이 안 되면 끊고 3초 뒤 재시도(거부 응답이면 0.5초) | [코드 확인] | `meon/MeonSession.cpp:413`, `996`, `1172-1180` |
| 방 안에서 서버가 끊기면 | MEON 은 직접 재시도하지 않고 **엔진 자동 재접속**에 맡긴다(`setReconnectAfterServerLoss(true)`). 엔진은 1초마다 마지막 연결 정보(MEON 이 입장 때 등록)로 다시 접속하고 **같은 그룹에 다시 들어간다**. 피어 연결은 끊지 않는다 | [실행 확인] 서버를 9초 동안 끔: 소리 송수신 계속, 1초 간격 재시도, 서버가 살아나자 `serverReconnected` + 같은 방 재입장 | `meon/MeonSession.cpp:316-318`, `501-508`, `SPP.cpp:4208-4218`, `8843-8868` |
| 방 밖에서 끊기면 | MEON 타이머가 3초 뒤 재연결 | [코드 확인] | `meon/MeonSession.cpp:1004-1008`, `1167-1171` |
| 로그 주의 | 재시도가 실패할 때마다 `serverLost` 이벤트가 하나씩 쌓인다(9초에 10개) | [실행 확인] | `meon/MeonSession.cpp:1008-1009` |
| 단일 인스턴스 | 독립 앱은 한 번에 하나. Linux·macOS 는 JUCE 잠금 파일(Linux `/var/tmp/juceAppLock_Meon`), Windows 는 추가로 `Local\MEON-SingleInstance` 뮤텍스 | [실행 확인] Linux: 두 번째 실행은 바로 꺼짐 / Windows 는 [코드 확인] | `Source/SonoStandaloneFilterApp.cpp:182`, `76-136`; `deps/juce/modules/juce_events/messages/juce_ApplicationBase.cpp:94-104`; `deps/juce/modules/juce_core/native/juce_SharedCode_posix.h:736-741` |

---

## 3. 코덱·대역폭

### 3-1. 코덱 고정

| 항목 | 현재 | 확인 | 근거 |
|---|---|---|---|
| 고정 위치 | 엔진 형식 목록에서 `CodecPCM` 이고 `bitdepth == 2`(바이트) 인 것을 찾아 **기본 형식**으로 설정 + "기본값 바꾸면 기존 피어도 바꿈" 켬. 새 피어마다 다시 강제 | [코드 확인] | `meon/MeonSession.cpp:267-282`, `343-347` |
| 이유 | **기록된 이유는 없다.** 커밋 `1c2dc4f` 본문: "기본값: PCM 16bit 고정, 지터버퍼 자동, …". 진행 문서: "코덱: **PCM 16 bit** 고정 (새 피어에도 강제 적용). 엔진 코드는 그대로." / "PCM 비트 수: 16 bit 로 두었다 (48 kHz 모노 기준 상향 약 0.8 Mbps/피어). 24 bit 로 올리면 대역폭 1.5배." README 비교표: "Opus / PCM 선택 → 무압축 PCM 16 bit 고정" | [코드 확인] | `git show 1c2dc4f`, `doc/MEON_PROGRESS.md:74`, `88`, `README.md:15` |
| 상대 요청 | 엔진이 상대의 코덱 변경 요청을 따르게 켜져 있다(`set_respect_codec_change_requests(1)`). 일반 SonoBus 앱이 같은 방에 들어와 요청하면 바뀔 수 있다 | [코드 확인] | `SPP.cpp:6070` |

### 3-2. 샘플레이트·채널·패킷

| 항목 | 값 | 확인 | 근거 |
|---|---|---|---|
| 샘플레이트 | **장치(앱) 또는 호스트(플러그인)의 샘플레이트 그대로** 보낸다. 고정값 없음. 서로 다르면 받는 쪽 aoo 가 재표본화 | [실행 확인] JACK 48 000 Hz → 로그 `sampleRate: 48000` / [코드 확인] | `SPP.cpp:6323`, `6017` |
| 블록(패킷당 샘플) | `currSamplesPerBlock`(장치·호스트 블록, 16 미만이면 16) | [실행 확인] 64·128·256·512 샘플에서 패킷 크기가 그대로 바뀜 | `SPP.cpp:6322`, `163` |
| 송신 채널 (앱) | 입력 채널 수에 맞춰 1(모노) 또는 2(스테레오). 파일 재생은 그 채널 안에 섞는다 | [실행 확인] 모노 `sendChannels: 1`, 스테레오 설정 시 2채널 크기 패킷 | `meon/MeonSession.cpp:328-341` |
| 송신 채널 (플러그인) | 엔진 기본 "입력 수에 맞춤"(0). 이때 채널 수 = **입력 채널(호스트 메인 입력, 보통 2) + 파일 재생 채널(파일이 없어도 2)** = 보통 **4채널**. 파일 소리는 3·4번 채널로 따로 실린다 | [코드 확인] — 플러그인을 실행하지 못해 실측 못 함 | `SPP.cpp:781-786`, `6997-7001`, `7028-7049`, `5220-5234`, `7600-7604`, `7655-7672`, `meon/MeonSession.cpp:311`, `330-331` |
| 패킷 크기 한도 | 피어당 `packetsize = 600` 바이트. 데이터 부분은 600 − 80 = 520 바이트까지 한 패킷, 넘으면 여러 패킷으로 나눔 | [실행 확인] 모노 512 샘플(데이터 1024 B)은 블록당 패킷 2개 | `SPP.cpp:302`, `6039`, `deps/aoo/lib/src/source.cpp:15`, `1206-1245` |
| 패킷 형식 | 데이터가 한 패킷에 들어가면 압축형 `/d` 메시지(salt·seq·blob) = **24 + 데이터** 바이트. 나누면 `/aoo/sink/<id>/data` (더 큼) | [실행 확인] 아래 표의 패킷당 바이트가 계산과 같음 | `deps/aoo/lib/src/source.cpp:779-797`, `746-766` |
| 중복 전송 | 1회 (`AOO_SEND_REDUNDANCY 1`) | [코드 확인] | `deps/aoo/lib/aoo/aoo.h:83` |
| 엔진 바이트 집계 | `bytesSent` 는 OSC 페이로드만 센다(UDP/IP 헤더 0 으로 둠) | [코드 확인] | `SPP.cpp:162`, `386-387` |

### 3-3. 피어 1명당 대역폭 (PCM 16 bit, 48 kHz)

**계산식** [계산]
- 데이터 = 블록 N × 채널 C × 2 바이트
- 데이터 ≤ 520 B: 패킷 1개, OSC 페이로드 = 24 + 2NC 바이트, 초당 패킷 = 48000 / N
- 네트워크 실제 크기 = 페이로드 + 28 B (IPv4 UDP 8 + IP 20, 이더넷 헤더 제외)
- **송신 kbps = (24 + 2NC + 28) × (48000/N) × 8 / 1000** (한 패킷일 때)
- 수신은 상대가 보내는 형식(상대의 블록·채널)으로 같은 식. 모든 멤버가 같은 설정이면 송신 = 수신.

| 블록 N | 채널 | 패킷당 페이로드 | 초당 데이터그램 | 페이로드 kbps (실측) | 페이로드 kbps (계산) | **네트워크 kbps (계산)** | 근거 |
|---|---|---|---|---|---|---|---|
| 64 | 모노 | 152 B | 750 | 901.0 / 907.8 | 912 | **1,080** | [실행 확인] |
| 128 | 모노 | 280 B | 375 | 827.2 / 834.8 / 840.5 | 840 | **924** | [실행 확인] |
| 256 | 모노 | 536 B | 187.5 | 803.6 / 802.9 | 804 | **846** | [실행 확인] |
| 512 | 모노 | 블록당 1,168.8 B (2개로 나뉨) | 187.5 | 873.1 / 876.9 | 877 | **919** | [실행 확인] |
| 128 | 스테레오 | 536 B | 375 | 1,605.2 / 1,603.1 | 1,608 | **1,692** | [실행 확인] |
| 64 | 스테레오 | 280 B | 750 | — | 1,680 | **1,848** | [계산] |
| 256 | 스테레오 | 블록당 약 1,169 B (2개) | 375 | — | 약 1,753 | **약 1,837** | [계산] (나눈 패킷 오버헤드는 모노 512 실측값 72.4 B/패킷 사용) |
| 128 | 4채널 (플러그인 추정) | 블록당 약 1,169 B (2개) | 750 | — | 약 3,507 | **약 3,675** | [계산] (3-2 의 플러그인 4채널이 맞을 때) |

- 실측은 같은 PC 두 인스턴스의 세션 로그(5초 샘플 2개 이상) 차이로 냈다. 두 숫자는 A·B 각각.
- 제어 트래픽(핑 2초마다, 피어 정보 등)은 **약 0.5 kbps** 로 무시할 수 있다 [실행 확인: 상대가 나를 뮤트해 소리가 멈춘 15초 동안 `bytesSent` 가 884 B 늘어남].
- 44.1 kHz 장치면 초당 패킷이 0.919배가 되어 대역폭도 비례해 줄어든다 [계산].

### 3-4. 인원별 총 대역폭 (한 사람 기준)

각 멤버에게 따로 보내고 따로 받으므로 **업로드 = 다운로드 = (인원 − 1) × 피어당** [계산, 3-3 네트워크 kbps 기준].

| 설정 (48 kHz) | 피어당 | **5명 (현재 정원, 피어 4)** | **7명 (피어 6)** |
|---|---|---|---|
| 모노 128 샘플 | 924 kbps | 업 3,696 / 다운 3,696 kbps | **업 5,544 / 다운 5,544 kbps (≈ 5.5 Mbps)** |
| 모노 64 샘플 | 1,080 kbps | 4,320 / 4,320 | 6,480 / 6,480 |
| 모노 256 샘플 | 846 kbps | 3,384 / 3,384 | 5,076 / 5,076 |
| 스테레오 128 샘플 | 1,692 kbps | 6,768 / 6,768 | **10,152 / 10,152 kbps (≈ 10.2 Mbps)** |
| 플러그인 4채널 128 (추정) | 약 3,675 kbps | 약 14,700 / 14,700 | 약 22,050 / 22,050 |

- **7명은 지금 코드로는 불가능하다**(정원 5, `meon/MeonTheme.h:44-45`). 정원을 늘리면 위 숫자가 된다.
- 5명 실측: 6개 인스턴스 테스트에서 5명이 동시에 연결된 상태를 만들었지만(JACK 256 샘플), 그 테스트는 대역폭을 따로 계산하지 않았다.

### 3-5. 엔진이 지원하는 다른 코덱

| 번호 | 코덱 | 비트레이트 | 최소 블록 | 근거 |
|---|---|---|---|---|
| 0 | Opus | 16 kbps/채널 | 960 샘플 | `SPP.cpp:1339` |
| 1 | Opus | 24 kbps/채널 | 480 | `SPP.cpp:1340` |
| 2 | Opus | 48 kbps/채널 | 240 | `SPP.cpp:1341` |
| 3 | Opus | 64 kbps/채널 | 240 | `SPP.cpp:1342` |
| 4 | Opus | 96 kbps/채널 (**SonoBus 기본값**) | 120 | `SPP.cpp:1343`, `1353` |
| 5 | Opus | 128 kbps/채널 | 120 | `SPP.cpp:1344` |
| 6 | Opus | 160 kbps/채널 | 120 | `SPP.cpp:1345` |
| 7 | Opus | 256 kbps/채널 | 120 | `SPP.cpp:1346` |
| 8 | PCM 16 bit (**MEON 고정**) | 768 kbps/채널 @48 kHz (오버헤드 제외) | 16 | `SPP.cpp:1348` |
| 9 | PCM 24 bit | 1,152 kbps/채널 | 16 | `SPP.cpp:1349` |
| 10 | PCM 32 bit float | 1,536 kbps/채널 | 16 | `SPP.cpp:1350` |

[코드 확인]
- Opus 설정: complexity 10, signal MUSIC, `OPUS_APPLICATION_RESTRICTED_LOWDELAY`, 비트레이트는 채널 수를 곱함 (`SPP.cpp:6330-6342`).
- 엔진의 지연 추정은 Opus 를 쓰면 방향마다 2.5 ms 를 더한다 (`SPP.cpp:5683-5684`).
- **Opus 로 바꿀 때 영향받는 곳**: `meon/MeonSession.cpp:267-282`(기본 형식 선택), `345-346`(피어마다 강제), 대역폭(3-3 표 전체), 지연(최소 블록이 장치 블록보다 크면 블록이 커짐, `SPP.cpp:6332`), CPU(인코드·디코드), 세션 로그 해석, `doc/MEON_PROGRESS.md`·README 의 설명.

---

## 4. 지연·버퍼

### 4-1. 지터 버퍼 Auto-Full

MEON 은 기본값과 모든 피어를 **Auto-Full** 로 둔다 (`meon/MeonSession.cpp:285`, `347`). 엔진 동작 [코드 확인]:

| 규칙 | 값 | 근거 |
|---|---|---|
| 처음 크기 | 파라미터 `Default Jitter Buffer Time` 기본 0.001 s = **1 ms**. 같은 사용자 이름의 캐시가 있으면 그 값 | `SPP.h:974`, `SPP.cpp:6003`, `6195-6215` |
| 화면·로그 값 | `max(설정값, 장치 블록 1개)` (128 샘플이면 최소 2.667 ms) | `SPP.cpp:5314-5326` |
| 늘리기 | 블록 손실 이벤트가 올 때, 지난 손실 뒤 **0.5 s 넘게 지났고 그 간격이 2 s 미만**(손실률 > 0.5회/s)이면 **블록 1개만큼**(128 샘플 @48k = 2.667 ms) 늘림. 감소한 지 2 s 안에 손실이 나면 지금 크기를 이후 감소의 하한으로 고정 | `SPP.cpp:3907-3944`, `SPP.h:1037` (`mAutoresizeDropRateThresh = 0.5`) |
| 줄이기 | 핑 이벤트(2 s 마다) 때, 하한보다 크고 **마지막 감소·마지막 손실 둘 다 10 s 넘게 지났으면** 블록 1개만큼 줄임. 하한 = 처음엔 블록 1개 | `SPP.cpp:4053-4100`, `5335-5336` |
| 상한 | `mMaxBufferTime` 1.0 s (파라미터 범위) | `SPP.h:975`, `SPP.cpp:630` |
| 새 피어 안전 뮤트 | 버퍼가 3 ms 미만으로 시작하면 그 피어 소리를 처음엔 0 으로 내보내다가, 리셋 후 0.5 s 지나고 (손실 없이 0.75 s, 또는 손실률 < 2회/s, 또는 버퍼 > 15 ms)이면 켬 | `SPP.cpp:6023`, `2237-2260`, `7908-7911` |

[실행 확인] (같은 PC, 128 샘플 @48 kHz)
- 정상 상태: 지터 버퍼 2.667 → 3.667 → 5.333 / 6.333 ms 사이에서 움직였다 (1 ms + 블록 정수배).
- JACK 버퍼를 합주 중 128 → 512 로 바꾸자(`jack_bufsize 512`) 10초 동안 손실 300여 개, 지터 버퍼가 **3.667 → 11.667 → 19.667 ms** 로 늘었고, 손실이 멈춘 뒤 10초에 **19.667 → 17.0 ms**(블록 1개) 줄었다. 연결은 유지됐다.

### 4-2. 엔진의 다른 버퍼 모드

| 모드 | 동작 | 근거 |
|---|---|---|
| `Off` (0) | 수동. 크기 고정 | `SPP.h:125`, `SPP.cpp:4049-4052` |
| `AutoIncreaseOnly` (1) | 손실 때 늘리기만 | `SPP.h:126` |
| `AutoFull` (2) | 늘리고 줄이기 (MEON) | `SPP.h:127` |
| `InitAuto` (3) | 처음 0 에서 시작해 손실 없이 7 s 지나면 고정 (SonoBus 기본 UI 에서 "Initial-Auto") | `SPP.h:128`, `SPP.cpp:4026-4035`, `5337-5338` |

[코드 확인]

### 4-3. 장치 버퍼

| 항목 | 현재 | 확인 | 근거 |
|---|---|---|---|
| 선택지 (앱) | 64/128/256/512 샘플 각각에 가장 가까운, 장치가 지원하는 크기 | [코드 확인] | `meon/MeonAudio.cpp:91-112` |
| 적용 | `AudioDeviceManager::setAudioDeviceSetup` → 장치를 다시 엶. **합주 중에도 바로 적용**(경고 없음) | [코드 확인] | `meon/MeonAudio.cpp:121-126`, `meon/SettingsScreen.cpp:214-226` |
| 엔진 반응 | 블록 크기가 30블록 동안 같으면 `currSamplesPerBlock` 를 바꾸고 표시(`mNeedsSampleSetup`)를 남김 → 송신 스레드가 모든 송신 형식을 다시 잡고, 받는 소리를 잠깐 뮤트했다 풀어 초기화 | [코드 확인] | `SPP.cpp:7369-7382`, `3293-3300` |
| 합주 중 변경 실측 | 위 4-1 의 `jack_bufsize` 실험: 연결 유지, 손실 후 지터 버퍼 자동 증가. 단 이 실험에서 패킷 크기는 128 샘플 그대로였다(JACK 경로라 MEON 설정 화면의 경로와 다르다) | [실행 확인] (일부) | — |
| 플러그인 | 호스트가 정함. 엔진은 `prepareToPlay` 의 크기로 시작하고, 실제 블록이 30번 연속 다르면 따라감(Logic 이 I/R 버튼으로 `prepareToPlay` 없이 바꾸는 경우 대비) | [코드 확인] | `SPP.cpp:6918`, `7369-7382`, `meon/SettingsScreen.cpp:523-524` |

### 4-4. 핑·지연 측정과 화면 값

| 값 | 정의 | 어디에 보이나 | 확인 | 근거 |
|---|---|---|---|---|
| 멤버 핑 `pingMs` | 엔진이 각 피어와 **2 s 마다** 주고받는 핑의 **왕복(RTT)** 평활값(`smoothPingTime.xbar`). 오디오·지터 버퍼 **불포함** | 멤버 카드 "N ms"(정수 반올림), 18 ms 초과 경고 | [실행 확인] 같은 PC 에서 0.09–0.29 ms → 카드 "0 ms" | `SPP.cpp:43`, `3430-3450`, `5673`, `meon/JamScreen.cpp:654`, `meon/MeonTheme.h:43` |
| 총 왕복 지연 추정 `roundtripMs` | 들어옴 = 받는 코덱 지연 + 상대 입력 지연 + 핑/2 + 내 지터 버퍼, 나감 = 보내는 코덱 지연 + 상대 출력 지연 + 핑/2 + 상대 지터 버퍼. 합 | **로그에만** (화면 없음) | [실행 확인] 128 샘플·같은 PC 에서 12.8–18.1 ms, 지터 버퍼가 19.7 ms 로 커지자 44.8 ms | `SPP.cpp:5675-5690`, `meon/MeonSession.cpp:1272`, `1299` |
| 지터 버퍼 `jitterBufferMs` | 4-1 | 로그에만 | [실행 확인] | `meon/MeonSession.cpp:1274` |
| 서버 핑 | 서버에 TCP 접속하는 데 걸린 시간(홈 5 s / 방 10 s / 그 밖 15 s 마다) | 로그에만 | [코드 확인] | `meon/MeonSession.cpp:116-149` |
| 화면 갱신 | 통계는 0.4 s 마다 엔진에서 읽음 | — | [코드 확인] | `meon/MeonSession.cpp:1189-1190` |

---

## 5. 믹스·레벨

### 5-1. 신호 경로 (엔진 `processBlock`)

```
장치 입력 ─(입력 미터 7466)─▶ 입력 채널 그룹: × In Gain × (Main In Mute 면 0) (7497)
            ─(post-입력 미터 7513)─▶ inputPostBuffer
   ├─▶ 송신 버퍼 sendWorkBuffer (7535~)  ◀─ 파일 재생 × 파일 볼륨 (7642-7683)
   │      └─(송신 미터 7791)─▶ AOO 소스 → 각 피어 (Main Send Mute 면 소스를 멈춤, 6732-6760)
   └─▶ 셀프 모니터: 입력 그룹 모니터 → inputBuffer × Dry Level (7591-7596, 8095-8104)

각 피어 수신 → × 멤버 볼륨(게인) (7902-7929) ─(멤버 미터 7934)─▶ 패닝(가운데 -4.5 dB) → 출력 버퍼에 합산
출력 버퍼 + 파일 재생(모니터) × 파일 볼륨 (8234-8243)
  → × Output Level(마스터, wet) (8259-8265) → 귀 테러 방지 (8272) ─(출력 미터 8274)─▶ 장치·호스트 출력
```
[코드 확인] (줄 번호는 `SPP.cpp`)

### 5-2. 항목별

| 항목 | 범위·단위·기본값 | 엔진 이름 | 신호 경로 위치 | 확인 | 근거 |
|---|---|---|---|---|---|
| 멤버 볼륨 | 선형 게인 0 ~ 2 (−∞ ~ +6.02 dB), 기본 1.0 (0 dB). 새 피어 기본값은 파라미터 `Default User Level`(1.0) | `setRemotePeerLevelGain(index, gain)` / `remote->gain` | 수신 디코드 뒤, 채널 그룹 처리에서 곱함 → 미터 → 패닝 | [실행 확인] 카드 기본 "0.0 dB" / [코드 확인] | `meon/MeonSession.cpp:616-625`, `SPP.cpp:6015`, `7902`, `SPP.h:997` |
| 멤버 패닝 | MEON 값 −100 ~ +100 (정수) → 엔진 −1.0 ~ +1.0. 모노 피어는 한 값, 스테레오 피어는 L = −1+2p, R = +1+2p (±1 로 자름) | `setRemotePeerChannelPan(index, group, ch, pan)` | 출력 합산 직전 | [코드 확인] | `meon/MeonSession.cpp:354-374`, `627-652` |
| 팬 법칙 | 선형 밸런스(p ≥ 0: L×(1−p), p < 0: R×(1+p)) × **(0.596 + \|p\|×0.404)**. 가운데 0.596 = **−4.5 dB**, 끝 0 dB | `centerPanLaw = 0.596` | — | [코드 확인] | `Source/ChannelGroup.h:53`, `Source/ChannelGroup.cpp:344-357` |
| 멤버 뮤트 | 켬/끔. 엔진이 그 피어 소스를 **초대 취소**(uninvite) → **상대가 나에게 보내는 것을 멈춤**(네트워크도 줄어듦). 다시 켜면 재초대 | `setRemotePeerRecvActive(index, active)` | 수신 자체를 멈춤 | [실행 확인] B 가 A 를 뮤트하자 A→B `bytesSent` 정지, B `packetsReceived` 정지 | `meon/MeonSession.cpp:604-614`, `SPP.cpp:5385-5409` |
| 마스터 볼륨 | 선형 0 ~ 2, 기본 1.0. **MEON UI 없음** | 파라미터 `wet` ("Output Level") | 모든 믹스 뒤, 귀 테러 방지 앞 (메인 버스만) | [코드 확인] | `SPP.cpp:626`, `8259-8265`, `SPP.h:973` |
| 셀프 모니터 | 0 ~ 1, 기본 **앱 0 / 플러그인 1.0**. **MEON UI 없음** | 파라미터 `dry` ("Dry Level") | 입력 그룹 모니터 결과를 출력에 복사 | [코드 확인] | `SPP.cpp:622`, `781-789`, `8095-8104` |
| 송출 정지 | 켬/끔, 기본 끔 | 파라미터 `mastsendmute` ("Main Send Mute") | 오디오 처리는 그대로 하고, 각 피어 AOO 소스를 멈춤(`sendActive=false`). 송신 미터는 계속 움직이므로 MEON 은 뮤트 중 송신 레벨을 −100 dB 로 돌려줌 | [실행 확인] 버튼 상태 / [코드 확인] 송신 중단 | `SPP.cpp:665`, `6732-6760`, `4432-4441`, `meon/MeonSession.cpp:869-874` |
| 입력 게인 | 선형 0 ~ 4 (−∞ ~ +12.04 dB), 기본 1.0. **MEON UI 없음**(플러그인은 호스트 자동화로 바뀔 수 있음) | 파라미터 `ingain` ("In Gain") | 입력 채널 그룹에서 곱함. 송신·셀프 모니터 모두에 영향, 입력 미터에는 영향 없음 | [코드 확인] | `SPP.cpp:606`, `7347`, `7362`, `7497`, `SPP.h:968` |
| 입력 뮤트 / 수신 뮤트 / 모니터 솔로 | 켬/끔. MEON 은 켤 때 입력·수신 뮤트를 0 으로 둠, UI 없음 | `mastinmute`, `mastrecvmute`, `mastmonsolo` | — | [코드 확인] | `SPP.cpp:666-668`, `meon/MeonSession.cpp:307-308` |

---

## 6. 미터

| 측정 지점 | 엔진 소스 | MEON 에서 쓰는 곳 | 근거 |
|---|---|---|---|
| 장치 입력 (In Gain 앞) | `inputMeterSource` (`SPP.cpp:7466`) | 첫 실행 '소리 내보기', 설정 입력 레벨 (`getMyInputLevelDb`) | `meon/MeonSession.cpp:864-867` |
| 입력 처리 뒤 | `postinputMeterSource` (`SPP.cpp:7513`) | 안 씀 | — |
| 송신 (파일 섞인 뒤) | `sendMeterSource` (`SPP.cpp:7791`) | 합주 화면 내 영역 (`getMySendLevelDb`, 뮤트면 −100) | `meon/MeonSession.cpp:869-874` |
| 멤버별 수신 (멤버 볼륨 뒤, 패닝 앞) | `remote->recvMeterSource` (`SPP.cpp:7934`) | 멤버 카드 (`getMemberLevelDb`, 끊김·뮤트면 −100) | `meon/MeonSession.cpp:667-678` |
| 출력 (귀 테러 방지 뒤) | `outputMeterSource` (`SPP.cpp:8274`) | **안 씀** | — |
| 파일 재생 | `filePlaybackMeterSource` (`SPP.cpp:7632`) | 안 씀 | — |

| 항목 | 값 | 확인 | 근거 |
|---|---|---|---|
| 피크 / RMS | **샘플 피크**(블록마다 `getMagnitude` 최댓값). 1.0 에서 자름. RMS 도 계산하지만 MEON 은 안 씀. 채널이 여럿이면 가장 큰 채널 | [코드 확인] | `deps/ff_meters/LevelMeter/LevelMeterSource.h:108-124`, `192-209`, `284-287`, `meon/MeonSession.cpp:654-665` |
| 엔진 피크 유지 | 입력·송신·멤버 수신: **25 ms** (MEON 설정), 그 밖 ff_meters 기본 500 ms | [코드 확인] | `meon/MeonSession.cpp:8`, `324-325`, `348-349`, `LevelMeterSource.h:159` |
| 화면 갱신 | 화면 주사율마다(VBlank, 보통 60 Hz) | [코드 확인] | `meon/JamScreen.h:85`, `meon/OnboardingScreens.h:140` |
| 감쇠 | 오를 땐 즉시, 내릴 땐 **30 dB/s**. −70 dB 아래로 가면 바로 바닥 | [코드 확인] | `meon/MeonWidgets.cpp:269-282` |
| 피크 홀드 표시 | 막대에는 없음. 숫자(내 영역 "최고 N dB", 첫 실행 값)만 **1.5 s** 유지 | [코드 확인] | `meon/JamScreen.cpp:115-125`, `meon/OnboardingScreens.cpp:628-632` |
| 클리핑 | 표시 레벨 ≥ **−0.1 dBFS** 이면 1.5 s 동안 막대 끝 6% 를 검정으로. 엔진 값이 1.0 에서 잘리므로 사실상 0 dBFS 도달 | [코드 확인] | `meon/MeonWidgets.cpp:283-286`, `321-328` |
| 눈금 | −60 ~ 0 dB, −60/−40/−20/−12/−6/0 을 같은 간격에 둔 구간 선형 | [실행 확인] 눈금 표시 | `meon/MeonWidgets.cpp:253-267` |

---

## 7. 귀 테러 방지

| 항목 | 현재 | 확인 | 근거 |
|---|---|---|---|
| 있는지 | 있음 (MEON 추가) | [실행 확인] | `meon/MeonEarGuard.*` |
| 위치 | **내 모니터 최종 출력**: 멤버 믹스·파일 모니터·마스터 볼륨 뒤, 출력 미터 앞, 메인 버스 채널만. 송신·녹음에는 적용 안 함 | [코드 확인] | `SPP.cpp:8270-8274` |
| 알고리즘 | ① **2–5 kHz 대역 리미터**: 대역 통과(RBJ, 중심 3,162 Hz, Q 1.05)의 파워 엔벨로프(채널 최대)가 기준을 넘으면 넘은 양 × (1 − 1/비율) 만큼(최대 감쇠까지) 그 대역만 뺌: `y = x − (1−g)·BP(x)`. 기준 아래면 출력 = 입력 ② **전체 리미터**: ①의 결과를 BS.1770 K-가중(셸프 + 고역 통과)한 40 ms 평균 파워가 상한을 넘으면 넘은 만큼 전체 게인을 낮춤 ③ **피크 상한**: 샘플 피크가 상한을 넘으면 그 샘플에서 바로 누름. **미리보기 없음(지연 0)**, 모든 채널 같은 게인. 급변 감지 같은 별도 로직은 없다(레벨 기준) | [코드 확인] + [실행 확인] 지연 0 샘플 | `meon/MeonEarGuard.cpp:61-109`, `147-249` |
| 채널 한도 | 16채널까지 대역 처리, 그 이상은 전체 게인만 | [코드 확인] | `meon/MeonEarGuard.h:30` |

**단계별 수치** [코드 확인] (`meon/MeonEarGuard.cpp:11-27`)

| 단계 | 대역 기준 (2–5 kHz RMS) | 대역 비율 | 대역 최대 감쇠 | K-가중 상한 | 피크 상한 |
|---|---|---|---|---|---|
| 끔 | (40 dBFS, 사실상 없음) | 1:1 | 0 dB | (40) | (40) |
| 보통 (기본) | −18 dBFS | 4:1 | 12 dB | −10 LKFS | −1 dBFS |
| 강하게 | −26 dBFS | 8:1 | 18 dB | −16 LKFS | −4 dBFS |

**시간 상수 (모든 단계 같음)** [코드 확인] (`meon/MeonEarGuard.cpp:20-24`)

| 대상 | 어택 | 릴리스 |
|---|---|---|
| 대역 레벨 측정 | 5 ms | 60 ms |
| K-가중 레벨 측정 | 40 ms | 40 ms (같은 값 = 40 ms 평균 파워) |
| 감쇠 적용 (대역·전체) | 1 ms | 150 ms |
| 피크 감쇠 | 즉시 | 120 ms |
| 단계 전환 | 기준값을 100 ms 동안 옮김 | |
| 동작 횟수 세기 | 1 dB 이상 줄이면 1회, 0.1 dB 안쪽으로 돌아오면 다시 셀 준비 | |

**측정 결과** [실행 확인] (48 kHz, 스테레오 같은 신호, 4초 중 마지막 3초)

| 입력 | 입력 RMS / 피크 (dBFS) | 끔 | 보통 | 강하게 |
|---|---|---|---|---|
| 핑크 노이즈 RMS −18 | −17.99 / −5.36 | 비트 단위 같음 | **비트 단위 같음** | −18.24 / −5.33 |
| 핑크 노이즈 RMS −14 | −14.02 / −0.96 | 같음 | −14.02 / −1.00 | −15.80 / −4.00 |
| 핑크 노이즈 RMS −10 | −9.91 / +2.55 | 같음 | −11.51 / −1.00 | −15.46 / −4.00 |
| 화이트 노이즈 풀스케일 | −4.76 / 0.00 | 같음 | −13.21 / −5.46 | −19.21 / −10.43 |
| 3 kHz 사인 0 dBFS (하울링 흉내) | −3.01 / 0.00 | 같음 | −14.26 / −11.28 | −19.12 / −16.23 |
| 1 kHz 사인 −20 dBFS | −23.01 / −20.00 | 같음 | **같음** | **같음** |
| 1 kHz 사인 0 dBFS | −3.01 / 0.00 | 같음 | −10.01 / −7.00 | −16.01 / −13.01 |

- 지연: 입력·출력 상호상관 최대 위치 **0 샘플**(강하게, 핑크 −10).
- 어택(무음 → 1 kHz 0 dBFS): 첫 10 ms 피크 보통 **−1.00 dBFS**, 강하게 **−4.00 dBFS**(피크 상한이 즉시 걸림). 50 ms 뒤 −6.18 / −13.93 dB, 200 ms 뒤 −7.16 / −13.60 dB.
- 릴리스(0 dBFS 1초 → −20 dBFS): 남은 감쇠 보통 10 ms −6.96, 100 ms −4.47, 300 ms −1.20, 1 s −0.01 dB / 강하게 10 ms −12.96, 100 ms −10.05, 300 ms −2.84, 1 s −0.03 dB.

| 항목 | 현재 | 확인 | 근거 |
|---|---|---|---|
| 앱·플러그인 차이 | DSP 는 같다. 플러그인은 **창을 열지 않아도** 프로세서가 만들어질 때 설정 파일에서 단계를 읽는다 | [코드 확인] | `SPP.cpp:851` |
| 설정 저장 | `MEONUser.settings` 의 `earGuard` = 0(끔) / 1(보통, 기본) / 2(강하게). 같은 PC 의 앱·플러그인이 공유. **실행 중인 다른 인스턴스에는 바로 전달되지 않고**, 그 인스턴스가 에디터를 다시 만들 때 읽음 | [코드 확인] | `meon/MeonSettings.cpp:84-93`, `meon/MeonSession.cpp:156` |
| 로그 | 입장 때·변경 때 이벤트, 5초 샘플에 단계·동작 횟수 | [실행 확인] 로그에 `earGuard: normal` 이벤트 | `meon/MeonSession.cpp:700-703`, `1320-1323` |

---

## 8. 파일 플레이어

| 항목 | 현재 | 확인 | 근거 |
|---|---|---|---|
| 지원 형식·디코더 | JUCE `registerBasicFormats()`: WAV·AIFF(JUCE 내장), FLAC(JUCE 동봉 libFLAC), Ogg Vorbis(JUCE 동봉 libvorbis) 공통 + macOS `CoreAudioFormat`(OS 디코더: MP3·M4A/AAC·CAF 등) + Windows `WindowsMediaAudioFormat`(OS 디코더: `.mp3 .wmv .asf .wm .wma`) + Linux JUCE MP3 디코더 | [코드 확인] | `SPP.cpp:836`, `deps/juce/modules/juce_audio_formats/format/juce_AudioFormatManager.cpp:54-78`, `deps/juce/modules/juce_audio_formats/codecs/juce_WindowsMediaAudioFormat.cpp:127`, `CMakeLists.txt:236-239`, `424` |
| 읽기 | 디스크 스레드로 65,536 샘플 미리 읽음, 파일 샘플레이트를 장치 샘플레이트로 변환 | [코드 확인] | `SPP.cpp:9603-9607`, `9658-9662` |
| **신호 경로 (앱, 송신 1·2채널)** | 파일 → `fileBuffer` → × 파일 볼륨 → **내 마이크 신호와 같은 송신 버퍼에 더함 → 같은 AOO 스트림**. 모노 송신이면 파일 채널을 1/채널수로 섞어 1채널에, 스테레오면 채널 그룹 패닝으로 2채널에 | [코드 확인] | `meon/MeonSession.cpp:311`, `SPP.cpp:7622-7685` |
| 신호 경로 (플러그인, 송신 "입력 수에 맞춤") | 송신 채널이 입력+파일 채널(보통 4)이 되고 파일은 **같은 스트림의 별도 채널(3·4번)** 로 간다 | [코드 확인] (플러그인 실행 못 함) | `SPP.cpp:7600-7604`, `7655-7672`, 3-2 |
| 받는 쪽이 파일 소리만 조절 | **불가능**. 앱이 보내면 한 소리로 섞여 도착한다. 플러그인이 보내 채널이 나뉘어도 MEON 받는 쪽 UI 는 멤버 단위 볼륨·패닝뿐이고 재생 중이라는 표시도 없다 | [코드 확인] | `meon/MeonSession.cpp:616-652` |
| 내가 듣는 볼륨 = 보내는 볼륨? | **같은 값 하나**(`mFilePlaybackChannelGroup.params.gain`)를 송신과 내 모니터에 똑같이 곱한다. 범위 −30 ~ +10 dB (−30 이하는 0) | [코드 확인] | `SPP.cpp:1749-1759`, `7646`, `7663`, `7678`, `8238`, `meon/MeonSession.cpp:852-862` |
| 송출 정지 때 | 송신 전체가 멈춰 파일도 안 나감. 나는 계속 들음 | [코드 확인] | `SPP.cpp:6732-6760` |
| 재생 위치 동기화 | **없음**. 재생·정지·위치는 내 엔진 안에서만. 다른 멤버에게 위치·상태를 보내는 메시지도 없다 | [코드 확인] | `meon/MeonSession.cpp:811-849` (엔진 transport 만 조작) |
| 방을 나가면 | 파일을 닫음 | [코드 확인] | `meon/MeonSession.cpp:539` |
| 끝까지 재생 | 엔진이 처음으로 되돌리고 정지 | [코드 확인] | `SPP.cpp:5256-5260` |

---

## 9. 채팅

| 항목 | 현재 | 확인 | 근거 |
|---|---|---|---|
| 전송 방식 | 서버를 거치지 않고 **각 피어에게 UDP 로 직접** OSC 메시지 `/sb/chat` (`s:group s:from s:targets s:tags s:message`). 받는 쪽은 그룹이 같고 보낸 사람이 내가 아닐 때만 표시 | [실행 확인] B 가 보낸 "hello meon" 이 A 에 표시 | `SPP.cpp:2433-2435`, `2866-2905`, `3229-3232`, `meon/MeonSession.cpp:877-896`, `1132-1147` |
| 신뢰성 | UDP 한 번 보냄. **재전송·확인 응답 없음**. 패킷이 사라지면 그 글은 상대에게 없다 | [코드 확인] | `SPP.cpp:3229-3232` |
| 최대 길이 | 별도 제한 없음. OSC 메시지 버퍼가 **4,096 바이트**(`AOO_MAXPACKETSIZE`)라 그룹·보낸 사람 이름·타입 태그를 빼면 메시지는 **약 4,000 바이트(UTF-8)** 까지 [계산: 주소 12 + 타입 8 + 그룹 8 + 이름 최대 약 44 + 빈 문자열 2개 8 바이트를 뺀 값]. 한글은 글자당 3바이트라 약 1,300자 | [실행 확인] 4,200자(4,200 B)는 실패 / 경계값은 [계산] | `SPP.cpp:2869-2884`, `deps/aoo/lib/aoo/aoo_types.h:39` |
| 실패 처리 | 엔진은 메시지를 못 만들면 `false` 를 돌려주지만 **MEON 은 결과를 보지 않고 내 채팅에 글을 추가**한다. 사용자에게 실패를 알리지 않는다 | [실행 확인] 4,200자: B 화면에만 보이고 A 는 못 받음, 바로 뒤 짧은 글은 정상 도착 | `meon/MeonSession.cpp:887-895` |
| 보관 | 메모리에만, 방을 나가면 지움. 늦게 들어온 사람은 이전 글을 못 봄 | [코드 확인] | `meon/MeonSession.cpp:550` |

---

## 10. 설정 저장

### 10-1. MEON 사용자 설정 (`MEONUser.settings`, 앱·플러그인 공유)

| OS | 경로 | 근거 |
|---|---|---|
| macOS | `~/Library/Application Support/MEON/MEONUser.settings` | `meon/MeonSettings.cpp:6-22` |
| Windows | `%APPDATA%\MEON\MEONUser.settings` | 같음 |
| Linux | `~/.config/meon/MEONUser.settings` [실행 확인] | 같음 |

형식: JUCE `PropertiesFile` **XML** (`<PROPERTIES><VALUE name=… val=…/>`). 바꾸면 대부분 바로 `save()`, 그 밖에 0.5 s 뒤 자동 저장 (`meon/MeonSettings.cpp:19-20`). [실행 확인: 파일 생성·내용]

| 키 | 값 | 기본값 | 근거 |
|---|---|---|---|
| `nickname` | 문자열 (2–12자) | `""` | `meon/MeonSettings.cpp:34-43` |
| `part` | `vocal`·`guitar`·`bass`·`drums`·`keys`·`other` | `""` (없음) | `meon/MeonSettings.cpp:45-54`, `meon/MeonParts.cpp:10-30` |
| `onboardingDoneApp` | 0/1 | 0 | `meon/MeonSettings.cpp:56-65` |
| `onboardingDonePlugin` | 0/1 | 0 | 같음 |
| `inputChannelStart` | 0 기반 정수 | 0 | `meon/MeonSettings.cpp:67-82` |
| `inputChannelCount` | 1 또는 2 | 1 | 같음 |
| `earGuard` | 0 / 1 / 2 | 1 (보통) | `meon/MeonSettings.cpp:84-93` |
| `chatOpen` | 0/1 | 1 | `meon/MeonSettings.cpp:95-103` |

[실행 확인] Linux 에서 `nickname`, `onboardingDoneApp`, `chatOpen`, `inputChannelCount` 를 읽고 쓰는 것을 봄. 나머지 [코드 확인].

### 10-2. 독립 앱 셸 설정 (`Meon.settings`, SonoBus 셸 그대로)

| OS | 경로 | 근거 |
|---|---|---|
| macOS | `~/Library/Application Support/MEON/Meon.settings` | `Source/SonoStandaloneFilterApp.cpp:160-170` |
| Windows | `%APPDATA%\Meon\Meon.settings` (JUCE 규칙: 폴더 이름이 비어 있으면 앱 이름 폴더. 대소문자 무시라 `MEON` 폴더와 같은 곳) | 같음, JUCE `PropertiesFile::Options::getDefaultFile` |
| Linux | `~/.config/meon/Meon.settings` [실행 확인] | 같음 |

키 [실행 확인] (Linux 에서 정상 종료한 뒤 파일에서 읽음): `audioSetup`(장치 XML), `filterStateXML`(엔진 상태 XML, 파라미터·최근 연결·피어 캐시 포함), `shouldOverrideSampleRate`, `shouldCheckForNewVersion`, `allowBluetoothInput`, `recentSetupFiles`, `lastRecentsSetupFolder`, `windowX`, `windowY`, `windowW`, `windowH` (`Source/SonoStandaloneFilterWindow.h:353-366`, `473-484`, `881`, `918`).
- 이 파일은 **정상 종료 때와 `saveSettingsIfNeeded()` 때만** 저장된다. 강제 종료(SIGTERM) 실험에서는 파일이 생기지 않았다 [실행 확인].
- MEON 은 켤 때마다 엔진 기본값(코덱·지터·이펙트·뮤트 해제 등)을 덮어쓰므로, 이 파일에 저장된 엔진 상태 중 상당수는 다음 실행에 쓰이지 않는다 (`meon/MeonSession.cpp:155`, `265-326`). [코드 확인]

### 10-3. 그 밖의 파일

| 파일 | 경로 | 근거 |
|---|---|---|
| 세션 로그 | macOS `~/Library/Logs/MEON/`, Windows `%APPDATA%\MEON\logs\`, Linux `~/.config/meon/logs/` [실행 확인]. 방마다 `meon-session-YYYYMMDD-HHMMSS.json` | `meon/MeonSettings.cpp:115-124`, `meon/MeonSessionLog.cpp:32-34` |
| 업데이트 기록 (macOS) | `~/Library/Logs/MEON/update.log` | `meon/MeonUpdater.cpp:104-114` |
| 플러그인 상태 | DAW 프로젝트 안 (엔진 상태 + 마지막 창 크기) | `meon/MeonEditor.cpp:69-72`, `400` |
| 단일 인스턴스 잠금 (Linux) | `/var/tmp/juceAppLock_Meon` [실행 확인] | `juce_SharedCode_posix.h:736-741` |

---

## 11. 플러그인 특이사항

### 11-1. 호스트에 노출되는 파라미터 (37개, `SPP.cpp:606-691`)

"MEON" 열: **씀** = MEON 이 읽거나 씀 / **고정** = MEON 이 켤 때 값을 덮어씀 / **안 씀**. "제안" 열은 이 표의 사실에서 나오는 분류일 뿐 결정이 아니다. [코드 확인]

| # | ID | 이름 | 범위·기본값 | MEON | 제안 | 근거 |
|---|---|---|---|---|---|---|
| 1 | `ingain` | In Gain | 0–4 선형, 1.0 | 안 씀 | 숨김 또는 UI 로 승격 | `SPP.cpp:606`, `SPP.h:968` |
| 2 | `inmonmonopan` | In Pan | −1–1, 0 | 안 씀 | 숨김 | `SPP.h:969` |
| 3 | `inmonpan1` | In Pan 1 | −1–1, −1 | 안 씀 | 숨김 | `SPP.h:970` |
| 4 | `inmonpan2` | In Pan 2 | −1–1, +1 | 안 씀 | 숨김 | `SPP.h:971` |
| 5 | `dry` | Dry Level (셀프 모니터) | 0–1, 앱 0 / 플러그인 1 | 안 씀 | 결정 필요 (5-2) | `SPP.cpp:622` |
| 6 | `wet` | Output Level (마스터) | 0–2, 1.0 | 안 씀 | 결정 필요 | `SPP.cpp:626` |
| 7 | `defnetbuf` | Default Jitter Buffer Time | 0–1 s, 0.001 | 안 씀 | 숨김 | `SPP.cpp:630` |
| 8 | `sendchannels` | Send Channels | Match/Mono/Stereo, 앱 1(Mono) / 플러그인 0(Match) | **씀 (앱만)** | 숨김 (플러그인 값 결정 필요, 3-2) | `SPP.cpp:633`, `meon/MeonSession.cpp:339-340` |
| 9 | `metenabled` | Metronome Enabled | bool, false | **고정 0** | 숨김 | `meon/MeonSession.cpp:309` |
| 10 | `sendmetaudio` | Send Metronome Audio | bool, false | **고정 0** | 숨김 | `meon/MeonSession.cpp:310` |
| 11 | `metgain` | Metronome Gain | 0–1, 0.5 | 안 씀 | 숨김 | `SPP.h:983` |
| 12 | `mettempo` | Metronome Tempo | 10–400, 100 | 안 씀 | 숨김 | `SPP.h:984` |
| 13 | `sendfileaudio` | Send Playback Audio | bool, false | **고정 1** | 숨김 | `meon/MeonSession.cpp:311` |
| 14 | `sendsoundboardaudio` | Send Soundboard Audio | bool, false | **고정 0** | 숨김 | `meon/MeonSession.cpp:312` |
| 15 | `hearlatencytest` | Hear Latency Test | bool, false | **고정 0** | 숨김 | `meon/MeonSession.cpp:313` |
| 16 | `metisrecorded` | Record Metronome to File | bool, true | 안 씀 | 숨김 | `SPP.h:988` |
| 17 | `mainreverbenabled` | Main Reverb Enabled | bool, false | **고정 0** | 숨김 | `meon/MeonSession.cpp:306` |
| 18 | `nmainreverblevel` | Main Reverb Level | 0–1, 1.0 | 안 씀 | 숨김 | `SPP.h:990` |
| 19 | `mainreverbsize` | Main Reverb Size | 0–1, 0.15 | 안 씀 | 숨김 | `SPP.h:991` |
| 20 | `mainreverbdamp` | Main Reverb Damping | 0–1, 0.5 | 안 씀 | 숨김 | `SPP.h:992` |
| 21 | `mainreverbpredelay` | Pre-Delay Time | 0–100 ms, 20 | 안 씀 | 숨김 | `SPP.h:993` |
| 22 | `mainreverbmodel` | Main Reverb Model | Freeverb/MVerb/Zita, MVerb | 안 씀 | 숨김 | `SPP.h:994` |
| 23 | `mastsendmute` | Main Send Mute | bool, false | **씀** (마이크 켜짐/꺼짐) + 켤 때 0 으로 고정 | **노출 유지 후보** (DAW 에서 자동화) — 단 10장 #1 | `meon/MeonSession.cpp:314`, `721-728` |
| 24 | `mastrecvmute` | Main Receive Mute | bool, false | **고정 0** | 숨김 | `meon/MeonSession.cpp:307` |
| 25 | `mastinmute` | Main In Mute | bool, false | **고정 0** | 숨김 | `meon/MeonSession.cpp:308` |
| 26 | `mastmonsolo` | Main Monitor Solo | bool, false | 안 씀 | 숨김 | `SPP.cpp:668` |
| 27 | `defnetauto` | Def Auto Net Buffer Mode | Off/Auto-Increase/Auto-Full/Initial-Auto, Auto-Full | **고정 Auto-Full** (함수로) | 숨김 | `meon/MeonSession.cpp:285`, `SPP.h:1023` |
| 28 | `defsendqual` | Def Send Format | 0–14, 4 | **고정 8 (PCM16)** (함수로) | 숨김 | `meon/MeonSession.cpp:281` |
| 29 | `dynamicresampling` | Dynamic Resampling | bool, false | 안 씀 | 숨김 | `SPP.h:995` |
| 30 | `reconnectlast` | Reconnect Last | bool, false | 안 씀 | 숨김 | `SPP.h:996` |
| 31 | `defPeerLevel` | Default User Level | 0–1, 1.0 | 안 씀 | 숨김 | `SPP.h:997` |
| 32 | `syncMetHost` | Sync to Host | bool, 앱 false / 플러그인 true | 안 씀 | 숨김 | `SPP.cpp:678` |
| 33 | `inreverblevel` | Input Reverb Level | 0–1, 1.0 | 안 씀 | 숨김 | `SPP.h:1002` |
| 34 | `inreverbsize` | Input Reverb Size | 0–1, 0.15 | 안 씀 | 숨김 | `SPP.h:1003` |
| 35 | `inreverbdamp` | Input Reverb Damping | 0–1, 0.5 | 안 씀 | 숨김 | `SPP.h:1004` |
| 36 | `inreverbpredelay` | Input Reverb Pre-Delay Time | 0–100 ms, 20 | 안 씀 | 숨김 | `SPP.h:1005` |
| 37 | `syncMetFile` | Sync Met to File Playback | bool, false | 안 씀 | 숨김 | `SPP.cpp:691` |

MEON 의 멤버별 볼륨·패닝·뮤트, 파일 볼륨, 귀 테러 방지는 **호스트 파라미터가 아니다**(자동화 불가). [코드 확인]

### 11-2. 입출력 채널 구성 (`SPP.cpp:548-596`, `7186-7215`) [코드 확인]

| 버스 | 구성 | 기본 활성 |
|---|---|---|
| 입력 "Main In" | 스테레오 | 켬 |
| 출력 "Mix Out" | 스테레오 | 켬 |
| 입력 "Aux 1 In" ~ "Aux 8 In" | 스테레오 × 8 (AU·VST3), AAX 는 모노 1개, VST2 는 없음 | **끔** (`ALTBUS_ACTIVE = false`, iOS 만 켬) |
| 출력 "Aux 1 Out" ~ "Aux 8 Out" | 스테레오 × 8 (VST2 제외) | 끔 |
| 지원 레이아웃 | VST2 만 메인 입출력 모노/스테레오/꺼짐으로 제한, 나머지는 모두 허용 | |

- MIDI 입력·출력 요구 켬 (`NEEDS_MIDI_INPUT/OUTPUT TRUE`, `CMakeLists.txt:151-152`). AU 타입 MusicEffect (`CMakeLists.txt:171`).
- 귀 테러 방지는 메인 버스 출력 채널에만 (`SPP.cpp:8272`).

### 11-3. 호스트 샘플레이트·블록

| 항목 | 현재 | 근거 |
|---|---|---|
| 샘플레이트 | `prepareToPlay` 의 값으로 엔진·귀 테러 방지 준비. 귀 테러 방지는 처리 중 샘플레이트가 바뀌어도 계수를 다시 계산 | `SPP.cpp:6918-6926`, `meon/MeonEarGuard.cpp:241-246` |
| 블록 | `prepareToPlay` 값으로 시작, 실제 블록이 30번 연속 다르면 `currSamplesPerBlock` 갱신 후 송신 형식 재설정. 화면에 보이는 블록도 이 값 | `SPP.cpp:7369-7382`, `3293-3300`, `meon/SettingsScreen.cpp:523-524` |
| 플러그인 창을 닫아도 | 엔진은 계속 방에 있고 소리도 계속 (에디터만 사라짐). 다시 열면 상태를 이어받지만 **기본값을 다시 덮어씀**(뮤트 해제 — 12장 #1) | `meon/MeonSession.cpp:155-177`, `182-197` |

[코드 확인] (플러그인 실행 못 함)

---

## 12. 리메이크 때 같이 고쳐야 할 버그 (엔진·로직 쪽)

`docs/FEATURES_AS_IS.md` 10장 중 엔진·로직 항목 + 이번에 새로 확인한 것. **"UI 교체 후에도 남음"** = 문제 코드가 UI 파일이 아니라 로직·엔진·셸에 있어서, 화면을 새로 만들어도 그대로 남는다는 뜻.

| # | 문제 | 위치 | UI 교체 후에도 남음? | 확인 |
|---|---|---|---|---|
| 1 | 플러그인 창을 다시 열거나 앱을 다시 켜면 **송출 정지(뮤트)가 풀린다** (`applyProcessorDefaults()` 가 매번 `mastsendmute = 0`) | `meon/MeonSession.cpp:155`, `314` | **남음** | 앱 재실행 [실행 확인] / 플러그인 [코드 확인] |
| 2 | 멤버 **'연결 끊김' 상태가 사실상 생기지 않는다**: `connected` 가 false 로 바뀌는 곳이 NAT 실패뿐, 30초 정리도 NAT 실패에만 | `meon/MeonSession.cpp:1074-1097`, `1233-1253` | **남음** | [코드 확인] |
| 3 | 끊김 횟수 `dropCount` 를 늘리는 코드 없음 (로그 항상 0) | `meon/MeonSession.h:38`, `meon/MeonSession.cpp:1304` | **남음** | [실행 확인] 로그 값 0 |
| 4 | 초대 코드 = **SonoBus 공개 서버의 그룹 이름**, 비밀번호 없음, 중복 확인 없음 (아무 SonoBus 사용자나 같은 이름으로 들어올 수 있음) | `meon/MeonSession.cpp:200-208`, `489` | **남음** | [코드 확인] |
| 5 | '없는 코드' 판정이 "4초 안에 피어가 안 보임" — 늦으면 진짜 방도 "없음"으로 처리하고, 판정 중 서버에 빈 그룹을 만든다 | `meon/MeonSession.cpp:1028-1030`, `1182-1184` | **남음** | 동작 [실행 확인] / 오판 가능성 [코드 확인] |
| 6 | 5·6번째 동시 입장 경쟁: 입장 후 3초 안 인원으로 판정해 둘 다 나갈 수 있음. 정원 상수가 UI 테마 파일에 있음 | `meon/MeonSession.cpp:1099-1111`, `meon/MeonTheme.h:44-45` | **남음** | 순차 입장 [실행 확인] / 동시 [코드 확인] |
| 7 | 입장 코드 검사가 0·1·O·I 를 받는다 (생성에는 안 쓰는 글자) | `meon/MeonSession.cpp:210-221` (+ 입력 위젯 `meon/MeonWidgets.cpp:544-547`) | **남음** (로직 쪽 검사) | [실행 확인] |
| 8 | **채팅 길이 제한 없음**, 약 4,000 B 를 넘으면 상대에게 안 가는데 내 화면에는 보낸 것으로 표시 | `meon/MeonSession.cpp:877-896` | **남음** | [실행 확인] |
| 9 | 채팅이 UDP 한 번 전송(재전송 없음) — 손실 시 조용히 사라짐 | `SPP.cpp:2866-2905`, `3229-3232` | **남음** (엔진) | [코드 확인] |
| 10 | 새 채팅 수(`unread`)를 세지만 쓰는 곳 없음 | `meon/MeonSession.h:148` | UI 에서 쓰면 해결 | [코드 확인] |
| 11 | 서버 핑 스레드가 TCP 접속을 주기적으로 하지만 값은 로그에만 | `meon/MeonSession.cpp:116-149` | **남음** | [코드 확인] |
| 12 | 서버 재접속 실패가 1초마다 `serverLost` 이벤트로 쌓임 (로그 부풀림) | `meon/MeonSession.cpp:1004-1009` | **남음** | [실행 확인] 9초에 10개 |
| 13 | **플러그인 송신 채널이 "입력 수에 맞춤"이라 파일 채널까지 더해 보통 4채널** → 앱(모노 1채널)의 약 4배 대역폭 | `meon/MeonSession.cpp:330-331`, `SPP.cpp:7028-7049`, `5220-5234` | **남음** | [코드 확인] (실측 못 함) |
| 14 | 송출 정지 상태에서도 송신 버퍼·미터 계산은 계속 (MEON 이 값을 −100 으로 가림) | `meon/MeonSession.cpp:869-874` | 남음 (동작상 문제는 아님) | [코드 확인] |
| 15 | 장치·버퍼를 합주 중 바꾸면 장치를 다시 열어 소리가 끊기고 지터 버퍼가 커짐 (경고·되돌리기 없음) | `meon/MeonAudio.cpp:105-126` | **남음** | [실행 확인] (JACK 경로, 4-1) |
| 16 | 업데이트: 켤 때 한 번만 확인, Windows 설치 파일 서명 검증 없음, macOS 는 ad-hoc 서명 무결성만 확인. 설치·재시작 흐름이 에디터(`MeonEditor::installUpdate`)에 있음 | `meon/MeonEditor.cpp:83-88`, `226-243`, `meon/MeonUpdater.cpp:420-446` | **남음** (흐름은 옮겨야 함) | [코드 확인] |
| 17 | 엔진 파라미터 37개가 그대로 호스트에 노출 (MEON UI 에 없는 마스터·셀프 모니터·리버브 등을 DAW 가 바꿀 수 있음) | `SPP.cpp:606-691` | **남음** (엔진) | [코드 확인] |
| 18 | 귀 테러 방지 단계 변경이 실행 중인 다른 인스턴스에 전달되지 않음 | `meon/MeonSession.cpp:156`, `711-719` | **남음** | [코드 확인] |
| 19 | 피어 캐시가 사용자 이름 앞부분(공백 앞)으로도 맞춰진다. 닉네임에 공백이 있으면 다른 사람의 볼륨·지터 설정이 적용될 수 있음 | `SPP.cpp:6177-6215` | **남음** (엔진) | [코드 확인] |
| 20 | 셸이 `meon::MeonEditor` 에 직접 묶여 있음(캐스팅) — 새 에디터로 바꾸면 셸도 고쳐야 함 | `Source/SonoStandaloneFilterApp.cpp:555-558`, `874-879`, `Source/SonoStandaloneFilterWindow.h:1061-1063` | **반드시 같이 수정** | [코드 확인] |
| 21 | 개발용 실행 옵션(`--create`, `--join`, `--snapshot` 등)과 자동 방 동작이 배포 빌드에 들어 있고, 일부 로직이 에디터에 있다 | `Source/SonoStandaloneFilterApp.cpp:498-509`, `555-580`, `meon/MeonEditor.cpp:308-365` | **남음** (셸) — 리메이크 테스트에도 유용하므로 유지 여부 결정 필요 | [실행 확인] (이번 실험이 이 옵션을 씀) |

UI 쪽에만 있는 문제(첫 실행 3초 오표시 등)는 `docs/FEATURES_AS_IS.md` 10장에 그대로 있다.

---

## 확인 못 한 것

| # | 항목 | 이유 |
|---|---|---|
| 1 | 진짜 서버(`aoo.sonobus.net`)·인터넷 환경에서의 접속, 핑, 손실, NAT 통과/실패 | 컨테이너에서 서버 DNS 조회 실패. 로컬 서버·같은 PC 루프백으로만 확인 |
| 2 | 플러그인(AU·VST3)의 송신 채널 수(4채널 추정)와 그 대역폭 | 플러그인 호스트가 없다 |
| 3 | 플러그인 창을 다시 열 때 뮤트가 풀리는 것 | 같은 이유 |
| 4 | 호스트 파라미터 노출이 DAW 화면에 실제로 어떻게 보이는지 | 같은 이유 |
| 5 | macOS·Windows 장치(ASIO·CoreAudio)에서 버퍼 변경·드라이버 패널 | Linux 컨테이너 |
| 6 | MEON 설정 화면 경로(장치 재시작)로 합주 중 버퍼를 바꿀 때의 패킷 크기 변화 | JACK `jack_bufsize` 로만 바꿔 봤고, 그때 패킷 블록은 128 샘플 그대로였다 |
| 7 | 스테레오 64·256 샘플, 44.1 kHz 대역폭 | 계산만 했다 |
| 8 | 5명·7명이 동시에 연결된 상태의 총 대역폭 실측 | 5명 연결은 만들었지만 대역폭은 재지 않았고, 7명은 정원 때문에 불가 |
| 9 | 채팅 최대 길이의 정확한 경계값 | 4,200 B 실패만 확인, 경계는 계산값 |
| 10 | Opus 로 바꿨을 때의 실제 대역폭·지연 | 코드 수정 금지 |
| 11 | 셀프 모니터(Dry)·마스터(Output Level)·입력 게인의 실제 소리 | 오디오 출력을 들을 수 없음 |
| 12 | 자동 업데이트 실제 동작 | Linux 미지원, 로컬 빌드 번호 0 |
