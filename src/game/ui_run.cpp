// functions/UI_definitions.lua: the run's UIs (HUD, blind choices, hand buttons)
#include <string>

#include "engine/event.h"
#include "engine/sprite.h"
#include "engine/text.h"
#include "engine/ui.h"
#include "game/blind.h"
#include "game/card.h"
#include "game/functions.h"
#include "game/g.h"
#include "game/sound.h"
#include "game/state_events.h"
#include "game/tag.h"
#include "game/ui_definitions.h"

namespace vl {

namespace {
inline Value fld(const Value &t, const char *k) { return t.get(Value(intern(k))); }
Value node(int uit, TableRef config, TableRef nodes = nullptr) {
    TableRef t = vl::T({{"n", (double)uit}, {"config", Value(config)}});
    if (nodes) t->set("nodes", Value(nodes));
    return Value(t);
}
Value col(const char *name) { return G.colour(name); }
Value ui_col(const char *name) { return fld(G.colour("UI"), name); }
Value dyn(const char *name) { return fld(G.colour("DYN_UI"), name); }
Value obj(Object *o) { return Value(o); }
Value gv(const char *k) { return G.GAME->get(Value(intern(k))); }
// G.LANGUAGES['en-us'] and its font
Value en_lang() { return Value((Object *)G.LANGUAGES["en-us"].get()); }
Value en_font() { return Value((Object *)G.LANGUAGES["en-us"]->font); }
// {{ref_table = t, ref_value = k}} for DynaText strings
Value ref_str(const Value &t, const char *k, const char *prefix = nullptr) {
    TableRef r = vl::T({{"ref_table", t}, {"ref_value", k}});
    if (prefix) r->set("prefix", Value(prefix));
    return Value(A({Value(r)}));
}
DynaText *dyna(std::initializer_list<std::pair<const char *, Value>> kv) { return new DynaText(vl::T(kv)); }
Value text(const Value &s, double scale, const Value &colour, bool shadow = true) {
    return node(UIT_T, vl::T({{"text", s}, {"scale", scale}, {"colour", colour}, {"shadow", shadow}}));
}
Blind *game_blind() { return gv("blind").as<Blind>(); }
}  // namespace

TableRef UIBox_dyn_container(const TableRef &inner, bool horizontal, const Value &colour_override, const Value &background_override, bool flipped,
                             const Value &padding) {
    return TableRef(
        node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.03}, {"colour", ui_col("TRANSPARENT_DARK")}, {"r", 0.1}}),
             A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.05}, {"colour", colour_override.truthy() ? colour_override : dyn("MAIN")}, {"r", 0.1}}),
                     A({node(UIT_R,
                             vl::T({{"align", horizontal ? "cl" : (flipped ? "bm" : "tm")},
                                    {"colour", background_override.truthy() ? background_override : dyn("BOSS_DARK")},
                                    {"minw", horizontal ? 100 : 0},
                                    {"minh", horizontal ? 0 : 30},
                                    {"r", 0.1},
                                    {"padding", padding.truthy() ? padding : Value(0.08)}}),
                             inner)}))}))
            .table());
}

TableRef create_UIBox_HUD_blind() {
    const double scale = 0.4;
    Sprite *stake_sprite = get_stake_sprite(gv("stake").truthy() ? (int)gv("stake").num() : 1, 0.5f);
    Blind *blind = game_blind();
    blind->change_dim(1.5f, 1.5f);
    Value bv(blind->vars);
    return TableRef(node(
        UIT_ROOT,
        vl::T({{"align", "cm"}, {"minw", 4.5}, {"r", 0.1}, {"colour", col("BLACK")}, {"emboss", 0.05}, {"padding", 0.05}, {"func", "HUD_blind_visible"},
               {"id", "HUD_blind"}}),
        A({node(UIT_R, vl::T({{"align", "cm"}, {"minh", 0.7}, {"r", 0.1}, {"emboss", 0.05}, {"colour", dyn("MAIN")}}),
                A({node(UIT_C, vl::T({{"align", "cm"}, {"minw", 3}}),
                        A({node(UIT_O, vl::T({{"object", obj(dyna({{"string", ref_str(bv, "loc_name")},
                                                                    {"colours", Value(A({ui_col("TEXT_LIGHT")}))},
                                                                    {"shadow", true},
                                                                    {"rotate", true},
                                                                    {"silent", true},
                                                                    {"float", true},
                                                                    {"scale", 1.6 * scale},
                                                                    {"y_offset", 0}}))},
                                              {"id", "HUD_blind_name"}}))}))})),
           node(UIT_R, vl::T({{"align", "cm"}, {"minh", 2.74}, {"r", 0.1}, {"colour", dyn("DARK")}}),
                A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.05}}),
                        A({node(UIT_R, vl::T({{"align", "cm"}, {"minh", 0.3}, {"maxw", 4.2}}),
                                A({node(UIT_T, vl::T({{"ref_table", Value(vl::T({{"val", ""}}))}, {"ref_value", "val"}, {"scale", scale * 0.9},
                                                      {"colour", ui_col("TEXT_LIGHT")}, {"func", "HUD_blind_debuff_prefix"}})),
                                   node(UIT_T, vl::T({{"ref_table", blind->vars->get(K("loc_debuff_lines"))}, {"ref_value", 1}, {"scale", scale * 0.9},
                                                      {"colour", ui_col("TEXT_LIGHT")}, {"id", "HUD_blind_debuff_1"}, {"func", "HUD_blind_debuff"}}))})),
                           node(UIT_R, vl::T({{"align", "cm"}, {"minh", 0.3}, {"maxw", 4.2}}),
                                A({node(UIT_T, vl::T({{"ref_table", blind->vars->get(K("loc_debuff_lines"))}, {"ref_value", 2}, {"scale", scale * 0.9},
                                                      {"colour", ui_col("TEXT_LIGHT")}, {"id", "HUD_blind_debuff_2"}, {"func", "HUD_blind_debuff"}}))}))})),
                   node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.15}}),
                        A({node(UIT_O, vl::T({{"object", obj(blind)}, {"draw_layer", 1}})),
                           node(UIT_C, vl::T({{"align", "cm"}, {"r", 0.1}, {"padding", 0.05}, {"emboss", 0.05}, {"minw", 2.9}, {"colour", col("BLACK")}}),
                                A({node(UIT_R, vl::T({{"align", "cm"}, {"maxw", 2.8}}),
                                        A({text(localize("ph_blind_score_at_least"), 0.3, col("WHITE"))})),
                                   node(UIT_R, vl::T({{"align", "cm"}, {"minh", 0.6}}),
                                        A({node(UIT_O, vl::T({{"w", 0.5}, {"h", 0.5}, {"colour", col("BLUE")}, {"object", obj(stake_sprite)}, {"hover", true},
                                                              {"can_collide", false}})),
                                           node(UIT_B, vl::T({{"h", 0.1}, {"w", 0.1}})),
                                           node(UIT_T, vl::T({{"ref_table", bv}, {"ref_value", "chip_text"}, {"scale", 0.001}, {"colour", col("RED")},
                                                              {"lang", en_lang()}, {"shadow", true}, {"id", "HUD_blind_count"}, {"func", "blind_chip_UI_scale"}}))})),
                                   node(UIT_R, vl::T({{"align", "cm"}, {"minh", 0.45}, {"maxw", 2.8}, {"func", "HUD_blind_reward"}}),
                                        A({text(localize("ph_blind_reward"), 0.3, col("WHITE"), false),
                                           node(UIT_O, vl::T({{"object", obj(dyna({{"string", ref_str(gv("current_round"), "dollars_to_be_earned")},
                                                                                {"font", en_font()},
                                                                                {"colours", Value(A({col("MONEY")}))},
                                                                                {"shadow", true},
                                                                                {"rotate", true},
                                                                                {"bump", true},
                                                                                {"silent", true},
                                                                                {"scale", 0.45}}))},
                                                              {"id", "dollars_to_be_earned"}}))}))}))}))}))}))
                        .table());
}

