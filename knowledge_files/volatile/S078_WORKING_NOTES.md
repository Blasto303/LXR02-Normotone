# S079 Inter-Session Working Notes — carry-over after the Session 079 close

Last updated: 2026-10-10 (Session 079 closure; for Session 080). This file
was the S078 → S079 carry-over and has been rewritten in place.

The full Session 079 record is
`knowledge_files/log_archive/079_SESSION_HANDOFF_LOG.md`. Step specials
authority: `PATTERN_DYNAMIC_STACK.md` (§2 pool block, §3 swap block, §4
block format, §6 sequencer integration, §7 PAT4 format). Roll fill duck:
`MODULE_INTERCHANGE_SPEC.md` (S079 additions).

## Build metrics at S079 closure

| Metric | Value |
|---|---|
| text / data / bss (DEV config) | 544,072 / 408 / 427,776 |
| Flash payload | 544,480 B of 753,664 B; **headroom 209,184 B** |
| BSS growth from S078 | +160 B (P1 +80 B: ~67 B sequencer condition/roll ISR state + alignment; P2 +80 B: InstrumentManager gate arrays 54 B padded to 56 B + sequencer duck arrays 21 B padded to 24 B) |
| Branch | `dev-ph6-cleanup`, HEAD uncommitted |
| Production build (`DEV_MODE_LOGGING 0`) | last measured at S075 F1: `text=516,688 data=408 bss=409,840`; re-measure |

## Carry-over from Session 079

1. **Hardware testing (S079):** P2 checklist (4 items) not yet run:
   (1) step re-press toggle, (2) `>` marker + scroll transitions, (3)
   rhythm presentation `off → 1-2 … 4+5`, (4) roll fill duck (fast fade,
   decay unchanged). P1 addendum fix 3 (7mu/!7m) also needs on-device
   confirmation.
2. **Decision (carried from S078):** the STEP-page deferred-marker retry gap
   (`menu_serviceRuntimeWidgets()` retry excludes SEQ_PAGE; fix: add
   `|| menu_activePage == SEQ_PAGE`). Offered, not applied.
3. **Accepted limits (S079):** special-flags byte bits 0–6 all assigned;
   only bit 7 reserved. Pool capacity under extreme use (7 specials × 128
   steps × 7 tracks = 8,960 B > 8,192 B pool). Roll timing quantization at
   64th notes (1.5 PPQ). Combined shuffle + micro-timing can drop deferred
   trigger. No retrigger cap on slow tracks.
4. **Accepted limits (S078, carried):** track length 128 cannot be step-
   automated (7-bit); a queued patSvc removal can leave a stale STEP name
   underline until the next restart; clearing automation does not undo the
   last applied value (sticky until transport stop / Pattern restore); old
   PAT4 scale bytes play fast until converted (`tools/convert_scene_scale.py`).
5. **Card preparation:** SD cards must be converted with
   `tools/convert_pattern_specials.py` (v1→v2) before use with S079+
   firmware. Also delete `.hcprms1`/`.hcprms2` after flashing S078+ firmware
   on an older card (AutoSave Scene cells 51..71 are not migrated).
6. **Open defects carried (not fixed):** Pattern Load fan-out `memcpy` can
   tear one playback tick; O1 Settings Load per-voice Morph equalisation;
   F4 AutoSave trace ring drops lifecycle records during dirty bursts;
   underline limitations (live erase race, deferred clear race); S077 P2
   limitation: Scene-namespace LFO/velocity target tokens resolve through
   the active Scene in InstrumentManager.
7. **Behaviours to remember (accepted):** Effects saved at pan 64 show `1`;
   MIDI-entered values reach the LCD only at the next repaint; copy/clear
   limits in `COPYCLEAR_UTILITIES.md` §18.
8. **S074 items still open:** the unexplained boot timeout; the `cpu` widget
   with `cmp` on; CrumpBit minimum-share run; BC11; saturator α 0.35; stale
   comments (`BusCompressor.h` "24 B", `BusCompressor.c` "+0.45 %",
   `CrumpBitEffect.h` "73,632 B", the S074 C4 comment in `menu.c`, `main.c`
   about 532).
9. **Stale tools/small debt:** `decode_devlogs.py` misreads `pattrace.bin`;
   Makefile echoed comments and bare-`make` default goal; duplicated comment
   lines in `mixer.c` and `ResonantFilter.c`; `presetManager.c` comment
   indentation; unreachable Scene Save phases 33–36;
   `AUTOSAVE_TRACE_RECORD_COUNT` still at the temporary 2,048.
10. **Deferred features:** `/Effect/` browser and Load/Save item (A35); MIDI
    mapping (A20); live record of FX moves (A22) and of automation; roll
    overhaul (live performance roll is now independent of per-step roll);
    Patgen/Euklid revert; triplet `12a/12b` scale mode (SCOPING §4.10);
    one-shot LFOs; looper; external MIDI sequencing tracks.
11. **Still open from earlier sessions:** Phase 5 hardware acceptance for
    S072 Steps 6–10; slow Load type switching; D-C1 (boot image check); LFO
    noise range (suspected); optional AutoSave old-format mask guard;
    filesystem budget primitive extraction.
12. **Retest rows still blank:** 3.10/3.14/3.15 (S077 P4 Scene morph
    fan-out), C1–C6. S077 P5 (bar-to-step) and P6 (PERF morph underline)
    never had checklist rows.
13. **Possible follow-ups the user may want (carried from S078):** move
    `len`/`scl`/`shf` to category `Track` (three `valueNames` edits); a
    STEP Morph latch would need a gesture other than SHIFT+MODE STEP (the
    SOM entry).
14. **Disposable:** the root `S079_*.md` documents (P1/P2 plans and
    schedules); earlier `S078_*`, `S077_*`, `S076_*` and `S075_*` root
    documents if still present.

## Next session recommended goal

1. Hardware test P2 (4-item checklist) and P1 addendum fix 3.
2. Decide the STEP marker retry one-liner (carried from S078).
3. Clear the remaining retest rows (3.10/3.14/3.15, C1–C6) and give S077
   P5/P6 a quick check.
4. Re-measure the production build.
5. Agree the next feature/fix target (SCOPING §6.0 remainder: live record
   of automation, Patgen/Euklid revert; or Phase 6 MIDI work).
