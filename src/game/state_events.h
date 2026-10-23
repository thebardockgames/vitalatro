#pragma once
// functions/state_events.lua, and the run helpers of common_events.lua / misc_functions.lua that
// starting a run, choosing a blind and drawing a hand need.
#include <string>
#include <utility>
#include <vector>

#include "core/value.h"
#include "game/card.h"

namespace vl {

class Sprite;
class UIElement;

// misc_functions.lua
void stop_use();
void inc_career_stat(const char *stat, double mod);
void check_and_set_high_score(const char *score, const Value &amt);
void save_run();
double scale_number(const Value &number, double scale, double max = 10000);
void pseudoshuffle(std::vector<CardRef> &list, double seed);
std::string generate_starting_seed();
Value get_deck_from_name(const Value &name);
Sprite *get_stake_sprite(int stake, float scale = 1);
void card_from_control(const TableRef &control);

// common_events.lua
void calculate_reroll_cost(bool skip_increment = false);
void reset_idol_card();
void reset_mail_rank();
void reset_ancient_card();
void reset_castle_card();
void reset_blinds();
Value get_new_boss();
// get_current_pool(_type, _rarity, _legendary, _append): the pool and its seed key
std::pair<TableRef, std::string> get_current_pool(const char *type, const Value &rarity = Value(), bool legendary = false,
                                                  const std::string &append = "");
Value get_next_voucher_key(bool from_tag = false);
Value get_next_tag_key(const std::string &append = "");
void ease_background_colour_blind(int state, const char *blind_override = nullptr);
void ease_round(double mod);
void ease_ante(double mod);
// draw_card(from, to, percent, dir, sort, card, delay, mute, stay_flipped, vol, discarded_only)
void draw_card(CardArea *from, CardArea *to, double percent = 50, const char *dir = nullptr, bool sort = false, Card *card = nullptr,
               double delay = 0.1, bool mute = false, bool stay_flipped = false, double vol = 1, bool discarded_only = false);

// state_events.lua
void new_round();
// G.FUNCS.draw_from_deck_to_hand(e): e = cards to draw, or -1 for the free hand space
bool draw_from_deck_to_hand(int e = -1);

// the run's G.FUNCS (HUD funcs, blind choice, hand buttons)
void register_run_callbacks();
// G.FUNCS.start_run(e, args)
void start_run_with(UIElement *e, const TableRef &args);

}  // namespace vl
