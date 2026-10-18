#pragma once
// functions/UI_definitions.lua: functions that build UI definition tables.
#include "core/value.h"

namespace vl {

TableRef create_popup_UIBox_tooltip(const Value &tooltip);
TableRef create_UIBox_detailed_tooltip(const Value &joker);
TableRef create_UIBox_card_alert(const TableRef &args = nullptr);
TableRef create_UIBox_main_menu_buttons();
TableRef create_UIBox_profile_button();
TableRef create_UIBox_notify_alert(const Value &achievement, const Value &type, bool left);
TableRef UIBox_button(const TableRef &args);
TableRef create_UIBox_generic_options(const TableRef &args);
TableRef create_UIBox_options();
TableRef create_UIBox_settings();
TableRef settings_tab(const Value &tab);  // G.UIDEF.settings_tab
TableRef create_tabs(const TableRef &args);
TableRef create_option_cycle(const TableRef &args);
TableRef create_toggle(const TableRef &args);
TableRef create_slider(const TableRef &args);
TableRef create_button_binding_pip(const TableRef &args);
TableRef language_selector();  // G.UIDEF.language_selector
TableRef overlay_infotip(const Value &text_rows);

}  // namespace vl
