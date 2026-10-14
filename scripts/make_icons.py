#!/usr/bin/env python3
"""Meon 앱 아이콘 생성기.

디자인 파일(images/brand/app-icon)을 그대로 그려서 만든다. 원본 SVG 는 고치지 않는다.
- macOS: meon_app_icon_mac.svg (Apple 템플릿: 1024 캔버스, 824 타일, 여백 100)
    → images/icons/mac/meon_mac_N.png, images/icons/MEON.icns
    → images/icons/AppIcon.icon (macOS 26 의 Icon Composer 형식. 타일 안쪽만 잘라 1024 로 키운 마크 + 흰 배경.
       모서리 모양·테두리는 시스템이 그린다. CI 의 Xcode 26 actool 로 Assets.car 를 만든다)
- Windows: meon_app_icon_win.svg (여백을 거의 없앤 Windows 전용)
    → images/icons/win/meon_win_N.png, images/icons/MEON.ico (16·24·32·48·64·128·256)

필요 도구: rsvg-convert (librsvg), Python Pillow
사용: python3 scripts/make_icons.py
"""

import io
import json
import os
import re
import struct
import subprocess
import tempfile

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "images", "brand", "app-icon")
OUT = os.path.join(ROOT, "images", "icons")
MAC_SVG = os.path.join(SRC, "meon_app_icon_mac.svg")
WIN_SVG = os.path.join(SRC, "meon_app_icon_win.svg")

MAC_SIZES = [16, 32, 64, 128, 256, 512, 1024]
WIN_SIZES = [16, 24, 32, 48, 64, 128, 256]


def render(svg_path, size):
    """SVG 를 size×size PNG(바이트)로"""
    return subprocess.run(["rsvg-convert", "-w", str(size), "-h", str(size), svg_path],
                          check=True, capture_output=True).stdout


def write_icns(pngs, path):
    """pngs: {크기: PNG 바이트}. 모든 항목을 PNG 로 담는다 (macOS 10.7+)"""
    types = [("icp4", 16), ("icp5", 32), ("icp6", 64), ("ic07", 128), ("ic08", 256), ("ic09", 512),
             ("ic10", 1024), ("ic11", 32), ("ic12", 64), ("ic13", 256), ("ic14", 512)]
    body = b""
    for code, size in types:
        data = pngs[size]
        body += code.encode("ascii") + struct.pack(">I", 8 + len(data)) + data
    with open(path, "wb") as f:
        f.write(b"icns" + struct.pack(">I", 8 + len(body)) + body)


def bmp_entry(png):
    """작은 크기는 예전 Windows 구성 요소와도 맞게 32비트 BMP(+AND 마스크)로 담는다"""
    img = Image.open(io.BytesIO(png)).convert("RGBA")
    w, h = img.size
    px = img.load()
    rows = b""
    for y in range(h - 1, -1, -1):   # 아래에서 위로
        for x in range(w):
            r, g, b, a = px[x, y]
            rows += bytes((b, g, r, a))
    mask_row = ((w + 31) // 32) * 4
    mask = b"\x00" * (mask_row * h)   # 투명도는 알파로 한다
    header = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0, len(rows) + len(mask), 0, 0, 0, 0)
    return header + rows + mask


def write_ico(pngs, path):
    entries = []
    for size in sorted(pngs):
        data = pngs[size] if size >= 256 else bmp_entry(pngs[size])   # 256 은 PNG 그대로
        entries.append((size, data))
    offset = 6 + 16 * len(entries)
    head = struct.pack("<HHH", 0, 1, len(entries))
    dir_ = b""
    blob = b""
    for size, data in entries:
        dim = 0 if size >= 256 else size
        dir_ += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(data), offset + len(blob))
        blob += data
    with open(path, "wb") as f:
        f.write(head + dir_ + blob)


def write_icon_composer(path):
    """macOS 26 Icon Composer 형식 (.icon 폴더). 레이어 하나(마크), 흰 배경, 유리 효과·그림자 없음"""
    with open(MAC_SVG, encoding="utf-8") as f:
        svg = f.read()
    # 타일(둥근 사각형)은 시스템이 그리므로 빼고, 타일 영역(100,100,824,824)만 캔버스 전체로 쓴다
    svg = re.sub(r"<rect\b[^>]*/>", "", svg, count=1)
    svg = svg.replace('viewBox="0 0 1024 1024"', 'viewBox="100 100 824 824"', 1)
    os.makedirs(os.path.join(path, "Assets"), exist_ok=True)
    with tempfile.NamedTemporaryFile("w", suffix=".svg", delete=False, encoding="utf-8") as tmp:
        tmp.write(svg)
    try:
        with open(os.path.join(path, "Assets", "mark.png"), "wb") as f:
            f.write(render(tmp.name, 1024))
    finally:
        os.unlink(tmp.name)
    icon = {
        "fill": {"solid": "srgb:1.00000,1.00000,1.00000,1.00000"},
        "groups": [{
            "layers": [{"glass": False, "image-name": "mark.png", "name": "mark"}],
            "shadow": {"kind": "neutral", "opacity": 0},
            "translucency": {"enabled": False, "value": 0},
        }],
        "supported-platforms": {"squares": ["macOS"]},
    }
    with open(os.path.join(path, "icon.json"), "w", encoding="utf-8") as f:
        json.dump(icon, f, indent=2)
        f.write("\n")


def main():
    os.makedirs(os.path.join(OUT, "mac"), exist_ok=True)
    os.makedirs(os.path.join(OUT, "win"), exist_ok=True)

    mac = {s: render(MAC_SVG, s) for s in MAC_SIZES}
    for s, data in mac.items():
        with open(os.path.join(OUT, "mac", f"meon_mac_{s}.png"), "wb") as f:
            f.write(data)
    write_icns(mac, os.path.join(OUT, "MEON.icns"))

    win = {s: render(WIN_SVG, s) for s in WIN_SIZES}
    for s, data in win.items():
        with open(os.path.join(OUT, "win", f"meon_win_{s}.png"), "wb") as f:
            f.write(data)
    write_ico(win, os.path.join(OUT, "MEON.ico"))

    write_icon_composer(os.path.join(OUT, "AppIcon.icon"))
    print("만들어짐:", OUT)


if __name__ == "__main__":
    main()
