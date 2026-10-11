# Session 080 — Handoff Log

```
DATE: 2026-10-10 / 2026-10-11
SESSION GOAL: PERF-mode performance surface: PERF page re-order and entry
              fix, loop trigger conditions, SELECT-button looper, BAR
              VOICE-trigger overlay, SHIFT+REC settings menu, scene change
              division / queued scene change / queue actions, scene
              chaining; then remediation of the hardware-test defects.
COMPLETED: P1 plan and implementation of phases A, B, C, E, F (D partly;
           G and H deferred). P1 hardware test produced 11 defects + a new
           parameter (P2 plan). A first P2 implementation was hardware
           tested, failed in four ways, and was rolled back. Root causes
           traced; P2 plan amended (defects 12-15), full P2 implementation
           schedule written, implemented (incl. Phase G scene chaining) and
           committed as 4869a98. Follow-up (global page layout, qcd list)
           implemented and tested.
VERIFIED ON HARDWARE:
  - P1 build: tested by the user -> defect list (P2 plan, §5).
  - First P2 attempt: tested -> failed (looper out of sync on individual
    tracks, queued-scene blink not continuous, chain menu snapping back,
    scene restart from bar 0); rolled back.
  - Final P2 implementation: "seems ok so far" (user, 2026-10-11).
  - Step 14 follow-up (compressor page, qcd list): PASS (user, 2026-10-11).
  - The P2 test matrix (§12) was not reported row by row.

CHANGES THIS SESSION (detail in §9):
- Core/Sequencer/sequencer.c/.h: lop/!lp conditions; master-clock-anchored
  looper engine; scene change division / queue state, ISR boundary detection,
  queued-division list; scene chain engine (evaluation, resolve, stp freeze,
  repeat); loopable mask; reset/rebase hooks.
- Core/Hardware/frontPanel/buttonHandler.c/.h: PERF entry cursor fix; looper
  SELECT/VOICE/BAR gestures, sustain and drop rules; BAR overlay toggle and
  VOICE trigger dispatch with 1 ms repeat timer; SHIFT+REC latched menu;
  SHIFT+PLAY+SEQ chain chord; SHIFT+SEQ queue; latched-menu SHIFT rules;
  MODE/overflow cleanup; clearAllMutes, loopSelectLed.
- Core/Hardware/frontPanel/ledHandler.c/.h: LED_LAYER_FAST queued-scene fast
  blink; led_isBlinkLed(); led_initPerformanceLeds() removed.
- Core/Menu/menu.c/.h, menuPages.h, MenuText.h: PERF page split, marker
  remap, cross-sub-page encoder step (PERF, RECORDING, MENU_MIDI pages);
  RECORDING_PAGE repurposed; S080 static-param names/clamps; scene-change
  executor, queue-action decode, deferred-change service, queued-LED owner;
  PERF Scene LED refresh rewrite; chain editor on PATTERN_SETTINGS_PAGE;
  loopable cell; global settings page re-layout (scene globals sub-page 2,
  compressor sub-page 3); menu_syncPerfSelectLed() from looper state.
- Core/Bank/Scene/SceneData.c/.h: chain_condition[2]/chain_next[2] +
  accessors + commitSettings.
- Core/Bank/Scene/Autosave.c/.h: Scene cells 72..75 (chain); restore
  `return` -> `continue` fix.
- Core/Bank/Scene/Pattern/PatternData.c/.h: track_loop_exclude + accessors.
- Core/Bank/Scene/Preset/ParameterArray.h: PAR_PERF_*, PAR_SCENE_*,
  PAR_CHAIN_*, PAR_TRACK_LOOPABLE (PAR_SCENE_PC_RESET added in P1, removed
  in P2).
- Core/Hardware/SD/filesystem.c: settings.cfg keys (perf_*, scd, qcd, qac;
  lines=24) + sanitizing; sceneset chain lines; PAT4 track byte 4.
- Core/Hardware/SD/storageTypes.c: sceneset chain keys.
- Core/Menu/CopyClear/copyClearService.c: track copy/clear carry loopable.

KNOWN ISSUES INTRODUCED: see §15 (reload in rld/r+r deferred; ens/enl
  approximated; fls fixed spacing; projection approximates mid-span Morph
  scale changes; transport start with SELECT held does not auto-engage).
KNOWN ISSUES RESOLVED: S079 mute-condition label order (labels one group out
  of step with the dispatch); AutoSave Scene restore aborted at the first
  zero track-Morph-length cell (pre-S078 records skipped every later cell).

NEXT SESSION RECOMMENDED GOAL: run the §12 hardware matrix row by row;
  decide on reload-from-source (rld/r+r) and live record (RECORDING_PAGE
  sub-pages 2+ reserved); carry-overs in §15.
BLOCKERS: none.

CRITICAL REMINDERS FOR NEXT SESSION:
- The looper is master-clock anchored: never reintroduce a shared tick
  counter or snapshot-at-press without anchoring (P1/P2 sync failures, §6).
- Every looper publish runs under PRIMASK; the wrap runs BEFORE the DDA.
- One PC reset: PAR_BAR_RESET_MODE (pcr) for direct + chain changes; queued
  changes use the qac reset bit and ignore pcr.
- PERF SEQ LEDs: the queued Scene uses LED_LAYER_FAST; the refresh never
  restarts a running blink (led_isBlinkLed()).
- Latched menus: SHIFT release is ignored while open; SHIFT press closes
  and its release is consumed.
- The Scene-owned compressor page (MENU_GLOBAL_SCENE_SUBPAGE = 3) stays
  last; new global pages go before it.
```

---

## 1. Session overview and timeline

Session 080 ran on `dev-ph6-cleanup`.

| Step | Commit / state | What happened |
|------|----------------|---------------|
| Start | `efcdbe1` (S079 close) | text 544,072, data 408, bss 427,776 |
| P1 plan | `a0a2d64`..`8c5946d` | `S080_P1_PERF_MODE_UPDATES.md` (plan, 7 features) and `S080_P1_IMPLEMENTATION.md` (phases A–H, per-change schedule with comment blocks) |
| P1 implementation | `8c5946d` | phases A, B, C, E, F built clean; D1 only; G, H deferred; post-implementation audit appended to the P1 schedule |
| P1 hardware test | — | 11 defects + 1 new parameter → `S080_P2_PERF_EXPANSION_DEFECTS.md` |
| P2 plan | `e621884` | defects 1–11, 8a; implementation schedule (first version) |
| P2 attempt 1 | rolled back | implemented, hardware tested, failed: looper out of sync on individual tracks, queued-scene blink not continuous at double rate, chain menu snapped back to PERF on button release, scene change restarted from bar 0, chain-editor LED killed. Plan amended (defects 12–13, amended 7/10); all P2 code rolled back |
| Review | — | code deep-dive of the three failures (§6); spec correction for loop start on scene change (Defect 14, user); queued-change PC reset as qac options (Defect 15, user) |
| P2 schedule | — | `S080_P2_IMPLEMENTATION.md` Steps 0–13 rewritten from the code (decisions D1–D17, §7) |
| P2 implementation | `4869a98` | implemented as scheduled (incl. Phase G); "seems ok so far" on hardware |
| Follow-up | `4869a98` | Step 14: compressor alone on global sub-page 3 with `^`/`+` cues; `qcd` 5b..16b; PASS |
| Close | uncommitted docs | text 556,008, data 412, bss 427,976; payload 556,420 B of 753,664 (headroom 197,244 B) |

All S080 features are in PERF mode or support it. No DSP, mixer or Effect
code changed. The S080 task documents are superseded by this log and the
specification updates (§13); the user deletes them.

