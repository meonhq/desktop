#!/usr/bin/env bash
# dl.meon.co.kr (Cloudflare R2 버킷 meon-dl) 시험. .github/workflows/dl-test.yml 이 부른다.
# 배포 설계 DISTRIBUTION_PLAN 8장 "확인 못 함" (AWS CLI 체크섬 설정, 조건부 PUT, 캐시 동작) 과
# CI 서명 키 ↔ 앱 키 헤더 일치 (앱과 같은 코드인 MEON_ManifestTool 로 검증) 를 본다.
#
# 쓰는 곳은 시험 경로뿐이다: 매니페스트 meon/v1/test/, 파일 meon/test/<실행 번호>/. 진짜 채널(stable·beta)·빌드 폴더는 건드리지 않는다.
# 비밀값은 출력하지 않는다 (set -x 금지). 결과는 실행 요약(GITHUB_STEP_SUMMARY)과 로그에 적는다.
# 꼭 맞아야 하는 항목(키 일치, 검증, CDN sha256)이 틀리면 1 로 끝난다. 나머지는 "확인" 기록이다.
#
# 필요한 환경 변수: BUCKET DL TESTDIR TOOL R2_ACCOUNT_ID AWS_ACCESS_KEY_ID AWS_SECRET_ACCESS_KEY SIGNING_KEY
set -uo pipefail

for v in BUCKET DL TESTDIR TOOL R2_ACCOUNT_ID AWS_ACCESS_KEY_ID AWS_SECRET_ACCESS_KEY SIGNING_KEY; do
    if [ -z "${!v:-}" ]; then echo "::error::$v 가 비어 있음"; exit 1; fi
done

REPO=$(cd "$(dirname "$0")/.." && pwd)
PUBLIC=https://dl.meon.co.kr   # 매니페스트에 적는 주소 (앱이 믿는 머리). 받아 보는 주소는 $DL
export AWS_DEFAULT_REGION=auto
export AWS_ENDPOINT_URL="https://${R2_ACCOUNT_ID}.r2.cloudflarestorage.com"
export AWS_PAGER=""
unset AWS_REQUEST_CHECKSUM_CALCULATION AWS_RESPONSE_CHECKSUM_VALIDATION

W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
cd "$W"
umask 077

RUN=${GITHUB_RUN_NUMBER:-0}
SUMMARY=${GITHUB_STEP_SUMMARY:-/dev/null}
FAILED=0

