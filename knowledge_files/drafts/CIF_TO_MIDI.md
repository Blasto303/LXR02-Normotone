# Crystal Diffraction Sonifier: Draft Spec (v0.1)

A pattern generator that turns a crystal structure (CIF) into note events by simulating a rotating, precessing crystal in an electron beam. Reflections sound while they are near the Ewald sphere. Everything musical is derived from kinematic electron-diffraction quantities.

Three parts:

1. **Builder** (Python, offline): CIF -> `.xtl` block file with a pre-rendered reflection list.
2. **Synth** (STM32F765, C): loads `.xtl` from SD, runs the rotation/excitation loop, generates notes, sends SysEx.
3. **Visualizer** (Python, laptop): unit cell + highlighted planes, driven by the synth's SysEx.

```
 CIF --> [builder.py] --> name.xtl --+
  |                                  v
  |                         SD card: name.cif + name.xtl
  |                                  |
  |                          [synth, STM32F765]
  |                         params, rotation, gating,
  |                         pitch/timbre mapping, voices
  |                            |              |
  |                      audio / MIDI      SysEx (USB MIDI)
  |                                           |
  +-----> [visualizer.py] <-------------------+
```

## 1. Goals and non-goals

**Goals**
- Principled mapping: what sounds, when, how loud and how long all follow from diffraction physics.
- Minimal on-device compute: no CIF parsing, no structure-factor calculation on the MCU.
- Every musical and physical option adjustable live on-device.
- Deterministic: same file + params + start phase = same pattern, so the visualizer can stay in sync.

**Non-goals (v0.1)**
- Dynamical scattering (multiple scattering, Pendellosung). Kinematic only. Thickness `t` is a shape parameter, not a claim of accuracy.
- Absorption, inelastic scattering, Kikuchi lines.
- On-device CIF parsing (see open questions).

## 2. Conventions

- Reciprocal vectors use **g = 1/d** (no 2pi factor). Units: A^-1.
- Crystal Cartesian frame: **a || x, b in the xy plane** (standard crystallographic setting). The builder writes the orthogonalization matrix to the header, and the visualizer must use that matrix, not its own.
- Lab frame: beam travels along **+z**, wavevector **k = (0, 0, 1/lambda)**.
- Rotation matrices are 3x3 float32. Orientation also sent as unit quaternions.

## 3. Physical model

### 3.1 Beam

Relativistic electron wavelength from accelerating voltage V (volts):

```
lambda [A] = 12.2643 / sqrt( V * (1 + 0.97845e-6 * V) )
```

Example: 200 kV -> 0.02508 A, 100 kV -> 0.0370 A, 300 kV -> 0.0197 A. Because lambda is tiny, the Ewald sphere is almost flat, so many reflections sit near it at once (zone axes become chords).

### 3.2 Orientation over time

Spin about a crystal-fixed axis, whose lab direction precesses around a lab axis:

```
R(t) = Rot(p, w_p*t + zeta0) * Rot(a0, phi0 + w_s*t) * Align([uvw]_s -> a0)
```

- `[uvw]_s`: spin axis as a **crystal direction** (default [001]).
- `p`: precession axis in the lab frame (polar angle from beam, azimuth). Default: lab +y, perpendicular to the beam.
- `a0`: initial spin-axis direction, tilted from `p` by the **cone half-angle theta_c** (default 15 deg) at initial azimuth `zeta0`.
- `w_s`: spin rate, `w_p`: precession rate. Units are deg/s (free) or deg/beat (clock-synced).
- `phi0`: initial spin phase.
- With theta_c = 0 this reduces to plain rotation about `p`.

**Note on beam-axis rotations:** rotating about the beam (z) changes nothing audible, since excitation error depends only on the z-component of g. The visualizer still shows it. Avoid setting `p` parallel to the beam unless you want a static chord with a spinning picture.

### 3.3 Excitation error

For reflection g0 (crystal frame, from `.xtl`), the rotated vector is g' = R(t)*g0. Then:

```
s_g(t) = -( g'_z + lambda * |g|^2 / 2 )
```

