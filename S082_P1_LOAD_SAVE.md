# S082 P1 — Load/Save: Background Bank Load and Scene 17

*Opened 2026-10-17 from the S083 MIDI planning discussion
(`S083_P1_MIDI_IMPLEMENTATION_PLAN.md` §4). Planning only: no code has
changed. This document holds the **decided** behaviour of the background
Bank load. Other Load/Save cleanup items for S082 will be added in §9 by the
user when the S082 session starts. They are not part of this discussion.*

*Update 2026-10-18: implemented as S082 P0 Phase C. D28–D35 record the
answers to the post-implementation audit; §10 lists the remaining work.*

Read first: `MEMORY.md` (RAM policy, looper invariants, per-track
playback, Active Scene definition, edit-mask intent, the
`filesystem_ack()` rule), `FILESYSTEM_SPEC.md` (Bank Load/Save),
`AUTOSAVE.md`, `PATTERN_DYNAMIC_STACK.md` §6.5/§6.6,
`COPYCLEAR_UTILITIES.md`, `STORAGE_SRAM_MANIFEST.md`
(`pat_background_region`).

---

## 1. Goal

A Bank load happens **immediately**, without stopping playback. What is
playing at the moment of the request keeps sounding exactly the same while
Scenes 1–16 are replaced. This is the main reason Scene 17 exists. Every
other data operation on the outgoing Bank (AutoSave, Pattern AutoSave,
copy/clear) gives way to the load, because everything except what is
playing is about to be replaced.

The same flow serves a Bank load chosen in the Menu and one requested by
MIDI CC0 (S083).

---

## 2. Decisions (user, 2026-10-16/17) — binding

