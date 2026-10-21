#ifndef PLAITS_SNARE_PARAMETERS_H_
#define PLAITS_SNARE_PARAMETERS_H_

#include "InstrumentManager.h"

/*
 * Plaits analog snare (`psn`) registry exports.
 *
 * Descriptor table, menu pages, Load-browser label and type flags of the
 * Plaits snare instrument. Basic tier: five fixed-coefficient resonators
 * and a noise band-pass, about the cost of the LXR Snare. Affiliates: PlaitsSnareVoice.c/.h, InstrumentManager registry.
 */
#define PLAITS_SNARE_PARAM_DESCRIPTOR_COUNT 27u
#define PLAITS_SNARE_MENU_PAGE_COUNT       8u
#define PLAITS_SNARE_INSTRUMENT_TYPE_FLAGS INSTRUMENT_FLAG_BASIC

extern const ParamDescriptor plaits_snare_param_descriptors[];
extern const uint8_t plaits_snare_param_descriptor_count;
extern const instrument_menu_page_t plaits_snare_menu_pages[];
extern const uint8_t plaits_snare_menu_page_count;
extern const char plaits_snare_instrument_display_label[];
extern const uint8_t plaits_snare_instrument_type_flags;

#endif
