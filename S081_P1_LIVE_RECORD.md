# S081 P1 — Live Record

Session 081 plan, branch `dev-ph6-cleanup`, base `02553df`.

- **Revision 1 (2026-10-12):** scope, audit, ownership, open questions.
- **Revision 2 (2026-10-12):** decisions on Q1–Q22. Added the non-record
  menu/automation rule, the Effect automation hold rule, live erase, overdub,
  `qnt trk` and new roll indices.
- **Revision 3 (2026-10-12):** answers to F1–F14 folded in. Roll table fix
  confirmed. Two overlay defects added. Latch state is now full-range
  bitmaps. Live erase uses held buttons and is mutually exclusive with
  overdub. The Morph-view drop (old R18) is withdrawn.
- **Revision 4 (2026-10-12):** F-A..F-G answered: re-stamp only on fired
  steps (or would-have-fired when muted); `48` dropped from the roll list;
  overlay switch rules; live erase defined per mode (FX-sequence erase in FX
  mode, nothing for SEQ in PERF, nothing in Load/Save and global settings);
  PLAY/SEQ chords during erase; the menu value is the recorded value.
- **Revision 5 (2026-10-12):** F-H..F-K answered:
  - stored roll indices shift with no migration;
  - linked Scenes stay up to date during the take (FX-sequence erase fans
    out, through a deferred reconcile);
  - FX knob erase covers the track automation and the FX lane;
  - SOM_GEN is ignored;
  - `menu_knobClearTarget()` is the single target resolver.
- **Revision 6 (2026-10-12):** module-by-module pass over the code each
  change touches. Adds the findings in §3.3, the change catalogue §19, a
  revised API (§4.2), the FX-erase mechanism (§10.4), RAM (§14), phases
  (§15), and new follow-ups (§18).
- **Revision 7 (2026-10-12):** F-L..F-Q answered:
  - the step reset rule (§5.8);
  - slow rolls and flams are recorded as individual steps (§8.8);
  - two more pre-existing roll defects (§3.3-15/16);
  - the voice-queue reset (§19.15);
  - new follow-ups F-R and F-S.
- **Revision 8 (2026-10-12):** F-R and F-S accepted as recommended. No
  follow-ups remain open.
- **Revision 9 (2026-10-13):** schedule review. `S081_P1_IMPLEMENTATION.md`
  was corrected (revision 2). Appendix A records every correction. Plan body
  corrections, for plan errors and items the user asked to fix in both
  documents:
  - §5.2 (track length);
  - §5.3 and §19.1-4 (realign keeps and realigns the FX sequence);
  - §14 (RAM);
  - §19.2 (value source, take boundary);
  - §19.6 (override call);
  - §19.15 (reset ordering, atomic mask).

  Item 15 (looper) is open, pending a decision.
- **Revision 10 (2026-10-13):** looper decisions.
  - **L-2 decided:** record and live erase no longer lock the looper out,
    in either direction.
  - **L-1:** the timing analysis for `trk` loop anchors is recorded
    (Appendix A-15).
  - New follow-ups L1–L4 (§18).
- **Revision 11 (2026-10-13):** L1 kept (`trk` gives per-track loop
  anchors). L3 adopted in P1: step-end writes and clears only on real
  changes, which also helps without the looper. Looping while recording
  (L-2, L2, L4) moves to its own plan, `S081_P2_LOOP_WHILE_RECORD.md`. P1
  keeps the S080 looper/record lockout as an interim state.

General plan; no code has changed. All follow-ups are decided; the
implementation schedule (`S081_P1_IMPLEMENTATION.md`) is next. §19 is the
per-module change list the schedule will be cut from.

