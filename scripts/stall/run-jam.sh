#!/bin/bash
# 2인 합주 멈춤·끊김 측정 (Linux, JACK dummy). 앱 코드는 바꾸지 않고, 개발 빌드(빌드 0)의 --dev-* 옵션만 쓴다.
#
# 사용: scripts/stall/run-jam.sh <이름> <초> <one|sep>
#   one = 한 JACK 서버에 두 앱 (DAW 안의 플러그인처럼, 한쪽이 멈추면 그래프 전체가 멈춤)
#   sep = 앱마다 JACK 서버 따로 (두 컴퓨터처럼, 상대가 듣는 끊김을 잴 수 있음)
# 환경 변수
#   APP            앱 실행 파일 (MEON_Standalone, 빌드 0)                    [필수]
#   CHURN=n        합주 중 들어오고 나가기를 반복하는 앱 수 (0~2, 기본 0). 따로 JACK 서버(srvC)에 붙는다
#   SELF_MONITOR=1 A 의 셀프 모니터를 켠다 (건너뛴 블록에도 내 소리가 이어지는지 볼 때)
#   NOPROBE=1      stallprobe 를 붙이지 않는다 (ThreadSanitizer 빌드)
#   BS             JACK 블록 크기 (기본 128 = 2.67 ms)
#   USERS          계정 접두어 (기본 stall → stalla, stallb, stallc, stalld). 먼저 make_user 로 만든다
#   그 밖에 testenv.sh 의 변수 (서버는 이 스크립트가 켜고 끈다)
# 결과: $STALL_WORK/out-<이름>/  (summarize.py 로 표를 만든다)
set -u
STALL_DIR=$(cd "$(dirname "$0")" && pwd)
. "$STALL_DIR/testenv.sh"
NAME=$1; SECS=$2; LAYOUT=$3
BS=${BS:-128}; CHURN=${CHURN:-0}; U=${USERS:-stall}
: "${APP:?APP 이 필요합니다}"
OUT=$STALL_WORK/out-$NAME
mkdir -p "$STALL_WORK"; grep -h -o "방 입장: r_[0-9a-f-]*" "$OUT"/home-*/.config/meon/logs/app.log 2>/dev/null | awk '{print $3}' >> "$STALL_WORK/rooms.txt"
rm -rf "$OUT"; mkdir -p "$OUT"
BIN=$STALL_WORK/bin; mkdir -p "$BIN"
[ "$BIN/stallprobe.so" -nt "$STALL_DIR/stallprobe.c" ] || gcc -O2 -g -fPIC -shared -o "$BIN/stallprobe.so" "$STALL_DIR/stallprobe.c" -ldl -lpthread
[ "$BIN/jamprobe" -nt "$STALL_DIR/jamprobe.c" ] || gcc -O2 -g -o "$BIN/jamprobe" "$STALL_DIR/jamprobe.c" -ljack -lm
export DISPLAY=${DISPLAY_FOR_TEST:-:58}
pgrep -f "Xvfb $DISPLAY " >/dev/null || (Xvfb "$DISPLAY" -screen 0 1600x1000x24 >/dev/null 2>&1 &)

# JACK 서버
SERVERS="srvA"; [ "$LAYOUT" = sep ] && SERVERS="srvA srvB"; [ "$CHURN" -gt 0 ] && SERVERS="$SERVERS srvC"
for s in $SERVERS; do pkill -f "jackd -n $s " 2>/dev/null; done; sleep 0.5
for s in $SERVERS; do
  jackd -n $s -R -P 70 -d dummy -r 48000 -p "$BS" > "$OUT/jackd-$s.log" 2>&1 &
  echo $! > "$OUT/jackd-$s.pid"
done
for s in $SERVERS; do JACK_DEFAULT_SERVER=$s jack_wait -w -t 10 >/dev/null || { echo "jackd $s 실패"; exit 1; }; done
srv_of() { case $1 in a) echo srvA;; b) [ "$LAYOUT" = sep ] && echo srvB || echo srvA;; *) echo srvC;; esac; }

env_start || exit 1
leave_rooms ${U}a ${U}b ${U}c ${U}d   # 지난 시험의 방에서 나간다