# 요약 표 한 줄 (| 는 / 로 바꾼다)
row() {
    local a=${1//|//} b=${2//|//} c=${3//|//}
    b=${b//$'\n'/ }
    c=${c//$'\n'/ }
    b=${b//$R2_ACCOUNT_ID/***}
    c=${c//$R2_ACCOUNT_ID/***}   # 오류 문구의 엔드포인트 주소에 계정 ID 가 들어갈 수 있다 (요약에는 남기지 않는다)
    echo "| $a | $b | $c |" >> "$SUMMARY"
    echo "== $a: $b — $c"
}
critical() { FAILED=1; row "$1" "**실패**" "$2"; echo "::error::$1: $2"; }
section() { printf '\n### %s\n\n| 항목 | 결과 | 내용 |\n|---|---|---|\n' "$1" >> "$SUMMARY"; echo; echo "##### $1"; }

# put <key> <file> [추가 인자…] → 0/1, 오류는 put.err
put() { aws s3api put-object --bucket "$BUCKET" --key "$1" --body "$2" "${@:3}" > put.out 2> put.err; }
err() { tr '\n' ' ' < "$1" | tail -c 400; }

# get <url> : 본문 body.out, 머리 h.out, 상태 CODE, 캐시 상태 CS, Cache-Control CC, Age AGE
get() {
    CODE=$(curl -sS -o body.out -D h.out -w '%{http_code}' -A "meon-dl-test/$RUN" "$@" 2> curl.err) || CODE="curl 실패: $(err curl.err)"
    CS=$(grep -i '^cf-cache-status:' h.out 2>/dev/null | tail -1 | cut -d' ' -f2- | tr -d '\r')
    CC=$(grep -i '^cache-control:' h.out 2>/dev/null | tail -1 | cut -d' ' -f2- | tr -d '\r')
    AGE=$(grep -i '^age:' h.out 2>/dev/null | tail -1 | cut -d' ' -f2- | tr -d '\r')
    CT=$(grep -i '^content-type:' h.out 2>/dev/null | tail -1 | cut -d' ' -f2- | tr -d '\r')
}
desc() { echo "HTTP $CODE, cf-cache-status ${CS:-없음}, cache-control '${CC:-없음}', age ${AGE:-없음}, content-type ${CT:-없음}"; }

{
    echo "## dl.meon.co.kr 시험 (실행 $RUN, 커밋 ${GITHUB_SHA:-?})"
    echo
    echo "버킷 $BUCKET, 시험 파일 $TESTDIR/, 시험 매니페스트 meon/v1/test/. $(aws --version 2>&1)"
} >> "$SUMMARY"

printf 'meon dl test %s run %s\n' "$(date -u +%FT%TZ)" "$RUN" > small.txt
head -c 2200000 /dev/urandom > pkg.bin
PKG_SHA=$(sha256sum pkg.bin | cut -d' ' -f1)

#==============================================================================
section "1. AWS CLI 체크섬 설정"
# AWS CLI 2.23 부터 기본으로 요청마다 CRC 체크섬을 붙인다. R2 가 받는지 본다 (DISTRIBUTION_PLAN 3.9)
if put "$TESTDIR/checksum-default.txt" small.txt; then D1=통과; else D1="거절: $(err put.err)"; fi
if put "$TESTDIR/checksum-default.bin" pkg.bin --content-type application/octet-stream; then D2=통과; else D2="거절: $(err put.err)"; fi
row "기본값 put-object (작은 파일)" "$D1" "AWS_REQUEST_CHECKSUM_CALCULATION 없음"
row "기본값 put-object (2.2 MB)" "$D2" ""
if aws s3api get-object --bucket "$BUCKET" --key "$TESTDIR/checksum-default.bin" back.bin > get.out 2> get.err \
   && [ "$(sha256sum back.bin | cut -d' ' -f1)" = "$PKG_SHA" ]; then D3=통과; else D3="실패: $(err get.err)"; fi
row "기본값 get-object (응답 체크섬 검사)" "$D3" "AWS_RESPONSE_CHECKSUM_VALIDATION 없음"

export AWS_REQUEST_CHECKSUM_CALCULATION=when_required AWS_RESPONSE_CHECKSUM_VALIDATION=when_required
if put "$TESTDIR/checksum-when-required.bin" pkg.bin --content-type application/octet-stream; then R1=통과; else R1="거절: $(err put.err)"; fi
if aws s3api get-object --bucket "$BUCKET" --key "$TESTDIR/checksum-when-required.bin" back2.bin > get.out 2> get.err \
   && [ "$(sha256sum back2.bin | cut -d' ' -f1)" = "$PKG_SHA" ]; then R2=통과; else R2="실패: $(err get.err)"; fi
row "when_required put-object (2.2 MB)" "$R1" ""
row "when_required get-object" "$R2" ""
if [ "$R1" != 통과 ] && [ "$D2" = 통과 ]; then
    unset AWS_REQUEST_CHECKSUM_CALCULATION AWS_RESPONSE_CHECKSUM_VALIDATION
    row "이후 시험" "기본값" "when_required 가 실패해서"
else
    row "이후 시험" "when_required" ""
fi

#==============================================================================
section "2. 조건부 PUT (If-None-Match: *)"
echo first > c1.txt
echo second > c2.txt
COND_OK=0
if put "$TESTDIR/cond.txt" c1.txt --if-none-match '*'; then C1=통과; else C1="실패: $(err put.err)"; fi
row "없는 키에 조건부 PUT" "$C1" ""
if put "$TESTDIR/cond.txt" c2.txt --if-none-match '*'; then
    C2="덮어씀 (조건이 무시됨)"
else
    C2="거절됨: $(err put.err)"
    grep -q 'PreconditionFailed\|412' put.err && COND_OK=1
fi
row "있는 키에 조건부 PUT" "$C2" "거절돼야 맞다 (PreconditionFailed 412)"
aws s3api get-object --bucket "$BUCKET" --key "$TESTDIR/cond.txt" cond-back.txt > /dev/null 2> get.err
row "남은 내용" "$(cat cond-back.txt 2>/dev/null || echo 못 읽음)" "first 여야 맞다"
if aws s3api head-object --bucket "$BUCKET" --key "$TESTDIR/no-such-key-$RUN" > /dev/null 2> head.err; then
    H="있다고 함"
else
    H="없음: $(err head.err)"
fi
row "없는 키 head-object" "$H" "올리기 전 '있으면 덮어쓰지 않기' 에 쓴다"
if [ "$COND_OK" = 1 ]; then COND=(--if-none-match '*'); else COND=(); fi

#==============================================================================
section "3. 캐시 동작 (https://dl.meon.co.kr, 캐시 규칙: Cache-Control 을 따르고 없으면 캐시 안 함)"
M=meon/v1/test/cache.json
printf '{"v":1,"run":%s}' "$RUN" > v1.json
printf '{"v":2,"run":%s}' "$RUN" > v2.json
put "$M" v1.json --content-type application/json --cache-control "public, max-age=60" || critical "매니페스트 올리기" "$(err put.err)"
get "$DL/$M"; row "매니페스트 첫 GET" "$(cat body.out | head -c 40)" "$(desc)"
sleep 3
get "$DL/$M"; row "매니페스트 두 번째 GET" "$(cat body.out | head -c 40)" "$(desc) (HIT 이어야 캐시됨)"
put "$M" v2.json --content-type application/json --cache-control "public, max-age=60" || critical "매니페스트 바꾸기" "$(err put.err)"
get "$DL/$M"; row "바꾼 직후 GET" "$(cat body.out | head -c 40)" "$(desc) (v1 이면 60초 캐시가 옛것을 줌)"
get "$DL/$M?check=$RUN-$RANDOM"; row "쿼리를 붙인 GET" "$(cat body.out | head -c 40)" "$(desc) (v2 면 쿼리가 캐시 키에 들어감 → 올린 뒤 확인에 씀)"
sleep "${CACHE_WAIT:-65}"
get "$DL/$M"; row "65초 뒤 GET" "$(cat body.out | head -c 40)" "$(desc) (v2 여야 맞다)"

put "$TESTDIR/immutable.bin" pkg.bin --content-type application/octet-stream --cache-control "public, max-age=31536000, immutable" \
    || critical "빌드 파일 모양 올리기" "$(err put.err)"
get "$DL/$TESTDIR/immutable.bin"; S1=$(sha256sum body.out | cut -d' ' -f1)
row "빌드 파일 첫 GET" "$([ "$S1" = "$PKG_SHA" ] && echo sha256 같음 || echo sha256 다름)" "$(desc)"
get "$DL/$TESTDIR/immutable.bin"; S2=$(sha256sum body.out | cut -d' ' -f1)
row "빌드 파일 두 번째 GET" "$([ "$S2" = "$PKG_SHA" ] && echo sha256 같음 || echo sha256 다름)" "$(desc)"

put "$TESTDIR/no-cache-control.json" v1.json --content-type application/json || critical "Cache-Control 없는 파일 올리기" "$(err put.err)"
get "$DL/$TESTDIR/no-cache-control.json"; row "Cache-Control 없음 첫 GET" "$CODE" "$(desc)"
sleep 2
get "$DL/$TESTDIR/no-cache-control.json"; row "Cache-Control 없음 두 번째 GET" "$CODE" "$(desc) (HIT 가 아니어야 규칙대로)"

get "$DL/meon/v1/test/missing-$RUN.json"; row "없는 매니페스트" "$CODE" "$(desc) (404 면 앱은 GitHub 로 간다)"
get "$DL/meon/v1/test/missing-$RUN.json"; row "없는 매니페스트 다시" "$CODE" "$(desc)"

get -H "Origin: https://meon.co.kr" "$DL/$M"
ACAO=$(grep -i '^access-control-allow-origin:' h.out | tail -1 | cut -d' ' -f2- | tr -d '\r')
row "CORS (Origin https://meon.co.kr)" "${ACAO:-없음}" "웹 /download 페이지가 매니페스트를 읽을 때"
get -H "Origin: https://example.com" "$DL/$M"
ACAO=$(grep -i '^access-control-allow-origin:' h.out | tail -1 | cut -d' ' -f2- | tr -d '\r')
row "CORS (Origin https://example.com)" "${ACAO:-없음}" "없어야 맞다"
HTTP=$(curl -sS -o /dev/null -w '%{http_code} %{redirect_url}' "http://dl.meon.co.kr/$M" 2>&1)
row "http:// (TLS 없음)" "$HTTP" "앱은 https 만 쓴다"

#==============================================================================
section "4. 서명 키: CI 개인 키 ↔ 앱 키 헤더 (MeonUpdateKeys.h gpl-1)"
"$TOOL" selftest > /dev/null || critical "검증 도구 selftest" "RFC 8032 벡터 실패"
printf '%s\n' "$SIGNING_KEY" > key.pem
if ! openssl pkey -in key.pem -noout 2> pkey.err; then
    critical "개인 키 읽기" "PEM 으로 읽지 못함 ($(err pkey.err | head -c 120))"
fi
PUBDER=$(openssl pkey -in key.pem -pubout -outform DER 2>/dev/null | od -An -tx1 | tr -d ' \n')
APPKEY=$("$TOOL" keyhex gpl-1)
if [ "${PUBDER:0:24}" != "302a300506032b6570032100" ]; then
    critical "키 종류" "Ed25519 공개 키 모양이 아님"
elif [ "${PUBDER:24}" = "$APPKEY" ]; then
    row "개인 키에서 뽑은 공개 키 = 앱 키 헤더 gpl-1" "일치" "$APPKEY"
else
    critical "개인 키에서 뽑은 공개 키 = 앱 키 헤더 gpl-1" "다름: 개인 키 쪽 ${PUBDER:24}, 앱 $APPKEY"
fi

# sign <payload> <봉투> : openssl 로 서명하고 도구로 봉투를 만든다
sign() { openssl pkeyutl -sign -inkey key.pem -rawin -in "$1" -out sig.bin && "$TOOL" envelope "$1" gpl-1 sig.bin > "$2"; }
VERSION=$(sed -n 's/^project(MEON VERSION \([0-9.]*\)).*/\1/p' "$REPO/CMakeLists.txt")
COMMIT=${GITHUB_SHA:-unknown}

#==============================================================================
section "5. 시험 매니페스트 (meon/v1/test/{mac,windows}.json): 파일 먼저, 매니페스트 마지막"
for P in mac windows; do
    if [ "$P" = mac ]; then KIND=mac-app-zip; FILE=meon-test-mac.zip; CT=application/zip; MINOS=10.13
    else KIND=win-inno; FILE=meon-test-setup.exe; CT=application/vnd.microsoft.portable-executable; MINOS=6.1; fi
    KEY="$TESTDIR/$FILE"
    if put "$KEY" pkg.bin --content-type "$CT" --cache-control "public, max-age=31536000, immutable" "${COND[@]}"; then
        row "$P 파일 올리기" 통과 "$KEY"
    else
        critical "$P 파일 올리기" "$(err put.err)"
        continue
    fi
    get "$DL/$KEY"
    CDN_SHA=$(sha256sum body.out | cut -d' ' -f1)
    APP_SHA=$("$TOOL" sha256 body.out)
    if [ "$CODE" = 200 ] && [ "$CDN_SHA" = "$PKG_SHA" ] && [ "$APP_SHA" = "$PKG_SHA" ]; then
        row "$P CDN 으로 다시 받은 sha256" "같음" "sha256sum = 앱 코드(JUCE SHA256) = $PKG_SHA"
    else
        critical "$P CDN 으로 다시 받은 sha256" "HTTP $CODE, sha256sum $CDN_SHA, 앱 코드 $APP_SHA, 올린 것 $PKG_SHA"
    fi

    python3 "$REPO/scripts/dl_payload.py" --platform "$P" --channel test --build "$((RUN > 0 ? RUN : 1))" --version "$VERSION" \
        --commit "$COMMIT" --min-os "$MINOS" --package "$KIND" "$PUBLIC/$KEY" pkg.bin --installer "$PUBLIC/$KEY" pkg.bin > "$P-payload.json"
    if ! sign "$P-payload.json" "$P.json"; then critical "$P 서명" "openssl 또는 봉투 만들기 실패"; continue; fi
    if "$TOOL" verify "$P.json" --platform "$P" --channel test > verify.out; then
        row "$P 올리기 전 검증 (앱과 같은 코드·키 헤더)" 통과 "$(grep -E '^(keyid|build|package.sha256)=' verify.out | tr '\n' ' ')"
    else
        critical "$P 올리기 전 검증" "$(cat verify.out)"
        continue
    fi
    put "meon/v1/test/$P.json" "$P.json" --content-type application/json --cache-control "public, max-age=60" \
        || { critical "$P 매니페스트 올리기" "$(err put.err)"; continue; }
    get "$DL/meon/v1/test/$P.json?check=$RUN-$RANDOM"
    if [ "$CODE" = 200 ] && cmp -s body.out "$P.json" && "$TOOL" verify body.out --platform "$P" --channel test > verify.out; then
        row "$P CDN 으로 받은 매니페스트" "검증 통과" "$(desc)"
    else
        critical "$P CDN 으로 받은 매니페스트" "HTTP $CODE, $(head -1 verify.out)"
    fi
done

#==============================================================================
section "6. 위조·잘못된 매니페스트는 거절 (앱과 같은 코드)"
expect_reject() {  # expect_reject <설명> <봉투> <verify 인자…>
    local what=$1 file=$2
    shift 2
    if "$TOOL" verify "$file" "$@" > verify.out; then
        critical "$what" "받아들임 (거절해야 함)"
    else
        row "$what" "거절" "$(head -1 verify.out)"
    fi
}
# payload 를 바꾸고 원래 서명을 붙임
python3 - mac-payload.json forged-payload.json <<'EOF'
import json, sys
p = json.load(open(sys.argv[1]))
p["build"] += 1000
p["packages"][0]["sha256"] = "0" * 64
open(sys.argv[2], "w").write(json.dumps(p, separators=(",", ":")))
EOF
openssl pkeyutl -sign -inkey key.pem -rawin -in mac-payload.json -out orig.sig
"$TOOL" envelope forged-payload.json gpl-1 orig.sig > forged.json
expect_reject "payload 를 바꾸고 원래 서명" forged.json --platform mac --channel test
put "meon/v1/test/forged-mac.json" forged.json --content-type application/json --cache-control "public, max-age=60" || true
get "$DL/meon/v1/test/forged-mac.json?check=$RUN-$RANDOM"
expect_reject "CDN 에 올린 위조 매니페스트" body.out --platform mac --channel test
# 다른 키로 서명하고 gpl-1 이라고 붙임
openssl genpkey -algorithm ed25519 -out other.pem 2> /dev/null
openssl pkeyutl -sign -inkey other.pem -rawin -in mac-payload.json -out other.sig
"$TOOL" envelope mac-payload.json gpl-1 other.sig > other.json
expect_reject "다른 키로 서명 (keyid 는 gpl-1)" other.json --platform mac --channel test
expect_reject "다른 채널로 요청 (test → beta)" mac.json --platform mac --channel beta
expect_reject "다른 플랫폼으로 요청 (mac → windows)" mac.json --platform windows --channel test
# 빌드 기록(release.json): channel 이 없다. 기록으로는 통과, 채널 자리에서는 거절
python3 "$REPO/scripts/dl_payload.py" --platform mac --channel '' --build "$((RUN > 0 ? RUN : 1))" --version "$VERSION" \
    --commit "$COMMIT" --min-os 10.13 --package mac-app-zip "$PUBLIC/$TESTDIR/meon-test-mac.zip" pkg.bin > release-payload.json
sign release-payload.json release.json
if "$TOOL" verify release.json --platform mac --release > verify.out; then
    row "빌드 기록(release.json) 검증" 통과 "channel 없음"
    put "$TESTDIR/release.json" release.json --content-type application/json --cache-control "public, max-age=300" || true
else
    critical "빌드 기록(release.json) 검증" "$(cat verify.out)"
fi
expect_reject "빌드 기록을 채널 매니페스트로 요청" release.json --platform mac --channel test

rm -f key.pem other.pem
echo >> "$SUMMARY"
if [ "$FAILED" = 0 ]; then echo "**꼭 맞아야 하는 항목 모두 통과**" >> "$SUMMARY"; else echo "**실패한 항목이 있음**" >> "$SUMMARY"; fi
exit $FAILED
