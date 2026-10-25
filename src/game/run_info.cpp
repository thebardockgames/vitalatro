// The run's info screens (UI_definitions.lua): run info (poker hands, blinds, vouchers, stake), the
// deck view (G.UIDEF.view_deck / deck_info) and the deck preview shown while hovering the deck.
#include <algorithm>
#include <map>

#include "engine/random.h"
#include "engine/text.h"
#include "engine/ui.h"
#include "game/button_callbacks.h"
#include "game/card.h"
#include "game/card_factory.h"
#include "game/functions.h"
#include "game/g.h"
#include "game/sound.h"
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
Value gv(const char *k) { return G.GAME->get(Value(intern(k))); }
Value en() { return Value((Object *)G.LANGUAGES["en-us"].get()); }
Value str(const std::string &s) { return Value(s); }
Value num_str(double v) { return Value(number_format(Value(v))); }
const char *ui_atlas() { return G.SETTINGS->get(K("colourblind_option")).truthy() ? "ui_2" : "ui_1"; }
Func *func(std::function<Value(const Value &)> f) { return new Func(std::move(f)); }
// table.sort(G.playing_cards, function(a, b) return a:get_nominal('suit') > b:get_nominal('suit') end)
std::vector<Card *> sorted_playing_cards() {
    std::vector<Card *> cards;
    for (auto &c : G.playing_cards)
        if (c) cards.push_back(c.get());
    std::stable_sort(cards.begin(), cards.end(), [](Card *a, Card *b) { return a->get_nominal("suit") > b->get_nominal("suit"); });
    return cards;
}
// a DynaText {{string = a, colour = flip_col}, {string = b, colour = BLUE}} (the base value, then the effective one)
Value two_part(const std::string &a, const Value &ca, const std::string &b) {
    return Value(A({Value(vl::T({{"string", str(a)}, {"colour", ca}})), Value(vl::T({{"string", str(b)}, {"colour", col("BLUE")}}))}));
}
const char *suit_map[] = {"Spades", "Hearts", "Clubs", "Diamonds"};
}  // namespace

// tally_sprite(pos, value, tooltip)
Value tally_sprite(int x, int y, const Value &value_in, const Value &tooltip) {
    Value text_colour = col("BLACK");
    Value value = value_in;
    if (value.is_table() && fld(value.get(1), "string") == fld(value.get(2), "string")) {
        text_colour = fld(value.get(1), "colour").truthy() ? fld(value.get(1), "colour") : col("WHITE");
        value = fld(value.get(1), "string");
    }
    SpritePos p;
    p.x = (float)x;
    p.y = (float)y;
    auto *ts = new Sprite(0, 0, 0.5f, 0.5f, &G.ASSET_ATLAS[ui_atlas()], p);
    ts->states.drag.can = false;
    ts->states.hover.can = false;
    ts->states.collide.can = false;
    Value tip = Value(vl::T({{"text", tooltip}}));
    Value val_node = value.is_table() ? node(UIT_O, vl::T({{"object", obj(new DynaText(vl::T({{"string", value}, {"colours", Value(A({col("RED")}))}, {"scale", 0.4},
                                                                                                {"silent", true}, {"shadow", true}, {"pop_in_rate", 10}, {"pop_delay", 4}})))}}))
                                      : node(UIT_T, vl::T({{"text", value.truthy() ? value : Value("NIL")}, {"colour", text_colour}, {"scale", 0.4}, {"lang", en()}, {"shadow", true}}));
    return node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.07}, {"force_focus", true}, {"focus_args", Value(vl::T({{"type", "tally_sprite"}}))}, {"tooltip", tip}}),
                A({node(UIT_R, vl::T({{"align", "cm"}, {"r", 0.1}, {"padding", 0.04}, {"emboss", 0.05}, {"colour", col("JOKER_GREY")}}),
                        A({node(UIT_O, vl::T({{"w", 0.5}, {"h", 0.5}, {"can_collide", false}, {"object", obj(ts)}, {"tooltip", tip}}))})),
                   node(UIT_R, vl::T({{"align", "cm"}}), A({val_node}))}));
}