| Id | Decision |
|----|----------|
| D1 | **Immediate.** A Bank load request is acted on at once. AutoSave (scalar and Pattern) is dropped; pending dirty state of the outgoing Bank is discarded, not flushed. |
| D2 | **Copy what is playing into Scene 17.** The active Scene and the playing tracks, honouring per-track playback (each track's data from its played Scene), are copied into the background 17th Scene. |
| D3 | **Track priority when it does not fit.** Whole tracks, in or out, in track order T1 → T7, only when the per-track mix does not fit one Pattern allocation. T6 and T7 are separate tracks: T6 is included if it fits even when T7 does not. |
| D4 | **Sound identical.** When all tracks fit, Scene 17 must sound exactly like what was playing. |
| D5 | **Switch instantly.** Playback moves seamlessly to Scene 17 as soon as the copy completes. |
| D6 | **Then load.** The Bank loads, replacing Scenes 1–16. |
| D7 | **UI.** From MIDI, the UI locks and switches to Load/Save → Bank showing the loading slot, exactly as if the user had selected it and pressed OK. From the Menu it is the normal OK. Afterwards the UI unlocks on Load/Save exactly as after a normal Bank Load, and **stays there**. |
| D8 | **PERF shows no Scene selected** while Scene 17 is active. |
| D9 | **Scene 17 is a normal Scene in every way**, except: (a) once left, it can never be selected again; (b) it has no chain: it always loops itself, and any chain conditions are discarded at the copy. |
| D10 | **Released only by a Scene change.** Per-track changes do not release it, even if all seven tracks have been moved to other Scenes: Scene 17 still owns the Effect (and the other Scene-level state) that is sounding. Any whole-Scene change (SEQ press, MIDI PC, queued change) releases it. |
| D11 | **While Scene 17 exists:** AutoSave and replace (overlapping) copy/paste are unavailable. Both return after the release. |
| D12 | **Copy/clear in progress at the request:** drop it or let it complete, whichever the implementation can do safely, then perform the Bank load. |
| D13 | **MIDI PC and MIDI Bank Change arriving during a load are dropped.** |
| D14 | **Transport stopped at the request:** no Scene 17. A normal Bank load; the loaded Bank's `active_scene` becomes active. |
| D15 | Scene 17 is editable, never AutoSaved (its edits are lost at power-off), can be saved with Scene Save and used as a copy source, and its edit mask is itself only. Its LCD names are the identity captured at the copy. |
| D16 | Live record follows playback into Scene 17 seamlessly with no reset (D35). The looper keeps its anchors. A pending direct or queued Scene change is cancelled. |
| D17 | **Dirty marks while 17 exists.** Changes to Scenes 1–16 (Scene Load into a slot, copy from 17 into a Scene) keep being marked, and AutoSave writes them after the release. Scene 17 is never marked. |
| D18 | Scene 17 can be a copy/clear **destination** (non-overlapping operations only, D11). Nothing loads into Scene 17 (R1): every load (Kit, KitMrp, Scene, Pattern, Instrument, InstrumentMrp, the `kit` row) goes to the **home Scene** (`scene_backgroundHomeIndex()`), the SEQ the Load page shows as its destination. |
| D19 | **Bank load while 17 is active:** rebuild 17 in place. Tracks playing 17 stay. Tracks playing Scenes 1–16 are copied in, T1 first, whole tracks, dropped if they do not fit. Scene 17's Effect and other Scene-level state stay. |
| D20 | **Bank Save while on 17** is allowed. It saves Scenes 1–16 with the loaded Bank's own `active_scene`; 17 is never saved as the active Scene. |
| D21 | **Power-off while on 17:** boot loads the new Bank at its stored `active_scene`; 17 is gone. The old Bank's AutoSave records are ignored (identity mismatch). Accepted. |
| D22 | **Labels:** where a screen shows a Scene number, 17 shows `--`. Its name is the identity captured at the copy. |
| D23 | An overlapping paste refused while on 17 uses the existing copy/clear refusal display. |
| D24 | `settings.cfg` is not AutoSave: it keeps writing while on 17 (`active_bank`, global edits, S083 MIDI Learn lines). |
| D25 | **Merge load (user, 2026-10-18; `S082_P0_LOAD_SAVE_MENU_CLEANUP.md` S5).** A Bank load applies the P0 §4.2 outcome (Replace or Merge) instead of always replacing Scenes 1–16 (amends D6). Scene 17 is built only when a played Scene (the active one or any `seq_perTrackPattern[t]`) is loaded or emptied (amends D2–D5). Only replaced Scenes lose their pending dirty state (amends D1, D17); after a Merge load the whole Bank is re-marked for AutoSave. |
| D26 | **Active and viewed Scene (user, 2026-10-18; P0 S9–S11).** A load or save never changes the active Scene or the viewed Scene. Stopped: the active Scene stays (amends D14: the loaded Bank's `active_scene` is **not** applied). Running with a played Scene replaced: Scene 17. Running with no played Scene replaced: load directly, active unchanged. `bankset.bcg` `active_scene` is legacy: read only at boot without an AutoSave record; Bank Load never applies it (amends §4.4 "record that value"). |
| D27 | **View at the switch (user, 2026-10-18; P0 S18, S25).** At the Scene 17 switch the view follows playback onto 17 only with `PAR_FOLLOW` on (edits and live record then go to 17); with it off the viewed Scene stays. Apart from that switch, no load or save ever changes the active or viewed Scene; only the user does. The active Scene is re-applied only when its content was replaced, and an emptied active Scene stays present (P0 S17). |
| D28 | **No return to Scene 17 (user, 2026-10-18).** A track moved off 17 cannot be put back on it, and the view cannot return to 17 once it has left (with `PAR_FOLLOW` off at the switch, 17 is never viewed). Amends §4.6. |
| D29 | **Scene 17 names (user, 2026-10-18).** Scene 17 keeps all ten names captured at the copy: Scene, Kit, six Instruments, Pattern, Effect (90 B static, SRAM approved). They exist so the user can save any component of 17 before switching away: Scene, Kit/KitMrp, Instrument and Pattern Save from 17 are allowed and prefill from the capture. Name reads for index 16 use the capture, never HCNAMES. Amends D15 and §6. |
| D30 | **Rebuild, track that does not fit (recommendation accepted 2026-10-18).** In a D19 rebuild a track that does not fit is left **empty** in 17, as in D3. The track's old Scene 17 blocks are freed before the fit check. |
| D31 | **Rebuild scope (recommendation accepted 2026-10-18).** A Bank load while 17 is active copies into 17 only the tracks whose played resident Scene the load replaces or empties. Tracks on unaffected resident Scenes stay where they are. When no played resident Scene is affected, the load runs directly and 17 is unchanged. Amends D19 under D25. |
| D32 | **MIDI load takes the UI (user, 2026-10-18).** A MIDI-origin Bank load may abandon any Menu state, including an unsaved name edit. |
| D33 | **MIDI while busy (user, 2026-10-18).** While a Bank load is in flight (`preset_backgroundBankLoadRunning()`), incoming MIDI other than realtime, note and CC messages is dropped; a Bank Change is also dropped (D13). When something other than a Bank load is busy (another Preset op, a card op, an AutoSave write, a Menu command), a MIDI Bank load request is **deferred**: one pending request, latest wins, run when idle (2 B, approved). |
| D34 | **Completion result (recommendation accepted 2026-10-18).** The background load keeps a result (none / done / failed) that the origin reads; S083 sends CC0 TX from it (1 B, approved). |
| D35 | **Live record at the switch (recommendation accepted 2026-10-18).** The switch keeps the current take going with no reset, so recording carries on into 17 seamlessly. Amends D16 ("as with a Scene change while recording"). |
| D36 | **Nothing loads into Scene 17 (user, 2026-10-18; schedule R1).** Every load while on 17 (Kit, KitMrp, Scene, Pattern, Instrument, InstrumentMrp, the `kit` row) goes to the **home Scene**, the Scene that was active when 17 was built. That is the SEQ the Load page shows as its destination. Saves from 17 (D29) and copy/clear pastes into 17 (D18) stay allowed. Amends D18. |
| D37 | **AutoSave dropped at once (user, 2026-10-18; schedule R6).** A Bank load does not wait for an in-flight AutoSave drain; it aborts it (torn, stale record). The only exception is an HCNAMES register publication already under way, which finishes so that `/.hcnames` cannot be lost. Replace discards the dirty state on every path; Merge keeps it (D1, D25). Amends §4.1 step 2 and the §7 "D1 AutoSave yield" note. |
| D38 | **Panel input during a load is dropped (user, 2026-10-18; schedule R7).** While a Bank load runs, MODE, SHIFT+REC, VOICE entry and page switches are dropped, not queued. Nothing happens after the load; the UI stays on Load:[Bank] (D7). |
| D39 | **MIDI load closes everything (user, 2026-10-18; schedule R8).** A MIDI Bank load closes every latched menu and session and shows the Bank Load screen exactly as if it had been entered by hand. |

---

## 3. Baseline facts (checked 2026-10-17)

- `pat_background_region` (`PatternData.c`, 10,527 B) is a 17th Scene-sized
  Pattern region outside `pat_regions[16]`. Today it is **not playable**.
  Its users:
  - the immutable Pattern AutoSave snapshot (`filesystem_patternSnapshotInUse()`);
  - the overlap-paste pool (`pat_backgroundPoolMut()`, `copyClearService.c`).

  "Bank Load staging" is listed as a **future** use only; current Bank
  Load does not stage through it.

  *Since S082 Phase C the region is also Scene 17's Pattern and is playable
  while Scene 17 exists (`pat_regionFor()`, `PATTERN_DYNAMIC_STACK.md`).*
- Resident Scenes: `scenes[16]`, `scene_t` = 1,656 B each (75 B settings,
  420 B Effect record, 1,160 B Kit).
- Playback addresses: `seq_activePattern`, `seq_perTrackPattern[7]`
  (single-byte ISR reads), `seq_perTrackActive`. The mixer resolves per-voice
  Scene state through `preset_getSlotPlayedScene()`.
- Scene activation (`preset_startDrumsetApply()`) clears outgoing targets,
  image-applies all six types and rebinds LFO/velocity. A switch that runs
  it is **not** seamless (D4).
- Copy/clear has a queue and multi-tick Pattern engines, but no abort API
  (`COPYCLEAR_UTILITIES.md`).
- Bank Load runs per-child Scene loads through the shared Scene loader and
  commits at phase 20 (`seq_alignActivePatternToScene()`,
  `menu_setShownPattern()`).

---

## 4. Sequence

### 4.1 Request

`preset_requestBackgroundBankLoad(slot, origin)`, origin = MENU or MIDI.
Foreground, non-blocking, returns accepted or refused.

Refused when:
- a Bank load is already running (a MIDI request during a load is dropped,
  D13);
- the slot is empty or invalid (show it as the Menu would, then refuse).

Otherwise, in order:

1. **Lock the UI.** Origin MIDI: switch to Load/Save → Bank, cursor on the
   slot, name from `/Bank/.hcindex`, show `...` (same state as Menu OK).
   Close any latched menu (SHIFT+REC, chain editor) as MODE would.
2. **Drop background work** (D1, D12, R6):
   - abort an in-flight AutoSave scalar or Pattern drain at its next tick
     through its error close-down (open handles closed, torn record
     accepted — it is stale); the HCNAMES publication (scalar phases 70–77,
     Pattern follow-on) is never cut (R6);
   - drop pending scalar and Pattern dirty state;
   - release the Pattern snapshot;
   - drop queued copy/clear operations and finish or abandon the running
     one at a safe boundary. Pattern writes publish before free, so the
     exclusive boundary is the abort point.
3. **Transport stopped?** Skip to 4.4 without Scene 17 (D14).

### 4.2 Build Scene 17 (D2–D4, D9)

Destination: `pat_background_region` plus a new `scene_t` record (§6).

- **Scene-level state from the active Scene:** settings, Effect record and
  FX sequence, bus compressor, global Morph. Chain settings are
  **cleared** (D9b).
- **Per track t, from `seq_perTrackPattern[t]`:**
  - the Pattern track data, plus the track parameters: length, scale,
    shuffle, run mode, rotation, loopable flag, MIDI channel/note, track
    Morph endpoints;
  - that slot's Kit instrument image (Normal, Morph and interpolation),
    voice Morph amount, audio out, FX send (Normal/Morph) and fader
    setting.
  - T6 and T7 share slot 6. The slot data comes from T6's played Scene if
    T6 is included, else T7's.
- **Fit:** add tracks T1 → T7. A track whose blocks do not fit is left out
  whole; later tracks are still tried (D3). A left-out track is empty in
  Scene 17.
- **Fast path:** with no per-track overrides, the whole active region fits
  by construction (same capacity).
- **Names:** the 90 B name capture (`fs_background_name[10][9]`, E1) stores
  Scene 17's identity names (Scene, Kit, six Instruments, Pattern, Effect).
  Each slot's Instrument name comes from that slot's played Scene. A rebuild
  (D31) refreshes only the rows of the tracks it copies in. Menu name
  requests for index 16 read from the capture instead of HCNAMES. HCNAMES
  rows 1..16 are about to change.

### 4.3 Switch (D5, D16)

- Under PRIMASK: `seq_activePattern = 16`, every `seq_perTrackPattern[t] =
  16`, `seq_perTrackActive = 0`. Step positions, DDA accumulators, roll
  state and looper anchors are kept. Re-snapshot the looper (as a Scene
  change does), with no reset (`pcr` not applied).
- **No Scene activation worker:** the runtime already holds exactly these
  images, Morph amounts, held automation and LFO phases. Any re-apply must
  be a no-op in sound (D4).
- Cancel pending direct/queued changes. LiveRecord target → Scene 17.
- PERF SEQ LEDs: none selected (D8). No MIDI PC is sent (17 has no PC
  number).

### 4.4 Load Scenes 1–16 (D6)

The normal Bank Load path, while playback runs from Scene 17. Nothing plays
Scenes 1–16, so the children write directly into `pat_regions[0..15]` and
`scenes[0..15]`. The background region is busy and must not be used as
staging.

At the commit, with Scene 17 active, do **not** realign `seq_activePattern`
to the Bank's `active_scene`. A runtime Bank Load never applies that value
(D26; the earlier "record that value" is superseded). HCNAMES and
`settings.cfg` `active_bank` are published as today.

### 4.5 Finish (D7)

Unlock the UI on Load/Save exactly as after a normal Bank Load. Signal
completion or failure to the origin (MIDI then sends CC0 TX). On failure,
playback stays on Scene 17 and the normal error overlay shows.

### 4.6 Release (D10, D11)

A whole-Scene change away from 17 releases it:
- the background region and the Scene 17 record are freed;
- AutoSave and overlap paste become available again;
- the 90 B name capture is left stale and unread (no cleanup needed);
- 17 cannot be selected again.

A per-track change while 17 is active only moves that track. A track moved
off 17 cannot return to it, and neither can the view (D28).

---

## 5. Scene index 16 audit (do this first)

Code assumes 16 Scenes. Each site needs a defined rule for index 16:

| Site | Rule needed |
|------|-------------|
| `pat_patternValid()` and PatternData accessors | Map index 16 → `pat_background_region` while Scene 17 exists; invalid otherwise. |
| `scene_getConst()` and SceneData accessors | Index 16 → the Scene 17 record. |
| Edit mask, present mask (16-bit) | 17 is never in a mask; its fan-out is itself only. |
| AutoSave Scene cells, Pattern A/B files | None for 17; must not be marked. |
| HCNAMES rows 1..16 | None for 17; the display uses the captured identity. |
| SEQ LEDs, PERF Scene display | No Scene lit. |
| Chain `nxt`/`prv`/`sNN` | 17 has no chain. |
| PC TX, MIDI PC targets | 17 has no number. |
| Copy/clear sources, destinations, ranges | 17 is a source and a non-overlapping destination (D18). |
| Scene Save / Load | Save from 17 allowed (D15); no Scene Load into 17 (D18). |
| LiveRecord target negotiation | Target 17 allowed. |
| `preset_getSlotPlayedScene()`, mixer, InstrumentManager | Index 16 resolves to the Scene 17 record. |
| Trace/log Scene fields | Encode 16. |

---

## 6. Resources

| Item | Bytes | Region | Status |
|------|------:|--------|--------|
| Scene 17 `scene_t` | 1,656 | SRAM1, static, owner SceneData | **Accepted 2026-10-17** |
| Scene 17 Pattern | 0 new | Existing `pat_background_region` | Reuse |
| Scene 17 active flag, original-index and `active_scene` records | ~3 | SRAM1, static, owner SceneData | **Accepted 2026-10-17** |
| Captured names for 17 (10 × 9 B: Scene, Kit, 6 Instruments, Pattern, Effect) | 90 | SRAM1, static, owner filesystem (beside `fs_identity_name`) | **Accepted 2026-10-18** (D29). The runtime identity block cannot be reused: it holds 8 rows only and is cleared at every Menu session exit. |
| Pending MIDI Bank load request (slot + valid flag) | 2 | SRAM1, static, owner Preset | **Accepted 2026-10-18** (D33) |
| Background load result (none / done / failed) | 1 | SRAM1, static, owner Preset | **Accepted 2026-10-18** (D34). `pm_bg_phase` has no free bit. |

The user acknowledged and accepted these SRAM allocations on 2026-10-17,
under the MEMORY.md RAM Allocation Approval Policy. Record the measured
sizes in `STORAGE_SRAM_MANIFEST.md` at implementation. Any allocation larger
than listed here needs a new acknowledgement.

---

## 7. Open questions (background load only)

None at present. The former Q1–Q8 were accepted as recommended and are now
D17–D24. The 2026-10-18 audit questions are answered as D28–D35, with the
remaining work in §10. Questions that come up during implementation go here.

**R1–R8 (S082 P1, 2026-10-19):** resolved during implementation.

| Tag | Answer |
|-----|--------|
| R1 | Nothing loads into Scene 17. Every load targets the home Scene (`scene_backgroundHomeIndex()`), the SEQ the Load page shows. D36. |
| R2 | Saves from Scene 17 use the 90 B name capture and suppress HCNAMES publication for index 16. After a Save, the capture row is updated so the next prefill shows the saved name. D29. |
| R3 | Name reads for index 16 come from the in-RAM `fs_background_name` capture, not a card read. No error overlay. |
| R4 | MIDI drop filter: while `preset_backgroundBankLoadRunning()`, only realtime, Note On/Off and CC (except CC0/CC32) are accepted. PC, pitch bend, aftertouch, SysEx and Bank Select CCs are dropped. MIDI Thru forwards everything. D33. |
| R5 | MIDI deferred request cleared on a Menu-origin load so the user's explicit action takes priority. |
| R6 | AutoSave dropped at once (D37): the yield flag aborts an in-flight drain through its error close-down at the next phase that has no card callback outstanding (assessment A1/A2: an open, close, remove or seek in progress runs on for a few ms first). The HCNAMES publication (scalar phases 70–77, Pattern follow-on) is never cut. |
| R7 | Panel gates: MODE, SHIFT+REC, VOICE entry and page switches are dropped (not queued) while `preset_backgroundBankLoadRunning()`. D38. Each wait of the load is bounded (20 s) so the panel cannot stay locked (assessment A8). |
| R8 | A MIDI Bank load closes everything (SHIFT+REC without restoring its page, chain editor, COPY/CLEAR, looper, nested Instrument, name editing) and shows the Bank Load screen as if entered by hand. D39. |

Implementation notes (S082 P0 Phase C, 2026-10-18):

* D19 rebuild in place (**corrected 2026-10-18**): the earlier note said every
  track already points at 16 while Scene 17 is active. That is wrong: per-track
  changes move tracks off 17 without releasing it (D10). The rebuild copies
  those tracks in; D30/D31 now define which ones and what happens when one
  does not fit.
* D22 Scene-number labels: no current LCD screen prints the active Scene
  number. The only `Sc NN` text is the boot empty-Scene notice (resident
  0..15), so no label site needed a change.
* `pat_initScene()` deliberately stays resident-only (0..15); Scene 17 resets
  through `pat_rawRegionReset(16)` and `pat_regionFor(16)`.
* D1 AutoSave yield (**updated P1 R6**): the yield flag now also aborts an
  in-flight drain at its next tick through its error close-down, instead of
  waiting for the facade to reach its normal safe boundary. Replace discards
  dirty marks on every path; Merge keeps them and re-marks the whole Bank.

---

## 8. Verification outline

- Running transport, no per-track overrides: load a Bank. There must be no
  audible change at the switch or during the load: steps, rolls, looper,
  LFO phases, held automation, Effect and FX sequence. Then select Scene N:
  normal change; 17 released; AutoSave resumes.
- Per-track overrides from 3 Scenes: 17 sounds identical. Then force a
  non-fitting mix; check that the dropped tracks are the highest that do
  not fit and that T6-without-T7 works.
- Stopped transport: normal load; no 17.
- Request during AutoSave write, Pattern AutoSave snapshot, copy/clear
  (Pattern engine mid-run), live record, looper engaged, queued change
  pending.
- Move all 7 tracks off 17 per track: 17 stays, its Effect keeps sounding;
  a Scene change releases it.
- Bank load while on 17 (D19); Bank Save while on 17 (D20); power-off on
  17 (D21).
- On 17: an edit to Scene N via copy is marked and written after the
  release (D17); an overlapping paste is refused (D23); `settings.cfg` still
  writes (D24).
- MIDI hooks (after S083): CC0 load, PC dropped during the load.

---

## 9. Other Load/Save cleanup for S082

See `S082_P0_LOAD_SAVE_MENU_CLEANUP.md` (Bank Load/Save Replace and Merge,
Instrument `chg`, name-editing pots and BAR2). It is the session plan; this
document stays the authority for Scene 17.

---

## 10. Remaining implementation (audit of the working tree, 2026-10-18)

Phase C of `S082_P0_IMPLEMENTATION.md` (C0–C10, post-C10 fixes, review fixes
F1/F7) implements most of this document. An audit against D1–D27 found the
gaps below. D28–D35 record the answers. None of this is hardware-verified.

| Id | Task | Decision | Where | Status |
|----|------|----------|-------|--------|
| P1-1 | **Name capture.** 90 B capture (`fs_background_name`). Names read from the in-RAM HCNAMES mirror, not a card read (R3). | D29 | `filesystem.c/.h`, `menu.c` | **Done** (E1/E2) |
| P1-2 | **Saving from 17.** Scene/Kit/KitMrp/Instrument/Pattern Save from source 16 suppresses HCNAMES publication, uses capture rename. | D29 | `filesystem.c` | **Done** (E3) |
| P1-3 | **Rebuild scope and fit.** Candidate tracks from `preset_backgroundCandidateTracks(replaced)`. Non-fitting tracks cleared (D30). | D30, D31 | `presetManager.c`, `sequencer.c/.h` | **Done** (E4) |
| P1-4 | **Slot refusal.** `filesystem_bankSlotExists(slot)` guard in request and Menu OK. | §4.1 | `presetManager.c`, `menu.c` | **Done** (E5/E6) |
| P1-5 | **Direct path waits for the card.** Phase 1→4 retries on facade busy, bounded (A8). | D1 | `presetManager.c` | **Done** (E5) |
| P1-6 | **Deferred MIDI request.** Pending slot, latest wins; cleared on Menu origin (R5). | D7, D32, D33 | `presetManager.c`, `menu.c`, `buttonHandler.c/.h` | **Done** (E5/E6) |
| P1-7 | **MIDI drop filter.** `midiParser_acceptedDuringBankLoad()` (R4). | D13, D33 | `MidiParser.c`, `MidiMessages.h` | **Done** (E7) |
| P1-8 | **Completion result.** `preset_takeBackgroundBankLoadResult()`, MIDI flag. | D34 | `presetManager.c/.h` | **Done** (E5) |
| P1-9 | **Docs.** Spec files, `MEMORY.md`. | — | — | **Done** (F). Final build in `S082_P1_IMPLEMENTATION.md` §11/§13. |
| P1-10 | **Hardware verification:** §8, plus `S082_P0_IMPLEMENTATION.md` §13.5. | — | — | Pending |
