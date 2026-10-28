// The run setup screen (UI_definitions.lua G.UIDEF.run_setup / run_setup_option / stake_option /
// viewed_stake_option / stake_description / deck_stake_column, back.lua Back:generate_UI) and its
// callbacks (button_callbacks.lua setup_run, start_setup_run, change_viewed_back, change_stake,
// RUN_SETUP_check_*, toggle_seeded_run, can_start_run, can_continue).
#include <algorithm>

#include "engine/text.h"
#include "engine/ui.h"
#include "game/button_callbacks.h"
#include "game/card.h"
#include "game/card_ui.h"
#include "game/functions.h"
#include "game/g.h"
#include "game/state_events.h"
#include "game/ui_definitions.h"

namespace vl {

namespace {
inline Value fld(const Value &t, const char *k) { return t.get(Value(intern(k))); }
Value node(int uit, const TableRef &cfg, const TableRef &nodes = nullptr) {
    TableRef t = vl::T({{"n", (double)uit}, {"config", Value(cfg)}});
    if (nodes) t->set("nodes", Value(nodes));
    return Value(t);
}
Value col(const char *n) { return G.colour(n); }
Value ui_col(const char *n) { return fld(G.colour("UI"), n); }
Value obj(Object *o) { return Value(o); }
Table *profile() { return G.PROFILES->get(G.SETTINGS->get(K("profile"))).table(); }
Table *memory() {
    Table *p = profile();
    if (!p->get(K("MEMORY")).is_table()) p->set("MEMORY", Value(new_table()));
    return p->get(K("MEMORY")).table();
}
// G.viewed_stake, G.run_setup_seed, G.setup_seed... live on G (G_TABLE: UI ref_tables point at G)
Value gget(const char *k) { return G.G_TABLE->get(Value(intern(k))); }
void gset(const char *k, const Value &v) { G.G_TABLE->set(k, v); }
int viewed_stake() { return gget("viewed_stake").truthy() ? (int)gget("viewed_stake").num() : 1; }
Back *viewed_back() { return G.GAME->get(K("viewed_back")).as<Back>(); }
Value viewed_center() { return viewed_back() ? viewed_back()->effect->get(K("center")) : Value(); }
int max_stake_of_viewed() {
    int m = get_deck_win_stake(fld(viewed_center(), "key"));
    if (profile()->get(K("all_unlocked")).truthy()) m = 8;
    return m;
}
Func *func(std::function<Value(const Value &)> f) { return new Func(std::move(f)); }
// e.config.object:remove(); e.config.object = UIBox{definition, config = {offset 0, align, parent = e}}
void replace_object(UIElement *e, const TableRef &def, const char *align) {
    if (Node *o = e->config->get(K("object")).as<Node>()) o->remove();
    TableRef cfg = vl::T({{"offset", Value(vl::T({{"x", 0}, {"y", 0}}))}, {"parent", obj(e)}});
    if (align) cfg->set("align", align);
    e->config->set("object", obj(new UIBox(def, cfg)));
}
}  // namespace

// simple_text_container(_loc, args)
Value simple_text_container(const char *loc, const TableRef &args) {
    Value r = localize(loc);
    if (!r.is_table()) return Value();
    TableRef container = new_table();
    auto a = [&](const char *k) { return args ? args->get(Value(intern(k))) : Value(); };
    for (int k = 1; k <= r.table()->len(); k++)
        container->push(node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}}),
                             A({node(UIT_T, vl::T({{"text", r.table()->get(k)},
                                                   {"scale", a("scale").truthy() ? a("scale") : Value(0.35)},
                                                   {"colour", a("colour").truthy() ? a("colour") : ui_col("TEXT_DARK")},
                                                   {"shadow", a("shadow")}}))})));
    return node(a("col").truthy() ? UIT_C : UIT_R, vl::T({{"align", "cm"}, {"padding", a("padding").truthy() ? a("padding") : Value(0.03)}}), container);
}

// misc_functions.lua get_deck_win_sticker(_center)
Value get_deck_win_sticker(const Value &center) {
    const Value du = fld(profile()->get(K("deck_usage")), fld(center, "key").to_string().c_str());
    Table *wins = fld(du, "wins").table();
    if (!wins) return Value();
    int w = -1;
    wins->each([&](const Value &k, const Value &) {
        if (k.is_num()) w = std::max(w, (int)k.num());
    });
    if (w > 0 && w <= (int)G.sticker_map.size()) return Value(G.sticker_map[w - 1]);
    return Value();
}