// G.UIDEF.view_deck(unplayed_only)
TableRef view_deck(bool unplayed_only) {
    TableRef deck_tables = new_table();
    G.VIEWING_DECK = true;
    const std::vector<Card *> cards = sorted_playing_cards();
    std::map<std::string, std::vector<Card *>> SUITS;
    for (Card *c : cards) SUITS[fld(Value(c->base), "suit").to_string()].push_back(c);
    for (const char *suit : suit_map) {
        const auto &list = SUITS[suit];
        if (list.empty()) continue;
        auto *area = new CardArea(G.ROOM->T->x + 0.2f * G.ROOM->T->w / 2, G.ROOM->T->h, 6.5f * G.CARD_W, 0.6f * G.CARD_H,
                                  vl::T({{"card_limit", (double)list.size()}, {"type", "title"}, {"view_deck", true}, {"highlight_limit", 0}, {"card_w", G.CARD_W * 0.7},
                                         {"draw_layers", Value(A({"card"}))}}));
        deck_tables->push(node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}}), A({node(UIT_O, vl::T({{"object", obj(area)}}))})));
        for (Card *c : list) {
            const bool greyed = unplayed_only && !((c->area && c->area == G.deck.get()) || c->ability->get(K("wheel_flipped")).truthy());
            Card *copy = copy_card(c, nullptr, 0.7);
            copy->greyed = greyed;
            copy->T->x = area->T->x + area->T->w / 2;
            copy->T->y = area->T->y;
            copy->hard_set_T();
            area->emplace(copy);
        }
    }

    Value flip_col = col("WHITE");
    std::map<std::string, int> suit_t, mod_suit_t;
    int rank_t[13] = {}, mod_rank_t[13] = {};
    const char *rank_name[] = {"2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K", "A"};
    int face = 0, mod_face = 0, num = 0, mod_num = 0, ace = 0, mod_ace = 0, wheel = 0;
    for (Card *v : cards) {
        if (v->ability->get(K("name")).equals("Stone Card")) continue;
        if (unplayed_only && !((v->area && v->area == G.deck.get()) || v->ability->get(K("wheel_flipped")).truthy())) continue;
        if (v->ability->get(K("wheel_flipped")).truthy() && unplayed_only) wheel++;
        suit_t[fld(Value(v->base), "suit").to_string()]++;
        for (const char *s : suit_map) mod_suit_t[s] += v->is_suit(Value(s)) ? 1 : 0;
        const int id = v->get_id();
        face += (id == 11 || id == 12 || id == 13) ? 1 : 0;
        mod_face += v->is_face() ? 1 : 0;
        if (id > 1 && id < 11) {
            num++;
            if (!v->debuff) mod_num++;
        }
        if (id == 14) {
            ace++;
            if (!v->debuff) mod_ace++;
        }
        if (id >= 2 && id <= 14) {
            rank_t[id - 2]++;
            if (!v->debuff) mod_rank_t[id - 2]++;
        }
    }
    bool modded = face != mod_face;
    for (const char *s : suit_map) modded = modded || mod_suit_t[s] != suit_t[s];
    if (wheel > 0) flip_col = Value(mix_colours(col("FILTER"), col("WHITE"), 0.7f));

    TableRef rank_cols = new_table();
    for (int i = 12; i >= 0; i--) {
        const bool delta = mod_rank_t[i] != rank_t[i];
        Value count = delta ? node(UIT_O, vl::T({{"object", obj(new DynaText(vl::T({{"string", two_part(std::to_string(rank_t[i]), flip_col, std::to_string(mod_rank_t[i]))},
                                                                                      {"colours", Value(A({col("RED")}))},
                                                                                      {"scale", 0.4},
                                                                                      {"y_offset", -2},
                                                                                      {"silent", true},
                                                                                      {"shadow", true},
                                                                                      {"pop_in_rate", 10},
                                                                                      {"pop_delay", 4}})))}}))
                            : node(UIT_T, vl::T({{"text", str(std::to_string(rank_t[i]))}, {"lang", en()}, {"colour", flip_col}, {"scale", 0.45}, {"shadow", true}}));
        rank_cols->push(node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.07}}),
                             A({node(UIT_C, vl::T({{"align", "cm"}, {"r", 0.1}, {"padding", 0.04}, {"emboss", 0.04}, {"minw", 0.5}, {"colour", col("L_BLACK")}}),
                                     A({node(UIT_T, vl::T({{"text", rank_name[i]}, {"lang", en()}, {"colour", col("JOKER_GREY")}, {"scale", 0.35}, {"shadow", true}}))})),
                                node(UIT_C, vl::T({{"align", "cr"}, {"minw", 0.4}}), A({count}))})));
    }

    Back *back = gv("selected_back").as<Back>();
    const std::string bname = back->loc_name.to_string();
    auto tally = [&](int x, int y, int a, int b, const Value &tip) { return tally_sprite(x, y, two_part(std::to_string(a), flip_col, std::to_string(b)), tip); };
    TableRef base_parts = A({Value(vl::T({{"string", localize("k_base_cards")}, {"colour", col("RED")}}))});
    if (modded) base_parts->push(Value(vl::T({{"string", localize("k_effective")}, {"colour", col("BLUE")}})));
    auto suit_tip = [](const char *s) { return Value(A({localize(s, "suits_plural")})); };
    TableRef notes = new_table();
    if (modded)
        notes->push(node(UIT_R, vl::T({{"align", "cm"}}),
                         A({node(UIT_C, vl::T({{"padding", 0.3}, {"r", 0.1}, {"colour", Value(mix_colours(col("BLUE"), col("WHITE"), 0.7f))}}), new_table()),
                            node(UIT_T, vl::T({{"text", str(" " + localize("ph_deck_preview_effective").to_string())}, {"colour", col("WHITE")}, {"scale", 0.3}}))})));
    if (wheel > 0)
        notes->push(node(UIT_R, vl::T({{"align", "cm"}}),
                         A({node(UIT_C, vl::T({{"padding", 0.3}, {"r", 0.1}, {"colour", flip_col}}), new_table()),
                            node(UIT_T, vl::T({{"text", str(" " + localize(vl::T({{"type", "variable"}, {"key", wheel > 1 ? "deck_preview_wheel_plural" : "deck_preview_wheel_singular"},
                                                                                   {"vars", Value(A({Value((double)wheel)}))}}))
                                                                      .to_string())},
                                                 {"colour", col("WHITE")},
                                                 {"scale", 0.3}}))})));
    return TableRef(node(
        UIT_ROOT, vl::T({{"align", "cm"}, {"colour", col("CLEAR")}}),
        A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.05}}), new_table()),
           node(UIT_R, vl::T({{"align", "cm"}}),
                A({node(UIT_C, vl::T({{"align", "cm"}, {"minw", 1.5}, {"minh", 2}, {"r", 0.1}, {"colour", col("BLACK")}, {"emboss", 0.05}}),
                        A({node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.1}}),
                                A({node(UIT_R, vl::T({{"align", "cm"}, {"r", 0.1}, {"colour", col("L_BLACK")}, {"emboss", 0.05}, {"padding", 0.15}}),
                                        A({node(UIT_R, vl::T({{"align", "cm"}}),
                                                A({node(UIT_O, vl::T({{"object", obj(new DynaText(vl::T({{"string", back->loc_name}, {"colours", Value(A({col("WHITE")}))}, {"bump", true},
                                                                                                          {"rotate", true}, {"shadow", true},
                                                                                                          {"scale", 0.6 - bname.size() * 0.01}})))}}))})),
                                           node(UIT_R, vl::T({{"align", "cm"}, {"r", 0.1}, {"padding", 0.1}, {"minw", 2.5}, {"minh", 1.3}, {"colour", col("WHITE")}, {"emboss", 0.05}}),
                                                A({node(UIT_O, vl::T({{"object", obj(new UIBox(back->generate_UI(Value(), 0.7, 0.5),
                                                                                               vl::T({{"offset", Value(vl::T({{"x", 0}, {"y", 0}}))}})))}}))}))})),
                                   node(UIT_R, vl::T({{"align", "cm"}, {"r", 0.1}, {"outline_colour", col("L_BLACK")}, {"line_emboss", 0.05}, {"outline", 1.5}}),
                                        A({node(UIT_R, vl::T({{"align", "cm"}, {"minh", 0.05}, {"padding", 0.07}}),
                                                A({node(UIT_O, vl::T({{"object", obj(new DynaText(vl::T({{"string", Value(base_parts)}, {"colours", Value(A({col("RED")}))},
                                                                                                          {"silent", true}, {"scale", 0.4}, {"pop_in_rate", 10},
                                                                                                          {"pop_delay", 4}})))}}))})),
                                           node(UIT_R, vl::T({{"align", "cm"}, {"minh", 0.05}, {"padding", 0.1}}),
                                                A({tally(1, 0, ace, mod_ace, Value(A({localize("k_aces")}))), tally(2, 0, face, mod_face, Value(A({localize("k_face_cards")}))),
                                                   tally(3, 0, num, mod_num, Value(A({localize("k_numbered_cards")})))})),
                                           node(UIT_R, vl::T({{"align", "cm"}, {"minh", 0.05}, {"padding", 0.1}}),
                                                A({tally(3, 1, suit_t["Spades"], mod_suit_t["Spades"], suit_tip("Spades")),
                                                   tally(0, 1, suit_t["Hearts"], mod_suit_t["Hearts"], suit_tip("Hearts"))})),
                                           node(UIT_R, vl::T({{"align", "cm"}, {"minh", 0.05}, {"padding", 0.1}}),
                                                A({tally(2, 1, suit_t["Clubs"], mod_suit_t["Clubs"], suit_tip("Clubs")),
                                                   tally(1, 1, suit_t["Diamonds"], mod_suit_t["Diamonds"], suit_tip("Diamonds"))}))}))})),
                           node(UIT_C, vl::T({{"align", "cm"}}), rank_cols), node(UIT_B, vl::T({{"w", 0.1}, {"h", 0.1}}))})),
                   node(UIT_B, vl::T({{"w", 0.2}, {"h", 0.1}})),
                   node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.1}, {"r", 0.1}, {"colour", col("BLACK")}, {"emboss", 0.05}}), deck_tables)})),
           node(UIT_R, vl::T({{"align", "cm"}, {"minh", 0.8}, {"padding", 0.05}}), notes)}))
                        .table());
}

