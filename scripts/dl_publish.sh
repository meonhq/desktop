#!/usr/bin/env bash
# dl.meon.co.kr (Cloudflare R2 버킷 meon-dl) 에 릴리스를 올린다. mac.yml·windows.yml 의 publish-dl 작업이 부른다.
# 배포 설계 DISTRIBUTION_PLAN 3.2·3.6·3.9, 부록 B. GitHub 릴리스(build.txt 마지막)는 빌드 작업이 먼저 끝냈다.
#
# 순서 (하나라도 틀리면 거기서 멈추고 1 로 끝난다. 채널 매니페스트는 마지막이라, 멈추면 앱은 새 빌드를 보지 못한다)
#   0. 감시: 지금 채널 매니페스트(beta·stable)를 받아 앱과 같은 코드로 검증한다. 이 빌드보다 작은 채널만 올린다
#      (같거나 큰 채널은 건너뛴다). 다른 플랫폼의 매니페스트가 이 빌드보다 크면 옛 커밋이므로 멈춘다.
#   1. 빌드 파일: meon/<플랫폼>/<빌드>/ 아래. 덮어쓰지 않는다 (이미 있으면 같은 파일인지만 본다: 실패한 작업 다시 돌리기)
#   2. https://dl.meon.co.kr 로 다시 받아 sha256 비교 (sha256sum 과 앱 코드)
#   3. release.json (그 빌드의 서명한 기록, channel 없음): 서명 → 앱과 같은 코드로 검증 → 올리기
#   4. 채널 매니페스트 마지막: beta, stable 순서. 서명 → 검증 → 올리기 → CDN 으로 다시 받아 검증
# 출시 전(지금)은 desktop 릴리스를 beta·stable 둘 다에 올린다 (배포 설계 3.6).
# 비밀값은 출력하지 않는다 (set -x 금지).
#
# 인자: <mac|windows> <산출물 폴더>
# 환경: BUILD_ID VERSION TOOL R2_ACCOUNT_ID AWS_ACCESS_KEY_ID AWS_SECRET_ACCESS_KEY SIGNING_KEY (GITHUB_SHA, GITHUB_STEP_SUMMARY)
set -uo pipefail

P=${1:-}
ART=${2:-}
for v in BUILD_ID VERSION TOOL R2_ACCOUNT_ID AWS_ACCESS_KEY_ID AWS_SECRET_ACCESS_KEY SIGNING_KEY; do
    if [ -z "${!v:-}" ]; then echo "::error::$v 가 비어 있음"; exit 1; fi
done

