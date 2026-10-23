// functions/UI_definitions.lua
#include "game/ui_definitions.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/lua_sort.h"
#include "engine/controller.h"
#include "engine/event.h"
#include "engine/particles.h"
#include "engine/sprite.h"
#include "engine/text.h"
#include "engine/ui.h"
#include "game/card.h"
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

void attention_text(TableRef args) {
    if (!args) args = new_table();
    auto get = [&](const char *k) { return args->get(Value(intern(k))); };
    if (!get("text").truthy()) args->set("text", "test");
    if (!get("scale").truthy()) args->set("scale", 1);
    args->set("colour", copy_table(get("colour").truthy() ? get("colour") : col("WHITE")));
    args->set("hold", get("hold").num() + 0.1 * G.SPEEDFACTOR);
    if (!get("pos").truthy()) args->set("pos", Value(vl::T({{"x", 0}, {"y", 0}})));
    if (!get("align").truthy()) args->set("align", "cm");
    args->set("fade", 1);
    if (get("cover").truthy()) {
        args->set("cover_colour", copy_table(get("cover_colour").truthy() ? get("cover_colour") : col("RED")));
        args->set("cover_colour_l", copy_table(Value(lighten(get("cover_colour"), 0.2f))));
        args->set("cover_colour_d", copy_table(Value(darken(get("cover_colour"), 0.2f))));
    } else {
        args->set("cover_colour", copy_table(col("CLEAR")));
    }
    args->set("uibox_config", Value(vl::T({{"align", get("align")},
                                           {"offset", get("offset").truthy() ? get("offset") : Value(vl::T({{"x", 0}, {"y", 0}}))},
                                           {"major", get("cover").truthy() ? get("cover") : get("major")}})));

    EventConfig e = ev_after(0, [args] {
        auto get = [&](const char *k) { return args->get(Value(intern(k))); };
        Moveable *cover = dynamic_cast<Moveable *>(get("cover").obj());
        const double cw = (cover ? cover->T->w : 0.001) + get("cover_padding").num(), ch = (cover ? cover->T->h : 0.001) + get("cover_padding").num();
        DynaText *dt = new DynaText(vl::T({{"scale", get("scale")}, {"string", get("text")}, {"maxw", get("maxw")}, {"colours", Value(A({get("colour")}))},
                                           {"float", true}, {"shadow", true}, {"silent", !get("noisy").truthy()}, {"pop_in", 0}, {"pop_in_rate", 6},
                                           {"rotate", get("rotate")}}));
        Value pos = get("pos");
        Transform T{(float)fld(pos, "x").num(), (float)fld(pos, "y").num(), 0, 0, 0, 1};
        UIBox *AT = new UIBox(TableRef(node(UIT_ROOT,
                                            vl::T({{"align", get("cover_align").truthy() ? get("cover_align") : Value("cm")}, {"minw", cw}, {"minh", ch},
                                                   {"padding", 0.03}, {"r", 0.1}, {"emboss", get("emboss")}, {"colour", get("cover_colour")}}),
                                            A({node(UIT_O, vl::T({{"draw_layer", 1}, {"object", obj(dt)}}))}))
                                           .table()),
                              TableRef(get("uibox_config").table()), &T);
        AT->attention_text = true;
        args->set("AT", obj(AT));
        args->set("text", obj(dt));
        dt->pulse(0.5);
        if (cover) {
            new Particles((float)fld(pos, "x").num(), (float)fld(pos, "y").num(), 0, 0,
                          vl::T({{"timer_type", "TOTAL"}, {"timer", 0.01}, {"pulse_max", 15}, {"max", 0}, {"scale", 0.3}, {"vel_variation", 0.2},
                                 {"padding", 0.1}, {"fill", true}, {"lifespan", 0.5}, {"speed", 2.5}, {"attach", obj(AT->UIRoot.get())},
                                 {"colours", Value(A({get("cover_colour"), get("cover_colour_l"), get("cover_colour_d")}))}}));
        }
        if (get("backdrop_colour").truthy()) {
            args->set("backdrop_colour", copy_table(get("backdrop_colour")));
            double bs = get("backdrop_scale").truthy() ? get("backdrop_scale").num() : 1;
            new Particles((float)fld(pos, "x").num(), (float)fld(pos, "y").num(), 0, 0,
                          vl::T({{"timer_type", "TOTAL"}, {"timer", 5}, {"scale", 2.4 * bs}, {"lifespan", 5}, {"speed", 0}, {"attach", obj(AT)},
                                 {"colours", Value(A({get("backdrop_colour")}))}}));
        }
        return true;
    });
    e.blockable = false;
    e.blocking = false;
    G.E_MANAGER->add_event(e);

    EventConfig e2 = ev_after(args->get(K("hold")).num(), [args] {
        auto get = [&](const char *k) { return args->get(Value(intern(k))); };
        if (!get("start_time").truthy()) {
            args->set("start_time", G.TIMERS.TOTAL);
            if (auto *dt = dynamic_cast<DynaText *>(get("text").obj())) dt->pop_out(3);
            return false;
        }
        double fade = std::max(0.0, 1 - 3 * (G.TIMERS.TOTAL - get("start_time").num()));
        args->set("fade", fade);
        for (const char *k : {"cover_colour", "backdrop_colour"})
            if (Table *c = get(k).table()) c->set(4, std::min(c->get(4).num(), (k[0] == 'c' ? 2 : 1) * fade));
        for (const char *k : {"cover_colour_l", "cover_colour_d", "colour"})
            if (Table *c = get(k).table()) c->set(4, std::min(c->get(4).num(), fade));
        if (fade <= 0) {
            if (auto *at = dynamic_cast<UIBox *>(get("AT").obj())) at->remove();
            return true;
        }
        return false;
    });
    e2.blockable = false;
    e2.blocking = false;
    G.E_MANAGER->add_event(e2);
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
    if (args->get(K("infotip")).truthy()) {
        Value infotip = args->get(K("infotip"));
        EventConfig e = ev_immediate([infotip] {
            if (UIBox *o = dynamic_cast<UIBox *>(G.OVERLAY_MENU.get()))
                if (UIElement *info = o->get_UIE_by_ID("overlay_menu_infotip")) {
                    if (Node *old = info->config->get(K("object")).as<Node>()) old->remove();
                    info->config->set("object", Value((Object *)new UIBox(overlay_infotip(infotip),
                                                                            vl::T({{"offset", Value(vl::T({{"x", 0}, {"y", 0}}))},
                                                                                   {"align", "bm"},
                                                                                   {"parent", Value((Object *)info)}}))));
                }
            return true;
        });
        e.blocking = false;
        e.blockable = false;
        e.has_timer = true;
        e.timer = EventTimer::REAL;
        G.E_MANAGER->add_event(std::move(e));
    }
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
        // Vitalatro: no localization exists for it; Spanish for the es_* languages, English otherwise
        bool es = G.LANG && G.LANG->key.compare(0, 2, "es") == 0;
        Value large = Value(create_toggle(vl::T({{"label", es ? "Men\u00fas grandes" : "Large menus"},
                                                 {"ref_table", Value(G.SETTINGS)}, {"ref_value", "vl_large_menus"}})));
        return root(A({shadows, smoothing, crt, large}));
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

// ------------------------------------------------------------------ static UI data
namespace {
// G.C.<path> for a colour reference written by the extractor ("UI.TEXT_LIGHT")
Value colour_at(std::string_view path) {
    Value v = Value(G.C);
    while (!path.empty()) {
        size_t dot = path.find('.');
        v = v.get(Value(intern(path.substr(0, dot))));
        path = dot == std::string_view::npos ? std::string_view() : path.substr(dot + 1);
    }
    return v;
}
}  // namespace

// A fresh copy of an extracted UI definition, with its colour references and builder markers
// ({__ui = 'UIBox_button', args = ...}) resolved.
Value ui_data(const Value &v) {
    Table *t = v.table();
    if (!t) return v;
    Value c = t->get(K("__C"));
    if (c.is_str()) return colour_at(c.str());
    Value ui = t->get(K("__ui"));
    if (ui.equals("UIBox_button")) return Value(UIBox_button(TableRef(ui_data(t->get(K("args"))).table())));
    if (ui.equals("create_option_cycle")) return Value(create_option_cycle(TableRef(ui_data(t->get(K("args"))).table())));
    TableRef out = new_table();
    t->each([&](const Value &k, const Value &val) { out->set(k, ui_data(val)); });
    return Value(out);
}

// ------------------------------------------------------------------ credits
namespace {
TableRef ui_credits() {
    static TableRef data;
    if (!data) data = load_data("ui_credits");
    return data;
}

TableRef multiline_credit_text(const Value &lines) {
    TableRef t = new_table();
    for (int i = 1; lines.get(Value(i)).truthy(); i++)
        t->push(node(UIT_R, vl::T({{"align", "cl"}, {"padding", 0}}),
                     A({node(UIT_T, vl::T({{"text", lines.get(Value(i))}, {"scale", 0.28}, {"colour", col("WHITE")}}))})));
    return t;
}

TableRef credits_collabs_tab() {
    if (!G.collab_credits) G.collab_credits = TableRef(ui_data(ui_credits()->get(K("collab_credits"))).table());
    Value middle = node(UIT_R, vl::T({{"align", "cm"}, {"minh", 4.8}, {"minw", 12.3}}),
                        A({node(UIT_O, vl::T({{"id", Value()}, {"func", "CREDITS_check_collab"}, {"object", obj(new Moveable(0, 0, 0, 0))}}))}));
    std::vector<std::string> names;
    G.collab_credits->each([&](const Value &k, const Value &) { names.push_back(k.to_string()); });
    std::sort(names.begin(), names.end());  // table.sort: the names are unique strings
    TableRef options = new_table();
    for (auto &n : names) options->push(Value(n));
    G.viewed_collab = names.empty() ? std::string() : names[0];
    return TableRef(
        node(UIT_ROOT,
             vl::T({{"align", "cm"}, {"padding", 0.2}, {"colour", col("L_BLACK")}, {"r", 0.1}, {"emboss", 0.05}, {"minh", 6}, {"minw", 10}}),
             A({node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.2}, {"outline_colour", col("JOKER_GREY")}, {"r", 0.1}, {"outline", 1}}),
                     A({Value(create_option_cycle(vl::T({{"options", Value(options)},
                                                         {"opt_callback", "change_viewed_collab"},
                                                         {"current_option", 1},
                                                         {"colour", col("RED")},
                                                         {"w", 4},
                                                         {"mid", middle}})))}))}))
            .table());
}
}  // namespace

