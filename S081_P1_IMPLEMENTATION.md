# S081 P1 — Live Record: Implementation Schedule

Session 081 implementation schedule, branch `dev-ph6-cleanup`, base `02553df`.
Cut from `S081_P1_LIVE_RECORD.md` §15 and §19.

Line numbers are at base `02553df` unless noted. Each change entry carries a
comment-block description to be placed adjacent to the code as
documentation-in-place.

---

## Phase A1 — Step Reset Rule, Instrument Menu Override, Per-Target Clears

§5.8, §5.1, §5.2. Prerequisite for all recording: fixes the
trigger/automation ordering defects (§3.3-1/2) and establishes R12.

### A1-1. MidiVoiceControl: per-voice pending-reset mask

**File:** `Core/MIDI/MidiVoiceControl.c`

#### A1-1a. New static variable — ADD after line 70

```
/*
 * Per-voice pending-reset mask (S081 §5.8, §19.15).
 *
 * What: bit N (0..6) is set when voice N's next trigger must restore
 * Instrument automation to the Morph base and clear the menu override
 * before the voice fires. Why: voiceControl_noteOff() deletes queued
 * entries for that voice, so a flag stored inside VoiceTriggerEvent
 * would be lost on retrigger. A separate mask survives drops: a reset
 * stays pending until a trigger or end-of-pass consumes it. Inputs:
 * set by voiceControl_noteOn() (every live/seq first trigger),
 * voiceControl_resetOnly() (roll renewal with no trigger). Cleared by
 * voiceControl_processPending() when consumed. Outputs: gates the call
 * to seq_restoreAutomatedParameters() and the menu-override clear.
 * Affiliates: seq_triggerVoice() reset argument, main.c audio loop
 * ordering (voiceControl_processPending → seq_drainPendingAutomation).
 * RAM: +1 B SRAM1.
 */
static uint8_t voiceControl_pendingReset;
```

#### A1-1b. voiceControl_noteOn() — MODIFY at line ~108

Set the reset bit for the voice under the existing PRIMASK section.

```
/*
 * Live and sequencer first triggers always request a reset (F12, §5.8).
 * The bit is set under the queue's existing PRIMASK so that the enqueue
 * and the reset intent are published atomically. Roll retriggers 2+ use
 * voiceControl_noteOnNoReset() instead.
 */
voiceControl_pendingReset |= (uint8_t)(1u << voice);
```

#### A1-1c. New voiceControl_noteOnNoReset() — ADD after voiceControl_noteOn()

```
/*
 * Enqueue a voice trigger without requesting an automation reset (S081 §5.8).
 *
 * What: identical to voiceControl_noteOn() but does not set the
 * pending-reset bit. Why: roll retriggers 2+ must not restore the
 * Morph base or clear the menu override — only the roll's first hit
 * resets (F-L). Inputs: voice 0..6, note, vel. Outputs: ring entry
 * enqueued. Affiliates: seq_triggerVoice(…, reset=0) for roll hits,
 * voiceControl_noteOn() for first triggers.
 */
void voiceControl_noteOnNoReset(uint8_t voice, uint8_t note, uint8_t vel);
```

#### A1-1d. New voiceControl_resetOnly() — ADD

```
/*
 * Request an automation reset for a voice with no trigger (S081 §5.8).
 *
 * What: sets the pending-reset bit under PRIMASK without enqueuing a
 * ring entry. Why: a roll carried across steps renews the roll at the
 * new step boundary. The renewal resets automation (so the new step's
 * entries apply cleanly) but fires no new first hit. Inputs: voice
 * 0..6. Outputs: pending-reset bit set; consumed by the next
 * voiceControl_processPending() pass. Affiliates: seq_rollBegin()
 * carry path, seq_restoreAutomatedParameters().
 */
void voiceControl_resetOnly(uint8_t voice);
```

#### A1-1e. voiceControl_triggerNow() — MODIFY at line ~170

Remove the unconditional `seq_restoreAutomatedParameters(ev.voice)` call.

```
/*
 * The restore is no longer called here. It moves to
 * voiceControl_processPending(), gated by the pending-reset mask, so
 * that only first triggers restore and roll retriggers do not (§5.8).
 */
```

#### A1-1f. voiceControl_processPending() — MODIFY at line ~185

```
/*
 * Foreground voice-trigger drain with conditional reset (S081 §5.8).
 *
 * What: pops queued VoiceTriggerEvents. For each event whose voice has
 * the pending-reset bit set, runs seq_restoreAutomatedParameters()
 * and clears the bit before triggering (preset_applyDeferredSceneSlotForTrigger,
 * instrumentManager_triggerTrack, busComp_sidechainTrigger, led_pulseLed).
 * After the ring is empty, any bits still set (from voiceControl_resetOnly
 * or entries dropped by noteOff) are consumed: restore runs, bit clears.
 * Why: the main.c ordering voiceControl_processPending() →
 * seq_drainPendingAutomation() guarantees reset → trigger → that step's
 * automation for every trigger type. Inputs: the ring and the mask.
 * Outputs: instruments triggered, automation restored where needed.
 * Affiliates: main.c:212-213 audio loop, seq_menuOverride[] clear
 * inside seq_restoreAutomatedParameters().
 */
```

**File:** `Core/MIDI/MidiVoiceControl.h`

#### A1-1g. Header declarations — ADD after line 75

```
void voiceControl_noteOnNoReset(uint8_t voice, uint8_t note, uint8_t vel);
void voiceControl_resetOnly(uint8_t voice);
```

---

### A1-2. Sequencer: seq_triggerVoice gains reset argument

**File:** `Core/Sequencer/sequencer.c`

#### A1-2a. seq_triggerVoice() signature — MODIFY at line 1268

```
/*
 * Trigger a voice with optional automation reset (S081 §5.8).
 *
 * What: fires noteOff then noteOn (or noteOnNoReset) for the voice,
 * sends MIDI, triggers hardware trigger output. The reset argument
 * selects whether the pending-reset mask is set: 1 for first triggers
 * (step start, deferred fire, roll first hit, overlay, preview), 0 for
 * roll retriggers 2+. Why: only a step's first trigger may restore the
 * Morph base and clear the menu override; later roll hits must not
 * (F-L). Inputs: voiceNr 0..6, vol, note, reset 0/1. Outputs: voice
 * queued in the trigger ring, pending-reset bit set if reset=1.
 * Affiliates: voiceControl_noteOn/noteOnNoReset, seq_advanceTrackStep
 * callers, seq_processRollTicks, buttonHandler overlay,
 * seq_previewVoice.
 */
void seq_triggerVoice(uint8_t voiceNr, uint8_t vol, uint8_t note, uint8_t reset)
```

The body replaces `voiceControl_noteOn(voiceNr, note, vol)` with:
```c
if (reset)
    voiceControl_noteOn(voiceNr, note, vol);
else
    voiceControl_noteOnNoReset(voiceNr, note, vol);
```

#### A1-2b. All seq_triggerVoice callers — MODIFY

| Caller | Line | reset | Reason |
|---|---|---|---|
| `seq_advanceTrackStep` on-time trigger | 2287/2292 | 1 | first trigger of the step |
| `seq_advanceTrackStep` deferred flush | 2257 | 1 | first trigger fires at flush |
| `seq_rollBegin` first hit (`!carry`) | 1996 | 1 | first hit of a new roll |
| `seq_rollBegin` carry path | 1999 | — | uses `voiceControl_resetOnly()` instead (no trigger) |
| `seq_processRollTicks` retrigger | 3780 | 0 | roll retrigger 2+ |
| `seq_previewVoice` | 1337 | 1 | live trigger |
| overlay in `buttonHandler.c` | 871-957 | 1 | live trigger |
| legacy roll block (removed in A3) | 2314 | — | removed |

**File:** `Core/Sequencer/sequencer.h`

#### A1-2c. Header declaration — MODIFY

Update `seq_triggerVoice` declaration to include the `reset` parameter.

---

### A1-3. Sequencer: deferred trigger carries step index and roll flag

**File:** `Core/Sequencer/sequencer.c`

#### A1-3a. New deferral state — ADD after line 211

```
/*
 * Deferred-trigger step index and roll flag per track (S081 §5.8).
 *
 * What: when a trigger is deferred by shuffle or micro-timing,
 * seq_trackShuffleStep[track] records the step that produced it and
 * seq_trackShuffleRoll[track] records whether it was a roll step.
 * Why: at the deferred fire time, the step's automation entries must be
 * queued (seq_queueStepAutomations) and a roll step must start via
 * seq_rollBegin rather than a plain trigger. The existing deferral
 * state (vel, note, delay, pending) carries only the trigger data, not
 * the step context. Inputs: written by seq_advanceTrackStep at the
 * deferral point. Outputs: read by seq_processDeferredTriggers at fire
 * time. RAM: +14 B SRAM1 (7 × u8 + 7 × u8). Affiliates:
 * seq_trackShuffleDelay/Pending/Vel/Note, seq_queueStepAutomations,
 * seq_rollBegin.
 */
static uint8_t seq_trackShuffleStep[NUM_TRACKS];
static uint8_t seq_trackShuffleRoll[NUM_TRACKS];
```

#### A1-3b. seq_advanceTrackStep trigger deferral — MODIFY at lines 2250–2295

The trigger branch currently skips roll steps past the deferral path (roll
steps call `seq_rollBegin()` immediately at line 2253). Change so that roll
steps go through the same shuffle/timing deferral as plain steps:

```
/*
 * Roll steps honour shuffle and micro-timing (S081 §3.3-15, F-S).
 *
 * What: both roll and non-roll triggers are deferred by the same
 * shuffle + micro-timing delay. The deferral stores the step index and
 * a roll flag so that at fire time seq_processDeferredTriggers can call
 * seq_rollBegin (for rolls) or seq_triggerVoice (for plain hits).
 * Why: pre-existing defect — roll steps ignored shuffle and micro-
 * timing, causing them to play early. The deferred automation queueing
 * (seq_queueStepAutomations) is also skipped until the deferred fire,
 * fixing §3.3-1 (delayed trigger undoes its own step's automation).
 * Inputs: sp.roll, shuffle_val, sp.timing, step index. Outputs:
 * seq_trackShuffle* deferral state. Affiliates:
 * seq_processDeferredTriggers, seq_rollBegin, seq_queueStepAutomations.
 */
```

The new logic:
1. Flush any existing pending deferred trigger for this track (fire it now).
2. Compute `total_delay` from shuffle and micro-timing (existing code).
3. If `total_delay > 0`: defer. Store `seq_trackShuffleStep[track]`,
   `seq_trackShuffleRoll[track]`, vel, note, delay. Skip
   `seq_queueStepAutomations()` — it will be called at fire time.
4. If `total_delay == 0` and roll: call `seq_queueStepAutomations(track, step)`,
   then `seq_rollBegin(track, &sp)` with reset=1 on the first hit.
5. If `total_delay == 0` and not roll: call `seq_queueStepAutomations(track, step)`,
   then `seq_triggerVoice(track, sp.velocity, sp.note, 1)`.

#### A1-3c. seq_queueStepAutomations skip for deferred steps — MODIFY at lines 2303–2305

```
/*
 * Step automation queueing is moved to the deferred fire point (§5.8).
 *
 * What: the call to seq_queueStepAutomations that was here is removed.
 * For on-time steps, the trigger branch above calls it directly. For
 * deferred steps, seq_processDeferredTriggers calls it at fire time.
 * Why: queueing automation at the step start and triggering later means
 * the foreground order (trigger funnel → drain) runs the restore before
 * the step's own automation, undoing it. By co-locating queueing with
 * the trigger, the entries arrive in the pending buffer at the right
 * time. Affiliates: seq_drainPendingAutomation (main.c:212-213).
 */
```

The `seq_queueStepAutomations` call at line 2305 is removed from the
step-allowed block. It is instead called in the trigger emission site (A1-3b)
and in `seq_processDeferredTriggers` (A1-3d). The FX marker call
(`seq_queueEffectStepMarker`, line 2302) is removed in A2.

#### A1-3d. seq_processDeferredTriggers — MODIFY at lines 3710–3725

```
/*
 * Fire deferred triggers with automation and optional roll (S081 §5.8).
 *
 * What: when a deferred trigger's delay expires, this function now:
 * (1) queues the step's automation entries via seq_queueStepAutomations,
 * (2) fires the trigger — seq_rollBegin for roll steps, seq_triggerVoice
 * with reset=1 for plain steps. Why: co-locating the automation queue
 * and the trigger at the deferred fire point preserves the foreground
 * ordering guarantee: voiceControl_processPending (reset+trigger) runs
 * before seq_drainPendingAutomation (applies entries), so the step's
 * own automation survives its delayed trigger. Inputs:
 * seq_trackShuffleStep, seq_trackShuffleRoll, vel, note. Outputs:
 * automation entries in the pending buffer, voice in the trigger ring.
 * Affiliates: seq_advanceTrackStep deferral, seq_processSchedulerTick
 * (caller), main.c audio loop ordering.
 */
static void seq_processDeferredTriggers(void)
{
    /* For each track with seq_trackShufflePending:
     *   if delay expired:
     *     seq_queueStepAutomations(track, seq_trackShuffleStep[track]);
     *     if (seq_trackShuffleRoll[track])
     *         re-read specials, seq_rollBegin(track, &sp);  // first hit has reset=1
     *     else
     *         seq_triggerVoice(track, vel, note, 1);
     *     clear pending.
     *   else: decrement delay.
     */
}
```

