// Booster packs: card.lua Card:open / Card:explode, UI_definitions.lua create_UIBox_*_pack,
// game.lua Game:update_*_pack, and G.FUNCS.skip_booster / can_skip_booster / select_button_check.
#include <cmath>
#include <string>

#include "engine/controller.h"
#include "engine/event.h"
#include "engine/particles.h"
#include "engine/random.h"
#include "engine/text.h"
#include "engine/ui.h"
#include "game/card.h"
#include "game/card_factory.h"
#include "game/functions.h"
#include "game/g.h"
#include "game/shop.h"
#include "game/sound.h"
#include "game/state_events.h"
#include "game/ui_definitions.h"

namespace vl {

namespace {
inline Value fld(const Value &t, const char *k) { return t.get(Value(intern(k))); }
inline Value gv(const char *k) { return G.GAME->get(Value(intern(k))); }
Value obj(Object *o) { return Value(o); }
Value nd(int uit, TableRef cfg, TableRef nodes = nullptr) {
    TableRef t = vl::T({{"n", (double)uit}, {"config", Value(cfg)}});
    if (nodes) t->set("nodes", Value(nodes));
    return Value(t);
}
TableRef list(std::initializer_list<Value> items) {
    TableRef t = new_table();
    for (const Value &v : items)
        if (!v.is_nil()) t->push(v);
    return t;
}
Value hex(const char *h) {
    auto byte = [&](int i) { return std::stoi(std::string(h + i, 2), nullptr, 16) / 255.0; };
    return Value(A({byte(0), byte(2), byte(4), 1}));
}
std::string ante_str() { return fld(gv("round_resets"), "ante").to_string(); }
double gamespeed() { return G.SETTINGS->get(K("GAMESPEED")).num(); }

// create_UIBox_{arcana,spectral,standard,buffoon,celestial}_pack: one layout, the area width and the
// title differ
TableRef create_UIBox_pack(const char *title_key, double width_mult, double width_add) {
    const double size = gv("pack_size").num();
    G.pack_cards = new CardArea(G.ROOM->T->x + 9 + G.hand->T->x, G.hand->T->y, (float)(size * G.CARD_W * width_mult + width_add), 1.05f * G.CARD_H,
                                vl::T({{"card_limit", size}, {"type", "consumeable"}, {"highlight_limit", 1}}));
    auto dyna = [](TableRef cfg) { return nd(UIT_O, vl::T({{"object", obj(new DynaText(cfg))}})); };
    Value white = Value(A({G.colour("WHITE")}));
    TableRef title = vl::T({{"string", localize(title_key)}, {"colours", white}, {"shadow", true}, {"rotate", true}, {"bump", true}, {"spacing", 2},
                            {"scale", 0.7}, {"maxw", 4}, {"pop_in", 0.5}});
    TableRef choose = vl::T({{"string", Value(A({Value(localize("k_choose").to_string() + " ")}))}, {"colours", white}, {"shadow", true}, {"rotate", true},
                             {"bump", true}, {"spacing", 2}, {"scale", 0.5}, {"pop_in", 0.7}});
    TableRef choices = vl::T({{"string", Value(A({Value(vl::T({{"ref_table", Value(G.GAME)}, {"ref_value", "pack_choices"}}))}))}, {"colours", white},
                              {"shadow", true}, {"rotate", true}, {"bump", true}, {"spacing", 2}, {"scale", 0.5}, {"pop_in", 0.7}});
    return TableRef(
        nd(UIT_ROOT, vl::T({{"align", "tm"}, {"r", 0.15}, {"colour", G.colour("CLEAR")}, {"padding", 0.15}}),
           list({nd(UIT_R, vl::T({{"align", "cl"}, {"colour", G.colour("CLEAR")}, {"r", 0.15}, {"padding", 0.1}, {"minh", 2}, {"shadow", true}}),
                    list({nd(UIT_R, vl::T({{"align", "cm"}}),
                             list({nd(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.1}}),
                                      list({nd(UIT_C, vl::T({{"align", "cm"}, {"r", 0.2}, {"colour", G.colour("CLEAR")}, {"shadow", true}}),
                                               list({nd(UIT_O, vl::T({{"object", obj(G.pack_cards.get())}}))}))}))})),
                          nd(UIT_R, vl::T({{"align", "cm"}}), new_table()),
                          nd(UIT_R, vl::T({{"align", "tm"}}),
                             list({nd(UIT_C, vl::T({{"align", "tm"}, {"padding", 0.05}, {"minw", 2.4}}), new_table()),
                                   nd(UIT_C, vl::T({{"align", "tm"}, {"padding", 0.05}}),
                                      list({Value(UIBox_dyn_container(
                                          list({nd(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.05}, {"minw", 4}}),
                                                   list({nd(UIT_R, vl::T({{"align", "bm"}, {"padding", 0.05}}), list({dyna(title)})),
                                                         nd(UIT_R, vl::T({{"align", "bm"}, {"padding", 0.05}}), list({dyna(choose), dyna(choices)}))}))})))})),
                                   nd(UIT_C, vl::T({{"align", "tm"}, {"padding", 0.05}, {"minw", 2.4}}),
                                      list({nd(UIT_R, vl::T({{"minh", 0.2}}), new_table()),
                                            nd(UIT_R,
                                               vl::T({{"align", "tm"}, {"padding", 0.2}, {"minh", 1.2}, {"minw", 1.8}, {"r", 0.15}, {"colour", G.colour("GREY")},
                                                      {"one_press", true}, {"button", "skip_booster"}, {"hover", true}, {"shadow", true}, {"func", "can_skip_booster"}}),
                                               list({nd(UIT_T, vl::T({{"text", localize("b_skip")}, {"scale", 0.5}, {"colour", G.colour("WHITE")}, {"shadow", true},
                                                                      {"focus_args", Value(vl::T({{"button", "y"}, {"orientation", "bm"}}))},
                                                                      {"func", "set_button_pip"}}))}))}))}))}))}))
            .table());
}

