"""소개 페이지 파일 만들기: capture.sh 의 원본 렌더링(raw/)에서 PNG·WebP·크롭·영상을 만든다.

사용: python3 -I scripts/landing/postprocess.py <raw 폴더> <출력 폴더> [daw]
  daw  tab-daw 만 만든다 (raw/ 에 jam-plugin.png 만 있어도 된다). 다른 파일은 건드리지 않는다
  raw/  jam · rooms · rooms-before · friends · request · joined · file-empty · file · earguard · jam-plugin (.png + .layout.json)
        · frames/0000.png… · frames/pings.json
  출력  hero.mp4 (1512×982) · hero.webm (1512×982) · hero@2x.mp4 (3024×1964 원본) · hero-poster.png
        tab-jam · tab-rooms · tab-friends · tab-daw · feat-mix · feat-connection · feat-chat · feat-rooms · feat-friends
        feat-request-host · feat-request-card · feat-request-card-before · feat-file-empty · feat-file-playing
        feat-earguard · feat-member-joined
        이미지마다 .png (2배 원본, 무손실) + @2x.webp (원본 크기) + @1x.webp (절반), WebP 품질 90
필요: Pillow, ffmpeg (libx264, libvpx-vp9). 늘리지 않는다 (1배·웹 영상은 2배 렌더링을 줄인 것).
핑 검사: 모든 배치(*.layout.json)와 영상 프레임(frames/pings.json)의 핑·예상 지연 글자가 2~4 ms 인지 본다. 아니면 실패.
"""
import glob
import json
import os
import re
import shutil
import subprocess
import sys

from PIL import Image, ImageDraw

RAW, OUT = sys.argv[1], sys.argv[2]
ONLY = sys.argv[3] if len(sys.argv) > 3 else ""
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


def crop_box(lay, box, margin=MARGIN, obstacles=OBSTACLES, behind=(), within=None):
    """논리 좌표 상자 + 사방 여백 → 2배 이미지 좌표.
    여백은 최대 margin 이고, 이웃 카드·이웃 행·헤더에 닿기 직전에서 멈춘다 (잘린 조각이 들어오지 않게). 화면 밖은 자른다.
    behind: 상자 뒤에 깔린 요소 (팝오버 아래 카드 등). 상자 밖으로 이어지는 쪽은 여백 0 (뒤의 글자가 잘려 들어오지 않게).
    within: (x0, y0, x1, y1) 이 안으로만 자른다 (설정 창 내용 영역 등).
    좌표가 정수(논리) × 2 라 1배는 정확히 절반이다."""
    bx0, by0, bx1, by1 = box["x"], box["y"], box["x"] + box["w"], box["y"] + box["h"]
    obs = [n for n in lay["nodes"] if n["visible"] and n["type"].endswith(obstacles)
           and not (overlaps(n["x"], n["x"] + n["w"], bx0, bx1) and overlaps(n["y"], n["y"] + n["h"], by0, by1))]
    obs.append({"x": 0, "y": 0, "w": lay["width"], "h": HEADER_H})
    left = right = top = bottom = margin
    for b in behind:
        if b["x"] < bx0: left = 0
        if b["x"] + b["w"] > bx1: right = 0
        if b["y"] < by0: top = 0
        if b["y"] + b["h"] > by1: bottom = 0
    if within is not None:
        left = min(left, bx0 - within[0]); right = min(right, within[2] - bx1)
        top = min(top, by0 - within[1]); bottom = min(bottom, within[3] - by1)
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


CANVAS = (3024, 1964)        # 전체 화면 이미지와 같은 크기 (2배)
CANVAS_BG = (0xFC, 0xFC, 0xFC)
WINDOW_RADIUS = 20           # 창 모서리 (2배 픽셀) = 앱의 radiusWindow 10 (MeonTheme.h) × 2
WINDOW_BORDER = 2            # 창 테두리 두께 (2배 픽셀, 논리 1). 창 바깥에 두른다 (창 내용을 가리지 않게)
WINDOW_BORDER_COLOR = (0xE0, 0xE0, 0xE0)


