#!/usr/bin/env python3
"""Rewrite legacy text Pattern files (v1-v3) as PAT4 v2, in place.

The firmware's Scene and Bank loaders only accept PAT4: a Scene whose Pattern
child is a `format=helicase.pattern` text file fails its header check (phase
46) and the Scene is dropped from the Bank Load (BKKit14). 229 such files
shipped in SD_CARD/Bank/001..023.

Legacy formats (FILESYSTEM_SPEC.md, Legacy text formats):
  v1  placeholder=1                    -> empty Pattern
  v2  trackN=<length>,<scale>,<bits>   -> 128 on/off characters per track
  v3  trackN=<32 hex digits>           -> 16 bytes, bit 0 = earlier step
Only the trigger bits exist in these formats; every track gets the PAT4
defaults (length 16, scale 1/16, no shuffle), as an empty Scene does.

    python3 tools/convert_legacy_patterns.py [--dry-run] <card root or .pat> ...
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pat4  # noqa: E402


def parse_legacy(text: str) -> dict[tuple[int, int], tuple[int, list[int], dict[int, int]]]:
    rows = dict(
        line.split("=", 1) for line in text.splitlines() if "=" in line
    )
    if rows.get("format") != "helicase.pattern":
        raise ValueError("not a helicase.pattern text file")
    version = rows.get("version")
    steps: dict[tuple[int, int], tuple[int, list[int], dict[int, int]]] = {}
    if version == "1":
        return steps
    for track in range(pat4.NUM_TRACKS):
        value = rows.get(f"track{track + 1}")
        if value is None:
            raise ValueError(f"track{track + 1} missing")
        if version == "3":
            data = bytes.fromhex(value)
            if len(data) != 16:
                raise ValueError(f"track{track + 1}: {len(data)} bytes")
            active = [i for i in range(pat4.NUM_STEPS) if data[i >> 3] >> (i & 7) & 1]
        elif version == "2":
            bits = value.split(",")[-1]
            active = [i for i, c in enumerate(bits[:pat4.NUM_STEPS]) if c == "1"]
        else:
            raise ValueError(f"unknown version {version}")
        for step in active:
            steps[(track, step)] = (0, [], {})
    return steps


def iter_pat_files(paths: list[Path]):
    for path in paths:
        if path.is_dir():
            yield from sorted(p for p in path.rglob("*.pat") if not p.name.startswith("._"))
        else:
            yield path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("paths", nargs="+", type=Path)
    args = parser.parse_args()
    converted = skipped = failed = 0
    for path in iter_pat_files(args.paths):
        data = path.read_bytes()
        if data[:4] == b"PAT4":
            skipped += 1
            continue
        try:
            out = pat4.build(parse_legacy(data.decode("ascii")))
        except (ValueError, UnicodeDecodeError) as exc:
            print(f"FAIL {path}: {exc}")
            failed += 1
            continue
        if not args.dry_run:
            path.write_bytes(out)
        converted += 1
    print(f"converted {converted}, already PAT4 {skipped}, failed {failed}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
