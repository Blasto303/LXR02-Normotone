# S081 P4 — VOICE Mode Bar-Change LED Pulse

Session 081, part 4 (2026-10-16), branch `dev-ph6-cleanup`. Plan only; no
code has changed.

- **Revision 1:** the bar pulse (§1–§7) with questions Q1–Q4.
- **Revision 2:** answers Q1–Q4 folded in (§8). New: the pulse-expiry
  clock-wrap defect (former risk R4-3) is fixed in this part (§4.5, user).
  One question remains (§9, Q5).
- **Revision 3:** Q5 answered: the flash expiry is fixed the same way
  (§4.5). Implementation schedule: `S081_P4_IMPLEMENTATION.md`.

**Authority:**

- the user's specification (§1);
- `MEMORY.md` binding rules: RAM approval policy; "Sequencer-to-LED feedback
  uses `seq_ledState`" and "`led_processSeqLedState()` drains sequencer LED
  events in the main loop" (Sequencer / PATGEN reminders);
- the LED layer contract in `ledHandler.c` (pulse > fast > flash >
  blink/chase > base).

---

## 1. Specification (user)

1. In **VOICE mode**, the SELECT LED of the bar the active track is playing
   **pulses once** when that bar changes.
2. The pulse must **not interrupt** the normal VOICE-mode SELECT LED
   operation (the sub-page indicator).
3. **No pulse** when:
   - the track has only a single bar;
   - the bar changed to, or looped back to, is the bar being exited from;
   - the track's play mode is `rnd`.
4. **VOICE mode only.** STEP mode has its own bar interaction (the SELECT bar
   chaselight); PERF mode never shows the bar.

---

## 2. Terms

- **Track:** the active voice's track (`menu_getActiveVoice()`), the track
  whose steps the chase light shows.
- **Bar:** `step / 16` (`NUM_STEPS_PER_BAR`), 0..7. SELECT1..8 show bars
  1..8. A track of length 1..16 has one bar; 17..32 has two; and so on.
- **Single bar:** the track's **effective** length (the length playing now,
  after Morph and length automation, `seq_effectiveTrackLength[]`) is 16 or
  less.
- **Pulse:** the existing `led_pulseLed()`: a one-shot inversion of the
  LED's current state for `LED_PULSE_TIME_MS` (50 ms) on `LED_LAYER_PULSE`,
  after which the LED re-renders from its remaining layers. On the lit
  sub-page LED it shows as a short blink off; on a dark LED as a short
  blink on.

---

## 3. The code as it stands

| Item | Where | Role |
|---|---|---|
| Chase publish | `sequencer.c` `seq_processSchedulerTick()` (`:~5885`), realigns (`:4384`, `:4460`), loop resume (`:5103`) | TIM3/foreground write `seq_ledState.chaseStep = seq_stepIndex[active voice]` and set `SEQ_LED_DIRTY_CHASE` |
| `SeqLedState` | `ledHandler.h:218-229` | `dirty`, `chaseStep`, `beatPulse`, `recordMainStep`; no track id |
| Drain | `led_processSeqLedState()` (`ledHandler.c:1620`) | Foreground, main loop: CHASE → STEP-row chase light; STEP mode also runs `led_updateSelectBarChaselight()` |
| STEP bar chaselight | `led_updateSelectBarChaselight()` (`ledHandler.c:1527`) | STEP mode only: writes SELECT **base** states (viewed bar on) plus a beat-timed playback-bar indicator |
| Pulse | `led_pulseLed()` (`ledHandler.c:585`) | 8 shared slots; 50 ms temporary inversion; top-priority layer; expiry in `led_tickHandler()` re-renders from the stack |
| VOICE SELECT row | `led_setActiveSelectButton()` (`ledHandler.c:1098`) | Base state: one lit LED for the voice sub-page |
| Capture snapshot | `seq_captureTrackTime()` (`sequencer.c:4663`) | PRIMASK copy of a track's live step, effective length, play mode, played Scene |

In VOICE mode nothing writes the SELECT row on playback today.

---

## 4. Design

### 4.1 Detection in the foreground drain

In `led_processSeqLedState()`, in the `SEQ_LED_DIRTY_CHASE` branch:

1. **Read the track live.** `seq_captureTrackTime(activeTrack, &snap)`
   gives the active track's live step, effective length and play mode in
   one PRIMASK copy.
   - **Why not `seq_ledState.chaseStep`:** it carries no track id. If the
     active voice changes between TIM3's write and the drain, the drain
     would compare the new track against the old track's step and pulse
     falsely.
   - **Why the capture helps:** it reads the right track every time, and
     needs no new Sequencer API and no change to `SeqLedState`.
2. **Compute the bar:** `bar = snap.stepIndex / 16` (no bar while the
   cursor is −1).
3. **Pulse** `LED_PART_SELECT1 + bar` only when **all** of these hold:
   - transport running (`seq_isRunning()`);
   - mode is VOICE (`buttonHandler_getMode() == SELECT_MODE_VOICE`);
   - the remembered track is this track (no active-voice change since the
     last drain);
   - a bar was remembered and `bar != remembered bar` (spec 3b);
   - `snap.length > 16` (not a single bar, spec 3a);
   - `snap.playMode != 3` (not `rnd`, spec 3c);
   - `bar < 8`.
4. **Remember** the track and bar **in every mode**, whether or not a pulse
   fired. Entering VOICE mode mid-play then compares against a current
   baseline, so the first step after the mode change does not pulse
   falsely.
5. **Transport stopped** (the CHASE dirty at stop): forget the bar, so the
   first step after a start does not pulse (Q2, decided).

### 4.2 Why the pulse cannot disturb the SELECT row (spec 2)

- `led_pulseLed()` changes only the physical output and adds
  `LED_PULSE_TIME_MS` on the pulse layer. It never touches the base state
  that `led_setActiveSelectButton()` owns.
- A sub-page change during the pulse writes the base state as usual. The
  renderer holds the pulse until it expires, then shows the new base (or a
  SELECT flash/blink layer, if one is active).
- The pulse is the highest layer, so it is also visible over a SELECT
  flash or blink, and those resume afterwards.

### 4.3 What counts as a bar change

The rule is purely "the bar of the step now playing differs from the bar of
the last step seen". So:

- fwd/rev/pip/onc/1fr crossings into another bar pulse;
- the pip boundary repeat and a wrap within one bar do not;
- a wrap from the last bar back to bar 1 pulses (different bar);
- a looper wrap or resume, realign, per-track Scene change or length change
  that lands in another bar pulses once at the next drain (Q3, decided).
- A Morph or automation change of the length to ≤16 stops all pulses at
  once (single bar), even on the step that wraps back to bar 1.

### 4.4 Pulse length (Q1, decided)

The shared 50 ms `led_pulseLed()` is used as it is. No duration argument is
added.

### 4.5 Pulse expiry across the clock wrap (user: fix)

- **Defect:** `led_tickHandler()` (`ledHandler.c:973`) ends a pulse when
  `time_sysTick > led_pulseEndTime[i]`. `time_sysTick` is a 16-bit
  millisecond counter (`timebase.h:72`) that wraps every 65.536 s.
  `led_pulseLed()` stores `end = time_sysTick + 50`, truncated to 16 bits.
  - Started in the last 50 ms before the wrap (for example at 65,520), the
    end wraps to a small value (34). `65,520 > 34` is true on the next tick,
    so the pulse ends at once and is **never seen**.
  - This affects every pulse client (REC overdub pulse, copy/step feedback,
    and now the bar pulse); about 1 pulse in 1,300 is lost.
- **Fix:** compare by signed 16-bit distance, the form the fast blink
  already uses (`ledHandler.c:1021`):

  ```c
  (int16_t)(uint16_t)(time_sysTick - led_pulseEndTime[i]) > 0
  ```

  This is true once the clock is past the end time, and false while the end
  time is still up to 32,767 ms ahead, across the wrap. The pulse lasts its
  full 50 ms at any clock value.
- **Flash (Q5):** the flash end and phase-step tests (`ledHandler.c:985`,
  `:989`) use the same signed distance.
- **Comment:** the comment blocks of `led_tickHandler()`, the pulse slots
  and the flash slots say the expiry is wrap-safe.

### 4.6 Other modes

- **STEP:** unchanged. `led_updateSelectBarChaselight()` keeps owning the
  SELECT bar display there. The P4 test is mode-gated, so no pulse is ever
  started in STEP.
- **PERF, FX, Load/Save, global:** no pulse (mode gate).
- A pulse started in VOICE just before a mode change ends by itself after
  50 ms and re-renders the new mode's SELECT state.

---

