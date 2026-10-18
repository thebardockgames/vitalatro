// functions/UI_definitions.lua
#include "game/ui_definitions.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "engine/controller.h"
#include "engine/event.h"
#include "engine/sprite.h"
#include "engine/text.h"
#include "engine/ui.h"
#include "game/functions.h"
#include "game/g.h"

namespace vl {

namespace {
inline Value fld(const Value &t, const char *k) { return t.get(Value(intern(k))); }
// {n = uit, config = {...}, nodes = {...}}
Value node(int uit, TableRef config, TableRef nodes = nullptr) {
    TableRef t = vl::T({{"n", (double)uit}, {"config", Value(config)}});
    if (nodes) t->set("nodes", Value(nodes));
    return Value(t);
}
Value col(const char *name) { return G.colour(name); }
Value ui_col(const char *name) { return fld(G.colour("UI"), name); }
Value obj(Object *o) { return Value(o); }
Sprite *icon_sprite(float w, float h, float x, float y) {
    SpritePos p;
    p.x = x;
    p.y = y;
    Sprite *s = new Sprite(0, 0, w, h, &G.ASSET_ATLAS["icons"], p);
    s->states.drag.can = false;
    return s;
}
}  // namespace

// TODO(ui): the real tooltip layouts; an empty root keeps hover popups harmless until then.
TableRef create_popup_UIBox_tooltip(const Value &) {
    return vl::T({{"n", (double)UIT_ROOT}, {"config", Value(vl::T({{"align", "cm"}}))}, {"nodes", Value(A({}))}});
}

TableRef create_UIBox_detailed_tooltip(const Value &v) { return create_popup_UIBox_tooltip(v); }

TableRef create_UIBox_notify_alert(const Value &achievement, const Value &type, bool from_left) {
    Value c = G.P_CENTERS->get(achievement);
    const char *an = type.equals("Joker") ? "Joker" : type.equals("Voucher") ? "Voucher" : type.equals("Back") ? "centers" : "icons";
    Atlas *atlas = &G.ASSET_ATLAS[an];
    SpritePos p;
    if (c.truthy()) {
        p.x = (float)fld(fld(c, "pos"), "x").num();
        p.y = (float)fld(fld(c, "pos"), "y").num();
    } else {
        p.x = 3;
        p.y = 0;
    }
    Sprite *t_s = new Sprite(0, 0, 1.5f * (atlas->px / atlas->py), 1.5f, atlas, p);
    t_s->states.drag.can = false;
    t_s->states.hover.can = false;
    t_s->states.collide.can = false;

    Value subtext = type.equals("achievement") ? localize("k_achievement")
                    : type.equals("Joker")     ? localize("k_joker")
                    : type.equals("Voucher")   ? localize("k_voucher")
                    : type.equals("Back")      ? localize("k_deck")
                                               : Value("ERROR");
    if (achievement.equals("b_challenge")) subtext = localize("k_challenges");
    Value name = type.equals("achievement") ? localize(achievement.str(), "achievement_names") : Value("ERROR");
    const char *al = from_left ? "cr" : "cl";
    auto text = [](const Value &s, double scale, const Value &colour) {
        return node(UIT_T, vl::T({{"text", s}, {"scale", scale}, {"colour", colour}, {"shadow", true}}));
    };
    Value body = !type.equals("achievement")
                     ? node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.04}}),
                            A({node(UIT_R, vl::T({{"align", "cm"}, {"maxw", 3.4}}), A({text(subtext, 0.5, col("FILTER"))})),
                               node(UIT_R, vl::T({{"align", "cm"}, {"maxw", 3.4}}), A({text(localize("k_unlocked_ex"), 0.35, col("FILTER"))}))}))
                     : node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.04}}),
                            A({node(UIT_R, vl::T({{"align", "cm"}, {"maxw", 3.4}, {"padding", 0.1}}), A({text(name, 0.4, ui_col("TEXT_LIGHT"))})),
                               node(UIT_R, vl::T({{"align", "cm"}, {"maxw", 3.4}}), A({text(subtext, 0.3, col("FILTER"))})),
                               node(UIT_R, vl::T({{"align", "cm"}, {"maxw", 3.4}}), A({text(localize("k_unlocked_ex"), 0.35, col("FILTER"))}))}));
    return TableRef(
        node(UIT_ROOT, vl::T({{"align", al}, {"r", 0.1}, {"padding", 0.06}, {"colour", ui_col("TRANSPARENT_DARK")}}),
             A({node(UIT_R,
                     vl::T({{"align", al}, {"padding", 0.2}, {"minw", 20}, {"r", 0.1}, {"colour", col("BLACK")}, {"outline", 1.5}, {"outline_colour", col("GREY")}}),
                     A({node(UIT_R, vl::T({{"align", "cm"}, {"r", 0.1}}),
                             A({node(UIT_R, vl::T({{"align", "cm"}, {"r", 0.1}}), A({node(UIT_O, vl::T({{"object", obj(t_s)}}))})), body}))}))}))
            .table());
}