- Only the **third row of R** is needed: `g'_z = r3 . g0`. That is 3 MACs per reflection per tick. `|g|^2` is stored in the file.
- s > 0: reciprocal point inside the Ewald sphere. Sign is irrelevant for gating.
- s = 0 is exact Bragg condition.

### 3.4 Relrod shape and gating (duration)

Finite thickness `t` turns each reciprocal point into a rod of length ~1/t. Kinematic intensity vs excitation error:

```
E(s) = sinc^2(pi * t * s)          // sinc(x) = sin(x)/x
```

**Note gate (this defines duration):** a reflection is *on* while

```
|s_g| < s_w(g) = kappa / t  +  alpha * |g|
```

- `kappa`: gate width factor (default 0.5, roughly the FWHM of E(s)).
- `alpha`: beam convergence semi-angle (rad), see 3.5.
- **Hysteresis:** turn on at `s_w`, off at `s_w * (1 + h)`, with h = 0.1, to prevent chatter at the threshold.
- Duration is *emergent*: it is the dwell time inside the window. For a reflection crossing at the fastest rate (grazing crossings last longer):

```
T_min ~= 2 * ( kappa/t + alpha*|g| ) / ( w * |g| )       // w = angular rate in rad/s
```

Consequences:
- Thickness term `kappa/(t*w*|g|)` falls with |g|: **high pitches are short, low pitches are longer** (natural).
- Convergence term `2*alpha/w` is independent of |g|: it adds a constant floor, so **convergence lengthens every note, and high notes proportionally most**.
- A reflection that stays near the sphere (e.g. spin about a zone axis) sustains as a drone. Optional `max_note_ms` cap (default off).
- Worked example: t = 200 A, kappa = 0.5, alpha = 0, |g| = 0.5 A^-1, w = 2 deg/s -> T_min ~ 0.29 s. With alpha = 2 mrad -> ~0.40 s.

### 3.5 Beam convergence

A convergent beam makes the Ewald sphere "thick": tilting the incident direction by up to alpha shifts s by up to alpha*|g| (units A^-1). Model:

```
s_eff = max(0, |s_g| - alpha*|g|)
E_conv(s) = sinc^2(pi * t * s_eff)
```

This gives each note a natural shape: **attack** (s approaching the window), **sustain plateau** while |s| < alpha*|g|, **release** on the way out. With alpha = 0 it collapses to a plain relrod pulse. Typical alpha: 0 (parallel), 0.5-5 mrad (nano-beam), 5-30 mrad (CBED).

### 3.6 Intensity and loudness

Kinematic intensity, precomputed at build time and stored relative to the strongest reflection:

```
I_rel(g) = |F_g|^2 / max|F|^2          // from .xtl, includes electron scattering factors, B-factors, occupancy
level(g,t) = I_rel(g) * E_conv(s_g(t))
amp = level ^ gamma                    // gamma default 0.3 (diffraction intensities span orders of magnitude)
```

Systematic absences (|F|^2 ~ 0) are removed by the builder, so forbidden reflections never sound.

## 4. Musical mapping

### 4.1 Pitch

Pitch comes from |g| = 1/d:

```
p_raw = n_ref + 12 * k * log2( |g| / g_ref )          // semitones (float)
```

- **k = 1.0** (default): frequency is proportional to |g|. Higher orders of one plane family (nh,nk,nl) become exact harmonics n*f0, and cubic cells yield sqrt(h^2+k^2+l^2) ratios. `k < 1` compresses the range, `k > 1` expands it.
- `g_ref`: `auto` (smallest |g| in the active set, the "fundamental") or a user d_ref.
- `n_ref`: MIDI note for g_ref (default 36).

Pitch pipeline (each stage optional, applied in this order):

| Stage | Switch | Behavior |
|---|---|---|
| 1. Mapping | always | `p_raw` as above |
| 2. Scale | `scale_on` | Snap to nearest member of scale (12-bit mask + root); ties go down. Implies quantization. Presets: chromatic, major, minor, pentatonic, whole-tone, custom mask |
| 3. Quantize | `quant_on` | If scale off: round to nearest semitone. If both off: keep fractional pitch (microtonal) |
| 4. Range | `range_on` | Constrain to [lo, hi]. Policies: `drop`, `clamp` (nearest allowed pitch, scale-aware), `fold` (octave-shift into range, preserves pitch class), `fit` (rescale k so active [g_min, g_max] spans [lo, hi]) |