REPO=$(cd "$(dirname "$0")/.." && pwd)
BUCKET=${BUCKET:-meon-dl}
DL=${DL:-https://dl.meon.co.kr}       # 받아 보는 주소
PUBLIC=https://dl.meon.co.kr          # 매니페스트에 적는 주소 (앱이 믿는 머리)
CHANNELS=${CHANNELS:-"beta stable"}
COMMIT=${GITHUB_SHA:-$(git -C "$REPO" rev-parse HEAD)}
SUMMARY=${GITHUB_STEP_SUMMARY:-/dev/null}
N=$BUILD_ID

export AWS_DEFAULT_REGION=auto
export AWS_ENDPOINT_URL="https://${R2_ACCOUNT_ID}.r2.cloudflarestorage.com"
export AWS_PAGER=""
export AWS_REQUEST_CHECKSUM_CALCULATION=${AWS_REQUEST_CHECKSUM_CALCULATION:-when_required}
export AWS_RESPONSE_CHECKSUM_VALIDATION=${AWS_RESPONSE_CHECKSUM_VALIDATION:-when_required}

# 조건부 PUT (If-None-Match: *): 빌드 파일을 덮어쓰지 않게. dl-test 로 R2 가 지원하는지 확인했다. DL_COND_PUT=0 이면 head-object 로만 막는다
COND=()
[ "${DL_COND_PUT:-1}" = 1 ] && COND=(--if-none-match '*')

W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
umask 077

fail() {
    local m=${1//$R2_ACCOUNT_ID/***}
    echo "::error::$m"
    echo "**실패**: $m" >> "$SUMMARY"
    exit 1
}
say() { echo "$1"; echo "- $1" >> "$SUMMARY"; }
err() { tr '\n' ' ' < "$1" | tail -c 400; }
sha() { sha256sum "$1" | cut -d' ' -f1; }

# put <key> <file> <content-type> <cache-control> [추가 인자…]
put() {
    aws s3api put-object --bucket "$BUCKET" --key "$1" --body "$2" --content-type "$3" --cache-control "$4" "${@:5}" \
        > "$W/put.out" 2> "$W/put.err"
}
exists() { aws s3api head-object --bucket "$BUCKET" --key "$1" > /dev/null 2> "$W/head.err"; }
# fetch <주소> <파일> → HTTP 상태 (캐시를 피하려면 주소에 ?check= 를 붙인다)
fetch() { curl -sS -o "$2" -w '%{http_code}' -A "meon-publish/$N" "$1" 2> "$W/curl.err" || echo 000; }

#==============================================================================
[[ "$N" =~ ^[0-9]+$ ]] && [ "$N" -ge 1 ] && [ "$N" -lt 10000 ] || fail "빌드 번호가 1–9999 가 아님 ($N). desktop 은 10000 미만 (배포 설계 4.1)"
[[ "$VERSION" =~ ^[0-9]+(\.[0-9]+)*$ ]] || fail "버전 형식이 틀림: $VERSION"

case "$P" in
    mac)
        PKG_FILE="$ART/MEON-mac.zip"
        INS_FILE=$(ls "$ART"/meon-*.dmg 2> /dev/null | head -1)
        KIND=mac-app-zip
        PKG_KEY="meon/mac/$N/meon-$VERSION-b$N-mac.zip"; PKG_CT=application/zip
        INS_KEY="meon/mac/$N/meon-$VERSION-b$N.dmg";     INS_CT=application/x-apple-diskimage
        MINOS=$(sed -n 's/.*set *(CMAKE_OSX_DEPLOYMENT_TARGET *"\([0-9.]*\)".*/\1/p' "$REPO/CMakeLists.txt" | head -1)
        OTHER=windows
        ;;
    windows)
        PKG_FILE=$(ls "$ART"/meon-*-setup.exe 2> /dev/null | head -1)
        INS_FILE=$PKG_FILE
        KIND=win-inno
        PKG_KEY="meon/windows/$N/meon-$VERSION-b$N-setup.exe"; PKG_CT=application/vnd.microsoft.portable-executable
        INS_KEY=$PKG_KEY; INS_CT=$PKG_CT
        MINOS=$(sed -n 's/^MinVersion=\([0-9.]*\).*/\1/p' "$REPO/release/wininstaller.iss" | head -1)
        OTHER=mac
        ;;
    *) fail "플랫폼은 mac 또는 windows";;
esac
[ -f "$PKG_FILE" ] && [ -f "$INS_FILE" ] || fail "산출물이 없음 ($ART: $(ls "$ART" 2> /dev/null | tr '\n' ' '))"
[[ "$MINOS" =~ ^[0-9]+(\.[0-9]+)*$ ]] || fail "최소 OS 버전을 읽지 못함 ($MINOS)"
"$TOOL" selftest > /dev/null || fail "검증 도구 selftest 실패"

{
    echo "## dl.meon.co.kr 올리기: $P build $N (v$VERSION, ${COMMIT:0:8})"
    echo
} >> "$SUMMARY"

#==============================================================================
# 0. 감시
TODO=""
for ch in $CHANNELS; do
    code=$(fetch "$DL/meon/v1/$ch/$P.json?check=$N-$RANDOM" "$W/cur-$ch.json")
    if [ "$code" = 404 ]; then
        say "$ch/$P.json: 아직 없음 → 올린다"
        TODO="$TODO $ch"
    elif [ "$code" = 200 ]; then
        "$TOOL" verify "$W/cur-$ch.json" --platform "$P" --channel "$ch" > "$W/v.out" \
            || fail "지금 $ch/$P.json 이 검증되지 않음 ($(head -1 "$W/v.out")). 사람이 확인한 뒤 다시 돌린다"
        cur=$(sed -n 's/^build=//p' "$W/v.out")
        if [ "$cur" -lt "$N" ]; then
            say "$ch/$P.json: 지금 build $cur → $N 으로 올린다"
            TODO="$TODO $ch"
        else
            say "$ch/$P.json: 지금 build $cur ≥ $N → 건너뜀"
        fi
    else
        fail "$ch/$P.json 을 받지 못함 (HTTP $code $(err "$W/curl.err"))"
    fi