**LED words in this document (user's meaning):**

| Word | Meaning | Code |
|---|---|---|
| flash | continuous | `led_setBlinkLed()` |
| blink | one transient pulse that returns to the previous state | `led_pulseLed()` |

The code names are the reverse of these.

Authority read: `MEMORY.md`, `SCOPING_TARGETS.md` §4.3/§4.3a/§4.3b/§4.8/§6.0,
`080_SESSION_HANDOFF_LOG.md` §2, `079_SESSION_HANDOFF_LOG.md` §2.1,
`PATTERN_DYNAMIC_STACK.md` §4–§6 and §12, `EFFECTS_BUS_REFERENCE.md` §6 and
§10–§11, `MODULE_INTERCHANGE_SPEC.md` (PERF surface, Sequencer, MidiParser).

---

## 1. Requirements

| # | Requirement |
|---|---|
| R1 | Overlay triggers (VOICE with BAR `<`/`>` in PERF) record an active step (note, velocity, roll rate, roll length) at the track's position, quantized or un-quantized (micro-timing delay). |
| R2 | MIDI notes record the same way (later; API unchanged). |
| R3 | Knob edits of Instrument, Effect and Scene parameters record automation onto the recording track `rtk` under the LATCH rules (§9.3). |
| R4 | MIDI CC automation, same rules (later; API compatible). |
| R5 | PERF-page actions are not recorded. |
| R6 | One recording-target Scene: the played Scene of `rtk`. Anything belonging to another Scene is not recorded. Best effort; playback always has priority. |
| R7 | `rtk` follows every active-track change in any mode. SHIFT+REC can override it until the next track selection. At boot it is the active track. |
| R8 | A recorded trigger overwrites trigger, note, velocity, micro-timing and roll; never automation; never the condition except R10. |
| R9 | A quantized flam records the main on the quantize division and the grace on the earlier step with a delay; main only if both cannot be placed. |
| R10 | Any step that receives anything while PERF is held (fill active) gets `fil`, replacing its condition. |
| R11 | MIDI is separable: two thin call sites only. |
| R12 | Outside record: a menu edit of an automated Instrument parameter sounds at once and suppresses that parameter's automation until the voice's next trigger (any source), a transport change or a Scene change. For all other parameters, the menu value applies until the next automation of that parameter. A menu value is never reverted by anything. |
| R13 | Track automation of Effect and Scene parameters is set once and forgotten. An FX-sequence lock (including the FX Morph lane) applies at its step start and returns to the menu value at its step end. Lock beats track automation; a menu edit beats both until the next automation. |
| R14 | Live erase (§10). |
| R15 | Overdub: holding REC records automation only on steps where the parameter was adjusted. No latching. Never toggles the record state. Every overdub entry and exit is a new take. Works with fill. Mutually exclusive with live erase. |
| R16 | `qnt` gains `trk` (track step-grid quantize); the looper uses it for per-track loop starts. |
| R17 | Step roll table corrected to 96 PPQ, plus `64T` and `128`. |
| R18 | *(Withdrawn.)* The SHIFT Morph view is not changed by this work. Edits made in it are not recordable adjustments. Selecting a parameter there (for copy, clear or live erase) works as anywhere else. |
| R19 | Overlay defect fixes (§5.7): the BAR overlay LED shows only in PERF (the state is kept for re-entry); switching overlays while a VOICE trigger is held updates the held trigger/roll. |

---

## 2. Decision log

| Item | Decision (user, 2026-10-12) |
|---|---|
| Q1 | Non-target-Scene items are dropped. |
| Q2 | Master-clock quantize plus `trk`; `rnd` run mode snaps backwards only. |
| Q3 | Per-PPQ-tick µs stamp. MIDI timestamping and realtime-MIDI queue-skip priority are deferred to the MIDI rework. |
| Q4 | Superseded by R12 and LATCH. |
| Q5 | Dedicated step-boundary ring. |
| Q6 | Per-entry `patSvc` writes first; composite op only if drops are measured. |
| Q7 | Record track scale and shuffle; never length. |
| Q8 | `fil` on every step that receives anything while PERF is held. |
| Q9 | Un-quantized flam in one step: keep the main. |
| Q10 | Type mismatch across Scenes: drop. |
| Q11/F1 | Step rolls are 4× fast on hardware: fix the table and add 64T/128. |
| Q12 | `def` note: no note special. |
| Q13 | Silent refusal; the shown value does not change. |
| Q14 | `PAR_ROLL` retired in place. |
| Q15 | Erase follows the target rule. |
| Q16/F10 | Morph view unchanged; its edits are not recorded; parameter selection there works. |
| Q17/F2/F3 | R13. |
| Q18 | `rtk` only, runtime. |
| Q19/F6 | The menu value is never reverted, including at the end of recording or overdub. |
| Q20 | Muted tracks may be recorded. |
| Q21 | Arming while stopped is allowed; recording starts with playback. The same applies to live erase. |
| Q22 | Tracks 6/7 are ordinary `rtk` values. |
| F5 | Overdub records triggers; latches start fresh; entry/exit = a new take. |
| F7 | SEQ- and TRACK-button erase are held gestures (§10); SEQ erase targets the active track. |
| F8 | Erase and overdub are mutually exclusive (§10.3). |
| F9 | Looper uses `trk` for per-track loop starts. |
| F11 | Any Scene change resets latches. |
| F12 | Any trigger of the voice ends the R12 override. |
| F13 | REC LED blinks on overdub entry and exit, then shows the record state. |
| F14 | Latches cover the whole target ID space as bitmaps. |
| F-D | Own-Instrument re-stamps go only on steps of `rtk` that fire this pass, or would have fired if the track were not muted (§9.2). |
| F-A | Keep `32T`; drop `48` from the roll list. |
| F-H | No migration: stored roll indices simply shift. Backward compatibility is never a constraint. |
| F-I | Linked Scenes (VOICE edit mask) are kept up to date as the take runs: menu edits fan out as today; FX-sequence erase fans out (§10.4). Pattern data never fans out. |
| F-J | FX knob erase removes both the track automation on `rtk` and that FX lane's locks as the playheads pass. The normal SHIFT+CLEAR pot clear already clears both (`COPYCLEAR_UTILITIES.md` §8). |
| F-K | SOM_GEN (SHIFT+STEP) is incomplete future work: live erase and live record ignore it. |
| F-B | Overlay switch while held: single → roll starts repeating at the new rate; roll → single/flam stops repeating, no extra hit; single → single does nothing. |
| F-C | A recorded roll changes rate at the step containing the switch. |
| F-E/F-F | While record is on and SHIFT+CLEAR has enabled live erase: PLAY is a plain transport toggle (never the chain menu); SEQ never opens the chain menu; SEQ in PERF does nothing. Live erase does nothing in Load/Save and global settings modes. In FX mode it erases the FX sequence. §10. |
| F-G | The recorded value is the menu value, as observed. Raw playback with Morph ≠ 0 is accepted behaviour. |
| F-L | Instrument automation resets at each step's **first** trigger only: the delayed trigger for a micro-timed step; the first hit of a roll (later hits never reset); each step's renewal of a roll carried across steps. A roll that runs into the next step's time does not reset there; the next step's own trigger does. Flams and rolls slower than the step rate are recorded as individual steps (§5.8, §8.8). |
| F-M | Pattern AutoSave is gated on `liveRec_patternWriting()` (armed and running), not on armed. |
| F-N | COPY without SHIFT stays refused while armed. |
| F-O | Toggling the held overlay's own BAR off stops the roll repeating (no extra hit) and ends the recorded roll at that step. |
| F-P | Live-hold suppression of stored triggers while a recorded roll is held, only while recording. |
| F-Q | Effect/Scene edits in the SHIFT Morph view count as menu edits for R12/R13 (overlay cleared, `menuHold` set); still not recordable. Try on hardware. |
| F-R | A roll runs its own length (step interval × fill, from its delayed first hit). Only the next step's trigger cuts it: a plain hit ends it, a matching roll carries it, a different roll replaces it. An empty or automation-only step does not. |
| F-S | Roll steps take the track's shuffle as well as micro-timing (one timing rule for every trigger). |
| RAM | Approved, including follow-ons (§14). |
| L-2 | (2026-10-13) The looper is **not** locked out by record or live erase. Engaging the looper no longer turns record off, and REC works normally while looping. Looper actions are not recorded; looping only moves track playheads, and recording or erasing follows the playhead wherever it is. Supersedes the S080 rule "looping and recording are mutually exclusive". **Implemented in P2** (`S081_P2_LOOP_WHILE_RECORD.md`), together with L2 and L4. P1 keeps the S080 lockout until then (revision 11). |
| L1 | (2026-10-13) Kept: with `qnt trk`, each looped track's loop starts at its own last step start (a per-track anchor computed in `seq_loopSnapshotTracks()`). `off` keeps the fastest-track anchor; 1/8..1/64 keep the grid boundary. Anchors choose only which window a track replays; step timing under the loop is unchanged (Appendix A-15). |
| L3 | (2026-10-13) Adopted in P1. A step-end automation write is queued only when the step holds no entry for the target or a different value; a clear only when an entry exists. It helps without the looper too: LATCH re-stamps every fired step of `rtk` on every pass of the pattern, so an unchanged held value would otherwise rewrite the same entries every bar (§9.5). |

---

## 3. Audit of existing code

### 3.1 State today

Recording is a trigger-bit-only bridge from the fixed-grid era.
`seq_recordTrigger()` (`sequencer.c:4308`) folds every recording into bar 1
(`% NUM_STEPS_PER_BAR`). `seq_quantize()` (`:4268`) uses the retired
128-sub-step bar. No note, velocity, timing, roll or automation is recorded,
and BAR overlay triggers are not recorded.

### 3.2 Disposition

| Item | Location | Action |
|---|---|---|
| `seq_recordActive` | `sequencer.c:673` | **Keep**, written only by LiveRecord. Filesystem gates (`filesystem.c:24704`, `:25066`) and the copy/clear refusal (`copyClearSession.c:584`) stay. |
| `seq_recordTrigger()`, `seq_quantize()` | `sequencer.c:4266-4349` | **Remove**; replaced by LiveRecord and a master-clock quantizer shared with `seq_loopEventAnchor()` (`:3079`). |
| `seq_setRecordingMode()` per-track refusal | `sequencer.c:4352-4367` | **Replace** with the LiveRecord arm path and target rule. |
| Legacy live roll (`seq_setRoll` (no callers), `seq_rollState`, `seq_rollRate`, `seq_setRollRate`, block `:2310-2317`) | `sequencer.c` | **Remove** (dead). |
| `PAR_ROLL` usage | `menuPages.h:159`, `menu.c:16077`, `:16784`, `:1101` | **Retire in place.** |
| `seq_rollPpq()` table (24-PPQ values on the 96-PPQ tick) | `sequencer.c:1841` | **Fix + extend** (§5.4). |
| Erase (`seq_eraseActive`, TIM3 block, SHIFT+COPY handlers) | `sequencer.c:675`, `:2211-2222`, `:4369`; `buttonHandler.c:2421-2428`, `:2615-2624`, `:2812-2817` | **Replace** with live erase (§10). |
| REC button handler (toggles on press) | `buttonHandler.c:2377-2408` | **Modify**: toggle on release, hold = overdub, erase exclusivity (§6). |
| `PAR_QUANTISATION` / `MENU_SEQ_QUANT` | `menu.c:15973`, `:1182`, `filesystem.c:2796` | Add `trk` (5); looper loop start reads it (F9). |
| `SEQ_LED_DIRTY_REC_MAIN` | `ledHandler.c:1747` | Absolute step; draw only in the visible bar. |
| `SEQ_LED_DIRTY_REC_SUB` endpoint | `ledHandler.c:1391-1404` | **Remove** (no writer). |
| BAR overlay trigger/repeat; BAR LED on mode exit | `buttonHandler.c:830-957`, `:2088`, `:2466`, `:2490` | **Modify**: LiveRecord reports, R19 fixes. |
| PERF fill hold | `buttonHandler.c:815-828`, `:966-975`, `:2734-2742` | **Keep**; same hold delay for REC. |
| MIDI note record call / CC hook site | `MidiParser.c:1550`, `:128-145` | One `liveRec_*` call each. |
| `midiParser_playVoiceMidiNote()`, `midiParser_triggerVoice()` (no callers) | `MidiParser.c:1575`, `:1637` | **Leave** for the MIDI rework. |
| `PAR_AUTOM_TRACK` | `ParameterArray.h:103` | **Reuse** as runtime `rtk`. |
| Knob → target resolution | `menu_knobClearTarget()` (pot clear, returns Pattern target + FX lane); `va_writeAutomationFromKnob()` / `menu_sceneSettingAutomationTarget()` `menu.c:3628-3712`, `:4645` | **Reuse** `menu_knobClearTarget()` as the one resolver for record and erase (it covers VOICE incl. `7dc`, STEP `len/scl/shf`, PERF `Nvm`/`fxm`, EFFECTS rows and `mrp`). Share its 7-bit value conversion with the held-step overlay. |
| Effect step markers / overlay end rule (identity bit 11, `effects_automationStepBegin/Flush`, `pending_end`, `seq_setEffectAutomationTracks`) | `sequencer.c` (~`:2303`), `EffectsManager.c:1209-1258` | **Remove/replace** (§5.3). |
| Held FX Morph lane (holds through unlocked steps) | `EffectsManager.c` (`held_morph_*`) | **Change** to one-step (R13). |
| Scene-target overrides with no per-target clear | `presetManager.c:1160-1240`, `presetMorphEngine.c:1128-1150` | **Add** per-target clears (§5.2). |
| Effect menu edit vs overlay (not cleared; overlay beats menu) | `EffectsManager.c:556-605` | **Modify** (§5.3). |
| Instrument hold vs menu edit (S075 F3) | `sequencer.c:2608`, `presetMorph_writeRuntimeBase()` | **Modify** (§5.1). |
| `preset_applySoundParameter(…recordAutomation)`, STEP armed-automation remnant | `presetManager.c:761-823`, `buttonHandler.c:700-800` | **Leave.** |
| Stale `MODULE_INTERCHANGE_SPEC.md` rows | spec | **Fix at close.** |

### 3.3 Findings from the module pass (revision 6)

1. **A delayed trigger undoes its own step's Instrument automation**
   (pre-existing defect).
   - TIM3 queues a step's automation at the step start
     (`seq_queueStepAutomations()`, `sequencer.c:2305`).
   - A shuffled or micro-timed trigger fires later
     (`seq_trackShufflePending`, `sequencer.c:3719`).
   - Every trigger goes through `voiceControl_triggerNow()` →
     `seq_restoreAutomatedParameters()` (`MidiVoiceControl.c` ~`:170`). That
     restores the Morph base for every held parameter, including the ones
     the same step just set.
   - The foreground order is `voiceControl_processPending()` then
     `seq_drainPendingAutomation()` (`main.c:212-213`). That order is right
     for an on-time trigger and wrong for a delayed one.
   - Live record places un-quantized triggers with a delay, so re-stamped
     values on those steps would never sound. F-L.
2. **Every roll retrigger also restores the base** (pre-existing). Each hit
   of `seq_processRollTicks()` calls `seq_triggerVoice()` (`sequencer.c:3780`)
   → restore. So a step's Instrument automation lasts only until the second
   roll hit. F-L.
3. **Effect overlay owner state becomes dead.** Under R13, `owner[64]`,
   `pending_end`, `pending_track` and `owner_tracks` in
   `effects_automation_t` (184 B, `EffectsManager.c:196`) have no remaining
   reader. Removing them releases about 75 B. The static assert and
   `STORAGE_SRAM_MANIFEST.md` must be updated.
4. **Re-align does not reset Effect overlays.** `seq_realignTrackToMasterClock()`
   and `seq_realignActivePatternToMasterClock()` publish no FX reset;
   R13 says re-align clears track overlays.
5. **The PAT4 reader has no roll-range check** (`pat_rawDecode()`
   `PatternData.c:1811`), and `tools/convert_pattern_specials.py` never
   reads roll. Neither needs a change for §5.4.
6. **Two roll name tables exist.**
   - `stepRollRateNames` (`MenuText.h:266`) is the STEP list (gains
     `64t`/`128`, loses ` 48`).
   - `rollRateNames` (`MenuText.h:101`) is the legacy `PAR_ROLL` list
     (`MENU_ROLL_RATES`); it retires with `PAR_ROLL`.
   - Revision 5 named the wrong table.
7. **Pattern Stack Service queue format.** The code packs 6 bits of
   operation, 10 bits of track/step and 16 bits of payload
   (`PatternStackService.c:28-60`). `PATTERN_DYNAMIC_STACK.md` §12.3
   documents a different 3/4/3/7/9/7 layout. Fix the spec at close. The
   payload cannot hold a full record spec, so the staging table stands.
8. **The service target is `seq_activePattern` in eight places:**
   `patSvc_submit()` `:1203`, the submit variants `:1761`, `:1788`, `:1821`,
   `patSvc_tick()` `:1869`/`:1904`, `patSvc_idle()` `:1609`,
   `patSvc_finishSceneReplace()` `:1354`, `patSvc_endExclusive()` `:1422`.
   The target override replaces all of them with one "desired Scene" helper.
9. **Dependency direction.** `seq_loopEngage()` turns recording off inside
   the Sequencer (`sequencer.c:3214`).
   - LiveRecord depends on the Sequencer, so the Sequencer must not call
     back into LiveRecord.
   - The record-off moves to the caller in buttonHandler, before
     `seq_loopEngage()` (`liveRec_forceOff()`). P2 removes it outright
     (L-2).
   - The drain filter reads bitmaps that LiveRecord registers with the
     Sequencer (§19.1), rather than calling LiveRecord.
10. **LiveRecord must not depend on Menu either.**
    - Menu calls LiveRecord (record hook, `rtk`, active voice).
    - The value resolver (§9.2) therefore reads owner getters directly:
      SceneData `instrument_parameters[]`, `scene_getVoice*()`,
      `scene_getSlot6Track7AmpEnvelopeDecay()`,
      `scene_getEffectMorphAmount()`, Pattern track scale/shuffle,
      EffectsManager retained values.
    - The 7-bit conversions it needs move out of `menu.c` into
      SceneModTargets / EffectTypes helpers.
11. **STEP toggles bypass the Pattern Stack Service.**
    `buttonHandler_setRemoveStep()` writes bit 15 directly
    (`pat_toggleStep()`). The Q13 refusal therefore has to be an explicit UI
    check; service admission alone does not catch it.
12. **Copy/clear refuses while armed** (`copyClearSession.c:584`), including
    while stopped. With Q21 (arm while stopped) that refusal stays, by
    decision F-N.
13. **Pattern AutoSave is suspended while `seq_recordActive`**
    (`filesystem.c:24704`, `:25066`), even while stopped. F-M.
14. **`led_pulseLed()`** inverts the LED for `LED_PULSE_TIME_MS` (50 ms),
    then re-renders the stack. That is the "blink" for overdub entry/exit.
    If 50 ms is too subtle on hardware, use a longer one-shot.
15. **Roll steps ignore micro-timing and shuffle** (pre-existing defect
    against S079 §2.1, "Roll starts at delayed position").
    - In `seq_advanceTrackStep()` (`:2253`), `sp.roll > 0` calls
      `seq_rollBegin()` at the step start. The shuffle/timing deferral
      (`:2262-2288`) is only reached for non-roll steps.
    - A recorded un-quantized roll would therefore play early. F-L's
      delayed-roll rule needs the fix (§5.8).
16. **Any step without a roll ends a running roll at its start**
    (`:2203-2206`: `sp.roll == 0` clears `seq_rollActive`), even an empty
    step. Once rolls can be delayed, a roll pushed into the next step's time
    would be cut at that boundary. F-R.

---

## 4. Module ownership

### 4.1 `Core/Sequencer/LiveRecord.c/.h` (prefix `liveRec_`)

| Owns | Does not own |
|---|---|
| Mode (OFF / LATCH / OVERDUB / ERASE), arm-while-stopped, REC hold resolution | Transport, DDA, projection (Sequencer) |
| `rtk` follow/override | Block encoding (PatternData) |
| Target Scene negotiation | Admission/handover (PatternStackService) |
| Trigger → (step, specials) resolution | Sound (overlay/MIDI fire their own triggers) |
| Latch, dirty and erase bitmaps; step-end write/clear/re-stamp | Commit, fan-out, Morph, DSP |
| "Is this target's playback suppressed" query | MIDI parsing, routing, CC mapping |
| Drop counters | |

`liveRec_service()` runs in the foreground 500 Hz pass beside
`patSvc_tick()`. Capture entry points are foreground-only.

### 4.2 Public API (draft, revision 6)

Gesture timing (REC hold, SHIFT/CLEAR chord) stays in buttonHandler, like the
fill hold. LiveRecord receives decided events.

```c
void    liveRec_init(uint8_t activeTrack);                 /* main.c */
void    liveRec_service(void);                             /* timebase, before patSvc_tick() */

/* Mode (buttonHandler) */
void    liveRec_setLatch(uint8_t on);                      /* REC short press */
void    liveRec_overdubBegin(void);  void liveRec_overdubEnd(void);
void    liveRec_forceOff(void);                            /* looper engage (P1 interim) */
void    liveRec_eraseBegin(void);    void liveRec_eraseEnd(void);
uint8_t liveRec_mode(void);                                /* OFF LATCH OVERDUB ERASE */
uint8_t liveRec_armed(void);                               /* mode != OFF (gates) */
uint8_t liveRec_patternWriting(void);                      /* armed && seq_isRunning() */

/* Recording track (Menu) */
void    liveRec_onActiveTrackChanged(uint8_t track);
void    liveRec_setRecordTrack(uint8_t track);
uint8_t liveRec_recordTrack(void);
uint8_t liveRec_activePatternLocked(void);                 /* Q13: T != A */

/* Triggers (buttonHandler overlay now, MidiParser later) */
void    liveRec_triggerIn(const liveRec_trigger_t *t);     /* each hit, when it sounds */
void    liveRec_rollRateChange(uint8_t track, uint8_t rollIndex);
void    liveRec_rollRelease(uint8_t track);

/* Automation (Menu now, MidiParser later) */
void    liveRec_paramIn(uint16_t target9, uint8_t source); /* recordable gesture, incl. clamped */

/* Erase gestures (buttonHandler / Menu) */
void    liveRec_eraseParam(uint16_t target9, uint8_t fxLane);
void    liveRec_eraseSeqHeld(uint8_t held);                /* VOICE/STEP: active track; FX: FX sequence */
void    liveRec_eraseTrackHeld(uint8_t track, uint8_t held);
```

- `liveRec_trigger_t`: track, note, velocity, kind (SINGLE / FLAM_GRACE /
  FLAM_MAIN / ROLL_START), step roll index, source, `note_is_default`.
- Each overlay hit is reported when it sounds (the flam main 1/32 later
  from `buttonHandler_overlayTick()`). LiveRecord pairs grace and main for
  R9.
- `liveRec_paramIn()` carries no value: the step-end write reads the
  current retained value (§9.2, §3.3-10).
- MidiParser includes `LiveRecord.h` and calls only `liveRec_triggerIn()`
  and `liveRec_paramIn()`.

### 4.3 Services needed elsewhere

Detailed per module in §19. Summary of the edges:

```
buttonHandler ──► LiveRecord ──► Sequencer (capture, projection, quantise,
Menu ──────────► LiveRecord        ring, echo/live-hold/erase masks, filter)
MidiParser ────► LiveRecord ──► PatternStackService (record/erase ops, target)
                 LiveRecord ──► EffectsManager (FX-sequence erase), SceneData getters
Menu ──► Preset / EffectsManager / Sequencer (R12/R13 menu-edit rules)
```

LiveRecord never calls Menu, buttonHandler or MidiParser, and the
Sequencer never calls LiveRecord.

---

## 5. Behaviour changes outside record (prerequisites, Phase A)

### 5.1 Instrument parameters: menu edit beats automation until the next trigger

- A menu edit of Instrument parameter `(slot, local)` writes the sounding
  value now: the endpoint is stored and interpolated at the Morph amount, as
  today. It also sets an **override bit** and clears the held bit.
- While the override bit is set, the drain skips that target.
- The override ends:
  - at **any** trigger of that voice (sequencer, overlay, MIDI, preview: the
    single `voiceControl_triggerNow()` funnel);
  - at transport start, stop or reset;
  - at a Scene change.

  Ordering requirement: the override must clear before the triggering step's
  own automation applies (R-5).
- The Morph sweep guard is unchanged.
- This amends the binding S075 F3 rule (update `MEMORY.md` and
  `PATTERN_DYNAMIC_STACK.md` §6.2a).
- MIDI CC keeps its current path until the MIDI rework.

### 5.2 Scene targets: set once and forget; menu applies until the next automation

| Target | Today | Change |
|---|---|---|
| Voice Morph `Nvm` | menu edit clears the override | none |
| FX send `Nfx`, audio out `Nou` | override persists until transport restart | menu edit clears that slot's override |
| Track length/scale/shuffle (405..425) | override persists | menu edit clears that track's override and refreshes the effective cache (length added in revision 9, Appendix A-21) |
| `fxm` (Pattern track) | holds until the Scene-rule reset | menu edit clears it; an FX Morph-lane lock beats it for one step (§5.3) |

### 5.3 Effect parameters (R13)

Per Effect parameter `i` (the Morph lane works the same way, with `fxm` as
its track overlay):

```
lock(i)     FX-sequence step locks lane i: applies at that FX step's start,
            released at that step's end
overlay(i)  last Pattern-track automation of i: set once, held
menuHold(i) a menu edit of i since the last automation of i

eff(i) = menuHold(i) ? menu(i)
       : lock(i)     ? lock value
       : overlay(i)  ? overlay value
       : menu(i)
```

- **A track overlay** is replaced by another track's automation of `i` (last
  writer wins). It is cleared by:
  - an FX lock on `i` (when that lock step starts), so after the lock step
    `i` returns to the **menu** value;
  - a menu edit of `i`;
  - a Scene change, reload or re-align (transport reset, double-click
    realign, Bank/Scene load). A re-align also realigns the FX sequence
    position to the master clock; it does not reset it (Appendix A-5).
