> 복사본입니다. 원본은 meonhq/server/docs/API.md (v0.3). 이 파일은 수정하지 마세요.

# MEON API 계약 v0.3

데스크톱 앱(`meonhq/desktop`)과 백엔드(`meonhq/server`)가 주고받는 모든 요청·응답·이벤트의 기준 문서.
양쪽 에이전트는 이 문서만 보고 구현한다. 문서와 코드가 다르면 문서가 맞다. 바꿀 땐 문서를 먼저 바꾼다.

- 상태: v0.3 (12장 "미정" 참고)
- 범위: 계정, 프로필, 방, 방 권한, 입장 승인, 친구, 초대, 차단, 알림, 버전 검사, 실시간 이벤트
- 범위 밖: 합주 오디오 (SonoBus/AOO가 담당. 서버는 접속 정보만 발급)
- 화면 표기 규칙: 아이디는 어디서든 `@username`으로 보여준다 (`@`는 클라이언트가 붙인다. 서버 값에는 없음). `#숫자` 같은 꼬리표는 없다

---

## 1. 공통 규칙

### 1.1 주소
- REST: `https://api.meon.co.kr/v1`
- WebSocket: `wss://api.meon.co.kr/v1/ws`
- 도메인 `meon.co.kr`는 구매 완료, 아직 서버에 연결하지 않음. DNS는 Cloudflare로 옮겨 API를 Cloudflare 뒤에 둔다
- AOO 서버는 UDP라 Cloudflare를 거치지 못한다. `aoo.meon.co.kr`은 실제 IP를 가리킨다 (DNS only)
- 로컬 개발: `http://localhost:8080/v1`, `ws://localhost:8080/v1/ws`

### 1.2 형식
- 요청·응답 본문은 JSON, UTF-8
- 필드 이름은 `snake_case`
- 시각은 ISO 8601 UTC 문자열 (`2026-10-13T07:33:00Z`)
- ID는 서버가 발급하는 문자열 (UUID). 클라이언트는 내부 구조를 해석하지 않는다

### 1.3 인증
- 로그인하면 `access_token`(짧은 수명, 15분)과 `refresh_token`(긴 수명, 30일)을 받는다
- 인증이 필요한 요청은 헤더에 `Authorization: Bearer {access_token}`
- `access_token`이 만료되면 `401 token_expired` → 클라이언트는 `POST /auth/refresh`로 재발급 후 원래 요청을 한 번 재시도
- `refresh_token`은 한 번 쓰면 폐기되고 새 것을 받는다 (회전). 이미 쓴 토큰이 다시 오면 그 계정의 모든 토큰을 무효화한다
- 로그아웃하면 해당 `refresh_token` 무효화

### 1.4 클라이언트 버전 헤더
- 모든 REST 요청에 `X-Meon-Build: {빌드 번호}` 헤더를 붙인다 (CI `run_number`. 로컬 빌드는 `0`)
- 서버는 빌드 번호가 최소 버전(`min_build`, 13장)보다 낮으면 `426 client_outdated`를 돌려준다. 예외: `GET /config`
- 로컬 빌드(`0`)는 서버 설정 `ALLOW_DEV_BUILD=true`일 때만 통과 (개발 서버 전용)

### 1.5 오류 응답
모든 오류는 같은 모양이다.

```json
{
  "error": {
    "code": "room_full",
    "message": "방이 가득 찼어요"
  }
}
```

- `code`: 클라이언트가 분기에 쓰는 고정 문자열. 아래 각 API에 가능한 값을 적는다
- `message`: 사람이 읽는 한국어 문구. 클라이언트는 그대로 보여줘도 되지만 분기에 쓰지 않는다

공통 오류 코드

| HTTP | code | 의미 |
|---|---|---|
| 400 | `invalid_request` | 형식 오류, 필수 값 누락, 입력 규칙 위반 |
| 401 | `unauthorized` | 토큰 없음 또는 무효 |
| 401 | `token_expired` | access_token 만료 |
| 403 | `forbidden` | 권한 없음 |
| 404 | `not_found` | 대상 없음 |
| 409 | `conflict` | 상태 충돌 (개별 API에서 더 구체적인 코드 사용) |
| 426 | `client_outdated` | 앱 버전이 최소 버전보다 낮음 → 클라이언트는 강제 업데이트 화면 |
| 429 | `rate_limited` | 요청 횟수 초과. `Retry-After` 헤더 포함 |
| 500 | `internal` | 서버 오류 |

`400 invalid_request`에서 입력 규칙 위반이면 `error.field`(예: `"password"`)를 함께 준다.

### 1.6 페이지 나누기
목록 API는 커서 방식. 응답에 `next_cursor`가 있으면 `?cursor={next_cursor}`로 다음 페이지. 없으면 끝.
예외: `GET /rooms`는 v0.3에서 페이지를 나누지 않는다 (5.1).

---

## 2. 데이터 모델

### 2.1 Part (파트)
`"vocal" | "guitar" | "bass" | "drums" | "keys" | "other"`

화면 표기: 보컬 / 기타 / 베이스 / 드럼 / 키보드 / 그 외

### 2.2 UserPublic — 다른 사람에게 보이는 사용자
```json
{
  "id": "u_...",
  "username": "chan_keys",
  "nickname": "승찬",
  "part": "keys"
}
```

### 2.3 Me — 내 계정
```json
{
  "id": "u_...",
  "username": "chan_keys",
  "nickname": "승찬",
  "part": "keys",
  "email": "me@example.com",
  "email_verified": true,
  "pending_email": null,
  "settings": {
    "friend_requests": "everyone",
    "room_invites": "friends"
  },
  "created_at": "2026-10-13T07:33:00Z",
  "deletion_scheduled_at": null
}
```
- `pending_email`: 이메일 변경 중이면 새 주소 (4.4). 아니면 `null`
- `settings.friend_requests`: `"everyone"`(모두) | `"none"`(받지 않음). 기본 `"everyone"`
- `settings.room_invites`: `"friends"`(친구만) | `"none"`(받지 않음). 기본 `"friends"`