// Back:generate_UI(other, ui_scale, min_dims, challenge)
TableRef Back::generate_UI(const Value &other, double, double min_dims) {
    const Value bc = other.truthy() ? other : effect->get(K("center"));
    const std::string name_to_check = other.truthy() ? fld(other, "name").to_string() : name;
    const Value ec = other.truthy() ? fld(other, "config") : effect->get(K("config"));
    TableRef loc_nodes = new_table();
    Value loc_args;
    auto name_text = [](const char *set, const Value &key) { return localize(vl::T({{"type", "name_text"}, {"set", set}, {"key", key}})); };
    auto desc = [&](const char *set, const Value &key) {
        localize(vl::T({{"type", "descriptions"}, {"key", key}, {"set", set}, {"nodes", Value(loc_nodes)}, {"vars", loc_args}}));
    };
    if (!fld(bc, "unlocked").truthy()) {
        const Value uc = fld(bc, "unlock_condition");
        const Value ut = fld(uc, "type");
        if (!uc.truthy() || fld(bc, "demo").truthy()) {
            desc("Other", Value("demo_locked"));
        } else if (ut.equals("win_deck")) {
            Value other_name = localize("k_unknown");
            if (fld(G.P_CENTERS->get(fld(uc, "deck")), "unlocked").truthy()) other_name = name_text("Back", fld(uc, "deck"));
            loc_args = Value(A({other_name}));
            desc("Other", Value("deck_locked_win"));
        } else if (ut.equals("discover_amount")) {
            loc_args = Value(A({Value(fld(uc, "amount").to_string())}));
            desc("Other", Value("deck_locked_discover"));
        } else if (ut.equals("win_stake")) {
            const int st = (int)fld(uc, "stake").num();
            TableRef la = A({name_text("Stake", fld(G.P_CENTER_POOLS->get(K("Stake")).get(st), "key"))});
            la->set("colours", Value(A({get_stake_col(st)})));
            loc_args = Value(la);
            desc("Other", Value("deck_locked_stake"));
        }
    } else {
        auto c = [&](const char *k) { return fld(ec, k); };
        if (name_to_check == "Blue Deck") loc_args = Value(A({c("hands")}));
        else if (name_to_check == "Red Deck") loc_args = Value(A({c("discards")}));
        else if (name_to_check == "Yellow Deck") loc_args = Value(A({c("dollars")}));
        else if (name_to_check == "Green Deck") loc_args = Value(A({c("extra_hand_bonus"), c("extra_discard_bonus")}));
        else if (name_to_check == "Black Deck") loc_args = Value(A({c("joker_slot"), Value(-c("hands").num())}));
        else if (name_to_check == "Magic Deck") loc_args = Value(A({name_text("Voucher", Value("v_crystal_ball")), name_text("Tarot", Value("c_fool"))}));
        else if (name_to_check == "Nebula Deck") loc_args = Value(A({name_text("Voucher", Value("v_telescope")), Value(-1)}));
        else if (name_to_check == "Zodiac Deck")
            loc_args = Value(A({name_text("Voucher", Value("v_tarot_merchant")), name_text("Voucher", Value("v_planet_merchant")),
                                name_text("Voucher", Value("v_overstock_norm"))}));
        else if (name_to_check == "Painted Deck") loc_args = Value(A({c("hand_size"), c("joker_slot")}));
        else if (name_to_check == "Anaglyph Deck") loc_args = Value(A({name_text("Tag", Value("tag_double"))}));
        else if (name_to_check == "Plasma Deck") loc_args = Value(A({c("ante_scaling")}));
        desc("Back", fld(bc, "key"));
    }
    // (the Challenge Deck's button belongs to the challenges screen)
    return TableRef(node(UIT_ROOT, vl::T({{"align", "cm"}, {"minw", min_dims * 5}, {"minh", min_dims * 2.5}, {"id", Value(name)}, {"colour", col("CLEAR")}}),
                         A({desc_from_rows(Value(loc_nodes), true, Value(min_dims * 5))}))
                        .table());
}

// G.UIDEF.deck_stake_column(_deck_key)
TableRef deck_stake_column(const Value &deck_key) {
    const Value du = fld(profile()->get(K("deck_usage")), deck_key.to_string().c_str());
    const Value wins = fld(du, "wins");
    const bool all = profile()->get(K("all_unlocked")).truthy();
    TableRef stake_col = new_table();
    bool valid = false;
    const int n = G.P_CENTER_POOLS->get(K("Stake")).table()->len();
    for (int i = n; i >= 1; i--) {
        const double w = wins.get(i).num();
        if ((du.truthy() && wins.get(i - 1).truthy()) || i == 1 || all) valid = true;
        stake_col->push(node(UIT_R,
                             vl::T({{"id", i}, {"align", "cm"}, {"colour", w > 0 ? col("GREY") : col("CLEAR")}, {"outline", 0}, {"outline_colour", col("WHITE")}, {"r", 0.1},
                                    {"minh", 0.25}, {"minw", valid ? 0.45 : 0.25}, {"func", "RUN_SETUP_check_back_stake_highlight"}}),
                             A({node(UIT_R, vl::T({{"align", "cm"}, {"minh", valid ? 0.17 : 0.13}, {"minw", valid ? 0.37 : 0.13},
                                                   {"colour", w > 0 ? get_stake_col(i) : ui_col("TRANSPARENT_LIGHT")}, {"r", 0.1}}),
                                     new_table())})));
        if (i > 1) stake_col->push(node(UIT_R, vl::T({{"align", "cm"}, {"minh", 0.1}, {"minw", 0.04}}), new_table()));
    }
    return TableRef(node(UIT_ROOT, vl::T({{"align", "cm"}, {"colour", col("CLEAR")}}), stake_col).table());
}