---

## 2. Feature specification (final, as built)

This section is the complete behavioural specification after P2 and the
follow-up. It supersedes the P1 plan where they differ (differences are
marked).

### 2.1 PERF page layout and entry

`PERFORMANCE_PAGE` has two populated sub-pages (S080 A1):

| Sub-page | Cell 0 | Cell 1 | Cell 2 | Cell 3 | Cells 4–7 |
|----------|--------|--------|--------|--------|-----------|
| 0 | `mrp` (overall Morph) | `fxm` (Effect Morph) | `1VM` | `2VM` | empty |
| 1 | `3VM` | `4VM` | `5VM` | `6VM` | empty |

- PERF entry always lands on sub-page 0, cell 0 (`mrp`). The old bug (entry
  on `4VM`) came from `menuIndex` keeping a column ≥ 4 from the previous
  page; `menu_resetActiveParameter()` after `menu_switchSubPage(0)` fixes it
  (A2), also on the PERF SHIFT-release path.
- The encoder crosses between sub-pages: past the last populated column it
  moves to the first populated column of the next populated sub-page, and
  back; non-cyclic (cross-sub-page step in `menu_moveToMenuItem()`, used by
  PERFORMANCE_PAGE, RECORDING_PAGE and, since the follow-up, MENU_MIDI_PAGE).
- PERF Morph automation underline: `menu_applyPerfMarkers()` maps columns
  by sub-page — sub-page 0: col 1 = `fxm` (`VA_PERF_MORPH_EFFECT_BIT` + FX
  sequence lock), col 2 = 1VM (bit 0), col 3 = 2VM (bit 1); sub-page 1:
  cols 0–3 = 3VM–6VM (bits 2–5). The scanner (`va_scanService()`) is
  parameter-based, so it needed no change (A4 no-op).
- **SELECT LEDs in PERF (Defect 4, final):** dark unless the looper is held
  or engaged, then the active loop division's SELECT LED is lit. The P1 A5
  sub-page indication on SELECT1/SELECT2 was removed.

### 2.2 Trigger conditions `lop` / `!lp` (Phase B)

The step condition table grew 39 → 41 entries; `lop` (23) fires only while
the looper is engaged (`seq_loopActive`), `!lp` (24) only while it is not.
Later conditions shifted by two: `lst` 25, `!ls` 26, `1mu..7mu` 27..33,
`!1m..!7m` 34..40 (dispatch `(cond − 27) % 7`, boundary `cond <= 33`; the
self-mute bypass in `seq_advanceTrackStep()` uses the same ranges). No PAT4
migration (user: backward compatibility not a concern; patterns using
conditions ≥ 23 shift meaning). The 6-bit condition field still has room.

`conditionNames[]` is indexed directly by stored value, so it must stay in
numeric order. S079 had emitted the mute labels as `1mu..6mu, !1m..!6m,
7mu, !7m`, one group out of step with the dispatch; S080 B1 corrected the
order.

"Engaged" = `seq_loopActive`, which is 1 while any looper SELECT, looped
VOICE or BAR sustains the loop, even when the looped track set is empty.

### 2.3 Looper (Phase C/D, rewritten in P2)

**Divisions** (SELECT button → loop length on the master clock, 96 PPQ):

| SELECT | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|--------|---|---|---|---|---|---|---|---|
| Division | 1 bar | 1/2 bar | 1/4 bar | 1/8 bar | 1/16 | 1/32 | 1/64 | 1/128 |
| Ticks | 384 | 192 | 96 | 48 | 24 | 12 | 6 | 3 |

- Last pressed SELECT wins; when the owner is released with others still
  held, the highest held index takes over (press order is not stored).
- **Modifiers** (while the loop is engaged): hold BAR2 = dot (×3/2), hold
  BAR1 = triplet (×2/3); momentary; releasing one falls back to the other if
  still held. BAR LEDs: both forced off while the looper runs; the held
  modifier's LED lights while held; overlay LEDs are restored when the loop
  ends.
- **Track set:** SELECT with no VOICE held loops all seven tracks; VOICE
  then SELECT loops the held tracks. Both are filtered by the per-track
  `lop` (Loopable) flag of each track's played Scene (Defect 8a); excluded
  tracks are silently left out.
- **Swap (SELECT held, VOICE pressed):** the loop moves to the set of
  currently held VOICEs; tracks leaving resume where they would have been,
  tracks staying keep their loop start, new tracks get a fresh loop start at
  this press (Defect 7).
- **Sustain (Defect 8):** the loop stays engaged while any looper SELECT,
  any VOICE of a still-looped track, or any BAR button is held. The SELECT
  LED keeps showing the division while VOICE/BAR sustain it.
- **Drop (Defect 8 follow-up, D1):** releasing a looped track's VOICE drops
  that track at once (it resumes where it would have been); the loop ends
  when nothing sustains it. With SELECT still held and all looped VOICEs
  released, the loop stays engaged with no tracks (`lop` still true).
- **Loop start (plan §3.4, D4):** qnt off → the tick at which the fastest
  track of the engage/add event last stepped (so the first repeat plays the
  step that was sounding); qnt 8/16/32/64 → the last quantise boundary on the
  master clock (48/24/12/6 ticks). If that boundary is more than one loop
  length back, the track jumps back at the next wrap (accepted, plan note).
- **Division change** never moves a loop start; wraps stay on each track's
  anchor grid.
- **Scene change while looping (Defect 14, user spec correction):** each
  looped track's loop start stays at the same master-clock position; the
  incoming Scene's track is placed there with its own scale/length (example:
  1/16 loop start at step 5 = tick 96 → incoming 1/8 track → step 3; tick 120
  → mid-step 3). Wrap timing is unchanged. Applies to direct, queued and
  chain-fired changes. A **resetting** change (pcr on for direct/chain, qac
  reset for queued) moves every loop start to tick 0, the beginning of the
  incoming Scene (user decision). The P1 plan text "loop start resets to the
  very beginning of the new scene" was a misstatement.
- **SEQ while looping (Defect 9):** always a full scene change, never a
  per-track assignment; SHIFT+SEQ always queues a full change. Per-track
  scene changes (TRACK+SEQ) are therefore impossible during a loop.
- **Release:** every looped track resumes exactly where it would have been
  without the loop (projection, §3.1), independent of whether its position
  was the canonical master-clock position.
- **Recording:** engaging forces recording off and clears the REC LED; REC
  is ignored (for record toggle) while looping.
- **Effects and the FX sequence are never looped.**
- **Transport:** the looper engages only while the transport runs (a SELECT
  press while stopped only lights its LED); transport stop ends the loop.
  Starting the transport with SELECT still held does not auto-engage.
- **Leaving PERF** (MODE press) or an event-ring overflow force-releases the
  looper.
- Tracks 6 and 7 (HiHat pair) loop independently.

### 2.4 BAR `<` / `>` VOICE-trigger overlay (Phase E)

- In PERF, when the loop is not engaged, BAR1 toggles overlay `<`, BAR2
  toggles overlay `>`; only one is active; its BAR LED is lit. Runtime only;
  persists across Scene changes in a PERF session.
- With an overlay active and SHIFT not held, a VOICE press fires a trigger
  for that track at once; the release sends note-off (`voiceControl_noteOff`
  keeps MIDI-out correct). SHIFT+VOICE keeps the normal PERF mute behaviour.
  A VOICE pressed under the overlay does not join `perf_heldVoiceMask`, so
  VOICE+SELECT subsetting is not available while an overlay is active.
