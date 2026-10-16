# MEON 데스크톱 UI 리메이크 계획

이 문서는 계획서다. 승인되면 아래 10장의 순서대로 `remake` 브랜치에 PR을 하나씩 올린다.

**기준 자료**
- 디자인: `docs/design/MEON_Lab.dc.html` (화면·문구·수치·동작의 기준. 마크업 + 상태 JS 전부 읽음)
- API: `docs/API.md` v0.4 (수정 금지. 문제는 11장 질문으로)
- 현황: `docs/FEATURES_AS_IS.md`, `docs/ENGINE_AS_IS.md` (브랜치 `docs/as-is`, 이하 FEAT·ENG)
- 지시서의 기획 결정과 추가 반영 4가지(Connection 사용자 정보·멤버 필터, RTT 1분 보고, 방 안 친구 초대 버튼 숨김·입장 시 초대 소멸, 송신 레벨 기록·무음 경고)

**표기**: `meon/` = `Source/meon/`, `SPP` = `Source/SonobusPluginProcessor`. "디자인" = MEON_Lab, "(Q n)" = 11장 질문 번호. 질문의 답이 오기 전에는 그 항목에 적은 **잠정안**대로 만든다.

---

## 0. 원칙

1. **화면은 전부 새로 만든다. 엔진·AOO·코덱·네트워크 코드는 허용된 곳만 고친다.** 허용 범위를 넘는 엔진 수정 2건은 8장에 이유와 함께 적고 승인을 받는다.
2. **UI는 로직을 모른다.** UI는 `core`가 내주는 상태(읽기)와 명령(쓰기)만 쓴다. `core`는 UI 헤더를 포함하지 않고, 화면 문구도 만들지 않는다. 오류는 코드(`apperr` 같은 열거형)로 넘기고, 한국어 문구는 UI가 정한다 (ENG 1-2의 `getServerStatusText`·`driverLabel` 문제 해결).
3. **서버는 인터페이스 뒤에 숨긴다.** 지금은 가짜 구현(`FakeMeonServer`)을 쓰고, 서버가 생기면 진짜 구현(`HttpMeonApi` + `WsRealtime`)으로 바꾼다. **가짜 구현도 API.md와 같은 모양의 JSON을 만들어서 진짜와 같은 디코더를 지나게 한다.** 화면 코드는 바꿀 필요가 없다.
4. **합주 오디오는 진짜다.** 엔진에 넘기는 서버 주소·그룹·비밀번호·사용자 이름은 전부 `Connection`(API 2.8)에서 읽는다. `DEFAULT_SERVER_HOST`·`DEFAULT_SERVER_PORT`를 쓰는 곳은 없앤다.
5. **스레드 규칙은 지금과 같다.** `core` 공개 함수는 메시지 스레드에서만 부른다. 엔진 콜백(네트워크 스레드)은 큐에 넣어 메시지 스레드로 넘긴다 (ENG 1-3).
6. **독립 앱과 플러그인은 같은 화면을 쓴다.** 차이는 `Platform` 정보(플러그인 여부, 창 크기)로 화면이 분기하고, 장치·업데이트 같은 OS 차이는 `core` 아래 계층에서 처리한다.

---

## 1. 새 구조

### 1.1 디렉터리와 클래스

```
Source/meon/
├── core/                        # 로직. UI·문구 없음. 단위 테스트 대상
│   ├── MeonCore.h/.cpp          # 한 프로세서(앱 1개, 플러그인 인스턴스 1개)당 하나. 아래 서비스를 소유·연결
│   ├── api/
│   │   ├── ApiTypes.h           # API.md 2장 모델을 그대로 옮긴 구조체 (Me, Room, RoomDetail, Connection, Friend …)
│   │   ├── ApiJson.h/.cpp       # juce::var(JSON) ↔ 구조체. snake_case, ISO 8601, ID 문자열 그대로
│   │   ├── ApiError.h           # API 1.5 오류 코드 열거형 + HTTP 상태 (서버의 apperr 표와 같은 목록)
│   │   ├── MeonApi.h            # 서버 인터페이스 (1.3)
│   │   ├── Realtime.h           # 실시간 이벤트 인터페이스 (1.4)
│   │   ├── fake/FakeMeonServer.*   # 가짜 구현: MeonApi + Realtime 둘 다. 메모리 DB + 타이머
│   │   ├── fake/FakeWorld.*        # 가짜 다른 사용자·공개 방·친구 (fixtures/*.json 에서 읽음)
│   │   ├── fake/FakeAooDirectory.* # 가짜 기간에 실제 AOO 상대를 방 멤버로 올리는 다리 (1.5)
│   │   └── fake/fixtures/*.json    # API.md 예시 모양 그대로의 초기 데이터 (BinaryData 로 넣음)
│   ├── state/
│   │   ├── AppState.h/.cpp      # 화면이 읽는 상태 하나: me, config, 공개 방, 친구, 알림 점, 받은 초대, 현재 방, 승인 요청, 연결 상태
│   │   └── AppStore.h/.cpp      # REST 응답·실시간 이벤트를 AppState 에 반영하고 변경을 알림 (ChangeBroadcaster 대신 키별 리스너)
│   ├── auth/
│   │   ├── AuthController.*     # 로그인·가입·인증·온보딩 단계 판정 (API 3.4: me 를 보고 빠진 단계부터)
│   │   └── TokenStore.*         # refresh 토큰 보관. macOS 키체인, Windows 자격 증명 관리자 (Q21)
│   ├── jam/
│   │   ├── EngineLink.*         # SonobusAudioProcessor 호출·ClientListener 를 감싸는 얇은 층. 엔진을 직접 부르는 곳은 여기뿐
│   │   ├── JamSession.*         # 합주: Connection 으로 접속, 피어 상태, 멤버 필터, 믹스(볼륨·패닝·뮤트), 송출·모니터, 파일 재생, 채팅
│   │   ├── PeerState.h          # 멤버별 P2P 상태: 연결 중 / 연결됨 / 끊김(소리 안 옴) / 연결 실패 (버그 #2, #3)
│   │   ├── ChatChannel.*        # 길이 제한 + 전달 확인(ack) + 실패 표시 (버그 #8, 6.2)
│   │   ├── SilenceDetector.*    # 입력 무음 5초 감지 (추가 반영 4)
│   │   └── RttReporter.*        # AOO 서버 RTT 를 재서 1분마다 POST /me/rtt (추가 반영 2, 버그 #11)
│   ├── audio/
│   │   ├── MeonAudio.*          # 장치·드라이버·버퍼 (기존, 문구 함수만 UI 로 이동)
│   │   └── MeonEarGuard.*       # 그대로
│   ├── update/
│   │   ├── MeonUpdater.*        # 그대로 (받기·교체)
│   │   └── UpdateController.*   # 켤 때 + 6시간마다 확인, 버튼 눌렀을 때만 받기, 합주 중 막기, min/latest_build 판정 (버그 #16)
│   ├── settings/MeonSettings.*  # 설정 파일 (키 추가, 4.3)
│   ├── log/MeonSessionLog.*     # 그대로 + 기록 항목 추가 (송신 레벨, 무음, 재접속 요약)
│   ├── log/AppLog.*             # 진단 로그 logs/app.log (플러그인 plugin.log): 시작·종료, 로그인 단계, 토큰 갱신·세션 끊김 이유, 실시간 연결, 저장 실패. 줄마다 바로 쓴다
│   ├── MeonCrashDump.*          # Windows 독립 앱이 죽으면 미니덤프를 %APPDATA%\MEON\crashes 에 (레지스트리·관리자 권한 없이). PDB 는 Actions 아티팩트
│   └── model/
│       ├── Parts.*              # 파트 데이터만 (key·이름·코드). 그리기는 ui 로
│       ├── Validation.*         # API 3.1 입력 규칙 (NFC 코드포인트, 이모지, 비밀번호 ASCII …). 서버와 같은 규칙
│       ├── RoomFilter.*         # API 5.2 홈 필터
│       └── Latency.*            # API 2.11 예상 지연
├── ui/
│   ├── theme/                   # MeonTheme(토큰), MeonLookAndFeel, 글꼴(Pretendard), 아이콘(디자인 SVG 경로)
│   ├── widgets/                 # 4장 컴포넌트들
│   ├── screens/                 # OnboardingScreen, HomeScreen, JamScreen
│   ├── overlays/                # 모달·팝오버·토스트·배너 (3장 표)
│   ├── AppRoot.*                # 화면 전환, 모달 스택, 토스트 스택, 키보드 단축키
│   └── MeonEditor.*             # 새 에디터. 크기 제약, ShellHooks 구현만
└── shell/ShellHooks.h           # 셸(SonoStandaloneFilterApp/Window)이 에디터에 바라는 것을 추상 인터페이스로 (버그 #20)
```

- 기존 `Source/meon/*` 파일은 2장 표대로 옮기거나 지운다. 옛 UI 파일은 합주 화면이 들어오는 단계(10장 R6)에서 지운다.
- 네임스페이스는 `meon`, 하위 `meon::core`, `meon::ui`.

### 1.2 UI와 로직의 경계

| 방향 | 방법 |
|---|---|
| UI → core (읽기) | `AppState` 의 값(구조체 사본)과 키별 변경 알림. 예: `state.rooms()`, `state.friends()`, `state.currentRoom()` |
| UI → core (명령) | 컨트롤러 함수. 비동기는 `std::function` 콜백 하나로 결과(`Result<T>` = 값 또는 `ApiError`)를 메시지 스레드에 돌려준다 |
| core → UI | `AppStore` 리스너(상태 키별), `JamSession::Listener`(멤버·통계·채팅·파일), `UpdateController` 상태 |
| 미터 | 오디오 스레드가 쓰는 원자 값을 UI 가 화면 주사율(VBlank)로 읽는다. 지금과 같다 |
| 문구 | core 는 문구를 만들지 않는다. `ApiError` → 문구 표, `PeerState` → 문구 표는 `ui/` 에 있다 |

`core` 는 JUCE 의 `juce_core`·`juce_events`·`juce_audio_*` 만 쓰고 `juce_gui_*` 를 쓰지 않는다. 테스트 타깃에서 GUI 없이 돈다.

### 1.3 서버 인터페이스 (`MeonApi`)

모든 함수는 메시지 스레드에서 부르고, 결과는 메시지 스레드의 콜백으로 온다. `Result<T>` 는 성공 값 또는 `ApiError{code, httpStatus, message, field}` 다. 오류 코드 목록은 API 1.5 표와 같다.

| 영역 | 함수 | API |
|---|---|---|
| 설정 | `getConfig()` → `Config` | 13장 |
| 계정 | `usernameAvailable(u)`, `signup(email, u, pw, agreed)` → `AuthResult{tokens, me}`, `login(email, pw)`, `refresh()`, `logout()`, `verifyEmail(code)` → `Me`, `resendVerification()` | 3장 |
| 내 계정 | `getMe()`, `patchMe(MePatch)`, `changePassword(cur, new)`, `requestEmailChange(newEmail, pw)`, `verifyEmailChange(code)`, `deleteAccount(pw)`, `getUser(id)` → `UserProfile` | 4장 |
| 방 | `listPublicRooms()`, `createRoom(CreateRoom)` → `RoomDetail`, `getRoom(id)`, `joinRoom(id)` → `JoinResult{room | requestedUntil}`, `joinByCode(code)`, `leaveRoom(id)`, `reportRtt(ms)`, `sendLatencyTelemetry(…)` | 5장 |
| 승인 요청 | `listJoinRequests(roomId)`, `acceptJoinRequest(roomId, rid)`, `declineJoinRequest(roomId, rid)` | 5.7 |
| 방장 | `patchRoom(id, RoomPatch)`, `transferHost(id, userId)`, `kick(id, userId)`, `closeRoom(id)` | 6장 |
| 친구 | `listFriends()`, `sendFriendRequest(username)` → `FriendRequestResult`, `listFriendRequests()`, `acceptFriendRequest(id)`, `declineFriendRequest(id)`, `removeFriend(userId)` | 7.1~7.6 |
| 초대 | `sendInvite(userId)` → `{id, deliverAt}`, `cancelInvite(id)`, `acceptInvite(id)` → `JoinResult`, `dismissInvite(id)` | 7.7~7.10 |
| 차단 | `listBlocks()`, `block(userId)`, `unblock(userId)` | 8장 |
| 알림 | `notificationState()`, `markNotificationsRead()` | 9장 |

- 토큰·`X-Meon-Build` 헤더·401 → refresh → 한 번 재시도·426 처리는 진짜 구현의 공통 층(`HttpMeonApi::send`)에 있다. 가짜 구현도 같은 규칙을 흉내 낸다 (빌드 번호 검사, 토큰 만료 시뮬레이션은 개발 옵션).
- 진짜 구현은 서버가 생긴 뒤 별도 단계(10장 R11)에서 만든다. HTTP 는 JUCE `URL`(`withHttpRequestCmd` 로 PATCH·DELETE), WebSocket 은 JUCE 에 없어서 라이브러리가 필요하다 (Q23).

### 1.4 실시간 이벤트 (`Realtime`, API 10장)