void add_tag(Tag *tag) {
    TagSprite *s = nullptr;
    TableRef ui = tag->generate_UI(0.8f, &s);
    AlignArgs al;
    UIBox *prev = G.HUD_tags.empty() ? nullptr : dynamic_cast<UIBox *>(G.HUD_tags.back().get());
    TableRef cfg = prev ? vl::T({{"align", "tm"}, {"offset", Value(vl::T({{"x", 0}, {"y", 0}}))}, {"major", obj(prev)}})
                        : vl::T({{"align", "bri"}, {"offset", Value(vl::T({{"x", 0.7}, {"y", 0}}))}, {"major", obj(G.ROOM_ATTACH.get())}});
    UIBox *box = new UIBox(TableRef(node(UIT_ROOT, vl::T({{"align", "cm"}, {"padding", 0.05}, {"colour", col("CLEAR")}}), A({Value(ui)})).table()), cfg);
    G.HUD_tags.push_back(NodeRef(box));
    discover_card(G.P_TAGS->get(Value(tag->key)));
    Table *tags = gv("tags").table();
    for (int i = 1; i <= tags->len(); i++)
        if (Tag *t = tags->get(i).as<Tag>()) t->apply_to_run(vl::T({{"type", "tag_add"}, {"tag", obj(tag)}}));
    tags->push(obj(tag));
    tag->HUD_tag = box;
}

