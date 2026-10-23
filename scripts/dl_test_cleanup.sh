#!/usr/bin/env bash
# dl 시험 파일 지우기 (dl-test.yml 을 cleanup 으로 실행할 때). 시험 경로 meon/v1/test/ 와 meon/test/ 만 지운다.
# 진짜 채널 매니페스트(meon/v1/stable·beta)와 빌드 폴더(meon/mac·windows)는 건드리지 않는다:
#   지울 경로는 아래 두 개로 고정이고, 받은 목록의 키가 그 경로로 시작하지 않으면 아무것도 지우지 않고 멈춘다.
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

SUMMARY=${GITHUB_STEP_SUMMARY:-/dev/null}
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
fail() { local m=${1//$R2_ACCOUNT_ID/***}; echo "::error::$m"; echo "**실패**: $m" >> "$SUMMARY"; exit 1; }

# keys <prefix> : 그 경로 아래 키 목록 (한 줄에 하나)
keys() {
    aws s3api list-objects-v2 --bucket "$BUCKET" --prefix "$1" --output json > "$W/list.json" 2> "$W/list.err" \
        || fail "목록을 받지 못함 ($1): $(tr '\n' ' ' < "$W/list.err" | tail -c 300)"
    python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); [print(o["Key"]) for o in d.get("Contents") or []]' "$W/list.json"
}

printf '## dl 시험 파일 지우기\n\n| 경로 | 지운 것 | 남은 것 |\n|---|---|---|\n' >> "$SUMMARY"
TOTAL=0
for P in meon/v1/test/ meon/test/; do
    keys "$P" > "$W/keys.txt"
    # 목록의 모든 키가 이 경로 아래인지 먼저 본다 (하나라도 아니면 아무것도 지우지 않는다)
    while IFS= read -r k; do
        case "$k" in "$P"?*) ;; *) fail "목록에 시험 경로 밖의 키가 있음: $k";; esac
    done < "$W/keys.txt"
    n=0
    while IFS= read -r k; do
        [ -n "$k" ] || continue
        aws s3api delete-object --bucket "$BUCKET" --key "$k" > /dev/null 2> "$W/del.err" \
            || fail "지우지 못함: $k ($(tr '\n' ' ' < "$W/del.err" | tail -c 300))"
        echo "지움: $k"
        n=$((n + 1))
    done < "$W/keys.txt"
    left=$(keys "$P" | wc -l)
    echo "| $P | $n | $left |" >> "$SUMMARY"
    [ "$left" = 0 ] || fail "$P 아래에 $left 개가 남음"
    TOTAL=$((TOTAL + n))
done

# CDN 으로도 없는지 (캐시를 피해 쿼리를 붙인다)
for u in meon/v1/test/mac.json meon/v1/test/windows.json; do
    code=$(curl -sS -o /dev/null -w '%{http_code}' "$DL/$u?check=$RANDOM" 2> /dev/null || echo 000)
    echo "| $DL/$u | CDN | HTTP $code |" >> "$SUMMARY"
    [ "$code" = 404 ] || fail "$u 가 CDN 에서 아직 HTTP $code"
done
echo "완료: $TOTAL 개 지움"
printf '\n%s 개 지움. 진짜 채널·빌드 폴더는 건드리지 않음.\n' "$TOTAL" >> "$SUMMARY"
