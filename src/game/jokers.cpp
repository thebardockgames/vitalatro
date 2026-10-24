// card.lua: Card:calculate_joker (every joker's effect, by context), add_to_deck /
// remove_from_deck (the jokers' passive effects), the stickers; blind.lua: Blind:disable.
// The structure follows the original branch by branch, so the RNG calls happen in the same order.
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "core/object.h"
#include "engine/event.h"
#include "engine/random.h"
#include "engine/ui.h"
#include "game/blind.h"
#include "game/card.h"
#include "game/card_factory.h"
#include "game/functions.h"
#include "game/g.h"
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
Value colour(const char *n) { return G.colour(n); }
Value secondary(const char *set) { return fld(colour("SECONDARY_SET"), set); }
Value lvar(const char *key, const Value &v) { return localize(vl::T({{"type", "variable"}, {"key", key}, {"vars", Value(A({v}))}})); }
Value money_text(const Value &v) { return Value(localize("$").to_string() + v.to_string()); }
Value hex(const char *h) {
    auto byte = [&](int i) { return std::stoi(std::string(h + i, 2), nullptr, 16) / 255.0; };
    return Value(A({byte(0), byte(2), byte(4), 1}));
}
Blind *game_blind() { return gv("blind").as<Blind>(); }
std::vector<Card *> cards_of(const Value &t) {
    std::vector<Card *> r;
    if (Table *tt = t.table())
        for (int i = 1; i <= tt->len(); i++)
            if (Card *c = tt->get(i).as<Card>()) r.push_back(c);
    return r;
}
bool non_empty(const Value &t) {
    bool any = false;
    if (Table *tt = t.table()) tt->each([&](const Value &, const Value &) { any = true; });
    return any;
}
double consumeable_buffer() { return gv("consumeable_buffer").num(); }
bool consumeable_room() {
    return G.consumeables->cards.size() + consumeable_buffer() < G.consumeables->config->get(K("card_limit")).num();
}
void add_dollar_buffer(double d) {
    Value db = gv("dollar_buffer");
    G.GAME->set("dollar_buffer", (db.truthy() ? db.num() : 0) + d);
    G.E_MANAGER->add_event(ev_immediate([] {
        G.GAME->set("dollar_buffer", 0);
        return true;
    }));
}
// a consumable made by an effect: the slot is reserved now, the card made by the event
EventConfig make_consumable_event(const char *type, const char *append, bool before) {
    std::string t(type), a(append);
    EventConfig e = ev_immediate([t, a] {
        Card *card = create_card(t, G.consumeables.get(), false, Value(), false, false, "", a);
        card->add_to_deck();
        G.consumeables->emplace(card);
        G.GAME->set("consumeable_buffer", 0);
        return true;
    });
    if (before) {
        e.trigger = Trigger::Before;
        e.delay = 0;
    }
    return e;
}
void reserve_consumable() { G.GAME->set("consumeable_buffer", consumeable_buffer() + 1); }
// the food jokers' exit: tilt, pinch and remove
void eaten(Card *self) {
    CardRef s(self);
    G.E_MANAGER->add_event(ev_immediate([s] {
        play_sound("tarot1");
        s->T->r = -0.2f;
        float r = 0.4f;
        s->juice_up_card(0.3f, &r);
        s->states.drag.is = true;
        if (Sprite *c = s->sprite("center")) c->pinch->x = true;
        EventConfig e = ev_after(0.3, [s] {
            G.jokers->remove_card(s.get());
            s->remove();
            return true;
        });
        e.blockable = false;
        G.E_MANAGER->add_event(e);
        return true;
    }));
}
}  // namespace

// ------------------------------------------------------------------ stickers
void Card::set_eternal(bool eternal) {
    ability->set("eternal", Value());
    if (fld(config->get(K("center")), "eternal_compat").truthy() && !ability->get(K("perishable")).truthy())
        ability->set("eternal", eternal ? Value(true) : Value());
}

void Card::set_perishable(bool) {
    ability->set("perishable", Value());
    if (fld(config->get(K("center")), "perishable_compat").truthy() && !ability->get(K("eternal")).truthy()) {
        ability->set("perishable", true);
        ability->set("perish_tally", gv("perishable_rounds"));
    }
}

void Card::set_rental(bool rental) {
    ability->set("rental", rental ? Value(true) : Value());
    set_cost();
}

// ------------------------------------------------------------------ add_to_deck / remove_from_deck
void Card::add_to_deck(bool from_debuff) {
    if (!fld(config->get(K("center")), "discovered").truthy()) discover_card(config->get(K("center")));
    if (added_to_deck) return;
    added_to_deck = true;
    const Value set = ability->get(K("set"));
    const Value nm = ability->get(K("name"));
    if (set.equals("Enhanced") || set.equals("Default")) {
        if (nm.equals("Gold Card") && seal.equals("Gold") && playing_card.truthy()) check_for_unlock(vl::T({{"type", "double_gold"}}));
        return;
    }
    Value ecenter = G.P_CENTERS->get(Value(edition ? "e_" + edition->get(K("type")).to_string() : std::string("e_base")));
    if (!fld(ecenter, "discovered").truthy()) discover_card(ecenter);
    if (ability->get(K("h_size")).num() != 0) G.hand->change_size((int)ability->get(K("h_size")).num());
    if (ability->get(K("d_size")).num() > 0) {
        Table *rr = gt("round_resets");
        rr->set("discards", rr->get(K("discards")).num() + ability->get(K("d_size")).num());
        ease_discard(ability->get(K("d_size")).num());
    }
    if (nm.equals("Credit Card")) G.GAME->set("bankrupt_at", gv("bankrupt_at").num() - ability->get(K("extra")).num());
    Blind *b = G.GAME ? game_blind() : nullptr;
    if (nm.equals("Chicot") && b && b->boss && !b->disabled) {
        b->disable();
        play_sound("timpani");
        card_eval_status_text(this, "extra", 0, -1, nullptr, vl::T({{"message", localize("ph_boss_disabled")}}));
    }
    if (nm.equals("Chaos the Clown")) {
        Table *cr = gt("current_round");
        cr->set("free_rerolls", cr->get(K("free_rerolls")).num() + 1);
        calculate_reroll_cost(true);
    }
    if (nm.equals("Turtle Bean")) G.hand->change_size((int)fld(ability->get(K("extra")), "h_size").num());
    if (nm.equals("Oops! All 6s")) {
        Table *p = gt("probabilities");
        std::vector<std::pair<Value, Value>> kv;
        p->each([&](const Value &k, const Value &v) { kv.emplace_back(k, v); });
        for (auto &e : kv) p->set(e.first, e.second.num() * 2);
    }
    if (nm.equals("To the Moon")) G.GAME->set("interest_amount", gv("interest_amount").num() + ability->get(K("extra")).num());
    if (nm.equals("Astronomer"))
        G.E_MANAGER->add_event(ev_immediate([] {
            G.I.CARD.each([](Moveable *v) {
                if (Card *c = dynamic_cast<Card *>(v)) c->set_cost();
            });
            return true;
        }));
    if (nm.equals("Troubadour")) {
        G.hand->change_size((int)fld(ability->get(K("extra")), "h_size").num());
        Table *rr = gt("round_resets");
        rr->set("hands", rr->get(K("hands")).num() + fld(ability->get(K("extra")), "h_plays").num());
    }
    if (nm.equals("Stuntman")) G.hand->change_size(-(int)fld(ability->get(K("extra")), "h_size").num());
    if (edition && edition->get(K("negative")).truthy()) {
        if (from_debuff) {
            ability->set("queue_negative_removal", Value());
        } else {
            CardArea *a = ability->get(K("consumeable")).truthy() ? G.consumeables.get() : G.jokers.get();
            a->config->set("card_limit", a->config->get(K("card_limit")).num() + 1);
        }
    }
    if (b)
        G.E_MANAGER->add_event(ev_immediate([] {
            if (Blind *bl = game_blind()) bl->set_blind(Value(), true);
            return true;
        }));
}

