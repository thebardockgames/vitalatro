#pragma once
// Global Lua functions of functions/misc_functions.lua and functions/common_events.lua.
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/value.h"
#include "engine/event.h"

namespace vl {

class Card;
class CardArea;
struct Atlas;

// ------------------------------------------------------------------ misc_functions.lua
// pseudorandom_element(_t, seed): returns the value; *key_out receives the key
Value pseudorandom_element(const Value &t, const double *seed = nullptr, Value *key_out = nullptr);
double pseudoseed(std::string_view key);
double pseudoseed(std::string_view key, std::string_view predict_seed);
double pseudorandom(double seed);
double pseudorandom(double seed, double min, double max);
inline double pseudorandom(std::string_view key) { return pseudorandom(pseudoseed(key)); }
std::string random_string(int length, double seed);
std::vector<Card *> find_joker(std::string_view name, bool non_debuff = false);
Value get_joker_win_sticker(const Value &center);
int get_joker_win_sticker_index(const Value &center);
int get_deck_win_stake(const Value &key);
// data/<name>.lua, extracted from the game at build time (fatal when missing)
TableRef load_data(const std::string &name);
void set_profile_progress();
void set_discover_tallies();
void init_localization();
Value localize(std::string_view key, const char *misc_cat = nullptr);
Value localize(const TableRef &args);
TableRef loc_parse_string(std::string_view line);
Value loc_colour(const Value &name, const Value &default_col = Value());
TableRef profile_init_table();
bool get_cloud_flag(std::string_view flag);
void set_cloud_flag(std::string_view flag);
std::pair<Atlas *, Value> get_front_spriteinfo(const Value &front);
void reset_drawhash();
void RESET_STATES(int state);
Value get_stake_col(int stake);
std::string number_format(const Value &num);

// ------------------------------------------------------------------ common_events.lua
void set_screen_positions();
// ease_value(ref_table, ref_value, mod, floored, timer_type, not_blockable, delay, ease_type). Every caller
// leaves not_blockable nil or true, so these events are never blockable.
void ease_value(ValueRef ref, double mod, bool floored = false, const EventTimer *timer = nullptr, double delay = 0.3,
                EaseType ease = EaseType::Lerp);
void ease_background_colour(const TableRef &args);
void ease_colour(const TableRef &old_colour, const Value &new_colour, double delay = 0.3);
void delay(double time, EventManager::Queue queue = EventManager::BASE);
void set_alerts();
void set_main_menu_UI();
void update_canvas_juice(double dt);
void check_for_unlock(const TableRef &args);
void unlock_card(const Value &card);
void discover_card(const Value &card);
void unlock_notify();
void fetch_achievements();
void notify_alert(const Value &achievement, const Value &type = Value());

}  // namespace vl