#### A1-3e. Roll end rule (F-R) — MODIFY at lines 2203–2206

```
/*
 * A running roll ends only when the next step has a trigger that fires
 * (S081 F-R, §3.3-16).
 *
 * What: the previous code ended a roll at any step with sp.roll == 0,
 * including empty and automation-only steps. The new code ends a roll
 * only when the step has a trigger (bit 15 set) and its condition
 * passes. An empty step does not cut the roll; the roll engine ends it
 * naturally after its fill window (seq_rollRemaining reaches 0). Why:
 * a roll delayed into the next step's time was immediately cut by that
 * boundary, losing the last hits. With this change, a roll runs its
 * own length from its delayed first hit, as designed. Inputs: sp.roll,
 * step trigger state, condition result. Outputs: seq_rollActive may be
 * cleared. Affiliates: seq_rollBegin carry path, seq_rollRemaining,
 * seq_processRollTicks.
 */
```

New logic: replace `if (sp.roll == 0u) { seq_rollActive[track] = 0u; ... }`
with:
```c
if (seq_rollActive[track] && sp.roll == 0u &&
    pat_isStepActive(...) && step_allowed) {
    seq_rollActive[track] = 0u;
    seq_rollDuckActive[track] = 0u;
}
```

#### A1-3f. seq_rollBegin carry path — MODIFY at line 1999

```
/*
 * Roll renewal requests a reset-only (no new trigger) (S081 §5.8).
 *
 * What: when a roll carries across a step boundary (same rate/rhythm/
 * fill), the carry path adds the new step's remaining slots but fires
 * no new first hit. It now calls voiceControl_resetOnly(track) so that
 * the pending-reset mask is set, causing the next
 * voiceControl_processPending pass to restore automation and clear the
 * menu override without an audible trigger. Why: the new step may have
 * different automation entries; the restore ensures the old step's
 * entries do not persist. Inputs: track. Outputs: pending-reset bit
 * set. Affiliates: voiceControl_resetOnly, seq_restoreAutomatedParameters,
 * the ring push in seq_advanceTrackStep.
 */
voiceControl_resetOnly(track);
```

---

### A1-4. Sequencer: Instrument menu-override bitmap (R12)

**File:** `Core/Sequencer/sequencer.c`

#### A1-4a. New static variable — ADD after line 734

```
/*
 * Per-slot Instrument menu-override bitmap (S081 §5.1, R12).
 *
 * What: 6 × uint64_t, one per Instrument slot (0..5). Bit N is set
 * when the user has edited Instrument parameter N from the menu and the
 * edit should suppress that parameter's step automation until the next
 * trigger of that voice (or a transport/Scene change). Why: R12 says
 * "a menu edit of an automated Instrument parameter sounds at once and
 * suppresses that parameter's automation until the voice's next
 * trigger." This replaces the S075 F3 rule where automation always beat
 * the menu. Inputs: set by seq_menuOverrideSet() (called from
 * menu_cellCommitValue). Cleared by seq_restoreAutomatedParameters()
 * (on trigger), and by seq_setStepIndexToStart / seq_selectActivePattern
 * / seq_alignActivePatternToScene / seq_setTrackPlayedScene (on
 * transport/Scene events). Outputs: read by seq_drainPendingAutomation
 * to skip entries. RAM: +48 B SRAM1.
 * Affiliates: seq_automation_dirty[], presetMorph_writeRuntimeBase().
 */
static uint64_t seq_menuOverride[INSTRUMENT_SLOT_COUNT];
```

#### A1-4b. New seq_menuOverrideSet() — ADD

```
/*
 * Set the menu-override bit and clear the held bit for one parameter
 * (S081 §5.1).
 *
 * What: sets bit `local` in seq_menuOverride[slot] and clears the same
 * bit in seq_automation_dirty[slot]. Why: the held bit gates whether
 * presetMorph_writeRuntimeBase() applies the Morph sweep. Clearing it
 * lets the menu edit's endpoint write stand. The override bit then
 * tells the drain to skip that parameter's automation entries.
 * Foreground only — the drain and restore are foreground too, so no
 * PRIMASK. Inputs: slot 0..5, local parameter index. Outputs: override
 * and dirty bitmaps updated. Affiliates: menu_cellCommitValue
 * INSTRUMENT branch, seq_drainPendingAutomation filter,
 * seq_restoreAutomatedParameters clear.
 */
void seq_menuOverrideSet(uint8_t slot, uint8_t local);
```

#### A1-4c. seq_restoreAutomatedParameters — MODIFY at lines 2564–2602

```
/*
 * Restore automated parameters and clear the menu override (S081 §5.8).
 *
 * What: iterates the seq_automation_dirty[slot] bitmap, restoring each
 * held parameter to its Morph-interpolated base, then clears both
 * seq_automation_dirty[slot] and seq_menuOverride[slot]. This function
 * now runs conditionally — only for voices with the pending-reset bit
 * set in voiceControl_pendingReset. Why: under the old rule, every
 * trigger (including roll retriggers) restored, which undid a step's
 * own automation on delayed triggers and on every roll hit after the
 * first. Now only the first trigger restores, and the override clear
 * ensures the next step's automation can apply. Inputs: trigger_track
 * 0..6. Outputs: runtime parameters written to the Morph base,
 * override cleared. Affiliates: voiceControl_processPending (caller),
 * seq_menuOverrideSet, seq_drainPendingAutomation.
 */
```

Add `seq_menuOverride[slot] = 0u;` after `seq_automation_dirty[slot] = 0u;`.

#### A1-4d. seq_drainPendingAutomation — MODIFY at lines 2442–2551

```
/*
 * Skip overridden parameters in the drain (S081 §5.1, R12).
 *
 * What: for each Instrument-parameter entry in the pending buffer,
 * check seq_menuOverride[slot] bit for the parameter's local index.
 * If set, skip that entry — the menu edit takes priority. Why: R12
 * says the menu value suppresses automation until the next trigger.
 * The override bit is cleared by seq_restoreAutomatedParameters at the
 * next trigger, so automation resumes. Inputs: seq_menuOverride[slot].
 * Outputs: entry skipped or applied. Affiliates: seq_menuOverrideSet,
 * seq_restoreAutomatedParameters.
 */
```

In the `instrumentParam_isVoiceParameter(target)` branch (line 2476), after
the slot/local computation, add:
```c
if (seq_menuOverride[slot] & (1ULL << local))
    { i++; continue; }
```

#### A1-4e. Override clear at transport/Scene sites — MODIFY

All of these sites add `memset(seq_menuOverride, 0, sizeof(seq_menuOverride));`:

| Function | Line | Event |
|---|---|---|
| `seq_setStepIndexToStart()` | 4478 | transport start/stop/reset |
| `seq_selectActivePattern()` | 1386 | direct Scene change |
| `seq_alignActivePatternToScene()` | 1495 | align-to-Scene |
| `seq_setTrackPlayedScene()` | 1159 | per-track Scene reassignment (that slot + pair) |
| `seq_realignActivePatternToMasterClock()` | 2719 | double-click realign |
| `seq_realignTrackToMasterClock()` | 2820 | per-track realign |

```
/*
 * Clear the menu override at transport/Scene change (S081 §5.1).
 *
 * What: zeroes the entire seq_menuOverride array (or the affected
 * slot for per-track changes). Why: R12 override ends at transport
 * start/stop/reset and at Scene changes. After clearing, step
 * automation resumes on the next drain pass. Affiliates:
 * seq_restoreAutomatedParameters (per-trigger clear),
 * seq_menuOverrideSet (set site).
 */
```

**File:** `Core/Sequencer/sequencer.h`

#### A1-4f. Header declaration — ADD

```
void seq_menuOverrideSet(uint8_t slot, uint8_t local);
```

---

### A1-5. Preset / presetMorphEngine: per-target Scene clears (§5.2)

**File:** `Core/Bank/Scene/Preset/presetManager.c`

#### A1-5a. New preset_clearFxSendStepOverride(slot) — ADD

```
/*
 * Clear a single voice slot's FX-send step-automation override (S081 §5.2).
 *
 * What: clears the per-slot FX-send override so the value returns to
 * the Scene-stored amount. Why: R12/§5.2 says a menu edit of FX send
 * clears that slot's override. Only bulk-clear versions existed before;
 * live record and live erase need per-slot granularity. Inputs: slot
 * 0..5. Outputs: the slot's FX-send override is removed; the runtime
 * reverts to the Scene base. Affiliates: menu_cellCommitValue
 * SCENE_SETTING FX_SEND branch, preset_setFxSendStepOverride (set
 * site), seq_applySceneAutomation FX_SEND branch.
 */
void preset_clearFxSendStepOverride(uint8_t slot);
```

#### A1-5b. New preset_clearAudioOutStepOverride(slot) — ADD

```
/*
 * Clear a single voice slot's audio-out step-automation override and
 * re-apply the Scene-stored route (S081 §5.2).
 *
 * What: clears the per-slot audio-out override and calls
 * preset_applyVoiceAudioOutRuntime(slot, scene_getVoiceAudioOut(A, slot))
 * to restore the Scene base route. Why: unlike FX send, clearing the
 * audio-out override alone would leave the runtime on the last
 * automation value until the next trigger. The explicit re-apply makes
 * the menu edit audible immediately. Inputs: slot 0..5. Outputs:
 * audio-out override cleared, runtime route restored. Affiliates:
 * menu_cellCommitValue SCENE_SETTING AUDIO_OUT branch,
 * preset_setAudioOutStepOverride, preset_applyVoiceAudioOutRuntime,
 * scene_getVoiceAudioOut.
 */
void preset_clearAudioOutStepOverride(uint8_t slot);
```

**File:** `Core/Bank/Scene/Preset/presetMorphEngine.c`

#### A1-5c. New presetMorph_clearTrackParamStepOverride(track, param) — ADD

```
/*
 * Clear a single track's scale/shuffle step-automation override and
 * refresh the effective cache (S081 §5.2).
 *
 * What: clears the per-track step override for the given param
 * (PRESETMORPH_TRACK_SCALE or PRESETMORPH_TRACK_SHUFFLE) and calls
 * seq_refreshTrackEffectiveParams() to recompute the effective values
 * from the Scene base. Why: when the user edits scale or shuffle from
 * the menu, the override must clear so the edit takes effect. The
 * effective cache must also update so TIM3 uses the new interval.
 * Inputs: track 0..6, param (PRESETMORPH_TRACK_SCALE or _SHUFFLE).
 * Outputs: override cleared, effective cache refreshed. Affiliates:
 * presetMorph_setTrackParamStepOverride (set site), menu.c STEP
 * scale/shuffle commit, seq_effectiveTrackScale/Shuffle arrays,
 * seq_refreshTrackEffectiveParams.
 */
void presetMorph_clearTrackParamStepOverride(uint8_t track,
                                              presetMorph_trackParam_t param);
```

**File:** `Core/Bank/Scene/Preset/presetMorphEngine.h`

#### A1-5d. Header declaration — ADD

```
void presetMorph_clearTrackParamStepOverride(uint8_t track,
                                              presetMorph_trackParam_t param);
```

**File:** `Core/Bank/Scene/Preset/presetManager.h`

#### A1-5e. Header declarations — ADD

```
void preset_clearFxSendStepOverride(uint8_t slot);
void preset_clearAudioOutStepOverride(uint8_t slot);
```

---

### A1-6. Sequencer: per-target Scene-automation bit clear

**File:** `Core/Sequencer/sequencer.c`

#### A1-6a. New seq_clearSceneAutomationBit() — ADD

```
/*
 * Clear one bit in the Scene-automation dirty mask (S081 §5.2).
 *
 * What: clears the bit at position `index` in seq_scene_automation_dirty.
 * Why: when the user edits a Scene-target parameter from the menu, the
 * per-target clear (§5.2) must also clear that target's held bit so the
 * restore path does not revert the edit. The existing bulk clear
 * (seq_clearSceneAutomationDirty) clears all bits and is used at Scene
 * changes. This per-bit version serves the R12 menu-edit path. Inputs:
 * index, the bit position from sceneModTarget_indexFromId(). Outputs:
 * one bit cleared. Affiliates: seq_scene_automation_dirty,
 * seq_restoreAllSceneAutomation, the per-target clear functions in
 * preset/presetMorphEngine.
 */
void seq_clearSceneAutomationBit(uint8_t index);
```

**File:** `Core/Sequencer/sequencer.h`

#### A1-6b. Header declaration — ADD

```
void seq_clearSceneAutomationBit(uint8_t index);
```

---

### A1-7. Menu: R12 menu-edit call sites

**File:** `Core/Menu/menu.c`

#### A1-7a. menu_cellCommitValue INSTRUMENT branch — MODIFY at line ~4817

```
/*
 * Instrument menu edit sets the override before applying (S081 §5.1, R12).
 *
 * What: before calling preset_setInstrumentParameter(), calls
 * seq_menuOverrideSet(slot, local) for each slot in the active Scene's
 * edit mask that is being written. Why: R12 says a menu edit suppresses
 * that parameter's step automation until the next trigger. The override
 * must be set before the parameter write so that any concurrent drain
 * pass sees it. Inputs: slot from the edit mask, local from the cell
 * target. Outputs: override bit set, dirty bit cleared. Affiliates:
 * seq_menuOverrideSet, preset_setInstrumentParameter,
 * seq_drainPendingAutomation filter.
 */
```