// G.UIDEF.stake_description(_stake)
Value stake_description(int stake) {
    const Value sc = G.P_CENTER_POOLS->get(K("Stake")).get(stake);
    TableRef ret = new_table();
    if (sc.truthy()) localize(vl::T({{"type", "descriptions"}, {"key", fld(sc, "key")}, {"set", fld(sc, "set")}, {"nodes", Value(ret)}}));
    TableRef desc_t = new_table();
    for (int k = 1; k <= ret->len(); k++) desc_t->push(node(UIT_R, vl::T({{"align", "cm"}, {"maxw", 5.3}}), TableRef(ret->get(k).table())));
    return node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.05}, {"r", 0.1}, {"colour", col("L_BLACK")}}),
                A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}}),
                        A({node(UIT_T, vl::T({{"text", localize(vl::T({{"type", "name_text"}, {"key", fld(sc, "key")}, {"set", fld(sc, "set")}}))},
                                              {"scale", 0.35}, {"colour", col("WHITE")}}))})),
                   node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.03}, {"colour", col("WHITE")}, {"r", 0.1}, {"minh", 1}, {"minw", 5.5}}), desc_t)}));
}

// G.UIDEF.viewed_stake_option()
TableRef viewed_stake_option() {
    gset("viewed_stake", (double)std::min(max_stake_of_viewed() + 1, viewed_stake()));
    // (the original's `_type ~= 'Continue'` reads an undefined global: always true)
    memory()->set("stake", (double)viewed_stake());
    Sprite *stake_sprite = get_stake_sprite(viewed_stake(), 1);
    return TableRef(node(UIT_ROOT, vl::T({{"align", "cm"}, {"colour", col("BLACK")}, {"r", 0.1}}),
                         A({node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0}}),
                                 A({node(UIT_T, vl::T({{"text", localize("k_stake")}, {"scale", 0.4}, {"colour", col("L_BLACK")}, {"vert", true}}))})),
                            node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.1}}),
                                 A({node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0}}),
                                         A({node(UIT_O, vl::T({{"colour", col("BLUE")}, {"object", obj(stake_sprite)}, {"hover", true}, {"can_collide", false}}))})),
                                    stake_description(viewed_stake())}))}))
                        .table());
}

// G.UIDEF.stake_option(_type)
TableRef stake_option(const std::string &type) {
    Value middle = node(UIT_R, vl::T({{"align", "cm"}, {"minh", 1.7}, {"minw", 7.3}}),
                        A({node(UIT_O, vl::T({{"func", "RUN_SETUP_check_stake2"}, {"object", obj(new Moveable(0, 0, 0, 0))}}))}));
    TableRef options = new_table();
    for (int i = 1; i <= std::min(max_stake_of_viewed() + 1, 8); i++) options->push(Value((double)i));
    Value content = type == "Continue" ? middle
                                        : Value(create_option_cycle(vl::T({{"options", Value(options)}, {"opt_callback", "change_stake"}, {"current_option", (double)viewed_stake()},
                                                                           {"colour", col("RED")}, {"w", 6}, {"mid", middle}})));
    return TableRef(node(UIT_ROOT, vl::T({{"align", "tm"}, {"colour", col("CLEAR")}, {"minh", 2.03}, {"minw", 8.3}}), A({content})).table());
}

// G.FUNCS.can_continue(e): a saved run of this version (sets G.SAVED_GAME); e (when given) turns
// inactive otherwise, and stops asking (e.config.func is cleared)
bool can_continue(UIElement *e) {
    if (e && !e->config->get(K("func")).truthy()) return false;
    bool ok = false;
    const Value savefile = G.FILES->get(Value(G.SETTINGS->get(K("profile")).to_string() + "/save.jkr"));
    if (savefile.truthy()) {
        if (!G.G_TABLE->get(K("SAVED_GAME")).truthy()) G.G_TABLE->set("SAVED_GAME", savefile);
        const Value ver = fld(G.G_TABLE->get(K("SAVED_GAME")), "VERSION");
        ok = ver.truthy() && ver.to_string() >= "0.9.2";
    }
    if (e) {
        if (!ok) {
            e->config->set("colour", ui_col("BACKGROUND_INACTIVE"));
            e->config->set("button", Value());
        }
        e->config->set("func", Value());
    }
    return ok;
}