def framed(win):
    """플러그인 창을 CANVAS 가운데에 놓는다. 모서리는 둥글게, 바깥에 얇은 테두리, 나머지는 CANVAS_BG.
    둥근 모서리는 4배로 그려 줄여서 계단이 없게 한다. 위치는 짝수라 1배로 줄여도 반 픽셀이 생기지 않는다."""
    win = win.convert("RGB")
    b, r, ss = WINDOW_BORDER, WINDOW_RADIUS, 4
    ow, oh = win.width + 2 * b, win.height + 2 * b
    ox, oy = (CANVAS[0] - ow) // 2 // 2 * 2, (CANVAS[1] - oh) // 2 // 2 * 2

    def mask(size, inset, radius):
        m = Image.new("L", (size[0] * ss, size[1] * ss), 0)
        ImageDraw.Draw(m).rounded_rectangle((inset * ss, inset * ss, (size[0] - inset) * ss - 1, (size[1] - inset) * ss - 1),
                                            radius * ss, fill=255)
        return m.resize(size, Image.BOX)

    out = Image.new("RGB", CANVAS, CANVAS_BG)
    out.paste(Image.new("RGB", (ow, oh), WINDOW_BORDER_COLOR), (ox, oy), mask((ow, oh), 0, r + b))
    inner = mask((ow, oh), b, r).crop((b, b, b + win.width, b + win.height))
    out.paste(win, (ox + b, oy + b), inner)
    return out


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


def save_daw():
    save(framed(Image.open(os.path.join(RAW, "jam-plugin.png"))), "tab-daw")   # 창만 그린다 (DAW 화면 없음)


