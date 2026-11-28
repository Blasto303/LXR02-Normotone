/*
 * LIVE mode for the LXR-02 (Normotone fork).
 *
 * A Bank is played the way the stock LXR-02 plays a commercial project:
 * Scene N = pattern N + kit N. On top of Helicase's PERF page:
 *   - SHIFT + step queues pattern N for the end of the bar (Helicase qcd),
 *   - hold LOAD + step 7 opens the live scene-kit page: a step press puts kit N
 *     on the playing pattern at the end of the bar; leaving that pattern
 *     gives it its own kit back,
 *   - the Effect, compressor and voice mix (outputs, FX sends, faders) follow
 *     the player across pattern changes, so the rumble on DRUM1 never drops,
 *   - SAVE opens the Bank list only (no Save page, no Scene writing); a
 *     loaded Bank starts on its pattern 1 at the end of the bar.
 * Step labels of the LXR-02 panel: LOAD + KIT (8) = SD kit library, LOAD +
 * PROJECT (11) = Bank list, SHIFT/LOAD + PATTERN (16) = PATTERN page,
 * SHIFT + FX (15) = Effect, SHIFT + CONFIG (12) = settings, SHIFT + RELOAD
 * (13) = own kit back. SHIFT + steps 1..11 in PERF still queue directly.
 * LIVE_MODE 0 builds the stock Helicase panel.
 */
#ifndef LIVE_MODE_H_
#define LIVE_MODE_H_

#include <stdint.h>

#ifndef LIVE_MODE
#define LIVE_MODE 1
#endif

/* Seed the unsaved factory mix before boot activates the first Scene. */
void live_initFactoryRumble(void);
/* Build every PLAITS conversion table now (boot, before audio). */
void live_buildPlaitsTables(void);
/* Simple Morph (LOAD + MORPH): step = kit of pattern N, again = none. */
void live_morphSelect(uint8_t step);
void live_morphLeds(void);
void live_morphCaptured(void);
uint8_t live_morphSource(void);

/* KIT page state, owned by liveMode.c, driven by buttonHandler.c. */
void live_kitPageOpen(void);
void live_kitPageClose(void);
void live_kitLoadCommitted(uint16_t scene_mask);
uint8_t live_kitPageActive(void);
/* Step press on the KIT page: kit of Scene `scene` for the playing pattern. */
void live_kitSelect(uint8_t scene);
/*
 * DATA on the KIT page: turn right = kit stays on pattern change ("fixe"),
 * left = the pattern brings its kit ("suit", stock); click = PLAITS on/off.
 */
void live_encoder(int8_t inc, uint8_t clicked);
/* PATTERN page (SHIFT or LOAD + step 16): a step queues pattern N. */
void live_patternPageOpen(void);
uint8_t live_patternPageActive(void);
void live_patternSelect(uint8_t scene);
/*
 * PLAITS mode (plaitsMode.h): every kit plays on Plaits engines, the patterns
 * stay. Toggled by DATA click on the KIT page or LOAD + RELOAD (step 13);
 * applied at the bar while playing, at once when stopped.
 */
void live_plaitsToggle(void);
/* Kit option suit / fixe (SHIFT + DATA click in the kit library). */
void live_kitHoldToggle(void);
/* Project Save: own kits around the save (menu.c). */
void live_beforeProjectSave(void);
void live_afterProjectSave(void);
uint8_t live_plaitsActive(void);
/* SHIFT + RELOAD (step 13): the playing pattern gets its own kit back. */
void live_reloadKit(void);
/*
 * RANDOM page (SHIFT + LAST STEP, step 14): steps 1/2/3 = pattern, kit or
 * both, DATA = dose (0 = original), DATA click = new draw for the kit.
 */
void live_rndPageOpen(void);
/* CPU page (LOAD + CONFIG, step 12): DATA scrolls (cpuProfile.h). */
void live_cpuPageOpen(void);
/*
 * EUCLID page (LOAD + LAST STEP, step 14): pots 1/2/3 = length / hits /
 * rotation of the selected track (track buttons), DATA = hits. The first
 * turn rewrites the track's triggers; the track loop length follows len.
 */
/* GEN page (LOAD + LAST STEP): per-track generators, see TrackGen.h. */
void live_genPageOpen(void);
uint8_t live_genPageActive(void);
void live_genTrack(uint8_t track);
void live_genStep(uint8_t step);
void live_genKnob(uint8_t knob, int8_t delta);
/* PERF pot 2: hits (Euclid) or prb (Turing/standard) of the active track. */
void live_genMacro(int8_t delta);
/* PERF pot 3: NUM mode global prime (0..31) of the active Pattern. */
void live_genPrimeMacro(int8_t delta);
/* PERF pot 4: DJ filter, centre off, left low-pass, right high-pass. */
void live_djFilter(int8_t delta);
uint8_t live_cpuPageActive(void);
uint8_t live_rndPageActive(void);
void live_rndStep(uint8_t step);
void live_rndPageLeds(void);
/* Repaint the step LEDs of the KIT page. */
void live_kitPageLeds(void);

/* Called by menu_perfSceneChangeExecute() before a Scene change. */
void live_beforeSceneChange(uint8_t target);
/*
 * Called by menu_serviceDeferredSceneChange() when a queued change fires.
 * Returns 1 when the fire only carried a pending kit change (the Scene
 * change itself must be skipped).
 */
uint8_t live_queuedFire(uint8_t scene);
/* Foreground service (every buttonHandler tick): retries a pending kit. */
void live_service(void);

/* LCD overlay for the KIT page (two 16-char rows), 1 when drawn. */
uint8_t live_formatOverlay(char *row0, char *row1);

/* PERF held + step: Morph glide to pattern `step` (1 = started, 0 = do a
 * plain change: stopped, same or empty pattern). */
uint8_t live_glideStart(uint8_t step);
uint8_t live_glideActive(void);

#endif /* LIVE_MODE_H_ */
