# S082 P2 — Load/Save follow-up: audio during a background Bank load

*Opened 2026-10-19 from a hardware report on the S082 P1 build (scroll fix
included). Assessment only: no code has changed. Card copy:
`SD_CARD_082_P1/`.*

---

## 1. Report (user, 2026-10-19)

A Bank load replaced the playing Scene, so playback moved to Scene 17.

1. **The playing Scene got noticeably louder while the load was running.**
   There must be no audible change at all after the switch to Scene 17
   (`S082_P1` D4, D5).
2. **Each Scene of the load was extremely slow.**
3. **There was an audio glitch when the load finished**, although playback
   had been on Scene 17 since the load started.

---

## 2. What the card shows

| Item | Finding |
|------|---------|
| Image | `LXRV2_lxr02.img` 595,572 B = the scroll-fix build (595,556 B + 16 B header). The report is against current code. |
| Result | `settings.cfg` `active_bank=0`; `.hcnames` row 0 = `FluxTime 000`. The load completed and Bank 000 is resident. |
| Trace coverage of the load | **None.** The last session (#057377 on) ends with budget (`H`) reports up to tick 29900. The trace flush is suppressed while an accepted Load/Save command runs (`filesystem_autosaveTraceFlushSchedule_tick()`), and no flush happened between the end of the load and power-off. The runtime Bank Load also writes no per-child trace records. **Every finding below comes from the code, not the trace.** |
| Scroll fix | Working. Each cancelled preview ends as an `E` record with op `0x2b`, `FS_INTERNAL_OP_SCAN_BANK_SCENES`, at phases 5, 10 and 16 (the scan's close phases), followed by a fresh `b` preview for the slot the user stopped on. The decoder labels `0x2b` as `FS_INTERNAL_OP_REPAIR_NAMES`; its op table is out of date (P2-5). |
| Errors / stalls | No `X` stall records and no other operation errors in the session. |

---

## 3. Findings

### P2-1 — The Effect keeps reading the home Scene after the switch (**High, confirmed in code**; symptom 1)

**Mechanism:**
- EffectsManager keeps its own source Scene, `effects_state.scene_index`, set only by `effects_activateScene()` (`EffectsManager.c:1713-1722`).
- `effects_service()` runs inside the mixer once per render block (`mixer.c:893`). Every block it reads `scene_effectConst(effects_state.scene_index)` (`EffectsManager.c:1807-1810`) and applies:
  - the Effect Morph amount;
  - the FX sequence step;
  - **`EFFECT_COMMON_PARAM_LEVEL` (FX return level)**, route and pan;
  - every type parameter (`EffectsManager.c:1884-1892`).
- The Scene 17 switch runs no Scene activation, by design (`S082_P1` §4.3, C6: "the runtime already holds exactly these images"). `seq_switchToBackgroundScene()` (`sequencer.c:2288`) and the phase 3 Menu steps (`presetManager.c:3858`) never call into EffectsManager. **`effects_state.scene_index` therefore still names the home Scene for the whole time Scene 17 plays.**
- Until the home child is loaded this is inaudible, because Scene 17's Effect record is an exact copy. When the Bank load commits the new child into the home slot, `filesystem_commitSceneStage()` copies the staged Effect over `scenes[home].effect` (`filesystem.c:17619-17650`). From the next block the **sounding** Effect takes the **new Bank's** level, parameters, Morph amount and FX sequence.
- A Replace that empties the home Scene resets it to default Effect values, with the same effect.
- If the new record's type differs from the running type, the old type's descriptors are fed the new type's parameter values (clamped only), which gives arbitrary settings.

**Why it reads as "louder during the load":** the change happens at the home child's commit, part-way through the load. The FX return level and type parameters jump to another Scene's values.

**Why the S082 audit missed it:**
- The §5 audit and Appendix A follow reads of `scene_getActiveIndex()`, `seq_activePattern` and the played-Scene functions.
- EffectsManager reads none of them per block; it uses a **cached index**.

**Other runtime modules checked for the same pattern:**
- **Mixer:** fader and send resolve through `preset_getSlotPlayedScene()`, which gives 16.
- **Bus compressor:** reads `scene_getActiveIndex()`, which gives 16 (`mixer.c:1099`).
- **Morph engine:** its runtime writes are guarded to the active Scene (`presetMorphEngine.c:498`).
- **Scene apply worker:** drops itself when the active Scene changes (`presetManager.c:2208`).
- **Single-voice worker:** resolves the played Scene at commit time.
- **Modulation bindings:** point into runtime instance memory (`InstrumentManager.c:2350-2375`).

**EffectsManager is the only cached-index consumer found.**

**Fix (proposed):**
- Add `effects_rebindSourceScene(uint8_t scene_index)` to EffectsManager. It sets `effects_state.scene_index` only:
  - no `effects_switchRuntime()`;
  - no handoff refresh;
  - no `force_all`;
  - no reset of the sequence clock, automation or LFO state.
- Because Scene 17's record is identical, `effects_service()` then computes the same values, `last_applied[]` matches, and nothing is written to the runtime.
- Call it in phase 3 after `seq_switchToBackgroundScene()` (fresh build only; a rebuild keeps Scene 17's Scene-level state, D19).
- The release (a whole-Scene change away from 17) already re-activates through the normal path.
- Add a "cached Scene index" rule to the `S082_P1` §5 audit.

### P2-2 — The end-of-load burst underruns the audio (**High, probable**; symptom 3)

**Mechanism:**
- When the load finishes, `on_bank_load_complete()` (`presetManager.c` ~650-690) runs inside one `filesystem_tick()` callback and calls `autosave_markSceneWithPatternDirty()` for **every loaded child**, all 16 on a Replace.
- Each call marks every implemented payload byte of the Scene one at a time:
  - 76 Scene parameters;
  - the Effect type bytes and 419 Effect parameters;
  - the Kit parameters;
  - six whole Instruments (`Autosave.c:2201-2322`).
- That is roughly 1,300–1,500 marks per Scene, so **about 20,000+ marks in one foreground call**. Each mark (`autosave_markPayloadOffsetDirty()`, `Autosave.c:410-434`):
  - does a mask OR under an IRQ save/restore;
  - in DEV builds, also writes one `D` trace record.
- A Merge load then repeats the whole-Bank marking in the Menu completion (`autosave_markResidentBankDirty()`).
- **Estimated cost: several milliseconds of foreground time.** The audio is rendered in the same foreground loop (`main.c:1386-1440`, `audio_check_and_render()` between every service call). Each render slot is one 96-frame buffer, 2.18 ms (`config.h:795`), built from 32-frame DSP blocks, and the queue holds two slots (`AudioCodecManager.c:330-333`). That leaves roughly 2–4 ms of headroom after a full queue, and less when a render is due. **A multi-millisecond call underruns the output: the glitch at the end.** *(Corrected 2026-10-19: an earlier draft stated 32-frame slots and 1.5 ms of headroom.)*
- **Why it was never heard before:** a stopped Bank load runs with the codec suspended (S058 no-playback storage), so the same burst is silent. Scene 17 is the first case where this completion runs with audio playing.

**Confirm first:**
- `audioCodec_underrunCount` exists (`AudioCodecManager.c:256`); record its value at load start and at load end.
- Time `on_bank_load_complete()` with the existing tick, or with DWT cycles in a DEV build.

**Fix (proposed; choose after measuring):**
- **(a) Range marking.** Mark a whole Scene by OR-ing its contiguous mask range (one 1,920-byte Scene section = 240 mask bytes), in one short IRQ section, with one `L` witness instead of per-byte `D` records. *Check first:* the per-byte path marks only implemented, gettable bytes. Range marking would also mark reserved bytes, so the drain must treat a non-gettable dirty byte as harmless (it captures a stable value) or the range must exclude them.
- **(b) Spread the marks over passes.** Keep per-byte marking, but let a small cursor mark one Scene (or one scope) per foreground pass after the completion. No new RAM beyond the cursor (a mask of pending Scenes, 2 B), which needs approval under the RAM policy.
- In both cases, also check the other completion work while audio plays:
  - the HCNAMES and index rewrites;
  - `bank_mergeVoiceEditMasksAfterLoad()`;
  - `bank_revalidateVoiceEditMasks()`.

  Each must stay well under one audio block per call.

### P2-3 — Loads are slow while audio plays (**Medium, structural**; symptom 2)

**Mechanism:**
- The main loop calls `filesystem_tick()` **once per pass**, between 16 calls of `audio_check_and_render()` (`main.c:1386-1440`).
- Each tick does one bounded step: one line, one 160–512 B read, or one open/close request.
- With the transport stopped, S058 suspends the codec, the loop spins, and the steps run back to back.
- With audio playing, every pass spends most of its time rendering. A Bank child is around 600+ steps: `sceneset.scg`, `kitset.kcg`, six Instruments of about 70 lines each, a 10.6 kB Pattern and the Effect file. So each child takes roughly a second, and a 16-child Bank 10–20 s.
- This is the expected cost of the current scheduler, not a Scene 17 defect, but it makes D1's "immediate" load feel broken.

**Fix (proposed):**
- While a background Bank load runs, give the filesystem the idle time between renders. After `filesystem_tick()` in the main loop:
  - keep calling `filesystem_tick()` while `audioCodec_queueFreeSlots() == 0` and the facade is BUSY;
  - bound it by a short time budget, for example 150–250 µs measured on `timebase_tim2Now()`, and stop as soon as a render slot frees.
- Audio always renders first, and the filesystem only uses time that is otherwise spent spinning.
- Requires each filesystem phase to stay bounded. This is already the design rule; the F4 stall observers cover the waits.
- It is a foreground scheduling change, not a DSP change, so the DSP CPU policy is not touched. It does raise main-loop variance for the other foreground services, so limit it to accepted Bank load commands.

**Measure first:** per-child time with a new DEV trace record (P2-5).

### P2-4 — The switch must be verified against everything Scene-level (**Medium, process**)

D4 ("sound identical") was verified by reasoning in Phase C, not by test. Before the next hardware run, compare each Scene-level runtime consumer against Scene 17's record:
- Effect (P2-1);
- bus compressor;
- global and voice Morph;
- fader and send;
- routing;
- LFO and velocity bindings;
- FX sequence clock;
- automation latches.

A DEV-only self-check helps. At the switch, and again at load end, compare `effects_state.scene_index`, the bus compressor scene and the morph worker scene with `scene_getActiveIndex()`, and record a mismatch.

### P2-5 — No evidence path for runtime Bank loads (**Low, tooling**)

- **Trace records:** add DEV records for runtime Bank Load at:
  - request;
  - the Scene 17 switch, with `effects_state.scene_index`;
  - each child commit, with the tick;
  - completion;
  - `audioCodec_underrunCount` at start and end.
- **Trace flush:** these must survive the suppressed flush. The existing `TRACE_SUPPRESSED` witness is only one record, so recommend a final flush right after the command ends, which is the normal path; the user powered off before it ran.
- **Decoder:** update the `tools/decode_devlogs.py` op table; `0x2b` is now `FS_INTERNAL_OP_SCAN_BANK_SCENES`.

---

## 4. Recommended order

1. **P2-1:** the Effect source rebind at the switch. It is small, it fixes symptom 1, and it is required by D4.
2. **P2-5** instrumentation, then measure P2-2 and P2-3 on hardware.
3. **P2-2**, (a) or (b) per the measurement.
4. **P2-3:** idle-time filesystem servicing during background loads.
5. **P2-4:** the switch self-check and a full D4 listening test: no audible change at the switch, during the load, or at completion, with each Effect type, the bus compressor on, Morph off-centre and per-track overrides.

---

## 5. Questions for the user

| Id | Question | Recommendation |
|----|----------|----------------|
| Q1 | P2-2: mark whole Scenes by range (fast, needs the reserved-byte check) or spread per-byte marks over passes (needs a 2 B cursor, RAM approval)? | Range marking, if the drain tolerates non-gettable dirty bytes; otherwise spread. |
| Q2 | P2-3: allow the filesystem to use idle main-loop time between audio renders during a background Bank load? | Yes, limited to accepted Bank load commands, with a 200 µs budget. |
| Q3 | P2-5: add the DEV trace records (no production RAM; DEV-only ring use)? | Yes. |
