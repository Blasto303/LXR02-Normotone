# S081 P4 — VOICE Mode Bar-Change LED Pulse: Implementation Schedule

Session 081, part 4 (2026-10-16), branch `dev-ph6-cleanup`.

This schedule implements `S081_P4_VOICE_MODE_BAR_LED.md` revision 3
(answers Q1–Q5). Implementation is complete; see the progress log at the end
of this file.

**Line numbers** are the working tree on 2026-10-16 (P1/P1B/P1C, P2 and its
follow-ups uncommitted on top of `d21e245`; S081 P3 not yet applied). P4
touches only `ledHandler.c` and `ledHandler.h`, which P3 does not change, so
the two schedules can land in either order. Each entry gives:

- `file:line`;
- the action (**ADD** / **REMOVE** / **MODIFY**);
- the code;
- the comment block that goes beside the change as documentation in place.

Where a step replaces an existing comment block, the new block replaces the
old one; it is not added beside it.

**Authority:**

- `S081_P4_VOICE_MODE_BAR_LED.md` revision 3;
- `MEMORY.md` binding rules: RAM approval policy; sequencer LED feedback goes
  through `seq_ledState` and is drained by `led_processSeqLedState()` in the
  main loop;
- the LED layer contract in `ledHandler.c` (pulse > fast > flash >
  blink/chase > base).

**Build gate:** `make all` clean (no new warnings), then
`python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf`.

---

## 0. Summary

### RAM and cost

| Item | Bytes | Region | Owner | Lifetime |
|---|---|---|---|---|
| `led_barPulseTrack` | 1 | SRAM1 | ledHandler | static, runtime-only |
| `led_barPulseBar` | 1 | SRAM1 | ledHandler | static, runtime-only |
| **Total** | **2** | | | approved (Q4, 2026-10-16) |

- **Stack:** one `seq_track_time_t` (28 B today, 40 B after S081 P3 HD-2) in
  `led_updateVoiceBarPulse()`, called from the main-loop drain.
- **CPU:** one PRIMASK snapshot per CHASE drain (each step advance of any
  track), foreground only. No TIM3 change.
- **Flash:** about +150 B.

### Code facts checked for this schedule

- `led_tickHandler()` runs in the main loop (`main.c:1425`), the same
  context as `led_processSeqLedState()`, so a pulse started by the drain
  and its expiry never run concurrently.
- `time_sysTick` is `volatile uint16_t`, 1 ms (`timebase.h:72`).
- A transport stop sets `SEQ_LED_DIRTY_CHASE` after `seq_running = 0`
  (`sequencer.c:6143`), so the drain sees the stop and can forget the bar.
- `seq_captureTrackTime()` is public (`sequencer.h:492`). Its snapshot holds
  the live `stepIndex`, the effective `length` and the played Scene's
  `playMode`.
- Visible in `ledHandler.c` through the existing includes:
  - `NUM_TRACKS`, `NUM_BARS`, `NUM_STEPS_PER_BAR` (`PatternData.h`);
  - `SELECT_MODE_VOICE` and `buttonHandler_getMode()` (`buttonHandler.h`);
  - `menu_getActiveVoice()` (`menu.h`);
  - `seq_isRunning()` (`sequencer.h`).
- Play mode `3` is `rnd`. There is no named constant: the sequencer's
  play-mode switch uses literals (`sequencer.c` `seq_advanceTrackStep()`).

### Expiry fix, checked before scheduling

A host check of `(int16_t)(uint16_t)(now - end) > 0` with
`end = start + 50`:

- start times 65,400..65,535 and 0..200;
- every pulse ended after exactly 51 ticks, and none ended early.

The old `now > end` ended every pulse started at 65,486..65,535 on the
next tick.

---

## Step list

### LH-1 Pulse slot comment — MODIFY `ledHandler.c:85-93`

Replace the "One-shot pulse slots." comment block with:

