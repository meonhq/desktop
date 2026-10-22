# SonoBus 엔진 → ensemble-rust 교체 설계 (1단계)

상태: **설계 초안, 승인 전.** 구현은 승인 뒤에 한다. 작성 2026-10-22.
실험 코드: meonhq/ensemble-rust `claude/inspiring-noether-0iebak` 브랜치의 `experiments/aoo-port-share/`, `ffi/`.
관련: 플러그인 브리지 실험 #73, 엔진 수정 기록 `docs/REMAKE_PLAN.md` 8.2 (E1–E10).

크기 표기: **작다** 1–2일, **중간** 3–7일, **크다** 1–3주 (집중 작업일. 사람이 실기에서 확인하는 시간은 빼고).

---

## 0. 한눈에

- **교체는 할 수 있다.** 소리 처리(코덱·전송·지터 버퍼·믹싱·미터)는 ensemble-rust, 서버 연결·그룹·상대 찾기·구멍 뚫기는 지금 쓰는 AOO 클라이언트(BSD)가 맡는다. `Engine.h` 위(화면·`JamSession`·서버)는 그대로다.
- **포트 공유**: 앱(C++)이 UDP 소켓 하나를 갖고, 받은 패킷을 첫 바이트로 나눈다 (`/`·`#` → AOO, `EJ` → ensemble-rust). ensemble-rust 는 소켓을 모른다. 실험에서 NAT 두 개 뒤 두 클라이언트가 AOO 로 서로를 찾은 뒤 **같은 포트로 EJR1 패킷이 100% 오갔고, 다른 포트로 보내면 0개** 였다. 운영 서버 aoo.meon.co.kr 로도 서로 다른 네트워크의 러너 두 대가 14분 동안 99.97% 이상 주고받았다. AOO 가 상대를 못 찾는 NAT 조합(대칭형)은 지금 SonoBus 와 똑같이 실패한다 (3.6).
- **ensemble-rust 가 이미 가진 것은 핵심의 일부다**: Opus, 암호화 패킷, 재정렬 버퍼, 클록 차이 보정, 믹서, 할당 0 시험. Meon 이 쓰는 기능 중 PCM16, 자동 지터 버퍼, 상대별 통계·핑, 채팅, 미터·모니터 경로, 48k 가 아닌 장치, C 연결층은 **새로 만들거나 크게 고쳐야 한다** (4장).
- **채팅은 AOO 기능이 아니다.** 지금 채팅·핑·상대 정보는 SonoBus 자체 `/sb/...` OSC 메시지다. 이 AOO 버전에는 상대에게 메시지를 보내는 API 가 없다. → ensemble-rust 제어 패킷으로 옮기자고 제안한다 (질문 1).
- **SonoBus 종속을 다 없애려면 엔진만이 아니라 앱 껍데기도 바꿔야 한다** (`SonoStandaloneFilterApp/Window`, `SonobusAudioProcessor`). 계획에 넣었다 (6장, 7장).
- **라이선스가 들여오기 방식보다 먼저다.** desktop 은 공개, ensemble-rust 는 비공개 저장소다. JUCE 를 GPL 로 쓰는 한 ensemble-rust 를 링크해 배포하면 그 소스도 GPL 로 줘야 한다 (질문 2).
- **크기: 전체 크다.** 추정 9–14주, 가운데 약 11주 (오차 큼, 브리지 제외). 1월 4일까지 남은 약 10주보다 길다. 1월 4일 출시는 이 교체에 걸지 않기를 권한다 (9장).

---

## 1. 지금과 바꾼 뒤

### 1.1 지금

```
화면 ─ JamSession ─ Engine.h ─ EngineLink ─ SonobusAudioProcessor (SonoBus, GPL)
                                              ├ UDP 소켓 1개 + 받기·보내기·이벤트 스레드
                                              ├ AOO 클라이언트 (서버 TCP, 구멍 뚫기)          ← deps/aoo (BSD)
                                              ├ AOO 소스·싱크 (PCM16/Opus, 지터, 재전송)        ← deps/aoo (BSD)
                                              ├ /sb/chat·ping·pinfo… OSC 메시지 (SonoBus 코드)
                                              ├ 채널 그룹·이펙트·메트로놈·녹음·파일 재생·미터
                                              └ processBlock (E10 관문)
독립 앱 껍데기: SonoStandaloneFilterApp.cpp + SonoStandaloneFilterWindow.h (SonoBus, GPL)
```

### 1.2 바꾼 뒤

```
화면 ─ JamSession ─ Engine.h ─ RustEngine (C++, Source/meon/engine/)
                                 ├ Transport (C++): UDP 소켓 1개, AOO 클라이언트, 나누기, 허용 목록
                                 │     받기 스레드 ─┬─ "/" "#"  → AOO 클라이언트
                                 │                  └─ "EJ"     → ens_net_* (Rust)
                                 │     보내기 스레드 ← AOO send() + Rust 가 만든 패킷
                                 ├ MeonFilePlayer (C++, JUCE AudioTransportSource)
                                 ├ EarGuard (지금 Meon 코드 그대로)
                                 └ ensemble-rust 정적 라이브러리 (C ABI, 소켓·스레드 없음)
                                        net 쪽(패킷 열기·봉하기·제어) / audio 쪽(블록 처리) / ctrl 쪽(설정·통계)
MeonProcessor (C++, juce::AudioProcessor) ← JUCE 오디오 장치 콜백이 블록마다 부름
Meon 독립 앱 껍데기 (C++, JUCE 표준 standalone 기반)
```

- 오디오 장치(ASIO, 샘플레이트, 입력 채널)는 지금처럼 JUCE `AudioDeviceManager`. ensemble-rust 의 CPAL 코드는 쓰지 않는다.
- AOO 는 클라이언트 파일만 쓴다 (`client.cpp`, `net_utils.cpp`, `time.cpp`, `sync.cpp`, `common.cpp`, oscpack, md5). 소스·싱크·Opus 코덱 부분은 빠진다. AOO 버전은 서버(essej/aooserver 81a4cf9)와 같은 2.0.0-pre2 그대로, E8·E9 포함.
- SonoBus 파일은 고치지 않는다 (E 번호 필요 없음). 새 엔진은 CMake 옵션으로만 켠다 (`MEON_ENGINE=rust`, 기본 `sonobus`). 그 빌드에는 SonoBus 소스가 아예 들어가지 않아, 남은 의존이 있으면 링크에서 드러난다.

### 1.3 ensemble-rust 의 지금 상태 (main 29aeb51 기준, 약 3,900줄)

