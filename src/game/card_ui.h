#pragma once
// Card descriptions (generate_card_ui and the h_popup UI); see card_ui.cpp.
#include <string>

#include "core/value.h"

namespace vl {

class Card;

// generate_card_ui(_c, full_UI_table, specific_vars, card_type, badges, hide_desc, main_start, main_end)
TableRef generate_card_ui(const Value &c, TableRef full, const Value &specific_vars = Value(), const std::string &card_type = "",
                          const TableRef &badges = nullptr, bool hide_desc = false, const Value &main_start = Value(),
                          const Value &main_end = Value());
Value get_type_colour(const Value &c, Card *card = nullptr);
Value get_badge_colour(const std::string &key);
Value create_badge(const Value &str, const Value &badge_col, const Value &text_col = Value(), double scaling = 1);
Value desc_from_rows(const Value &desc_nodes, bool empty = false, const Value &maxw = Value());
Value info_tip_from_rows(const Value &desc_nodes, const Value &name);
Value name_from_rows(const Value &name_nodes, const Value &background_colour = Value());
// G.UIDEF.card_h_popup(card)
TableRef card_h_popup(Card *card);
// the same for any node with an ability_UIBox_table (a tag's sprite: card nil, center = its config)
TableRef card_h_popup(const TableRef &AUT, const Value &center, Card *card = nullptr);
// G.FUNCS.show_infotip / blueprint_compat
void register_card_ui_callbacks();
// G.UIDEF.card_focus_ui(card): the controller's frame and buttons around the focused card
class UIBox;
UIBox *card_focus_ui(Card *card);
void realign_focused_ui(Card *card);
// the size of the focus buttons (1, or 1.4 with large menus on)
double focus_button_scale();
// small HUD details that are unreadable on the Vita's screen (button pips, card area counts):
// 1, or 1.6 with large menus on
double small_detail_scale();

}  // namespace vl