- Trigger types (`pb<` / `pb>`, values 0..11): `vmx` 127, `v10` 100, `v64`
  64, `v32` 32 (single hit); `flm` / `fls` flam = velocity 64 now, velocity
  100 one 1/32 later (`fls` uses the same 1/32 spacing; no per-track scale
  accessor, P1 simplification); held rolls `r/8` (eighth), `r16`, `r32`,
  `r64`, `r6t` (1/64 triplet), `128` at velocity 100, first hit at press,
  repeating while held.
- Note (`pn<` / `pn>`): 0 = `def` = the track's MIDI note in its played
  Scene (fallback `MIDI_DEFAULT_TRIGGER_NOTE`); 1..127 = that note.
- Repeat timing: one foreground 1 ms timer slot in `buttonHandler_tick()`
  (`bh_overlayInterval/Next/Count`), period derived from BPM
  (`buttonHandler_overlaySixteenthMs()`); only the most recent overlay track
  repeats. (Deviation from plan §9.3-5, which proposed DDA timing.)
- Overlay triggers are not recorded (plan §9.2-1).

### 2.5 SHIFT+REC settings menu (Phase E, Defect 3)

`RECORDING_PAGE` repurposed:

| Sub-page | Cells | Category |
|----------|-------|----------|
| 0 | `pb<` BtnStat<, `pn<` BtnNote<, `pb>` BtnStat>, `pn>` BtnNote> | Prfrmnc |
| 1 | `qnt` Quantize | (existing) |
| 2–7 | reserved for live-record options (next session) | — |

- **Entry:** SHIFT held + REC → saves page/sub-page, opens sub-page 0 cell
  0, SHIFT and REC LEDs lit.
- **Latched (Defect 3, D8):** releasing SHIFT (or any button) keeps it open;
  the SHIFT LED stays lit.
- **Exit:** a SHIFT press (its release is then consumed) or a REC press;
  restores the saved page/sub-page (cursor reset to cell 0, not the exact
  prior column — P1 limitation). A MODE press or event overflow also closes
  it first.
- Encoder crosses sub-pages (non-cyclic). Edits mark settings.cfg dirty
  (RECORDING_PAGE counts as a Global page for the debounce).
- The future live-record exception (REC LED flashing, REC exit not toggling
  record) is not implemented.

### 2.6 Global scene change settings (Phase F, Defects 5/6/15, follow-up)

Global settings page (MENU_MIDI_PAGE) layout after the follow-up:

| Sub-page | Screen 1 (cue) | Screen 2 (cue) |
|----------|----------------|----------------|
| 0 | `bpm syn qnt ch` (`^`) | `txf rxf mrt ssv` (`*`) |
| 1 | `pcr cki co1 flw` (`*`) | `oit cpu ats pts` (`*`) |
| 2 | `scd qcd qac` (`^` = Scene-settings page follows) | — |
| 3 (`MENU_GLOBAL_SCENE_SUBPAGE`) | `cmp cam ctm csc` (`+` = Scene-owned) | — |

- **`scd`** (ChngDiv, cat Scene, key `scd`, default `ins`): division at
  which a direct (SEQ) change happens: `ins 1/8 1/4 1/2 1b 2b 3b 4b ens enl`
  (0..9; units on the master 1/16 clock 0, 2, 4, 8, 16, 32, 48, 64; `ens` /
  `enl` approximated by one bar). A stopped transport always changes at once.
- **`qcd`** (QueDiv, key `qcd`, default `ins`): division for queued
  (SHIFT+SEQ) changes, own list since the follow-up: `ins 1/8 1/4 1/2 1b 2b
  3b 4b 5b 6b 7b 8b 9b 12b 15b 16b ens enl` (0..17; units 0, 2, 4, 8, 16, 32,
  48, 64, 80, 96, 112, 128, 144, 192, 240, 256; ens/enl one bar). Instant or
  stopped → fires at once (Defect 1).
- **`qac`** (QueActn, key `qac`, default `nrm`) — values 0..8 (Defect 15):

| Value | `qac` | Mute reset | Morph reset | PC reset | Reload |
|-------|-------|:---:|:---:|:---:|:---:|
| 0 | `nrm` | | | | |
| 1 | `mut` | ✓ | | | |
| 2 | `mrp` | | ✓ | | |
| 3 | `res` | | | ✓ | |
| 4 | `u+r` | ✓ | | ✓ | |
| 5 | `o+r` | | ✓ | ✓ | |
| 6 | `mmr` | ✓ | ✓ | ✓ | |
| 7 | `rld` | ✓ | ✓ | | ✓ (deferred) |
| 8 | `r+r` | ✓ | ✓ | ✓ | ✓ (deferred) |

  The P1 list was `nrm mut mor m+m rld`; `mor` was renamed `mrp`, `m+m` was
  removed (user). Mute reset unmutes the Sequencer and the VOICE LED mirror
  (Defect 11). Morph reset = `preset_morphScene(target, 0)` on the incoming
  Scene only (overall, six voice and Effect Morph; no edit-mask fan-out),
  applied before the change. Reload from source is **not implemented**:
  `rld`/`r+r` degrade to their mute/morph(/reset) parts (plan §9.1-8).
- **`pcr`** (PCReset, cat Sequencr, key `bar_reset_mode`, sub-page 1): the
  one PC reset (Defect 5; P1 had added a duplicate `PAR_SCENE_PC_RESET`).
  With pcr on, a **direct or chain-fired** change restarts every track and
  the master clock (`seq_resetToPatternStart()`); with it off, tracks
  realign to the running master clock. Queued changes ignore pcr. pcr still
  also drives the legacy `seq_resetBarOnPatternChange`.
- The compressor cells are Scene settings shown together on the last page
  with `+` (follow-up; Defect 6's interim layout put scd/qcd/qac and cmp on
  one screen).

### 2.7 Queued scene change (SHIFT+SEQ in PERF)

- Queues a change to that Scene at `qcd`; queuing again replaces the target;
  queuing the playing Scene still fires the queue action.
- Fires when due regardless of the current menu or mode (serviced from
  `buttonHandler_tick()`).
- Only a transport stop cancels it. A manual change does not.
- The queued Scene's SEQ LED flashes at double rate (125 ms half-period)
  continuously until the change fires (Defect 10), on its own LED layer
  (§3.4); shown only in PERF mode.
- Edit lock during changeover (plan §5.3) keeps the P1 behaviour.

### 2.8 Scene chaining (Phase G, Defects 2/13)

Per Scene, two (condition, next) pairs: `cn1 nx1 cn2 nx2` (cat Chain,
Conditn1/NxtScn1/Conditn2/NxtScn2; defaults `nul`/`slf` = no chaining).
Stored in `scene_settings_t`, saved in `sceneset.scg` and AutoSave; copies
and clears with the Scene; never fans out through the VOICE edit mask.

**Conditions (`cn1`/`cn2`, 0..78):**

| Values | Display | Fires |
|--------|---------|-------|
| 0 | `nul` | never |
| 1 | `xal` | every repetition end |
| 2 | `bal` | every bar end |
| 3..13 | `x01..x08 x16 x32 x64` | at the repetition end when the repetition count equals N |
| 14..24 | `b01..b08 b16 b32 b64` | at the bar end when the bar count equals N |
| 25..33 | `r1x..r9x` | 10..90 % at a repetition end |
| 34..42 | `r1b..r9b` | 10..90 % at a bar end |
| 43..46 | `1:4..4:4` | at a bar end when the master bar just completed is X of 4 |
| 47..54 | `1:8..8:8` | same, of 8 |
| 55..70 | `>01..>16` | at a repetition end when the Scene was entered from Scene NN |
| 71 / 72 | `fix` / `fib` | fill active, repetition / bar end |
| 73 / 74 | `!fx` / `!fb` | fill inactive, repetition / bar end |
| 75 / 76 | `lox` / `lob` | looper engaged, repetition / bar end |
| 77 / 78 | `!lx` / `!lb` | looper not engaged, repetition / bar end |