TableRef create_UIBox_HUD() {
    const double scale = 0.4;
    Sprite *stake_sprite = get_stake_sprite(gv("stake").truthy() ? (int)gv("stake").num() : 1, 0.5f);
    const double spacing = 0.13;
    Value temp_col = dyn("BOSS_MAIN"), temp_col2 = dyn("BOSS_DARK");
    Value cr = gv("current_round"), ch = fld(cr, "current_hand");

    auto counter = [&](const char *id, const char *label, Value dt_obj, const char *dt_id, bool ante_row) {
        TableRef cfg = vl::T({{"align", "cm"}, {"padding", 0.05}, {"minw", 1.45}, {"colour", temp_col}, {"emboss", 0.05}, {"r", 0.1}});
        if (id) cfg->set("id", id);
        if (ante_row) cfg->set("minh", 1);
        return cfg;
    };
    (void)counter;
    auto dt_count = [&](const Value &ref_t, const char *ref_v, const Value &colour, const char *id) {
        return node(UIT_O, vl::T({{"object", obj(dyna({{"string", ref_str(ref_t, ref_v)},
                                                       {"font", en_font()},
                                                       {"colours", Value(A({colour}))},
                                                       {"shadow", true},
                                                       {"rotate", true},
                                                       {"scale", 2 * scale}}))},
                                  {"id", id}}));
    };
    auto label_row = [&](const char *key) {
        return node(UIT_R, vl::T({{"align", "cm"}, {"minh", 0.33}, {"maxw", 1.35}}),
                    A({text(localize(key), 0.85 * scale, ui_col("TEXT_LIGHT"))}));
    };

    TableRef round = A({
        node(UIT_R, vl::T({{"align", "cm"}}),
             A({node(UIT_C, vl::T({{"id", "hud_hands"}, {"align", "cm"}, {"padding", 0.05}, {"minw", 1.45}, {"colour", temp_col}, {"emboss", 0.05}, {"r", 0.1}}),
                     A({label_row("k_hud_hands"),
                        node(UIT_R, vl::T({{"align", "cm"}, {"r", 0.1}, {"minw", 1.2}, {"colour", temp_col2}}),
                             A({dt_count(cr, "hands_left", col("BLUE"), "hand_UI_count")}))})),
                node(UIT_C, vl::T({{"minw", spacing}}), A({})),
                node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.05}, {"minw", 1.45}, {"colour", temp_col}, {"emboss", 0.05}, {"r", 0.1}}),
                     A({label_row("k_hud_discards"),
                        node(UIT_R, vl::T({{"align", "cm"}}),
                             A({node(UIT_R, vl::T({{"align", "cm"}, {"r", 0.1}, {"minw", 1.2}, {"colour", temp_col2}}),
                                     A({dt_count(cr, "discards_left", col("RED"), "discard_UI_count")}))}))}))})),
        node(UIT_R, vl::T({{"minh", spacing}}), A({})),
        node(UIT_R, vl::T({{"align", "cm"}}),
             A({node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.05}, {"minw", 1.45 * 2 + spacing}, {"minh", 1.15}, {"colour", temp_col}, {"emboss", 0.05}, {"r", 0.1}}),
                     A({node(UIT_R, vl::T({{"align", "cm"}}),
                             A({node(UIT_C, vl::T({{"align", "cm"}, {"r", 0.1}, {"minw", 1.28 * 2 + spacing}, {"minh", 1}, {"colour", temp_col2}}),
                                     A({node(UIT_O, vl::T({{"object", obj(dyna({{"string", ref_str(Value(G.GAME), "dollars", localize("$").str().data())},
                                                                                {"maxw", 1.35},
                                                                                {"colours", Value(A({col("MONEY")}))},
                                                                                {"font", en_font()},
                                                                                {"shadow", true},
                                                                                {"spacing", 2},
                                                                                {"bump", true},
                                                                                {"scale", 2.2 * scale}}))},
                                                           {"id", "dollar_text_UI"}}))}))}))}))})),
        node(UIT_R, vl::T({{"minh", spacing}}), A({})),
        node(UIT_R, vl::T({{"align", "cm"}}),
             A({node(UIT_C, vl::T({{"id", "hud_ante"}, {"align", "cm"}, {"padding", 0.05}, {"minw", 1.45}, {"minh", 1}, {"colour", temp_col}, {"emboss", 0.05}, {"r", 0.1}}),
                     A({label_row("k_ante"),
                        node(UIT_R, vl::T({{"align", "cm"}, {"r", 0.1}, {"minw", 1.2}, {"colour", temp_col2}}),
                             A({node(UIT_O, vl::T({{"object", obj(dyna({{"string", ref_str(gv("round_resets"), "ante")},
                                                                        {"colours", Value(A({col("IMPORTANT")}))},
                                                                        {"shadow", true},
                                                                        {"font", en_font()},
                                                                        {"scale", 2 * scale}}))},
                                                   {"id", "ante_UI_count"}})),
                                node(UIT_T, vl::T({{"text", " "}, {"scale", 0.3 * scale}})),
                                node(UIT_T, vl::T({{"text", "/ "}, {"scale", 0.7 * scale}, {"colour", col("WHITE")}, {"shadow", true}, {"lang", en_lang()}})),
                                node(UIT_T, vl::T({{"ref_table", Value(G.GAME)}, {"ref_value", "win_ante"}, {"scale", scale}, {"colour", col("WHITE")},
                                                   {"shadow", true}, {"lang", en_lang()}}))}))})),
                node(UIT_C, vl::T({{"minw", spacing}}), A({})),
                node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.05}, {"minw", 1.45}, {"minh", 1}, {"colour", temp_col}, {"emboss", 0.05}, {"r", 0.1}}),
                     A({node(UIT_R, vl::T({{"align", "cm"}, {"maxw", 1.35}}),
                             A({node(UIT_T, vl::T({{"text", localize("k_round")}, {"minh", 0.33}, {"scale", 0.85 * scale}, {"colour", ui_col("TEXT_LIGHT")},
                                                   {"shadow", true}}))})),
                        node(UIT_R, vl::T({{"align", "cm"}, {"r", 0.1}, {"minw", 1.2}, {"colour", temp_col2}, {"id", "row_round_text"}}),
                             A({node(UIT_O, vl::T({{"object", obj(dyna({{"string", ref_str(Value(G.GAME), "round")},
                                                                        {"font", en_font()},
                                                                        {"colours", Value(A({col("IMPORTANT")}))},
                                                                        {"shadow", true},
                                                                        {"scale", 2 * scale}}))},
                                                   {"id", "round_UI_count"}}))}))}))})),
    });

    auto hand_dt = [&](const char *ref_v, double s, bool en) {
        std::initializer_list<std::pair<const char *, Value>> base = {};
        (void)base;
        TableRef c = vl::T({{"string", ref_str(ch, ref_v)}, {"colours", Value(A({ui_col("TEXT_LIGHT")}))}, {"shadow", true}, {"float", true}, {"scale", s}});
        if (en) c->set("font", en_font());
        return obj(new DynaText(c));
    };
    Value hand = node(
        UIT_R, vl::T({{"align", "cm"}, {"id", "hand_text_area"}, {"colour", Value(darken(col("BLACK"), 0.1f))}, {"r", 0.1}, {"emboss", 0.05}, {"padding", 0.03}}),
        A({node(UIT_C, vl::T({{"align", "cm"}}),
                A({node(UIT_R, vl::T({{"align", "cm"}, {"minh", 1.1}}),
                        A({node(UIT_O, vl::T({{"id", "hand_name"}, {"func", "hand_text_UI_set"}, {"object", hand_dt("handname_text", scale * 1.4, false)}})),
                           node(UIT_O, vl::T({{"id", "hand_chip_total"}, {"func", "hand_chip_total_UI_set"}, {"object", hand_dt("chip_total_text", scale * 1.4, true)}})),
                           node(UIT_T, vl::T({{"ref_table", ch}, {"ref_value", "hand_level"}, {"scale", scale}, {"colour", ui_col("TEXT_LIGHT")}, {"id", "hand_level"},
                                              {"shadow", true}}))})),
                   node(UIT_R, vl::T({{"align", "cm"}, {"minh", 1}, {"padding", 0.1}}),
                        A({node(UIT_C, vl::T({{"align", "cr"}, {"minw", 2}, {"minh", 1}, {"r", 0.1}, {"colour", col("UI_CHIPS")}, {"id", "hand_chip_area"}, {"emboss", 0.05}}),
                                A({node(UIT_O, vl::T({{"func", "flame_handler"}, {"no_role", true}, {"id", "flame_chips"}, {"object", obj(new Moveable(0, 0, 0, 0))}, {"w", 0}, {"h", 0}})),
                                   node(UIT_O, vl::T({{"id", "hand_chips"}, {"func", "hand_chip_UI_set"}, {"object", hand_dt("chip_text", scale * 2.3, true)}})),
                                   node(UIT_B, vl::T({{"w", 0.1}, {"h", 0.1}}))})),
                           node(UIT_C, vl::T({{"align", "cm"}}),
                                A({node(UIT_T, vl::T({{"text", "X"}, {"lang", en_lang()}, {"scale", scale * 2}, {"colour", col("UI_MULT")}, {"shadow", true}}))})),
                           node(UIT_C, vl::T({{"align", "cl"}, {"minw", 2}, {"minh", 1}, {"r", 0.1}, {"colour", col("UI_MULT")}, {"id", "hand_mult_area"}, {"emboss", 0.05}}),
                                A({node(UIT_O, vl::T({{"func", "flame_handler"}, {"no_role", true}, {"id", "flame_mult"}, {"object", obj(new Moveable(0, 0, 0, 0))}, {"w", 0}, {"h", 0}})),
                                   node(UIT_B, vl::T({{"w", 0.1}, {"h", 0.1}})),
                                   node(UIT_O, vl::T({{"id", "hand_mult"}, {"func", "hand_mult_UI_set"}, {"object", hand_dt("mult_text", scale * 2.3, true)}}))}))}))}))}));

    Value dollars_chips = node(
        UIT_R, vl::T({{"align", "cm"}, {"r", 0.1}, {"padding", 0}, {"colour", dyn("BOSS_MAIN")}, {"emboss", 0.05}, {"id", "row_dollars_chips"}}),
        A({node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.1}}),
                A({node(UIT_C, vl::T({{"align", "cm"}, {"minw", 1.3}}),
                        A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}, {"maxw", 1.3}}), A({text(localize("k_round"), 0.42, ui_col("TEXT_LIGHT"))})),
                           node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}, {"maxw", 1.3}}), A({text(localize("k_lower_score"), 0.42, ui_col("TEXT_LIGHT"))}))})),
                   node(UIT_C, vl::T({{"align", "cm"}, {"minw", 3.3}, {"minh", 0.7}, {"r", 0.1}, {"colour", dyn("BOSS_DARK")}}),
                        A({node(UIT_O, vl::T({{"w", 0.5}, {"h", 0.5}, {"object", obj(stake_sprite)}, {"hover", true}, {"can_collide", false}})),
                           node(UIT_B, vl::T({{"w", 0.1}, {"h", 0.1}})),
                           node(UIT_T, vl::T({{"ref_table", Value(G.GAME)}, {"ref_value", "chips_text"}, {"lang", en_lang()}, {"scale", 0.85}, {"colour", col("WHITE")},
                                              {"id", "chip_UI_count"}, {"func", "chip_UI_set"}, {"shadow", true}}))}))}))}));

    TableRef buttons = A({node(
        UIT_C, vl::T({{"align", "cm"}, {"r", 0.1}, {"colour", col("CLEAR")}, {"shadow", true}, {"id", "button_area"}, {"padding", 0.2}}),
        A({node(UIT_R, vl::T({{"id", "run_info_button"}, {"align", "cm"}, {"minh", 1.75}, {"minw", 1.5}, {"padding", 0.05}, {"r", 0.1}, {"hover", true}, {"colour", col("RED")},
                              {"button", "run_info"}, {"shadow", true}}),
                A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}, {"maxw", 1.4}}), A({text(localize("b_run_info_1"), 1.2 * scale, ui_col("TEXT_LIGHT"))})),
                   node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}, {"maxw", 1.4}}),
                        A({node(UIT_T, vl::T({{"text", localize("b_run_info_2")}, {"scale", 1 * scale}, {"colour", ui_col("TEXT_LIGHT")}, {"shadow", true},
                                              {"focus_args", Value(vl::T({{"button", "back"}, {"orientation", "bm"}}))}, {"func", "set_button_pip"}}))}))})),
           node(UIT_R, vl::T({{"align", "cm"}, {"minh", 1.75}, {"minw", 1.5}, {"padding", 0.05}, {"r", 0.1}, {"hover", true}, {"colour", col("ORANGE")}, {"button", "options"},
                              {"shadow", true}}),
                A({node(UIT_C, vl::T({{"align", "cm"}, {"maxw", 1.4}, {"focus_args", Value(vl::T({{"button", "start"}, {"orientation", "bm"}}))}, {"func", "set_button_pip"}}),
                        A({text(localize("b_options"), scale, ui_col("TEXT_LIGHT"))}))}))}))});

    return TableRef(node(UIT_ROOT, vl::T({{"align", "cm"}, {"padding", 0.03}, {"colour", ui_col("TRANSPARENT_DARK")}}),
                         A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.05}, {"colour", dyn("MAIN")}, {"r", 0.1}}),
                                 A({node(UIT_R, vl::T({{"align", "cm"}, {"colour", dyn("BOSS_DARK")}, {"r", 0.1}, {"minh", 30}, {"padding", 0.08}}),
                                         A({node(UIT_R, vl::T({{"align", "cm"}, {"minh", 0.3}}), A({})),
                                            node(UIT_R, vl::T({{"align", "cm"}, {"id", "row_blind"}, {"minw", 1}, {"minh", 3.75}}), A({})),
                                            dollars_chips, hand,
                                            node(UIT_R, vl::T({{"align", "cm"}, {"id", "row_round"}}),
                                                 A({node(UIT_C, vl::T({{"align", "cm"}}), buttons), node(UIT_C, vl::T({{"align", "cm"}}), round)}))}))}))}))
                        .table());
}