// G.UIDEF.run_setup_option(type)
TableRef run_setup_option(const std::string &type) {
    const std::string prof = G.SETTINGS->get(K("profile")).to_string();
    if (!G.G_TABLE->get(K("SAVED_GAME")).truthy()) G.G_TABLE->set("SAVED_GAME", G.FILES->get(Value(std::to_string(G.focused_profile) + "/save.jkr")));
    G.SETTINGS->set("current_setup", Value(type));
    G.GAME->set("viewed_back", obj(new Back(get_deck_from_name(memory()->get(K("deck"))))));
    if (!memory()->get(K("stake")).truthy()) memory()->set("stake", 1);
    const Value saved = G.G_TABLE->get(K("SAVED_GAME"));
    if (type == "Continue") {
        gset("viewed_stake", 1);
        if (saved.truthy()) {
            Value viewed_deck("b_red");
            const Value bn = fld(fld(saved, "BACK"), "name");
            G.P_CENTERS->each([&](const Value &k, const Value &v) {
                if (fld(v, "name") == bn) viewed_deck = k;
            });
            viewed_back()->change_to(G.P_CENTERS->get(viewed_deck));
            const Value st = fld(fld(saved, "GAME"), "stake");
            gset("viewed_stake", st.truthy() ? st : Value(1));
        }
    }
    UIBox *ov = dynamic_cast<UIBox *>(G.OVERLAY_MENU.get());
    UIElement *seed_toggle = ov ? ov->get_UIE_by_ID("run_setup_seed") : nullptr;
    if (type == "New Run") {
        if (seed_toggle) seed_toggle->states.visible = true;
        gset("viewed_stake", memory()->get(K("stake")).truthy() ? memory()->get(K("stake")) : Value(1));
        auto it = G.FUNCS_T.find("change_stake");
        if (it != G.FUNCS_T.end()) it->second(vl::T({{"to_key", gget("viewed_stake")}}));
    } else {
        gset("run_setup_seed", Value());
        if (seed_toggle) seed_toggle->states.visible = false;
    }

    // the deck preview: 10 face-down cards in a thin deck
    CardArea *area = new CardArea(G.ROOM->T->x + 0.2f * G.ROOM->T->w / 2, G.ROOM->T->h, G.CARD_W, G.CARD_H,
                                  vl::T({{"card_limit", 5}, {"type", "deck"}, {"highlight_limit", 0}, {"deck_height", 0.75}, {"thin_draw", 1}}));
    for (int i = 1; i <= 10; i++) {
        Card *card = new Card(G.ROOM->T->x + 0.2f * G.ROOM->T->w / 2, G.ROOM->T->h, G.CARD_W, G.CARD_H, pseudorandom_element(Value(G.P_CARDS)),
                              G.P_CENTERS->get(K("c_base")), vl::T({{"playing_card", i}, {"viewed_back", true}}));
        card->sprite_facing = "back";
        card->facing = "back";
        area->emplace(card);
        if (i == 10) {
            G.sticker_card = card;
            card->sticker = get_deck_win_sticker(viewed_center());
        }
    }

    TableRef ordered_names = new_table();
    int viewed_deck = 1;
    Table *backs = G.P_CENTER_POOLS->get(K("Back")).table();
    for (int k = 1; k <= backs->len(); k++) {
        ordered_names->push(fld(backs->get(k), "name"));
        if (fld(backs->get(k), "name").equals(viewed_back()->name)) viewed_deck = k;
    }
    const double lwidth = 1.4, rwidth = 1.8, scale = 0.39;
    gset("setup_seed", "");

    auto back_name_slot = [] {
        return node(UIT_R, vl::T({{"align", "cm"}, {"r", 0.1}, {"minw", 4}, {"maxw", 4}, {"minh", 0.6}}),
                    A({node(UIT_O, vl::T({{"func", "RUN_SETUP_check_back_name"}, {"object", obj(new Moveable(0, 0, 0, 0))}}))}));
    };
    auto stake_column = [&] {
        return node(UIT_C, vl::T({{"align", "cm"}}),
                    A({node(UIT_O, vl::T({{"id", Value(viewed_back()->name)},
                                          {"func", "RUN_SETUP_check_back_stake_column"},
                                          {"object", obj(new UIBox(deck_stake_column(fld(viewed_center(), "key")), vl::T({{"offset", Value(vl::T({{"x", 0}, {"y", 0}}))}})))}}))}));
    };
    Value top;
    if (type == "Continue") {
        const Value sg = fld(saved, "GAME");
        auto stat = [&](const char *label, const Value &text, const char *colour, double s) {
            return node(UIT_R, vl::T({{"align", "cm"}}),
                        A({node(UIT_C, vl::T({{"align", "cm"}, {"minw", lwidth}, {"maxw", lwidth}}),
                                A({node(UIT_T, vl::T({{"text", localize(label)}, {"colour", ui_col("TEXT_DARK")}, {"scale", scale * 0.8}}))})),
                           node(UIT_C, vl::T({{"align", "cm"}}), A({node(UIT_T, vl::T({{"text", ": "}, {"colour", ui_col("TEXT_DARK")}, {"scale", scale * 0.8}}))})),
                           node(UIT_C, vl::T({{"align", "cl"}, {"minw", rwidth}, {"maxw", lwidth}}),
                                A({node(UIT_T, vl::T({{"text", text}, {"colour", col(colour)}, {"scale", s}}))}))}));
        };
        const Value best = fld(fld(fld(sg, "round_scores"), "hand"), "amt");
        TableRef stats = A({stat("k_round", Value(fld(sg, "round").to_string()), "RED", 0.8 * scale),
                            stat("k_ante", Value(fld(fld(sg, "round_resets"), "ante").to_string()), "BLUE", 0.8 * scale),
                            stat("k_money", Value(localize("$").to_string() + fld(sg, "dollars").to_string()), "ORANGE", 0.8 * scale),
                            stat("k_best_hand", Value(number_format(best)), "RED", scale_number(best, 0.8 * scale))});
        if (fld(sg, "seeded").truthy()) stats->push(stat("k_seed", Value(fld(fld(sg, "pseudorandom"), "seed").to_string()), "RED", 0.8 * scale));
        top = node(UIT_R, vl::T({{"align", "tm"}, {"minh", 3.8}, {"padding", 0.15}}),
                   A({node(UIT_R, vl::T({{"align", "cm"}, {"minh", 3.3}, {"minw", 6.8}}),
                           A({node(UIT_C, vl::T({{"align", "cm"}, {"colour", col("BLACK")}, {"padding", 0.15}, {"r", 0.1}, {"emboss", 0.05}}),
                                   A({node(UIT_C, vl::T({{"align", "cm"}}), A({node(UIT_R, vl::T({{"align", "cm"}, {"shadow", false}}), A({node(UIT_O, vl::T({{"object", obj(area)}}))}))})),
                                      node(UIT_C, vl::T({{"align", "cm"}, {"minw", 4}, {"maxw", 4}, {"minh", 1.7}, {"r", 0.1}, {"colour", col("L_BLACK")}, {"padding", 0.1}}),
                                           A({back_name_slot(),
                                              node(UIT_R, vl::T({{"align", "cm"}, {"colour", col("WHITE")}, {"padding", 0.03}, {"minh", 1.75}, {"r", 0.1}}), stats)})),
                                      stake_column()}))}))}));
    } else {
        Value mid = node(
            UIT_R, vl::T({{"align", "cm"}, {"minh", 3.3}, {"minw", 5}}),
            A({node(UIT_C, vl::T({{"align", "cm"}, {"colour", col("BLACK")}, {"padding", 0.15}, {"r", 0.1}, {"emboss", 0.05}}),
                    A({node(UIT_C, vl::T({{"align", "cm"}}), A({node(UIT_R, vl::T({{"align", "cm"}, {"shadow", false}}), A({node(UIT_O, vl::T({{"object", obj(area)}}))}))})),
                       node(UIT_C, vl::T({{"align", "cm"}, {"minh", 1.7}, {"r", 0.1}, {"colour", col("L_BLACK")}, {"padding", 0.1}}),
                            A({back_name_slot(),
                               node(UIT_R, vl::T({{"align", "cm"}, {"colour", col("WHITE")}, {"minh", 1.7}, {"r", 0.1}}),
                                    A({node(UIT_O, vl::T({{"id", Value(viewed_back()->name)},
                                                          {"func", "RUN_SETUP_check_back"},
                                                          {"object", obj(new UIBox(viewed_back()->generate_UI(), vl::T({{"offset", Value(vl::T({{"x", 0}, {"y", 0}}))}})))}}))}))})),
                       stake_column()}))}));
        top = node(UIT_R, vl::T({{"align", "cm"}, {"minh", 3.8}}),
                   A({Value(create_option_cycle(vl::T({{"options", Value(ordered_names)}, {"opt_callback", "change_viewed_back"}, {"current_option", (double)viewed_deck},
                                                       {"colour", col("RED")}, {"w", 3.5}, {"mid", mid}})))}));
    }
    Value stake_slot = node(UIT_O, vl::T({{"func", "RUN_SETUP_check_stake"}, {"insta_func", true}, {"object", obj(new Moveable(0, 0, 0, 0))}}));
    Value stake_row = type == "Continue" ? node(UIT_R, vl::T({{"align", "cm"}, {"minh", 2.2}, {"minw", 5}}),
                                                A({node(UIT_R, vl::T({{"align", "cm"}, {"minh", 0.17}}), new_table()), node(UIT_R, vl::T({{"align", "cm"}}), A({stake_slot}))}))
                                         : node(UIT_R, vl::T({{"align", "cm"}, {"minh", 2.2}, {"minw", 6.8}}), A({stake_slot}));
    TableRef seed_col = new_table();
    if (type == "New Run")
        seed_col->push(Value(create_toggle(vl::T({{"col", true}, {"label", localize("k_seeded_run")}, {"label_scale", 0.25}, {"w", 0}, {"scale", 0.7},
                                                  {"ref_table", Value(G.G_TABLE)}, {"ref_value", "run_setup_seed"}}))));
    return TableRef(
        node(UIT_ROOT, vl::T({{"align", "cm"}, {"colour", col("CLEAR")}, {"minh", 6.6}, {"minw", 6}}),
             A({top, node(UIT_R, vl::T({{"align", "cm"}}), A({stake_row})),
                node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.05}, {"minh", 0.9}}),
                     A({node(UIT_O, vl::T({{"align", "cm"}, {"func", "toggle_seeded_run"}, {"object", obj(new Moveable(0, 0, 0, 0))}}), new_table())})),
                node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}}),
                     A({node(UIT_C, vl::T({{"align", "cm"}, {"minw", 2.4}, {"id", "run_setup_seed"}}), seed_col),
                        node(UIT_C, vl::T({{"align", "cm"}, {"minw", 5}, {"minh", 0.8}, {"padding", 0.2}, {"r", 0.1}, {"hover", true}, {"colour", col("BLUE")},
                                           {"button", "start_setup_run"}, {"shadow", true}, {"func", "can_start_run"}}),
                             A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}}),
                                     A({node(UIT_T, vl::T({{"text", localize("b_play_cap")}, {"scale", 0.8}, {"colour", ui_col("TEXT_LIGHT")}, {"func", "set_button_pip"},
                                                           {"focus_args", Value(vl::T({{"button", "x"}, {"set_button_pip", true}}))}}))}))})),
                        node(UIT_C, vl::T({{"align", "cm"}, {"minw", 2.5}}), new_table())}))}))
            .table());
}