TableRef viewed_collab_option() {
    if (G.viewed_collab.empty()) G.viewed_collab = "The Binding of Isaac";
    Value curr = G.collab_credits->get(Value(G.viewed_collab));
    if (!curr.truthy()) curr = G.collab_credits->get(Value("The Binding of Isaac"));
    SpritePos p;
    p.x = 2;
    Sprite *collab_sprite = new Sprite(0, 0, 0.8f * G.CARD_W, 0.8f * G.CARD_H, &G.ASSET_ATLAS[fld(curr, "art").to_string() + "_1"], p);

    Value ml = fld(curr, "ml_text");
    Value artist = fld(curr, "artist"), link = fld(curr, "artist_link");
    auto text_row = [](const Value &text, const Value &colour) {
        return node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}}),
                    A({node(UIT_T, vl::T({{"text", text}, {"scale", 0.3}, {"colour", colour}}))}));
    };
    Value art_box = artist.truthy()
                        ? node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.07}, {"r", 0.1}, {"outline", 0.7}, {"outline_colour", col("WHITE")}}),
                               A({text_row("Art created by:", col("WHITE")), text_row(artist, col("GOLD")),
                                  link.truthy() ? text_row(link, col("BLUE")) : Value()}))
                        : Value();
    TableRef rights = multiline_credit_text(Value(A({"All rights reserved. No part of this work may be",
                                                     "reproduced in any form or by any means\xE2\x80\x94 graphic,",
                                                     "electronic, or mechanical, including recording,",
                                                     "online distribution, or information storage and retrieval",
                                                     "systems\xE2\x80\x94without the written permission of the publisher",
                                                     "or the designated rightsholder, as applicable."})));
    return TableRef(
        node(UIT_ROOT, vl::T({{"align", "cm"}, {"colour", col("BLACK")}, {"r", 0.1}}),
             A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.1}}),
                     A({node(UIT_T, vl::T({{"text", Value(G.viewed_collab)}, {"scale", 0.5}, {"colour", col("WHITE")}}))})),
                node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}}),
                     A({node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.1}, {"minw", 9}, {"minh", 4.2}}),
                             A({node(UIT_R, vl::T({{"align", "cl"}, {"padding", 0}}), rights),
                                ml.truthy() ? node(UIT_R, vl::T({{"align", "cl"}, {"padding", 0}}), multiline_credit_text(ml)) : Value()})),
                        node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.1}, {"minw", 3}, {"minh", 4.2}}),
                             A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.0}}),
                                     A({node(UIT_R, vl::T({{"align", "cm"}, {"colour", col("WHITE")}, {"r", 0.3}}),
                                             A({node(UIT_O, vl::T({{"colour", col("BLUE")},
                                                                   {"object", obj(collab_sprite)},
                                                                   {"hover", true},
                                                                   {"can_collide", false}}))}))})),
                                art_box}))}))}))
            .table());
}

TableRef credits() {
    TableRef tabs = new_table();
    ui_credits()->get(K("tabs")).table()->each([&](const Value &, const Value &tab) {
        Value def = fld(tab, "definition");
        Func *f = def.truthy() ? new Func([def](const Value &) { return ui_data(def); })
                               : new Func([](const Value &) { return Value(credits_collabs_tab()); });
        TableRef t = vl::T({{"label", fld(tab, "label")}, {"tab_definition_function", Value((Object *)f)}});
        if (fld(tab, "chosen").truthy()) t->set("chosen", true);
        tabs->push(Value(t));
    });
    return create_UIBox_generic_options(vl::T(
        {{"contents", Value(A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}}),
                                    A({Value(create_tabs(vl::T({{"tabs", Value(tabs)}, {"snap_to_nav", true}})))}))}))}}));
}

// ------------------------------------------------------------------ customize deck
TableRef create_UIBox_customize_deck() {
    auto tab = [](const char *suit, bool chosen) {
        TableRef t = vl::T({{"label", localize(suit, "suits_plural")},
                            {"tab_definition_function", Value((Object *)new Func([](const Value &s) { return Value(custom_deck_tab(s.str())); }))},
                            {"tab_definition_function_args", suit}});
        if (chosen) t->set("chosen", true);
        return Value(t);
    };
    return create_UIBox_generic_options(vl::T(
        {{"back_func", "options"},
         {"contents",
          Value(A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}}),
                        A({Value(create_tabs(vl::T({{"tabs", Value(A({tab("Spades", true), tab("Hearts", false), tab("Clubs", false), tab("Diamonds", false)}))},
                                                    {"snap_to_nav", true},
                                                    {"no_shoulders", true}})))}))}))}}));
}

