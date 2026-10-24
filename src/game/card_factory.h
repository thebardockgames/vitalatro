#pragma once
// common_events.lua / misc_functions.lua: making cards at runtime (shops, packs, jokers that
// create or copy cards).
#include <functional>
#include <string>
#include <vector>

#include "core/value.h"
#include "game/card.h"

namespace vl {

// poll_edition(_key, _mod, _no_neg, _guaranteed): an edition table ({foil = true}...) or nil
Value poll_edition(const std::string &key = "edition_generic", double mod = 1, bool no_neg = false, bool guaranteed = false);
// create_card(_type, area, legendary, _rarity, skip_materialize, soulable, forced_key, key_append)
Card *create_card(const std::string &type, CardArea *area, bool legendary = false, const Value &rarity = Value(), bool skip_materialize = false,
                  bool soulable = false, const std::string &forced_key = "", const std::string &key_append = "");
// copy_card(other, new_card, card_scale, playing_card, strip_edition)
Card *copy_card(Card *other, Card *new_card = nullptr, double card_scale = 1, const Value &playing_card = Value(), bool strip_edition = false);
// create_playing_card(card_init {front, center}, area, skip_materialize, silent, colours)
Card *create_playing_card(Value front, Value center, CardArea *area, bool skip_materialize = false, bool silent = false,
                          TableRef colours = nullptr);
// juice_card_until(card, eval_func, first, delay)
void juice_card_until(Card *card, std::function<bool(Card *)> eval, bool first, double delay = 0.1);
// playing_card_joker_effects(cards)
void playing_card_joker_effects(const std::vector<Card *> &cards);

// tests and the dev server: create_card('Joker', G.jokers, nil, nil, nil, nil, key), add_to_deck and
// emplace, for each key of a comma separated list
void add_jokers(const std::string &keys);

}  // namespace vl