```
/*
 * One-shot pulse slots.
 *
 * led_pulseEndTime stores the time_sysTick deadline for each active pulse
 * slot (start + LED_PULSE_TIME_MS, truncated to the 16-bit millisecond
 * clock). led_pulseLedNumber stores the logical LED ID that must be restored
 * when the deadline passes. led_pulsingLeds is the active-slot bitset: bit N
 * set means slot N is in use. led_tickHandler() is the only normal consumer;
 * it tests the deadline by signed 16-bit distance (S081 P4), so a pulse
 * started just before the clock wraps lasts its full time. The arrays are
 * volatile because effects are started and serviced from different call
 * sites of the main loop.
 */
```

### LH-2 Flash slot comment — MODIFY `ledHandler.c:96-106`

At the end of the "Short patterned flash slots." block, before `*/`, ADD:

```
 *
 * S081 P4: led_flashEndTime and led_flashNextTime are 16-bit time_sysTick
 * deadlines; led_tickHandler() compares them by signed 16-bit distance, so
 * a flash near the clock wrap neither ends at once nor steps its phase on
 * every tick.
```

### LH-3 Wrap-safe expiry — MODIFY `led_tickHandler()` at `ledHandler.c:955-995`

Replace the function comment (`:955-968`) with:

```c
/*
 * Service time-based LED effects.
 *
 * Why: pulse, flash and blink effects are small slot tables so callers can
 * request visual feedback without blocking. Inputs are implicit global effect
 * state and time_sysTick. Outputs: expired pulse slots clear their layers and
 * render fallback state, flash groups step their phase and expire, active
 * blink slots toggle at LED_BLINK_TIME_MS intervals unless a higher layer owns
 * the LED, and the fast blink toggles its phase.
 *
 * Deadlines (S081 P4): every deadline is a 16-bit time_sysTick value. Pulse
 * and flash deadlines are tested with led_timeReached() (signed 16-bit
 * distance), the same rule the fast blink uses. The former `now > deadline`
 * test ended a pulse or flash started just before the 65.536 s clock wrap at
 * once (the pulse was never seen) and made a flash's phase step on every
 * tick.
 *
 * Call timing: the main loop (main.c), between audio render passes. It must
 * stay cheap and non-blocking. It does not drain sequencer LED dirty state;
 * that is led_processSeqLedState(), also in the main loop.
 */
```

ADD above the function:

```c
static uint8_t led_timeReached(uint16_t deadline)
{
    return (uint8_t)((int16_t)(uint16_t)(time_sysTick - deadline) > 0);
}
```

```
/*
 * Has a 16-bit millisecond deadline passed? (S081 P4)
 *
 * What: 1 once time_sysTick is past `deadline`, by signed 16-bit distance;
 * 0 while the deadline is up to 32,767 ms ahead, also across the clock wrap.
 * Why: time_sysTick wraps every 65.536 s, so a plain `now > deadline`
 * compare is wrong whenever the deadline has wrapped and the clock has not
 * (pulses lost, flashes ended or flickering). Matches the plain compare's
 * "strictly after" meaning, so effect durations are unchanged away from the
 * wrap. Inputs: deadline. Output: flag. Accessors: led_tickHandler() (pulse
 * and flash). Affiliates: the fast-blink test in led_tickHandler()
 * (the same rule, written inline), timebase.h time_sysTick.
 */
```

In the body:

- `:973` `(time_sysTick > led_pulseEndTime[i])` becomes
  `led_timeReached(led_pulseEndTime[i])`.
- `:985` `if (time_sysTick > led_flashEndTime[i]) {` becomes
  `if (led_timeReached(led_flashEndTime[i])) {`.
- `:989` `} else if (time_sysTick > led_flashNextTime[i]) {` becomes
  `} else if (led_timeReached(led_flashNextTime[i])) {`.

The blink test (`:997`) and the fast-blink test (`:1020-1021`) are
unchanged: the blink test already has a wrap guard, and the fast blink
already uses the signed distance.

### LH-4 Header comment for the tick handler — MODIFY `ledHandler.h:114-125`

At the end of the comment above `void led_tickHandler(void);`, before
`*/`, ADD:

```
 * S081 P4: pulse and flash deadlines are compared by signed 16-bit distance,
 * so effects started just before the time_sysTick wrap last their full time.
```

### LH-5 Bar-pulse state and helper — ADD after `led_updateSelectBarChaselight()` (after its closing brace, `ledHandler.c:1561`)

