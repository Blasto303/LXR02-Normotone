# Dynamic Pattern Stack Specification

## Authority and status

This is the authoritative live-memory, allocator, PAT4 interchange, Pattern
Stack Service, and Pattern AutoSave reference, **current as of the Session 080
close** (2026-10-11, `dev-ph6-cleanup`). Session 073 changed
nothing. Session 074 changed no Pattern storage or allocator code; it added a
second reader of step automation (the Effect-page underline search reads every
step of every track of the viewed Pattern through
`pat_readStepAutomations()`, 4 steps per foreground pass) and saw a torn PAT4
file repair itself exactly as designed (§9).

**Session 080 changed this area in five ways** (record:
`080_SESSION_HANDOFF_LOG.md`; PERF UI side: `MODULE_INTERCHANGE_SPEC.md`
"PERF performance surface"):

- **Trigger conditions** (§6): `lop` (23) / `!lp` (24) inserted after `!fl`;
  the table is 41 entries; `lst`/`!ls` move to 25/26 and the mute groups to
  27..33 / 34..40. No PAT4 migration (stored values ≥ 23 shift meaning).
- **PERF looper** (§6.5): a master-clock-anchored per-track loop engine in the
  TIM3 scheduler, driven by the PERF SELECT/VOICE/BAR buttons.
- **Scene change timing, queue and chaining** (§6.6): direct and queued
  scene changes at musical divisions, queue actions, the one PC reset, and
  per-Scene condition-driven chaining evaluated in TIM3.
- **Per-track looper exclusion** (§1, §7): `track_loop_exclude` region byte
  (bit N = track N not loopable), stored in PAT4 track header byte 4 (was
  reserved zero; PAT4 version unchanged). Region 10,526 → **10,527 B**.
- **Cursor reset no longer ends the looper** (§6.5): `seq_setStepIndexToStart()`
  rebases an engaged loop instead of clearing it; transport stop still ends
  it.

**Session 079 changed this area in five ways:**

- **7-special pool block format** (§4): expanded from 3 specials
  (note/velocity/probability) to 7 (note/velocity/condition/roll/timing/
  rhythm/fill) using flag bits 0–6. `PAT_SPECIAL_PROB_BIT` renamed to
  `PAT_SPECIAL_COND_BIT`; four new bit definitions. `PAT_SPECIAL_FLAGS_MASK`
  = `0x7Fu`. `pat_step_specials_t` widened to 8 fields (7 values + flags).
  `PAT_RAW_BLOCK_MAX` 132 → 136 (34 chunks). `pat_rawEncode()` signature
  widened from 3 positional values to `const pat_step_specials_t *`. Default
  fill = 100 (bit cleared when value == 100).
- **Swap block** (§3): `PAT_POOL_SWAP_CHUNKS` 33 → 34 (136 B) to match the
  wider maximum block.
- **Trigger condition system** (§6): probability replaced by a 39-entry
  condition dispatch (10 probability, 10 repeat-count X:Y, fill/!fill,
  last/!last, 14 mute/!mute across 7 tracks); self-mute bypass on muted
  tracks; `seq_trackRepeatCount[7]`, `seq_trackLastConditionFired[7]`,
  `seq_fillActive`.
- **Per-step roll engine** (§6): `seq_rollBegin()`, `seq_processRollTicks()`,
  `seq_rollComputeVelocity()` — 8 roll rates (absolute note values), packed
  rhythm byte encoding (21 ordered entries), fill percentage with ~5 ms
  output-gain duck via `instrumentManager_requestGateDuck()` /
  `instrumentManager_gateGain()` in the mixer. Carry-over across matching
  steps. Roll + duck state reset at all four sequencer reset sites.
- **PAT4 version** (§7): bumped from 1 to 2; reader rejects version 1.
  Offline converter `tools/convert_pattern_specials.py`.

**Session 078 changed this area in four ways:**

- **Per-track timing is now played** (§6.4): a continuous 128-position step
  scale with per-track Q8.8 DDA accumulators, per-track shuffle on the 1/16
  grid, and per-track run modes (`track_play_mode[7]`, §1, PAT4 track header
  byte 3, §7). The sequencer reads an effective (morphed/automated) cache, never
  the region directly, and never writes the region for timing.
- **Track timing Morph and automation** (§6.1, §6.4): Scene targets
  405..425 (length/scale/shuffle per track) are runtime-only overlays; Morph
  endpoints live in Scene settings (`BANK_PRESET_ARCHITECTURE.md`).
- **STEP-page editing** (§6.3): held SEQ steps broadcast velocity/note/
  probability; held steps + TRACK opens a held-step track-automation overlay;
  the STEP track-settings page has a Pattern-wide automation underline search
  and SHIFT+COPY pot clears.
- **Region size:** 10,519 → **10,526 B** (+7 B `track_play_mode`).

**Session 077 changed this area in two ways:**

- **`pat_background_region`** (§1): the standalone `pat_autosave_snapshot` is
  now `pat_background_region`, an identical-type 10,519 B region outside
  `pat_regions[]`. It serves the AutoSave snapshot, the copy/clear overlap
  paste pool (`pat_backgroundPoolMut()`), and future Bank Load staging. These
  three uses must never run concurrently. The copy/clear overlap path now
  waits on `filesystem_patternSnapshotInUse()` instead of borrowing name-buffer
  space, and the paste source table is a static `uint16_t[128]` in
  `copyClearService.c` instead of being stored in the name buffer.
- **Per-track Scene playback** (§6): the sequencer reads
  `seq_perTrackPattern[track]` instead of `seq_activePattern` in all ISR/
  foreground step-advance, automation, and trigger paths, enabling independent
  track-to-Scene mapping during live performance.

**Session 075 (Phase 6) changed this area in three ways:**

- **Copy/clear** (`Core/Menu/CopyClear/`, reference `COPYCLEAR_UTILITIES.md`)
  replaced the Session 062 no-op copy APIs. It adds a permanent 132 B swap
  block to every pool (§3), an exclusive per-Scene write boundary for any
  resident Scene, a raw block API and sliding compaction (§12.17).
- **Automation priority** (§6.2a): a held step-automation value is never
  overwritten before the voice's next trigger; menu edits apply only their
  own parameter's interpolation; external MIDI is lowest priority.
- **Documentation corrections** found while closing S075: the block header is
  big-endian (§4), the pending queue is a 128-record append queue (§6.1), and
  the drain writes voice values without 7→8-bit expansion (§6.1).

Historical Session 062/063/064 plans describe how the design was reached but
do not override this file.
Filesystem hierarchy and HCNAMES grammar are in `FILESYSTEM_SPEC.md`; scalar
and Pattern AutoSave scheduling/recovery are in `AUTOSAVE.md`; exact linked
memory totals are in `STORAGE_SRAM_MANIFEST.md`.

Implemented and hardware accepted:

- 16 independent resident Scene Patterns;
- 7 tracks × 128 steps per Scene;
- trigger state plus dynamic note, velocity, condition, roll, timing,
  rhythm, and fill specials (7 specials since S079; was 3 before);
- per-step automation entries (2-byte LE, 7-bit value + 9-bit target,
  up to 63 per step) with uniqueness invariant and dtype-aware editing;
- first-fit bit-packed pool allocator and block reclamation;
- Pattern/track settings storage, Menu edit, and PAT4 persistence; Sequencer
  condition playback (39-entry dispatch since S079, 41 since S080); per-track step-length playback (Session 068) and,
  since Session 078, per-track continuous step scale, shuffle and run modes
  with Morph and step automation (§6.4);
- sequencer automation playback: TIM3 copies to 32-entry debounced
  pending buffer, foreground drain via `instrumentManager_writeRuntime()`,
  per-slot dirty bitmap with morph-interpolation restore on voice trigger;
- step-edit automation pages (Method 1): cursor navigation, detail views,
  add/delete/clear, parameter cycling with uniqueness, dtype-aware display;
- exact binary PAT4 Scene/Bank/root Pattern Load and Save;
- scene-mask Pattern Load fan-out;
- per-Scene hidden A/B Pattern AutoSave and boot restore;
- HCNAMES Pattern identity rows 129..144;
- Phase 6 copy and clear of steps, ranges, bars, tracks, automation and whole
  Patterns (Session 075; partly hardware-tested by the user, see
  `COPYCLEAR_UTILITIES.md` §16).

Not implemented: live-record capture of automation, and real-time editing
guarantees while a snapshot is admitted during record/erase (admission is
instead deferred while those modes are active). Allocator
compaction/defragmentation is implemented via the Pattern Stack Service
(Session 067, see §12) and, for the copy/clear holder, as sliding compaction
(§12.17).

## 1. Resident object

`PatternData` owns:

```c
typedef struct __attribute__((packed)) {
    uint16_t address[7][128];       /* 1,792 B */
    uint8_t  pool[256 * 32];        /* 8,192 B */
    uint8_t  bitmap[512];           /* 512 B */
    uint8_t  track_length[7];       /* 7 B */
    uint8_t  track_scale[7];        /* 7 B */
    uint8_t  track_shuffle[7];      /* 7 B */
    uint8_t  track_play_mode[7];    /* 7 B, S078: 0 fwd .. 5 1fr */
    uint8_t  track_loop_exclude;    /* 1 B, S080: bit N = track N not loopable */
    uint8_t  pattern_change_bar;    /* 1 B */
    uint8_t  pattern_next;          /* 1 B */
} pat_scene_region_t;               /* 10,527 B */
```

The header `_Static_assert` (and its copy in PatternData.c) pins the size as
address + pool + bitmap + 31 (the parameter tail: four 7-byte track arrays,
the S080 loop-exclude mask and 2 Pattern bytes; 23 before S078, 30 before
S080). `track_loop_exclude` is stored inverted so a zero-filled region or an
older PAT4 file means "every track loopable"; accessors
`pat_setTrackLoopable(scene, track, loopable)` (marks the Pattern dirty only
on change) and `pat_trackLoopable(scene, track)` (1 for invalid
coordinates); `pat_applyTrackSettingsToMenu()` mirrors the active track into
`PAR_TRACK_LOOPABLE`; `pat_rawRegionCopyBody()` copies it; copy/clear track
copy carries it and track clear sets loopable again. `pattern_change_bar` /
`pattern_next` are retired (no playback use) but still persisted. `track_scale` holds a step-scale **CC 0..127**
(S078; 76 = 1/16, §6.4), not the pre-S078 14-entry index.
`track_play_mode` values ≥ 6 are treated as `fwd`.