done
for ch in $CHANNELS; do
    code=$(fetch "$DL/meon/v1/$ch/$OTHER.json?check=$N-$RANDOM" "$W/other-$ch.json")
    if [ "$code" = 200 ] && "$TOOL" verify "$W/other-$ch.json" --platform "$OTHER" --channel "$ch" > "$W/v.out"; then
        other=$(sed -n 's/^build=//p' "$W/v.out")
        [ "$other" -le "$N" ] || fail "$ch/$OTHER.json 이 build $other 로 이 빌드($N)보다 큼: 옛 커밋을 릴리스하고 있다"
    fi
done
if [ -z "$TODO" ]; then
    say "올릴 채널이 없음 (모두 build $N 이상)"
    exit 0
fi

#==============================================================================
# 서명 준비: 무엇이든 올리기 전에 키를 읽고 앱 키 헤더와 맞는지 본다 (틀리면 아무것도 올리지 않는다)
# sign <payload> <봉투> : 서명(openssl) → 봉투(도구). 개인 키는 이 작업의 임시 폴더에만 잠깐 둔다.
# 비밀값은 openssl 이 읽는 PEM 으로 맞춘다 (scripts/dl_signing_key.py: 줄바꿈이 깨진 PEM 등. 내용은 적지 않는다)
SIGNING_KEY="$SIGNING_KEY" python3 "$REPO/scripts/dl_signing_key.py" "$W/key.pem" 2> "$W/keyshape.txt" \
    || fail "서명 키(DL_SIGNING_KEY_GPL1)를 맞추지 못함: $(tr '\n' ' ' < "$W/keyshape.txt")"
openssl pkey -in "$W/key.pem" -noout 2> /dev/null || fail "서명 키(DL_SIGNING_KEY_GPL1)를 읽지 못함"
PUBDER=$(openssl pkey -in "$W/key.pem" -pubout -outform DER 2> /dev/null | od -An -tx1 | tr -d ' \n')
[ "${PUBDER:24}" = "$("$TOOL" keyhex gpl-1)" ] || fail "서명 키의 공개 키가 앱 키 헤더(MeonUpdateKeys.h gpl-1)와 다름"
sign() { openssl pkeyutl -sign -inkey "$W/key.pem" -rawin -in "$1" -out "$W/sig.bin" && "$TOOL" envelope "$1" gpl-1 "$W/sig.bin" > "$2"; }
payload() {   # <채널 (비우면 release.json)> > payload
    python3 "$REPO/scripts/dl_payload.py" --platform "$P" --channel "$1" --build "$N" --version "$VERSION" \
        --commit "$COMMIT" --min-os "$MINOS" --released-at "$RELEASED_AT" \
        --package "$KIND" "$PUBLIC/$PKG_KEY" "$PKG_FILE" --installer "$PUBLIC/$INS_KEY" "$INS_FILE"
}
RELEASED_AT=$(date -u +%Y-%m-%dT%H:%M:%SZ)

#==============================================================================
# 1·2. 빌드 파일 (덮어쓰지 않는다) → CDN 으로 다시 받아 sha256
upload_file() {   # <로컬 파일> <키> <content-type>
    local file=$1 key=$2 ct=$3 want got app
    want=$(sha "$file")
    if exists "$key"; then
        code=$(fetch "$DL/$key?check=$N-$RANDOM" "$W/exist.bin")
        got=$(sha "$W/exist.bin")
        [ "$code" = 200 ] && [ "$got" = "$want" ] || fail "$key 가 이미 있고 이 파일과 다름 (덮어쓰지 않는다. 새 커밋으로 다시 릴리스)"
        say "$key: 이미 같은 파일이 있음 (다시 돌린 작업)"
    else
        put "$key" "$file" "$ct" "public, max-age=31536000, immutable" "${COND[@]}" \
            || fail "$key 올리기 실패: $(err "$W/put.err")"
        say "$key: 올림 ($(stat -c %s "$file") 바이트)"
    fi
    code=$(fetch "$DL/$key" "$W/cdn.bin")
    got=$(sha "$W/cdn.bin")
    app=$("$TOOL" sha256 "$W/cdn.bin")
    [ "$code" = 200 ] && [ "$got" = "$want" ] && [ "$app" = "$want" ] \
        || fail "$key: CDN 으로 받은 파일이 다름 (HTTP $code, sha256 $got / 앱 코드 $app / 올린 것 $want)"
    say "$key: CDN sha256 확인 $want"
    rm -f "$W/cdn.bin" "$W/exist.bin"
}
upload_file "$PKG_FILE" "$PKG_KEY" "$PKG_CT"
if [ "$INS_KEY" != "$PKG_KEY" ]; then
    upload_file "$INS_FILE" "$INS_KEY" "$INS_CT"