// G.UIDEF.deck_info(_show_remaining)
TableRef deck_info(bool show_remaining) {
    auto tab = [](const char *label, bool chosen, const Value &arg) {
        TableRef t = vl::T({{"label", localize(label)}, {"tab_definition_function", obj(func([](const Value &a) { return Value(view_deck(a.truthy())); }))},
                            {"tab_definition_function_args", arg}});
        if (chosen) t->set("chosen", true);
        return Value(t);
    };
    TableRef tabs = show_remaining ? A({tab("b_remaining", true, Value(true)), tab("b_full_deck", false, Value())}) : A({tab("b_full_deck", true, Value())});
    return create_UIBox_generic_options(vl::T({{"contents", Value(A({Value(create_tabs(vl::T({{"tabs", Value(tabs)}, {"tab_h", 8}, {"snap_to_nav", true}})))}))}}));
}

// G.UIDEF.deck_preview(): the counts of the cards left in the deck, by suit and rank
TableRef deck_preview() {
    const double minh = 0.35, minw = 0.5;
    std::map<std::string, int> suit_counts, mod_suit_counts;
    std::map<std::string, std::map<int, int>> SUITS;
    std::map<int, int> rank_counts;
    int wheel = 0;
    bool has_stones = false;
    int stones = 0;
    for (Card *v : sorted_playing_cards()) {
        if (v->ability->get(K("effect")).equals("Stone Card")) has_stones = true;
        if (!((v->area && v->area == G.deck.get()) || v->ability->get(K("wheel_flipped")).truthy())) continue;
        if (v->ability->get(K("wheel_flipped")).truthy()) wheel++;
        if (v->ability->get(K("effect")).equals("Stone Card")) {
            stones++;
            continue;
        }
        const std::string suit = fld(Value(v->base), "suit").to_string();
        for (const char *s : suit_map) {
            if (suit == s) suit_counts[s]++;
            if (v->is_suit(Value(s))) mod_suit_counts[s]++;
        }
        const int id = (int)fld(Value(v->base), "id").num();
        if (id >= 1 && id <= 14) SUITS[suit][id]++;
        rank_counts[id]++;
    }
    const bool flipped = wheel > 0;
    const Value flip_col = flipped ? Value(mix_colours(col("FILTER"), col("WHITE"), 0.7f)) : col("WHITE");
    TableRef suit_labels = new_table();
    TableRef stone_nodes = new_table();
    if (has_stones) {
        stone_nodes->push(node(UIT_T, vl::T({{"text", str(localize("ph_deck_preview_stones").to_string() + ": ")}, {"colour", col("WHITE")}, {"scale", 0.25}, {"shadow", true}})));
        stone_nodes->push(node(UIT_T, vl::T({{"text", str(std::to_string(stones))}, {"colour", stones > 0 ? col("WHITE") : ui_col("TRANSPARENT_LIGHT")}, {"scale", 0.4},
                                             {"shadow", true}})));
    }
    suit_labels->push(node(UIT_R, vl::T({{"align", "cm"}, {"r", 0.1}, {"padding", 0.04}, {"minw", minw}, {"minh", 2 * minh + 0.25}}), stone_nodes));

    TableRef deck_tables = new_table();
    const char *rank_names[] = {"A", "K", "Q", "J", "10", "9", "8", "7", "6", "5", "4", "3", "2"};
    const Value bg = col("JOKER_GREY");
    TableRef row = new_table();
    for (int k = 1; k <= 13; k++) {
        const std::string v = rank_names[k - 1];
        Value rank_col = (v == "K" || v == "Q" || v == "J") ? col("WHITE") : bg;
        rank_col = Value(mix_colours(rank_col, bg, 0.8f));
        row->push(node(UIT_C, vl::T({{"align", "cm"}}),
                       A({node(UIT_C, vl::T({{"align", "cm"}, {"r", 0.1}, {"minw", minw}, {"minh", minh}, {"colour", rank_col}, {"emboss", 0.04}, {"padding", 0.03}}),
                               A({node(UIT_R, vl::T({{"align", "cm"}}),
                                       A({node(UIT_T, vl::T({{"text", str(v)}, {"colour", col("BLACK")}, {"lang", en()}, {"scale", 1.6 * 0.3}}))})),
                                  node(UIT_R, vl::T({{"align", "cm"}, {"minw", minw + 0.04}, {"minh", minh}, {"colour", col("L_BLACK")}, {"r", 0.1}}),
                                       A({node(UIT_T, vl::T({{"text", str(std::to_string(rank_counts[15 - k]))}, {"lang", en()}, {"colour", flip_col}, {"scale", 0.3},
                                                             {"shadow", true}}))}))}))})));
    }
    deck_tables->push(node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.04}}), row));
    for (const char *s : suit_map) {
        row = new_table();
        const Value sbg = Value(mix_colours(col("SUITS").get(Value(s)), col("L_BLACK"), 0.7f));
        for (int i = 14; i >= 2; i--) {
            const int n = SUITS[s][i];
            row->push(node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.05}, {"minw", minw + 0.098}, {"minh", minh}}),
                           A({node(UIT_T, vl::T({{"text", str(std::to_string(n))}, {"colour", n > 0 ? flip_col : ui_col("TRANSPARENT_LIGHT")}, {"scale", n > 0 ? 0.3 : 0.25},
                                                 {"shadow", true}, {"lang", en()}}))})));
        }
        deck_tables->push(node(UIT_R, vl::T({{"align", "cm"}, {"r", 0.1}, {"padding", 0.04}, {"minh", 0.4}, {"colour", sbg}}), row));
    }
    bool mod_diff = false;
    for (const char *s : suit_map) {
        const std::string sv(s);
        const int x = sv == "Spades" ? 3 : sv == "Hearts" ? 0 : sv == "Clubs" ? 2 : 1;
        SpritePos p;
        p.x = (float)x;
        p.y = 1;
        auto *ts = new Sprite(0, 0, 0.3f, 0.3f, &G.ASSET_ATLAS[ui_atlas()], p);
        ts->states.drag.can = false;
        ts->states.hover.can = false;
        ts->states.collide.can = false;
        if (mod_suit_counts[s] != suit_counts[s]) mod_diff = true;
        TableRef cnt = A({node(UIT_T, vl::T({{"text", str(std::to_string(suit_counts[s]))}, {"colour", flip_col}, {"scale", 0.3}, {"shadow", true}, {"lang", en()}}))});
        if (mod_suit_counts[s] != suit_counts[s])
            cnt->push(node(UIT_T, vl::T({{"text", str(" (" + std::to_string(mod_suit_counts[s]) + ")")}, {"colour", Value(mix_colours(col("BLUE"), col("WHITE"), 0.7f))},
                                         {"scale", 0.28}, {"shadow", true}, {"lang", en()}})));
        suit_labels->push(node(UIT_R, vl::T({{"align", "cm"}, {"r", 0.1}, {"padding", 0.03}, {"colour", col("JOKER_GREY")}}),
                               A({node(UIT_C, vl::T({{"align", "cm"}, {"minw", minw}, {"minh", minh}}), A({node(UIT_O, vl::T({{"can_collide", false}, {"object", obj(ts)}}))})),
                                  node(UIT_C, vl::T({{"align", "cm"}, {"minw", minw * 2.4}, {"minh", minh}, {"colour", col("L_BLACK")}, {"r", 0.1}}), cnt)})));
    }
    TableRef inner = A({node(UIT_R, vl::T({{"align", "cm"}}),
                             A({node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.04}}), suit_labels), node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.02}}), deck_tables)}))});
    if (mod_diff)
        inner->push(node(UIT_R, vl::T({{"align", "cm"}}),
                         A({node(UIT_C, vl::T({{"padding", 0.3}, {"r", 0.1}, {"colour", Value(mix_colours(col("BLUE"), col("WHITE"), 0.7f))}}), new_table()),
                            node(UIT_T, vl::T({{"text", str(" " + localize("ph_deck_preview_effective").to_string())}, {"colour", col("WHITE")}, {"scale", 0.3}}))})));
    if (flipped)
        inner->push(node(UIT_R, vl::T({{"align", "cm"}}),
                         A({node(UIT_C, vl::T({{"padding", 0.3}, {"r", 0.1}, {"colour", flip_col}}), new_table()),
                            node(UIT_T, vl::T({{"text", str(" " + localize(vl::T({{"type", "variable"}, {"key", wheel > 1 ? "deck_preview_wheel_plural" : "deck_preview_wheel_singular"},
                                                                                   {"vars", Value(A({Value((double)wheel)}))}}))
                                                                      .to_string())},
                                                 {"colour", col("WHITE")},
                                                 {"scale", 0.3}}))})));
    return TableRef(node(UIT_ROOT, vl::T({{"align", "cm"}, {"colour", col("JOKER_GREY")}, {"r", 0.1}, {"emboss", 0.05}, {"padding", 0.07}}),
                         A({node(UIT_R, vl::T({{"align", "cm"}, {"r", 0.1}, {"emboss", 0.05}, {"colour", col("BLACK")}, {"padding", 0.1}}), inner)}))
                        .table());
}

