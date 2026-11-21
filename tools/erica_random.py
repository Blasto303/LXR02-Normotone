#!/usr/bin/env python3
"""Add dosable RANDOM material to stock LXR-02 projects (Erica firmware 1.7x).

The stock firmware is closed, so RANDOM is delivered as files inside each
project, used with stock gestures:

  Random kits    kit N+16 ("RNDnn") is a random version of kit N (DRUM1 kept).
                 LOAD + MORPH, pick RNDnn, then the PERF `mrp` knob is the dose:
                 0 = the original kit, 127 = fully random.
  Random patterns pattern N+16 / N+32 / N+48 is pattern N with light / medium /
                 heavy randomness: some steps get a probability below 100 %,
                 ghost hits with a probability are added on empty steps, and
                 velocities are jittered. DRUM1 (kick) is never touched.
                 LOAD + PATTERN (or SHIFT + PATTERN), BAR to page 2/3/4, step N.
                 Each variant keeps pattern N's kit number.

Only empty slots are written: kit slots must hold the init kit and pattern
slots must have no trigger, otherwise the project is reported and skipped
for that part. Files keep their size and layout (PatV03 patterns, 255 B kits).

    python3 tools/erica_random.py <stock card copy> <output dir> [--seed N]

The output dir receives a copy of every PROJxx folder that was changed.
"""

from __future__ import annotations

import argparse
import random
import re
import shutil
import sys
from pathlib import Path

KIT_BYTES = 255
KIT_NAME = 8
PAT_V3 = 3663
PAT_HEADER = 14
STEPS = 64
STEP_BYTES = 7
TRACK_BYTES = STEPS * STEP_BYTES + STEPS        # PatV03: steps + 64 B extra
TRAILER = PAT_HEADER + 7 * TRACK_BYTES          # masks, shuffle, lengths, kit
NEUTRAL_NOTE = 60
PROJ_RE = re.compile(r"^PROJ(\d\d)$", re.IGNORECASE)

# Kit payload offsets (payload = file[8:], offset = legacy PAR_* value) of the
# continuous synthesis parameters of voices 2..6. Waveforms, filter types,
# volumes, pans, outputs, LFO routing, MIDI notes and the FX block are kept.
KIT_RANDOM_OFFSETS = [
    10, 12, 14, 16, 18,                 # coarse tune 2..6
    27, 28,                             # snare noise freq, mix
    29, 30, 31, 32,                     # cymbal mod osc f1/f2/gain1/gain2
    33, 34, 35, 36,                     # hihat mod osc f1/f2/gain1/gain2
    38, 39, 40, 41, 42,                 # filter freq 2..6
    44, 45, 46, 47, 48,                 # resonance 2..6 (capped below)
    52, 54, 56, 58, 60, 61,             # decays 2..6 (hh closed/open)
    63, 64, 65, 66, 67,                 # decay slopes 2..6
    71, 72, 73, 75, 76, 77,             # pitch EG decay / amount 2..4
    79, 80, 81,                         # pitch slopes 2..4
    84, 85, 86, 87,                     # FM amount / freq 2..3
    103, 104, 105, 106, 107,            # voice drive / distortion 2..6
    129, 130, 131, 132, 133,            # filter drive 2..6
    135, 136,                           # osc/mod mix 2..3
    122, 123, 124, 125, 126,            # LFO amount 2..6
    198, 199, 200, 201, 202,            # transient volume 2..6
    210, 211, 212, 213, 214,            # transient freq 2..6
]
RESONANCE = {44, 45, 46, 47, 48}

# name: (skip %, ghosts per 16 steps, ghost probability %, velocity jitter)
LEVELS = [("light", 10, 1, 35, 10), ("medium", 25, 2, 40, 20),
          ("heavy", 40, 4, 50, 35)]


def is_init_kit(path: Path) -> bool:
    return path.read_bytes()[:7] == b"Initkit"


def random_kit(src: bytes, name: str, rng: random.Random) -> bytes:
    data = bytearray(src)
    data[:KIT_NAME] = name.encode("ascii").ljust(KIT_NAME)[:KIT_NAME]
    for off in KIT_RANDOM_OFFSETS:
        i = KIT_NAME + off
        v = data[i] + rng.randint(-64, 64)
        hi = 100 if off in RESONANCE else 127
        data[i] = max(0, min(hi, v))
    return bytes(data)


