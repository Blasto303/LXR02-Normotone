#!/usr/bin/env python3
"""Offline step-special migration utility (S079 P1 step 9).

S079 replaced the per-step probability special (dynamic-block flags bit 2,
value 0..127) with a generic trigger condition (same bit 2; value 0..38, where
0 is always-fire, 1..10 probability, 11..20 X:Y, 21..22 fill, 23..24 last,
25..38 mute pairs for voices 1..7) and
assigned bits 3..6 to roll rate, micro-timing, roll rhythm, and roll fill.
There is no on-device migration: the reader rejects pre-S079 Pattern files
(header version 1) outright. Run this script on a card image directory (or on
individual files) to rewrite old probability bytes into the new condition
encoding and bump the header version so the new firmware loads them.

What it rewrites:
  * PAT4 Pattern files: the header version byte (offset 4..5) is set to 2, and
    every dynamic pool block whose special-flags byte has bit 2 set has its
    probability byte replaced by the equivalent condition index. The file
    CRC32C is recomputed.

Old probability byte (0..127) -> new condition index:
    127        -> 0   (always fire, displayed "-")
    114..126   -> 10  (r95)
    102..113   ->  9  (r90)
     89..101   ->  8  (r80)
     76..88    ->  7  (r70)
     64..75    ->  6  (r60)
     51..63    ->  5  (r50)
     38..50    ->  4  (r40)
     25..37    ->  3  (r30)
     13..24    ->  2  (r20)
      0..12    ->  1  (r10)

The mapping rounds to the nearest 10% bucket; the old continuous range is not
carried forward. The block layout is unchanged (bit 2 stays set, so the byte
count and pool geometry are identical); an old probability of 127 is written
as condition 0, which is the equivalent always-fire default.

Usage:
    python3 tools/convert_pattern_specials.py [--dry-run] PATH [PATH ...]

PATH may be a single file or a directory (scanned recursively). Files that are
not PAT4 content are skipped. Exit status is nonzero if any file could not be
rewritten.
"""

from __future__ import annotations

import argparse
import os
import sys

# PAT4 header geometry (mirrors PatternData.h / convert_scene_scale.py).
PAT4_MAGIC = b"PAT4"
PATTERN_FILE_HEADER_BYTES = 160
PATTERN_FILE_ADDRESS_BYTES = 1792
PATTERN_FILE_BITMAP_BYTES = 512
PATTERN_FILE_CRC_OFFSET = 14
PATTERN_FILE_VERSION_OFFSET = 4
PAT4_NEW_VERSION = 2
NUM_TRACKS = 7
NUM_STEPS = 128

# Address-entry and pool-block field masks (mirror PatternData.h).
PAT_ADDR_SPECIALS_BIT = 1 << 14
PAT_ADDR_OFFSET_MASK = 0x3FFF
PAT_SPECIAL_COND_BIT = 1 << 2
PAT_SPECIAL_NOTE_BIT = 1 << 0
PAT_SPECIAL_VEL_BIT = 1 << 1
PAT_BLOCK_HEADER_BYTES = 2


def crc32c(data: bytes) -> int:
    """CRC32C (Castagnoli, reflected), matching autosave_recordCrcBegin/Finish."""
    crc = 0xFFFFFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ 0x82F63B78 if crc & 1 else crc >> 1
            crc &= 0xFFFFFFFF
    return (~crc) & 0xFFFFFFFF


def probability_to_condition(prob: int) -> int:
    """Map one old 0..127 probability byte to a new 0..38 condition index.

    Only the probability buckets (0..10) and the always-fire default (0) can be
    produced here; the extended mute range (up to 38) is never emitted because
    old files only stored probability values.
    """
    if prob >= 127:
        return 0
    if prob >= 114:
        return 10
    if prob >= 102:
        return 9
    if prob >= 89:
        return 8
    if prob >= 76:
        return 7
    if prob >= 64:
        return 6
    if prob >= 51:
        return 5
    if prob >= 38:
        return 4
    if prob >= 25:
        return 3
    if prob >= 13:
        return 2
    return 1


def pat4_length(header_size: int, stack_size: int) -> int:
    return (
        header_size
        + PATTERN_FILE_ADDRESS_BYTES
        + PATTERN_FILE_BITMAP_BYTES
        + stack_size * 32
    )


