"""--showcase 캡처(2배)에서 채팅 카드만 자른다. 사용: python3 -I crop_card.py <전체.png> <전체.layout.json> <저장.png>"""
import json
import sys

from PIL import Image

src, lay_path, dst = sys.argv[1:4]
with open(lay_path, encoding="utf-8") as f:
    lay = json.load(f)
nodes = lay["nodes"]
view = next(n for n in nodes if n["visible"] and n["type"].endswith("JamScreen::ChatView"))
card, i = None, view["parent"]
while i >= 0:
    if nodes[i]["type"].endswith("ui::Card"):
        card = nodes[i]
        break
    i = nodes[i]["parent"]
card = card or view
s = lay["scale"]
box = tuple(int(v * s) for v in (card["x"], card["y"], card["x"] + card["w"], card["y"] + card["h"]))
Image.open(src).crop(box).convert("RGB").save(dst, optimize=True)
print(dst, box)