| 있음 | 없음 |
|---|---|
| 48 kHz 스테레오, 5 ms(240 샘플) 고정 프레임 | 48k 가 아닌 장치, 모노, 다른 프레임 크기 |
| Opus 256 kbps CBR (`codec.rs`) | PCM16 |
| EJR1 패킷: 방·상대·세션 ID, 순번, XChaCha20-Poly1305 (`packet.rs`) | 코덱·채널 표시, 제어 메시지(핑·채팅·상대 정보) |
| 재정렬 버퍼 128칸, 중복·늦음 거부, Opus PLC (`jitter.rs`) | **자동 지터 버퍼** (지금은 고정 `latency_frames`) |
| 클록 차이 보정 ±0.2% sinc 리샘플 (`DriftBuffer`) | 상대별 통계 (지금 `Stats` 는 전체 합) |
| 믹서: 상대별 gain·밸런스·뮤트·솔로, 마스터, 리버브 (`engine.rs`) | 미터, 셀프 모니터, 모니터 음소거, 파일 소리 |
| 입력 게이트·컴프레서·EQ (`effects.rs`) | (Meon 은 이펙트를 끄므로 안 씀) |
| 실행 구조: 작업 스레드 하나가 소켓·인코딩·디코딩·믹싱 (`runtime.rs`), CPAL 콜백은 링 버퍼만 | JUCE 콜백에서 블록마다 부르는 API, C ABI |
| 할당 0 시험 (`tests/steady_state.rs`), `#![forbid(unsafe_code)]` | NAT 통과 (README: "미구현") |

`Engine::render` 는 상대 수로 나눠 섞는다 (`1/N`). Meon 은 사람마다 정한 크기로 그대로 더해야 하므로(SonoBus 와 같게) 고친다.

---

## 2. 정해진 방향과 다르게 제안하는 것 (먼저 묻는다)

| # | 정해진 방향 | 사실 | 제안 |
|---|---|---|---|
| 1 | 채팅 전송은 AOO 라이브러리를 계속 쓴다 | 채팅은 AOO 기능이 아니다. 지금 채팅은 SonoBus 코드의 `/sb/chat` OSC 메시지를 UDP 로 상대마다 보내는 것이다 (`SonobusPluginProcessor.cpp` `sendChatEvent`). 이 AOO 버전에는 상대 메시지 API 가 없다 (`aoo_net.h`: "LATER add API functions … chat"). 핑(`/sb/ping`)·상대 정보(`/sb/pinfo`)도 같다 | **채팅·핑·상대 정보를 ensemble-rust 제어 패킷(`EJC1`)으로**: 같은 포트·같은 방 키로 암호화. 크기 작다. 전달 확인(ack 3초)은 지금처럼 `ChatChannel` 이 한다. 다른 길: Meon 서버 WebSocket — 서버를 바꿔야 해서 비추천 |
| 2 | (정해지지 않음) ensemble-rust 를 앱에 링크 | **desktop 은 공개 저장소, ensemble-rust 는 비공개다.** desktop CI 는 기본 토큰으로 비공개 저장소를 받을 수 없다. 더 큰 문제는 라이선스: JUCE 를 GPL 로 쓰는 한(지금 SonoBus 포크가 그렇다) ensemble-rust 를 링크한 앱을 배포하면 받은 사람에게 ensemble-rust 소스도 GPL 조건으로 줘야 한다. 테스터 빌드도 배포다 | **라이선스 방향을 먼저 정한다** (질문 2). 그 전까지는 submodule + 읽기 전용 토큰(Actions 비밀값)으로 **CI 빌드만** 하고, 새 엔진 빌드를 사람에게 배포하지 않는다. subtree 로 desktop 에 넣는 것은 ensemble-rust 를 공개하는 것과 같다 |
| 3 | Engine.h 창구 뒤만 바꾼다 | 창구 뒤만 바꾸면 SonoBus 껍데기(독립 앱 창·장치 관리 `SonoStandaloneFilterApp/Window`, `SonobusAudioProcessor` 의 MeonCore 소유)가 남는다. 껍데기는 `dynamic_cast<SonobusAudioProcessor*>` 로 엔진에 묶여 있다 | 새 엔진 빌드에서 **MeonProcessor + Meon 껍데기** 로 바꾼다 (JUCE 표준 standalone 기반). 화면·JamSession 은 그대로 |
| 4 | (ensemble-rust 는 5 ms 고정) | 지금 SonoBus PCM16 은 오디오 블록 단위로 보낸다 (128 샘플 = 2.67 ms). 5 ms 프레임은 보내는 쪽에서 평균 +2 ms 쯤 늦다 | **프레임 2.5 ms (120 샘플)**: PCM16·Opus 둘 다 되는 크기 |

---

## 3. 포트 공유

### 3.1 소켓 주인과 스레드

소켓은 C++ `Transport` 가 갖는다. 지금 SonoBus 와 같은 모양이다 (SonoBus 도 소켓을 직접 갖고 AOO 에 보내기 콜백을 준다, `SPP.cpp` 937·1012·2319).

| 스레드 | 하는 일 | 우선순위 |
|---|---|---|
| 받기 | `recvfrom` → 나누기 → AOO `handle_message` 또는 `ens_net_ingest` (Rust 가 복호·인증 후 오디오 쪽 링에 넣음) | 실시간 (지금 SonoBus 받기 스레드와 같게) |
| 보내기 | AOO `send()` (서버·상대 핑), `ens_net_poll` 이 내준 패킷 `sendto`, 제어 타이머 (핑 1초, 정보) | 실시간 |
| AOO `run()` | 서버 TCP (로그인·그룹·상대 알림) | 보통 |
| 메시지 | AOO 이벤트·채팅 → `Engine::Listener`, 설정 바꾸기, 통계 읽기 | 메시지 스레드 |
| 오디오 | JUCE 콜백 → `ens_audio_process` 한 번 (5장) | 장치 콜백 |

ensemble-rust 는 sans-IO 다: 받은 바이트를 넣고, 보낼 바이트를 꺼내 가는 함수만 있다. 그래서 Rust 쪽은 소켓·OS 를 몰라도 되고 시험하기 쉽다 (실험의 `ffi/` 가 이 모양).