TableRef custom_deck_tab(std::string_view suit) {
    TableRef t = new_table();
    CardArea *face_cards = new CardArea(0, 0, 4 * G.CARD_W, 1.4f * G.CARD_H, vl::T({{"card_limit", 3}, {"type", "title"}, {"highlight_limit", 0}}));
    t->push(node(UIT_R, vl::T({{"align", "cm"}, {"colour", col("BLACK")}, {"r", 0.1}, {"padding", 0.07}, {"no_fill", true}}),
                 A({node(UIT_O, vl::T({{"object", obj(face_cards)}}))})));

    // the localized names are keyed "1", "2", ...: numbered for the cycle (G.F_BABYNAMES is off)
    TableRef loc_options = new_table();
    if (Table *lo = localize(suit, "collabs").table())
        lo->each([&](const Value &k, const Value &v) { loc_options->set(Value(std::strtod(k.to_string().c_str(), nullptr)), v); });

    int current_option = 1;
    Value custom = fld(fld(Value(G.SETTINGS), "CUSTOM_DECK"), "Collabs").get(Value(suit));
    if (Table *opts = fld(fld(Value(G.COLLABS), "options"), std::string(suit).c_str()).table())
        opts->each([&](const Value &k, const Value &v) {
            if (custom.is_str() && v.is_str() && custom.str() == v.str()) current_option = (int)k.num();
        });

    t->push(node(UIT_R, vl::T({{"align", "cm"}}),
                 A({Value(create_option_cycle(vl::T({{"options", Value(loc_options)},
                                                     {"w", 5.5},
                                                     {"cycle_shoulders", true},
                                                     {"curr_suit", Value(std::string(suit))},
                                                     {"opt_callback", "change_collab"},
                                                     {"current_option", current_option},
                                                     {"colour", col("RED")},
                                                     {"focus_args", Value(vl::T({{"snap_to", true}, {"nav", "wide"}}))}})))})));
    t->push(Value(create_toggle(vl::T({{"label", localize("b_high_contrast_cards")},
                                       {"ref_table", Value(G.SETTINGS)},
                                       {"ref_value", "colourblind_option"},
                                       {"callback", "refresh_contrast_mode"}}))));

    for (const char *face : {"K", "Q", "J"}) {
        std::string card_code = std::string(suit.substr(0, 1)) + "_" + face;
        Card *card = new Card(0, 0, G.CARD_W * 1.2f, G.CARD_H * 1.2f, G.P_CARDS->get(Value(card_code)), G.P_CENTERS->get(K("c_base")));
        card->no_ui = true;
        face_cards->emplace(card);
    }
    return TableRef(
        node(UIT_ROOT, vl::T({{"align", "cm"}, {"padding", 0}, {"colour", col("CLEAR")}, {"r", 0.1}, {"minw", 7}, {"minh", 4.2}}), t).table());
}

// ------------------------------------------------------------------ stats
namespace {
Value profile_of(const Value &n) { return G.PROFILES->get(n); }
Value current_profile() { return profile_of(G.SETTINGS->get(K("profile"))); }

Value dyna(const std::string &s, const Value &colour, double scale) {
    return node(UIT_O, vl::T({{"object", obj(new DynaText(vl::T({{"string", Value(A({Value(s)}))},
                                                                 {"colours", Value(A({colour}))},
                                                                 {"shadow", true},
                                                                 {"float", true},
                                                                 {"scale", scale}})))}}));
}
std::string fmt_int(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.14g", v);
    return b;
}
std::string percent(double tally, double of) { return fmt_int(std::floor(0.01 + 100 * tally / of)) + "%"; }
}  // namespace

// functions/misc_functions.lua score_number_scale
double score_number_scale(double scale, const Value &amt) {
    const double E_SWITCH_POINT = 100000000000.0;
    if (!amt.is_num()) return 0.7 * scale;
    double a = amt.num();
    if (a >= E_SWITCH_POINT) return 0.7 * scale;
    if (a >= 1000000) return 14 * 0.75 / (std::floor(std::log(a)) + 4) * scale;
    return 0.75 * scale;
}

TableRef create_progress_box(const Value &profile_progress, bool smaller) {
    Value pp = profile_progress.truthy() ? profile_progress : fld(current_profile(), "progress");
    Table *p = pp.table();
    auto ratio = [&](const char *k) { return fld(pp, k)["tally"].num() / fld(pp, k)["of"].num(); };
    // (G.FTP_LOCKED is never set)
    p->set("overall_tally", ratio("discovered") + ratio("deck_stakes") + ratio("joker_stickers") + ratio("challenges"));
    p->set("overall_of", 4);

    double text_scale = smaller ? 0.7 : 1;
    Value bar_colour = fld(profile_of(Value(G.focused_profile)), "all_unlocked").truthy() ? col("RED") : col("BLUE");

    TableRef rows = new_table();
    for (const char *v : {"collection", "challenges", "joker_stickers", "deck_stake_wins"}) {
        const char *k = std::strcmp(v, "collection") == 0        ? "discovered"
                        : std::strcmp(v, "deck_stake_wins") == 0 ? "deck_stakes"
                                                                 : v;
        Value tab = fld(pp, k);
        double tally = fld(tab, "tally").num(), of = fld(tab, "of").num();
        TableRef filling = A({dyna(percent(tally, of), col("WHITE"), 0.55 * text_scale),
                              node(UIT_T, vl::T({{"text", Value(" (" + fmt_int(tally) + "/" + fmt_int(of) + ")")},
                                                 {"scale", 0.35},
                                                 {"colour", col("JOKER_GREY")}}))});
        rows->push(node(
            UIT_R, vl::T({{"align", "cm"}, {"padding", 0.05}, {"r", 0.1}, {"colour", Value(darken(col("JOKER_GREY"), 0.1f))}, {"emboss", 0.05}}),
            A({node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.05}, {"minw", 3.5 * text_scale}, {"maxw", 3.5 * text_scale}}),
                    A({node(UIT_T, vl::T({{"text", localize(std::string("k_") + v)},
                                          {"scale", 0.5 * text_scale},
                                          {"colour", ui_col("TEXT_LIGHT")},
                                          {"shadow", true}}))})),
               node(UIT_C,
                    vl::T({{"align", "cl"},
                           {"minh", smaller ? 0.5 : 0.8},
                           {"r", 0.1},
                           {"minw", 3.5 * text_scale},
                           {"colour", col("BLACK")},
                           {"emboss", 0.05},
                           {"progress_bar", Value(vl::T({{"max", of},
                                                         {"ref_table", tab},
                                                         {"ref_value", "tally"},
                                                         {"empty_col", col("BLACK")},
                                                         {"filled_col", Value(adjust_alpha(bar_colour, 0.5f))}}))}}),
                    A({node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.05}, {"r", 0.1}, {"minw", 3.5 * text_scale}}), filling)}))})));
    }
    return TableRef(
        node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.05}, {"r", 0.1}, {"colour", col("BLACK")}, {"emboss", 0.05}}),
             A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.05}}),
                     A({node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.05}, {"minw", 3.5 * text_scale}, {"maxw", 3.5 * text_scale}}),
                             A({node(UIT_T, vl::T({{"text", localize("k_progress")}, {"scale", 0.5}, {"colour", ui_col("TEXT_LIGHT")}, {"shadow", true}}))})),
                        node(UIT_C,
                             vl::T({{"align", "cl"},
                                    {"minh", 0.6},
                                    {"r", 0.1},
                                    {"minw", 3.5 * text_scale},
                                    {"colour", col("BLACK")},
                                    {"emboss", 0.05},
                                    {"progress_bar", Value(vl::T({{"max", fld(pp, "overall_of")},
                                                                  {"ref_table", pp},
                                                                  {"ref_value", "overall_tally"},
                                                                  {"empty_col", col("BLACK")},
                                                                  {"filled_col", Value(adjust_alpha(bar_colour, 0.5f))}}))}}),
                             A({node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.05}, {"r", 0.1}, {"minw", 3.5 * text_scale}}),
                                     A({dyna(percent(fld(pp, "overall_tally").num(), fld(pp, "overall_of").num()), col("WHITE"), 0.55)}))}))})),
                node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.05}}), rows)}))
            .table());
}