### 2.4 UserProfile — 프로필 모달
```json
{
  "user": { /* UserPublic */ },
  "relation": "friend",
  "status": "in_room",
  "last_seen_at": null,
  "room": { "id": "r_...", "title": "준호의 새벽 합주", "joinable": true },
  "can_send_friend_request": false,
  "can_invite": true
}
```
- `relation`: `"none" | "friend" | "request_sent" | "request_received" | "blocked_by_me"`
- `status`, `last_seen_at`, `room`: 친구일 때만 채운다. 친구가 아니면 전부 `null` (2.10과 같은 규칙)
- `can_send_friend_request`: 상대가 `friend_requests: "none"`이면 `false` → 클라이언트는 친구 요청 버튼을 **숨긴다**
  - 상대가 나를 차단한 경우에는 `true`로 준다 (차단 사실을 드러내지 않기 위해. 요청은 7.2 규칙대로 조용히 버려진다)
- `can_invite`: 친구이고, 상대가 `room_invites: "friends"`이고, 내가 방에 있고, 상대가 온라인일 때 `true`. 아니면 초대 버튼을 **숨긴다**

### 2.5 Room
```json
{
  "id": "r_...",
  "type": "public",
  "entry_mode": "approval",
  "title": "준호의 새벽 합주",
  "description": "92bpm 발라드 위주",
  "capacity": 7,
  "member_count": 4,
  "host_id": "u_...",
  "host_present": true,
  "host_rtt_ms": 9,
  "members": [ /* RoomMember[] */ ],
  "my_join_request_until": null,
  "created_at": "2026-10-13T07:33:00Z"
}
```

- `type`: `"public"`(공개, 목록 노출) | `"private"`(비공개, 목록 비노출, 초대 코드로 입장)
- `entry_mode`: 공개 방만. `"instant"`(바로 입장) | `"approval"`(방장 승인). 비공개 방은 항상 `null` (승인 없음)
- `capacity`: 2~7
- `host_present`: 방장이 지금 방에 있는지. `false`면 방장 자리가 예약된 상태
- `host_rtt_ms`: 방장 ↔ AOO 서버 왕복 시간. 방장 클라이언트가 보고한 값 (10.6). 모르면 `null`. 예상 지연 계산에 쓴다 (2.11)
- `my_join_request_until`: 내가 이 방에 승인 요청을 보냈으면, 버튼을 "요청 보냄"으로 유지할 시각 (요청 시각 + 5분). 없으면 `null` (5.7)
- 비공개 방의 초대 코드는 Room에 포함하지 않는다. 방 안 멤버에게만 내려준다 (2.7)

### 2.6 RoomMember
```json
{
  "user": { /* UserPublic */ },
  "is_host": true,
  "is_friend": false,
  "joined_at": "2026-10-13T07:33:00Z"
}
```
- `is_friend`: 요청한 사람 기준

### 2.7 RoomDetail — 방 안 멤버에게만
Room 전체 필드 +
```json
{
  "invite_code": "A7K2QX",
  "connection": { /* Connection */ }
}
```
- `invite_code`: 비공개 방만. 공개 방은 `null`

### 2.8 Connection — 합주 접속 정보
```json
{
  "aoo_host": "aoo.meon.co.kr",
  "aoo_port": 10999,
  "group_name": "meon-r_...",
  "group_password": "server-issued-random-secret"
}
```
- 서버가 방 생성 시 무작위로 만든다. 클라이언트는 그대로 SonoBus 엔진에 넘긴다
- `aoo_host`는 **MEON이 직접 운영하는 aooserver**다. SonoBus 공개 서버(`aoo.sonobus.net`)를 쓰지 않는다
- 초대 코드와 AOO 그룹 이름은 다르고 비밀번호가 있다. 초대 코드를 알아도 서버를 거치지 않으면 그룹에 들어갈 수 없다
- 입장에 성공한 사람에게만 준다

### 2.9 FriendRequest
```json
{
  "id": "fr_...",
  "from": { /* UserPublic */ },
  "created_at": "2026-10-13T07:33:00Z"
}
```

### 2.10 Friend
```json
{
  "user": { /* UserPublic */ },
  "status": "offline",
  "last_seen_at": "2026-10-13T04:10:00Z",
  "room": null,
  "can_invite": false
}
```
- `status`: `"offline" | "online" | "in_room"`
- `last_seen_at`: `offline`일 때만. 마지막으로 WebSocket이 끊긴 시각. 화면 표기 예: "3시간 전 접속"
- `room`: `status == "in_room"`이고 그 방이 공개 방일 때만 `{ id, title, joinable }`. 비공개 방이면 `null` (위치 비공개)
- `joinable`: 정원이 남아 있고, 나와 방장 사이에 차단이 없을 때 `true`
- `can_invite`: 2.4와 같은 규칙. `false`면 친구 패널에서 초대 버튼을 숨긴다
- 접속 상태를 숨기는 설정은 없다. 친구에게는 항상 실제 상태가 보인다

### 2.11 예상 지연 (방 카드, 필터)
방에 들어가기 전에는 나와 방장 사이에 연결이 없어 직접 잴 수 없다. 그래서 서버를 거친 두 구간으로 추정한다.

```
예상 지연(ms) = round( latency_coefficient × (my_rtt_ms + host_rtt_ms) )
```
- `my_rtt_ms`: 내 클라이언트가 AOO 서버까지 잰 왕복 시간 (10.6과 같은 측정)
- `host_rtt_ms`: Room에 들어 있는 값
- `latency_coefficient`: `GET /config`에서 받는다 (13장). 시작값 `0.7`
- 둘 중 하나라도 없으면 표시하지 않는다 (`—`)
- 화면에는 근사값임을 드러낸다: "약 12ms"
- 방 안의 멤버별 핑과 같은 단위(왕복)다
- 보정: 방에 들어간 뒤 방장과의 실제 P2P 왕복 시간을 재서 `POST /telemetry/latency` (5.10)로 보낸다. 서버는 쌓인 값으로 `latency_coefficient`를 다시 맞춘다

### 2.12 Invite
```json
{
  "id": "inv_...",
  "from": { /* UserPublic */ },
  "room": { "id": "r_...", "title": "준호의 새벽 합주" },
  "created_at": "2026-10-13T07:33:00Z"
}
```

### 2.13 JoinRequest — 방장에게만 보임
```json
{
  "id": "jr_...",
  "room_id": "r_...",
  "user": { /* UserPublic */ },
  "via_invite": false,
  "created_at": "2026-10-13T07:33:00Z",
  "expires_at": "2026-10-13T07:34:00Z"
}
```
- `via_invite`: 다른 멤버의 초대를 수락해서 생긴 요청이면 `true` (7.8)