#### A1-7b. menu_cellCommitValue SCENE_SETTING branches — MODIFY at line ~4817

For `AUDIO_OUT`, `FX_SEND` branches:

```
/*
 * Scene-setting menu edit clears that slot's override (S081 §5.2).
 *
 * What: after the Scene-setting commit, calls the per-slot clear
 * (preset_clearAudioOutStepOverride or preset_clearFxSendStepOverride)
 * and seq_clearSceneAutomationBit(index). Why: §5.2 says a menu edit
 * of these parameters clears the automation override so the menu value
 * takes effect immediately. Inputs: slot, Scene automation index.
 * Outputs: override cleared, dirty bit cleared. Affiliates:
 * preset_clearAudioOutStepOverride, preset_clearFxSendStepOverride,
 * seq_clearSceneAutomationBit.
 */
```

#### A1-7c. menu_cellCommitValue KIT_SETTING 7dc — MODIFY

```
/*
 * 7dc menu edit clears its step override (S081 §5.2).
 *
 * What: calls instrumentManager_clearSlot6Track7StepDecayOverride()
 * and seq_clearSceneAutomationBit(index). The clear function already
 * exists at InstrumentManager.c:2695. Why: same as A1-7b. Affiliates:
 * instrumentManager_clearSlot6Track7StepDecayOverride.
 */
```

#### A1-7d. STEP scale/shuffle commits — MODIFY at lines ~15886/15896/15945 and ~4993–4999

```
/*
 * Track scale/shuffle menu edit clears the step override (S081 §5.2).
 *
 * What: after committing the new value, calls
 * presetMorph_clearTrackParamStepOverride(track, param) and
 * seq_clearSceneAutomationBit(index). Why: §5.2 says a menu edit
 * clears that track's scale or shuffle override and refreshes the
 * effective cache so TIM3 uses the new value immediately. Inputs:
 * track, param, Scene automation index. Affiliates:
 * presetMorph_clearTrackParamStepOverride,
 * seq_clearSceneAutomationBit, seq_refreshTrackEffectiveParams.
 */
```

---

## Phase A2 — Effect Parameter Rules (R13)

§5.3. Removes step markers, adds menuHold, makes Morph lane one-step.

### A2-1. EffectsManager: remove dead owner/marker state

**File:** `Core/DSP/Effects/EffectsManager.c`

#### A2-1a. Remove fields from effects_automation_t — MODIFY at line ~196

Remove: `owner[64]`, `pending_end` (u64), `pending_track` (u8),
`owner_tracks` (u8). ~75 B released.

```
/*
 * Owner and pending-end fields removed (S081 §3.3-3, §5.3, R13).
 *
 * What: under R13, Effect overlays are "set once, held until cleared"
 * with no per-step ownership tracking. The owner array, pending_end
 * mask, pending_track and owner_tracks have no remaining reader.
 * Why: step markers are removed; locks start and end at FX step
 * boundaries, not at pattern-track step boundaries. The 75 B are
 * released per policy (not reused). Affiliates: effects_automation_t
 * static assert, STORAGE_SRAM_MANIFEST.md.
 */
```

#### A2-1b. Remove effects_automationStepBegin — REMOVE at line ~1209

```
/*
 * Removed: effects_automationStepBegin (S081 §5.3, R13).
 *
 * What: this function was called by the drain when it encountered a
 * step-marker entry (SEQ_PENDING_TYPE_FX_STEP_BIT). It tracked which
 * tracks had stepped and published owner state. Why: R13 removes step
 * markers entirely. Effect overlays are now applied as simple last-
 * writer-wins, and locks are managed at FX-step boundaries. Affiliates:
 * seq_drainPendingAutomation (marker branch removed),
 * seq_queueEffectStepMarker (removed).
 */
```

#### A2-1c. Remove effects_automationStepFlush — REMOVE at line ~1258

```
/*
 * Removed: effects_automationStepFlush (S081 §5.3, R13).
 *
 * What: called at the end of seq_drainPendingAutomation to flush the
 * current step's owner state to pending_end. Removed because step
 * markers and pending_end are gone. Affiliates:
 * seq_drainPendingAutomation final call (removed).
 */
```

#### A2-1d. Remove effects_automationPublishOwners — REMOVE

If present, remove for the same reason.

#### A2-1e. Remove held_morph latch — MODIFY

Remove `held_morph`, `held_morph_valid` from `effects_state_t` (~2 B).

```
/*
 * Held Morph latch removed (S081 §5.3, R13).
 *
 * What: the FX Morph lane previously held its value through unlocked
 * steps, using held_morph/held_morph_valid. Under R13, every FX
 * parameter including Morph is one-step: a lock applies at its step
 * start and returns to the menu value at its step end. Why: the held
 * latch contradicted R13's "set once and forgotten" rule. Inputs:
 * formerly set by effects_seqLatchMorph (line 728). Affiliates:
 * effects_seqConsume, effects_seqSelect, effects_service resolution.
 */
```

Remove calls to `effects_seqLatchMorph()` in `effects_seqConsume()` (~line 761)
and `effects_seqSelect()` (~line 829), and the `held_morph` read in
`effects_service()` (~line 1707).

### A2-2. EffectsManager: add menuHold bitmap

**File:** `Core/DSP/Effects/EffectsManager.c`

#### A2-2a. New menuHold state in effects_automation_t — ADD

```
/*
 * Per-parameter menuHold bitmap and Morph menuHold flag (S081 §5.3, R13).
 *
 * What: uint64_t menuHold in effects_automation_t, plus a uint8_t
 * menuHoldMorph flag. Bit N of menuHold is set when the user has edited
 * Effect parameter N from the menu since the last automation of that
 * parameter. menuHoldMorph is the same for the Effect Morph amount.
 * Why: R13 priority is menuHold > lock > overlay > menu. A menu edit
 * clears the overlay and sets menuHold; the next automation (lock start
 * or track overlay) clears menuHold and applies. This keeps the menu
 * value sounding until new automation arrives. Inputs: set by
 * effects_setParameter/effects_setMorphAmount (menu edits). Cleared by
 * effects_applyAutomation/effects_setMorphAutomation (automation),
 * effects_automationReset (Scene change/realign), and lock step start.
 * Outputs: read by effects_service resolution. RAM: +9 B SRAM1.
 * Affiliates: effects_service resolution order, seq_drainPendingAutomation.
 */
uint64_t menuHold;
uint8_t  menuHoldMorph;
```

#### A2-2b. effects_setParameter — MODIFY at line ~581

```
/*
 * Menu edit of an Effect parameter clears its overlay and sets menuHold
 * (S081 §5.3, R13, F-Q).
 *
 * What: when the origin Scene matches the active Scene and the row is
 * in the runtime Effect type, clears the overlay (active bit) for that
 * local parameter and sets menuHold for it. Why: R13 says a menu edit
 * beats both lock and overlay until the next automation. Clearing the
 * overlay ensures the lock can see the menu value when it starts.
 * Setting menuHold prevents the lock from overriding the menu value
 * for the remainder of its step. Inputs: local parameter index, origin
 * Scene. Outputs: overlay cleared, menuHold set. Affiliates:
 * effects_applyAutomation (clears menuHold on next automation),
 * effects_service resolution.
 */
```

#### A2-2c. effects_setMorphAmount — MODIFY at line ~705

Same logic for the Effect Morph amount: clear `morph_override_valid`, set
`menuHoldMorph`.

#### A2-2d. effects_applyAutomation — MODIFY at line ~1276

```
/*
 * Track automation of an Effect parameter clears menuHold (S081 §5.3).
 *
 * What: sets the overlay value and active bit for the local parameter,
 * and clears the menuHold bit. Why: R13 says the next automation of a
 * parameter after a menu edit clears the menu hold and applies. The
 * track argument is unused after owner removal but kept for signature
 * stability. Inputs: owner_track, local, value. Outputs: overlay set,
 * menuHold cleared. Affiliates: seq_drainPendingAutomation Effect
 * branch, effects_service resolution.
 */
```

#### A2-2e. effects_setMorphAutomation — MODIFY at line ~1301

Clear `menuHoldMorph` on track automation of the Morph amount.

#### A2-2f. Lock step start: clear overlay and menuHold — MODIFY

In `effects_seqConsume()` (STEP handler) and `effects_seqSelect()` (`sel`
handler), for each lane locked on the new step:

```
/*
 * Lock step start clears overlay and menuHold (S081 §5.3, R13).
 *
 * What: for each FX lane locked on the new step, clears the overlay
 * bit and the menuHold bit of the mapped Effect parameter. For lane 0
 * (Morph), clears morph_override_valid and menuHoldMorph. Why: R13
 * says a lock beats a track overlay, and a lock start after a menu
 * edit should clear menuHold so the lock value can apply. After the
 * lock step ends, the parameter returns to the menu value (not the
 * overlay). Inputs: lane mask from the FX step. Outputs: overlay and
 * menuHold bits cleared. Affiliates: effects_service resolution
 * (lock > overlay when menuHold not set), live erase lane exclusion
 * (Phase G).
 */
```

#### A2-2g. effects_automationReset — MODIFY at line ~1238

```
/*
 * Reset clears menuHold and erase state (S081 §5.3).
 *
 * What: in addition to clearing overlay active bits, also zeroes
 * menuHold, menuHoldMorph, and the erase lane mask (Phase G). Why:
 * a Scene change or realign resets all Effect parameter state. The
 * existing reset already clears overlays; menuHold must follow.
 * Affiliates: seq_fxPublishReset → seq_drainPendingAutomation →
 * effects_automationReset call chain.
 */
```

#### A2-2h. effects_service resolution — MODIFY at line ~1679

```
/*
 * R13 resolution priority: menuHold > lock > overlay > menu (S081 §5.3).
 *
 * What: for each Effect parameter, the resolution reads menuHold before
 * checking the lock and overlay. If menuHold is set, the menu value is
 * used regardless of lock or overlay state. Otherwise: if a lock is
 * active (and the parameter's lock is not under live erase), the lock
 * value is used; if an overlay is active, the overlay value is used;
 * otherwise the menu value. For Morph: menuHoldMorph > lockMorph
 * (current step) > morph_override > menu. Why: this implements the
 * three-layer priority of R13. Inputs: menuHold, lock state, overlay
 * state. Outputs: resolved parameter value applied to the DSP chain.
 * Affiliates: effects_setParameter (sets menuHold), lock step start
 * (clears menuHold), effects_applyAutomation (clears menuHold).
 */
```

### A2-3. Sequencer: remove step markers

**File:** `Core/Sequencer/sequencer.c`

#### A2-3a. Remove seq_queueEffectStepMarker — REMOVE at lines 1698–1730

```
/*
 * Removed: Effect step markers (S081 §5.3, R13).
 *
 * What: seq_queueEffectStepMarker() queued a marker entry with
 * SEQ_PENDING_TYPE_FX_STEP_BIT for each track step. The drain called
 * effects_automationStepBegin() on these. Under R13, Effect overlays
 * are last-writer-wins with no per-step ownership tracking, so markers
 * are unnecessary. Affiliates: seq_advanceTrackStep caller (removed),
 * SEQ_PENDING_TYPE_FX_STEP_BIT (kept but unused), seq_drainPendingAutomation
 * marker branch (removed).
 */
```

#### A2-3b. Remove the marker call in seq_advanceTrackStep — MODIFY at line ~2302

Remove `seq_queueEffectStepMarker(track, ...)` from the step-allowed block.

#### A2-3c. seq_drainPendingAutomation: remove marker branch — MODIFY at line ~2473

Remove the `if ((identity & SEQ_PENDING_TYPE_FX_STEP_BIT) != 0u)` branch
and the final `effects_automationStepFlush()` call after the drain loop.

```
/*
 * Marker branch and flush removed from drain (S081 §5.3, R13).
 *
 * What: the FX step marker entries and the final flush call are
 * removed. The drain now processes only automation entries
 * (SEQ_PENDING_TYPE_AUTOMATION_BIT). Why: step markers served the
 * per-step ownership model which R13 replaces. Affiliates:
 * effects_automationStepBegin (removed), effects_automationStepFlush
 * (removed).
 */
```

### A2-4. Sequencer: realign publishes FX reset (§3.3-4)

**File:** `Core/Sequencer/sequencer.c`

#### A2-4a. seq_realignActivePatternToMasterClock — MODIFY at line 2719

```
/*
 * Realign clears Effect overlays (S081 §3.3-4, §5.3).
 *
 * What: calls seq_fxPublishReset() so the next drain pass clears all
 * Effect overlay/lock/menuHold state. Why: a realign is a transport
 * event that should reset Effect state like a Scene change, but this
 * was missing. Without it, stale overlays persisted after double-click
 * realign. Affiliates: seq_fxPublishReset, effects_automationReset.
 */
```

Add `seq_fxPublishReset();` at the start of the function body.

#### A2-4b. seq_realignTrackToMasterClock — MODIFY at line 2820

Same addition. Add `seq_fxPublishReset();` at the start.

---

## Phase A3 — Roll Table, qnt trk, Overlay Fixes, PAR_ROLL Retirement

### A3-1. Roll table fix (§5.4, R17)

**File:** `Core/Sequencer/sequencer.c`

#### A3-1a. seq_rollPpq — MODIFY at lines 1841–1850