TableRef create_UIBox_card_alert(const TableRef &args_in) {
    TableRef args = args_in ? args_in : new_table();
    Value bg = args->get(K("no_bg")).truthy()      ? col("CLEAR")
               : args->get(K("bg_col")).truthy()   ? args->get(K("bg_col"))
               : args->get(K("red_bad")).truthy()  ? Value(darken(col("RED"), 0.1f))
                                                   : col("RED");
    Value text = args->get(K("text"));
    auto num = [&](const char *k, double d) { return args->get(Value(intern(k))).truthy() ? args->get(Value(intern(k))) : Value(d); };
    DynaText *dt = new DynaText(vl::T({{"string", text.truthy() ? text : Value("!")},
                                       {"colours", Value(A({col("WHITE")}))},
                                       {"shadow", true},
                                       {"rotate", true},
                                       {"H_offset", num("y_offset", 0)},
                                       {"bump_rate", text.truthy() ? 3 : 7},
                                       {"bump_amount", num("bump_amount", 3)},
                                       {"bump", true},
                                       {"maxw", args->get(K("maxw"))},
                                       {"text_rot", num("text_rot", 0.2)},
                                       {"spacing", 3 * num("scale", 1).num()},
                                       {"scale", num("scale", 0.48)}}));
    return TableRef(node(UIT_ROOT, vl::T({{"align", "cm"}, {"colour", col("CLEAR")}, {"refresh_movement", true}}),
                         A({node(UIT_R,
                                 vl::T({{"align", "cm"}, {"r", 0.15}, {"minw", 0.42}, {"minh", 0.42}, {"colour", bg}, {"draw_layer", 1},
                                        {"emboss", 0.05}, {"refresh_movement", true}}),
                                 A({node(UIT_O, vl::T({{"object", obj(dt)}}))}))}))
                        .table());
}

TableRef UIBox_button(const TableRef &args) {
    auto def = [&](const char *k, Value d) {
        if (!args->get(Value(intern(k))).truthy()) args->set(k, d);
    };
    def("button", "exit_overlay_menu");
    def("colour", col("RED"));
    def("label", Value(A({"LABEL"})));
    def("minw", 2.7);
    def("maxw", args->get(K("minw")).num() - 0.2);
    if (args->get(K("minw")).num() < args->get(K("maxw")).num()) args->set("maxw", args->get(K("minw")).num() - 0.2);
    def("minh", 0.9);
    def("scale", 0.5);
    def("text_colour", ui_col("TEXT_LIGHT"));
    int but_UIT = args->get(K("col")).boolean() ? UIT_C : UIT_R;  // args.col == true

    TableRef label_nodes = new_table();
    Value fa = args->get(K("focus_args"));
    Table *labels = args->get(K("label")).table();
    int n = labels ? labels->len() : 0;
    for (int k = 1; k <= n; k++) {
        Value button_pip;
        if (k == n && fa.truthy() && fld(fa, "set_button_pip").truthy()) button_pip = "set_button_pip";
        label_nodes->push(node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}, {"minw", args->get(K("minw"))}, {"maxw", args->get(K("maxw"))}}),
                               A({node(UIT_T, vl::T({{"text", labels->get(k)},
                                                     {"scale", args->get(K("scale"))},
                                                     {"colour", args->get(K("text_colour"))},
                                                     {"shadow", args->get(K("shadow"))},
                                                     {"focus_args", button_pip.truthy() ? fa : Value()},
                                                     {"func", button_pip},
                                                     {"ref_table", args->get(K("ref_table"))}}))})));
    }
    Value count = args->get(K("count"));
    if (count.truthy()) {
        std::string s = fld(count, "tally").to_string() + " / " + fld(count, "of").to_string();
        label_nodes->push(node(UIT_R, vl::T({{"align", "cm"}, {"minh", 0.4}}),
                               A({node(UIT_T, vl::T({{"scale", 0.35},
                                                     {"text", Value(s)},
                                                     {"lang", Value((Object *)G.LANGUAGES["en-us"].get())},
                                                     {"colour", Value(A({1, 1, 1, 0.9}))}}))})));
    }
    Value button = args->get(K("button"));
    return TableRef(node(but_UIT, vl::T({{"align", "cm"}}),
                         A({node(UIT_C,
                                 vl::T({{"align", "cm"},
                                        {"padding", args->get(K("padding")).truthy() ? args->get(K("padding")) : Value(0)},
                                        {"r", 0.1},
                                        {"hover", true},
                                        {"colour", args->get(K("colour"))},
                                        {"one_press", args->get(K("one_press"))},
                                        {"button", button.equals("nil") ? Value() : button},
                                        {"choice", args->get(K("choice"))},
                                        {"chosen", args->get(K("chosen"))},
                                        {"focus_args", fa},
                                        {"minh", args->get(K("minh")).num() - 0.3 * (count.truthy() ? 1 : 0)},
                                        {"shadow", true},
                                        {"func", args->get(K("func"))},
                                        {"id", args->get(K("id"))},
                                        {"back_func", args->get(K("back_func"))},
                                        {"ref_table", args->get(K("ref_table"))},
                                        {"mid", args->get(K("mid"))}}),
                                 label_nodes)}))
                        .table());
}

TableRef create_UIBox_profile_button() {
    // (G.F_DISP_USERNAME is not used on the Vita)
    Value prof = G.PROFILES->get(G.SETTINGS->get(K("profile")));
    if (!fld(prof, "name").truthy()) prof.table()->set("name", Value("P" + G.SETTINGS->get(K("profile")).to_string()));
    return TableRef(
        node(UIT_ROOT, vl::T({{"align", "cm"}, {"colour", col("CLEAR")}}),
             A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.2}, {"r", 0.1}, {"emboss", 0.1}, {"colour", col("L_BLACK")}}),
                     A({node(UIT_R, vl::T({{"align", "cm"}}),
                             A({node(UIT_T, vl::T({{"text", localize("k_profile")}, {"scale", 0.4}, {"colour", ui_col("TEXT_LIGHT")}, {"shadow", true}}))})),
                        node(UIT_R, vl::T({{"align", "cm"}}),
                             A({node(UIT_C,
                                     vl::T({{"align", "cm"}, {"padding", 0.15}, {"minw", 2}, {"minh", 0.8}, {"maxw", 2}, {"r", 0.1}, {"hover", true},
                                            {"colour", Value(mix_colours(col("WHITE"), col("GREY"), 0.2f))}, {"button", "profile_select"}, {"shadow", true}}),
                                     A({node(UIT_T, vl::T({{"ref_table", Value(G.G_TABLE)},
                                                           {"ref_value", "profile_display"},
                                                           {"scale", 0.4},
                                                           {"colour", ui_col("TEXT_LIGHT")},
                                                           {"shadow", true}}))}))}))}))}))
            .table());
}