// create_UIBox_hand_tip(handname): the hand's example cards
Value create_UIBox_hand_tip(const Value &handname) {
    const Value ex = fld(gv("hands").get(handname), "example");
    if (!ex.truthy()) return node(UIT_R, vl::T({{"align", "cm"}}), new_table());
    auto *area = new CardArea(2, 2, 3.5f * G.CARD_W, 0.75f * G.CARD_H, vl::T({{"card_limit", 5}, {"type", "title"}, {"highlight_limit", 0}}));
    Table *e = ex.table();
    for (int k = 1; k <= e->len(); k++) {
        const Value v = e->get(k);
        Card *card = new Card(0, 0, 0.5f * G.CARD_W, 0.5f * G.CARD_H, G.P_CARDS->get(v.get(1)), G.P_CENTERS->get(K("c_base")));
        if (v.get(2).truthy()) {
            float r = 0.2f;
            card->juice_up_card(0.3f, &r);
        }
        if (k == 1) play_sound("paper1", (float)(0.95 + lua_math.random() * 0.1), 0.3f);
        static const EventTimer real = EventTimer::REAL;
        ease_value(ValueRef::of(&card->T->scale, card), v.get(2).truthy() ? 0.25 : -0.15, false, &real, 0.2);
        area->emplace(card);
    }
    return node(UIT_R, vl::T({{"align", "cm"}, {"colour", col("WHITE")}, {"r", 0.1}}),
                A({node(UIT_C, vl::T({{"align", "cm"}}), A({node(UIT_O, vl::T({{"object", obj(area)}}))}))}));
}

