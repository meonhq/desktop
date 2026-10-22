#!/bin/bash
# 채팅 말풍선 줄바꿈 캡처 (빌드 0 앱의 --showcase=chat, Source/meon/ui/dev/Showcase.cpp buildChat).
# 사용: scripts/captures/chat-wrap.sh <앱 실행 파일> <출력 폴더> [이름 앞붙이]
#   경우마다 상대 한 줄 + 내 말풍선 한 줄: ko(한글 문장) · en(영어 문장) · link(긴 링크) · nospace(띄어쓰기 없는 긴 문장)
#   출력: <출력 폴더>/<앞붙이><경우>.png (채팅 카드만, 2배 렌더링)
# macOS·Windows 는 그대로, Linux 는 가상 화면(xvfb-run)에서 실행한다. Python 은 PYTHON 으로 고를 수 있다 (기본 python3).
set -u
APP=$1; OUT=$2; PREFIX=${3:-}
DIR=$(cd "$(dirname "$0")" && pwd)
RAW="$OUT/raw-$PREFIX"; mkdir -p "$RAW" "$OUT"
status=0
for c in ko en link nospace; do
  home=$(mktemp -d)
  cmd=("$APP" --allow-multi --no-input --showcase=chat --chat-case=$c --capture-out="$RAW" --capture-name=$c
       --window=0,0,1512,982 --capture-size=1512x982)
  [ "$(uname)" = Linux ] && cmd=(xvfb-run -a -s "-screen 0 1920x1200x24" "${cmd[@]}")
  HOME="$home" "${cmd[@]}" > "$RAW/$c.log" 2>&1 &
  pid=$! t=0
  while kill -0 $pid 2>/dev/null && [ $t -lt 180 ]; do sleep 1; t=$((t + 1)); done
  if kill -0 $pid 2>/dev/null; then kill -9 $pid; echo "::error::$c: 180초 안에 끝나지 않음"; status=1; continue; fi
  wait $pid || { echo "::error::$c 실패"; tail -20 "$RAW/$c.log"; status=1; continue; }
  "${PYTHON:-python3}" -I "$DIR/crop_card.py" "$RAW/$c.png" "$RAW/$c.layout.json" "$OUT/$PREFIX$c.png" || status=1
  rm -rf "$home"
done
ls -la "$OUT"
exit $status
