# 오디오 멈춤 측정 도구 (Linux, JACK)

합주 중 오디오 콜백이 멈추는지(엔진 처리 멈춤), 상대가 듣는 소리가 끊기는지 잰다.
엔진 멈춤 조사(2026-10, 원인: mCoreLock 우선순위 역전)와 E10(오디오 스레드 관문) 확인에 썼다.
앱 코드는 바꾸지 않는다. 개발 빌드(빌드 0)의 `--dev-signup` · `--dev-create` · `--dev-join` 만 쓴다.

| 파일 | 하는 일 |
|---|---|
| `stallprobe.c` | `LD_PRELOAD` 로 앱에 붙는다. 오디오 콜백 시간을 재고, 10 ms 넘게 멈추면 그 순간 모든 스레드 스택을 찍는다 |
| `jamprobe.c` | JACK 클라이언트. 앱 입력에 사인파를 넣고, 앱 출력에서 무음(2 ms 넘게)·멈춘 버퍼(같은 블록 반복)를 찾는다 |
| `run-jam.sh` | 2인 합주 한 번: 서버·JACK·앱·측정을 켜고 끈다. 배치 `one`(한 JACK 서버) / `sep`(서버 따로), 들어오고 나가기 반복(`CHURN`) |
| `summarize.py` | 결과 표 (콜백 최대·100 ms 넘은 횟수·끊김·건너뛴 블록·멈춘 곳) |
| `testenv.sh` | 로컬 시험 서버 (meon 서버 + AOO 서버 + 인증 메일 받기), 시험 계정 만들기 |
| `smtp_capture.py` | 인증 메일을 파일로 받는다 |

## 준비 (한 번)

- 패키지: `jackd2 libjack-jackd2-dev xvfb postgresql gcc python3 openssl curl`
- meon 서버: `cd server && go build -o meon-api ./cmd/api`
- AOO 서버: [essej/aooserver](https://github.com/essej/aooserver) 를 `Builds/LinuxMakefile` 에서 빌드
- 빈 시험용 Postgres DB. 서버는 PostgreSQL 18 의 `uuidv7()` 를 쓴다. 16 이하라면 시험 DB 에만 대신할 함수를 만든다:
  ```sql
  create or replace function uuidv7() returns uuid as $$
    select encode(set_bit(set_bit(overlay(uuid_send(gen_random_uuid()) placing
      substring(int8send(floor(extract(epoch from clock_timestamp()) * 1000)::bigint) from 3) from 1 for 6), 52, 1), 53, 1), 'hex')::uuid;
  $$ language sql volatile;
  ```
- 앱: 이 레포를 Linux 에서 빌드 (`cmake --build build --target MEON_Standalone`, 빌드 번호 0)

```bash
export MEON_API_BIN=…/meon-api AOO_SERVER_BIN=…/aooserver
export DATABASE_URL='postgres://meon@127.0.0.1:55433/meon_test?sslmode=disable'
export STALL_WORK=/tmp/meon-stall
. scripts/stall/testenv.sh
env_start && for u in stalla stallb stallc stalld; do make_user $u; done; env_stop
```

## 재기

```bash
APP=build/MEON_artefacts/RelWithDebInfo/Standalone/meon scripts/stall/run-jam.sh one10 600 one
APP=… scripts/stall/run-jam.sh sep10 600 sep
APP=… CHURN=2 SELF_MONITOR=1 scripts/stall/run-jam.sh churn10 600 sep    # 합주 중 2명이 들어오고 나가기 반복
python3 -I scripts/stall/summarize.py $STALL_WORK/out-one10 $STALL_WORK/out-sep10 $STALL_WORK/out-churn10
```

ThreadSanitizer: `-fsanitize=thread` 로 따로 빌드한 앱을 `NOPROBE=1` 로 돌린다. 경고는 `out-이름/tsan-a.*`, `tsan-b.*` 에 남는다.

## 읽는 법

- **콜백**: 128 프레임(2.67 ms) 블록. 정상은 0.05~3 ms. 100 ms 넘으면 그동안 그 앱의 출력이 멈추고(같은 조각 반복), 상대는 그 사람 소리가 끊긴다. 플러그인이면 DAW 전체 소리가 끊긴다.
- **멈춘 곳** (`summarize.py` 마지막 부분, `probe-*.log` 의 `=== 멈춤`): 오디오 스레드가 기다린 함수.
  - E10 전에는 `juce::ReadWriteLock::…` ← `SonobusAudioProcessor::processBlock` (엔진 잠금).
  - E10 뒤에 `pthread_rwlock_rdlock` ← `aoo::…::process` 같은 줄이 나오면 **AOO 내부 잠금**(E10 범위 밖)에서 기다린 것이다.
    그 스택과 같은 순간 다른 스레드가 잡고 있던 곳(같은 기록의 다른 스레드)을 함께 본다.
- **건너뛴 블록**: E10 관문. 쓰는 쪽(사람 추가·삭제, 형식 바꾸기)과 겹친 블록은 상대 소리 섞기·보내기를 건너뛴다 (그 블록만 상대 소리 2.67 ms 비고, 내 소리는 그대로). 앱 로그 `엔진이 상대 소리 섞기를 건너뛴 블록 (누적, E10)` 에서 센다.
- **무음** 은 네트워크·지터 버퍼 때문에도 생긴다 (특히 들어오고 나가는 순간). 엔진 멈춤과 구분하려면 콜백 시간과 시각을 맞춰 본다 (둘 다 CLOCK_MONOTONIC 초).
