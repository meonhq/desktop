#!/usr/bin/env bash
# 올라간 릴리스를 밖에서 확인한다 (update-e2e.yml 의 dl 작업). 비밀값 없이 공개 주소만 읽는다.
#   - meon/v1/{beta,stable}/{mac,windows}.json 을 받아 앱과 같은 코드(MEON_ManifestTool)로 검증
#   - 매니페스트의 파일(packages·installer)을 받아 크기·sha256 비교 (sha256sum 과 앱 코드)
#   - 같은 플랫폼의 GitHub build.txt 와 번호가 같은지
#   - 매니페스트를 바꾸면(번호·해시) 거절하는지, 다른 채널·플랫폼 자리로는 거절하는지
# 환경: TOOL (MEON_ManifestTool). 결과는 GITHUB_STEP_SUMMARY 에 표로.
set -uo pipefail
: "${TOOL:?}"
DL=${DL:-https://dl.meon.co.kr}
SUMMARY=${GITHUB_STEP_SUMMARY:-/dev/null}
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
FAILED=0
row() { echo "| $1 | $2 | $3 |" >> "$SUMMARY"; echo "== $1: $2 — $3"; }
bad() { FAILED=1; row "$1" "**실패**" "$2"; echo "::error::$1: $2"; }

printf '## dl.meon.co.kr 릴리스 확인\n\n| 항목 | 결과 | 내용 |\n|---|---|---|\n' >> "$SUMMARY"
"$TOOL" selftest > /dev/null || bad "검증 도구" "selftest 실패"

for P in mac windows; do
    GH=$(curl -fsSL "https://github.com/meonhq/desktop/releases/download/$P-latest/build.txt" | tr -d '[:space:]')
    for CH in beta stable; do
        M="$W/$CH-$P.json"
        code=$(curl -sS -o "$M" -w '%{http_code}' "$DL/meon/v1/$CH/$P.json?check=$RANDOM")
        if [ "$code" != 200 ]; then bad "$CH/$P.json" "HTTP $code"; continue; fi
        if ! "$TOOL" verify "$M" --platform "$P" --channel "$CH" > "$W/v.out"; then bad "$CH/$P.json 검증" "$(head -1 "$W/v.out")"; continue; fi
        BUILD=$(sed -n 's/^build=//p' "$W/v.out")
        row "$CH/$P.json 검증" "통과" "build $BUILD, 키 $(sed -n 's/^keyid=//p' "$W/v.out"), min_os $(sed -n 's/^min_os=//p' "$W/v.out")"
        [ "$BUILD" = "$GH" ] && row "$CH/$P GitHub build.txt" "같음" "$GH" || bad "$CH/$P GitHub build.txt" "매니페스트 $BUILD, GitHub $GH"
        for part in package installer; do
            URL=$(sed -n "s/^$part.url=//p" "$W/v.out"); SHA=$(sed -n "s/^$part.sha256=//p" "$W/v.out"); SIZE=$(sed -n "s/^$part.size=//p" "$W/v.out")
            [ -n "$URL" ] || continue
            curl -fsS -o "$W/f.bin" "${URL/#https:\/\/dl.meon.co.kr/$DL}" || { bad "$CH/$P $part 받기" "$URL"; continue; }
            GOT=$(sha256sum "$W/f.bin" | cut -d' ' -f1); APP=$("$TOOL" sha256 "$W/f.bin"); LEN=$(stat -c %s "$W/f.bin")
            if [ "$GOT" = "$SHA" ] && [ "$APP" = "$SHA" ] && [ "$LEN" = "$SIZE" ]; then
                row "$CH/$P $part" "크기·sha256 같음" "$URL ($LEN 바이트, $SHA)"
            else
                bad "$CH/$P $part" "$URL: 크기 $LEN/$SIZE, sha256 $GOT / 앱 코드 $APP / 매니페스트 $SHA"
            fi
            rm -f "$W/f.bin"
        done
        # 바꾼 매니페스트·다른 자리는 거절해야 한다 (앱과 같은 코드)
        python3 - "$M" "$W/forged.json" <<'EOF'
import base64, json, sys
env = json.load(open(sys.argv[1]))
p = json.loads(base64.b64decode(env["signed"]))
p["build"] += 1
p["packages"][0]["sha256"] = "0" * 64
env["signed"] = base64.b64encode(json.dumps(p, separators=(",", ":")).encode()).decode()
json.dump(env, open(sys.argv[2], "w"))
EOF
        if "$TOOL" verify "$W/forged.json" --platform "$P" --channel "$CH" > "$W/v.out"; then bad "$CH/$P 위조 거절" "번호·해시를 바꾼 매니페스트를 받아들임"; else row "$CH/$P 위조 거절" "거절" "$(head -1 "$W/v.out")"; fi
        OTHER_CH=$([ "$CH" = beta ] && echo stable || echo beta)
        OTHER_P=$([ "$P" = mac ] && echo windows || echo mac)
        if "$TOOL" verify "$M" --platform "$P" --channel "$OTHER_CH" > /dev/null; then bad "$CH/$P 를 $OTHER_CH 자리에" "받아들임"; else row "$CH/$P 를 $OTHER_CH 자리에" "거절" "channel 확인"; fi
        if "$TOOL" verify "$M" --platform "$OTHER_P" --channel "$CH" > /dev/null; then bad "$CH/$P 를 $OTHER_P 자리에" "받아들임"; else row "$CH/$P 를 $OTHER_P 자리에" "거절" "platform 확인"; fi
    done
done

echo >> "$SUMMARY"
[ "$FAILED" = 0 ] && echo "**모두 통과**" >> "$SUMMARY" || echo "**실패한 항목이 있음**" >> "$SUMMARY"
exit $FAILED
