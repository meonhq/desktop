#!/usr/bin/env python3
"""MEON 아이콘 생성기.

images/meon_logo.svg (필기체 로고, 외곽선 수정 없음) 를 #FFFFFF 단색 배경 위에 #1A1A1A 로 올려
macOS .icns / Windows .ico / 크기별 PNG / 미리보기 이미지를 만든다.

- 큰 크기는 로고 전체, 작은 크기(아래 M_ONLY_*)는 첫 글자 M 만 잘라서 쓴다.
- 로고 주변 여백은 로고(보이는 글자) 높이의 25% 이상.
- 그림자·그라디언트·반투명 없음.
- macOS 메뉴 막대 아이콘(images/icons/tray): 배경 없는 검은 M. 템플릿 이미지라 OS 가 밝기·다크 모드에 맞춰 칠한다.
  (Windows 알림 영역은 앱 아이콘 .ico 의 작은 크기를 그대로 쓴다)

필요 도구: rsvg-convert, magick (ImageMagick), iconutil (macOS)
사용: python3 scripts/make_icons.py
"""

import os
import re
import shutil
import subprocess
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOGO_SVG = os.path.join(ROOT, "images", "meon_logo.svg")
OUT_DIR = os.path.join(ROOT, "images", "icons")

BG = "#FFFFFF"
INK = "#1A1A1A"

MAC_SIZES = [16, 32, 64, 128, 256, 512, 1024]
WIN_SIZES = [16, 24, 32, 48, 256]

# 이 크기 이하에서는 로고 전체 대신 M 만 쓴다 (전체 로고는 획 굵기가 1px 아래로 떨어져 뭉개진다)
M_ONLY_MAX_MAC = 64
M_ONLY_MAX_WIN = 48

# 로고 경로의 실제 외곽(viewBox 기준). 여백 계산은 이 '보이는 글자' 기준으로 한다.
FULL_BOX = (8.1, 8.1, 388.6, 144.7)          # x0, y0, x1, y1
# M 만 남기는 자르기 다각형 (E 로 이어지는 연결 획을 획 방향에 수직으로 끊는다)
M_CLIP = [(0, 0), (182, 0), (182, 58), (150, 96), (132, 114), (152, 134), (152, 153), (0, 153)]
M_BOX = (8.1, 8.1, 176.9, 144.7)

# macOS 앱 아이콘 격자: 1024 캔버스 안에 824 둥근 사각형 (Apple 템플릿과 같은 비율)
MAC_TILE = 824 / 1024
MAC_RADIUS = 185.4 / 824
# Windows: 캔버스를 꽉 채우는 사각형, 모서리만 살짝 둥글게
WIN_TILE = 1.0
WIN_RADIUS = 0.125


def logo_path_d():
    with open(LOGO_SVG, encoding="utf-8") as f:
        return re.search(r' d="([^"]+)"', f.read()).group(1)


def icon_svg(size, tile, radius, m_only):
    """size×size 아이콘 한 장의 SVG. 로고는 viewBox 단위 그대로 두고 transform 으로 크기·위치만 바꾼다."""
    d = logo_path_d()
    tile_px = size * tile
    off = (size - tile_px) / 2
    x0, y0, x1, y1 = M_BOX if m_only else FULL_BOX
    w, h = x1 - x0, y1 - y0
    # 타일 안에서 로고가 차지할 폭 비율 (여백 = 로고 높이의 25% 이상이 되도록 아래에서 다시 제한)
    target_w = tile_px * (0.56 if m_only else 0.80)
    scale = target_w / w
    # 여백 조건: 가로·세로 모두 (타일 - 로고) / 2 >= 0.25 * 로고 높이
    max_scale = min(tile_px / (w + 0.5 * h), tile_px / (h * 1.5))
    scale = min(scale, max_scale)
    lw, lh = w * scale, h * scale
    tx = off + (tile_px - lw) / 2 - x0 * scale
    ty = off + (tile_px - lh) / 2 - y0 * scale
    r = tile_px * radius
    clip = ""
    clip_ref = ""
    if m_only:
        pts = " ".join(f"{x},{y}" for x, y in M_CLIP)
        clip = f'<clipPath id="m"><polygon points="{pts}"/></clipPath>'
        clip_ref = ' clip-path="url(#m)"'
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{size}" height="{size}" viewBox="0 0 {size} {size}">'
            f'<defs>{clip}</defs>'
            f'<rect x="{off}" y="{off}" width="{tile_px}" height="{tile_px}" rx="{r}" ry="{r}" fill="{BG}"/>'
            f'<g transform="translate({tx} {ty}) scale({scale})">'
            f'<path fill="{INK}" fill-rule="evenodd" d="{d}"{clip_ref}/></g></svg>')