**Next scene (`nx1`/`nx2`, 0..43):** 0 `slf`, 1 `nxt` (wraps 16→1), 2 `prv`
(wraps 1→16), 3..18 `s01..s16`, 19..34 `r01..r16` (random among Scenes
1..NN), 35..38 `r+1..r+4` (random among the next N, excluding current),
39..42 `r-1..r-4` (previous N), 43 `stp`. A target not present in the Bank
means no change.

**Evaluation (TIM3, every master tick):**
- Bar end = master tick multiple of 384. Repetition end = the first tick at
  or after each multiple of the longest track's effective length × Q8.8 step
  interval (D10). Bar and repetition counts start at 0 when a Scene becomes
  active and count boundaries (saturating 255); `xNN`/`bNN` fire when equal
  (once per activation, D11).
- Weights: deterministic true = 100, probability = 10..90, else 0.
  Condition 1 has priority: w1 = 100 fires 1; two probabilities summing over
  100 are drawn proportionally (one always fires); otherwise one draw
  r in 0..99: r < w1 → 1, r < w1 + w2 → 2, else nothing (the Scene repeats).
- Suppressed while a direct, queued or chain change is already pending (a
  manual or queued switch cancels conditional changes).
- A fire executes at that boundary, not re-quantised by `scd` (D9). It uses
  pcr for the reset. A fire to the playing Scene (`slf`, or a resolved
  target equal to it) is a **repeat**: counters reset, `stp` cleared,
  once-mode and condition counters retriggered, tracks realigned (or
  restarted with pcr); no DSP re-apply (D13).
- **`stp`** freezes every track in place (transport keeps running; the DDA
  and loop wraps stop; deferred triggers and rolls finish). Counts continue.
  Any executed change or repeat clears it. Plan example `cn1 b03 → stp`,
  `cn2 b04 → slf` plays 3 bars, stops 1 bar, repeats.
- A manual change, a queued change, a Bank Load alignment and transport
  stop/start reset the chain state; `>NN` history (`seq_previousScene`) is
  set only on a real change and cleared on transport stop.

**Chain editor (SHIFT+PLAY+SEQ in PERF):**
- SHIFT held, PLAY pressed (consumed: never toggles transport in PERF; SHIFT+
  PLAY outside PERF is unchanged), then a SEQ press opens the editor for that
  present Scene on `PATTERN_SETTINGS_PAGE` sub-page 0 (`cn1 nx1 cn2 nx2`).
- Latched: all entry buttons can be released; SHIFT LED stays lit.
- A SEQ press while open switches the viewed Scene; the viewed Scene never
  follows a chain/queue change of the active Scene.
- The active Scene and the viewed Scene SEQ LEDs both blink at the standard
  rate regardless of transport (Defect 13); the queued Scene keeps its fast
  flash.
- Exit: SHIFT press (release consumed) → PERFORMANCE_PAGE sub-page 0; MODE
  press or event overflow also close it. PLAY without SHIFT keeps
  transport behaviour while open.

---

## 3. Engine and UI design (how it works)

### 3.1 Looper engine (`sequencer.c`)

State (67 B SRAM1, runtime-only): `seq_loopActive` (extern gate),
`seq_loopTrackMask`, `seq_loopDivision`, `seq_loopModifier`, and per track
`seq_loopAnchor[7]` (uint32 master tick, `seq_elapsedPpqTicks` units) plus a
snapshot `seq_loopStartStep/StartAccum/StartPlayState[7]` = the track's
step cursor, Q8.8 accumulator and play-state bits **just before** the anchor
tick's DDA advance.

- **Wrap** (`seq_loopProcessTick()`, TIM3, every tick, **before** the DDA
  advance): for each looped track, if `elapsed > anchor` and
  `(elapsed − anchor) % loopTicks == 0`, restore the snapshot. The DDA then
  replays the anchor tick's step, so the step sounding at the loop start is
  repeated.
- **Position maths** (`seq_loopShiftPosition()`, foreground, 64-bit): move a
  position image by ±N ticks: accumulator ± N·256, floor-divided by the
  interval into step advances, then the cursor walked per run mode (fwd/rev
  modulo length — from cursor −1 the first forward step is 0 and the first
  reverse step is L−1; pip walks its 2L cycle and sets the direction bit; rnd
  draws a new step; onc/1fr clamp and set/clear STOPPED). Used to rewind live
  state to the anchor (engage/add) and to project the snapshot to now
  (resume). Uses the current effective scale/length for the whole span.
- **Anchor** (`seq_loopEventAnchor()`): qnt → `now − now % q`; else
  `now − (accumulator_fastest >> 8)` (ticks since the fastest track's last
  advance; residual < 256 after an advance); 0 before the first scheduler
  tick.
- **Snapshot** (`seq_loopSnapshotTracks()`): one anchor per gesture; each
  track's live state rewound by `now − anchor + 1` ticks.
- **Resume** (`seq_loopResumeTrack()`): snapshot projected forward by
  `now − anchor + 1` → written to the live cursor/accumulator/play state,
  `seq_lastMasterStep`, chase LED.
- **API:** `seq_loopEngage(mask, div, mod)` (returns the mask actually
  looping; 0 and no engage when stopped; filters loopable; turns recording
  off; publishes gate last), `seq_loopUpdateMask(newMask)` (resume removed
  tracks inside the call before committing, snapshot added tracks with one
  fresh anchor, keep the rest), `seq_loopDisengage()`,
  `seq_loopUpdateDivision()` (single-byte stores, never moves anchors),
  `seq_loopableTrackMask()`. All foreground writers hold PRIMASK for the
  whole publish.
- **Scene change** (`seq_selectActivePattern()`,
  `seq_alignActivePatternToScene()`, `seq_chainRepeatSelf()`): under one
  PRIMASK section, realign every track and `seq_loopRefreshSceneSnapshots()`
  (keep anchors; snapshot = canonical pre-advance position of the incoming
  Scene at `anchor − 1` via the realign mapping; anchor 0 = seed: cursor −1,
  accumulator `interval − 256`).
- **Reset** (`seq_setStepIndexToStart()` → `seq_loopRebaseToStart()`): an
  engaged loop survives a cursor reset (pcr/qac reset change, external reset,
  boundary pattern commit): anchors 0, snapshots = seeded state. Transport
  stop clears `seq_loopActive` first, so a stop ends the loop.
- **Why anchors (P1/P2 failures, §6):** a shared tick counter wrapped at an
  arbitrary point when the length shrank, a per-track snapshot on a shared
  counter shifted added tracks off-grid, snapshot-at-press repeated the
  following step, an unmasked foreground snapshot could skew tracks by one
  tick, and master-clock realign on release only matches tracks that sit on
  their canonical position (not after Morph/automation scale changes,
  boundary commits or once modes).

### 3.2 Scene change pipeline

- Direct SEQ: `menu_perfModeSceneButtonPressed()` → `scd` instant or
  stopped → `menu_perfSceneChangeExecute(scene, pcr)`; else
  `seq_requestSceneChange(scene, scd)` (clears a stale chain marker).
- Queued SHIFT+SEQ: `seq_queueSceneChange(scene, qcd)` → instant/stopped
  raises `seq_sceneFireFlag` at once.