// create_UIBox_current_hand_row(handname, simple)
Value create_UIBox_current_hand_row(const char *handname, bool simple) {
    const Value h = gv("hands").get(Value(handname));
    if (!fld(h, "visible").truthy()) return Value();
    const Value tip = Value(vl::T({{"text", localize(handname, "poker_hand_descriptions")},
                                   {"filler", Value(vl::T({{"func", obj(func([](const Value &a) { return create_UIBox_hand_tip(a); }))}, {"args", Value(handname)}}))}}));
    if (simple)
        return node(UIT_R,
                    vl::T({{"align", "cm"}, {"padding", 0.05}, {"r", 0.1}, {"colour", Value(darken(col("JOKER_GREY"), 0.1f))}, {"force_focus", true}, {"emboss", 0.05}, {"hover", true},
                           {"on_demand_tooltip", tip}, {"focus_args", Value(vl::T({{"snap_to", std::string(handname) == "Straight Flush"}}))}}),
                    A({node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0}, {"minw", 5}}),
                            A({node(UIT_T, vl::T({{"text", localize(handname, "poker_hands")}, {"scale", 0.5}, {"colour", ui_col("TEXT_LIGHT")}, {"shadow", true}}))}))}));
    const int level = (int)fld(h, "level").num();
    auto num_txt = [&](const char *k, const Value &colour, bool shadow, double scale) {
        TableRef c = vl::T({{"text", fld(h, k)}, {"scale", scale}, {"lang", en()}, {"colour", colour}});
        if (shadow) c->set("shadow", true);
        return node(UIT_T, c);
    };
    return node(UIT_R,
                vl::T({{"align", "cm"}, {"padding", 0.05}, {"r", 0.1}, {"colour", Value(darken(col("JOKER_GREY"), 0.1f))}, {"emboss", 0.05}, {"hover", true}, {"force_focus", true},
                       {"on_demand_tooltip", tip}}),
                A({node(UIT_C, vl::T({{"align", "cl"}, {"padding", 0}, {"minw", 5}}),
                        A({node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.01}, {"r", 0.1}, {"colour", col("HAND_LEVELS").get(std::min(7, level))}, {"minw", 1.5},
                                              {"outline", 0.8}, {"outline_colour", col("WHITE")}}),
                                A({node(UIT_T, vl::T({{"text", str(localize("k_level_prefix").to_string() + number_format(fld(h, "level")))}, {"scale", 0.5},
                                                      {"colour", ui_col("TEXT_DARK")}}))})),
                           node(UIT_C, vl::T({{"align", "cm"}, {"minw", 4.5}, {"maxw", 4.5}}),
                                A({node(UIT_T, vl::T({{"text", str(" " + localize(handname, "poker_hands").to_string())}, {"scale", 0.45}, {"colour", ui_col("TEXT_LIGHT")},
                                                      {"shadow", true}}))}))})),
                   node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.05}, {"colour", col("BLACK")}, {"r", 0.1}}),
                        A({node(UIT_C, vl::T({{"align", "cr"}, {"padding", 0.01}, {"r", 0.1}, {"colour", col("CHIPS")}, {"minw", 1.1}}),
                                A({num_txt("chips", ui_col("TEXT_LIGHT"), false, 0.45), node(UIT_B, vl::T({{"w", 0.08}, {"h", 0.01}}))})),
                           node(UIT_T, vl::T({{"text", "X"}, {"scale", 0.45}, {"lang", en()}, {"colour", col("MULT")}})),
                           node(UIT_C, vl::T({{"align", "cl"}, {"padding", 0.01}, {"r", 0.1}, {"colour", col("MULT")}, {"minw", 1.1}}),
                                A({node(UIT_B, vl::T({{"w", 0.08}, {"h", 0.01}})), num_txt("mult", ui_col("TEXT_LIGHT"), false, 0.45)}))})),
                   node(UIT_C, vl::T({{"align", "cm"}}), A({node(UIT_T, vl::T({{"text", "  #"}, {"scale", 0.45}, {"colour", ui_col("TEXT_LIGHT")}, {"shadow", true}}))})),
                   node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.05}, {"colour", col("L_BLACK")}, {"r", 0.1}, {"minw", 0.9}}),
                        A({num_txt("played", col("FILTER"), true, 0.45)}))}));
}