Particles *pack_particles(TableRef cfg) {
    cfg->set("attach", obj(G.ROOM_ATTACH.get()));
    return new Particles(1, 1, 0, 0, cfg);
}
}  // namespace

// ------------------------------------------------------------------ Card:explode
void Card::explode(TableRef colours, double explode_time_fac) {
    const double explode_time = 1.3 * explode_time_fac * std::sqrt(gamespeed());
    dissolve = 0;
    dissolve_colours = colours ? colours : A({G.colour("WHITE")});
    const double start_time = G.TIMERS.TOTAL;
    play_sound("explosion_buildup1");
    has_juice = true;
    juice = Juice{};
    juice.handled_elsewhere = true;
    juice.start_time = start_time;
    juice.end_time = start_time + explode_time;
    Ref<Particles> parts1 = new Particles(0, 0, 0, 0,
                                          vl::T({{"timer_type", "TOTAL"},
                                                 {"timer", 0.01 * explode_time},
                                                 {"scale", 0.2},
                                                 {"speed", 2},
                                                 {"lifespan", 0.2 * explode_time},
                                                 {"attach", obj(this)},
                                                 {"colours", Value(dissolve_colours)},
                                                 {"fill", true}}));
    CardRef self(this);
    EventConfig e0 = ev_immediate([self, start_time, explode_time] {
        if (self->has_juice) {
            const double percent = (G.TIMERS.TOTAL - start_time) / explode_time;
            const double t = G.TIMERS.TOTAL;
            self->juice.r = (float)(0.05 * (std::sin(5 * t) + std::cos(0.33 + 41.15332 * t) + std::cos(67.12 * t)) * percent);
            self->juice.scale = (float)(percent * 0.15);
        }
        return G.TIMERS.TOTAL - start_time > 1.5 * explode_time;
    });
    e0.blockable = false;
    G.E_MANAGER->add_event(e0);
    EventConfig ease;
    ease.trigger = Trigger::Ease;
    ease.blockable = false;
    ease.ref = ValueRef::of(&dissolve, this);
    ease.ease_to = 0.3;
    ease.delay = 0.9 * explode_time;
    G.E_MANAGER->add_event(ease);
    auto parts2 = std::make_shared<Ref<Particles>>();
    EventConfig e1 = ev_after(0.9 * explode_time, [self, parts1, parts2, explode_time] {
        Particles *p2 = new Particles(0, 0, 0, 0,
                                      vl::T({{"timer_type", "TOTAL"},
                                             {"pulse_max", 30},
                                             {"timer", 0.003},
                                             {"scale", 0.6},
                                             {"speed", 15},
                                             {"lifespan", 0.5},
                                             {"attach", obj(self.get())},
                                             {"colours", Value(self->dissolve_colours)}}));
        *parts2 = p2;
        RoleArgs ra;
        static const Bond weak = Bond::Weak;
        ra.r_bond = &weak;
        p2->set_role(ra);
        EventConfig e = {};
        e.trigger = Trigger::Ease;
        e.blockable = false;
        e.ref = ValueRef::of(&self->dissolve, self.get());
        e.ease_to = 1;
        e.delay = 0.1 * explode_time;
        G.E_MANAGER->add_event(e);
        self->juice_up_card();
        G.VIBRATION += 1;
        G.MOBILE_VIBRATION_QUEUE = std::max(G.MOBILE_VIBRATION_QUEUE, 3.0);
        play_sound("explosion_release1");
        parts1->fade(0.3 * explode_time);
        return true;
    });
    e1.blockable = false;
    G.E_MANAGER->add_event(e1);
    EventConfig e2 = ev_after(1.4 * explode_time, [parts2, explode_time] {
        if (*parts2) {
            EventConfig e = {};
            e.trigger = Trigger::Ease;
            e.blockable = false;
            e.blocking = false;
            e.ref = ValueRef::of(&(*parts2)->pscale, parts2->get());
            e.ease_to = 0;
            e.delay = 0.1 * explode_time;
            G.E_MANAGER->add_event(e);
        }
        return true;
    });
    e2.blockable = false;
    G.E_MANAGER->add_event(e2);
    EventConfig e3 = ev_after(1.5 * explode_time, [self] {
        self->remove();
        return true;
    });
    e3.blockable = false;
    G.E_MANAGER->add_event(e3);
}

