#!/usr/bin/env bash
# 윈도우 창 상태별 화면 자가 테스트 (--uitest=winstates, 빌드 0 전용): 배율 100% → 125% → 150% 로 켜서 같은 시험.
# 배율마다 모달 구분선 두께 (--uitest=modallines) 도 잰다.
# 상태마다 화면에 보이는 창을 앱이 그린 그림과 비교하고(가장자리 줄·창 버튼 자리), 캡처를 wincaps-<이름표>/ 에 PNG 로 남긴다.
# 사용: scripts/win_uitest_states.sh <Meon.exe> <이름표>. 실패가 있으면 1 로 끝난다 (워크플로는 빌드를 막지 않는다)
set -u
EXE="$1"
TAG="${2:-win}"
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$PWD/wincaps-$TAG"
mkdir -p "$OUT"
fail=0
for steps in 0 1 2; do
  powershell -NoProfile -ExecutionPolicy Bypass -File "$HERE/win_set_scale.ps1" -Steps "$steps" || true
  log="uitest-winstates-$TAG-steps$steps.log"
  MEON_WINCAPS="$OUT/steps$steps" timeout 900 "$EXE" --allow-multi --no-input --fake-script=0 --uitest=winstates --window=60,40,1024,680 > "$log" 2>&1 || true
  echo "── winstates $TAG scale steps $steps"
  grep -E "^UITEST (INFO|FAIL|DONE)" "$log" || { echo "결과 없음"; tail -30 "$log"; }
  grep -q "^UITEST DONE .* 0 failed" "$log" || fail=1
  cp "$log" "$OUT/" 2>/dev/null || true
  # 모달·확인 창 구분선 두께 (--uitest=modallines): 이 배율에서 헤더·본문·푸터 선이 물리 1px 한 줄씩인지
  mlog="uitest-modallines-$TAG-steps$steps.log"
  timeout 300 "$EXE" --allow-multi --no-input --fake-script=0 --uitest=modallines --window=40,30,1100,700 > "$mlog" 2>&1 || true
  echo "── modallines $TAG scale steps $steps"
  grep -E "^UITEST (INFO modallines|FAIL|DONE)" "$mlog" || { echo "결과 없음"; tail -30 "$mlog"; }
  grep -q "^UITEST DONE .* 0 failed" "$mlog" || fail=1
  cp "$mlog" "$OUT/" 2>/dev/null || true
done
powershell -NoProfile -ExecutionPolicy Bypass -File "$HERE/win_set_scale.ps1" -Steps 0 || true
exit $fail
