#ifndef PLAITS_HAT_PARAMETERS_H_
#define PLAITS_HAT_PARAMETERS_H_

#include "InstrumentManager.h"

/*
 * Plaits hi-hat (`pht`) registry exports.
 *
 * Descriptor table, menu pages, Load-browser label and type flags of the
 * Plaits hi-hat instrument. Basic + Choke: visible track 7 plays the open
 * hat through the `phat_decay_choke` sibling. Affiliates: PlaitsHatVoice.c/.h,
 * InstrumentManager registry.
 */
#define PLAITS_HAT_PARAM_DESCRIPTOR_COUNT 29u
#define PLAITS_HAT_MENU_PAGE_COUNT       8u
#define PLAITS_HAT_INSTRUMENT_TYPE_FLAGS (INSTRUMENT_FLAG_BASIC | INSTRUMENT_FLAG_CHOKE)

extern const ParamDescriptor plaits_hat_param_descriptors[];
extern const uint8_t plaits_hat_param_descriptor_count;
extern const instrument_menu_page_t plaits_hat_menu_pages[];
extern const uint8_t plaits_hat_menu_page_count;
extern const char plaits_hat_instrument_display_label[];
extern const uint8_t plaits_hat_instrument_type_flags;

#endif