---

## 3. 계정 — `/auth`

### 3.1 입력 규칙
| 항목 | 규칙 |
|---|---|
| `email` | 일반 이메일 형식, 최대 254자. 대소문자 구분 없이 고유 |
| `username` | 영문 소문자·숫자·밑줄, **4~16자**. 대소문자 구분 없이 고유(저장은 소문자). **변경 불가**. 탈퇴로 삭제된 아이디도 재사용 불가 |
| `nickname` | **2~12자**, 앞뒤 공백 제거, 중복 허용. 이모지·제어 문자 불가 |
| `password` | **8자 이상, 영문·숫자·특수문자를 각각 1자 이상 포함**, 최대 128자. 서버는 argon2id로 해싱. 평문·단순 해시 저장 금지 |
| 방 `title` | **1~30자** |
| 방 `description` | **0~80자** |

클라이언트도 같은 규칙으로 미리 검사하되, 최종 판정은 서버가 한다.

### 3.2 `GET /auth/username-available?username=...`
- 회원가입 화면의 "중복 확인" 버튼용. 인증 불필요
- `200`: `{ "available": true }`
- 형식이 틀리면 `400 invalid_request`
- IP당 1분 30회

### 3.3 `POST /auth/signup`
```json
{ "email": "...", "username": "...", "password": "...", "agreed_terms": true }
```
- 닉네임과 파트는 받지 않는다. 온보딩 3·4단계에서 `PATCH /me`로 넣는다 (3.4)
- `agreed_terms`가 `true`가 아니면 `400 invalid_request`
- 성공 `201`:
```json
{ "access_token": "...", "refresh_token": "...", "me": { /* Me, email_verified: false, nickname: null, part: null */ } }
```
- 가입과 동시에 로그인 상태가 되고, 6자리 숫자 인증 코드를 메일로 발송
- 오류: `email_taken`, `username_taken`, `invalid_request`

### 3.4 온보딩 순서와 서버 상태
1. 로그인 / 회원가입
2. **이메일 인증 (필수)**
3. 닉네임
4. 파트
5. 디스코드 안내
6. 완료

- 인증을 마치기 전에는 아래 API만 허용하고, 나머지 인증 필요 API는 전부 `403 email_not_verified`
  - 허용: `POST /auth/verify-email`, `POST /auth/resend-verification`, `POST /auth/refresh`, `POST /auth/logout`, `GET /me`, `POST /me/email`(가입 때 이메일을 잘못 넣은 경우), `DELETE /me`
  - WebSocket도 인증 후에만 허용 (`auth` 단계에서 `email_not_verified`로 거부)
- 인증 후 닉네임이나 파트가 `null`이면 방·친구·초대 API는 `403 profile_incomplete`. 허용되는 건 `PATCH /me`와 위 목록
- 클라이언트는 앱을 켤 때 `me`를 보고 빠진 단계부터 다시 보여준다

### 3.5 `POST /auth/verify-email` (인증 필요)
```json
{ "code": "482913" }
```
- 앱 안에서 입력하는 6자리 숫자 코드
- 성공 `200`: `Me` (`email_verified: true`)
- 오류: `code_invalid`, `code_expired` (유효 10분)
- 같은 코드로 5회 틀리면 그 코드 폐기, 재발송 필요

### 3.6 `POST /auth/resend-verification` (인증 필요)
- 성공 `204`. 새 코드 발송, 이전 코드 폐기
- 30초에 1회 (화면의 "30초 뒤 재전송"과 같다)

### 3.7 `POST /auth/login`
```json
{ "email": "me@example.com", "password": "..." }
```
- **이메일 + 비밀번호로만 로그인**한다. 아이디 로그인 없음
- 성공 `200`:
```json
{ "access_token": "...", "refresh_token": "...", "me": { /* Me */ } }
```
- 오류: `invalid_credentials` — 이메일이 없든 비밀번호가 틀리든 **같은 코드, 같은 문구** ("이메일 또는 비밀번호가 맞지 않아요")
- 탈퇴 유예 중인 계정이 로그인하면 **탈퇴가 취소**되고 정상 로그인. `me.deletion_scheduled_at`은 `null`
- 로그인 실패 제한: 같은 이메일 기준 15분에 10회, 같은 IP 기준 15분에 30회. 초과 시 `429`

### 3.8 `POST /auth/refresh`
```json
{ "refresh_token": "..." }
```
- 성공 `200`: `{ "access_token": "...", "refresh_token": "..." }`
- 오류: `unauthorized`

### 3.9 `POST /auth/logout` (인증 필요)
```json
{ "refresh_token": "..." }
```
- 성공 `204`

### 3.10 비밀번호 찾기 — **미정 (12장)**
- 결정 전까지 서버는 구현하지 않는다
- 클라이언트는 로그인 화면의 "비밀번호를 잊었어요"를 디스코드 문의 채널 링크로 연결한다

---

## 4. 내 계정 — `/me`

### 4.1 `GET /me`
- `200`: `Me`

### 4.2 `PATCH /me`
```json
{ "nickname": "...", "part": "bass", "settings": { "friend_requests": "none" } }
```
- 보낸 필드만 변경. `settings`도 보낸 키만 바꾼다
- `username`, `email`은 여기서 못 바꾼다
- `200`: `Me`

### 4.3 `POST /me/password`
```json
{ "current_password": "...", "new_password": "..." }
```
- `204`. 현재 세션 외 모든 refresh_token 무효화
- 오류: `invalid_credentials`

### 4.4 이메일 변경 — 6자리 코드
1. `POST /me/email`
```json
{ "new_email": "...", "password": "..." }
```
- `204`. 새 주소로 6자리 코드 발송. `me.pending_email`에 새 주소가 들어간다. 인증 완료 전까지 기존 이메일로 로그인
- 오류: `email_taken`, `invalid_credentials`
- 아직 이메일 인증을 안 한 계정(가입 직후 주소를 잘못 넣은 경우)은 `password` 없이 바로 바뀌고, 인증 코드가 새 주소로 간다

