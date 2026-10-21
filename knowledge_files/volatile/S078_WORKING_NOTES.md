# S082 Inter-Session Working Notes — carry-over after the Session 082 close

Last updated: 2026-10-21 (Session 082 closure, then the late Session 081
close-out; for Session 083). This file was the S078 → S079, S079 → S080 and
S080 → S081 carry-over and has been rewritten in place again. Session 081 was
closed out after S082; its carry-over is the section "Carry-over from Session
081" below (item 9).

The full Session 082 record is
`knowledge_files/log_archive/082_SESSION_HANDOFF_LOG.md`. Authorities:
- `FILESYSTEM_SPEC.md` "Current Bank behavior" (Bank Load/Save, preview, HCNAMES tokens, background and progressive load);
- `MODULE_INTERCHANGE_SPEC.md` "Background Bank load (S082)";
- `BANK_PRESET_ARCHITECTURE.md` (availability, edit masks);
- `AUTOSAVE.md` (abort allow-list, range marking);
- `DEV_MODES.md` (`b`/`h`/`Q` 0x08, stall site 10).

## Build metrics at S082 closure

| Metric | Value |
|---|---|
| text / data / bss (DEV config) | 598,120 / 420 / 430,928 |
| Flash payload | 598,540 B of 753,664 B; **headroom 155,124 B** |
| S082 SRAM | Scene 17 1,658 B; P0 16 B; P1 93 B (91 BSS + 2 `.data`); P3 4 B; DEV-only diagnostics |
| Branch | `dev-ph6-midi`; code at `8a83df0` (P3), `b25ffbe` (P0–P2); close-out docs uncommitted |
| Production build (`DEV_MODE_LOGGING 0`) | last measured at S075 F1: `text=516,688 data=408 bss=409,840`; re-measure |

## Next session: S083 MIDI

- Plan: `S083_P1_MIDI_IMPLEMENTATION_PLAN.md` (rev 4 + the §13 S082 outcome
  notes); reference `MIDI_SPEC.md`.
- **Already built in S082:**
  - the background load API, the result byte, the MIDI deferral and the drop filter;
  - CC0 RX only needs to call `preset_requestBackgroundBankLoad(v, 0, 0, PRESET_BANK_ORIGIN_MIDI)`.
- **Keep:**
  - the drop filter runs after thru routing; under consume-then-route a filtered message counts as not consumed;
  - MIDI parameter writes must fan out through `bank_editTargetMask()`, so that they respect Scene 17 and load availability.
- **S083 Phase D (PC/CC0)** depends on the S082 P1/P3 hardware checks below.

## Carry-over from Session 082

1. **Hardware testing (S082):**
   - **P0:**
     - Bank round trips: 4 modes × {all, partial, none} × {empty, Kit-only, full} children;
     - check the active/viewed Scene, the emptied active Scene, the `.hcnames` rows (`-`, `bNNN`, `x`), reboot on both boot paths, and `tools/verify_bank_autosave.py`;
     - Bank page UI (LED matrix vs PERF, COPY tap/hold, labels, retention), Instrument `chg`, pot coalescing, BAR2.
   - **P0 fixes:**
     - stopped and Merge-keeping loads must not build Scene 17;
     - a 30 s all-pot spin during the preview and an AutoSave drain;
     - a truncated `.pat` in a Bank child.
   - **P1:**
     - name prefills and saves from 17;
     - rebuild scope and fit (T3 on Scene 5, forced non-fit, T6/T7);
     - MIDI deferral, drop and missing slot (needs S083 or a test hook);
     - loads on 17 go to the home Scene;
     - a load during a scalar drain in a wait phase and an action phase, during a Pattern drain in phases 7 and 4, and during an HCNAMES publication;
     - the A3 sequence (Kit Load into home, Kit Save from 17, exit, reboot with no AutoSave record; home must keep its Kit);
     - A8 (a stall in phase 2 ends FAILED after 20 s).
   - **P2:** confirmed by listening; record `audioCodec_underrunCount` across a 16-Scene Replace and a Merge.
   - **P3** (untested):
     - leave Load/Save at once;
     - PERF fill; switching 17 → A → B → A with no second activation and AutoSave idle;
     - fan-out links;
     - the frozen screen (1-based `NN.`, D7 on page, quiet off page);
     - queued/chain behaviour;
     - FOLLOW-off STEP drop;
     - copy/clear mid-load (`LOAD_PENDING`, names after the load);
     - stopped load locked;
     - MIDI CC0 unlock at phase 5;
     - card pull off page;
     - underrun count.
