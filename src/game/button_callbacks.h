#pragma once
// functions/button_callbacks.lua (G.FUNCS). Callbacks that buttons and UI funcs call with the UI
// element go into G.FUNCS, the ones called with an args table into G.FUNCS_T.
#include <string>

#include "core/value.h"

namespace vl {

class UIElement;

void register_button_callbacks();

// G.FUNCS.overlay_menu{definition = ..., config = ...}
void overlay_menu(const TableRef &definition, const TableRef &config = nullptr);
void exit_overlay_menu();
// G.FUNCS.wipe_on(message, no_card, timefac, alt_colour) / G.FUNCS.wipe_off()
void wipe_on(const Value &message = Value(), bool no_card = false, double timefac = 1, const Value &alt_colour = Value());
void wipe_off();
// G.FUNCS.slider_descreet(e, per): sliders driven by the d-pad
void slider_descreet(UIElement *e, double per);
// G.FUNCS.text_input_key{key = key, caps = caps} for the hooked text input
void text_input_key(std::string key, bool caps);

}  // namespace vl
