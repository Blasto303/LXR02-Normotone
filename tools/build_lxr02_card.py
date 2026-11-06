#!/usr/bin/env python3
"""Assemble a ready-to-copy SD card tree for an LXR-02 owner switching to Helicase.

The stock card content is kept as it is, so the stock firmware still finds its
projects after a switch back. Added beside it: the Helicase image (the only
.img at the root), the repository SD_CARD content, and the stock projects
converted by convert_lxr02_project.py (Kits and Banks).

    python3 tools/build_lxr02_card.py <stock card copy> <output dir>
"""

from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
IGNORED = shutil.ignore_patterns("._*", ".DS_Store", ".Spotlight-V100", ".Trashes", ".fseventsd")


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    backup, out = Path(sys.argv[1]), Path(sys.argv[2])
    if out.exists():
        shutil.rmtree(out)
    shutil.copytree(backup, out, ignore=IGNORED)

    # Stock images move out of the root so the bootloader finds only Helicase.
    for image in out.glob("*.img"):
        stock_dir = out / "firmware_origine"
        stock_dir.mkdir(exist_ok=True)
        shutil.move(str(image), stock_dir / image.name)

    # FAT is case-insensitive: Helicase 'samples' is the stock 'SAMPLES'
    # folder, which stays untouched.
    existing = {path.name.lower(): path for path in out.iterdir()}
    for src in (ROOT / "SD_CARD").iterdir():
        if src.name.lower() == "samples":
            continue
        dst = existing.get(src.name.lower(), out / src.name)
        if src.is_dir():
            shutil.copytree(src, dst, dirs_exist_ok=True, ignore=IGNORED)
        elif not dst.exists():
            shutil.copy2(src, dst)
    shutil.copy2(ROOT / "build" / "LXRV2_lxr02.img", out / "LXRV2_lxr02.img")

    subprocess.run(
        [sys.executable, "-I", str(ROOT / "tools" / "convert_lxr02_project.py"), str(backup), "--out", str(out)],
        check=True,
    )
    # Directory indexes are rebuilt by the firmware at boot.
    for index in (out / "Kit" / ".hcindex", out / "Bank" / ".hcindex"):
        index.unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