2. `POST /me/email/verify`
```json
{ "code": "482913" }
```
- `200`: `Me` (이메일 변경 완료, `pending_email: null`)
- 오류: `code_invalid`, `code_expired`(10분), `email_taken`(그사이 다른 사람이 가입)
- 재발송은 `POST /me/email`을 다시 호출 (30초 1회)

### 4.5 `DELETE /me` — 탈퇴
```json
{ "password": "..." }
```
- 화면에서 아이디 입력으로 한 번 더 확인하는 단계는 클라이언트 몫. 서버는 비밀번호만 검사
- `204`. 즉시:
  - 모든 토큰 무효화
  - 참여 중인 방에서 퇴장. 마지막 사람이었으면 방 삭제
  - **방장이었으면 예외로 자동 위임**: 그 방에 가장 오래 있던 멤버가 방장이 된다(`room.updated`). 탈퇴한 사람은 돌아오지 않으므로 방장 자리를 예약하지 않는다
  - 지금 방에 없는데 방장 권한이 남아 있던 방(방장 부재 중)도 같은 규칙으로 위임
  - 친구 목록, 방 목록, 방 멤버, 아이디 검색에서 **바로 보이지 않음**
- 30일 뒤 계정·친구 관계·차단 목록·받은/보낸 요청 영구 삭제. 아이디는 영구 예약 (재사용 불가)
- **30일 안에 로그인하면 탈퇴 취소** (3.7). 탈퇴 화면 문구는 이 규칙을 그대로 안내한다

### 4.6 `GET /users/{id}` — 프로필
- `200`: `UserProfile` (2.4)
- 탈퇴 유예 중이거나 없는 사용자: `404 not_found`
- 나를 차단한 사용자도 정상 응답 (차단 사실을 드러내지 않는다)

---

## 5. 방 — `/rooms`

### 5.1 `GET /rooms` — 공개 방 목록
- 공개 방만. 비공개 방은 절대 포함하지 않는다
- 차단 필터 (8장): 내가 차단한 사람이 **방장**인 방, 나를 차단한 사람이 **방장**인 방 제외
- 정렬: 미정 (12장). 임시로 생성 시각 내림차순
- v0.3은 페이지를 나누지 않고 전부 준다 (최대 200개). 필터를 클라이언트에서 하기 때문 (5.2)
- `200`:
```json
{ "rooms": [ /* Room[] */ ] }
```

### 5.2 홈 필터 — 클라이언트가 처리
서버 API 없음. `GET /rooms` 결과에 클라이언트가 적용한다.

| 필터 | 규칙 |
|---|---|
| 비어 있는 파트 (다중 선택) | 고른 파트가 **모두** 멤버 중에 없는 방 (AND) |
| 지연 | 예상 지연(2.11)이 10ms 이하 / 20ms 이하. 예상 지연을 모르는 방은 제외 |
| 빈자리 있는 방만 | 내가 지금 들어갈 수 있는 인원 (5.5의 정원 규칙 그대로) |
| 친구가 있는 방만 | `members` 중 `is_friend == true`가 있는 방 |

"방 N개 보기" 버튼의 N은 필터를 적용한 개수.

### 5.3 `POST /rooms` — 방 만들기
```json
{ "type": "public", "entry_mode": "approval", "title": "...", "description": "...", "capacity": 7 }
```
- `title` 1~30자 / `description` 0~80자 / `capacity` 2~7
- `type: "public"`이면 `entry_mode` 필수. `type: "private"`이면 `entry_mode`를 보내지 않는다 (보내면 `400`). 화면도 비공개를 고르면 입장 방식 항목을 숨긴다
- 만든 사람이 방장이 되고 자동 입장
- 이미 다른 방에 있으면 `409 already_in_room` (한 사람은 한 방에만)
- `201`: `RoomDetail`

### 5.4 `GET /rooms/{id}`
- 방 안 멤버: `200 RoomDetail`
- 그 외: 공개 방이면 `200 Room`, 비공개 방이면 `404`

### 5.5 입장
- `POST /rooms/{id}/join` — 공개 방
- `POST /rooms/join-by-code` — 비공개 방
```json
{ "code": "A7K2QX" }
```
- 코드: 6자리. 생성에 쓰는 문자는 혼동되는 글자(0, 1, O, I)를 뺀 영문 대문자·숫자. 입력은 대소문자 구분 없이 받고, 서버가 `O→0`, `I→1` 같은 치환은 하지 않는다 (애초에 생성에 안 쓰므로 그런 코드는 없음)

공통 검사 (순서대로)
1. 방 없음 → `404 room_not_found` (비공개 방은 `invalid_code`)
   - 24시간 안에 닫히거나 사라진 방이면 `410 room_closed` (5.8)
2. 이 방에서 추방된 적 있음 → `403 kicked`
3. 공개 방이고 방장과 차단 관계 → `404 room_not_found` (목록에서 안 보이는 것과 동일하게)
4. 이미 다른 방에 있음 → `409 already_in_room`
5. 정원 검사 → `409 room_full`
   - 방장이 방에 있을 때: `member_count < capacity`면 입장 가능
   - 방장이 없을 때: 방장 자리 하나를 비워둔다. 방장이 아닌 사람은 `member_count < capacity - 1`일 때만 입장 가능
   - 방장 본인은 자리 예약이 있으므로 항상 입장 가능
6. 공개 방이고 `entry_mode == "approval"`이고 내가 방장이 아님 → 입장 대신 **승인 요청** (5.7). `202`
7. 성공 `200`: `RoomDetail`
8. 방장 본인이 돌아오면 `host_present: true`로 바뀐다

입장 실패 화면과의 대응

| 화면 | 코드 |
|---|---|
| 가득 참 | `room_full` |
| 방이 닫혔어요 | `room_closed`, `room_not_found`, `invalid_code` |
| 연결 실패 | 네트워크 오류, `500`, 시간 초과 |
| 앱 버전이 달라요 | `426 client_outdated` |

### 5.6 `POST /rooms/{id}/leave`
- `204`
- 방장이 나가면 방장 권한은 그대로 그 사람에게 남고 `host_present: false`. 자동 위임 없음 (예외: 방장 탈퇴, 4.5)
- **마지막 사람이 나가면 방 삭제** (방장 부재 여부 무관). 삭제 시 그 방의 초대·승인 요청은 전부 무효
- 연결이 끊긴 경우: WebSocket이 끊기고 30초 안에 재연결하지 않으면 서버가 퇴장 처리