```
/*
 * Step-roll tick table corrected to 96 PPQ (S081 §5.4, R17, Q11).
 *
 * What: the table values are in 96-PPQ ticks (the TIM3 tick rate).
 * Previously the values were 24-PPQ, making every roll 4× too fast on
 * hardware. New table: {0, 48, 32, 24, 16, 12, 8, 6, 4, 3} for
 * indices 0..9 (off, 1/8, 8T, 16, 16T, 32, 32T, 64, 64T, 128).
 * 48 (was index 7) is dropped (same rate as 32T, F-A). 64T and 128 are
 * added. Guard changed from rate > 8 to rate > 9. Why: the original
 * table was written for a 24-PPQ sequencer; the port to 96-PPQ did not
 * scale the divisors. Every existing stored roll slows to its labelled
 * rate. Stored indices shift with no migration (F-H). Inputs: rate
 * index 0..9. Outputs: ticks between roll hits. Affiliates:
 * seq_processRollTicks, seq_rollBegin, stepRollRateNames (MenuText.h),
 * menu_getRollRateName, STEP roll clamp.
 */
static uint8_t seq_rollPpq(uint8_t rate)
{
    static const uint8_t table[10] = {
        0u, 48u, 32u, 24u, 16u, 12u, 8u, 6u, 4u, 3u
    };
    if (rate > 9u)
        return 0u;
    return table[rate];
}
```

### A3-2. Roll name table and clamp updates

**File:** `Core/Menu/MenuText.h`

#### A3-2a. stepRollRateNames — MODIFY at line 266

```
/*
 * Step-roll rate display names, aligned with the §5.4 table (S081 R17).
 *
 * What: 10 entries plus the count byte. Drop " 48" (was between 32T
 * and  64). Add "64t" and "128". Why: the table indices and names
 * must match the new 10-entry seq_rollPpq table. Stored bytes shift
 * with no migration. Inputs: roll index 0..9. Affiliates:
 * seq_rollPpq, menu_getRollRateName, STEP roll clamp.
 */
```

New value: `{10}, {"off"},{"1/8"},{"8tr"},{" 16"},{"16t"},{" 32"},{"32t"},{" 64"},{"64t"},{"128"}`

#### A3-2b. quantisationNames — MODIFY at line 57

```
/*
 * Quantisation display names with trk option (S081 §5.6, R16).
 *
 * What: 6 entries. Adds "trk" after "64". Why: R16 adds track-grid
 * quantisation for recording and looper per-track loop starts. Inputs:
 * quantisation index 0..5. Affiliates: seq_quantisation,
 * QUANT_TRACK enum, seq_setQuantisation clamp, seq_loopEventAnchor.
 */
```

New value: `{6}, {"off"}, {"8"}, {"16"}, {"32"}, {"64"}, {"trk"}`

**File:** `Core/Menu/menu.c`

#### A3-2c. STEP roll clamp — MODIFY at line 5788 (approx `5790`)

Change `*value > 8u` to `*value > 9u`.

```
/*
 * STEP roll clamp extended for 64T and 128 (S081 §5.4).
 *
 * What: the upper bound changes from 8 to 9 to allow the two new roll
 * rates. Inputs: value from encoder. Outputs: clamped to 0..9.
 * Affiliates: seq_rollPpq table, stepRollRateNames.
 */
```

#### A3-2d. menu_getRollRateName — MODIFY at line 9974 (approx `9978`)

Change `value > 8u` guard to `value > 9u`, and `stepRollRateNames[value + 1u]`
indexing stays the same.

### A3-3. Quantisation: QUANT_TRACK

**File:** `Core/Sequencer/sequencer.h`

#### A3-3a. Enum — MODIFY at line 16

```
/*
 * QUANT_TRACK: track-grid quantisation (S081 §5.6, R16).
 *
 * What: adds QUANT_TRACK = 5 after QUANT_64. Recording snaps to the
 * nearest step start of the recorded track's own grid. The looper uses
 * it for per-track loop starts (each looped track's loop start is its
 * own current step start). Why: the existing quantisation options are
 * all master-clock divisions; trk provides a track-relative snap for
 * tracks running at non-standard scales. Affiliates: seq_quantisation,
 * seq_setQuantisation, seq_loopEventAnchor, quantisationNames.
 */
QUANT_TRACK,
```

**File:** `Core/Sequencer/sequencer.c`

#### A3-3b. seq_setQuantisation — MODIFY at lines 4042–4045

Clamp `value` to 0..5 (was implicit no clamp).

```
/*
 * Clamp quantisation to the valid range 0..QUANT_TRACK (S081 §5.6).
 *
 * What: rejects values > 5. Why: the enum now has 6 entries (0..5).
 * An out-of-range value from a corrupt file or MIDI could index past
 * the quantisationNames array. Inputs: value 0..5. Outputs:
 * seq_quantisation set. Affiliates: filesystem.c quantisation parse.
 */
```

#### A3-3c. seq_loopEventAnchor — MODIFY at lines 3079–3112

Add `QUANT_TRACK` case in the switch: for each track in the mask, use that
track's own step start (`now − accumulator/256`).

```
/*
 * Per-track loop anchor for QUANT_TRACK (S081 §5.6, F9).
 *
 * What: when seq_quantisation == QUANT_TRACK, the loop anchor for each
 * track is that track's own last step start: now minus its fractional
 * accumulator. This replaces the existing fastest-track heuristic for
 * the NO_QUANTISATION case. Why: the looper with trk quantisation
 * should anchor each track's loop to its own grid, not to the fastest
 * grid in the mask. Inputs: mask of tracks to anchor. Outputs: per-
 * track anchor tick. Affiliates: seq_trackAccumulator,
 * seq_effectiveTrackScale, seq_loopEngage.
 */
```

### A3-4. Overlay defect fixes (R19)

**File:** `Core/Hardware/frontPanel/buttonHandler.c`

#### A3-4a. BAR LED restore on mode exit — MODIFY at line ~1531

```
/*
 * BAR overlay LEDs shown only in PERF mode (S081 §5.7, R19).
 *
 * What: leaving PERF clears both BAR LEDs (LED_BARLT, LED_BARRT).
 * Entering PERF restores them from bh_overlayActive. The overlay state
 * (bh_overlayActive, bh_overlayType) is kept for re-entry. Why: the
 * toggled BAR LED previously hung in other modes, showing stale
 * overlay state in VOICE/STEP/FX pages. Inputs: mode transition
 * events in handleModeButtons(). Outputs: LED state matches the mode.
 * Affiliates: bh_overlayActive, led_setLed for BAR LEDs,
 * buttonHandler_overlayTrigger.
 */
```

#### A3-4b. Overlay switch while held — MODIFY at lines ~2453/2490

```
/*
 * BAR overlay switch while a VOICE trigger is held (S081 §5.7, R19, F-B).
 *
 * What: pressing the other BAR button while a VOICE is held for an
 * overlay trigger switches the overlay type and reconfigures the held
 * track. Transitions: single→roll starts repeating at the new rate;
 * roll→single/flam stops repeating (no extra hit); single→single does
 * nothing. Why: previously the type switched but the held trigger's
 * behaviour did not update (the old type's timing continued). Now the
 * held trigger adopts the new type immediately. Inputs:
 * bh_overlayVoiceMask, new overlay type. Outputs: held trigger
 * reconfigured; liveRec_rollRateChange() called if recording (Phase E).
 * Affiliates: buttonHandler_overlayTrigger, buttonHandler_overlayTick,
 * buttonHandler_overlayRelease.
 */
```

### A3-5. Legacy roll retirement

**File:** `Core/Sequencer/sequencer.c`

#### A3-5a. Remove seq_rollRate, seq_rollState globals — REMOVE at lines 170–171

```
/*
 * Removed: legacy roll state (S081 §3.2).
 *
 * What: seq_rollRate and seq_rollState were the PAR_ROLL-driven
 * performance roll mechanism from the pre-S079 era. They have no
 * remaining callers. Removing them releases 2 B SRAM1. Affiliates:
 * seq_setRoll (removed), seq_setRollRate (removed), the legacy roll
 * block in seq_advanceTrackStep (removed).
 */
```

#### A3-5b. Remove legacy roll block in seq_advanceTrackStep — REMOVE at lines 2310–2317

#### A3-5c. Remove seq_setRoll — REMOVE at lines 4169–4184

#### A3-5d. Remove seq_setRollRate — REMOVE at lines 4186–4263

#### A3-5e. PAR_ROLL retirement in menu.c — MODIFY

- `menuPages.h:159`: remove PAR_ROLL from the Euklid cell.
- `menu.c:16077`: remove the parse case for PAR_ROLL.
- `menu.c:16784`: remove the default-value entry.
- `menu.c:1101`: remove the dtype entry.
- `MenuText.h:101`: remove `rollRateNames` and `MENU_ROLL_RATES` if no
  other user.

```
/*
 * PAR_ROLL retired in place (S081 §3.2, Q14).
 *
 * What: PAR_ROLL was the performance-roll parameter bound to the legacy
 * seq_setRoll/seq_setRollRate mechanism. It is removed from the Euklid
 * menu cell, the parse/default/dtype tables, and the name table. The
 * parameter ID is not reused. Why: the performance roll is superseded
 * by the overlay trigger system (S079) and step-roll recording (S081).
 * Affiliates: seq_setRoll (removed), seq_setRollRate (removed),
 * rollRateNames (removed).
 */
```

**File:** `Core/Sequencer/sequencer.h`

#### A3-5f. Remove declarations — REMOVE

Remove `extern` declarations for `seq_rollRate`, `seq_rollState`, and function
declarations for `seq_setRoll`, `seq_setRollRate`.

---

## Phase B — LiveRecord Skeleton, REC Gestures, Gates, Removals

### B-1. LiveRecord module: new files

**File:** `Core/Sequencer/LiveRecord.c` — ADD (new file)

```
/*
 * LiveRecord — live recording and erasing engine (S081 §4).
 *
 * What: captures overlay triggers, MIDI notes and knob automation into
 * Pattern steps on the recording track (rtk). Manages mode (OFF/LATCH/
 * OVERDUB/ERASE), target-Scene negotiation, latch/dirty/erase bitmaps,
 * placement and quantisation, flam pairing, roll span tracking, and
 * FX-sequence erase fan-out. Why: the old recording path
 * (seq_recordTrigger, seq_quantize) was a trigger-bit-only bridge from
 * the fixed-grid era with no note, velocity, timing, roll or automation
 * recording. This module implements the full live-record feature set.
 * Inputs: liveRec_triggerIn (triggers), liveRec_paramIn (automation
 * gestures), liveRec_erase* (erase gestures), mode setters (from
 * buttonHandler). Outputs: patSvc_recordStep, patSvc_writeStepAutomation,
 * patSvc_removeStepAutomation, seq_suppressStepOnce, seq_setRecordTarget,
 * seq_setAutomationFilter, effects_clearSeqLanes. Does not call: Menu,
 * buttonHandler, MidiParser. The Sequencer does not call LiveRecord.
 * RAM: ~270 B SRAM1 (bitmaps 192, state ~16, roll capture ~8,
 * flam grace ~4, staging via patSvc ~34, drops ~16). Service: 500 Hz
 * foreground pass (liveRec_service) before patSvc_tick().
 */
```

**File:** `Core/Sequencer/LiveRecord.h` — ADD (new file)

Public API per §4.2 — all declarations with comment blocks:

```c
void    liveRec_init(uint8_t activeTrack);
void    liveRec_service(void);

/* Mode */
void    liveRec_setLatch(uint8_t on);
void    liveRec_overdubBegin(void);
void    liveRec_overdubEnd(void);
void    liveRec_forceOff(void);
void    liveRec_eraseBegin(void);
void    liveRec_eraseEnd(void);
uint8_t liveRec_mode(void);
uint8_t liveRec_armed(void);
uint8_t liveRec_patternWriting(void);

/* Recording track */
void    liveRec_onActiveTrackChanged(uint8_t track);
void    liveRec_setRecordTrack(uint8_t track);
uint8_t liveRec_recordTrack(void);
uint8_t liveRec_activePatternLocked(void);

/* Triggers */
void    liveRec_triggerIn(const liveRec_trigger_t *t);
void    liveRec_rollRateChange(uint8_t track, uint8_t rollIndex);
void    liveRec_rollRelease(uint8_t track);

/* Automation */
void    liveRec_paramIn(uint16_t target9, uint8_t source);

/* Erase */
void    liveRec_eraseParam(uint16_t target9, uint8_t fxLane);
void    liveRec_eraseSeqHeld(uint8_t held);
void    liveRec_eraseTrackHeld(uint8_t track, uint8_t held);
```

Each function carries the full comment-block description from §4.2 and §19.2.
The `liveRec_trigger_t` struct is also declared here.

### B-2. REC button handler rewrite

**File:** `Core/Hardware/frontPanel/buttonHandler.c`

#### B-2a. REC press handler — MODIFY at lines 2377–2408

```
/*
 * REC press: start hold timer for overdub detection (S081 §6, §19.7).
 *
 * What: on REC press, if SHIFT is held → open the settings menu (unchanged);
 * if the settings menu is open → exit it (unchanged); if looping or
 * erase is enabled → ignore; otherwise start the hold timer
 * (bh_recPressTick = current tick, bh_recArmed = 1). The press no
 * longer toggles recording. Why: the toggle moves to release, and a
 * hold past DOUBLE_CLICK_TIMEOUT enters overdub. This matches the fill
 * hold pattern (bh_fillArmed/bh_fillPressTick). Inputs: REC press
 * event. Outputs: hold timer started or menu toggled. Affiliates:
 * buttonHandler_tick (overdub threshold), DOUBLE_CLICK_TIMEOUT,
 * liveRec_setLatch/liveRec_overdubBegin.
 */
```