`pat_regions[16]` is exactly 168,432 bytes (S080). Pattern storage is not
embedded in `scene_t`; `scenes[16]` is a separate 26,496-byte SceneData object
(S080: 1,656 B per Scene after the +4 B Scene chain settings; S078 had added
the 21 B track Morph endpoints).
`pat_sceneRegion(scene)` returns
read-only access and `pat_sceneRegionMut(scene)` is reserved for bounded owner
paths such as validated filesystem application. Ordinary clients use the
public operations so mutation tracking cannot be bypassed.

One additional `pat_background_region` is exactly 10,527 bytes (S077; it
replaces the former standalone `pat_autosave_snapshot` at identical size, and
grew with the region type in S078 and S080). It
is not a playable Scene: every Scene-indexed bound (scene_indexValid(),
pat_patternValid(), the sequencer, the UI, PERF and Bank) stays at
`SCENE_COUNT` (16), and it is reached only through its accessors. It serves
three purposes that must never run concurrently: the immutable source for an
in-flight Pattern AutoSave write (`pat_snapshotScene()` /
`pat_autosaveSnapshot()`), the copy/clear scratch pool for an overlapping
paste (`pat_backgroundPoolMut()`; the pool is a full PAT_STACK_SIZE * 32
bytes, so it always fits what the source pool could hold), and future Bank
Load staging. There is no snapshot per Scene and no PAT4-sized filesystem
stage.

## 2. Address encoding

Every track/step owns one little-endian native `uint16_t`:

```text
bit 15       trigger active
bit 14       dynamic specials block exists
bits 13..0   4-byte-aligned byte offset into the Scene pool
0x3fff       no-data sentinel (never a valid aligned pool offset)
```

Turning a step off preserves bits 14..0, so an existing specials block can
survive off/on. Erasing a step, clearing a range, or initializing the Scene
frees any owned block and restores `PAT_ADDR_SENTINEL`.

An address is valid only if the offset is aligned, within the 8,192-byte pool,
the referenced chunks are occupied, the stored back-reference identifies the
same track/step, the special flags are known, and the block fits before the
pool end.

## 3. Pool and bitmap geometry

`PAT_STACK_SIZE == 256` is a historical sizing unit, not the number of
allocatable chunks. It creates an 8,192-byte pool (`256 × 32`). Allocation is
in 4-byte chunks, so the pool contains 2,048 chunks.

**Swap block (S075, widened S079).** The top 34 chunks (136 B, bytes
8,056..8,191, `PAT_POOL_SWAP_OFFSET`) of every Scene's pool are permanently
reserved: normal allocation (`pat_poolAlloc()`), service searches, repair and
reservations stop at `PAT_POOL_ALLOC_CHUNKS` (2,014 chunks, 8,056 B usable).
The block holds one maximum dynamic block (7 specials + 63 automations) and
is a guaranteed rewrite area: a step can be
republished without free pool space while its old block is still live.
Copy/clear is its first user; the reserve is kept for any later feature that
needs the same guarantee. PAT4 is unchanged (the reserve is free space);
Patterns saved before S075 may have blocks there, which copy/clear moves
below the reserve before first use (§12.17).

The 512-byte bitmap is bit-packed: bit `chunk & 7` of byte `chunk >> 3` owns
one 4-byte chunk. Only its first 256 bytes correspond to the 2,048 backed
chunks. Initialization clears those 256 bytes and fills the upper 256 bytes
with `0xff`, permanently reserving the unbacked range. Do not treat the bitmap
as one byte per chunk or limit the live pool to 1,024 bytes.

Allocation is deterministic first-fit linear search. Blocks are contiguous;
free clears exactly the occupied span. Allocation failure leaves the old step
block/value intact. The Pattern Stack Service (§12) provides two-tier
maintenance: Tier 1 trailing-gap merge after frees, and Tier 2 paced
compaction that relocates live blocks toward pool start.

## 4. Dynamic block format

A block starts at a 4-byte-aligned pool offset:

```text
bytes 0..1  big-endian header (byte 0 = bits 15..8, byte 1 = bits 7..0)
             bits 15..6: track * 128 + step (10-bit back-reference)
             bits 5..0: automation count (0..63), so byte 1 & 0x3F
byte 2      special flags: bit0 note, bit1 velocity, bit2 condition,
            bit3 roll, bit4 timing, bit5 rhythm, bit6 fill
bytes 3..   present values in ascending flag-bit order:
            note, velocity, condition, roll, timing, rhythm, fill
next bytes  automation entries, 2 bytes each, auto_count entries:
             each entry little-endian: bits 15..9 = 7-bit value (0..127),
             bits 8..0 = 9-bit instrument_param_id_t target
padding     zero to a 4-byte boundary
```

Block size: `chunks = (2 + 1 + popcount(flags & 0x7F) + auto_count * 2 + 3) / 4`.
One special with no automation is one 4-byte chunk. Seven specials with no
automation is three chunks (10 bytes padded to 12). Seven specials with 63
automations is 34 chunks (136 bytes). An unknown flag bit (bit 7) or an
inconsistent back-reference makes a block invalid.

Each automation entry's 9-bit target is the canonical `instrument_param_id_t`:
`slot * INSTRUMENT_PARAM_COUNT + descriptor_index` for voice parameters
(IDs 0..383), a Scene target ID (384+; since S078 including the track
timing targets 405..411 length, 412..418 scale, 419..425 shuffle, one per
track), or an Effect parameter ID 448..510
(block 7, Session 072 step 9). A step must never contain two
entries with the same 9-bit target (uniqueness invariant, enforced at write
time). The 7-bit value is an identity mapping: stored value = parameter value.
Every automatable descriptor parameter has a range that fits in 7 bits
(DTYPE_0B127, DTYPE_PM63, DTYPE_MENU, DTYPE_ON_OFF, DTYPE_MIX_FM,
DTYPE_LFO_POLARITY, DTYPE_NOTE_NAME, DTYPE_1B16). No DTYPE_0B255 parameter
is currently automatable. If a DTYPE_0B255 parameter becomes automatable in
the future, its conversion must be handled as a dtype-conditional special case.

Default resolved values when a special is absent are: note 63, velocity 100,
condition 0 (always fire), roll 0 (off), timing 0 (none), rhythm 0 (off),
fill 100 (full step). `pat_readStepSpecials()` always returns usable values
plus flags saying which were explicitly stored.

Writers use read-modify-write semantics with publication-safe ordering
(Session 067): allocate new block, write complete content, PRIMASK-protect the
address-entry swap (re-read trigger bit to avoid race with TIM3 step advance),
then free old block. In-place rewrite of same-size blocks is not used: even a
removal can compact automation bytes, creating a window where TIM3 reads
partially updated content. Clearing the last special removes the block and
bit 14 only when no automation entries remain; a block with zero specials but
nonzero automation count is preserved (flags byte = 0, no special values
stored, automation entries follow immediately). Erase and clear paths detach
the address entry before freeing the pool block.

## 5. Public behavior

Core operations validate Scene/track/step coordinates and do no work for an
invalid coordinate:

- `pat_isStepActive`, `pat_setStepActive`, `pat_toggleStep`, `pat_eraseStep`
  (Session 068: `buttonHandler_setRemoveStep()` emits a `DEV_MODE_LOGGING`-
  gated `K` witness record, via `AutosaveTrace.h`/`autosaveTrace_record()`,
  immediately before calling `pat_toggleStep()` — packs track/absolute
  step/pattern/pre-toggle trigger state, so a future report can distinguish
  front-panel input delivery failing to reach this call from a failure in
  the mutation itself. `K` is an AutoSaveTrace stage code, not a
  PatternTrace one — see `DEV_MODES.md` and note the two trace systems share
  some letters with different meanings);
- `pat_clearTrack`, `pat_clearPattern`;
- `pat_readStepSpecials` and the note/velocity/condition/roll/timing/
  rhythm/fill setters;
- `pat_readStepAutomations` — read decoded entries for one step (returns
  count);
- `pat_writeStepAutomation` — add or update one entry (uniqueness enforced,
  returns 1 on success, 0 on pool exhaustion or 63-entry ceiling);
- `pat_removeStepAutomation` — remove one entry by 9-bit target (returns 1
  if found);
- `pat_removeTrackAutomationByTarget` — remove all entries with a given
  target from all 128 steps of a track (returns count removed);
- `pat_stepAutomationCount` — count entries without reading them;
- track length/scale/shuffle and Pattern change-bar/next setters;
- menu apply helpers for Pattern, track, and selected-step state.

`pat_writeSpecials` preserves existing automation entries across
specials-only edits. `pat_eraseStep` and `pat_clearTrack` include automation
entries in block-size calculations when freeing pool chunks.

The Session 062 no-op copy surface (`pat_copyTrack`, `pat_copyPattern`,
`pat_copyBar`) was removed in S075. Copies are made by the copy/clear service
(`Core/Menu/CopyClear/`) through the raw block API (§12.17), which duplicates
pool blocks and never aliases one Scene's or step's allocation from another.

All real Pattern mutations converge on PatternData's local dirty helper. It
invalidates the Bank clean-Scene witness and calls
`autosave_markPatternDirty(scene)`. Direct mutable-region clients must provide
equivalent complete-operation marking or they violate AutoSave ownership.

Since Session 067, all pool-mutating operations from Menu, Sequencer,
and EuklidGenerator route through the Pattern Stack Service
(`patSvc_*` API) rather than calling `pat_*` mutation functions directly. The
service guarantees exactly one mutation target at a time and serializes all
pool access. See §12. The S075 copy/clear service is the one exception: it
writes through the raw block API only while it holds the service's exclusive
boundary (§12.17).

## 6. Sequencer and menu integration