TableRef create_UIBox_blind_tag(const char *blind_choice, bool run_info) {
    Table *rr = gv("round_resets").table();
    if (!rr->get(K("blind_tags")).truthy()) rr->set("blind_tags", Value(new_table()));
    Value key = rr->get(K("blind_tags")).get(Value(blind_choice));
    if (!key.truthy()) return nullptr;
    Tag *tag = new Tag(key, false, Value(blind_choice));
    TagSprite *sprite = nullptr;
    TableRef tag_ui = tag->generate_UI(0.8f, &sprite);
    sprite->states.collide.can = false;
    const bool disabled = false;
    Value button = !run_info
                       ? node(UIT_C, vl::T({{"align", "cm"}, {"colour", ui_col("BACKGROUND_INACTIVE")}, {"minh", 0.6}, {"minw", 2}, {"maxw", 2}, {"padding", 0.07}, {"r", 0.1},
                                            {"shadow", true}, {"hover", true}, {"one_press", true}, {"button", "skip_blind"}, {"func", "hover_tag_proxy"}, {"ref_table", obj(tag)}}),
                              A({text(localize("b_skip_blind"), 0.4, ui_col("TEXT_INACTIVE"), false)}))
                       : node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.1}, {"emboss", 0.05}, {"colour", Value(mix_colours(col("BLUE"), col("BLACK"), 0.4f))}, {"r", 0.1},
                                            {"maxw", 2}, {"hover", true}, {"func", "hover_tag_proxy"}, {"ref_table", obj(tag)}}),
                              A({text(localize("b_skip_reward"), 0.35, col("WHITE"), false)}));
    return TableRef(
        node(UIT_R, vl::T({{"id", "tag_container"}, {"ref_table", obj(tag)}, {"align", "cm"}}),
             A({node(UIT_R, vl::T({{"align", "tm"}, {"minh", 0.65}}),
                     A({text(localize("k_or"), 0.55, disabled ? ui_col("TEXT_INACTIVE") : col("WHITE"), !disabled)})),
                node(UIT_R, vl::T({{"id", Value("tag_" + std::string(blind_choice))}, {"align", "cm"}, {"r", 0.1}, {"padding", 0.1}, {"minw", 1}, {"can_collide", true},
                                   {"ref_table", obj(sprite)}}),
                     A({node(UIT_C, vl::T({{"id", "tag_desc"}, {"align", "cm"}, {"minh", 1}}), A({Value(tag_ui)})), button}))}))
            .table());
}