#### B-2b. New state variables — ADD after line ~155

```
/*
 * REC hold timer state (S081 §6).
 *
 * What: bh_recPressTick records the tick of the REC press; bh_recArmed
 * is 1 while waiting for the hold threshold. Why: overdub requires a
 * hold past DOUBLE_CLICK_TIMEOUT, identical to the fill-hold pattern.
 * Inputs: set on REC press, cleared on release or threshold. Outputs:
 * read by buttonHandler_tick. RAM: +5 B. Affiliates: bh_fillArmed/
 * bh_fillPressTick.
 */
static uint32_t bh_recPressTick;
static uint8_t  bh_recArmed;
```

#### B-2c. buttonHandler_tick: overdub threshold — MODIFY

```
/*
 * REC hold past DOUBLE_CLICK_TIMEOUT enters overdub (S081 §6).
 *
 * What: if bh_recArmed and the current tick minus bh_recPressTick
 * exceeds DOUBLE_CLICK_TIMEOUT, call liveRec_overdubBegin() and
 * led_pulseLed(LED_REC). Clear bh_recArmed. Why: overdub is a held
 * gesture, not a toggle. The hold delay matches the fill delay for
 * consistency. Inputs: bh_recPressTick, current tick. Outputs:
 * overdub mode entered, REC LED pulsed. Affiliates:
 * liveRec_overdubBegin, led_pulseLed.
 */
```

#### B-2d. REC release handler — MODIFY

```
/*
 * REC release: toggle latch or end overdub (S081 §6).
 *
 * What: if bh_recArmed (short press): toggle liveRec_setLatch(), update
 * REC LED. If an overdub was in progress (threshold passed): call
 * liveRec_overdubEnd(), led_pulseLed(LED_REC), restore prior state.
 * Clear bh_recArmed. Why: the toggle moves to release so the hold can
 * be detected first. A short press toggles latch mode; a long press
 * was overdub and exits it on release. Inputs: REC release event.
 * Outputs: mode changed, LED updated. Affiliates: liveRec_setLatch,
 * liveRec_overdubEnd, led_pulseLed.
 */
```

#### B-2e. Remove bh_state.seqRecording and bh_state.seqErasing — MODIFY at line ~155

```
/*
 * Removed: bh_state.seqRecording and bh_state.seqErasing bits (S081 §19.7).
 *
 * What: these bitfield members tracked the local button handler's view
 * of recording and erasing state. They are replaced by queries to
 * liveRec_mode() and liveRec_armed(). Why: the recording state is now
 * owned by LiveRecord, not by the button handler or the Sequencer.
 * Affiliates: liveRec_mode, liveRec_armed.
 */
```

### B-3. Erase chord handler

**File:** `Core/Hardware/frontPanel/buttonHandler.c`

#### B-3a. SHIFT+CLEAR chord — MODIFY at lines 2421–2428 (erase enter), 2615–2624 and 2812–2817 (erase exit)

```
/*
 * Live erase entry via SHIFT+CLEAR chord (S081 §10, §19.7).
 *
 * What: while liveRec_armed(), pressing SHIFT+COPY (in either order)
 * calls liveRec_eraseBegin() and lights both SHIFT and CLEAR LEDs.
 * copyClear_copyPressed() is NOT called (the gesture is consumed by
 * erase). Releasing one key keeps erase enabled; releasing both calls
 * liveRec_eraseEnd() and restores the LEDs. Why: live erase requires
 * a SHIFT+CLEAR hold, which must not also trigger the copy/clear
 * function. Both keys tracked via bh_shiftHeld and bh_clearHeld.
 * Inputs: SHIFT and COPY/CLEAR press/release events. Outputs: erase
 * mode entered/exited. Affiliates: liveRec_eraseBegin/End,
 * copyClear_copyPressed (bypassed while armed).
 */
```

### B-4. Looper record-off move (§3.3-9)

**File:** `Core/Hardware/frontPanel/buttonHandler.c`

#### B-4a. Before seq_loopEngage calls — MODIFY at line ~1840

```
/*
 * Record forced off before looper engage (S081 §3.3-9, §19.7).
 *
 * What: before every seq_loopEngage() call, call liveRec_forceOff()
 * and turn off the REC LED. The old seq_setRecordingMode(0) call
 * inside seq_loopEngage() (sequencer.c:3214) is removed. Why: the
 * Sequencer must not call back into LiveRecord (dependency direction).
 * Moving the record-off to the caller keeps the dependency one-way:
 * buttonHandler → LiveRecord → Sequencer. Inputs: looper engage
 * request. Outputs: recording stopped, REC LED off. Affiliates:
 * liveRec_forceOff, seq_loopEngage.
 */
```

**File:** `Core/Sequencer/sequencer.c`

#### B-4b. Remove record-off from seq_loopEngage — MODIFY at line 3214

Remove `if (seq_recordActive) seq_setRecordingMode(0u);` from
`seq_loopEngage()`.

### B-5. Recording track (rtk) and RECORDING_PAGE

**File:** `Core/Menu/menu.c`

#### B-5a. menu_setActiveVoice: rtk follow — MODIFY at line ~16362

```
/*
 * Active-track change updates rtk (S081 §11, §19.6).
 *
 * What: inside the != branch of menu_setActiveVoice(), calls
 * liveRec_onActiveTrackChanged(v). Why: R7 says rtk follows every
 * active-track change. Re-selecting the same track does not reset
 * an rtk override. Each change is a new take (latches reset).
 * Inputs: new active track v. Outputs: rtk updated in LiveRecord.
 * Affiliates: liveRec_onActiveTrackChanged, liveRec_setRecordTrack.
 */
```

**File:** `Core/Menu/menuPages.h`

#### B-5b. RECORDING_PAGE sub-page 2 — MODIFY at lines 185–214

```
/*
 * rtk cell on RECORDING_PAGE sub-page 2 (S081 §11, §19.6).
 *
 * What: adds PAR_AUTOM_TRACK to sub-page 2, cell 0. The dtype changes
 * from DTYPE_0b1 to a 0..6 value shown as 1..7 (displayed with a
 * static-param name special case). The cell text is "RecTrk". Why: the
 * recording track must be user-editable from the SHIFT+REC menu.
 * Commit → liveRec_setRecordTrack(); display mirrors
 * liveRec_recordTrack(). Inputs: encoder input. Outputs: rtk changed,
 * new take started. Affiliates: PAR_AUTOM_TRACK (ParameterArray.h:103),
 * liveRec_setRecordTrack.
 */
```

### B-6. Gate moves

**File:** `Core/Menu/CopyClear/copyClearSession.c`

#### B-6a. copyClear_copyPressed refusal — MODIFY at line ~584

```
/*
 * Copy refusal uses liveRec_armed (S081 §19.10, F-N).
 *
 * What: the guard `seq_recordActive || seq_eraseActive` becomes
 * `liveRec_armed()`. Why: the recording/erasing state is now owned by
 * LiveRecord; seq_eraseActive is removed. COPY stays refused while
 * armed (including while stopped), by decision F-N. Inputs:
 * liveRec_armed(). Outputs: copy refused. Affiliates: liveRec_armed.
 */
```

**File:** `Core/Hardware/SD/filesystem.c`

#### B-6b. AutoSave gates — MODIFY at lines 24704 and 25066

```
/*
 * Pattern AutoSave gates use liveRec_patternWriting (S081 §19.11, F-M).
 *
 * What: the gates `seq_recordActive || seq_eraseActive` become
 * `liveRec_patternWriting()`. Why: the AutoSave drain must not copy the
 * live Pattern region while LiveRecord can still modify it.
 * liveRec_patternWriting() returns true when armed AND the transport is
 * running, which is when writes actually happen. While armed and
 * stopped, AutoSave can run safely. Inputs: liveRec_patternWriting().
 * Outputs: AutoSave deferred or allowed. Affiliates:
 * liveRec_patternWriting, seq_recordActive (stays as the published
 * writing flag, written by LiveRecord).
 */
```

### B-7. Sequencer removals

**File:** `Core/Sequencer/sequencer.c`

#### B-7a. Remove seq_recordTrigger — REMOVE at lines 4308–4349

```
/*
 * Removed: seq_recordTrigger (S081 §3.2).
 *
 * What: this function recorded a trigger-only bit onto bar 1 using the
 * legacy 128-sub-step quantizer. It is replaced by LiveRecord's
 * trigger capture (liveRec_triggerIn) with full note, velocity, timing,
 * roll and automation recording. Affiliates: liveRec_triggerIn,
 * pat_recordStep (Phase D).
 */
```

#### B-7b. Remove seq_quantize — REMOVE at lines 4268–4305

```
/*
 * Removed: seq_quantize (S081 §3.2).
 *
 * What: the legacy quantizer using the retired 128-sub-step bar. It is
 * replaced by seq_quantiseTick (Phase C) which operates on the master
 * PPQ clock. Affiliates: seq_quantiseTick, liveRec placement (§8.3).
 */
```

#### B-7c. Remove seq_setRecordingMode — REMOVE at lines 4352–4367

```
/*
 * Removed: seq_setRecordingMode (S081 §3.2).
 *
 * What: the old mode setter that guarded against per-track-active.
 * LiveRecord writes seq_recordActive directly through
 * seq_setRecordTarget(). Affiliates: liveRec_setLatch,
 * seq_setRecordTarget.
 */
```

#### B-7d. Remove seq_setErasingMode — REMOVE at lines 4369–4372

#### B-7e. Remove seq_eraseActive — REMOVE at line 675

```
/*
 * Removed: seq_eraseActive (S081 §3.2).
 *
 * What: the global erase flag. Replaced by seq_eraseSpecialsMask and
 * seq_eraseFullMask set through seq_setEraseMasks(). The erase logic in
 * seq_advanceTrackStep now uses these per-track masks instead. Affiliates:
 * seq_setEraseMasks, seq_advanceTrackStep erase branches.
 */
```

**File:** `Core/Sequencer/sequencer.h`

#### B-7f. Remove header declarations — REMOVE

Remove `extern seq_eraseActive`, declarations for `seq_setRecordingMode`,
`seq_setErasingMode`, `seq_recordTrigger`, `seq_setRoll`, `seq_setRollRate`,
`extern seq_rollRate`, `extern seq_rollState`.

### B-8. LED handler cleanup

**File:** `Core/Hardware/frontPanel/ledHandler.c`

#### B-8a. Remove SEQ_LED_DIRTY_REC_SUB — REMOVE at lines 1391–1404

```
/*
 * Removed: SEQ_LED_DIRTY_REC_SUB endpoint (S081 §19.8).
 *
 * What: the led_updateRecordedSubStep handler and the recordSubStep
 * field in SeqLedState. This endpoint had no writer (led_updateRecordedSubStep
 * was a compatibility no-op). The absolute-step recordMainStep stays and is
 * updated by LiveRecord. Affiliates: SEQ_LED_DIRTY_REC_MAIN (kept).
 */
```

**File:** `Core/Hardware/frontPanel/ledHandler.h`

#### B-8b. Remove SEQ_LED_DIRTY_REC_SUB define — REMOVE at line 215

Remove `#define SEQ_LED_DIRTY_REC_SUB` and the `recordSubStep` field from
`SeqLedState`.

### B-9. Init and service wiring

**File:** `Core/Hardware/timebase.c`

#### B-9a. liveRec_service call — ADD before line 201

```
/*
 * LiveRecord service runs before patSvc_tick (S081 §19.12).
 *
 * What: calls liveRec_service() immediately before patSvc_tick() in the
 * 500 Hz foreground pass. Why: LiveRecord's step-end writes and erase
 * ops must be submitted to the Pattern Stack Service before
 * patSvc_tick() drains the queue, so that idle-direct execution can
 * process them in the same pass. Inputs: none. Outputs: LiveRecord
 * state serviced, patSvc ops submitted. Affiliates: patSvc_tick,
 * liveRec_service.
 */
liveRec_service();
```

**File:** `main.c`

#### B-9b. liveRec_init call — ADD after line ~1299

```
/*
 * LiveRecord init at boot (S081 §19.12).
 *
 * What: calls liveRec_init(menu_getActiveVoice()) after copyClear_init().
 * Why: initialises rtk to the boot-time active track, clears all
 * bitmaps and state. Must run after the menu and Pattern systems are
 * initialised. Inputs: initial active track. Outputs: LiveRecord ready.
 * Affiliates: liveRec_init, menu_getActiveVoice, copyClear_init.
 */
liveRec_init(menu_getActiveVoice());
```

---

## Phase C — Sequencer Services

Capture, projection, quantiser, ring, echo/live-hold, µs stamp, epoch,
filter registration. These are the Sequencer-side APIs that LiveRecord calls
(LiveRecord → Sequencer direction).

### C-1. Step-boundary ring

**File:** `Core/Sequencer/sequencer.c`

#### C-1a. Ring state — ADD