Fractional pitch output: internal synth takes float semitones directly. MIDI out uses pitch bend with **one channel per voice** (MPE-style), since pitch bend is per-channel.

**Coalescing:** if several reflections land on the same quantized pitch, they share one voice via a refcount. Note-off fires when the last contributor releases, with amp = max of contributors.

### 4.2 Velocity / amplitude

- Note-on velocity = `amp` at entry.
- While on, `amp` tracks `I_rel * E_conv(s)^...` each control tick, delivered as a per-voice envelope (internal), or poly aftertouch (MIDI, optional, <= 60 Hz).
- Options: `gamma` (0.1-1.0), floor (below this, note is not triggered), velocity curve (power / log).

### 4.3 Timbre

- Builder stores, per reflection, up to 4 (species_id, weight) pairs. Weights are normalized |f_j(g)| contributions, summed per species.
- Device has a **species -> patch table** (up to 8 species; default patch = species index; user-editable).
- Each voice receives up to 4 (patch, weight) pairs for the engine to blend or morph.
- MIDI out: dominant species -> channel/program; secondary weights -> optional CC.

### 4.4 Other optional modulations (each on/off, default noted)

| Modulation | Source | Target | Default |
|---|---|---|---|
| Pan | spot position on detector, g'_x/|g| | stereo pan | on |
| Brightness | planar density sigma = N*d/V_cell (precomputed) | filter / brightness | off |
| Excitation detune | s / s_w | pitch +/- `s_detune_cents` | off (0 cents) |
| Order-dependent brightness | n = gcd(h,k,l) or |g|/g_ref | brightness | off |

### 4.5 Voices and timing

- Max voices (default 16). Steal lowest current `amp` first.
- Intensity threshold: ignore reflections below a fraction of max `I_rel` (default 0.001).
- Resolution cutoff: `d_min` / `|g|_max` runtime filter. The file is sorted by |g| so this is a prefix cut.
- Control tick: default 1 kHz. Event timestamps are quantized to ticks (optionally interpolate the s crossing for sub-tick timing).
- Time base: `free` (deg/s) or `clock` (deg/beat, locked to MIDI clock/tempo).
- Optional event quantize: delay note-ons to a grid (off, 1/4 ... 1/32).
- Optional loop reset: reset phases every N beats/seconds so the pattern repeats exactly.
- Rhythm for free: symmetry shows up as repetition (a 4-fold axis spin yields 4 equivalent events per revolution).

## 5. On-device parameters

All adjustable live. IDs defined once in `params.h`, with a generated Python mirror for the visualizer. Defaults are suggestions.

**Beam / specimen**

| Param | Range | Default |
|---|---|---|
| `accel_kV` | 20-300 | 200 |
| `thickness_A` | 10-2000 | 200 |
| `convergence_mrad` | 0-30 | 0 |
| `kappa` | 0.1-2.0 | 0.5 |
| `gate_hyst` | 0-0.5 | 0.1 |
| `max_note_ms` | off / 50-60000 | off |

**Motion**

| Param | Range | Default |
|---|---|---|
| `spin_uvw` | crystal direction | [001] |
| `spin_rate` | -360..360 deg/s or deg/beat | 3 |
| `prec_axis` | polar, azimuth (lab) | +y |
| `prec_cone_deg` | 0-90 | 15 |
| `prec_rate` | -360..360 | 0.5 |
| `spin_phase`, `prec_phase` | deg | 0 |
| `time_mode` | free / clock | free |

**Pitch**

| Param | Range | Default |
|---|---|---|
| `k_octaves_per_doubling` | 0.1-4 | 1.0 |
| `n_ref`, `g_ref_mode` | MIDI note, auto/manual | 36, auto |
| `scale_on`, `scale_root`, `scale_mask` | | off, C, chromatic |
| `quant_on` | | off |
| `range_on`, `range_lo`, `range_hi`, `range_policy` | | off, 24, 96, fold |

**Voice / dynamics**