- ISR (`seq_handleMasterBoundary()`, each master 1/16): pending queue →
  `seq_queueChangeDivUnits(qcd)`, else direct → `seq_sceneChangeDivUnits(scd)`;
  `units != 0 && clock % units == 0` raises the fire flag. Chain fires raise
  it directly (§2.8).
- Foreground `menu_serviceDeferredSceneChange()` (every
  `buttonHandler_tick()`): if fired, read target/queue/chain, clear pending;
  queued → `menu_applyQueuedSceneAction()` (mute/morph) then execute with
  the qac reset bit; chain to the playing Scene → `seq_chainRepeatSelf(pcr)`;
  else execute with pcr. Then `menu_syncQueuedSceneLed()`.
- `menu_perfSceneChangeExecute(scene, restart)`: scene_selectActive, edit
  mask, shown pattern, `seq_selectActivePattern()` (realign + loop
  re-snapshot + chain hooks), `seq_cancelPendingDirectChange()` (keeps a
  queued change; re-arms an instant queue), `seq_resetToPatternStart()` when
  `restart`, `preset_startDrumsetApply()`, Scene LED refresh, repaint.

### 3.3 Chain engine (`sequencer.c`)

`seq_chainServiceTick()` (TIM3, after the master boundary, before deferred
triggers) → `seq_chainAtRepetitionEnd()` (`((elapsed << 8) % longest) <
256`), counters, `seq_chainEvaluate()` → `seq_chainConditionWeight()` ×2 →
draw → `seq_chainResolveNext()` (RNG + `bank_scenePresent()`, both ISR-safe
reads) → `stp` sets `seq_chainStopped`, else publishes the pending direct
change with `seq_pendingChainFire = 1` and the fire flag last.
`seq_processSchedulerTick()` skips the loop wrap and DDA while
`seq_chainStopped`. State 5 B: `seq_chainBarCount`, `seq_chainRepCount`,
`seq_previousScene` (0xFF none), `seq_chainStopped`, `seq_pendingChainFire`.

### 3.4 LED behaviour (`ledHandler.c`, `menu.c`)

- **`LED_LAYER_FAST`** (bit 4): one LED (`led_fastBlinkLed`, 0xFF none),
  phase toggled every `LED_FAST_BLINK_TIME_MS` 125 in `led_tickHandler()`.
  Priority pulse > fast > flash > blink/chase > base. `led_renderFromStack()`
  re-applies the fast phase, so `led_setValue()`, blink cancel and
  `led_clearAllBlinkLeds()` cannot disturb it; blink start/toggle skip a
  FAST LED; `led_clearAll()` resets the owner. API
  `led_setFastBlinkLed(ledNr|0xFF)`, `led_getFastBlinkLed()`.
- Owner: `menu_syncQueuedSceneLed()` derives the wanted LED (PERF mode and
  `seq_queuedSceneTarget() < 16`) every tick, so queuing, replacing, firing,
  transport stop and leaving PERF need no extra call sites.
- **Why (Defect 10 / P2 failure):** the P2 attempt shared `LED_LAYER_BLINK`
  and the eight blink slots; `menu_refreshPerfSceneLeds()` cleared every SEQ
  blink and rewrote base values every beat, and scene changes called
  `led_clearAllBlinkLeds()`, so the fast blink stopped or stuttered.
- **PERF Scene refresh** (`menu_refreshPerfSceneLeds()`, rewritten): builds
  a blink set (active Scene while stopped or while the chain editor is open;
  viewed Scene when follow is off and it differs; chain-viewed Scene); a LED
  already blinking is left alone (`led_isBlinkLed()`), so the per-beat
  refresh no longer restarts blink phases; others get blink off + base
  value; the active Scene follows the beat pulse while running unless it is
  in the blink set.
- **SELECT row:** `menu_syncPerfSelectLed()` lights
  `buttonHandler_loopSelectLed()` (division while the looper is held or
  engaged) or clears the row. `led_initPerformanceLeds()` (which forced
  SELECT1 on) was removed; the PERF SHIFT-release path no longer clears the
  SELECT row (it wiped the looper LED).

### 3.5 Latched menus and SHIFT handling (`buttonHandler.c`)

- `bh_shiftRecMenuActive` (SHIFT+REC) and `menu_chainMenuIsOpen()` (chain
  editor) are latched menus.
- SHIFT press while one is open: close it, set `bh_shiftReleaseConsume`,
  return. SHIFT release: if consume armed → clear it, SHIFT LED off, stop;
  if a latched menu is open → ignore (LED stays lit). Otherwise the normal
  mode-specific release path runs (whose PERF branch re-enters
  PERFORMANCE_PAGE — the cause of the P2 chain menu "snap back").
- `handleModeButtons()` closes both latched menus first and, when leaving
  PERF, calls `buttonHandler_loopForceRelease()`.
- `bh_chainPlayArmed` is set by a PLAY press with SHIFT held in PERF and
  cleared on PLAY release; the SHIFT+SEQ PERF branch opens the editor while
  armed, otherwise queues.

### 3.6 Menu plumbing patterns used

- All `DTYPE_MENU` ids and `DTYPE_*` nibbles are full (S078): the S080
  enumerated cells use `DTYPE_0B127` + static-param special cases keyed on
  the PAR id: `menu_getS080StaticName()` (via `menu_nameTable3()`) for
  display in `va_formatValue3()`, `menu_formatCellValue3()` and the edit
  painter; `menu_clampCellValue()` for domains (`pb<`/`pb>` 0..11, `pn` 0..127,
  scd 0..9, qcd 0..17, qac 0..8, chain conditions 0..78, next 0..43).
- Mirrors outside the Global range: `PAR_CHAIN_*` and `PAR_TRACK_LOOPABLE`
  are appended after the Globals, so `menu_sendAllGlobals()` replays them on
  every settings load; `menu_parseGlobalParam()` treats them as no-ops, and
  page edits commit in `menu_cellCommitValue()` (`menu_commitChainParam()`,
  `pat_setTrackLoopable()`), the S074 bus-compressor precedent.
- Chain editor host: the retired, empty `PATTERN_SETTINGS_PAGE` (already
  handled by `menu_switchPage()`); its LED block is treated like PERF while
  the editor is open.

---

## 4. P1 implementation record (`S080_P1_IMPLEMENTATION.md`)

Phases from plan §7: A menu re-order, B conditions, C looper core, D
per-track looping / scene change during loop, E BAR overlay + SHIFT+REC, F
scene globals, G chaining, H AutoSave/integration. Each change was scheduled
by file/line/action with comment blocks.

| Phase | Result | Notes / deviations |
|-------|--------|--------------------|
| A | done | A1 PERF split (two padding rows removed); A2 cursor reset (entry + SHIFT release); A3 marker remap; **A4 no-op** (scan is parameter-based); A5 SELECT sub-page LED (later removed by Defect 4); added the cross-sub-page encoder step (unscheduled, needed). Build text 544,816 |
| B | done | 41 conditions; **bonus fix** of the S079 mute-label order |
| C | done | looper state/API; **C3 deviation:** wrap via a tick counter in `seq_loopProcessTick()` from `seq_processSchedulerTick()` instead of inside `seq_advanceTrackStep()`; last-pressed-wins approximated by the highest held index; REC consumed while looping; overflow clears |
| D | D1 only | VOICE swap via disengage + re-engage (later replaced); D2/D3 (scene change re-snapshot) not done — superseded by Defect 14 |
| E | done | overlay params, RECORDING_PAGE, static names, SHIFT+REC enter/exit (exited on SHIFT release — Defect 3), BAR toggle/modifiers, settings keys, VOICE trigger dispatch. `fls` simplified to fixed 1/32; overlay runtime ≈14 B (plan ~4 B) |
| F | done | scene globals; **F3 deviation:** placed in the second half of global sub-page 2 because the old traversal could not reach a sub-page 3 (fixed in the follow-up by extending the cross-sub-page step); `ens`/`enl` one bar; morph reset / reload not wired; PCR as a duplicate `PAR_SCENE_PC_RESET`; queued LED at standard blink. Build text 549,592, data 412, bss 427,856 |
| G, H | deferred | to keep the tree safe; name tables/enums (G9/G10) were added already. Implemented in P2 Step 11 |