// the Challenges tab (challenges.cpp)
TableRef challenges_tab(bool from_game_over, bool chosen);

// G.UIDEF.run_setup(from_game_over)
TableRef run_setup(const std::string &from) {
    gset("run_setup_seed", Value());
    const bool challenge_chosen = from == "challenge_list";
    const bool from_game_over = !from.empty() && !challenge_chosen;
    const bool can_cont = G.MAIN_MENU_UI && can_continue(nullptr);
    auto tab = [&](const char *label, const std::string &arg, bool chosen) {
        TableRef t = vl::T({{"label", localize(label)},
                            {"chosen", chosen},
                            {"tab_definition_function", obj(func([](const Value &a) { return Value(run_setup_option(a.to_string())); }))},
                            {"tab_definition_function_args", Value(arg)}});
        return t;
    };
    TableRef tabs = A({Value(tab("b_new_run", "New Run", !challenge_chosen && !can_cont))});
    if (G.STAGE == MAIN_MENU) {
        TableRef cont = tab("b_continue", "Continue", !challenge_chosen && can_cont);
        cont->set("func", "can_continue");
        tabs->push(Value(cont));
    }
    if (TableRef ch = challenges_tab(from_game_over, challenge_chosen)) tabs->push(Value(ch));
    return create_UIBox_generic_options(vl::T({{"no_back", from_game_over},
                                               {"no_esc", from_game_over},
                                               {"contents", Value(A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}, {"draw_layer", 1}}),
                                                                          A({Value(create_tabs(vl::T({{"tabs", Value(tabs)}, {"snap_to_nav", true}})))}))}))}}));
}