Value create_UIBox_high_scores_row(const char *score) {
    Value prof = current_profile();
    Value hs = fld(fld(prof, "high_scores"), score);
    if (!hs.truthy()) return Value();
    double label_scale = 0.65 - 0.025 * std::max<double>((double)fld(hs, "label").to_string().size() - 8, 0);
    const double label_w = 3.5, score_w = 4, h = 0.8;
    Value amt = fld(hs, "amt");
    TableRef score_tab;
    if (std::strcmp(score, "poker_hand") == 0) {
        Value handname = localize("k_none");
        double amount = 0;
        if (Table *hu = fld(prof, "hand_usage").table())
            hu->each([&](const Value &, const Value &v) {
                if (fld(v, "count").num() > amount) {
                    handname = fld(v, "order");
                    amount = fld(v, "count").num();
                }
            });
        score_tab = A({dyna((amount < 1 ? handname : localize(handname.str(), "poker_hands")).to_string(), col("WHITE"), 0.55),
                       node(UIT_T, vl::T({{"text", Value(" (" + fmt_int(amount) + ")")}, {"scale", 0.45}, {"colour", col("JOKER_GREY")}}))});
    } else if (std::strcmp(score, "most_money") == 0) {
        score_tab = A({dyna(localize("$").to_string() + number_format(amt), col("MONEY"), score_number_scale(0.85, amt))});
    } else if (std::strcmp(score, "win_streak") == 0) {
        Value cur = fld(fld(fld(prof, "high_scores"), "current_streak"), "amt");
        score_tab = A({dyna(number_format(amt), col("WHITE"), score_number_scale(0.85, amt)),
                       node(UIT_T, vl::T({{"text", Value(" (" + cur.to_string() + ")")}, {"scale", 0.45}, {"colour", col("JOKER_GREY")}}))});
    } else if (std::strcmp(score, "hand") == 0) {
        Sprite *chip_sprite = new Sprite(0, 0, 0.4f, 0.4f,
                                         &G.ASSET_ATLAS[G.SETTINGS->get(K("colourblind_option")).truthy() ? "ui_2" : "ui_1"], SpritePos{});
        chip_sprite->states.drag.can = false;
        score_tab = A({node(UIT_C, vl::T({{"align", "cm"}}), A({node(UIT_O, vl::T({{"w", 0.4}, {"h", 0.4}, {"object", obj(chip_sprite)}}))})),
                       node(UIT_C, vl::T({{"align", "cm"}}),
                            A({dyna(number_format(amt), col("RED"), std::min(0.75, score_number_scale(1.5, amt)))}))});
    } else if (std::strcmp(score, "collection") == 0) {
        double tot = fld(hs, "tot").num();
        score_tab = A({node(UIT_C, vl::T({{"align", "cm"}}),
                            A({dyna("%" + fmt_int(std::floor(0.01 + 100 * amt.num() / tot)), col("WHITE"), std::min(0.75, score_number_scale(1.5, amt))),
                               node(UIT_T, vl::T({{"text", Value(" (" + amt.to_string() + "/" + fmt_int(tot) + ")")},
                                                  {"scale", 0.45},
                                                  {"colour", col("JOKER_GREY")}}))}))});
    } else {
        score_tab = A({dyna(number_format(amt), col("FILTER"), score_number_scale(0.85, amt))});
    }
    return node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.05}, {"r", 0.1}, {"colour", Value(darken(col("JOKER_GREY"), 0.1f))}, {"emboss", 0.05}}),
                A({node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.05}, {"minw", label_w}, {"maxw", label_w}}),
                        A({node(UIT_T, vl::T({{"text", localize(score, "high_scores")},
                                              {"scale", label_scale},
                                              {"colour", ui_col("TEXT_LIGHT")},
                                              {"shadow", true}}))})),
                   node(UIT_C, vl::T({{"align", "cl"}, {"minh", h}, {"r", 0.1}, {"minw", score_w}, {"colour", col("BLACK")}, {"emboss", 0.05}}),
                        A({node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.05}, {"r", 0.1}, {"minw", score_w}, {"maxw", score_w}}), score_tab)}))}));
}

TableRef create_UIBox_high_scores() {
    fetch_achievements();
    set_profile_progress();
    set_discover_tallies();
    TableRef scores = A({create_UIBox_high_scores_row("hand"), create_UIBox_high_scores_row("furthest_round"),
                         create_UIBox_high_scores_row("furthest_ante"), create_UIBox_high_scores_row("poker_hand"),
                         create_UIBox_high_scores_row("most_money"), create_UIBox_high_scores_row("win_streak")});
    G.focused_profile = (int)G.SETTINGS->get(K("profile")).num();
    TableRef cheevs = new_table();
    return create_UIBox_generic_options(vl::T(
        {{"back_func", "options"},
         {"snap_back", true},
         {"contents",
          Value(A({node(UIT_C, vl::T({{"align", "cm"}, {"minw", 3}, {"padding", 0.2}, {"r", 0.1}, {"colour", col("CLEAR")}}),
                        A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.1}}), scores)})),
                   node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.1}, {"r", 0.1}, {"colour", col("CLEAR")}}),
                        A({Value(create_progress_box(Value(), false)),
                           Value(UIBox_button(vl::T({{"button", "usage"},
                                                     {"label", Value(A({localize("k_card_stats")}))},
                                                     {"minw", 7.5},
                                                     {"minh", 1},
                                                     {"focus_args", Value(vl::T({{"nav", "wide"}}))}})))})),
                   !G.F_NO_ACHIEVEMENTS ? node(UIT_C, vl::T({{"align", "cm"}, {"r", 0.1}, {"colour", col("CLEAR")}}), cheevs) : Value()}))}}));
}

TableRef create_UIBox_usage(const Value &args) {
    Value _type = args[1], _set = args[2];
    struct Used {
        double count;
        Value key;
    };
    std::vector<Used> used_cards;
    double max_amt = 0;
    if (Table *u = fld(current_profile(), std::string(_type.str()).c_str()).table())
        u->each([&](const Value &k, const Value &v) {
            Value c = G.P_CENTERS->get(k);
            if (c.truthy() && (!_set.truthy() || fld(c, "set").equals(_set.str())) && fld(c, "discovered").truthy()) {
                used_cards.push_back({fld(v, "count").num(), k});
                if (fld(v, "count").num() > max_amt) max_amt = fld(v, "count").num();
            }
        });
    Value secondary = col("SECONDARY_SET");
    lua_sort(used_cards, [](const Used &a, const Used &b) { return a.count > b.count; });

    TableRef histograms = new_table();
    Value count_col = Value(mix_colours(col("FILTER"), col("WHITE"), 0.8f));
    for (int i = 0; i < 10; i++) {
        if (i < (int)used_cards.size()) {
            const Used &v = used_cards[i];
            Value center = G.P_CENTERS->get(v.key);
            Card *card = new Card(0, 0, 0.7f * G.CARD_W, 0.7f * G.CARD_H, Value(), center);
            card->ambient_tilt = 0.8f;
            CardArea *cardarea = new CardArea(G.ROOM->T->x + 0.2f * G.ROOM->T->w / 2, G.ROOM->T->h, G.CARD_W * 0.7f, G.CARD_H * 0.7f,
                                              vl::T({{"card_limit", 2}, {"type", "title"}, {"highlight_limit", 0}}));
            cardarea->emplace(card);
            Value bar_col = secondary.get(fld(center, "set"));
            histograms->push(node(
                UIT_C, vl::T({{"align", "bm"}, {"minh", 6.2}, {"colour", ui_col("TRANSPARENT_DARK")}, {"r", 0.1}}),
                A({node(UIT_R, vl::T({{"align", "bm"}}),
                        A({node(UIT_R, vl::T({{"align", "cm"}, {"minh", 0.7 * G.CARD_H + 0.1}}), A({node(UIT_O, vl::T({{"object", obj(cardarea)}}))})),
                           node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.1}}),
                                A({node(UIT_T, vl::T({{"text", Value(fmt_int(v.count))}, {"scale", 0.35}, {"colour", count_col}, {"shadow", true}}))})),
                           node(UIT_R, vl::T({{"align", "cm"}}),
                                A({node(UIT_R, vl::T({{"align", "cm"},
                                                      {"minh", v.count / max_amt * 3.6},
                                                      {"minw", 0.8},
                                                      {"colour", bar_col.truthy() ? bar_col : col("RED")},
                                                      {"res", 0.15},
                                                      {"r", 0.001}}),
                                        new_table())}))}))})));
        } else {
            histograms->push(node(
                UIT_C, vl::T({{"align", "bm"}, {"minh", 6.2}, {"colour", ui_col("TRANSPARENT_DARK")}, {"r", 0.1}}),
                A({node(UIT_R, vl::T({{"align", "bm"}}),
                        A({node(UIT_R, vl::T({{"align", "cm"}, {"minh", 0.7 * G.CARD_H + 0.1}, {"minw", 0.7 * G.CARD_W}}), new_table()),
                           node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.1}}),
                                A({node(UIT_T, vl::T({{"text", "-"}, {"scale", 0.35}, {"colour", count_col}, {"shadow", true}}))})),
                           node(UIT_R, vl::T({{"align", "cm"}}),
                                A({node(UIT_R, vl::T({{"align", "cm"}, {"minh", 0.2}, {"minw", 0.8}, {"colour", ui_col("TRANSPARENT_LIGHT")}, {"res", 0.15}, {"r", 0.001}}),
                                        new_table())}))}))})));
        }
    }
    const char *title = _type.equals("joker_usage")         ? "ph_stat_joker"
                        : _type.equals("consumeable_usage") ? "ph_stat_consumable"
                                                            : "ph_stat_voucher";
    return TableRef(node(UIT_ROOT, vl::T({{"align", "cm"}, {"minw", 3}, {"padding", 0.1}, {"r", 0.1}, {"colour", ui_col("TRANSPARENT_DARK")}}),
                         A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.1}}),
                                 A({node(UIT_B, vl::T({{"w", 0.2}, {"h", 0.2}, {"r", 0.1}, {"colour", col("FILTER")}})),
                                    node(UIT_T, vl::T({{"text", localize(title)}, {"scale", 0.35}, {"colour", col("WHITE")}}))})),
                            node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.1}}), histograms)}))
                        .table());
}