- **An FX lock** is per step only. The former held Morph lane (a Morph lock
  holding through later unlocked steps) becomes one-step like every other
  lane.
- **A menu edit** clears the overlay and sets `menuHold(i)`. The next
  automation of `i` (an FX lock step start, or a track automation entry)
  clears `menuHold(i)` and applies. A lock that is already running when the
  menu edit happens stays overridden for the rest of its step.
- In `sel` run mode, "lock step start" = the step being selected, and "end"
  = another step being selected.
- Removed:
  - step markers (identity bit 11), `pending_end`, owner publication;
  - the "failed-condition/muted steps keep overlays" rule;
  - `held_morph_*` hold-through.
- Cost: `menuHold` 64-bit mask (8 B); releases about 10 B of marker state.

### 5.4 Step roll rates (R17, confirmed 4× fast on hardware)

`seq_processRollTicks()` runs on the 96-PPQ tick, and the new table is in
96-PPQ ticks:

| Index | Label | Ticks |
|---|---|---|
| 0 | off | 0 |
| 1 | 1/8 | 48 |
| 2 | 8T | 32 |
| 3 | 16 | 24 |
| 4 | 16T | 16 |
| 5 | 32 | 12 |
| 6 | 32T | 8 |
| 7 | 64 | 6 |
| 8 | 64T | 4 |
| 9 | 128 | 3 |

- Every existing step roll slows to its labelled rate.
- `48` is dropped (same rate as `32T`, F-A). The indices after it move down
  by one. Stored bytes are not migrated (F-H).
- Also update `stepRollRateNames` (`MenuText.h:266`), the STEP clamp
  (`menu.c:5790`) and `menu_getRollRateName()` (`menu.c:9978`). The PAT4
  reader and the converter need nothing (§3.3-5, §3.3-6). Byte format
  unchanged.

### 5.5 *(withdrawn — former Morph-view drop)*

### 5.6 `qnt trk` (R16)

- The list becomes `off 1/8 1/16 1/32 1/64 trk`.
- Recording: snap to the nearest step start of the recorded track's own grid
  (delay 0).
- Looper: each looped track's loop start is its own current step start.

### 5.7 Overlay defects (R19)

- **BAR LED:** the overlay LEDs are shown only in PERF.
  - Leaving PERF restores the target mode's normal BAR LED state.
  - `bh_overlayActive` is kept, and its LED returns on PERF re-entry.
  - Today the toggled LED hangs in other modes.
- **Switch while held:** with a VOICE held for an overlay trigger, pressing
  the other BAR switches the overlay (existing). The held trigger now adopts
  the new overlay's type and rate:
  - roll → roll: the repeat interval changes from the next hit;
  - single → roll: repeating starts at the new rate from the switch;
  - roll → single or flam: repeating stops, no extra hit;
  - single → single: nothing (the hit already fired).
  - Recording: §8.8.

### 5.8 Step reset rule: when Instrument automation resets (F-L)

**Rule:**

- **Reset points.** Instrument automation (the held bits and the R12
  override) resets at a step's first trigger only:

  | Step | Reset at |
  |---|---|
  | Plain trigger | its hit |
  | Micro-timed / shuffled trigger | its delayed hit |
  | Roll step | its first hit; later hits of that roll never reset |
  | Roll carried across steps | each step's renewal of the roll, even though the renewal fires no new first hit |

- **No reset:** a roll whose hits run into the next step's time does not
  reset there. The next step's own trigger resets, whether it is a plain
  trigger or another roll.
- **Live triggers** (overlay presses, MIDI notes, preview) reset at each new
  press, per F12. Each flam hit counts as a press, matching its recording as
  two steps. Overlay roll repeats from the 1 ms timer do not reset.

**Mechanism (replaces "every trigger restores"):**

- **Voice queue reset.**
  - `seq_restoreAutomatedParameters()` stays in the trigger funnel, but runs
    only for a voice with a pending reset (per-voice mask, §19.15).
  - The Sequencer requests a reset only for first triggers.
  - A roll renewal requests a reset with no voice trigger: the restore and
    override clear only.
  - Live triggers always request one.
- **Deferred automation.** A step whose first trigger is deferred (shuffle
  or micro-timing) queues its automation entries at the deferred fire time,
  in TIM3, together with the resetting trigger, not at the step start.
  - The foreground order `voiceControl_processPending()` →
    `seq_drainPendingAutomation()` (`main.c:212-213`) then gives
    reset → trigger → that step's automation, as for on-time steps.
  - A deferral that is flushed early (the next step fires first, existing
    flush at `:2257`) queues its entries at the flush.
  - Non-trigger steps keep queueing at the step start.
- **Roll steps honour micro-timing and shuffle** (§3.3-15).
  - A roll step goes through the same deferral as a plain trigger. When it
    fires, it calls `seq_rollBegin()`, which fires the first hit with RESET.
  - The deferral state gains the step index (to re-read the block) and a
    roll flag.
- **Spilled rolls (F-R):**
  - A roll runs its own length: the step interval × fill, measured from its
    delayed first hit.
  - Only the next step's **trigger** cuts it short: a plain hit ends it, a
    matching roll carries it, a different roll replaces it.
  - An empty or automation-only next step does not cut it.
- **Shuffle (F-S):** roll steps take the track's shuffle as well as
  micro-timing, like every other trigger.

---

## 6. Modes and the REC button

| Gesture | Result |
|---|---|
| REC short press (released before `DOUBLE_CLICK_TIMEOUT`, 300 ms, the PERF fill delay) | Toggles LATCH on/off **on release** |
| REC held past the delay | OVERDUB while held. On release the prior state (LATCH or OFF) returns. Entry and exit each start a new take (latches reset). |
| SHIFT+REC | Latched settings menu (unchanged); REC in the menu closes it without toggling |
| REC while looping | P1: ignored, and engaging the looper turns record off (the S080 rule, kept as an interim). P2 lifts it (L-2, `S081_P2_LOOP_WHILE_RECORD.md`) |
| SHIFT+CLEAR while LATCH or OVERDUB | Live erase (§10); disables overdub |

- Armed while stopped: nothing happens until playback starts; playback then
  starts recording (or erasing).
- REC LED:
  - on in LATCH, off otherwise;
  - **blinks** on overdub entry and on overdub exit, then shows the record
    state;
  - **flashes** while the SHIFT+REC menu is open and armed (S080 reserved
    rule).
- Fill (PERF hold) and REC hold may be held together: overdub writes get
  `fil`.

---

## 7. Recording-target Scene (R6)

`P(t)` = track `t`'s played Scene; `A` = active Scene; target `T = P(rtk)`.

1. **Target:** while armed, `T = P(rtk)`.
   - If `T ≠ A`, request `patSvc_requestTarget(T)`.
   - Release when REC/erase ends, or when `T == A`.
2. **Ownership:**

   | Item | Recorded only if |
   |---|---|
   | Trigger on track `t` | `P(t) == T` and the slot type in `T` matches `A` |
   | Instrument parameter of slot `s` | the slot's played Scene `== T`, types match |
   | Effect parameter, Scene parameter | `T == A` |
   | Erase on track `t` | `P(t) == T` |

3. **Handover:** everything is dropped during a handover.
4. **Scene changes:** any Scene change (direct, queued, chain, per-track
   reassignment, realign, load):
   - re-evaluates `T`;
   - resets latches;
   - discards pending step-end work and open rolls for the old Scene.
5. **Refusal:** while `T ≠ A`, STEP edits of `A` are refused before the
   display changes; no message.

---

## 8. Trigger recording (R1, R2, R8–R10)

### 8.1 Gates

LATCH or OVERDUB; transport running; looper not engaged (P1 interim; P2
removes this gate, L-2); §7 ownership.

### 8.2 Capture

At the moment the trigger sounds: master tick plus a µs sub-tick fraction.
MIDI uses processing time until the MIDI rework.

### 8.3 Placement

- **Un-quantized:** the latest step whose played start (nominal + shuffle)
  is at or before `t`.
  - Delay = `(t − played_start) × 100 / interval`, clamped 0..99.
  - A rounded 100 goes to the next step at delay 0.
- **1/8..1/64:** snap on the master clock (48/24/12/6 ticks), then place as
  above.
- **`trk`:** nearest step start of the track's grid.
- **Run modes:** fwd/rev/pip project forward. `rnd` never snaps forward.

### 8.4 Written fields

One `pat_recordStep()`:

| Field | Value |
|---|---|
| bit 15 | on |
| note | played note (`def` → none) |
| velocity | played velocity |
| timing | delay or cleared |
| roll / fill | §8.8, else cleared; rhythm cleared |
| condition | kept; `fil` when fill is active |
| automation | kept |

Last write wins within a pass; a grace never replaces its own main.

### 8.5 `fil`

Any step receiving a trigger, an automation write or a re-stamp while fill is
active gets `fil`. Clears and erases do not set it.

### 8.6 Echo suppression

A write ahead of the playhead in the current pass calls
`seq_suppressStepOnce()`. TIM3 skips that trigger once (automation still
plays). The mark expires at the next pass, a Scene change, transport stop, or
a length or run-mode change.

### 8.7 Flam

Grace (velocity 64) and main (velocity 100) are 12 ticks apart.

- **Quantized:** main at the snap, grace 12 ticks earlier. If they map to
  different steps, write both; otherwise the main only.
- **Un-quantized:** each hit at its own time. If both land in one step, keep
  the main.

### 8.8 Roll

| Overlay | `r/8` | `r16` | `r32` | `r64` | `r6t` | `128` |
|---|---|---|---|---|---|---|
| Step index | 1 | 3 | 5 | 7 | 8 | 9 |

- **Slow rolls are recorded as individual steps (F-L).** If the roll's
  interval is equal to or longer than the track's step interval (for
  example `r/8` on a 1/16 track), it is recorded as hits, not as a roll
  special.
  - Each hit is a separate single-hit capture with its own placement,
    quantize and `fil`.
  - Flams are always two individual steps (§8.7).
  - The rules below apply only to rolls faster than the step.
- The start is placed like a single hit. Length = hold time.
- Released in the start step: fill = elapsed % of the step.
- Spanned steps: trigger + rate, fill 100 (carry-over); the last step gets
  the release fill.
- Released before the second hit: a plain trigger.
- Ended early by a Scene change, stop, REC off or `rtk` change.
- **Overlay switch while held (R19):** the roll is split at the step
  containing the switch. That step and later ones take the new rate; earlier
  spanned steps keep the old rate (F-C). Roll → single/flam ends the
  recorded roll at that step; single → roll starts a recorded roll there.
  The slow-roll test is made per segment: after a switch to a slower rate,
  the new segment's hits are recorded as individual steps.
- **Overlay toggled off while held (F-O):** repeating stops (no extra hit)
  and the recorded roll ends at that step.
- **Live hold (F-P):** while recording, the track's stored triggers are
  skipped as the playhead passes during the hold; automation still plays.

### 8.9 MIDI (later)

`midiParser_noteOn(…, do_rec)` → `liveRec_triggerIn(kind = SINGLE)`.

---

## 9. Automation recording (R3, R4, R15)

