#pragma once
// The shop (shop.cpp), consumables (consumables.cpp) and booster packs (boosters.cpp).
#include <string>

#include "core/value.h"

namespace vl {

class Card;
class CardArea;
class UIElement;

// shop.cpp
Value get_pack(const std::string &key = "", const std::string &type = "");
void create_shop_card_ui(Card *card, const std::string &type = "", CardArea *area = nullptr);
Card *create_card_for_shop(CardArea *area);
void change_shop_size(int mod);
TableRef shop_definition();  // G.UIDEF.shop()
void shop_update();          // Game:update_shop
void set_voucher_usage(Card *card);
void card_apply_to_run(Card *self, const Value &center);  // Card.apply_to_run(self, center)
void alert_no_space(Card *card, CardArea *area);
bool can_buy(Card *c);
bool can_buy_and_use(Card *c);
bool check_for_buy_space(Card *card);
void buy_from_shop(Card *c1, const std::string &id, UIElement *e);
// G.FUNCS.use_card(e, mute, nosave) with e.config.ref_table = card (e may be nil)
void use_card(Card *card, UIElement *e, bool mute = false, bool nosave = false);
void create_drag_target_from_card(Card *card);
void register_shop_callbacks();

// consumables.cpp
void end_consumeable(double delayfac = 1);  // G.FUNCS.end_consumeable
bool can_select_card(Card *card);           // G.FUNCS.can_select_card
void register_consumable_callbacks();

// boosters.cpp
void pack_update(int state);  // Game:update_{arcana,celestial,spectral,standard,buffoon}_pack
void register_booster_callbacks();

}  // namespace vl