2. **Decide P3 A1/A2** (both one-liners, not applied):
   - **A1:** skip the whole queued action when the target is unavailable (`menu_serviceDeferredSceneChange()`, before `menu_applyQueuedSceneAction()`). Today a SHIFT+SEQ queue armed before a direct Merge load into a replaced Scene still clears mutes and writes Morph 0.
   - **A2:** repaint the Scene LEDs in the frozen-screen re-press guard of `menu_switchPage()`.
   - **A3:** SHIFT+REC opened from Load/Save mode shows no W1 blink. Leave it unless the user asks.
3. **F5 Scene 2 Case-3 invalidation:** root cause open. Capture protocol:
   1. spin the Kit page with Scene 2 active;
   2. exit Load/Save and wait for the drain;
   3. copy the card before rebooting, then reboot and copy again;
   4. look for `h` 0x02/0x01/0x04 on rows 17..32 and `Q` 0x08.
4. **First-boot freeze** (not reproduced): if it recurs, capture with `DEV_LOGGING_IWDG 1` (§3.5 of the log).
5. **Not built:**
   - P2-4, a DEV self-check comparing cached Scene indices with the active Scene at the switch and at the end;
   - P2-5, runtime Bank Load trace records (request, switch, per-child commit, completion, underrun count).

   The trace flush is suppressed during a command, so power-off before the next flush loses a load's records.
6. **Optional, only if asked:** run `FS_FAST_DRAIN_POLL_PASSES` inside the idle window. The user declined further load speed-ups (W5).
7. **Card preparation:** delete `/.hcnames`, `.hcnamtmp`, `.hcprms1` and `.hcprms2` after flashing S082+ firmware on an older card.
8. **Disposable:** the eight root `S082_*.md` documents and `SD_CARD_082_P0/`, `SD_CARD_082_P1/`.
9. **Session 081 close-out: done** (2026-10-21): `081_SESSION_HANDOFF_LOG.md`, the index entry, the spec updates and `MEMORY.md`. Its open items are in the next section.

## Carry-over from Session 081 (live record, ramps, loop while record, bar pulse)

Record: `081_SESSION_HANDOFF_LOG.md`. Code `2a0fd86` (unchanged by S082 for
LiveRecord). Authorities: `PATTERN_DYNAMIC_STACK.md` §6.1–§6.2b, §6.5, §6.7,
§12; `EFFECTS_BUS_REFERENCE.md` §6/§10/§11; `MODULE_INTERCHANGE_SPEC.md`
"Core/Sequencer/LiveRecord (S081)".

9a. **Hardware testing (S081, none run row by row):**
   - **Phase A behaviour first** (existing patterns sound different by
     design): step reset at the first trigger (delayed, rolled, carried,
     spilled steps), R12 menu-vs-automation per parameter class, R13 FX
     lock/overlay/menu incl. `sel`, roll rates by count (rolls were 4× fast),
     BAR overlay LED across modes, overlay switch while held.
   - **P1B F6 rows** (`081_SESSION_HANDOFF_LOG.md` §13.2) and **P1C C5 rows**
     1–10, plus the **C-1 boundary roll check** (rolls started/released on
     step boundaries at 1/32, 160 BPM: no overwritten step before the start,
     no gap before the end).
   - **P2 matrix** (log §6.8) including the **64-ramp CPU worst case** on the
     `cpu` widget (fallback: 16-per-pass round-robin); run
     `tools/convert_pattern_autosmooth.py --dry-run` on the card.
   - **D12 first-change checks** 1–6 (log §7.5).
   - **P3-C rows** 1–15 (log §8.7) and **G-1 checks** 1–4 (log §8.6).
   - **P4 rows** 1–10 (log §9.4), including minutes of playback across the
     65.5 s `time_sysTick` wrap.
   - F-Q: SHIFT Morph-view Effect/Scene edits count as menu edits — try.
9b. **Decisions owed:**
   - ~~P1 item 29~~ **decided 2026-10-21:** correct as implemented — with
     `pcr` off, Scene changes keep the tracks and the FX sequence aligned to
     the master clock.
   - **P3 A-1:** tapping the VOICE of a track already in the loop drops it on
     release (S080 D1). Wanted?
