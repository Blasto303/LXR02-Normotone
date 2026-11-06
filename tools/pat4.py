"""PAT4 v2 pattern file writer (PATTERN_DYNAMIC_STACK.md section 7).

Shared by convert_lxr02_project.py and convert_legacy_patterns.py. A step is
written as a bare trigger when it carries no special and no automation, or
as a trigger plus one dynamic pool block otherwise.
"""

from __future__ import annotations

import struct

NUM_TRACKS = 7
NUM_STEPS = 128
HEADER_BYTES = 160
ADDRESS_OFFSET = 160
BITMAP_OFFSET = 1952
POOL_OFFSET = 2464
FILE_BYTES = 10656
POOL_ALLOC_BYTES = 8056  # chunks above this are the reserved swap block
ADDR_TRIGGER = 0x8000
ADDR_SPECIALS = 0x4000
ADDR_SENTINEL = 0x3FFF
SPECIAL_NOTE = 0x01
SPECIAL_VELOCITY = 0x02
SPECIAL_CONDITION = 0x04
DEFAULT_LENGTH = 16
DEFAULT_SCALE = 76  # 1/16


def crc32c(data: bytes) -> int:
    """CRC32C (Castagnoli, reflected), as in tools/convert_pattern_specials.py."""
    crc = 0xFFFFFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ 0x82F63B78 if crc & 1 else crc >> 1
    return (~crc) & 0xFFFFFFFF


def build(
    steps: dict[tuple[int, int], tuple[int, list[int], dict[int, int]]],
    lengths: list[int] | None = None,
    scales: list[int] | None = None,
    shuffles: list[int] | None = None,
) -> bytes:
    """Return one PAT4 file.

    steps maps (track, step) of every active step to (flags, values in flag
    bit order, {target: 7-bit value}). Tracks default to length 16, scale
    1/16 and no shuffle.
    """
    lengths = lengths or [DEFAULT_LENGTH] * NUM_TRACKS
    scales = scales or [DEFAULT_SCALE] * NUM_TRACKS
    shuffles = shuffles or [0] * NUM_TRACKS
    address = [ADDR_SENTINEL] * (NUM_TRACKS * NUM_STEPS)
    pool = bytearray()

    for (track, step), (flags, values, automations) in sorted(steps.items()):
        index = track * NUM_STEPS + step
        if not flags and not automations:
            address[index] = ADDR_TRIGGER | ADDR_SENTINEL
            continue
        offset = len(pool)
        header = (index << 6) | len(automations)
        block = bytearray([header >> 8, header & 0xFF, flags])
        block += bytes(values)
        for target, value in automations.items():
            block += struct.pack("<H", (value << 9) | target)
        while len(block) % 4:
            block.append(0)
        pool += block
        address[index] = ADDR_TRIGGER | ADDR_SPECIALS | offset

    if len(pool) > POOL_ALLOC_BYTES:
        raise ValueError(f"pattern needs {len(pool)} pool bytes")

    out = bytearray(FILE_BYTES)
    out[0:4] = b"PAT4"
    struct.pack_into("<HHH", out, 4, 2, 256, HEADER_BYTES)
    for track in range(NUM_TRACKS):
        base = 48 + 16 * track
        out[base] = min(lengths[track], NUM_STEPS) or DEFAULT_LENGTH
        out[base + 1] = scales[track]
        out[base + 2] = min(shuffles[track], 127)
    struct.pack_into(f"<{NUM_TRACKS * NUM_STEPS}H", out, ADDRESS_OFFSET, *address)
    bitmap = bytearray(512)
    for chunk in range(len(pool) // 4):
        bitmap[chunk >> 3] |= 1 << (chunk & 7)
    bitmap[256:] = b"\xff" * 256
    out[BITMAP_OFFSET:BITMAP_OFFSET + 512] = bitmap
    out[POOL_OFFSET:POOL_OFFSET + len(pool)] = pool
    struct.pack_into("<I", out, 14, crc32c(bytes(out)))
    return bytes(out)
