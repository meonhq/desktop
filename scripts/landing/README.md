# 소개 페이지 이미지·영상

`docs/marketing/landing/` 의 파일을 만드는 도구다. 앱 동작은 바꾸지 않는다. 개발용 실행 옵션(빌드 0)의 `--showcase` 장면만 쓴다.

## 만드는 법

- **macOS (권장)**: Actions 의 **Landing assets** 를 수동 실행한다.
  - macOS 러너에서 빌드 → 렌더링 → 파일 만들기를 한다.
  - 결과는 실행한 브랜치의 `docs/marketing/landing/` 에 커밋된다.
- **Linux (확인용)**:
  ```
  scripts/landing/capture.sh build/MEON_artefacts/RelWithDebInfo/Standalone/meon /tmp/landing all
  python3 -I scripts/landing/postprocess.py /tmp/landing/raw /tmp/landing/out
  ```

## 장면 (`Source/meon/ui/dev/Showcase.*`)

모든 인물은 가상 인물이다. 데이터는 `Source/meon/core/api/fake/fixtures/showcase.json` 에 있다.

| 장면 | 내용 |
|---|---|
| `jam` | 공개 방(승인 입장) '금요일 밤 잼' 7/7.<br>승찬(나, 건반, 방장) · 민준 · 서연 · 도윤 · 하은 · 지호 · 유나.<br>사람마다 볼륨·패닝이 다르고 셀프 모니터는 켜져 있다. |
| `jam` + `--as-plugin` | 같은 상태의 플러그인 창 (900×600). |
| `rooms` | 홈 화면. 공개 방 10개(가득 찬 방 2개)와 '록 클래식 셋리스트'에 보낸 입장 요청. |
| `friends` | `rooms` 와 같은 화면에 민준의 초대 토스트('시티팝 잼'). |

## 렌더링

- 창을 화면에서 뗀 뒤 지정한 논리 크기(1512×982, 플러그인 900×600)로 화면 전체를 2배로 직접 그린다. 늘리지 않는다.
- 첫 화면 영상: 12초, 30fps, 360프레임.
  - 레벨·핑은 `ShowcaseEngine` 의 시계를 1/30초씩 넘기며 그린다.
  - 레벨은 120 BPM 6마디, 핑은 1 ms 변화가 몇 번 있다. 둘 다 12초마다 똑같이 반복한다.
  - 미터는 시작 전에 한 주기를 미리 돌려 첫 프레임과 마지막 다음 프레임을 맞춘다.
- 채팅: 4초에 상대 메시지, 8초에 내 메시지가 올라온다. 마지막 0.5초(15프레임)는 채팅 칸만 첫 장면으로 페이드한다.
- 채팅 시각은 저녁 9시대로 보이게 실행 시간대(TZ)를 고른다. 앱 코드는 시스템 시각을 그대로 쓴다.

## 파일

- 이미지:
  - 원본 `.png` (2배, 무손실)
  - `@2x.webp` (원본 크기, 품질 90)
  - `@1x.webp` (절반, 품질 90)
- 크롭: 같은 2배 렌더링에서 해당 영역 + 사방 24(논리) 여백.
- 영상:
  - `hero@2x.mp4` (3024×1964 원본)
  - `hero.mp4` (1512×982, H.264)
  - `hero.webm` (1512×982, VP9)
  - `hero-poster.png` (첫 프레임)
  - 모두 소리 트랙 없음.