TableRef usage_tabs() {
    auto tab = [](const char *label, Value args, bool chosen) {
        TableRef t = vl::T({{"label", localize(label)},
                            {"tab_definition_function", Value((Object *)new Func([](const Value &a) { return Value(create_UIBox_usage(a)); }))},
                            {"tab_definition_function_args", args}});
        if (chosen) t->set("chosen", true);
        return Value(t);
    };
    TableRef tabs = A({tab("b_stat_jokers", Value(A({"joker_usage"})), true), tab("b_stat_consumables", Value(A({"consumeable_usage"})), false),
                       tab("b_stat_tarots", Value(A({"consumeable_usage", "Tarot"})), false),
                       tab("b_stat_planets", Value(A({"consumeable_usage", "Planet"})), false),
                       tab("b_stat_spectrals", Value(A({"consumeable_usage", "Spectral"})), false),
                       tab("b_stat_vouchers", Value(A({"voucher_usage", "Voucher"})), false)});
    return create_UIBox_generic_options(vl::T(
        {{"back_func", "high_scores"},
         {"contents", Value(A({Value(create_tabs(vl::T({{"tabs", Value(tabs)}, {"tab_h", 8}, {"snap_to_nav", true}})))}))}}));
}

// ------------------------------------------------------------------ text input
TableRef create_text_input(const TableRef &args) {
    args->set("colour", Value(args->get(K("colour")).truthy() ? args->get(K("colour")).table()->deep_copy() : col("BLUE").table()->deep_copy()));
    args->set("hooked_colour", Value(args->get(K("hooked_colour")).truthy() ? args->get(K("hooked_colour")).table()->deep_copy()
                                                                             : darken(Value(col("BLUE").table()->deep_copy()), 0.3f)));
    auto def = [&](const char *k, const Value &v) {
        if (!args->get(Value(intern(k))).truthy()) args->set(k, v);
    };
    def("w", 2.5);
    def("h", 0.7);
    def("text_scale", 0.4);
    def("max_length", 16);
    def("all_caps", false);
    def("prompt_text", localize("k_enter_text"));
    args->set("current_prompt_text", "");

    Value ref_table = args->get(K("ref_table")), ref_value = args->get(K("ref_value"));
    std::string current = ref_table.get(ref_value).to_string();
    TableRef letters = new_table();
    TableRef text = vl::T({{"ref_table", ref_table}, {"ref_value", ref_value}, {"letters", Value(letters)}, {"current_position", (double)current.size()}});
    TableRef ui_letters = new_table();
    int max_length = (int)args->get(K("max_length")).num();
    double text_scale = args->get(K("text_scale")).num();
    for (int i = 1; i <= max_length; i++) {
        letters->set(i, Value(i <= (int)current.size() ? current.substr(i - 1, 1) : std::string()));
        ui_letters->push(node(UIT_T, vl::T({{"ref_table", Value(letters)},
                                            {"ref_value", i},
                                            {"scale", text_scale},
                                            {"colour", ui_col("TEXT_LIGHT")},
                                            {"id", Value("letter_" + std::to_string(i))}})));
    }
    args->set("text", Value(text));

    Value position_text_colour = Value(lighten(Value(col("BLUE").table()->deep_copy()), 0.4f));
    ui_letters->push(node(UIT_T, vl::T({{"ref_table", Value(args)},
                                        {"ref_value", "current_prompt_text"},
                                        {"scale", text_scale},
                                        {"colour", Value(lighten(Value(args->get(K("colour")).table()->deep_copy()), 0.4f))},
                                        {"id", "prompt"}})));
    ui_letters->push(node(UIT_B, vl::T({{"r", 0.03}, {"w", 0.1}, {"h", 0.4}, {"colour", position_text_colour}, {"id", "position"}, {"func", "flash"}})));

    return TableRef(
        node(UIT_C, vl::T({{"align", "cm"}, {"draw_layer", 1}, {"colour", col("CLEAR")}}),
             A({node(UIT_C, vl::T({{"id", "text_input"}, {"align", "cm"}, {"padding", 0.05}, {"r", 0.1}, {"draw_layer", 2}, {"hover", true},
                                   {"colour", args->get(K("colour"))}, {"minw", args->get(K("w"))}, {"min_h", args->get(K("h"))},
                                   {"button", "select_text_input"}, {"shadow", true}}),
                     A({node(UIT_R, vl::T({{"ref_table", Value(args)}, {"padding", 0.05}, {"align", "cm"}, {"r", 0.1}, {"colour", col("CLEAR")}}),
                             A({node(UIT_R, vl::T({{"ref_table", Value(args)}, {"align", "cm"}, {"r", 0.1}, {"colour", col("CLEAR")}, {"func", "text_input"}}),
                                     ui_letters)}))}))}))
            .table());
}

namespace {
Value create_keyboard_button(const std::string &key, const char *binding) {
    double overall_scale = G.F_MOBILE ? 1.9 : 1;
    std::string key_label = key == "backspace" ? "Backspace" : key == " " ? "Space" : key == "back" ? "Back" : key == "return" ? "Enter" : key;
    double minw = key == " " ? overall_scale * 6 : (key == "return" || key == "backspace" || key == "back") ? overall_scale * 2.5 : overall_scale * 0.8;
    double minh = (key == "return" || key == "backspace") ? 1.5 : key == "back" ? 0.8 : overall_scale * 0.7;
    TableRef b = vl::T({{"label", Value(A({Value(key_label)}))},
                        {"scale", overall_scale * 0.5},
                        {"button", "key_button"},
                        {"ref_table", Value(vl::T({{"key", Value(key == "back" ? std::string("return") : key)}}))},
                        {"minw", minw},
                        {"minh", minh},
                        {"col", true},
                        {"colour", col("GREY")}});
    if (binding)
        b->set("focus_args", Value(vl::T({{"button", binding}, {"orientation", (key == " " || key == "back") ? "cr" : "bm"}, {"set_button_pip", true}})));
    return Value(UIBox_button(b));
}
}  // namespace