```c
static uint8_t led_barPulseTrack = 0xFFu;
static uint8_t led_barPulseBar   = 0xFFu;
```

```
/*
 * VOICE-mode bar pulse state (S081 P4).
 *
 * What: the track and bar (0..7) last seen by led_updateVoiceBarPulse();
 * 0xFF = none (transport stopped, cursor not started, or never seen). Why:
 * the pulse fires when the active track's bar differs from the bar of the
 * last step seen on the SAME track; remembering in every mode keeps the
 * baseline current, so entering VOICE mode or changing the active voice never
 * pulses by itself. Writers/readers: led_updateVoiceBarPulse() only.
 * Lifetime: static, runtime-only. RAM: 2 B SRAM1 (approved, plan Q4).
 * Affiliates: led_processSeqLedState() (CHASE branch).
 */
```

```c
static void led_updateVoiceBarPulse(void)
{
    seq_track_time_t snap;
    uint8_t track = menu_getActiveVoice();
    uint8_t bar;

    if (!seq_isRunning() || track >= NUM_TRACKS) {
        led_barPulseTrack = 0xFFu;
        led_barPulseBar = 0xFFu;
        return;
    }
    seq_captureTrackTime(track, &snap);
    bar = (snap.stepIndex < 0)
        ? 0xFFu : (uint8_t)((uint8_t)snap.stepIndex / NUM_STEPS_PER_BAR);
    if (bar < NUM_BARS &&
        track == led_barPulseTrack &&
        led_barPulseBar != 0xFFu &&
        bar != led_barPulseBar &&
        snap.length > NUM_STEPS_PER_BAR &&
        snap.playMode != 3u &&                    /* 3 = rnd */
        buttonHandler_getMode() == SELECT_MODE_VOICE)
        led_pulseLed((uint8_t)(LED_PART_SELECT1 + bar));
    led_barPulseTrack = track;
    led_barPulseBar = bar;
}
```

```
/*
 * Pulse the SELECT LED of a new bar in VOICE mode (S081 P4).
 *
 * What: reads the active track's live step, effective length and play mode
 * (seq_captureTrackTime(), one PRIMASK copy) and, when its bar (step / 16)
 * differs from the bar last seen on the same track, pulses SELECT(bar + 1)
 * once with led_pulseLed(): a 50 ms inversion on the pulse layer that never
 * changes the SELECT row's base state, so the VOICE sub-page indicator keeps
 * working (spec 2). No pulse when:
 *  - the mode is not VOICE (STEP has its own bar chaselight; PERF never shows
 *    the bar);
 *  - the effective length is 16 or less (a single bar; also when a Morph or
 *    length automation shortens the track);
 *  - the play mode is rnd (3);
 *  - the bar is the one being left (pip boundary repeat, a wrap within one
 *    bar, a loop back to the same bar);
 *  - the active voice changed since the last call (re-baselined instead);
 *  - the transport is stopped: the bar is forgotten, so the first step after
 *    a start never pulses (plan Q2).
 * A jump into another bar (realign, looper resume, per-track Scene or length
 * change) pulses once like any bar change (plan Q3). The track and bar are
 * remembered in every mode.
 * Why: why not seq_ledState.chaseStep: it carries no track id, so a voice
 * change between the TIM3 write and this drain would compare two tracks and
 * pulse falsely. Inputs: menu_getActiveVoice(), seq_isRunning(), the
 * snapshot, buttonHandler_getMode(). Outputs: at most one led_pulseLed();
 * led_barPulseTrack/Bar. Context: foreground (main-loop drain). Accessors:
 * led_processSeqLedState() (CHASE branch, every mode). Affiliates:
 * led_pulseLed(), led_tickHandler() (expiry), led_setActiveSelectButton()
 * (the base state it must not disturb), led_updateSelectBarChaselight()
 * (STEP-mode counterpart).
 */
```

### LH-6 Call from the CHASE branch — MODIFY `led_processSeqLedState()` at `ledHandler.c:1716-1717`

After the existing

```c
        if (buttonHandler_getMode() == SELECT_MODE_STEP)
            led_updateSelectBarChaselight();
```