### 5.7 승인 요청 — 공개 방 + 방장 승인
대상: 공개 방 중 `entry_mode == "approval"`. 비공개 방에는 승인이 없다.

요청 만들기
- `POST /rooms/{id}/join`이 5.5의 1~5를 통과하면 `202`:
```json
{ "status": "requested", "request_until": "2026-10-13T07:38:00Z" }
```
- `request_until` = 요청 시각 + **5분**. 그때까지 클라이언트는 그 방 버튼을 "요청 보냄"으로 둔다. Room의 `my_join_request_until`도 같은 값 (앱을 다시 켜도 유지)
- 방장에게 `join_request.received` 이벤트

요청의 수명
- 방장이 수락/거절하지 않으면 **60초** 뒤 자동 만료 (**잠정값**, 12장)
- 신청자에게는 **거절과 만료를 구분해서 알리지 않는다**. 둘 다 아무 이벤트도 보내지 않는다. 버튼은 `request_until`까지 "요청 보냄"으로 유지된다
- `request_until` 전에 같은 방에 다시 요청하면 같은 `202`와 기존 `request_until`을 돌려주고 **아무것도 새로 만들지 않는다** (방장에게 요청이 쌓이지 않게)
- 한 사람은 동시에 하나의 방에만 요청할 수 있다. 다른 방에 요청하거나 어느 방에든 입장하면 기존 요청은 조용히 사라진다 (`request_until` 표시는 그대로 둬도 된다)
- 신청자가 앱을 끄거나 WebSocket이 30초 넘게 끊기면 요청은 조용히 사라진다

방장이 없을 때
- 방장 부재 중인 승인 방에는 요청을 받지 않는다 → **미정 (12장)**. 임시 규칙: `409 host_absent`

방장 API (6장 권한 규칙 적용)
- `GET /rooms/{id}/join-requests` → `200 { "requests": [ /* JoinRequest[] */ ] }`
- `POST /rooms/{id}/join-requests/{rid}/accept`
  - 수락 시점에 신청자를 대상으로 5.5의 2~5를 다시 검사
  - 통과하면 신청자를 멤버로 넣고, 신청자에게 `join_request.accepted { room: RoomDetail }` → 클라이언트는 바로 합주 화면으로
  - 신청자가 그사이 다른 방에 들어갔거나 오프라인이면 `409 request_unavailable`, 요청 삭제
  - 정원이 찼으면 `409 room_full`, 요청은 남는다
  - `200`: `RoomDetail`
- `POST /rooms/{id}/join-requests/{rid}/decline` → `204`. 신청자에게 알리지 않는다
- 요청이 만료·취소되면 방장에게 `join_request.removed { request_id }`

### 5.8 방 닫기
- 6.4 참고. 닫히거나 마지막 사람이 나가 사라진 방은 24시간 동안 ID와 코드를 기억해 두었다가 `410 room_closed`를 돌려준다. 그 뒤로는 `404`

### 5.9 `POST /me/rtt` — 내 AOO 서버 왕복 시간 보고
```json
{ "aoo_rtt_ms": 7 }
```
- 클라이언트는 앱을 켤 때, 그리고 홈 화면에 있는 동안 1분마다 AOO 서버까지 왕복 시간을 재서 보고한다
- 서버는 이 값을 그 사람이 방장인 방의 `host_rtt_ms`로 쓴다. 바뀌면 `rooms.updated`
- `204`. 사용자당 1분 2회

### 5.10 `POST /telemetry/latency` — 예상 지연 보정 자료
```json
{ "room_id": "r_...", "estimated_ms": 14, "my_rtt_ms": 8, "host_rtt_ms": 12, "actual_p2p_rtt_ms": 11 }
```
- 방에 들어가고 방장과 P2P가 연결된 뒤 10초 동안 잰 왕복 시간의 중앙값을 한 번 보낸다. 방장이 없으면 보내지 않는다
- 서버는 쌓인 자료로 `latency_coefficient`를 다시 계산한다 (지금은 수동. 운영자가 13장 설정값을 바꾼다)
- `204`

---

## 6. 방 권한 — 방장만

모든 API에서 서버가 요청자가 현재 방장인지 검사한다. 아니면 `403 not_host`.
방장이 방에 없는 동안은 아무도 이 API를 쓸 수 없다.
화면: 합주 화면 헤더의 방장 전용 톱니 버튼 → **방 설정 모달** (설정 변경 + 방 닫기). 멤버 행의 "관리" → **멤버 관리 모달** (방장 넘기기, 내보내기).

### 6.1 `PATCH /rooms/{id}` — 방 설정
```json
{ "title": "...", "description": "...", "capacity": 5, "type": "private", "entry_mode": null }
```
- 보낸 필드만 변경. 입력 규칙은 3.1, 5.3과 같다
- `capacity` 하한: `member_count + (host_present ? 0 : 1)`. 그보다 작으면 `409 capacity_below_members`
- **공개 → 비공개** (`type: "private"`)
  - 초대 코드를 새로 발급한다
  - `entry_mode`는 `null`이 된다
  - 대기 중인 승인 요청은 조용히 전부 삭제 (신청자에게 알리지 않음, 방장에게 `join_request.removed`)
  - 공개 방 목록에서 바로 빠진다 (`rooms.removed`)
- **비공개 → 공개** (`type: "public"`)
  - 기존 초대 코드는 **즉시 무효**. 옛 코드로 입장하면 `invalid_code`
  - `entry_mode`를 안 보내면 `"approval"`로 정한다
  - 공개 방 목록에 바로 나타난다 (`rooms.added`)
- 이미 보낸 초대(7장)는 방 공개 여부가 바뀌어도 유효하다. 수락할 때의 규칙은 그 시점의 방 상태를 따른다 (7.8)
- `200`: `RoomDetail`

### 6.2 `POST /rooms/{id}/transfer-host`
```json
{ "user_id": "u_..." }
```
- 대상이 지금 방에 있어야 함. 아니면 `409 target_not_in_room`
- 대기 중인 승인 요청은 새 방장에게 넘어간다
- `200`: `RoomDetail`