fi


#==============================================================================
# 3. release.json
REC_KEY="meon/$P/$N/release.json"
payload "" > "$W/release-payload.json" || fail "payload 만들기 실패"
sign "$W/release-payload.json" "$W/release.json" || fail "release.json 서명 실패"
"$TOOL" verify "$W/release.json" --platform "$P" --release > "$W/v.out" || fail "release.json 검증 실패: $(head -1 "$W/v.out")"
if exists "$REC_KEY"; then
    code=$(fetch "$DL/$REC_KEY?check=$N-$RANDOM" "$W/rec-old.json")
    "$TOOL" verify "$W/rec-old.json" --platform "$P" --release > "$W/v-old.out" || fail "있는 $REC_KEY 가 검증되지 않음"
    [ "$(grep '^package.sha256=' "$W/v-old.out")" = "$(grep '^package.sha256=' "$W/v.out")" ] || fail "있는 $REC_KEY 의 파일이 이 빌드와 다름"
    say "$REC_KEY: 이미 있음 (같은 파일)"
else
    put "$REC_KEY" "$W/release.json" application/json "public, max-age=300" "${COND[@]}" \
        || fail "$REC_KEY 올리기 실패: $(err "$W/put.err")"
    say "$REC_KEY: 서명·검증·올림"
fi

#==============================================================================
# 4. 채널 매니페스트 (마지막)
for ch in $TODO; do
    KEY="meon/v1/$ch/$P.json"
    payload "$ch" > "$W/$ch-payload.json" || fail "payload 만들기 실패 ($ch)"
    sign "$W/$ch-payload.json" "$W/$ch.json" || fail "$ch 서명 실패"
    "$TOOL" verify "$W/$ch.json" --platform "$P" --channel "$ch" > "$W/v.out" || fail "$ch 매니페스트 검증 실패: $(head -1 "$W/v.out")"
    [ "$(sed -n 's/^build=//p' "$W/v.out")" = "$N" ] || fail "$ch 매니페스트의 build 가 $N 이 아님"
    put "$KEY" "$W/$ch.json" application/json "public, max-age=60" || fail "$KEY 올리기 실패: $(err "$W/put.err")"
    code=$(fetch "$DL/$KEY?check=$N-$RANDOM" "$W/back.json")
    [ "$code" = 200 ] && cmp -s "$W/back.json" "$W/$ch.json" \
        && "$TOOL" verify "$W/back.json" --platform "$P" --channel "$ch" > /dev/null \
        || fail "$KEY: 올린 뒤 CDN 으로 받은 것이 다르거나 검증되지 않음 (HTTP $code)"
    say "$KEY: build $N 올림 (서명 gpl-1, CDN 으로 다시 받아 검증)"
done
rm -f "$W/key.pem"

{
    echo
    echo "| 항목 | 값 |"
    echo "|---|---|"
    echo "| 빌드 | $N (v$VERSION, $COMMIT) |"
    echo "| 앱 안 업데이트 ($KIND) | $PUBLIC/$PKG_KEY |"
    echo "| sha256 | $(sha "$PKG_FILE") ($(stat -c %s "$PKG_FILE") 바이트) |"
    if [ "$INS_KEY" != "$PKG_KEY" ]; then
        echo "| 사람용 설치 파일 | $PUBLIC/$INS_KEY |"
        echo "| sha256 | $(sha "$INS_FILE") ($(stat -c %s "$INS_FILE") 바이트) |"
    fi
    echo "| 최소 OS | $MINOS |"
    echo "| 매니페스트 | $(for ch in $TODO; do printf '%s ' "$PUBLIC/meon/v1/$ch/$P.json"; done)|"
} >> "$SUMMARY"
echo "완료: $P build $N →$TODO"