TableRef create_UIBox_main_menu_buttons() {
    const double text_scale = 0.45;
    Sprite *language = nullptr;
    if (!G.F_ENGLISH_ONLY) language = icon_sprite(0.6f, 0.6f, 2, 0);
    // (F_LINKTREE is off: external links are not available on the Vita)
    G.REFRESH_ALERTS = true;
    bool tut_done = G.SETTINGS->get(K("tutorial_complete")).truthy();
    auto btn = [](std::initializer_list<std::pair<const char *, Value>> kv) { return Value(UIBox_button(vl::T(kv))); };
    Value quit_spacer, quit_button;
    if (G.F_QUIT_BUTTON) {
        quit_spacer = node(UIT_C, vl::T({{"align", "cm"}, {"minw", 0.2}}), A({}));
        quit_button = btn({{"button", "quit"}, {"colour", col("RED")}, {"minw", 2.65}, {"minh", 1.35},
                           {"label", Value(A({localize("b_quit_cap")}))}, {"scale", text_scale * 1.2}, {"col", true}});
    }
    Value lang_row;
    if (!G.F_ENGLISH_ONLY)
        lang_row = node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.2}, {"r", 0.1}, {"emboss", 0.1}, {"colour", col("L_BLACK")}}),
                        A({node(UIT_R,
                                vl::T({{"align", "cm"}, {"padding", 0.15}, {"minw", 1}, {"r", 0.1}, {"hover", true},
                                       {"colour", Value(mix_colours(col("WHITE"), col("GREY"), 0.2f))}, {"button", "language_selection"}, {"shadow", true}}),
                                A({node(UIT_O, vl::T({{"object", obj(language)}})),
                                   node(UIT_T, vl::T({{"text", Value(G.LANG->label)}, {"scale", 0.4}, {"colour", ui_col("TEXT_LIGHT")}, {"shadow", true}}))}))}));
    return TableRef(
        node(UIT_ROOT, vl::T({{"align", "cm"}, {"colour", col("CLEAR")}}),
             A({node(UIT_C, vl::T({{"align", "bm"}}),
                     A({Value(),  // G.FTP_LOCKED and G.F_JAN_CTA: wishlist row (demo only)
                        node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.2}, {"r", 0.1}, {"emboss", 0.1}, {"colour", col("L_BLACK")}, {"mid", true}}),
                             A({btn({{"id", "main_menu_play"}, {"button", tut_done ? "setup_run" : "start_run"}, {"colour", col("BLUE")}, {"minw", 3.65},
                                     {"minh", 1.55}, {"label", Value(A({localize("b_play_cap")}))}, {"scale", text_scale * 2}, {"col", true}}),
                                node(UIT_C, vl::T({{"align", "cm"}}),
                                     A({btn({{"button", "options"}, {"colour", col("ORANGE")}, {"minw", 2.65}, {"minh", 1.35},
                                             {"label", Value(A({localize("b_options_cap")}))}, {"scale", text_scale * 1.2}, {"col", true}}),
                                        quit_spacer, quit_button})),
                                btn({{"id", "collection_button"}, {"button", "your_collection"}, {"colour", col("PALE_GREEN")}, {"minw", 3.65},
                                     {"minh", 1.55}, {"label", Value(A({localize("b_collection_cap")}))}, {"scale", text_scale * 1.5}, {"col", true}})}))})),
                node(UIT_C, vl::T({{"align", "br"}, {"minw", 3.2}, {"padding", 0.1}}), A({Value(), lang_row}))}))
            .table());
}


// ------------------------------------------------------------------ generic option screens
TableRef create_UIBox_generic_options(const TableRef &args) {
    Value back_func = args->get(K("back_func")).truthy() ? args->get(K("back_func")) : Value("exit_overlay_menu");
    Value contents = args->get(K("contents")).truthy()
                         ? args->get(K("contents"))
                         : node(UIT_T, vl::T({{"text", "EMPTY"}, {"colour", ui_col("RED")}, {"scale", 0.4}}));
    // (args.infotip fills overlay_menu_infotip from an event; no screen ported so far uses it)
    Value grey = col("GREY");
    Value bg = args->get(K("bg_colour")).truthy() ? args->get(K("bg_colour")) : Value(A({grey[1], grey[2], grey[3], 0.7}));
    Value back;
    if (!args->get(K("no_back")).truthy()) {
        Value back_id = args->get(K("back_id"));
        Value pip_args;
        if (!args->get(K("no_pip")).truthy())
            pip_args = Value(vl::T({{"button", args->get(K("back_button")).truthy() ? args->get(K("back_button")) : Value("b")}}));
        back = node(UIT_R,
                    vl::T({{"id", back_id.truthy() ? back_id : Value("overlay_menu_back_button")},
                           {"align", "cm"},
                           {"minw", 2.5},
                           {"minh", 0.8},
                           {"button_delay", args->get(K("back_delay"))},
                           {"padding", 0.1},
                           {"r", 0.1},
                           {"hover", true},
                           {"colour", args->get(K("back_colour")).truthy() ? args->get(K("back_colour")) : col("ORANGE")},
                           {"button", back_func},
                           {"shadow", true},
                           {"focus_args", Value(vl::T({{"nav", "wide"}, {"button", "b"}, {"snap_to", args->get(K("snap_back"))}}))}}),
                    A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}, {"no_fill", true}}),
                            A({node(UIT_T, vl::T({{"id", back_id},
                                                  {"text", args->get(K("back_label")).truthy() ? args->get(K("back_label")) : localize("b_back")},
                                                  {"scale", 0.5},
                                                  {"colour", ui_col("TEXT_LIGHT")},
                                                  {"shadow", true},
                                                  {"func", args->get(K("no_pip")).truthy() ? Value() : Value("set_button_pip")},
                                                  {"focus_args", pip_args}}))}))}));
    }
    Value padding = args->get(K("padding")).truthy() ? args->get(K("padding")) : Value(0.2);
    Value minw = args->get(K("minw")).truthy() ? args->get(K("minw")) : Value(7);
    Value inner = node(UIT_C,
                       vl::T({{"align", "cm"}, {"minh", 1}, {"r", 0.2}, {"padding", 0.15}, {"minw", 1},
                              {"colour", args->get(K("colour")).truthy() ? args->get(K("colour")) : col("L_BLACK")}}),
                       A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", padding}, {"minw", minw}}), TableRef(contents.table())), back}));
    Value frame = node(UIT_R,
                       vl::T({{"align", "cm"}, {"minh", 1}, {"r", 0.3}, {"padding", 0.07}, {"minw", 1},
                              {"colour", args->get(K("outline_colour")).truthy() ? args->get(K("outline_colour")) : col("JOKER_GREY")},
                              {"emboss", 0.1}}),
                       A({inner}));
    Value infotip = node(UIT_R, vl::T({{"align", "cm"}}),
                         A({node(UIT_O, vl::T({{"id", "overlay_menu_infotip"}, {"object", obj(new Moveable(0, 0, 0, 0))}}))}));
    return TableRef(node(UIT_ROOT,
                         vl::T({{"align", "cm"}, {"minw", G.ROOM->T->w * 5}, {"minh", G.ROOM->T->h * 5}, {"padding", 0.1}, {"r", 0.1}, {"colour", bg}}),
                         A({frame, infotip}))
                        .table());
}