### 6.3 `POST /rooms/{id}/kick`
```json
{ "user_id": "u_..." }
```
- 자기 자신 추방 불가 → `400 invalid_request`
- 추방된 사람은 **방이 살아 있는 동안** 다시 입장 불가 (승인 요청도 `403 kicked`)
- 추방된 사람에게 WebSocket `room.kicked` 전송, 클라이언트는 합주 연결을 끊고 홈으로
- `204`

### 6.4 `POST /rooms/{id}/close` — 방 닫기
- 화면: 방 설정 모달의 "방 닫기". 한 번 누르면 "한 번 더 누르면 방을 닫아요"로 바뀌고, 다시 누르면 요청한다 (확정 단계는 클라이언트 몫)
- 서버 처리:
  1. 방장을 뺀 모든 멤버에게 `room.closed { room_id, reason: "host_closed" }` → 클라이언트는 합주 연결을 끊고 홈으로, 토스트 "방장이 방을 닫았어요"
  2. 방 삭제, 그 방의 초대 전부 소멸(`invite.expired`), 승인 요청 전부 삭제
  3. 공개 방이면 `rooms.removed`
- `204`

---

## 7. 친구 · 초대 — `/friends`, `/invites`

### 7.1 `GET /friends`
- `200`: `{ "friends": [ /* Friend[] */ ] }`
- 정렬: `in_room` → `online` → `offline`, 같은 상태 안에서는 닉네임순. `offline`끼리는 `last_seen_at` 최근순

### 7.2 `POST /friends/requests` — 친구 요청
```json
{ "username": "minjun_gt" }
```
- 프로필의 버튼과 친구 패널의 "친구 추가"(아이디 입력) 둘 다 이 API를 쓴다
- 결과는 `200`과 함께 `result`로 돌려준다:

| result | 의미 |
|---|---|
| `sent` | 요청 보냄 |
| `not_found` | 없는 아이디 |
| `already_friends` | 이미 친구 |
| `already_requested` | 이미 요청함 (대기 중) |
| `accepted` | 상대가 이미 나에게 요청해둔 상태 → 바로 친구가 됨 |
| `self` | 자기 자신 |

- 아래 두 경우는 **`sent`로 응답하고 실제로는 아무것도 만들지 않는다** (상대의 차단·설정을 드러내지 않기 위해)
  - 상대가 나를 차단함
  - 상대가 `friend_requests: "none"`
- 내가 상대를 차단했으면 `409 blocked_by_me` (내가 한 차단이라 숨길 필요 없음)
- 탈퇴 유예 중인 상대는 `not_found`
- 같은 사람에게 거절된 뒤 재요청은 허용

### 7.3 `GET /friends/requests` — 받은 요청
- `200`: `{ "requests": [ /* FriendRequest[] */ ] }`

### 7.4 `POST /friends/requests/{id}/accept`
- `204`. 양쪽에 `friend.added` 이벤트

### 7.5 `POST /friends/requests/{id}/decline`
- `204`. 보낸 사람에게 알리지 않는다

### 7.6 `DELETE /friends/{user_id}` — 친구 삭제
- `204`. 상대에게 알리지 않는다. 양쪽 목록에서 사라짐 (`friend.removed`는 상대에게도 조용히 전송되어 목록만 갱신)

### 7.7 `POST /invites` — 방 초대 (3초 지연)
```json
{ "user_id": "u_..." }
```
- 내가 지금 있는 방으로 초대. 방장이 아닌 멤버도 초대할 수 있다
- 조건 (순서대로):
  1. 내가 방에 없음 → `409 not_in_room`
  2. 친구가 아님 → `403 not_friend`
  3. 상대가 `room_invites: "none"` → `403 not_accepting` (정상이라면 버튼이 숨겨져 있어 오지 않는다)
  4. 상대가 오프라인 → `409 target_offline`
  5. 정원이 남지 않음 → `409 room_full`
  6. 같은 방에서 같은 사람에게 **이미 전달된** 초대가 있음 → `409 already_invited` (한 방에 한 번만)
- `201`:
```json
{ "id": "inv_...", "deliver_at": "2026-10-13T07:33:03Z" }
```
- **서버는 3초 뒤(`deliver_at`)에 상대에게 `invite.received`를 보낸다.** 그 전까지는 상대에게 아무것도 가지 않는다
- 화면: 버튼 "초대" → 누르면 3초 동안 "취소" → 3초가 지나면 "초대됨"
- 보낸 사람은 수락·거절 결과를 받지 않는다. 상대가 들어오면 멤버 목록에 나타나는 것으로 안다

### 7.8 `DELETE /invites/{id}` — 초대 취소 (3초 안에만)
- `deliver_at` 전이면 초대를 지우고 `204`. 상대에게는 **아무것도 가지 않는다**. 전달된 적이 없으므로 같은 사람에게 다시 초대할 수 있다
- 이미 전달됐으면 `409 already_delivered` (화면은 "초대됨" 상태라 취소 버튼이 없다)

### 7.9 `POST /invites/{id}/accept`
- 초대를 수락한 시점의 방 상태에 따라:
  - **비공개 방**: 코드 없이 바로 입장. 응답·오류는 5.5와 같다
  - **공개 방 + 바로 입장**: 바로 입장
  - **공개 방 + 방장 승인**:
    - 초대한 사람이 **방장**이면 승인 없이 바로 입장
    - 그 외에는 **승인 요청**이 된다 (`202`, 5.7 규칙. `JoinRequest.via_invite: true`)
- 방이 없어졌으면 `410 room_closed`

### 7.10 `POST /invites/{id}/dismiss`
- `204`. 보낸 사람에게 알리지 않는다

### 7.11 초대 소멸
- 방이 삭제되거나 닫히면 그 방 초대 전부 소멸, 받은 사람에게 `invite.expired`
- 초대받은 사람이 다른 방에 입장해도 소멸하지 않는다 (토스트를 끌 때까지 유지)

---

## 8. 차단 — `/blocks`

### 8.1 규칙
- **친구 상태에서는 차단 불가.** 친구 삭제 후 차단. 화면: 친구의 프로필에는 차단 버튼 대신 "친구를 먼저 삭제해야 차단할 수 있어요" 안내
- 차단은 한 방향이다. A가 B를 차단해도 B는 A를 차단하지 않은 상태
- 효과는 아래 두 가지뿐:
  1. 방 목록: A가 B를 차단하면 → A의 목록에서 B가 방장인 방이 숨겨지고, B의 목록에서 A가 방장인 방이 숨겨진다
  2. 친구 요청: B는 A에게 친구 요청을 보낼 수 없다 (B에게는 `sent`로 보임). 친구가 아니므로 초대도 불가