// ------------------------------------------------------------------ Card:open
void Card::open() {
    if (!ability->get(K("set")).equals("Booster")) return;
    stop_use();
    G.STATE_COMPLETE = false;
    opening = true;
    const Value center = config->get(K("center"));
    if (!fld(center, "discovered").truthy()) discover_card(center);
    states.hover.can = false;
    const std::string name = ability->get(K("name")).to_string();
    auto has = [&](const char *s) { return name.find(s) != std::string::npos; };
    if (has("Arcana")) G.STATE = TAROT_PACK;
    else if (has("Celestial")) G.STATE = PLANET_PACK;
    else if (has("Spectral")) G.STATE = SPECTRAL_PACK;
    else if (has("Standard")) G.STATE = STANDARD_PACK;
    else if (has("Buffoon")) G.STATE = BUFFOON_PACK;
    if (has("Arcana") || has("Celestial") || has("Spectral") || has("Standard") || has("Buffoon")) G.GAME->set("pack_size", ability->get(K("extra")));
    const Value choose = fld(fld(center, "config"), "choose");
    G.GAME->set("pack_choices", choose.truthy() ? choose : Value(1));
    CardRef self(this);
    if (cost > 0) {
        G.E_MANAGER->add_event(ev_after(0.2, [self] {
            inc_career_stat("c_shop_dollars_spent", self->cost);
            self->juice_up_card();
            return true;
        }));
        ease_dollars(-cost);
    } else {
        delay(0.2);
    }
    G.E_MANAGER->add_event(ev_after(0.4, [self, name] {
        self->explode();
        auto pack_cards = std::make_shared<std::vector<CardRef>>();
        auto has = [name](const char *s) { return name.find(s) != std::string::npos; };
        EventConfig mk = ev_after(1.3 * std::sqrt(gamespeed()), [self, pack_cards, has] {
            const int size = (int)self->ability->get(K("extra")).num();
            for (int i = 1; i <= size; i++) {
                Card *card = nullptr;
                if (has("Arcana")) {
                    if (fld(gv("used_vouchers"), "v_omen_globe").truthy() && pseudorandom("omen_globe") > 0.8)
                        card = create_card("Spectral", G.pack_cards.get(), false, Value(), true, true, "", "ar2");
                    else
                        card = create_card("Tarot", G.pack_cards.get(), false, Value(), true, true, "", "ar1");
                } else if (has("Celestial")) {
                    if (fld(gv("used_vouchers"), "v_telescope").truthy() && i == 1) {
                        static const char *handlist[] = {"Flush Five", "Flush House", "Five of a Kind", "Straight Flush", "Four of a Kind", "Full House",
                                                         "Flush", "Straight", "Three of a Kind", "Two Pair", "Pair", "High Card"};
                        std::string hand, planet;
                        double tally = 0;
                        for (const char *h : handlist) {
                            Value hv = fld(gv("hands"), h);
                            if (fld(hv, "visible").truthy() && fld(hv, "played").num() > tally) {
                                hand = h;
                                tally = fld(hv, "played").num();
                            }
                        }
                        if (!hand.empty())
                            G.P_CENTER_POOLS->get(K("Planet")).table()->each([&](const Value &, const Value &v) {
                                if (fld(fld(v, "config"), "hand_type").equals(hand)) planet = fld(v, "key").to_string();
                            });
                        card = create_card("Planet", G.pack_cards.get(), false, Value(), true, true, planet, "pl1");
                    } else {
                        card = create_card("Planet", G.pack_cards.get(), false, Value(), true, true, "", "pl1");
                    }
                } else if (has("Spectral")) {
                    card = create_card("Spectral", G.pack_cards.get(), false, Value(), true, true, "", "spe");
                } else if (has("Standard")) {
                    card = create_card(pseudorandom(pseudoseed("stdset" + ante_str())) > 0.6 ? "Enhanced" : "Base", G.pack_cards.get(), false, Value(), false, true,
                                       "", "sta");
                    card->set_edition(poll_edition("standard_edition" + ante_str(), 2, true));
                    const double seal_poll = pseudorandom(pseudoseed("stdseal" + ante_str()));
                    if (seal_poll > 1 - 0.02 * 10) {
                        const double seal_type = pseudorandom(pseudoseed("stdsealtype" + ante_str()));
                        card->set_seal(Value(seal_type > 0.75 ? "Red" : seal_type > 0.5 ? "Blue" : seal_type > 0.25 ? "Gold" : "Purple"));
                    }
                } else if (has("Buffoon")) {
                    card = create_card("Joker", G.pack_cards.get(), false, Value(), true, true, "", "buf");
                }
                if (!card) continue;
                card->T->x = self->T->x;
                card->T->y = self->T->y;
                card->start_materialize(A({G.colour("WHITE"), G.colour("WHITE")}), false, (float)(1.5 * gamespeed()));
                pack_cards->push_back(CardRef(card));
            }
            return true;
        });
        mk.blockable = false;
        mk.blocking = false;
        G.E_MANAGER->add_event(mk);
        EventConfig em = ev_after(1.3 * std::sqrt(gamespeed()), [pack_cards] {
            if (G.pack_cards && G.pack_cards->VT.y < G.ROOM->T->h) {
                for (auto &c : *pack_cards) G.pack_cards->emplace(c.get());
                return true;
            }
            return false;
        });
        em.blockable = false;
        em.blocking = false;
        G.E_MANAGER->add_event(em);
        for (auto &j : std::vector<CardRef>(G.jokers->cards)) j->calculate_joker(vl::T({{"open_booster", true}, {"card", obj(self.get())}}));
        if (fld(gv("modifiers"), "inflation").truthy()) {
            G.GAME->set("inflation", gv("inflation").num() + 1);
            G.E_MANAGER->add_event(ev_immediate([] {
                G.I.CARD.each([](Moveable *v) {
                    if (Card *c = dynamic_cast<Card *>(v)) c->set_cost();
                });
                return true;
            }));
        }
        return true;
    }));
}

