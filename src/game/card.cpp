// card.lua (structure, sprites, visuals, input). Rules (calculate_joker, use_consumeable...) are
// ported with the run logic.
#include "game/card.h"

#include <algorithm>
#include <cmath>

#include "core/lua_sort.h"
#include "engine/controller.h"
#include "engine/particles.h"
#include "engine/random.h"
#include "engine/ui.h"
#include "game/blind.h"
#include "game/card_factory.h"
#include "game/card_ui.h"
#include "game/functions.h"
#include "game/g.h"
#include "game/shop.h"
#include "game/state_events.h"
#include "game/ui_definitions.h"
#include "game/sound.h"

namespace vl {

namespace {
inline Value fld(const Value &t, const char *k) { return t.get(Value(intern(k))); }
SpritePos pos_of(const Value &p) {
    SpritePos s;
    s.x = (float)fld(p, "x").num();
    s.y = (float)fld(p, "y").num();
    if (fld(p, "v").is_num()) s.v = (int)fld(p, "v").num();
    return s;
}
Atlas *atlas(const std::string &name) {
    auto it = G.ASSET_ATLAS.find(name);
    return it == G.ASSET_ATLAS.end() ? nullptr : &it->second;
}
Value set_of(const Value &center) { return fld(center, "set"); }
}  // namespace

// ------------------------------------------------------------------ init
Card::Card(float x, float y, float w, float h, const Value &card, const Value &center, TableRef p) {
    params = p ? p : new_table();
    Moveable::init(x, y, w, h);
    CT = &VT;
    config = vl::T({{"card", card.truthy() ? card : Value(new_table())}, {"center", center}});
    has_tilt_var = true;
    tilt_var = {};
    ambient_tilt = 0.2f;

    states.collide.can = true;
    states.hover.can = true;
    states.drag.can = true;
    states.click.can = true;

    playing_card = params->get(K("playing_card"));
    sort_id = ++G.sort_id;

    back = params->get(K("viewed_back")).truthy() ? "viewed_back" : "selected_back";
    bypass_discovery_center = params->get(K("bypass_discovery_center")).truthy();
    bypass_discovery_ui = params->get(K("bypass_discovery_ui")).truthy();
    bypass_lock = params->get(K("bypass_lock")).truthy();
    no_ui = fld(config->get(K("card")), "no_ui").truthy();
    children.clear();
    base_cost = extra_cost = cost = sell_cost = 0;
    sell_cost_label = 0;
    children.set("shadow", new Moveable(0, 0, 0, 0));
    unique_val = 1 - ID / 1603301.0;
    edition = nullptr;
    zoom = true;
    set_ability(center, true);
    set_base(card, true);

    discard_pos.r = 3.6 * (lua_math.random() - 0.5);
    discard_pos.x = lua_math.random();
    discard_pos.y = lua_math.random();

    facing = "front";
    sprite_facing = "front";
    flipping.clear();
    area = nullptr;
    highlighted = false;
    set_var("click_timeout", 0.3);
    T->scale = 0.95f;
    debuff = false;
    rank = 0;
    added_to_deck = false;

    Sprite *front = sprite("front"), *bk = sprite("back"), *cen = sprite("center");
    if (front) front->VT.w = 0;
    bk->VT.w = 0;
    cen->VT.w = 0;
    for (Sprite *s : {front, bk, cen}) {
        if (!s) continue;
        s->parent = this;
        s->layered_parallax_nil = true;
    }
    set_cost();
    G.I.CARD.add(this);
}

void Card::update_alert() {
    Value set = ability->get(K("set"));
    if (!(set.equals("Joker") || set.equals("Voucher") || ability->get(K("consumeable")).truthy() || set.equals("Edition") ||
          set.equals("Booster")))
        return;
    Value center = config->get(K("center"));
    if (!(area && area->config->get(K("collection")).truthy() && center.truthy())) return;
    if (fld(center, "alerted").truthy() && children.get("alert")) {
        children.get("alert")->remove();
        children.set("alert", nullptr);
    } else if (!fld(center, "alerted").truthy() && !children.get("alert") && fld(center, "discovered").truthy()) {
        bool left = set.equals("Voucher") && (int)fld(center, "order").num() % 2 == 1;
        children.set("alert", new UIBox(create_UIBox_card_alert(),
                                        vl::T({{"align", left ? "tli" : "tri"},
                                               {"offset", Value(vl::T({{"x", left ? 0.1 : -0.1}, {"y", 0.1}}))},
                                               {"parent", Value((Object *)this)}})));
    }
}

// ------------------------------------------------------------------ set_base / sprites / ability
void Card::set_base(const Value &card_in, bool initial) {
    Value card = card_in.truthy() ? card_in : Value(new_table());
    config->set("card", card);
    G.P_CARDS->each([&](const Value &k, const Value &v) {
        if (card == v) config->set("card_key", k);
    });
    if (Table *ct = card.table(); ct && (ct->len() > 0 || [&] {
            bool any = false;
            ct->each([&](const Value &, const Value &) { any = true; });
            return any;
        }()))
        set_sprites(Value(), card);

    Value snom_orig;
    if (base && base->get(K("suit_nominal_original")).truthy()) snom_orig = base->get(K("suit_nominal_original"));
    Value cc = config->get(K("card"));
    base = vl::T({{"name", fld(cc, "name")},
                  {"suit", fld(cc, "suit")},
                  {"value", fld(cc, "value")},
                  {"nominal", 0},
                  {"suit_nominal", 0},
                  {"face_nominal", 0},
                  {"colour", G.C->get(K("SUITS")).get(fld(cc, "suit"))},
                  {"times_played", 0}});
    struct R { const char *v; double nominal, face; int id; };
    static const R ranks[] = {{"2", 2, 0, 2},       {"3", 3, 0, 3},         {"4", 4, 0, 4},      {"5", 5, 0, 5},
                              {"6", 6, 0, 6},       {"7", 7, 0, 7},         {"8", 8, 0, 8},      {"9", 9, 0, 9},
                              {"10", 10, 0, 10},    {"Jack", 10, 0.1, 11},  {"Queen", 10, 0.2, 12},
                              {"King", 10, 0.3, 13}, {"Ace", 11, 0.4, 14}};
    Value value = base->get(K("value"));
    for (const R &r : ranks) {
        if (value.equals(r.v)) {
            base->set("nominal", r.nominal);
            if (r.face != 0) base->set("face_nominal", r.face);
            base->set("id", r.id);
            break;
        }
    }
    if (initial) base->set("original_value", value);
    Value suit = base->get(K("suit"));
    struct S { const char *s; double nom, orig; };
    static const S suits[] = {{"Diamonds", 0.01, 0.001}, {"Clubs", 0.02, 0.002}, {"Hearts", 0.03, 0.003}, {"Spades", 0.04, 0.004}};
    for (const S &s : suits) {
        if (suit.equals(s.s)) {
            base->set("suit_nominal", s.nom);
            base->set("suit_nominal_original", snom_orig.truthy() ? snom_orig : Value(s.orig));
            break;
        }
    }
    if (!initial && G.GAME)
        if (Blind *b = G.GAME->get(K("blind")).as<Blind>()) b->debuff_card(this);
    if (playing_card.truthy() && !initial) check_for_unlock(vl::T({{"type", "modify_deck"}}));
}

void Card::set_sprites(const Value &center, const Value &front) {
    if (front.truthy()) {
        auto [a, pos] = get_front_spriteinfo(front);
        if (Sprite *f = sprite("front")) {
            f->atlas = a;
            f->set_sprite_pos(pos_of(pos));
        } else {
            Sprite *s = new Sprite(T->x, T->y, T->w, T->h, a, pos_of(pos));
            children.set("front", s);
            s->states.collide.can = false;
            RoleType glued = RoleType::Glued;
            RoleArgs ra;
            ra.major = this;
            ra.role_type = &glued;
            ra.draw_major = this;
            s->set_role(ra);
        }
    }
    if (center.truthy()) {
        Value set = set_of(center);
        Value self_center = config->get(K("center"));
        if (set.truthy()) {
            bool joker_like = set.equals("Joker") || fld(center, "consumeable").truthy() || set.equals("Voucher");
            if (Sprite *c = sprite("center")) {
                Value at = fld(center, "atlas");
                std::string name = at.truthy() ? std::string(at.str()) : joker_like ? std::string(set.str()) : "centers";
                c->atlas = atlas(name);
                c->set_sprite_pos(pos_of(fld(center, "pos")));
            } else {
                Sprite *s;
                bool bdc = params->get(K("bypass_discovery_center")).truthy();
                auto mk = [&](const std::string &atl, const Value &pos) { return new Sprite(T->x, T->y, T->w, T->h, atlas(atl), pos_of(pos)); };
                if (set.equals("Joker") && !fld(center, "unlocked").truthy() && !bdc) {
                    s = mk("Joker", fld(G.j_locked, "pos"));
                } else if (set_of(self_center).equals("Voucher") && !fld(self_center, "unlocked").truthy() && !bdc) {
                    s = mk("Voucher", fld(G.v_locked, "pos"));
                } else if (fld(self_center, "consumeable").truthy() && fld(self_center, "demo").truthy()) {
                    s = mk("Tarot", fld(G.c_locked, "pos"));
                } else if (!bdc && (set.equals("Edition") || set.equals("Joker") || fld(center, "consumeable").truthy() ||
                                    set.equals("Voucher") || set.equals("Booster")) &&
                           !fld(center, "discovered").truthy()) {
                    Value at = fld(center, "atlas");
                    std::string name = at.truthy() ? std::string(at.str()) : std::string(set.str());
                    Value pos = set.equals("Joker")      ? fld(G.j_undiscovered, "pos")
                                : set.equals("Edition")  ? fld(G.j_undiscovered, "pos")
                                : set.equals("Tarot")    ? fld(G.t_undiscovered, "pos")
                                : set.equals("Planet")   ? fld(G.p_undiscovered, "pos")
                                : set.equals("Spectral") ? fld(G.s_undiscovered, "pos")
                                : set.equals("Voucher")  ? fld(G.v_undiscovered, "pos")
                                : set.equals("Booster")  ? fld(G.booster_undiscovered, "pos")
                                                         : Value();
                    s = mk(name, pos);
                } else if (joker_like) {
                    s = mk(std::string(set.str()), fld(self_center, "pos"));
                } else {
                    Value at = fld(center, "atlas");
                    s = mk(at.truthy() ? std::string(at.str()) : "centers", fld(center, "pos"));
                }
                children.set("center", s);
                s->states.collide.can = false;
                RoleType glued = RoleType::Glued;
                RoleArgs ra;
                ra.major = this;
                ra.role_type = &glued;
                ra.draw_major = this;
                s->set_role(ra);
            }
            bool shown = fld(center, "discovered").truthy() || bypass_discovery_center;
            Value nm = fld(center, "name");
            Sprite *c = sprite("center");
            if (nm.equals("Half Joker") && shown) c->scale.y = c->scale.y / 1.7f;
            if (nm.equals("Photograph") && shown) c->scale.y = c->scale.y / 1.2f;
            if (nm.equals("Square Joker") && shown) c->scale.y = c->scale.x;
        }
        if (fld(center, "soul_pos").truthy()) {
            Sprite *s = new Sprite(T->x, T->y, T->w, T->h, atlas("Joker"), pos_of(fld(self_center, "soul_pos")));
            children.set("floating_sprite", s);
            s->role.draw_major = this;
            s->states.hover.can = false;
            s->states.click.can = false;
        }
        if (!children.get("back")) {
            Value pos;
            if (params->get(K("bypass_back")).truthy()) pos = params->get(K("bypass_back"));
            else if (playing_card.truthy()) {
                Back *b = G.GAME->get(Value(intern(back))).as<Back>();
                pos = b ? Value(b->pos) : Value();
            } else {
                pos = fld(G.P_CENTERS->get(K("b_red")), "pos");
            }
            Sprite *s = new Sprite(T->x, T->y, T->w, T->h, atlas("centers"), pos_of(pos));
            children.set("back", s);
            s->states.collide.can = false;
            RoleType glued = RoleType::Glued;
            RoleArgs ra;
            ra.major = this;
            ra.role_type = &glued;
            ra.draw_major = this;
            s->set_role(ra);
        }
    }
}

void Card::set_ability(const Value &center, bool initial, bool delay_sprites) {
    float W = T->w, H = T->h;
    Value old_center = config->get(K("center"));
    config->set("center", center);
    sticker_run = Value();
    G.P_CENTERS->each([&](const Value &k, const Value &v) {
        if (center == v) config->set("center_key", k);
    });
    if (params->get(K("discover")).truthy() && !fld(center, "discovered").truthy()) {
        unlock_card(center);
        discover_card(center);
    }
    bool shown = fld(center, "discovered").truthy() || bypass_discovery_center;
    Value nm = fld(center, "name");
    if (nm.equals("Half Joker") && shown) T->h = H = H / 1.7f;
    if (nm.equals("Photograph") && shown) T->h = H = H / 1.2f;
    if (nm.equals("Square Joker") && shown) T->h = H = W;
    if (nm.equals("Wee Joker") && shown) {
        T->h = H = H * 0.7f;
        T->w = W = W * 0.7f;
    }
    if (delay_sprites) {
        Ref<Card> self(this);
        G.E_MANAGER->add_event(ev_immediate([self, center] {
            if (!self->REMOVED) self->set_sprites(center, Value());
            return true;
        }));
    } else {
        set_sprites(center, Value());
    }
    Value ccfg = fld(center, "config");
    if (ability && old_center.truthy() && fld(fld(old_center, "config"), "bonus").truthy())
        ability->set("bonus", ability->get(K("bonus")).num() - fld(fld(old_center, "config"), "bonus").num());

    auto cn = [&](const char *k, double d) {
        Value v = fld(ccfg, k);
        return v.truthy() ? v : Value(d);
    };
    Value extra = fld(ccfg, "extra");
    if (Table *et = extra.table()) extra = Value(et->deep_copy());
    TableRef old = ability;
    ability = vl::T({{"name", nm},
                     {"effect", fld(center, "effect")},
                     {"set", set_of(center)},
                     {"mult", cn("mult", 0)},
                     {"h_mult", cn("h_mult", 0)},
                     {"h_x_mult", cn("h_x_mult", 0)},
                     {"h_dollars", cn("h_dollars", 0)},
                     {"p_dollars", cn("p_dollars", 0)},
                     {"t_mult", cn("t_mult", 0)},
                     {"t_chips", cn("t_chips", 0)},
                     {"x_mult", cn("Xmult", 1)},
                     {"h_size", cn("h_size", 0)},
                     {"d_size", cn("d_size", 0)},
                     {"extra", extra},
                     {"extra_value", 0},
                     {"type", fld(ccfg, "type").truthy() ? fld(ccfg, "type") : Value("")},
                     {"order", fld(center, "order")},
                     {"forced_selection", old ? old->get(K("forced_selection")) : Value()},
                     {"perma_bonus", old && old->get(K("perma_bonus")).truthy() ? old->get(K("perma_bonus")) : Value(0)}});
    ability->set("bonus", ability->get(K("bonus")).num() + fld(ccfg, "bonus").num());
    if (fld(center, "consumeable").truthy()) ability->set("consumeable", ccfg);

    Value an = ability->get(K("name"));
    if (an.equals("Gold Card") && seal.equals("Gold") && playing_card.truthy()) check_for_unlock(vl::T({{"type", "double_gold"}}));
    if (an.equals("Invisible Joker")) ability->set("invis_rounds", 0);
    if (an.equals("To Do List")) {
        TableRef hands = new_table();
        Value gh = G.GAME ? G.GAME->get(K("hands")) : Value();
        if (Table *ht = gh.table())
            ht->each([&](const Value &k, const Value &v) {
                if (fld(v, "visible").truthy()) hands->push(k);
            });
        Value old_hand = ability->get(K("to_do_poker_hand"));
        ability->set("to_do_poker_hand", Value());
        bool title = area && fld(Value(area->config), "type").equals("title");
        while (!ability->get(K("to_do_poker_hand")).truthy() && hands->len() > 0) {
            double s = pseudoseed(title ? "false_to_do" : "to_do");
            ability->set("to_do_poker_hand", pseudorandom_element(Value(hands), &s));
            if (ability->get(K("to_do_poker_hand")) == old_hand) ability->set("to_do_poker_hand", Value());
        }
    }
    if (an.equals("Caino")) ability->set("caino_xmult", 1);
    if (an.equals("Yorick")) ability->set("yorick_discards", fld(ability->get(K("extra")), "discards"));
    if (an.equals("Loyalty Card")) {
        ability->set("burnt_hand", 0);
        ability->set("loyalty_remaining", fld(ability->get(K("extra")), "every"));
    }
    base_cost = fld(center, "cost").truthy() ? fld(center, "cost").num() : 1;
    ability->set("hands_played_at_create", G.GAME && G.GAME->get(K("hands_played")).truthy() ? G.GAME->get(K("hands_played")) : Value(0));

    Value set = ability->get(K("set"));
    label = fld(center, "label").truthy()                         ? fld(center, "label")
            : fld(config->get(K("card")), "label").truthy()        ? fld(config->get(K("card")), "label")
                                                                   : set;
    if (set.equals("Joker") || set.equals("Edition") || ability->get(K("consumeable")).truthy() || set.equals("Voucher"))
        label = an;
    if (set.equals("Booster")) {
        label = an;
        mouse_damping = 1.5f;
        has_mouse_damping = true;
    }
    if (!G.OVERLAY_MENU && G.GAME) {
        Value used = G.GAME->get(K("used_jokers"));
        G.P_CENTERS->each([&](const Value &k, const Value &v) {
            if (fld(v, "name") == an && used.is_table()) used.table()->set(k, true);
        });
    }
    if ((G.jokers && area == G.jokers.get()) || (G.consumeables && area == G.consumeables.get()))
        check_for_unlock(vl::T({{"type", "modify_jokers"}}));
    if (!initial && G.GAME)
        if (Blind *b = G.GAME->get(K("blind")).as<Blind>()) b->debuff_card(this);
    if (playing_card.truthy() && !initial) check_for_unlock(vl::T({{"type", "modify_deck"}}));
}

void Card::set_cost() {
    Value game = Value(G.GAME);
    extra_cost = 0 + fld(game, "inflation").num();
    if (edition) {
        extra_cost += (edition->get(K("holo")).truthy() ? 3 : 0) + (edition->get(K("foil")).truthy() ? 2 : 0) +
                      (edition->get(K("polychrome")).truthy() ? 5 : 0) + (edition->get(K("negative")).truthy() ? 5 : 0);
    }
    cost = std::max(1.0, std::floor((base_cost + extra_cost + 0.5) * (100 - fld(game, "discount_percent").num()) / 100));
    Value set = ability->get(K("set"));
    if (set.equals("Booster") && fld(fld(game, "modifiers"), "booster_ante_scaling").truthy())
        cost = cost + fld(fld(game, "round_resets"), "ante").num() - 1;
    if (set.equals("Booster") && !G.SETTINGS->get(K("tutorial_complete")).truthy() &&
        G.SETTINGS->get(K("tutorial_progress")).truthy() &&
        !fld(fld(G.SETTINGS->get(K("tutorial_progress")), "completed_parts"), "shop_1").truthy())
        cost = cost + 3;
    if ((set.equals("Planet") || (set.equals("Booster") && ability->get(K("name")).str().find("Celestial") != std::string_view::npos)) &&
        !find_joker("Astronomer").empty())
        cost = 0;
    if (ability->get(K("rental")).truthy()) cost = 1;
    sell_cost = std::max(1.0, std::floor(cost / 2)) + ability->get(K("extra_value")).num();
    if (area && ability->get(K("couponed")).truthy() &&
        ((G.shop_jokers && area == G.shop_jokers.get()) || (G.shop_booster && area == G.shop_booster.get())))
        cost = 0;
    sell_cost_label = facing == "back" ? Value("?") : Value(sell_cost);
}

void Card::set_edition(const Value &ed, bool immediate, bool silent) {
    edition = nullptr;
    if (!ed.truthy()) return;
    if (fld(ed, "holo").truthy()) {
        edition = vl::T({{"mult", fld(fld(G.P_CENTERS->get(K("e_holo")), "config"), "extra")}, {"holo", true}, {"type", "holo"}});
    } else if (fld(ed, "foil").truthy()) {
        edition = vl::T({{"chips", fld(fld(G.P_CENTERS->get(K("e_foil")), "config"), "extra")}, {"foil", true}, {"type", "foil"}});
    } else if (fld(ed, "polychrome").truthy()) {
        edition = vl::T({{"x_mult", fld(fld(G.P_CENTERS->get(K("e_polychrome")), "config"), "extra")}, {"polychrome", true}, {"type", "polychrome"}});
    } else if (fld(ed, "negative").truthy()) {
        edition = new_table();
        if (added_to_deck) {
            CardArea *a = ability->get(K("consumeable")).truthy() ? G.consumeables.get() : G.jokers.get();
            if (a) a->config->set("card_limit", a->config->get(K("card_limit")).num() + 1);
        }
        edition->set("negative", true);
        edition->set("type", "negative");
    }
    if (area && G.jokers && area == G.jokers.get()) {
        std::string key = edition ? "e_" + edition->get(K("type")).to_string() : "e_base";
        Value c = G.P_CENTERS->get(Value(intern(key)));
        if (!fld(c, "discovered").truthy()) discover_card(c);
    }
    if (edition && !silent) {
        G.CONTROLLER->locks["edition"] = true;
        Ref<Card> self(this);
        EventConfig e = ev_after(!immediate ? 0.2 : 0, [self] {
            float r = 0.5f;
            self->juice_up_card(1, &r);
            TableRef ed2 = self->edition;
            if (ed2) {
                if (ed2->get(K("foil")).truthy()) play_sound("foil1", 1.2f, 0.4f);
                if (ed2->get(K("holo")).truthy()) play_sound("holo1", 1.2f * 1.58f, 0.4f);
                if (ed2->get(K("polychrome")).truthy()) play_sound("polychrome1", 1.2f, 0.7f);
                if (ed2->get(K("negative")).truthy()) play_sound("negative", 1.5f, 0.4f);
            }
            return true;
        });
        e.blockable = !immediate;
        G.E_MANAGER->add_event(std::move(e));
        G.E_MANAGER->add_event(ev_after(0.1, [] {
            G.CONTROLLER->locks["edition"] = false;
            return true;
        }));
    }
    if (G.jokers && area == G.jokers.get()) check_for_unlock(vl::T({{"type", "modify_jokers"}}));
    set_cost();
}

void Card::set_seal(const Value &s, bool silent, bool immediate) {
    seal = Value();
    if (s.truthy()) {
        seal = s;
        if (!silent) {
            G.CONTROLLER->locks["seal"] = true;
            if (immediate) {
                float r = 0.3f;
                juice_up_card(0.3f, &r);
                play_sound("gold_seal", 1.2f, 0.4f);
                G.CONTROLLER->locks["seal"] = false;
            } else {
                Ref<Card> self(this);
                G.E_MANAGER->add_event(ev_after(0.3, [self] {
                    float r = 0.3f;
                    self->juice_up_card(0.3f, &r);
                    play_sound("gold_seal", 1.2f, 0.4f);
                    return true;
                }));
                G.E_MANAGER->add_event(ev_after(0.15, [] {
                    G.CONTROLLER->locks["seal"] = false;
                    return true;
                }));
            }
        }
    }
    if (ability->get(K("name")).equals("Gold Card") && seal.equals("Gold") && playing_card.truthy())
        check_for_unlock(vl::T({{"type", "double_gold"}}));
    set_cost();
}

Value Card::get_seal(bool bypass_debuff) const {
    if (debuff && !bypass_debuff) return Value();
    return seal;
}

void Card::change_suit(const std::string &new_suit) {
    const Value value = base->get(K("value"));
    static const std::pair<const char *, const char *> suit_prefix[] = {{"Spades", "S_"}, {"Hearts", "H_"}, {"Diamonds", "D_"}, {"Clubs", "C_"}};
    static const std::pair<const char *, const char *> rank_suffix[] = {{"2", "2"},  {"3", "3"},  {"4", "4"},  {"5", "5"},     {"6", "6"},
                                                                        {"7", "7"},  {"8", "8"},  {"9", "9"},  {"10", "T"},    {"Jack", "J"},
                                                                        {"Queen", "Q"}, {"King", "K"}, {"Ace", "A"}};
    std::string prefix, suffix;
    for (auto &p : suit_prefix)
        if (new_suit == p.first) prefix = p.second;
    for (auto &p : rank_suffix)
        if (value.equals(p.first)) suffix = p.second;
    set_base(G.P_CARDS->get(Value(prefix + suffix)));
    if (Blind *b = G.GAME->get(K("blind")).as<Blind>()) b->debuff_card(this);
}

void Card::set_debuff(bool should_debuff) {
    if (ability->get(K("perishable")).truthy() && ability->get(K("perish_tally")).num() <= 0) {
        if (!debuff) {
            debuff = true;
            if (G.jokers && area == G.jokers.get()) remove_from_deck(true);
        }
        return;
    }
    if (should_debuff != debuff) {
        if (G.jokers && area == G.jokers.get()) {
            if (should_debuff) remove_from_deck(true);
            else add_to_deck(true);
        }
        debuff = should_debuff;
    }
}

void Card::remove_UI() {
    config->set("h_popup", Value());
    config->set("h_popup_config", Value());
    no_ui = true;
}

double Card::get_nominal(const char *mod) const {
    double mult = 1;
    if (mod && std::string_view(mod) == "suit") mult = 1000;
    if (ability->get(K("effect")).equals("Stone Card")) mult = -1000;
    return base->get(K("nominal")).num() + base->get(K("suit_nominal")).num() * mult +
           base->get(K("suit_nominal_original")).num() * 0.0001 * mult + base->get(K("face_nominal")).num() + 0.000001 * unique_val;
}

int Card::get_id() const {
    if (ability->get(K("effect")).equals("Stone Card") && !var("vampired").truthy()) return -(int)lua_math.random(100, 1000000);
    return (int)base->get(K("id")).num();
}

bool Card::is_face(bool from_boss) const {
    if (debuff && !from_boss) return false;
    int id = get_id();
    return id == 11 || id == 12 || id == 13 || !find_joker("Pareidolia").empty();
}

bool Card::is_suit(const Value &suit, bool bypass_debuff, bool flush_calc) const {
    const Value bs = base->get(K("suit"));
    auto red = [](const Value &s) { return s.equals("Hearts") || s.equals("Diamonds"); };
    if (flush_calc) {
        if (ability->get(K("effect")).equals("Stone Card")) return false;
        if (ability->get(K("name")).equals("Wild Card") && !debuff) return true;
        if (!find_joker("Smeared Joker").empty() && red(bs) == red(suit)) return true;
        return bs == suit;
    }
    if (debuff && !bypass_debuff) return false;
    if (ability->get(K("effect")).equals("Stone Card")) return false;
    if (ability->get(K("name")).equals("Wild Card")) return true;
    if (!find_joker("Smeared Joker").empty() && red(bs) == red(suit)) return true;
    return bs == suit;
}

// ------------------------------------------------------------------ dissolve / materialize
void Card::start_dissolve(TableRef colours, bool silent, float time_fac, bool no_juice) {
    float dissolve_time = 0.7f * time_fac;
    dissolve = 0;
    dissolve_colours = colours ? colours
                               : A({G.colour("BLACK"), G.colour("ORANGE"), G.colour("RED"), G.colour("GOLD"), G.colour("JOKER_GREY")});
    if (!no_juice) juice_up_card();
    Ref<Particles> parts = new Particles(0, 0, 0, 0,
                                         vl::T({{"timer_type", "TOTAL"},
                                                {"timer", 0.01 * dissolve_time},
                                                {"scale", 0.1},
                                                {"speed", 2},
                                                {"lifespan", 0.7 * dissolve_time},
                                                {"attach", Value((Object *)this)},
                                                {"colours", Value(dissolve_colours)},
                                                {"fill", true}}));
    EventConfig e1 = ev_after(0.7 * dissolve_time, [parts, dissolve_time] {
        parts->fade(0.3 * dissolve_time);
        return true;
    });
    e1.blockable = false;
    G.E_MANAGER->add_event(std::move(e1));
    if (!silent) {
        EventConfig e = ev_immediate([] {
            play_sound("whoosh2", (float)(lua_math.random() * 0.2 + 0.9), 0.5f);
            std::string c = "crumple" + std::to_string((int)lua_math.random(1, 5));
            play_sound(c.c_str(), (float)(lua_math.random() * 0.2 + 0.9), 0.5f);
            return true;
        });
        e.blockable = false;
        G.E_MANAGER->add_event(std::move(e));
    }
    Ref<Card> self(this);
    EventConfig ease;
    ease.trigger = Trigger::Ease;
    ease.blockable = false;
    ease.ref = ValueRef::of(&dissolve, this);
    ease.ease_to = 1;
    ease.delay = 1 * dissolve_time;
    G.E_MANAGER->add_event(std::move(ease));
    EventConfig rm = ev_after(1.05 * dissolve_time, [self] {
        self->remove();
        return true;
    });
    rm.blockable = false;
    G.E_MANAGER->add_event(std::move(rm));
    EventConfig tail = ev_after(1.051 * dissolve_time, nullptr);
    tail.blockable = false;
    G.E_MANAGER->add_event(std::move(tail));
}

void Card::shatter() {
    const float dissolve_time = 0.7f;
    shattered = true;
    dissolve = 0;
    dissolve_colours = A({Value(A({1, 1, 1, 0.8}))});
    juice_up_card();
    Ref<Particles> parts = new Particles(0, 0, 0, 0,
                                         vl::T({{"timer_type", "TOTAL"},
                                                {"timer", 0.007 * dissolve_time},
                                                {"scale", 0.3},
                                                {"speed", 4},
                                                {"lifespan", 0.5 * dissolve_time},
                                                {"attach", Value((Object *)this)},
                                                {"colours", Value(dissolve_colours)},
                                                {"fill", true}}));
    EventConfig e1 = ev_after(0.5 * dissolve_time, [parts, dissolve_time] {
        parts->fade(0.15 * dissolve_time);
        return true;
    });
    e1.blockable = false;
    G.E_MANAGER->add_event(std::move(e1));
    EventConfig e = ev_immediate([] {
        std::string g = "glass" + std::to_string((int)lua_math.random(1, 6));
        play_sound(g.c_str(), (float)(lua_math.random() * 0.2 + 0.9), 0.5f);
        play_sound("generic1", (float)(lua_math.random() * 0.2 + 0.9), 0.5f);
        return true;
    });
    e.blockable = false;
    G.E_MANAGER->add_event(std::move(e));
    Ref<Card> self(this);
    EventConfig ease;
    ease.trigger = Trigger::Ease;
    ease.blockable = false;
    ease.ref = ValueRef::of(&dissolve, this);
    ease.ease_to = 1;
    ease.delay = 0.5 * dissolve_time;
    G.E_MANAGER->add_event(std::move(ease));
    EventConfig rm = ev_after(0.55 * dissolve_time, [self] {
        self->remove();
        return true;
    });
    rm.blockable = false;
    G.E_MANAGER->add_event(std::move(rm));
    EventConfig tail = ev_after(0.51 * dissolve_time, nullptr);
    tail.blockable = false;
    G.E_MANAGER->add_event(std::move(tail));
}

double Card::get_chip_bonus() const {
    if (debuff) return 0;
    const double perma = ability->get(K("perma_bonus")).truthy() ? ability->get(K("perma_bonus")).num() : 0;
    if (ability->get(K("effect")).equals("Stone Card")) return ability->get(K("bonus")).num() + perma;
    return base->get(K("nominal")).num() + ability->get(K("bonus")).num() + perma;
}

double Card::get_chip_mult() {
    if (debuff) return 0;
    if (ability->get(K("set")).equals("Joker")) return 0;
    if (ability->get(K("effect")).equals("Lucky Card")) {
        if (pseudorandom("lucky_mult") < fld(G.GAME->get(K("probabilities")), "normal").num() / 5) {
            lucky_trigger = true;
            return ability->get(K("mult")).num();
        }
        return 0;
    }
    return ability->get(K("mult")).num();
}

double Card::get_chip_x_mult() const {
    if (debuff) return 0;
    if (ability->get(K("set")).equals("Joker")) return 0;
    if (ability->get(K("x_mult")).num() <= 1) return 0;
    return ability->get(K("x_mult")).num();
}

double Card::get_chip_h_mult() const { return debuff ? 0 : ability->get(K("h_mult")).num(); }
double Card::get_chip_h_x_mult() const { return debuff ? 0 : ability->get(K("h_x_mult")).num(); }

Value Card::get_edition() {
    if (debuff || !edition) return Value();
    TableRef ret = vl::T({{"card", Value((Object *)this)}});
    if (edition->get(K("x_mult")).truthy()) ret->set("x_mult_mod", edition->get(K("x_mult")));
    if (edition->get(K("mult")).truthy()) ret->set("mult_mod", edition->get(K("mult")));
    if (edition->get(K("chips")).truthy()) ret->set("chip_mod", edition->get(K("chips")));
    return Value(ret);
}

TableRef Card::get_end_of_round_effect() {
    TableRef ret = new_table();
    if (debuff) return ret;
    if (ability->get(K("h_dollars")).num() > 0) {
        ret->set("h_dollars", ability->get(K("h_dollars")));
        ret->set("card", Value((Object *)this));
    }
    Value game = Value(G.GAME);
    if (seal.equals("Blue") &&
        G.consumeables->cards.size() + fld(game, "consumeable_buffer").num() < G.consumeables->config->get(K("card_limit")).num()) {
        G.GAME->set("consumeable_buffer", fld(game, "consumeable_buffer").num() + 1);
        EventConfig e = ev_immediate([] {
            Value last = G.GAME->get(K("last_hand_played"));
            if (last.truthy()) {
                Value planet(0);
                G.P_CENTER_POOLS->get(K("Planet")).table()->each([&](const Value &, const Value &v) {
                    if (fld(fld(v, "config"), "hand_type") == last) planet = fld(v, "key");
                });
                Card *card = create_card("Planet", G.consumeables.get(), false, Value(), false, false, planet.is_str() ? std::string(planet.str()) : "",
                                         "blusl");
                card->add_to_deck();
                G.consumeables->emplace(card);
                G.GAME->set("consumeable_buffer", 0);
            }
            return true;
        });
        e.trigger = Trigger::Before;
        e.delay = 0;
        G.E_MANAGER->add_event(e);
        card_eval_status_text(this, "extra", 0, -1, nullptr,
                              vl::T({{"message", localize("k_plus_planet")}, {"colour", fld(G.colour("SECONDARY_SET"), "Planet")}}));
        ret->set("effect", true);
    }
    return ret;
}

double Card::get_p_dollars() {
    if (debuff) return 0;
    double ret = 0;
    if (seal.equals("Gold")) ret += 3;
    if (ability->get(K("p_dollars")).num() > 0) {
        if (ability->get(K("effect")).equals("Lucky Card")) {
            if (pseudorandom("lucky_money") < fld(G.GAME->get(K("probabilities")), "normal").num() / 15) {
                lucky_trigger = true;
                ret += ability->get(K("p_dollars")).num();
            }
        } else {
            ret += ability->get(K("p_dollars")).num();
        }
    }
    if (ret > 0) {
        Value db = G.GAME->get(K("dollar_buffer"));
        G.GAME->set("dollar_buffer", (db.truthy() ? db.num() : 0) + ret);
        G.E_MANAGER->add_event(ev_immediate([] {
            G.GAME->set("dollar_buffer", 0);
            return true;
        }));
    }
    return ret;
}

Value Card::calculate_dollar_bonus() const {
    if (debuff) return Value();
    if (!ability->get(K("set")).equals("Joker")) return Value();
    const Value nm = ability->get(K("name"));
    if (nm.equals("Golden Joker")) return ability->get(K("extra"));
    if (nm.equals("Cloud 9") && ability->get(K("nine_tally")).truthy() && ability->get(K("nine_tally")).num() > 0)
        return Value(ability->get(K("extra")).num() * ability->get(K("nine_tally")).num());
    if (nm.equals("Rocket")) return fld(ability->get(K("extra")), "dollars");
    if (nm.equals("Satellite")) {
        int planets_used = 0;
        if (Table *u = G.GAME->get(K("consumeable_usage")).table())
            u->each([&](const Value &, const Value &v) {
                if (fld(v, "set").equals("Planet")) planets_used++;
            });
        if (planets_used == 0) return Value();
        return Value(ability->get(K("extra")).num() * planets_used);
    }
    Value cr = G.GAME->get(K("current_round"));
    if (nm.equals("Delayed Gratification") && fld(cr, "discards_used").num() == 0 && fld(cr, "discards_left").num() > 0)
        return Value(fld(cr, "discards_left").num() * ability->get(K("extra")).num());
    return Value();
}

Value Card::calculate_seal(const TableRef &context) {
    if (debuff) return Value();
    if (context->get(K("repetition")).truthy() && seal.equals("Red"))
        return Value(vl::T({{"message", localize("k_again_ex")}, {"repetitions", 1}, {"card", Value((Object *)this)}}));
    if (context->get(K("discard")).truthy()) {
        Value game = Value(G.GAME);
        if (seal.equals("Purple") &&
            G.consumeables->cards.size() + fld(game, "consumeable_buffer").num() < G.consumeables->config->get(K("card_limit")).num()) {
            G.GAME->set("consumeable_buffer", fld(game, "consumeable_buffer").num() + 1);
            EventConfig e = ev_immediate([] {
                Card *card = create_card("Tarot", G.consumeables.get(), false, Value(), false, false, "", "8ba");
                card->add_to_deck();
                G.consumeables->emplace(card);
                G.GAME->set("consumeable_buffer", 0);
                return true;
            });
            e.trigger = Trigger::Before;
            e.delay = 0;
            G.E_MANAGER->add_event(e);
            card_eval_status_text(this, "extra", 0, -1, nullptr, vl::T({{"message", localize("k_plus_tarot")}, {"colour", G.colour("PURPLE")}}));
        }
    }
    return Value();
}

void Card::calculate_rental() {
    if (ability->get(K("rental")).truthy()) {
        ease_dollars(-G.GAME->get(K("rental_rate")).num());
        card_eval_status_text(this, "dollars", -G.GAME->get(K("rental_rate")).num());
    }
}

void Card::calculate_perishable() {
    if (ability->get(K("perishable")).truthy() && ability->get(K("perish_tally")).num() > 0) {
        if (ability->get(K("perish_tally")).num() == 1) {
            ability->set("perish_tally", 0);
            card_eval_status_text(this, "extra", 0, -1, nullptr, vl::T({{"message", localize("k_disabled_ex")}, {"colour", G.colour("FILTER")}, {"delay", 0.45}}));
            set_debuff(false);
        } else {
            ability->set("perish_tally", ability->get(K("perish_tally")).num() - 1);
            card_eval_status_text(this, "extra", 0, -1, nullptr,
                                  vl::T({{"message", localize(vl::T({{"type", "variable"}, {"key", "a_remaining"}, {"vars", Value(A({ability->get(K("perish_tally"))}))}}))},
                                         {"colour", G.colour("FILTER")},
                                         {"delay", 0.45}}));
        }
    }
}


void Card::start_materialize(TableRef colours, bool silent, float time_fac) {
    float dissolve_time = 0.6f * time_fac;
    states.visible = true;
    states.hover.can = false;
    dissolve = 1;
    if (colours) {
        dissolve_colours = colours;
    } else {
        Value set = ability->get(K("set"));
        Value C = Value(G.C);
        if (set.equals("Joker")) dissolve_colours = A({fld(C, "RARITY")[(int)fld(config->get(K("center")), "rarity").num()]});
        else if (set.equals("Planet")) dissolve_colours = A({fld(fld(C, "SECONDARY_SET"), "Planet")});
        else if (set.equals("Tarot")) dissolve_colours = A({fld(fld(C, "SECONDARY_SET"), "Tarot")});
        else if (set.equals("Spectral")) dissolve_colours = A({fld(fld(C, "SECONDARY_SET"), "Spectral")});
        else if (set.equals("Booster")) dissolve_colours = A({fld(C, "BOOSTER")});
        else if (set.equals("Voucher")) dissolve_colours = A({fld(fld(C, "SECONDARY_SET"), "Voucher"), fld(C, "CLEAR")});
        else dissolve_colours = A({fld(C, "GREEN")});
    }
    juice_up_card();
    Particles *p = new Particles(0, 0, 0, 0,
                                 vl::T({{"timer_type", "TOTAL"},
                                        {"timer", 0.025 * dissolve_time},
                                        {"scale", 0.25},
                                        {"speed", 3},
                                        {"lifespan", 0.7 * dissolve_time},
                                        {"attach", Value((Object *)this)},
                                        {"colours", Value(dissolve_colours)},
                                        {"fill", true}}));
    children.set("particles", p);
    if (!silent) {
        if (!G.has_last_materialized || G.last_materialized + 0.01 < G.TIMERS.REAL || G.last_materialized > G.TIMERS.REAL) {
            G.last_materialized = G.TIMERS.REAL;
            G.has_last_materialized = true;
            EventConfig e = ev_immediate([] {
                play_sound("whoosh1", (float)(lua_math.random() * 0.1 + 0.6), 0.3f);
                std::string c = "crumple" + std::to_string((int)lua_math.random(1, 5));
                play_sound(c.c_str(), (float)(lua_math.random() * 0.2 + 1.2), 0.8f);
                return true;
            });
            e.blockable = false;
            G.E_MANAGER->add_event(std::move(e));
        }
    }
    Ref<Card> self(this);
    EventConfig e1 = ev_after(0.5 * dissolve_time, [self] {
        if (auto *pp = dynamic_cast<Particles *>(self->children.get("particles"))) pp->max = 0;
        return true;
    });
    e1.blockable = false;
    G.E_MANAGER->add_event(std::move(e1));
    EventConfig ease;
    ease.trigger = Trigger::Ease;
    ease.blockable = false;
    ease.ref = ValueRef::of(&dissolve, this);
    ease.ease_to = 0;
    ease.delay = 1 * dissolve_time;
    G.E_MANAGER->add_event(std::move(ease));
    EventConfig e2 = ev_after(1.05 * dissolve_time, [self] {
        self->states.hover.can = true;
        if (Node *pp = self->children.get("particles")) {
            pp->remove();
            self->children.set("particles", nullptr);
        }
        return true;
    });
    e2.blockable = false;
    G.E_MANAGER->add_event(std::move(e2));
}

// ------------------------------------------------------------------ area / movement
void Card::set_card_area(CardArea *a) {
    area = a;
    parent = a;
    layered_parallax_of = a;
}

void Card::remove_from_area() {
    area = nullptr;
    parent = nullptr;
    layered_parallax_of = nullptr;
    layered_parallax = {0, 0};
}

void Card::align() {
    if (Sprite *f = sprite("floating_sprite")) {
        f->T->y = T->y;
        f->T->x = T->x;
        f->T->r = T->r;
    }
    realign_focused_ui(this);
}

void Card::flip() {
    if (facing == "front") {
        flipping = "f2b";
        facing = "back";
        pinch->x = true;
    } else if (facing == "back") {
        ability->set("wheel_flipped", Value());
        flipping = "b2f";
        facing = "front";
        pinch->x = true;
    }
}

void Card::update(double) {
    if (flipping == "f2b" && VT.w <= 0) {
        sprite_facing = "back";
        pinch->x = false;
    }
    if (flipping == "b2f" && VT.w <= 0) {
        sprite_facing = "front";
        pinch->x = false;
    }
    if (!states.focus.is && children.get("focused_ui")) {
        children.get("focused_ui")->remove();
        children.set("focused_ui", nullptr);
    }
    update_alert();
    if (ability->get(K("set")).equals("Joker") && !sticker_run.truthy()) {
        Value s = get_joker_win_sticker(config->get(K("center")));
        sticker_run = s.truthy() ? s : Value("NONE");
    }
    Value cons = ability->get(K("consumeable"));
    if (cons.truthy() && fld(cons, "max_highlighted").truthy())
        cons.table()->set("mod_num", std::min(5.0, fld(cons, "max_highlighted").num()));
    if (G.STAGE == RUN) {
        if (ability->get(K("perma_debuff")).truthy()) debuff = true;
        if (area && ((G.jokers && area == G.jokers.get()) || (G.consumeables && area == G.consumeables.get()))) {
            bypass_lock = true;
            bypass_discovery_center = true;
            bypass_discovery_ui = true;
        }
        sell_cost_label = facing == "back" ? Value("?") : Value(sell_cost);
        const Value nm = ability->get(K("name"));
        const std::string_view name = nm.is_str() ? nm.str() : std::string_view();
        auto center_name = [](Card *c) { return fld(c->config->get(K("center")), "name"); };
        if (name == "Temperance") {
            double money = 0;
            for (auto &j : G.jokers->cards)
                if (j->ability->get(K("set")).equals("Joker")) money += j->sell_cost;
            ability->set("money", std::min(money, ability->get(K("extra")).num()));
        }
        if (name == "Throwback") ability->set("x_mult", 1 + G.GAME->get(K("skips")).num() * ability->get(K("extra")).num());
        if (name == "Driver's License") {
            int n = 0;
            for (auto &c : G.playing_cards)
                if (c->config->get(K("center")).truthy() && !center_name(c.get()).equals("Default Base")) n++;
            ability->set("driver_tally", n);
        }
        if (name == "Steel Joker") {
            int n = 0;
            for (auto &c : G.playing_cards)
                if (center_name(c.get()).equals("Steel Card")) n++;
            ability->set("steel_tally", n);
        }
        if (name == "Cloud 9") {
            int n = 0;
            for (auto &c : G.playing_cards)
                if (c->get_id() == 9) n++;
            ability->set("nine_tally", n);
        }
        if (name == "Stone Joker") {
            int n = 0;
            for (auto &c : G.playing_cards)
                if (center_name(c.get()).equals("Stone Card")) n++;
            ability->set("stone_tally", n);
        }
        if (name == "Joker Stencil") {
            double x = G.jokers->config->get(K("card_limit")).num() - G.jokers->cards.size();
            for (auto &j : G.jokers->cards)
                if (j->ability->get(K("name")).equals("Joker Stencil")) x += 1;
            ability->set("x_mult", x);
        }
        if (name == "The Wheel of Fortune") {
            eligible_strength_jokers.clear();
            for (auto &v : G.jokers->cards)
                if (v->ability->get(K("set")).equals("Joker") && !v->edition) eligible_strength_jokers.push_back(v);
        }
        if (name == "Ectoplasm" || name == "Hex") {
            eligible_editionless_jokers.clear();
            for (auto &v : G.jokers->cards)
                if (v->ability->get(K("set")).equals("Joker") && !v->edition) eligible_editionless_jokers.push_back(v);
        }
        if (name == "Blueprint" || name == "Brainstorm") {
            Card *other = nullptr;
            if (name == "Brainstorm") {
                other = G.jokers->cards.empty() ? nullptr : G.jokers->cards[0].get();
            } else {
                for (size_t i = 0; i < G.jokers->cards.size(); i++)
                    if (G.jokers->cards[i].get() == this) other = i + 1 < G.jokers->cards.size() ? G.jokers->cards[i + 1].get() : nullptr;
            }
            const bool compat = other && other != this && fld(other->config->get(K("center")), "blueprint_compat").truthy();
            ability->set("blueprint_compat", compat ? "compatible" : "incompatible");
        }
        if (name == "Swashbuckler") {
            double sc = 0;
            for (auto &j : G.jokers->cards)
                if (j.get() != this && j->area && j->area == G.jokers.get()) sc += j->sell_cost;
            ability->set("mult", sc);
        }
    } else if (ability->get(K("name")).equals("Temperance")) {
        ability->set("money", 0);
    }
}

void Card::hard_set_T(float x, float y, float w, float h) {
    Moveable::hard_set_T(x, y, w, h);
    if (Sprite *f = sprite("front")) f->hard_set_T(x, y, w, h);
    sprite("back")->hard_set_T(x, y, w, h);
    sprite("center")->hard_set_T(x, y, w, h);
}



void Card::stop_hover() { Node::stop_hover(); }

void Card::juice_up_card(float scale, const float *rot_amount, bool disable_rumble) {
    G.MOBILE_VIBRATION_QUEUE = disable_rumble ? 0 : std::max(G.MOBILE_VIBRATION_QUEUE, 1.0);
    float rot_amt = rot_amount ? 0.4f * (lua_math.random() > 0.5 ? 1 : -1) * *rot_amount : (lua_math.random() > 0.5 ? 1 : -1) * 0.16f;
    scale = scale >= 0 ? scale * 0.4f : 0.11f;
    Moveable::juice_up(scale, &rot_amt);
}

// ------------------------------------------------------------------ draw
void Card::draw_layer(std::string_view layer) {
    if (G.vl_bench == 4) return;
    hover_tilt = 1;
    if (!states.visible) return;
    bool l_shadow = layer == "shadow" || layer == "both";
    bool l_card = layer == "card" || layer == "both";
    if (l_shadow) {
        send_to_shader[0] = (float)(std::min(VT.r * 3, 1.f) + G.TIMERS.REAL / 28 + (has_juice ? juice.r * 20 : 0) + tilt_var.amt);
        send_to_shader[1] = (float)G.TIMERS.REAL;
        children.each([&](Str *, Node *c) {
            if (Moveable *m = dynamic_cast<Moveable *>(c)) m->VT.scale = VT.scale;
        });
    }
    Sprite *center = sprite("center"), *front = sprite("front"), *bk = sprite("back");
    Sprite *shared_shadow = sprite_facing == "front" ? center : bk;
    std::string area_type = area ? area->type() : "";
    bool stone = ability->get(K("effect")).equals("Stone Card");

    if (!no_shadow && G.shadows_on() && l_shadow && !ability->get(K("effect")).equals("Glass Card") && !greyed &&
        ((area && area != G.discard.get() && area_type != "deck") || !area || states.drag.is)) {
        shadow_height = ((highlighted && G.play && area == G.play.get()) || states.drag.is) ? 0.35f
                        : (area && area_type == "title_2")                                   ? 0.04f
                                                                                             : 0.1f;
        shared_shadow->draw_shader(gfx::ShaderId::Dissolve, shadow_height);
    }
    if (l_card && !(G.hand && area == G.hand.get())) {
        if (Node *fu = children.get("focused_ui")) fu->draw();
    }
    if (!l_card) return;

    const float tilt_factor = 0.3f;
    if (states.focus.is) {
        tilt_var.mx = G.CONTROLLER->cursor_position.x + tilt_var.dx * T->w * G.TILESCALE * G.TILESIZE;
        tilt_var.my = G.CONTROLLER->cursor_position.y + tilt_var.dy * T->h * G.TILESCALE * G.TILESIZE;
        tilt_var.amt = std::fabs(hover_offset.y + hover_offset.x - 1 + tilt_var.dx + tilt_var.dy - 1) * tilt_factor;
    } else if (states.hover.is) {
        tilt_var.mx = G.CONTROLLER->cursor_position.x;
        tilt_var.my = G.CONTROLLER->cursor_position.y;
        tilt_var.amt = std::fabs(hover_offset.y + hover_offset.x - 1) * tilt_factor;
    } else if (has_ambient_tilt) {
        double tilt_angle = G.TIMERS.REAL * (1.56 + std::fmod(ID / 1.14212, 1.0)) + ID / 1.35122;
        tilt_var.mx = (float)(((0.5 + 0.5 * ambient_tilt * std::cos(tilt_angle)) * VT.w + VT.x + G.ROOM->T->x) * G.TILESIZE * G.TILESCALE);
        tilt_var.my = (float)(((0.5 + 0.5 * ambient_tilt * std::sin(tilt_angle)) * VT.h + VT.y + G.ROOM->T->y) * G.TILESIZE * G.TILESCALE);
        tilt_var.amt = (float)(ambient_tilt * (0.5 + std::cos(tilt_angle)) * tilt_factor);
    }
    if (Node *p = children.get("particles")) p->draw();
    if (Node *p = children.get("price")) p->draw();
    if (Node *bb = children.get("buy_button")) {
        if (highlighted) {
            bb->states.visible = true;
            bb->draw();
            if (Node *bu = children.get("buy_and_use_button")) bu->draw();
        } else {
            bb->states.visible = false;
        }
    }
    if (Node *ub = children.get("use_button"); ub && highlighted) ub->draw();

    SpriteShaderArgs send;
    send.send = send_to_shader;
    Value cen = config->get(K("center"));
    bool cdisc = fld(cen, "discovered").truthy() || bypass_discovery_center;
    Value an = ability->get(K("name"));
    Value set = ability->get(K("set"));

    if (vortex) {
        if (facing == "back") {
            bk->draw_shader(gfx::ShaderId::Vortex);
        } else {
            center->draw_shader(gfx::ShaderId::Vortex);
            if (front) front->draw_shader(gfx::ShaderId::Vortex);
        }
        gfx::set_shader(gfx::ShaderId::Default);
    } else if (sprite_facing == "front") {
        bool negative = (edition && edition->get(K("negative")).truthy()) || (an.equals("Antimatter") && cdisc);
        if (negative) {
            center->draw_shader(gfx::ShaderId::Negative, send);
            if (front && !stone) front->draw_shader(gfx::ShaderId::Negative, send);
        } else if (!greyed) {
            center->draw_shader(gfx::ShaderId::Dissolve);
            if (front && !stone) front->draw_shader(gfx::ShaderId::Dissolve);
        }
        if (!fld(cen, "discovered").truthy() && (ability->get(K("consumeable")).truthy() || fld(cen, "unlocked").truthy()) &&
            !fld(cen, "demo").truthy() && !bypass_discovery_center) {
            Sprite *shared = (set.equals("Edition") || set.equals("Joker")) ? G.shared_undiscovered_joker.get() : G.shared_undiscovered_tarot.get();
            SpriteShaderArgs a;
            a.other_obj = center;
            a.ms = (float)(-0.05 + 0.05 * std::sin(1.8 * G.TIMERS.REAL));
            a.mr = (float)(0.03 * std::sin(1.219 * G.TIMERS.REAL));
            shared->role.draw_major = this;
            shared->draw_shader(gfx::ShaderId::Dissolve, a);
        }
        if (an.equals("Invisible Joker") && cdisc) center->draw_shader(gfx::ShaderId::Voucher, send);

        bool extras = edition || seal.truthy() || ability->get(K("eternal")).truthy() || ability->get(K("rental")).truthy() ||
                      ability->get(K("perishable")).truthy() || sticker.truthy() ||
                      ((sticker_run.truthy() && !sticker_run.equals("NONE")) && G.SETTINGS->get(K("run_stake_stickers")).truthy()) ||
                      set.equals("Spectral") || debuff || greyed || an.equals("The Soul") || set.equals("Voucher") ||
                      set.equals("Booster") || fld(cen, "soul_pos").truthy() || fld(cen, "demo").truthy();
        if (extras) {
            auto on_center = [&](Sprite *s, gfx::ShaderId id, bool with_send) {
                SpriteShaderArgs a;
                if (with_send) a.send = send_to_shader;
                a.other_obj = center;
                s->role.draw_major = this;
                s->draw_shader(id, a);
            };
            if ((set.equals("Voucher") || fld(cen, "demo").truthy()) && (!an.equals("Antimatter") || !cdisc))
                center->draw_shader(gfx::ShaderId::Voucher, send);
            if (set.equals("Booster") || set.equals("Spectral")) center->draw_shader(gfx::ShaderId::Booster, send);
            if (edition && edition->get(K("holo")).truthy()) {
                center->draw_shader(gfx::ShaderId::Holo, send);
                if (front && !stone) front->draw_shader(gfx::ShaderId::Holo, send);
            }
            if (edition && edition->get(K("foil")).truthy()) {
                center->draw_shader(gfx::ShaderId::Foil, send);
                if (front && !stone) front->draw_shader(gfx::ShaderId::Foil, send);
            }
            if (edition && edition->get(K("polychrome")).truthy()) {
                center->draw_shader(gfx::ShaderId::Polychrome, send);
                if (front && !stone) front->draw_shader(gfx::ShaderId::Polychrome, send);
            }
            if (negative) center->draw_shader(gfx::ShaderId::NegativeShine, send);
            if (seal.truthy()) {
                auto it = G.shared_seals.find(std::string(seal.str()));
                if (it != G.shared_seals.end()) {
                    on_center(it->second.get(), gfx::ShaderId::Dissolve, false);
                    if (seal.equals("Gold")) on_center(it->second.get(), gfx::ShaderId::Voucher, true);
                }
            }
            if (ability->get(K("eternal")).truthy()) {
                on_center(G.shared_sticker_eternal.get(), gfx::ShaderId::Dissolve, false);
                on_center(G.shared_sticker_eternal.get(), gfx::ShaderId::Voucher, true);
            }
            if (ability->get(K("perishable")).truthy()) {
                on_center(G.shared_sticker_perishable.get(), gfx::ShaderId::Dissolve, false);
                on_center(G.shared_sticker_perishable.get(), gfx::ShaderId::Voucher, true);
            }
            if (ability->get(K("rental")).truthy()) {
                on_center(G.shared_sticker_rental.get(), gfx::ShaderId::Dissolve, false);
                on_center(G.shared_sticker_rental.get(), gfx::ShaderId::Voucher, true);
            }
            auto st = sticker.is_str() ? G.shared_stickers.find(std::string(sticker.str())) : G.shared_stickers.end();
            auto sr = sticker_run.is_str() ? G.shared_stickers.find(std::string(sticker_run.str())) : G.shared_stickers.end();
            if (st != G.shared_stickers.end()) {
                on_center(st->second.get(), gfx::ShaderId::Dissolve, false);
                on_center(st->second.get(), gfx::ShaderId::Voucher, true);
            } else if (sr != G.shared_stickers.end() && G.SETTINGS->get(K("run_stake_stickers")).truthy()) {
                on_center(sr->second.get(), gfx::ShaderId::Dissolve, false);
                on_center(sr->second.get(), gfx::ShaderId::Voucher, true);
            }
            const double R = G.TIMERS.REAL, fr = R - std::floor(R);
            if (an.equals("The Soul") && cdisc) {
                SpriteShaderArgs a;
                a.other_obj = center;
                a.ms = (float)(0.05 + 0.05 * std::sin(1.8 * R) + 0.07 * std::sin(fr * PI * 14) * std::pow(1 - fr, 3));
                a.mr = (float)(0.1 * std::sin(1.219 * R) + 0.07 * std::sin(R * PI * 5) * std::pow(1 - fr, 2));
                G.shared_soul->role.draw_major = this;
                SpriteShaderArgs s1 = a;
                float zero = 0, tilt_sh = 0.6f;
                s1.shadow_height = &zero;
                s1.my = (float)(0.1 + 0.03 * std::sin(1.8 * R));
                s1.tilt_shadow = &tilt_sh;
                G.shared_soul->draw_shader(gfx::ShaderId::Dissolve, s1);
                G.shared_soul->draw_shader(gfx::ShaderId::Dissolve, a);
            }
            if (fld(cen, "soul_pos").truthy() && cdisc) {
                Sprite *fs = sprite("floating_sprite");
                float scale_mod = (float)(0.07 + 0.02 * std::sin(1.8 * R) + 0.00 * std::sin(fr * PI * 14) * std::pow(1 - fr, 3));
                float rotate_mod = (float)(0.05 * std::sin(1.219 * R) + 0.00 * std::sin(R * PI * 5) * std::pow(1 - fr, 2));
                if (an.equals("Hologram")) {
                    hover_tilt *= 1.5f;
                    SpriteShaderArgs a;
                    a.send = send_to_shader;
                    a.other_obj = center;
                    a.ms = 2 * scale_mod;
                    a.mr = 2 * rotate_mod;
                    fs->draw_shader(gfx::ShaderId::Hologram, a);
                    hover_tilt /= 1.5f;
                } else {
                    SpriteShaderArgs a;
                    a.other_obj = center;
                    a.ms = scale_mod;
                    a.mr = rotate_mod;
                    SpriteShaderArgs s1 = a;
                    float zero = 0, tilt_sh = 0.6f;
                    s1.shadow_height = &zero;
                    s1.my = (float)(0.1 + 0.03 * std::sin(1.8 * R));
                    s1.tilt_shadow = &tilt_sh;
                    fs->draw_shader(gfx::ShaderId::Dissolve, s1);
                    fs->draw_shader(gfx::ShaderId::Dissolve, a);
                }
            }
            if (debuff) {
                center->draw_shader(gfx::ShaderId::Debuff, send);
                if (front && !stone) front->draw_shader(gfx::ShaderId::Debuff, send);
            }
            if (greyed) {
                center->draw_shader(gfx::ShaderId::Played, send);
                if (front && !stone) front->draw_shader(gfx::ShaderId::Played, send);
            }
        }
    } else if (sprite_facing == "back") {
        float overlay[4] = {1, 1, 1, 1};
        if (area && area_type == "deck" && rank > 3) {
            float v = 0.5f + (float)(((int)area->cards.size() - rank) % 7) / 50;
            overlay[0] = overlay[1] = overlay[2] = v;
        }
        if (area && area_type == "deck") bk->draw_with(overlay);
        else bk->draw_shader(gfx::ShaderId::Dissolve);
        auto st = sticker.is_str() ? G.shared_stickers.find(std::string(sticker.str())) : G.shared_stickers.end();
        if (st != G.shared_stickers.end()) {
            SpriteShaderArgs a;
            a.no_tilt = true;
            a.other_obj = center;
            st->second->role.draw_major = this;
            st->second->draw_shader(gfx::ShaderId::Dissolve, a);
            if (sticker.equals("Gold")) {
                a.send = send_to_shader;
                st->second->draw_shader(gfx::ShaderId::Voucher, a);
            }
        }
    }

    static Str *skip[] = {intern("focused_ui"), intern("front"),      intern("back"),       intern("soul_parts"),
                          intern("center"),     intern("floating_sprite"), intern("shadow"), intern("use_button"),
                          intern("buy_button"), intern("buy_and_use_button"), intern("debuff"), intern("price"),
                          intern("particles"),  intern("h_popup")};
    children.each([&](Str *k, Node *v) {
        for (Str *s : skip)
            if (k == s) return;
        v->draw();
    });
    if (G.hand && area == G.hand.get()) {
        if (Node *fu = children.get("focused_ui")) fu->draw();
    }
    add_to_drawhash(this);
    draw_boundingrect();
}

// ------------------------------------------------------------------ input
void Card::release(Node *dragged) {
    if (dragged && dragged->is_card() && area) area->release(dragged);
}

void Card::highlight(bool is_highlighted) {
    highlighted = is_highlighted;
    const bool cons = ability->get(K("consumeable")).truthy(), joker = ability->get(K("set")).equals("Joker");
    const bool owned = area && ((G.jokers && area == G.jokers.get()) || (G.consumeables && area == G.consumeables.get()));
    if (cons || joker || (area && G.pack_cards && area == G.pack_cards.get())) {
        if (highlighted && area && !area->config->get(K("type")).equals("shop")) {
            const double x_off = cons ? -0.1 : 0;
            if (Node *old = children.get("use_button")) old->remove();
            children.set("use_button", new UIBox(use_and_sell_buttons(this),
                                                 vl::T({{"align", owned ? "cr" : "bmi"},
                                                        {"offset", Value(owned ? vl::T({{"x", x_off - 0.4}, {"y", 0}}) : vl::T({{"x", 0}, {"y", 0.65}}))},
                                                        {"parent", Value((Object *)this)}})));
        } else if (Node *b = children.get("use_button")) {
            b->remove();
            children.set("use_button", nullptr);
        }
    }
    if (cons || joker) {
        if (!highlighted && area && area->config->get(K("type")).equals("joker") &&
            (G.jokers->cards.size() >= G.jokers->config->get(K("card_limit")).num() || (edition && edition->get(K("negative")).truthy())))
            if (G.shop_jokers) G.shop_jokers->unhighlight_all();
    }
}

void Card::sell_card() {
    G.CONTROLLER->locks["selling_card"] = true;
    stop_use();
    CardArea *a = area;
    const std::string area_name = a == G.jokers.get() ? "jokers" : "consumeables";
    G.CONTROLLER->save_cardarea_focus(area_name);
    for (const char *k : {"use_button", "sell_button"})
        if (Node *b = children.get(k)) {
            b->remove();
            children.set(k, nullptr);
        }
    calculate_joker(vl::T({{"selling_self", true}}));
    CardRef self(this);
    G.E_MANAGER->add_event(ev_after(0.2, [self] {
        play_sound("coin2");
        float r = 0.4f;
        self->juice_up_card(0.3f, &r);
        return true;
    }));
    delay(0.2);
    G.E_MANAGER->add_event(ev_immediate([self, area_name] {
        ease_dollars(self->sell_cost);
        self->start_dissolve(A({G.colour("GOLD")}));
        delay(0.3);
        inc_career_stat("c_cards_sold", 1);
        if (self->ability->get(K("set")).equals("Joker")) inc_career_stat("c_jokers_sold", 1);
        Blind *b = G.GAME->get(K("blind")).as<Blind>();
        if (self->ability->get(K("set")).equals("Joker") && b && b->name == "Verdant Leaf")
            G.E_MANAGER->add_event(ev_immediate([] {
                G.GAME->get(K("blind")).as<Blind>()->disable();
                return true;
            }));
        EventConfig e = ev_after(0.3, [area_name] {
            G.E_MANAGER->add_event(ev_immediate([area_name] {
                G.E_MANAGER->add_event(ev_immediate([area_name] {
                    G.CONTROLLER->locks.erase("selling_card");
                    G.CONTROLLER->recall_cardarea_focus(area_name);
                    return true;
                }));
                return true;
            }));
            return true;
        });
        e.blocking = false;
        G.E_MANAGER->add_event(e);
        return true;
    }));
}

bool Card::can_sell_card() const {
    Value su = G.GAME->get(K("STOP_USE"));
    if ((G.play && !G.play->cards.empty()) || G.CONTROLLER->locked || (su.truthy() && su.num() > 0)) return false;
    if ((G.SETTINGS->get(K("tutorial_complete")).truthy() || !fld(G.GAME->get(K("pseudorandom")), "seed").equals("TUTORIAL") ||
         fld(G.GAME->get(K("round_resets")), "ante").num() > 1) &&
        area && area->config->get(K("type")).equals("joker") && !ability->get(K("eternal")).truthy())
        return true;
    return false;
}

void Card::click() {
    if (area && area->can_highlight(this)) {
        if (G.hand && area == G.hand.get() && G.STATE == HAND_PLAYED) return;
        if (!highlighted) {
            area->add_to_highlighted(this);
        } else {
            area->remove_from_highlighted(this);
            play_sound("cardSlide2", 1, 0.3f);
        }
    }
    if (area && G.deck && area == G.deck.get() && !area->cards.empty() && area->cards[0].get() == this) {
        auto it = G.FUNCS.find("deck_info");
        if (it != G.FUNCS.end()) it->second(nullptr);
    }
}

void Card::single_tap() {
    if (area && area->can_highlight(this) && G.hand && area == G.hand.get()) {
        if (G.STATE == HAND_PLAYED) return;
        if (!highlighted) {
            area->add_to_highlighted(this);
        } else {
            area->remove_from_highlighted(this);
            play_sound("cardSlide2", 1, 0.3f);
        }
    }
    if (area && G.deck && area == G.deck.get() && !area->cards.empty() && area->cards[0].get() == this) {
        auto it = G.FUNCS.find("deck_info");
        if (it != G.FUNCS.end()) it->second(nullptr);
    }
    G.MOBILE_VIBRATION_QUEUE = std::max(G.MOBILE_VIBRATION_QUEUE, 1.0);
}

bool Card::can_long_press() {
    return area && ((G.hand && area == G.hand.get()) ||
                    (G.deck && area == G.deck.get() && !area->cards.empty() && area->cards[0].get() == this));
}

void Card::remove() {
    removed = true;
    Ref<Card> keep(this);
    if (area) area->remove_card(this);
    remove_from_deck();
    if (ability->get(K("queue_negative_removal")).truthy()) {
        CardArea *a = ability->get(K("consumeable")).truthy() ? G.consumeables.get() : G.jokers.get();
        if (a) a->config->set("card_limit", a->config->get(K("card_limit")).num() - 1);
    }
    if (!G.OVERLAY_MENU && G.GAME) {
        Value an = ability->get(K("name"));
        Value used = G.GAME->get(K("used_jokers"));
        G.P_CENTERS->each([&](const Value &k, const Value &v) {
            if (fld(v, "name") == an && find_joker(an.str(), true).empty() && used.is_table()) used.table()->set(k, Value());
        });
    }
    if (G.has_playing_cards) {
        for (size_t k = 0; k < G.playing_cards.size(); k++) {
            if (G.playing_cards[k].get() == this) {
                G.playing_cards.erase(G.playing_cards.begin() + k);
                break;
            }
        }
        for (size_t k = 0; k < G.playing_cards.size(); k++) G.playing_cards[k]->playing_card = (int)k + 1;
    }
    remove_all(children);
    G.I.CARD.remove(this);
    Moveable::remove();
}

// ================================================================== CardArea
CardArea::CardArea(float x, float y, float w, float h, TableRef cfg) {
    Moveable::init(x, y, w, h);
    states.drag.can = false;
    states.hover.can = false;
    states.click.can = false;
    config = cfg ? cfg : new_table();
    card_w = config->get(K("card_w")).is_num() ? (float)config->get(K("card_w")).num() : G.CARD_W;
    cards.clear();
    children.clear();
    highlighted.clear();
    auto def = [&](const char *k, const char *src, Value d) {
        Value v = config->get(Value(intern(src)));
        config->set(k, v.truthy() ? v : d);
    };
    def("highlighted_limit", "highlight_limit", 5);
    def("card_limit", "card_limit", 52);
    config->set("temp_limit", config->get(K("card_limit")));
    config->set("card_count", 0);
    def("type", "type", "deck");
    def("sort", "sort", "desc");
    def("lr_padding", "lr_padding", 0.1);
    shuffle_amt = 0;
    G.I.CARDAREA.add(this);
}

std::string CardArea::type() const { return config->get(K("type")).to_string(); }

void CardArea::emplace(Card *card, const char *location, bool stay_flipped) {
    std::string t = type();
    if ((location && std::string_view(location) == "front") || t == "deck") cards.insert(cards.begin(), CardRef(card));
    else cards.push_back(CardRef(card));
    if (card->facing == "back" && t != "discard" && t != "deck" && !stay_flipped) card->flip();
    if (G.hand && this == G.hand.get() && stay_flipped) card->ability->set("wheel_flipped", true);
    if ((double)cards.size() > config->get(K("card_limit")).num()) {
        if (G.deck && this == G.deck.get()) config->set("card_limit", (double)cards.size());
    }
    card->set_card_area(this);
    set_ranks();
    align_cards();
    if (G.jokers && this == G.jokers.get()) {
        int joker_tally = 0;
        for (auto &c : cards)
            if (c->ability->get(K("set")).equals("Joker")) joker_tally++;
        if (joker_tally > G.GAME->get(K("max_jokers")).num()) G.GAME->set("max_jokers", joker_tally);
        check_for_unlock(vl::T({{"type", "modify_jokers"}}));
    }
    if (G.deck && this == G.deck.get()) check_for_unlock(vl::T({{"type", "modify_deck"}, {"deck", Value((Object *)this)}}));
}

Card *CardArea::remove_card(Card *card, bool discarded_only) {
    if (cards_nil) return nullptr;
    std::vector<Card *> c;
    for (auto &v : cards)
        if (!discarded_only || v->ability->get(K("discarded")).truthy()) c.push_back(v.get());
    std::string t = type();
    if (!card) {
        if (t == "discard" || t == "deck") card = c.empty() ? nullptr : c.back();
        else card = c.empty() ? nullptr : c.front();
    }
    CardRef keep(card);
    for (int i = (int)cards.size() - 1; i >= 0; i--) {
        if (cards[i].get() == card) {
            card->remove_from_area();
            cards.erase(cards.begin() + i);
            remove_from_highlighted(card, true);
            break;
        }
    }
    set_ranks();
    if (G.deck && this == G.deck.get()) check_for_unlock(vl::T({{"type", "modify_deck"}, {"deck", Value((Object *)this)}}));
    return card;
}

void CardArea::change_size(int delta) {
    if (delta == 0) return;
    Ref<CardArea> self(this);
    G.E_MANAGER->add_event(ev_immediate([self, delta] {
        Value rl = self->config->get(K("real_card_limit"));
        double real = (rl.truthy() ? rl.num() : self->config->get(K("card_limit")).num()) + delta;
        self->config->set("real_card_limit", real);
        self->config->set("card_limit", std::max(0.0, real));
        if (delta > 0 && real > 1 && G.hand && self.get() == G.hand.get() && !self->cards.empty() &&
            (G.STATE == DRAW_TO_HAND || G.STATE == SELECTING_HAND)) {
            const int card_count = std::abs(delta);
            for (int i = 1; i <= card_count; i++) {
                draw_card(G.deck.get(), G.hand.get(), i * 100.0 / card_count, nullptr, false, nullptr, 0.07);
                G.E_MANAGER->add_event(ev_immediate([self] {
                    self->sort();
                    return true;
                }));
            }
        }
        if (G.hand && self.get() == G.hand.get()) check_for_unlock(vl::T({{"type", "min_hand_size"}}));
        return true;
    }));
}

bool CardArea::can_highlight(Card *) const {
    std::string t = type();
    if (G.CONTROLLER->HID.controller || G.CONTROLLER->HID.touch) return t == "hand";
    return t == "hand" || t == "joker" || t == "consumeable" || (t == "shop" && config->get(K("highlighted_limit")).num() > 0);
}

void CardArea::add_to_highlighted(Card *card, bool silent) {
    std::string t = type();
    double limit = config->get(K("highlighted_limit")).num();
    if (t == "shop") {
        if (!highlighted.empty()) remove_from_highlighted(highlighted[0].get());
        highlighted.push_back(CardRef(card));
        card->highlight(true);
        if (!silent) play_sound("cardSlide1");
    } else if (t == "joker" || t == "consumeable") {
        if ((double)highlighted.size() >= limit) remove_from_highlighted(highlighted[0].get());
        highlighted.push_back(CardRef(card));
        card->highlight(true);
        if (!silent) play_sound("cardSlide1");
    } else {
        if ((double)highlighted.size() >= limit) {
            card->highlight(false);
        } else {
            highlighted.push_back(CardRef(card));
            card->highlight(true);
            if (!silent) play_sound("cardSlide1");
        }
        if (G.hand && this == G.hand.get() && G.STATE == SELECTING_HAND) parse_highlighted();
    }
}

void CardArea::parse_highlighted() { parse_hand_highlight(this); }

void CardArea::remove_from_highlighted(Card *card, bool force) {
    if (!force && card && card->ability->get(K("forced_selection")).truthy() && G.hand && this == G.hand.get()) return;
    for (int i = (int)highlighted.size() - 1; i >= 0; i--) {
        if (highlighted[i].get() == card) {
            highlighted.erase(highlighted.begin() + i);
            break;
        }
    }
    if (card) card->highlight(false);
    if (G.hand && this == G.hand.get() && G.STATE == SELECTING_HAND) parse_highlighted();
}

void CardArea::unhighlight_all() {
    for (int i = (int)highlighted.size() - 1; i >= 0; i--) {
        if (highlighted[i]->ability->get(K("forced_selection")).truthy() && G.hand && this == G.hand.get()) continue;
        highlighted[i]->highlight(false);
        highlighted.erase(highlighted.begin() + i);
    }
    if (G.hand && this == G.hand.get() && G.STATE == SELECTING_HAND) parse_highlighted();
}

void CardArea::set_ranks() {
    std::string t = type();
    int k = 1;
    for (auto &card : cards) {
        card->rank = k;
        card->states.collide.can = true;
        if (k > 1 && t == "deck") {
            card->states.drag.can = false;
            card->states.collide.can = false;
        } else if (t == "play" || t == "shop" || t == "consumeable") {
            card->states.drag.can = G.CONTROLLER->HID.touch && (t == "shop" || t == "consumeable");
        } else {
            card->states.drag.can = true;
        }
        k++;
    }
}

void CardArea::move(double dt) {
    if (G.hand && this == G.hand.get()) {
        bool sliding_up = !G.deck_preview && (G.STATE == SELECTING_HAND || G.STATE == DRAW_TO_HAND);
        float desired_y = G.TILE_H - T->h - 1.9f * (sliding_up ? 1 : 0);
        T->y = (float)(15 * G.real_dt * desired_y + (1 - 15 * G.real_dt) * T->y);
        if (std::fabs(desired_y - T->y) < 0.01f) T->y = desired_y;
        if (G.STATE == TUTORIAL && G.play) G.play->T->y = T->y - (3 + 0.6f);
    }
    Moveable::move(dt);
    align_cards();
}

void CardArea::update(double) {
    if (G.hand && this == G.hand.get()) {
        // TODO(run): minus_hand_size_per_X_dollar modifier
        for (auto &v : std::vector<CardRef>(cards))
            if (v->ability->get(K("forced_selection")).truthy() && highlighted.empty()) add_to_highlighted(v.get());
    }
    if (G.deck && this == G.deck.get()) {
        states.collide.can = true;
        states.hover.can = true;
        states.click.can = true;
    }
    if (G.CONTROLLER->HID.controller && !(G.hand && this == G.hand.get())) unhighlight_all();
    if (G.deck && this == G.deck.get() && config->get(K("card_limit")).num() > (double)G.playing_cards.size())
        config->set("card_limit", (double)G.playing_cards.size());
    config->set("temp_limit", std::max((double)cards.size(), config->get(K("card_limit")).num()));
    config->set("card_count", (double)cards.size());
}

void CardArea::draw() {
    if (!states.visible) return;
    if (G.VIEWING_DECK && ((G.deck && this == G.deck.get()) || (G.hand && this == G.hand.get()) || (G.play && this == G.play.get())))
        return;
    std::string t = type();
    int state = G.TAROT_INTERRUPT ? G.TAROT_INTERRUPT : G.STATE;
    bool invisible = t == "discard" || t == "voucher" || t == "play" || t == "consumeable" || t == "title" || t == "title_2" ||
                     (t == "hand" && (state == SHOP || state == TAROT_PACK || state == SPECTRAL_PACK || state == STANDARD_PACK ||
                                      state == BUFFOON_PACK || state == PLANET_PACK || state == ROUND_EVAL || state == BLIND_SELECT)) ||
                     (t == "deck" && !(G.deck && this == G.deck.get())) || (t == "shop" && !(G.shop_vouchers && this == G.shop_vouchers.get()));
    if (!invisible) {
        if (!children.get("area_uibox")) {
            const bool sv = G.shop_vouchers && this == G.shop_vouchers.get();
            auto nd = [](int uit, TableRef cfg, TableRef nodes = nullptr) {
                TableRef t = vl::T({{"n", (double)uit}, {"config", Value(cfg)}});
                if (nodes) t->set("nodes", Value(nodes));
                return Value(t);
            };
            Value en = Value((Object *)G.LANGUAGES["en-us"].get());
            Value card_count;
            if (!sv) {
                const char *al = (G.jokers && this == G.jokers.get()) ? "cl" : (G.hand && this == G.hand.get()) ? "cm" : "cr";
                card_count = nd(UIT_R, vl::T({{"align", al}, {"padding", 0.03}, {"no_fill", true}}),
                                A({nd(UIT_B, vl::T({{"w", 0.1}, {"h", 0.1}})),
                                   nd(UIT_T, vl::T({{"ref_table", Value(config)}, {"ref_value", "card_count"}, {"scale", 0.3}, {"lang", en}, {"colour", G.colour("WHITE")}})),
                                   nd(UIT_T, vl::T({{"text", "/"}, {"scale", 0.3}, {"lang", en}, {"colour", G.colour("WHITE")}})),
                                   nd(UIT_T, vl::T({{"ref_table", Value(config)}, {"ref_value", "card_limit"}, {"scale", 0.3}, {"lang", en}, {"colour", G.colour("WHITE")}})),
                                   nd(UIT_B, vl::T({{"w", 0.1}, {"h", 0.1}}))}));
            }
            TableRef inner = new_table();
            if (sv) {
                auto line = [&](const char *text, double scale) {
                    return nd(UIT_R, vl::T({{"align", "cm"}}), A({nd(UIT_T, vl::T({{"text", text}, {"scale", scale}, {"colour", G.colour("WHITE")}}))}));
                };
                // (the original's config says `paddin`: no padding)
                inner->push(nd(UIT_C, vl::T({{"align", "cm"}, {"paddin", 0.1}, {"func", "shop_voucher_empty"}, {"visible", false}}),
                               A({line("DEFEAT", 0.6), line("BOSS BLIND", 0.4), line("TO RESTOCK", 0.4)})));
            }
            TableRef rcfg = vl::T({{"minw", T->w}, {"minh", T->h}, {"align", "cm"}, {"padding", 0.1}, {"mid", true}, {"r", 0.1}, {"ref_table", Value((Object *)this)}});
            if (!sv) rcfg->set("colour", Value(A({0, 0, 0, 0.1})));
            TableRef root_nodes = A({nd(UIT_R, rcfg, inner)});
            if (card_count.truthy()) root_nodes->push(card_count);
            UIBox *box = new UIBox(TableRef(nd(UIT_ROOT, vl::T({{"align", "cm"}, {"colour", G.colour("CLEAR")}}), root_nodes).table()),
                                   vl::T({{"align", "cm"}, {"offset", Value(vl::T({{"x", 0}, {"y", 0}}))}, {"major", Value((Object *)this)}, {"parent", Value((Object *)this)}}));
            children.set("area_uibox", box);
        }
        children.get("area_uibox")->draw();
    }
    draw_boundingrect();
    add_to_drawhash(this);

    if (draw_layers.empty()) {
        Value dl = config->get(K("draw_layers"));
        if (Table *d = dl.table()) {
            for (int i = 1; i <= d->len(); i++) draw_layers.push_back(d->get(i).to_string());
        } else {
            draw_layers = {"shadow", "card"};
        }
    }
    Node *focused = G.CONTROLLER->focused.target.get();
    Node *dragging = G.CONTROLLER->dragging.target.get();
    for (const std::string &layer : draw_layers) {
        int n = (int)cards.size();
        if (t == "deck") {
            int thin = config->get(K("thin_draw")).is_num() ? (int)config->get(K("thin_draw")).num() : 9;
            for (int i = n; i >= 1; i--) {
                Card *c = cards[i - 1].get();
                if (c == focused) continue;
                if (i == 1 || i % thin == 0 || i == n || std::fabs(c->VT.x - T->x) > 1 || std::fabs(c->VT.y - T->y) > 1)
                    if (dragging != c) c->draw_layer(layer);
            }
        }
        if (t == "joker" || t == "consumeable" || t == "shop" || t == "title_2") {
            for (int pass = 0; pass < 2; pass++)
                for (int i = 0; i < n && i < (int)cards.size(); i++) {
                    Card *c = cards[i].get();
                    if (c == focused || c->highlighted != (pass == 1)) continue;
                    if (dragging != c) c->draw_layer(layer);
                }
        }
        if (t == "discard") {
            for (int i = 0; i < n; i++) {
                Card *c = cards[i].get();
                if (c != focused && std::fabs(c->VT.x - T->x) > 1 && dragging != c) c->draw_layer(layer);
            }
        }
        if (t == "hand" || t == "play" || t == "title" || t == "voucher") {
            for (int i = 0; i < n; i++) {
                Card *c = cards[i].get();
                if ((c != focused || (G.hand && this == G.hand.get())) && dragging != c) c->draw_layer(layer);
            }
        }
    }
    if (G.deck && this == G.deck.get()) {
        auto nd = [](int uit, TableRef cfg, TableRef nodes = nullptr) {
            TableRef t = vl::T({{"n", (double)uit}, {"config", Value(cfg)}});
            if (nodes) t->set("nodes", Value(nodes));
            return Value(t);
        };
        auto text = [&](const Value &s, double scale) {
            return nd(UIT_T, vl::T({{"text", s}, {"scale", scale}, {"colour", G.colour("WHITE")}, {"shadow", true}}));
        };
        const bool ctrl = G.CONTROLLER->HID.controller;
        if (ctrl && G.STATE == SELECTING_HAND && !children.get("peek_deck")) {
            UIBox *pk = new UIBox(
                TableRef(nd(UIT_ROOT, vl::T({{"align", "cm"}, {"padding", 0.1}, {"r", 0.1}, {"colour", G.colour("CLEAR")}}),
                            A({nd(UIT_R,
                                  vl::T({{"align", "cm"}, {"r", 0.1}, {"colour", Value(adjust_alpha(G.colour("L_BLACK"), 0.5f))}, {"func", "set_button_pip"},
                                         {"focus_args", Value(vl::T({{"button", "triggerleft"}, {"orientation", "bm"}, {"scale", 0.6}, {"type", "none"}}))}}),
                                  A({nd(UIT_R, vl::T({{"align", "cm"}}), A({text(Value("PEEK"), 0.48)})),
                                     nd(UIT_R, vl::T({{"align", "cm"}}), A({text(Value("DECK"), 0.38)}))}))}))
                    .table()),
                vl::T({{"align", "cl"}, {"offset", Value(vl::T({{"x", -0.5}, {"y", 0.1}}))}, {"major", Value((Object *)this)}, {"parent", Value((Object *)this)}}));
            pk->states.collide.can = false;
            children.set("peek_deck", pk);
        } else if ((!ctrl || G.STATE != SELECTING_HAND) && children.get("peek_deck")) {
            children.get("peek_deck")->remove();
            children.set("peek_deck", nullptr);
        }
        if (!children.get("view_deck")) {
            Object *major = cards.empty() ? (Object *)this : (Object *)cards[0].get();
            UIBox *vd = new UIBox(
                TableRef(nd(UIT_ROOT, vl::T({{"align", "cm"}, {"padding", 0.1}, {"r", 0.1}, {"colour", G.colour("CLEAR")}}),
                            A({nd(UIT_R,
                                  vl::T({{"align", "cm"}, {"padding", 0.05}, {"r", 0.1}, {"colour", Value(adjust_alpha(G.colour("BLACK"), 0.5f))}, {"func", "set_button_pip"},
                                         {"focus_args", Value(vl::T({{"button", "triggerright"}, {"orientation", "bm"}, {"scale", 0.6}}))}, {"button", "deck_info"}}),
                                  A({nd(UIT_R, vl::T({{"align", "cm"}, {"maxw", 2}}), A({text(localize("k_view"), 0.48)})),
                                     nd(UIT_R, vl::T({{"align", "cm"}, {"maxw", 2}}), A({text(localize("k_deck"), 0.38)}))}))}))
                    .table()),
                vl::T({{"align", "cm"}, {"offset", Value(vl::T({{"x", 0}, {"y", 0}}))}, {"major", Value(major)}, {"parent", Value((Object *)this)}}));
            vd->states.collide.can = false;
            children.set("view_deck", vd);
        }
        auto *buttons = dynamic_cast<UIBox *>(G.buttons.get());
        if (G.deck_preview || (!under_overlay && states.collide.is) || (buttons && !buttons->under_overlay && buttons->states.collide.is && ctrl))
            children.get("view_deck")->draw();
        if (Node *pk = children.get("peek_deck")) pk->draw();
    }
}

void CardArea::align_cards() {
    std::string t = type();
    const int n = (int)cards.size();
    const double motion = G.reduced_motion() ? 0 : 1;
    const double R = G.TIMERS.REAL;
    const double temp_limit = config->get(K("temp_limit")).num();
    auto by_x = [](const CardRef &a, const CardRef &b) { return a->T->x + a->T->w / 2 < b->T->x + b->T->w / 2; };
    if (t == "deck") {
        double deck_height = (config->get(K("deck_height")).is_num() ? config->get(K("deck_height")).num() : 0.15) / 52;
        bool is_deck = G.deck && this == G.deck.get();
        for (int k = 1; k <= n; k++) {
            Card *card = cards[k - 1].get();
            if (card->facing == "front") card->flip();
            if (!card->states.drag.is) {
                double off = (double)n / (is_deck ? 1 : 2) - k;
                card->T->x = (float)(T->x + 0.5 * (T->w - card->T->w) + shadow_parrallax->x * deck_height * off +
                                     0.9 * shuffle_amt * (1 - k * 0.01) * (k % 2 == 1 ? 1 : -0.0));
                card->T->y = (float)(T->y + 0.5 * (T->h - card->T->h) + shadow_parrallax->y * deck_height * off);
                card->T->r = (float)(0 + 0.3 * shuffle_amt * (1 + k * 0.05) * (k % 2 == 1 ? 1 : -0.0));
                card->T->x = card->T->x + card->shadow_parrallax->x / 30;
            }
        }
    }
    if (t == "discard") {
        for (auto &cr : cards) {
            Card *card = cr.get();
            if (card->facing == "front") card->flip();
            if (!card->states.drag.is) {
                card->T->x = (float)(T->x + (T->w - card->T->w) * card->discard_pos.x);
                card->T->y = (float)(T->y + (T->h - card->T->h) * card->discard_pos.y);
                card->T->r = (float)card->discard_pos.r;
            }
        }
    }
    auto spread_x = [&](Card *card, int k) {
        double max_cards = std::max((double)n, temp_limit);
        return T->x + (T->w - card_w) * ((k - 1) / std::max(max_cards - 1, 1.0) - 0.5 * (n - max_cards) / std::max(max_cards - 1, 1.0)) +
               0.5 * (card_w - card->T->w);
    };
    bool pack_state = G.STATE == TAROT_PACK || G.STATE == SPECTRAL_PACK || G.STATE == PLANET_PACK;
    if (t == "hand" && pack_state) {
        for (int k = 1; k <= n; k++) {
            Card *card = cards[k - 1].get();
            if (card->states.drag.is) continue;
            card->T->r = (float)(0.4 * (-n / 2.0 - 0.5 + k) / n + motion * 0.02 * std::sin(2 * R + card->T->x));
            card->T->x = (float)spread_x(card, k);
            double hh = card->highlighted ? G.HIGHLIGHT_H : 0;
            card->T->y = (float)(G.hand->T->y - 1.8 * card->T->h - hh + motion * 0.1 * std::sin(0.666 * R + card->T->x) +
                                 std::pow(std::fabs(1.3 * (-n / 2.0 + k - 0.5) / n), 2) - 0.3);
            card->T->x = card->T->x + card->shadow_parrallax->x / 30;
        }
        lua_sort(cards, by_x);
    }
    if (t == "hand" && !pack_state) {
        for (int k = 1; k <= n; k++) {
            Card *card = cards[k - 1].get();
            if (card->states.drag.is) continue;
            card->T->r = (float)(0.2 * (-n / 2.0 - 0.5 + k) / n + motion * 0.02 * std::sin(2 * R + card->T->x));
            card->T->x = (float)spread_x(card, k);
            double hh = card->highlighted ? G.HIGHLIGHT_H : 0;
            card->T->y = (float)(T->y + T->h / 2 - card->T->h / 2 - hh + motion * 0.03 * std::sin(0.666 * R + card->T->x) +
                                 std::fabs(0.5 * (-n / 2.0 + k - 0.5) / n) - 0.2);
            card->T->x = card->T->x + card->shadow_parrallax->x / 30;
        }
        lua_sort(cards, by_x);
    }
    if (t == "title" || (t == "voucher" && n == 1)) {
        for (int k = 1; k <= n; k++) {
            Card *card = cards[k - 1].get();
            if (card->states.drag.is) continue;
            card->T->r = (float)(0.2 * (-n / 2.0 - 0.5 + k) / n + motion * 0.02 * std::sin(2 * R + card->T->x));
            card->T->x = (float)spread_x(card, k);
            double hh = card->highlighted ? G.HIGHLIGHT_H : 0;
            card->T->y = (float)(T->y + T->h / 2 - card->T->h / 2 - hh + motion * 0.03 * std::sin(0.666 * R + card->T->x) +
                                 std::fabs(0.5 * (-n / 2.0 + k - 0.5) / n) - (n > 1 ? 0.2 : 0));
            card->T->x = card->T->x + card->shadow_parrallax->x / 30;
        }
        lua_sort(cards, by_x);
    }
    if (t == "voucher" && n > 1) {
        double self_w = std::max((double)T->w, 3.2);
        for (int k = 1; k <= n; k++) {
            Card *card = cards[k - 1].get();
            if (card->states.drag.is) continue;
            card->T->r = (float)(0.2 * (-n / 2.0 - 0.5 + k) / n + motion * 0.02 * std::sin(2 * R + card->T->x + card->T->y) +
                                 (k % 2 == 0 ? 1 : -1) * 0.08);
            double max_cards = std::max((double)n, temp_limit);
            card->T->x = (float)(T->x + (self_w - card_w) * ((k - 1) / std::max(max_cards - 1, 1.0) - 0.5 * (n - max_cards) / std::max(max_cards - 1, 1.0)) +
                                 0.5 * (card_w - card->T->w) + (k % 2 == 1 ? 1 : -1) * 0.27 + (T->w - self_w) / 2);
            double hh = card->highlighted ? G.HIGHLIGHT_H : 0;
            card->T->y = (float)(T->y + T->h / 2 - card->T->h / 2 - hh + motion * 0.03 * std::sin(0.666 * R + card->T->x) +
                                 std::fabs(0.5 * (-n / 2.0 + k - 0.5) / n) - (n > 1 ? 0.2 : 0));
            card->T->x = card->T->x + card->shadow_parrallax->x / 30;
        }
        lua_sort(cards, [](const CardRef &a, const CardRef &b) {
            return a->ability->get(K("order")).num() < b->ability->get(K("order")).num();
        });
    }
    if (t == "play" || t == "shop") {
        double limit = config->get(K("card_limit")).num();
        for (int k = 1; k <= n; k++) {
            Card *card = cards[k - 1].get();
            if (card->states.drag.is) continue;
            card->T->r = 0;
            card->T->x = (float)(spread_x(card, k) + (limit == 1 ? 0.5 * (T->w - card->T->w) : 0));
            double hh = card->highlighted ? G.HIGHLIGHT_H : 0;
            card->T->y = (float)(T->y + T->h / 2 - card->T->h / 2 - hh);
            card->T->x = card->T->x + card->shadow_parrallax->x / 30;
        }
        lua_sort(cards, by_x);
    }
    if (t == "joker" || t == "title_2") {
        bool flat = config->get(K("flat")).truthy();
        bool is_cons = G.consumeables && this == G.consumeables.get();
        for (int k = 1; k <= n; k++) {
            Card *card = cards[k - 1].get();
            if (card->states.drag.is) continue;
            card->T->r = (float)((flat ? 0 : 0.1) * (-n / 2.0 - 0.5 + k) / n + motion * 0.02 * std::sin(2 * R + card->T->x));
            card->T->x = (float)spread_x(card, k);
            if (n > 2 || (n > 1 && is_cons) || (n > 1 && config->get(K("spread")).truthy()))
                card->T->x = (float)(T->x + (T->w - card_w) * ((k - 1.0) / (n - 1)) + 0.5 * (card_w - card->T->w));
            else if (n > 1 && !is_cons)
                card->T->x = (float)(T->x + (T->w - card_w) * ((k - 0.5) / n) + 0.5 * (card_w - card->T->w));
            else
                card->T->x = (float)(T->x + T->w / 2 - card_w / 2 + 0.5 * (card_w - card->T->w));
            double hh = card->highlighted ? G.HIGHLIGHT_H / 2 : 0;
            card->T->y = (float)(T->y + T->h / 2 - card->T->h / 2 - hh + motion * 0.03 * std::sin(0.666 * R + card->T->x));
            card->T->x = card->T->x + card->shadow_parrallax->x / 30;
        }
        lua_sort(cards, [](const CardRef &a, const CardRef &b) {
            return a->T->x + a->T->w / 2 - 100 * (a->pinned ? a->sort_id : 0) < b->T->x + b->T->w / 2 - 100 * (b->pinned ? b->sort_id : 0);
        });
    }
    if (t == "consumeable") {
        for (int k = 1; k <= n; k++) {
            Card *card = cards[k - 1].get();
            if (card->states.drag.is) continue;
            if (n > 1) card->T->x = (float)(T->x + (T->w - card_w) * ((k - 1.0) / (n - 1)) + 0.5 * (card_w - card->T->w));
            else card->T->x = (float)(T->x + T->w / 2 - card_w / 2 + 0.5 * (card_w - card->T->w));
            double hh = card->highlighted ? G.HIGHLIGHT_H : 0;
            card->T->y = (float)(T->y + T->h / 2 - card->T->h / 2 - hh +
                                 (!card->highlighted ? motion * 0.05 * std::sin(2 * 1.666 * R + card->T->x) : 0));
            card->T->x = card->T->x + card->shadow_parrallax->x / 30;
        }
        lua_sort(cards, by_x);
    }
    int k = 1;
    for (auto &c : cards) c->rank = k++;
    if (Moveable *vd = dynamic_cast<Moveable *>(children.get("view_deck"))) {
        RoleArgs ra;
        ra.major = cards.empty() ? (Moveable *)this : cards[0].get();
        vd->set_role(ra);
    }
}

void CardArea::hard_set_T(float x, float y, float w, float h) {
    Moveable::hard_set_T(x, y, w, h);
    calculate_parrallax();
    align_cards();
    hard_set_cards();
}

void CardArea::hard_set_cards() {
    for (auto &card : std::vector<CardRef>(cards)) {
        card->hard_set_T();
        card->calculate_parrallax();
    }
}

void CardArea::shuffle(const std::string &seed) {
    pseudoshuffle(cards, pseudoseed(seed));
    set_ranks();
}

void CardArea::sort(const char *method) {
    if (method) config->set("sort", method);
    std::string s = config->get(K("sort")).to_string();
    if (s == "desc") lua_sort(cards, [](const CardRef &a, const CardRef &b) { return a->get_nominal() > b->get_nominal(); });
    else if (s == "asc") lua_sort(cards, [](const CardRef &a, const CardRef &b) { return a->get_nominal() < b->get_nominal(); });
    else if (s == "suit desc")
        lua_sort(cards, [](const CardRef &a, const CardRef &b) { return a->get_nominal("suit") > b->get_nominal("suit"); });
    else if (s == "suit asc")
        lua_sort(cards, [](const CardRef &a, const CardRef &b) { return a->get_nominal("suit") < b->get_nominal("suit"); });
    else if (s == "order") {
        auto ord = [](const CardRef &c) {
            Value o = fld(c->config->get(K("card")), "order");
            return o.truthy() ? o.num() : fld(c->config->get(K("center")), "order").num();
        };
        lua_sort(cards, [&](const CardRef &a, const CardRef &b) { return ord(a) < ord(b); });
    }
}

bool CardArea::draw_card_from(CardArea *from, bool stay_flipped, bool discarded_only) {
    if (!from) return false;
    bool is_deck = G.deck && this == G.deck.get(), is_hand = G.hand && this == G.hand.get();
    if ((double)cards.size() < config->get(K("card_limit")).num() || is_deck || is_hand) {
        Card *card = from->remove_card(nullptr, discarded_only);
        if (card) {
            CardRef keep(card);
            if (G.discard && from == G.discard.get()) card->T->r = 0;
            (void)stay_flipped;
            bool sf = false;  // TODO(run): G.GAME.blind:stay_flipped(self, card) and flipped_cards modifier
            emplace(card, nullptr, sf);
            return true;
        }
    }
    return false;
}

void CardArea::click() {
    if (G.deck && this == G.deck.get()) {
        auto it = G.FUNCS.find("deck_info");
        if (it != G.FUNCS.end()) it->second(nullptr);
    }
}

void CardArea::remove() {
    Ref<CardArea> keep(this);
    for (int i = (int)cards.size() - 1; i >= 0; i--) {
        CardRef c = cards[i];
        cards.erase(cards.begin() + i);
        remove_all(c->children);
        c->remove();
    }
    cards.clear();
    cards_nil = true;
    remove_all(children);
    G.I.CARDAREA.remove(this);
    Moveable::remove();
}

// ================================================================== Back
Back::Back(const Value &selected) {
    Value sb = selected.truthy() ? selected : G.P_CENTERS->get(K("b_red"));
    name = fld(sb, "name").truthy() ? fld(sb, "name").to_string() : "Red Deck";
    Value cfg = fld(sb, "config");
    effect = vl::T({{"center", sb}, {"text_UI", ""}, {"config", cfg.is_table() ? Value(cfg.table()->deep_copy()) : Value()}});
    loc_name = localize(vl::T({{"type", "name_text"}, {"set", "Back"}, {"key", fld(sb, "key")}}));
    Value p = fld(sb, "unlocked").truthy() ? fld(sb, "pos") : Value(vl::T({{"x", 4}, {"y", 0}}));
    pos = vl::T({{"x", fld(p, "x")}, {"y", fld(p, "y")}});
}

Value Back::get_name() const {
    if (fld(effect->get(K("center")), "unlocked").truthy()) return loc_name;
    return localize("k_locked");
}

void Back::change_to(const Value &new_back_in) {
    Value nb = new_back_in.truthy() ? new_back_in : G.P_CENTERS->get(K("b_red"));
    name = fld(nb, "name").truthy() ? fld(nb, "name").to_string() : "Red Deck";
    Value cfg = fld(nb, "config");
    effect = vl::T({{"center", nb}, {"text_UI", ""}, {"config", cfg.is_table() ? Value(cfg.table()->deep_copy()) : Value()}});
    loc_name = localize(vl::T({{"type", "name_text"}, {"set", "Back"}, {"key", fld(nb, "key")}}));
    Value p = fld(nb, "unlocked").truthy() ? fld(nb, "pos") : Value(vl::T({{"x", 4}, {"y", 0}}));
    pos->set("x", fld(p, "x"));
    pos->set("y", fld(p, "y"));
}

void Back::apply_to_run() {
    Table *cfg = effect->get(K("config")).table();
    if (!cfg) return;
    Table *game = G.GAME.get(), *sp = game->get(K("starting_params")).table();
    auto c = [&](const char *k) { return cfg->get(Value(intern(k))); };
    if (c("voucher").truthy()) {
        game->get(K("used_vouchers")).table()->set(c("voucher"), true);
        game->set("starting_voucher_count", game->get(K("starting_voucher_count")).num() + 1);
        card_apply_to_run(nullptr, G.P_CENTERS->get(c("voucher")));
    }
    if (c("hands").truthy()) sp->set("hands", sp->get(K("hands")).num() + c("hands").num());
    if (c("consumables").truthy()) {
        delay(0.4);
        Value list = c("consumables");
        G.E_MANAGER->add_event(ev_immediate([list] {
            for (int i = 1; i <= list.table()->len(); i++) {
                Card *card = create_card("Tarot", G.consumeables.get(), false, Value(), false, false, list.table()->get(i).to_string(), "deck");
                card->add_to_deck();
                G.consumeables->emplace(card);
            }
            return true;
        }));
    }
    if (c("dollars").truthy()) sp->set("dollars", sp->get(K("dollars")).num() + c("dollars").num());
    if (c("remove_faces").truthy()) sp->set("no_faces", true);
    if (c("spectral_rate").truthy()) game->set("spectral_rate", c("spectral_rate"));
    if (c("discards").truthy()) sp->set("discards", sp->get(K("discards")).num() + c("discards").num());
    if (c("reroll_discount").truthy()) sp->set("reroll_cost", sp->get(K("reroll_cost")).num() - c("reroll_discount").num());
    if (c("edition").truthy()) {
        const Value ed = c("edition"), count = c("edition_count");
        G.E_MANAGER->add_event(ev_immediate([ed, count] {
            int i = 0;
            TableRef pcs = new_table();
            for (auto &pc : G.playing_cards) pcs->push(Value((Object *)pc.get()));
            while (i < count.num()) {
                double seed = pseudoseed("edition_deck");
                Card *card = pseudorandom_element(Value(pcs), &seed).as<Card>();
                if (!card->edition) {
                    i++;
                    TableRef e = new_table();
                    e->set(ed, true);
                    card->set_edition(Value(e), false, true);
                }
            }
            return true;
        }));
    }
    if (Table *vs = c("vouchers").table())
        vs->each([&](const Value &, const Value &v) {
            game->get(K("used_vouchers")).table()->set(v, true);
            game->set("starting_voucher_count", game->get(K("starting_voucher_count")).num() + 1);
            card_apply_to_run(nullptr, G.P_CENTERS->get(v));
        });
    if (name == "Checkered Deck")
        G.E_MANAGER->add_event(ev_immediate([] {
            for (auto &v : std::vector<CardRef>(G.playing_cards)) {
                if (v->base->get(K("suit")).equals("Clubs")) v->change_suit("Spades");
                if (v->base->get(K("suit")).equals("Diamonds")) v->change_suit("Hearts");
            }
            return true;
        }));
    if (c("randomize_rank_suit").truthy()) sp->set("erratic_suits_and_ranks", true);
    if (c("joker_slot").truthy()) sp->set("joker_slots", sp->get(K("joker_slots")).num() + c("joker_slot").num());
    if (c("hand_size").truthy()) sp->set("hand_size", sp->get(K("hand_size")).num() + c("hand_size").num());
    if (c("ante_scaling").truthy()) sp->set("ante_scaling", c("ante_scaling"));
    if (c("consumable_slot").truthy()) sp->set("consumable_slots", sp->get(K("consumable_slots")).num() + c("consumable_slot").num());
    Table *mods = game->get(K("modifiers")).table();
    if (c("no_interest").truthy()) mods->set("no_interest", true);
    if (c("extra_hand_bonus").truthy()) mods->set("money_per_hand", c("extra_hand_bonus"));
    if (c("extra_discard_bonus").truthy()) mods->set("money_per_discard", c("extra_discard_bonus"));
}

void Back::trigger_effect_final_scoring(double &chips, double &mult) {
    if (name != "Plasma Deck") return;
    const double tot = chips + mult;
    chips = std::floor(tot / 2);
    mult = std::floor(tot / 2);
    update_hand_text(vl::T({{"delay", 0}}), vl::T({{"mult", mult}, {"chips", chips}}));
    G.E_MANAGER->add_event(ev_immediate([] {
        Value text = localize("k_balanced");
        play_sound("gong", 0.94f, 0.3f);
        play_sound("gong", 0.94f * 1.5f, 0.2f);
        play_sound("tarot1", 1.5f);
        ease_colour(TableRef(G.colour("UI_CHIPS").table()), Value(A({0.8, 0.45, 0.85, 1})));
        ease_colour(TableRef(G.colour("UI_MULT").table()), Value(A({0.8, 0.45, 0.85, 1})));
        attention_text(vl::T({{"scale", 1.4}, {"text", text}, {"hold", 2}, {"align", "cm"}, {"offset", Value(vl::T({{"x", 0}, {"y", -2.7}}))},
                              {"major", Value((Object *)G.play.get())}}));
        EventConfig e = ev_after(4.3, [] {
            ease_colour(TableRef(G.colour("UI_CHIPS").table()), G.colour("BLUE"), 2);
            ease_colour(TableRef(G.colour("UI_MULT").table()), G.colour("RED"), 2);
            return true;
        });
        e.blockable = false;
        e.blocking = false;
        G.E_MANAGER->add_event(e);
        EventConfig e2 = ev_after(6.3, [] {
            Table *uc = G.colour("UI_CHIPS").table(), *um = G.colour("UI_MULT").table();
            Table *b = G.colour("BLUE").table(), *r = G.colour("RED").table();
            for (int i = 1; i <= 4; i++) {
                uc->set(i, b->get(i));
                um->set(i, r->get(i));
            }
            return true;
        });
        e2.blockable = false;
        e2.blocking = false;
        e2.no_delete = true;
        G.E_MANAGER->add_event(e2);
        return true;
    }));
    delay(0.6);
}

}  // namespace vl