- 방장이 아닌 참여자로 같은 방에 있는 것은 막지 않는다
- 비공개 방 코드 입장은 막지 않는다
- 차단 목록 설명 문구는 위 두 가지 효과와 맞아야 한다

### 8.2 `GET /blocks`
- `200`: `{ "blocks": [ { "user": { /* UserPublic */ }, "created_at": "..." } ] }`

### 8.3 `POST /blocks`
```json
{ "user_id": "u_..." }
```
- 친구면 `409 is_friend`
- 차단 시 양쪽 사이의 대기 중인 친구 요청 삭제
- `204`

### 8.4 `DELETE /blocks/{user_id}`
- `204`

---

## 9. 알림 — `/notifications`

- 알림 모달의 내용은 **받은 친구 요청뿐**이다. 별도 알림 저장소를 두지 않고 `GET /friends/requests`를 그대로 쓴다
- 헤더의 새 알림 점: 읽지 않은 친구 요청이 하나라도 있으면 표시
- 방장이 받는 승인 요청은 알림 모달이 아니라 합주 화면 안에서 보여준다 (10.6 `join_request.*`)

### 9.1 `GET /notifications/state`
- `200`: `{ "has_unread": true }`

### 9.2 `POST /notifications/read`
- 알림 모달을 열면 호출. 현재까지의 친구 요청을 읽음 처리
- `204`

방 초대는 알림이 아니라 실시간 토스트다 (10장 `invite.received`). 앱이 꺼져 있을 때 받은 초대는 저장하지 않는다.

---

## 10. 실시간 — WebSocket

### 10.1 연결
- 로그인 직후 `wss://.../v1/ws` 연결
- 첫 메시지로 인증. 5초 안에 오지 않으면 서버가 연결 종료
```json
{ "type": "auth", "access_token": "...", "build": 51 }
```
- 성공 시 서버가 `hello` 전송 (10.4)
- 실패 시 `{ "type": "error", "code": "unauthorized" | "email_not_verified" | "client_outdated" }` 후 종료

### 10.2 메시지 형식
```json
{ "type": "room.updated", "seq": 1042, "data": { } }
```
- `seq`: 서버→클라이언트 이벤트 순번 (연결마다 1부터 증가). 클라이언트는 빠진 번호를 발견하면 `resync` 요청

### 10.3 하트비트
- 클라이언트가 20초마다 `{ "type": "ping" }`, 서버가 `{ "type": "pong" }`
- 서버는 45초 동안 아무것도 못 받으면 연결 종료 → 그 사용자 온라인 상태 해제, `last_seen_at` 기록, 방에 있었다면 30초 유예 후 퇴장 (5.6)

### 10.4 재연결과 동기화
- 끊기면 1초, 2초, 4초 … 최대 30초 간격으로 재연결
- 재연결 후 `auth`를 다시 보내면 서버가 `hello`로 **현재 전체 상태**를 보낸다. 클라이언트는 이걸로 화면을 통째로 덮어쓴다 (끊긴 사이 놓친 추방·방 닫힘·방 삭제 등을 반영)
```json
{
  "type": "hello",
  "seq": 1,
  "data": {
    "me": { /* Me */ },
    "current_room": { /* RoomDetail 또는 null */ },
    "join_requests": [ /* 내가 방장이면 JoinRequest[], 아니면 [] */ ],
    "friends": [ /* Friend[] */ ],
    "has_unread_notifications": true,
    "pending_invites": [ /* Invite[] — 이미 전달된 것만 */ ],
    "config": { /* 13장 Config */ }
  }
}
```
- 클라이언트가 `{ "type": "resync" }`를 보내도 같은 `hello`를 받는다
- 공개 방 목록은 `hello`에 넣지 않는다. 홈 화면이 보일 때 `GET /rooms` 후 `rooms.*` 이벤트로 갱신

### 10.5 구독
- 홈 화면에 있을 때만 방 목록 이벤트를 받는다
```json
{ "type": "subscribe", "topic": "public_rooms" }
{ "type": "unsubscribe", "topic": "public_rooms" }
```

### 10.6 서버 → 클라이언트 이벤트

방 목록 (`public_rooms` 구독자, 차단 필터 적용된 상태로 각자에게)
| type | data | 언제 |
|---|---|---|
| `rooms.added` | `Room` | 공개 방 생성, 비공개 → 공개 전환 |
| `rooms.updated` | `Room` | 인원·제목·설명·정원·입장 방식·방장·`host_rtt_ms` 변경 |
| `rooms.removed` | `{ "room_id" }` | 방 삭제·닫힘, 공개 → 비공개 전환 |

내가 있는 방
| type | data | 언제 |
|---|---|---|
| `room.updated` | `RoomDetail` | 멤버 입장·퇴장, 설정 변경, 방장 위임, 방장 복귀·이탈 |
| `room.kicked` | `{ "room_id" }` | 내가 추방됨 |
| `room.closed` | `{ "room_id", "reason": "host_closed" }` | 방장이 방을 닫음 → 홈 + 토스트 |

승인 요청
| type | 받는 사람 | data | 언제 |
|---|---|---|---|
| `join_request.received` | 방장 | `JoinRequest` | 새 요청 |
| `join_request.removed` | 방장 | `{ "request_id" }` | 만료, 신청자 취소·이탈, 공개 → 비공개 전환 |
| `join_request.accepted` | 신청자 | `{ "room": RoomDetail }` | 방장이 수락 → 바로 합주 화면 |

거절·만료는 신청자에게 보내지 않는다.

친구
| type | data | 언제 |
|---|---|---|
| `friend.updated` | `Friend` | 친구의 상태·방·`last_seen_at` 변경 |
| `friend.added` | `Friend` | 친구 요청 수락됨 (양쪽) |
| `friend.removed` | `{ "user_id" }` | 친구 삭제됨 (양쪽, 조용히) |
| `friend_request.received` | `FriendRequest` | 친구 요청 받음 → 알림 점 켜기 |