// ------------------------------------------------------------------ Game:update_*_pack
void pack_update(int state) {
    if (G.buttons) {
        G.buttons->remove();
        G.buttons = nullptr;
    }
    if (auto *s = dynamic_cast<Moveable *>(G.shop.get())) s->alignment.offset.y = G.ROOM->T->y + 11;
    if (G.STATE_COMPLETE) return;
    G.STATE_COMPLETE = true;
    G.CONTROLLER->interrupt.focus = true;
    G.E_MANAGER->add_event(ev_immediate([state] {
        auto open_box = [](TableRef def) {
            UIBox *b = new UIBox(def, vl::T({{"align", "tmi"}, {"offset", Value(vl::T({{"x", 0}, {"y", G.ROOM->T->y + 9}}))}, {"major", obj(G.hand.get())},
                                             {"bond", "Weak"}}));
            G.booster_pack = b;
            b->alignment.offset.y = -2.2f;
            G.ROOM_jiggle += 3;
        };
        auto sparkles = [](TableRef cfg) {
            Particles *p = pack_particles(cfg);
            p->fade_alpha = 1;
            p->fade(1, 0);
            G.booster_pack_sparkles = p;
        };
        auto then_focus = [](bool draw_hand) {
            G.E_MANAGER->add_event(ev_immediate([draw_hand] {
                if (draw_hand) draw_from_deck_to_hand();
                G.E_MANAGER->add_event(ev_after(0.5, [] {
                    G.CONTROLLER->recall_cardarea_focus("pack_cards");
                    return true;
                }));
                return true;
            }));
        };
        auto lighten_c = [](const char *c, float p) { return Value(lighten(G.colour(c), p)); };
        switch (state) {
        case TAROT_PACK:
            sparkles(vl::T({{"timer", 0.015}, {"scale", 0.2}, {"initialize", true}, {"lifespan", 1}, {"speed", 1.1}, {"padding", -1}, {"fill", true},
                            {"colours", Value(A({G.colour("WHITE"), lighten_c("PURPLE", 0.4f), lighten_c("PURPLE", 0.2f), lighten_c("GOLD", 0.2f)}))}}));
            open_box(create_UIBox_pack("k_arcana_pack", 1, 0));
            ease_background_colour_blind(TAROT_PACK);
            then_focus(true);
            break;
        case SPECTRAL_PACK:
            sparkles(vl::T({{"timer", 0.015}, {"scale", 0.1}, {"initialize", true}, {"lifespan", 3}, {"speed", 0.2}, {"padding", -1}, {"fill", true},
                            {"colours", Value(A({G.colour("WHITE"), lighten_c("GOLD", 0.2f)}))}}));
            open_box(create_UIBox_pack("k_spectral_pack", 1, 0));
            ease_background_colour_blind(SPECTRAL_PACK);
            then_focus(true);
            break;
        case STANDARD_PACK:
            sparkles(vl::T({{"timer", 0.015}, {"scale", 0.3}, {"initialize", true}, {"lifespan", 3}, {"speed", 0.2}, {"padding", -1}, {"fill", true},
                            {"colours", Value(A({G.colour("BLACK"), G.colour("RED")}))}}));
            open_box(create_UIBox_pack("k_standard_pack", 1.1, 0));
            ease_background_colour_blind(STANDARD_PACK);
            then_focus(false);
            break;
        case BUFFOON_PACK:
            open_box(create_UIBox_pack("k_buffoon_pack", 1.1, 0));
            ease_background_colour_blind(BUFFOON_PACK);
            then_focus(false);
            break;
        case PLANET_PACK: {
            ease_background_colour_blind(PLANET_PACK);
            G.booster_pack_stars = pack_particles(vl::T({{"timer", 0.07}, {"scale", 0.1}, {"initialize", true}, {"lifespan", 15}, {"speed", 0.1}, {"padding", -4},
                                                         {"fill", true}, {"colours", Value(A({G.colour("WHITE"), hex("a7d6e0"), hex("fddca0")}))}}));
            G.booster_pack_meteors = pack_particles(
                vl::T({{"timer", 2}, {"scale", 0.05}, {"lifespan", 1.5}, {"speed", 4}, {"fill", true}, {"colours", Value(A({G.colour("WHITE")}))}}));
            open_box(create_UIBox_pack("k_celestial_pack", 1.1, 0.5));
            G.E_MANAGER->add_event(ev_immediate([] {
                G.CONTROLLER->recall_cardarea_focus("pack_cards");
                return true;
            }));
            break;
        }
        }
        return true;
    }));
}

