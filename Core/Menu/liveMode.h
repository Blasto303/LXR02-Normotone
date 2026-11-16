/*
 * LIVE mode for the LXR-02 (Normotone fork).
 *
 * A Bank is played the way the stock LXR-02 plays a commercial project:
 * Scene N = pattern N + kit N. On top of Helicase's PERF page:
 *   - SHIFT + step queues pattern N for the end of the bar (Helicase qcd),
 *   - hold LOAD + step 8 (KIT) opens the KIT page: a step press puts kit N
 *     on the playing pattern at the end of the bar; leaving that pattern
 *     gives it its own kit back,
 *   - the Effect, compressor and voice mix (outputs, FX sends, faders) follow
 *     the player across pattern changes, so the rumble on DRUM1 never drops,
 *   - SAVE opens the Bank list only (no Save page, no Scene writing).
 * LIVE_MODE 0 builds the stock Helicase panel.
 */
#ifndef LIVE_MODE_H_
#define LIVE_MODE_H_

#include <stdint.h>

#ifndef LIVE_MODE
#define LIVE_MODE 1
#endif

/* KIT page state, owned by liveMode.c, driven by buttonHandler.c. */
void live_kitPageOpen(void);
void live_kitPageClose(void);
uint8_t live_kitPageActive(void);
/* Step press on the KIT page: kit of Scene `scene` for the playing pattern. */
void live_kitSelect(uint8_t scene);
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

#endif /* LIVE_MODE_H_ */
