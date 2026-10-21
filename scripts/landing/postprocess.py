"""소개 페이지 파일 만들기: capture.sh 의 원본 렌더링(raw/)에서 PNG·WebP·크롭·영상을 만든다.

사용: python3 -I scripts/landing/postprocess.py <raw 폴더> <출력 폴더>
  raw/  jam.png · rooms.png · friends.png · jam-plugin.png · *.layout.json · frames/0000.png…
  출력  hero.mp4 (1512×982) · hero.webm (1512×982) · hero@2x.mp4 (3024×1964 원본) · hero-poster.png
        tab-jam · tab-rooms · tab-friends · tab-daw · feat-mix · feat-connection · feat-chat · feat-rooms · feat-friends
        이미지마다 .png (2배 원본, 무손실) + @2x.webp (원본 크기) + @1x.webp (절반), WebP 품질 90
필요: Pillow, ffmpeg (libx264, libvpx-vp9). 늘리지 않는다 (1배·웹 영상은 2배 렌더링을 줄인 것).
"""
import json
import os
import shutil
import subprocess
import sys

from PIL import Image

RAW, OUT = sys.argv[1], sys.argv[2]
MARGIN = 24          # 크롭 여백 (논리 픽셀)
FPS = 30
FADE_FRAMES = 15     # 마지막 0.5초: 채팅 칸만 첫 장면으로 페이드
os.makedirs(OUT, exist_ok=True)


def layout(name):
    with open(os.path.join(RAW, name + ".layout.json"), encoding="utf-8") as f:
        return json.load(f)


def nodes(lay, type_suffix):
    return [n for n in lay["nodes"] if n["visible"] and n["type"].endswith(type_suffix)]


def ancestor(lay, node, type_suffix):
    i = node["parent"]
    while i >= 0:
        n = lay["nodes"][i]
        if n["type"].endswith(type_suffix):
            return n
        i = n["parent"]
    return None


def inside(n, box):
    return n["x"] >= box["x"] and n["y"] >= box["y"] and n["x"] + n["w"] <= box["x"] + box["w"] and n["y"] + n["h"] <= box["y"] + box["h"]


def union(ns):
    x0 = min(n["x"] for n in ns); y0 = min(n["y"] for n in ns)
    x1 = max(n["x"] + n["w"] for n in ns); y1 = max(n["y"] + n["h"] for n in ns)
    return {"x": x0, "y": y0, "w": x1 - x0, "h": y1 - y0}


HEADER_H = 59                       # 화면 맨 위 헤더 (로고·버튼) + 아래 선
OBSTACLES = ("ui::Card", "JamScreen::FilePlayer", "ui::Toast", "HomeScreen::RoomCard", "JamScreen::MemberRow")


def overlaps(a0, a1, b0, b1):
    return a0 < b1 and b0 < a1


def crop_box(lay, box, margin=MARGIN):
    """논리 좌표 상자 + 사방 여백 → 2배 이미지 좌표.
    여백은 최대 margin 이고, 이웃 카드·이웃 행·헤더에 닿기 직전에서 멈춘다 (잘린 조각이 들어오지 않게). 화면 밖은 자른다.
    좌표가 정수(논리) × 2 라 1배는 정확히 절반이다."""
    bx0, by0, bx1, by1 = box["x"], box["y"], box["x"] + box["w"], box["y"] + box["h"]
    obs = [n for n in lay["nodes"] if n["visible"] and n["type"].endswith(OBSTACLES)
           and not (overlaps(n["x"], n["x"] + n["w"], bx0, bx1) and overlaps(n["y"], n["y"] + n["h"], by0, by1))]
    obs.append({"x": 0, "y": 0, "w": lay["width"], "h": HEADER_H})
    left = right = top = bottom = margin
    for o in obs:
        ox0, oy0, ox1, oy1 = o["x"], o["y"], o["x"] + o["w"], o["y"] + o["h"]
        if overlaps(oy0, oy1, by0, by1):
            if ox1 <= bx0: left = min(left, bx0 - ox1 - 1)
            if ox0 >= bx1: right = min(right, ox0 - bx1 - 1)
        if overlaps(ox0, ox1, bx0 - left, bx1 + right):
            if oy1 <= by0: top = min(top, by0 - oy1 - 1)
            if oy0 >= by1: bottom = min(bottom, oy0 - by1 - 1)
    left, right, top, bottom = (max(0, v) for v in (left, right, top, bottom))
    s = lay["scale"]
    x0 = max(0, bx0 - left); y0 = max(0, by0 - top)
    x1 = min(lay["width"], bx1 + right); y1 = min(lay["height"], by1 + bottom)
    return (int(x0 * s), int(y0 * s), int(x1 * s), int(y1 * s))


saved = []