TableRef create_UIBox_options() {
    EventConfig e = ev_immediate([] {
        G.REFRESH_ALERTS = true;
        return true;
    });
    e.blockable = false;
    G.E_MANAGER->add_event(std::move(e));
    auto btn = [](std::initializer_list<std::pair<const char *, Value>> kv) { return Value(UIBox_button(vl::T(kv))); };
    Value restart, main_menu, your_collection, credits, current_seed;
    if (G.STAGE == RUN) {
        restart = btn({{"id", "restart_button"}, {"label", Value(A({localize("b_start_new_run")}))}, {"button", "setup_run"}, {"minw", 5}});
        main_menu = btn({{"label", Value(A({localize("b_main_menu")}))}, {"button", "go_to_menu"}, {"minw", 5}});
        your_collection = btn({{"label", Value(A({localize("b_collection")}))}, {"button", "your_collection"}, {"minw", 5}, {"id", "your_collection"}});
        // TODO(run): the seeded run row (current_seed)
    }
    if (G.STAGE == MAIN_MENU) credits = btn({{"label", Value(A({localize("b_credits")}))}, {"button", "show_credits"}, {"minw", 5}});
    Value settings = btn({{"button", "settings"}, {"label", Value(A({localize("b_settings")}))}, {"minw", 5},
                          {"focus_args", Value(vl::T({{"snap_to", true}}))}});
    Value high_scores = btn({{"label", Value(A({localize("b_stats")}))}, {"button", "high_scores"}, {"minw", 5}});
    Value customize = btn({{"label", Value(A({localize("b_customize_deck")}))}, {"button", "customize_deck"}, {"minw", 5}});
    bool seeded = fld(Value(G.GAME), "seeded").truthy();
    return create_UIBox_generic_options(vl::T({{"contents", Value(A({settings, seeded ? current_seed : Value(), restart, main_menu, high_scores,
                                                                     your_collection, customize, credits}))}}));
}

namespace {
Value settings_tab_func() {
    return Value((Object *)new Func([](const Value &tab) { return Value(settings_tab(tab)); }));
}
Value tab_entry(const char *label, const char *arg, bool chosen) {
    TableRef t = vl::T({{"label", localize(label)}, {"tab_definition_function", settings_tab_func()}, {"tab_definition_function_args", arg}});
    if (chosen) t->set("chosen", true);
    return Value(t);
}
}  // namespace

TableRef create_UIBox_settings() {
    TableRef tabs = new_table();
    tabs->push(tab_entry("b_set_game", "Game", true));
    if (G.F_VIDEO_SETTINGS) tabs->push(tab_entry("b_set_video", "Video", false));
    tabs->push(tab_entry("b_set_graphics", "Graphics", false));
    tabs->push(tab_entry("b_set_audio", "Audio", false));
    Value t = Value(create_tabs(vl::T({{"tabs", Value(tabs)}, {"tab_h", 7.7}, {"tab_alignment", "tm"}, {"snap_to_nav", true}})));
    return create_UIBox_generic_options(vl::T({{"back_func", "options"}, {"contents", Value(A({t}))}}));
}