### 9.1 Recordable sources

Targets are resolved by `menu_knobClearTarget()`, the pot-clear resolver:

- VOICE-page Instrument cells;
- `Nvm`, `Nou`, `Nfx`, `7dc`;
- EFFECT-page AUTOMATABLE rows and `mrp` (`fxm`);
- SEQ-page `scl`, `shf`;
- later: external CC.

Not recorded:

- PERF, RECORDING, global and Load/Save pages;
- step editors and held-step overlays, FX lane locks;
- LFO/velocity target cells, `typ`;
- SHIFT Morph-view edits;
- `len`;
- non-automatable targets.

The Morph view and PERF knobs are not recordable, but they can still
**select** a parameter for copy, clear and live erase.

### 9.1a Terms

- **Adjusted:** any user edit gesture, including a detent clamped at min or
  max.
- **Own Instrument parameter:** an Instrument parameter of the slot that
  `rtk` plays (tracks 6 and 7 both map to slot 6). Everything else is
  **foreign**.
- **Fired step (F-D, F-L):** a step of `rtk` whose **first trigger** fired
  this pass: bit 15 set and its condition passed. That trigger is the
  on-time hit, the delayed hit, the roll's first hit, or the step's renewal
  of a carried roll. On a muted track, a step that *would* have fired counts
  too.
  - Every step a multi-step roll spans is therefore a fired step, and gets
    the own-Instrument re-stamp (F-L: "automation must be re-written at each
    step's renewal of the roll").
  - A step recorded this pass and echo-suppressed counts as fired: the live
    hit was its sound.
  - Steps whose condition fails this pass are not fired.
  - TIM3 marks each boundary-ring entry with a `fired` bit.
  - Muted tracks skip condition evaluation today (`process_step =
    !track_muted`, `sequencer.c:2151`). For a muted `rtk` only, TIM3
    evaluates the condition **without side effects**: no update of
    `seq_trackLastConditionFired`, so `lst`/`!ls` after unmuting is
    unchanged. A probability condition takes a fresh RNG draw.

### 9.2 State: three bitmaps over the 9-bit target ID space

One bit per target ID 0..511: 64 B each, indexed directly. The 480 usable
IDs would pack into 60 B; direct indexing costs 4 B more and needs no
mapping.

| Bitmap | Set by | Cleared by | Meaning |
|---|---|---|---|
| `latch` | first adjustment in LATCH | new take: REC off, overdub entry/exit, track or `rtk` change, any Scene change | the parameter owns its automation on `rtk` for this take |
| `dirty` | every adjustment (LATCH and OVERDUB), including clamped detents | the step-end write | adjusted since its last write |
| `erase` | knob turn during live erase | erase end | automation of this target is being erased from `rtk` |

- The value written is always the parameter's **current menu value**
  (`liveRec_targetValue7()`, §19.2). No values are stored.
- A second bit (`dirty`) is still needed beside `latch`. On a step that is
  not active, the rule is "write if adjusted since the last write, else clear
  an existing entry". `latch` alone cannot tell those apart. Without
  `dirty`, every passed step would either be written (not conservative) or
  cleared (losing the adjustment).

### 9.3 LATCH mode (REC toggled on)

- **Live sound:** a latched parameter's menu value always sounds. The drain
  skips every track's automation of it while latched.
- At the end of each step `N` of `rtk`, for every set bit in `latch`:

| End of step N | Own Instrument parameter (slot played by `rtk`) | Foreign (other slot, Effect, Scene) |
|---|---|---|
| `dirty` | write menu value, clear `dirty` | write menu value, clear `dirty` |
| not `dirty`, N **fired** (F-D) | write menu value (re-stamp) | clear an existing entry |
| not `dirty`, N not fired, entry exists | clear | clear |
| otherwise | nothing | nothing |

- New take (REC off etc.): a pending `dirty` write for the current step is
  flushed first.
- The menu keeps its value; afterwards R12 applies as after any menu edit.

### 9.4 OVERDUB mode (REC held)

- At the end of step `N`, every `dirty` target is written to `N` (menu
  value) and `dirty` is cleared. Nothing latches; nothing is cleared;
  nothing is re-stamped.
- **Live sound:** playback automation plays. An adjusted parameter follows
  the normal menu rules (R12): its menu value stays and is never reverted.
  - An Instrument parameter's automation is suppressed until the voice's
    next trigger.
  - Other parameters sound at the menu value until their next automation.

### 9.5 Write volume

- **Only real changes are written (L3).**
  - A write is queued only when the step has no entry for the target or a
    different value.
  - A clear is queued only when an entry exists.
  - One foreground `pat_readStepAutomations()` read of the ended step
    answers all latched targets.
  - Without this, LATCH would rewrite every fired step's re-stamps on every
    pattern pass even when nothing changed. That is the bulk of the write
    traffic in long takes, with or without the looper.
- One block replacement per write or clear that remains.
- Latching many parameters on a fast `rtk` can exceed the busy drain (one
  event per 2 ms, 64-entry queue).
- Drops are counted. A composite per-step op is built only if measured
  (Q6).
- Unbounded latches (§9.2) make this more likely than the old 8-entry table
  did (R-2).

---

## 10. Live erase (R14)

### 10.1 Entry and exit

- **Enter:** SHIFT+CLEAR (the COPY/CLEAR button) while LATCH or OVERDUB.
  Overdub ends; latches reset.
- SHIFT and CLEAR LEDs stay lit while either is held.
- Erase stays enabled until **both** are released.
- Outside record, SHIFT+CLEAR keeps its S075 clear meaning.
- While erase is enabled:
  - **PLAY** is a plain transport toggle, never the SHIFT+PLAY chain chord;
  - **SEQ** never opens the chain menu.
- While stopped, nothing passes. Starting playback (PLAY above) starts with
  erase enabled.
- A mode change while erase is enabled keeps erase enabled. Gestures then
  follow the new mode, and any held SEQ/TRACK erase stops. In Load/Save or
  global settings mode it does nothing until the user leaves.

### 10.2 Gestures

The three gesture kinds:

| Gesture | Duration | Effect as the playhead passes |
|---|---|---|
| **Knob:** turn a knob over an automatable target (selection, not adjustment; works in the Morph view and on PERF Morph cells) | until erase ends (both released) | The parameter is not adjusted. Its target joins `erase`. The drain skips it on `rtk`, and each passed step of `rtk` has its entry removed before it plays. |
| **SEQ hold** (VOICE, STEP) | while held | On the **active track**: triggers and specials removed, not played. The condition is kept on steps with automation; automation still plays. |
| **SEQ hold** (FX) | while held | On the **FX sequence**: every lock (all lanes, including Morph) of each FX step the FX playhead reaches is removed before it applies; linked Scenes follow (§10.4). |
| **TRACK hold** | while held | On **that track**: everything removed (trigger, specials, condition, automation), not played. TIM3 clears bit 15, queues `DELETE_DYNAMIC`, skips that step's automation. |

Per mode (presses are consumed: no step toggle, voice select, mute, overlay
trigger, Scene change or FX hook):

| Mode | Knob | SEQ hold | TRACK hold |
|---|---|---|---|
| VOICE | Instrument cells, `Nvm` `Nou` `Nfx` `7dc` | active track triggers/specials | whole track |
| STEP | `len`, `scl`, `shf` (selection only; `len` is erasable though not recordable) | active track triggers/specials | whole track |
| PERF | `1VM`..`6VM` (`Nvm`); `fxm` (track automation + FX Morph lane, as in FX mode); `mrp` has no automation target, so nothing | **nothing** (SEQ = Scenes in PERF) | whole track |
| FX | Effect parameters and `mrp` (Effect Morph): track automation on `rtk` **and** that FX lane's locks, as each playhead passes | FX sequence locks | whole track |
| SOM_GEN | nothing (ignored, F-K) | nothing | nothing |
| Load/Save, global settings (MENU) | nothing | nothing | nothing |

- Releasing a SEQ or TRACK button stops its erase. Another press restarts
  it while SHIFT or CLEAR is still held.
- Erase obeys §7 ownership: Pattern erase only on tracks playing `T`; FX
  sequence and Effect/Scene targets only when `T == A`.

### 10.3 Exclusivity with overdub

| Sequence | Result |
|---|---|
| Erase enabled, then REC held | Nothing; SHIFT/CLEAR LEDs stay lit |
| REC held (overdub), then SHIFT, then CLEAR | Overdub disabled, erase enabled. Releasing REC later restores the prior LATCH/OFF state without a toggle. |

### 10.4 Linked Scenes during a take (F-I)

Scenes in the active Scene's VOICE edit mask are kept up to date while
recording or erasing.

- **Menu edits** (Instrument, Kit, Scene settings, Effect parameters) fan
  out at commit, exactly as outside record. Nothing new is needed.
- **Pattern data** (recorded triggers, automation writes and clears, Pattern
  erase) never fans out. This is the binding edit-mask rule.
- **FX-sequence erase** (SEQ hold in FX mode, FX-lane part of a knob erase):
  - LiveRecord tells EffectsManager which lanes are being erased
    (`effects_setLiveEraseLanes(laneMask)`).
  - At each FX step start, EffectsManager excludes those lanes' locks from
    resolution for that step, so they never sound, not even for one render
    block. It sets the (step, lanes) in its pending mask (16 × 16 bits,
    32 B).
  - `liveRec_service()` takes the pending mask (`effects_takeErasedLocks()`)
    on its next pass (≤ 2 ms) and calls the existing
    `effects_clearSeqLanes(sceneIndex, stepMask, laneMask)`. That clears
    the retained locks in the origin Scene and every linked same-type Scene
    (`effects_fanoutMask()`).
  - The take carries its origin Scene index. A flush after a Scene change
    still fans out correctly, because `effects_fanoutMask()` uses the
    inactive Scene's own indexed edit-mask entry
    (`bank_sceneMaskVoiceEditForScene()`).
  - The service also flushes when erase or recording ends.

---

## 11. SHIFT+REC menu and `rtk`

RECORDING_PAGE sub-page 2:

| Short | Long | Values | Storage |
|---|---|---|---|
| `rtk` | RecTrk | 1..7 | runtime (`PAR_AUTOM_TRACK`) |

- Boot: the active track.
- Every `menu_setActiveVoice()` sets `rtk`.
- The user can edit it until the next track selection.
- Each change is a new take.

---

## 12. UI feedback

- REC LED per §6.
- STEP LEDs mark recorded triggers (absolute step, visible bar).
- Recorded and erased targets update the automation underline search bits.
- No new LCD text; Q13 refusals show nothing.

---

## 13. Deferred out of this session

- Automation `hold` reconciliation (SCOPING §4.3a); R12/R13 likely settle
  it. Confirm at close.
- Step-record while stopped.
- Recording FX-sequence locks.

---

## 14. RAM and flash (approved 2026-10-12; revised in revision 6)

Normal SRAM1, static. The schedule fixes exact bytes.

| Item | Bytes | Owner |
|---|---|---|
| `latch`, `dirty`, `erase` bitmaps (3 × 64) | 192 | LiveRecord |
| Mode, `rtk`, target `T`, take origin Scene, playback-epoch copy, drop counters | ~16 | LiveRecord |
| Open roll capture (track, start tick, current step, rate) | ~8 | LiveRecord |
| Pending flam grace (track, step, delay, velocity) | ~4 | LiveRecord |
| Record staging slots (4 × 8) + cursor | ~34 | PatternStackService |
| Target request / desired-Scene state | ~2 | PatternStackService |
| Step-boundary ring (16 × 2: step, track, fired) + indices | ~34 | Sequencer (TIM3 writer) |
| Echo-suppression step per track | 7 | Sequencer |
| Live-hold mask, erase masks (specials, full), published `rtk`/writing | 5 | Sequencer |
| PPQ µs stamp + period | 8 | Sequencer |
| Deferred trigger: step index + roll flag per track (§5.8) | 7 | Sequencer |
| Per-voice pending-reset mask (§19.15) | 1 | MidiVoiceControl |
| Playback epoch counter | 1 | Sequencer |
| Instrument menu-override bitmap, 6 × `u64` (§5.1) | 48 | Sequencer |
| Effect `menuHold` (64-bit + Morph bit) | ~9 | EffectsManager |
| FX-erase pending mask (16 × 16 bits) + erase lane mask | ~34 | EffectsManager |
| **Added** | **~410** (revision 9: ~428 in every build plus 16 B DEV-only; Appendix A, minor M-6) | |

Released (not reused, per policy):

- `effects_automation_t` owner/pending fields (about 75 B, §3.3-3);
- `held_morph`/`held_morph_valid` (2 B);
- legacy roll bytes (2 B);
- `recordSubStep`.

Flash: an estimated 8–11 kB of the 197,244 B headroom.

---

## 15. Phases

Each phase lists its §19 subsections.

| Phase | Content | §19 | Verify (hardware unless noted) |
|---|---|---|---|
| A1 | §5.8 step reset rule (per-voice reset mask, deferred automation, delayed rolls, spill rule); R12 Instrument menu override; per-target Scene clears | 19.1 (1–3, 5–7), 19.5, 19.6 (R12), 19.15 | Plain / delayed / rolled / carried / spilled steps reset exactly once at the right point; menu vs automation per class |
| A2 | R13 Effect rules: marker removal, lock-start clear, `menuHold`, one-step Morph lane, realign reset | 19.1 (4,8), 19.4 | FX lock/overlay/menu sequences incl. `sel`; Scene change/realign clears |
| A3 | Roll table + 64T/128; `qnt trk` (recording quantiser + looper); R19 overlay fixes; `PAR_ROLL` retirement | 19.1 (9,10), 19.6 (lists), 19.7 (overlay) | Roll rates by count; looper `trk`; overlay LED across modes; overlay switch while held |
| B | LiveRecord skeleton; REC press/hold/blink; arm while stopped; `rtk` cell and follow; gate moves (filesystem, copy/clear); looper record-off move; removals | 19.2, 19.7 (REC), 19.10, 19.11, 19.12 | REC gestures/LEDs; `rtk`; AutoSave runs while armed and stopped |
| C | Sequencer services: capture, projection, quantiser, ring, echo/live-hold, µs stamp, epoch, filter registration | 19.1 (1–3, 11–14) | Host test of placement math against the looper projection; build |
| D | `pat_recordStep`, `pat_eraseSpecials`, trigger policy, staging, `RECORD_STEP`/`ERASE_SPECIALS`, target override | 19.3 | Under patSvc/AutoSave load; drop counters; handover |
| E | Trigger recording incl. flam pairing, roll spans, `fil`, echo; MIDI note call site | 19.2, 19.7, 19.9 | Type × qnt × scale × run mode × shuffle |
| F | Automation: hook, value resolver, LATCH, OVERDUB, drain filter; CC call site | 19.2, 19.6, 19.9 | Own/foreign × fired/not fired × existing entries; overdub takes |
| G | Live erase (knob, SEQ, TRACK, FX sequence) and overdub exclusivity | 19.1 (erase), 19.2, 19.4, 19.7 | Per-mode table §10.2; release/re-press; stopped start |
| H | Target negotiation, Scene/epoch handling, Q13 refusal | 19.2, 19.3, 19.6 (Q13), 19.7 | Per-track Scenes; direct/queued/chain while recording; STEP edits refused silently |
| I | Specs and memory (§19.14) | 19.14 | — |

---

## 16. Deferred to the MIDI rework

- Accurate MIDI note timestamps; queue-skip priority for realtime MIDI.
- Whether MIDI CC adopts R12 menu priority (today: Normal endpoint, lowest
  priority).
- Dead helpers `midiParser_playVoiceMidiNote()` and
  `midiParser_triggerVoice()`.
- CC → target mapping (A20).

---

## 17. Risks

- **R-1 Target handover cost.** Each move of `T` drains the queue and
  rebuilds slack; actions in that window are dropped.
- **R-2 Write throughput.** Unbounded latches times one block replacement
  per write or clear per step can exceed the busy drain. Drops are counted;
  the composite op is the fallback.
- **R-3 Pool growth.** Own-Instrument re-stamps add an entry per fired step
  per latched parameter.
- **R-4 Settled behaviour changes (§5).** S075 F3 is inverted for menu
  edits, Effect markers are removed, the Morph lane holds one step only, and
  step rolls slow 4×. Existing patterns will sound different (intended);
  Phase A is signed off on hardware before record builds on it.
- **R-5 Trigger/drain ordering.**
  - For on-time triggers, `main.c:212-213` already runs the trigger funnel
    (restore, override clear) before the drain applies that step's entries.
  - Delayed triggers and roll retriggers break this today (§3.3-1/2). §5.8
    fixes it in Phase A1, before R12 and recording rely on it.
  - The reset must survive the voice queue's drop path (`noteOff` deletes
    queued entries for the voice). Hence the per-voice pending-reset mask,
    not a flag inside the entry (§19.15).
