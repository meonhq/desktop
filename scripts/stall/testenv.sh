# 멈춤 측정용 로컬 시험 환경 (Linux). source 해서 쓴다: . scripts/stall/testenv.sh
#   meon 서버(meonhq/server 의 cmd/api) + AOO 서버(aooserver) + 인증 메일 받기(smtp_capture.py)
#   DB 는 빈 Postgres 데이터베이스 하나 (서버가 켜질 때 마이그레이션한다). 운영 DB 를 쓰지 않는다.
#
# 필요한 환경 변수
#   MEON_API_BIN    meon 서버 실행 파일 (cd server && go build -o meon-api ./cmd/api)
#   AOO_SERVER_BIN  aooserver 실행 파일 (https://github.com/essej/aooserver, Builds/LinuxMakefile)
#   DATABASE_URL    빈 시험용 DB (예: postgres://meon@127.0.0.1:55433/meon_e10?sslmode=disable)
# 고를 수 있는 것
#   STALL_WORK (기본 /tmp/meon-stall)  API_PORT (18082)  AOO_PORT (11999)  SMTP_PORT (11025)
# 비밀 값(JWT 등)은 켤 때마다 새로 만든다. 레포·로그에 남기지 않는다.

STALL_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
export STALL_WORK=${STALL_WORK:-/tmp/meon-stall}
export API_PORT=${API_PORT:-18082} AOO_PORT=${AOO_PORT:-11999} SMTP_PORT=${SMTP_PORT:-11025}
export API_URL=http://127.0.0.1:$API_PORT
export API=$API_URL/v1
TEST_PASSWORD='abcd123!x'   # 앱의 --dev-signup 과 같은 시험용 비밀번호 (개발 빌드 전용)

env_start() {
  mkdir -p "$STALL_WORK/env"
  local E=$STALL_WORK/env
  : "${MEON_API_BIN:?MEON_API_BIN 이 필요합니다}" "${AOO_SERVER_BIN:?AOO_SERVER_BIN 이 필요합니다}" "${DATABASE_URL:?DATABASE_URL 이 필요합니다}"
  "$AOO_SERVER_BIN" -p "$AOO_PORT" > "$E/aoo.log" 2>&1 &
  echo $! > "$E/aoo.pid"
  rm -rf "$E/mail"; python3 -I "$STALL_DIR/smtp_capture.py" "$SMTP_PORT" "$E/mail" > "$E/smtp.log" 2>&1 &
  echo $! > "$E/smtp.pid"
  ( export APP_ENV=dev HTTP_ADDR=127.0.0.1:$API_PORT DISCORD_URL=https://discord.gg/test ALLOW_DEV_BUILD=true \
           JWT_SECRET=$(openssl rand -hex 32) CODE_HMAC_SECRET=$(openssl rand -hex 32) AOO_USER_SECRET=$(openssl rand -hex 32) \
           AOO_HOST=127.0.0.1 AOO_PORT=$AOO_PORT SMTP_HOST=127.0.0.1 SMTP_PORT=$SMTP_PORT SMTP_TLS=none LOG_LEVEL=warn
    exec "$MEON_API_BIN" ) > "$E/api.log" 2>&1 &
  echo $! > "$E/api.pid"
  for i in $(seq 1 100); do curl -s -o /dev/null "$API_URL/healthz" && return 0; sleep 0.2; done
  echo "meon 서버가 켜지지 않음: $(tail -3 "$E/api.log")" >&2; return 1
}

env_stop() { for p in api smtp aoo; do [ -f "$STALL_WORK/env/$p.pid" ] && kill "$(cat "$STALL_WORK/env/$p.pid")" 2>/dev/null; done; return 0; }

# 시험 계정 만들기: 가입 → 메일 코드 → 인증 → 닉네임·파트. 이미 있으면 아무것도 하지 않는다
make_user() {
  local u=$1 E=$STALL_WORK/env
  local resp tok code
  resp=$(curl -s "$API/auth/signup" -H 'Content-Type: application/json' -H 'X-Meon-Build: 1' \
    -d "{\"email\":\"$u@example.com\",\"username\":\"$u\",\"password\":\"$TEST_PASSWORD\",\"agreed_terms\":true}")
  tok=$(printf '%s' "$resp" | python3 -c 'import json,sys
try: print(json.load(sys.stdin).get("access_token",""))
except Exception: print("")')
  [ -n "$tok" ] || { echo "$u: 이미 있음"; return 0; }
  for i in $(seq 1 50); do code=$(grep -h -o -E '\b[0-9]{6}\b' $(ls -t "$E"/mail/*.eml 2>/dev/null | head -1) 2>/dev/null | head -1); [ -n "$code" ] && break; sleep 0.2; done
  curl -s -o /dev/null "$API/auth/verify-email" -H "Authorization: Bearer $tok" -H 'Content-Type: application/json' -H 'X-Meon-Build: 1' -d "{\"code\":\"$code\"}"
  curl -s -o /dev/null -X PATCH "$API/me" -H "Authorization: Bearer $tok" -H 'Content-Type: application/json' -H 'X-Meon-Build: 1' -d "{\"nickname\":\"$u\",\"part\":\"keys\"}"
  rm -f "$E"/mail/*.eml
  echo "$u: 만듦"
}