| Param | Range | Default |
|---|---|---|
| `max_voices` | 1-32 | 16 |
| `gamma` | 0.1-1.0 | 0.3 |
| `i_floor` | 0-0.1 | 0.001 |
| `d_min_A` | runtime cut | file value |
| `patch_table[8]` | | by index |
| mod toggles (4.4) | | see table |

## 6. `.xtl` file format (little-endian)

Pre-rendered by the builder, read by both synth and visualizer.

**Header**

| Field | Type | Notes |
|---|---|---|
| magic | char[4] | `XTL1` |
| version | u16 | 1 |
| n_refl | u32 | |
| cif_crc32 | u32 | ties file to its CIF |
| file_crc32 | u32 | over body |
| name | char[32] | |
| cell | float32[6] | a b c alpha beta gamma |
| orth | float32[9] | cell -> Cartesian matrix |
| recip | float32[9] | recip -> Cartesian (g = B*(h,k,l)) |
| v_cell | float32 | A^3 |
| n_atoms_cell | u16 | |
| n_species | u8 | <= 8 |
| species[8] | {char sym[3], u8 Z} | |
| g_max, i_max | float32 | build limits |

**Reflection record (32 bytes, sorted ascending |g|)**

| Field | Type |
|---|---|
| h, k, l | int8 x3 |
| n_species_used | u8 |
| g (crystal Cartesian) | float32 x3 |
| g2 = |g|^2 | float32 |
| I_rel | float32 |
| species slots | 4 x {u8 id, u8 weight} |

`n_refl` default cap 4096 (about 128 KB). Keep the working set in on-chip SRAM. Optionally enlarge with external memory later.

## 7. Builder (Python)

`xtlbuild.py structure.cif -o structure.xtl --dmin 0.6 --nmax 4096 --voltage-independent`

1. Parse CIF with **gemmi** (symmetry expansion, cell, occupancies, ADPs). pymatgen is an alternative.
2. Build orth/recip matrices per the frame convention in section 2.
3. Enumerate **all** hkl (not only symmetry-unique) up to `d_min`. Rotation sweeps +g and -g separately, so each must exist.
4. Compute F(g) with **electron** scattering factors (Peng/Kirkland parameterization or Mott-Bethe from X-ray factors), including B-factors and occupancy. Voltage only changes overall scale, so it is excluded from the file.
5. Drop |F|^2 < epsilon (absences); if over `n_max`, keep the strongest/lowest-|g|.
6. Compute per-reflection species weights and planar density (optional field).
7. Sort by |g|, write file. Print summary (counts, g range, pitch span for k=1).
8. Also copy the CIF alongside (same stem) for the visualizer.

## 8. On-device computation budget

Per control tick:
1. Update R(t): two Rodrigues rotations + align matrix (~100 flops).
2. For each reflection: `s = -(r3.g + 0.5*lambda*g2)`, then compare against `s_w` (computed as `kappa/t + alpha*sqrt(g2)`, cache `|g|` or compute on param change). About 8 flops.
3. State machine on gate crossings -> note-on/off.
4. For active voices only: update envelope (sinc^2 via LUT), pan (needs g'_x, g'_y).

At N = 4096, 1 kHz: ~35 Mflop/s, trivial for a Cortex-M7 with FPU at 216 MHz. SD access happens only at load time. Per-reflection runtime state (gate bit, voice id, previous s): ~8 bytes.

LUTs: `sinc^2` (512 entries), `log2`, scale tables.

## 9. SysEx protocol (synth <-> visualizer)

Manufacturer ID `0x7D` (non-commercial/educational). Transport: **USB MIDI** (DIN is too slow for dense chords: 3.1 kB/s).

Frame: `F0 7D <dev_id> <msg> <payload...> <chk> F7`. All data bytes 7-bit. `chk` = XOR of payload & 0x7F. Integers MSB-first in septets: `u14` = 2 bytes, `u21` = 3, `u28` = 4, `i32` = 5 (offset-binary).