void Card::remove_from_deck(bool from_debuff) {
    if (!added_to_deck) return;
    added_to_deck = false;
    const Value nm = ability->get(K("name"));
    if (ability->get(K("h_size")).num() != 0) G.hand->change_size(-(int)ability->get(K("h_size")).num());
    if (ability->get(K("d_size")).num() > 0) {
        Table *rr = gt("round_resets");
        rr->set("discards", rr->get(K("discards")).num() - ability->get(K("d_size")).num());
        ease_discard(-ability->get(K("d_size")).num());
    }
    if (nm.equals("Credit Card")) G.GAME->set("bankrupt_at", gv("bankrupt_at").num() + ability->get(K("extra")).num());
    if (nm.equals("Chaos the Clown")) {
        Table *cr = gt("current_round");
        cr->set("free_rerolls", cr->get(K("free_rerolls")).num() - 1);
        calculate_reroll_cost(true);
    }
    if (nm.equals("Turtle Bean")) G.hand->change_size(-(int)fld(ability->get(K("extra")), "h_size").num());
    if (nm.equals("Oops! All 6s")) {
        Table *p = gt("probabilities");
        std::vector<std::pair<Value, Value>> kv;
        p->each([&](const Value &k, const Value &v) { kv.emplace_back(k, v); });
        for (auto &e : kv) p->set(e.first, e.second.num() / 2);
    }
    if (nm.equals("To the Moon")) G.GAME->set("interest_amount", gv("interest_amount").num() - ability->get(K("extra")).num());
    if (nm.equals("Astronomer"))
        G.E_MANAGER->add_event(ev_immediate([] {
            G.I.CARD.each([](Moveable *v) {
                if (Card *c = dynamic_cast<Card *>(v)) c->set_cost();
            });
            return true;
        }));
    if (nm.equals("Troubadour")) {
        G.hand->change_size(-(int)fld(ability->get(K("extra")), "h_size").num());
        Table *rr = gt("round_resets");
        rr->set("hands", rr->get(K("hands")).num() - fld(ability->get(K("extra")), "h_plays").num());
    }
    if (nm.equals("Stuntman")) G.hand->change_size((int)fld(ability->get(K("extra")), "h_size").num());
    if (edition && edition->get(K("negative")).truthy() && G.jokers) {
        if (from_debuff) {
            ability->set("queue_negative_removal", true);
        } else {
            CardArea *a = ability->get(K("consumeable")).truthy() ? G.consumeables.get() : G.jokers.get();
            a->config->set("card_limit", a->config->get(K("card_limit")).num() - 1);
        }
    }
    if (game_blind())
        G.E_MANAGER->add_event(ev_immediate([] {
            if (Blind *bl = game_blind()) bl->set_blind(Value(), true);
            return true;
        }));
}

// ------------------------------------------------------------------ Blind:disable
void Blind::disable() {
    disabled = true;
    for (auto &v : G.jokers->cards)
        if (v->facing == "back") v->flip();
    if (name == "The Water") ease_discard(discards_sub.num());
    if (name == "The Wheel" || name == "The House" || name == "The Mark" || name == "The Fish") {
        for (auto &c : G.hand->cards)
            if (c->facing == "back") c->flip();
        for (auto &c : G.playing_cards) c->ability->set("wheel_flipped", Value());
    }
    if (name == "The Needle") ease_hands_played(hands_sub.num());
    if (name == "The Wall") {
        chips = chips / 2;
        vars->set("chip_text", Value(number_format(Value(chips))));
    }
    if (name == "Cerulean Bell")
        for (auto &c : G.playing_cards) c->ability->set("forced_selection", Value());
    if (name == "The Manacle") {
        G.hand->change_size(1);
        draw_from_deck_to_hand(1);
    }
    if (name == "Violet Vessel") {
        chips = chips / 3;
        vars->set("chip_text", Value(number_format(Value(chips))));
    }
    Ref<Blind> self(this);
    G.E_MANAGER->add_event(ev_immediate([self] {
        if (self->boss && gv("chips").num() - self->chips >= 0) {
            G.STATE = NEW_ROUND;
            G.STATE_COMPLETE = false;
        }
        return true;
    }));
    for (auto &c : std::vector<CardRef>(G.playing_cards)) debuff_card(c.get());
    for (auto &c : std::vector<CardRef>(G.jokers->cards)) debuff_card(c.get());
    set_text();
    wiggle();
}

