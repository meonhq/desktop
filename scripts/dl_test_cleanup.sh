#!/usr/bin/env bash
# dl 시험 파일 지우기 (dl-test.yml 을 cleanup 으로 실행할 때). 시험 경로 meon/v1/test/ 와 meon/test/ 만 지운다.
# 진짜 채널 매니페스트(meon/v1/stable·beta)와 빌드 폴더(meon/mac·windows)는 건드리지 않는다:
#   지울 경로는 아래 두 개로 고정이고, 받은 목록에 그 경로 밖의 키가 하나라도 있으면 아무것도 지우지 않고 멈춘다.
# 비밀값은 출력하지 않는다 (set -x 금지). 결과는 실행 요약(GITHUB_STEP_SUMMARY)과 로그에.
# 필요한 환경 변수: BUCKET DL R2_ACCOUNT_ID AWS_ACCESS_KEY_ID AWS_SECRET_ACCESS_KEY
set -uo pipefail

for v in BUCKET DL R2_ACCOUNT_ID AWS_ACCESS_KEY_ID AWS_SECRET_ACCESS_KEY; do
    if [ -z "${!v:-}" ]; then echo "::error::$v 가 비어 있음"; exit 1; fi
done
[ "$BUCKET" = meon-dl ] || { echo "::error::버킷이 meon-dl 이 아님"; exit 1; }

export AWS_DEFAULT_REGION=auto
export AWS_ENDPOINT_URL="https://${R2_ACCOUNT_ID}.r2.cloudflarestorage.com"
export AWS_PAGER=""
export AWS_REQUEST_CHECKSUM_CALCULATION=when_required AWS_RESPONSE_CHECKSUM_VALIDATION=when_required

PREFIXES="meon/v1/test/ meon/test/"
SUMMARY=${GITHUB_STEP_SUMMARY:-/dev/null}
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
# 오류는 표준 오류로 낸다 (keys 의 출력을 파일로 받는 중에도 로그에 남게)
fail() { local m=${1//$R2_ACCOUNT_ID/***}; echo "::error::$m" >&2; echo "**실패**: $m" >> "$SUMMARY"; exit 1; }

# keys <경로> : 그 경로 아래 키 목록 (한 줄에 하나). 목록을 받거나 읽지 못하면 스크립트를 끝낸다.
#   "keys P > 파일" 로만 부른다. 파이프나 $( ) 안에서 부르면 fail 이 스크립트를 끝내지 못한다.
#   빈 목록은 CLI 버전에 따라 출력이 없거나 Contents 없는 JSON 이다. 둘 다 0개로 본다.
keys() {
    aws s3api list-objects-v2 --bucket "$BUCKET" --prefix "$1" --output json > "$W/list.json" 2> "$W/list.err" \
        || fail "목록을 받지 못함 ($1): $(tr '\n' ' ' < "$W/list.err" | tail -c 300)"
    python3 -c '
import json, sys
t = open(sys.argv[1], encoding="utf-8").read().strip()
d = json.loads(t) if t else {}
for o in d.get("Contents") or []:
    print(o["Key"])
' "$W/list.json" || fail "목록을 읽지 못함 ($1)"
}

printf '## dl 시험 파일 지우기\n\n| 경로 | 지운 것 | 남은 것 |\n|---|---|---|\n' >> "$SUMMARY"

# 1) 두 경로의 목록을 받고, 모든 키가 그 경로 아래인지 먼저 본다 (하나라도 아니면 아무것도 지우지 않는다)
i=0
for P in $PREFIXES; do
    i=$((i + 1))
    keys "$P" > "$W/keys-$i.txt"
    while IFS= read -r k; do
        case "$k" in "$P"?*) ;; *) fail "목록에 시험 경로 밖의 키가 있음: $k (아무것도 지우지 않음)";; esac
    done < "$W/keys-$i.txt"
done

# 2) 지우고, 다시 목록을 받아 0개인지 본다
: > "$W/deleted.txt"
i=0
for P in $PREFIXES; do
    i=$((i + 1))
    n=0
    while IFS= read -r k; do
        aws s3api delete-object --bucket "$BUCKET" --key "$k" > /dev/null 2> "$W/del.err" \
            || fail "지우지 못함: $k ($(tr '\n' ' ' < "$W/del.err" | tail -c 300))"
        echo "지움: $k"
        echo "$k" >> "$W/deleted.txt"
        n=$((n + 1))
    done < "$W/keys-$i.txt"
    keys "$P" > "$W/after.txt"
    left=$(wc -l < "$W/after.txt")
    echo "| $P | $n | $left |" >> "$SUMMARY"
    [ "$left" -eq 0 ] || fail "$P 아래에 $left 개가 남음: $(head -3 "$W/after.txt" | tr '\n' ' ')"
done
TOTAL=$(wc -l < "$W/deleted.txt")

# 3) CDN 으로 확인
#  - 쿼리를 붙이면 엣지 캐시를 거치지 않고 원본(R2)까지 간다 → 지운 키는 모두 404 여야 한다 (아니면 실패)
#  - 쿼리 없이 받으면 엣지에 남은 사본이 있을 때 200 이다. 정보로만 적는다:
#    Cache-Control 의 max-age 동안 남을 수 있고, 이 실행이 거친 Cloudflare 지점 한 곳만 본다. 지우려면 Cloudflare 에서 URL 로 purge.
origin_left=0
: > "$W/edge.txt"
while IFS= read -r k; do
    o=$(curl -sS -o /dev/null -w '%{http_code}' "$DL/$k?check=$RANDOM$RANDOM" 2> /dev/null) || true
    if [ "$o" != 404 ]; then
        origin_left=$((origin_left + 1))
        echo "::error::CDN(원본)에서 아직 HTTP ${o:-000}: $k" >&2
    fi
    e=$(curl -sS -I -o /dev/null -w '%{http_code}' "$DL/$k" 2> /dev/null) || true
    [ "$e" = 200 ] && echo "$DL/$k" >> "$W/edge.txt"
done < "$W/deleted.txt"
edge_left=$(wc -l < "$W/edge.txt")

{
    printf '\n| CDN 확인 | 결과 |\n|---|---|\n'
    echo "| 쿼리를 붙여 원본까지 (지운 $TOTAL 개) | 404 아님: $origin_left 개 |"
    echo "| 쿼리 없이 (엣지 사본, 이 실행의 Cloudflare 지점) | 아직 200: $edge_left 개 |"
    if [ "$edge_left" -gt 0 ]; then
        printf '\n엣지에 남은 사본 (Cloudflare › Caching › Purge by URL 로 지울 수 있음):\n\n'
        sed 's/^/- /' "$W/edge.txt"
    fi
} >> "$SUMMARY"
echo "엣지(이 실행의 Cloudflare 지점)에 아직 200: $edge_left 개"
[ "$edge_left" -eq 0 ] || sed 's/^/  /' "$W/edge.txt"
[ "$origin_left" -eq 0 ] || fail "CDN 으로 원본에 아직 있는 것이 $origin_left 개"

echo "완료: $TOTAL 개 지움"
printf '\n%s 개 지움. 진짜 채널·빌드 폴더는 건드리지 않음.\n' "$TOTAL" >> "$SUMMARY"
