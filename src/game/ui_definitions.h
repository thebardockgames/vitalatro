#pragma once
// functions/UI_definitions.lua: functions that build UI definition tables.
#include <string>

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
Value ui_data(const Value &extracted);  // a live copy of a UI tree from data/ui_*.lua
TableRef credits();                     // G.UIDEF.credits
TableRef viewed_collab_option();        // G.UIDEF.viewed_collab_option
TableRef create_UIBox_customize_deck();
TableRef custom_deck_tab(std::string_view suit);  // G.UIDEF.custom_deck_tab
double score_number_scale(double scale, const Value &amt);
TableRef create_progress_box(const Value &profile_progress, bool smaller);
TableRef create_UIBox_high_scores();
TableRef create_UIBox_usage(const Value &args);
TableRef usage_tabs();  // G.UIDEF.usage_tabs
TableRef create_text_input(const TableRef &args);
TableRef create_keyboard_input(const TableRef &args);
TableRef profile_select();             // G.UIDEF.profile_select
TableRef profile_option(int profile);  // G.UIDEF.profile_option
TableRef create_UIBox_your_collection();
// the card pages: kind is the pool (Joker, Tarot, Planet, Spectral, Voucher, Booster)
TableRef create_UIBox_your_collection_cards(const std::string &kind);
void collection_fill(const std::string &kind, int page);  // a page's cards + INIT_COLLECTION_CARD_ALERTS
// Seal, Enhanced, Edition
TableRef create_UIBox_your_collection_special(const std::string &kind);

}  // namespace vl