보내기 스레드 깨우기: 오디오 스레드가 패킷을 링에 넣은 블록에서만, Rust 호출이 끝난 뒤 C++ 가 OS 세마포어를 한 번 올린다 (`semaphore_signal`/`SetEvent`/`sem_post`, 기다리지 않음). 보내기 스레드는 그와 별도로 2 ms 마다 깨어 링을 본다 (깨우기를 놓쳐도 멈추지 않게). 맥에서 깨우기가 느리면(#73 측정: VM 에서 최대 수 ms) 1 ms 주기 확인만 쓰는 방식으로 바꾼다 (평균 +0.5 ms). 실측으로 고른다.

### 3.2 받은 패킷 나누기

| 첫 바이트 | 보내는 쪽 | 가는 곳 |
|---|---|---|
| `/aoo/…` (OSC) | AOO 서버 응답, 상대의 구멍 뚫기 핑 | AOO 클라이언트 `handle_message` |
| `#bundle` | (AOO 가 묶음으로 보낼 때 대비) | AOO 클라이언트 |
| `EJR2` | 상대 오디오 | `ens_net_ingest` |
| `EJC1` | 상대 제어 (인사·핑·정보·채팅) | `ens_net_ingest` |
| 그 밖 (`/sb/…` 포함) | 옛 SonoBus 앱 등 | 버리고 센다 |

OSC 주소는 늘 `/` 로, 번들은 `#` 로 시작하므로 `EJ` 와 겹치지 않는다.

### 3.3 주소 ↔ 사람 ↔ 키

- AOO 가 상대를 찾으면 `AOONET_CLIENT_PEER_JOIN_EVENT` 로 (사용자 이름, 그 상대에 닿은 주소)를 준다. `Transport` 는 이 표를 갖는다.
- **허용 목록 (E1 과 같은 뜻)**: 목록에 있는 사람만 Rust 에 등록한다. 목록 밖이면 주소만 대기 표에 둔다 (소리 0). 목록이 바뀌면 등록·해제한다. 멤버 목록이 AOO 입장보다 늦게 와도 된다.
- E5 (서버 밖에서 모르는 주소의 요청)는 생기지 않는다: 새 엔진에는 "모르는 주소의 초대를 받아 상대 만들기" 길이 없다. AOO 가 알려 주고 허용된 주소의 `EJ` 패킷만 받는다.
- 방 키 = HKDF-SHA256(그룹 비밀번호, 그룹 이름). 상대 ID = SHA-256(AOO 사용자 이름) 앞 8바이트. 서버 필드를 새로 만들지 않는다 (API 2.8 의 값 그대로).
- 받은 패킷의 상대 ID 가 **보낸 주소의 AOO 사용자**와 다르면 버린다 (실험에서 확인).
- 보안 수준: 지금 SonoBus PCM 은 평문이다. 새 엔진은 암호화·인증되므로 나빠지지 않는다. 추방된 사람은 그룹 비밀번호를 알지만(API 2.8: 바꾸지 않음) 허용 목록에서 빠져 주소가 등록되지 않으므로 그 사람 패킷은 버린다. 경로 중간에서 엿듣는 것까지 막으려면 방 키를 서버가 따로 줘야 한다 (지금 범위 밖).

### 3.4 패킷 (제안)

- 오디오 `EJR2`: 지금 EJR1 헤더에 코덱(PCM16/Opus)·채널 수(1/2)·프레임 크기(120)를 넣는다. 세션 ID·순번·인증 태그는 그대로. PCM16 스테레오 2.5 ms 는 본문 480 B + 헤더·태그 72 B.
- 제어 `EJC1`: 같은 키, 오디오와 다른 세션 ID (nonce 가 겹치지 않게). 종류: 인사(엔진 버전·형식), 핑·퐁(보낸 시각만, 시계 맞추기 불필요), 정보(지터 버퍼 ms, 장치 입출력 지연, 송출 중인지, 받기 원하는지), 채팅(태그 + 1000 B 이하 UTF-8).
- 대역폭 (상대 1명, 한 방향): PCM16 스테레오 약 1.92 Mbps (지금 SonoBus 128 블록 약 1.88 Mbps). 헤더 줄이기는 나중.
- NAT 유지: 소리 패킷이 흐르면 매핑이 유지된다. 송출 정지 중에도 핑(1초)과 AOO 자체 핑이 오간다.

### 3.5 같은 방에 옛 앱과 새 앱이 섞이면

서로 소리가 안 들린다 (프로토콜이 다름). AOO 상대 찾기는 되므로 `connected` 는 인사(`EJC1`) 왕복으로 판정한다. 인사가 없고 `/aoo/` 오디오 메시지만 오면 "다른 엔진" 으로 로그에 남긴다. 기본값을 바꿀 때 서버 `min_build` 를 올려(API 13장, 강제 업데이트) 섞이지 않게 한다. 시험 기간에는 같은 방 사람이 모두 같은 빌드를 써야 한다.

### 3.6 작은 검증 결과

ensemble-rust `experiments/aoo-port-share` (커밋 11a11f1). C++ 실험 프로그램 `ejr-aoo-probe` 가 UDP 소켓 하나로 AOO 클라이언트(desktop `deps/aoo` 복사)와 EJR1 을 함께 쓴다. EJR1 은 AOO 가 알려 준 주소로만, 같은 소켓에서 보낸다 (5 ms 마다 Opus 테스트 톤).

**컨테이너 (리눅스 네트워크 이름공간으로 공유기 두 개 + 인터넷 + 운영과 같은 aooserver, 시나리오마다 30초)**

| 시나리오 | AOO 상대 찾기 | EJR1 받음 / 상대가 보냄 | 결과 |
|---|---|---|---|
| 일반 NAT ↔ 일반 NAT | 0.1초 | 5959/5959, 5958/5959 (마지막 1개는 종료 순간). 붙은 뒤 첫 패킷 5 ms | 통과 |
| **같은 NAT 쌍, EJR1 만 다른 포트에서** (대조) | 0.1초 | **0/5960** 양쪽 | 실패 = 같은 포트가 필요 |
| 같은 컴퓨터 두 앱 | 0.1초 (LAN 주소) | 5959/5959 양쪽 | 통과 |
| 세 명 (둘은 같은 공유기, 하나는 밖) | 0.1초 | 모든 쌍 100% | 통과 |
| 일반 NAT ↔ 대칭 NAT | 실패 (5초 시간 초과) | 시작 안 함 | 실패 |
| 대칭 NAT ↔ 대칭 NAT | 실패 | 시작 안 함 | 실패 |

- 인증 실패 0, 모르는 주소에서 온 EJR1 0. 복호한 톤이 들린 프레임: 시작 4프레임(지터 버퍼 채우기) 빼고 전부.
- 대칭 NAT 실패는 AOO 단계다. **새 엔진의 NAT 통과 능력은 지금 SonoBus 와 같다** (같은 AOO 코드가 같은 주소를 찾는다). 더 좋게 하려면 중계 서버가 필요하다 (범위 밖).
- 처음 시험에서는 NAT 를 흉내 낸 리눅스가 먼저 도착한 상대 핑을 자기 앞 연결로 기록해 포트를 바꿔서 양쪽 다 실패했다. 집 공유기처럼 "밖에서 먼저 온 연결은 버림" 규칙을 넣자 통과했다. 이 규칙이 없는 공유기라면 AOO 단계에서 같은 일이 나므로 지금 SonoBus 도 같다 (EJR1 과 무관).
- 이 컨테이너는 aoo.meon.co.kr 이름을 찾지 못해 운영 서버에 붙을 수 없다. 그래서 운영 서버 시험은 GitHub Actions 러너로 했다 (아래).

**운영 서버 aoo.meon.co.kr (GitHub Actions 러너 3대, 2026-10-22 04:11–04:28 UTC)**

ensemble-rust 워크플로 `AOO port-share probe (experiment)` 1번 실행. 리눅스·맥·윈도우 러너가 서로 다른 공인 주소에서 일회용 이름(그룹 `ejrprobe-<실행번호>`)으로 운영 서버에 붙었다. 세 러너 모두 서버 TCP 로그인·UDP 핸드셰이크(공인 주소 알아내기)·그룹 입장 성공.

| 쌍 | AOO 상대 찾기 | EJR1 (같은 포트, 약 14분) |
|---|---|---|
| 리눅스 ↔ 윈도우 | 성공. 붙은 뒤 첫 EJR1 8 ms / 27 ms | 리눅스→윈도우 170,551 보냄 → 170,493 받음 (99.97%). 윈도우→리눅스 170,576 → 170,546 (99.98%). 인증 실패 0, 모르는 주소 0. 복호한 톤이 들린 프레임 거의 전부 |
| 맥 ↔ 리눅스, 맥 ↔ 윈도우 | 실패 (5초 시간 초과) | 시작 안 함 |

- 맥 러너는 VM 안 사설 주소에서 포트가 바뀌는 NAT 를 거쳤다 (로컬 포트 ≠ 서버가 본 공인 포트). 대칭형 NAT 로 보이지만 확인하지는 못했다. 어느 쪽이든 AOO 단계 실패라 지금 SonoBus 도 이 조합에서는 소리가 안 간다.
- 실행 로그의 "95% of 200/s" 는 상대가 먼저 끝난 뒤의 시간까지 나눈 값이라 낮게 나온다. 위 표는 보낸 개수 대비 받은 개수다.
- 운영 aooserver 메모리에 시험용 사용자 3개·그룹 1개가 남는다 (서버를 다시 켜면 사라짐).

사람이 직접 해 보는 방법: 같은 실행의 Artifacts 에서 맥·윈도우 실행 파일을 받아, 서로 다른 네트워크(예: 집 와이파이 + 휴대폰 핫스팟)의 두 컴퓨터에서 같은 그룹 이름으로 실행한다. 방법은 ensemble-rust `experiments/aoo-port-share/README.md`. Meon 계정과 무관한 일회용 이름을 쓰고, 마이크·스피커는 쓰지 않는다.

---

## 4. Engine.h 대응표

"ensemble-rust" 칸: **있음** = 이미 있음(노출만), **고침** = 있는 것을 고치면 됨, **새로** = Rust 에 새로, **C++** = ensemble-rust 와 무관 (AOO·JUCE·Meon C++ 로).

### 4.1 함수별

| Engine.h | 지금 (EngineLink → SonoBus) | 새 구현 | ensemble-rust | 크기 |
|---|---|---|---|---|
| `addListener` `removeListener` | ListenerList | 그대로 | C++ | 작다 |
| `applyDefaults` | PCM16 고르기, 이펙트·리버브·메트로놈 끔, 파일 송신 켬, 자동 재접속 끔, 미터 유지 25 ms | 새 엔진의 기본이 곧 Meon 값 → 거의 빈 함수 | 새로 (PCM16, 4.2) | 작다 |
| `connect` `disconnect` `isConnected` `connectedUser` | SonoBus 가 감싼 AOO 클라이언트 | `Transport` 가 AOO 클라이언트를 직접 감쌈. `disconnect` 때 남은 상대를 모두 지우는 지금 동작 유지 | C++ (AOO 재사용) | 중간 (Transport 전체) |
| `joinedGroup` `joinGroup` `leaveGroup` | 같음 | `Transport` | C++ | (위에 포함) |
| `setAllowedPeers` (E1) | 엔진 수정 E1·E5 | `Transport` 주소 표 (3.3) | C++ | 작다 |
| `peers()` | 엔진 getter 여러 개 (그때마다 `mCoreLock`) | 고정 상대 칸의 원자 값 스냅숏 + `Transport` 연결 상태. 잠금 없음 | 고침 + 새로 | 중간 |
| · `connected` | AOO 상대 + 소스·싱크 | AOO 상대 찾기 성공 + `EJC1` 인사 왕복 | 새로 | |
| · `recvActive` | 싱크가 받는 중 | 받는 중 && 상대가 "송출 중" 알림 && 내가 음소거 안 함 | 새로 | |
| · `sendActive` | 소스가 보내는 중 | 송출 정지 아님 && 상대가 받기 원함 | 새로 | |
| · `packetsReceived` `packetsDropped` | AOO 이벤트 | 상대별 카운터 (지금은 전체 합) | 고침 | |
| · `packetsResent` | AOO 재전송 | 늘 0 (재전송 없음, 6장) | — | |
| · `packetsSent` | 소스 카운터 | 상대별 카운터 | 새로 | |
| · `pingMs` | `/sb/ping` | `EJC1` 핑·퐁 1초, 평활 | 새로 | |
| · `roundtripMs` | 핑 + 양쪽 지터 버퍼 + 양쪽 장치 지연 + 코덱 (`getRemotePeerLatencyInfo`) | 같은 공식, 상대 값은 `EJC1` 정보 | 새로 | |
| · `bufferMs` | 지터 버퍼 목표 | 상대별 목표 (프레임 × 2.5 ms) | 고침 | |
| · `recvChannels` | 받는 형식 | 패킷 헤더 채널 수 | 고침 (지금 스테레오 고정) | |
| `removePeer` | `removeRemotePeer` | 표에서 지우고 Rust 칸 반납 | C++ + 고침 | 작다 |
| `setPeerReceive` (멤버 음소거) | AOO 초대 끊기 → 상대가 보내기를 멈춤 | `EJC1` "보내지 말아요" + 내 쪽 음소거 | 새로 | 작다 |
| `setPeerGainDb` | 채널 그룹 gain | `PeerControls.gain` (0..2, −60~+6 dB 범위 맞음). 런타임 원자 값 + 램프로 | 있음 (고침) | 작다 |
| `setPeerPan` | 모노는 위치, 스테레오는 L/R 함께 밈 | `PeerControls.pan` 은 밸런스(한쪽 줄이기) → Meon 방식으로 | 고침 | 작다 |
| `peerLevel` | foleys 미터 (사람이 빠지면 메모리가 사라짐 → E10 TSan 크래시) | 고정 칸 원자 피크 (25 ms 유지). 빠진 사람을 읽어도 안전 | 새로 | 작다 |
| `applyPeerDefaults` | 상대별 PCM16 + 버퍼 모드 + 미터 유지 | 상대별 버퍼 모드 | 새로 (4.2 자동 지터) | — |
| `isSendMuted` `setSendMuted` | `sendmute` 파라미터 → 소스 정지 | 소리 패킷을 안 만듦. 제어·핑은 계속(NAT 유지). 상대에게 "송출 정지" 알림 → 상대 `recvActive` false (끊김으로 안 봄) | 새로 | 작다 |
| `setSendGainDb` | 입력 그룹 gain (송신·입력 미터·셀프 모니터가 그 뒤에서 갈림) | 입력 gain. `InputFx` 의 `gain_db` 는 시작 때만 → 런타임 값 | 고침 | 작다 |
| `setSelfMonitor` `setSelfMonitorDb` `setSelfMonitorPan` (E4) | dry 0/1 + 입력 그룹 모니터 gain·`monitorPan` | 장치 샘플레이트에서 바로 (리샘플 없음), 송출과 따로 패닝 | 새로 | 작다 |
| `setEarGuardLevel` | `meon::EarGuard` (Meon 코드) | 그대로, Rust 출력 뒤 C++ 에서 | C++ 재사용 | — |
| `setMasterDb` | `wet` 0..2 | `master_gain` 0..2 (범위 맞음). 셀프 모니터·상대·파일 합 뒤에 곱함 (지금과 같은 순서) | 있음 (고침) | 작다 |
| `setMonitorMuted` | wet 0 + dry 0 | 모니터 출력만 0 (보내기는 그대로) | 새로 | 작다 |
| `reapplyMonitor` | 엔진이 저장 상태로 덮을 때 다시 씀 | 덮는 상태가 없음 → 빈 함수 | — | — |
| `setBufferMode` | 기본 자동 모드 | 새 상대의 기본 모드 | 새로 (4.2) | — |
| `setInputChannels` `inputChannelCount` `inputChannelText` | 입력 그룹 1개, `sendchannels` | C++ 가 장치 채널을 고르고 Rust 에 모노/스테레오 송신 설정 | 고침 (모노) | 작다 |
| `outputAvailable` `audioDescription` `deviceInfo` | AudioDeviceManager | 그대로 옮김 | C++ | — |
| `inputLevel` `sendLevel` `monitorLevel` | foleys 미터 3개 | 원자 피크: 입력 처리 뒤 / 실제 송신 (파일 포함) / 귀 보호 뒤 출력 (C++) | 새로 | 작다 |
| `sendChannelCount` | `getSendChannels` | 설정값 | 새로 | 작다 |
| `peerMixSkippedBlocks` (E10) | 관문이 건너뛴 블록 | 잠금·관문이 없어 늘 0. 대신 "링이 가득 차 버린 패킷 수" 를 로그에 | — | 작다 |
| `sendChat` | `/sb/chat` (SonoBus 코드) | `EJC1` 채팅 (질문 1) | 새로 | 작다 |
| 파일 11개 (`loadFile` … `setFileGainDb`) | SonoBus 의 AudioTransportSource + 읽기 스레드, EngineLink `PlaybackFader` | C++ `MeonFilePlayer` 로 옮김 (JUCE, 읽기 스레드). 블록마다 파일 소리를 Rust 에 넘겨 송신·모니터에 섞음 (gain 램프) | C++ + 고침 (입력 2개) | 중간 |
| Listener 8개 | AOO 이벤트 → 메시지 스레드 | `Transport` (AOO 이벤트) + Rust (채팅) → 메시지 스레드 | C++ | 작다 |

### 4.2 꼭 짚을 항목

**PCM16 (Meon 기본값)** — 크기 중간
- 지금: PCM 16bit, 오디오 블록 단위 (48 kHz·128 샘플이면 2.67 ms, AOO 패킷 600 B 안), 빠진 블록은 무음.
- 새로: 코덱 {PCM16, Opus}, 프레임 120 샘플, 모노/스테레오. 빠진 프레임은 1 ms 안에 0 으로 줄여 딸깍 소리를 막는다 (PCM 은 PLC 가 없다). Opus 는 지금 코드 그대로 (선택지로만 남김).

**자동 지터 버퍼 (BufferMode latency/stable)** — 크기 중간, 귀에 가장 잘 들리는 위험
- 지금 SonoBus 규칙 (`SPP.cpp` 3946–4140): 끊김(블록 손실)이 나면 +1 블록 (0.5초에 한 번까지). 지연 우선(AutoFull)은 10초 동안 끊김이 없으면 −1 블록 (10초에 한 번까지), 줄인 지 2초 안에 또 끊기면 그 값을 바닥으로. 안정 우선(AutoIncreaseOnly)은 늘리기만. 시작 7초는 초기 구간.
- 새로: 같은 규칙을 2.5 ms 프레임 단위로, 상대마다. 늘릴 때는 바로(무음 한 프레임), 줄일 때는 이미 있는 재생 속도 보정(±0.2%)으로 천천히 (소리가 튀지 않음).
- 다른 점: SonoBus 는 장치 블록 크기 단위라 장치마다 단위가 다르다. 같은 조건에서 비교해 고친다 (8장).

**상대별 핑·왕복·버퍼 ms·패킷 통계** — 크기 작다~중간
- 핑: `EJC1` 핑에 내 시각을 넣고 퐁으로 돌려받아 내 시계로만 잰다. 1초마다, 평활.
- 왕복: 지금 공식 그대로 = (상대 입력 지연 + 핑/2 + 내 지터 버퍼 + 코덱) + (상대 출력 지연 + 핑/2 + 상대 지터 버퍼 + 코덱). 상대 값은 `EJC1` 정보(1초마다·바뀔 때).
- 통계: 상대 칸마다 원자 카운터 (받음·빠짐·보냄). `peers()` 는 잠금 없이 읽는다.

**미터·셀프 모니터·귀 보호·마스터·모니터 음소거** — 크기 작다

```
입력(고른 채널) ×송출 레벨 ─[입력 미터]─┬─ +파일×파일 gain ─[송신 미터]─▶ (송출 정지면 멈춤) → FIFO → 48k 로 → 인코딩 → 패킷
                                       └─ 셀프 모니터 (켜짐·크기·패닝) ─┐
상대마다: 지터 버퍼 → 디코딩 → 장치 샘플레이트로 (+클록 보정) → gain·pan·음소거 ─[상대 미터]─ Σ ─┤
파일 ×파일 gain ──────────────────────────────────────────────────────────────────────┤
                                          (Σ) × 마스터 → 모니터 음소거 → [C++] 귀 보호 → [모니터 미터] → 출력
```
SonoBus 와 같은 순서다 (`SPP.cpp` 7861 송신 미터, 8162 dry, 8313 파일 모니터, 8332 wet, 8344 귀 보호, 8346 출력 미터).

**허용 목록 (E1)·송출 정지·파일 소리 섞기** — 3.3, 4.1 표. 셋 다 엔진 수정 없이 새 구조의 기본 동작이 된다.

**44.1 kHz 등 48k 가 아닌 장치** — 크기 중간
- 네트워크는 늘 48 kHz. 장치 쪽은 장치 샘플레이트 그대로 섞는다 (셀프 모니터·파일은 리샘플 없음 = 지연 0).
- 보내기: 장치 → 48k 리샘플러 하나 (48k 장치면 건너뜀).
- 받기: 상대마다 이미 있는 클록 보정 리샘플러(`DriftBuffer`)가 48k → 장치 비율까지 맡는다 (단계가 하나 줄어 지연이 더 붙지 않음). 지금은 비율 1.0±1% 로만 만들 수 있어 기준 비율을 받게 고친다.
- 장치 샘플레이트가 바뀌면 (재생 중 44.1 ↔ 48) 오디오 스레드 밖에서 새로 준비해 바꿔 끼운다.

---

## 5. 실시간 규칙: 오디오 콜백 안의 ensemble-rust 호출은 할당·잠금·대기를 하지 않는다

### 5.1 오디오 스레드가 부르는 것

블록마다 `ens_audio_process(audio, in, in_ch, file_in, out, out_ch, frames)` 한 번. 그 밖의 Rust 함수는 오디오 스레드에서 부르지 않는다. 안에서 하는 일: 명령 링 비우기 → 받은 패킷 링 비우기(지터 버퍼에 넣기, 블록당 최대 개수 제한) → 입력·송신 경로 → 인코딩 → 보낼 링에 넣기 → 상대마다 디코딩·리샘플·섞기 → 미터 원자 값 쓰기.

### 5.2 지키는 방법

| 규칙 | 방법 |
|---|---|
| 스레드마다 자기 것만 만진다 | Rust 객체를 셋으로 나눈다: `Net`(받기·보내기 스레드), `Audio`(오디오 스레드), `Ctrl`(메시지 스레드). 각자 `&mut self` 만, 공유 `Mutex`/`RwLock` 없음 |
| 스레드 사이 전달 | 크기가 정해진 단일 생산자·단일 소비자 링(`rtrb`, 이미 의존성에 있음): 받은 패킷(net→audio), 보낼 패킷(audio→net), 명령(ctrl→audio), 버릴 것(audio→ctrl). 가득 차면 버리고 센다 (기다리지 않음) |
| 할당 없음 | 상대 칸 최대 개수(예: 16)를 준비 단계에 만든다: 지터 버퍼·디코더·리샘플러·작업 버퍼. 사람이 들어오면 준비된 칸을 켜고, 형식이 바뀌면(모노↔스테레오) 새 디코더를 `Ctrl` 에서 만들어 링으로 넘기고 옛것은 링으로 돌려받아 `Ctrl` 에서 버린다 |
| 값 바꾸기 | gain·pan·뮤트·마스터 등은 원자 값. 오디오는 블록 시작에 읽고 블록 안에서 램프 |
| 미터·통계 | 오디오가 원자 값에 쓰고 화면·`peers()` 가 읽는다 (서로 기다리지 않음) |
| 코덱·암호 | PCM16 은 산술만. libopus 인코딩·디코딩은 만든 뒤 할당하지 않는다. 암호화·복호는 net 스레드에서 (오디오 스레드는 평문만) |
| 패닉 | 릴리스 `panic = "abort"`. 오디오 경로에서 `unwrap`·인덱스 범위 밖이 없게 하고, 받은 바이트를 해석하는 코드는 퍼징한다 |
| OS 호출 없음 | 오디오 경로에 시스템 호출·파일·로그 없음. 보내기 스레드 깨우기만 C++ 가 Rust 호출 뒤 한 번 (3.1) |

### 5.3 확인 방법

1. Rust 시험: 지금 `tests/steady_state.rs` (할당 세는 전역 할당기)를 `ens_audio_process` 전체로 넓힌다. 사람 추가·삭제, 형식 바꾸기, 파라미터 바꾸기, 미터 읽기를 섞어 10만 블록 동안 오디오 스레드 할당 0.
2. 정적 규칙: 오디오 모듈에서 `Mutex`·`RwLock`·`Vec::push`·`Box::new` 등을 clippy `disallowed-methods/types` 로 막는다.
3. 실행 검사: 리눅스 CI 에서 Clang RealtimeSanitizer(`-fsanitize=realtime`, `processBlock` 에 `[[clang::nonblocking]]`)로 앱을 돌린다. 오디오 콜백 안에서 malloc·잠금·막히는 시스템 호출이 있으면 그 자리에서 멈춘다 (Rust 코드도 libc 를 거치므로 잡힌다).
4. `scripts/stall` (E10 때 쓴 도구)로 콜백 최대 시간·10 ms 넘은 콜백 수를 잰다. 기준: 10분 동안 10 ms 넘는 콜백 0.

### 5.4 E10 과 비교

E10 의 원인은 상대 목록을 여러 스레드가 `mCoreLock` 으로 같이 만진 것이었다 (오디오 스레드가 스핀락에서 70–960 ms 기다림). 새 구조에는 그런 공유 목록이 없다. 상대 추가·삭제는 링으로 보내는 메시지다. 그래서 관문(E10)도 필요 없다.

---

## 6. SonoBus 에서 빠지는 것

### 6.1 목록

| SonoBus 기능 | Meon 이 쓰나 | 새 엔진에서 |
|---|---|---|
| AOO 소스·싱크 (코덱·전송·지터·리샘플) | 씀 | ensemble-rust 로 |
| AOO **재전송** (빠진 패킷을 다시 요청) | 간접 (지터 버퍼가 클 때 손실을 메움, 로그 `packetsResent`) | **빠짐.** 손실은 무음 보정. 필요해지면 나중에 FEC 또는 재요청 |
| `/sb/chat` | 씀 | `EJC1` 채팅 (새로) |
| `/sb/ping` `/sb/pngack` | 씀 (`pingMs`, 지연 보정 자료 `LatencyProbe`) | `EJC1` 핑 (새로) |
| `/sb/pinfo` (상대 지터 버퍼·장치 지연) | 씀 (`roundtripMs`, 로그) | `EJC1` 정보 (새로) |
| `/sb/clayinfo` (채널 구성) | 씀 (`recvChannels`) | 패킷 헤더 채널 수로 |
| `/sb/reqlatinfo` `latinfo` `suggestlat` `blockedinfo` `suggestgroup` | 안 씀 | 빠짐 |
| 입력 그룹·다중 채널 라우팅 (ChannelGroup) | 1그룹 모노/스테레오만 | 단순화해 새로 |
| 입력 이펙트 (압축·확장·리미터·EQ, Faust), 메인 리버브 (zitaRev·MVerb) | 끔 | 빠짐 |
| 메트로놈, 사운드보드, 로컬 녹음, 지연 시험(echo·latency 소스, mtdm) | 안 씀 | 빠짐 |
| 처음 연결 때 안전 음소거 (손실 많으면 잠시 막음) | 간접 (붙는 순간 지글거림 방지) | 지터 버퍼 시작 대기로 대신 (새로, 작다) |
| 공개 그룹 감시, 최근 연결·자동 재접속(E7), 명령줄 연결(E6), 모르는 주소 초대(E5) | 안 씀 (막아 둠) | 빠짐 (막을 필요도 없어짐) |
| 파라미터 37개·상태 저장 (ValueTree), 호스트 자동화 | 안 씀 (숨겨 둠) | 빠짐 |
| 파일 재생 (AudioTransportSource + 읽기 스레드) | 씀 | C++ `MeonFilePlayer` 로 옮김 (새로, 중간) |
| 미터 (foleys LevelMeterSource) | 씀 | Rust 원자 피크 (새로) |
| 셀프 모니터·dry/wet·모니터 패닝(E4)·송출 채널 수 | 씀 | 새로 (작다) |
| 독립 앱 껍데기: 창, 장치 관리자·설정 파일, 메뉴, 마이크 권한, 닫기 확인, 크래시 처리 연결, `ShellHooks` | 씀 | **Meon 껍데기 (새로, 중간~크다)**. 장치 설정 파일 위치·키는 그대로 읽어 사용자가 고른 장치·채널을 잃지 않게 |
| `SonobusAudioProcessor` 의 MeonCore 소유(E2), 에디터 만들기, 창 크기 기억 | 씀 | `MeonProcessor` (새로, 작다) |
| 플러그인 래퍼 (AU·VST3 안의 엔진) | 씀 | 브리지(#73) 플러그인으로 (7.3) |
| E1·E4·E5·E10 엔진 수정 | 씀 | 필요 없어짐 (새 구조의 기본 동작). E8·E9 (AOO 클라이언트)는 그대로 씀 |

### 6.2 Meon 이 쓰는데 새로 만들어야 하는 것 (요약)

PCM16·모노·2.5 ms 프레임, 자동 지터 버퍼 2모드, 상대별 통계, 핑·왕복, 채팅, 송출 정지·받기 원함 알림, 미터 4종, 셀프 모니터(패닝 포함)·마스터·모니터 음소거 경로, 48k 가 아닌 장치, 파일 소리 섞기, 허용 목록, `Transport` (AOO 감싸기·나누기·스레드), C ABI, `RustEngine`, `MeonFilePlayer`, `MeonProcessor`, Meon 독립 앱 껍데기.

---

## 7. 단계별 작업

### 7.1 단계

| 단계 | 저장소 | 할 일 | 크기 |
|---|---|---|---|
| R0 | — | 질문 1–5 정하기, ensemble-rust 들여오기 (라이선스 결정 전에는 submodule + 읽기 토큰), CI 에 Rust 툴체인 | 작다 |
| R1-1 | ensemble-rust | 패킷 `EJR2`(코덱·채널·프레임), PCM16, 모노, 2.5 ms, 손실 보정 | 중간 |
| R1-2 | ensemble-rust | 오디오 쪽 블록 처리: 장치 샘플레이트, FIFO, 보내기 리샘플, 상대별 리샘플(기준 비율), 1/N 없는 합, 셀프 모니터·파일·마스터·모니터 음소거, 램프, 미터 | 크다 |
| R1-3 | ensemble-rust | 자동 지터 버퍼 2모드, 상대별 통계 | 중간 |
| R1-4 | ensemble-rust | net 쪽: 열기·봉하기, 상대 칸, `EJC1` (인사·핑·정보·송출 상태·받기 원함·채팅) | 중간 |
| R1-5 | ensemble-rust | C ABI 크레이트(정적 라이브러리·헤더·스레드 약속), 할당 0 시험 확장, 퍼징 | 중간 |
| R2-1 | desktop | `Transport`: 소켓, AOO 클라이언트 감싸기, 나누기, 스레드·우선순위, 허용 목록 | 중간 |
| R2-2 | desktop | `RustEngine` (`Engine.h` 전체), `MeonFilePlayer`, 귀 보호·출력 미터 연결 | 중간 |
| R2-3 | desktop | `MeonProcessor` + Meon 독립 앱 껍데기, 장치 설정 이어받기 | 중간~크다 |
| R2-4 | desktop | CMake `MEON_ENGINE=rust`, 맥 유니버설(arm64+x86_64 따로 빌드해 lipo), Windows MSVC, 리눅스. CI 세 OS + RTSan 작업 | 중간 |
| R3 | desktop | 비교 측정 (8장), 고치기 | 중간 (고칠 거리에 따라 늘어남) |
| R4 | — | 테스터 빌드(따로 번들 ID, 빌드 0 = 자동 업데이트 없음)로 실제 합주 2–3번. 배포이므로 질문 2 가 정해진 뒤 | 중간 (달력 시간) |
| R5 | desktop | 기본값 전환 + `min_build` 올리기 → SonoBus 소스 삭제 (삭제는 따로 승인). 플러그인은 브리지가 나온 뒤 | 작다 |

합계 추정: R1 약 4–5주, R2 약 3.5–5주, R3·R4 2–4주 → **9–14주, 가운데 약 11주**. 오차가 크다 (특히 R1-3 자동 지터, R3 측정 뒤 고칠 거리). 구현보다 실기 확인·측정이 오래 걸리는 쪽이다.

### 7.2 위험

| 위험 | 크기 | 줄이는 법 |
|---|---|---|
| 자동 지터 버퍼가 SonoBus 보다 자주 끊기거나 지연이 길다 | 큼 | SonoBus 규칙을 그대로 옮기고, 같은 조건 측정(8장)으로 맞춘 뒤에만 전환 |
| 프레임·리샘플·FIFO 로 지연이 늘어남 | 중간 | 2.5 ms 프레임, 받기 리샘플 단계 합치기, 클릭 측정으로 확인 |
| 옛 앱·새 앱이 같은 방에서 서로 안 들림 | 중간 | `min_build` 로 한 번에 전환, 시험 기간에는 같은 빌드끼리 |
| 플러그인: 브리지가 나오기 전까지 플러그인은 SonoBus 엔진 → SonoBus 종속이 완전히 없어지지 않음 | 큼 | 브리지 본작업을 따로 계획 (7.3) |
| 앱 껍데기 교체로 장치 설정·창·권한 동작이 달라짐 | 중간 | 설정 파일 위치·키 유지, 맥·윈도우 실기 점검표 |
| 빌드: Rust 툴체인, 맥 유니버설, Windows CRT, 빌드 시간 | 중간 | R2-4 를 일찍 (빈 C ABI 로 먼저 세 OS 링크) |
| AOO 재전송이 빠져 손실 많은 망에서 나빠짐 | 낮음~중간 | 측정. 필요하면 FEC |
| NAT 통과 | 변화 없음 | AOO 그대로 (3.6). 대칭 NAT 는 지금도 실패 |
| 라이선스 | 큼 (결정 필요) | SonoBus(GPL)를 없애도 JUCE 를 GPL 로 쓰면 앱은 여전히 GPL 이고, 링크한 ensemble-rust 도 소스를 줘야 한다. 비공개로 두려면 JUCE 상용 라이선스와 Steinberg ASIO SDK 계약을 따로 확인해야 한다. ensemble-rust 는 라이선스가 정해져 있지 않다(`publish = false`) → 사용 허락과 조건을 글로 남긴다. 법률 검토는 별도 |

### 7.3 플러그인

- 전제: 브리지(#73) 구조. 플러그인은 통로, 엔진은 앱. 앱 쪽은 가짜 장치 "MEON Bridge" 로 도는 같은 `RustEngine` 이라 엔진 코드에 플러그인용 분기가 없다.
- **브리지가 Logic(AUHostingService 샌드박스)에서 막히면**: 같은 `RustEngine`·`Transport` 를 플러그인 안에 직접 넣는다 (지금 구조와 같은 모양, `Engine.h` 그대로). 추가로 블록 크기가 블록마다 바뀌는 호스트·인스턴스 여러 개·오프라인 렌더 처리가 필요하다 (중간).

---

## 8. 비교 방법 (지금 엔진 vs 새 엔진, 같은 조건)

### 8.1 같은 조건

- 같은 컴퓨터·같은 장치·같은 버퍼(48 kHz, 128 샘플)·같은 망·같은 인원·같은 버퍼 모드(지연 우선)·PCM16.
- 두 빌드: 지금 main(SonoBus) / `MEON_ENGINE=rust`. 같은 방에 섞지 않고 차례로.

### 8.2 측정

| 측정 | 도구 | 보는 값 |
|---|---|---|
| 오디오 멈춤 | `scripts/stall` (`run-jam.sh one/sep`, `CHURN=2`), 10분씩 | 콜백 최대, 10 ms·100 ms 넘은 콜백 수, 출력 무음(2 ms↑) 횟수 |
| 클릭 왕복 지연 | `jamprobe` 확장 (새로, 작다): A 입력에 클릭(초당 4번) → B 출력에서 찾기 = 편도. B 출력을 B 입력에 이어 A 출력에서 찾기 = 왕복. 한 JACK 서버라 시계가 같아 샘플 단위로 정확 | p50·p90·최대 ms, 놓친 클릭 |
| 10분 끊김 수 | A 입력에 사인파 10분, B 출력의 무음 구간 + 세션 로그 `peerInterrupted` + 엔진 카운터(빠짐·보정) | 끊김 횟수, 총 무음 ms |
| 나쁜 망 | 네트워크 이름공간 + netem 으로 손실 0/1/3%, 지터 0/5/10/20 ms. 이 컨테이너 커널에는 netem 이 없어 GitHub 리눅스 러너에서 한다 (러너에서 되는지는 R3 첫 일로 확인) | 위 세 값 |
| 실제 인터넷 | 사람 두 명, 서로 다른 집, 10분 합주 두 번씩 | 세션 로그 끊김·핑·버퍼 ms, 들은 느낌 |
| 자원 | 같은 시험 중 CPU %, 메모리 | 평균·최대 |

### 8.3 전환 기준 (제안)

새 엔진이 모든 조건에서: 10 ms 넘는 콜백 0, 클릭 편도 지연 p50 이 지금 이하(+1 ms 안), 10분 끊김 수가 지금 이하, CPU 1.5배 이하. 실제 합주에서 들은 느낌이 나쁘지 않음.

---

## 9. 1월 4일 공개 출시 일정 (의견)

- 오늘(10월 22일)부터 약 10주다. 위 추정(9–14주, 가운데 약 11주)이 이미 더 길고, 브리지(플러그인)와 출시 준비 시간은 넣지도 않은 값이다.
- 출시 직전에 소리 엔진 전체를 바꾸면, 실제 사용자 망에서 검증할 시간이 거의 없이 첫인상(끊김·지연)을 거는 셈이다. 사용자에게 보이는 기능은 같다.
- 그래서 **1월 4일은 지금 엔진(E10 으로 멈춤을 고친 SonoBus)으로 내고, 새 엔진은 옵션 빌드로 나란히 키워 8장 기준을 넘으면 출시 뒤에 바꾸기**를 권한다.
- 다만 SonoBus 제거가 출시 조건(예: 비공개 배포 라이선스)이라면 계획이 달라진다. 그 경우 엔진뿐 아니라 JUCE 라이선스·껍데기·플러그인까지 1월 4일 전에 끝내야 해서, 범위를 줄이는 결정(예: 출시 때 플러그인 제외)이 먼저 필요하다 (질문 5).

---

## 10. 승인 전에 정할 것

1. **채팅·핑·상대 정보 경로**: ensemble-rust 제어 패킷(`EJC1`, 추천) / 다른 길?
2. **라이선스 방향과 ensemble-rust 들여오기**: (가) Meon 을 비공개로 → JUCE 상용 라이선스, ensemble-rust 비공개 유지, desktop 저장소도 SonoBus 를 뺀 뒤 비공개로. (나) 공개(GPL) 유지 → ensemble-rust 를 GPL 호환 조건으로 공개 (공동창업자 동의 필요). 정해지기 전에는 submodule + 읽기 토큰으로 CI 빌드만 한다.
3. **앱 껍데기 교체**를 이번 교체 범위에 넣는가 (추천: 넣음. 안 넣으면 SonoBus 종속이 남음).
4. **프레임 2.5 ms (120 샘플)** 로 바꾸는가 (추천).
5. **SonoBus 제거가 1월 4일 출시 조건인가.** 조건이 아니면 9장 권고대로, 조건이면 범위를 다시 잡는다.