// create_UIBox_current_hands(simple)
TableRef create_UIBox_current_hands(bool simple) {
    TableRef hands = new_table();
    for (const char *h : {"Flush Five", "Flush House", "Five of a Kind", "Straight Flush", "Four of a Kind", "Full House", "Flush", "Straight", "Three of a Kind",
                          "Two Pair", "Pair", "High Card"}) {
        Value r = create_UIBox_current_hand_row(h, simple);
        if (r.truthy()) hands->push(r);
    }
    return TableRef(node(UIT_ROOT, vl::T({{"align", "cm"}, {"minw", 3}, {"padding", 0.1}, {"r", 0.1}, {"colour", col("CLEAR")}}),
                         A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.04}}), hands)}))
                        .table());
}

// G.UIDEF.current_blinds()
TableRef current_blinds() {
    TableRef cols = new_table();
    const Value bs = fld(gv("round_resets"), "blind_states");
    for (const char *t : {"Small", "Big", "Boss"})
        if (!bs.get(Value(t)).equals("Hide"))
            cols->push(node(UIT_C, vl::T({{"align", "tm"}, {"padding", 0.1}, {"outline", 2}, {"r", 0.1}, {"line_emboss", 0.2}, {"outline_colour", col("BLACK")}}),
                            A({Value(create_UIBox_blind_choice(t, true))})));
    return TableRef(node(UIT_ROOT, vl::T({{"align", "cm"}, {"colour", col("CLEAR")}, {"padding", 0.2}}), cols).table());
}