TableRef settings_tab(const Value &tab) {
    auto root = [](TableRef nodes) {
        return TableRef(node(UIT_ROOT, vl::T({{"align", "cm"}, {"padding", 0.05}, {"colour", col("CLEAR")}}), nodes).table());
    };
    if (tab.equals("Game")) {
        double gs = G.SETTINGS->get(K("GAMESPEED")).num();
        Value rumble;
        if (G.F_RUMBLE)
            rumble = Value(create_toggle(vl::T({{"w", 1}, {"label", localize(G.F_MOBILE ? "b_set_vibration" : "b_set_rumble")},
                                                {"ref_table", Value(G.SETTINGS)}, {"ref_value", "rumble"}})));
        Value speed = Value(create_option_cycle(vl::T({{"label", localize("b_set_gamespeed")},
                                                       {"scale", 0.8},
                                                       {"options", Value(A({0.5, 1, 2, 4}))},
                                                       {"opt_callback", "change_gamespeed"},
                                                       {"current_option", gs == 0.5 ? 1 : gs == 4 ? 4 : gs + 1}})));
        Value playpos = Value(create_option_cycle(vl::T({{"w", 5},
                                                         {"label", localize("b_set_play_discard_pos")},
                                                         {"scale", 0.8},
                                                         {"options", localize("ml_play_discard_pos_opt")},
                                                         {"opt_callback", "change_play_discard_position"},
                                                         {"current_option", G.SETTINGS->get(K("play_button_pos"))}})));
        Value shake = Value(create_slider(vl::T({{"label", localize("b_set_screenshake")}, {"w", 4}, {"h", 0.4}, {"ref_table", Value(G.SETTINGS)},
                                                 {"ref_value", "screenshake"}, {"min", 0}, {"max", 100}})));
        Value stickers = Value(create_toggle(vl::T({{"label", localize("ph_display_stickers")}, {"ref_table", Value(G.SETTINGS)}, {"ref_value", "run_stake_stickers"}})));
        Value contrast = Value(create_toggle(vl::T({{"label", localize("b_high_contrast_cards")}, {"ref_table", Value(G.SETTINGS)},
                                                    {"ref_value", "colourblind_option"}, {"callback", "refresh_contrast_mode"}})));
        Value motion = Value(create_toggle(vl::T({{"label", localize("b_reduced_motion")}, {"ref_table", Value(G.SETTINGS)}, {"ref_value", "reduced_motion"}})));
        Value crash;  // (G.F_CRASH_REPORTS is off)
        return root(A({speed, playpos, rumble, shake, stickers, contrast, motion, crash}));
    }
    if (tab.equals("Audio")) {
        Value snd = G.SETTINGS->get(K("SOUND"));
        auto sl = [&](const char *label, const char *ref) {
            return Value(create_slider(vl::T({{"label", localize(label)}, {"w", 5}, {"h", 0.4}, {"ref_table", snd}, {"ref_value", ref}, {"min", 0}, {"max", 100}})));
        };
        return root(A({sl("b_set_master_vol", "volume"), sl("b_set_music_vol", "music_volume"), sl("b_set_game_vol", "game_sounds_volume")}));
    }
    if (tab.equals("Graphics")) {
        Value gr = G.SETTINGS->get(K("GRAPHICS"));
        Value shadows = Value(create_option_cycle(vl::T({{"w", 4}, {"scale", 0.8}, {"label", localize("b_set_shadows")}, {"options", localize("ml_shadow_opt")},
                                                         {"opt_callback", "change_shadows"}, {"current_option", fld(gr, "shadows").equals("On") ? 1 : 2}})));
        Value smoothing = Value(create_option_cycle(vl::T({{"w", 4}, {"scale", 0.8}, {"label", localize("b_set_pixel_smoothing")},
                                                           {"options", localize("ml_smoothing_opt")}, {"opt_callback", "change_pixel_smoothing"},
                                                           {"current_option", fld(gr, "texture_scaling")}})));
        Value crt = Value(create_slider(vl::T({{"label", localize("b_set_CRT")}, {"w", 4}, {"h", 0.4}, {"ref_table", gr}, {"ref_value", "crt"}, {"min", 0}, {"max", 100}})));
        return root(A({shadows, smoothing, crt}));
    }
    // (the Video tab is never offered: G.F_VIDEO_SETTINGS is off on the Vita)
    return TableRef(node(UIT_ROOT, vl::T({{"align", "cm"}, {"padding", 0.05}, {"colour", col("CLEAR")}, {"minh", 5}, {"minw", 5}}), A({})).table());
}

// ------------------------------------------------------------------ controls
namespace {
void set_default(const TableRef &args, const char *k, Value d) {
    if (!args->get(Value(intern(k))).truthy()) args->set(k, d);
}
Value info_rows(const Value &info, double scale) {
    Table *inf = info.table();
    if (!inf) return Value();
    TableRef rows = new_table();
    for (int i = 1; i <= inf->len(); i++)
        rows->push(node(UIT_R, vl::T({{"align", "cm"}, {"minh", 0.05}}),
                        A({node(UIT_T, vl::T({{"text", inf->get(i)}, {"scale", scale}, {"colour", ui_col("TEXT_LIGHT")}}))})));
    return node(UIT_R, vl::T({{"align", "cm"}, {"minh", 0.05}}), rows);
}
Value shoulder_pip(const char *button, double x) {
    return node(UIT_C,
                vl::T({{"minw", 0.7}, {"align", "cm"}, {"colour", col("CLEAR")}, {"func", "set_button_pip"},
                       {"focus_args", Value(vl::T({{"button", button}, {"type", "none"}, {"orientation", "cm"}, {"scale", 0.7},
                                                   {"offset", Value(vl::T({{"x", x}, {"y", 0}}))}}))}}),
                A({}));
}
}  // namespace

