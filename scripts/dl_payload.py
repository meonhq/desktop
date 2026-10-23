#!/usr/bin/env python3
"""업데이트 매니페스트의 payload(서명하기 전 JSON)를 만든다. 배포 설계 DISTRIBUTION_PLAN 3.3, 부록 A.

받을 파일의 sha256·크기는 로컬 파일에서 계산한다. 서명은 하지 않는다 (openssl pkeyutl -sign -rawin),
봉투 만들기·검증은 MEON_ManifestTool (앱과 같은 코드). 표준 라이브러리만 쓴다.

    python3 scripts/dl_payload.py --platform mac --channel beta --build 980 --version 1.0.0 \\
        --commit <sha> --min-os 10.13 \\
        --package mac-app-zip https://dl.meon.co.kr/meon/mac/980/meon-1.0.0-b980-mac.zip dist/MEON-mac.zip \\
        --installer https://dl.meon.co.kr/meon/mac/980/meon-1.0.0-b980.dmg dist/meon-1.0.0.dmg > payload.json

--channel 을 비우면(--channel '') 빌드 기록(release.json)용: channel 필드를 넣지 않는다.
"""
import argparse
import datetime
import hashlib
import json
import os
import sys


def file_entry(url, path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return {"url": url, "sha256": h.hexdigest(), "size": os.path.getsize(path)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--platform", required=True, choices=["mac", "windows"])
    ap.add_argument("--channel", required=True)
    ap.add_argument("--build", required=True, type=int)
    ap.add_argument("--version", required=True)
    ap.add_argument("--commit", required=True)
    ap.add_argument("--min-os", required=True)
    ap.add_argument("--released-at", default="")
    ap.add_argument("--package", nargs=3, metavar=("KIND", "URL", "FILE"), required=True)
    ap.add_argument("--installer", nargs=2, metavar=("URL", "FILE"))
    ap.add_argument("--notes", default="")
    a = ap.parse_args()

    if not 1 <= a.build <= 2**31 - 1:
        sys.exit("build out of range")

    released = a.released_at or datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    p = {"schema": 1, "product": "meon"}
    if a.channel:
        p["channel"] = a.channel
    p.update({
        "platform": a.platform,
        "build": a.build,
        "version": a.version,
        "commit": a.commit,
        "released_at": released,
        "min_os": a.min_os,
    })
    kind, url, path = a.package
    pkg = {"kind": kind}
    pkg.update(file_entry(url, path))
    p["packages"] = [pkg]
    if a.installer:
        p["installer"] = file_entry(a.installer[0], a.installer[1])
    if a.notes:
        p["notes"] = a.notes
    # desktop 은 GPL: 실행 파일 옆에 소스 위치를 둔다 (DISTRIBUTION_PLAN 5.4)
    p["license"] = "GPL-3.0"
    p["source_url"] = "https://github.com/meonhq/desktop/tree/" + a.commit

    sys.stdout.write(json.dumps(p, ensure_ascii=True, separators=(",", ":")))


if __name__ == "__main__":
    main()