## 5. Changes (for the schedule)

| File | Change |
|---|---|
| `ledHandler.c` `led_tickHandler()` | Pulse and flash expiry use the wrap-safe signed distance (§4.5); comment blocks updated. |
| `ledHandler.c` | Two static bytes `led_barPulseTrack` (0xFF = none) and `led_barPulseBar`, with a contract comment. A static helper `led_updateVoiceBarPulse(uint8_t running)` implementing §4.1, called from the CHASE branch of `led_processSeqLedState()` in every mode (it remembers in every mode and pulses only in VOICE). The drain's comment gains one paragraph. |
| `ledHandler.h` | None (the helper is static). The `SeqLedState` comment gains a note that P4 reads the live track through `seq_captureTrackTime()`, not `chaseStep`, for the bar pulse. |
| `sequencer.c/.h` | None. |
| `buttonHandler.c`, `menu.c` | None. |

**RAM:** **2 B static SRAM1** (`led_barPulseTrack`, `led_barPulseBar`),
owner ledHandler, lifetime static, runtime-only. **Acknowledged** (Q4,
user, 2026-10-16). The expiry fix uses no RAM. Stack: one `seq_track_time_t` (28 B, 40 B
after S081 P3 HD-2) in the drain while the CHASE branch runs.

**CPU:** one PRIMASK snapshot per CHASE drain (each step advance of any
track, foreground). No TIM3 change.

**Flash:** roughly +150 B.

---

## 6. Risks

- **R4-1 Pulse length.** 50 ms (`LED_PULSE_TIME_MS`) is the shared pulse
  length (Q1: accepted). If it proves too short on hardware, a dedicated
  length can be passed without RAM.
- **R4-2 Pulse slots.** The 8 pulse slots are shared. With all 8 busy the
  bar pulse is dropped silently (existing rule). A bar pulse occupies one
  slot for 50 ms.
- **R4-3 Pulse lost at the clock wrap:** fixed in this part (§4.5).
- **R4-5 Flash has the same comparison.** The SELECT/SEQ/MODE flash in the
  same function ends and steps its phase with `time_sysTick > end` and
  `time_sysTick > next` (`ledHandler.c:985`, `:989`). Near the wrap a
  400 ms flash ends at once, or its phase steps on every tick until the
  next time catches up (a burst of flicker). Fixed in this part (Q5).
- **R4-4 Very fast tracks.** Bars of a 1/128 track change about every
  80 ms at 120 BPM; pulses then nearly merge. The behaviour is correct by
  the rule; the hardware check will show whether it reads well.

---

## 7. Verification (hardware, user)

1. VOICE mode, a 64-step fwd track playing: each bar change pulses
   SELECT 1→2→3→4→1 once. The lit sub-page LED blinks off when it is the
   bar LED, and the others blink on.
2. Changing sub-page during playback: the SELECT row keeps working; no
   stuck or missing sub-page LED.
3. Track length 16 or less (including after a Morph or length automation
   down to 16): no pulse.
4. pip at a bar boundary (the repeated boundary step): no pulse for the
   repeat; one pulse when it crosses.
5. `rnd`: no pulse at all.
6. STEP mode: the bar chaselight is unchanged; no extra pulse. PERF: no
   pulse.
7. Changing the active voice during playback: no pulse caused by the
   change itself.
8. Transport start: no pulse on the first step.
9. Looper (PERF) does not apply; returning to VOICE mid-loop or after a
   realign: one pulse only if the bar actually changed.
10. Expiry fix (§4.5): a host check of the comparison for start times
    65,400..65,535 and 0..200 (every pulse lasts exactly 50–51 ticks). On
    hardware, leave a 64-step track playing past several 65.5 s clock wraps
    with a bar change every few hundred milliseconds: no bar pulse is
    missing.

---

## 8. Answers (user, 2026-10-16)

| # | Question | Answer |
|---|---|---|
| Q1 | Pulse length | The shared 50 ms pulse is fine |
| Q2 | No pulse on the first step after a transport start | Yes, correct |
| Q3 | A jump that lands in another bar pulses once | Yes |
| Q4 | 2 B static SRAM1 in ledHandler | Acknowledged |
| — | The pulse lost at the clock wrap (former R4-3) | Fix it (§4.5) |
| Q5 | Fix the flash expiry the same way | Yes (§4.5) |

---

## 9. Questions

All answered (§8). None open.