| Msg | Dir | Payload |
|---|---|---|
| `0x01` HELLO | synth -> PC | proto ver, crystal `file_crc32` (i32), name (ASCII) |
| `0x02` PARAM | both | param_id u14, value i32 |
| `0x03` DUMP_REQ | PC -> synth | none; synth replies with HELLO, all PARAMs, all active notes |
| `0x10` FRAME | synth -> PC | t_ms u28, quaternion 4 x u14 (offset-binary, q in [-1,1]), n_active u7. Sent at 30 Hz |
| `0x20` NOTE_ON | synth -> PC | refl_idx u21, voice u7, pitch u14 (1/128 semitone), amp u7, s u14 (scaled by s_w), lead_species u7 |
| `0x21` NOTE_OFF | synth -> PC | refl_idx u21, voice u7 |
| `0x22` ENV | synth -> PC | n u7, then n x (voice u7, amp u7). Rate <= 30 Hz |
| `0x30` ALL_OFF / RESET | synth -> PC | none (clears highlights, resyncs phase) |

The visualizer resolves `refl_idx` to (h,k,l) using the same `.xtl` (matched by `file_crc32`), and loads the CIF from the same stem. If it does not have the file, it reports a mismatch.

Late connection: PC sends `DUMP_REQ`; the synth replies with the current state. Bandwidth: typical load (30 Hz FRAME + a few notes/s) is well under 1 kB/s.

## 10. Visualizer (Python, laptop)

- Libraries: pyvista or vispy for 3D, `python-rtmidi` or `mido` for MIDI, gemmi for the CIF.
- Draw: unit cell edges, atoms (colored by species, sized by radius), beam arrow, spin and precession axes.
- Cell orientation: apply the received quaternion each FRAME. Optional local interpolation between frames.
- **Plane highlighting:** on NOTE_ON, draw the (hkl) plane family clipped to the cell (translucent polygons, a few planes through the cell, spacing d). Opacity follows `ENV`. Color by pitch class or dominant species. Remove on NOTE_OFF.
- Optional second panel: **diffraction pattern view**. Projected spots (g'_x, g'_y) sized by `I_rel`, excited ones lit, Ewald circle overlay.
- Optional: HUD showing current params (lambda, t, alpha, rates), active notes list (hkl, d, pitch).
- Stays correct under param edits made on either side via PARAM messages.

## 11. Related techniques (grounding)

- **Rotation / 3D electron diffraction**: crystal rotation about an axis sweeps reflections through the Ewald sphere. Matches the default setup (spin axis perpendicular to beam).
- **Precession electron diffraction (PED)**: beam precesses on a cone and integrates intensity. Geometrically similar to the precession here (the crystal wobble is equivalent to a conical beam tilt). A "PED-like" mode could be added by tilting k instead of R.
- **CBED**: convergent beam gives disk-shaped reflections. Our convergence model is the first-order version of that.

## 12. Open questions

1. **On-device CIF parsing?** The spec assumes pre-rendered `.xtl` files built on a laptop. A tiny on-device parser is possible but pulls in symmetry expansion and scattering factors. Is "SD with .cif + .xtl, builder run on laptop" acceptable?
2. **Internal events vs. strict MIDI?** The internal engine takes float pitch, per-voice amp curves and multiple timbre weights. MIDI out is a lossy projection. Which is primary?
3. **Loop/pattern length:** commensurate rates (w_s : w_p rational) give a closed loop. Do you want the device to compute the loop period and expose it, or just use `phase_reset`?
4. **Relative gating vs. absolute:** should weak reflections be gated by `i_floor` only, or also scaled so quiet planes still read in dense chords?
5. **Dynamical effects** (extinction distance as a duration/brightness modulator) as a v0.2 option?
6. **Memory:** is a 4096-reflection cap enough, or is external RAM available for ~16k?
7. **Audio engine interface:** patch blend format (up to 4 weights per voice) to be agreed with the existing synth code.

## 13. Suggested milestones

1. Builder -> `.xtl` for a simple structure (Si, NaCl, a perovskite); validate against pymatgen's diffraction pattern.
2. Python reference simulator: orientation, s_g, gating, note log (CSV). Listen via MIDI. Tune defaults.
3. C port of the loop, tested against the reference log.
4. SD loading, param UI, voice allocator.
5. SysEx + visualizer (cell render, plane highlights).
6. Pattern/loop features, clock sync, spot-pattern view.