TableRef create_keyboard_input(const TableRef &args) {
    const char *keyboard_rows[] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM", args->get(K("space_key")).truthy() ? " " : nullptr};
    TableRef rows[5];
    for (int k = 0; k < 5; k++) {
        rows[k] = new_table();
        if (!keyboard_rows[k]) continue;
        for (const char *c = keyboard_rows[k]; *c; c++) rows[k]->push(create_keyboard_button(std::string(1, *c), *c == ' ' ? "y" : nullptr));
    }
    Value grey = col("GREY");
    bool side = args->get(K("backspace_key")).truthy() || args->get(K("return_key")).truthy();
    auto row = [&](TableRef nodes) { return node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.07}, {"colour", col("CLEAR")}}), nodes); };
    auto key_row = [&](const char *key, const char *binding) {
        return node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.05}, {"colour", col("CLEAR")}}), A({create_keyboard_button(key, binding)}));
    };
    Value side_col = side ? node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.05}, {"colour", col("CLEAR")}}),
                                 A({args->get(K("backspace_key")).truthy() ? key_row("backspace", "x") : Value(),
                                    args->get(K("return_key")).truthy() ? key_row("return", "start") : Value(), key_row("back", "b")}))
                          : Value();
    return TableRef(
        node(UIT_ROOT, vl::T({{"align", "cm"}, {"padding", 15}, {"r", 0.1}, {"colour", Value(A({grey[1], grey[2], grey[3], 0.7}))}}),
             A({node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.05}, {"colour", col("CLEAR")}}),
                     A({node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.05}, {"colour", col("BLACK")}, {"emboss", 0.05}, {"r", 0.1}, {"mid", true}}),
                             A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.05}}),
                                     A({node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.05}, {"colour", col("CLEAR")}}),
                                             A({row(rows[0]), row(rows[1]), row(rows[2]), row(rows[3]), row(rows[4])})),
                                        side_col}))}))}))}))
            .table());
}

// ------------------------------------------------------------------ profiles
TableRef profile_select() {
    if (!G.focused_profile) G.focused_profile = G.SETTINGS->get(K("profile")).truthy() ? (int)G.SETTINGS->get(K("profile")).num() : 1;
    TableRef tabs = new_table();
    for (int i = 1; i <= 3; i++) {
        TableRef t = vl::T({{"label", i},
                            {"tab_definition_function", Value((Object *)new Func([](const Value &p) { return Value(profile_option((int)p.num())); }))},
                            {"tab_definition_function_args", i}});
        if (G.focused_profile == i) t->set("chosen", true);
        tabs->push(Value(t));
    }
    return create_UIBox_generic_options(vl::T(
        {{"padding", 0},
         {"contents", Value(A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}, {"draw_layer", 1}, {"minw", 4}}),
                                    A({Value(create_tabs(vl::T({{"tabs", Value(tabs)}, {"snap_to_nav", true}})))}))}))}}));
}

TableRef profile_option(int _profile) {
    set_discover_tallies();
    G.focused_profile = _profile;
    G.load_profile(G.focused_profile);
    Value profile_data = G.FILES->get(Value(std::to_string(_profile) + "/profile.jkr"));
    if (!profile_data.is_table() || profile_data.table()->empty() || fld(profile_data, "deleted").truthy()) profile_data = Value();
    if (profile_data.truthy() && !fld(profile_data, "name").truthy()) profile_data.table()->set("name", Value("P" + std::to_string(_profile)));
    Value prof = G.PROFILES->get(_profile);
    prof.table()->set("name", profile_data.truthy() ? fld(profile_data, "name") : Value(""));

    const double lwidth = 1, rwidth = 1, scale = 1;
    G.CHECK_PROFILE_DATA = -1;
    int current = (int)G.SETTINGS->get(K("profile")).num();
    Value name_row =
        (_profile == current || !profile_data.truthy())
            ? node(UIT_R, vl::T({{"align", "cm"}}),
                   A({Value(create_text_input(vl::T({{"w", 4},
                                                     {"max_length", 16},
                                                     {"prompt_text", localize("k_enter_name")},
                                                     {"ref_table", prof},
                                                     {"ref_value", "name"},
                                                     {"extended_corpus", true},
                                                     {"keyboard_offset", 2},
                                                     {"callback", Value((Object *)new Func([](const Value &) {
                                                          G.save_settings();
                                                          G.FILE_HANDLER->set("force", true);
                                                          return Value();
                                                      }))}})))}))
            : node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.1}, {"minw", 4}, {"r", 0.1}, {"colour", col("BLACK")}, {"minh", 0.6}}),
                   A({node(UIT_T, vl::T({{"text", fld(prof, "name")}, {"scale", 0.45}, {"colour", col("WHITE")}}))}));
    Value progress = fld(prof, "progress");
    Value progress_col = (progress.truthy() && fld(progress, "discovered").truthy())
                             ? Value(create_progress_box(progress, true))
                             : node(UIT_C, vl::T({{"align", "cm"}, {"minh", 4}, {"minw", 5.2}, {"colour", col("BLACK")}, {"r", 0.1}}),
                                    A({node(UIT_T, vl::T({{"text", localize("k_empty_caps")}, {"scale", 0.5}, {"colour", ui_col("TRANSPARENT_LIGHT")}}))}));
    Value wins = profile_data.truthy()
                     ? node(UIT_R, vl::T({{"align", "cm"}}),
                            A({node(UIT_C, vl::T({{"align", "cm"}, {"minw", lwidth}}),
                                    A({node(UIT_T, vl::T({{"text", localize("k_wins")}, {"colour", ui_col("TEXT_LIGHT")}, {"scale", scale * 0.7}}))})),
                               node(UIT_C, vl::T({{"align", "cm"}}),
                                    A({node(UIT_T, vl::T({{"text", ": "}, {"colour", ui_col("TEXT_LIGHT")}, {"scale", scale * 0.7}}))})),
                               node(UIT_C, vl::T({{"align", "cl"}, {"minw", rwidth}}),
                                    A({node(UIT_T, vl::T({{"text", Value(fld(fld(profile_data, "career_stats"), "c_wins").to_string())},
                                                          {"colour", col("RED")},
                                                          {"shadow", true},
                                                          {"scale", 1 * scale}}))}))}))
                     : Value();
    auto button = [&](double minw, double maxw, double minh, const Value &colour, const char *func, const char *btn, const Value &text, double tscale) {
        return node(UIT_R, vl::T({{"align", "cm"}, {"minw", minw}, {"maxw", maxw}, {"minh", minh}, {"padding", 0.2}, {"r", 0.1}, {"hover", true},
                                  {"colour", colour}, {"func", func}, {"button", btn}, {"shadow", true},
                                  {"focus_args", Value(vl::T({{"nav", "wide"}}))}}),
                    A({node(UIT_T, vl::T({{"text", text}, {"scale", tscale}, {"colour", ui_col("TEXT_LIGHT")}}))}));
    };
    Value load_text = _profile == current        ? localize("b_current_profile")
                      : profile_data.truthy() ? localize("b_load_profile")
                                              : localize("b_create_profile");
    Value load_btn = button(4, 4, 0.8, col("BLUE"), "can_load_profile", "load_profile", load_text, 0.5);
    load_btn.table()->get(K("nodes"))[1]["config"].table()->set("ref_value", "load_button_text");
    bool all_unlocked = fld(prof, "all_unlocked").truthy();
    Value third =
        (_profile == current && !fld(G.PROFILES->get(current), "all_unlocked").truthy())
            ? node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}, {"minh", 0.7}}),
                   A({button(3, 4, 0.6, col("ORANGE"), "can_unlock_all", "unlock_all", localize("b_unlock_all"), 0.3)}))
            : node(UIT_R, vl::T({{"align", "cm"}, {"minw", 3}, {"maxw", 4}, {"minh", 0.7}}),
                   A({(all_unlocked && !G.F_NO_ACHIEVEMENTS)
                          ? node(UIT_T, vl::T({{"text", localize("k_achievements_disabled")}, {"scale", 0.3}, {"colour", ui_col("TEXT_LIGHT")}}))
                          : Value()}));
    return TableRef(
        node(UIT_ROOT, vl::T({{"align", "cm"}, {"colour", col("CLEAR")}}),
             A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.1}, {"minh", 0.8}}), A({name_row})),
                node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.1}}),
                     A({node(UIT_C, vl::T({{"align", "cm"}, {"minw", 6}}), A({progress_col})),
                        node(UIT_C, vl::T({{"align", "cm"}, {"minh", 4}}),
                             A({node(UIT_R, vl::T({{"align", "cm"}, {"minh", 1}}), A({wins})),
                                node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.2}}),
                                     A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}}), A({load_btn})),
                                        node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}, {"minh", 0.7}}),
                                             A({button(3, 4, 0.6, col("RED"), "can_delete_profile", "delete_profile",
                                                       _profile == current ? localize("b_reset_profile") : localize("b_delete_profile"), 0.3)})),
                                        third}))}))})),
                node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}}),
                     A({node(UIT_T, vl::T({{"id", "warning_text"}, {"text", localize("ph_click_confirm")}, {"scale", 0.4}, {"colour", col("CLEAR")}}))}))}))
            .table());
}

