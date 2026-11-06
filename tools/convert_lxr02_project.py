#!/usr/bin/env python3
"""Convert Erica Synths LXR-02 stock projects into Helicase Kits and Banks.

Input is the root of a stock LXR-02 card (or a copy of it) holding PROJ00..
PROJ09 folders. Each project folder has 64 kits (NN-NAME.SND), 64 patterns
(NN-NAME.PAT) and songs (NN-NAME.SNG). Output is written under --out:

    Kit/NNN <pp>-<name>/          one root Kit per distinct non-init kit
    Bank/NNN <project name>/      one Bank per 16 non-empty patterns
        bankset.bcg
        SS <pp>-<nn>/             Scene: sceneset.scg, Kit <name>/, <name>.pat

Stock formats (reverse engineered from cards written by firmware 1.70):

  .SND  255 B: 8 B name + 247 B parameters. Bytes 0..227 of the parameter
        block follow the original Sonic Potions LXR ParameterArray order
        (tools/lxr_legacy/ParameterArray.h); bytes 228..246 are LXR-02 only
        and are not converted. LFO and velocity targets are indexes into the
        LXR-02 modulation target list (LXR02_MOD_TARGET_PARAMS), which is
        longer than the original LXR list.
  .PAT  8 B name, 6 B version tag, then 7 tracks, then a 65 B trailer.
        PatV03 (3,663 B): per track 64 x 7 B steps + 64 B per-step extra
        (0x70 by default; meaning unknown, not converted).
        PatV02 (3,215 B): per track 64 x 7 B steps only.
        Step: volume (bit 7 not the trigger), probability 0..100 (percent),
        note (60 = unchanged pitch), dest1, value1, dest2, value2.
        Automation dest is a legacy PAR_* index plus one; 0, 1 and 0xFF
        are none.
        Trailer: 7 x 64-bit little-endian trigger masks, shuffle,
        7 track lengths (0 = 16), kit number of the pattern. Triggers past a
        track length are kept, as the stock firmware keeps them.
  .SNG  song chains, not converted (Helicase chains are per Scene).

Helicase output follows FILESYSTEM_SPEC.md (Kit, Bank, Scene) and
PATTERN_DYNAMIC_STACK.md section 7 (PAT4 v2).
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import convert_legacy_kits as kits  # noqa: E402
import pat4  # noqa: E402

LEGACY_PARAMETER_ARRAY_H = Path(__file__).resolve().parent / "lxr_legacy" / "ParameterArray.h"

KIT_FILE_BYTES = 255
KIT_NAME_BYTES = 8
KIT_CONVERTED_BYTES = 228  # original LXR parameter block

PAT_V3_BYTES = 3663
PAT_V2_BYTES = 3215
PAT_HEADER_BYTES = 14
PAT_STEPS = 64
PAT_STEP_BYTES = 7
PAT_TRAILER_BYTES = 65

LXR02_NEUTRAL_NOTE = 60
NO_AUTOMATION = 0xFF

NUM_TRACKS = 7
DEFAULT_NOTE = 63
DEFAULT_VELOCITY = 100
INSTRUMENT_PARAM_LIMIT = 384  # voice parameter IDs are 0..383

# LXR-02 modulation target list: target index -> legacy PAR_* index. Read
# from the (text, parameter) uint16 table at offset 0x33544 of the stock
# firmware 1.70 application payload. Parameters 228..241 are LXR-02 only.
LXR02_MOD_TARGET_PARAMS = [
      0, 114,   8,   9,   1, 228,  49,  50,  62,  70,  78,  74,
    149, 143, 137,  82,  83,  20, 134, 203, 197, 209,  37,  43,
    191, 128, 115, 179, 121, 155, 173, 185,  88,  94, 108, 102,
    238, 239, 240, 241,  10,  11,   2, 229,  51,  52,  63,  71,
     79,  75, 150, 144, 138,  84,  85,  21, 135, 204, 198, 210,
     38,  44, 192, 129, 116, 180, 122, 156, 174, 186,  89,  95,
    109, 103, 238, 239, 240, 241,  12,  13,   3, 230,  53,  54,
     64,  72,  80,  76, 151, 145, 139,  86,  87,  22, 136, 205,
    199, 211,  39,  45, 193, 130, 117, 181, 123, 157, 175, 187,
     90,  96, 110, 104, 238, 239, 240, 241,  14,  15,  27,  28,
      4, 231,  55,  56,  68,  65,  73,  81,  77, 152, 146, 140,
    206, 200, 212,  40,  46, 194, 131, 118, 182, 124, 158, 176,
    188,  91,  99, 111, 105, 238, 239, 240, 241,  16,  17,   6,
    232,  57,  58,  69,  66, 153, 147, 141,  29,  30,  31,  32,
     23,  24, 207, 201, 213,  41,  47, 195, 132, 119, 183, 125,
    159, 177, 189,  92, 100, 112, 106, 238, 239, 240, 241,  18,
     19,   7, 233,  59,  60,  61,  67, 154, 148, 142,  33,  34,
     35,  36,  25,  26, 208, 202, 214,  42,  48, 196, 133, 120,
    184, 126, 160, 178, 190,  93, 101, 113, 107, 238, 239, 240,
    241,
]

PROJECT_DIR_RE = re.compile(r"^PROJ(\d\d)$", re.IGNORECASE)


def display_name(raw: bytes) -> str:
    return re.sub(r"[^A-Za-z0-9]", "", raw.decode("latin-1"))


def probability_to_condition(percent: int) -> int:
    """Map a 0..100 % step probability to Helicase conditions 0 (always),
    1..9 (10..90 %) and 10 (95 %)."""
    if percent >= 98:
        return 0
    if percent >= 93:
        return 10
    return max(1, min(9, (percent + 5) // 10))


class Converter:
    def __init__(self) -> None:
        kits.PARAMETER_ARRAY_H = LEGACY_PARAMETER_ARRAY_H
        self.param_values = kits.parse_param_enum()
        self.param_renames = kits.parse_param_renames()
        kits.validate_param_renames(self.param_renames)
        self.canonical_by_param = kits.build_canonical_param_ids(self.param_renames)
        # Legacy PAR_* index -> canonical instrument parameter ID. Several
        # enum names share an index; any of them mapping to an instrument
        # parameter gives the target.
        self.target_by_index: dict[int, int] = {}
        for name, index in self.param_values.items():
            canonical = self.canonical_by_param.get(name)
            if canonical is not None and index not in self.target_by_index:
                self.target_by_index[index] = canonical
        # Kit LFO/velocity targets index the LXR-02 list; give the shared kit
        # converter that list as PAR_* names (PAR_NONE where Helicase has no
        # voice parameter).
        name_by_index = {}
        for name, index in self.param_values.items():
            if name in self.canonical_by_param:
                name_by_index.setdefault(index, name)
        kits.LEGACY_MOD_TARGET_PARAMS = [
            name_by_index.get(index, "PAR_NONE") for index in LXR02_MOD_TARGET_PARAMS
        ]
        self.notes: list[str] = []

    # ---- kits -----------------------------------------------------------

    def write_kit(self, data: bytes, kit_dir: Path, name: str) -> None:
        payload = data[KIT_NAME_BYTES:]
        kit_dir.mkdir(parents=True, exist_ok=True)
        prefix = kits.filename_prefix(name, "lxrkit")
        files = [
            (instrument_type, f"{prefix}{suffix}.{instrument_type}", slot)
            for instrument_type, suffix, slot in kits.INSTRUMENT_FILES
        ]
        for instrument_type, filename, slot in files:
            kits.write_instrument(
                kit_dir / filename,
                instrument_type,
                kits.instrument_values(
                    payload,
                    self.param_values,
                    self.param_renames,
                    self.canonical_by_param,
                    slot,
                ),
            )
        kits.write_kitset(kit_dir / "kitset.kcg", files, payload, self.param_values)

    # ---- patterns -------------------------------------------------------

    def read_pattern(self, data: bytes, where: str) -> dict | None:
        if len(data) == PAT_V3_BYTES:
            track_bytes = PAT_STEPS * PAT_STEP_BYTES + PAT_STEPS
        elif len(data) == PAT_V2_BYTES:
            track_bytes = PAT_STEPS * PAT_STEP_BYTES
        else:
            self.notes.append(f"{where}: unknown pattern size {len(data)}, skipped")
            return None
        trailer = data[PAT_HEADER_BYTES + NUM_TRACKS * track_bytes:]
        masks = [
            int.from_bytes(trailer[8 * track:8 * track + 8], "little")
            for track in range(NUM_TRACKS)
        ]
        tracks = []
        for track in range(NUM_TRACKS):
            base = PAT_HEADER_BYTES + track * track_bytes
            steps = [
                data[base + PAT_STEP_BYTES * i:base + PAT_STEP_BYTES * (i + 1)]
                for i in range(PAT_STEPS)
            ]
            extra = data[base + PAT_STEPS * PAT_STEP_BYTES:base + track_bytes]
            tracks.append((steps, extra))
        return {
            "masks": masks,
            "tracks": tracks,
            "shuffle": trailer[56],
            "lengths": list(trailer[57:64]),
            "kit": trailer[64],
        }

    def build_pat4(self, pattern: dict, where: str) -> bytes:
        steps: dict[tuple[int, int], tuple[int, list[int], dict[int, int]]] = {}
        dropped_targets: set[int] = set()
        unknown_extra = 0

        for track in range(NUM_TRACKS):
            track_steps, extra = pattern["tracks"][track]
            for step in range(PAT_STEPS):
                if not pattern["masks"][track] >> step & 1:
                    continue
                if extra and extra[step] != 0x70:
                    unknown_extra += 1
                volume, prob, note, dest1, val1, dest2, val2 = track_steps[step]
                flags = 0
                values = []
                helicase_note = max(0, min(127, note - LXR02_NEUTRAL_NOTE + DEFAULT_NOTE))
                if helicase_note != DEFAULT_NOTE:
                    flags |= pat4.SPECIAL_NOTE
                    values.append(helicase_note)
                velocity = volume & 0x7F
                if velocity != DEFAULT_VELOCITY:
                    flags |= pat4.SPECIAL_VELOCITY
                    values.append(velocity)
                condition = probability_to_condition(prob)
                if condition:
                    flags |= pat4.SPECIAL_CONDITION
                    values.append(condition)

                automations: dict[int, int] = {}
                for dest, value in ((dest1, val1), (dest2, val2)):
                    if dest in (0, 1, NO_AUTOMATION):
                        continue
                    target = self.target_by_index.get(dest - 1)
                    if target is None or target >= INSTRUMENT_PARAM_LIMIT:
                        dropped_targets.add(dest - 1)
                        continue
                    automations[target] = min(value, 127)
                steps[(track, step)] = (flags, values, automations)

        if dropped_targets:
            self.notes.append(
                f"{where}: automations on PAR indexes without a Helicase voice "
                f"parameter dropped: {sorted(dropped_targets)}"
            )
        if unknown_extra:
            self.notes.append(f"{where}: {unknown_extra} steps with an unconverted extra byte")
        # Length 0 holds a normal 16-step track on every card seen; pat4
        # writes it as 16, the length the firmware falls back to for 0.
        try:
            return pat4.build(steps, lengths=pattern["lengths"],
                              shuffles=[pattern["shuffle"]] * NUM_TRACKS)
        except ValueError as exc:
            raise RuntimeError(f"{where}: {exc}") from exc


def sceneset_text(lengths: list[int]) -> str:
    length_morph = ",".join(str(value if 1 <= value <= 128 else 16) for value in lengths)
    return "\n".join([
        "format=helicase.sceneset",
        "version=1",
        "morph_amount=0",
        "voice_morph_amount=0,0,0,0,0,0",
        "midi_channel=0,0,0,0,0,0,0",
        "midi_note=0,0,0,0,0,0,0",
        "audio_out=0,0,0,0,0,0",
        "fx_send_amount=0,0,0,0,0,0",
        "fader_setting=0,0,0,0,0,0",
        "fx_send_morph=0,0,0,0,0,0",
        f"track_morph_length={length_morph}",
        "",
    ])


def bankset_text() -> str:
    lines = ["format=helicase.bankset", "version=2", "active_scene=0"]
    lines += [f"scene_mask_voice_edit_{i}={1 << i:04X}" for i in range(16)]
    return "\n".join(lines + [""])


def project_name(project: Path) -> str:
    info = project / "PRJ.NFO"
    raw = info.read_bytes()[:8] if info.exists() else b""
    return display_name(raw) or project.name


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("card", type=Path, help="stock LXR-02 card root (PROJ00..PROJ09)")
    parser.add_argument("--out", type=Path, required=True, help="Helicase card root to write into")
    parser.add_argument("--kit-slot", type=int, default=100, help="first root Kit slot")
    parser.add_argument("--bank-slot", type=int, default=100, help="first root Bank slot")
    args = parser.parse_args()

    conv = Converter()
    kit_root = args.out / "Kit"
    bank_root = args.out / "Bank"
    kit_slot = args.kit_slot
    bank_slot = args.bank_slot
    report = []

    projects = sorted(
        p for p in args.card.iterdir() if p.is_dir() and PROJECT_DIR_RE.match(p.name)
    )
    for project in projects:
        pp = PROJECT_DIR_RE.match(project.name).group(1)
        kit_files = {}
        for path in project.glob("[0-9][0-9]-*.SND"):
            if not path.name.startswith("._"):
                kit_files[int(path.name[:2])] = path
        init_core = None
        for number in sorted(kit_files):
            data = kit_files[number].read_bytes()
            if data[:7] == b"Initkit":
                init_core = data[KIT_NAME_BYTES:KIT_NAME_BYTES + KIT_CONVERTED_BYTES]
                break

        def kit_label(number: int, data: bytes) -> str:
            return f"{pp}-{display_name(data[:KIT_NAME_BYTES]) or f'{number:02d}'}"[:8]

        # Root Kit library: distinct, non-init kits.
        seen = set()
        for number in sorted(kit_files):
            data = kit_files[number].read_bytes()
            if len(data) != KIT_FILE_BYTES:
                conv.notes.append(f"{kit_files[number]}: {len(data)} B kit skipped")
                continue
            core = data[KIT_NAME_BYTES:KIT_NAME_BYTES + KIT_CONVERTED_BYTES]
            if core == init_core or core in seen:
                continue
            seen.add(core)
            label = kit_label(number, data)
            conv.write_kit(data, kit_root / f"{kit_slot:03d} {label}", label)
            report.append(f"Kit  {kit_slot:03d} {label:<8} <- {project.name}/{kit_files[number].name}")
            kit_slot += 1

        # Banks: non-empty patterns, 16 Scenes per Bank, each with its kit.
        scenes = []
        for path in sorted(project.glob("[0-9][0-9]-*.PAT")):
            if path.name.startswith("._"):
                continue
            pattern = conv.read_pattern(path.read_bytes(), f"{project.name}/{path.name}")
            if pattern is None or not any(pattern["masks"]):
                continue
            scenes.append((int(path.name[:2]), path, pattern))

        name = project_name(project)
        for first in range(0, len(scenes), 16):
            group = scenes[first:first + 16]
            suffix = "" if first == 0 else f"{first // 16 + 1}"
            bank_dir = bank_root / f"{bank_slot:03d} {(name[:8 - len(suffix)] + suffix)}"
            bank_dir.mkdir(parents=True, exist_ok=True)
            (bank_dir / "bankset.bcg").write_text(bankset_text(), encoding="ascii")
            for scene_slot, (number, path, pattern) in enumerate(group):
                where = f"{project.name}/{path.name}"
                scene_dir = bank_dir / f"{scene_slot:02d} {pp}-{number:02d}"
                scene_dir.mkdir(parents=True, exist_ok=True)
                (scene_dir / "sceneset.scg").write_text(
                    sceneset_text(pattern["lengths"]), encoding="ascii"
                )
                (scene_dir / f"{pp}-{number:02d}.pat").write_bytes(conv.build_pat4(pattern, where))
                kit_path = kit_files.get(pattern["kit"])
                if kit_path is None:
                    conv.notes.append(f"{where}: kit {pattern['kit']} missing, Scene has no kit")
                    kit_text = "none"
                else:
                    data = kit_path.read_bytes()
                    label = kit_label(pattern["kit"], data)
                    conv.write_kit(data, scene_dir / f"Kit {label}", label)
                    kit_text = f"{label} ({kit_path.name})"
                report.append(
                    f"Bank {bank_dir.name} / {scene_dir.name} <- {where}, kit {kit_text}"
                )
            bank_slot += 1

    print("\n".join(report))
    if conv.notes:
        print("\nNotes:")
        print("\n".join(f"  {note}" for note in conv.notes))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