9c. **Open defects / limits (low):**
   - first ring entry after arming REC while running ends a stale step from
     an earlier take (harmless with first-change 1; with 0 a dirty value can
     land there);
   - a change made while a Pattern replacement is pending is lost;
   - P4 B-2 (stop + start inside one LED drain, external MIDI only);
   - ramp limits (one extra sample after a TIM3 transport reset; ramp ends a
     little early when live record suppresses B's trigger);
   - LiveRecord includes `menu.h` (recorded invariant deviation);
   - stale "held Morph latch" wording in two `EffectsManager.c` comments
     (~lines 1227, 1279).
9d. **Small debt:** add stage `W` (causes 1–5) to `tools/decode_devlogs.py`;
    re-measure the production build (S081 and S082 both unmeasured).
9e. **Deferred (MIDI rework, S083):** MIDI note timestamps and realtime
    queue-skip priority; CC → target mapping for `liveRec_paramIn()`; whether
    MIDI CC adopts R12 menu priority; global-channel CC1 must stay unrecorded
    (G-1). Also deferred: step record while stopped, recording FX-sequence
    locks, automation `hold` reconciliation (SCOPING §4.3a).
9f. **Disposable:** the ten root `S081_*.md` documents (superseded by the
    081 log; the user deletes them).

## Carried from Sessions 078–080 (unchanged unless noted)

10. **S080 hardware matrix** (`080_SESSION_HANDOFF_LOG.md` §12) row by row: looper sync (T7.x, T8.x, T14.x), chaining (T2.x), queue actions (T15.x), fast blink (T10.x). S082 changed the fast blink to two slots and suspends chains during Bank loads.
11. **Deferred S080 features:**
    - reload from source for `rld`/`r+r`;
    - ~~live recording of overlay triggers~~ (done in S081);
    - `ens`/`enl` track-length-aware boundaries;
    - `fls` variable flam spacing;
    - edit lock during scene changeover.
12. **Accepted limits:**
    - S080: looper projection uses the current scale for the whole span; the overlay repeat timer has a single slot; and so on.
    - S079: special-flags byte full; pool capacity under extreme use; roll quantization; shuffle + micro-timing.
    - S078: track length 128 cannot be automated; a queued removal can leave a stale underline; automation is sticky until stop (since S081 a menu edit of that track parameter clears the override, R12).
13. **S079 P2 checklist** (4 items) and the P1 addendum fix 3 (7mu/!7m) still need on-device confirmation.
14. **STEP-page deferred-marker retry gap** (`menu_serviceRuntimeWidgets()` retry excludes SEQ_PAGE): offered, not applied.
15. **Open defects carried:**
    - Pattern Load fan-out `memcpy` tear;
    - O1 Settings Load per-voice Morph equalisation;
    - F4 trace ring drops during dirty bursts;
    - underline races;
    - Scene-namespace LFO/velocity tokens resolve through the active Scene.
16. **S074 items:**
    - the unexplained boot timeout;
    - the `cpu` widget with `cmp` on;
    - the CrumpBit minimum-share run;
    - BC11;
    - the saturator α;
    - stale comments.
17. **Stale tools and small debt:**
    - `decode_devlogs.py` misreads `pattrace.bin`;
    - Makefile echo / bare-`make` default goal;
    - duplicated comments;
    - unreachable Scene Save phases 33–36;
    - `AUTOSAVE_TRACE_RECORD_COUNT` still at 2,048.
18. **Deferred features:**
    - `/Effect/` browser (A35);
    - live record of FX-sequence locks (automation and Effect-parameter recording done in S081);
    - the roll overhaul;
    - Patgen/Euklid revert;
    - triplet scale mode;
    - one-shot LFOs;
    - external MIDI sequencing tracks.

    MIDI mapping (A20) is now the S083 plan.
19. **Still open from earlier sessions:**
    - Phase 5 hardware acceptance for S072 Steps 6–10;
    - slow Load type switching;
    - D-C1;
    - the LFO noise range;
    - the optional AutoSave mask guard;
    - the budget primitive extraction;
    - retest rows 3.10/3.14/3.15 and C1–C6.

## Next session recommended goal

1. S083 MIDI, Phase A first (cleanup, consume-then-route), keeping the S082
   drop filter and deferral intact.
2. In parallel, run the S082 P1/P3 hardware checks (item 1) before S083
   Phase D.
3. Decide P3 A1/A2.
4. Close out S081 when the user asks.