The Sequencer obtains trigger/special state through PatternData. For a
triggered step it evaluates the trigger condition via
`seq_evaluateStepCondition()` — a 41-entry dispatch table (S080) covering
probability (r10–r95 via hardware RNG, 1..10), repeat-count X:Y conditions
(11..20, `seq_trackRepeatCount[7]`, modulo comparison), fill/!fill (21/22,
`seq_fillActive`, armed by PERF hold past `DOUBLE_CLICK_TIMEOUT`),
loop/!loop (23/24 `lop`/`!lp`, S080: `seq_loopActive`, §6.5), last/!last
chaining (25/26, `seq_trackLastConditionFired[7]`), and mute/!mute (27..33
`1mu..7mu`, 34..40 `!1m..!7m`; dispatch `(cond − 27) % 7`, boundary
`cond <= 33`) across all 7 tracks (with self-mute bypass: a step on a muted track whose
condition tests its own track's mute state bypasses the mute gate). Condition
0 (default/absent) always passes. A failed condition suppresses both trigger
and automation. The sequencer then triggers with the resolved note and
velocity. If the step has a roll special, `seq_rollBegin()` starts a
per-step retrigger engine at the specified rate (8 absolute note values,
independent of track scale), with rhythm-modified velocity via the packed
byte pattern and a fill percentage that controls how much of the step
duration the roll occupies. Fill < 100 ducks the voice at the fill boundary
via `instrumentManager_requestGateDuck()`. If the step has a micro-timing
delay, the trigger is deferred by `(step_interval * timing_value) / 100`
ticks, additive with shuffle. Roll carry-over extends across adjacent steps
with matching rate/rhythm/fill.

### 6.1 Step automation playback

THE DEFAULT FOR ALL AUTOMATION IS THAT IT PLAYS AND AFFECTS SOUND UNTIL THE
NEXT VOICE TRIGGER. AUTOMATION ON A NON-TRIGGER STEP IS VALID AUDIO
AUTOMATION DATA, VALUE CHANGES BETWEEN NOTES, AND TIMBRAL MOTION INDEPENDENT
OF RHYTHM ARE A CRITICAL PRODUCT FEATURE. ANY CHANGE THAT CONDITIONS
AUTOMATION QUEUEING ON TRIGGER STATE (BIT 15) DESTROYS THIS CAPABILITY AND
IS UNCONDITIONALLY WRONG.

On each step advance, `seq_advanceTrackStep()` reads automation entries from
every step that has a pool block (bit 14 set, valid offset), regardless of
trigger state. Each entry (except the Pattern-only off target `0x1FF`) is
appended to the TIM3-to-foreground queue `seq_pending_automation[]`:
`SEQ_PENDING_BUF_COUNT` (128) four-byte records (identity = step id | type
bit, payload = the packed entry) plus two publication bytes (+514 B SRAM1).
There is no coalescing; a full queue drops the entry and records a
PatternTrace overflow witness (`H`). Effect step markers (identity bit 11)
share the queue. The ISR is the sole writer.

Trigger condition gates the complete step: trigger and automation together,
for any step that has a condition special set (value 1–40), whether or not the
step has an active trigger (bit 15). The condition is read from the dynamic
block before the trigger-active check so it applies to non-trigger steps with
automation. Default condition (0, or absent special) always passes — a step
without a condition plays unconditionally. A step whose condition fails
produces no trigger and no automation entries; previously held automation
values persist through the skipped step. Erase is independent of condition.

The foreground drain (`seq_drainPendingAutomation()`) runs inside
`audio_check_and_render()` immediately after `voiceControl_processPending()`,
within the per-chunk render loop. For each entry, it validates the target and
dispatches by target range. Values stay in descriptor space (identity
mapping, no MIDI-CC-style 7→8-bit expansion); only voice Morph Scene targets
expand to 0..255:

- **Voice descriptor targets (IDs 0..383):** calls
  `instrumentManager_writeRuntime(slot, descriptor, value)` and sets the
  corresponding bit in `seq_automation_dirty[slot]` (a `uint64_t` per-slot
  bitmap, 48 B total). That bit is the "automation holds this value" record
  (§6.2a).
- **Scene targets (IDs 384+, Session 070; `fxm` is ID 404):** calls
  `seq_applySceneAutomation()` which dispatches to runtime-only overlays —
  `presetMorph_setStepAutomationOverride()` for Voice Morph (with 7→8 bit
  expansion via `menu_morphAutomationExpand()`),
  `slot6_track7_decay_step_value` for generated slot-6 track-7 decay,
  `preset_applyVoiceAudioOutRuntime()` plus `preset_setAudioOutStepOverride()`
  for Audio Out routing, `preset_setFxSendStepOverride()` for FX Send (the
  mixer reads the effective send each block; the override beats the
  Normal/Morph send endpoints), `effects_setMorphAutomation()` for `fxm`, or
  (S078) `presetMorph_setTrackParamStepOverride()` for the track
  length/scale/shuffle targets 405..425, which also writes the sequencer's
  effective cache directly so the next DDA tick follows (§6.4). Sets the corresponding bit in
  `seq_scene_automation_dirty` (`uint32_t`, 4 B). Scene automation never
  writes retained Scene/Kit setters — this prevents AutoSave thrashing and
  preserves user-set values.
- **Effect targets (IDs 448..510, Session 072 step 9):** the foreground drain
  brackets entries with FX step markers (identity bit 11) and calls
  `effects_applyAutomation(track, local, value7)`. EffectsManager owns the
  value, owner track, and pending-end mask. An overlay ends when its writing
  track advances to an automation step that does not rewrite that parameter;
  a failed-condition or muted step queues no marker and therefore holds it.
  The common reset latch clears all Effect overlays and `fxm` before the new
  pass's records apply. `fxm` follows the reset rule rather than the
  per-parameter end rule.

### 6.2 Automation reset on voice retrigger and transport restart

All trigger sources funnel through `voiceControl_triggerNow()` in
`MidiVoiceControl.c`. Before `instrumentManager_triggerTrack()`, it calls
`seq_restoreAutomatedParameters(voice)`, which iterates set bits in the
dirty bitmap using `__builtin_ctzll`, writes the `morph_interpolation[]`
value for each dirty descriptor back to the runtime, and clears the bitmap.
The dirty bitmap is also cleared on `seq_init()`, transport stop, and
`seq_setStepIndexToStart()`.

On transport restart (Session 070), `seq_setStepIndexToStart()` calls
`seq_restoreAllAutomation()` (walks all 6 voice dirty bitmaps and restores
from `morph_interpolation[]`) and `seq_restoreAllSceneAutomation()` (walks
`seq_scene_automation_dirty` and restores each Scene target from its retained
SceneData getter: clears morph step overrides, slot6 decay step state, and
audio routing overrides). Both restore functions run BEFORE
`seq_clearAutomationDirty()`. `seq_setRunning()` is structured so stop sets
`seq_running=0` first (preventing TIM3 ISR from advancing during cleanup)
and start sets `seq_running=1` last (after all initialization).

Effect step markers are queued immediately before the owning track's
automation entries on the same conditional playback gate. The marker's
payload-free identity keeps the track/step ID in bits 0..9 and sets identity
bit 11. Entries after a marker can re-hold a pending parameter; the next
marker or end-of-drain flush ends candidates not rewritten. This preserves
last-writer ownership when multiple tracks write the same Effect row.

### 6.2a Automation priority: automation, then menu edits, then MIDI (S075 F3)

**Rule (user):** a voice-parameter automation value written by the drain
holds until that voice's next trigger. Nothing else may change that
parameter's runtime value before then.

| Writer | What it does to a held parameter | Mechanism |
|---|---|---|
| Morph sweep (`presetMorph_tick()`), synchronous voice apply (`presetMorph_applyVoiceNow()`) | updates `morph_interpolation[local]` only; the runtime value is left alone | every Morph-base runtime write goes through `presetMorph_writeRuntimeBase()`, which skips an inactive Scene and any parameter for which `seq_automationHoldsParameter(slot, local)` is set |
| Menu edit of any instrument endpoint (`preset_setInstrumentParameter()`) | stores the endpoint; re-interpolates **only that parameter** at the voice's resolved Morph amount (`presetMorph_applyParameterNow()`), so a held parameter changes at its next trigger | no whole-voice queue; the runtime never receives the raw edited value |
| External MIDI CC/NRPN (`midiParser_enterTaggedParameter()` → `preset_setInstrumentParameterFromMidi()`) | stores the active Scene's clamped Normal endpoint and queues the voice for the Morph sweep (lowest priority) | the sweep's guarded write applies it when it gets there |

At the trigger, `seq_restoreAutomatedParameters()` writes
`morph_interpolation[local]` — which by then holds every edit and Morph
change made while the value was held — and clears the bitmap. Before S075 F3
a menu edit queued the whole voice and the worker overwrote every held
parameter mid-note; the same happened on Morph changes, an LFO on a voice's
Morph, `Nvm` automation and Instrument/Kit applies. Effect parameters and the
Scene targets (`Nfx`, `Nou`, `Nvm`, `fxm`, `7dc`) were never affected: they
are layered overlays read every block. An Instrument type change while values
are held is out of scope (a held bit may name the old type's descriptor until
the next trigger). Details: `075_SESSION_HANDOFF_LOG.md` §11.

### 6.3 Step-edit automation pages

After the step-specials subpages (subpage 1: velocity, note, condition,
micro-timing; subpage 2: roll rate, rhythm, fill), the selected-step edit
page shows dynamically counted automation pages. Each
page displays one entry with a 5-item cursor: number (with number-lock
mode), del/clr action, voice, parameter (with uniqueness-filtered cycling),
and amount (with dtype-aware display and bounds clamping). An "add" page
follows the last assigned entry. Detail views show category+long_name for
parameter targets and dtype-aware named values for amounts.

The selected-step edit page reads the live dynamic block. Setting note,
velocity, condition, or any roll/timing special performs a tracked pool
read-modify-write and repaints the menu. Track and Pattern setting pages
read/write the fields in the resident region; PAT4 persists them.

The automation pages are entered by scrolling right from subpage 2 position 2
(roll fill). `menu_stepAutomationPageActive()` returns true when
`menu_stepAutoActive && activePage == 2u` (S079; was `== 1u` before S079).
Left-exit from automations returns to subpage 2 position 2. Step-button
re-press toggles sub 1 ↔ sub 2 via `menu_toggleStepEditSubpage()` (S079 P2). Every target change goes through
`menu_stepAutomationReplaceTarget()` and every delete through
`menu_stepAutomationExecuteItem0()`; since S078 both restart the
automation-presence search unconditionally (the former
`menu_isVoicePage()` guards were dead), so the STEP track-settings underline
(§6.3a) follows editor changes. Value-only edits and Add (`off` sentinel) do
not change presence.

### 6.3a STEP-page multi-step editing and track automation (S078)

**Multi-step specials broadcast.** In STEP mode with two or more SEQ buttons
held, a velocity/note/condition/timing/roll/rhythm/fill edit is written to every held step of the
visible bar (`menu_broadcastStepSpecial()` → `patSvc_setStepVolume/Note/
Probability()`; `buttonHandler_seqHeldMask()` + `buttonHandler_visibleStep()`).
The display seeds from the last-pressed step; one held step is the old path;
the automation pages stay single-step. Triggerless steps receive specials;
a missing block is allocated; pool exhaustion drops writes silently.

**Held-step track automation overlay.** Hold SEQ steps, press TRACK
(`handleVoiceButton()` STEP arm → `menu_enterStepTrackAutomationOverlay()`):
the track-settings page becomes a per-held-step automation editor for the
three automatable cells (`len`/`scl`/`shf` → Scene targets 405/412/419 +
track via `menu_seqCellToTrackTarget()`, which re-validates through
`sceneModTarget_descriptor()`). Pot/encoder turns write the 7-bit value to
every held step (`patSvc_writeStepAutomation()`; length 128 clamps to 127);
non-automatable cells (`run`, `mch`, `not`) edit their retained value. With
SHIFT+COPY held (clear mode, no menu shown) a pot turn removes that target
from the held steps only (`copyClear_isClearMode()`). The overlay shares the
VOICE overlay's held/marker/search state (`menu.c` va_* block, 46 B) and
exits when all steps are released, the page/mode changes, or the track
changes.

**Underline.** On SEQ_PAGE subpage 0 the VOICE one-marker-per-cell rule
applies to `len`/`scl`/`shf`: a held step carrying the target shows and
underlines its value; otherwise the name is underlined when the active-track
Pattern search found the target (`VA_SEARCH_SCENE_TRACK_BIT()` in
`va_searchSceneMask`, set by `va_seqTrackSearchBit()` in `va_scanService()`).
The search runs on every SEQ subpage and restarts on SEQ entry, track change,
Scene change, copy/clear completion, editor target changes/deletes and
held-step clears. Without held steps, SHIFT+COPY + pot over a track cell is an
ordinary whole-Scene pot clear (`menu_knobClearTarget()` SEQ arm,
`COPYCLEAR_UTILITIES.md` §8). Reader count: this search is a third reader of
step automation (4 step reads per pass, active track only).

### 6.4 Per-track timing: length, scale, shuffle, run mode (S068, S078)

All four per-track settings (§1) are stored, Menu-editable, dirty-marked,
PAT4-persisted **and played** since Session 078. Implementation:
`Core/Sequencer/sequencer.c` (ISR side), `Core/Sequencer/StepScale.c/.h`
(curve), `Core/Bank/Scene/Preset/presetMorphEngine.c` (effective values).
They compose independently: length selects which step indices exist, scale
how fast they are visited, shuffle offsets odd steps in time, and the run
mode where the index goes next.

**Effective cache — the sequencer never reads timing from the region
directly.** `seq_effectiveTrackLength/Scale/Shuffle[NUM_TRACKS]` (21 B,
file-static in sequencer.c) hold Normal (region) interpolated against the
Scene's Morph endpoint at the retained Morph amount of the track's voice
(tracks 1–5 → voices 1–5, tracks 6–7 → voice 6), or a step-automation
overlay value when one is active. `seq_refreshTrackEffectiveParams()`
recomputes them per track from each track's **played** Scene
(`seq_perTrackPattern[track]`, S077), calling
`presetMorph_getTrackEffectiveLength/Scale/Shuffle()`. Refresh points:
`presetMorph_tick()` every foreground worker tick (before its idle
early-out, so endpoint edits propagate within one main-loop pass),
`seq_init()`, `seq_selectActivePattern()`, `seq_alignActivePatternToScene()`,
`seq_setTrackPlayedScene()`, `seq_clearPerTrackOverrides()`,
`seq_setStepIndexToStart()`. Morph and automation never write the region and
never mark the Pattern dirty.

**Length (S068).** The effective length is each track's wrap boundary
(fallback `NUM_STEPS_PER_BAR` when 0). A Morph sweep that shortens the loop
parks an out-of-range index at L−1 before the mode advance, so the change takes
effect at once. `seq_handleMasterBoundary()` intentionally stays on the fixed
16-step master grid (pattern-change commit, beat LED, clock out): it is a
bar-level concept independent of track length. The playback bar shown on the
SELECT LEDs follows the viewed track (`seq_ledState.chaseStep` =
`seq_stepIndex[menu_getActiveVoice()]`); `menu_currentBar` is only the
user-selected viewed bar.

**Scale — 128-position continuous log curve (S078).** `track_scale` is a CC
0..127; `multiplier(cc) = 0.25 × 2^(cc / (127/5))` (0.25× = 1/64 … 8× = 1/2,
relative to the 1/16 step of 24 PPQ ticks), with 14 positions nudged onto
exact musical values: 0 `/64`, 16 `32t`, 38 `/32`, 54 `16t`, 60 `d32`,
**76 `/16` (default)**, 83 `8Tr`, 86 `d16`, 93 `/8`, 100 `4Tr`, 103 `d/8`,
110 `/4`, 120 `d/4`, 127 `/2`. The display shows the symbolic name at a stop
and the raw CC elsewhere (`stepScale_formatShort()`).
`stepScale_ticksQ8(cc)` returns the Q8.8 tick interval
`round(multiplier × 24 × 256)` from a 256 B flash LUT (1,536..49,152).

Per-track DDA (`seq_processSchedulerTick()`): each PPQ tick every track adds
256 to `seq_trackAccumulator[track]` (`uint16_t[7]`) and, while the
accumulator ≥ interval, subtracts it and calls `seq_advanceTrackStep()`.
The remainder carries, so fractional rates are exact on average with no
drift; the loop runs at most once per tick (minimum interval 1,536 > 256).
`seq_setStepIndexToStart()` seeds every accumulator to `interval − 256` so
step 0 fires on the first tick (a zero seed would lose it). A scale change
mid-playback keeps the remainder (no reset). The master 1/16 counter is
still the 24-tick modulo and is used only for boundaries, beat, clock and
MIDI.

Realignment (`seq_realignActivePatternToMasterClock()`,
`seq_realignTrackToMasterClock()`): phase = `(seq_elapsedPpqTicks << 8) %
interval`; the step comes from the same Q8.8 timeline mapped through
`seq_stepIndexForMode()` (so rev/pip/onc/1fr land where their mode would be,
and pip's direction bit is set from the cycle phase). Stopped once-mode
tracks are skipped.

The FX sequencer uses the same curve and a DDA of its own
(`EFFECTS_BUS_REFERENCE.md`). Old PAT4 files (bytes 0..13 from the
14-index era) are not migrated on device and play near the fast end of the
curve; `tools/convert_scene_scale.py` remaps them offline (old index →
CC 0,16,38,54,76,83,86,93,100,103,110,127,127,127) and recomputes the CRC32C.

**Shuffle (S078).** Odd-indexed steps (0-indexed 1, 3, 5, …) with nonzero
effective shuffle are deferred by `(shuffle × 24) / 256` PPQ ticks (0..11):
always on the 1/16 grid, never scaled by the track's scale (a slow track gets
a proportionally tiny swing; musically intended). `seq_advanceTrackStep()`
stores velocity/note in `seq_trackShuffleVel/Note[7]` and sets
`seq_trackShuffleDelay[7] = delay − 1` and `seq_trackShufflePending[7]`
instead of triggering; `seq_processShuffleDelays()` runs each tick **before**
the DDA advance, decrements and fires at 0 (hence `− 1`). A pending deferral
is flushed before a new one is set, and fires even if the track then stops.
28 B ISR-static.

**Run mode (S078).** `track_play_mode[track]` (Menu cell `run`, category
`Track`, long name `RunMode`):

| Value | Token | Index movement on each step boundary |
|---:|---|---|
| 0 | `fwd` | +1, wrap L−1 → 0 (default) |
| 1 | `rev` | −1, wrap 0 → L−1 |
| 2 | `pip` | ping-pong, ends play twice (cycle 2L; same as the FX sequencer) |
| 3 | `rnd` | `(GetRngValue() & 0x7FFF) % L` |
| 4 | `onc` | once, entered aligned to the master clock; stops after L−1 |
| 5 | `1fr` | once, always from step 0; stops after L−1 |

`seq_trackPlayState[7]`: bit 0 `SEQ_PLAY_STATE_STOPPED`, bit 1
`SEQ_PLAY_STATE_PIP_REV` (pip needs no cycle counter: direction bit plus
index). A stopped once-track advances no further. **Retrigger** (STOPPED
cleared): global Scene change (`seq_selectActivePattern()`,
`seq_alignActivePatternToScene()`), per-track Scene reassignment of that
track (`seq_setTrackPlayedScene()`; voice 6 reassigns tracks 6 and 7
together), transport start/stop and Pattern boundaries
(`seq_setStepIndexToStart()`). **Not** retriggered by the double-click
realign or a reassignment of another track. The DDA decides **when** a step
fires; the run mode decides **where** the index goes.

**Step automation of track timing.** Scene targets 405..425 (§4) apply
through `presetMorph_setTrackParamStepOverride()` (mask + value, 24 B in the
Morph engine) and write the effective cache directly;
`seq_restoreAllSceneAutomation()` clears them
(`presetMorph_clearAllTrackParamStepOverrides()`) and
`seq_setStepIndexToStart()` recomputes the cache. Like all step automation
the overlay holds until transport stop or a Pattern restore; clearing the
automation does not undo the last applied value. The run mode is not
automatable or morphable.

**Adding a per-track timing parameter** (how-to): add the region byte
(update the size assert and PAT4 track header byte), init/setter/menu apply
in PatternData, PAT4 writer + three readers in filesystem.c, copy/clear track
copy/clear in copyClearService.c, a `PAR_*` id + SEQ_PAGE cell (menuPages.h)
+ dispatch case in `menu_parseGlobalParam()`, and — if morphable — a Scene
endpoint, AutoSave cell, `sceneset.scg` key, effective cache entry, getter
in presetMorphEngine.c, and a Scene target kind. Read timing only through the
effective cache in ISR code.

### 6.5 PERF looper engine (S080)

The PERF SELECT buttons loop tracks over a musical division while held
(gesture rules: `MODULE_INTERCHANGE_SPEC.md` "PERF performance surface").
The engine lives in `Core/Sequencer/sequencer.c`; it reuses the DDA,
effective cache and realign mapping of §6.4 and never touches Pattern data.

**State** (67 B SRAM1, runtime-only, file-static except the gate):

| Symbol | Meaning |
|---|---|
| `seq_loopActive` (extern) | ISR gate: 1 while the looper is engaged (sustained by a SELECT, a looped VOICE or a BAR); may be 1 with an empty mask. Read by `lop`/`!lp` and the chain conditions `lox`/`lob`/`!lx`/`!lb` |
| `seq_loopTrackMask` | tracks currently looping |
| `seq_loopDivision` / `seq_loopModifier` | SELECT index 0..7 (1 bar..1/128); 0 none, 1 dot ×3/2, 2 triplet ×2/3 |
| `seq_loopAnchor[7]` | per track, the master tick (`seq_elapsedPpqTicks` units) where its loop window starts |
| `seq_loopStartStep/StartAccum/StartPlayState[7]` | per track, the snapshot: cursor, Q8.8 accumulator and play-state bits **just before** the anchor tick's DDA advance |

Loop length `seq_loopLengthTicks()`: 384, 192, 96, 48, 24, 12, 6, 3 ticks for
divisions 0..7 (96 PPQ, 1 bar = 16 × 24), ×3/2 dot, ×2/3 triplet, minimum 1.

**Wrap** (`seq_loopProcessTick()`, TIM3, every tick, **before** the DDA
advance and after deferred triggers): for each looped track with
`elapsed > anchor` and `(elapsed − anchor) % length == 0`, restore the
snapshot. The DDA advance in the same tick then replays the anchor tick's
step, so the step that sounded at the loop start repeats on every wrap.
Wraps stay on each track's own anchor grid; changing the division or
modifier never moves an anchor (a shrinking length takes effect at the next
multiple). The wrap and the DDA are both skipped while a chain `stp` freeze
is active (§6.6).

**Position maths** (`seq_loopShiftPosition(track, pos, ticks)`, foreground
only, 64-bit): moves a position image (cursor, accumulator, play state) by a
signed tick count: accumulator ± ticks·256, floor-divided by the effective
Q8.8 interval into step advances, then the cursor walked per run mode:
fwd/rev modulo the effective length (from the pre-start cursor −1 the first
forward step is 0 and the first reverse step is L−1), pip along its 2L cycle
setting `SEQ_PLAY_STATE_PIP_REV`, rnd a fresh random step, onc/1fr clamped
with `SEQ_PLAY_STATE_STOPPED` set past the end and cleared when rewound
inside. It uses the current effective scale and length for the whole span,
so a Morph/automation change during the span is approximated.

**Loop start** (`seq_loopEventAnchor(mask)`): one anchor per gesture. With
recording quantisation on (`seq_quantisation` 8/16/32/64 → 48/24/12/6 ticks)
it is the last quantise boundary `now − now % q` (may be more than one loop
length back; the track then jumps back at the next wrap). With it off it is
the tick at which the fastest track of the gesture last advanced,
`now − (accumulator >> 8)` (after an advance the residual is < 256, so the
whole ticks in the accumulator are the ticks since that step). Before the
first scheduler tick the anchor is 0.

**Snapshot** (`seq_loopSnapshotTracks(mask)`): each track's live state
(post-advance at `now`) rewound by `now − anchor + 1` ticks = pre-advance at
the anchor (0 ticks before the first scheduler tick, whose seeded state is
already pre-advance at tick 0).

**Resume** (`seq_loopResumeTrack(track)`): the snapshot projected forward by
`now − anchor + 1` ticks and written to `seq_stepIndex`,
`seq_trackAccumulator`, `seq_trackPlayState`, `seq_lastMasterStep` (chase
LED for the selected voice). This is exactly where the track would have been
without the loop, whether or not it was on its canonical master-clock
position — the reason the looper does not resume through
`seq_realignTrackToMasterClock()`.

**API** (foreground; every publish holds PRIMASK for its whole body, and
`seq_loopActive` is written last on engage / first on disengage):

| Function | Behaviour |
|---|---|
| `seq_loopEngage(mask, div, mod)` | returns 0 and does nothing while stopped (recording is not touched: S081 P3, L-2); filters `mask` by `seq_loopableTrackMask()`; snapshots at one anchor; returns the mask now looping |
| `seq_loopUpdateMask(newMask)` | engaged only: tracks leaving resume (inside the call, before the mask is committed), tracks joining get one fresh anchor, tracks staying keep theirs; returns the committed mask |
| `seq_loopDisengage()` | clears gate and mask, resumes every looped track (if running) |
| `seq_loopUpdateDivision(div, mod)` | single-byte stores; anchors untouched |
| `seq_loopableTrackMask()` | bit N set unless track N's played Scene region has the exclude bit |

**Scene changes** (`seq_selectActivePattern()`,
`seq_alignActivePatternToScene()`, `seq_chainRepeatSelf()`): after
`seq_refreshTrackEffectiveParams()`, one PRIMASK section runs
`seq_realignActivePatternToMasterClock()` and
`seq_loopRefreshSceneSnapshots()`: anchors are kept and each snapshot becomes
the incoming Scene's canonical pre-advance position at `anchor − 1`
(`seq_loopCanonicalPreAdvance()`: the realign mapping of §6.4; anchor 0 →
seed cursor −1, accumulator `interval − 256`). The loop start therefore
stays at the same master-clock position with the incoming Scene's own scale
(1/16 loop start at step 5 = tick 96 → a 1/8 incoming track starts at step 3;
tick 120 → mid step 3). Between the change and the next wrap the track plays
the incoming Scene from its realigned position. Resume after a scene change
projects the refreshed snapshot, which equals the incoming Scene's
master-clock position.

**Cursor reset** (`seq_setStepIndexToStart()`): ends with
`seq_loopRebaseToStart()`: anchors 0, snapshots = the freshly seeded state.
This covers a resetting scene change (`seq_resetToPatternStart()`: pcr for
direct/chain changes, the qac reset bit for queued ones), external resets and
boundary pattern commits — the loop start becomes the beginning of the Scene
and the loop stays engaged. `seq_setRunning(0)` clears `seq_loopActive` and
the mask before the reset, so a transport stop ends the loop.

**Invariants:** never reintroduce a shared loop tick counter or an
un-anchored snapshot taken at the press (both drift off the grid); never
publish looper state outside PRIMASK; keep the wrap before the DDA advance;
FX sequencer and Effects are never looped.

**Looping while recording (S081 P3).** Live record (LATCH or OVERDUB) and live
erase run while the loop is engaged; the looper and the record modes no longer
exclude each other (plan L-2). The looper never touches Pattern data, so the
record engine simply follows the looped playhead:

- **Jump entries.** A wrap that lands mid-step (with `qnt off` or a 1/8..1/64
  grid, when the track's step does not start at the anchor) pushes a ring
  entry with the jump flag (`SEQ_REC_RING_JUMP_BIT`), because no step advance
  follows on that tick; `seq_loopResumeTrack()` does the same when a resume
  moves the track to another step, and drops that track's echo mark. The
  step a jump entry begins is a partial replay: its trigger did not fire on
  this pass, so its step end runs dirty writes and erase but no LATCH
  re-stamp or clear.
- **Placement.** `seq_placeFromSnapshot()` maps a captured hit that crosses a
  wrap onto the pass the target falls in (`seq_placeLoopImage()`): a forward
  snap across the wrap lands on the replayed step after it, a backward one at
  the end of the previous pass, or in the unlooped history before the loop's
  first pass. `rel` is decided by time when the shift crossed a wrap.
- **Echo.** No echo mark is cleared at the wrap; the P1B deadline expires it,
  measured through the wrap, so exactly the one replay after the capture is
  silenced. A resume clears the track's mark directly.
- **Ring producer.** Every ring entry comes from one function,
  `seq_recRingPush()`: TIM3 for a step advance and a mid-step wrap, the
  foreground under the looper's PRIMASK for a resume.

### 6.6 Scene change timing, queued changes and scene chaining (S080)

**Divisions.** A direct (SEQ) change uses `scd` (`PAR_SCENE_CHANGE_DIV`,
0..9 → master 1/16 units 0, 2, 4, 8, 16, 32, 48, 64, and 16 for `ens`/`enl`,
`seq_sceneChangeDivUnits()`); a queued (SHIFT+SEQ) change uses `qcd`
(`PAR_SCENE_QUEUE_DIV`, 0..17 → 0, 2, 4, 8, 16, 32, 48, 64, 80, 96, 112, 128,
144, 192, 240, 256, 16, 16, `seq_queueChangeDivUnits()`). Units 0 = instant.
`ens`/`enl` (end of shortest/longest track) are approximated by one bar.

**State** (file-static): `seq_pendingSceneChange/Index/Div` (direct),
`seq_pendingSceneQueue/QueueScene/QueueDiv` (queued), `seq_pendingChainFire`
(the direct pending change was raised by a chain), `seq_sceneFireFlag`
(TIM3 → foreground). API: `seq_requestSceneChange()` (clears a stale chain
marker), `seq_queueSceneChange()` (instant or stopped transport raises the
fire flag at once), `seq_sceneChangeFirePending()`, `seq_pendingSceneTarget()`
(queue wins), `seq_pendingSceneIsQueue()`, `seq_pendingSceneIsChain()`,
`seq_queuedSceneTarget()` (0xFF none; the fast-blink LED owner reads it),
`seq_clearPendingSceneChange()` (all; used when a fire is consumed and on
transport stop), `seq_cancelPendingDirectChange()` (direct + chain only; a
queued change survives a manual change, and an instant queue re-arms).

**Boundary detection** (`seq_handleMasterBoundary()`, TIM3, every master
1/16): with a change pending, `units != 0 && clock % units == 0` raises the
fire flag (queue units if a queue is pending, else direct). The foreground
executor and queue actions are in `MODULE_INTERCHANGE_SPEC.md` (menu).

**PC reset.** A resetting change runs `seq_resetToPatternStart()` after
`seq_selectActivePattern()`: master clock, bar counter and every cursor
restart (and the looper rebases, §6.5). Direct and chain changes reset when
pcr (`PAR_BAR_RESET_MODE`) is on; queued changes ignore pcr and reset only
for qac `res`, `u+r`, `o+r`, `mmr`, `r+r`. Non-resetting changes realign to
the running master clock (`seq_selectActivePattern()` never resets).

**Scene chaining.** Per Scene, `chain_condition[2]` (0..78) and
`chain_next[2]` (0..43) in `scene_settings_t`
(`BANK_PRESET_ARCHITECTURE.md`). Engine state (5 B): `seq_chainBarCount`,
`seq_chainRepCount` (saturating 255, reset when a Scene is selected or
repeated, on Bank Load alignment and on transport start/stop),
`seq_previousScene` (set only when the active Scene really changes; 0xFF on
stop/Bank Load), `seq_chainStopped`, `seq_pendingChainFire`.

`seq_chainServiceTick()` runs in `seq_processSchedulerTick()` after the
master boundary and before deferred triggers, on every tick after tick 0:

1. Bar end = `elapsed % 384 == 0`. Repetition end =
   `seq_chainAtRepetitionEnd()`: the longest track cycle
   `max(effective length × Q8.8 interval)`; true when
   `((elapsed << 8) % cycle) < 256` (the first tick at or after each cycle
   multiple, exact for fractional scales).
2. Counters advance on their boundary.
3. Evaluation is skipped while a direct, queued or chain change is pending.
4. `seq_chainEvaluate()`: weight per condition (`seq_chainConditionWeight()`:
   0 nul; 1 xal / 2 bal = 100 at their boundary; 3..13 / 14..24 = 100 when the
   repetition / bar count equals 1..8, 16, 32, 64; 25..33 / 34..42 = 10..90 at
   repetition / bar ends; 43..46 / 47..54 = 100 at a bar end when the master
   bar just completed is X of 4 / 8 (`((elapsed/384) − 1) % n`); 55..70 = 100
   at a repetition end when `seq_previousScene == NN−1`; 71..78 fill / loop
   active / inactive at repetition / bar ends). Condition 1 priority:
   w1 = 100 → 1; two probabilities summing over 100 → proportional draw (one
   always fires); otherwise one draw r ∈ 0..99: r < w1 → 1, r < w1 + w2
   (capped at 100) → 2, else no change.
5. `stp` (43) sets `seq_chainStopped`: the DDA and loop wraps stop; the
   transport, deferred triggers and rolls continue; counts continue.
   Otherwise `seq_chainResolveNext()` (0 slf, 1 nxt, 2 prv, 3..18 sNN, 19..34
   random 1..NN, 35..38 random next N excluding current, 39..42 previous N;
   absent target → no change) and the change is published as a direct
   pending change with `seq_pendingChainFire = 1`, `div` 0, fire flag last —
   it executes at this boundary, not re-quantised by `scd`.

The foreground executor treats a chain fire whose target is the playing Scene
as a repeat: `seq_chainRepeatSelf(pcr)` resets the chain counters and the
`stp` freeze, retriggers once modes and the per-track condition counters,
realigns (keeping loop anchors), or restarts when pcr is on; there is no
DSP re-apply. Any executed change clears `stp` through
`seq_selectActivePattern()`.

**Adding a chain condition or next value:** extend the SceneData clamp
constants (`SCENE_CHAIN_CONDITION_MAX` / `SCENE_CHAIN_NEXT_MAX`), the name
table in MenuText.h (index = stored value, numeric order), the weight or
resolve switch in sequencer.c, the sceneset parser maximum and the menu clamp.
Conditions must be ISR-safe single-byte reads.

## 7. PAT4 wire format

All current Scene/Bank/root Pattern Save operations write PAT4. Legacy text
v1-v3 files may be accepted for import but are never emitted.

One PAT4 file is exactly 10,656 bytes:

| Region | Offset | Bytes | Contents |
| --- | ---: | ---: | --- |
| Fixed header | 0 | 32 | magic/version/stack/generation/CRC/reserved |
| Pattern parameters | 32 | 16 | change-bar, next, 14 reserved |
| Track parameters | 48 | 112 | 7 × (length, scale CC, shuffle, run mode, loop exclude, 11 reserved) |
| Address array | 160 | 1,792 | 7 × 128 little-endian encoded entries |
| Bitmap | 1,952 | 512 | exact bit-packed allocator state |
| Pool | 2,464 | 8,192 | dynamic blocks/padding |

Fixed-header fields:

```text
0..3    "PAT4"
4..5    uint16 LE format version = 2 (bumped from 1 in S079; reader rejects v1)
6..7    uint16 LE stack size = 256 (must equal firmware PAT_STACK_SIZE)
8..9    uint16 LE header size = 160
10..13  uint32 LE generation
14..17  uint32 LE CRC32C
18..31  reserved zero
```

CRC32C uses the Castagnoli polynomial, covers the entire exact-size file, and
treats bytes 14..17 as zero. Readers require current version, exact stack size,
exact EOF and matching CRC before committing. Firmware does not add a semantic
allocator graph audit during load; host validators should check address,
bitmap, back-reference, and pool consistency. Root/library saves use generation
zero.

Track header bytes per track (16 each, base = 48 + 16 × track): 0 length,
1 scale (CC 0..127 since S078; older files carry the 14-index value and are
not migrated on device), 2 shuffle, 3 run mode (S078; pre-S078 files carry
0 = `fwd`), 4 looper exclusion (S080: 1 = track not loopable; written
0/1 from bit N of `track_loop_exclude`, read back nonzero → bit set; older
files carry 0 = loopable), 5..15 reserved zero. The writer
(`filesystem_patternBuildHeader()`) and all three readers (Scene load, root
Pattern Load, AutoSave Pattern boot read) handle byte 4. The file size, CRC
rules and version (2) are unchanged.

Scene directories contain exactly one `<Pattern name>.pat`, not a fixed
`pattern.pat`. The root library uses `Pattern/NNN <name>.pat`. Pattern Load
accepts a destination Scene mask, streams into the first selected Scene, then
copies the validated `pat_scene_region_t` to every other selected Scene.

## 8. Pattern identity

HCNAMES rows 129..144 own one Pattern name/source/refreshed witness per Scene.
They have the non-Instrument grammar:

```text
name<TAB>source[<TAB>R]
```

Accepted sources are inherit `-`, unknown `?`, direct library slot `000` to
`999`, and Pattern AutoSave `@`. The RAM value for Pattern `@` is
`FS_RESIDENT_SOURCE_PATTERN_AUTOSAVE == 0x1ffc`; it must not be confused with
Instrument-direct `@ == 0x1ffd`. Pattern rows have no type column.

Scene/Bank directory load commits cache and publish the Pattern identity with
the Scene's other child rows. Root Pattern Load publishes the selected Pattern
name/source to every destination row. A Pattern mutation clears its row's
refresh witness before later AutoSave publication.

## 9. Pattern AutoSave

Each Scene has an independent hidden root pair:

```text
/.patNNa   generation even
/.patNNb   generation odd
```

`NN` is decimal `00`..`15`. Files are complete PAT4 images. The higher valid
generation wins and A wins a tie. Generations never intentionally use zero;
wrap skips zero.

Pattern dirtiness is a separate 16-bit mask. When all ordinary filesystem and
policy gates are open, Pattern is admitted only after higher-priority settings,
trace, and scalar AutoSave work declines. The scheduler chooses the lowest
dirty Scene, refuses admission during `seq_recordActive` or `seq_eraseActive`,
clears the bit before copying to the snapshot, advances generation, and starts
the whole-file writer.

Clear-before-snapshot is binding: a later edit re-sets the canonical bit and
must survive the in-flight completion. An admission/write/close/sync error
re-arms the bit. Successful durable completion publishes the Pattern row as
`name<TAB>@<TAB>R`, but the completion may set `R` only if no post-snapshot
edit made the Pattern dirty again.

**Torn files.** A Pattern write interrupted before its first close leaves
the target at AsyncFATFS's cluster-rounded size: 32,768 B instead of
10,656 B (`ASYNCFATFS_REFERENCE.md`, "Open-file size on the card").
`filesystem_patternAutosaveCandidateValid()` reads one byte past the payload
and rejects the candidate unless that read returns end-of-file. The next
Pattern generation for that Scene recreates the file with `"w"`. The S074
card showed this: `.pat06b` was 32,768 B, was rejected, and was rewritten at
10,656 B. The scalar HCPR validators were brought in line with this probe in
S074 (`AUTOSAVE.md`).

At boot the Pattern reader evaluates present Scenes independently after the
scalar Bank/Scene restore. It applies the winning hidden candidate only when
the HCNAMES Pattern row is Pattern-AutoSave `@` and generation is nonzero.
Missing/invalid candidates leave the current initialized or library-loaded
Pattern intact and do not invalidate scalar Scene data. Explicit root Pattern
Load and Scene/Bank directory Pattern load reset the destination generation
baseline to zero.

## 10. Concurrency and failure invariants

- PatternData owns live regions and the snapshot; filesystem owns handles and
  Pattern generations; Autosave owns the dirty mask.
- Snapshot copy is a plain `memcpy`; it does not mask TIM3 or any interrupt.
- Record/erase activity gates admission because those paths can mutate the
  Pattern asynchronously.
- The snapshot is immutable until its writer completes.
- Only one filesystem facade operation is active; Pattern is not a second
  concurrent SD writer.
- FAT timestamps are irrelevant because the hardware has no RTC.
- Boot selects by exact format, size/EOF, CRC, generation, and provenance—not
  filename timestamp. Host diagnostics additionally validate allocator
  structure.
- A corrupt Pattern candidate never empties an otherwise valid scalar Scene.
- HCPR never contains Pattern identity or payload bytes. Its v2 APIs align
  with the 145-row HCNAMES schema, while HCNAMES/PAT4 own Pattern identity and
  payload durability.

## 11. Accepted validation and remaining work

Session 064 hardware acceptance covered single-Scene restore, ping-pong
parity, full 16-Scene drains, HCNAMES `@|R` publication, HCPR v2 coexistence,
absent rows, and content changes in every Scene. The full card contained 19
valid hidden candidates and no identified Pattern defect. See
`../log_archive/064_SESSION_HANDOFF_LOG.md` for exact evidence.

Session 065 implemented step automation editing (Method 1) and sequencer
playback: pool block automation read/write/remove APIs, step-edit menu with
cursor navigation and detail views, sequencer pending buffer with foreground
drain, per-slot dirty bitmap and trigger-time morph-interpolation restore.
Hardware-tested. See `../log_archive/065_SESSION_HANDOFF_LOG.md`.

Session 066 implemented the VOICE-page held-step automation overlay (Method 2):
overlay activation via configurable short long-press, four-slot bounded CGRAM
underline cache (slots 2..5), held-value resolution with endpoint fallback,
async track-wide automation search agent (4 steps/pass), pot/encoder-to-
automation write with working-value cache, debounced value-underline
reapplication, step illumination of automated steps, and Morph integration.
44 bytes overlay state in `menu.c` (approved). 496 bytes flash font table in
`lcd.c`. Six post-hardware-test fixes applied: delta handling working-value
cache, editMode bit index, non-numeric dtype display, 'S' glyph, PM63 nibble
split, diff-based CGRAM transactions with retry bit. Hardware-tested.
See `../log_archive/066_SESSION_HANDOFF_LOG.md`.

Session 067 implemented the Pattern Stack Service (§12): unified pool mutation
dispatcher with SPSC queue, two-tier defragmentation (trailing-gap merge and
paced compaction), bulk barriers for track/pattern clear, filesystem
replacement handover, pool usage monitor widget, and elastic gap policy.
Publication ordering fix applied to all address-to-pool transactions
(write-new/swap/free-old with PRIMASK trigger-bit re-read). Dtype offset bug
fixed: automation value domain changed from halved MIDI CC-style to identity
mapping across all four code sites (menu write, menu read, sequencer drain,
step automation add). Hardware-validated: PatternTrace zero errors across
6,172 records (5,185 gap + 987 relocation), AutoSaveTrace zero errors across
157,207 records, PAT4 structural integrity confirmed, automation values
confirmed in identity domain post-fix. See
`../log_archive/067_SESSION_HANDOFF_LOG.md` for exact evidence.

Session 068 confirmed the Pattern Stack Service was not implicated in a
reported VOICE-mode step-toggle failure (the `pattrace.bin` trace showed
zero error-class records; ordinary trigger toggling never enters the
service, see §12.14 item 1) — the actual defect was in front-panel event
delivery (`buttonHandler.c`'s event ring, pairing masks, and hold timer),
fixed with no Pattern Stack Service changes. The same session implemented
per-track step-length playback consumption (§6.4) and fixed a
`menu_playedPattern` UI-mirror desync that suppressed the sequencer chase
LED after boot (Menu/Sequencer concern, not a Pattern-storage defect). Session
069 then implemented the settled plan: physical relocation is non-semantic
AutoSave work, and the Tier 1/Tier 2 periodic chase is replaced by owned
trailing-slack repair plus reactive-only compaction. Source/build verification
passed; hardware fixtures remain pending. See
`../../S069_SLACK_REACTIVE_COMPACTION_IMPLEMENTATION.md` and
`../log_archive/068_SESSION_HANDOFF_LOG.md` §4 for the plan and closeout.

Session 069 Pass 1 also made scalar dirty detection constant-time through a
maintained population count, removed the clean idle-tick occupancy recount
while retaining mutation/lifecycle reconciliation, and added semantic Pattern
AutoSave coalescing (250 ms quiet window, 5 s maximum latency, rotating Scene
fairness). Hardware-accepted (2026-09-20).

Session 069 Pass 2 added a shared elapsed-time background CPU budget
(2.5% playback / 5% stopped, signed credit with one-millisecond accumulation
cap), integrated it into the repair section and both AutoSave drain
schedulers, added a Load/Save repair gate (`menu_activePage` check suppresses
repair during Load/Save menu), and added DEV-only per-class `H` trace
reports. Source/build verified; hardware validation pending.

Deferred supplemental cases are deterministic mid-write power interruption,
record/erase admission instrumentation, injected CRC fallback, and performance
measurement. They do not reopen the functional closeout. Live-record capture
is a future feature.

Session 075 implemented Phase 6 copy and clear (`COPYCLEAR_UTILITIES.md`):
the swap block (§3), the exclusive boundary, raw block API and sliding
compaction (§12.17), whole-Pattern copy with the sentinel-first publication
order, early trigger writes and Pattern clears; and the automation-priority
fix (§6.2a). The user tested the base pass, F1 and F2 on hardware and fed back
each round; the remaining case list is `COPYCLEAR_UTILITIES.md` §16. See
`../log_archive/075_SESSION_HANDOFF_LOG.md`. The Session 068
self-generated relocation/dirty-work-at-idle finding is closed in source by
the S069 repair/reactive design and confirmed by Pass 1 hardware acceptance.

## 12. Pattern Stack Service

### 12.1 Architecture

`PatternStackService.c` and `PatternStackService.h`
implement a unified dispatcher that serializes all pool-mutating operations
through a single service tick. This guarantees exactly one mutation target at
a time, preventing concurrent access between foreground callers, the TIM3
ISR's automation reads, and the filesystem replacement boundary.

Admission policy: direct-when-idle (immediate foreground execution with
service lock), queued-when-busy (PRIMASK-protected enqueue into the SPSC ring
for later drain).

### 12.2 Service tick priority order

Evaluated every call to `patSvc_tick()` (called from `timebase.c` after
`endlessPots_tick()`). The repair section (item 4) is suppressed when
`menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE` (Session 069
Pass 2 Load/Save repair gate). All sections are subject to the shared
elapsed-time background CPU budget (`filesystem_backgroundBudgetAvailable()`,
Session 069 Pass 2) — the repair section checks budget before proceeding:

1. **Handover** — check and complete filesystem replacement boundary
   transitions.
2. **Reactive recovery** — when the queue head is blocked by fragmentation,
   inspect a bounded set of address entries and relocate one live block toward
   a lower destination; surplus reservations may be reclaimed only when the
   density latch is inactive.
3. **Queue/bulk work** — advance one bounded track/pattern clear barrier or
   dequeue and execute one pending mutation.
4. **Finite bounded repair** — scan up to `PAT_REPAIR_SCAN_IDLE` (or
   `PAT_REPAIR_SCAN_BUSY` under AutoSave pressure) address entries per tick,
   creating or verifying one trailing-chunk reservation per occupied block.
   The cursor sleeps at `PATSVC_ADDRESS_COUNT` between epochs and wakes only
   on mutation, reservation consumption, density restore, handover, or
   filesystem replacement. Occupancy is reconciled at those mutation/lifecycle
   boundaries and cached during clean idle ticks; the service does not rescan
   the unchanged bitmap on every pass.

### 12.3 Queue format

64-entry volatile `uint32_t` ring buffer (256 bytes SRAM1). Each entry packs:

```text
bits 31..29   operation code (3 bits)
bits 28..25   scene (4 bits)
bits 24..22   track (3 bits)
bits 21..15   step (7 bits)
bits 14..6    target (9 bits) — instrument_param_id_t
bits 5..0     value (7 bits) — 0..127 instrument_param_value_t
```

Enqueue uses `__disable_irq()`/`__enable_irq()` (PRIMASK) to protect
head/tail consistency. Dequeue is foreground-only. Queue full is a silent drop.

### 12.4 Bulk barriers

Track clear and pattern clear are bounded operations, 16 steps per service
tick:

- **Track clear**: 128-step sweep, completes in 8 ticks.
- **Pattern clear**: 7-track × 128-step sweep, completes in 56 ticks.

### 12.5 Filesystem replacement handover

`patSvc_idle()` is called from 5 filesystem replacement boundary points in
`filesystem.c`. When a Scene Load, Bank Load, or Pattern Load completes, the
service completes or abandons any in-flight bulk barrier, drains remaining
queue entries for the old Scene, switches `service_scene` to the new target,
and clears internal repair/recovery cursors.
The handover-complete boundary also clears the non-persisted reservation image
and lazily rebuilds it through the next repair epoch.

### 12.6 Owned trailing-slack reservation

The reservation image is a 512-byte bit-packed array with the same geometry as
the occupancy bitmap. A set reservation bit means the chunk is reserved as
trailing slack for the immediately-preceding occupied block and is refused to
ordinary allocation. The repair pass creates reservations; the Gate-6 growth
path (`pat_tryAppendAutomation`) consumes them; reactive recovery reclaims
surplus ones under allocation pressure. The image is not persisted or included
in PAT4 payloads.

The density latch disables new reservations at or above
`PAT_RESERVATION_REDUCE_THRESHOLD` (70% occupancy) and re-enables them below
`PAT_RESERVATION_RESTORE_THRESHOLD` (50%). Hysteresis prevents oscillation.
An adaptive per-tick budget (`PAT_REPAIR_SCAN_IDLE` versus
`PAT_REPAIR_SCAN_BUSY`) yields foreground cycles to AutoSave I/O when dirty
work is pending.

The reservation image is cleared and lazily rebuilt at init, handover
completion, and filesystem replacement. During the rebuild window, allocation
falls back to the occupancy bitmap alone. Ordinary block frees and service
relocations clear the former positional trailing claim so the image cannot
retain stale reservations after a block changes owner.

Physical relocations are non-semantic AutoSave work. Reactive recovery runs
only after a blocked allocation; there is no periodic Tier-2 sweep.

### 12.8 Elastic gap policy

The former elastic-gap policy is retired. Reservation-density hysteresis now
controls owned trailing slack, and `PAT_COMPACT_SCAN_PER_TICK` (16 address
entries) is retained only as the reactive-recovery scan bound.

### 12.9 Pool usage monitor

`pat_poolUsagePercent()` in PatternData.c reads the Scene bitmap with
`memcpy` (packed-bitmap-safe), counts set bits with `__builtin_popcount`
across 64 words, and returns `(occupied × 100) / 2015` (usable chunks below
the swap block, S075) clamped to 99.
Displayed as `"pts:NN"` on the Settings menu Global subpage, computed once
on page entry and retained in a static byte.

### 12.10 Publication ordering contract

All address-to-pool transactions follow detach/publish-before-free:

1. **Replace**: allocate new, write content, PRIMASK swap address entry
   (re-read trigger bit), free old.
2. **Clear last special**: PRIMASK detach address (clear bit 14, sentinel
   offset), free old.
3. **Erase step**: detach entire address word, free old.

In-place rewrite of same-size blocks is not used. All PRIMASK sections are
under 50 ns: one load, one bit-field insert/clear, one store, no allocation
or loop.

### 12.11 Config constants

```c
#define PAT_COMPACT_SCAN_PER_TICK          16u /* reactive entries/tick */
#define PAT_RESERVATION_REDUCE_THRESHOLD   70u /* % occupancy: disable */
#define PAT_RESERVATION_RESTORE_THRESHOLD  50u /* % occupancy: re-enable */
#define PAT_REPAIR_SCAN_IDLE               16u /* entries/tick */
#define PAT_REPAIR_SCAN_BUSY                4u /* entries/tick under AutoSave */
```

### 12.12 Public API

```c
void     patSvc_init(void);
void     patSvc_tick(void);
void     patSvc_idle(void);
uint8_t  patSvc_writeStepAutomation(scene, track, step, target9, value7);
uint8_t  patSvc_removeStepAutomation(scene, track, step, target9);
void     patSvc_setStepNote(scene, track, step, note);
void     patSvc_setStepVolume(scene, track, step, volume);
void     patSvc_setStepProbability(scene, track, step, prob);
void     patSvc_eraseStep(scene, track, step);
void     patSvc_clearTrack(scene, track);
void     patSvc_clearPattern(scene);
void     patSvc_removeTrackAutomationByTarget(scene, track, target9);
void     patSvc_enqueueErase(scene, track, step);
uint8_t  patSvc_isChunkReserved(chunk);
void     patSvc_consumeReservation(chunk);
/* S075 copy/clear holder (§12.17) */
uint8_t  patSvc_beginExclusive(scene);
void     patSvc_endExclusive(scene);
uint8_t  patSvc_exclusiveCompactStep(scene);
uint8_t  patSvc_exclusiveEvacuateSwapStep(scene);
```

### 12.13 Integration points

| Location | Call |
|----------|------|
| `main.c` | `patSvc_init()` after boot filesystem ladder |
| `timebase.c` | `patSvc_tick()` after `endlessPots_tick()` |
| `filesystem.c` | `patSvc_idle()` at 5 replacement boundary points |
| `timebase.c` | `ccSvc_tick()` (copy/clear service) right after `patSvc_tick()` (S075) |

### 12.14 Binding constraints

1. No pool mutation path may bypass the service without explicit justification.
2. Exactly one mutation target Scene at a time.
3. TIM3 reads address entries directly; they must always point at valid or
   sentinel data.
4. PRIMASK sections must stay under 50 ns.
5. Bulk barriers must be bounded per-tick.
6. `patSvc_idle()` must be called at every filesystem replacement boundary.
7. Queue drop on full is the accepted failure mode.

### 12.15 Background CPU budget

Session 069 Pass 2 added a shared elapsed-time budget primitive in
`filesystem.c` that bounds background work across three consumer classes:
repair (`patSvc_tick()`), scalar AutoSave drain, and Pattern AutoSave drain.

- **Budget rate**: 2.5% during playback (25µs/ms), 5% stopped (50µs/ms).
- **Refill**: `filesystem_backgroundBudgetRefill()` adds credit based on
  wall time elapsed since last refill, capped at one millisecond of
  accumulated time to prevent idle buildup.
- **Charge**: `filesystem_backgroundBudgetCharge(start, end)` subtracts
  elapsed work time. Signed credit allows overshoot tracking.
- **Query**: `filesystem_backgroundBudgetAvailable()` returns `credit > 0`.

The repair section of `patSvc_tick()` checks budget availability before
proceeding. The Load/Save repair gate additionally suppresses repair when
`menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE`.

DEV-only `H` AutosaveTrace reports every ~5 seconds show per-class
cumulative microseconds (repair, scalar drain, pattern drain).

RAM: 8 bytes always-on (credit + last refill timestamp) + 28 bytes DEV
accounting.

### 12.16 PatternTrace stage codes

Eleven stage codes in `PatternTrace.h` for service diagnostics:

| Code | Meaning |
|------|---------|
| `H` | Pending-buffer overflow witness |
| `Q` | Queue event (enqueue/dequeue/drop) |
| `C` | Capacity drop |
| `F` | Fragmentation drop |
| `R` | Reactive relocation (historical enum name: `TIER2_RELOC`) |
| `M` | Retired Tier-1 gap relocation |
| `G` | Retired gap fallback |
| `X` | Service state change (handover, mode transition) |
| `V` | Repair created an in-place trailing reservation |
| `L` | Repair relocated a block to create a reservation |
| `D` | Direct-path mutation retained for reactive recovery |

These are PatternTrace codes in `PatternTrace.h`, distinct from the
AutoSaveTrace codes in `AutosaveTrace.h` that use the same single-letter
convention.

### 12.17 Copy/clear: exclusive boundary, raw block API, compaction (S075)

**Exclusive boundary.** `patSvc_beginExclusive(scene)` /
`patSvc_endExclusive(scene)` give one caller sole write access to one Scene's
Pattern region, on any resident Scene (the active Scene may differ from the
playback Scene). Begin records the claim, closes admission on the service
Scene and returns 1 once the FIFO, bulk barrier and reactive recovery have
drained (0 while a filesystem replacement or another claim is pending); it
clears the reservation image. While the claim exists the handover branch of
`patSvc_tick()` neither reopens admission nor adopts the claimed Scene. End
recounts occupancy, restarts the repair epoch and reopens admission or
resumes the handover. One holder at a time.

**Raw block API** (`PatternData.h`, legal only inside the boundary):
`pat_rawReadBlock`, `pat_rawBlockBytes`, `pat_rawDecode`, `pat_rawEncode`,
`pat_rawPlace`, `pat_rawPlaceViaSwap`, `pat_rawSwapReturn`,
`pat_rawPublishEmpty`, `pat_rawFreeChunks`, `pat_rawSwapFree`, and the
whole-region set `pat_rawRegionSilence`, `pat_rawRegionCopyBody`,
`pat_rawRegionPublishSteps`, `pat_rawRegionCopiedBlock`,
`pat_rawRegionPublishRewritten`, `pat_rawRegionReset`. Every step write keeps
§12.10: new bytes into unreferenced space, one PRIMASK store of the complete
address entry (trigger policy KEEP re-reads the live trigger bit), then free
the old run. A step rewrite that does not grow goes through the swap block:
place there, publish, free the old run, then `pat_rawSwapReturn()` moves it
into the freed run and publishes again.

**Whole-region copy order** (`copy pattern`, `copy scene`): publish every
destination entry as empty (trigger off), copy pool, bitmap, track settings
and Pattern globals, rewrite retargeted blocks in place while still
unreferenced (they only shrink; freed tail chunks return to the bitmap), and
publish the address entries last.

**Compaction for the holder.** `patSvc_exclusiveCompactStep()` is a sliding
compaction: one call moves the block just above the lowest free chunk down
into it, through the empty swap block when the runs overlap. Repeated calls
leave all free space as one run below the swap block, so a growing step
whose new size fits the free chunks always finds its run.
`patSvc_exclusiveEvacuateSwapStep()` moves a pre-S075 block out of the swap
block (or clears orphan swap bits); if a full pool makes that impossible the
copy/clear job that needed the swap block is dropped.

Copy/clear emits only service-level relocation summaries to the S075 stage-`c`
trace; `patSvc_exclusiveCompactStep()` and
`patSvc_exclusiveEvacuateSwapStep()` do not emit one record per moved block.
Pattern paste selections that change triggers capture the source and previous
destination trigger masks at button time, before the queued exclusive job,
and restore only entries still containing the service's write if the job is
dropped. Clears that end trigger-off write those bits at acceptance too. These
early writes are ordinary foreground trigger edits (`pat_setStepActive()`, one
halfword RMW each) outside the claim; the job later publishes the same
trigger state with its blocks.

**Rates.** The copy/clear engines do at most 8 step placements, 16 snapshot
reads or 32 region entries per 2 ms tick. While a filesystem writer that
started before the operation is still running, a whole-call governor limits
them to 0.1 % CPU on average (`COPYCLEAR_UTILITIES.md` §11.5).

**Suspension.** While a copy/clear operation runs, the repair epoch does not
start (`copyClear_backgroundSuspended()` gate beside the Load/Save gate);
queue drain, barriers and handover continue.

**In-place append bound.** `pat_tryAppendAutomation()` never grows a block
into the swap block.

Full behaviour, engine phases and trace layouts:
`COPYCLEAR_UTILITIES.md` §12 and §15.

### 12.18 Session 075 finding: Pattern Load fan-out

Pattern Load's fan-out `memcpy` into Scenes other than the loaded one is not
publication-ordered and can tear one playback tick of a playing destination.
Logged in `SCOPING_TARGETS.md` ("Session 075 findings"); not changed in S075.