```cpp
struct Realtime {
    enum class Link { connecting, online, reconnecting };
    struct Listener {
        virtual void linkChanged (Link) {}
        virtual void hello (const Hello&) {}                 // 연결·재연결·resync 때 전체 상태 (10.4)
        virtual void event (const RealtimeEvent&) {}         // 10.6 표의 모든 type. data 는 디코드된 구조체
    };
    virtual void connect (Credentials) = 0;
    virtual void subscribePublicRooms (bool) = 0;            // 홈 화면이 보일 때만 (10.5)
    virtual void resync() = 0;
};
```

- `AppStore` 가 `hello` 로 상태를 통째로 덮어쓰고, 이벤트로 부분 갱신한다. `seq` 빈 번호 감지 → `resync()` 는 진짜 구현 안에서 한다.
- 이벤트와 REST 응답이 다르면 REST 가 이긴다 (10.7): 명령의 응답이 오면 그 값으로 다시 덮어쓴다.

**가짜 구현이 이벤트를 흉내 내는 방법**

`FakeMeonServer` 는 메모리 DB(사용자, 방, 친구, 요청, 초대, 차단)와 `juce::Timer` 하나를 가진다. 서버 규칙(정원, 승인, 3초 초대, 60초 만료, 차단 필터, 숨은 친구 요청 기록 …)을 서버 계획서와 같은 규칙으로 아주 작게 구현하고, 변화가 생기면 `Realtime::Listener` 에 같은 이벤트를 보낸다.

| 이벤트 | 가짜 구현에서 일어나는 때 |
|---|---|
| `rooms.added/updated/removed` | 내가 방을 만들거나 바꿀 때 + `FakeWorld` 의 각본(가짜 사용자 입장·방 생성·닫힘)이 주기적으로 |
| `room.updated` | 나·가짜 사용자 입장·퇴장, 설정 변경, 방장 위임, 가짜 기간의 실제 AOO 상대 입장·퇴장 (1.5) |
| `room.kicked`, `room.closed` | 개발 옵션으로 발생 (가짜 방장이 나를 내보냄 / 방을 닫음) |
| `join_request.received/removed/accepted` | 내가 방장인 승인 방에 가짜 사용자가 요청 → 60초 만료, 내가 승인 요청 → 가짜 방장이 몇 초 뒤 수락 |
| `friend.*`, `friend_request.received` | 가짜 친구의 상태가 각본대로 바뀜, 가짜 사용자가 친구 요청 |
| `invite.received/expired` | 가짜 친구가 초대(보낸 지 3초 뒤 전달), 방 닫힘·내 입장 때 소멸 |
| `config.updated` | 개발 옵션 |
| `hello` | 앱을 켤 때, 개발 옵션의 "연결 끊김"이 끝날 때 |