# 앱마다 HOME (오디오 장치 = JACK, 셀프 모니터)
for u in a b c d; do
  H=$OUT/home-$u; mkdir -p "$H/.config/meon"
  cat > "$H/.config/meon/Meon.settings" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<PROPERTIES>
  <VALUE name="audioSetup">
    <DEVICESETUP deviceType="JACK" audioOutputDeviceName="system" audioInputDeviceName="system"
                 audioDeviceRate="48000" audioDeviceBufferSize="$BS"/>
  </VALUE>
  <VALUE name="shouldOverrideSampleRate" val="1"/>
</PROPERTIES>
EOF
  SM=0; [ "$u" = a ] && [ -n "${SELF_MONITOR:-}" ] && SM=1
  cat > "$H/.config/meon/MEONUser.settings" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<PROPERTIES>
  <VALUE name="selfMonitorApp" val="$SM"/>
  <VALUE name="selfMonitorResetQ30" val="1"/>
  <VALUE name="onboardingDoneApp" val="1"/>
</PROPERTIES>
EOF
done

start_app() {   # $1 = a|b|c|d, $2 = 로그 이름, 나머지 = 앱 옵션. 백그라운드 pid 를 출력
  local u=$1 log=$2; shift 2
  local pre=""; [ -z "${NOPROBE:-}" ] && pre=$BIN/stallprobe.so
  # exec: $! 가 앱 자신의 pid 가 되게 (강제 종료가 앱에 바로 닿게)
  ( export HOME=$OUT/home-$u JACK_DEFAULT_SERVER=$(srv_of $u) STALLPROBE_LOG=$OUT/probe-$u.log STALLPROBE_MS=${STALL_MS:-10} \
           LD_PRELOAD=$pre TSAN_OPTIONS="${TSAN_OPTIONS:-} log_path=$OUT/tsan-$u"
    exec "$APP" --allow-multi --server=$API_URL --dev-signup=$U$u "$@" ) >> "$OUT/$log.log" 2>&1 &
  echo $!
}
jack_clients() { JACK_DEFAULT_SERVER=$1 jack_lsp 2>/dev/null | grep -v -E '^(system|probe)' | sed 's/:.*//' | sort -u; }

PA=$(start_app a app-a --dev-create)
CODE=""
for i in $(seq 1 240); do CODE=$(grep -o 'room code [A-Za-z0-9-]*' "$OUT/app-a.log" | awk '{print $3}' | head -1); [ -n "$CODE" ] && break; sleep 0.5; done
[ -n "$CODE" ] || { echo "방 코드 없음"; tail -5 "$OUT/app-a.log"; env_stop; exit 1; }
echo "room code $CODE" | tee "$OUT/room.txt"
for i in $(seq 1 240); do [ -n "$(jack_clients srvA)" ] && break; sleep 0.5; done
CA=$(jack_clients srvA | head -1)
PB=$(start_app b app-b --dev-join=$CODE)
SB=$(srv_of b)
for i in $(seq 1 240); do CB=$(jack_clients $SB | grep -v -x "$CA" | head -1); [ -n "$CB" ] && break; sleep 0.5; done
echo "JACK: A=$CA@srvA B=$CB@$SB" | tee "$OUT/clients.txt"
sleep 2

port_of() { JACK_DEFAULT_SERVER=$1 jack_lsp 2>/dev/null | grep "^$2:" | grep -i "$3" | head -1; }
unplug_inputs() { for p in $(JACK_DEFAULT_SERVER=$1 jack_lsp 2>/dev/null | grep "^$2:" | grep -i 'in'); do
  JACK_DEFAULT_SERVER=$1 jack_disconnect system:capture_1 "$p" 2>/dev/null; JACK_DEFAULT_SERVER=$1 jack_disconnect system:capture_2 "$p" 2>/dev/null; done; }