// ------------------------------------------------------------------ collection
namespace {
Value pool(const char *name) { return G.P_CENTER_POOLS->get(Value(intern(name))); }
int pool_len(const char *name) { return pool(name).table() ? pool(name).table()->len() : 0; }

// G.your_collection[j] = CardArea(G.ROOM.T.x + 0.2*G.ROOM.T.w/2, G.ROOM.T.h, w, h, config)
CardArea *collection_area(float w, float h, TableRef config) {
    return new CardArea(G.ROOM->T->x + 0.2f * G.ROOM->T->w / 2, G.ROOM->T->h, w, h, config);
}
TableRef area_rows(const std::vector<CardArea *> &areas, double padding) {
    TableRef t = new_table();
    for (CardArea *a : areas)
        t->push(node(UIT_R, vl::T({{"align", "cm"}, {"padding", padding}, {"no_fill", true}}), A({node(UIT_O, vl::T({{"object", obj(a)}}))})));
    return t;
}
Card *collection_card(CardArea *a, const Value &front, const Value &center, float scale = 1) {
    return new Card(a->T->x + a->T->w / 2, a->T->y, G.CARD_W * scale, G.CARD_H * scale, front, center);
}
TableRef page_options(int pages, int shown_pages) {
    TableRef t = new_table();
    for (int i = 1; i <= pages; i++)
        t->push(Value(localize("k_page").to_string() + " " + std::to_string(i) + "/" + std::to_string(shown_pages)));
    return t;
}
Value page_cycle(TableRef options, const char *callback, bool focus_first) {
    TableRef a = vl::T({{"options", Value(options)},
                        {"w", 4.5},
                        {"cycle_shoulders", true},
                        {"opt_callback", callback},
                        {"current_option", 1},
                        {"colour", col("RED")},
                        {"no_pips", true},
                        {"focus_args", Value(vl::T({{"snap_to", true}, {"nav", "wide"}}))}});
    (void)focus_first;
    return Value(create_option_cycle(a));
}
TableRef black_box_rows(TableRef deck_tables) {
    return TableRef(node(UIT_R, vl::T({{"align", "cm"}, {"minw", 2.5}, {"padding", 0.1}, {"r", 0.1}, {"colour", col("BLACK")}, {"emboss", 0.05}}), deck_tables)
                        .table());
}
}  // namespace

TableRef create_UIBox_your_collection() {
    set_discover_tallies();
    EventConfig e = ev_immediate([] {
        G.REFRESH_ALERTS = true;
        return true;
    });
    e.blockable = false;
    G.E_MANAGER->add_event(std::move(e));
    Value tallies = Value(G.DISCOVER_TALLIES);
    auto btn = [&](const char *button, const char *label, const char *count, double minw, std::initializer_list<std::pair<const char *, Value>> extra) {
        TableRef a = vl::T({{"button", button}, {"label", Value(A({localize(label)}))}, {"minw", minw}});
        if (count) a->set("count", fld(tallies, count));
        for (auto &kv : extra) a->set(kv.first, kv.second);
        return Value(UIBox_button(a));
    };
    Value secondary = col("SECONDARY_SET");
    return create_UIBox_generic_options(vl::T(
        {{"back_func", G.STAGE == RUN ? "options" : "exit_overlay_menu"},
         {"contents",
          Value(A({node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.15}}),
                        A({btn("your_collection_jokers", "b_jokers", "jokers", 5, {{"minh", 1.7}, {"scale", 0.6}, {"id", "your_collection_jokers"}}),
                           btn("your_collection_decks", "b_decks", "backs", 5, {}),
                           btn("your_collection_vouchers", "b_vouchers", "vouchers", 5, {{"id", "your_collection_vouchers"}}),
                           node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.1}, {"r", 0.2}, {"colour", col("BLACK")}}),
                                A({node(UIT_C, vl::T({{"align", "cm"}, {"maxh", 2.9}}),
                                        A({node(UIT_T, vl::T({{"text", localize("k_cap_consumables")}, {"scale", 0.45}, {"colour", col("L_BLACK")}, {"vert", true}, {"maxh", 2.2}}))})),
                                   node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.15}}),
                                        A({btn("your_collection_tarots", "b_tarot_cards", "tarots", 4, {{"id", "your_collection_tarots"}, {"colour", secondary["Tarot"]}}),
                                           btn("your_collection_planets", "b_planet_cards", "planets", 4, {{"id", "your_collection_planets"}, {"colour", secondary["Planet"]}}),
                                           btn("your_collection_spectrals", "b_spectral_cards", "spectrals", 4,
                                               {{"id", "your_collection_spectrals"}, {"colour", secondary["Spectral"]}})}))}))})),
                   node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.15}}),
                        A({btn("your_collection_enhancements", "b_enhanced_cards", nullptr, 5, {}),
                           btn("your_collection_seals", "b_seals", nullptr, 5, {{"id", "your_collection_seals"}}),
                           btn("your_collection_editions", "b_editions", "editions", 5, {{"id", "your_collection_editions"}}),
                           btn("your_collection_boosters", "b_booster_packs", "boosters", 5, {{"id", "your_collection_boosters"}}),
                           btn("your_collection_tags", "b_tags", "tags", 5, {{"id", "your_collection_tags"}}),
                           btn("your_collection_blinds", "b_blinds", "blinds", 5,
                               {{"minh", 2.0}, {"id", "your_collection_blinds"}, {"focus_args", Value(vl::T({{"snap_to", true}}))}})}))}))}}));
}