- 연결 끊김(`Link::reconnecting`)·재연결도 개발 옵션으로 흉내 낸다. 끊긴 채 `--screen=home:disc`·`jam:disc`, 4초 끊겼다 다시 붙음 `home:discback`·`jam:discback`, 오래 끊겨 방에서 나옴 `home:roomlost`. 뒤에 `invite` 를 붙이면 초대 토스트가 먼저 떠 있는 경우 (`home:discinvite` 등).
- 각본 속도는 개발 옵션으로 조절한다 (배포 빌드에서 빠짐, 6장 버그 #21).
- 가짜 데이터의 사용자 이름·방 이름은 디자인의 예시 데이터를 쓴다.

### 1.5 가짜 기간의 실제 합주 (Connection)

서버가 없으니 두 컴퓨터의 가짜 서버는 서로를 모른다. 그래도 합주 오디오는 진짜로 동작해야 하므로 이렇게 한다 (Q24).

| `Connection` 필드 | 가짜 구현 값 |
|---|---|
| `aoo_host`, `aoo_port` | 지금 쓰는 `aoo.sonobus.net`, `10998` (개발 옵션으로 바꿀 수 있음. 로컬 aooserver 시험용) |
| `group_name` | 비공개 방의 6자리 입장 코드 그대로 (지금 앱과 같은 방식) |
| `group_password` | `SHA-256("meon-fake:" + 코드)` 앞 32자. 두 컴퓨터가 같은 값을 계산하고, 비밀번호 없이 들어오는 옛 MEON·SonoBus 와는 섞이지 않는다 |
| `aoo_username` | 이 설치의 가짜 계정 ID (`u_` + 무작위 UUID, 설정 파일에 저장) |
| `aoo_user_password` | 이 설치에서 한 번 만든 무작위 값 (설정 파일에 저장) |
| `members` | 내 ID + **실제로 AOO 그룹에 들어온 상대의 ID** (`FakeAooDirectory` 가 엔진의 피어 입장 이벤트를 받아 가짜 방 멤버로 올림) |

- 상대의 닉네임·파트는 AOO 이름(계정 ID)으로 알 수 없으므로, 가짜 서버에서만 채팅 메시지의 `tags` 로 "프로필" 메시지(`{"nickname","part"}`)를 주고받는다 (엔진 수정 없음). 진짜 서버에서는 `RoomDetail.members` 가 대신한다.
- **R11 뒤에도 남긴다 (2026-10-15 결정).** 빌드 0(PR 빌드)은 가짜 서버가 기본이라, 서버 없이 PR 빌드로 두 컴퓨터 합주를 시험하는 데 이 방식이 필요하다. 실제 번호 빌드는 가짜 서버를 고를 수 없다 (`ServerConfig::choose`).
- **할 일: 운영 서버 + 실제 번호 빌드로 두 대 합주를 확인한 뒤 제거** — 가짜 AOO 주소 `aoo.sonobus.net:10998` (`FakeMeonServer::Options`, `ServerConfig::fake`), `FakeAooDirectory`, 프로필 채팅 메시지, 채팅 받을 사람에서 가짜 각본 멤버를 빼는 판정 (`JamSession::applyAllowed` 의 `isScriptedUser` 필터, `FakeMeonServer::isScriptedUser`·`User::scripted`). 10장 "가짜용 합주 코드 제거".
- 공개 방은 다른 컴퓨터의 목록에 나타나지 않는다. **가짜 기간에 두 컴퓨터로 소리를 확인할 때는 비공개 방 + 입장 코드**로 한다. 공개 방 목록은 가짜 데이터로 화면만 확인한다.

### 1.6 프로세스 모델

- `MeonCore` 하나 = 로그인 1개 + 실시간 연결 1개 + `JamSession` 1개.
- **독립 앱**: 프로세서 1개 → `MeonCore` 1개.
- **플러그인**: 인스턴스마다 `MeonCore` 1개. 플러그인 창(에디터)을 닫아도 `MeonCore` 는 살아 있어야 한다(방에 있는 동안 추방·방 닫힘·초대 이벤트, RTT 보고, 소리). 그래서 `MeonCore` 를 에디터가 아니라 프로세서가 소유한다 → **엔진 수정 E2** (8장).
- 같은 계정으로 앱과 플러그인, 또는 플러그인 여러 개를 동시에 켜면 서버는 WebSocket 을 하나만 허용한다 (API 11장) → 처리 방식은 Q20.

---

## 2. 기존 파일별 처리 (ENG 1-2 기준)

| 파일 | 처리 | 내용 |
|---|---|---|
| `meon/MeonEarGuard.*` | **재사용** | `core/audio/` 로 이동. 코드 그대로 |
| `meon/MeonSettings.*` | **재사용 + 키 추가** | `core/settings/`. 4.3 의 키 추가. 다른 인스턴스 변경 감지(버그 #18)를 위해 파일 수정 시각 확인 함수 추가 |
| `meon/MeonSessionLog.*` | **재사용 + 항목 추가** | `core/log/`. 송신 레벨, 무음 구간, 재접속 요약, 멤버 끊김 (6장) |
| `meon/MeonAudio.*` | **재사용, 문구 분리** | `core/audio/`. `driverLabel()` 의 한국어 문구를 `ui/` 로 |
| `meon/MeonUpdater.*` | **재사용, 흐름 분리** | `core/update/`. `MeonTheme.h` 포함 제거. 받기 끝 → 설치 → 종료 흐름을 `MeonEditor::installUpdate` 에서 `UpdateController` 로 옮김 (버그 #16) |
| `meon/MeonSession.*` | **분리 후 대체** | 엔진 호출은 `EngineLink`, 합주 로직은 `JamSession`, 서버 연결·방 코드 판정은 서버 API 로 대체. 사용자 이름에 파트를 싣는 방식, 코드 = 그룹 이름 판정(4초 피어 확인), 정원 판정은 지움 (서버가 함) |
| `meon/MeonParts.*` | **분리** | 데이터(`Part`, key·이름)는 `core/model/Parts`, 아이콘 경로·그리기는 `ui/theme/Icons`, `PartSelector` 는 새 위젯으로 대체. `code/fromCode`(이름 끝 글자)는 지움 |
| `meon/MeonEditor.*` | **새로 작성** | 새 `MeonEditor` 는 `AppRoot` 를 담고 크기 제약·`ShellHooks` 만 한다. 업데이트 설치·종료 요청·오디오 정보 수집·개발용 자동 방 동작은 `core` 로 옮김 |
| `meon/MeonKeyRepeat*` | **재사용** | 새 `TextInput` 위젯(JUCE `TextEditor` 기반)에 그대로 붙인다 |
| `meon/MeonTheme.*`, `MeonLookAndFeel.*`, `MeonWidgets.*`, `MeonEmoji.*`, `OnboardingScreens.*`, `LobbyScreens.*`, `JamScreen.*`, `SettingsScreen.*` | **삭제** | 새 `ui/` 로 대체. 단, 쓸 만한 조각은 옮겨 쓴다: Pretendard 로딩·Windows ttf 처리(`MeonTheme.cpp`), 로고 경로, 링크 감지·한글 자모 합치기(`JamScreen.cpp`, `MeonSession::composeHangul`), 이모지 그리기(`MeonEmoji`), 6칸 코드 입력의 키 처리(`MeonWidgets.cpp`) |
| `Source/SonoStandaloneFilterApp.cpp` | **수정** | `meon::MeonEditor` 캐스팅 → `meon::ShellHooks` 캐스팅 (버그 #20). 개발용 실행 옵션은 `MEON_DEV_OPTIONS` 일 때만 해석 (버그 #21) |
| `Source/SonoStandaloneFilterWindow.h` | **수정** | 같은 이유로 캐스팅 변경. 최소·기본 창 크기 1024×680 / 1280×800 유지 |
| `Source/SonobusPluginProcessor.*` | **허용 범위 수정** | `createEditor()` 가 새 에디터, 호스트 파라미터 숨김, 플러그인 송신 채널·셀프 모니터 기본값, 버퍼 모드 연결. 승인 필요: E1 멤버 필터, E2 `MeonCore` 소유, E4 `setInputMonitorPan` (8장) |
| `Source/ChannelGroup.*` | **승인 수정 (E4)** | `ChannelGroupParams::monitorPan`, `processMonitor` 에서만 사용 (8.2) |
| `CMakeLists.txt` | **수정** | 새 소스 목록, 테스트 타깃 `MEON_Tests`(콘솔, JUCE `UnitTest`), `MEON_DEV_OPTIONS` 정의, fixtures BinaryData |

---

## 3. 화면 목록과 디자인 대응표

디자인 상단의 상태 칩("요청 1개", "방 닫힘 토스트" 등)은 데모용이라 넣지 않는다. 같은 상태는 가짜 서버의 개발 옵션으로 재현한다.

### 3.1 화면 (전체)

| 디자인 | 컴포넌트 | 내용 | 플러그인 차이 |
|---|---|---|---|
| L3 온보딩 · 로그인 | `OnboardingScreen` (`LoginStep`) | 이메일·비밀번호, 오류 문구, 로그인(진행 중 "로그인 중…"), "비밀번호를 잊었나요?" → 디스코드 안내 상자, 회원가입 링크 | 같음 |
| L3 · 회원가입 | `SignupStep` | 아이디 + 중복 확인(`username-available`), 이메일, 비밀번호·확인(불일치 문구), 약관 동의, 다음. 진행 막대 1/4 | 같음 |
| L3 · 이메일 인증 | `VerifyStep` | 6칸 코드(`OtpInput`), 인증하기, 30초 재전송 카운트다운, 이메일 바꾸기 (Q3) | 같음 |
| L3 · 닉네임 | `NicknameStep` | 글자 수 `n / 12`, Enter = 다음 | 같음 |
| L3 · 파트 | `PartStep` | 3열 타일 6개(아이콘 28px), 이전·시작하기 | 같음 |
| L3 · 디스코드 | `DiscordStep` | 혜택 3줄, 디스코드 참여하기(`discord_url`), 이미 참여해 있어요 | 같음 |
| L3 · 완료 | `DoneStep` | 가입: "준비됐어요, {닉네임}님" / 로그인: "다시 오셨네요", 홈으로, "오디오 장치는 설정 › 오디오에서…" | 플러그인은 마지막 문구 없음 (오디오 탭 숨김이라) |
| L2 홈 | `HomeScreen` | 헤더(로고, 알림 버튼+점, 설정), 배너 2종, 공개 방(필터·코드로 입장·방 만들기, 메이슨리 카드, 빈 상태), 내 오디오, 친구(접기), 친구 추가 | 카드 열 2개(창 폭 기준, 4.1), 장치 이름 "DAW 입력" |
| L1 합주 | `JamScreen` | 헤더(로고 · 방 이름 · 비공개 배지 · 방장 톱니 · 입장 코드+복사 · 입장 요청 버튼 · 설정 · 방 나가기), 멤버 목록, 내 오디오·모니터 카드, 채팅·친구·파일 플레이어 | 오른쪽 폭 240(앱 320), 행 간격 24, 미터 폭 유연 |

### 3.2 홈의 겹침 요소

| 디자인 | 컴포넌트 | 동작 |
|---|---|---|
| 연결 끊김 토스트 | `Toast` 경고 종류 (`Toast::Kind::warning`: 무음 경고 줄과 같은 연한 노란 면 `col::accentSoft`, 제목 굵게 + 설명, 설명은 자르지 않고 줄바꿈. 크기·모서리·자리는 다른 토스트와 같고 아래에서 위로 쌓는다. 배치를 밀지 않는다) | 서버 실시간 연결이 다시 연결 중이거나 방 안에서 합주 서버(AOO)가 끊긴 지 2초 (`ConnectionWatch`): 경고 "연결이 끊겼어요 / 인터넷 연결을 확인해 주세요. 다시 연결하는 중이에요". 끊긴 동안은 닫기 버튼 없이 계속 떠 있고, 다시 붙으면 같은 자리에서 일반 토스트 한 줄 "다시 연결됐어요"로 바뀌어 3초 뒤 닫힌다. 4000·4001·로그아웃은 띄우지 않는다 (2026-10-16: 노란 줄 → 일반 토스트 → 경고 토스트) |
| 새 버전 배너 | `UpdateBanner` | `latest_build > 내 빌드` 일 때. 나중에 / 업데이트 → 진행 막대 + "1.4.0 내려받는 중 · N%" → "설치를 마쳤어요 · 다시 시작하는 중…". **`min_build > 내 빌드` 이면 "나중에" 숨김** (기획 결정) |
| 알림 팝오버 | `NotificationPopover` | 헤더 아래 오른쪽 340px. 받은 친구 요청(아이콘, 이름, "친구 요청 · 파트", 거절·수락), 비면 "새 알림이 없어요". 열 때 `markNotificationsRead` |
| 코드로 입장 모달 | `JoinCodeModal` | 308px, 6칸, "방장에게 받은…" / 오류 "이 코드로 열린 방이 없어요", 취소·입장("확인 중…") |
| 방 필터 모달 | `FilterModal` | 420px, 비어 있는 파트(다중), 지연(전체/10/20ms), 빈자리만, 친구 있는 방만, 초기화, "방 N개 보기" / "맞는 방이 없어요"(비활성) |
| 새 합주실 모달 | `CreateRoomModal` | 460px, 방 이름(30, 카운터), 소개(80, 2줄), 공개/비공개, 입장 방식(공개만), 최대 인원 2~7(기본 7), 방 만들기("만드는 중…") |
| 프로필 모달 | `ProfileModal` | 340px, 아바타 + 상태 점, 이름 · 친구 배지 · 친구 삭제(두 번 눌러 확정) / 차단(두 번), `@아이디`, 파트·상태·마지막 접속, 하단 친구 요청 / 같은 방 참여. 버튼 노출은 API 2.4 규칙 (`can_send_friend_request`, `can_invite`, `relation`) |
| 설정 모달 | `SettingsModal` | 860×620, 왼쪽 탭 8개(플러그인은 오디오 탭 없음), 3.4 표 |
| 입장 실패 모달 | `JoinFailModal` | 380px, 4종(가득 참 / 방 닫힘 / 연결 실패 / 앱 버전 다름(업데이트 버튼)). 오류 코드 대응은 API 5.5 표 |
| 초대 토스트 | `ToastStack` + `InviteToast` | 오른쪽 아래 280px 쌓기. "{이름}님이 초대했어요 / 방 이름 / 입장 / ×". `invite.expired` 로 사라짐. 소리 설정(알림 탭) |
| 방 닫힘 토스트 | `InfoToast` | "방장이 방을 닫았어요 / 방 이름", 6초 뒤 자동으로 닫힘 |
| 친구 추가 결과 | `FriendAddBox` 아래 문구 | `result` 별 문구 (sent / not_found / already_* / accepted / self, `blocked_by_me`) |

### 3.3 합주 화면의 겹침 요소

| 디자인 | 컴포넌트 | 동작 |
|---|---|---|
| 입장 요청 팝오버 | `JoinRequestPopover` | 방장 + 승인 방일 때 헤더 버튼(점 = 요청 있음). 340px, 요청마다 아이콘·이름·"파트 · 핑"·거절·수락. 가득 차면 수락 비활성 + "방이 가득 찼어요. 자리가 나야…" |
| 방 설정 모달 | `RoomSettingsModal` | 460px, 이름·소개·공개 설정(비공개면 코드 안내)·입장 방식(공개만)·최대 인원(지금 인원보다 작은 값 비활성 + 안내), 방 닫기(두 번 눌러 확정), 취소·저장(바뀐 게 있고 이름이 있을 때만) |
| 방을 닫았어요 화면 | `RoomClosedOverlay` | 방장이 닫은 뒤 전체 덮개 "방을 닫았어요 / 멤버 모두 홈으로 돌아갔어요 / 홈으로" |
| 멤버 관리 모달 | `ManageMemberModal` | 340px, 아바타·이름·파트, 방장 넘기기(두 번), 내보내기(두 번) |
| 프로필 모달 (합주용) | `ProfileModal` 같은 것 | 합주 중에는 "같은 방 참여" 대신 지금 방 정보 |
| 설정 모달 | 홈과 같은 것 | |

### 3.4 설정 탭

| 탭 | 항목 | 비고 |
|---|---|---|
| 계정 | 아이디 + 복사, 이메일 변경(새 이메일 → 6칸 코드 → 확인, 30초 재전송), 비밀번호 변경(현재·새·확인), 로그아웃, 회원 탈퇴(3단계: 탈퇴 → 계속 → 아이디 입력 후 탈퇴하기) | 비밀번호 입력 칸 추가 필요 (Q2) |
| 프로필 | 닉네임(12), 파트 선택 | `PATCH /me` |
| 오디오 (앱만) | 입력 장치, 출력 장치, 버퍼 크기, 입력 채널(모노/스테레오), 입력 테스트(미터 + 테스트/중지) | 추가: Windows 드라이버·ASIO 안내·장치 경고·드라이버 설정 버튼 (기획 결정, Q9), 입력 채널 위치 선택 (Q8), 합주 중 변경 안내 |
| 합주 | 버퍼 모드 기본값(지연 우선/안정 우선), 셀프 모니터 기본값(앱 켬, 플러그인 끔), 마스터 볼륨 기억, 귀 테러 방지(끔/보통/강하게 + 설명) | |
| 친구 · 개인정보 | 친구 요청 받기(모두/받지 않음), 방 초대 받기(친구만/받지 않음), 차단 목록 + 해제 | `PATCH /me` settings, `/blocks` |
| 알림 | 초대 토스트 소리 | 합주 중에는 소리 없음 |
| 단축키 | 목록(고정) | 4.4 |
| 정보 | 버전 + 업데이트 확인/업데이트, 로그 폴더 열기, 오픈소스 라이선스(펼치기), 문의(디스코드) | 플러그인의 업데이트 버튼 (Q18) |

### 3.5 이번에 새로 넣는 표시 (디자인에 없음, 기획 결정·추가 반영에서 옴)

| 표시 | 위치 | 잠정안 |
|---|---|---|
| 입력 신호 없음 경고 | 합주 화면 "내 오디오" 카드 안, 미터 아래 한 줄 | "입력 신호가 없어요 · 설정에서 입력 채널을 확인해 주세요" + [설정 열기](오디오 탭). 노란 면(`#FFF8D6`) 배경. 조건은 6.3 |
| ~~입력 장치 없음 안내 (버그 17)~~ | **제거함 (사용자 결정, 2026-10-15)** | 화면에 띄우지 않는다. 판정은 로그(`inputMissing`)에만 남긴다 (6.3) |
| (두 안내 공통) 좁은 폭 | 같은 자리 | 한 줄(제목 · 설명 · 버튼)에 설명이 다 안 들어가면 설명을 제목 아래 줄로 내린다 (플러그인 900px 등). 말줄임 없음 |
| 합주 중 장치·버퍼 변경 안내 | 설정 › 오디오, 합주 중일 때 장치·버퍼·채널 행 아래 | "합주 중에 바꾸면 소리가 잠깐 끊겨요" |
| 채팅 전송 실패 | 내 말풍선 아래 | "일부 멤버에게 전달되지 않았어요 · 다시 보내기" (6.2) |
| 채팅 길이 | 입력창 오른쪽 | 남은 바이트가 100 미만일 때만 "남은 글자 수"를 보여 주고, 한도에서 더 입력되지 않음 |
| 추방됨 (Q14) | 홈 토스트 | "방에서 내보내졌어요 / 방 이름" |
| 방장 부재 승인 방 (Q15) | 입장 실패 모달 | "방장이 자리에 없어요 / 방장이 돌아오면 다시 요청해 주세요" |
| 합주 중 연결 끊김 (Q16) | 토스트 | 홈과 같은 연결 끊김 토스트 (서버 실시간 연결 또는 합주 서버). 끊긴 채 30초가 넘어 방에서 나와 있으면 홈 + 경고 토스트 "방에서 나왔어요 / 연결이 오래 끊겨서 방에서 나왔어요" (닫기 버튼, 6초 뒤 닫힘. "다시 연결됐어요"는 띄우지 않는다) |

### 3.6 디자인 파일과 다르게 정한 것

여기 적은 것은 디자인 파일(`docs/design/MEON_Lab.dc.html`)보다 우선한다.

| 날짜 | 자리 | 디자인 파일 | 결정 |
|---|---|---|---|
| 2026-10-14 | 합주 화면 헤더 (버그 19) | 로고 오른쪽에 세로 구분선(1px `#E0E0E0`), 양옆 18px | 구분선을 없앤다. 로고 오른쪽 끝에서 방 제목까지 18px 여백만 |
| 2026-10-14 | 로고 (홈 헤더, 합주 헤더, 로그인·가입 화면) | `MEON-logo.svg` 높이 34px | 새 로고(`images/brand/logo/meon_logo.svg`)는 높이 약 31px 로 그린다 (`metric::logoDrawH`). 디자인의 34px 는 예전 파일의 안쪽 여백을 포함한 높이이고, 새 파일은 여백이 없어 34px 로 그리면 약 11% 커 보인다. 로고 자리(34px)와 헤더 배치는 그대로 |
| 2026-10-14 | 파트 아이콘 (방 카드 멤버 줄, 방 필터 칩) | 아이콘 없음 (글자만) | 다른 자리처럼 얇은 버전(`-thin`) 아이콘을 글자 앞에 넣는다. 설정 › 프로필의 파트 선택은 디자인대로 글자만 |
| 2026-10-14 | 홈·합주 화면의 작업 영역 (상단 바·배너 아래, 패널들을 감싸는 `flex:1` · `gap 18px` · `padding 18px` 컨테이너) | 흰색 (`#FFFFFF`) | 바탕을 `#FCFCFC` 로 (`col::surfaceCanvas`, 두 화면이 같은 토큰). 안쪽 카드(흰 면 + `#E0E0E0` 테두리)·상단 바·배너·모달·설정 화면은 그대로. 플러그인 크기(900px)도 같다 |

---

## 4. JUCE로 구현하기 어려운 디자인 요소와 대안

### 4.1 레이아웃·그리기

| 디자인 요소 | 어려운 점 | 대안 |
|---|---|---|
| 메이슨리(공개 방 카드) | CSS 열 배치가 없다 | `MasonryLayout` 직접 작성. 카드 높이는 디자인 식 그대로 (`2+24+19+9+17×소개 줄 수+9+(인원×20−3)+9+19`), 가장 짧은 열에 넣고 **한 번 들어간 열은 유지**(디자인의 `_cm` 맵과 같다, 카드가 튀지 않게). 열 수는 창 폭으로: 1100 미만 2열, 1450 미만 3열, 그 이상 4열(디자인의 900/1280/1512 기준) |
| 카드 등장·높이 변화 애니메이션 (0.3s) | CSS transition 없음 | `AnimatedValue`(VBlank 로 보간하는 작은 클래스)로 투명도·높이 보간. 사라질 때 0.3s 투명해진 뒤 목록에서 제거 |
| 접기 화살표 회전 0.15s, 토글 스위치 0.15s | 같음 | 같은 `AnimatedValue` |
| 소개 2줄 말줄임 | `-webkit-line-clamp` | `TextLayout` 으로 줄 수를 재고 2줄째 끝을 "…" 로 자름. 실제 줄 수(1·2)를 카드 높이 계산에 넘김 (디자인의 `descL`) |
| 숫자 고정폭(`tabular-nums`) | JUCE 7 은 OpenType 기능을 못 켠다 | 숫자 칸을 고정 폭 상자에 오른쪽/가운데 정렬. 미터 값·시간·카운트다운이 흔들리지 않게 |
| 아이콘 (톱니, 종, 필터, 사람+, 닫기, 화살표, 재생, 스피커, 외부 링크) | SVG | 디자인의 SVG `path` 문자열을 `Drawable::createFromSVG` 로 읽어 경로로 캐시. 파트 아이콘은 `images/brand/part-icons/part-*.svg` (모든 자리에 얇은 `-thin`. 방 카드·필터 칩은 디자인에 없던 자리, 3.6), 로고는 `images/brand/logo/meon_logo.svg` (워드마크, 좁은 자리는 `meon_mark.svg`). 브랜드 SVG 는 `meon::brandSvg` 로 도형만 읽고 색·선 굵기는 앱이 정한다 (JUCE 는 `non-scaling-stroke` 를 모른다) |
| 디스코드 로고 | 디자인은 CDN 이미지 | 지금 앱 코드 안의 경로를 쓴다 (외부 요청 없음) |
| 반투명 덮개(rgba 26,26,26,0.4), 둥근 모서리 6, 테두리 1px | 쉬움 | `MeonLookAndFeel` 과 공통 `Panel` 이 그림 |
| hover 색 | JUCE 는 hover 상태를 직접 관리 | 위젯 기본 클래스에서 `mouseEnter/Exit` 로 처리 |

### 4.2 입력

| 디자인 요소 | 어려운 점 | 대안 |
|---|---|---|
| 텍스트 입력(placeholder, 테두리, 오류 테두리 빨강) | `TextEditor` 기본 모양 | `TextInput` = `TextEditor` + LookAndFeel. 한글 입력기는 `TextEditor` 가 처리(지금 앱과 같음), macOS 플러그인 키 반복은 `MeonKeyRepeat` 재사용 |
| 6칸 코드 입력 (방 코드·이메일 코드) | 디자인은 칸마다 `<input>` | 칸 6개를 그리는 컴포넌트 하나 + 보이지 않는 입력 하나. 디자인 동작 그대로: 한 칸 입력 후 다음 칸, Backspace(빈 칸이면 앞 칸 지우고 이동), ←/→ 이동, 붙여넣기 6자, Enter 제출. 방 코드는 `A–H J–N P–Z 2–9` 만 받고 소문자는 대문자로 (버그 #7). 이메일 코드는 숫자만 |
| 비밀번호 입력 | 쉬움 | `TextEditor::setPasswordCharacter` |
| 소개 2줄 입력(textarea) | 쉬움 | 여러 줄 `TextEditor`, 높이 고정 |
| 선택 상자(select) | JUCE `ComboBox` 모양 | LookAndFeel 로 디자인 모양(32px, 테두리, 화살표). 펼친 목록은 지금 앱의 둥근 팝업 방식 재사용 |
| 슬라이더 (멤버 볼륨, 송출, 마스터, 파일 볼륨, 파일 위치) | 디자인 동작이 세밀함 | `DbSlider` 직접 작성: 손잡이 19px, 트랙 4px, 채움 노랑(정지·음소거면 회색), 드래그 중 위 말풍선(`+1.5 dB`), 0.5 dB 단위, ±1 dB 안이면 0, 더블클릭 0 dB, 휠 끔. 범위는 인자로 (멤버 −60~+6, 송출·파일 −30~+10, 마스터는 Q10) |
| 패닝 노브 | 세로 드래그 | `PanKnob`: 19px 원, 세로 드래그 1px = 1, ±100, \|값\| < 3 이면 0, 더블클릭 가운데, 회전 = 값 × 1.35°, 드래그 중 말풍선(`L 30`/`C`/`R 30`) |
| 토글 스위치, 세그먼트 버튼, 칩 | 쉬움 | 위젯 3종 |
| 두 번 눌러 확정 버튼 (삭제·차단·넘기기·내보내기·방 닫기) | 상태 문구 바뀜 | `ConfirmButton`: 첫 클릭에 문구·색이 바뀌고, 다른 곳을 누르거나 3초가 지나면 되돌아감 |

### 4.3 미터 (Logic 스타일)

- 색: 디자인의 그라데이션 그대로 (초록 ≤ −18 dB, 노랑 −6 dB, 주황 0 dB 직전, 빨강 0 dB 초과). 눈금 매핑도 디자인의 `DB_MAP` (−60→0, −18→0.5, −6→0.75, 0→0.9, +6→1).
- 피크 홀드 1.5초 뒤 10 dB/s 로 내려감, 막대 감쇠 26 dB/s (디자인 값). 화면 주사율(VBlank)로 갱신.
- 스테레오면 2줄. 클리핑 표시는 클릭하면 초기화.
- **문제**: 엔진 미터(ff_meters)는 값을 1.0(0 dBFS)에서 자른다 (ENG 6). 그래서 "0 dB 를 넘을 때만 빨강"은 나오지 않는다 → Q11. 잠정안: **0 dBFS 에 닿으면 빨강(클리핑)**으로 표시하고, 클리핑 표시를 켠다.

### 4.4 키보드 단축키

디자인 단축키 탭 그대로: M 송출 끄기/켜기, N 모니터 끄기/켜기, ⌘↑/⌘↓(Windows Ctrl) 마스터 볼륨 ±1 dB, Enter 채팅 입력으로 이동, ⌘, 설정, Esc 맨 위 창 닫기.
- 빼는 것(기획 결정): 멤버별 숫자 키, 파일 재생/정지 키. C(채팅 접기)도 디자인에 없어 뺀다.
- 글자 입력 중에는 글자 단축키를 듣지 않는다 (지금과 같음).
- 플러그인에서 M·N 이 DAW 단축키와 겹치는 문제(FEAT 10 #17)는 그대로 남는다. 플러그인 창에 포커스가 있을 때만 MEON 이 받는다.

### 4.5 그 밖

| 항목 | 대안 |
|---|---|
| 토스트 소리 | 짧은 효과음을 앱에 넣는다 (Q13). 합주 중에는 울리지 않음 |
| 링크 열기·복사 | 지금 코드 재사용 (`URL::launchInBrowser`, `SystemClipboard`) |
| 창 크기별 레이아웃 | 디자인 3종(1280×800, 1512×982, 플러그인 900×600)을 폭 기준 분기로. 앱 최소 1024×680 |

---

## 5. 화면과 데이터의 연결 (주요 흐름)

| 흐름 | 순서 |
|---|---|
| 앱 시작 | `getConfig` → 토큰이 있으면 `refresh` → `getMe` → 단계 판정(인증 전 → 인증, 닉네임·파트 없음 → 그 단계, 모두 있으면 홈) → 실시간 연결 → `hello` 로 상태 채움. 토큰이 없으면 로그인 |
| 로그인 | 성공 후에도 단계 판정을 한 번 더 (Q5) |
| 홈이 보일 때 | `listPublicRooms` + `subscribePublicRooms(true)`, 1분마다 RTT 보고 |
| 공개 방 카드 버튼 | 바로 입장 → `joinRoom` → 200 이면 합주 화면, 승인 방 → 202 → "요청 보냄"(`request_until` 까지), 가득 참 → 비활성 |
| 승인 수락됨 | `join_request.accepted` → 바로 합주 화면 |
| 합주 화면 들어감 | `RoomDetail.connection` 으로 엔진 접속 → 그룹 입장 → `members` 로 피어 필터 → 이후 `room.updated` 마다 `members` 갱신 |
| 방 나가기 | `leaveRoom` → 엔진 그룹 나가기·접속 끊기 → 홈 |
| 추방·방 닫힘 | `room.kicked` / `room.closed` → 엔진 끊기 → 홈 + 토스트 |
| 친구 초대 | 버튼 "초대" → `sendInvite` → 3초 카운트다운 "N초 뒤 초대가 가요" + "취소"(`cancelInvite`) → "초대됨". **상대가 `online`(방에 없음)일 때만 버튼이 보인다** (추가 반영 3, API 2.4) |
| 받은 초대 | `invite.received` → 토스트. 내가 어느 방에든 들어가면 `invite.expired` 로 전부 사라짐 (추가 반영 3, API 7.11). 가짜 서버도 같은 규칙 |
| 업데이트 | 켤 때 + 6시간마다 `build.txt` 확인(지금 업데이터) + `config.latest_build`·`min_build` 비교. 받기·설치는 "업데이트"를 눌렀을 때만, 합주 중에는 버튼 비활성 |
| 최소 버전 미만 | 새 버전 배너의 "나중에" 숨김. 입장하려 하면 `426` → "앱 버전이 달라요" 모달 (별도 화면 없음) |

---

## 6. 기획 결정·추가 반영·버그를 어디서 어떻게 처리하나

### 6.1 기획 결정

| 결정 | 처리 | 단계 |
|---|---|---|
| 정원 7, 상수 제거 | `MeonTheme.h` 의 `maxMembers` 삭제. 정원은 `Room.capacity`. 방 만들기 기본 7 | R1·R5 |
| 버퍼 모드 | `JamSession::setBufferMode(latency|stable)` → 엔진 `setDefaultAutoresizeBufferMode` + 모든 피어 `setRemotePeerAutoresizeBufferMode` (지연 우선 = AutoFull, 안정 우선 = AutoIncreaseOnly). 설정 기본값은 입장 때 적용, 합주 중에 바꾸면 바로 모든 피어에 | R4 |
| 코덱 PCM 16bit 유지 | 지금 `applyProcessorDefaults` 의 코덱 고정을 `JamSession` 으로 그대로 옮김 | R4 |
| 송출 정지 유지 (버그 #1) | 켤 때마다 `mastsendmute = 0` 으로 덮는 코드 삭제. 앱: 설정 파일 `sendMuted` 에 저장하고 켤 때 적용. 플러그인: 프로젝트 상태(엔진 상태)에 남은 값을 그대로 씀 + `MeonCore` 가 프로세서에 있어 창을 다시 열어도 덮지 않음 | R4 |
| 플러그인 송신 채널 고정 | 플러그인도 `paramSendChannels` 를 설정의 모노(1)/스테레오(2)로. 파일 소리는 같은 채널에 섞임(앱과 같은 경로) (버그 #13) | R8 |
| 셀프 모니터 기본값 | `paramDry`: 앱 1.0(켬), 플러그인 0(끔). 설정 › 합주의 "셀프 모니터 기본값"을 따름 | R4·R8 |
| 호스트 파라미터 0개 (버그 #17) | 8.1 | R8 |
| 채팅 | 멤버끼리 직접(지금처럼) + 길이 제한 + 실패 표시 (6.2) | R6 |
| 최소 버전 미만 | 5장 | R7 |
| 업데이트 확인 켤 때 + 6시간 | `UpdateController` 타이머 | R7 |
| 합주 중 장치·버퍼 변경 안내 | 3.5 | R7 |
| 빼는 것: 온보딩 오디오·헤드폰·소리 내보기 단계, 헤드폰 확인, 멤버 솔로, 파일 단축키 | 새 화면에 만들지 않는다. 헤드폰 확인 코드(`OnboardingScreens`)는 옛 UI 와 함께 삭제 | R3·R6 |
| ASIO 안내·장치 경고 → 설정 › 오디오 | 지금 S2 의 판정·문구·버튼을 설정 오디오 탭으로 옮김 | R7 |
| 비밀번호 찾기 → 디스코드 | 로그인 화면의 안내 상자 | R3 |
| 개발용 실행 옵션은 배포 빌드에서 빼기 (버그 #21) | `MEON_DEV_OPTIONS` = `MEON_BUILD_ID == 0`(로컬·PR·main 빌드)일 때만 켬. 릴리스 빌드(태그·수동 실행)에는 없음 | R0 |
| 창 크기 | 앱 최소 1024×680 / 기본 1280×800, 플러그인 기본 900×600 (최소는 Q19) | R2 |

### 6.2 채팅 길이와 실패 표시 (버그 #8)

**길이 제한 제안: 메시지 본문 UTF-8 1,000 바이트** (한글 약 333자, 영문 1,000자).
- 근거: 채팅은 OSC 메시지 하나로 UDP 에 실린다 (ENG 9). 머리(주소·타입 태그·그룹 이름 `meon-r_…` 약 44 B·보낸 사람 `u_…` 40 B·빈 문자열 2개)를 더하면 약 **1,115 B**, IP·UDP 헤더 28 B 를 더해 **약 1,143 B** 다. 인터넷에서 조각나지 않는다고 보는 UDP 크기(약 1,200 B) 안이다. 4,000 B 까지 보낼 수는 있지만 IP 조각으로 나뉘면 하나만 잃어도 메시지 전체가 사라진다.
- 입력창에서 바이트로 세어 한도에서 더 받지 않는다(붙여넣기는 한도까지 자름). 남은 바이트가 100 미만일 때만 숫자를 보여 준다.

**전달 확인과 실패 표시 (엔진 수정 없음)**
- 보낼 때 메시지 ID 를 만들어 `SBChatEvent.tags` 에 `id:<ID>` 로 싣는다.
- 받은 쪽은 화면에 표시하고, 같은 채널로 `tags = ack:<ID>`, 본문 없는 메시지를 돌려보낸다(화면에 안 보임).
- 보낸 쪽은 **3초 안에** 그 순간의 멤버 전원(나 제외)의 ack 를 받지 못하면 그 말풍선에 "일부 멤버에게 전달되지 않았어요 · 다시 보내기"를 띄운다. 엔진이 `sendChatEvent` 에서 실패를 돌려주면 바로 실패로 표시한다.
- 나 말고 멤버가 없거나, 남은 멤버가 전부 가짜 서버의 각본 멤버이면 엔진으로 보내지 않고 내 채팅 목록에만 평소처럼 보인다 (확인·실패 표시 없음, 2026-10-15). 나중에 들어온 사람에게는 보이지 않는다 (기록을 저장하지 않는다).
- 실제 멤버(다른 컴퓨터의 앱)가 한 명이라도 있으면 연결 중·연결 실패여도 보내고 확인을 기다린다 (3초 안에 없으면 실패 표시·다시 보내기). 확인을 기다리는 대상에서 각본 멤버는 뺀다.
- 로그: `chatSent`, `chatAckMissing`(누가 빠졌는지).

### 6.3 송신 레벨 기록과 무음 경고 (추가 반영 4)

**세션 로그**
- 5초 샘플에 `sendPeakDb`(그 5초 동안 송신 미터의 최댓값), `inputPeakDb`(입력 처리 뒤 미터 = 마이크 경로, 파일 소리 제외), `sendMuted`, `sendGainDb`, `inputChannels`(예: `"2"`, `"1-2"`) 를 넣는다. 미터 값은 0.4초 통계 주기마다 읽어 5초 최댓값을 모은다.
- 이벤트: `inputSilenceStart`(입력 채널·장치·송신 게인과 함께), `inputSilenceEnd`(지속 시간). 입력 장치가 없을 때는 `inputMissingStart`·`inputMissingEnd`.
- 5초 샘플의 `inputSilent`(장치는 있는데 소리가 없음)와 `inputMissing`(입력 장치·채널이 없음)은 따로 남긴다.
- 세션 시작의 `audio` 는 `deviceType`(장치 종류)·`inputDevice`·`outputDevice`·`sampleRate`·`bufferSize` 를 칸별로 남기고, 입력 장치가 없으면 `inputDevice` 를 "없음"으로 적는다 (버그 18).

**무음 경고**
- 조건: 방 안 + **송출 정지가 아님** + 입력 채널이 1개 이상 잡혀 있음(앱: 입력 장치가 열려 있고 채널 수 ≥ 1, 플러그인: 메인 입력 버스 채널 ≥ 1) + **입력 처리 뒤 미터(`getPostInputMeterSource`, 파일 소리 미포함)가 5초 내내 −60 dBFS 이하**.
- 신호가 −50 dBFS 를 넘으면 바로 경고를 지운다 (깜빡임 방지용 차이).
- 송출 정지를 켜면 경고를 지우고 판정 시간도 처음부터 센다.
- 판정에 파일 소리를 빼는 이유: 파일이 재생 중이어도 마이크가 무음이면 다른 사람에게 내 연주가 안 들리기 때문이다 (오늘 생긴 일과 같은 경우).
- 입력 채널이 0 (앱: 입력 장치가 없거나 켜진 입력 채널이 없음, 플러그인: 메인 입력 버스 채널 0) 이면 레벨로 판정하지 않는다. 그 상태가 5초 이어지면 로그에 `inputMissingStart` 를 남기고 무음 경고는 띄우지 않는다. 장치가 생기면 바로 끝내고 무음 판정을 그 순간부터 센다.
- "입력 장치 없음" 화면 안내 (버그 17 에서 넣음): **제거함 (사용자 결정, 2026-10-15).** 소리가 안 나면 사용자가 먼저 장치를 확인하므로 안내가 방해가 된다. 로그 기록은 그대로.
- 화면은 3.5. 단위 테스트로 판정 시간·조건을 확인한다 (`SilenceDetector` 는 시각을 인자로 받는다).

**출력 장치 없음 (2026-10-15)**
- 모니터 카드의 "출력 장치가 없어요" 화면 안내: **제거함 (사용자 결정, 2026-10-15).** 판정과 로그만 남긴다. 홈의 장치 이름 자리의 "오디오 장치 없음" 글자도 같은 이유로 지웠다 (장치가 없으면 비워 둔다).
- 판정 (로그용): 방 안 + 출력 장치가 실제로 동작하지 않음 (열린 장치가 없거나, 켜진 출력 채널이 0 이거나, 샘플레이트가 0 이하. 장치 이름이 골라져 있는지는 보지 않는다, `EngineLink::outputAvailable`) 이 5초 이어짐. 장치가 열리면 바로 끝난다. 플러그인은 DAW 가 출력을 맡으므로 판정하지 않는다.
- 로그: `outputMissingStart`·`outputMissingEnd`, 5초 샘플의 `outputMissing`.

### 6.4 Connection 과 멤버 필터 (추가 반영 1, API 2.8)

- 엔진 접속: `connectToServer(aoo_host, aoo_port, aoo_username, aoo_user_password)` → `joinServerGroup(group_name, group_password)`. 하드코딩 상수 사용처 제거 (`SPP.h:35-36` 의 `#define` 은 엔진 파일이라 남겨 두되 MEON 은 쓰지 않음).
- 화면 이름은 `RoomDetail.members` 에서 계정 ID(= AOO 이름)로 찾는다. 사용자 이름에 파트를 싣던 방식은 지운다.
- **멤버 목록 밖의 상대와는 소리·채팅을 주고받지 않는다.**
  - 소리: 엔진이 그룹 피어가 들어오면 바로 자동 연결한다(`SPP.cpp:4340`). 연결 전에 거르려면 엔진에 허용 목록이 필요하다 → **엔진 수정 E1** (8장).
  - 채팅: `sbChatEventReceived` 에서 보낸 사람이 멤버가 아니면 버린다 (엔진 수정 없음).
  - `room.updated` 로 멤버가 빠지면(추방·퇴장) 그 피어를 엔진에서 지운다(`removeRemotePeer`), 새 멤버가 들어오면 E1 의 대기 목록에서 연결한다.

### 6.5 RTT 보고 (추가 반영 2, API 5.9)

- 앱을 켤 때 1회, 그 뒤 **홈이든 방이든 1분마다** AOO 서버 왕복 시간을 재서 `reportRtt` (요청 제한 1분 2회 안).
- 측정: 지금의 `ServerPinger`(서버에 TCP 접속하는 데 걸린 시간 = 왕복 1회)를 `RttReporter` 로 옮기고, 3회 재서 가장 작은 값을 쓴다. 측정 대상은 `config` 나 마지막 `Connection` 의 `aoo_host:aoo_port` (가짜 기간에는 지금 서버).
- 화면 표시는 하지 않는다. 내 값은 공개 방 카드의 예상 지연 계산(2.11)에 쓴다. 지금 스레드를 쓰되 측정 주기만 1분으로 (버그 #11).

### 6.6 버그 (ENG 12장)

| # | 처리 | 단계 |
|---|---|---|
| 1 송출 정지 풀림 | 6.1 | R4 |
| 2 멤버 끊김 감지 안 됨 | `PeerState` 를 엔진 통계로 판정: 연결된 피어가 뮤트가 아닌데 받은 패킷 수가 **3초** 동안 그대로면 "끊김", 다시 늘면 "연결됨". 서버 멤버 목록에 있는데 AOO 피어가 **15초** 안에 안 붙으면 "연결 실패". 멤버 행에 상태 표시 | R6 |
| 3 끊김 횟수 0 | "연결됨 → 끊김" 전환마다 `dropCount` +1, 로그 이벤트 `peerInterrupted`/`peerResumed` | R6 |
| 6 정원 판정 경쟁 | 클라이언트 판정을 없애고 서버(`room_full`)가 결정. 가짜 서버도 한 곳에서 판정 | R1·R5 |
| 7 코드 문자 검사 | 6칸 입력이 `A–H J–N P–Z 2–9` 만 받음, 0·1·O·I 를 누르면 "0·1·O·I는 코드에 쓰지 않아요" 안내 | R4 |
| 8 채팅 길이·실패 | 6.2 | R6 |
| 11 서버 핑 | 6.5 (값을 RTT 보고에 씀, 주기 1분) | R4 |
| 12 재접속 로그 쌓임 | 로그는 상태가 바뀔 때만(`serverLost` 1번 → `serverReconnected` 1번 + 그사이 재시도 횟수·시간) | R4 |
| 16 업데이트 흐름 | `UpdateController` 로 이동 | R7 |
| 17 파라미터 노출 | 8.1 | R8 |
| 18 귀 테러 방지 단계 전달 | `MeonCore` 가 1초마다 설정 파일 수정 시각을 보고 바뀌었으면 `earGuard` 를 다시 읽어 엔진에 적용 | R7 |
| 20 셸·에디터 결합 | `ShellHooks` 인터페이스 (`saveSettingsIfNeeded`, `requestedQuit`, `getAudioDeviceManager`, `applyDevOptions`) | R0 |
| 21 개발 옵션 | 6.1 | R0 |

ENG 12 의 #4·#5(코드 = 공개 서버 그룹 이름, 4초 판정)는 서버가 방을 관리하면서 사라진다. 가짜 기간에는 1.5 의 그룹 비밀번호로 옛 앱과 섞이지 않게 한다.

### 6.7 맥 테스트 21–26 (2026-10-14)

| # | 처리 |
|---|---|
| 21 셀프 모니터 기본값 | 기본 끔(Q30). 예전 빌드가 켜진 채 저장한 값은 이 버전에서 한 번만 끔으로 (`selfMonitorResetQ30`). 엔진이 저장된 상태(dry)를 다시 읽어 와도 `JamSession` 이 만들어질 때 덮는다 |
| 22 홈에서 셀프 모니터 | 셀프 모니터는 방 안에서만 (`JamSession::isSelfMonitorAudible`). 방을 나오면 끄고 다시 들어가면 설정값대로. 1초마다 엔진 값을 다시 맞춘다 (플러그인: DAW 가 상태를 다시 넣는 경우). 서버가 끊긴 채 방을 나오면 남던 직접 연결 상대도 지운다 (`EngineLink::disconnect`) |
| 23 송출 정지 | 송출 정지 중에는 셀프 모니터도 끔(설정값은 그대로). 홈 입력 미터 회색(합주 화면과 같음), 합주 화면 내 행의 음소거·볼륨·패닝·초기화 비활성. 세션 로그에 `sendingTo`(보내는 상대 수)·멤버별 `sendActive`·`packetsSent`, `sendMuted` 이벤트에 `sendingTo n/m` |
| 24 프로필 변경 | 가짜 서버 `PATCH /me` → 방 멤버 `room.updated`, 공개 방 구독자 `rooms.updated`, 친구 `friend.updated` (진짜 서버 반영 대기). 가짜 기간 두 컴퓨터: 방 안에서 바꾸면 프로필 채팅을 바로 다시 보낸다 |
| 25 친구 목록 참여 버튼 | 바로 입장 "참여", 승인 방 "요청" → "요청 보냄"(request_until 까지, 방 목록 카드와 같은 상태·같은 규칙). 입장 방식이 바뀌면 따라간다 |
| 27 프로필 창 "같은 방 참여" | 25 와 같은 상태·규칙으로 "참여"·"요청"·"요청 보냄" (`HomeController::roomJoinState`). 실패하면 입장 실패 상자 (홈·합주 화면 모두). "요청 보냄" 은 카드·친구 목록·프로필 창 모두 화살표 커서 (눌러도 아무 일 없음, 모양은 그대로) |
| 28 입장 전에도 내 소리 듣기 | 설정 › 오디오(앱만) 토글, 기본 끔. 켜면 홈에서도 셀프 모니터 (22 의 규칙은 꺼져 있을 때만). 크기는 마스터·내 행 볼륨·송출 레벨 그대로, 송출 정지 중에는 안 들림, 방 안에서는 이 설정과 상관없이 내 행 음소거를 따른다. 홈 "내 오디오" 머리말 오른쪽에 "내 소리 듣는 중" 태그 (디자인에 자리가 없어 비공개·친구 태그와 같은 모양으로) |
| 26 볼륨 | 내 행 볼륨이 0 dB 위에서 안 커지던 것(dry 범위 0~1) → 크기를 입력 그룹 모니터로. 송출 레벨이 엔진에서 쓰지 않는 `ingain` 파라미터를 바꾸고 있어 아무 데도 반영되지 않던 것 → 입력 그룹 gain 으로 |

---

## 7. 설정 저장 (`MEONUser.settings` 키 추가)

| 키 | 값 | 기본 | 비고 |
|---|---|---|---|
| `sendMuted` | 0/1 | 0 | 앱만 (플러그인은 프로젝트 상태) |
| `sendGainDb` | −30~+10 | 0 | 엔진 입력 그룹 gain 으로 (`ingain` 파라미터는 엔진이 쓰지 않는다) |
| `bufferMode` | `latency` / `stable` | `latency` | |
| `selfMonitorApp` / `selfMonitorPlugin` | 0/1 | 0 / 0 | Q30. 방 안·송출 중일 때만 실제로 들린다 (6.7) |
| `homeMonitor` | 0/1 | 0 | 앱만. 입장 전(홈)에도 내 소리 듣기 (6.7 #28) |
| `selfMonitorResetQ30` | 0/1 | 0 | 예전 빌드의 켜진 값을 한 번만 끔으로 되돌렸는지 (6.7 #21) |
| `keepMasterVolume` | 0/1 | 1 | |
| `masterVolumeDb` | −30~+10 (Q10) | 0 | `keepMasterVolume` 일 때만 사용 |
| `inviteToastSound` | 0/1 | 1 | |
| `inputChannelStart` / `inputChannelCount` | 기존 키 | | Q8 |
| `fakeAccount` | JSON | — | 가짜 기간 계정 ID·AOO 비밀번호 (1.5). 진짜 서버 단계에서 지움 |

로그인 토큰은 설정 파일에 넣지 않는다 (`TokenStore`, Q21). `nickname`·`part` 는 서버(`Me`)가 기준이 되어 지운다.

---

## 8. 엔진 수정

### 8.1 허용된 수정 (지시서 범위)

| 수정 | 방법 |
|---|---|
| `createEditor()` | 새 `meon::ui::MeonEditor` 를 돌려줌 |
| 호스트 파라미터 숨기기 | 37개 파라미터를 담는 `AudioProcessorValueTreeState` 를 **호스트에 보이지 않는 내부 `AudioProcessor`**(작은 내부 클래스)에 붙인다. 엔진 코드는 지금처럼 `mState` 로 값을 읽고 쓰지만, 호스트가 보는 프로세서에는 파라미터가 0개다. 상태 저장(`getStateInformation`)은 지금과 같은 트리를 쓴다. 안 되면 대안: 모든 파라미터를 자동화 불가로 표시(호스트에 이름은 보임) |
| 플러그인 송신 채널 | 플러그인 기본 `sendchannels` 를 설정값(1/2)으로 |
| 플러그인 셀프 모니터 기본값 | 플러그인 `dry` 기본 0 |
| 버퍼 모드 연결 | 6.1 (엔진의 기존 함수 호출만) |

옛 프로젝트에 저장된 파라미터 자동화는 의미가 없어진다(파라미터가 사라지므로). 플러그인은 이미 배포됐으니 릴리스 노트에 적는다.

### 8.2 승인이 필요한 수정

| 번호 | 수정 | 이유 | 크기 |
|---|---|---|---|
| **E1** | **피어 허용 목록**: `setAllowedPeerNames(StringArray)` 추가. 그룹 피어 입장 이벤트(`SPP.cpp:4320-4345`)에서 허용 목록이 켜져 있고 이름이 없으면 **연결하지 않고 주소를 대기 목록에 둔다**. 목록이 바뀌면 새로 허용된 대기 피어를 연결하고, 빠진 피어는 연결을 끊는다 | API 2.8 "멤버 목록 밖의 상대와는 소리를 주고받지 않는다". 엔진이 피어를 들어오는 즉시 자동 연결하므로, 엔진 밖에서는 연결된 뒤에야 끊을 수 있어 그사이 소리가 오간다. 멤버 목록(`room.updated`)이 AOO 입장보다 늦게 오는 경우도 있어서 "나중에 연결"이 필요하다 | 약 60줄. 허용 목록이 비어 있으면(꺼짐) 지금 동작과 같다 |
| **E2** | **`SonobusAudioProcessor` 가 `MeonCore` 를 소유**: 전방 선언 + `std::unique_ptr<meon::core::MeonCore>` 멤버 1개, 생성·소멸 각 1줄 | 플러그인 창을 닫아도 로그인·실시간 연결·방 상태·RTT 보고가 이어져야 한다 (1.6). 지금은 에디터가 `MeonSession` 을 만들어서 창을 다시 열 때마다 기본값을 덮는 버그(#1)가 생겼다 | 약 10줄 |
| E3 (안 함) | 마스터(`wet`) 범위를 0~2 에서 0~3.17(+10 dB)로 | 디자인 마스터 −30~+10 dB. 지금 엔진 최대는 +6.02 dB | Q10 에서 마스터를 −30~+6 dB 로 제한해 필요 없어졌다 |
| **E4** (사후 승인) | **셀프 모니터 전용 패닝**: `ChannelGroupParams::monitorPan` 추가. `ChannelGroup::processMonitor` 가 입력 패닝에 이 값을 더해(−1~1 로 자름) 모니터에만 쓴다. `SonobusAudioProcessor::setInputMonitorPan(changroup, pan)` 추가. `EngineLink::applyMonitorParams` 가 입력 그룹마다 이 값을 넣는다 | 합주 화면 내 줄의 패닝(= 셀프 모니터 패닝). 엔진의 입력 패닝(`pan`/`panStereo`)은 모니터와 송출에 같이 걸려서, 그대로 쓰면 스테레오로 보낼 때 남이 듣는 내 소리 위치도 바뀐다 | 약 15줄 (아래 근거) |
| **E5** (승인, R11 PR3 에서 구현) | **서버에 로그인하지 않았을 때 모르는 상대의 연결 요청 거절**: `AOO_INVITE_EVENT`(더미 소스, `SPP.cpp` 3550 근처)는 AOO 서버에 로그인하지 않은 동안(홈, 서버 끊김 중) 어느 주소의 요청이든 받아 이름 없는 피어를 만든다. E1 허용 목록이 켜져 있으면 이 요청을 받지 않는다 | 이름 없는 피어는 E1 필터에 걸리지 않아(`setAllowedPeerNames` 가 이름 없는 피어를 건너뜀), 멤버가 아닌 상대와 소리가 오갈 수 있다 (API 2.8 위반) | 2줄 추가·1줄 삭제 (추가 중 1줄은 주석). 허용 목록이 꺼져 있으면 지금 동작과 같다 (아래 근거) |
| **E6** (승인, R11 PR5 에서 구현. 2026-10-15 범위 축소) | **실제 번호 빌드에서 SonoBus 공개 서버(`aoo.sonobus.net`)로 붙는 길을 막는다**: 엔진에서 이 주소를 쓰는 곳은 `SonoStandaloneFilterApp.cpp` 의 옛 SonoBus 명령줄 연결 옵션(`-c/--connect`·`-g/--group`·`-p`·`-n`·`-q/--headless`·`-l/--load-setup`, `DEFAULT_SERVER_HOST`·`DEFAULT_SERVER_PORT` 사용) 뿐이다. 실제 번호 빌드(`MEON_BUILD_ID != 0`)에서는 이 옵션을 읽은 뒤 버리고 표준 오류에 한 줄 남긴다. 빌드 0 은 그대로이고, 빌드 0 의 가짜 서버가 주는 `aoo.sonobus.net:10998` (`Source/meon`, 1.5)도 남긴다 | 실제 번호 빌드의 MEON 은 운영 서버가 준 `aoo_host` 만 쓴다 (API 2.8). 옛 옵션은 MEON 화면과 무관하게 `aoo.sonobus.net` 에 붙을 수 있는 길이다 | 12줄 추가, 삭제 없음 (`#if`·`#endif` 2줄, 주석 2줄, 빈 줄 1줄 포함). 빌드 0 은 컴파일되는 코드가 같다 (아래 근거) |
| **E7** (승인 2026-10-15, R11 PR5 에서 구현) | **엔진이 저장된 최근 연결 목록으로 스스로 AOO 서버에 접속하지 않게 한다**: `SonobusAudioProcessor::reconnectToMostRecent()` 가 바로 `false` 를 돌려준다. 빌드 0·실제 번호 빌드 모두 | 이 함수는 (1) 첫 상태 복원 때 `reconnectlast` 파라미터가 켜져 있으면, (2) AOO 서버가 끊겼을 때 `mReconnectAfterServerLoss` 가 켜져 있으면 불려, 최근 연결 목록 첫 항목(옛 설치에 남은 `aoo.sonobus.net` 일 수 있음)으로 접속한다. MEON 은 두 값을 켜지 않지만 상태 복원(앱 시작, DAW 프로젝트)이 다시 켤 수 있다. MEON 은 AOO 재접속을 직접 한다 (R11 PR3) | 4줄 추가, 삭제 없음 (주석 2줄, 빈 줄 1줄 포함) (아래 근거) |

**E4 가 보내는 소리에 영향이 없는 근거** (줄 번호는 `remake` 의 eb7c371 기준)

- 바꾼 곳: `Source/ChannelGroup.h` (`ChannelGroupParams::monitorPan`, 기본 0), `Source/ChannelGroup.cpp` `processMonitor` 안의 패닝 계산 한 줄(491)과 램프용으로 기억하는 값(539–543), `Source/SonobusPluginProcessor.h/.cpp` `setInputMonitorPan`(2084–2089). 이 밖의 엔진 코드는 그대로다.
- 보내는 소리는 `processBlock` 에서 `sendWorkBuffer` 로 만든다. 입력은 `ChannelGroup::processPan`(`SPP.cpp:7620`)이 `inputPostBuffer` 에서 `sendWorkBuffer` 로 옮기는데, `processPan`(`ChannelGroup.cpp:322`)은 `params.pan`/`panStereo` 만 읽고 `monitorPan` 을 읽지 않는다. 램프 상태도 따로 쓴다(`processPan` 은 `mainProcState`, `processMonitor` 는 `monProcState`). 그 밖에 `sendWorkBuffer` 에 더해지는 것은 파일·메트로놈·입력 리버브(`inputRevBuffer`, 7845)뿐이고, 모두 `monitorPan` 과 무관하다.
- `processMonitor`(`SPP.cpp:7656`)가 쓰는 `inputBuffer` 는 내 출력(`buffer`, 8162), 메인 리버브 버스(`mainFxBuffer`, 8191·8207, MEON 은 리버브를 끔), 로컬 녹음(8419–8442)에만 들어가고 `sendWorkBuffer` 로는 가지 않는다.
- `processMonitor` 안의 리버브 보내기(`processReverbSend`)도 `params.pan` 만 읽는다.
- `monitorPan` 은 입력 그룹(`mInputChannelGroups`)에만 넣는다. 파일·메트로놈 그룹도 `processMonitor` 를 쓰지만 값이 늘 0 이라 동작이 그대로다.
- 따라서 바뀌는 것은 내가 듣는 소리와, 로컬 녹음 기능을 쓸 경우 그 녹음 속 내 소리 위치뿐이다.

**E5 가 기존 동작과 보내는 소리에 영향이 없는 근거** (줄 번호는 `main` 의 1edc545 기준)

- 바꾼 곳: `Source/SonobusPluginProcessor.cpp` `handleSourceEvents` 의 `AOO_INVITE_EVENT`(더미 소스) 분기 조건 한 줄(3557)과 주석 한 줄. 원래 조건 `!mIsConnectedToServer` 에 "E1 허용 목록이 꺼져 있을 때" 를 더했다. 허용 목록 값은 `mAllowedPeersLock` 을 잡고 읽고 바로 놓는다(다른 락을 잡은 채로 기다리지 않음).
- 허용 목록이 꺼져 있으면 조건이 원래와 같다. 엔진 동작이 그대로다.
- 허용 목록이 켜져 있고 AOO 서버에 로그인하지 않은 동안에는 원래 "서버에 로그인한 동안" 가던 분기를 그대로 탄다: 이미 아는 피어(`findRemotePeer`)의 요청은 지금처럼 받고, 모르는 주소면 피어를 만들지 않고 넘어간다. 피어가 없으니 그 주소로 싱크를 붙이지 않고 소리도 보내지 않는다.
- MEON 의 합주 상대는 모두 AOO 그룹 입장(서버 로그인 중)으로 들어오므로 이 분기에 닿지 않는다. 서버가 잠깐 끊긴 동안 이미 연결된 멤버는 `findRemotePeer` 로 찾아져 그대로 유지된다. 서버 없이 주소로 바로 붙는 옛 SonoBus 기능은 MEON 이 쓰지 않는다.
- MEON 은 허용 목록을 늘 켜 둔다: 홈(방 밖)에서는 빈 목록으로 켜고(`JamSession` 생성자와 `exit`), 방 안에서는 `connection.members` 로 채운다. 그래서 홈에서는 누구의 연결 요청도 받지 않는다.

**E6 근거** (줄 번호는 R11 PR5 브랜치 기준)

- 바꾼 곳: `Source/SonoStandaloneFilterApp.cpp` 491–501 (`initialise` 의 명령줄 해석, `-l` 처리 바로 앞)에 `#if MEON_BUILD_ID != 0` 블록 하나. 이 블록은 앞에서 읽은 `-c -g -p -n` 결과(`doInitialConnect`, `copyInfo`, `cmdlineConnInfo`)를 지우고, `-q`(`headlessSpec`)·`-l`(`loadSetupSpec`)을 인자 목록에서 뺀다. 옵션을 하나라도 줬으면 표준 오류에 `Meon: SonoBus connection options are not supported` 를 찍는다.
- 그래서 실제 번호 빌드에서는 `connectToServer(cmdlineConnInfo…)`(655), `addRecentServerConnectionInfo`, `joinServerGroup` 에 닿지 않고, 헤드리스로 뜨지 않고 보통 창으로 뜬다. `-l` 설정 파일도 읽지 않는다.
- `MEON_BUILD_ID` 는 앱 타깃의 컴파일 정의다 (`CMakeLists.txt` 590). 빌드 0 에서는 블록이 컴파일되지 않아 이전과 같은 코드다.
- 보내는 소리·오디오 처리와 무관하다 (명령줄 해석만 바뀜). 플러그인은 이 파일을 쓰지 않는다.
- 확인 (리눅스, 로컬 빌드): `MEON_BUILD_ID=7` 빌드에 `-q -g testgroup -c aoo.sonobus.net:10998 -n tester` 를 주면 위 한 줄을 찍고 창으로 뜨며, 25초 동안 네트워크 호출에 `aoo.sonobus.net`·10998 이 없다 (DNS 는 운영 API 주소뿐). 빌드 0 은 같은 옵션으로 이전처럼 헤드리스로 뜨고 `aoo.sonobus.net` 을 찾아 접속을 시도한다.
- 엔진에 남은 다른 길은 `reconnectToMostRecent()` 하나였고, E7 로 막았다.

**E7 근거** (줄 번호는 R11 PR5 브랜치 기준)

- 바꾼 곳: `Source/SonobusPluginProcessor.cpp` `reconnectToMostRecent()` 첫머리 8921–8924 (주석 2줄, `return false;`, 빈 줄). 그 아래 원래 코드는 그대로 두었다 (닿지 않음).
- 엔진에서 AOO 서버에 접속하는 호출은 셋뿐이다: `EngineLink::connect`(`EngineLink.cpp` 284, MEON 이 Connection 의 `aoo_host` 로), `SonoStandaloneFilterApp.cpp` 655 (옛 명령줄, E6), 이 함수의 `connectToServer`. 이 함수를 부르는 곳은 둘이다.
  - 첫 상태 복원(`setStateInformationWithOptions`, `SPP.cpp` 8859–8864): `reconnectlast` 파라미터가 켜져 있으면 부른다. 앱은 MEON 화면(`MeonCore`)이 만들어지기 전에 상태를 복원하므로 MEON 이 끌 틈이 없다.
  - 서버 끊김 타이머(`ServerReconnectTimer::timerCallback`, `SPP.cpp` 8910): `AOONET_CLIENT_DISCONNECT_EVENT` 때 `mReconnectAfterServerLoss` 가 켜져 있으면(`SPP.cpp` 4262) 1초마다 부른다. MEON 은 이 값을 `EngineLink::applyDefaults`(`EngineLink.cpp` 271)에서 끄지만, 방에 있는 동안 상태 복원(`SPP.cpp` 8789, DAW 프로젝트)이 다시 켤 수 있다.
- MEON 은 이 함수의 결과를 쓰지 않는다. AOO 재접속은 `JamSession` 이 방의 같은 Connection 으로 한다 (R11 PR3). 최근 연결 목록도 MEON 이 채우지 않는다.
- 남는 차이: (2) 의 값이 켜진 채로 서버가 끊기면 엔진은 여전히 "서버 끊김 복구 중" 표시(`mRecoveringFromServerLoss`)를 세우고 타이머를 돈다. 그래서 MEON 이 다시 접속할 때 `connectToServer` 가 기존 피어를 지우지 않는다(`SPP.cpp` 1137). 이 값이 꺼져 있는 보통의 경우(MEON 기본)는 그대로다. 소리 처리와 보내는 소리에는 영향이 없다.
- 확인 (리눅스, 빌드 0 로컬 빌드. 최근 연결 `127.0.0.1:10998` 을 넣은 저장 상태, 10998 에서 접속을 기록하는 리스너):
  - 시작할 때 (`reconnectlast` 켬): E7 없는 빌드는 켜자마자 10998 에 접속했고, E7 빌드는 20초 동안 접속하지 않았다.
  - 서버 끊김 (방 안에서 `mReconnectAfterServerLoss` 를 디버거로 켜고 로컬 aooserver 를 8초 멈춤): E7 없는 빌드는 1초마다 10998 에 15번 접속했고, E7 빌드는 한 번도 접속하지 않았다. 두 빌드 모두 MEON 재접속으로 방의 AOO 서버에 다시 붙었다 (4번째 시도).

새 엔진 수정은 고치기 전에 이 표에 적고 승인을 받는다. E4 는 고친 뒤 승인받았다 (2026-10-14).
E 번호는 다시 쓰지 않는다. 하지 않기로 한 수정도 번호를 그대로 두고 "안 함" 으로 표시하며, 새 수정은 다음 번호를 받는다.

---

## 9. 테스트와 확인 방법

### 9.1 자동 테스트 (`MEON_Tests`, JUCE `UnitTest`, 새 의존성 없음)

| 대상 | 내용 |
|---|---|
| `ApiJson` | API.md 2장의 모든 예시 JSON 을 그대로 읽어 구조체로 → 다시 JSON → 키·타입·`null` 이 같은지. 가짜 서버 fixture 도 같은 디코더로 읽히는지 |
| `Validation` | API 3.1 경계값 (서버 테스트와 같은 표) |
| `RoomFilter`, `Latency` | API 5.2, 2.11 |
| `FakeMeonServer` | 정원(방장 부재 포함), 승인 요청 5분·60초, 초대 3초·취소·소멸(내 입장·끊김), 숨은 친구 요청, 차단 필터. 가짜 시계 |
| `ChatChannel` | 1,000 B 경계, ack 3초, 일부 누락, 엔진 실패 |
| `SilenceDetector` | 5초, −60/−50 dB 차이, 송출 정지 때 안 뜸, 입력 없음 때 안 뜸 |
| `PeerState` | 3초 정지 → 끊김, 재개, `dropCount` |
| `UpdateController` | 6시간 주기, 합주 중 막기, `min_build` 일 때 "나중에" 숨김 |

### 9.2 CI (단계마다)

- **3개 OS 빌드**: 기존 `mac.yml`·`windows.yml`(PR 에서 자동) + **새 `linux.yml`** (Q27). 기존 워크플로 파일은 이름을 바꾸지 않는다 (자동 업데이트 `run_number`).
- `linux.yml`: Ubuntu 에서 독립 앱 + `MEON_Tests` 빌드 → 테스트 실행 → **Xvfb 로 화면 스냅숏**(개발 옵션 `--screen=… --fake-state=… --window=… --snapshot=…`) 을 PNG 아티팩트로 올림. 화면 상태마다 1280×800, 900×600 두 크기
- Windows 워크플로의 스냅숏 단계(이미 있음)도 같은 목록으로 늘린다

### 9.3 단계마다 사람이 확인하는 것

- **실행 확인**: PR 마다 "무엇을 어떻게 확인하면 되는지"를 적는다 (CLAUDE.md 규칙). 예: R6 "두 컴퓨터에서 비공개 방을 만들고 코드로 들어와 소리·채팅·멤버 끊김 표시 확인"
- **디자인 비교**: 스냅숏 PNG 와 디자인 화면을 나란히 본다. 디자인 HTML 은 `support.js` 가 없어 브라우저로 그대로 열리지 않는다 → 상태별 디자인 PNG 가 있으면 CI 가 나란히 붙인 비교 이미지를 만든다 (Q1)

---

## 10. 구현 순서 (단계별 PR → `remake`)

각 단계 = PR 하나. **통과 조건**: mac·windows·linux CI 성공, 그 단계의 단위 테스트, 스냅숏 아티팩트, 사용자 확인.

| 단계 | 내용 | 사용자 확인 |
|---|---|---|
| R0 기반 | `linux.yml`, `MEON_Tests` 타깃, `MEON_DEV_OPTIONS`, `ShellHooks` (버그 #20·#21). 화면 변화 없음 | CI 3개 초록 |
| R1 core: API·가짜 서버 | `ApiTypes`/`ApiJson`/`ApiError`, `MeonApi`/`Realtime`, `FakeMeonServer`·fixtures, `AppState`/`AppStore`, `Validation`/`RoomFilter`/`Latency`, `TokenStore`(키체인·자격 증명 관리자) | 테스트 결과만 |
| R2 테마·위젯 | 토큰(색·간격 18/12·헤더 58·버튼 32·간격 9), 글꼴, 아이콘, 4장의 위젯 전부 + 개발용 위젯 모음 화면 | 위젯 스냅숏을 디자인과 비교 |
| R3 온보딩 | 새 `MeonEditor`·`AppRoot`, 온보딩 7단계(가짜 서버). 옛 홈·합주 화면은 R6 까지 남겨 두고, 온보딩을 마치면 옛 홈으로 간다(소리 확인을 계속할 수 있게) | 가입 → 인증(가짜 코드는 개발 빌드 화면에 표시) → 닉네임 → 파트 → 디스코드 → 완료, 로그인 후 빠진 단계부터 재개 |
| R4 합주 로직 | `EngineLink`/`JamSession`/`PeerState`/`SilenceDetector`/`RttReporter`, Connection 접속, 송출 정지 저장(#1), 버퍼 모드, 셀프 모니터, 코덱 고정, 로그 정리(#12), RTT 1분 보고(#11). **E1 승인 후** 멤버 필터, **E2 승인 후** 프로세서 소유. 옛 화면은 R6 까지 옛 `MeonSession` 을 그대로 쓰고, 새 `JamSession` 은 개발 빌드 전용 확인 화면(`--dev-jam`: 코드 입장·멤버·미터·무음 경고·로그만 있는 단순한 화면)으로 확인한다. 둘은 동시에 만들지 않는다 | 단위 테스트 + `--dev-jam` 으로 두 컴퓨터 소리·멤버 필터·무음 경고·로그 확인 |
| R5 홈 | `HomeScreen`(메이슨리·필터·코드로 입장·방 만들기·친구·친구 추가·알림 팝오버·토스트·배너·입장 실패 모달). 방에 들어가면 아직 옛 합주 화면 | 가짜 데이터로 각 상태, 실제 비공개 방 만들기·코드 입장 |
| R6 합주 화면 | `JamScreen` 전체(멤버 행·내 오디오·모니터·채팅·친구·파일 플레이어·무음 경고), 채팅 ack(#8), 끊김 표시(#2·#3). **옛 UI 파일 삭제** | 두 컴퓨터: 비공개 방 → 코드 입장 → 소리·볼륨·패닝·뮤트·송출 정지·모니터·파일 재생·채팅(실패 표시 포함)·무음 경고(입력 채널을 일부러 틀리게)·로그의 송신 레벨 |
| R7 모달·설정 | 방 설정·멤버 관리·입장 요청·프로필·방 닫힘, 설정 8탭, ASIO·장치 경고 이동, 합주 중 변경 안내, `UpdateController`(#16, 6시간), 귀 테러 방지 전달(#18) | 각 모달 동작(가짜 서버), 설정 저장, 업데이트 배너 |
| R8 플러그인 | 파라미터 숨김(#17), 송신 채널 고정(#13), 셀프 모니터 기본 끔, 플러그인 크기·레이아웃, DAW 에서 창 닫았다 열기 | Logic·Ableton 등에서: 파라미터 목록이 비었는지, 창을 닫았다 열어도 방·송출 정지 유지 |
| R9 다듬기 | 애니메이션, 토스트 소리, 키보드 단축키, 접근성(탭 이동), 스냅숏 전체 비교 | 디자인과 전체 비교 |
| R10 main 반영 준비 | `remake` → `main` PR. 태그는 붙이지 않는다 (2026-10-14 결정). 이후 PR 은 `main` 대상, 테스터 빌드는 `main` 수동 실행 | 사용자 최종 확인 |
| R11 진짜 서버 (서버 준비 후) — **완료 (2026-10-15)**. 운영 서버가 뜬 뒤 확인할 것은 아래 "R11 이후" | `main` 대상 PR 5개: PR1 통신 기반(IXWebSocket+mbedTLS, `HttpTransport`·`WsTransport`, 서버 선택 `ServerConfig`, 서버별 토큰) → PR2 인증·계정(닫는 코드, refresh 프로세스 간 잠금, 강제 업데이트 화면) → PR3 방·합주(재접속, RTT, E5. AOO 재접속은 MEON 이 직접 한다: 엔진 자동 재접속은 `setReconnectAfterServerLoss(false)` 로 끄고 (엔진은 재접속 정보를 최근 연결 목록에 넣어 엔진 상태·DAW 프로젝트·설정 파일에 저장한다), AOO 서버가 끊기면 방의 같은 Connection 으로 1·2·4…30초 간격 재접속, 붙으면 그룹에 다시 들어간다(엔진이 그룹 이름을 지우지 않아 `joinGroup` 을 건너뛰지 않도록 MEON 이 강제). 4000 은 방에서 나가지 않고 AOO 만 끊고 자동 재접속하지 않는다. hello 의 `current_room` 이 같은 방이면 AOO 를 그대로 두고, 다른 방이면 원래 방의 AOO 를 끊고 그 방에 송출 정지로 들어간다(저장된 송출 설정은 그대로, 토스트 "다른 기기에서 들어간 방으로 연결됐어요"), null 이면 AOO 를 끊고 홈. 정상 종료(창 닫기·메뉴 종료·플러그인 제거)는 `MeonCore` 소멸자에서 나가기를 보내고 응답을 최대 2.5초 기다린다. 닿지 않으면 표시 파일(`MEONLeftOnQuit.json`)을 남겨 40초 안에 다시 켰을 때 그 방에 들어가지 않고 나가기를 다시 보낸다. 강제 종료·크래시는 30초 안에 다시 켜면 hello 로 같은 방에 돌아간다) → PR4 친구·초대·알림(지연 보정 자료) → PR5 정리(가짜 서버 v0.5 맞춤, E6 줄인 범위). 서버: 실제 번호 빌드는 운영 고정, 빌드 0(PR 빌드)은 가짜 서버가 기본이고 `--server=local\|<주소>` 로 바꾼다 (플러그인도 가짜 서버). 가짜 서버의 합주 코드(1.5)는 남긴다 | 로컬 서버 계약 테스트(`Contract`), 전송층 테스트(CI 3 OS), 앱 두 개로 전체 흐름 |
| 가짜용 합주 코드 제거 (할 일) | **조건: 운영 서버 + 실제 번호 빌드로 두 대 합주를 확인한 뒤.** 가짜 AOO 주소 `aoo.sonobus.net:10998`, `FakeAooDirectory`, 프로필 채팅 메시지, 채팅의 가짜 각본 멤버 판정 (`isScriptedUser`) (1.5). 그 뒤 PR 빌드의 가짜 서버로는 두 컴퓨터 합주를 시험할 수 없다 | 실제 번호 빌드 두 대 합주 |

### R11 이후: 운영 서버가 뜬 뒤 확인할 것 (순서대로)

R11 의 PR3·PR4 에서 가짜 서버·로컬 서버로는 확인할 수 없던 것. 앞 단계가 되어야 뒤 단계를 볼 수 있는 순서다.

1. 첫 실제 번호 빌드 점검 (아래 표)
2. 가입·이메일 인증 메일이 실제로 오는지, 로그인·자동 로그인(refresh)
3. 운영 `aoo_host` 로 접속, RTT 보고 (방 카드의 방장 ms)
4. 다른 컴퓨터 두 대가 같은 방에서 합주 (공개 방 입장, 비공개 방 코드 입장, 방 제목이 양쪽에 같게 보이는지)
5. 승인 방 요청 → 방장 수락·거절, 방장 동작 실패 토스트가 실제 상황에서 뜨는지
6. 친구 요청 → 알림 점 → 수락, 다른 기기에서 알림을 읽으면 이 기기의 점이 꺼지는지, 위장 `sent` (차단·요청 막음) 를 실제 두 계정으로
7. 초대: 다른 컴퓨터 사이의 3초 전달과 토스트, 수락해서 입장. 실제 상황에서만 나는 실패 토스트 (오프라인, 다른 방에 있음, 정원 참)
8. 지연 보정 자료: 입장 10초 뒤 전송, 서버에 쌓인 값이 실제 망의 값인지
9. 실제 망에서 AOO 서버가 끊겼다가 다시 붙는 것 (와이파이를 잠깐 끊기)
10. 두 기기 사이: 다른 기기에서 다른 방에 들어갔을 때 hello 로 옮겨 가는 것, 앱과 플러그인 사이의 4000
11. 종료: access 토큰이 만료된 채로 창을 닫았을 때 다시 나가기 경로 (15분 넘게 켜 둔 뒤 닫기)
12. 위가 모두 되면: 가짜용 합주 코드 제거 (위 표의 할 일)

### 첫 실제 번호 빌드 점검표

| 확인 | 방법 | 근거 |
|---|---|---|
| 서버가 운영으로 고정 | 앱 로그의 `서버:` 줄이 운영 (`MeonCore.cpp` 34). `--server=fake`·`--server=local` 을 줘도 운영 | `ServerConfig::choose`(`core/api/net/ServerConfig.cpp` 71: `build > 0` 이면 `prod()`) |
| 가짜 서버를 못 고름 | 위와 같음. 가짜 AOO 주소 `aoo.sonobus.net` 도 쓰지 않는다 (가짜 서버가 없으므로) | 같은 곳, `MeonProcessorGlue.cpp` 85·87 |
| 옛 SonoBus 명령줄 연결 막힘 | `-c aoo.sonobus.net -g x -q` 로 실행해도 창으로 뜨고 접속하지 않음 | E6 |
| 자동 업데이트 켜짐 | 앱만 (플러그인 제외). 켤 때 `mac-latest`/`windows-latest` 의 `build.txt` 와 비교 | `MeonUpdater.cpp` 270·273·355 |
| 업데이트 흐름 | 첫 실제 번호 빌드를 설치한 뒤, `main` 에서 한 번 더 수동 실행해 번호가 오른 빌드를 올린다. 설치된 앱을 다시 켜면 받아서 바꾸고 다시 뜨는지 (mac: `MEON-mac.zip` 안 `Meon.app`, Windows: `MEON-Installer.exe /UPDATE=1`) | CLAUDE.md "건드리면 자동 업데이트가 깨지는 것" |
| 이미 깔린 PR 빌드(빌드 0) | 빌드 0 은 자동 업데이트를 하지 않으므로 테스터에게 새로 설치하라고 안내 | `MeonUpdater.cpp` 270 |
| 강제 업데이트 | 서버 `min_build` 를 올리면 옛 빌드가 강제 업데이트 화면으로 | API 1.4, R11 PR2 |
| 버전 헤더 | 요청마다 `X-Meon-Build` 가 실제 번호 | `MeonProcessorGlue.cpp` 82 |

R4 의 E1·E2 는 8장 승인이 나야 들어간다. 승인 전에는 E1 없이(연결 후 바로 끊는 방식, 잠깐 소리가 섞일 수 있음) 만들고 표시해 둔다.

---

## 11. 디자인과 API.md 사이의 모순·누락·질문

1. **디자인 비교 기준 PNG**: `MEON_Lab.dc.html` 은 `support.js` 가 없어 브라우저로 열리지 않는다. 화면·상태별 PNG 를 `docs/design/ref/` 에 넣어 줄 수 있나? (없으면 사람이 눈으로 비교)
2. **비밀번호 입력 칸이 없다**: API 4.4(이메일 변경)·4.5(탈퇴)는 비밀번호가 필요한데, 디자인의 이메일 변경은 새 이메일만, 탈퇴는 아이디 입력만 받는다. → 잠정: 이메일 변경 첫 단계에 비밀번호 칸, 탈퇴 마지막 단계에 아이디 + 비밀번호 칸 추가
3. **인증 화면의 "이메일 바꾸기"**: 디자인은 회원가입 화면으로 돌아가지만, 그때 계정은 이미 만들어졌다. API 4.4 는 인증 전 계정의 주소를 바로 바꾸는 방식이다. → 잠정: 인증 화면 안에 새 이메일 입력 줄을 펼쳐 `POST /me/email` 로 바꾸고 코드를 다시 보냄
4. **디스코드 단계의 진행 표시**: 디자인 코드대로면 "5 / 4"가 된다. → 잠정: 디스코드 단계에는 진행 막대를 숨김
5. **로그인 후 흐름**: 디자인은 로그인하면 바로 "다시 오셨네요"(완료)다. API 3.4 는 빠진 단계부터 다시 보여 준다. → 잠정: 인증 전이면 인증, 닉네임·파트가 없으면 그 단계, 다 있으면 완료 화면
6. **입력 오류 문구 없음**: 비밀번호 규칙(영문·숫자·특수문자), 닉네임 길이·이모지, 이메일 형식, 방 이름 빈칸 오류 문구가 디자인에 없다. 디자인의 비밀번호 검사도 "8자 이상"뿐이다. → 잠정: API 3.1 대로 검사하고 칸 아래 빨간 한 줄 문구(예: "영문, 숫자, 특수문자를 하나씩 넣어 주세요")
7. **이용약관·개인정보 처리방침 주소**가 없다. → 주소를 알려 주세요 (그때까지 링크 비활성)
8. **입력 채널 위치를 고르는 곳이 없다**: 디자인은 모노/스테레오만 있다. 오늘 일처럼 "인터페이스 2번 입력"을 골라야 하는 경우가 있다. 지금 앱에는 있다. → 잠정: 설정 › 오디오에 "입력 채널" 선택 상자("입력 1", "입력 2", …, 스테레오면 "입력 1–2") 추가, 모노/스테레오는 그대로
9. **Windows 드라이버·ASIO 안내·드라이버 설정 버튼**이 디자인 설정 › 오디오에 없다 (기획 결정은 "설정 › 오디오로 옮김"). → 잠정: 오디오 탭 맨 위에 드라이버 선택(Windows), 장치 행 아래 경고 상자(장치 없음 / ASIO 아님 + "장치 다시 검색"·"ASIO 설치 안내 보기"), 버퍼 옆 "드라이버 설정"(ASIO 일 때)
10. **마스터 볼륨 +10 dB**: 디자인은 −30~+10 dB 인데 엔진 마스터 최대는 +6.02 dB 다. → (a) 화면을 −30~+6 으로 (엔진 수정 없음, 잠정) (b) 엔진 범위를 넓힘 (E3, 안 함)
11. **미터의 빨강(0 dB 초과)**: 엔진 미터가 0 dBFS 에서 값을 잘라 0 dB 를 넘는 값이 나오지 않는다. → 잠정: 0 dBFS 에 닿으면 빨강·클리핑 표시. (의존성 `ff_meters` 를 고치면 넘는 값도 보이지만 의존성 수정이다)
12. **모니터 "전체 음소거"의 범위**: "내가 듣는 모든 소리"라 멤버·파일·셀프 모니터를 모두 끈다(엔진 Output Level 0 + Dry 0). 맞나? (잠정: 그렇게)
13. **초대 토스트 소리**: 효과음 파일이 없다. → 잠정: 짧은 효과음 하나를 직접 만들어 넣음 (원하는 소리가 있으면 주세요)
14. **추방 알림**이 디자인에 없다. → 잠정: 홈 토스트 "방에서 내보내졌어요 / 방 이름"
15. **방장 부재 승인 방(`host_absent`)** 실패 화면이 없다. → 잠정: 입장 실패 모달 "방장이 자리에 없어요 / 방장이 돌아오면 다시 요청해 주세요"
16. **합주 중 연결 끊김 표시**가 없다 (홈에만 있음). → 잠정: 합주 헤더 아래 같은 노란 배너 "서버 연결 끊김 · 재연결 중 / 합주 소리는 계속 들려요" → **바뀜 (2026-10-16)**: 홈·합주 모두 연결 끊김 토스트 (3장 표)
17. **"연결 실패" 모달 문구**: 디자인 문구는 P2P 실패("멤버들과 직접 연결되지 않아요…")인데, API 5.5 는 서버 요청 실패(네트워크·500·시간 초과)도 이 화면이다. → 잠정: 제목은 같고, 서버 요청 실패 때 본문은 "서버에 연결하지 못했어요. 인터넷 연결을 확인해 주세요"
18. **플러그인의 업데이트 버튼**: 플러그인은 스스로 업데이트하지 못한다 (macOS 는 앱이 플러그인을 바꿔 줌). → 잠정: 플러그인 정보 탭은 "새 버전 1.4.0이 있어요 · MEON 앱에서 업데이트해 주세요", 버튼 없음
19. **플러그인 최소 창 크기**: 디자인은 900×600 하나뿐이다. 지금 최소는 760×520 이다. → 잠정: 최소 900×600 (그보다 작으면 디자인이 깨짐)
20. **같은 계정으로 앱과 플러그인(또는 플러그인 여러 개)을 동시에**: 서버는 WebSocket 을 하나만 허용해 나중에 연결한 쪽이 앞의 것을 끊는다 (API 11장). → **결정 (2026-10-14)**: 나중에 켠 쪽이 남는다. 먼저 켠 쪽은 닫는 코드 `4000` 을 받고 다시 접속하지 않으며 "다른 곳에서 접속했어요" 안내를 띄운다 (API 10.1). aooserver 도 같은 이름의 두 번째 로그인을 거부하므로, 끊긴 쪽은 방에서 나가지 않고 AOO 연결만 바로 끊는다 (R11 PR2·PR3)
21. **로그인 토큰 저장**: macOS 키체인 / Windows 자격 증명 관리자에 저장하고 앱·플러그인이 같이 쓴다 (네이티브 코드 조금). 맞나?
22. **엔진 수정 E1(피어 허용 목록)·E2(프로세서가 core 소유) 승인** (8.2)
23. **진짜 서버 단계의 WebSocket 라이브러리**: JUCE 에는 WebSocket(특히 TLS) 클라이언트가 없다. → **결정 (2026-10-14)**: IXWebSocket 12.0.1(BSD-3) + Mbed TLS 3.6.7(Apache-2.0)을 `deps/` 에 넣었다 (R11 PR1). macOS 는 키체인의 시스템 루트 인증서를 읽어 mbedTLS 에 넘기고, Windows 는 시스템 인증서 저장소를 쓴다. 오픈소스 라이선스 화면에도 넣었다
24. **가짜 기간의 실제 합주 방식** (1.5): 그룹 이름 = 비공개 방 코드, 그룹 비밀번호 = 코드에서 계산, 공개 방은 다른 컴퓨터에 안 보임, 상대 닉네임은 채팅 태그로 교환. 이렇게 해도 되나?
25. **필터의 파트 목록**: 디자인은 "그 외"를 뺀 5개다. 의도한 것인가? (잠정: 디자인대로 5개)
26. **채팅 전송 실패의 정의**: 6.2 처럼 "3초 안에 일부 멤버의 확인이 없으면 실패". 맞나?
27. **Linux CI 추가**: "3개 OS CI"를 위해 `linux.yml` 을 새로 만든다 (기존 mac·windows 파일은 그대로). Linux 는 배포하지 않고 빌드·테스트·스냅숏만. 맞나?
28. **프로필의 관계별 버튼**: 디자인은 `blocked_by_me`, `request_sent`, `request_received` 관계를 그리지 않았다. → 잠정: 차단한 사람 = "차단 해제", 요청 보냄 = "요청 보냄"(비활성), 요청 받음 = 수락·거절
29. **무음 경고 기준**: −60 dBFS 이하 5초, −50 dBFS 를 넘으면 해제, 파일 소리는 판정에서 뺌 (6.3). 맞나?
30. **셀프 모니터 기본값(앱 켬)**: 디자인은 독립 앱 기본이 "켬"이다. 지금 앱은 꺼져 있고, 헤드폰을 안 쓰면 하울링이 날 수 있다. 디자인대로 켤까? (잠정: 디자인대로 켬)