def convert_pat4(data: bytearray, path: str, dry_run: bool) -> int:
    """Convert one PAT4 file in place. Returns the number of bytes changed."""
    if len(data) < PATTERN_FILE_HEADER_BYTES or data[0:4] != PAT4_MAGIC:
        return -1
    header_size = data[8] | (data[9] << 8)
    stack_size = data[6] | (data[7] << 8)
    version = data[PATTERN_FILE_VERSION_OFFSET] | (
        data[PATTERN_FILE_VERSION_OFFSET + 1] << 8)
    if header_size < PATTERN_FILE_HEADER_BYTES:
        raise ValueError("%s: header_size %d < 160" % (path, header_size))
    total = pat4_length(header_size, stack_size)
    if len(data) != total:
        raise ValueError("%s: size %d != expected %d" % (path, len(data), total))

    if version == PAT4_NEW_VERSION:
        return 0

    pool_bytes = stack_size * 32
    address_base = header_size
    pool_base = header_size + PATTERN_FILE_ADDRESS_BYTES + PATTERN_FILE_BITMAP_BYTES
    changed = 0

    for index in range(NUM_TRACKS * NUM_STEPS):
        entry = data[address_base + index * 2] | (
            data[address_base + index * 2 + 1] << 8)
        if not (entry & PAT_ADDR_SPECIALS_BIT):
            continue
        offset = entry & PAT_ADDR_OFFSET_MASK
        if offset & 3 or offset >= pool_bytes:
            continue
        block = pool_base + offset
        flags = data[block + 2]
        if not (flags & PAT_SPECIAL_COND_BIT):
            continue
        # The probability byte follows the stored note and velocity bytes.
        value_index = PAT_BLOCK_HEADER_BYTES + 1
        if flags & PAT_SPECIAL_NOTE_BIT:
            value_index += 1
        if flags & PAT_SPECIAL_VEL_BIT:
            value_index += 1
        prob_index = block + value_index
        if prob_index >= len(data):
            continue
        old = data[prob_index]
        new = probability_to_condition(old)
        if new != old:
            data[prob_index] = new
            changed += 1

    # Always bump the version so the reader accepts the (converted) file.
    data[PATTERN_FILE_VERSION_OFFSET] = PAT4_NEW_VERSION & 0xFF
    data[PATTERN_FILE_VERSION_OFFSET + 1] = (PAT4_NEW_VERSION >> 8) & 0xFF
    changed += 1  # count the version byte as a change

    if dry_run:
        return changed

    # Recompute the header CRC32C with bytes 14..17 treated as zero.
    zeroed = bytearray(data)
    zeroed[PATTERN_FILE_CRC_OFFSET:PATTERN_FILE_CRC_OFFSET + 4] = bytes(4)
    crc = crc32c(bytes(zeroed))
    data[PATTERN_FILE_CRC_OFFSET:PATTERN_FILE_CRC_OFFSET + 4] = crc.to_bytes(4, "little")
    return changed


def iter_paths(paths):
    for path in paths:
        if os.path.isdir(path):
            for root, _dirs, files in os.walk(path):
                for name in sorted(files):
                    yield os.path.join(root, name)
        else:
            yield path


def main(argv) -> int:
    parser = argparse.ArgumentParser(
        description="Migrate old step probability bytes to trigger conditions (S079).")
    parser.add_argument("--dry-run", action="store_true",
                        help="report changes without writing")
    parser.add_argument("paths", nargs="+",
                        help="files or directories to migrate")
    args = parser.parse_args(argv)

    failures = 0
    for path in iter_paths(args.paths):
        try:
            with open(path, "rb") as handle:
                data = bytearray(handle.read())
        except OSError as exc:
            print("skip %s: %s" % (path, exc), file=sys.stderr)
            continue

        if data[0:4] != PAT4_MAGIC:
            continue
        try:
            changed = convert_pat4(data, path, args.dry_run)
        except ValueError as exc:
            print(str(exc), file=sys.stderr)
            failures += 1
            continue
        if changed < 0:
            continue
        if changed == 0:
            print("ok   %s: already migrated" % path)
            continue
        if not args.dry_run:
            with open(path, "wb") as handle:
                handle.write(data)
        verb = "plan" if args.dry_run else "done"
        print("%s %s: version bump + condition rewrite" % (verb, path))

    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
