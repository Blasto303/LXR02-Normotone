# S080 Inter-Session Working Notes — carry-over after the Session 080 close

Last updated: 2026-10-11 (Session 080 closure; for Session 081). This file
was the S078 → S079 and S079 → S080 carry-over and has been rewritten in
place again.

The full Session 080 record is
`knowledge_files/log_archive/080_SESSION_HANDOFF_LOG.md`. Engine authority:
`PATTERN_DYNAMIC_STACK.md` §6.5 (looper), §6.6 (scene change timing, queue,
chaining). PERF gestures, LEDs, pages and the scene-change executor:
`MODULE_INTERCHANGE_SPEC.md` "PERF performance surface". Step specials
(S079): `PATTERN_DYNAMIC_STACK.md` §2–§7.

## Build metrics at S080 closure

| Metric | Value |
|---|---|
| text / data / bss (DEV config) | 556,008 / 412 / 427,976 |
| Flash payload | 556,420 B of 753,664 B; **headroom 197,244 B** |
| BSS growth from S079 | +200 B (looper 67 B, scene change/chain 12 B, PERF UI 25 B, `scenes` +64 B, Pattern regions +17 B, alignment) |
| Branch | `dev-ph6-cleanup`; code at `4869a98`; close-out docs uncommitted |
| Production build (`DEV_MODE_LOGGING 0`) | last measured at S075 F1: `text=516,688 data=408 bss=409,840`; re-measure |

## Carry-over from Session 080

1. **Hardware testing (S080):** the final P2 build was checked informally
   ("seems ok so far"); only the Step 14 follow-up rows are formally PASS.
   Run `080_SESSION_HANDOFF_LOG.md` §12 row by row — especially the looper
   sync rows (T7.x, T8.x, T14.x), chaining (T2.x), queue actions (T15.x) and
   the fast blink (T10.x).
2. **Deferred S080 features:** reload from source for queue actions
   `rld`/`r+r` (currently mute + morph (+ reset) only; needs a
   performance-safe Scene Load from the queue executor: HCNAMES Scene row
   source slot → `filesystem_requestLoadSceneForScenes()`); live record on
   RECORDING_PAGE sub-pages 2+ (and its REC-LED / REC-exit exceptions); live
   recording of overlay triggers; `ens`/`enl` track-length-aware boundaries
   (scd and qcd lists approximate one bar); `fls` variable flam spacing (uses
   1/32 like `flm`); edit lock during scene changeover (plan §5.3).
3. **Accepted limits (S080):** looper rewind/projection use the current
   effective scale for the whole span (a Morph change mid-loop is
   approximated); a scene change does not re-check Loopable flags of tracks
   already looping; transport start with SELECT held does not auto-engage;
   SHIFT+REC restores the cursor to cell 0; chain `>NN` history updates only
   on real changes; an absent chain target means no change; the overlay
   repeat timer is a single 1 ms foreground slot (one repeating track); a
   VOICE pressed under an active overlay does not join the looper hold set.
4. **Settings files (S080):** P1-era settings.cfg (`pcr=` line,
   `lines=25`) loads fine; qac 3/4 now read as `res`/`u+r`, qcd 8/9 as
   `5b`/`6b` (no migration, user). No card conversion needed for S080 (PAT4
   v2 and the AutoSave format are unchanged).
5. **Hardware testing (S079, carried):** P2 checklist (4 items) not yet run:
   (1) step re-press toggle, (2) `>` marker + scroll transitions, (3)
   rhythm presentation `off → 1-2 … 4+5`, (4) roll fill duck (fast fade,
   decay unchanged). P1 addendum fix 3 (7mu/!7m) also needs on-device
   confirmation. Note: S080 shifted the condition indices (`lop`/`!lp` at
   23/24; mute groups now 27..33 / 34..40) and fixed the S079 mute-label
   order.
6. **Decision (carried from S078):** the STEP-page deferred-marker retry gap
   (`menu_serviceRuntimeWidgets()` retry excludes SEQ_PAGE; fix: add
   `|| menu_activePage == SEQ_PAGE`). Offered, not applied.