inside `if (d & SEQ_LED_DIRTY_CHASE) {`, ADD:

```c
        /*
         * VOICE-mode bar pulse (S081 P4).
         *
         * What: one pulse of the new bar's SELECT LED when the active
         * track's bar changes in VOICE mode; in every other mode the call
         * only keeps the remembered track/bar current, and with the
         * transport stopped (the CHASE dirty at stop) it forgets them. Why:
         * the CHASE dirty bit fires at every step advance, at playhead jumps
         * and at stop, exactly when the bar can change. Inputs/outputs: see
         * led_updateVoiceBarPulse(). Affiliates: led_updateVoiceBarPulse(),
         * led_updateSelectBarChaselight() (STEP mode, above).
         */
        led_updateVoiceBarPulse();
```

Extend the drain's function comment (`:1606-1619`) with:

```
 * S081 P4: the CHASE branch also runs led_updateVoiceBarPulse(), which
 * pulses the SELECT LED of a new bar in VOICE mode and keeps its baseline
 * current in every mode.
```

### LH-7 Drain declaration comment — MODIFY `ledHandler.h:266-273`

At the end of the comment above `void led_processSeqLedState(void);`,
before `*/`, ADD:

```
 * S081 P4: in VOICE mode a CHASE drain that finds the active track in a new
 * bar pulses that bar's SELECT LED once (single-bar and rnd tracks never; no
 * pulse on the first step after a start). The bar is read live through
 * seq_captureTrackTime(), not from chaseStep, which has no track id.
```

### LH-8 Flag comment — MODIFY `ledHandler.h:200` (the CHASE bullet of the dirty-flag comment)

`- CHASE: chase/current-step LED should move to seq_ledState.chaseStep.`
becomes:

```
 * - CHASE: chase/current-step LED should move to seq_ledState.chaseStep;
 *   the drain also checks the active track's bar for the VOICE-mode bar
 *   pulse (S081 P4), reading the track live, not from chaseStep.
```

---

## Verification

**Build:** `make all` clean, `link_budget.py`, and `nm -S` showing
`led_barPulseTrack` and `led_barPulseBar` at 1 B each.

**Host check (scratch):** `led_timeReached()` copied 1:1, with deadlines
`start + 50` and `start + 400`, start 65,000..65,535 and 0..1,000. Every
pulse lasts 51 ticks, and every flash lasts 401 ticks with its phase
stepping every 80 ticks.

**Hardware (user):**

1. VOICE mode, a 64-step fwd track playing: one pulse per bar change, SELECT
   1→2→3→4→1. The lit sub-page LED blinks off when it is the bar LED; the
   others blink on.
2. Sub-page changes during playback: the SELECT row always shows the right
   sub-page.
3. Length 16 or less, including after a Morph or length automation down to
   16: no pulse.
4. pip across a bar boundary: no pulse for the repeated boundary step; one
   pulse when it crosses.
5. `rnd`: no pulse.
6. STEP mode: bar chaselight unchanged, no pulse. PERF: no pulse.
7. Active voice change during playback: no pulse from the change itself.
8. Transport start: no pulse on the first step.
9. A realign into another bar: one pulse.
10. Several minutes of playback across 65.5 s clock wraps: no bar pulse
    missing, and copy/step/REC pulses and SELECT/SEQ flashes behave the
    same near a wrap as elsewhere.

---

## Documentation at close

- `MODULE_INTERCHANGE_SPEC.md`, the LED/front-panel section: the VOICE-mode
  bar pulse (rules from plan §1/§4) and the wrap-safe pulse and flash
  expiry.
- `STORAGE_SRAM_MANIFEST.md`: +2 B ledHandler statics.
- `SCOPING_TARGETS.md`: the pulse/flash wrap defect closed in S081 P4.

---

## Order of work

| Step | Content | Gate |
|---|---|---|
| LH-1..LH-4 | Wrap-safe expiry (pulse and flash) | clean build |
| LH-5..LH-8 | Bar pulse | clean build; `link_budget.py` |
| — | Hardware list above | — |

---

## Progress log

Baseline before any change (2026-10-16, at the end of S081 P3):
`text=573,072`, `data=412`, `bss=429,104`; flash payload 573,484 / 753,664 B
(headroom 180,180 B).