TableRef create_UIBox_blind_choice(const char *type, bool run_info) {
    if (!gv("blind_on_deck").truthy()) G.GAME->set("blind_on_deck", "Small");
    Table *rr = gv("round_resets").table();
    if (!run_info) rr->get(K("blind_states")).table()->set(gv("blind_on_deck"), "Select");
    const bool disabled = false;
    if (!type) type = "Small";
    Value cfg = G.P_BLINDS->get(rr->get(K("blind_choices")).get(Value(type)));
    Value pos = fld(cfg, "pos");
    SpritePos sp;
    sp.x = (float)fld(pos, "x").num();
    sp.y = (float)fld(pos, "y").num();
    AnimatedSprite *animation = new AnimatedSprite(0, 0, 1.4f, 1.4f, &G.ANIMATION_ATLAS["blind_chips"], sp);
    DrawStep a, b;
    a.has_shadow_height = true;
    a.shadow_height = 0.05f;
    animation->define_draw_steps({a, b});
    Sprite *stake_sprite = get_stake_sprite(gv("stake").truthy() ? (int)gv("stake").num() : 1, 0.5f);

    if (!gv("orbital_choices").truthy()) G.GAME->set("orbital_choices", Value(new_table()));
    Table *oc = gv("orbital_choices").table();
    Value ante = rr->get(K("ante"));
    if (!oc->get(ante).truthy()) oc->set(ante, Value(new_table()));
    if (!oc->get(ante).get(Value(type)).truthy()) {
        TableRef hands = new_table();
        gv("hands").table()->each([&](const Value &k, const Value &v) {
            if (fld(v, "visible").truthy()) hands->push(k);
        });
        double s = pseudoseed("orbital");
        oc->get(ante).table()->set(type, pseudorandom_element(Value(hands), &s));
    }

    Value extras;
    if (std::string_view(type) == "Small" || std::string_view(type) == "Big") {
        TableRef t = create_UIBox_blind_tag(type, run_info);
        if (t) extras = Value(t);
    } else if (!run_info) {
        auto up = [](const char *key, const Value &c, const Value &colours, double scale, bool bump) {
            TableRef cfg = vl::T({{"string", Value(A({Value(vl::T({{"string", localize(key)}, {"colour", c}}))}))}, {"colours", Value(A({colours}))}, {"scale", scale},
                                  {"silent", true}, {"pop_delay", 4.5}, {"shadow", true}, {"maxw", 3}});
            if (bump) cfg->set("bump", true);
            return obj(new DynaText(cfg));
        };
        Value dt1 = up("ph_up_ante_1", col("FILTER"), col("BLACK"), 0.55, true);
        Value dt2 = up("ph_up_ante_2", col("WHITE"), col("CHANCE"), 0.35, false);
        Value dt3 = up("ph_up_ante_3", col("WHITE"), col("CHANCE"), 0.35, false);
        extras = node(UIT_R, vl::T({{"align", "cm"}}),
                      A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.07}, {"r", 0.1}, {"colour", Value(A({0, 0, 0, 0.12}))}, {"minw", 2.9}}),
                              A({node(UIT_R, vl::T({{"align", "cm"}}), A({node(UIT_O, vl::T({{"object", dt1}}))})),
                                 node(UIT_R, vl::T({{"align", "cm"}}), A({node(UIT_O, vl::T({{"object", dt2}}))})),
                                 node(UIT_R, vl::T({{"align", "cm"}}), A({node(UIT_O, vl::T({{"object", dt3}}))}))}))}));
    }
    if (!rr->get(K("blind_ante")).truthy()) rr->set("blind_ante", rr->get(K("ante")));
    Value mpph = localize(fld(gv("current_round"), "most_played_poker_hand").str(), "poker_hands");
    Value loc_target = localize(vl::T({{"type", "raw_descriptions"}, {"key", fld(cfg, "key")}, {"set", "Blind"}, {"vars", Value(A({mpph}))}}));
    Value loc_name = localize(vl::T({{"type", "name_text"}, {"key", fld(cfg, "key")}, {"set", "Blind"}}));
    Table *text_table = loc_target.table();
    TableRef blind_col = get_blind_main_colour(Value(type));
    double blind_amt = get_blind_amount((int)rr->get(K("blind_ante")).num()) * fld(cfg, "mult").num() * fld(gv("starting_params"), "ante_scaling").num();

    Value blind_state = rr->get(K("blind_states")).get(Value(type));
    bool reward = !fld(fld(gv("modifiers"), "no_blind_reward"), type).truthy();
    if (blind_state.equals("Select")) blind_state = Value("Current");
    Value run_info_colour;
    if (run_info)
        run_info_colour = blind_state.equals("Defeated") ? col("GREY") : blind_state.equals("Skipped") ? col("BLUE") : blind_state.equals("Upcoming") ? col("ORANGE")
                          : blind_state.equals("Current") ? col("RED") : col("GOLD");
    auto dis = [&](const Value &c) { return disabled ? ui_col("TEXT_INACTIVE") : c; };

    Value top = !run_info
                    ? node(UIT_R, vl::T({{"id", "select_blind_button"}, {"align", "cm"}, {"ref_table", cfg}, {"colour", disabled ? ui_col("BACKGROUND_INACTIVE") : col("ORANGE")},
                                         {"minh", 0.6}, {"minw", 2.7}, {"maxw", 2.7}, {"padding", 0.07}, {"r", 0.1}, {"shadow", true}, {"hover", true}, {"one_press", true},
                                         {"button", "select_blind"}}),
                           A({node(UIT_T, vl::T({{"ref_table", rr->get(K("loc_blind_states"))}, {"ref_value", type}, {"scale", 0.45}, {"colour", dis(ui_col("TEXT_LIGHT"))},
                                                 {"shadow", !disabled}}))}))
                    : node(UIT_R, vl::T({{"id", "select_blind_button"}, {"align", "cm"}, {"ref_table", cfg}, {"colour", run_info_colour}, {"minh", 0.6}, {"minw", 2.7},
                                         {"padding", 0.07}, {"r", 0.1}, {"emboss", 0.08}}),
                           A({text(localize(blind_state.str(), "blind_states"), 0.45, ui_col("TEXT_LIGHT"))}));

    TableRef desc_rows = A({node(UIT_R, vl::T({{"align", "cm"}, {"minh", 1.5}}), A({node(UIT_O, vl::T({{"object", obj(animation)}}))}))});
    if (text_table && text_table->get(1).truthy()) {
        TableRef lines = new_table();
        lines->push(node(UIT_R, vl::T({{"align", "cm"}, {"maxw", 2.8}}),
                         A({node(UIT_T, vl::T({{"id", fld(cfg, "key")}, {"ref_table", Value(vl::T({{"val", ""}}))}, {"ref_value", "val"}, {"scale", 0.32},
                                               {"colour", dis(col("WHITE"))}, {"shadow", !disabled}, {"func", "HUD_blind_debuff_prefix"}})),
                            text(text_table->get(1), 0.32, dis(col("WHITE")), !disabled)})));
        if (text_table->get(2).truthy())
            lines->push(node(UIT_R, vl::T({{"align", "cm"}, {"maxw", 2.8}}), A({text(text_table->get(2), 0.32, dis(col("WHITE")), !disabled)})));
        desc_rows->push(node(UIT_R, vl::T({{"align", "cm"}, {"minh", 0.7}, {"padding", 0.05}, {"minw", 2.9}}), lines));
    }
    TableRef score_rows = A({node(UIT_R, vl::T({{"align", "cm"}, {"maxw", 3}}), A({text(localize("ph_blind_score_at_least"), 0.3, dis(col("WHITE")), !disabled)})),
                             node(UIT_R, vl::T({{"align", "cm"}, {"minh", 0.6}}),
                                  A({node(UIT_O, vl::T({{"w", 0.5}, {"h", 0.5}, {"colour", col("BLUE")}, {"object", obj(stake_sprite)}, {"hover", true}, {"can_collide", false}})),
                                     node(UIT_B, vl::T({{"h", 0.1}, {"w", 0.1}})),
                                     node(UIT_T, vl::T({{"text", Value(number_format(Value(blind_amt)))}, {"scale", score_number_scale(0.9, Value(blind_amt))}, {"lang", en_lang()},
                                                        {"colour", dis(col("RED"))}, {"shadow", !disabled}}))}))});
    if (reward) {
        std::string d;
        for (int i = 0; i < (int)fld(cfg, "dollars").num(); i++) d += std::string(localize("$").str());
        score_rows->push(node(UIT_R, vl::T({{"align", "cm"}}),
                              A({text(localize("ph_blind_reward"), 0.35, dis(col("WHITE")), !disabled),
                                 node(UIT_T, vl::T({{"text", Value(d + "+")}, {"lang", en_lang()}, {"scale", 0.35}, {"colour", dis(col("MONEY"))}, {"shadow", !disabled}}))})));
    }

    TableRef ref = new_table();
    if (run_info) ref->set("run_info", true);
    TableRef cfg_root = vl::T({{"id", type}, {"align", "tm"}, {"func", "blind_choice_handler"}, {"ref_table", Value(ref)}, {"r", 0.1}, {"padding", 0.05}});
    if (!run_info) cfg_root->set("minh", 10);
    return TableRef(
        node(UIT_R, cfg_root,
             A({node(UIT_R, vl::T({{"align", "cm"}, {"colour", Value(mix_colours(col("BLACK"), col("L_BLACK"), 0.5f))}, {"r", 0.1}, {"outline", 1}, {"outline_colour", col("L_BLACK")}}),
                     A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.2}}), A({top})),
                        node(UIT_R, vl::T({{"id", "blind_name"}, {"align", "cm"}, {"padding", 0.07}}),
                             A({node(UIT_R, vl::T({{"align", "cm"}, {"r", 0.1}, {"outline", 1}, {"outline_colour", Value(blind_col)}, {"colour", Value(darken(Value(blind_col), 0.3f))},
                                                   {"minw", 2.9}, {"emboss", 0.1}, {"padding", 0.07}, {"line_emboss", 1}}),
                                     A({node(UIT_O, vl::T({{"object", obj(dyna({{"string", loc_name}, {"colours", Value(A({dis(col("WHITE"))}))}, {"shadow", !disabled},
                                                                                {"float", !disabled}, {"y_offset", 0}, {"scale", 0.45}, {"maxw", 2.8}}))}}))}))})),
                        node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.05}}),
                             A({node(UIT_R, vl::T({{"id", "blind_desc"}, {"align", "cm"}, {"padding", 0.05}}),
                                     A({node(UIT_R, vl::T({{"align", "cm"}}), desc_rows),
                                        node(UIT_R, vl::T({{"align", "cm"}, {"r", 0.1}, {"padding", 0.05}, {"minw", 3.1}, {"colour", col("BLACK")}, {"emboss", 0.05}}), score_rows)}))}))})),
                node(UIT_R, vl::T({{"id", "blind_extras"}, {"align", "cm"}}), extras.truthy() ? A({extras}) : A({}))}))
            .table());
}

