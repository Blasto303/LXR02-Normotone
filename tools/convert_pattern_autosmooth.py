#!/usr/bin/env python3
"""Pattern automation-ramp field normaliser (S081 P2).

S081 P2 stores each track's automation ramp mode ("asm", off/on) in byte 5
of the PAT4 16-byte track header (offset 48 + track*16 + 5). The byte was
reserved and written as zero, so the PAT4 version stays 2 and existing files
read as "off". This script validates every PAT4 file (size, version, CRC32C)
and forces byte 5 to 0 (off) on any track where it is not 0, recomputing the
CRC. Non-PAT4 .pat files (the legacy text format under Bank/) are skipped
and listed; the firmware does not read them.

Usage:
    python3 tools/convert_pattern_autosmooth.py [--dry-run] PATH [PATH ...]

PATH may be a single file or a directory (scanned recursively). Files that are
neither PAT4 content nor named *.pat are ignored silently. A file that fails
validation is reported and left untouched. Exit status is nonzero if any file
failed validation or could not be rewritten.
"""

from __future__ import annotations

import argparse
import os
import sys

# PAT4 v4 header geometry (mirrors PatternData.h; same constants as
# tools/convert_scene_scale.py).
PAT4_MAGIC = b"PAT4"
PATTERN_FILE_VERSION = 2
PATTERN_FILE_HEADER_BYTES = 160
PATTERN_FILE_TRACK_HEADER_BYTES = 16
PATTERN_FILE_TRACK_BASE = 48
PATTERN_FILE_ADDRESS_BYTES = 1792
PATTERN_FILE_BITMAP_BYTES = 512
PATTERN_FILE_CRC_OFFSET = 14
NUM_TRACKS = 7

# S081 P2: track-header byte that holds the automation ramp mode.
AUTO_SMOOTH_BYTE = 5


def crc32c(data: bytes) -> int:
    """CRC32C (Castagnoli, reflected), matching autosave_recordCrcBegin/Finish."""
    crc = 0xFFFFFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ 0x82F63B78 if crc & 1 else crc >> 1
            crc &= 0xFFFFFFFF
    return (~crc) & 0xFFFFFFFF


def pat4_length(header_size: int, stack_size: int) -> int:
    return (
        header_size
        + PATTERN_FILE_ADDRESS_BYTES
        + PATTERN_FILE_BITMAP_BYTES
        + stack_size * 32
    )


def file_crc(data: bytearray) -> int:
    """CRC32C over the whole file with the four CRC bytes treated as zero
    (filesystem_patternCrcFeed())."""
    zeroed = bytearray(data)
    zeroed[PATTERN_FILE_CRC_OFFSET:PATTERN_FILE_CRC_OFFSET + 4] = b"\x00" * 4
    return crc32c(bytes(zeroed))


def validate_pat4(data: bytearray, path: str) -> None:
    """Raise ValueError when the file is not a well-formed PAT4 v2 file."""
    if len(data) < PATTERN_FILE_HEADER_BYTES:
        raise ValueError("%s: shorter than the 160-byte header" % path)
    version = data[4] | (data[5] << 8)
    stack_size = data[6] | (data[7] << 8)
    header_size = data[8] | (data[9] << 8)
    if version != PATTERN_FILE_VERSION:
        raise ValueError("%s: version %d != %d"
                         % (path, version, PATTERN_FILE_VERSION))
    if header_size < PATTERN_FILE_HEADER_BYTES:
        raise ValueError("%s: header_size %d < 160" % (path, header_size))
    total = pat4_length(header_size, stack_size)
    if len(data) != total:
        raise ValueError("%s: size %d != expected %d"
                         % (path, len(data), total))
    stored = int.from_bytes(
        data[PATTERN_FILE_CRC_OFFSET:PATTERN_FILE_CRC_OFFSET + 4], "little")
    computed = file_crc(data)
    if stored != computed:
        raise ValueError("%s: CRC32C stored 0x%08X != computed 0x%08X"
                         % (path, stored, computed))


def normalise_pat4(data: bytearray) -> int:
    """Force byte 5 of every track header to 0 (off) and restamp the CRC.
    Returns the number of tracks changed."""
    changed = 0
    for track in range(NUM_TRACKS):
        offset = (PATTERN_FILE_TRACK_BASE
                  + track * PATTERN_FILE_TRACK_HEADER_BYTES
                  + AUTO_SMOOTH_BYTE)
        if data[offset] != 0:
            data[offset] = 0
            changed += 1
    if changed:
        crc = file_crc(data)
        data[PATTERN_FILE_CRC_OFFSET:PATTERN_FILE_CRC_OFFSET + 4] = \
            crc.to_bytes(4, "little")
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
        description="Normalise the PAT4 automation ramp byte (S081 P2).")
    parser.add_argument("--dry-run", action="store_true",
                        help="report changes without writing")
    parser.add_argument("paths", nargs="+",
                        help="files or directories to normalise")
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
            if path.lower().endswith(".pat"):
                print("skip %s: not PAT4" % path)
            continue

        try:
            validate_pat4(data, path)
        except ValueError as exc:
            print("fail %s" % exc, file=sys.stderr)
            failures += 1
            continue

        changed = normalise_pat4(data)
        if changed == 0:
            print("ok   %s" % path)
            continue
        if args.dry_run:
            print("plan %s: %d track(s)" % (path, changed))
            continue
        try:
            with open(path, "wb") as handle:
                handle.write(data)
        except OSError as exc:
            print("fail %s: %s" % (path, exc), file=sys.stderr)
            failures += 1
            continue
        print("done %s: %d track(s)" % (path, changed))

    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