### LH-1..LH-4 — wrap-safe expiry (pulse and flash) — DONE

- **LH-1** `ledHandler.c`: the "One-shot pulse slots." comment now states the
  deadline is start + `LED_PULSE_TIME_MS` and is tested by signed 16-bit
  distance.
- **LH-2** `ledHandler.c`: the "Short patterned flash slots." comment gained
  the S081 P4 paragraph about `led_flashEndTime`/`led_flashNextTime`.
- **LH-3** `ledHandler.c`: `led_timeReached(uint16_t)` added above
  `led_tickHandler()` with its own comment block; the `led_tickHandler()`
  comment was replaced with the "Deadlines (S081 P4)" text. The pulse test,
  the flash-end test and the flash phase-step test now use
  `led_timeReached()`. The blink and fast-blink tests are unchanged.
- **LH-4** `ledHandler.h`: the `led_tickHandler()` comment notes the
  wrap-safe distance comparison.

### LH-5..LH-8 — bar pulse — DONE

- **LH-5** `ledHandler.c`: `led_barPulseTrack`/`led_barPulseBar` statics (0xFF
  sentinel) with a state comment, and `led_updateVoiceBarPulse()` with its
  full comment block, added after `led_updateSelectBarChaselight()`. The
  helper follows the schedule exactly (mode gate, `length > 16`, `playMode !=
  3`, same-track comparison, forget on stop).
- **LH-6** `ledHandler.c`: `led_updateVoiceBarPulse()` is called in the
  `SEQ_LED_DIRTY_CHASE` branch of `led_processSeqLedState()`, after the STEP
  chaselight call, with a comment block; the drain's leading comment gained
  the S081 P4 paragraph.
- **LH-7** `ledHandler.h`: the `led_processSeqLedState()` comment notes the
  VOICE-mode bar pulse and that the bar is read live.
- **LH-8** `ledHandler.h`: the CHASE bullet of the dirty-flag comment notes
  the live read.
- Also (plan §5): the `SeqLedState.chaseStep` comment notes that it carries no
  track id and that the P4 bar pulse reads the track live through
  `seq_captureTrackTime()`.