TableRef create_UIBox_blind_select() {
    UIBox *hudbox = dynamic_cast<UIBox *>(G.HUD.get());
    G.blind_prompt_box = new UIBox(
        TableRef(node(UIT_ROOT, vl::T({{"align", "cm"}, {"colour", col("CLEAR")}, {"padding", 0.2}}),
                      A({node(UIT_R, vl::T({{"align", "cm"}}),
                              A({node(UIT_O, vl::T({{"object", obj(dyna({{"string", localize("ph_choose_blind_1")}, {"colours", Value(A({col("WHITE")}))}, {"shadow", true},
                                                                         {"bump", true}, {"scale", 0.6}, {"pop_in", 0.5}, {"maxw", 5}}))},
                                                    {"id", "prompt_dynatext1"}}))})),
                         node(UIT_R, vl::T({{"align", "cm"}}),
                              A({node(UIT_O, vl::T({{"object", obj(dyna({{"string", localize("ph_choose_blind_2")}, {"colours", Value(A({col("WHITE")}))}, {"shadow", true},
                                                                         {"bump", true}, {"scale", 0.7}, {"pop_in", 0.5}, {"maxw", 5}, {"silent", true}}))},
                                                    {"id", "prompt_dynatext2"}}))})),
                         (gv("used_vouchers").get(K("v_retcon")).truthy() || gv("used_vouchers").get(K("v_directors_cut")).truthy())
                             ? Value(UIBox_button(vl::T({{"label", Value(A({localize("b_reroll_boss"), Value(localize("$").to_string() + "10")}))},
                                                         {"button", "reroll_boss"},
                                                         {"func", "reroll_boss_button"}})))
                             : Value()}))
                     .table()),
        vl::T({{"align", "cm"}, {"offset", Value(vl::T({{"x", 0}, {"y", -15}}))}, {"major", obj(hudbox ? hudbox->get_UIE_by_ID("row_blind") : nullptr)}, {"bond", "Weak"}}));
    G.E_MANAGER->add_event(ev_immediate([] {
        if (auto *b = dynamic_cast<UIBox *>(G.blind_prompt_box.get())) b->alignment.offset.y = 0;
        return true;
    }));

    const float width = G.hand->T->w;
    Table *bs = gv("round_resets").get(K("blind_states")).table();
    auto out = [&](const char *t) {
        Value s = bs->get(Value(t));
        return s.equals("Defeated") || s.equals("Skipped") || s.equals("Hide");
    };
    G.GAME->set("blind_on_deck", !out("Small") ? "Small" : !out("Big") ? "Big" : "Boss");

    G.blind_select_opts.clear();
    TableRef row = new_table();
    for (const char *t : {"Small", "Big", "Boss"}) {
        if (bs->get(Value(t)).equals("Hide")) continue;
        TableRef bg = get_blind_main_colour(Value(t));
        Value back = std::string_view(t) == "Boss" ? Value(mix_colours(col("BLACK"), Value(get_blind_main_colour(Value(t))), 0.8f)) : Value();
        UIBox *opt = new UIBox(TableRef(node(UIT_ROOT, vl::T({{"align", "cm"}, {"colour", col("CLEAR")}}),
                                             A({Value(UIBox_dyn_container(A({Value(create_UIBox_blind_choice(t, false))}), false, Value(bg), back, false, Value()))}))
                                            .table()),
                               vl::T({{"align", "bmi"}, {"offset", Value(vl::T({{"x", 0}, {"y", 0}}))}}));
        std::string lk(t);
        lk[0] = (char)std::tolower(lk[0]);
        G.blind_select_opts[lk] = NodeRef(opt);
        row->push(node(UIT_O, vl::T({{"align", "cm"}, {"object", obj(opt)}})));
    }
    return TableRef(node(UIT_ROOT, vl::T({{"align", "tm"}, {"minw", width}, {"r", 0.15}, {"colour", col("CLEAR")}}),
                         A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.5}}), row)}))
                        .table());
}

