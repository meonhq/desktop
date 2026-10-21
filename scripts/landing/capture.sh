#!/bin/bash
# 소개 페이지 이미지·영상의 원본 렌더링 (빌드 0 앱의 --showcase, Source/meon/ui/dev/Showcase.*).
# 사용: scripts/landing/capture.sh <앱 실행 파일> <출력 폴더> [still|video|all]
#   출력 폴더/raw/  jam.png · rooms.png · friends.png · jam-plugin.png · *.layout.json · frames/0000.png…
# macOS 는 그대로 실행하고, Linux 는 가상 화면(xvfb-run)에서 실행한다. 창은 렌더링 직전에 화면에서 떼므로 화면 크기와 상관없다.
set -u
APP=$1
OUT=$2
WHAT=${3:-all}
RAW="$OUT/raw"
WORK="$OUT/work"
mkdir -p "$RAW" "$WORK"

# 채팅 시각이 저녁 9시대로 보이게 시간대를 고른다 ('금요일 밤 잼'). 앱 코드는 시스템 시각을 그대로 쓴다
H=$(date -u +%H)
OFF=$(( (21 - 10#$H + 36) % 24 - 12 ))
export TZ="MEON$(( -OFF ))"
echo "TZ=$TZ (지금 $(date +%H:%M))"

run() {   # $1 = 이름, $2 = 제한 시간(초), 나머지 = 앱 옵션
  local name=$1 limit=$2; shift 2
  local home="$WORK/home-$name"
  rm -rf "$home"; mkdir -p "$home"
  local cmd=("$APP" --allow-multi --no-input --capture-out="$RAW" "$@")
  [ "$(uname)" = Linux ] && cmd=(xvfb-run -a -s "-screen 0 1920x1200x24" "${cmd[@]}")
  echo "== $name: ${cmd[*]}"
  HOME="$home" "${cmd[@]}" > "$WORK/$name.log" 2>&1 &
  local pid=$! code=0 t=0
  while kill -0 $pid 2>/dev/null && [ $t -lt $limit ]; do sleep 1; t=$((t + 1)); done
  if kill -0 $pid 2>/dev/null; then
    echo "::error::$name 이 ${limit}초 안에 끝나지 않음"
    pkill -9 -P $pid 2>/dev/null; kill -9 $pid 2>/dev/null; wait $pid 2>/dev/null
    code=124
  else
    wait $pid; code=$?
  fi
  grep "^SHOWCASE" "$WORK/$name.log" | grep -v "SHOWCASE frame" | tail -40 || true
  if [ $code -ne 0 ]; then
    echo "::error::$name 실패 (종료 코드 $code)"; tail -40 "$WORK/$name.log"
    find "$home" -name app.log -exec tail -40 {} \; 2>/dev/null   # 앱 진단 로그 (macOS: ~/Library/Logs/MEON)
    return 1
  fi
}

status=0
if [ "$WHAT" = still ] || [ "$WHAT" = all ]; then
  run jam     240 --showcase=jam     --window=0,0,1512,982 --capture-size=1512x982 || status=1
  run rooms   240 --showcase=rooms   --window=0,0,1512,982 --capture-size=1512x982 || status=1
  run friends 240 --showcase=friends --window=0,0,1512,982 --capture-size=1512x982 || status=1
  run daw     240 --showcase=jam --as-plugin --window=0,0,900,600 --capture-size=900x600 || status=1
fi
if [ "$WHAT" = video ] || [ "$WHAT" = all ]; then
  run video   2400 --showcase=jam --capture=video --window=0,0,1512,982 --capture-size=1512x982 || status=1
fi
ls -la "$RAW"
exit $status
