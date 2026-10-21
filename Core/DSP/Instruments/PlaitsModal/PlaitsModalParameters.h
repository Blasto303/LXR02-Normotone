#ifndef PLAITS_MODAL_PARAMETERS_H_
#define PLAITS_MODAL_PARAMETERS_H_

#include "InstrumentManager.h"

/*
 * Plaits modal resonator (`pmd`) registry exports.
 *
 * Descriptor table, menu pages, Load-browser label and type flags of the
 * Plaits/Rings modal resonator instrument. Advanced tier: 24 band-pass
 * modes per sample. Affiliates: PlaitsModalVoice.c/.h, InstrumentManager
 * registry.
 */
#define PLAITS_MODAL_PARAM_DESCRIPTOR_COUNT 28u
#define PLAITS_MODAL_MENU_PAGE_COUNT       8u
#define PLAITS_MODAL_INSTRUMENT_TYPE_FLAGS INSTRUMENT_FLAG_ADVANCED

extern const ParamDescriptor plaits_modal_param_descriptors[];
extern const uint8_t plaits_modal_param_descriptor_count;
extern const instrument_menu_page_t plaits_modal_menu_pages[];
extern const uint8_t plaits_modal_menu_page_count;
extern const char plaits_modal_instrument_display_label[];
extern const uint8_t plaits_modal_instrument_type_flags;

#endif
