# 브리지 실험 (MEON_BRIDGE_SPIKE)

싱크룸처럼 DAW 플러그인은 소리 통로만 하고, 화면·로그인·네트워크·합주 엔진은 독립 앱이 맡는 구조로 갈지 판단하려는 실험.
**제품에 넣지 않는다.** `-DMEON_BRIDGE_SPIKE=ON` 일 때만 빌드되고, 기본 빌드(MEON 앱·플러그인)는 바뀌지 않는다.

## 만드는 것

| 타깃 | 이름 | 하는 일 |
|---|---|---|
| `MEONBridge` (VST3, macOS AU) | Meon Bridge (test) | DAW 안의 소리 통로. 상태·측정 화면만 있다. 번들 ID `com.meon.MEONBridgeTest`, 플러그인 코드 `Mbrt` (지금 Meon 은 `com.meon.MEON`, `Mjam`) |
| `MEONBridgeApp` (독립 앱) | Meon Bridge App (test) | 지금 앱과 같은 코드 + 브리지 오디오 장치. 번들 ID `com.meon.MEONBridgeApp`. 자동 업데이트 꺼짐 (빌드 번호 0) |
| `meon_bridge_bench` (리눅스·macOS 명령줄) | – | 플러그인 호스트 없이 통로를 잰다 (가짜 호스트, 되돌려 보내는 앱, 앱 죽이기, 두 앱 끝에서 끝까지) |

## 구조

```
DAW 오디오 스레드                         공유 메모리 (meonbrg.shm)                    앱 브리지 스레드 (실시간)
PluginEndpoint::process()  ── 입력 칸[seq] ──▶  in[8] 링  ── 세마포어/이벤트 ──▶  AppEndpoint::run()
     (방식 A: 시간 제한 바쁜 대기)        ◀── 출력 칸[seq] ──  out[8] 링                  │
                                                                                         ▼
                                                                  BridgeDevice (JUCE AudioIODevice "MEON Bridge")
                                                                  → AudioDeviceManager → AudioProcessorPlayer → SonoBus 프로세서
```