void open_run_setup(const std::string &from) {
    G.SETTINGS->set("paused", true);
    overlay_menu(run_setup(from));
    if (from == "from_game_over" || from == "from_game_won")
        if (G.OVERLAY_MENU) dynamic_cast<UIBox *>(G.OVERLAY_MENU.get())->config->set("no_esc", true);
}

// create_UIBox_your_collection_decks()
TableRef create_UIBox_your_collection_decks() {
    G.GAME->set("viewed_back", Value((Object *)new Back(G.P_CENTERS->get(K("b_red")))));

    CardArea *area = new CardArea(G.ROOM->T->x + 0.2f * G.ROOM->T->w / 2, G.ROOM->T->h, 1.2f * G.CARD_W, 1.2f * G.CARD_H,
                                  vl::T({{"card_limit", 52}, {"type", "deck"}, {"highlight_limit", 0}}));
    for (int i = 1; i <= 52; i++) {
        Card *card = new Card(G.ROOM->T->x + 0.2f * G.ROOM->T->w / 2, G.ROOM->T->h, G.CARD_W * 1.2f, G.CARD_H * 1.2f, pseudorandom_element(Value(G.P_CARDS)),
                              G.P_CENTERS->get(K("c_base")), vl::T({{"playing_card", i}, {"viewed_back", true}}));
        card->sprite_facing = "back";
        card->facing = "back";
        area->emplace(card);
        if (i == 52) {
            G.sticker_card = card;
            card->sticker = get_deck_win_sticker(viewed_center());
        }
    }

    TableRef ordered_names = new_table();
    Table *backs = G.P_CENTER_POOLS->get(K("Back")).table();
    for (int k = 1; k <= backs->len(); k++) ordered_names->push(fld(backs->get(k), "name"));

    Value mid = node(
        UIT_R, vl::T({{"align", "cm"}, {"minw", 2.5}, {"padding", 0.1}, {"r", 0.1}, {"colour", col("BLACK")}, {"emboss", 0.05}}),
        A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.2}, {"colour", col("BLACK")}, {"r", 0.2}}),
                A({node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0}}), A({node(UIT_O, vl::T({{"object", obj(area)}}))})),
                   node(UIT_C, vl::T({{"align", "tm"}, {"minw", 3.7}, {"minh", 2.1}, {"r", 0.1}, {"colour", col("L_BLACK")}, {"padding", 0.1}}),
                        A({node(UIT_R, vl::T({{"align", "cm"}, {"emboss", 0.1}, {"r", 0.1}, {"minw", 4}, {"maxw", 4}, {"minh", 0.6}}),
                                A({node(UIT_O, vl::T({{"func", "RUN_SETUP_check_back_name"}, {"object", obj(new Moveable(0, 0, 0, 0))}}))})),
                           node(UIT_R, vl::T({{"align", "cm"}, {"colour", col("WHITE")}, {"emboss", 0.1}, {"minh", 2.2}, {"r", 0.1}}),
                                A({node(UIT_O, vl::T({{"id", Value(viewed_back()->name)},
                                                      {"func", "RUN_SETUP_check_back"},
                                                      {"object", obj(new UIBox(viewed_back()->generate_UI(), vl::T({{"offset", Value(vl::T({{"x", 0}, {"y", 0}}))}})))}}))}))}))}))}));
    return create_UIBox_generic_options(vl::T(
        {{"back_func", "your_collection"},
         {"contents", Value(A({Value(create_option_cycle(vl::T({{"options", Value(ordered_names)}, {"opt_callback", "change_viewed_back"}, {"current_option", 1},
                                                                 {"colour", col("RED")}, {"w", 4.5}, {"focus_args", Value(vl::T({{"snap_to", true}}))}, {"mid", mid}})))}))}}));
}