```
/*
 * Step-boundary ring for LiveRecord (S081 §19.1 item 1).
 *
 * What: a 16-entry ring of uint16_t. Each entry packs: step (7 bits),
 * track (3 bits), fired (1 bit), plus 5 spare. TIM3 writes (producer);
 * LiveRecord drains (consumer) via seq_takeRecBoundary(). Why: LiveRecord
 * runs in the foreground and cannot read TIM3 state directly. The ring
 * communicates step boundaries so LiveRecord can perform its step-end
 * table (write/clear/re-stamp) at each step of rtk. A full ring drops
 * the entry and increments a drop counter. Inputs: written by
 * seq_advanceTrackStep when seq_recWriting is set. Outputs: read by
 * liveRec_service → seq_takeRecBoundary. RAM: +34 B SRAM1 (16 × 2 B
 * entries + producer byte + consumer byte). Affiliates:
 * seq_advanceTrackStep, liveRec_service.
 */
static uint16_t seq_recRing[16];
static uint8_t  seq_recRingHead;
static uint8_t  seq_recRingTail;
```

#### C-1b. seq_takeRecBoundary — ADD

```
/*
 * Pop one step-boundary entry from the ring (S081 §19.1).
 *
 * What: if the ring is non-empty, copies the tail entry into the output
 * struct and advances the tail. Returns 1 on success, 0 if empty.
 * Why: single-producer (TIM3) single-consumer (foreground) with byte
 * head/tail needs no PRIMASK. LiveRecord calls this in its service loop.
 * Inputs: output pointer. Outputs: step, track, fired. Affiliates:
 * liveRec_service, seq_advanceTrackStep ring push.
 */
uint8_t seq_takeRecBoundary(seq_rec_boundary_t *out);
```

### C-2. Published record target

**File:** `Core/Sequencer/sequencer.c`

#### C-2a. Target state — ADD

```
/*
 * Published recording target (S081 §19.1 item 1).
 *
 * What: seq_recTrack (the track being recorded) and seq_recWriting
 * (nonzero when LiveRecord is armed and running). Written by LiveRecord
 * through seq_setRecordTarget(). TIM3 reads both to decide ring pushes,
 * echo suppression, and live-hold filtering. Why: TIM3 must know which
 * track is being recorded and whether recording is active, but cannot
 * call LiveRecord. These are single-byte stores, atomic on Cortex-M7.
 * Inputs: set by seq_setRecordTarget. Outputs: read by TIM3 in
 * seq_advanceTrackStep. RAM: +2 B. Affiliates: liveRec_service,
 * seq_recRing, seq_echoStep, seq_liveHoldMask.
 */
static uint8_t seq_recTrack;
static uint8_t seq_recWriting;
```

#### C-2b. seq_setRecordTarget — ADD

```
void seq_setRecordTarget(uint8_t track, uint8_t writing);
```

Also updates `seq_recordActive = writing` so existing filesystem/copyClear
readers continue working.

### C-3. Echo suppression and live hold

**File:** `Core/Sequencer/sequencer.c`

#### C-3a. Echo state — ADD

```
/*
 * Echo-suppression step per track (S081 §8.6, §19.1 item 12).
 *
 * What: seq_echoStep[track] is the step index (0..127) of a recorded
 * trigger that is ahead of the playhead in the current pass, or −1 if
 * no suppression is pending. TIM3 reads it: when the playhead reaches
 * that step, the trigger is skipped once (fired bit still set for
 * automation and ring purposes). The mark expires at the next pass, a
 * Scene change, transport stop, or a length/run-mode change. Why:
 * without echo suppression, a trigger recorded ahead of the playhead
 * would play back within the same bar, causing a double hit. Inputs:
 * set by seq_suppressStepOnce() from LiveRecord. Cleared by TIM3 on
 * consumption. Outputs: trigger skipped once. RAM: +7 B.
 * Affiliates: seq_advanceTrackStep, liveRec capture (Phase E).
 */
static int8_t seq_echoStep[NUM_TRACKS];
```

#### C-3b. Live-hold mask — ADD

```
/*
 * Live-hold mask for overlay roll suppression (S081 §8.8, F-P, §19.1).
 *
 * What: bit N set means track N has an overlay trigger held and
 * recording is active. TIM3 skips stored triggers for that track while
 * the bit is set (automation still plays). Why: while the user holds a
 * VOICE for a recorded roll, the stored triggers on that track should
 * not double-fire alongside the live roll. Inputs: set/cleared by
 * seq_setLiveHold(track, on) from LiveRecord via buttonHandler.
 * Outputs: read by TIM3 in seq_advanceTrackStep trigger branch.
 * RAM: +1 B. Affiliates: buttonHandler overlay hold, liveRec_triggerIn.
 */
static uint8_t seq_liveHoldMask;
```

#### C-3c. API functions — ADD

```
void seq_suppressStepOnce(uint8_t track, uint8_t step);
void seq_setLiveHold(uint8_t track, uint8_t on);
void seq_cancelPendingTrigger(uint8_t track);
```

`seq_cancelPendingTrigger` clears `seq_trackShufflePending[track]`.

### C-4. Erase masks

**File:** `Core/Sequencer/sequencer.c`

#### C-4a. Erase mask state — ADD

```
/*
 * Per-track erase masks (S081 §10, §19.1 item 13).
 *
 * What: seq_eraseSpecialsMask (bit per track): clear bit 15 + specials,
 * keep automation. seq_eraseFullMask (bit per track): clear everything
 * including automation. Set by seq_setEraseMasks() from LiveRecord.
 * TIM3 reads them in seq_advanceTrackStep. Why: live erase has two
 * modes — SEQ hold erases triggers/specials (automation plays),
 * TRACK hold erases everything. The masks let TIM3 apply the right
 * erase without calling LiveRecord. Inputs: set by seq_setEraseMasks.
 * Outputs: read by seq_advanceTrackStep erase branches. RAM: +2 B.
 * Affiliates: liveRec_eraseSeqHeld, liveRec_eraseTrackHeld.
 */
static uint8_t seq_eraseSpecialsMask;
static uint8_t seq_eraseFullMask;
```

#### C-4b. seq_setEraseMasks — ADD

```
void seq_setEraseMasks(uint8_t specials, uint8_t full);
```

### C-5. PPQ microsecond stamp

**File:** `Core/Sequencer/sequencer.c`

#### C-5a. Stamp state — ADD

```
/*
 * PPQ tick microsecond stamp and period (S081 §19.1 item 11, Q3).
 *
 * What: seq_ppqStampUs is the timebase_tim2Now() value at the head of
 * seq_processSchedulerTick(). seq_ppqPeriodUs is the interval since
 * the previous tick. Why: LiveRecord needs sub-tick timing for
 * placement: the trigger's µs timestamp minus seq_ppqStampUs gives the
 * fraction of the current tick. Inputs: written by
 * seq_processSchedulerTick at each tick. Outputs: read by
 * seq_captureTrackTime(). RAM: +8 B. Affiliates:
 * timebase_tim2Now, liveRec placement (§8.3).
 */
static uint32_t seq_ppqStampUs;
static uint32_t seq_ppqPeriodUs;
```

#### C-5b. Write in seq_processSchedulerTick — MODIFY at line ~3789

Add at the head of the function body:
```c
{
    uint32_t now = timebase_tim2Now();
    seq_ppqPeriodUs = now - seq_ppqStampUs;
    seq_ppqStampUs = now;
}
```

### C-6. Playback epoch

**File:** `Core/Sequencer/sequencer.c`

#### C-6a. Epoch counter — ADD

```
/*
 * Playback epoch counter (S081 §19.1 item 6).
 *
 * What: uint8_t seq_playbackEpoch, incremented by every Scene change,
 * realign, transport start/stop, and pattern load. LiveRecord polls it
 * in liveRec_service() to detect take boundaries. Why: LiveRecord must
 * reset latches, discard pending work and re-evaluate the target Scene
 * whenever the playback context changes. A single counter is simpler
 * than tracking each event type individually. Wraps at 255. Inputs:
 * incremented at the sites listed in A1-4e plus seq_setRunning.
 * Outputs: read by liveRec_service. RAM: +1 B. Affiliates:
 * liveRec_service take boundary check, seq_setStepIndexToStart,
 * seq_selectActivePattern, seq_alignActivePatternToScene.
 */
static uint8_t seq_playbackEpoch;
```

Add `seq_playbackEpoch++` at the same sites that clear `seq_menuOverride` (A1-4e) plus `seq_setRunning()`.

#### C-6b. seq_getPlaybackEpoch — ADD

```
uint8_t seq_getPlaybackEpoch(void);
```

### C-7. Automation filter registration

**File:** `Core/Sequencer/sequencer.c`

#### C-7a. Filter state — ADD

```
/*
 * Automation drain filter registration (S081 §19.1 item 2).
 *
 * What: pointers to the LiveRecord latch and erase bitmaps, plus the
 * erase track. seq_drainPendingAutomation reads these to skip entries
 * whose targets are latched or being erased. Why: the Sequencer cannot
 * call LiveRecord (dependency direction). Instead, LiveRecord registers
 * its bitmaps and the drain reads them directly. When LiveRecord is
 * off, the pointers are NULL and the drain runs unfiltered. Inputs:
 * set by seq_setAutomationFilter() from LiveRecord. Outputs: read by
 * seq_drainPendingAutomation. RAM: +9 B (2 pointers + 1 byte).
 * Affiliates: liveRec_service, seq_drainPendingAutomation.
 */
static const uint8_t *seq_filterLatch;
static const uint8_t *seq_filterErase;
static uint8_t        seq_filterEraseTrack;
```

#### C-7b. seq_setAutomationFilter — ADD

```
void seq_setAutomationFilter(const uint8_t *latch, const uint8_t *erase,
                              uint8_t eraseTrack);
```

#### C-7c. seq_drainPendingAutomation filter additions — MODIFY at lines 2442–2551

Add to the entry processing, after the target is decoded:

```
/*
 * Filter entries against registered latch and erase bitmaps (S081 §19.1).
 *
 * What: for each pending entry, check if the target's bit is set in
 * seq_filterLatch (any owner track: skip if latched, because LiveRecord
 * writes its own value at step end) or in seq_filterErase with
 * owner_track == seq_filterEraseTrack (skip if that track's automation
 * is being erased). For Instrument parameters, also check
 * seq_menuOverride (A1-4d). Why: latched parameters must not have
 * their stored automation played back — the menu value is what sounds.
 * Erased parameters must not play back either — their entries are being
 * removed. Inputs: seq_filterLatch, seq_filterErase, target bits.
 * Outputs: entry skipped or applied. Affiliates: liveRec latch bitmap,
 * liveRec erase bitmap, seq_menuOverride.
 */
```

### C-8. Capture and projection helpers

**File:** `Core/Sequencer/sequencer.c`

#### C-8a. seq_captureTrackTime — ADD

```
/*
 * PRIMASK snapshot of a track's timing state (S081 §19.1 item 11).
 *
 * What: captures under PRIMASK: seq_elapsedPpqTicks, seq_ppqStampUs,
 * seq_ppqPeriodUs, seq_stepIndex[track], seq_trackAccumulator[track],
 * effective interval/length/shuffle, play mode/state, played Scene.
 * Returns them in a seq_track_time_t struct. Why: LiveRecord runs in
 * the foreground and needs a consistent snapshot of TIM3-mutated state
 * for placement math. The PRIMASK read is a handful of byte/word copies,
 * fast enough for the ISR blackout. Inputs: track 0..6. Outputs:
 * filled struct. Affiliates: liveRec placement (§8.3),
 * seq_projectStep.
 */
void seq_captureTrackTime(uint8_t track, seq_track_time_t *out);
```

#### C-8b. seq_projectStep — ADD

```
/*
 * Project a master-clock tick to a track's step and offset (S081 §19.1).
 *
 * What: given a master tick, computes which step of the track would be
 * playing and the Q8 offset within that step. Built on the same math as
 * seq_loopCanonicalPreAdvance (line 3036). Returns "unknown" for future
 * ticks in rnd run mode. Why: LiveRecord needs to convert a captured
 * trigger's master-clock time to a step index for placement. Inputs:
 * track, tick. Outputs: step index, offsetQ8, validity flag.
 * Affiliates: seq_loopCanonicalPreAdvance, liveRec placement.
 */
uint8_t seq_projectStep(uint8_t track, uint32_t tick,
                         uint8_t *step, uint16_t *offsetQ8);
```

#### C-8c. seq_quantiseTick — ADD

```
/*
 * Quantise a master-clock tick to the nearest boundary (S081 §19.1 item 10).
 *
 * What: for 1/8..1/64, snaps the tick to the nearest master-clock grid
 * (48/24/12/6 PPQ). For QUANT_TRACK, snaps to the nearest step start of
 * the given track's own grid. For NO_QUANTISATION, returns the input.
 * Why: LiveRecord uses this to quantise captured triggers before
 * placement. The looper's seq_loopEventAnchor already does a similar
 * snap for the master clock; this generalises it for per-track grids.
 * Inputs: track, tick. Outputs: quantised tick. Affiliates:
 * seq_loopEventAnchor, seq_quantisation, QUANT_TRACK.
 */
uint32_t seq_quantiseTick(uint8_t track, uint32_t tick);
```

**File:** `Core/Sequencer/sequencer.h`

#### C-8d. Header declarations and types — ADD