TableRef create_slider(const TableRef &args) {
    set_default(args, "colour", col("RED"));
    set_default(args, "w", 1);
    set_default(args, "h", 0.5);
    set_default(args, "label_scale", 0.5);
    set_default(args, "text_scale", 0.3);
    set_default(args, "min", 0);
    set_default(args, "max", 1);
    set_default(args, "decimal_places", 0);
    double v = args->get(K("ref_table")).get(args->get(K("ref_value"))).num();
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", (int)args->get(K("decimal_places")).num(), v);
    args->set("text", Value(std::string(buf)));
    double w = args->get(K("w")).num(), h = args->get(K("h")).num();
    double mn = args->get(K("min")).num(), mx = args->get(K("max")).num();
    double startval = w * (v - mn) / (mx - mn);
    Value bar = node(UIT_C,
                     vl::T({{"align", "cl"}, {"minw", w}, {"r", 0.1}, {"min_h", h}, {"collideable", true}, {"hover", true}, {"colour", col("BLACK")},
                            {"emboss", 0.05}, {"func", "slider"}, {"refresh_movement", true}}),
                     A({node(UIT_B, vl::T({{"w", startval}, {"h", h}, {"r", 0.1}, {"colour", args->get(K("colour"))}, {"ref_table", Value(args)},
                                           {"refresh_movement", true}}))}));
    Value label = node(UIT_C, vl::T({{"align", "cm"}, {"minh", h}, {"r", 0.1}, {"minw", 0.8}, {"colour", args->get(K("colour"))}, {"shadow", true}}),
                       A({node(UIT_T, vl::T({{"ref_table", Value(args)}, {"ref_value", "text"}, {"scale", args->get(K("text_scale"))},
                                             {"colour", ui_col("TEXT_LIGHT")}, {"decimal_places", args->get(K("decimal_places"))}}))}));
    Value t = node(UIT_C, vl::T({{"align", "cm"}, {"minw", w}, {"min_h", h}, {"padding", 0.1}, {"r", 0.1}, {"colour", col("CLEAR")},
                                 {"focus_args", Value(vl::T({{"type", "slider"}}))}}),
                   A({bar, label}));
    if (args->get(K("label")).truthy()) {
        double ls = args->get(K("label_scale")).num();
        t = node(UIT_R, vl::T({{"align", "cm"}, {"minh", 1}, {"minw", 1}, {"padding", 0.1 * ls}, {"colour", col("CLEAR")}}),
                 A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}}),
                         A({node(UIT_T, vl::T({{"text", args->get(K("label"))}, {"scale", ls}, {"colour", ui_col("TEXT_LIGHT")}}))})),
                    node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}}), A({t}))}));
    }
    return TableRef(t.table());
}

TableRef create_toggle(const TableRef &args) {
    set_default(args, "active_colour", col("RED"));
    set_default(args, "inactive_colour", col("BLACK"));
    set_default(args, "w", 3);
    set_default(args, "h", 0.5);
    set_default(args, "scale", 1);
    set_default(args, "label", "TEST?");
    set_default(args, "label_scale", 0.4);
    set_default(args, "ref_table", Value(new_table()));
    set_default(args, "ref_value", "test");
    double scale = args->get(K("scale")).num(), w = args->get(K("w")).num();
    Sprite *check = icon_sprite(0.5f * (float)scale, 0.5f * (float)scale, 1, 0);
    check->states.visible = false;
    Value info = info_rows(args->get(K("info")), 0.25);
    int uit = args->get(K("col")).truthy() ? UIT_C : UIT_R;
    Value box = node(UIT_C,
                     vl::T({{"align", "cm"}, {"r", 0.1}, {"padding", 0.03}, {"minw", 0.4 * scale}, {"minh", 0.4 * scale}, {"outline_colour", col("WHITE")},
                            {"outline", 1.2 * scale}, {"line_emboss", 0.5 * scale}, {"ref_table", Value(args)}, {"colour", args->get(K("inactive_colour"))},
                            {"button", "toggle_button"}, {"button_dist", 0.2}, {"hover", true}, {"toggle_callback", args->get(K("callback"))},
                            {"func", "toggle"}, {"focus_args", Value(vl::T({{"funnel_to", true}}))}}),
                     A({node(UIT_O, vl::T({{"object", obj(check)}}))}));
    Value t = node(uit, vl::T({{"align", "cm"}, {"padding", 0.1}, {"r", 0.1}, {"colour", col("CLEAR")}, {"focus_args", Value(vl::T({{"funnel_from", true}}))}}),
                   A({node(UIT_C, vl::T({{"align", "cr"}, {"minw", w}}),
                           A({node(UIT_T, vl::T({{"text", args->get(K("label"))}, {"scale", args->get(K("label_scale"))}, {"colour", ui_col("TEXT_LIGHT")}})),
                              node(UIT_B, vl::T({{"w", 0.1}, {"h", 0.1}}))})),
                      node(UIT_C, vl::T({{"align", "cl"}, {"minw", 0.3 * w}}), A({node(UIT_C, vl::T({{"align", "cm"}, {"r", 0.1}, {"colour", col("BLACK")}}), A({box}))}))}));
    if (info.truthy()) t = node(uit, vl::T({{"align", "cm"}}), A({t, info}));
    return TableRef(t.table());
}