초대
| type | data | 언제 |
|---|---|---|
| `invite.received` | `Invite` | 초대가 전달됨 (보낸 지 3초 뒤) → 토스트 |
| `invite.expired` | `{ "invite_id" }` | 방 삭제·닫힘으로 초대 소멸 → 토스트 제거 |

설정
| type | data | 언제 |
|---|---|---|
| `config.updated` | `Config` | 운영자가 최소·최신 버전이나 보정 계수를 바꿈 |

### 10.7 이벤트 원칙
- 이벤트는 "무엇이 바뀌었다"를 알리는 용도다. 클라이언트가 이벤트로 받은 상태와 REST 응답이 다르면 REST가 맞다
- 권한이 필요한 동작(추방, 위임, 방 설정, 방 닫기, 승인)은 WebSocket으로 보내지 않는다. 전부 REST로 하고 서버가 검증한다

---

## 11. 요청 횟수 제한

| 대상 | 제한 |
|---|---|
| 로그인 | 3.7 참고 |
| 회원가입 | IP당 1시간 5회 |
| 아이디 중복 확인 | IP당 1분 30회 |
| 인증 코드 재발송 | 30초 1회 |
| 친구 요청 | 사용자당 1시간 30회 |
| 방 만들기 | 사용자당 10분 5회 |
| 초대 | 사용자당 1분 10회 |
| 승인 요청 | 같은 방 5분 1회 (5.7). 사용자당 1분 10회 |
| RTT 보고 | 사용자당 1분 2회 |
| WebSocket 연결 | 사용자당 동시 1개. 새로 연결하면 기존 연결 종료 |
| 그 외 인증 API | 사용자당 1분 120회 |

---

## 12. 미정 (결정되면 이 문서를 고친다)

| 항목 | 현재 임시값 | 결정 필요 |
|---|---|---|
| 비밀번호 찾기 | 디스코드 문의로 연결, 서버 구현 없음 | 디스코드 수동 처리 / 이메일 6자리 코드 자동 재설정 (회의 후 결정) |
| 승인 요청 만료 시간 | 60초 | 보류 중인 잠정값 |
| 방장 부재 중 승인 방 | `409 host_absent` (요청 안 받음) | 이대로 / 자동 수락 / 그 밖 |
| 예상 지연 계수 | `0.7` | 실측 자료가 쌓이면 다시 계산 |
| 파트 단수/복수 | 단수 (`part`) | 복수 선택 시 `parts: Part[]` + 대표 파트로 변경 |
| 공개 방 연결 | P2P. `Connection`만 발급 | 릴레이 측정 후 `Connection`에 릴레이 정보 추가 예정 |
| 공개 방 정렬 | 생성 시각 내림차순 | 최신순 / 자리 많은 순 등 |
| 메일 발송 서비스 | 로컬은 Mailpit(가짜 메일함) | 배포 전 결정. 보내는 주소 `no-reply@meon.co.kr`, SPF·DKIM 등록 필요 |
| 초대 코드 재발급 | 공개 ↔ 비공개 전환 때만 바뀜 | 방장이 코드만 따로 바꿀 수 있게 할지 |

---

## 13. 설정과 버전 — `GET /config`

인증 불필요, 버전 헤더 검사 예외. 앱을 켤 때 가장 먼저 부르고, 이후 변경은 `config.updated`로 받는다.

```json
{
  "min_build": 51,
  "latest_build": 58,
  "latency_coefficient": 0.7,
  "discord_url": "https://discord.gg/..."
}
```

| 내 빌드 | 동작 |
|---|---|
| `< min_build` | **강제 업데이트.** 방 입장 불가, 서버 API는 `426`. 화면은 업데이트 버튼만 |
| `min_build` 이상, `< latest_build` | 홈 상단 새 버전 배너. "나중에" / "업데이트" |
| `>= latest_build` | 아무것도 표시하지 않음 |

- 업데이트 파일 다운로드와 설치는 지금처럼 앱의 업데이터가 GitHub 릴리스에서 한다. 서버는 버전 번호만 알려준다
- "업데이트"를 누르기 전에는 내려받거나 설치하지 않는다
- 운영자는 서버 환경변수(또는 관리 테이블)로 `min_build`, `latest_build`, `latency_coefficient`를 바꾼다. 바꾸면 접속 중인 모든 클라이언트에 `config.updated`
- 접속 중에 `min_build`가 올라 내 빌드가 낮아지면: 합주 중이면 방을 나갈 때까지 기다렸다가 강제 업데이트 화면, 홈이면 바로

---

## 변경 이력
- v0.1 (2026-10-13) 초안
- v0.2 (2026-10-13) 이메일 인증을 온보딩 2단계 필수로 변경, 링크 방식 → 앱 내 6자리 코드 방식
- v0.3 (2026-10-13) 디자인 파일(MEON_Lab) 기준으로 맞춤
  - 입력 규칙: 아이디 4~16, 닉네임 2~12, 비밀번호 영문·숫자·특수문자 포함 8자 이상, 방 이름 30, 소개 80
  - 로그인 이메일 + 비밀번호만. 아이디 중복 확인 API 추가. 가입에서 닉네임·파트를 빼고 온보딩 단계로
  - 비밀번호 찾기 API 보류 (미정)
  - 이메일 변경을 6자리 코드로
  - 방 `type` 이름 `code` → `private`. 공개 방 입장 방식(바로/승인) 추가
  - 승인 요청: 5분 재요청 제한, 거절·만료 구분 없음, 초대받은 사람도 승인(방장 초대는 예외)
  - 방 설정에서 정원·공개/비공개 변경, 전환 시 코드 처리. 방 닫기 API와 `room.closed`
  - 친구 요청·방 초대 수신 설정, 프로필 API와 버튼 노출 규칙
  - 친구 `last_seen_at`
  - 초대 3초 지연 전달과 3초 안 취소
  - 예상 지연(방장까지) 계산식, RTT 보고, 보정 자료 수집
  - 버전 헤더, `GET /config`, 최소 버전 강제 업데이트
  - 홈 필터는 클라이언트 처리, 방 목록 페이지 없음
  - 자체 aooserver 명시, 초대 코드 문자 집합
  - 방장이 탈퇴하면 가장 오래 있던 멤버에게 자동 위임 (유일한 자동 위임)
  - 도메인 `meon.co.kr` 확정 (API `api.meon.co.kr`, AOO `aoo.meon.co.kr`)