7. **Accepted limits (S079):** special-flags byte bits 0–6 all assigned;
   only bit 7 reserved. Pool capacity under extreme use (7 specials × 128
   steps × 7 tracks = 8,960 B > 8,192 B pool). Roll timing quantization at
   64th notes (1.5 PPQ). Combined shuffle + micro-timing can drop deferred
   trigger. No retrigger cap on slow tracks.
8. **Accepted limits (S078, carried):** track length 128 cannot be step-
   automated (7-bit); a queued patSvc removal can leave a stale STEP name
   underline until the next restart; clearing automation does not undo the
   last applied value (sticky until transport stop / Pattern restore); old
   PAT4 scale bytes play fast until converted (`tools/convert_scene_scale.py`).
9. **Card preparation (carried):** SD cards must be converted with
   `tools/convert_pattern_specials.py` (v1→v2) before use with S079+
   firmware. Delete `.hcprms1`/`.hcprms2` after flashing S078+ firmware on
   a pre-S078 card (AutoSave Scene cells 51..71 are not migrated).
10. **Open defects carried (not fixed):** Pattern Load fan-out `memcpy` can
    tear one playback tick; O1 Settings Load per-voice Morph equalisation;
    F4 AutoSave trace ring drops lifecycle records during dirty bursts;
    underline limitations (live erase race, deferred clear race); S077 P2
    limitation: Scene-namespace LFO/velocity target tokens resolve through
    the active Scene in InstrumentManager.
11. **Behaviours to remember (accepted):** Effects saved at pan 64 show `1`;
    MIDI-entered values reach the LCD only at the next repaint; copy/clear
    limits in `COPYCLEAR_UTILITIES.md` §18.
12. **S074 items still open:** the unexplained boot timeout; the `cpu` widget
    with `cmp` on; CrumpBit minimum-share run; BC11; saturator α 0.35; stale
    comments (`BusCompressor.h` "24 B", `BusCompressor.c` "+0.45 %",
    `CrumpBitEffect.h` "73,632 B", the S074 C4 comment in `menu.c`, `main.c`
    about 532).
13. **Stale tools/small debt:** `decode_devlogs.py` misreads `pattrace.bin`;
    Makefile echoed comments and bare-`make` default goal; duplicated comment
    lines in `mixer.c` and `ResonantFilter.c`; `presetManager.c` comment
    indentation; unreachable Scene Save phases 33–36;
    `AUTOSAVE_TRACE_RECORD_COUNT` still at the temporary 2,048.
14. **Deferred features (carried):** `/Effect/` browser and Load/Save item
    (A35); MIDI mapping (A20); live record of FX moves (A22) and of
    automation; roll overhaul / manual roll trigger; Patgen/Euklid revert;
    triplet `12a/12b` scale mode (SCOPING §4.10); one-shot LFOs; external
    MIDI sequencing tracks. (The looper, SCOPING §6.6, is done in S080.)
15. **Still open from earlier sessions:** Phase 5 hardware acceptance for
    S072 Steps 6–10; slow Load type switching; D-C1 (boot image check); LFO
    noise range (suspected); optional AutoSave old-format mask guard;
    filesystem budget primitive extraction.
16. **Retest rows still blank:** 3.10/3.14/3.15 (S077 P4 Scene morph
    fan-out), C1–C6. S077 P5 (bar-to-step) and P6 (PERF morph underline)
    never had checklist rows (P6's PERF cells moved in S080 A3).
17. **Possible follow-ups the user may want (carried from S078):** move
    `len`/`scl`/`shf` to category `Track` (three `valueNames` edits); a
    STEP Morph latch would need a gesture other than SHIFT+MODE STEP (the
    SOM entry).
18. **Disposable:** the root `S080_*.md` documents (P1/P2 plans and
    schedules); earlier `S079_*`, `S078_*`, `S077_*`, `S076_*` and `S075_*`
    root documents if still present.

## Next session recommended goal

1. Run the S080 hardware matrix row by row; run the S079 P2 checklist.
2. Decide reload-from-source for `rld`/`r+r` and the live-record scope for
   RECORDING_PAGE (next feature work named in the S080 plan).
3. Decide the STEP marker retry one-liner (carried from S078).
4. Clear the remaining retest rows (3.10/3.14/3.15, C1–C6).
5. Re-measure the production build.