---

## 5. P2 defect list (`S080_P2_PERF_EXPANSION_DEFECTS.md`) and resolutions

| # | Defect | Root cause | Resolution |
|---|--------|------------|------------|
| 1 | SHIFT+SEQ queue never fires at `qcd ins` | division 0 → 0 units; ISR test needs units ≠ 0 | instant/stopped queue raises the fire flag in `seq_queueSceneChange()` |
| 2 | SHIFT+PLAY+SEQ chain menu absent | Phase G not implemented | Phase G implemented (§2.8, §3.3) |
| 3 | SHIFT+REC closes on SHIFT release | release handler called exit | latched-menu rules (§3.5) |
| 4 | SELECT1 permanently lit in PERF | `led_initPerformanceLeds()` forced it; A5 sub-page LEDs | function removed; SELECT row = looper only |
| 5 | Duplicate `pcr` | P1 F3 added `PAR_SCENE_PC_RESET` | removed everywhere; one pcr = `PAR_BAR_RESET_MODE`; settings `lines=` 25 → 24 |
| 6 | Compressor not last | P1 F3 layout | final: compressor alone on sub-page 3 (follow-up) |
| 7 | Adding a looped track re-snapshots all | VOICE press did disengage + engage | `seq_loopUpdateMask()` (add/keep/resume) |
| 8 | Loop ends on SELECT release | only SELECT held mask checked | `buttonHandler_loopCheckDisengage()` (SELECT / looped VOICE / BAR); VOICE release drops its track |
| 8a | Per-track loopable flag (new) | — | `lop` cell, `track_loop_exclude`, PAT4 byte 4 |
| 9 | SEQ during a loop does per-track assignment | held VOICE routing | guard `!seq_loopActive` |
| 10 | Queued LED at standard rate | single blink rate | `LED_LAYER_FAST` (§3.4) |
| 11 | Mute LEDs not restored by queue unmute | LED mirror not cleared | `buttonHandler_clearAllMutes()` |
| 12 | Scene change restarts from bar 0 (P2 attempt) | the Defect 5 merge pointed the executor's reset at `PAR_BAR_RESET_MODE`; with `bar_reset_mode=1` every change ran `seq_resetToPatternStart()` (zeroes master clock and cursors). `seq_selectActivePattern()` never resets | confirmed by audit; pcr on now means restart (direct/chain); queued uses qac; loop survives the reset |
| 13 | Chain-viewed Scene LED killed by refresh | refresh cleared every SEQ blink | refresh rewrite with chain awareness |
| 14 | Loop start on scene change (spec correction) | plan §3.6 misstatement | same master-clock position, incoming Scene's scale; resetting change → tick 0 |
| 15 | Queued change needs its own PC reset (option change) | — | qac 0..8 with reset bits; queued changes ignore pcr |

---

## 6. P2 attempt 1: failure analysis (code audit before the final schedule)

The first P2 implementation (rolled back) failed on hardware. Causes found
in the P1/P2 code:

1. **Looper out of sync on individual tracks** — several independent
   causes, only audible when non-looped tracks provide a grid reference:
   - Shared `seq_loopTickCount`: shrinking the loop length (SELECT or BAR
     modifier) when the counter already exceeded it wrapped on the next
     tick at an arbitrary point; every later wrap was off-grid.
   - The Defect 7 rule as first written gave an added track its own
     snapshot but wrapped it on the shared counter, shifting its material by
     however far into the period the press landed.
   - Resume via `seq_realignTrackToMasterClock()` (canonical position) only
     matches neighbours that sit on their canonical position — not after
     Morph/automation scale or length changes, boundary pattern commits
     (`seq_setStepIndexToStart()` without a clock reset) or once modes.
   - P2 dropped tracks on VOICE release without repositioning and left the
     loop "active" with an empty mask.
   - The foreground snapshot was taken without masking TIM3 (one-tick skew).
   - The wrap ran after the DDA advance, repeating the step after the one
     sounding at the press; plan §3.4 loop-start quantisation was never
     implemented.
   - A PCR change went through `seq_setStepIndexToStart()`, which cleared
     `seq_loopActive` while buttonHandler still believed the loop was held.
2. **Queued-scene blink not continuous** — shared blink layer/slots cleared
   and rewritten every beat by the PERF refresh (§3.4).
3. **Chain menu snapped back** — the P2 schedule closed it on SHIFT release,
   and the PERF SHIFT-release path re-entered PERFORMANCE_PAGE anyway.
4. **Restart from bar 0** — Defect 12.

---

## 7. Decisions applied in the final schedule (D1–D17)

| # | Decision |
|---|----------|
| D1 | VOICE release of a looped track drops it at once; the loop is sustained by any SELECT, looped VOICE or BAR |
| D2 | A track added mid-loop gets its own loop start (quantised like an engage) |
| D3 | Master-clock-anchored looper; resume by projection, not realign |
| D4 | Loop start: fastest looped track's current step start (qnt off) or last qnt boundary; obtained by rewinding live state |
| D5 | Scene change keeps anchors, re-snapshots the incoming Scene; resetting change → anchor 0 |
| D6 | No SELECT LED in PERF unless the looper is held/engaged; A5 removed |
| D7 | SHIFT+PLAY consumed in PERF only |
| D8 | Latched menus: SHIFT release ignored; SHIFT press closes, its release consumed; MODE closes first |
| D9 | Chain fires execute at the evaluated boundary, not re-quantised by scd; suppressed while a change is pending |
| D10 | Repetition end = master-clock multiples of the longest track's length × scale; counts per Scene activation |
| D11 | `xNN`/`bNN` fire when the count equals NN |
| D12 | Absent chain target → no change; `rNN` picks among 1..NN; `r±N` exclude current |
| D13 | Chain fire to the playing Scene = repeat (no DSP re-apply) |
| D14 | Loopable stored as an exclude bit (region byte mask; PAT4 track byte 4; no version change) |
| D15 | Queue actions per Defect 15; reload deferred |
| D16 | Chain editor on the retired PATTERN_SETTINGS_PAGE; PAR_CHAIN_* mirrors commit to the viewed Scene only |
| D17 | Looper engages only while the transport runs |

---

## 8. Storage, formats and RAM

- **settings.cfg** (key order cases 0..23, terminator `lines=24`): S080 keys
  `perf_btn_lt`, `perf_note_lt`, `perf_btn_rt`, `perf_note_rt` (cases
  17..20), `scd`, `qcd`, `qac` (21..23). P1 wrote `pcr` and `lines=25`; an old
  `pcr=` line is now skipped as an unknown key; the recovery prelude
  compares N with the same file's line count, so P1 files still validate.
  Load sanitizing: qac > 8 → 0, qcd > 17 → 0, scd > 9 → 0. Old qac 3/4
  (m+m/rld) read as res/u+r; old qcd 8/9 (ens/enl) read as 5b/6b (backward
  compatibility not required).