def pattern_has_triggers(data: bytes) -> bool:
    return len(data) == PAT_V3 and any(data[TRAILER:TRAILER + 56])


def random_pattern(src: bytes, level, rng: random.Random) -> bytes:
    _, skip, ghosts, ghost_prob, jitter = level
    data = bytearray(src)
    lengths = data[TRAILER + 57:TRAILER + 64]
    for track in range(1, 7):                  # track 0 = DRUM1, kept
        mask = int.from_bytes(data[TRAILER + 8 * track:TRAILER + 8 * track + 8], "little")
        length = lengths[track] or 16
        base = PAT_HEADER + track * TRACK_BYTES
        empty = []
        for step in range(min(length, STEPS)):
            s = base + step * STEP_BYTES
            if mask >> step & 1:
                vol = data[s] & 0x7F
                vol = max(1, min(127, vol + rng.randint(-jitter, jitter)))
                data[s] = (data[s] & 0x80) | vol
                if rng.random() < 0.6:          # not every step gets a chance
                    data[s + 1] = max(5, min(data[s + 1], 100 - skip - rng.randint(0, skip)))
            else:
                empty.append(step)
        count = min(len(empty), max(1, ghosts * length // 16))
        for step in rng.sample(empty, count):
            s = base + step * STEP_BYTES
            data[s] = (data[s] & 0x80) | rng.randint(28, 60)
            data[s + 1] = ghost_prob
            data[s + 2] = NEUTRAL_NOTE
            data[s + 3:s + 7] = bytes([0xFF, 0, 0xFF, 0])
            mask |= 1 << step
        data[TRAILER + 8 * track:TRAILER + 8 * track + 8] = mask.to_bytes(8, "little")
    return bytes(data)


def slot_file(project: Path, number: int, ext: str) -> Path | None:
    for path in project.glob(f"{number:02d}-*.{ext}"):
        if not path.name.startswith("._"):
            return path
    return None


def process(project: Path, out: Path, rng: random.Random, report: list[str]) -> bool:
    changed = False
    dst = out / project.name
    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(project, dst, ignore=shutil.ignore_patterns("._*", ".DS_Store"))

    # Random kits: kit N -> slot N+16 when that slot is an init kit.
    kits_done = 0
    for n in range(1, 17):
        src = slot_file(dst, n, "SND")
        tgt = slot_file(dst, n + 16, "SND")
        if not src or not tgt or is_init_kit(src) or not is_init_kit(tgt):
            continue
        data = src.read_bytes()
        if len(data) != KIT_BYTES:
            continue
        name = f"RND{n:02d}"
        tgt.unlink()
        (dst / f"{n + 16:02d}-{name}.SND").write_bytes(random_kit(data, name, rng))
        kits_done += 1
    if kits_done:
        changed = True
    report.append(f"{project.name}: {kits_done} random kits (slots 17-32)")

    # Random patterns: pattern N -> N+16 / N+32 / N+48 when those are empty.
    for page, level in enumerate(LEVELS, start=1):
        done = 0
        for n in range(16):
            src = slot_file(dst, n, "PAT")
            tgt = slot_file(dst, n + 16 * page, "PAT")
            if not src or not tgt:
                continue
            data = src.read_bytes()
            if not pattern_has_triggers(data) or pattern_has_triggers(tgt.read_bytes()):
                continue
            tgt.write_bytes(random_pattern(data, level, rng))
            done += 1
        if done:
            changed = True
        report.append(f"{project.name}: {done} {level[0]} patterns (page {page + 1})")
    if not changed:
        shutil.rmtree(dst)
    return changed


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("card", type=Path)
    ap.add_argument("out", type=Path)
    ap.add_argument("--seed", type=int, default=303)
    ap.add_argument("--only", nargs="*", help="PROJxx folders to process (default: all)")
    args = ap.parse_args()
    rng = random.Random(args.seed)
    args.out.mkdir(parents=True, exist_ok=True)
    report: list[str] = []
    for project in sorted(p for p in args.card.iterdir() if p.is_dir() and PROJ_RE.match(p.name)):
        if args.only and project.name.upper() not in {o.upper() for o in args.only}:
            continue
        process(project, args.out, rng, report)
    print("\n".join(report))
    return 0


if __name__ == "__main__":
    sys.exit(main())