- **R-5a Roll playback changes (§5.8).** Existing patterns change in three
  ways (intended):
  - roll steps now honour micro-timing and shuffle;
  - a step's automation now survives its own delayed trigger and later roll
    hits;
  - a roll is no longer cut by an empty next step (F-R), and odd roll steps
    take the track's shuffle (F-S).
- **R-6 Menu value vs. Morph (accepted, F-G).** The recorded value is the
  menu value the user observes. With voice Morph ≠ 0, raw playback differs
  from the interpolated sound heard during the take. This is intended.
- **R-6a Muted `rtk` condition evaluation.** The side-effect-free evaluation
  (§9.1a) is a second, partial path through `seq_evaluateStepCondition()`.
  It must stay in step with the real one when conditions are added.
- **R-6b Deferred FX-sequence fan-out (F-I, §10.4).** Linked Scenes lag
  the active Scene by at most the propagation backlog. A missed flush point
  (a Scene change path that bypasses the forced flush) would leave them
  diverged. Every Scene-change entry point must call the flush.
- **R-7 Shared slot 6 and live triggers.** With `rtk` = 6, a trigger from
  track 7, or a live overlay/MIDI trigger of the voice, ends the voice's
  automation hold with no stamped step behind it. Playback can then revert
  to the base value until the next stamped step.
- **R-8 Echo marks** must expire, or they silence a later trigger.
- **R-9 Roll tail.** Recorded rolls end with the fill duck; live overlay
  rolls do not.
- **R-10 Pattern AutoSave deferral.** Pattern AutoSave is suspended while
  `seq_recordActive`.
- **R-11 REC toggle on release** delays a short press by up to 300 ms.
- **R-12 MIDI separability.** Logic beyond the two calls would be lost in
  the rework.

---

## 18. Follow-ups

None open for P1. Every item through F-S, plus L1 and L3, is decided (§2).

Looping while recording, meaning L-2 (lift the lockout), L2 (PERF button
sharing) and L4 (loop-aware placement and echo), is planned separately in
`S081_P2_LOOP_WHILE_RECORD.md`. Its open questions are listed there.

---

## 19. Module change details

Each subsection: new state, changed functions (with current locations),
new functions, and the invariants to keep. Line numbers are at base
`02553df`.

### 19.1 Sequencer — `Core/Sequencer/sequencer.c/.h`

**New state** (all SRAM1; TIM3-shared bytes are single-byte or
PRIMASK-published):

- `seq_recTrack`, `seq_recWriting` (published by LiveRecord through
  `seq_setRecordTarget(track, writing)`).
- `seq_recRing[16]` (`u16`: step 7 bits, track 3, fired 1) + producer and
  consumer bytes. TIM3 writes; LiveRecord drains with
  `seq_takeRecBoundary()`.
- `seq_echoStep[7]` (`int8`, −1 = none), `seq_liveHoldMask`,
  `seq_eraseSpecialsMask`, `seq_eraseFullMask`.
- `seq_ppqStampUs`, `seq_ppqPeriodUs` (written at the head of
  `seq_processSchedulerTick()` from `timebase_tim2Now()`).
- `seq_playbackEpoch` (`u8`, foreground): bumped by every Scene change and
  realign.
- `seq_menuOverride[6]` (`u64`).
- Filter registration: `const uint8_t *seq_filterLatch`,
  `*seq_filterErase`, `seq_filterEraseTrack` (set by
  `seq_setAutomationFilter()`).

**Changes:**