- **sceneset.scg:** lines 18/19 `chain_condition=c1,c2` (0..78) and
  `chain_next=n1,n2` (0..43); missing keys → 0 (no chaining).
- **AutoSave:** Scene parameter cells 72..73 (chain conditions) and 74..75
  (chain next), in previously reserved cells of the 118-byte Scene
  allocation; record layout, masks and format version unchanged
  (`AUTOSAVE_SCENE_PARAM_COUNT` 72 → 76). Restore fix: a zero track-Morph
  length cell `continue`s instead of `return`ing (it aborted every later
  cell for pre-S078 records).
- **PAT4:** track header byte 4 = looper exclusion (1 = not loopable);
  previously reserved zero → old files read loopable; version stays 2.
- **RAM (DEV link):** bss 427,776 → 427,976 (+200 B):
  - P1 (+80 B at Phase F): looper state, scene-change state, overlay runtime
    (~14 B), SHIFT+REC state.
  - P2 (+120 B): looper anchors 28 + play-state snapshot 7 − tick counter 2;
    chain runtime 5; fast blink 4; buttonHandler `bh_chainPlayArmed`,
    `bh_shiftReleaseConsume`, `bh_loopBarHeld` 3; `menu_chainMenuScene` 1;
    `scenes` +64 (26,432 → 26,496 B, 1,656 B per Scene); `pat_regions` +16
    (168,416 → 168,432 B, 10,527 B per region) and `pat_background_region`
    +1 (10,527 B); alignment. The P2 RAM table (schedule §2) was presented
    under the allocation policy before implementation; the user then
    implemented the schedule as written.
- `parameter_values[]` mirrors use the fixed 384-entry allocation.

---

## 9. Affected files

| File | Changes |
|------|---------|
| `Core/Sequencer/sequencer.c/.h` | conditions 23/24 + shifted mute ranges; looper engine (§3.1); scene change/queue state + `seq_sceneChangeDivUnits()`/`seq_queueChangeDivUnits()`; ISR boundary detection; `seq_cancelPendingDirectChange()`, `seq_pendingSceneIsChain()`, `seq_queuedSceneTarget()`; chain engine (§3.3); `seq_chainRepeatSelf()`; `seq_loopableTrackMask()`; hooks in `seq_selectActivePattern()`, `seq_alignActivePatternToScene()`, `seq_setRunning()`, `seq_setStepIndexToStart()`, `seq_processSchedulerTick()`; `BankData.h` include |
| `Core/Hardware/frontPanel/buttonHandler.c/.h` | PERF entry cursor; looper SELECT/VOICE/BAR handlers + helpers; overlay dispatch/timer; SHIFT+REC; latched SHIFT rules; chain chord; SHIFT+SEQ queue/chain open; SEQ routing guard; MODE/overflow cleanup; `buttonHandler_clearAllMutes()`, `buttonHandler_loopSelectLed()` |
| `Core/Hardware/frontPanel/ledHandler.c/.h` | `LED_LAYER_FAST`, fast blink API, `led_isBlinkLed()`; `led_initPerformanceLeds()` removed |
| `Core/Menu/menu.c/.h` | PERF markers, cross-sub-page step (PERF/RECORDING/MENU_MIDI), MODE MENU cycle, global cues (`^` before the Scene page), S080 static names/clamps, executor/queue action/service/LED owner, Scene LED refresh, chain editor API, lop/chain commits, `menu_syncPerfSelectLed()`, BC18 diagnostic, `MENU_GLOBAL_SCENE_SUBPAGE` 3 |
| `Core/Menu/menuPages.h` | PERF two sub-pages; RECORDING_PAGE; global sub-pages 2 (scd qcd qac) and 3 (compressor); SEQ_PAGE `lop` cell 6; PATTERN_SETTINGS_PAGE chain cells |
| `Core/Menu/MenuText.h` | conditionNames 41; perfBtnTypeNames; sceneChangeDivNames (scd); sceneQueueDivNames (qcd, 18); sceneQueueActionNames (9); chainConditionNames (79); chainNextNames (44); short/long/cat names (Prfrmnc, Chain, lop/Loopable) |
| `Core/Bank/Scene/SceneData.c/.h` | chain fields, `SCENE_CHAIN_*` domains, accessors, commitSettings |
| `Core/Bank/Scene/Autosave.c/.h` | cells 72..75, asserts, getter/restore, `continue` fix |
| `Core/Bank/Scene/Pattern/PatternData.c/.h` | `track_loop_exclude`, init, menu mirror, `pat_setTrackLoopable()`/`pat_trackLoopable()`, region copy, size asserts (+31u) |
| `Core/Bank/Scene/Preset/ParameterArray.h` | `PAR_PERF_BTN/NOTE_LT/RT`, `PAR_SCENE_CHANGE_DIV/QUEUE_DIV/QUEUE_ACTION`, `PAR_CHAIN_COND1..NEXT2`, `PAR_TRACK_LOOPABLE` (+ assert) |
| `Core/Hardware/SD/filesystem.c` | settings keys/defaults/sanitizing/terminator; sceneset lines 18/19; PAT4 writer + 3 readers byte 4 |
| `Core/Hardware/SD/storageTypes.c` | sceneset chain keys |
| `Core/Menu/CopyClear/copyClearService.c` | track copy/clear carry loopable |

---

## 10. API changes

**New:** `seq_loopEngage()` (returns mask), `seq_loopUpdateMask()`,
`seq_loopDisengage()`, `seq_loopUpdateDivision()`, `seq_loopableTrackMask()`,
`seq_requestSceneChange()`, `seq_queueSceneChange()`,
`seq_sceneChangeFirePending()`, `seq_pendingSceneIsQueue()`,
`seq_pendingSceneTarget()`, `seq_clearPendingSceneChange()`,
`seq_cancelPendingDirectChange()`, `seq_pendingSceneIsChain()`,
`seq_queuedSceneTarget()`, `seq_chainRepeatSelf()`; `led_setFastBlinkLed()`,
`led_getFastBlinkLed()`, `led_isBlinkLed()`; `buttonHandler_clearAllMutes()`,
`buttonHandler_loopSelectLed()`; `menu_serviceDeferredSceneChange()`,
`menu_chainMenuOpen/View/Close/IsOpen()`, `menu_syncPerfSelectLed()`;
`scene_setChainCondition/Next()`, `scene_getChainCondition/Next()`;
`pat_setTrackLoopable()`, `pat_trackLoopable()`.

**Static helpers (file-private):** looper `seq_loopShiftPosition`,
`seq_loopCanonicalPreAdvance`, `seq_loopEventAnchor`,
`seq_loopSnapshotTracks`, `seq_loopResumeTrack`,
`seq_loopRefreshSceneSnapshots`, `seq_loopRebaseToStart`,
`seq_loopProcessTick`, `seq_loopLengthTicks`; chain `seq_chainServiceTick`,
`seq_chainAtRepetitionEnd`, `seq_chainConditionWeight`,
`seq_chainResolveNext`, `seq_chainEvaluate`; menu `menu_perfSceneChangeExecute(scene, restart)`,
`menu_queueActionFlags`, `menu_applyQueuedSceneAction(target)`,
`menu_syncQueuedSceneLed`, `menu_getS080StaticName`, `menu_nameTable3`,
`menu_staticFirst/LastPopulatedColumn`, `menu_commitChainParam`,
`menu_paramIsChain`; buttonHandler `buttonHandler_loopCheckDisengage`,
`buttonHandler_loopForceRelease`, `buttonHandler_shiftRecMenuEnter/Exit`,
`buttonHandler_overlayTrigger/Release/Tick`.