```
typedef struct {
    uint32_t elapsedTick;
    uint32_t stampUs;
    uint32_t periodUs;
    int16_t  stepIndex;
    uint16_t accumulator;
    uint16_t interval;
    uint8_t  length;
    uint8_t  shuffle;
    uint8_t  playMode;
    uint8_t  playState;
    uint8_t  playedScene;
} seq_track_time_t;

typedef struct {
    uint8_t step;
    uint8_t track;
    uint8_t fired;
} seq_rec_boundary_t;

void    seq_setRecordTarget(uint8_t track, uint8_t writing);
void    seq_setAutomationFilter(const uint8_t *latch, const uint8_t *erase,
                                 uint8_t eraseTrack);
void    seq_setEraseMasks(uint8_t specials, uint8_t full);
void    seq_suppressStepOnce(uint8_t track, uint8_t step);
void    seq_setLiveHold(uint8_t track, uint8_t on);
void    seq_cancelPendingTrigger(uint8_t track);
uint8_t seq_takeRecBoundary(seq_rec_boundary_t *out);
void    seq_captureTrackTime(uint8_t track, seq_track_time_t *out);
uint8_t seq_projectStep(uint8_t track, uint32_t tick,
                         uint8_t *step, uint16_t *offsetQ8);
uint32_t seq_quantiseTick(uint8_t track, uint32_t tick);
uint8_t seq_getPlaybackEpoch(void);
void    seq_clearSceneAutomationBit(uint8_t index);
```

---

## Phase D — PatternData / PatternStackService

### D-1. pat_writeDynamic trigger policy

**File:** `Core/Bank/Scene/Pattern/PatternData.c`

#### D-1a. pat_writeDynamic — MODIFY at line ~640

```
/*
 * Trigger-policy argument for pat_writeDynamic (S081 §19.3).
 *
 * What: adds a trigger_policy parameter (PAT_RAW_TRIGGER_KEEP,
 * PAT_RAW_TRIGGER_OFF, PAT_RAW_TRIGGER_ON). Applied inside the
 * existing PRIMASK publish (line 741 and the detach path at 690):
 * KEEP preserves bit 15 as-is, ON sets it, OFF clears it. Why:
 * pat_recordStep needs to write specials/automation and set the trigger
 * in one atomic operation. The existing callers always want KEEP
 * (their trigger state is set separately). Inputs: trigger_policy enum
 * value. Outputs: bit 15 of the step address set, cleared, or kept.
 * Affiliates: all existing callers pass KEEP; pat_recordStep passes ON.
 */
```

### D-2. New pat_recordStep

**File:** `Core/Bank/Scene/Pattern/PatternData.c`

#### D-2a. pat_recordStep — ADD

```
/*
 * Write a recorded step's specials and trigger (S081 §8.4, §19.3).
 *
 * What: reads the existing specials and automation for the step, applies
 * the record spec (note, velocity, timing, roll, fill; clears rhythm;
 * keeps condition or sets fil), and writes the result with trigger ON.
 * If the result has no specials and no automation, it only sets bit 15
 * (pat_setStepActive). Last write wins within a pass; a grace never
 * replaces its own main. Why: the old seq_recordTrigger only set the
 * trigger bit. Live recording needs to write the full step data
 * atomically. Inputs: scene, track, step, const pat_record_spec_t*.
 * Outputs: step written. Affiliates: pat_writeDynamic (with trigger
 * ON), pat_readStepSpecials, pat_readStepAutomations, patSvc
 * PATSVC_OP_RECORD_STEP.
 */
void pat_recordStep(uint8_t scene, uint8_t track, uint8_t step,
                     const pat_record_spec_t *spec);
```

**File:** `Core/Bank/Scene/Pattern/PatternData.h`

#### D-2b. pat_record_spec_t — ADD

```
typedef struct {
    uint8_t note;
    uint8_t velocity;
    uint8_t timing;
    uint8_t roll;
    uint8_t fill;
    uint8_t setFil;
    uint8_t noteIsDefault;
} pat_record_spec_t;
```

### D-3. New pat_eraseSpecials

**File:** `Core/Bank/Scene/Pattern/PatternData.c`

#### D-3a. pat_eraseSpecials — ADD

```
/*
 * Erase a step's specials while keeping automation (S081 §10.2, §19.3).
 *
 * What: if automation is present, keeps the condition and removes all
 * other specials (note, velocity, timing, roll, rhythm, fill). If no
 * automation, releases the block entirely (trigger KEEP — TIM3 already
 * cleared bit 15). Why: SEQ-hold erase removes triggers and specials
 * but automation still plays. Keeping the condition on steps with
 * automation preserves the automation's playback gate. Inputs: scene,
 * track, step. Outputs: step block trimmed or released. Affiliates:
 * pat_writeDynamic (trigger KEEP), PATSVC_OP_ERASE_SPECIALS.
 */
void pat_eraseSpecials(uint8_t scene, uint8_t track, uint8_t step);
```

### D-4. PatternStackService: new operations

**File:** `Core/Bank/Scene/Pattern/PatternStackService.c`

#### D-4a. Staging table — ADD

```
/*
 * Record-step staging table (S081 §19.3).
 *
 * What: 4 slots of pat_record_spec_t plus a scene/track/step triplet.
 * The patSvc_recordStep() caller copies the spec into a free slot; the
 * queue entry's payload carries the slot index. The service execution
 * calls pat_recordStep and frees the slot. Why: the queue's 16-bit
 * payload cannot hold the full record spec. The staging table is owned
 * by the service (no LiveRecord dependency). A full table rejects and
 * counts. RAM: +34 B. Affiliates: patSvc_recordStep,
 * PATSVC_OP_RECORD_STEP.
 */
```

#### D-4b. PATSVC_OP_RECORD_STEP — ADD to patSvc_operation_t enum

```
/*
 * Record-step service operation (S081 §19.3).
 *
 * What: reads the staging slot from the payload, calls pat_recordStep
 * with the staged spec, then frees the slot. Chunk requirement: at
 * most the current block size plus new specials overhead. Why: record
 * writes go through the service to respect the admission/handover
 * protocol and to serialise with other Pattern mutations. Affiliates:
 * patSvc_recordStep (submit), pat_recordStep (execution).
 */
```

#### D-4c. PATSVC_OP_ERASE_SPECIALS — ADD to patSvc_operation_t enum

```
/*
 * Erase-specials service operation (S081 §19.3).
 *
 * What: calls pat_eraseSpecials for the given scene, track, step.
 * Chunk requirement: at most the current block size. Why: same service
 * serialisation as other Pattern mutations. Affiliates:
 * patSvc_enqueueEraseSpecials (TIM3-safe submit), pat_eraseSpecials.
 */
```

#### D-4d. patSvc_enqueueEraseSpecials — ADD

```
/*
 * TIM3-safe erase-specials enqueue (S081 §19.3).
 *
 * What: modelled on patSvc_enqueueErase(). Packs the scene/track/step
 * into the queue entry and enqueues PATSVC_OP_ERASE_SPECIALS. Why:
 * TIM3 calls this when a SEQ-hold erase reaches a step; it cannot call
 * pat_eraseSpecials directly (no pool mutation from TIM3). Inputs:
 * scene, track, step. Outputs: queue entry added. Affiliates:
 * seq_advanceTrackStep erase branch, patSvc_enqueueErase.
 */
void patSvc_enqueueEraseSpecials(uint8_t scene, uint8_t track, uint8_t step);
```

### D-5. Target override

**File:** `Core/Bank/Scene/Pattern/PatternStackService.c`

#### D-5a. Target request state — ADD

```
/*
 * Target override for recording into a non-active Scene (S081 §7, §19.3).
 *
 * What: service_target_request (0xFF = none) holds the requested
 * recording-target Scene. patSvc_desiredScene() returns the request if
 * set, otherwise seq_activePattern. This replaces the eight direct
 * seq_activePattern comparisons (§3.3-8) in patSvc_submit,
 * patSvc_tick, patSvc_idle, patSvc_finishSceneReplace,
 * patSvc_endExclusive, and the submit variants. Why: when rtk plays a
 * per-track Scene different from the active Scene, Pattern writes must
 * target that Scene. The override redirects all service writes to the
 * desired Scene. Inputs: set by patSvc_requestTarget(). Outputs: read
 * by patSvc_desiredScene(). RAM: +2 B. Affiliates: liveRec target
 * negotiation (Phase H), patSvc_targetOpen.
 */
static uint8_t service_target_request;
```

#### D-5b. patSvc_requestTarget / patSvc_releaseTarget / patSvc_targetOpen — ADD

```
void    patSvc_requestTarget(uint8_t scene);
void    patSvc_releaseTarget(void);
uint8_t patSvc_targetOpen(uint8_t scene);
```

#### D-5c. patSvc_desiredScene — ADD (static helper)

Replaces every `seq_activePattern` reference with `patSvc_desiredScene()`.

#### D-5d. patSvc_idle — MODIFY at line ~1609

```
/*
 * patSvc_idle returns false while a target override is held (S081 §19.3).
 *
 * What: adds `service_target_request == 0xFF` to the idle conditions.
 * Why: while a recording target is held, Pattern AutoSave must not
 * snapshot the overridden Scene mid-take. patSvc_idle() returning false
 * defers AutoSave. Affiliates: filesystem.c AutoSave drain check,
 * service_target_request.
 */
```

---

## Phase E — Trigger Recording

### E-1. liveRec_triggerIn implementation

**File:** `Core/Sequencer/LiveRecord.c`

```
/*
 * Trigger capture entry point (S081 §8, §19.2).
 *
 * What: called by the overlay trigger path (buttonHandler) and later by
 * MidiParser for each hit when it sounds. Takes the hit's kind (SINGLE,
 * FLAM_GRACE, FLAM_MAIN, ROLL_START), note, velocity, track, source,
 * and note_is_default flag. Performs:
 *   1. Gate: mode is LATCH or OVERDUB, transport running, not looping,
 *      ownership check (§7).
 *   2. Capture: seq_captureTrackTime + µs timestamp.
 *   3. Quantise: seq_quantiseTick.
 *   4. Placement (§8.3): project to step + delay.
 *   5. Flam pairing (§8.7): a grace is buffered; the main writes both
 *      if they land on different steps, or the main only if same step.
 *   6. Roll: start step written with rate + fill, spanned steps written
 *      with fill 100, last step gets release fill (§8.8).
 *   7. Write: patSvc_recordStep.
 *   8. Echo: seq_suppressStepOnce if ahead of playhead.
 *   9. LED: seq_ledState.recordMainStep + dirty flag.
 * Why: this is the core recording path, replacing seq_recordTrigger with
 * full note/velocity/timing/roll/automation support. Inputs:
 * liveRec_trigger_t pointer. Outputs: step written, echo mark set, LED
 * updated. Affiliates: buttonHandler_overlayTrigger (caller),
 * seq_captureTrackTime, seq_projectStep, seq_quantiseTick,
 * patSvc_recordStep, seq_suppressStepOnce.
 */
```

### E-2. Overlay trigger hooks

**File:** `Core/Hardware/frontPanel/buttonHandler.c`

#### E-2a. buttonHandler_overlayTrigger — MODIFY at lines ~871–957

After each `seq_triggerVoice()` call, add `liveRec_triggerIn()`:

```
/*
 * Overlay trigger reports to LiveRecord (S081 §19.7).
 *
 * What: after each seq_triggerVoice call in the overlay trigger path,
 * calls liveRec_triggerIn() with the appropriate kind (SINGLE for a
 * plain hit, FLAM_GRACE/FLAM_MAIN for flam, ROLL_START for the first
 * roll hit). Why: LiveRecord captures the trigger at the moment it
 * sounds. The overlay owns the timing (1 ms foreground timer for flam
 * main and roll repeats); LiveRecord only records. Inputs: voice, note,
 * velocity, kind. Outputs: trigger captured. Affiliates:
 * liveRec_triggerIn, buttonHandler_overlayTick.
 */
```

#### E-2b. buttonHandler_overlayTick — MODIFY

Roll repeat hits from the 1 ms timer: report `liveRec_triggerIn(kind = ROLL_HIT)`.

#### E-2c. buttonHandler_overlayRelease — MODIFY

```
/*
 * Overlay release reports roll end to LiveRecord (S081 §19.7).
 *
 * What: calls liveRec_rollRelease(track). Why: LiveRecord needs to
 * know when the roll ends to write the release fill on the last
 * spanned step. Inputs: track. Outputs: roll capture finalised.
 * Affiliates: liveRec_rollRelease.
 */
```

### E-3. MIDI note hook

**File:** `Core/MIDI/MidiParser.c`

#### E-3a. midiParser_noteOn — MODIFY at line 1550

```
/*
 * MIDI note record → liveRec_triggerIn (S081 §19.9).
 *
 * What: replaces seq_recordTrigger(voice) with liveRec_triggerIn()
 * using kind = SINGLE and source = MIDI. Why: MIDI notes record the
 * same way as overlay triggers (R2). The two-call-site rule is
 * maintained for MIDI separability (R11). Inputs: voice, note, vel.
 * Outputs: trigger captured. Affiliates: liveRec_triggerIn,
 * midiParser_noteOn.
 */
```

---

## Phase F — Automation Recording

### F-1. Menu record hook

**File:** `Core/Menu/menu.c`

#### F-1a. menu_parseKnobDelta hook — MODIFY at line ~13916

```
/*
 * Automation record hook in knob delta path (S081 §9, §19.6).
 *
 * What: after the value clamp, when liveRec_mode() is LATCH or OVERDUB
 * and the cell is recordable (menu_knobClearTarget resolves a Pattern
 * target, the page is not PERFORMANCE/RECORDING/MIDI/LOAD/SAVE, not in
 * a Morph view, and the target is not track length), calls
 * liveRec_paramIn(target9, SOURCE_KNOB). Why: every user edit of a
 * recordable parameter must mark it as dirty (and latch it in LATCH
 * mode) so that LiveRecord writes it at the next step boundary.
 * liveRec_paramIn carries no value — the step-end write reads the
 * current retained value. Inputs: target9 from menu_knobClearTarget,
 * source. Outputs: dirty bit set, latch bit set (in LATCH mode).
 * Affiliates: liveRec_paramIn, menu_knobClearTarget, liveRec_service
 * step-end table.
 */
```