// ------------------------------------------------------------------ Card:calculate_joker
Value Card::calculate_joker(const TableRef &context) {
    if (debuff) return Value();
    Card *self = this;
    TableRef &ctx = const_cast<TableRef &>(context);
    auto C = [&](const char *k) { return ctx->get(Value(intern(k))); };
    auto has = [&](const char *k) { return C(k).truthy(); };
    auto ab = [&](const char *k) { return ability->get(Value(intern(k))); };
    auto abn = [&](const char *k) { return ability->get(Value(intern(k))).num(); };
    auto setab = [&](const char *k, const Value &v) { ability->set(Value(intern(k)), v); };
    auto ex = [&](const char *k) { return fld(ability->get(K("extra")), k); };
    auto exn = [&](const char *k) { return fld(ability->get(K("extra")), k).num(); };
    auto setex = [&](const char *k, const Value &v) { ability->get(K("extra")).table()->set(Value(intern(k)), v); };
    const std::string name = ability->get(K("name")).is_str() ? std::string(ability->get(K("name")).str()) : std::string();
    auto bpc = [&]() -> Card * { return C("blueprint_card").as<Card>() ? C("blueprint_card").as<Card>() : self; };
    auto status = [&](Card *c, std::initializer_list<std::pair<const char *, Value>> kv) {
        card_eval_status_text(c, "extra", 0, -1, nullptr, vl::T(kv));
    };
    auto ret = [&](std::initializer_list<std::pair<const char *, Value>> kv) { return Value(vl::T(kv)); };
    Card *other = C("other_card").as<Card>();
    auto hand_has = [&](const char *h) { return non_empty(fld(C("poker_hands"), h)); };
    Value cr = gv("current_round");
    const double prob = fld(gv("probabilities"), "normal").num();

    if (ab("set").equals("Planet") && !debuff) {
        if (has("joker_main")) {
            if (fld(gv("used_vouchers"), "v_observatory").truthy() && fld(ab("consumeable"), "hand_type") == C("scoring_name")) {
                Value x = fld(fld(G.P_CENTERS->get(K("v_observatory")), "config"), "extra");
                return ret({{"message", lvar("a_xmult", x)}, {"Xmult_mod", x}});
            }
        }
    }
    if (!(ab("set").equals("Joker") && !debuff)) return Value();

    if (name == "Blueprint" || name == "Brainstorm") {
        Card *other_joker = nullptr;
        if (name == "Blueprint") {
            for (size_t i = 0; i < G.jokers->cards.size(); i++)
                if (G.jokers->cards[i].get() == self) other_joker = i + 1 < G.jokers->cards.size() ? G.jokers->cards[i + 1].get() : nullptr;
        } else {
            other_joker = G.jokers->cards.empty() ? nullptr : G.jokers->cards[0].get();
        }
        if (other_joker && other_joker != self) {
            ctx->set("blueprint", has("blueprint") ? C("blueprint").num() + 1 : 1.0);
            if (!has("blueprint_card")) ctx->set("blueprint_card", obj(self));
            if (C("blueprint").num() > G.jokers->cards.size() + 1) return Value();
            Value r = other_joker->calculate_joker(ctx);
            if (Table *t = r.table()) {
                t->set("card", has("blueprint_card") ? C("blueprint_card") : obj(self));
                t->set("colour", colour(name == "Blueprint" ? "BLUE" : "RED"));
                return r;
            }
        }
    }

    if (has("open_booster")) {
        if (name == "Hallucination" && consumeable_room()) {
            if (pseudorandom("halu" + fld(gv("round_resets"), "ante").to_string()) < prob / abn("extra")) {
                reserve_consumable();
                G.E_MANAGER->add_event(make_consumable_event("Tarot", "hal", true));
                status(self, {{"message", localize("k_plus_tarot")}, {"colour", colour("PURPLE")}});
            }
        }
    } else if (has("buying_card")) {
    } else if (has("selling_self")) {
        if (name == "Luchador") {
            Blind *b = game_blind();
            if (b && !b->disabled && b->get_type() && std::string_view(b->get_type()) == "Boss") {
                status(bpc(), {{"message", localize("ph_boss_disabled")}});
                b->disable();
            }
        }
        if (name == "Diet Cola")
            G.E_MANAGER->add_event(ev_immediate([] {
                add_tag(new Tag("tag_double"));
                play_sound("generic1", (float)(0.9 + lua_math.random() * 0.1), 0.8f);
                play_sound("holo1", (float)(1.2 + lua_math.random() * 0.1), 0.4f);
                return true;
            }));
        if (name == "Invisible Joker" && abn("invis_rounds") >= abn("extra") && !has("blueprint")) {
            juice_card_until(self, [](Card *c) { return c->ability->get(K("loyalty_remaining")) == Value(0) && !G.RESET_JIGGLES; }, true);
            TableRef jokers = new_table();
            for (auto &j : G.jokers->cards)
                if (j.get() != self) jokers->push(obj(j.get()));
            if (jokers->len() > 0) {
                if (G.jokers->cards.size() <= G.jokers->config->get(K("card_limit")).num()) {
                    status(bpc(), {{"message", localize("k_duplicated_ex")}});
                    double seed = pseudoseed("invisible");
                    Card *chosen = pseudorandom_element(Value(jokers), &seed).as<Card>();
                    Card *card = copy_card(chosen, nullptr, 1, Value(), chosen->edition && chosen->edition->get(K("negative")).truthy());
                    if (card->ability->get(K("invis_rounds")).truthy()) card->ability->set("invis_rounds", 0);
                    card->add_to_deck();
                    G.jokers->emplace(card);
                } else {
                    status(bpc(), {{"message", localize("k_no_room_ex")}});
                }
            } else {
                status(bpc(), {{"message", localize("k_no_other_jokers")}});
            }
        }
    } else if (has("selling_card")) {
        if (name == "Campfire" && !has("blueprint")) {
            setab("x_mult", abn("x_mult") + abn("extra"));
            CardRef s(self);
            G.E_MANAGER->add_event(ev_immediate([s] {
                card_eval_status_text(s.get(), "extra", 0, -1, nullptr, vl::T({{"message", localize("k_upgrade_ex")}}));
                return true;
            }));
        }
        return Value();
    } else if (has("reroll_shop")) {
        if (name == "Flash Card" && !has("blueprint")) {
            setab("mult", abn("mult") + abn("extra"));
            CardRef s(self);
            G.E_MANAGER->add_event(ev_immediate([s] {
                card_eval_status_text(s.get(), "extra", 0, -1, nullptr,
                                      vl::T({{"message", lvar("a_mult", s->ability->get(K("mult")))}, {"colour", G.colour("MULT")}}));
                return true;
            }));
        }
    } else if (has("ending_shop")) {
        if (name == "Perkeo") {
            if (!G.consumeables->cards.empty()) {
                G.E_MANAGER->add_event(ev_immediate([] {
                    TableRef cs = new_table();
                    for (auto &c : G.consumeables->cards) cs->push(obj(c.get()));
                    double seed = pseudoseed("perkeo");
                    Card *card = copy_card(pseudorandom_element(Value(cs), &seed).as<Card>());
                    card->set_edition(Value(vl::T({{"negative", true}})), true);
                    card->add_to_deck();
                    G.consumeables->emplace(card);
                    return true;
                }));
                status(bpc(), {{"message", localize("k_duplicated_ex")}});
            }
            return Value();
        }
        return Value();
    } else if (has("skip_blind")) {
        if (name == "Throwback" && !has("blueprint")) {
            CardRef s(self);
            G.E_MANAGER->add_event(ev_immediate([s] {
                card_eval_status_text(s.get(), "extra", 0, -1, nullptr,
                                      vl::T({{"message", lvar("a_xmult", s->ability->get(K("x_mult")))}, {"colour", G.colour("RED")}, {"card", obj(s.get())}}));
                return true;
            }));
        }
        return Value();
    } else if (has("skipping_booster")) {
        if (name == "Red Card" && !has("blueprint")) {
            setab("mult", abn("mult") + abn("extra"));
            CardRef s(self);
            G.E_MANAGER->add_event(ev_immediate([s] {
                card_eval_status_text(s.get(), "extra", 0, -1, nullptr,
                                      vl::T({{"message", lvar("a_mult", s->ability->get(K("extra")))}, {"colour", G.colour("RED")}, {"delay", 0.45},
                                             {"card", obj(s.get())}}));
                return true;
            }));
        }
        return Value();
    } else if (has("playing_card_added") && !getting_sliced) {
        if (name == "Hologram" && !has("blueprint") && C("cards").truthy() && C("cards").get(1).truthy()) {
            setab("x_mult", abn("x_mult") + C("cards").table()->len() * abn("extra"));
            status(self, {{"message", lvar("a_xmult", ab("x_mult"))}});
        }
    } else if (has("first_hand_drawn")) {
        if (name == "Certificate") {
            CardRef s(self);
            Value bp = C("blueprint_card");
            G.E_MANAGER->add_event(ev_immediate([s, bp] {
                double seed = pseudoseed("cert_fr");
                Value front = pseudorandom_element(Value(G.P_CARDS), &seed);
                Card *c = create_playing_card(front, G.P_CENTERS->get(K("c_base")), G.hand.get(), false, false,
                                              A({fld(G.colour("SECONDARY_SET"), "Enhanced")}));
                const double seal_type = pseudorandom(pseudoseed("certsl"));
                c->set_seal(Value(seal_type > 0.75 ? "Red" : seal_type > 0.5 ? "Blue" : seal_type > 0.25 ? "Gold" : "Purple"), true);
                game_blind()->debuff_card(c);
                G.hand->sort();
                if (Card *b = bp.as<Card>()) b->juice_up_card();
                else s->juice_up_card();
                return true;
            }));
            playing_card_joker_effects({nullptr});
        }
        if (name == "DNA" && !has("blueprint"))
            juice_card_until(self, [](Card *) { return fld(gv("current_round"), "hands_played").num() == 0; }, true);
        if (name == "Trading Card" && !has("blueprint"))
            juice_card_until(self, [](Card *) { return fld(gv("current_round"), "discards_used").num() == 0 && !G.RESET_JIGGLES; }, true);
    } else if (has("setting_blind") && !getting_sliced) {
        const bool blind_boss = fld(C("blind"), "boss").truthy();
        if (name == "Chicot" && !has("blueprint") && blind_boss && !getting_sliced) {
            CardRef s(self);
            G.E_MANAGER->add_event(ev_immediate([s] {
                G.E_MANAGER->add_event(ev_immediate([] {
                    game_blind()->disable();
                    play_sound("timpani");
                    delay(0.4);
                    return true;
                }));
                card_eval_status_text(s.get(), "extra", 0, -1, nullptr, vl::T({{"message", localize("ph_boss_disabled")}}));
                return true;
            }));
        }
        if (name == "Madness" && !has("blueprint") && !blind_boss) {
            setab("x_mult", abn("x_mult") + abn("extra"));
            TableRef destructable = new_table();
            for (auto &j : G.jokers->cards)
                if (j.get() != self && !j->ability->get(K("eternal")).truthy() && !j->getting_sliced) destructable->push(obj(j.get()));
            Card *joker_to_destroy = nullptr;
            if (destructable->len() > 0) {
                double seed = pseudoseed("madness");
                joker_to_destroy = pseudorandom_element(Value(destructable), &seed).as<Card>();
            }
            if (joker_to_destroy && !bpc()->getting_sliced) {
                joker_to_destroy->getting_sliced = true;
                CardRef b(bpc()), d(joker_to_destroy);
                G.E_MANAGER->add_event(ev_immediate([b, d] {
                    float r = 0.8f;
                    b->juice_up_card(0.8f, &r);
                    d->start_dissolve(A({G.colour("RED")}), false, 1.6f);
                    return true;
                }));
            }
            if (!bpc()->getting_sliced) status(bpc(), {{"message", lvar("a_xmult", ab("x_mult"))}});
        }
        if (name == "Burglar" && !bpc()->getting_sliced) {
            CardRef b(bpc());
            const Value extra = ab("extra");
            G.E_MANAGER->add_event(ev_immediate([b, extra] {
                ease_discard(-fld(gv("current_round"), "discards_left").num(), false, true);
                ease_hands_played(extra.num());
                card_eval_status_text(b.get(), "extra", 0, -1, nullptr, vl::T({{"message", lvar("a_hands", extra)}}));
                return true;
            }));
        }
        const double joker_limit = G.jokers->config->get(K("card_limit")).num();
        if (name == "Riff-raff" && !bpc()->getting_sliced && G.jokers->cards.size() + gv("joker_buffer").num() < joker_limit) {
            const int to_create = (int)std::min(2.0, joker_limit - (G.jokers->cards.size() + gv("joker_buffer").num()));
            G.GAME->set("joker_buffer", gv("joker_buffer").num() + to_create);
            G.E_MANAGER->add_event(ev_immediate([to_create] {
                for (int i = 1; i <= to_create; i++) {
                    Card *card = create_card("Joker", G.jokers.get(), false, Value(0), false, false, "", "rif");
                    card->add_to_deck();
                    G.jokers->emplace(card);
                    card->start_materialize();
                    G.GAME->set("joker_buffer", 0);
                }
                return true;
            }));
            status(bpc(), {{"message", localize("k_plus_joker")}, {"colour", colour("BLUE")}});
        }
        if (name == "Cartomancer" && !bpc()->getting_sliced && consumeable_room()) {
            reserve_consumable();
            CardRef b(bpc());
            G.E_MANAGER->add_event(ev_immediate([b] {
                G.E_MANAGER->add_event(make_consumable_event("Tarot", "car", false));
                card_eval_status_text(b.get(), "extra", 0, -1, nullptr, vl::T({{"message", localize("k_plus_tarot")}, {"colour", G.colour("PURPLE")}}));
                return true;
            }));
        }
        if (name == "Ceremonial Dagger" && !has("blueprint")) {
            int my_pos = -1;
            for (size_t i = 0; i < G.jokers->cards.size(); i++)
                if (G.jokers->cards[i].get() == self) {
                    my_pos = (int)i;
                    break;
                }
            Card *next = my_pos >= 0 && my_pos + 1 < (int)G.jokers->cards.size() ? G.jokers->cards[my_pos + 1].get() : nullptr;
            if (next && !getting_sliced && !next->ability->get(K("eternal")).truthy() && !next->getting_sliced) {
                next->getting_sliced = true;
                G.GAME->set("joker_buffer", gv("joker_buffer").num() - 1);
                CardRef s(self), sl(next);
                G.E_MANAGER->add_event(ev_immediate([s, sl] {
                    G.GAME->set("joker_buffer", 0);
                    s->ability->set("mult", s->ability->get(K("mult")).num() + sl->sell_cost * 2);
                    float r = 0.8f;
                    s->juice_up_card(0.8f, &r);
                    sl->start_dissolve(A({hex("57ecab")}), false, 1.6f);
                    play_sound("slice1", (float)(0.96 + lua_math.random() * 0.08));
                    return true;
                }));
                status(self, {{"message", lvar("a_mult", Value(abn("mult") + 2 * next->sell_cost))}, {"colour", colour("RED")}, {"no_juice", true}});
            }
        }
        if (name == "Marble Joker" && !bpc()->getting_sliced) {
            G.E_MANAGER->add_event(ev_immediate([] {
                double seed = pseudoseed("marb_fr");
                Value front = pseudorandom_element(Value(G.P_CARDS), &seed);
                G.playing_card++;
                Card *card = new Card(G.play->T->x + G.play->T->w / 2, G.play->T->y, G.CARD_W, G.CARD_H, front, G.P_CENTERS->get(K("m_stone")),
                                      vl::T({{"playing_card", (double)G.playing_card}}));
                card->start_materialize(A({fld(G.colour("SECONDARY_SET"), "Enhanced")}));
                G.play->emplace(card);
                G.playing_cards.push_back(CardRef(card));
                return true;
            }));
            status(bpc(), {{"message", localize("k_plus_stone")}, {"colour", secondary("Enhanced")}});
            G.E_MANAGER->add_event(ev_immediate([] {
                G.deck->config->set("card_limit", G.deck->config->get(K("card_limit")).num() + 1);
                return true;
            }));
            draw_card(G.play.get(), G.deck.get(), 90, "up");
            playing_card_joker_effects({nullptr});
        }
        return Value();
    } else if (has("destroying_card") && !has("blueprint")) {
        std::vector<Card *> full_hand = cards_of(C("full_hand"));
        if (name == "Sixth Sense" && full_hand.size() == 1 && full_hand[0]->get_id() == 6 && fld(cr, "hands_played").num() == 0) {
            if (consumeable_room()) {
                reserve_consumable();
                G.E_MANAGER->add_event(make_consumable_event("Spectral", "sixth", true));
                status(bpc(), {{"message", localize("k_plus_spectral")}, {"colour", secondary("Spectral")}});
            }
            return Value(true);
        }
        return Value();
    } else if (has("cards_destroyed")) {
        std::vector<Card *> glass = cards_of(C("glass_shattered"));
        if (name == "Caino" && !has("blueprint")) {
            int faces = 0;
            for (Card *v : glass)
                if (v->is_face()) faces++;
            if (faces > 0) {
                CardRef s(self);
                G.E_MANAGER->add_event(ev_immediate([s, faces] {
                    G.E_MANAGER->add_event(ev_immediate([s, faces] {
                        s->ability->set("caino_xmult", s->ability->get(K("caino_xmult")).num() + faces * s->ability->get(K("extra")).num());
                        return true;
                    }));
                    card_eval_status_text(
                        s.get(), "extra", 0, -1, nullptr,
                        vl::T({{"message", lvar("a_xmult", Value(s->ability->get(K("caino_xmult")).num() + faces * s->ability->get(K("extra")).num()))}}));
                    return true;
                }));
            }
            return Value();
        }
        if (name == "Glass Joker" && !has("blueprint")) {
            int glasses = 0;
            for (Card *v : glass)
                if (v->shattered) glasses++;
            if (glasses > 0) {
                CardRef s(self);
                G.E_MANAGER->add_event(ev_immediate([s, glasses] {
                    G.E_MANAGER->add_event(ev_immediate([s, glasses] {
                        s->ability->set("x_mult", s->ability->get(K("x_mult")).num() + s->ability->get(K("extra")).num() * glasses);
                        return true;
                    }));
                    card_eval_status_text(
                        s.get(), "extra", 0, -1, nullptr,
                        vl::T({{"message", lvar("a_xmult", Value(s->ability->get(K("x_mult")).num() + s->ability->get(K("extra")).num() * glasses))}}));
                    return true;
                }));
            }
            return Value();
        }
    } else if (has("remove_playing_cards")) {
        std::vector<Card *> removed = cards_of(C("removed"));
        if (name == "Caino" && !has("blueprint")) {
            int face_cards = 0;
            for (Card *v : removed)
                if (v->is_face()) face_cards++;
            if (face_cards > 0) {
                setab("caino_xmult", abn("caino_xmult") + face_cards * abn("extra"));
                CardRef s(self);
                G.E_MANAGER->add_event(ev_immediate([s] {
                    card_eval_status_text(s.get(), "extra", 0, -1, nullptr, vl::T({{"message", lvar("a_xmult", s->ability->get(K("caino_xmult")))}}));
                    return true;
                }));
            }
            return Value();
        }
        if (name == "Glass Joker" && !has("blueprint")) {
            int glass_cards = 0;
            for (Card *v : removed)
                if (v->shattered) glass_cards++;
            if (glass_cards > 0) {
                CardRef s(self);
                G.E_MANAGER->add_event(ev_immediate([s, glass_cards] {
                    G.E_MANAGER->add_event(ev_immediate([s, glass_cards] {
                        s->ability->set("x_mult", s->ability->get(K("x_mult")).num() + s->ability->get(K("extra")).num() * glass_cards);
                        return true;
                    }));
                    card_eval_status_text(
                        s.get(), "extra", 0, -1, nullptr,
                        vl::T({{"message", lvar("a_xmult", Value(s->ability->get(K("x_mult")).num() + s->ability->get(K("extra")).num() * glass_cards))}}));
                    return true;
                }));
            }
            return Value();
        }
    } else if (has("using_consumeable")) {
        Card *cons = C("consumeable").as<Card>();
        if (name == "Glass Joker" && !has("blueprint") && cons && cons->ability->get(K("name")).equals("The Hanged Man")) {
            int shattered_glass = 0;
            for (auto &v : G.hand->highlighted)
                if (v->ability->get(K("name")).equals("Glass Card")) shattered_glass++;
            if (shattered_glass > 0) {
                setab("x_mult", abn("x_mult") + abn("extra") * shattered_glass);
                CardRef s(self);
                G.E_MANAGER->add_event(ev_immediate([s] {
                    card_eval_status_text(s.get(), "extra", 0, -1, nullptr, vl::T({{"message", lvar("a_xmult", s->ability->get(K("x_mult")))}}));
                    return true;
                }));
            }
            return Value();
        }
        if (name == "Fortune Teller" && !has("blueprint") && cons && cons->ability->get(K("set")).equals("Tarot")) {
            CardRef s(self);
            G.E_MANAGER->add_event(ev_immediate([s] {
                card_eval_status_text(s.get(), "extra", 0, -1, nullptr, vl::T({{"message", lvar("a_mult", fld(gv("consumeable_usage_total"), "tarot"))}}));
                return true;
            }));
        }
        if (name == "Constellation" && !has("blueprint") && cons && cons->ability->get(K("set")).equals("Planet")) {
            setab("x_mult", abn("x_mult") + abn("extra"));
            CardRef s(self);
            G.E_MANAGER->add_event(ev_immediate([s] {
                card_eval_status_text(s.get(), "extra", 0, -1, nullptr, vl::T({{"message", lvar("a_xmult", s->ability->get(K("x_mult")))}}));
                return true;
            }));
            return Value();
        }
        return Value();
    } else if (has("debuffed_hand")) {
        if (name == "Matador") {
            if (game_blind()->triggered.truthy()) {
                ease_dollars(abn("extra"));
                add_dollar_buffer(abn("extra"));
                return ret({{"message", money_text(ab("extra"))}, {"dollars", ab("extra")}, {"colour", colour("MONEY")}});
            }
        }
    } else if (has("pre_discard")) {
        if (name == "Burnt Joker" && fld(cr, "discards_used").num() <= 0 && !has("hook")) {
            std::vector<Card *> hl;
            for (auto &c : G.hand->highlighted) hl.push_back(c.get());
            const std::string text = get_poker_hand_info(hl).text;
            status(bpc(), {{"message", localize("k_upgrade_ex")}});
            Value h = gt("hands")->get(Value(text));
            update_hand_text(vl::T({{"sound", "button"}, {"volume", 0.7}, {"pitch", 0.8}, {"delay", 0.3}}),
                             vl::T({{"handname", localize(text, "poker_hands")}, {"chips", fld(h, "chips")}, {"mult", fld(h, "mult")}, {"level", fld(h, "level")}}));
            level_up_hand(bpc(), text, false, 1);
            update_hand_text(vl::T({{"sound", "button"}, {"volume", 0.7}, {"pitch", 1.1}, {"delay", 0}}),
                             vl::T({{"mult", 0}, {"chips", 0}, {"handname", ""}, {"level", ""}}));
        }
    } else if (has("discard")) {
        std::vector<Card *> full_hand = cards_of(C("full_hand"));
        Card *last = full_hand.empty() ? nullptr : full_hand.back();
        if (name == "Ramen" && !has("blueprint")) {
            if (abn("x_mult") - abn("extra") <= 1) {
                eaten(self);
                return ret({{"message", localize("k_eaten_ex")}, {"colour", colour("FILTER")}});
            }
            setab("x_mult", abn("x_mult") - abn("extra"));
            return ret({{"delay", 0.2}, {"message", lvar("a_xmult_minus", ab("extra"))}, {"colour", colour("RED")}});
        }
        if (name == "Yorick" && !has("blueprint")) {
            if (abn("yorick_discards") <= 1) {
                setab("yorick_discards", ex("discards"));
                setab("x_mult", abn("x_mult") + exn("xmult"));
                return ret({{"delay", 0.2}, {"message", lvar("a_xmult", ab("x_mult"))}, {"colour", colour("RED")}});
            }
            setab("yorick_discards", abn("yorick_discards") - 1);
            return Value();
        }
        if (name == "Trading Card" && !has("blueprint") && fld(cr, "discards_used").num() <= 0 && full_hand.size() == 1) {
            ease_dollars(abn("extra"));
            return ret({{"message", money_text(ab("extra"))}, {"colour", colour("MONEY")}, {"delay", 0.45}, {"remove", true}, {"card", obj(self)}});
        }
        if (name == "Castle" && !other->debuff && other->is_suit(fld(fld(cr, "castle_card"), "suit")) && !has("blueprint")) {
            setex("chips", exn("chips") + exn("chip_mod"));
            return ret({{"message", localize("k_upgrade_ex")}, {"card", obj(self)}, {"colour", colour("CHIPS")}});
        }
        if (name == "Mail-In Rebate" && !other->debuff && other->get_id() == fld(fld(cr, "mail_card"), "id").num()) {
            ease_dollars(abn("extra"));
            return ret({{"message", money_text(ab("extra"))}, {"colour", colour("MONEY")}, {"card", obj(self)}});
        }
        if (name == "Hit the Road" && !other->debuff && other->get_id() == 11 && !has("blueprint")) {
            setab("x_mult", abn("x_mult") + abn("extra"));
            return ret({{"message", lvar("a_xmult", ab("x_mult"))}, {"colour", colour("RED")}, {"delay", 0.45}, {"card", obj(self)}});
        }
        if (name == "Green Joker" && !has("blueprint") && other == last) {
            const double prev = abn("mult");
            setab("mult", std::max(0.0, abn("mult") - exn("discard_sub")));
            if (abn("mult") != prev) return ret({{"message", lvar("a_mult_minus", ex("discard_sub"))}, {"colour", colour("RED")}, {"card", obj(self)}});
        }
        if (name == "Faceless Joker" && other == last) {
            int face_cards = 0;
            for (Card *v : full_hand)
                if (v->is_face()) face_cards++;
            if (face_cards >= exn("faces")) {
                CardRef s(self);
                Value bp = C("blueprint_card");
                G.E_MANAGER->add_event(ev_immediate([s, bp] {
                    Value d = fld(s->ability->get(K("extra")), "dollars");
                    ease_dollars(d.num());
                    card_eval_status_text(bp.as<Card>() ? bp.as<Card>() : s.get(), "extra", 0, -1, nullptr,
                                          vl::T({{"message", money_text(d)}, {"colour", G.colour("MONEY")}, {"delay", 0.45}}));
                    return true;
                }));
                return Value();
            }
        }
        return Value();
    } else if (has("end_of_round")) {
        if (has("individual")) {
        } else if (has("repetition")) {
            if (C("cardarea").obj() == G.hand.get()) {
                if (name == "Mime" && (non_empty(C("card_effects").get(1)) || C("card_effects").table()->len() > 1))
                    return ret({{"message", localize("k_again_ex")}, {"repetitions", ab("extra")}, {"card", obj(self)}});
            }
        } else if (!has("blueprint")) {
            Blind *b = game_blind();
            if (name == "Campfire" && b->boss && abn("x_mult") > 1) {
                setab("x_mult", 1);
                return ret({{"message", localize("k_reset")}, {"colour", colour("RED")}});
            }
            if (name == "Rocket" && b->boss) {
                setex("dollars", exn("dollars") + exn("increase"));
                return ret({{"message", localize("k_upgrade_ex")}, {"colour", colour("MONEY")}});
            }
            if (name == "Turtle Bean" && !has("blueprint")) {
                if (exn("h_size") - exn("h_mod") <= 0) {
                    eaten(self);
                    return ret({{"message", localize("k_eaten_ex")}, {"colour", colour("FILTER")}});
                }
                setex("h_size", exn("h_size") - exn("h_mod"));
                G.hand->change_size(-(int)exn("h_mod"));
                return ret({{"message", lvar("a_handsize_minus", ex("h_mod"))}, {"colour", colour("FILTER")}});
            }
            if (name == "Invisible Joker" && !has("blueprint")) {
                setab("invis_rounds", abn("invis_rounds") + 1);
                if (abn("invis_rounds") == abn("extra")) juice_card_until(self, [](Card *c) { return !c->REMOVED; }, true);
                Value msg = abn("invis_rounds") < abn("extra") ? Value(ab("invis_rounds").to_string() + "/" + ab("extra").to_string()) : localize("k_active_ex");
                return ret({{"message", msg}, {"colour", colour("FILTER")}});
            }
            if (name == "Popcorn" && !has("blueprint")) {
                if (abn("mult") - abn("extra") <= 0) {
                    eaten(self);
                    return ret({{"message", localize("k_eaten_ex")}, {"colour", colour("RED")}});
                }
                setab("mult", abn("mult") - abn("extra"));
                return ret({{"message", lvar("a_mult_minus", ab("extra"))}, {"colour", colour("MULT")}});
            }
            if (name == "To Do List" && !has("blueprint")) {
                TableRef hands = new_table();
                gt("hands")->each([&](const Value &k, const Value &v) {
                    if (fld(v, "visible").truthy() && !(k == ab("to_do_poker_hand"))) hands->push(k);
                });
                double seed = pseudoseed("to_do");
                setab("to_do_poker_hand", pseudorandom_element(Value(hands), &seed));
                return ret({{"message", localize("k_reset")}});
            }
            if (name == "Egg") {
                setab("extra_value", abn("extra_value") + abn("extra"));
                set_cost();
                return ret({{"message", localize("k_val_up")}, {"colour", colour("MONEY")}});
            }
            if (name == "Gift Card") {
                for (CardArea *a : {G.jokers.get(), G.consumeables.get()})
                    for (auto &v : a->cards) {
                        v->ability->set("extra_value", v->ability->get(K("extra_value")).num() + abn("extra"));
                        v->set_cost();
                    }
                return ret({{"message", localize("k_val_up")}, {"colour", colour("MONEY")}});
            }
            if (name == "Hit the Road" && abn("x_mult") > 1) {
                setab("x_mult", 1);
                return ret({{"message", localize("k_reset")}, {"colour", colour("RED")}});
            }
            if (name == "Gros Michel" || name == "Cavendish") {
                if (pseudorandom(name == "Cavendish" ? "cavendish" : "gros_michel") < prob / exn("odds")) {
                    eaten(self);
                    if (name == "Gros Michel") gt("pool_flags")->set("gros_michel_extinct", true);
                    return ret({{"message", localize("k_extinct_ex")}});
                }
                return ret({{"message", localize("k_safe_ex")}});
            }
            if (name == "Mr. Bones" && has("game_over") && gv("chips").num() / b->chips >= 0.25) {
                CardRef s(self);
                G.E_MANAGER->add_event(ev_immediate([s] {
                    if (auto *hb = dynamic_cast<UIBox *>(G.HUD_blind.get()))
                        if (UIElement *e = hb->get_UIE_by_ID("HUD_blind_count")) e->juice_up();
                    if (auto *h = dynamic_cast<UIBox *>(G.HUD.get()))
                        if (UIElement *e = h->get_UIE_by_ID("chip_UI_count")) e->juice_up();
                    play_sound("tarot1");
                    s->start_dissolve();
                    return true;
                }));
                return ret({{"message", localize("k_saved_ex")}, {"saved", true}, {"colour", colour("RED")}});
            }
        }
    } else if (has("individual")) {
        if (C("cardarea").obj() == G.play.get()) {
            if (name == "Hiker") {
                other->ability->set("perma_bonus", (other->ability->get(K("perma_bonus")).truthy() ? other->ability->get(K("perma_bonus")).num() : 0) + abn("extra"));
                return ret({{"extra", Value(vl::T({{"message", localize("k_upgrade_ex")}, {"colour", colour("CHIPS")}}))}, {"colour", colour("CHIPS")}, {"card", obj(self)}});
            }
            if (name == "Lucky Cat" && other->lucky_trigger && !has("blueprint")) {
                setab("x_mult", abn("x_mult") + abn("extra"));
                return ret({{"extra", Value(vl::T({{"focus", obj(self)}, {"message", localize("k_upgrade_ex")}, {"colour", colour("MULT")}}))}, {"card", obj(self)}});
            }
            if (name == "Wee Joker" && other->get_id() == 2 && !has("blueprint")) {
                setex("chips", exn("chips") + exn("chip_mod"));
                return ret({{"extra", Value(vl::T({{"focus", obj(self)}, {"message", localize("k_upgrade_ex")}}))}, {"card", obj(self)}, {"colour", colour("CHIPS")}});
            }
            std::vector<Card *> scoring = cards_of(C("scoring_hand"));
            if (name == "Photograph") {
                Card *first_face = nullptr;
                for (Card *c : scoring)
                    if (c->is_face()) {
                        first_face = c;
                        break;
                    }
                if (other == first_face) return ret({{"x_mult", ab("extra")}, {"colour", colour("RED")}, {"card", obj(self)}});
            }
            if (name == "8 Ball" && consumeable_room()) {
                if (other->get_id() == 8 && pseudorandom("8ball") < prob / abn("extra")) {
                    reserve_consumable();
                    return ret({{"extra", Value(vl::T({{"focus", obj(self)},
                                                       {"message", localize("k_plus_tarot")},
                                                       {"func", Value(new Func([](const Value &) {
                                                           G.E_MANAGER->add_event(make_consumable_event("Tarot", "8ba", true));
                                                           return Value();
                                                       }))}}))},
                                {"colour", secondary("Tarot")},
                                {"card", obj(self)}});
                }
            }
            Value idol = fld(cr, "idol_card");
            if (name == "The Idol" && other->get_id() == fld(idol, "id").num() && other->is_suit(fld(idol, "suit")))
                return ret({{"x_mult", ab("extra")}, {"colour", colour("RED")}, {"card", obj(self)}});
            if (name == "Scary Face" && other->is_face()) return ret({{"chips", ab("extra")}, {"card", obj(self)}});
            if (name == "Smiley Face" && other->is_face()) return ret({{"mult", ab("extra")}, {"card", obj(self)}});
            if (name == "Golden Ticket" && other->ability->get(K("name")).equals("Gold Card")) {
                add_dollar_buffer(abn("extra"));
                return ret({{"dollars", ab("extra")}, {"card", obj(self)}});
            }
            if (name == "Scholar" && other->get_id() == 14) return ret({{"chips", ex("chips")}, {"mult", ex("mult")}, {"card", obj(self)}});
            if (name == "Walkie Talkie" && (other->get_id() == 10 || other->get_id() == 4))
                return ret({{"chips", ex("chips")}, {"mult", ex("mult")}, {"card", obj(self)}});
            if (name == "Business Card" && other->is_face() && pseudorandom("business") < prob / abn("extra")) {
                add_dollar_buffer(2);
                return ret({{"dollars", 2}, {"card", obj(self)}});
            }
            const int id = other->get_id();
            if (name == "Fibonacci" && (id == 2 || id == 3 || id == 5 || id == 8 || id == 14)) return ret({{"mult", ab("extra")}, {"card", obj(self)}});
            if (name == "Even Steven" && other->get_id() <= 10 && other->get_id() >= 0 && other->get_id() % 2 == 0)
                return ret({{"mult", ab("extra")}, {"card", obj(self)}});
            if (name == "Odd Todd" && ((other->get_id() <= 10 && other->get_id() >= 0 && other->get_id() % 2 == 1) || other->get_id() == 14))
                return ret({{"chips", ab("extra")}, {"card", obj(self)}});
            if (ab("effect").equals("Suit Mult") && other->is_suit(ex("suit"))) return ret({{"mult", ex("s_mult")}, {"card", obj(self)}});
            if (name == "Rough Gem" && other->is_suit(Value("Diamonds"))) {
                add_dollar_buffer(abn("extra"));
                return ret({{"dollars", ab("extra")}, {"card", obj(self)}});
            }
            if (name == "Onyx Agate" && other->is_suit(Value("Clubs"))) return ret({{"mult", ab("extra")}, {"card", obj(self)}});
            if (name == "Arrowhead" && other->is_suit(Value("Spades"))) return ret({{"chips", ab("extra")}, {"card", obj(self)}});
            if (name == "Bloodstone" && other->is_suit(Value("Hearts")) && pseudorandom("bloodstone") < prob / exn("odds"))
                return ret({{"x_mult", ex("Xmult")}, {"card", obj(self)}});
            if (name == "Ancient Joker" && other->is_suit(fld(fld(cr, "ancient_card"), "suit"))) return ret({{"x_mult", ab("extra")}, {"card", obj(self)}});
            if (name == "Triboulet" && (other->get_id() == 12 || other->get_id() == 13))
                return ret({{"x_mult", ab("extra")}, {"colour", colour("RED")}, {"card", obj(self)}});
        }
        if (C("cardarea").obj() == G.hand.get()) {
            auto debuffed = [&] { return ret({{"message", localize("k_debuffed")}, {"colour", colour("RED")}, {"card", obj(self)}}); };
            if (name == "Shoot the Moon" && other->get_id() == 12) {
                if (other->debuff) return debuffed();
                return ret({{"h_mult", 13}, {"card", obj(self)}});
            }
            if (name == "Baron" && other->get_id() == 13) {
                if (other->debuff) return debuffed();
                return ret({{"x_mult", ab("extra")}, {"card", obj(self)}});
            }
            if (name == "Reserved Parking" && other->is_face() && pseudorandom("parking") < prob / exn("odds")) {
                if (other->debuff) return debuffed();
                add_dollar_buffer(exn("dollars"));
                return ret({{"dollars", ex("dollars")}, {"card", obj(self)}});
            }
            if (name == "Raised Fist") {
                double temp_mult = 15, temp_id = 15;
                Card *raised = nullptr;
                for (auto &c : G.hand->cards)
                    if (temp_id >= c->base->get(K("id")).num() && !c->ability->get(K("effect")).equals("Stone Card")) {
                        temp_mult = c->base->get(K("nominal")).num();
                        temp_id = c->base->get(K("id")).num();
                        raised = c.get();
                    }
                if (raised == other) {
                    if (other->debuff) return debuffed();
                    return ret({{"h_mult", 2 * temp_mult}, {"card", obj(self)}});
                }
            }
        }
    } else if (has("repetition")) {
        auto again = [&](const Value &n) { return ret({{"message", localize("k_again_ex")}, {"repetitions", n}, {"card", obj(self)}}); };
        if (C("cardarea").obj() == G.play.get()) {
            if (name == "Sock and Buskin" && other->is_face()) return again(ab("extra"));
            if (name == "Hanging Chad" && other == C("scoring_hand").get(1).as<Card>()) return again(ab("extra"));
            if (name == "Dusk" && fld(cr, "hands_left").num() == 0) return again(ab("extra"));
            if (name == "Seltzer") return again(Value(1));
            const int id = other->get_id();
            if (name == "Hack" && (id == 2 || id == 3 || id == 4 || id == 5)) return again(ab("extra"));
        }
        if (C("cardarea").obj() == G.hand.get()) {
            if (name == "Mime" && (non_empty(C("card_effects").get(1)) || C("card_effects").table()->len() > 1)) return again(ab("extra"));
        }
    } else if (has("other_joker")) {
        Card *oj = C("other_joker").as<Card>();
        if (name == "Baseball Card" && fld(oj->config->get(K("center")), "rarity") == Value(2) && self != oj) {
            CardRef o(oj);
            G.E_MANAGER->add_event(ev_immediate([o] {
                float r = 0.5f;
                o->juice_up_card(0.5f, &r);
                return true;
            }));
            return ret({{"message", lvar("a_xmult", ab("extra"))}, {"Xmult_mod", ab("extra")}});
        }
    } else if (C("cardarea").obj() == G.jokers.get()) {
        std::vector<Card *> full_hand = cards_of(C("full_hand")), scoring = cards_of(C("scoring_hand"));
        const Value scoring_name = C("scoring_name");
        if (has("before")) {
            if (name == "Spare Trousers" && (hand_has("Two Pair") || hand_has("Full House")) && !has("blueprint")) {
                setab("mult", abn("mult") + abn("extra"));
                return ret({{"message", localize("k_upgrade_ex")}, {"colour", colour("RED")}, {"card", obj(self)}});
            }
            if (name == "Space Joker" && pseudorandom("space") < prob / abn("extra"))
                return ret({{"card", obj(self)}, {"level_up", true}, {"message", localize("k_level_up_ex")}});
            if (name == "Square Joker" && full_hand.size() == 4 && !has("blueprint")) {
                setex("chips", exn("chips") + exn("chip_mod"));
                return ret({{"message", localize("k_upgrade_ex")}, {"colour", colour("CHIPS")}, {"card", obj(self)}});
            }
            if (name == "Runner" && hand_has("Straight") && !has("blueprint")) {
                setex("chips", exn("chips") + exn("chip_mod"));
                return ret({{"message", localize("k_upgrade_ex")}, {"colour", colour("CHIPS")}, {"card", obj(self)}});
            }
            if (name == "Midas Mask" && !has("blueprint")) {
                int faces = 0;
                for (Card *v : scoring)
                    if (v->is_face()) {
                        faces++;
                        v->set_ability(G.P_CENTERS->get(K("m_gold")), false, true);
                        CardRef r(v);
                        G.E_MANAGER->add_event(ev_immediate([r] {
                            r->juice_up_card();
                            return true;
                        }));
                    }
                if (faces > 0) return ret({{"message", localize("k_gold")}, {"colour", colour("MONEY")}, {"card", obj(self)}});
            }
            if (name == "Vampire" && !has("blueprint")) {
                int enhanced = 0;
                for (Card *v : scoring) {
                    Value vc = v->config->get(K("center"));
                    if (vc.truthy() && !fld(vc, "name").equals("Default Base") && !v->debuff && !v->vampired) {
                        enhanced++;
                        v->vampired = true;
                        v->set_ability(G.P_CENTERS->get(K("c_base")), false, true);
                        CardRef r(v);
                        G.E_MANAGER->add_event(ev_immediate([r] {
                            r->juice_up_card();
                            r->vampired = false;
                            return true;
                        }));
                    }
                }
                if (enhanced > 0) {
                    setab("x_mult", abn("x_mult") + abn("extra") * enhanced);
                    return ret({{"message", lvar("a_xmult", ab("x_mult"))}, {"colour", colour("MULT")}, {"card", obj(self)}});
                }
            }
            if (name == "To Do List" && scoring_name == ab("to_do_poker_hand")) {
                ease_dollars(exn("dollars"));
                add_dollar_buffer(exn("dollars"));
                return ret({{"message", money_text(ex("dollars"))}, {"dollars", ex("dollars")}, {"colour", colour("MONEY")}});
            }
            if (name == "DNA" && fld(cr, "hands_played").num() == 0) {
                if (full_hand.size() == 1) {
                    G.playing_card++;
                    Card *c = copy_card(full_hand[0], nullptr, 1, Value((double)G.playing_card));
                    c->add_to_deck();
                    G.deck->config->set("card_limit", G.deck->config->get(K("card_limit")).num() + 1);
                    G.playing_cards.push_back(CardRef(c));
                    G.hand->emplace(c);
                    c->states.visible = false;
                    CardRef r(c);
                    G.E_MANAGER->add_event(ev_immediate([r] {
                        r->start_materialize();
                        return true;
                    }));
                    return ret({{"message", localize("k_copied_ex")}, {"colour", colour("CHIPS")}, {"card", obj(self)}, {"playing_cards_created", Value(A({true}))}});
                }
            }
            if (name == "Ride the Bus" && !has("blueprint")) {
                bool faces = false;
                for (Card *c : scoring)
                    if (c->is_face()) faces = true;
                if (faces) {
                    const double last_mult = abn("mult");
                    setab("mult", 0);
                    if (last_mult > 0) return ret({{"card", obj(self)}, {"message", localize("k_reset")}});
                } else {
                    setab("mult", abn("mult") + abn("extra"));
                }
            }
            if (name == "Obelisk" && !has("blueprint")) {
                bool reset = true;
                Value sh = gt("hands")->get(scoring_name);
                const double play_more_than = fld(sh, "played").truthy() ? fld(sh, "played").num() : 0;
                gt("hands")->each([&](const Value &k, const Value &v) {
                    if (!(k == scoring_name) && fld(v, "played").num() >= play_more_than && fld(v, "visible").truthy()) reset = false;
                });
                if (reset) {
                    if (abn("x_mult") > 1) {
                        setab("x_mult", 1);
                        return ret({{"card", obj(self)}, {"message", localize("k_reset")}});
                    }
                } else {
                    setab("x_mult", abn("x_mult") + abn("extra"));
                }
            }
            if (name == "Green Joker" && !has("blueprint")) {
                setab("mult", abn("mult") + exn("hand_add"));
                return ret({{"card", obj(self)}, {"message", lvar("a_mult", ex("hand_add"))}});
            }
        } else if (has("after")) {
            if (name == "Ice Cream" && !has("blueprint")) {
                if (exn("chips") - exn("chip_mod") <= 0) {
                    eaten(self);
                    return ret({{"message", localize("k_melted_ex")}, {"colour", colour("CHIPS")}});
                }
                setex("chips", exn("chips") - exn("chip_mod"));
                return ret({{"message", lvar("a_chips_minus", ex("chip_mod"))}, {"colour", colour("CHIPS")}});
            }
            if (name == "Seltzer" && !has("blueprint")) {
                if (abn("extra") - 1 <= 0) {
                    eaten(self);
                    return ret({{"message", localize("k_drank_ex")}, {"colour", colour("FILTER")}});
                }
                setab("extra", abn("extra") - 1);
                return ret({{"message", Value(ab("extra").to_string())}, {"colour", colour("FILTER")}});
            }
        } else {
            auto xm = [&](const Value &v) { return ret({{"message", lvar("a_xmult", v)}, {"Xmult_mod", v}}); };
            auto mm = [&](const Value &v) { return ret({{"message", lvar("a_mult", v)}, {"mult_mod", v}}); };
            auto cm = [&](const Value &v) { return ret({{"message", lvar("a_chips", v)}, {"chip_mod", v}, {"colour", colour("CHIPS")}}); };
            const double dollars_now = gv("dollars").num() + (gv("dollar_buffer").truthy() ? gv("dollar_buffer").num() : 0);
            if (name == "Loyalty Card") {
                const double every = exn("every");
                setab("loyalty_remaining", std::fmod(every - 1 - (gv("hands_played").num() - abn("hands_played_at_create")), every + 1));
                if (abn("loyalty_remaining") < 0) setab("loyalty_remaining", abn("loyalty_remaining") + every + 1);  // Lua's % is a floored modulo
                if (has("blueprint")) {
                    if (abn("loyalty_remaining") == every) return xm(ex("Xmult"));
                } else {
                    if (abn("loyalty_remaining") == 0) {
                        juice_card_until(self, [](Card *c) { return c->ability->get(K("loyalty_remaining")).num() == 0; }, true);
                    } else if (abn("loyalty_remaining") == every) {
                        return xm(ex("Xmult"));
                    }
                }
            }
            const Value type = ab("type");
            if (name != "Seeing Double" && abn("x_mult") > 1 && (type.equals("") || hand_has(std::string(type.str()).c_str())))
                return ret({{"message", lvar("a_xmult", ab("x_mult"))}, {"colour", colour("RED")}, {"Xmult_mod", ab("x_mult")}});
            if (abn("t_mult") > 0 && hand_has(std::string(type.str()).c_str())) return mm(ab("t_mult"));
            if (abn("t_chips") > 0 && hand_has(std::string(type.str()).c_str()))
                return ret({{"message", lvar("a_chips", ab("t_chips"))}, {"chip_mod", ab("t_chips")}});
            if (name == "Half Joker" && full_hand.size() <= exn("size")) return mm(ex("mult"));
            if (name == "Abstract Joker") {
                int x = 0;
                for (auto &j : G.jokers->cards)
                    if (j->ability->get(K("set")).equals("Joker")) x++;
                return mm(Value(x * abn("extra")));
            }
            if (name == "Acrobat" && fld(cr, "hands_left").num() == 0) return xm(ab("extra"));
            if (name == "Mystic Summit" && fld(cr, "discards_left").num() == exn("d_remaining")) return mm(ex("mult"));
            if (name == "Misprint") {
                const double temp_mult = pseudorandom(pseudoseed("misprint"), exn("min"), exn("max"));
                return mm(Value(temp_mult));
            }
            if (name == "Banner" && fld(cr, "discards_left").num() > 0)
                return ret({{"message", lvar("a_chips", Value(fld(cr, "discards_left").num() * abn("extra")))},
                            {"chip_mod", fld(cr, "discards_left").num() * abn("extra")}});
            if (name == "Stuntman") return ret({{"message", lvar("a_chips", ex("chip_mod"))}, {"chip_mod", ex("chip_mod")}});
            if (name == "Matador") {
                if (game_blind()->triggered.truthy()) {
                    ease_dollars(abn("extra"));
                    add_dollar_buffer(abn("extra"));
                    return ret({{"message", money_text(ab("extra"))}, {"dollars", ab("extra")}, {"colour", colour("MONEY")}});
                }
            }
            if (name == "Supernova") return mm(fld(gt("hands")->get(scoring_name), "played"));
            if (name == "Ceremonial Dagger" && abn("mult") > 0) return mm(ab("mult"));
            if (name == "Vagabond" && consumeable_room()) {
                if (gv("dollars").num() <= abn("extra")) {
                    reserve_consumable();
                    G.E_MANAGER->add_event(make_consumable_event("Tarot", "vag", true));
                    return ret({{"message", localize("k_plus_tarot")}, {"card", obj(self)}});
                }
            }
            if (name == "Superposition" && consumeable_room()) {
                int aces = 0;
                for (Card *c : scoring)
                    if (c->get_id() == 14) aces++;
                if (aces >= 1 && hand_has("Straight")) {
                    reserve_consumable();
                    G.E_MANAGER->add_event(make_consumable_event("Tarot", "sup", true));
                    return ret({{"message", localize("k_plus_tarot")}, {"colour", secondary("Tarot")}, {"card", obj(self)}});
                }
            }
            if (name == "Seance" && consumeable_room()) {
                if (hand_has(std::string(ex("poker_hand").str()).c_str())) {
                    reserve_consumable();
                    G.E_MANAGER->add_event(make_consumable_event("Spectral", "sea", true));
                    return ret({{"message", localize("k_plus_spectral")}, {"colour", secondary("Spectral")}, {"card", obj(self)}});
                }
            }
            if (name == "Flower Pot" || name == "Seeing Double") {
                int hearts = 0, diamonds = 0, spades = 0, clubs = 0;
                const bool pot = name == "Flower Pot";
                for (Card *c : scoring) {
                    if (c->ability->get(K("name")).equals("Wild Card")) continue;
                    if (pot) {
                        if (c->is_suit(Value("Hearts"), true) && hearts == 0) hearts++;
                        else if (c->is_suit(Value("Diamonds"), true) && diamonds == 0) diamonds++;
                        else if (c->is_suit(Value("Spades"), true) && spades == 0) spades++;
                        else if (c->is_suit(Value("Clubs"), true) && clubs == 0) clubs++;
                    } else {
                        if (c->is_suit(Value("Hearts"))) hearts++;
                        if (c->is_suit(Value("Diamonds"))) diamonds++;
                        if (c->is_suit(Value("Spades"))) spades++;
                        if (c->is_suit(Value("Clubs"))) clubs++;
                    }
                }
                for (Card *c : scoring) {
                    if (!c->ability->get(K("name")).equals("Wild Card")) continue;
                    if (pot) {
                        if (c->is_suit(Value("Hearts")) && hearts == 0) hearts++;
                        else if (c->is_suit(Value("Diamonds")) && diamonds == 0) diamonds++;
                        else if (c->is_suit(Value("Spades")) && spades == 0) spades++;
                        else if (c->is_suit(Value("Clubs")) && clubs == 0) clubs++;
                    } else {
                        if (c->is_suit(Value("Clubs")) && clubs == 0) clubs++;
                        else if (c->is_suit(Value("Diamonds")) && diamonds == 0) diamonds++;
                        else if (c->is_suit(Value("Spades")) && spades == 0) spades++;
                        else if (c->is_suit(Value("Hearts")) && hearts == 0) hearts++;
                    }
                }
                if (pot ? (hearts > 0 && diamonds > 0 && spades > 0 && clubs > 0) : ((hearts > 0 || diamonds > 0 || spades > 0) && clubs > 0))
                    return xm(ab("extra"));
            }
            if (name == "Wee Joker") return cm(ex("chips"));
            if (name == "Castle" && exn("chips") > 0) return cm(ex("chips"));
            if (name == "Blue Joker" && !G.deck->cards.empty()) return cm(Value(abn("extra") * G.deck->cards.size()));
            const double erosion = gv("starting_deck_size").num() - G.playing_cards.size();
            if (name == "Erosion" && erosion > 0)
                return ret({{"message", lvar("a_mult", Value(abn("extra") * erosion))}, {"mult_mod", abn("extra") * erosion}, {"colour", colour("MULT")}});
            if (name == "Square Joker") return cm(ex("chips"));
            if (name == "Runner") return cm(ex("chips"));
            if (name == "Ice Cream") return cm(ex("chips"));
            if (name == "Stone Joker" && abn("stone_tally") > 0) return cm(Value(abn("extra") * abn("stone_tally")));
            if (name == "Steel Joker" && abn("steel_tally") > 0) {
                Value v(1 + abn("extra") * abn("steel_tally"));
                return ret({{"message", lvar("a_xmult", v)}, {"Xmult_mod", v}, {"colour", colour("MULT")}});
            }
            if (name == "Bull" && dollars_now > 0) return cm(Value(abn("extra") * std::max(0.0, dollars_now)));
            if (name == "Driver's License" && (ab("driver_tally").truthy() ? abn("driver_tally") : 0) >= 16) return xm(ab("extra"));
            if (name == "Blackboard") {
                int black = 0, all = 0;
                for (auto &v : G.hand->cards) {
                    all++;
                    if (v->is_suit(Value("Clubs"), false, true) || v->is_suit(Value("Spades"), false, true)) black++;
                }
                if (black == all) return xm(ab("extra"));
            }
            if (name == "Joker Stencil" && G.jokers->config->get(K("card_limit")).num() - G.jokers->cards.size() > 0) return xm(ab("x_mult"));
            if (name == "Swashbuckler" && abn("mult") > 0) return mm(ab("mult"));
            if (name == "Joker") return mm(ab("mult"));
            if ((name == "Spare Trousers" || name == "Ride the Bus" || name == "Flash Card" || name == "Popcorn" || name == "Green Joker") && abn("mult") > 0)
                return mm(ab("mult"));
            Value cut = gv("consumeable_usage_total");
            if (name == "Fortune Teller" && cut.truthy() && fld(cut, "tarot").num() > 0) return mm(fld(cut, "tarot"));
            if (name == "Gros Michel") return mm(ex("mult"));
            if (name == "Cavendish") return xm(ex("Xmult"));
            if (name == "Red Card" && abn("mult") > 0) return mm(ab("mult"));
            Value sh = gt("hands")->get(scoring_name);
            if (name == "Card Sharp" && sh.truthy() && fld(sh, "played_this_round").num() > 1) return xm(ex("Xmult"));
            if (name == "Bootstraps" && std::floor(dollars_now / exn("dollars")) >= 1)
                return mm(Value(exn("mult") * std::floor(dollars_now / exn("dollars"))));
            if (name == "Caino" && abn("caino_xmult") > 1) return xm(ab("caino_xmult"));
        }
    }
    return Value();
}

}  // namespace vl
