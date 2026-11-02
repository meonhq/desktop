#!/usr/bin/env bash
# 방 화면에서 창을 최소 크기로 끌어 줄였을 때 접힌 사이드바 아이콘이 실제 화면에 보이는지 (--uitest=winroomsidebar, 빌드 0 전용).
# 배율 100% → 125% → 150%, 앱 창 틀(app) / 운영체제 창 틀(os: --no-winframe, meonhq/app 과 같은 모양)로 각각 돌린다.
# 사이드바 캡처(다시 그리기 전·뒤)는 wincaps-roomsidebar-<이름표>/ 에 PNG 로. 사용자 보고(2026-11-02)의 원인 찾기용이고 고치는 것은 app 에서 한다.
# 사용: scripts/win_roomsidebar.sh <Meon.exe> <이름표>. 실패가 있으면 1 로 끝난다 (워크플로는 빌드를 막지 않는다)
set -u
EXE="$1"
TAG="${2:-win}"
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$PWD/wincaps-roomsidebar-$TAG"
mkdir -p "$OUT"
fail=0
for steps in 0 1 2; do
  powershell -NoProfile -ExecutionPolicy Bypass -File "$HERE/win_set_scale.ps1" -Steps "$steps" || true
  for frame in app os; do
    extra=""
    [ "$frame" = os ] && extra="--no-winframe"
    log="uitest-roomsidebar-$TAG-steps$steps-$frame.log"
    MEON_WINCAPS="$OUT/steps$steps-$frame" timeout 300 "$EXE" --allow-multi --no-input --fake-script=0 --uitest=winroomsidebar --window=40,30,1024,680 $extra > "$log" 2>&1 || true
    echo "── winroomsidebar $TAG scale steps $steps, $frame frame"
    grep -E "^UITEST (INFO|FAIL|DONE)" "$log" || { echo "결과 없음"; tail -30 "$log"; }
    grep -q "^UITEST DONE .* 0 failed" "$log" || fail=1
    cp "$log" "$OUT/" 2>/dev/null || true
  done
done
powershell -NoProfile -ExecutionPolicy Bypass -File "$HERE/win_set_scale.ps1" -Steps 0 || true
exit $fail