def save(img, name):
    """원본 PNG(무손실) + WebP 2배·1배 (품질 90)"""
    img = img.convert("RGB")
    png = os.path.join(OUT, name + ".png")
    img.save(png, optimize=True)
    img.save(os.path.join(OUT, name + "@2x.webp"), quality=90, method=6)
    half = img.resize((img.width // 2, img.height // 2), Image.LANCZOS)
    half.save(os.path.join(OUT, name + "@1x.webp"), quality=90, method=6)
    saved.append((name, img.size))
    print(f"{name}: {img.width}x{img.height}")


# ── 영상 ─────────────────────────────────────────────────────────────
frames_dir = os.path.join(RAW, "frames")
frames = sorted(f for f in os.listdir(frames_dir) if f.endswith(".png")) if os.path.isdir(frames_dir) else []
if frames:
    jam = layout("jam")   # 영상과 같은 배치 (frames 를 찍은 실행이 jam.layout.json 을 다시 쓴다)
    chat_view = nodes(jam, "JamScreen::ChatView")[0]
    chat_card = ancestor(jam, chat_view, "ui::Card")
    s = jam["scale"]
    cb = (chat_card["x"] * s, chat_card["y"] * s, (chat_card["x"] + chat_card["w"]) * s, (chat_card["y"] + chat_card["h"]) * s)
    cb = tuple(int(v) for v in cb)

    work = os.path.join(OUT, "_frames")
    os.makedirs(work, exist_ok=True)
    first = Image.open(os.path.join(frames_dir, frames[0])).convert("RGB")
    first_chat = first.crop(cb)
    n = len(frames)
    for i, f in enumerate(frames):
        src = os.path.join(frames_dir, f)
        dst = os.path.join(work, f"{i:04d}.png")
        k = i - (n - FADE_FRAMES - 1)          # 1..FADE_FRAMES
        if k >= 1:
            im = Image.open(src).convert("RGB")
            a = k / (FADE_FRAMES + 1)
            im.paste(Image.blend(im.crop(cb), first_chat, a), cb[:2])
            im.save(dst, compress_level=1)
        else:
            shutil.copyfile(src, dst)

    pattern = os.path.join(work, "%04d.png")
    common = ["-y", "-hide_banner", "-loglevel", "error", "-framerate", str(FPS), "-i", pattern, "-an"]
    subprocess.run(["ffmpeg", *common, "-c:v", "libx264", "-preset", "slow", "-crf", "14", "-pix_fmt", "yuv420p",
                    "-profile:v", "high", "-movflags", "+faststart", os.path.join(OUT, "hero@2x.mp4")], check=True)
    scale = ["-vf", "scale=1512:982:flags=lanczos"]
    subprocess.run(["ffmpeg", *common, *scale, "-c:v", "libx264", "-preset", "slow", "-crf", "20", "-pix_fmt", "yuv420p",
                    "-profile:v", "high", "-movflags", "+faststart", os.path.join(OUT, "hero.mp4")], check=True)
    subprocess.run(["ffmpeg", *common, *scale, "-c:v", "libvpx-vp9", "-b:v", "0", "-crf", "32", "-row-mt", "1",
                    "-deadline", "good", "-cpu-used", "2", "-pix_fmt", "yuv420p", os.path.join(OUT, "hero.webm")], check=True)
    shutil.rmtree(work)
    save(first, "hero-poster")
    save(first, "tab-jam")          # 영상의 정지 화면 = 첫 프레임
    jam_still = first
    jam_lay = jam
else:
    jam_still = Image.open(os.path.join(RAW, "jam.png"))
    jam_lay = layout("jam")
    save(jam_still, "tab-jam")

# ── 탭 갤러리 ─────────────────────────────────────────────────────────
rooms_img, rooms_lay = Image.open(os.path.join(RAW, "rooms.png")), layout("rooms")
friends_img, friends_lay = Image.open(os.path.join(RAW, "friends.png")), layout("friends")
save(rooms_img, "tab-rooms")
save(friends_img, "tab-friends")
save(Image.open(os.path.join(RAW, "jam-plugin.png")), "tab-daw")

# ── 기능 섹션 크롭 ───────────────────────────────────────────────────
rows = sorted(nodes(jam_lay, "JamScreen::MemberRow"), key=lambda n: n["y"])

# 볼륨·패닝: 아래쪽 4명(도윤·하은·지호·유나) 행의 왼쪽 (이름·음소거·볼륨·패닝·초기화·관리). 위는 행 경계에서 자른다
mix_rows = rows[3:7]
controls = [n for n in jam_lay["nodes"] if n["visible"] and any(inside(n, r) for r in mix_rows)
            and (n["type"].endswith(("ui::Button", "ui::DbSlider", "ui::PanKnob", "ui::Avatar")))]
box = union(mix_rows)
box["w"] = max(n["x"] + n["w"] for n in controls) - box["x"]
save(jam_still.crop(crop_box(jam_lay, box)), "feat-mix")

# 연결 상태·지연: 7명 행 전체 (모두 연결됨 · 핑 배지 · 미터)
save(jam_still.crop(crop_box(jam_lay, union(rows))), "feat-connection")

# 방 채팅: 채팅 패널 전체 (영상 첫 장면: 입장 안내 줄 · 상대 메시지 · 내 말풍선)
cv = nodes(jam_lay, "JamScreen::ChatView")[0]
save(jam_still.crop(crop_box(jam_lay, ancestor(jam_lay, cv, "ui::Card"))), "feat-chat")

# 공개 방 + 입장 요청: "요청 보냄" 카드가 있는 열과 오른쪽 열의 위 두 장씩
cards = nodes(rooms_lay, "HomeScreen::RoomCard")
req = [b for b in rooms_lay["nodes"] if b["visible"] and b.get("text") == "요청 보냄"][0]
req_card = [c for c in cards if inside(req, c)][0]
cols = sorted({c["x"] for c in cards})
ci = cols.index(req_card["x"])
pick_cols = cols[ci:ci + 2] if ci + 1 < len(cols) else cols[ci - 1:ci + 1]
pick = []
for cx in pick_cols:
    pick += sorted([c for c in cards if c["x"] == cx], key=lambda c: c["y"])[:2]
save(rooms_img.crop(crop_box(rooms_lay, union(pick))), "feat-rooms")

# 친구 목록 + 초대: 합주 화면의 친구 패널 (온라인 친구에 '초대' 버튼)
fl = nodes(jam_lay, "JamScreen::FriendList")[0]
save(jam_still.crop(crop_box(jam_lay, ancestor(jam_lay, fl, "ui::Card"))), "feat-friends")

with open(os.path.join(OUT, "_sizes.json"), "w", encoding="utf-8") as f:
    json.dump({n: list(s) for n, s in saved}, f)
print("끝")
