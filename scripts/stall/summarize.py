"""run-jam.sh 결과 표. 사용: python3 -I scripts/stall/summarize.py <out-이름 폴더> ...

- 앱별 오디오 콜백 시간 (stallprobe): 최대, 10/20/50/100 ms 넘은 횟수
- 듣는 쪽별 끊김 (jamprobe): 무음·멈춘 버퍼 횟수·합·최대, 100 ms 넘은 끊김
- 엔진이 상대 소리 섞기를 건너뛴 블록 (E10, 앱 로그), 들어오고 나가기 횟수, 끝날 때 앱이 살아 있었는지
- 멈춤 순간 오디오 스레드가 있던 곳 (스택 위쪽). AOO 내부 잠금 등 E10 밖의 원인을 찾을 때 본다
"""
import os
import re
import subprocess
import sys
from collections import Counter

PROBE = re.compile(r"콜백 (\d+)번, 최대 ([\d.]+) ms, ≥1ms (\d+) ≥2.67ms (\d+) ≥5ms (\d+) ≥10ms (\d+) ≥20ms (\d+) ≥50ms (\d+) ≥100ms (\d+) ≥500ms (\d+)")
EVENT = re.compile(r"EV .* (멈춤|무음) t=([\d.]+) ([\d.]+) ms")
SKIP = re.compile(r"건너뛴 블록 \(누적, E10\): (\d+)")


def read(path):
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError:
        return None


def last_probe(text):
    best = None
    for m in PROBE.finditer(text or ""):
        if int(m.group(1)) > 0:
            best = m
    return best


_names = {}


def frame_name(line):
    """'libc.so.6(__sched_yield+0xb)' → __sched_yield, '/경로/meon(+0x57c1a7)' → addr2line 함수 이름 (실행 파일이 있으면)"""
    m = re.search(r"^\s*(\S+?)\(([^+)]*)\+(0x[0-9a-f]+)\)", line)
    if not m:
        return line.strip()
    path, sym, off = m.groups()
    if sym:
        return sym
    key = (path, off)
    if key not in _names:
        name = os.path.basename(path) + "+" + off
        if os.path.exists(path):
            try:
                out = subprocess.run(["addr2line", "-C", "-f", "-e", path, hex(int(off, 16) - 1)],
                                     capture_output=True, text=True, timeout=20).stdout.split("\n")[0].strip()
                if out and out != "??":
                    name = re.sub(r"\(.*\)( const)?$", "", out)   # 인자 목록은 뺀다
            except (OSError, subprocess.SubprocessError):
                pass
        _names[key] = name
    return _names[key]


def stall_sites(text, top=4):
    """멈춤 기록마다 오디오 스레드 스택 위쪽 몇 줄 (함수 이름만)"""
    sites = Counter()
    for block in (text or "").split("\n=== 멈춤 ")[1:]:
        m = re.search(r"--- 스레드 \d+ \[[^\]]*\] \(오디오\)[^\n]*\n((?:  [^\n]*\n)+)", block)
        if not m:
            continue
        frames = []
        for line in m.group(1).splitlines():
            if "stallprobe" in line:
                continue
            name = frame_name(line)
            if name:
                frames.append(name)
            if len(frames) >= top:
                break
        sites[" ← ".join(frames)] += 1
    return sites


def main(dirs):
    print("| 시험 | 앱 | 콜백 수 | 최대 콜백 | >10 ms | >20 ms | >50 ms | >100 ms | 건너뛴 블록 | 끝날 때 |")
    print("|---|---|---|---|---|---|---|---|---|---|")
    for d in dirs:
        name = os.path.basename(d.rstrip("/"))[4:]
        alive = read(os.path.join(d, "alive.txt")) or ""
        for u in "ab":
            m = last_probe(read(os.path.join(d, f"probe-{u}.log")))
            log = read(os.path.join(d, f"home-{u}", ".config", "meon", "logs", "app.log")) or ""
            skips = SKIP.findall(log)
            state = "살아 있음" if f"{u} alive" in alive else ("죽음" if f"{u} DEAD" in alive else "?")
            if m:
                print(f"| {name} | {u.upper()} | {m.group(1)} | {float(m.group(2)):.1f} ms | {m.group(6)} | {m.group(7)} | {m.group(8)} | {m.group(9)} | {skips[-1] if skips else 0} | {state} |")
            else:
                print(f"| {name} | {u.upper()} | (stallprobe 없음) | | | | | | {skips[-1] if skips else 0} | {state} |")
    print()
    print("| 시험 | 듣는 쪽 | 무음 (번 / 합 / 최대 ms) | 멈춘 버퍼 (번 / 합 / 최대 ms) | 100 ms 넘은 끊김 | 소리 받음 |")
    print("|---|---|---|---|---|---|")
    for d in dirs:
        name = os.path.basename(d.rstrip("/"))[4:]
        for f in ("hear-a.log", "hear-b.log"):
            text = read(os.path.join(d, f))
            if text is None:
                continue
            q, s = [], []
            for m in EVENT.finditer(text):
                (s if m.group(1) == "멈춤" else q).append(float(m.group(3)))
            label = re.search(r"PROBE END (.+?) secs=", text)
            label = label.group(1) if label else f
            sig = re.search(r"signal=(\d)", text)
            big = [x for x in q + s if x > 100]
            print(f"| {name} | {label} | {len(q)} / {sum(q):.0f} / {max(q, default=0):.1f} | {len(s)} / {sum(s):.0f} / {max(s, default=0):.1f} | {len(big)} | {'예' if sig and sig.group(1) == '1' else '아니오'} |")
    for d in dirs:
        name = os.path.basename(d.rstrip("/"))[4:]
        churn = read(os.path.join(d, "churn.log"))
        if churn:
            lines = churn.strip().splitlines()
            print(f"\n{name}: 들어오고 나가기 {len(lines)}번 (정상 종료 {sum(' TERM' in l for l in lines)}, 강제 종료 {sum(' KILL' in l for l in lines)})")
        for u in "ab":
            sites = stall_sites(read(os.path.join(d, f"probe-{u}.log")))
            if sites:
                print(f"\n{name} {u.upper()}: 10 ms 넘게 멈춘 순간 오디오 스레드 위치")
                for k, n in sites.most_common(8):
                    print(f"  {n}번  {k}")


if __name__ == "__main__":
    main(sys.argv[1:])
