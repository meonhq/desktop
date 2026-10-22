"""ThreadSanitizer 경고 묶기. 사용: python3 -I scripts/stall/tsan_summary.py [--e10] <tsan 로그 파일 또는 폴더> ...

run-jam.sh 를 TSan 빌드로 돌리면 out-이름/tsan-a.<pid> 같은 파일이 생긴다.
경고마다 두 스택의 맨 위 우리 코드(Source/, deps/aoo) 줄을 뽑아, 같은 자리끼리 묶어 센다.

--e10: E10(오디오 스레드 관문)과 관련된 경고를 따로 표시한다.
  관련 = 한쪽이 관문 안 오디오 구간(processBlock 의 peersGate 블록)이고 다른 쪽이 관문을 건 쓰는 쪽 구간
         (ScopedGatedWrite·RtGate::Writer 가 살아 있는 블록), 또는 스택에 RtGate 가 있는 경고.
  구간은 Source/SonobusPluginProcessor.cpp 를 읽어 줄 번호로 정한다 (그 빌드와 같은 소스여야 한다).
"""
import os
import re
import sys
from collections import OrderedDict

HEAD = re.compile(r"^WARNING: ThreadSanitizer: (.+?) \(pid=\d+\)")
ACCESS = re.compile(r"^\s{2}(Previous )?(read|write|atomic read|atomic write|Read|Write|Atomic read|Atomic write)\b.*? by (thread T\d+|main thread)", re.I)
FRAME = re.compile(r"^\s+#\d+ (.+) (/\S+):(\d+)(?::\d+)? \(")
OURS = ("/Source/", "/deps/aoo/")
ENGINE = "Source/SonobusPluginProcessor.cpp"


def short_path(path):
    for k in OURS:
        i = path.find(k)
        if i >= 0:
            return path[i + 1:]
    i = path.find("/modules/")
    return path[i + 1:] if i >= 0 else os.path.basename(path)


def short_func(f):
    """인자 목록을 뗀 함수 이름 (마지막 괄호 묶음과 그 뒤 const 를 뗀다)"""
    f = re.sub(r"\s+const$", "", f.strip())
    if f.endswith(")"):
        depth = 0
        for i in range(len(f) - 1, -1, -1):
            if f[i] == ")":
                depth += 1
            elif f[i] == "(":
                depth -= 1
                if depth == 0:
                    return f[:i]
    return f


def block_end(lines, start):
    """start 줄이 들어 있는 블록이 닫히는 줄 (1부터)"""
    depth = 0
    for i in range(start - 1, len(lines)):
        for ch in lines[i]:
            if ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth < 0:
                    return i + 1
    return len(lines)


def e10_ranges(src):
    with open(src, encoding="utf-8", errors="replace") as f:
        lines = f.read().split("\n")
    audio, writers = [], []
    for n, line in enumerate(lines, 1):
        if "peersGate.entered()" in line:
            audio.append((n, block_end(lines, n + 2)))   # if (...) 다음 줄의 { 부터
        if "ScopedGatedWrite" in line and "class" not in line and "//" not in line.split("ScopedGatedWrite")[0]:
            writers.append((n, block_end(lines, n)))
        m = re.search(r"RtGate::Writer (\w+) \(", line)
        if m:
            end = block_end(lines, n)
            for k in range(n, end):
                if m.group(1) + ".release()" in lines[k]:
                    end = k + 1
                    break
            writers.append((n, end))
    return audio, writers


def files(args):
    for a in args:
        if os.path.isdir(a):
            for n in sorted(os.listdir(a)):
                if n.startswith("tsan-"):
                    yield os.path.join(a, n)
        elif os.path.exists(a):
            yield a


def parse(text):
    for block in text.split("==================\n"):
        h = HEAD.search(block)
        if not h:
            continue
        stacks, cur = [], None
        for line in block.splitlines():
            if ACCESS.match(line):
                cur = {"head": line.strip(), "frames": []}
                stacks.append(cur)
                continue
            if line.startswith("  ") and not line.startswith("    ") and cur is not None:
                cur = None   # Location / Mutex / Thread 설명 등은 스택으로 보지 않는다
                continue
            f = FRAME.match(line)
            if f and cur is not None:
                cur["frames"].append((short_func(f.group(1)), short_path(f.group(2)), int(f.group(3))))
        yield h.group(1), stacks[:2], block


def key_frame(stack):
    for fn, path, ln in stack["frames"]:
        if path.startswith(("Source/", "deps/aoo/")):
            return f"{fn} ({path}:{ln})"
    if stack["frames"]:
        fn, path, ln = stack["frames"][0]
        return f"{fn} ({path}:{ln})"
    return "?"


def in_ranges(stack, ranges):
    return any(path == ENGINE and any(a <= ln <= b for a, b in ranges) for _, path, ln in stack["frames"])


def main(argv):
    want_e10 = "--e10" in argv
    args = [a for a in argv if a != "--e10"]
    audio = writers = []
    if want_e10:
        src = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ENGINE)
        audio, writers = e10_ranges(src)
        print(f"관문 안 오디오 구간 {audio}, 쓰는 쪽 구간 {writers}")
    groups = OrderedDict()
    total = 0
    for p in files(args):
        with open(p, encoding="utf-8", errors="replace") as f:
            text = f.read()
        for kind, stacks, block in parse(text):
            total += 1
            k = (kind,) + tuple(sorted(key_frame(s) for s in stacks))
            g = groups.setdefault(k, {"n": 0, "e10": False})
            g["n"] += 1
            if want_e10:
                rt = "RtGate" in block or "ScopedGatedWrite" in block
                cross = len(stacks) == 2 and ((in_ranges(stacks[0], audio) and in_ranges(stacks[1], writers))
                                              or (in_ranges(stacks[1], audio) and in_ranges(stacks[0], writers)))
                g["e10"] = g["e10"] or rt or cross
    print(f"경고 {total}건, 자리 {len(groups)}곳" + (f", E10 관련 {sum(g['e10'] for g in groups.values())}곳" if want_e10 else ""))
    print("| # | 종류 | 한쪽 | 다른 쪽 | 건수 |" + (" E10 관련 |" if want_e10 else ""))
    print("|---|---|---|---|---|" + ("---|" if want_e10 else ""))
    for i, (k, g) in enumerate(sorted(groups.items(), key=lambda kv: (-kv[1]["n"], kv[0])), 1):
        a = k[1] if len(k) > 1 else ""
        b = k[2] if len(k) > 2 else ""
        row = f"| {i} | {k[0]} | `{a}` | `{b}` | {g['n']} |"
        if want_e10:
            row += " 예 |" if g["e10"] else " |"
        print(row)


if __name__ == "__main__":
    main(sys.argv[1:])