#### F-1b. menu_parseEncoder hook — ADD at line ~13466

Same hook for the encoder path (used for Scene-setting cells and other
encoder-driven parameters).

### F-2. Value resolver

**File:** `Core/Sequencer/LiveRecord.c`

#### F-2a. liveRec_targetValue7 — ADD

```
/*
 * Read the current 7-bit menu value for a target (S081 §9.2, §19.2).
 *
 * What: reads the parameter's current value from the owner module's
 * state, using the shared 7-bit conversions. Does not read Menu state
 * (LiveRecord must not depend on Menu). Reads SceneData getters
 * directly: instrument_parameters[] for Instrument targets,
 * scene_getVoiceAudioOut / scene_getVoiceFxSendAmount / etc. for Scene
 * targets, EffectsManager retained values for Effect targets. Why: the
 * recorded value is always the current menu value as observed (F-G).
 * No values are stored in LiveRecord's state — they are read at write
 * time. Inputs: target9 (9-bit target ID). Outputs: 7-bit value.
 *
 * Value sources by target class:
 *   Instrument:  scene_instrumentSlotConst(T, slot)->parameter_images
 *                .morph_interpolation[local] (the Normal image)
 *   Nvm:         Morph amount store-7 (127 ↔ 255)
 *   Nou:         scene_getVoiceAudioOut(T, slot)
 *   Nfx:         scene_getVoiceFxSendAmount(T, slot)
 *   7dc:         scene_getSlot6Track7AmpEnvelopeDecay(T)
 *   scl, shf:    Pattern region Normal (track_scale, track_shuffle)
 *   fxm:         scene_getEffectMorphAmount(T) store-7
 *   Effect:      retained Normal, WIDE8 rows compressed to 7 bits
 *
 * Affiliates: liveRec_service step-end write, SceneData.c getters,
 * EffectsManager retained values.
 */
static uint8_t liveRec_targetValue7(uint16_t target9);
```

### F-3. Drain filter for latched/erased parameters

Already covered in C-7c. The filter in `seq_drainPendingAutomation` checks the
registered latch and erase bitmaps and skips matching entries.

### F-4. MIDI CC stub hook

**File:** `Core/MIDI/MidiParser.c`

#### F-4a. midiParser_enterTaggedParameter — MODIFY at line ~128

```
/*
 * CC automation stub hook for LiveRecord (S081 §19.9).
 *
 * What: after a successful parameter store, calls
 * liveRec_paramIn(target, SOURCE_MIDI). The target ID comes from the
 * tagged key; the MIDI rework owns that mapping, so for now this is a
 * stub (the call is present but the mapping always returns 0 / invalid).
 * Why: R4 says MIDI CC automation records the same way as knob edits.
 * The stub ensures the call site exists and the API is exercised.
 * Inputs: tagged key → target9 (stub). Outputs: liveRec_paramIn called
 * (no-op until mapping exists). Affiliates: liveRec_paramIn,
 * midiParser_enterTaggedParameter, MIDI rework (future).
 */
```

---

## Phase G — Live Erase

### G-1. Erase button router

**File:** `Core/Hardware/frontPanel/buttonHandler.c`

#### G-1a. Erase router in processPress/processRelease — ADD

```
/*
 * Live erase button router (S081 §10.2, §19.7).
 *
 * What: while liveRec_mode() == ERASE, intercepts SEQ, VOICE and PLAY
 * presses/releases right after copyClear_buttonPressed(). Routing:
 *   SEQ in VOICE/STEP → liveRec_eraseSeqHeld(pressed) (active track)
 *   SEQ in FX → liveRec_eraseSeqHeld(pressed) (FX sequence)
 *   SEQ in PERF/Load/Save/MENU → consumed (no action)
 *   VOICE → liveRec_eraseTrackHeld(track, pressed)
 *   PLAY → plain transport toggle (buttonHandler_setRunStopState +
 *           seq_setRunning), bypassing bh_chainPlayArmed
 * All other buttons pass through unchanged. Why: live erase must
 * consume SEQ/VOICE/PLAY presses to prevent step toggles, voice
 * selection, mute, overlay triggers, Scene changes and FX hooks.
 * Inputs: button ID, press/release. Outputs: erase gesture routed or
 * consumed. Affiliates: liveRec_eraseSeqHeld, liveRec_eraseTrackHeld,
 * seq_setRunning.
 */
```

### G-2. Erase knob handler

**File:** `Core/Menu/menu.c`

#### G-2a. Knob erase in menu_parseKnobDelta — ADD at line ~13960

```
/*
 * Knob erase during live erase (S081 §10.2, §19.6).
 *
 * What: in the copy/clear pot-ownership branch, if
 * liveRec_mode() == ERASE: resolves the target via
 * menu_knobClearTarget(), calls liveRec_eraseParam(target, fx_lane),
 * does not adjust the value, and calls menu_automationTargetCleared()
 * to drop the underline. The search restarts when erase ends. Works
 * on any page whose cells resolve, including the Morph view and PERF
 * Morph cells (selection, not adjustment). Why: knob erase selects a
 * parameter for erase by turning its knob. The parameter is not
 * adjusted — only its automation is removed from rtk as the playhead
 * passes. Inputs: knob ID → target9 + fx_lane. Outputs:
 * liveRec_eraseParam called, underline cleared. Affiliates:
 * liveRec_eraseParam, menu_knobClearTarget, menu_automationTargetCleared.
 */
```

### G-3. EffectsManager FX-sequence erase

**File:** `Core/DSP/Effects/EffectsManager.c`

#### G-3a. New effects_setLiveEraseLanes — ADD

```
/*
 * Register lanes for FX-sequence live erase (S081 §10.4, §19.4).
 *
 * What: sets the live-erase lane mask. At each FX step start,
 * EffectsManager excludes these lanes' locks from resolution and records
 * the (step, lanes) in its pending mask. Why: FX-sequence erase removes
 * locks as the FX playhead passes, without the locks ever sounding
 * (not even for one render block). The pending mask is drained by
 * liveRec_service. Inputs: laneMask (16 bits). Outputs: erase lanes
 * registered. RAM: +34 B (16 × 16-bit pending mask + lane mask).
 * Affiliates: effects_seqConsume/effects_seqSelect (exclude lanes),
 * effects_takeErasedLocks (drain), effects_clearSeqLanes (fan-out),
 * liveRec_service.
 */
void effects_setLiveEraseLanes(uint16_t laneMask);
```

#### G-3b. New effects_takeErasedLocks — ADD

```
/*
 * Drain the FX-erase pending mask (S081 §10.4, §19.4).
 *
 * What: copies the pending mask (which steps had which lanes erased)
 * into the output, clears it. Called by liveRec_service on each 500 Hz
 * pass. Why: the pending mask accumulates at TIM3 speed (FX step start);
 * LiveRecord drains it at 500 Hz and calls effects_clearSeqLanes to
 * remove the locks from the origin Scene and its linked Scenes.
 * Inputs: output stepMask, laneMask pointers. Outputs: masks copied,
 * pending cleared. Affiliates: liveRec_service, effects_clearSeqLanes,
 * effects_fanoutMask.
 */
uint8_t effects_takeErasedLocks(uint16_t *stepMask, uint16_t *laneMask);
```

### G-4. Overdub/erase exclusivity

**File:** `Core/Sequencer/LiveRecord.c`

```
/*
 * Overdub and erase are mutually exclusive (S081 §10.3, F8).
 *
 * What: entering erase (liveRec_eraseBegin) disables overdub; entering
 * overdub while erase is enabled does nothing. Releasing both SHIFT and
 * CLEAR ends erase. Releasing REC during erase restores the prior
 * LATCH/OFF state without a toggle. Why: simultaneous overdub and erase
 * would create contradictory writes on the same step. The simpler rule
 * is mutual exclusion. Affiliates: liveRec_eraseBegin, liveRec_overdubBegin,
 * buttonHandler REC release handler.
 */
```

---

## Phase H — Target Negotiation, Scene/Epoch Handling, Q13 Refusal

### H-1. liveRec_service target and epoch handling

**File:** `Core/Sequencer/LiveRecord.c`

```
/*
 * Take boundary and target negotiation (S081 §7, §19.2).
 *
 * What: at the top of liveRec_service(), checks for take boundaries:
 *   - epoch change (seq_getPlaybackEpoch() != last seen);
 *   - rtk change or mode change;
 *   - menu_isStorageBusy() (Load: release target, suspend).
 * A new take clears latch/dirty, discards open roll and pending grace,
 * flushes FX-erase pending mask, resets the take origin.
 * Then evaluates T = seq_getTrackPlayedScene(rtk). If T != active
 * Scene, calls patSvc_requestTarget(T); otherwise patSvc_releaseTarget().
 * Publishes seq_setRecordTarget and seq_setAutomationFilter.
 * While !patSvc_targetOpen(T), all writes are dropped and counted.
 * Why: the recording target must track rtk's played Scene and reset
 * on any context change. Inputs: seq_getPlaybackEpoch,
 * seq_getTrackPlayedScene, menu_isStorageBusy. Outputs: target set,
 * filter registered, bitmaps cleared on new take. Affiliates:
 * patSvc_requestTarget/releaseTarget/targetOpen, seq_setRecordTarget,
 * seq_setAutomationFilter.
 */
```

### H-2. Q13 refusal

**File:** `Core/Menu/menu.c`

#### H-2a. menu_parseKnobDelta/menu_parseEncoder — MODIFY

```
/*
 * Q13 silent refusal while T != A (S081 §7.5, §19.6).
 *
 * What: at the top of the knob/encoder handler, if
 * liveRec_activePatternLocked() returns true and the cell writes the
 * viewed Pattern (PAR_STEP_*, PAR_TRACK_LENGTH/SCALE/SHUFFLE/PLAY_MODE/
 * LOOPABLE, step-automation editor cells, va_writeAutomationFromKnob,
 * sa_* writers, menu_broadcastStepSpecial), return before parameter_values
 * or the display changes. No message. Why: while the recording target
 * is a non-active Scene, edits to the active Scene's Pattern would
 * conflict with recording. The refusal is silent per Q13. Inputs:
 * liveRec_activePatternLocked(). Outputs: edit suppressed. Affiliates:
 * liveRec_activePatternLocked (T != A check).
 */
```

---

## Phase I — Specs and Memory

### I-1. Documentation updates

At session close (no code change required during implementation):

| Document | Section | Change |
|---|---|---|
| `MEMORY.md` | §5.1 S075 F3 | Update to reflect R12 menu override rule |
| `PATTERN_DYNAMIC_STACK.md` | §6.2a | Update to reflect R12 |
| `PATTERN_DYNAMIC_STACK.md` | §12.3 | Fix the queue format description (6/10/16) |
| `PATTERN_DYNAMIC_STACK.md` | §12.16 | Add LiveRecord stage codes |
| `EFFECTS_BUS_REFERENCE.md` | §6, §10, §11.2 | Rewrite for R13 |
| `MODULE_INTERCHANGE_SPEC.md` | stale rows (§3.2-12) | Fix |
| `STORAGE_SRAM_MANIFEST.md` | effects_automation_t | Update size after owner removal |

### I-2. config.h

No changes required. `DEV_MODE_LOGGING=1` stays for development.
`AUTOSAVE_TRACE_RECORD_COUNT=2048` stays temporary (existing carried debt).

---

## RAM Summary

| Phase | Item | Bytes |
|---|---|---|
| A1 | `voiceControl_pendingReset` (u8) | +1 |
| A1 | `seq_trackShuffleStep` + `seq_trackShuffleRoll` (7+7) | +14 |
| A1 | `seq_menuOverride` (6 × u64) | +48 |
| A2 | `menuHold` (u64) + `menuHoldMorph` (u8) | +9 |
| A2 | Remove owner/pending/held fields | −77 |
| A3 | (no new RAM) | 0 |
| B | LiveRecord state (mode, rtk, T, bitmaps, roll, grace, drops) | ~270 |
| C | Ring (34), echo (7), hold (1), erase masks (2), stamps (8), epoch (1), filter (9) | +62 |
| D | Staging table (34), target override (2) | +36 |
| E | (in LiveRecord) | 0 |
| F | (in LiveRecord) | 0 |
| G | FX-erase pending mask (34) | +34 |
| **Total added** | | **~397** |
| **Released** | | **~77** |
| **Net** | | **~320** |

The plan's §14 approved ~410 B added with ~79 B released. The schedule totals
are within that envelope. Exact byte counts will be fixed during implementation.

---

## Dependency Order

```
A1 ──► A2 ──► A3 ──► B ──► C ──► D ──► E ──► F ──► G ──► H ──► I
                      │           │
                      └───────────┘  (C and D are co-dependent:
                                      LiveRecord calls Sequencer
                                      services and patSvc ops)
```

A1 must land first (trigger/automation ordering fix). A2 builds on A1's
drain changes. A3 is independent of A2 but logically grouped. B creates the
LiveRecord module that C/D/E/F/G/H populate. I is documentation only.

Each phase is verified on hardware before the next begins (§15 verify column).