1. **`seq_advanceTrackStep()`** (`:2140-2320`):
   - **Muted `rtk`:** when `track == seq_recTrack && seq_recWriting` and the
     track is muted (`process_step = 0`, `:2151`), read the specials and
     evaluate `seq_conditionPassesPure()`. This is a new static that shares
     the dispatch of `seq_evaluateStepCondition()` (`:1755`) but writes no
     state. The result is only the ring's `fired` bit.
   - **Erase:** replace `seq_eraseActive && track == menu_getActiveVoice()`
     (`:2211`, `:2301`) with the masks.
     - **Full:** clear bit 15, `patSvc_enqueueErase()`, skip automation
       queueing (today's path).
     - **Specials:** clear bit 15, `patSvc_enqueueEraseSpecials()`;
       automation still queues.
   - **Echo / live hold:** in the trigger branch, a matching
     `seq_echoStep[track]` is consumed and the trigger skipped (fired = 1).
     A set `seq_liveHoldMask` bit skips the trigger (F-P).
   - **Ring push:** when `seq_recWriting` and (`track == seq_recTrack` or
     the live-hold bit is set), push {step, track, fired}. A full ring drops
     and counts.
   - **Effect step marker:** removed. That covers
     `seq_queueEffectStepMarker()` `:1698`, `SEQ_PENDING_TYPE_FX_STEP_BIT`
     `:712`, `seq_effectAutomationTracks` `:127`, and
     `seq_setEffectAutomationTracks()` `:166`.
   - **Legacy roll block** `:2310-2317`: removed.
2. **`seq_drainPendingAutomation()`** (`:2442`):
   - Remove the FX marker branch and the final
     `effects_automationStepFlush()`.
   - Filter each entry: skip if its target's bit is set in the registered
     latch bitmap (any owner track), or in the erase bitmap with
     `owner_track == seq_filterEraseTrack`.
   - **Voice branch:** also skip if `seq_menuOverride[slot]` has the local
     bit.
   - §5.8: entries of a deferred step are not queued at the step start;
     TIM3 queues them when the deferred trigger fires, so no foreground
     parking is needed.
3. **`seq_restoreAutomatedParameters()`** (`:2563`): also clears
   `seq_menuOverride[slot]`. It runs only for voices with a pending reset
   (per-voice mask, §5.8, §19.15).
   - `seq_triggerVoice()` gains a `reset` argument (or a
     `seq_triggerVoiceFirst()` twin):
     - `seq_advanceTrackStep()` on-time triggers, deferred fires and
       `seq_rollBegin()`'s first hit pass 1;
     - `seq_processRollTicks()` (`:3780`) passes 0;
     - the overlay and preview paths pass 1.
   - A roll renewal (the carry path in `seq_rollBegin()`) calls new
     `voiceControl_resetOnly(track)`.
   - **Deferral:** `seq_trackShuffle*` state gains `seq_trackShuffleStep[7]`
     and a roll flag.
     - At the trigger emission (`:2252-2290`), roll steps take the same
       deferral path as plain steps. The step's automation queueing
       (`:2303-2305`) is skipped when the trigger is deferred.
     - The deferred fire (`:3719`) and the early flush (`:2257`) call
       `seq_queueStepAutomations(track, step)`, then fire the trigger
       (`seq_rollBegin()` for a roll step).
   - **Roll end (F-R):** `:2203-2206` changes to end a running roll only
     when the new step has a trigger that fires. The roll engine ends a
     roll by itself after its fill window (`seq_rollRemaining`).
4. **Realign and the FX sequence** (corrected in revision 9, Appendix A-5):
   `seq_realignActivePatternToMasterClock()` (`:2719`) and
   `seq_realignTrackToMasterClock()` (`:2820`) call two new functions:
   - `seq_fxRealignToMasterClock()` recomputes the FX accumulator, step
     counter and published step from the master clock, the same mapping as
     the track realign. This is needed because the FX position drifts after
     an FX step-scale change.
   - `seq_requestEffectOverlayReset()` clears the Effect track overlays
     without touching the FX position.

   Not `seq_fxPublishReset()`: its RESET event invalidates the current FX
   step and drops the running locks. These sites also clear
   `seq_menuOverride` (transport/realign rule) and bump the epoch.
5. **New `seq_menuOverrideSet(slot, local)`:** clears the
   `seq_automation_dirty[slot]` bit and sets the override bit. Foreground
   only; the drain and restore are foreground too, so no PRIMASK is
   needed.
6. **Override clear sites:** `seq_setStepIndexToStart()` (`:4478`, all
   transport/PC-reset paths), `seq_selectActivePattern()` (`:1386`),
   `seq_alignActivePatternToScene()` (`:1495`), and
   `seq_setTrackPlayedScene()` (`:1159`, that slot; the pair for tracks
   5/6). The same sites bump `seq_playbackEpoch`, clear `seq_echoStep[]`
   and reset the ring.
7. **`seq_applySceneAutomation()`** (`:2333`): unchanged. Add
   `seq_clearSceneAutomationBit(index)` for the R12 per-target clears.
8. **Effect reset latch:** unchanged (`seq_fxPublishReset()` → drain →
   `effects_automationReset()`). `effects_automationReset()` now also clears
   `menuHold`.
9. **`seq_rollPpq()`** (`:1841`): the §5.4 table and `rate > 9` guard.
   `seq_rollCarries()` is unchanged.
10. **Quantise:**
    - `QUANT_TRACK` after `QUANT_64` in `sequencer.h:18`.
    - `seq_setQuantisation()` (`:4042`) clamps 0..5.
    - `seq_loopEventAnchor()` (`:3079`) gains a per-track anchor for
      `QUANT_TRACK` (each track's last step start: `now −
      accumulator/256`).
    - New `seq_quantiseTick(track, tick)` returns the nearest boundary for
      1/8..1/64, or the nearest step start of `track` for `trk`.
11. **Capture/projection:**
    - `seq_captureTrackTime(track, seq_track_time_t *)`: PRIMASK snapshot of
      elapsed tick, µs since the tick, period, `seq_stepIndex`,
      accumulator, effective interval/length/shuffle, play mode/state,
      played Scene.
    - `seq_projectStep(track, tick, *step, *offsetQ8)`: built on
      `seq_loopCanonicalPreAdvance()` (`:3036`); returns "unknown" for
      future ticks in `rnd`.
12. **Echo/live-hold API:** `seq_suppressStepOnce(track, step)`,
    `seq_setLiveHold(track, on)`, `seq_cancelPendingTrigger(track)` (clears
    `seq_trackShufflePending` when a record overwrites the current step
    before its delayed trigger fired).
13. **Erase API:** `seq_setEraseMasks(specials, full)`.
14. **Removed:** `seq_recordTrigger()`, `seq_quantize()`, `seq_setRoll()`,
    `seq_setRollRate()`, `seq_rollRate`, `seq_rollState`,
    `seq_setRecordingMode()`, `seq_setErasingMode()`, `seq_eraseActive`.
    The record-off inside `seq_loopEngage()` (`:3214`) moves to
    buttonHandler.
    - `seq_recordActive` stays as the published "writing" flag.
    - Its readers (filesystem, copy/clear) switch to LiveRecord queries
      (§19.10, §19.11).

**Invariants:**

- TIM3 never touches pool bytes: erase uses the service's TIM3-safe
  enqueue.
- Every new TIM3-visible field is a byte or a PRIMASK-published pair.
- The ring has one producer (TIM3) and one consumer (LiveRecord).

### 19.2 LiveRecord — new `Core/Sequencer/LiveRecord.c/.h`

**State:**

- mode, `rtk`, `T`, take origin Scene, last seen `seq_playbackEpoch`;
- the three bitmaps;
- open roll capture, pending flam grace;
- drop counters (DEV trace through `patternTrace_record()` stage codes;
  new codes listed in `PATTERN_DYNAMIC_STACK.md` §12.16 at close).

**`liveRec_service()` order (each 500 Hz pass, before `patSvc_tick()`):**

1. **Take boundary.** Start a new take on any of these:
   - an epoch change;
   - a `rtk` change or mode change;
   - `patSvc_replacePending()` (a Load is replacing Pattern data: release
     the target and suspend). Revision 9 replaced `menu_isStorageBusy()`,
     which would have made LiveRecord depend on Menu (Appendix A-11).

   A new take:
   - clears `latch`/`dirty`;
   - discards the open roll and pending grace;
   - flushes the FX-erase pending mask;
   - resets the take origin.
2. **Target.** `T = seq_getTrackPlayedScene(rtk)`.
   - If `T ≠ A`, call `patSvc_requestTarget(T)`; otherwise
     `patSvc_releaseTarget()`.
   - Publish `seq_setRecordTarget(rtk, patternWriting)` and
     `seq_setAutomationFilter()`.
   - While `!patSvc_targetOpen(T)`, every write below is dropped and
     counted.
3. **Ring drain** (`seq_takeRecBoundary()`). An entry marks the start of
   a step. When a track's next entry arrives, its previous entry, step `N`,
   has ended; the work below runs for `N` with that entry's fired bit
   (clarified in revision 11, Appendix A minor M-9):
   - `track == rtk`: run the §9.3/§9.4 step-end table over the set bits of
     `latch`/`dirty`.
     - Value: `liveRec_targetValue7()`.
     - Before any write or clear, one `pat_readStepAutomations()` read of
       step `N`. A write is queued only if the entry is missing or holds a
       different value; a clear only if the entry exists (L3).
     - Writes: `patSvc_writeStepAutomation()`; clears:
       `patSvc_removeStepAutomation()`.
     - Erase-bitmap targets present on `N`: removed.
     - `fil` (R10) on any step that received a write: one
       `patSvc_setStepCondition(…, 21)`.
   - Live-hold track: append step `N` to the open roll (trigger + rate +
     fill 100).
4. **Captures:**
   - Placement (§8.3) and flam pairing (§8.7).
   - Writes go through `patSvc_recordStep()`.
   - A write ahead of the playhead gets `seq_suppressStepOnce()`; a write
     onto the current step gets `seq_cancelPendingTrigger()`.
   - LED feedback through `seq_ledState` (§19.8).
5. **FX erase:** `effects_takeErasedLocks()` → `effects_clearSeqLanes()`
   (§10.4).

**`liveRec_targetValue7(target)`** reads owner getters (§3.3-10) and the
shared 7-bit conversions.

All values come from the take's origin Scene A: the active Scene, which the
VOICE, EFFECT and PERF pages display and edit. They never come from the
target Scene T. Under per-track playback, T = P(rtk) can differ from A, but
the edit writes A's image and the active-Scene runtime write puts A's value
on the sounding slot. So A's menu value is what the user hears (Appendix
A-6). `scl`/`shf` are read from A's Pattern region and are recordable only
while the viewed Pattern is A.

| Target | Value read (Scene A) |
|---|---|
| Instrument | `instrument_parameters[local]` (Normal endpoint; not `morph_interpolation[]`) |
| `Nvm` | Morph amount store-7 (127 ↔ 255) |
| `Nou`, `Nfx` | `scene_getVoiceAudioOut()`, `scene_getVoiceFxSendAmount()` |
| `7dc` | `scene_getSlot6Track7AmpEnvelopeDecay()` |
| `scl`, `shf` | Pattern region Normal (`track_scale`, `track_shuffle`) |
| `fxm` | `scene_getEffectMorphAmount()` store-7 |
| Effect | retained Normal, WIDE8 rows compressed to 7 bits |

**Ownership checks** (§7):

- `seq_getTrackPlayedScene()`;
- slot types from `scene_instrumentSlotConst()`;
- Effect types from `scene_effectConst()`.

**Invariants:**

- Never called from TIM3.
- Never writes a Scene other than `T` (Pattern) or the origin Scene's
  fan-out set (FX sequence).
- Never calls Menu, buttonHandler or MidiParser.

### 19.3 PatternData / PatternStackService

**`pat_writeDynamic()`** (`PatternData.c:640`):

- Gains a `trigger_policy` (KEEP / ON / OFF, reusing `PAT_RAW_TRIGGER_*`)
  applied inside the existing PRIMASK publish (`:741`, and the detach path
  `:690`).
- All current callers pass KEEP.

**New `pat_recordStep(scene, track, step, const pat_record_spec_t *)`:**

- Reads the existing specials and automation.
- Applies note/velocity/timing/roll/fill; clears rhythm; keeps the
  condition or sets `fil`.
- Writes with trigger ON.
- Cheap path: if the result has no specials and no automation, it only sets
  bit 15 (`pat_setStepActive()`).

**New `pat_eraseSpecials(scene, track, step)`:**

- If automation is present, keeps the condition and drops the other
  specials.
- Otherwise releases the block (trigger KEEP; TIM3 already cleared it).

**Service:**

- `PATSVC_OP_RECORD_STEP`:
  - The payload is a staging-slot index.
  - The 4-slot staging table is owned by the service, so it has no
    LiveRecord dependency.
  - `patSvc_recordStep()` copies the spec in; a full table rejects and
    counts.
  - Execution frees the slot.
- `PATSVC_OP_ERASE_SPECIALS`, plus the TIM3-safe
  `patSvc_enqueueEraseSpecials()` modelled on `patSvc_enqueueErase()`.
- `patSvc_requiredChunks()` learns both ops: record ≤ the current block
  plus the new specials; erase-specials ≤ the current block.

**Target override:**

- `service_target_request` (`0xFF` = none) and
  `patSvc_desiredScene()` = request, else `seq_activePattern`.
- It replaces the eight `seq_activePattern` comparisons in §3.3-8.
- `patSvc_requestTarget()` is refused while an exclusive claim or a
  filesystem replacement is pending.
- New `patSvc_targetOpen(scene)` query.

**Invariants:**

- Publish-before-free stays.
- One target at a time.
- Handover drains first.
- `patSvc_idle()` must be false while an override is held, so Pattern
  AutoSave never snapshots the overridden Scene mid-take.

### 19.4 EffectsManager — `Core/DSP/Effects/EffectsManager.c/.h`

- **Removed:**
  - `effects_automationStepBegin()`, `effects_automationStepFlush()`,
    `effects_automationPublishOwners()`;
  - `owner[]`, `pending_end`, `pending_track`, `owner_tracks`
    (§3.3-3);
  - the `held_morph` latch (`effects_seqLatchMorph()` `:728`, its calls in
    `effects_seqConsume()` `:761`, `effects_seqSelect()` `:829` and
    `effects_service()` `:1707`).
- **`effects_applyAutomation()`** (`:1276`): the track argument is unused
  after owner removal (keep the signature or drop it). It sets the overlay
  value/bit and clears `menuHold` for that local.
- **`effects_setMorphAutomation()`** (`:1301`): clears the Morph
  `menuHold`.
- **Lock step start** (`effects_seqConsume()` STEP, and
  `effects_seqSelect()` in `sel`), for each lane locked on the new step:
  - clear the overlay bit and `menuHold` of the mapped parameter (lane 0:
    `morph_override_valid` and the Morph hold);
  - lanes in the live-erase mask are excluded instead, and recorded in the
    pending mask.
- **Resolution** (`effects_service()` `:1679`), per parameter:

  ```
  value = menu
  if overlay:               value = overlay
  if lock && !menuHold:     value = lock
  then LFO
  ```

  Morph base:

  ```
  menuHoldMorph ? menu : lockMorph(current step) ? lock
              : morph_override ? override : menu
  ```

- **Menu edits:** `effects_setParameter()` (`:581`) and
  `effects_setMorphAmount()` (`:705`), when the origin is the active Scene
  and the row is in the runtime type, clear that overlay and set
  `menuHold` (F-Q decides Morph-image edits).
- **New:** `effects_setLiveEraseLanes(laneMask)` and
  `effects_takeErasedLocks(*stepMask, *laneMask)`.
- `effects_automationReset()` (`:1238`) also clears `menuHold` and the
  erase state.
- `EFFECTS_BUS_REFERENCE.md` §6, §10, §11.2 are rewritten at close.

### 19.5 Preset / presetMorphEngine / InstrumentManager

- **New per-target clears:**
  - `preset_clearFxSendStepOverride(slot)`;
  - `preset_clearAudioOutStepOverride(slot)`, which also re-applies the
    retained route with `preset_applyVoiceAudioOutRuntime(slot,
    scene_getVoiceAudioOut(A, slot))`;
  - `presetMorph_clearTrackParamStepOverride(track, param)`, followed by
    `seq_refreshTrackEffectiveParams()`;
  - `instrumentManager_clearSlot6Track7StepDecayOverride()`.

  Each pairs with `seq_clearSceneAutomationBit()`.
- **Instrument override:** no Preset change. `presetMorph_writeRuntimeBase()`
  already writes once the held bit is cleared by `seq_menuOverrideSet()`.
- **Supplemental (non-morphable) parameters**
  (`preset_setSupplementalParameter()`): confirm they write the runtime
  through the same guarded path. If they write it directly, the override
  only needs the drain skip.

### 19.6 Menu — `menu.c`, `menuPages.h`, `MenuText.h`

**R12 call sites:**

- `menu_cellCommitValue()` (`:4817`):
  - INSTRUMENT branch, Normal or Morph image: **one**
    `seq_menuOverrideSet(cell->slot, cell->descriptor_index)` before the
    edit-mask Scene loop. The override belongs to the one runtime slot; the
    edit mask is a set of Scenes, so it is not called per Scene
    (revision 9, Appendix A-14);
  - SCENE_SETTING `AUDIO_OUT`/`FX_SEND` → the per-slot clears;
  - KIT_SETTING `7dc` → its clear.
- STEP `len`/`scl`/`shf`: Normal commits at
  `menu_parseGlobalParam()` `:15886`/`:15896`/`:15945`, Morph endpoints at
  `:4993-4999` → the track clears.
- Effect cells go through `menuEffects_cellCommit()` →
  `effects_setParameter()` (§19.4).

**Record hook** in `menu_parseKnobDelta()` (`:13916`) and
`menu_parseEncoder()` (`:13466`):

- After the clamp, whether or not the commit changed anything, when
  `liveRec_mode()` is LATCH or OVERDUB and the cell is recordable →
  `liveRec_paramIn(target)`.
- Recordable means all of:
  - `menu_knobClearTarget()` (`:13816`) resolves a Pattern target;
  - the page is not PERFORMANCE, RECORDING, MENU_MIDI, LOAD or SAVE;
  - not in a Morph view (`voiceModeShowMorph` on VOICE pages,
    `menu_patternTrackMorphViewActive()` on SEQ, the Effect Morph view);
  - the target is not a track length (405..411).
- The held-step overlays (`va_writeAutomationFromKnob()`, `sa_*`) return
  before the hook and stay unrecorded.

**Erase knob:** at the copy/clear pot-ownership branch (`:13960`), if
`liveRec_mode() == ERASE`:

- `menu_knobClearTarget()` → `liveRec_eraseParam(target, fx_lane)`;
- no value change;
- `menu_automationTargetCleared()` drops the underline; the search
  restarts when erase ends.
- This works on any page whose cells resolve, including the Morph view and
  PERF Morph cells.

**Q13 refusal:** `liveRec_activePatternLocked()` is checked first in
`menu_parseKnobDelta()`/`menu_parseEncoder()` for cells that write the
viewed Pattern:

- `PAR_STEP_*`;
- `PAR_TRACK_LENGTH`/`SCALE`/`SHUFFLE`/`PLAY_MODE`/`LOOPABLE`;
- step-automation editor cells;
- `va_writeAutomationFromKnob()`, the `sa_*` writers,
  `menu_broadcastStepSpecial()`.

A refused edit returns before `parameter_values[]` or the display changes.

**`rtk`:**

- RECORDING_PAGE sub-page 2 cell 0 → `PAR_AUTOM_TRACK`.
- Its dtype changes from `DTYPE_0b1` to a 0..6 value shown as 1..7 (a
  static-param name special case, like the S080 cells); the cell text is
  `TEXT_REC_TRACK` "RecTrk".
- Commit → `liveRec_setRecordTrack()`.
- The display mirrors `liveRec_recordTrack()`.

**`menu_setActiveVoice()`** (`:16362`): inside the `!=` branch, call
`liveRec_onActiveTrackChanged(v)`. Re-selecting the same track does not
reset an `rtk` override.

**Lists:**

- `quantisationNames` gains `trk`.
- `stepRollRateNames` → `off 1/8 8tr 16 16t 32 32t 64 64t 128`.
- The STEP roll clamp (`:5790`) and `menu_getRollRateName()` (`:9978`)
  limits go 8 → 9.
- `PAR_ROLL` retirement:
  - the Euklid cell (`menuPages.h:159`);
  - the parse case (`:16077`);
  - the default (`:16784`);
  - the dtype (`:1101`);
  - `rollRateNames` and `MENU_ROLL_RATES` if no other user.

### 19.7 buttonHandler — `buttonHandler.c/.h`

**REC** (`:2377`):

- **Press:**
  - SHIFT → menu;
  - menu open → exit;
  - looping or erase enabled → ignore;
  - otherwise start the hold timer `bh_recPressTick`/`bh_recArmed`.
- **`buttonHandler_tick()`:** once the hold passes `DOUBLE_CLICK_TIMEOUT`,
  call `liveRec_overdubBegin()` and `led_pulseLed(LED_REC)`.
- **Release:**
  - an overdub → `liveRec_overdubEnd()` and a pulse;
  - otherwise toggle `liveRec_setLatch()`, and the REC LED shows the
    state.
- **REC LED flash** while the SHIFT+REC menu is open and armed:
  `led_setBlinkLed()` on enter, restored on exit.

**Erase chord:**

- Track `bh_shiftHeld` and `bh_clearHeld`.
- With `liveRec_armed()`, SHIFT+COPY in either order calls
  `liveRec_eraseBegin()` (this ends an overdub) and lights both LEDs.
  `copyClear_copyPressed()` is not called.
- Releasing one keeps erase enabled; releasing both calls
  `liveRec_eraseEnd()` and restores the LEDs.
- The old erase unwind at `:2615-2624` and `:2812-2817` is replaced.
- The SHIFT-release mode logic (Morph view end, PERF page re-entry) is
  unchanged.

**Erase router** in `processPress()`/`processRelease()`, right after
`copyClear_buttonPressed()` (`:2295`), while `liveRec_mode() == ERASE`:

| Button | VOICE / STEP | FX | PERF | Load/Save, MENU, SOM_GEN |
|---|---|---|---|---|
| SEQ | `liveRec_eraseSeqHeld()` press/release | `liveRec_eraseSeqHeld()` press/release | consumed | consumed |
| VOICE | `liveRec_eraseTrackHeld()` | `liveRec_eraseTrackHeld()` | `liveRec_eraseTrackHeld()` | consumed |
| PLAY | plain transport toggle (`buttonHandler_setRunStopState()` + `seq_setRunning()`), bypassing `bh_chainPlayArmed` | same | same | same |

Every other button passes through unchanged (MODE, SELECT, BAR, ...).

**Overlay** (`:830-957`):

- After each `seq_triggerVoice()` in `buttonHandler_overlayTrigger()` and
  `buttonHandler_overlayTick()`, call `liveRec_triggerIn()` with the hit
  kind.
- `buttonHandler_overlayRelease()` → `liveRec_rollRelease()`.
- **R19 switch:** a BAR overlay press (`:2466`, `:2501`) with
  `bh_overlayVoiceMask != 0` re-configures the held track per F-B/F-O and
  calls `liveRec_rollRateChange()`.

**R19 LED:** in `handleModeButtons()` (`:1531`), leaving PERF clears both
BAR LEDs; entering PERF restores them from `bh_overlayActive` (as at
`:1281`).

**Looper:**

- **P1:** before every `seq_loopEngage()` call, `liveRec_forceOff()` and the
  REC LED off. This replaces the Sequencer-internal call (§3.3-9) and keeps
  the S080 lockout as an interim.
- **P2** removes the lockout (L-2, `S081_P2_LOOP_WHILE_RECORD.md`).

### 19.8 ledHandler

- Remove `SEQ_LED_DIRTY_REC_SUB` and its `recordSubStep` endpoint
  (`ledHandler.c:1391-1404`, `ledHandler.h:215`). Remove
  `led_updateRecordedSubStep()` if it has no other caller.
- `recordMainStep` carries the absolute step. The REC_MAIN drain draws it
  only when it is in `menu_currentBar` on the visible track.

### 19.9 MidiParser

- `midiParser_noteOn()` `:1550`: `seq_recordTrigger(voice)` →
  `liveRec_triggerIn()` (`kind = SINGLE`, `source = MIDI`).
- `midiParser_enterTaggedParameter()` (`:128-145`): after a successful
  store, `liveRec_paramIn(target, MIDI)`. This needs a target ID from the
  tagged key; the MIDI rework owns that mapping, so for now it is a stub
  hook.

### 19.10 CopyClear

- `copyClear_copyPressed()` (`copyClearSession.c:584-585`): the
  `seq_recordActive` and `seq_eraseActive` refusals become
  `liveRec_armed()` (F-N).
- No pot-clear change: the EFFECTS/PERF FX-lane clear already matches F-J.

### 19.11 Filesystem

- `filesystem.c:24704`, `:25066`: the gates become
  `liveRec_patternWriting()` (F-M).
- `quantisation` = 5 (`trk`) needs no parser change (no clamp at `:2796`);
  the Sequencer clamps.
- PAT4: no change (§3.3-5).

### 19.12 timebase / main

- `timebase.c:201`: call `liveRec_service()` immediately before
  `patSvc_tick()`, so idle-direct writes execute in the same pass.
- `main.c` after `copyClear_init()` (`:1299`):
  `liveRec_init(menu_getActiveVoice())`.
- `buttonHandler_tick()` (`main.c:1417`) hosts the REC hold timer.

### 19.13 Tools

None: no converter change (F-H; §3.3-5).

### 19.15 MidiVoiceControl — `Core/MIDI/MidiVoiceControl.c/.h`

Owned by the trigger funnel, not by the MIDI parser. It survives the MIDI
rework.

**Current queue** (`MidiVoiceControl.c:72-150`):

- `VoiceTriggerEvent` is `{uint8_t voice, note, vel}`: 3 B with no padding,
  in a 32-entry ring (96 B). `voice` is always 0..6.
- `voiceControl_noteOff(voice)` calls `voiceControl_dropPendingLocked()`.
  That **removes every queued entry for that voice**.
- `seq_triggerVoice()` calls `voiceControl_noteOff()` before every
  `voiceControl_noteOn()` (`sequencer.c:1294-1297`). So a later trigger
  queued for the same voice, before the foreground drains, deletes the
  earlier one.

**Decision: the RESET intent is not stored in the entry.** It is a per-voice
pending-reset mask, `voiceControl_pendingReset` (`u8`, bits 0..6), set and
cleared under the queue's existing PRIMASK.

| Option | Problem |
|---|---|
| Flag in bit 7 of `note` or `vel` | Lost when the entry is dropped (for example: first roll hit queued, then retrigger 2 queued before the foreground runs; or a reset-only renewal followed by any trigger). A reset would be skipped (R-5). A stored step note or velocity byte is also not range-checked on PAT4 load, so a stray value ≥ 128 would read as RESET. |
| Flag in the spare bits of `voice` | Same drop problem. Also forces masking in `dropPendingLocked()` and the pop path. |
| Separate per-voice mask (chosen) | Survives drops: a reset is pending until some trigger or the end of the pass consumes it. |

**Behaviour:**

- **Enqueue:** a resetting trigger or `voiceControl_resetOnly()` sets the
  voice's bit. A trigger without reset (roll retriggers 2+) does not touch
  it.
- **`voiceControl_popPending()`:** inside its existing PRIMASK section,
  copies the event and tests-and-clears that voice's bit, returning it as a
  `reset` flag. TIM3 sets bits concurrently, so every read-modify-write of
  the mask is under PRIMASK (revision 9, Appendix A-13).
- **`voiceControl_triggerNow(voice, note, vel, reset)`** keeps the existing
  order:
  1. `preset_applyDeferredSceneSlotForTrigger()`;
  2. `seq_restoreAutomatedParameters()`, only if `reset`;
  3. `instrumentManager_triggerTrack()`, sidechain, LED.

  The restore must not move ahead of the deferred Scene slot apply.
- **`voiceControl_processPending()`:** pops and triggers as above. After the
  ring is empty, it takes the remaining mask under PRIMASK (copy, store 0)
  and runs the restore for each bit taken: reset-only renewals, and resets
  whose trigger entry was dropped.
  - All of this precedes `seq_drainPendingAutomation()` (`main.c:212-213`),
    so a step's own automation still lands after its reset.
- **Edge (accepted):** if the foreground falls behind, a reset bit set by
  step N+1 can be consumed by a still-queued roll retrigger of step N within
  the same pass. Both run before the drain, so the only effect is that that
  retrigger sounds with the base values.
- **RAM:** +1 B. The entry struct and ring are unchanged.
- **API:**
  - `voiceControl_noteOn()` keeps its signature for MIDI, preview and
    overlay callers. Those are live triggers, which always reset (F12), so
    it sets the bit.
  - A no-reset variant (`voiceControl_noteOnNoReset()`) serves roll
    retriggers 2+.
  - The Sequencer reaches them, and `voiceControl_resetOnly()`, through
    `seq_triggerVoice(…, reset)`.

### 19.14 Documentation at close

- `PATTERN_DYNAMIC_STACK.md`:
  - new live-record section;
  - §6.1/§6.2 (§5.8 step reset rule, deferred automation, delayed rolls);
  - §6.2a (R12 amendment to S075 F3);
  - §12.3 queue format (§3.3-7);
  - §12.12 API (record/erase ops, target override);
  - §12.16 new trace stages.
- `EFFECTS_BUS_REFERENCE.md` §6, §10, §11.2 (R13).
- `MODULE_INTERCHANGE_SPEC.md`:
  - LiveRecord section;
  - Sequencer/PatternStackService/EffectsManager/Preset rows;
  - PERF surface (REC hold, erase chord, overlay recording);
  - stale rows (§3.2).
- `STORAGE_SRAM_MANIFEST.md`: §14, plus the `effects_automation_t` size
  change.
- `FILESYSTEM_SPEC.md`: `quantisation` value 5.
- `MEMORY.md`:
  - S075 F3 amendment;
  - stale BAR1/BAR2 MIDI line;
  - live-record invariants.

---

## Appendix A — Schedule review corrections (2026-10-13)

The first `S081_P1_IMPLEMENTATION.md` was reviewed against this plan and the
code at `02553df`. Each item below records what was wrong, how it was fixed,
and where. "Schedule" means `S081_P1_IMPLEMENTATION.md` revision 2. Items
marked **plan error** were mistakes in this plan; the plan body is corrected
too.

### A-1 Automation-only steps stopped queueing automation (schedule A1-3c)

- **Problem:** the schedule moved `seq_queueStepAutomations()` out of the
  step-allowed block and called it only where triggers fire. Steps with
  automation but no trigger would never play their automation. That breaks
  `PATTERN_DYNAMIC_STACK.md` §6.1 and §5.8 of this plan.
- **Fix:** the call stays in the step-allowed block. It is skipped only when
  this step's own trigger was deferred (`deferred_this_step`); then
  `seq_fireDeferred()` queues the entries at the deferred fire. A deferral
  still pending from an earlier step is fired first when the next step
  queues automation, so entries stay in step order.
- **Where:** schedule A1-3b, A1-3c.

### A-2 The menu override would never clear at a trigger (schedule A1-4c)

- **Problem:** `seq_restoreAutomatedParameters()` returns early when the held
  mask is zero, and `seq_menuOverrideSet()` clears the held bit on every menu
  edit. A clear placed after the held-mask clear was skipped exactly when an
  override existed.
- **Fix:** `seq_menuOverride[slot] = 0u` runs before the early return.
- **Where:** schedule A1-4c.

### A-3 Incomplete `seq_triggerVoice()` caller table (schedule A1-2b)

- **Problem:**
  - It missed `SomGenerator.c:139`, and `seq_setRoll()` at `:4177` (still
    present in A1). Both would break the build.
  - It listed `seq_previewVoice()`, which calls `voiceControl_noteOn()`
    directly.
  - It gave the overlay repeat timer reset = 1 for every hit.
- **Fix:** a complete table from `grep`.
  - The overlay timer at `buttonHandler.c:950` passes
    `bh_overlayCount != 0xFFu`: 1 for the flam main, 0 for roll repeats
    (§5.8).
  - SOM passes 1 (it must compile; its behaviour is unchanged).
- **Where:** schedule A1-2b.

### A-4 The early flush lost automation and rolls (schedule A1-3b/A1-3d)

- **Problem:** "flush the pending deferral (fire it now)" fired a plain hit
  only. With automation now queued at fire time, a flushed step would lose
  its automation, and a flushed roll step would play a single hit.
- **Fix:** one `seq_fireDeferred(track)` routine, used by both the expiry
  path and every early flush. It queues the step's automation, then starts a
  roll (`seq_rollBegin()`) or fires a plain hit, with reset = 1.
- **Where:** schedule A1-3b, A1-3d.

### A-5 Realign must realign the FX sequence, not reset it (schedule A2-4) — plan error

- **Problem:** §19.1-4 of this plan said to call `seq_fxPublishReset()` from
  the realign functions. That sends an FX RESET, which invalidates the
  current FX step and leaves the FX accumulator un-seeded, so the FX
  sequence would drop its locks and stay out of phase.
- **Also:** realign must not ignore the FX sequence (user, 2026-10-13).
  `seq_fxStepCounter` counts boundaries since the transport reset, so after
  an FX step-scale change the FX sequence drifts against the Pattern.
  Realign has to put it back onto the master grid.
- **Fix:** two new functions, called at the end of both realign functions:
  - `seq_fxRealignToMasterClock()`: under PRIMASK, same mapping as the track
    realign (`position = (elapsed << 8) / interval`,
    `accumulator = remainder`, `counter = position + 1`); publishes the
    step for `position` as a STEP event; `sel` is untouched.
  - `seq_requestEffectOverlayReset()`: sets only the overlay reset latch.

  The per-track realign realigns the FX sequence too (idempotent when
  already aligned).
- **Where:** schedule A2-4a/b/c; plan §5.3 and §19.1-4 corrected.

### A-6 Value source for recorded automation (schedule F-2a)

- **Problem:** the schedule read Instrument values from
  `morph_interpolation[]` (the Morph-interpolated runtime image, not the
  Normal endpoint), and from the target Scene T.
- **Per-track playback** (user note): the recording track may play a Scene
  T = P(rtk) different from the active Scene A.
  - The VOICE page reads and edits A's image (`scene_getActiveIndex()`,
    `menu.c` ~4770).
  - The edit's active-Scene runtime write puts A's value on the sounding
    slot.
  - T's image still holds its own, unedited value.
  - So what the user hears is A's menu value.
- **Fix:**
  - `liveRec_targetValue7()` reads every value from the take's origin
    Scene A (captured at the take start). Instrument values come from
    `instrument_parameters[local]` (the Normal endpoint).
  - `scl`/`shf` come from A's Pattern region. They are recordable only while
    the viewed Pattern is A, because with FOLLOW off a single SEQ click
    changes the viewed Scene without changing playback.
  - Ownership is unchanged: an Instrument target needs the slot's played
    Scene = T and the same instrument type in A and T; Effect/Scene targets
    need T = A.
- **Where:** schedule F-1c, F-2a; plan §19.2 table corrected.

### A-7 Phase order did not build (schedule B-3, B-7, E-3, G-5)

- **Problem:**
  - B removed `seq_recordTrigger()` while its MIDI caller changed only in E.
  - B removed `seq_eraseActive` and `seq_setErasingMode()`, but no step
    rewrote the TIM3 erase branch that uses them.
- **Fix:**
  - The MIDI call site moves to B (B-7a), calling the `liveRec_triggerIn()`
    stub; MIDI recording is inactive from B to E (the old path only
    recorded bar 1).
  - The erase chord, the TIM3 erase branch and the old-erase removals all
    move to G-5. Phase B keeps the old erase working, gated by
    `liveRec_armed()`.
  - The copy/clear and AutoSave gates keep a `seq_eraseActive` term until
    G-5.
- **Where:** schedule B-2e, B-3, B-6, B-7a, B-7d, B-7f, E-3, G-5, dependency
  order.

### A-8 No step for the TIM3 changes in `seq_advanceTrackStep()`

- **Fix:** new schedule section C-9:
  - the fired bit (bit 15 and condition passed, known at the step start)
    and the ring push;
  - the side-effect-free condition check for a muted `rtk`;
  - the echo and live-hold trigger skips.

  New C-10 covers echo-mark expiry: consumed, pass wrap, transport, epoch
  sites, length/play-mode change.
- **Where:** schedule C-9, C-10.

### A-9 No step for the step-end logic

- **Fix:**
  - F-5: the LATCH/OVERDUB tables, existence checks before clears, and
    `fil` on written steps;
  - E-4: fast-roll spans from the ring;
  - G-6: knob-erase removals.
- **Where:** schedule E-4, F-5, G-6.

### A-10 STEP toggles escaped the silent refusal (Q13)

- **Problem:** H-2 covered knobs and the encoder only.
  `buttonHandler_setRemoveStep()` writes bit 15 directly (`pat_toggleStep()`
  at `:1111`).
- **Fix:** H-2b adds the refusal there. `buttonHandler_selectActiveStep()`
  only selects and needs none.
- **Where:** schedule H-2b.

### A-11 LiveRecord called Menu (schedule H-1) — plan error

- **Problem:** this plan's §19.2 used `menu_isStorageBusy()` for the Load
  suspend, which breaks "LiveRecord never calls Menu".
- **Fix:** new `patSvc_replacePending()` (true while a filesystem Pattern
  replacement is prepared or applied). The load's completion bumps the
  epoch through `seq_alignActivePatternToScene()`.
- **Where:** schedule D-5b, H-1; plan §19.2 corrected.

### A-12 Unscheduled items

- Recorded-step LED by absolute step: B-8c, with a new
  `seq_markRecordedStep()` (the Sequencer owns `seq_ledState`).
- `rtk` cell (B-5c):
  - dtype → 0..6;
  - reuse `TEXT_AUTOMATION_TRACK` as `rtk`/`RecTrk`;
  - display reads `liveRec_recordTrack()` directly (no mirror);
  - commit through a `menu_parseGlobalParam()` case;
  - excluded from the RECORDING_PAGE settings-dirty mark (runtime only).
- `patSvc_recordStep()` submit, `patSvc_requiredChunks()` and
  `patSvc_executeEvent()` cases: D-4e.
- FX lock exclusion at the FX step start: G-3c. Who sets the erase lanes:
  G-3d.
- Live-hold ownership (LiveRecord only): E-5.
- Slow-roll rule (rolls at or slower than the step rate recorded hit by
  hit): E-1.
- Drop counters and trace codes, DEV-only: H-3.

### A-13 Reset ordering and atomic mask (schedule A1-1e/f) — fixed in both documents

- **Problem:** the restore was moved ahead of
  `preset_applyDeferredSceneSlotForTrigger()`, reversing today's order. The
  mask read-and-clear in `processPending()` was not under PRIMASK, although
  TIM3 sets bits.
- **Fix:** `voiceControl_triggerNow()` gains `reset` and keeps the order
  apply → restore (if reset) → trigger. `voiceControl_popPending()` tests
  and clears the voice's bit inside its PRIMASK section. The end-of-pass
  leftovers are taken under PRIMASK.
- **Where:** schedule A1-1e, A1-1f; plan §19.15 corrected.

### A-14 Override call per slot, not per Scene (schedule A1-7a) — fixed in both documents

- **Problem:** "for each slot in the active Scene's edit mask" mixed Scenes
  and slots.
- **Fix:** one `seq_menuOverrideSet(cell->slot, cell->descriptor_index)`
  before the edit-mask Scene loop.
- **Where:** schedule A1-7a; plan §19.6 corrected.

### A-15 Looper and `qnt trk` (schedule A3-3c) — decided: keep (L1)

**What `trk` would change in the looper:** only where each looped track's
loop window starts. It does not change the timing of any step inside a loop.

- **Wrap.** At each wrap (`(elapsed − anchor) % L == 0`) a track restores its
  snapshot: its state just before the anchor tick's DDA advance. The DDA
  then replays from there. So each track replays the window
  `[anchor, anchor + L)` with every step at its original offset from the
  anchor.
- **All tracks share the period L.** Each track's content at real time `t`
  is its original content at a time congruent to `t` modulo L. Steps that
  sounded together still sound together, and every step keeps its position
  modulo L. This holds whether the tracks share one anchor or each has its
  own.
- **The anchor only selects the content.** It decides which window is
  replayed, which is the step caught at the loop start. Today:
  - `off`: one anchor for the gesture, the tick at which the **fastest**
    track last stepped. A slower track can then be anchored mid-step, so its
    current step's hit is not caught.
  - 1/8..1/64: the last grid boundary.
  - `trk`: each track's **own** last step start, so every track catches its
    current step. That is the purpose stated for looper quantize.
- **Existing edge, unchanged:** when a track's step is longer than the loop
  (for example a 1/2-note step under a 1/16 loop), its anchor lies more than
  one loop length back. The track jumps back at the next wrap and re-plays
  that step's start every loop. That is the accepted S080 behaviour for
  quantise boundaries more than one length back.
- **Implementation:** the engine already stores one anchor per track
  (`seq_loopAnchor[7]`). Only the gesture computes a single anchor:
  `seq_loopEventAnchor()` is called once in `seq_loopSnapshotTracks()`
  (`sequencer.c:3133`). Per-track anchors for `trk` mean computing the anchor
  and the `back` offset inside that function's per-track loop. That is
  binding S080 code (master-clock anchoring), so it is a deliberate change.
- **Schedule text error:** A3-3c says `trk` "replaces the fastest-track
  heuristic for NO_QUANTISATION". It does not; `off` keeps that behaviour.
  Revision 11 rewrites A3-3c: `seq_loopEventAnchor()` is unchanged for
  `off` and 1/8..1/64, and for `trk` the per-track anchor is computed
  inside `seq_loopSnapshotTracks()`.

### A-16 Overlay fixes used names that do not exist (schedule A3-4)

- **Problem:** the schedule used `LED_BARLT`, `LED_BARRT`, `bh_overlayType`
  and `led_setLed`.
- **Fix:**
  - `LED_BAR1`/`LED_BAR2` and `led_setValue()`, with the same expressions
    as `buttonHandler_loopCheckDisengage()`.
  - The switch-while-held mechanism is specified, with new
    `bh_overlayHeldType` (+1 B), and F-O (toggle off) added.
  - Interpretation recorded: single → roll starts repeating one interval
    after the switch, with no hit at the switch itself.
- **Where:** schedule A3-4a, A3-4b.

### A-17 REC button details (schedule B-2)

- `bh_recPressTick` is `uint16_t`, with the same wrap-safe subtraction as
  the fill hold (`time_sysTick` is `volatile uint16_t`).
- The REC LED flash while the SHIFT+REC menu is open and armed is added
  (B-2f).
- Every use of the removed `bh_state.seqRecording` is listed with its
  replacement (`:1846`, `:1963`, `:2404-2406`, `:2420`).
- **Where:** schedule B-2b, B-2e, B-2f.

### A-18 Pattern Stack Service return values (schedule D)

- `pat_recordStep()` returns success, for the service's failure
  classification.
- The same-step "last write wins, grace never replaces its main" rule moves
  to LiveRecord (E-1).
- `patSvc_requestTarget()` returns 0 when refused (exclusive claim,
  filesystem replacement, non-resident Scene).
- **Where:** schedule D-2a, D-5b, E-1.

### A-19 Hit kinds (schedule E)

- `LIVEREC_HIT_ROLL_HIT` joins the kind list (needed for slow rolls
  recorded hit by hit).
- The flam main is reported from `buttonHandler_overlayTick()`, where it
  fires; the grace from `buttonHandler_overlayTrigger()`.
- **Where:** schedule B-1, E-1, E-2a, E-2b.

### A-20 Erase routing (schedule G-1a, G-2a)

- TRACK presses in Load/Save, global settings and SOM_GEN are consumed with
  no action; SOM_GEN SEQ presses too.
- Knob erase is its own branch before the copy/clear pot branch, which
  never runs while armed.
- **Where:** schedule G-1a, G-2a.

### A-21 Track length override (schedule A1-5c, A1-7d) — plan error

- **Problem:** this plan's §5.2 cleared only the scale/shuffle overrides on
  a menu edit. R12 covers every non-Instrument parameter, so a `len` edit
  must clear the length override (targets 405..411) too.
- **Fix:** `presetMorph_clearTrackParamStepOverride()` takes LENGTH as well,
  and the `len` commits call it.
- **Where:** schedule A1-5c, A1-7d; plan §5.2 corrected.

### A-22 Looper not locked out by record or erase (decision L-2) — moved to P2

Revision 11: this work now lives in `S081_P2_LOOP_WHILE_RECORD.md`. The P1
schedule is back to the interim lockout:

- B-4: `liveRec_forceOff()` before `seq_loopEngage()`;
- B-2a: REC ignored while looping;
- E-1: looper gate.

The record of the revision 10 decision follows.


- **Decision (user):** the looper may run while recording or erasing.
  Looper actions are not recorded; looping only moves playheads.
- **Schedule changes:**
  - B-2a: REC is no longer ignored while looping;
  - B-4: the record-off is removed, not moved;
  - B-2e: the `:1846` row;
  - E-1: no looper gate.
- **What recording must handle while a loop runs:** follow-ups L2–L4.
  - Loop-aware placement for forward snaps.
  - Echo marks expire at the loop wrap.
  - Short loops repeat the same steps, so step-end writes must skip values
    already stored.
  - In PERF, VOICE and BAR presses are shared with the looper.
- **S080 documentation to update at close:**
  - `PATTERN_DYNAMIC_STACK.md` §6.5: the `seq_loopEngage()` row says "turns
    recording off";
  - `MODULE_INTERCHANGE_SPEC.md` PERF surface: "Recording is turned off on
    engage and REC is ignored while looping";
  - `080_SESSION_HANDOFF_LOG.md` §2.3 is history and stays as written.

### Minor corrections

| # | Fix | Schedule |
|---|---|---|
| M-1 | Line references corrected (`seq_rollBegin()` first hit `:1972`, flush `:2259`, `triggerNow` `:153`, and others) | A1-1e, A1-2b, A1-7a, E-2 |
| M-2 | The A1-4e table and snippet agree: per-track sites clear only their slot | A1-4e |
| M-3 | Roll-table comment no longer guesses at history; overlay credited to S080, not S079; `SEQ_PENDING_TYPE_FX_STEP_BIT` removed, not "kept but unused" | A3-1a, A3-5e, A2-3a |
| M-4 | C-2b no longer claims filesystem/copy-clear still read `seq_recordActive` | C-2b |
| M-5 | FX erase pending mask is filled in the foreground render path, not "at TIM3 speed" | G-3b |
| M-6 | RAM summary: no double-counted staging table; drop counters DEV-only (logging-allocation policy); new items added. Totals: about 428 B added in every build, plus 16 B DEV-only, about 80 B released | RAM summary |
| M-7 | Dependency order: C and D are independent; both precede E | Dependency order |
| M-8 | Phase I document list completed from plan §19.14; the nonexistent "MEMORY.md §5.1" reference removed | I-1 |
| M-9 | (revision 11) Ring semantics made consistent: TIM3 pushes an entry when a step starts; LiveRecord treats a track's next entry as the end of its previous step. Before, C-9a pushed at step start while F-5/E-4 and §19.2 read entries as "step ended" | C-9a, E-4, F-5; plan §19.2 |