**Gate PASS:** `make all` clean (no warnings from `ledHandler.c`/`.h`; the
`filesystem.c` and `libc_nano` notices are pre-existing). `text=573,160`
(+88 B), `data=416` (**+4 B reported** for 2 B of new data — the linker
script's `.data` `ALIGN(4)` rounds the section end), `bss=429,104` (unchanged).
Flash payload 573,576 / 753,664 B (headroom 180,088 B). `arm-none-eabi-nm -S`
shows `led_barPulseTrack` and `led_barPulseBar` at 1 B each, at
0x20020C35/0x20020C36 — **SRAM1**, owner ledHandler, static runtime-only, as
the approved Q4 accounting says (2 B).

**Host check (scratch, `/private/tmp/s081_p4_timeReached.py`, not committed):
PASS.** `led_timeReached()` copied 1:1, driven for pulse (`start + 50`) and
flash (`start + 400`, phase step 80 ms) deadlines, for start ticks
65,000..65,535 and 0..1,000 (1,537 starts).

- new comparison: every pulse ends after exactly **51** ticks (0 bad); every
  flash ends after exactly **401** ticks with **4** phase steps (0 bad);
- the old `now > deadline` test ended the pulse early for **50** starts
  (65,486..65,535), confirming the defect the fix removes.

### Documentation at close — DONE

- **`MODULE_INTERCHANGE_SPEC.md`** `Core/Hardware/frontPanel/ledHandler`: the
  `led_tickHandler()` row notes the signed-distance (wrap-safe) deadline test,
  and a **VOICE-mode bar pulse (S081 P4)** paragraph documents the pulse rules
  and the 2 B of state.
- **`STORAGE_SRAM_MANIFEST.md`**: a "Session 081 changes" ledger bullet
  (P3 no static RAM; P4 +2 B SRAM1 `.data`, reported +4 B by `ALIGN(4)`), and a
  §8.2 resident-owner row for the bar-pulse state.
- **`SCOPING_TARGETS.md`**: §4.11 notes the pulse/flash wrap defect closed in
  S081 P4 (with the bar pulse), and the Accomplished section gained an S081
  bullet (P3/P4) with hardware verification pending.

## Status

| Step | State |
|---|---|
| LH-1..LH-4 (wrap-safe expiry) | Done; clean build; host check 0 bad |
| LH-5..LH-8 (bar pulse) | Done; clean build; `nm -S` shows 1 B each |
| Hardware list | Pending (user) |
| Documentation at close | Done |

Final build: `text=573,160`, `data=416`, `bss=429,104`; flash payload
573,576 / 753,664 B (headroom 180,088 B). New static RAM: 2 B SRAM1
(ledHandler), as approved.

---

## Assessment (2026-10-16)

**Verdict: implementation conforms to the schedule. No functional defect
found.** One RAM accounting point needs your decision (B-1).

### What was checked

- **Build:** `ledHandler.c` forced to rebuild; `make all` clean.
  - Sizes: `text=573,160`, `data=416`, `bss=429,104`.
  - Flash: 573,576 / 753,664 B.
- **Step by step against the code:**

| Step | Code | Result |
|---|---|---|
| LH-1, LH-2 | pulse/flash slot comments | as scheduled |
| LH-3 | `led_timeReached()` (`ledHandler.c:977`); pulse (`:1007`), flash end (`:1019`) and flash phase (`:1023`) use it; blink and fast-blink tests untouched | as scheduled |
| LH-4 | `ledHandler.h:125` comment | as scheduled |
| LH-5 | `led_barPulseTrack/Bar` and `led_updateVoiceBarPulse()` (`:1597-1669`) identical to the schedule | as scheduled |
| LH-6 | called in the CHASE branch after the STEP chaselight (`:1840`), every mode | as scheduled |
| LH-7, LH-8 | `ledHandler.h:279`, `:205`; plus the `SeqLedState.chaseStep` note (`:227`) | as scheduled |

- **Rules against plan §1/§4:** VOICE-mode gate, effective length > 16,
  play mode ≠ 3 (`rnd`), same-track comparison, bar ≠ previous bar, and
  forgetting the bar when the transport is stopped are all present. The
  pulse uses `led_pulseLed()` and never writes base state, so the VOICE
  sub-page row is untouched (spec 2).
- **Context:** `led_tickHandler()` and `led_processSeqLedState()` both run
  in the main loop, so starting a pulse and expiring it cannot overlap.
- **Expiry fix:** the implementer's host check had 0 bad cases for pulses
  (51 ticks) and flashes (401 ticks, 4 phase steps). The old compare lost
  50 start values (65,486..65,535).

### Findings

- **B-1 (RAM accounting, decision needed):** the 2 approved bytes cost
  **4 B of RAM**.
  - `led_barPulseTrack` and `led_barPulseBar` are initialised to `0xFF`, so
    they live in `.data`. The linker script rounds `.data` to 4 bytes, so it
    grew from 412 to 416 B: the 2 B of state plus 2 B of padding, in RAM and
    in the flash init image.
  - Options:
    1. acknowledge 4 B;
    2. initialise both to 0, storing track + 1 and bar + 1 so that 0 means
       "none". They then move to `.bss`, where the end padding may absorb
       them (to be measured). No behaviour change.
  - **Recommendation: option 2.**
- **B-2 (low, not reachable from the buttons):** if a transport stop and a
  start both happen before the next LED drain (about 1–2 ms apart), the
  stop is not seen. The first step after the start can then pulse once if
  it is in a different bar from the last step before the stop. Only an
  external MIDI Stop immediately followed by Start could do this.
- **B-3 (note):** with P3 in place, the snapshot in
  `led_updateVoiceBarPulse()` is 40 B (P3 loop fields), not 28 B. It is a
  stack local in the main-loop drain and not nested in another snapshot
  frame.

### Hardware focus

- Rows 1–9 of the verification list.
- Row 10: a few minutes of playback across 65.5 s clock wraps. Copy, step
  and REC pulses and SELECT/SEQ flashes should look the same near a wrap as
  elsewhere.