if ONLY == "daw":
    save_daw()
    print("끝 (tab-daw 만)")
    sys.exit(0)

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
    subprocess.run(["ffmpeg", *common, "-c:v", "libx264", "-preset", "slow", "-crf", "12", "-pix_fmt", "yuv420p",
                    "-profile:v", "high", "-movflags", "+faststart", os.path.join(OUT, "hero@2x.mp4")], check=True)
    scale = ["-vf", "scale=1512:982:flags=lanczos"]
    subprocess.run(["ffmpeg", *common, *scale, "-c:v", "libx264", "-preset", "slow", "-crf", "16", "-pix_fmt", "yuv420p",
                    "-profile:v", "high", "-movflags", "+faststart", os.path.join(OUT, "hero.mp4")], check=True)
    subprocess.run(["ffmpeg", *common, *scale, "-c:v", "libvpx-vp9", "-b:v", "0", "-crf", "26", "-row-mt", "1",
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
save_daw()

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

# ── 새 기능 섹션 크롭 (입장 요청 · 파일 플레이어 · 귀 테러 방지 · 새 멤버) ──────────
def raw_scene(name):
    return Image.open(os.path.join(RAW, name + ".png")), layout(name)


def node_with(lay, key, value, type_suffix=""):
    hits = [n for n in lay["nodes"] if n["visible"] and n.get(key) == value and n["type"].endswith(type_suffix)]
    if len(hits) != 1:
        sys.exit(f"{key}={value!r}: {len(hits)}개 (1개여야 함)")
    return hits[0]


def same_rect(a, b):
    return all(a[k] == b[k] for k in ("x", "y", "w", "h"))


# 입장 요청 목록 (방장): 헤더의 '입장 요청' 버튼(알림 점) + 열린 목록. 목록 뒤의 채팅 카드가 아래로 이어지므로 아래 여백 0
req_img, req_lay = raw_scene("request")
req_btn = node_with(req_lay, "tooltip", "입장 요청", "ui::IconButton")
pop = nodes(req_lay, "ui::Popover")
assert len(pop) == 1, "입장 요청 목록이 하나 열려 있어야 함"
req_rows = nodes(req_lay, "JoinRequestList::Row")
assert len(req_rows) == 1, f"입장 요청 1건이어야 함 ({len(req_rows)})"
req_box = union([req_btn, pop[0]])
under = [c for c in nodes(req_lay, "ui::Card")
         if overlaps(c["x"], c["x"] + c["w"], req_box["x"], req_box["x"] + req_box["w"])
         and overlaps(c["y"], c["y"] + c["h"], req_box["y"], req_box["y"] + req_box["h"])]
save(req_img.crop(crop_box(req_lay, req_box, behind=under)), "feat-request-host")

# 공개 방 카드 하나: '요청 보냄' (rooms) / '요청 보내기' (rooms-before). 두 장면의 카드 위치가 같아야 한다
card_box = crop_box(rooms_lay, req_card)
save(rooms_img.crop(card_box), "feat-request-card")
before_img, before_lay = raw_scene("rooms-before")
same = [c for c in nodes(before_lay, "HomeScreen::RoomCard") if same_rect(c, req_card)]
assert len(same) == 1, "rooms-before 에 같은 위치의 카드가 없음"
assert any(b["visible"] and b.get("text") == "요청 보내기" and inside(b, same[0]) for b in before_lay["nodes"]), "요청 보내기 버튼이 없음"
assert crop_box(before_lay, same[0]) == card_box, "두 장면의 크롭 위치가 다름"
save(before_img.crop(card_box), "feat-request-card-before")

# 파일 플레이어: 빈 상태 / 재생 중 (두 장면 모두 오른쪽 패널을 파일 이름이 다 보이는 폭으로 넓힘)
for scene, name in (("file-empty", "feat-file-empty"), ("file", "feat-file-playing")):
    img, lay = raw_scene(scene)
    fp = nodes(lay, "JamScreen::FilePlayer")
    assert len(fp) == 1
    save(img.crop(crop_box(lay, fp[0])), name)

# 귀 테러 방지: 설정 › 합주 의 마지막 항목 (보통 = 켜짐). 설정 창 오른쪽 내용 영역(목록 + 좌우 여백 18) 안에서, 이웃 항목 앞까지
ear_img, ear_lay = raw_scene("earguard")
modal = nodes(ear_lay, "ui::SettingsModal")
assert len(modal) == 1
srows = sorted(nodes(ear_lay, "SettingsModal::Row"), key=lambda n: n["y"])
ear = srows[-1]
assert any(n["visible"] and n["type"].endswith("ui::Choice") and inside(n, ear) for n in ear_lay["nodes"]), "귀 테러 방지 항목(선택 버튼)이 아님"
vp = [n for n in ear_lay["nodes"] if n["visible"] and n["type"] == "juce::Viewport" and inside(ear, n) and inside(n, modal[0])][0]
PAD = 18
ear_within = (vp["x"] - PAD, vp["y"], vp["x"] + vp["w"] + PAD, vp["y"] + vp["h"])
save(ear_img.crop(crop_box(ear_lay, ear, obstacles=("SettingsModal::Row",), within=ear_within)), "feat-earguard")

# 새 멤버: 입장 요청을 수락해서 지우가 멤버 목록 맨 아래에 들어온 상태. 멤버 목록 전체
join_img, join_lay = raw_scene("joined")
jrows = sorted(nodes(join_lay, "JamScreen::MemberRow"), key=lambda n: n["y"])
assert len(jrows) == 7, f"멤버 7명이어야 함 ({len(jrows)})"
save(join_img.crop(crop_box(join_lay, union(jrows))), "feat-member-joined")

# ── 핑 검사: 화면에 보이는 핑·예상 지연은 모두 2~4 ms ('나' 는 내 행) ─────────────
PING_OK = re.compile(r"^[234]ms$")
bad = []
checked = 0
for path in sorted(glob.glob(os.path.join(RAW, "*.layout.json"))):
    with open(path, encoding="utf-8") as f:
        lay = json.load(f)
    for n in lay["nodes"]:
        if n["visible"] and n["type"].endswith("ui::PingBadge"):
            checked += 1
            t = n.get("text", "")
            if t != "나" and not PING_OK.match(t):
                bad.append(f"{os.path.basename(path)}: {t!r} @ {n['x']},{n['y']}")
pings_path = os.path.join(RAW, "frames", "pings.json")
if os.path.exists(pings_path):
    with open(pings_path, encoding="utf-8") as f:
        per_frame = json.load(f)
    assert len(per_frame) == len(frames), f"pings.json {len(per_frame)} 프레임, 영상 {len(frames)} 프레임"
    for i, texts in enumerate(per_frame):
        for t in texts:
            checked += 1
            if t != "나" and not PING_OK.match(t):
                bad.append(f"frame {i}: {t!r}")
elif frames:
    sys.exit("영상 프레임의 핑 기록(frames/pings.json)이 없음")
if bad:
    sys.exit("핑이 2~4 ms 가 아님:\n  " + "\n  ".join(bad[:50]))
print(f"핑 검사: {checked}개 모두 2~4 ms")

with open(os.path.join(OUT, "_sizes.json"), "w", encoding="utf-8") as f:
    json.dump({n: list(s) for n, s in saved}, f)
print("끝")