TableRef create_option_cycle(const TableRef &args) {
    set_default(args, "colour", col("RED"));
    set_default(args, "options", Value(A({"Option 1", "Option 2"})));
    set_default(args, "current_option", 1);
    Table *opts = args->get(K("options")).table();
    int n = opts->len();
    int cur = (int)args->get(K("current_option")).num();
    args->set("current_option_val", opts->get(cur));
    set_default(args, "scale", 1);
    double scale = args->get(K("scale")).num();
    args->set("w", (args->get(K("w")).truthy() ? args->get(K("w")).num() : 2.5) * scale);
    args->set("h", (args->get(K("h")).truthy() ? args->get(K("h")).num() : 0.8) * scale);
    args->set("text_scale", (args->get(K("text_scale")).truthy() ? args->get(K("text_scale")).num() : 0.5) * scale);
    args->set("l", "<");
    args->set("r", ">");
    set_default(args, "focus_args", Value(new_table()));
    args->get(K("focus_args")).table()->set("type", "cycle");

    Value info = info_rows(args->get(K("info")), 0.3 * scale);
    bool disabled = n < 2;
    TableRef pips = new_table();
    for (int i = 1; i <= n; i++)
        pips->push(node(UIT_B, vl::T({{"w", 0.1 * scale}, {"h", 0.1 * scale}, {"r", 0.05}, {"id", Value("pip_" + std::to_string(i))},
                                      {"colour", cur == i ? col("WHITE") : col("BLACK")}})));
    Value choice_pips;
    if (!args->get(K("no_pips")).truthy()) choice_pips = node(UIT_R, vl::T({{"align", "cm"}, {"padding", (0.05 - (n > 15 ? 0.03 : 0)) * scale}}), pips);
    double ts = args->get(K("text_scale")).num();
    auto arrow = [&](const char *side) {
        return node(UIT_C,
                    vl::T({{"align", "cm"}, {"r", 0.1}, {"minw", (G.F_MOBILE ? 1.2 : 0.6) * scale}, {"hover", !disabled},
                           {"colour", !disabled ? args->get(K("colour")) : col("BLACK")}, {"shadow", !disabled},
                           {"button", !disabled ? Value("option_cycle") : Value()}, {"ref_table", Value(args)}, {"ref_value", side},
                           {"focus_args", Value(vl::T({{"type", "none"}}))}}),
                    A({node(UIT_T, vl::T({{"ref_table", Value(args)}, {"ref_value", side}, {"scale", ts},
                                          {"colour", !disabled ? ui_col("TEXT_LIGHT") : ui_col("TEXT_INACTIVE")}}))}));
    };
    Value mid;
    if (args->get(K("mid")).truthy()) {
        mid = node(UIT_C, vl::T({{"id", "cycle_main"}}),
                   A({node(UIT_R, vl::T({{"align", "cm"}, {"minh", 0.05}}), A({args->get(K("mid"))})), !disabled ? choice_pips : Value()}));
    } else {
        DynaText *dt = new DynaText(vl::T({{"string", Value(A({Value(vl::T({{"ref_table", Value(args)}, {"ref_value", "current_option_val"}}))}))},
                                           {"colours", Value(A({ui_col("TEXT_LIGHT")}))},
                                           {"pop_in", 0},
                                           {"pop_in_rate", 8},
                                           {"reset_pop_in", true},
                                           {"shadow", true},
                                           {"float", true},
                                           {"silent", true},
                                           {"bump", true},
                                           {"scale", ts},
                                           {"non_recalc", true}}));
        Value text_rows = node(UIT_R, vl::T({{"align", "cm"}}),
                               A({node(UIT_R, vl::T({{"align", "cm"}}), A({node(UIT_O, vl::T({{"object", obj(dt)}}))})),
                                  node(UIT_R, vl::T({{"align", "cm"}, {"minh", 0.05}}), A({})), !disabled ? choice_pips : Value()}));
        mid = node(UIT_C,
                   vl::T({{"id", "cycle_main"}, {"align", "cm"}, {"minw", args->get(K("w"))}, {"minh", args->get(K("h"))}, {"r", 0.1},
                          {"padding", 0.05}, {"colour", args->get(K("colour"))}, {"emboss", 0.1}, {"hover", true}, {"can_collide", true},
                          {"on_demand_tooltip", args->get(K("on_demand_tooltip"))}}),
                   A({text_rows}));
    }
    Value id = args->get(K("id"));
    Value t = node(UIT_C,
                   vl::T({{"align", "cm"}, {"padding", 0.1}, {"r", 0.1}, {"colour", col("CLEAR")},
                          {"id", id.truthy() && !args->get(K("label")).truthy() ? id : Value()}, {"focus_args", args->get(K("focus_args"))}}),
                   A({arrow("l"), mid, arrow("r")}));
    if (args->get(K("cycle_shoulders")).truthy()) {
        t = node(UIT_R, vl::T({{"align", "cm"}, {"colour", col("CLEAR")}}),
                 A({shoulder_pip("leftshoulder", -0.1), node(UIT_C, vl::T({{"id", "cycle_shoulders"}, {"padding", 0.1}}), A({t})),
                    shoulder_pip("rightshoulder", 0.1)}));
    } else {
        t = node(UIT_R, vl::T({{"align", "cm"}, {"colour", col("CLEAR")}, {"padding", 0.0}}), A({t}));
    }
    if (args->get(K("label")).truthy() || args->get(K("info")).truthy()) {
        Value label;
        if (args->get(K("label")).truthy())
            label = node(UIT_R, vl::T({{"align", "cm"}}),
                         A({node(UIT_T, vl::T({{"text", args->get(K("label"))}, {"scale", 0.5 * scale}, {"colour", ui_col("TEXT_LIGHT")}}))}));
        t = node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.05}, {"id", id}}), A({label, t, info}));
    }
    return TableRef(t.table());
}

TableRef create_tabs(const TableRef &args) {
    set_default(args, "colour", col("RED"));
    set_default(args, "tab_alignment", "cm");
    set_default(args, "scale", 1);
    set_default(args, "tab_w", 0);
    set_default(args, "tab_h", 0);
    set_default(args, "text_scale", 0.5);
    double scale = args->get(K("scale")).num();
    Table *tabs = args->get(K("tabs")).table();
    TableRef tab_buttons = new_table();
    Value current_v;
    for (int k = 1; k <= tabs->len(); k++) {
        Value v = tabs->get(k);
        if (fld(v, "chosen").truthy()) {
            args->set("current", Value(vl::T({{"k", k}, {"v", v}})));
            current_v = v;
        }
        std::string label = fld(v, "label").truthy() ? fld(v, "label").to_string() : "";
        tab_buttons->push(Value(UIBox_button(vl::T({{"id", Value("tab_but_" + label)},
                                                    {"ref_table", v},
                                                    {"button", "change_tab"},
                                                    {"label", Value(A({fld(v, "label")}))},
                                                    {"minh", 0.8 * scale},
                                                    {"minw", 2.5 * scale},
                                                    {"col", true},
                                                    {"choice", true},
                                                    {"scale", args->get(K("text_scale"))},
                                                    {"chosen", fld(v, "chosen")},
                                                    {"func", fld(v, "func")},
                                                    {"focus_args", Value(vl::T({{"type", "none"}}))}}))));
    }
    bool shoulders = tabs->len() > 1 && !args->get(K("no_shoulders")).truthy();
    Value fa;
    if (tabs->len() > 1)
        fa = Value(vl::T({{"type", "tab"}, {"nav", "wide"}, {"snap_to", args->get(K("snap_to_nav"))}, {"no_loop", args->get(K("no_loop"))}}));
    Func *tdf = fld(current_v, "tab_definition_function").as<Func>();
    TableRef contents_def = tdf ? TableRef(tdf->call(fld(current_v, "tab_definition_function_args")).table()) : TableRef();
    UIBox *contents = new UIBox(contents_def, vl::T({{"offset", Value(vl::T({{"x", 0}, {"y", 0}}))}}));
    Value buttons = node(UIT_C,
                         vl::T({{"id", args->get(K("no_shoulders")).truthy() ? "no_shoulders" : "tab_shoulders"}, {"ref_table", Value(args)}, {"align", "cm"},
                                {"padding", 0.15}, {"group", 1}, {"collideable", true}, {"focus_args", fa}}),
                         tab_buttons);
    Value top = node(UIT_R, vl::T({{"align", "cm"}, {"colour", col("CLEAR")}}),
                     A({shoulders ? shoulder_pip("leftshoulder", -0.1) : Value(), buttons, shoulders ? shoulder_pip("rightshoulder", 0.1) : Value()}));
    Value body = node(UIT_R,
                      vl::T({{"align", args->get(K("tab_alignment"))}, {"padding", args->get(K("padding")).truthy() ? args->get(K("padding")) : Value(0.1)},
                             {"no_fill", true}, {"minh", args->get(K("tab_h"))}, {"minw", args->get(K("tab_w"))}}),
                      A({node(UIT_O, vl::T({{"id", "tab_contents"}, {"object", obj(contents)}}))}));
    return TableRef(node(UIT_R, vl::T({{"padding", 0.0}, {"align", "cm"}, {"colour", col("CLEAR")}}), A({top, body})).table());
}