void register_booster_callbacks() {
    auto &F = G.FUNCS;
    F["skip_booster"] = [](UIElement *) {
        for (auto &j : std::vector<CardRef>(G.jokers->cards)) j->calculate_joker(vl::T({{"skipping_booster", true}}));
        end_consumeable();
    };
    F["can_skip_booster"] = [](UIElement *e) {
        if (G.pack_cards && !G.pack_cards->cards.empty() &&
            (G.STATE == PLANET_PACK || G.STATE == STANDARD_PACK || G.STATE == BUFFOON_PACK ||
             (G.hand && (!G.hand->cards.empty() || G.hand->config->get(K("card_limit")).num() <= 0)))) {
            e->config->set("colour", G.colour("GREY"));
            e->config->set("button", "skip_booster");
        } else {
            e->config->set("colour", fld(G.colour("UI"), "BACKGROUND_INACTIVE"));
            e->config->set("button", Value());
        }
    };
    F["select_button_check"] = [](UIElement *e) {
        Card *c = e->config->get(K("ref_table")).as<Card>();
        if (c && can_select_card(c)) {
            e->config->set("colour", G.colour("GREEN"));
            e->config->set("button", "use_card");
        } else {
            e->config->set("colour", fld(G.colour("UI"), "BACKGROUND_INACTIVE"));
            e->config->set("button", Value());
        }
    };
}

}  // namespace vl