// G.UIDEF.current_stake()
TableRef current_stake() {
    const int stake = (int)gv("stake").num();
    auto desc_rows = [](int s) {
        const Value sc = G.P_CENTER_POOLS->get(K("Stake")).get(s);
        TableRef d = new_table();
        localize(vl::T({{"type", "descriptions"}, {"key", fld(sc, "key")}, {"set", fld(sc, "set")}, {"nodes", Value(d)}}));
        TableRef full = new_table();
        for (int k = 1; k <= d->len(); k++) full->push(node(UIT_R, vl::T({{"align", "cm"}}), TableRef(d->get(k).table())));
        if (full->len() > 0) full->set(full->len(), Value());  // (_full_desc[#_full_desc] = nil: the last line goes)
        return full;
    };
    Value other_col;
    if (stake > 2) {
        TableRef rows = A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.05}}),
                                A({node(UIT_T, vl::T({{"text", localize("k_also_applied")}, {"scale", 0.4}, {"colour", col("WHITE")}}))}))});
        for (int i = stake - 1; i >= 2; i--)
            rows->push(node(UIT_R, vl::T({{"align", "cm"}}),
                            A({node(UIT_C, vl::T({{"align", "cm"}}),
                                    A({node(UIT_C, vl::T({{"align", "cm"}, {"colour", get_stake_col(i)}, {"r", 0.1}, {"minh", 0.35}, {"minw", 0.35}, {"emboss", 0.05}}), new_table()),
                                       node(UIT_B, vl::T({{"w", 0.1}, {"h", 0.1}}))})),
                               node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.03}, {"colour", col("WHITE")}, {"r", 0.1}, {"minh", 0.7}, {"minw", 4.8}}), desc_rows(i))})));
        other_col = node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.05}, {"r", 0.1}, {"colour", col("L_BLACK")}}), rows);
    }
    Sprite *stake_sprite = get_stake_sprite(stake, 0.8f);
    const Value sc = G.P_CENTER_POOLS->get(K("Stake")).get(stake);
    Value current = node(
        UIT_R, vl::T({{"align", "cm"}}),
        A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.1}, {"minw", 4}}),
                A({node(UIT_O, vl::T({{"colour", col("BLUE")}, {"object", obj(stake_sprite)}, {"hover", true}, {"can_collide", false}})),
                   node(UIT_T, vl::T({{"text", localize(vl::T({{"type", "name_text"}, {"key", fld(sc, "key")}, {"set", "Stake"}}))}, {"scale", 0.45}, {"colour", col("WHITE")}}))})),
           node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.05}, {"colour", get_stake_col(stake)}, {"r", 0.05}}),
                A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.05}, {"colour", col("WHITE")}, {"r", 0.05}}),
                        A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.03}, {"minh", 0.7}, {"minw", 3.8}}), desc_rows(stake))}))}))}));
    return TableRef(node(UIT_ROOT, vl::T({{"align", "cm"}, {"colour", col("BLACK")}, {"r", 0.1}, {"padding", 0.1}}), A({current, other_col})).table());
}