# macOS 메뉴 막대 아이콘: 20×16pt 캔버스, M 높이 14pt (1x·2x 두 장)
TRAY_W, TRAY_H, TRAY_GLYPH_H = 20, 16, 14


def tray_svg(scale):
    d = logo_path_d()
    x0, y0, x1, y1 = M_BOX
    w, h = x1 - x0, y1 - y0
    cw, ch = TRAY_W * scale, TRAY_H * scale
    s = TRAY_GLYPH_H * scale / h
    tx = (cw - w * s) / 2 - x0 * s
    ty = (ch - h * s) / 2 - y0 * s
    pts = " ".join(f"{x},{y}" for x, y in M_CLIP)
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{cw}" height="{ch}" viewBox="0 0 {cw} {ch}">'
            f'<defs><clipPath id="m"><polygon points="{pts}"/></clipPath></defs>'
            f'<g transform="translate({tx} {ty}) scale({s})">'
            f'<path fill="#000000" fill-rule="evenodd" d="{d}" clip-path="url(#m)"/></g></svg>')


def render_rect(svg, png, w, h):
    with tempfile.NamedTemporaryFile("w", suffix=".svg", delete=False) as f:
        f.write(svg)
        tmp = f.name
    subprocess.run(["rsvg-convert", "-w", str(w), "-h", str(h), tmp, "-o", png], check=True)
    os.unlink(tmp)


def render(svg, png, size):
    with tempfile.NamedTemporaryFile("w", suffix=".svg", delete=False) as f:
        f.write(svg)
        tmp = f.name
    subprocess.run(["rsvg-convert", "-w", str(size), "-h", str(size), tmp, "-o", png], check=True)
    os.unlink(tmp)


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    mac_dir = os.path.join(OUT_DIR, "mac")
    win_dir = os.path.join(OUT_DIR, "win")
    os.makedirs(mac_dir, exist_ok=True)
    os.makedirs(win_dir, exist_ok=True)

    # macOS: 크기별 PNG → iconset → .icns
    mac_png = {}
    for s in MAC_SIZES:
        p = os.path.join(mac_dir, f"meon_mac_{s}.png")
        render(icon_svg(s, MAC_TILE, MAC_RADIUS, s <= M_ONLY_MAX_MAC), p, s)
        mac_png[s] = p
    iconset = os.path.join(tempfile.mkdtemp(), "MEON.iconset")
    os.makedirs(iconset)
    for base in [16, 32, 128, 256, 512]:
        shutil.copy(mac_png[base], os.path.join(iconset, f"icon_{base}x{base}.png"))
        shutil.copy(mac_png[base * 2], os.path.join(iconset, f"icon_{base}x{base}@2x.png"))
    subprocess.run(["iconutil", "-c", "icns", iconset, "-o", os.path.join(OUT_DIR, "MEON.icns")], check=True)
    shutil.rmtree(os.path.dirname(iconset))

    # Windows: 크기별 PNG → .ico
    win_png = []
    for s in WIN_SIZES:
        p = os.path.join(win_dir, f"meon_win_{s}.png")
        render(icon_svg(s, WIN_TILE, WIN_RADIUS, s <= M_ONLY_MAX_WIN), p, s)
        win_png.append(p)
    subprocess.run(["magick", *win_png, os.path.join(OUT_DIR, "MEON.ico")], check=True)

    # 설치 파일 아이콘은 앱 아이콘과 같은 그림을 쓴다
    shutil.copy(os.path.join(OUT_DIR, "MEON.icns"), os.path.join(OUT_DIR, "MEON-installer.icns"))
    shutil.copy(os.path.join(OUT_DIR, "MEON.ico"), os.path.join(OUT_DIR, "MEON-installer.ico"))

    # macOS 메뉴 막대 아이콘
    tray_dir = os.path.join(OUT_DIR, "tray")
    os.makedirs(tray_dir, exist_ok=True)
    for scale, suffix in [(1, ""), (2, "_2x")]:
        render_rect(tray_svg(scale), os.path.join(tray_dir, f"meon_tray_mac{suffix}.png"), TRAY_W * scale, TRAY_H * scale)

    print("written to", OUT_DIR)


if __name__ == "__main__":
    main()