**Removed:** `led_initPerformanceLeds()`; `PAR_SCENE_PC_RESET`,
`TEXT/SHORT/LONG_SCENE_PC_RESET`; settings key `pcr`; `seq_loopTickCount`;
externs `seq_loopTrackMask/Division/Modifier/StartStep/StartAccum` (now
file-static); `menu_setPerfSelectLed()` (A5).

---

## 11. Build results

| Point | text | data | bss | Payload / headroom |
|-------|------|------|-----|--------------------|
| S079 close | 544,072 | 408 | 427,776 | 544,480 / 209,184 |
| P1 Phase A | 544,816 | 408 | 427,776 | — |
| P1 Phase F | 549,592 | 412 | 427,856 | — |
| **S080 close** | **556,008** | **412** | **427,976** | **556,420 / 197,244** |

ITCM 4,168 / 16,384 B; DTCM statics 4,472 B; FXBUF 126,592 B (margin
3,712 B). Build clean except the long-standing warnings in untouched code.
Production build (`DEV_MODE_LOGGING 0`) not re-measured (last S075 F1).

---

## 12. Hardware verification

- Final P2 + follow-up: "seems ok so far" (user), follow-up rows T14.1–T14.5
  PASS (2026-10-11). The full matrix below is the reference for a row-by-row
  pass (open).

| Row | Setup / action | Expected |
|-----|----------------|----------|
| T1.1 | PERF, qcd = ins, SHIFT+SEQ another Scene | changes at once; queue action applies |
| T1.2 | stopped, SHIFT+SEQ | changes at once |
| T2.1 | SHIFT, PLAY, SEQ 3 | chain editor for Scene 3; transport unchanged |
| T2.2 | release SHIFT, PLAY, SEQ | editor open, SHIFT lit |
| T2.3 | SEQ 5 while open | shows Scene 5's chain; SEQ 5 and the active Scene blink |
| T2.4 | chain fires while open | viewed Scene unchanged; active blink moves |
| T2.5 | SHIFT press/release | PERF page; SHIFT LED off after release; looper LED intact |
| T2.6 | cn1 b03→stp, cn2 b04→slf | 3 bars play, 1 bar frozen, repeat |
| T2.7 | cn1 r5x→nxt, cn2 r6x→prv | one fires every repetition end |
| T2.8 | cn1 xal→s04, manual change first | chain cancelled, counts restart |
| T2.9 | save/load | chain survives sceneset and AutoSave |
| T3.1/2 | SHIFT+REC, release; SHIFT or REC | stays open; closes to previous page |
| T4.1 | PERF idle | no SELECT LED |
| T5.1 | global menu | one pcr; layout §2.6 |
| T7.1 | all 1/16, VOICE1 + SELECT5 | track 1 repeats the sounding step on the grid |
| T7.2 | SELECT held, VOICE2 | track 2 joins with its own start; track 1 unchanged |
| T7.3 | release VOICE1 | track 1 in time with unlooped tracks |
| T7.4 | 1 bar → SELECT5, BAR1, BAR2 | wraps stay on grid; starts never move |
| T7.5 | Morph scale sweep, loop, release | released tracks in time |
| T7.6 | qnt 16 | loop starts on the 1/16 grid |
| T8.1/2 | VOICE1+SELECT3, release SELECT3 then VOICE1 | loop continues, then ends; SELECT dark |
| T8.3 | SELECT + BAR2, release SELECT | continues dotted until BAR2 release |
| T8a.1/2 | track 7 lop off | track 7 free; persists; old Patterns loopable |
| T9.1 | looping, SEQ 4 | full change |
| T10.1/2 | qcd 1b queue; transport stop | continuous double-rate flash; stop cancels |
| T11.1 | qac mut | Sequencer + VOICE LEDs unmuted |
| T12.1/2 | pcr off / on | continue from master clock / restart |
| T14.1 | 1/16 loop at step 5 → Scene with 1/8 | loop start step 3; timing kept |
| T14.2 | looping, pcr on, direct change | loop restarts at Scene start, stays engaged |
| T15.1–3 | qac res/u+r/o+r/mmr; nrm + pcr on; mrp | reset per option; queued ignores pcr; incoming morphs 0 |
| F14.1–5 | global page scroll/MODE cycle; cmp edits; qcd 16b; qcd list | `^` after qac, `+` page, cycle reaches both, fires on 16-bar boundary — **PASS** |

---

## 13. Specification reference updates (S080 close)

- `MODULE_INTERCHANGE_SPEC.md` — new "PERF performance surface (S080)"
  section (gestures, LEDs, menus, contracts) and per-module notes
  (sequencer, buttonHandler, ledHandler, menu, SceneData, PatternData,
  ParameterArray, Autosave, filesystem, storageTypes).
- `PATTERN_DYNAMIC_STACK.md` — §1 region byte, §6 conditions 41, new §6.5
  looper engine and §6.6 scene change / queue / chain timing, §7 PAT4 byte 4.
- `BANK_PRESET_ARCHITECTURE.md` — Scene chain settings, `parameter_values[]`
  mirrors, dirty marking.
- `AUTOSAVE.md` — Scene cells 72..75, restore `continue` fix.
- `FILESYSTEM_SPEC.md` — settings.cfg keys/terminator, sceneset chain lines,
  PAT4 byte 4.
- `STORAGE_SRAM_MANIFEST.md` — S080 RAM and link numbers.
- `COPYCLEAR_UTILITIES.md` — loopable in track copy/clear; chain in Scene
  settings copy/clear.

---

## 14. Disposable documents

Superseded by this log and the specification updates; the user deletes them:
`S080_P1_PERF_MODE_UPDATES.md`, `S080_P1_IMPLEMENTATION.md`,
`S080_P2_PERF_EXPANSION_DEFECTS.md`, `S080_P2_IMPLEMENTATION.md`. Their
plan text, decisions, deviations, defect analyses, test matrices and
per-change schedules are preserved in §2–§12; the per-line code schedules
are not needed (the code carries the same comment blocks).

---

## 15. Carry-forward

1. **Hardware:** run §12 row by row (only the follow-up rows are formally
   PASS).
2. **Deferred features:** reload from source for `rld`/`r+r` (needs a
   performance-safe Scene Load from the queue executor; HCNAMES Scene row
   source slot → `filesystem_requestLoadSceneForScenes()`); live record on
   RECORDING_PAGE sub-pages 2+ (and the REC-LED/exit exceptions); live
   recording of overlay triggers; `ens`/`enl` track-length-aware boundaries
   (both lists approximate one bar); `fls` variable flam spacing; edit lock
   during changeover.
3. **Accepted limits:** projection/rewind use the current effective scale
   for the whole span (Morph change mid-loop approximated); a scene change
   does not re-check loopable flags of tracks already looping; transport
   start with SELECT held does not auto-engage; SHIFT+REC restores cursor to
   cell 0; chain `>NN` history only on real changes; `rNN` picks among
   present Scenes only by rejection (absent target = no change); overlay
   repeat timer is a 1 ms foreground slot (one repeating track).
4. **Carried from S079/S078 (unchanged):** S079 P2 checklist and P1
   addendum fix 3; STEP marker retry gap decision; S077 rows 3.10/3.14/3.15,
   C1–C6, P5/P6 rows; production build measurement; open defects listed in
   `knowledge_files/volatile/S078_WORKING_NOTES.md`.
5. **Card notes:** no new conversion needed (PAT4 v2 unchanged; AutoSave
   format unchanged). settings.cfg from P1 builds load with remapped qac/qcd
   values.