TableRef create_UIBox_buttons() {
    const double text_scale = 0.45, button_height = 1.3;
    Value play_button = node(UIT_C, vl::T({{"id", "play_button"}, {"align", "tm"}, {"minw", 2.5}, {"padding", 0.3}, {"r", 0.1}, {"hover", true}, {"colour", col("BLUE")},
                                          {"button", "play_cards_from_highlighted"}, {"one_press", true}, {"shadow", true}, {"func", "can_play"}}),
                             A({node(UIT_R, vl::T({{"align", "bcm"}, {"padding", 0}}),
                                     A({node(UIT_T, vl::T({{"text", localize("b_play_hand")}, {"scale", text_scale}, {"colour", ui_col("TEXT_LIGHT")},
                                                           {"focus_args", Value(vl::T({{"button", "x"}, {"orientation", "bm"}}))}, {"func", "set_button_pip"}}))}))}));
    Value discard_button = node(UIT_C, vl::T({{"id", "discard_button"}, {"align", "tm"}, {"padding", 0.3}, {"r", 0.1}, {"minw", 2.5}, {"minh", button_height}, {"hover", true},
                                             {"colour", col("RED")}, {"button", "discard_cards_from_highlighted"}, {"one_press", true}, {"shadow", true}, {"func", "can_discard"}}),
                                A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}}),
                                        A({node(UIT_T, vl::T({{"text", localize("b_discard")}, {"scale", text_scale}, {"colour", ui_col("TEXT_LIGHT")},
                                                              {"focus_args", Value(vl::T({{"button", "y"}, {"orientation", "bm"}}))}, {"func", "set_button_pip"}}))}))}));
    auto sort_btn = [&](const char *key, const char *button) {
        return node(UIT_C, vl::T({{"align", "cm"}, {"minh", 0.7}, {"minw", 0.9}, {"padding", 0.1}, {"r", 0.1}, {"hover", true}, {"colour", col("ORANGE")}, {"button", button}, {"shadow", true}}),
                    A({text(localize(key), text_scale * 0.7, ui_col("TEXT_LIGHT"), false)}));
    };
    const bool swap = G.SETTINGS->get(K("play_button_pos")).num() == 1;
    return TableRef(node(
        UIT_ROOT, vl::T({{"align", "cm"}, {"minw", 1}, {"minh", 0.3}, {"padding", 0.15}, {"r", 0.1}, {"colour", col("CLEAR")}}),
        A({swap ? discard_button : play_button,
           node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.1}, {"r", 0.1}, {"colour", ui_col("TRANSPARENT_DARK")}, {"outline", 1.5},
                              {"outline_colour", Value(mix_colours(col("WHITE"), col("JOKER_GREY"), 0.7f))}, {"line_emboss", 1}}),
                A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}}),
                        A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}}), A({text(localize("b_sort_hand"), text_scale * 0.8, ui_col("TEXT_LIGHT"), false)})),
                           node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.1}}), A({sort_btn("k_rank", "sort_hand_value"), sort_btn("k_suit", "sort_hand_suit")}))}))})),
           swap ? play_button : discard_button}))
                        .table());
}