// Fills G.your_collection for a page (the *_page callbacks share it with the first page).
void collection_fill(const std::string &kind, int page) {
    auto &yc = G.your_collection;
    if (kind == "Joker") {
        for (int i = 1; i <= 5; i++)
            for (size_t j = 1; j <= yc.size(); j++) {
                Value center = pool("Joker")[i + ((int)j - 1) * 5 + (5 * (int)yc.size() * (page - 1))];
                if (!center.truthy()) break;
                Card *card = collection_card(yc[j - 1].get(), page == 1 ? Value() : G.P_CARDS->get(K("empty")), center);
                card->sticker = get_joker_win_sticker(center);
                yc[j - 1]->emplace(card);
            }
    } else if (kind == "Tarot" || kind == "Spectral" || kind == "Planet" || kind == "Booster") {
        for (size_t j = 1; j <= yc.size(); j++) {
            int n = kind == "Tarot" ? 4 + (int)j : kind == "Spectral" ? 3 + (int)j : kind == "Planet" ? 6 : 4;
            for (int i = 1; i <= n; i++) {
                int idx = kind == "Tarot"    ? i + ((int)j - 1) * 5 + 11 * (page - 1)
                          : kind == "Planet" ? i + ((int)j - 1) * 6
                          : kind == "Booster" ? i + ((int)j - 1) * 4 + 8 * (page - 1)
                          // the first spectral page indexes (j-1)*3 + j-1, the page callback (j-1)*4: the same
                                              : i + ((int)j - 1) * 4 + 9 * (page - 1);
                Value center = pool(kind.c_str())[idx];
                if (!center.truthy()) break;
                Value front = (page == 1 || kind == "Booster") ? Value() : G.P_CARDS->get(K("empty"));
                Card *card = collection_card(yc[j - 1].get(), front, center, kind == "Booster" ? 1.27f : 1);
                card->start_materialize(nullptr, i > 1 || j > 1);
                yc[j - 1]->emplace(card);
            }
        }
    } else if (kind == "Voucher") {
        for (int i = 1; i <= 4; i++)
            for (size_t j = 1; j <= yc.size(); j++) {
                Value center = pool("Voucher")[i + ((int)j - 1) * 4 + 8 * (page - 1)];
                if (!center.truthy()) break;
                Card *card = collection_card(yc[j - 1].get(), page == 1 ? Value() : G.P_CARDS->get(K("empty")), center);
                if (page == 1) card->ability->set("order", i + ((int)j - 1) * 4);
                card->start_materialize(nullptr, i > 1 || j > 1);
                yc[j - 1]->emplace(card);
            }
    }
    // INIT_COLLECTION_CARD_ALERTS
    for (auto &a : yc)
        for (auto &c : a->cards) c->update_alert();
}

TableRef create_UIBox_your_collection_cards(const std::string &kind) {
    auto &yc = G.your_collection;
    yc.clear();
    TableRef options;
    const char *callback = nullptr;
    int rows = kind == "Joker" ? 3 : 2;
    for (int j = 1; j <= rows; j++) {
        CardArea *a = nullptr;
        if (kind == "Joker")
            a = collection_area(5 * G.CARD_W, 0.95f * G.CARD_H, vl::T({{"card_limit", 5}, {"type", "title"}, {"highlight_limit", 0}, {"collection", true}}));
        else if (kind == "Tarot")
            a = collection_area((4.25f + j) * G.CARD_W, G.CARD_H, vl::T({{"card_limit", 4 + j}, {"type", "title"}, {"highlight_limit", 0}, {"collection", true}}));
        else if (kind == "Booster")
            a = collection_area(5.25f * G.CARD_W, 1.3f * G.CARD_H, vl::T({{"card_limit", 4}, {"type", "title"}, {"highlight_limit", 0}, {"collection", true}}));
        else if (kind == "Planet")
            a = collection_area(6.25f * G.CARD_W, G.CARD_H, vl::T({{"card_limit", 6}, {"type", "title"}, {"highlight_limit", 0}, {"collection", true}}));
        else if (kind == "Spectral")
            a = collection_area((3.25f + j) * G.CARD_W, G.CARD_H, vl::T({{"card_limit", 3 + j}, {"type", "title"}, {"highlight_limit", 0}, {"collection", true}}));
        else  // Voucher
            a = collection_area(4.25f * G.CARD_W, G.CARD_H, vl::T({{"card_limit", 4}, {"type", "voucher"}, {"highlight_limit", 0}, {"collection", true}}));
        yc.push_back(a);
    }
    std::vector<CardArea *> raw;
    for (auto &a : yc) raw.push_back(a.get());
    TableRef deck_tables = area_rows(raw, kind == "Joker" ? 0.07 : 0);
    if (kind == "Joker") {
        int pages = (int)std::ceil(pool_len("Joker") / (5.0 * yc.size()));
        options = page_options(pages, pages);
        callback = "your_collection_joker_page";
    } else if (kind == "Tarot") {
        options = page_options(pool_len("Tarot") / 11, pool_len("Tarot") / 11);
        callback = "your_collection_tarot_page";
    } else if (kind == "Booster") {
        int pages = (int)std::ceil(pool_len("Booster") / 8.0);
        options = page_options(pages, pages);
        callback = "your_collection_booster_page";
    } else if (kind == "Spectral") {
        // (sic) the page count comes from the Tarot pool, the label from the Spectral pool
        options = page_options(pool_len("Tarot") / 9, pool_len("Spectral") / 9);
        callback = "your_collection_spectral_page";
    } else if (kind == "Voucher") {
        int pages = (int)std::ceil(pool_len("Voucher") / (4.0 * yc.size()));
        options = page_options(pages, pages);
        callback = "your_collection_voucher_page";
    }
    collection_fill(kind, 1);

    Value box = kind == "Joker"
                    ? node(UIT_R, vl::T({{"align", "cm"}, {"r", 0.1}, {"colour", col("BLACK")}, {"emboss", 0.05}}), deck_tables)
                    : Value(black_box_rows(deck_tables));
    Value bottom = kind == "Planet" ? node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.7}}), new_table())
                                    : node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}}), A({page_cycle(options, callback, true)}));
    return create_UIBox_generic_options(vl::T({{"back_func", "your_collection"}, {"contents", Value(A({box, bottom}))}}));
}

TableRef create_UIBox_your_collection_special(const std::string &kind) {
    auto &yc = G.your_collection;
    yc.clear();
    Value info = localize("ml_edition_seal_enhancement_explanation");
    if (kind == "Seal") {
        CardArea *a = collection_area(4.25f * G.CARD_W, 1.03f * G.CARD_H, vl::T({{"card_limit", 4}, {"type", "title"}, {"highlight_limit", 0}}));
        yc.push_back(a);
        for (int k = 1; k <= pool_len("Seal"); k++) {
            Card *card = collection_card(a, G.P_CARDS->get(K("empty")), G.P_CENTERS->get(K("c_base")));
            card->set_seal(fld(pool("Seal")[k], "key"), true);
            a->emplace(card);
        }
    } else if (kind == "Enhanced") {
        for (int j = 1; j <= 2; j++)
            yc.push_back(collection_area(4.25f * G.CARD_W, 1.03f * G.CARD_H, vl::T({{"card_limit", 4}, {"type", "title"}, {"highlight_limit", 0}})));
        for (int i = 1; i <= 4; i++)
            for (int j = 1; j <= 2; j++)
                yc[j - 1]->emplace(collection_card(yc[j - 1].get(), G.P_CARDS->get(K("empty")), pool("Enhanced")[i + (j - 1) * 4]));
    } else {  // Edition
        CardArea *a = collection_area(5.3f * G.CARD_W, 1.03f * G.CARD_H, vl::T({{"card_limit", 5}, {"type", "title"}, {"highlight_limit", 0}, {"collection", true}}));
        yc.push_back(a);
        for (const char *ed : {"base", "foil", "holo", "polychrome", "negative"}) {
            Value center = G.P_CENTERS->get(Value(std::string("e_") + ed));
            Card *card = collection_card(a, G.P_CARDS->get(K("empty")), center);
            card->start_materialize();
            if (fld(center, "discovered").truthy()) card->set_edition(Value(vl::T({{ed, true}})), true, true);
            a->emplace(card);
        }
        for (auto &c : a->cards) c->update_alert();
    }
    std::vector<CardArea *> raw;
    for (auto &a : yc) raw.push_back(a.get());
    TableRef deck_tables = area_rows(raw, 0);
    return create_UIBox_generic_options(vl::T({{"infotip", info},
                                               {"back_func", "your_collection"},
                                               {"snap_back", true},
                                               {"contents", Value(A({Value(black_box_rows(deck_tables))}))}}));
}

}  // namespace vl