// G.UIDEF.used_vouchers()
TableRef used_vouchers() {
    bool silent = false;
    Table *pool = G.P_CENTER_POOLS->get(K("Voucher")).table();
    const Value used = gv("used_vouchers");
    std::vector<std::vector<Value>> keys_used((pool->len() + 1) / 2 + 1);
    for (int k = 1; k <= pool->len(); k++) {
        const Value v = pool->get(k);
        const int key = 1 + (int)std::floor((k - 0.1) / 2);
        if (used.get(fld(v, "key")).truthy()) keys_used[key].push_back(v);
    }
    int area_count = 0;
    for (auto &v : keys_used)
        if (!v.empty()) area_count++;
    TableRef rows = new_table(), tables = new_table();
    int areas = 0;
    for (auto &v : keys_used) {
        if (v.empty()) continue;
        if (areas == 5 || areas == 10) {
            rows->push(node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}, {"no_fill", true}}), tables));
            tables = new_table();
        }
        auto *area = new CardArea(G.ROOM->T->x + 0.2f * G.ROOM->T->w / 2, G.ROOM->T->h, (v.size() == 1 ? 1.0f : 1.33f) * G.CARD_W,
                                  (area_count >= 10 ? 0.75f : 1.07f) * G.CARD_H, vl::T({{"card_limit", 2}, {"type", "voucher"}, {"highlight_limit", 0}}));
        areas++;
        for (const Value &vv : v) {
            Card *card = new Card(area->T->x + area->T->w / 2, area->T->y, G.CARD_W, G.CARD_H, Value(), G.P_CENTERS->get(fld(vv, "key")),
                                  vl::T({{"bypass_discovery_center", true}, {"bypass_discovery_ui", true}, {"bypass_lock", true}}));
            card->ability->set("order", fld(vv, "order"));
            card->start_materialize(nullptr, silent);
            silent = true;
            area->emplace(card);
        }
        tables->push(node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0}, {"no_fill", true}}), A({node(UIT_O, vl::T({{"object", obj(area)}}))})));
    }
    rows->push(node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}, {"no_fill", true}}), tables));
    if (!silent)
        return TableRef(node(UIT_ROOT, vl::T({{"align", "cm"}, {"colour", col("CLEAR")}}),
                             A({node(UIT_O, vl::T({{"object", obj(new DynaText(vl::T({{"string", Value(A({localize("ph_no_vouchers")}))}, {"colours", Value(A({ui_col("TEXT_LIGHT")}))},
                                                                                       {"bump", true}, {"scale", 0.6}})))}}))}))
                            .table());
    return TableRef(node(UIT_ROOT, vl::T({{"align", "cm"}, {"colour", col("CLEAR")}}),
                         A({node(UIT_R, vl::T({{"align", "cm"}}),
                                 A({node(UIT_O, vl::T({{"object", obj(new DynaText(vl::T({{"string", Value(A({localize("ph_vouchers_redeemed")}))},
                                                                                           {"colours", Value(A({ui_col("TEXT_LIGHT")}))}, {"bump", true}, {"scale", 0.6}})))}}))})),
                            node(UIT_R, vl::T({{"align", "cm"}, {"minh", 0.5}}), new_table()),
                            node(UIT_R, vl::T({{"align", "cm"}, {"colour", col("BLACK")}, {"r", 1}, {"padding", 0.15}, {"emboss", 0.05}}),
                                 A({node(UIT_R, vl::T({{"align", "cm"}}), rows)}))}))
                        .table());
}

// G.UIDEF.run_info()
TableRef run_info() {
    auto tab = [](const char *label, bool chosen, std::function<TableRef()> f) {
        TableRef t = vl::T({{"label", localize(label)}, {"tab_definition_function", obj(func([f](const Value &) { return Value(f()); }))}});
        if (chosen) t->set("chosen", true);
        return Value(t);
    };
    TableRef tabs = A({tab("b_poker_hands", true, [] { return create_UIBox_current_hands(false); }), tab("b_blinds", false, current_blinds),
                       tab("b_vouchers", false, used_vouchers)});
    if (gv("stake").num() > 1) tabs->push(tab("b_stake", false, current_stake));
    return create_UIBox_generic_options(vl::T({{"contents", Value(A({Value(create_tabs(vl::T({{"tabs", Value(tabs)}, {"tab_h", 8}, {"snap_to_nav", true}})))}))}}));
}

void register_run_info_callbacks() {
    auto &F = G.FUNCS;
    F["run_info"] = [](UIElement *) {
        G.SETTINGS->set("paused", true);
        overlay_menu(run_info());
    };
    F["deck_info"] = [](UIElement *) {
        G.SETTINGS->set("paused", true);
        if (G.deck_preview) {
            G.deck_preview->remove();
            G.deck_preview = nullptr;
        }
        overlay_menu(deck_info(G.STATE == SELECTING_HAND || G.STATE == HAND_PLAYED || G.STATE == DRAW_TO_HAND));
    };
}

}  // namespace vl