TableRef use_and_sell_buttons(Card *card) {
    auto nd = [](int uit, TableRef cfg, TableRef nodes = nullptr) {
        TableRef t = vl::T({{"n", (double)uit}, {"config", Value(cfg)}});
        if (nodes) t->set("nodes", Value(nodes));
        return Value(t);
    };
    auto ui = [](const char *n) { return G.colour("UI").get(Value(intern(n))); };
    Value self = Value((Object *)card);
    Value sell, use;
    const bool joker_area = card->area && card->area->config->get(K("type")).equals("joker");
    if (joker_area) {
        sell = nd(UIT_C, vl::T({{"align", "cr"}}),
                  A({nd(UIT_C,
                        vl::T({{"ref_table", self}, {"align", "cr"}, {"padding", 0.1}, {"r", 0.08}, {"minw", 1.25}, {"hover", true}, {"shadow", true},
                               {"colour", ui("BACKGROUND_INACTIVE")}, {"one_press", true}, {"button", "sell_card"}, {"func", "can_sell_card"}}),
                        A({nd(UIT_B, vl::T({{"w", 0.1}, {"h", 0.6}})),
                           nd(UIT_C, vl::T({{"align", "tm"}}),
                              A({nd(UIT_R, vl::T({{"align", "cm"}, {"maxw", 1.25}}),
                                    A({nd(UIT_T, vl::T({{"text", localize("b_sell")}, {"colour", ui("TEXT_LIGHT")}, {"scale", 0.4}, {"shadow", true}}))})),
                                 nd(UIT_R, vl::T({{"align", "cm"}}),
                                    A({nd(UIT_T, vl::T({{"text", localize("$")}, {"colour", G.colour("WHITE")}, {"scale", 0.4}, {"shadow", true}})),
                                       nd(UIT_T, vl::T({{"ref_table", self}, {"ref_value", "sell_cost_label"}, {"colour", G.colour("WHITE")}, {"scale", 0.55},
                                                        {"shadow", true}}))}))}))}))}));
    }
    const bool in_pack = card->area && G.pack_cards && card->area == G.pack_cards.get();
    if (card->ability->get(K("consumeable")).truthy()) {
        if (in_pack)
            return TableRef(nd(UIT_ROOT, vl::T({{"padding", 0}, {"colour", G.colour("CLEAR")}}),
                               A({nd(UIT_R, vl::T({{"mid", true}}), A({})),
                                  nd(UIT_R,
                                     vl::T({{"ref_table", self}, {"r", 0.08}, {"padding", 0.1}, {"align", "bm"}, {"minw", 0.5 * card->T->w - 0.15},
                                            {"minh", 0.8 * card->T->h}, {"maxw", 0.7 * card->T->w - 0.15}, {"hover", true}, {"shadow", true},
                                            {"colour", ui("BACKGROUND_INACTIVE")}, {"one_press", true}, {"button", "use_card"}, {"func", "can_use_consumeable"}}),
                                     A({nd(UIT_T, vl::T({{"text", localize("b_use")}, {"colour", ui("TEXT_LIGHT")}, {"scale", 0.55}, {"shadow", true}}))}))}))
                               .table());
        use = nd(UIT_C, vl::T({{"align", "cr"}}),
                 A({nd(UIT_C,
                       vl::T({{"ref_table", self}, {"align", "cr"}, {"maxw", 1.25}, {"padding", 0.1}, {"r", 0.08}, {"minw", 1.25}, {"minh", joker_area ? 0 : 1},
                              {"hover", true}, {"shadow", true}, {"colour", ui("BACKGROUND_INACTIVE")}, {"one_press", true}, {"button", "use_card"},
                              {"func", "can_use_consumeable"}}),
                       A({nd(UIT_B, vl::T({{"w", 0.1}, {"h", 0.6}})),
                          nd(UIT_T, vl::T({{"text", localize("b_use")}, {"colour", ui("TEXT_LIGHT")}, {"scale", 0.55}, {"shadow", true}}))}))}));
    } else if (in_pack) {
        return TableRef(nd(UIT_ROOT, vl::T({{"padding", 0}, {"colour", G.colour("CLEAR")}}),
                           A({nd(UIT_R,
                                 vl::T({{"ref_table", self}, {"r", 0.08}, {"padding", 0.1}, {"align", "bm"}, {"minw", 0.5 * card->T->w - 0.15},
                                        {"maxw", 0.9 * card->T->w - 0.15}, {"minh", 0.3 * card->T->h}, {"hover", true}, {"shadow", true},
                                        {"colour", ui("BACKGROUND_INACTIVE")}, {"one_press", true}, {"button", "use_card"}, {"func", "select_button_check"}}),
                                 A({nd(UIT_T, vl::T({{"text", localize("b_select")}, {"colour", ui("TEXT_LIGHT")}, {"scale", 0.45}, {"shadow", true}}))}))}))
                            .table());
    }
    // (Lua drops nil entries from the node lists: an absent sell/use leaves its row empty)
    return TableRef(nd(UIT_ROOT, vl::T({{"padding", 0}, {"colour", G.colour("CLEAR")}}),
                       A({nd(UIT_C, vl::T({{"padding", 0.15}, {"align", "cl"}}),
                             A({nd(UIT_R, vl::T({{"align", "cl"}}), sell.truthy() ? A({sell}) : A({})),
                                nd(UIT_R, vl::T({{"align", "cl"}}), use.truthy() ? A({use}) : A({}))}))}))
                        .table());
}

TableRef create_UIBox_round_evaluation() {
    const double width = G.hand->T->w - 2;
    auto row = [](TableRef cfg, TableRef nodes) { return Value(vl::T({{"n", (double)UIT_R}, {"config", Value(cfg)}, {"nodes", Value(nodes)}})); };
    return vl::T({{"n", (double)UIT_ROOT},
                  {"config", Value(vl::T({{"align", "tm"}, {"colour", G.colour("CLEAR")}}))},
                  {"nodes", Value(A({Value(UIBox_dyn_container(
                                A({row(vl::T({{"align", "tm"}, {"minw", width}, {"minh", 3}, {"padding", 0.1}, {"r", 0.1}, {"colour", G.colour("BLACK")}, {"emboss", 0.05}}),
                                       A({row(vl::T({{"align", "cm"}, {"minw", width}, {"minh", 1.4}}), A({})),
                                          row(vl::T({{"align", "cm"}, {"minw", width}, {"id", "base_round_eval"}}), A({})),
                                          row(vl::T({{"align", "cm"}, {"minw", width}, {"id", "bonus_round_eval"}}), A({}))})),
                                   row(vl::T({{"align", "cm"}, {"minh", 0.05}}), A({})),
                                   row(vl::T({{"align", "cm"}, {"minw", width}, {"id", "eval_bottom"}}), A({}))}),
                                false))}))}});
}

}  // namespace vl
