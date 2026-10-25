// card.lua Card:use_consumeable / Card:can_use_consumeable (every Tarot, Planet and Spectral card),
// misc_functions.lua set_consumeable_usage, and G.FUNCS.end_consumeable / can_select_card.
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "core/lua_sort.h"
#include "engine/controller.h"
#include "engine/event.h"
#include "engine/particles.h"
#include "engine/random.h"
#include "engine/ui.h"
#include "game/blind.h"
#include "game/card.h"
#include "game/card_factory.h"
#include "game/functions.h"
#include "game/g.h"
#include "game/shop.h"
#include "game/sound.h"
#include "game/state_events.h"
#include "game/tag.h"
#include "game/ui_definitions.h"

namespace vl {

namespace {
inline Value fld(const Value &t, const char *k) { return t.get(Value(intern(k))); }
inline Value gv(const char *k) { return G.GAME->get(Value(intern(k))); }
inline Table *gt(const char *k) { return G.GAME->get(Value(intern(k))).table(); }
Value obj(Object *o) { return Value(o); }

// discover_card(card) called with a Card (set_consumeable_usage does): the flag lands on the card
// itself, so only the collection tally of the round and the save are touched
void discover_card_object(Card *card) {
    if (gv("seeded").truthy() || gv("challenge").truthy()) return;
    if (card->lua_discovered) return;
    Table *nc = gt("round_scores")->get(K("new_collection")).table();
    nc->set("amt", nc->get(K("amt")).num() + 1);
    card->lua_discovered = true;
    set_discover_tallies();
    G.E_MANAGER->add_event(ev_immediate([] {
        G.save_progress();
        return true;
    }));
}

void set_consumeable_usage(Card *card) {
    const Value ck = card->config->get(K("center_key")), center = card->config->get(K("center"));
    if (ck.truthy() && card->ability->get(K("consumeable")).truthy()) {
        Table *pu = fld(G.PROFILES->get(G.SETTINGS->get(K("profile"))), "consumeable_usage").table();
        Value u = pu->get(ck);
        if (u.truthy()) u.table()->set("count", fld(u, "count").num() + 1);
        else pu->set(ck, Value(vl::T({{"count", 1}, {"order", fld(center, "order")}})));
        Table *gu = gt("consumeable_usage");
        Value g = gu->get(ck);
        if (g.truthy()) g.table()->set("count", fld(g, "count").num() + 1);
        else gu->set(ck, Value(vl::T({{"count", 1}, {"order", fld(center, "order")}, {"set", card->ability->get(K("set"))}})));
        if (!gv("consumeable_usage_total").truthy())
            G.GAME->set("consumeable_usage_total", Value(vl::T({{"tarot", 0}, {"planet", 0}, {"spectral", 0}, {"tarot_planet", 0}, {"all", 0}})));
        Table *t = gt("consumeable_usage_total");
        auto inc = [&](const char *k) { t->set(k, t->get(Value(intern(k))).num() + 1); };
        const Value set = fld(center, "set");
        if (set.equals("Tarot")) {
            inc("tarot");
            inc("tarot_planet");
        } else if (set.equals("Planet")) {
            inc("planet");
            inc("tarot_planet");
        } else if (set.equals("Spectral")) {
            inc("spectral");
        }
        inc("all");
        if (!fld(center, "discovered").truthy()) discover_card_object(card);
        if (set.equals("Tarot") || set.equals("Planet")) {
            G.E_MANAGER->add_event(ev_immediate([ck] {
                G.E_MANAGER->add_event(ev_immediate([ck] {
                    G.GAME->set("last_tarot_planet", ck);
                    return true;
                }));
                return true;
            }));
        }
    }
    G.save_settings();
}

std::string rank_letter(double id) {
    const int i = (int)id;
    if (i < 10) return std::to_string(i);
    return i == 10 ? "T" : i == 11 ? "J" : i == 12 ? "Q" : i == 13 ? "K" : "A";
}
std::string suit_letter(const Value &suit) { return suit.to_string().substr(0, 1); }

Value pick(std::initializer_list<const char *> items, const char *seed_key) {
    TableRef t = new_table();
    for (const char *s : items) t->push(Value(s));
    double seed = pseudoseed(seed_key);
    return pseudorandom_element(Value(t), &seed);
}
TableRef cards_table(const std::vector<Card *> &v) {
    TableRef t = new_table();
    for (Card *c : v) t->push(obj(c));
    return t;
}
// the flip-in / flip-out of the cards a Tarot changes
void flip_all(const std::vector<CardRef> &cards, bool in) {
    const int n = (int)cards.size();
    for (int i = 1; i <= n; i++) {
        const double percent = in ? 1.15 - (i - 0.999) / (n - 0.998) * 0.3 : 0.85 + (i - 0.999) / (n - 0.998) * 0.3;
        CardRef c = cards[i - 1];
        G.E_MANAGER->add_event(ev_after(0.15, [c, percent, in] {
            c->flip();
            if (in) play_sound("card1", (float)percent);
            else play_sound("tarot2", (float)percent, 0.6f);
            float r = 0.3f;
            c->juice_up_card(0.3f, &r);
            return true;
        }));
    }
}
void tarot_juice(const CardRef &used, double d = 0.4) {
    G.E_MANAGER->add_event(ev_after(d, [used] {
        play_sound("tarot1");
        float r = 0.5f;
        used->juice_up_card(0.3f, &r);
        return true;
    }));
}
}  // namespace

void Card::use_consumeable(CardArea *, Card *copier) {
    stop_use();
    if (!copier) set_consumeable_usage(this);
    if (debuff) return;
    CardRef used_tarot(copier ? copier : this);
    CardRef self(this);
    const Value cons = ability->get(K("consumeable"));
    const std::string name = ability->get(K("name")).to_string();
    auto is = [&](std::initializer_list<const char *> ns) {
        for (const char *n : ns)
            if (name == n) return true;
        return false;
    };
    // (the original indexes G.hand.highlighted[i] when each event runs; the list does not change meanwhile)
    std::vector<CardRef> hl(G.hand->highlighted);

    if (fld(cons, "max_highlighted").truthy())
        update_hand_text(vl::T({{"immediate", true}, {"nopulse", true}, {"delay", 0}}), vl::T({{"mult", 0}, {"chips", 0}, {"level", ""}, {"handname", ""}}));

    if (fld(cons, "mod_conv").truthy() || fld(cons, "suit_conv").truthy()) {
        tarot_juice(used_tarot);
        flip_all(hl, true);
        delay(0.2);
        if (name == "Death") {
            CardRef rightmost = hl[0];
            for (auto &c : hl)
                if (c->T->x > rightmost->T->x) rightmost = c;
            for (auto &c : hl)
                G.E_MANAGER->add_event(ev_after(0.1, [c, rightmost] {
                    if (c != rightmost) copy_card(rightmost.get(), c.get());
                    return true;
                }));
        } else if (name == "Strength") {
            for (auto &c : hl)
                G.E_MANAGER->add_event(ev_after(0.1, [c] {
                    const double id = c->base->get(K("id")).num();
                    const double r = id == 14 ? 2 : std::min(id + 1, 14.0);
                    c->set_base(G.P_CARDS->get(Value(suit_letter(c->base->get(K("suit"))) + "_" + rank_letter(r))));
                    return true;
                }));
        } else if (fld(cons, "suit_conv").truthy()) {
            const std::string suit = fld(cons, "suit_conv").to_string();
            for (auto &c : hl)
                G.E_MANAGER->add_event(ev_after(0.1, [c, suit] {
                    c->change_suit(suit);
                    return true;
                }));
        } else {
            const Value center = G.P_CENTERS->get(fld(cons, "mod_conv"));
            for (auto &c : hl)
                G.E_MANAGER->add_event(ev_after(0.1, [c, center] {
                    c->set_ability(center);
                    return true;
                }));
        }
        flip_all(hl, false);
        G.E_MANAGER->add_event(ev_after(0.2, [] {
            G.hand->unhighlight_all();
            return true;
        }));
        delay(0.5);
    }
    if (name == "Black Hole") {
        update_hand_text(vl::T({{"sound", "button"}, {"volume", 0.7}, {"pitch", 0.8}, {"delay", 0.3}}),
                         vl::T({{"handname", localize("k_all_hands")}, {"chips", "..."}, {"mult", "..."}, {"level", ""}}));
        G.E_MANAGER->add_event(ev_after(0.2, [self] {
            play_sound("tarot1");
            float r = 0.5f;
            self->juice_up_card(0.8f, &r);
            G.TAROT_INTERRUPT_PULSE = true;
            return true;
        }));
        update_hand_text(vl::T({{"delay", 0}}), vl::T({{"mult", "+"}, {"StatusText", true}}));
        G.E_MANAGER->add_event(ev_after(0.9, [self] {
            play_sound("tarot1");
            float r = 0.5f;
            self->juice_up_card(0.8f, &r);
            return true;
        }));
        update_hand_text(vl::T({{"delay", 0}}), vl::T({{"chips", "+"}, {"StatusText", true}}));
        G.E_MANAGER->add_event(ev_after(0.9, [self] {
            play_sound("tarot1");
            float r = 0.5f;
            self->juice_up_card(0.8f, &r);
            G.TAROT_INTERRUPT_PULSE = false;
            return true;
        }));
        update_hand_text(vl::T({{"sound", "button"}, {"volume", 0.7}, {"pitch", 0.9}, {"delay", 0}}), vl::T({{"level", "+1"}}));
        delay(1.3);
        std::vector<std::string> hands;
        gt("hands")->each([&](const Value &k, const Value &) { hands.push_back(k.to_string()); });
        for (auto &h : hands) level_up_hand(this, h, true);
        update_hand_text(vl::T({{"sound", "button"}, {"volume", 0.7}, {"pitch", 1.1}, {"delay", 0}}),
                         vl::T({{"mult", 0}, {"chips", 0}, {"handname", ""}, {"level", ""}}));
    }
    if (is({"Talisman", "Deja Vu", "Trance", "Medium"})) {
        CardRef conv_card = hl.empty() ? CardRef() : hl[0];
        G.E_MANAGER->add_event(ev_immediate([used_tarot] {
            play_sound("tarot1");
            float r = 0.5f;
            used_tarot->juice_up_card(0.3f, &r);
            return true;
        }));
        const Value seal = ability->get(K("extra"));
        G.E_MANAGER->add_event(ev_after(0.1, [conv_card, seal] {
            conv_card->set_seal(seal, false, true);
            return true;
        }));
        delay(0.5);
        G.E_MANAGER->add_event(ev_after(0.2, [] {
            G.hand->unhighlight_all();
            return true;
        }));
    }
    if (name == "Aura") {
        G.E_MANAGER->add_event(ev_after(0.4, [used_tarot] {
            Value edition = poll_edition("aura", 1, true, true);
            G.hand->highlighted[0]->set_edition(edition, true);
            float r = 0.5f;
            used_tarot->juice_up_card(0.3f, &r);
            return true;
        }));
    }
    if (name == "Cryptid") {
        const int extra = (int)ability->get(K("extra")).num();
        G.E_MANAGER->add_event(ev_immediate([extra] {
            bool first_dissolve = false;
            std::vector<Card *> new_cards;
            for (int i = 1; i <= extra; i++) {
                G.playing_card++;
                Card *c = copy_card(G.hand->highlighted[0].get(), nullptr, 1, Value((double)G.playing_card));
                c->add_to_deck();
                G.deck->config->set("card_limit", G.deck->config->get(K("card_limit")).num() + 1);
                G.playing_cards.push_back(CardRef(c));
                G.hand->emplace(c);
                c->start_materialize(nullptr, first_dissolve);
                first_dissolve = true;
                new_cards.push_back(c);
            }
            playing_card_joker_effects(new_cards);
            return true;
        }));
    }
    if (name == "Sigil" || name == "Ouija") {
        tarot_juice(used_tarot);
        std::vector<CardRef> hand(G.hand->cards);
        flip_all(hand, true);
        delay(0.2);
        if (name == "Sigil") {
            const std::string suit = pick({"S", "H", "D", "C"}, "sigil").to_string();
            for (auto &c : hand)
                G.E_MANAGER->add_event(ev_immediate([c, suit] {
                    c->set_base(G.P_CARDS->get(Value(suit + "_" + rank_letter(c->base->get(K("id")).num()))));
                    return true;
                }));
        }
        if (name == "Ouija") {
            const std::string rank = pick({"2", "3", "4", "5", "6", "7", "8", "9", "T", "J", "Q", "K", "A"}, "ouija").to_string();
            for (auto &c : hand)
                G.E_MANAGER->add_event(ev_immediate([c, rank] {
                    c->set_base(G.P_CARDS->get(Value(suit_letter(c->base->get(K("suit"))) + "_" + rank)));
                    return true;
                }));
            G.hand->change_size(-1);
        }
        flip_all(hand, false);
        delay(0.5);
    }
    if (fld(cons, "hand_type").truthy()) {
        const std::string ht = fld(cons, "hand_type").to_string();
        Value h = gt("hands")->get(Value(ht));
        update_hand_text(vl::T({{"sound", "button"}, {"volume", 0.7}, {"pitch", 0.8}, {"delay", 0.3}}),
                         vl::T({{"handname", localize(ht, "poker_hands")}, {"chips", fld(h, "chips")}, {"mult", fld(h, "mult")}, {"level", fld(h, "level")}}));
        level_up_hand(used_tarot.get(), ht);
        update_hand_text(vl::T({{"sound", "button"}, {"volume", 0.7}, {"pitch", 1.1}, {"delay", 0}}),
                         vl::T({{"mult", 0}, {"chips", 0}, {"handname", ""}, {"level", ""}}));
    }
    if (fld(cons, "remove_card").truthy()) {
        std::vector<Card *> destroyed;
        auto destroy_event = [](std::vector<CardRef> cards, double d, bool last_silent) {
            G.E_MANAGER->add_event(ev_after(d, [cards, last_silent] {
                for (int i = (int)cards.size(); i >= 1; i--) {
                    Card *c = cards[i - 1].get();
                    if (c->ability->get(K("name")).equals("Glass Card")) c->shatter();
                    else c->start_dissolve(nullptr, last_silent ? i == (int)cards.size() : i != (int)cards.size());
                }
                return true;
            }));
        };
        if (name == "The Hanged Man") {
            for (int i = (int)hl.size(); i >= 1; i--) destroyed.push_back(hl[i - 1].get());
            tarot_juice(used_tarot);
            // (iterated from the last highlighted card; the last one dissolves silently)
            std::vector<CardRef> fwd(hl);
            G.E_MANAGER->add_event(ev_after(0.2, [fwd] {
                for (int i = (int)fwd.size(); i >= 1; i--) {
                    Card *c = fwd[i - 1].get();
                    if (c->ability->get(K("name")).equals("Glass Card")) c->shatter();
                    else c->start_dissolve(nullptr, i == (int)fwd.size());
                }
                return true;
            }));
        } else if (is({"Familiar", "Grim", "Incantation"})) {
            TableRef hand = new_table();
            for (auto &c : G.hand->cards) hand->push(obj(c.get()));
            double seed = pseudoseed("random_destroy");
            destroyed.push_back(pseudorandom_element(Value(hand), &seed).as<Card>());
            tarot_juice(used_tarot);
            std::vector<CardRef> d;
            for (Card *c : destroyed) d.push_back(CardRef(c));
            destroy_event(d, 0.1, false);
            const int extra = (int)ability->get(K("extra")).num();
            const std::string nm = name;
            G.E_MANAGER->add_event(ev_after(0.7, [extra, nm] {
                std::vector<Card *> cards;
                for (int i = 1; i <= extra; i++) {
                    cards.push_back(nullptr);
                    std::string suit, rank;
                    if (nm == "Familiar") {
                        rank = pick({"J", "Q", "K"}, "familiar_create").to_string();
                        suit = pick({"S", "H", "D", "C"}, "familiar_create").to_string();
                    } else if (nm == "Grim") {
                        rank = "A";
                        suit = pick({"S", "H", "D", "C"}, "grim_create").to_string();
                    } else {
                        rank = pick({"2", "3", "4", "5", "6", "7", "8", "9", "T"}, "incantation_create").to_string();
                        suit = pick({"S", "H", "D", "C"}, "incantation_create").to_string();
                    }
                    TableRef cen_pool = new_table();
                    G.P_CENTER_POOLS->get(K("Enhanced")).table()->each([&](const Value &, const Value &v) {
                        if (!fld(v, "key").equals("m_stone")) cen_pool->push(v);
                    });
                    double s2 = pseudoseed("spe_card");
                    Value center = pseudorandom_element(Value(cen_pool), &s2);
                    create_playing_card(G.P_CARDS->get(Value(suit + "_" + rank)), center, G.hand.get(), false, i != 1,
                                        A({fld(G.colour("SECONDARY_SET"), "Spectral")}));
                }
                playing_card_joker_effects(cards);
                return true;
            }));
        } else if (name == "Immolate") {
            std::vector<CardRef> temp_hand(G.hand->cards);
            // table.sort(temp_hand, function(a, b) return not a.playing_card or not b.playing_card or a.playing_card < b.playing_card end)
            lua_sort(temp_hand, [](const CardRef &a, const CardRef &b) {
                return !a->playing_card.truthy() || !b->playing_card.truthy() || a->playing_card.num() < b->playing_card.num();
            });
            pseudoshuffle(temp_hand, pseudoseed("immolate"));
            const int n = (int)fld(ability->get(K("extra")), "destroy").num();
            for (int i = 1; i <= n && i <= (int)temp_hand.size(); i++) destroyed.push_back(temp_hand[i - 1].get());
            tarot_juice(used_tarot);
            std::vector<CardRef> d;
            for (Card *c : destroyed) d.push_back(CardRef(c));
            destroy_event(d, 0.1, true);
            delay(0.5);
            ease_dollars(fld(ability->get(K("extra")), "dollars").num());
        }
        delay(0.3);
        for (auto &j : std::vector<CardRef>(G.jokers->cards))
            j->calculate_joker(vl::T({{"remove_playing_cards", true}, {"removed", Value(cards_table(destroyed))}}));
    }
    if (name == "The Fool") {
        G.E_MANAGER->add_event(ev_after(0.4, [used_tarot] {
            if (G.consumeables->config->get(K("card_limit")).num() > G.consumeables->cards.size()) {
                play_sound("timpani");
                Card *card = create_card("Tarot_Planet", G.consumeables.get(), false, Value(), false, false, gv("last_tarot_planet").to_string(), "fool");
                card->add_to_deck();
                G.consumeables->emplace(card);
                float r = 0.5f;
                used_tarot->juice_up_card(0.3f, &r);
            }
            return true;
        }));
        delay(0.6);
    }
    if (name == "The Hermit" || name == "Temperance") {
        const bool hermit = name == "The Hermit";
        G.E_MANAGER->add_event(ev_after(0.4, [used_tarot, self, hermit] {
            play_sound("timpani");
            float r = 0.5f;
            used_tarot->juice_up_card(0.3f, &r);
            if (hermit) ease_dollars(std::max(0.0, std::min(gv("dollars").num(), self->ability->get(K("extra")).num())), true);
            else ease_dollars(self->ability->get(K("money")).num(), true);
            return true;
        }));
        delay(0.6);
    }
    if (name == "The Emperor" || name == "The High Priestess") {
        const bool emperor = name == "The Emperor";
        const double count = fld(cons, emperor ? "tarots" : "planets").truthy() ? fld(cons, emperor ? "tarots" : "planets").num() : fld(cons, "planets").num();
        const int n = (int)std::min(count, G.consumeables->config->get(K("card_limit")).num() - G.consumeables->cards.size());
        for (int i = 1; i <= n; i++)
            G.E_MANAGER->add_event(ev_after(0.4, [used_tarot, emperor] {
                if (G.consumeables->config->get(K("card_limit")).num() > G.consumeables->cards.size()) {
                    play_sound("timpani");
                    Card *card = create_card(emperor ? "Tarot" : "Planet", G.consumeables.get(), false, Value(), false, false, "", emperor ? "emp" : "pri");
                    card->add_to_deck();
                    G.consumeables->emplace(card);
                    float r = 0.5f;
                    used_tarot->juice_up_card(0.3f, &r);
                }
                return true;
            }));
        delay(0.6);
    }
    if (name == "Judgement" || name == "The Soul") {
        const bool soul = name == "The Soul";
        G.E_MANAGER->add_event(ev_after(0.4, [used_tarot, soul] {
            play_sound("timpani");
            Card *card = create_card("Joker", G.jokers.get(), soul, Value(), false, false, "", soul ? "sou" : "jud");
            card->add_to_deck();
            G.jokers->emplace(card);
            if (soul) check_for_unlock(vl::T({{"type", "spawn_legendary"}}));
            float r = 0.5f;
            used_tarot->juice_up_card(0.3f, &r);
            return true;
        }));
        delay(0.6);
    }
    if (name == "Ankh") {
        std::vector<CardRef> deletable;
        for (auto &v : G.jokers->cards)
            if (!v->ability->get(K("eternal")).truthy()) deletable.push_back(v);
        TableRef js = new_table();
        for (auto &v : G.jokers->cards) js->push(obj(v.get()));
        double seed = pseudoseed("ankh_choice");
        CardRef chosen(pseudorandom_element(Value(js), &seed).as<Card>());
        EventConfig e1 = ev_immediate([deletable, chosen] {
            bool first_dissolve = false;
            for (auto &v : deletable)
                if (v != chosen) {
                    v->start_dissolve(nullptr, first_dissolve);
                    first_dissolve = true;
                }
            return true;
        });
        e1.trigger = Trigger::Before;
        e1.delay = 0.75;
        G.E_MANAGER->add_event(e1);
        EventConfig e2 = ev_immediate([chosen] {
            Card *card = copy_card(chosen.get(), nullptr, 1, Value(), chosen->edition && chosen->edition->get(K("negative")).truthy());
            card->start_materialize();
            card->add_to_deck();
            if (card->edition && card->edition->get(K("negative")).truthy()) card->set_edition(Value(), true);
            G.jokers->emplace(card);
            return true;
        });
        e2.trigger = Trigger::Before;
        e2.delay = 0.4;
        G.E_MANAGER->add_event(e2);
    }
    if (name == "Wraith") {
        G.E_MANAGER->add_event(ev_after(0.4, [used_tarot] {
            play_sound("timpani");
            Card *card = create_card("Joker", G.jokers.get(), false, Value(0.99), false, false, "", "wra");
            card->add_to_deck();
            G.jokers->emplace(card);
            float r = 0.5f;
            used_tarot->juice_up_card(0.3f, &r);
            if (gv("dollars").num() != 0) ease_dollars(-gv("dollars").num(), true);
            return true;
        }));
        delay(0.6);
    }
    if (is({"The Wheel of Fortune", "Ectoplasm", "Hex"})) {
        const std::vector<CardRef> &pool = name == "The Wheel of Fortune" ? eligible_strength_jokers : eligible_editionless_jokers;
        TableRef temp_pool = new_table();
        for (auto &c : pool) temp_pool->push(obj(c.get()));
        const std::string nm = name;
        if (name == "Ectoplasm" || name == "Hex" || pseudorandom("wheel_of_fortune") < fld(gv("probabilities"), "normal").num() / ability->get(K("extra")).num()) {
            G.E_MANAGER->add_event(ev_after(0.4, [temp_pool, nm, used_tarot] {
                double seed = pseudoseed(nm == "The Wheel of Fortune" ? "wheel_of_fortune" : nm == "Ectoplasm" ? "ectoplasm" : "hex");
                Card *eligible = pseudorandom_element(Value(temp_pool), &seed).as<Card>();
                Value edition = nm == "Ectoplasm" ? Value(vl::T({{"negative", true}})) : nm == "Hex" ? Value(vl::T({{"polychrome", true}}))
                                                                                                      : poll_edition("wheel_of_fortune", 1, true, true);
                eligible->set_edition(edition, true);
                check_for_unlock(vl::T({{"type", "have_edition"}}));
                if (nm == "Hex") {
                    bool first_dissolve = false;
                    for (auto &v : std::vector<CardRef>(G.jokers->cards))
                        if (v.get() != eligible && !v->ability->get(K("eternal")).truthy()) {
                            v->start_dissolve(nullptr, first_dissolve);
                            first_dissolve = true;
                        }
                }
                if (nm == "Ectoplasm") {
                    if (!gv("ecto_minus").truthy()) G.GAME->set("ecto_minus", 1);
                    G.hand->change_size(-(int)gv("ecto_minus").num());
                    G.GAME->set("ecto_minus", gv("ecto_minus").num() + 1);
                }
                float r = 0.5f;
                used_tarot->juice_up_card(0.3f, &r);
                return true;
            }));
        } else {
            G.E_MANAGER->add_event(ev_after(0.4, [used_tarot] {
                const bool pack = G.STATE == TAROT_PACK || G.STATE == SPECTRAL_PACK;
                attention_text(vl::T({{"text", localize("k_nope_ex")}, {"scale", 1.3}, {"hold", 1.4}, {"major", obj(used_tarot.get())},
                                      {"backdrop_colour", fld(G.colour("SECONDARY_SET"), "Tarot")}, {"align", pack ? "tm" : "cm"},
                                      {"offset", Value(vl::T({{"x", 0}, {"y", pack ? -0.2 : 0}}))}, {"silent", true}}));
                EventConfig e = ev_after(0.06 * G.SETTINGS->get(K("GAMESPEED")).num(), [] {
                    play_sound("tarot2", 0.76f, 0.4f);
                    return true;
                });
                e.blockable = false;
                e.blocking = false;
                G.E_MANAGER->add_event(e);
                play_sound("tarot2", 1, 0.4f);
                float r = 0.5f;
                used_tarot->juice_up_card(0.3f, &r);
                return true;
            }));
        }
        delay(0.6);
    }
}

bool Card::can_use_consumeable(bool any_state, bool skip_check) {
    const Value cons = ability->get(K("consumeable"));
    if (!cons.truthy()) return false;
    Value su = G.GAME->get(K("STOP_USE"));
    if (!skip_check && ((G.play && !G.play->cards.empty()) || G.CONTROLLER->locked || (su.truthy() && su.num() > 0))) return false;
    if ((G.STATE != HAND_PLAYED && G.STATE != DRAW_TO_HAND && G.STATE != PLAY_TAROT) || any_state) {
        const std::string name = ability->get(K("name")).to_string();
        const double c_lim = G.consumeables->config->get(K("card_limit")).num(), j_lim = G.jokers->config->get(K("card_limit")).num();
        if (name == "The Hermit" || fld(cons, "hand_type").truthy() || name == "Temperance" || name == "Black Hole") return true;
        if (name == "The Wheel of Fortune" && !eligible_strength_jokers.empty()) return true;
        if (name == "Ankh")
            for (auto &v : G.jokers->cards)
                if (v->ability->get(K("set")).equals("Joker") && j_lim > 1) return true;
        if (name == "Aura" && G.hand && G.hand->highlighted.size() == 1 && !G.hand->highlighted[0]->edition) return true;
        if ((name == "Ectoplasm" || name == "Hex") && !eligible_editionless_jokers.empty()) return true;
        if ((name == "The Emperor" || name == "The High Priestess") && (G.consumeables->cards.size() < c_lim || area == G.consumeables.get())) return true;
        if (name == "The Fool" && (G.consumeables->cards.size() < c_lim || area == G.consumeables.get()) && gv("last_tarot_planet").truthy() &&
            !gv("last_tarot_planet").equals("c_fool"))
            return true;
        if (name == "Judgement" || name == "The Soul" || name == "Wraith") return G.jokers->cards.size() < j_lim || area == G.jokers.get();
        if (G.STATE == SELECTING_HAND || G.STATE == TAROT_PACK || G.STATE == SPECTRAL_PACK || G.STATE == PLANET_PACK) {
            if (fld(cons, "max_highlighted").truthy()) {
                const double minh = fld(cons, "min_highlighted").truthy() ? fld(cons, "min_highlighted").num() : 1;
                if (fld(cons, "mod_num").num() >= G.hand->highlighted.size() && G.hand->highlighted.size() >= minh) return true;
            }
            if ((name == "Familiar" || name == "Grim" || name == "Incantation" || name == "Immolate" || name == "Sigil" || name == "Ouija") &&
                G.hand->cards.size() > 1)
                return true;
        }
    }
    return false;
}

bool can_select_card(Card *card) {
    return !card->ability->get(K("set")).equals("Joker") || (card->edition && card->edition->get(K("negative")).truthy()) ||
           G.jokers->cards.size() < G.jokers->config->get(K("card_limit")).num();
}

void end_consumeable(double delayfac) {
    stop_use();
    if (G.booster_pack) {
        for (Ref<Moveable> *p : {&G.booster_pack_sparkles, &G.booster_pack_stars, &G.booster_pack_meteors})
            if (auto *pp = dynamic_cast<Particles *>(p->get())) pp->fade(1 * delayfac);
        if (auto *bp = dynamic_cast<Moveable *>(G.booster_pack.get())) bp->alignment.offset.y = G.ROOM->T->y + 9;
        EventConfig e1 = ev_after(0.2 * delayfac, [] {
            if (G.booster_pack) G.booster_pack->remove();
            G.booster_pack = nullptr;
            G.pack_cards = nullptr;
            return true;
        });
        e1.blocking = false;
        e1.blockable = false;
        G.E_MANAGER->add_event(e1);
        EventConfig e2 = ev_after(1 * delayfac, [] {
            for (Ref<Moveable> *p : {&G.booster_pack_sparkles, &G.booster_pack_stars, &G.booster_pack_meteors})
                if (*p) {
                    (*p)->remove();
                    *p = nullptr;
                }
            return true;
        });
        e2.blocking = false;
        e2.blockable = false;
        G.E_MANAGER->add_event(e2);
    }
    delay(0.2 * delayfac);
    G.E_MANAGER->add_event(ev_after(0.2 * delayfac, [delayfac] {
        draw_from_hand_to_deck();
        G.E_MANAGER->add_event(ev_after(0.2 * delayfac, [] {
            auto unpark = [](Node *n) {
                if (auto *m = dynamic_cast<Moveable *>(n); m && m->alignment.has_py) {
                    m->alignment.offset.y = m->alignment.py;
                    m->alignment.has_py = false;
                }
            };
            if (G.shop) unpark(G.shop.get());
            if (G.blind_select) unpark(G.blind_select.get());
            if (G.round_eval) unpark(G.round_eval.get());
            G.CONTROLLER->interrupt.focus = true;
            G.E_MANAGER->add_event(ev_immediate([] {
                if (auto *s = dynamic_cast<UIBox *>(G.shop.get())) G.CONTROLLER->snap_to(s->get_UIE_by_ID("next_round_button"));
                return true;
            }));
            G.STATE = (int)gv("PACK_INTERRUPT").num();
            ease_background_colour_blind(G.STATE);
            G.GAME->set("PACK_INTERRUPT", Value());
            return true;
        }));
        Table *tags = gt("tags");
        for (int i = 1; tags && i <= tags->len(); i++)
            if (Tag *t = tags->get(i).as<Tag>())
                if (t->apply_to_run(vl::T({{"type", "new_blind_choice"}})).truthy()) break;
        G.E_MANAGER->add_event(ev_after(0.2 * delayfac, [] {
            save_run();
            return true;
        }));
        return true;
    }));
}

void register_consumable_callbacks() {
    auto &F = G.FUNCS;
    F["can_use_consumeable"] = [](UIElement *e) {
        Card *c = e->config->get(K("ref_table")).as<Card>();
        if (c && c->can_use_consumeable()) {
            e->config->set("colour", G.colour("RED"));
            e->config->set("button", "use_card");
        } else {
            e->config->set("colour", fld(G.colour("UI"), "BACKGROUND_INACTIVE"));
            e->config->set("button", Value());
        }
    };
    F["end_consumeable"] = [](UIElement *) { end_consumeable(); };
}

}  // namespace vl