- **통로** (`BridgeCore.*`, JUCE 없음): macOS·리눅스는 POSIX `shm_open` + 이름 있는 세마포어 `sem_open`, Windows 는 `Local\` 파일 매핑 + 자동 리셋 이벤트.
  공유 메모리 안은 블록 번호(seq)로 쓰고 읽는 칸 8개짜리 링 두 개 (입력·출력). 잠금 없음, 원자 변수만.
- **플러그인 쪽 실시간 규칙**: `process()` 안에서 할당·잠금·파일/네트워크 입출력 없음. 앱을 깨우는 `sem_post`/`SetEvent` 만 부른다.
  결과를 기다릴 때(방식 A)는 OS 객체로 기다리지 않고 시간 제한이 있는 바쁜 대기만 한다.
  (macOS 에는 시간 제한 있는 세마포어 대기가 없고, Windows 이벤트 대기는 1ms 단위라 64~128 샘플 블록에는 거칠다)
- **방식 A (같은 블록)**: 입력을 넘기고 같은 블록의 결과를 블록 시간의 50%(25/50/75% 고를 수 있음)까지 기다린다. 추가 지연 0, 늦으면 그 블록은 무음.
- **방식 B (한 블록 늦게)**: 이번 입력을 넘기고 지난 블록의 결과를 낸다. 기다리지 않는다. 추가 지연 = 블록 하나.
- **늦거나 앱이 없을 때**: 무음으로 낸다 (입력 통과가 아니라). 이유: 실제 제품에서 플러그인 출력은 "상대들 소리 + 내 모니터" 라서,
  늦은 블록에 입력을 그대로 내보내면 DAW 의 입력 모니터와 겹쳐 내 소리가 두 번 들리고, 처리된 소리와 원음이 블록마다 바뀌며 소리 크기가 튄다.
  무음은 끊김이 분명히 드러나 세기 쉽다.
- **앱 쪽**: 자기 오디오 장치를 열지 않는다. "MEON Bridge" 라는 가짜 오디오 장치를 등록해 고르면, 앱의 기존 오디오 경로가 그대로 돈다.
  샘플레이트·블록 크기는 플러그인(DAW)을 따른다 (`formatChanged` → 프로세서 `prepareToPlay`). 시계가 DAW 하나라 어긋남(드리프트)이 없다.
  플러그인 블록이 0.1초 넘게 없으면 자기 시계로 무음 블록을 돌려 합주 엔진이 멈추지 않게 한다 (결과는 버린다).
  Windows 셸은 처음 장치를 열기 전에 `meon::audio::preferredDriverType` 을 부르는데, 실험 빌드에서는 여기서 브리지 장치를 돌려준다
  (ASIO 드라이버는 보통 한 프로그램만 쓸 수 있어서 앱이 잠깐이라도 열면 DAW 와 부딪힌다).
- **연결 주인**: 플러그인 하나, 앱 하나. 0.25초마다 살아 있다고 알리고(1.5초 넘게 없으면 자리를 넘긴다), 두 번째 플러그인은
  "다른 Meon Bridge (test) 가 이미 연결돼 있어요" 를 보이고 무음을 낸다.
- **다른 버전이 남긴 공유 메모리**: 형식·크기가 다르면 지우고 새로 만든다 (POSIX). Windows 는 옛 버전 프로세스를 모두 끄면 사라진다.
- 엔진 코드(SonoBus·AOO)는 고치지 않았다. 고친 기존 파일: `CMakeLists.txt`(옵션·타깃, `sono_add_custom_plugin_target` 의 번들 ID 인자),
  `MeonEditor.cpp`·`MeonAudio.cpp`(`#if MEON_BRIDGE_SPIKE` 안에서만).

## 측정

- 플러그인 화면: 상태 한 줄, A/B, A 기다림 한도, 지연 측정(클릭), 숫자 초기화, 기록 파일 보기.
  - 기본 지연(DAW 블록) 과 브리지가 더한 지연(클릭 왕복)을 따로 보인다. 측정 중에는 클릭이 들리지 않게 출력을 끈다.
  - 늦은 블록, 앱 없던 블록, 왕복 시간 분포, 최대 기다림, 앱 최대 처리, 경과 시간.
  - 기록: `문서/Meon Bridge (test)/기록.txt` 에 10초마다 누적 숫자, 10분이 지나면 "10분 결과" 한 줄.
- 앱 상태 창: 플러그인 연결, 1단계(되돌려 보내기)·2단계(합주 엔진) 고르기, 처리·혼자 돈·건너뛴 블록, 앱 최대 처리, 실시간 우선순위.

## 시험 프로그램

```bash
cmake -B build -DMEON_BRIDGE_SPIKE=ON && cmake --build build --target meon_bridge_bench
build/meon_bridge_bench matrix --seconds=15          # 기준(통로 없음)/A/B × 블록 64·128·256·512 × 44.1k·48k
build/meon_bridge_bench chaos --mode=B --seconds=30  # 늦게 켜기, SIGKILL, 다시 켜기, SIGSTOP 2초, SIGTERM, 다시 켜기
build/meon_bridge_bench app --name=x & build/meon_bridge_bench host --name=x --bs=128 --mode=A --seconds=600
# 2단계: 두 앱이 같은 방에 있을 때 (앱은 --bridge-name=brgA --bridge-engine 로 띄운다)
build/meon_bridge_bench e2e --a=brgA --b=brgB --bs=256 --mode=B --seconds=60 --warmup=10
```

실험 앱의 개발용 실행 옵션 (빌드 번호 0): `--bridge-name=이름`(통로 이름), `--bridge-engine`(처음부터 2단계), `--bridge-no-window`.
