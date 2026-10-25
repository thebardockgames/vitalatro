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
// G.FUNCS.reroll_boss: a new boss on the blind select screen ($10, free from the Boss Tag)
void reroll_boss(class UIElement *e = nullptr);
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

// misc_functions.lua: poker hands (tables of Value(Card*), as the jokers read them)
TableRef evaluate_poker_hand(const std::vector<Card *> &hand);
TableRef get_X_same(int num, const std::vector<Card *> &hand);
TableRef get_flush(const std::vector<Card *> &hand);
TableRef get_straight(const std::vector<Card *> &hand);
TableRef get_highest(const std::vector<Card *> &hand);
void set_hand_usage(const std::string &hand);
struct PokerHandInfo {
    std::string text, disp_text;
    Value loc_disp_text;
    TableRef poker_hands, scoring_hand;
};
PokerHandInfo get_poker_hand_info(const std::vector<Card *> &cards);

// state_events.lua
void new_round();
// G.FUNCS.draw_from_deck_to_hand(e): e = cards to draw, or -1 for the free hand space
bool draw_from_deck_to_hand(int e = -1);

// scoring.cpp: playing hands (state_events.lua) and its common_events.lua helpers
double mod_chips(double chips);
double mod_mult(double mult);
void ease_chips(double mod);
void ease_dollars(double mod, bool instant = false);
void ease_discard(double mod, bool instant = false, bool silent = false);
void ease_hands_played(double mod, bool instant = false);
void highlight_card(Card *card, double percent, const char *dir);
void play_area_status_text(const Value &text, bool silent = false, double delay = 0);
void juice_card(Card *card);
void update_hand_text(const TableRef &config, const TableRef &vals);
void level_up_hand(Moveable *card, const std::string &hand, bool instant = false, double amount = 1);
TableRef eval_card(Card *card, const TableRef &context);
// card_eval_status_text(card, eval_type, amt, percent, dir, extra); percent < 0 = nil
void card_eval_status_text(Card *card, const char *eval_type, double amt = 0, double percent = -1, const char *dir = nullptr,
                           const TableRef &extra = nullptr);
void discard_cards_from_highlighted(bool hook = false);
void play_cards_from_highlighted();
void evaluate_play();
void draw_from_play_to_discard();
void draw_from_discard_to_deck();
void draw_from_hand_to_deck();
void draw_from_hand_to_discard();
void end_round();
void add_round_eval_row(const TableRef &config);
void evaluate_round();
void cash_out(UIElement *e);
// CardArea:parse_highlighted() for the hand: the poker hand preview
void parse_hand_highlight(CardArea *area);

// the run's G.FUNCS (HUD funcs, blind choice, hand buttons)
void register_run_callbacks();
// G.FUNCS.start_run(e, args)
void start_run_with(UIElement *e, const TableRef &args);

}  // namespace vl