void register_run_setup_callbacks() {
    auto &F = G.FUNCS;
    F["your_collection_decks"] = [](UIElement *) {
        G.SETTINGS->set("paused", true);
        overlay_menu(create_UIBox_your_collection_decks());
    };
    auto &FT = G.FUNCS_T;
    F["setup_run"] = [](UIElement *e) {
        const Value id = e ? e->config->get(K("id")) : Value();
        open_run_setup(id.equals("from_game_over") || id.equals("from_game_won") || id.equals("challenge_list") ? id.to_string() : std::string());
    };
    FT["change_viewed_back"] = [](const TableRef &args) {
        if (!gget("viewed_stake").truthy()) gset("viewed_stake", 1);
        viewed_back()->change_to(G.P_CENTER_POOLS->get(K("Back")).get(args->get(K("to_key"))));
        if (G.sticker_card) G.sticker_card->sticker = get_deck_win_sticker(viewed_center());
        gset("viewed_stake", (double)std::min(viewed_stake(), max_stake_of_viewed() + 1));
        memory()->set("deck", args->get(K("to_val")));
    };
    FT["change_stake"] = [](const TableRef &args) {
        gset("viewed_stake", args->get(K("to_key")));
        memory()->set("stake", args->get(K("to_key")));
    };
    F["RUN_SETUP_check_back"] = [](UIElement *e) {
        if (e->config->get(K("id")).equals(viewed_back()->name)) return;
        replace_object(e, viewed_back()->generate_UI(), "cm");
        e->config->set("id", Value(viewed_back()->name));
    };
    F["RUN_SETUP_check_back_name"] = [](UIElement *e) {
        if (!e->config->get(K("object")).truthy() || e->config->get(K("id")).equals(viewed_back()->name)) return;
        auto *dt = new DynaText(vl::T({{"string", viewed_back()->get_name()}, {"maxw", 4}, {"colours", Value(A({col("WHITE")}))}, {"shadow", true}, {"bump", true},
                                       {"scale", 0.5}, {"pop_in", 0}, {"silent", true}}));
        replace_object(e,
                       TableRef(node(UIT_ROOT, vl::T({{"align", "cm"}, {"colour", col("CLEAR")}}),
                                     A({node(UIT_O, vl::T({{"id", Value(viewed_back()->name)}, {"func", "RUN_SETUP_check_back_name"}, {"object", obj(dt)}}))}))
                                    .table()),
                       "cm");
        e->config->set("id", Value(viewed_back()->name));
    };
    F["RUN_SETUP_check_stake"] = [](UIElement *e) {
        if (e->config->get(K("id")).equals(viewed_back()->name)) return;
        replace_object(e, stake_option(G.SETTINGS->get(K("current_setup")).to_string()), "tmi");
        e->config->set("id", Value(viewed_back()->name));
    };
    F["RUN_SETUP_check_stake2"] = [](UIElement *e) {
        if (e->config->get(K("id")) == gget("viewed_stake")) return;
        replace_object(e, viewed_stake_option(), "cm");
        e->config->set("id", gget("viewed_stake"));
    };
    F["RUN_SETUP_check_back_stake_column"] = [](UIElement *e) {
        if (e->config->get(K("id")).equals(viewed_back()->name)) return;
        replace_object(e, deck_stake_column(fld(viewed_center(), "key")), "cm");
        e->config->set("id", Value(viewed_back()->name));
    };
    F["RUN_SETUP_check_back_stake_highlight"] = [](UIElement *e) {
        const double outline = e->config->get(K("outline")).num();
        const bool is = e->config->get(K("id")) == gget("viewed_stake");
        if (is && outline < 0.1) e->config->set("outline", 0.8);
        else if (!is && outline > 0.1) e->config->set("outline", 0);
    };
    F["can_start_run"] = [](UIElement *e) {
        const bool unlocked = fld(viewed_center(), "unlocked").truthy();
        e->config->set("colour", unlocked ? col("BLUE") : ui_col("BACKGROUND_INACTIVE"));
        e->config->set("button", unlocked ? Value("start_setup_run") : Value());
    };
    F["can_continue"] = [](UIElement *e) { can_continue(e); };
    F["start_setup_run"] = [](UIElement *e) {
        if (G.OVERLAY_MENU) exit_overlay_menu();
        const std::string setup = G.SETTINGS->get(K("current_setup")).to_string();
        const Value saved = G.G_TABLE->get(K("SAVED_GAME"));
        if (setup == "New Run") {
            if (!G.GAME || (!G.GAME->get(K("won")).truthy() && !G.GAME->get(K("seeded")).truthy())) {
                if (saved.truthy()) {
                    if (!fld(fld(saved, "GAME"), "won").truthy())
                        profile()->get(K("high_scores")).get(K("current_streak")).table()->set("amt", 0);
                    G.save_settings();
                }
            }
            Value seed = gget("run_setup_seed").truthy() ? gget("setup_seed") : gget("forced_seed");
            Value stake = gget("forced_stake").truthy() ? gget("forced_stake") : memory()->get(K("stake")).truthy() ? memory()->get(K("stake")) : Value(1);
            TableRef args = vl::T({{"stake", stake}, {"seed", seed}, {"challenge", gget("challenge_tab")}});
            start_run_with(e, args);
        } else if (setup == "Continue") {
            if (saved.truthy()) start_run_with(nullptr, vl::T({{"savetext", copy_table(saved)}}));
        }
    };
    F["toggle_seeded_run"] = [](UIElement *e) {
        const bool on = gget("run_setup_seed").truthy();
        Node *o = e->config->get(K("object")).as<Node>();
        if (o && !on) {
            o->remove();
            e->config->set("object", Value());
        } else if (!o && on) {
            TableRef def = TableRef(
                node(UIT_ROOT, vl::T({{"align", "cm"}, {"colour", col("CLEAR")}}),
                     A({node(UIT_C, vl::T({{"align", "cm"}, {"minw", 2.5}, {"padding", 0.05}}),
                             A({simple_text_container("ml_disabled_seed", vl::T({{"colour", ui_col("TEXT_LIGHT")}, {"scale", 0.26}, {"shadow", true}}))})),
                        node(UIT_C, vl::T({{"align", "cm"}, {"minw", 0.1}}),
                             A({Value(create_text_input(vl::T({{"max_length", 8}, {"all_caps", true}, {"ref_table", Value(G.G_TABLE)}, {"ref_value", "setup_seed"},
                                                               {"prompt_text", localize("k_enter_seed")}}))),
                                node(UIT_C, vl::T({{"align", "cm"}, {"minw", 0.1}}), new_table()),
                                Value(UIBox_button(vl::T({{"label", localize("ml_paste_seed")}, {"minw", 1}, {"minh", 0.6}, {"button", "paste_seed"},
                                                          {"colour", col("BLUE")}, {"scale", 0.3}, {"col", true}})))})),
                        node(UIT_C, vl::T({{"align", "cm"}, {"minw", 2.5}}), new_table())}))
                    .table());
            auto *box = new UIBox(def, vl::T({{"offset", Value(vl::T({{"x", 0}, {"y", 0}}))}, {"parent", obj(e)}, {"type", "cm"}}));
            e->config->set("object", obj(box));
            box->recalculate();
        }
    };
    // (the Vita has no system clipboard: G.F_LOCAL_CLIPBOARD's in-game one, set by copy_seed)
    F["paste_seed"] = [](UIElement *) {
        const Value clip = G.G_TABLE->get(K("CLIPBOARD"));
        if (clip.truthy()) gset("setup_seed", clip);
    };
}

}  // namespace vl