TableRef create_button_binding_pip(const TableRef &args) {
    static const std::pair<const char *, int> kMap[] = {
        {"a", 0},          {"b", 1},           {"x", 2},          {"y", 3},         {"leftshoulder", 4}, {"rightshoulder", 5}, {"triggerleft", 6},
        {"triggerright", 7}, {"start", 8},      {"back", 9},       {"dpadup", 10},   {"dpadright", 11},   {"dpaddown", 12},      {"dpadleft", 13},
        {"left", 14},      {"right", 15},      {"leftstick", 16}, {"rightstick", 17}, {"guide", 19}};
    Value button = args->get(K("button"));
    int x = 0;
    for (auto &m : kMap)
        if (button.equals(m.first)) x = m.second;
    if (G.F_SWAP_AB_PIPS && (button.equals("a") || button.equals("b"))) x = 1 - x;
    const std::string &con = G.CONTROLLER->GAMEPAD_CONSOLE;
    SpritePos p;
    p.x = (float)x;
    p.y = con == "Nintendo" ? 2.f : con == "Playstation" ? (G.F_PS4_PLAYSTATION_GLYPHS ? 3.f : 1.f) : 0.f;
    float scale = args->get(K("scale")).truthy() ? (float)args->get(K("scale")).num() : 0.45f;
    Sprite *s = new Sprite(0, 0, scale, scale, &G.ASSET_ATLAS["gamepad_ui"], p);
    return TableRef(node(UIT_ROOT, vl::T({{"align", "cm"}, {"colour", col("CLEAR")}}), A({node(UIT_O, vl::T({{"object", obj(s)}}))})).table());
}

TableRef overlay_infotip(const Value &text_rows_in) {
    Value text_rows = text_rows_in.is_table() ? text_rows_in : Value(A({"ERROR"}));
    TableRef t = new_table();
    Table *rows = text_rows.table();
    for (int k = 1; k <= rows->len(); k++)
        t->push(node(UIT_R, vl::T({{"align", "cm"}}),
                     A({node(UIT_T, vl::T({{"text", rows->get(k)}, {"colour", ui_col("TEXT_LIGHT")}, {"scale", 0.45}, {"juice", true}, {"shadow", true},
                                           {"lang", rows->get(K("lang"))}}))})));
    return TableRef(node(UIT_ROOT, vl::T({{"align", "cm"}, {"colour", col("CLEAR")}, {"padding", 0.1}}), t).table());
}

TableRef language_selector() {
    std::vector<LangDef *> langs;
    for (auto &kv : G.LANGUAGES)
        if (!kv.second->omit) langs.push_back(kv.second.get());
    // table.sort(langs, a.label < b.label): the labels are unique, so any sort gives this order
    std::sort(langs.begin(), langs.end(), [](LangDef *a, LangDef *b) { return std::strcmp(a->label.c_str(), b->label.c_str()) < 0; });
    TableRef rows = new_table();
    TableRef row = new_table();
    for (size_t i = 0; i < langs.size(); i++) {
        LangDef *v = langs[i];
        int k = (int)i + 1;
        Value text = node(UIT_T, vl::T({{"text", Value(v->label)}, {"lang", obj(v)}, {"scale", 0.45}, {"colour", ui_col("TEXT_LIGHT")}, {"shadow", true}}));
        row->push(node(UIT_C,
                       vl::T({{"align", "cm"}, {"func", "beta_lang_alert"}, {"padding", 0.05}, {"r", 0.1}, {"minh", 0.7}, {"minw", 4.5},
                              {"button", v->beta ? "warn_lang" : "change_lang"}, {"ref_table", obj(v)}, {"colour", v->beta ? col("RED") : col("BLUE")},
                              {"hover", true}, {"shadow", true}, {"focus_args", Value(vl::T({{"snap_to", k == 1}}))}}),
                       A({node(UIT_R, vl::T({{"align", "cm"}}), A({text}))})));
        if (row->len() >= 3 || k == (int)langs.size()) {
            rows->push(node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.1}}), row));
            row = new_table();
        }
    }
    // (the language feedback link needs G.F_EXTERNAL_LINKS, which is off)
    return create_UIBox_generic_options(vl::T({{"contents", Value(A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.05}}), rows)}))}}));
}

}  // namespace vl