start_probe() {   # 서버 클라이언트이름 표시이름 Hz 로그 → pid
  local srv=$1 cn=$2 label=$3 hz=$4 log=$5
  for k in 1 2 3 4 5 6; do
    "$BIN/jamprobe" "$srv" "$SECS" "$label" "$hz" "$cn" > "$log" 2>&1 &
    local pid=$!; sleep 1
    if kill -0 $pid 2>/dev/null; then echo $pid; return; fi
  done
}
unplug_inputs srvA "$CA"; unplug_inputs "$SB" "$CB"
if [ "$LAYOUT" = sep ]; then
  # 서버마다: 사인파 → 그 앱 입력, 그 앱 출력 → 측정 (그 앱이 듣는 상대 소리)
  P1=$(start_probe srvA probe "A 가 듣는 B" 440 "$OUT/hear-a.log")
  P2=$(start_probe srvB probe "B 가 듣는 A" 660 "$OUT/hear-b.log")
  JACK_DEFAULT_SERVER=srvA jack_connect probe:tone "$(port_of srvA "$CA" in)"; JACK_DEFAULT_SERVER=srvA jack_connect "$(port_of srvA "$CA" out)" probe:recv
  JACK_DEFAULT_SERVER=srvB jack_connect probe:tone "$(port_of srvB "$CB" in)"; JACK_DEFAULT_SERVER=srvB jack_connect "$(port_of srvB "$CB" out)" probe:recv
else
  # 한 서버: probeA 사인파 → A 입력, B 출력 → probeA (B 가 듣는 A). probeB 는 반대
  P1=$(start_probe srvA probeA "B 가 듣는 A" 440 "$OUT/hear-b.log")
  P2=$(start_probe srvA probeB "A 가 듣는 B" 660 "$OUT/hear-a.log")
  JACK_DEFAULT_SERVER=srvA jack_connect probeA:tone "$(port_of srvA "$CA" in)"; JACK_DEFAULT_SERVER=srvA jack_connect "$(port_of srvA "$CB" out)" probeA:recv
  JACK_DEFAULT_SERVER=srvA jack_connect probeB:tone "$(port_of srvA "$CB" in)"; JACK_DEFAULT_SERVER=srvA jack_connect "$(port_of srvA "$CA" out)" probeB:recv
fi
for s in $SERVERS; do JACK_DEFAULT_SERVER=$s jack_lsp -c > "$OUT/ports-$s.txt" 2>/dev/null; done

# 들어오고 나가기 반복: 붙어서 2~6초 있다가 나간다 (번갈아 정상 종료·강제 종료), 1~2초 쉬고 다시
churn() {   # $1 = c|d
  local u=$1 n=0 end=$(( $(date +%s) + SECS - 5 ))
  while [ "$(date +%s)" -lt "$end" ]; do
    n=$((n + 1))
    local pid; pid=$(start_app $u app-$u --dev-join=$CODE)
    for i in $(seq 1 80); do jack_clients srvC | grep -q . && break; sleep 0.25; done
    sleep $(( 2 + RANDOM % 5 ))
    if [ $((n % 2)) -eq 0 ]; then kill -KILL $pid 2>/dev/null; how=KILL; else kill -TERM $pid 2>/dev/null; how=TERM; fi
    for i in $(seq 1 40); do kill -0 $pid 2>/dev/null || break; sleep 0.25; done
    kill -KILL $pid 2>/dev/null
    echo "$(date +%s.%N) churn $u $n $how" >> "$OUT/churn.log"
    sleep $(( 1 + RANDOM % 2 ))
  done
}
CP=""
[ "$CHURN" -ge 1 ] && { churn c & CP="$CP $!"; }
[ "$CHURN" -ge 2 ] && { sleep 3; churn d & CP="$CP $!"; }

( sleep $((SECS / 2)); top -b -n 1 -H -w 200 | head -45 > "$OUT/top.txt" ) &
while kill -0 $P1 2>/dev/null || kill -0 $P2 2>/dev/null; do sleep 1; done
for p in $CP; do wait $p 2>/dev/null; done

# 끝: 앱이 살아 있었는지 (크래시 확인) 기록하고 정상 종료
for u in a b; do eval pid=\$P${u^^}; if kill -0 $pid 2>/dev/null; then echo "$u alive" >> "$OUT/alive.txt"; else echo "$u DEAD" >> "$OUT/alive.txt"; fi; done
kill -TERM $PA $PB 2>/dev/null
for i in $(seq 1 40); do kill -0 $PA 2>/dev/null || kill -0 $PB 2>/dev/null || break; sleep 0.25; done
kill -KILL $PA $PB 2>/dev/null
pkill -KILL -f -- "--dev-signup=$U[a-d]" 2>/dev/null   # 남은 앱 (들어오고 나가기 중이던 것)
env_stop
for s in $SERVERS; do kill "$(cat "$OUT/jackd-$s.pid")" 2>/dev/null; done
echo "끝: $OUT"
