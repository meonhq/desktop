"""ThreadSanitizer 경고 묶기. 사용: python3 -I scripts/stall/tsan_summary.py <tsan 로그 파일 또는 폴더> ...

run-jam.sh 를 TSan 빌드로 돌리면 out-이름/tsan-a.<pid> 같은 파일이 생긴다.
경고마다 두 스택의 맨 위 우리 코드(Source/, deps/aoo) 줄을 뽑아, 같은 자리끼리 묶어 센다.
--e10 을 주면 E10 관련 후보(관문·관문 안 오디오 구간·관문을 거는 쓰는 쪽)를 따로 표시한다.
"""
import os
import re
import sys
from collections import OrderedDict

FRAME = re.compile(r"^\s+#(\d+) (\S+) (.*?) (/\S+?):(\d+)(?::\d+)?")
HEAD = re.compile(r"^WARNING: ThreadSanitizer: (.+?) \(pid=\d+\)")
ACCESS = re.compile(r"^\s{2}(\S.*?) (?:by|of) (thread T\d+|main thread)")
OURS = ("/Source/", "/deps/aoo/")
# E10 관련: 관문 자체, processBlock 의 관문 구간, 관문을 거는 쓰는 쪽 함수
E10_MARKS = ("RtGate", "ScopedGatedWrite")
E10_WRITERS = ("initializeAoo", "cleanupAoo", "startAooServer", "stopAooServer", "removeAllRemotePeers", "removeRemotePeer(",
               "doAddRemotePeerIfNecessary", "removeAllRemotePeersWithEndpoint", "doRemoveRemotePeerIfNecessary",
               "setupSourceFormatsForAll", "handleSinkEvents")


def files(args):
    for a in args:
        if os.path.isdir(a):
            for n in sorted(os.listdir(a)):
                if n.startswith("tsan-"):
                    yield os.path.join(a, n)
        elif os.path.exists(a):
            yield a


def short(path):
    for k in OURS:
        i = path.find(k)
        if i >= 0:
            return path[i + 1:]
    i = path.find("/modules/")
    return path[i + 1:] if i >= 0 else os.path.basename(path)


def parse(text):
    for block in text.split("==================\n"):
        h = HEAD.search(block)
        if not h:
            continue
        stacks, cur = [], None
        for line in block.splitlines():
            a = ACCESS.match(line)
            if a and ("Previous" in a.group(1) or "Write" in a.group(1) or "Read" in a.group(1) or "Atomic" in a.group(1)
                      or "Location" in a.group(1) or "Mutex" in a.group(1)):
                cur = {"what": a.group(1), "thread": a.group(2), "frames": []}
                stacks.append(cur)
                continue
            f = FRAME.match(line)
            if f and cur is not None:
                cur["frames"].append((f.group(3), f.group(4), int(f.group(5))))
        yield h.group(1), stacks[:2], block


def key_frame(stack):
    for fn, path, ln in stack["frames"]:
        if any(k in path for k in OURS):
            return f"{fn.split('(')[0]} ({short(path)}:{ln})"
    if stack["frames"]:
        fn, path, ln = stack["frames"][0]
        return f"{fn.split('(')[0]} ({short(path)}:{ln})"
    return "?"


def is_e10(stacks, block):
    if any(m in block for m in E10_MARKS):
        return True
    names = [fn for s in stacks for fn, _, _ in s["frames"]]
    in_block = any("SonobusAudioProcessor::processBlock" in n for n in names)
    in_writer = any(any(w in n for w in E10_WRITERS) for n in names)
    return in_block and in_writer


def main(argv):
    want_e10 = "--e10" in argv
    groups = OrderedDict()
    total = 0
    for p in files([a for a in argv if a != "--e10"]):
        with open(p, encoding="utf-8", errors="replace") as f:
            text = f.read()
        for kind, stacks, block in parse(text):
            total += 1
            k = (kind,) + tuple(sorted(key_frame(s) for s in stacks))
            g = groups.setdefault(k, {"n": 0, "e10": False, "files": set(), "threads": set()})
            g["n"] += 1
            g["files"].add(os.path.basename(p))
            g["threads"].update(s["thread"] for s in stacks)
            if want_e10 and is_e10(stacks, block):
                g["e10"] = True
    print(f"경고 {total}건, 자리 {len(groups)}곳")
    print("| # | 종류 | 한쪽 | 다른 쪽 | 건수 | E10 후보 |")
    print("|---|---|---|---|---|---|")
    for i, (k, g) in enumerate(sorted(groups.items(), key=lambda kv: -kv[1]["n"]), 1):
        a = k[1] if len(k) > 1 else ""
        b = k[2] if len(k) > 2 else ""
        print(f"| {i} | {k[0]} | `{a}` | `{b}` | {g['n']} | {'예' if g['e10'] else ''} |")


if __name__ == "__main__":
    main(sys.argv[1:])
