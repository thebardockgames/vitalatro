// functions/button_callbacks.lua: menus, settings and language selection.
#include "game/button_callbacks.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

#include "engine/controller.h"
#include "engine/particles.h"
#include "engine/random.h"
#include "engine/text.h"
#include "engine/ui.h"
#include "game/card.h"
#include "game/functions.h"
#include "game/g.h"
#include "game/sound.h"
#include "game/ui_definitions.h"
#include "platform/platform.h"

namespace vl {

namespace {
inline Value fld(const Value &t, const char *k) { return t.get(Value(intern(k))); }
UIBox *overlay() { return dynamic_cast<UIBox *>(G.OVERLAY_MENU.get()); }
UIElement *child_uie(Node *n, int i) { return n && n->children.count() > i ? dynamic_cast<UIElement *>(n->children.at(i)) : nullptr; }
void call_t(const Value &name, const TableRef &args) {
    if (!name.is_str()) return;
    auto it = G.FUNCS_T.find(std::string(name.str()));
    if (it != G.FUNCS_T.end()) {
        it->second(args);
    } else {
        static std::set<std::string> warned;
        if (warned.insert(std::string(name.str())).second) platform::log("[ui] missing G.FUNCS.%s", std::string(name.str()).c_str());
    }
}
}  // namespace

// ------------------------------------------------------------------ overlays
void overlay_menu(const TableRef &definition, const TableRef &config_in) {
    if (!definition) return;
    if (G.OVERLAY_MENU) G.OVERLAY_MENU->remove();
    G.CONTROLLER->locks["frame_set"] = true;
    G.CONTROLLER->locks["frame"] = true;
    G.CONTROLLER->cursor_down.target = nullptr;
    G.CONTROLLER->mod_cursor_context_layer(G.NO_MOD_CURSOR_STACK ? 0 : 1);

    TableRef cfg = config_in ? config_in : new_table();
    Value off = cfg->get(K("offset"));
    TableRef config = vl::T({{"align", cfg->get(K("align")).truthy() ? cfg->get(K("align")) : Value("cm")},
                             {"offset", off.truthy() ? off : Value(vl::T({{"x", 0}, {"y", 10}}))},
                             {"major", cfg->get(K("major")).truthy() ? cfg->get(K("major")) : Value((Object *)G.ROOM_ATTACH.get())},
                             {"bond", "Weak"},
                             {"no_esc", cfg->get(K("no_esc"))}});
    // G.OVERLAY_MENU = true while the UIBox is built (code run meanwhile checks it)
    G.STAGE_OBJECT_INTERRUPT = true;
    G.OVERLAY_MENU = new Moveable(0, 0, 0, 0);
    G.STAGE_OBJECT_INTERRUPT = false;
    UIBox *box = new UIBox(definition, config);
    G.OVERLAY_MENU = box;
    box->alignment.offset.y = 0;
    G.ROOM_jiggle += 1;
    box->align_to_major();
    G.CONTROLLER->touch_control.clear_touch = true;
}

void exit_overlay_menu() {
    if (!G.OVERLAY_MENU) return;
    G.CONTROLLER->locks["frame_set"] = true;
    G.CONTROLLER->locks["frame"] = true;
    G.CONTROLLER->mod_cursor_context_layer(-1000);
    G.OVERLAY_MENU->remove();
    G.OVERLAY_MENU = nullptr;
    G.VIEWING_DECK = false;
    G.SETTINGS->set("paused", false);
    G.CONTROLLER->touch_control.clear_touch = true;
    G.save_settings();
}

// ------------------------------------------------------------------ screen wipe
void wipe_on(const Value &message, bool no_card, double timefac, const Value &alt_colour) {
    if (G.screenwipe) return;
    G.CONTROLLER->locks["wipe"] = true;
    G.STAGE_OBJECT_INTERRUPT = true;
    if (!no_card) {
        Card *c = new Card(1, 1, G.CARD_W, G.CARD_H, pseudorandom_element(Value(G.P_CARDS)), G.P_CENTERS->get(K("c_base")));
        G.screenwipecard = c;
        c->sprite_facing = "back";
        c->facing = "back";
        c->states.hover.can = false;
        float r = 1;
        c->juice_up_card(0.5f, &r);
    }
    Value message_t;
    if (Table *m = message.table()) {
        TableRef rows = new_table();
        Value bgc = fld(G.C->get(K("BACKGROUND")), "C");
        Value colour = std::min(bgc[1].num(), bgc[2].num()) > 0.5 ? G.colour("BLACK") : G.colour("WHITE");
        for (int k = 1; k <= m->len(); k++) {
            DynaText *dt = new DynaText(vl::T({{"string", m->get(k).truthy() ? m->get(k) : Value("")},
                                               {"colours", Value(A({colour}))},
                                               {"shadow", true},
                                               {"silent", k != 1},
                                               {"float", true},
                                               {"scale", 1.3},
                                               {"pop_in", 0},
                                               {"pop_in_rate", 2},
                                               {"rotate", 1}}));
            rows->push(Value(vl::T({{"n", (double)UIT_R},
                                    {"config", Value(vl::T({{"align", "cm"}}))},
                                    {"nodes", Value(A({Value(vl::T({{"n", (double)UIT_O}, {"config", Value(vl::T({{"object", Value((Object *)dt)}}))}}))}))}})));
        }
        message_t = Value(rows);
    }
    Value text_row, card_row;
    if (message_t.truthy())
        text_row = Value(vl::T({{"n", (double)UIT_R}, {"config", Value(vl::T({{"id", "text"}, {"align", "cm"}, {"padding", 0.7}}))}, {"nodes", message_t}}));
    if (!no_card)
        card_row = Value(vl::T({{"n", (double)UIT_O},
                                {"config", Value(vl::T({{"object", Value((Object *)G.screenwipecard.get())},
                                                        {"role", Value(vl::T({{"role_type", "Major"}}))}}))}}));
    TableRef def = vl::T({{"n", (double)UIT_ROOT},
                          {"config", Value(vl::T({{"align", "cm"}, {"minw", 0}, {"minh", 0}, {"padding", 0.15}, {"r", 0.1}, {"colour", G.colour("CLEAR")}}))},
                          {"nodes", Value(A({Value(vl::T({{"n", (double)UIT_R}, {"config", Value(vl::T({{"align", "cm"}}))},
                                                          {"nodes", Value(A({text_row, card_row}))}}))}))}});
    UIBox *wipe = new UIBox(def, vl::T({{"align", "cm"}, {"offset", Value(vl::T({{"x", 0}, {"y", 0}}))}, {"major", Value((Object *)G.ROOM_ATTACH.get())}}));
    G.screenwipe = wipe;
    Value colours = alt_colour.truthy() ? alt_colour : fld(G.C->get(K("BACKGROUND")), "C");
    Particles *p = new Particles(0, 0, 0, 0,
                                 vl::T({{"timer", 0},
                                        {"max", 1},
                                        {"scale", 40},
                                        {"speed", 0},
                                        {"lifespan", 1.7 * timefac},
                                        {"attach", Value((Object *)wipe)},
                                        {"colours", Value(A({colours}))}}));
    wipe->children.set("particles", p);
    G.STAGE_OBJECT_INTERRUPT = false;
    wipe->alignment.offset.y = 0;
    if (message_t.truthy()) {
        if (UIElement *t = wipe->get_UIE_by_ID("text"))
            for (int i = 0; i < t->children.count(); i++)
                if (UIElement *row = child_uie(t->children.at(i), 0))
                    if (DynaText *dt = row->config->get(K("object")).as<DynaText>()) dt->pulse();
    }
    EventConfig e;
    e.trigger = Trigger::Before;
    e.delay = 0.7;
    e.no_delete = true;
    e.blockable = false;
    e.func = [no_card] {
        if (!no_card && G.screenwipecard) {
            static_cast<Card *>(G.screenwipecard.get())->flip();
            play_sound("cardFan2");
        }
        return true;
    };
    G.E_MANAGER->add_event(std::move(e));
}

void wipe_off() {
    EventConfig a = ev_immediate([] {
        delay(0.3);
        if (UIBox *w = dynamic_cast<UIBox *>(G.screenwipe.get()))
            if (auto *p = dynamic_cast<Particles *>(w->children.get("particles"))) p->max = 0;
        // (the original also eases G.screenwipe.colours, which nothing draws)
        return true;
    });
    a.no_delete = true;
    G.E_MANAGER->add_event(std::move(a));
    EventConfig b = ev_after(0.55, [] {
        if (G.screenwipecard)
            static_cast<Card *>(G.screenwipecard.get())->start_dissolve(A({G.colour("BLACK"), G.colour("ORANGE"), G.colour("GOLD"), G.colour("RED")}));
        if (UIBox *w = dynamic_cast<UIBox *>(G.screenwipe.get()))
            if (UIElement *t = w->get_UIE_by_ID("text"))
                for (int i = 0; i < t->children.count(); i++)
                    if (UIElement *row = child_uie(t->children.at(i), 0))
                        if (DynaText *dt = row->config->get(K("object")).as<DynaText>()) dt->pop_out(4);
        return true;
    });
    b.no_delete = true;
    b.blocking = false;
    b.has_timer = true;
    b.timer = EventTimer::REAL;
    G.E_MANAGER->add_event(std::move(b));
    EventConfig c = ev_after(1.1, [] {
        if (UIBox *w = dynamic_cast<UIBox *>(G.screenwipe.get())) {
            if (Node *p = w->children.get("particles")) p->remove();
            w->remove();
            w->children.set("particles", nullptr);
        }
        G.screenwipe = nullptr;
        G.screenwipecard = nullptr;
        return true;
    });
    c.no_delete = true;
    c.blocking = false;
    c.has_timer = true;
    c.timer = EventTimer::REAL;
    G.E_MANAGER->add_event(std::move(c));
    EventConfig d = ev_after(1.2, [] { return true; });
    d.no_delete = true;
    d.blocking = true;
    d.has_timer = true;
    d.timer = EventTimer::REAL;
    G.E_MANAGER->add_event(std::move(d));
}

// ------------------------------------------------------------------ controls
void slider_descreet(UIElement *e, double per) {
    UIElement *c = child_uie(e, 0);
    if (!c) return;
    e->states.drag.can = true;
    c->states.drag.can = true;
    if (per != 0) {
        Table *rt = c->config->get(K("ref_table")).table();
        Table *target = rt->get(K("ref_table")).table();
        Value key = rt->get(K("ref_value"));
        double mn = rt->get(K("min")).num(), mx = rt->get(K("max")).num();
        double v = std::min(mx, std::max(mn, target->get(key).num() + per * (mx - mn)));
        target->set(key, v);
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.*f", (int)rt->get(K("decimal_places")).num(), v);
        rt->set("text", Value(std::string(buf)));
        c->T->w = (float)((v - mn) / (mx - mn) * rt->get(K("w")).num());
        c->config->set("w", (double)c->T->w);
    }
}

namespace {
void slider(UIElement *e) {
    UIElement *c = child_uie(e, 0);
    if (!c) return;
    e->states.drag.can = true;
    c->states.drag.can = true;
    Node *drag = G.CONTROLLER->dragging.target.get();
    if (drag && (drag == e || drag == c)) {
        Table *rt = c->config->get(K("ref_table")).table();
        Table *target = rt->get(K("ref_table")).table();
        Value key = rt->get(K("ref_value"));
        double mn = rt->get(K("min")).num(), mx = rt->get(K("max")).num();
        double v = std::min(mx, std::max(mn, mn + (mx - mn) * (G.CURSOR->T->x - e->parent->T->x - G.ROOM->T->x) / e->T->w));
        target->set(key, v);
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.*f", (int)rt->get(K("decimal_places")).num(), v);
        rt->set("text", Value(std::string(buf)));
        c->T->w = (float)((v - mn) / (mx - mn) * rt->get(K("w")).num());
        c->config->set("w", (double)c->T->w);
        if (rt->get(K("callback")).truthy()) call_t(rt->get(K("callback")), TableRef(rt));
    }
}

void toggle_button(UIElement *e) {
    Table *rt = e->config->get(K("ref_table")).table();
    Table *target = rt->get(K("ref_table")).table();
    Value key = rt->get(K("ref_value"));
    target->set(key, !target->get(key).truthy());
    Value cb = e->config->get(K("toggle_callback"));
    if (cb.truthy()) call_t(cb, vl::T({{"value", target->get(key)}}));
}

void toggle(UIElement *e) {
    Table *rt = e->config->get(K("ref_table")).table();
    bool on = rt->get(K("ref_table")).get(rt->get(K("ref_value"))).truthy();
    bool active = e->config->get(K("toggle_active")).truthy();
    UIElement *c = child_uie(e, 0);
    Node *obj = c ? c->config->get(K("object")).as<Node>() : nullptr;
    if (!on && active) {
        e->config->set("toggle_active", Value());
        e->config->set("colour", rt->get(K("inactive_colour")));
        if (c) c->states.visible = false;
        if (obj) obj->states.visible = false;
    } else if (on && !active) {
        e->config->set("toggle_active", true);
        e->config->set("colour", rt->get(K("active_colour")));
        if (c) c->states.visible = true;
        if (obj) obj->states.visible = true;
    }
}

void option_cycle(UIElement *e) {
    Table *rt = e->config->get(K("ref_table")).table();
    Table *opts = rt->get(K("options")).table();
    int n = opts->len();
    int from_key = (int)rt->get(K("current_option")).num();
    Value from_val = opts->get(from_key);
    UIElement *scope = e->parent_uie() ? e->parent_uie()->parent_uie() : nullptr;
    UIElement *old_pip = e->box->get_UIE_by_ID("pip_" + std::to_string(from_key), scope);
    UIElement *cycle_main = e->box->get_UIE_by_ID("cycle_main", scope);
    if (cycle_main && cycle_main->config->get(K("h_popup")).truthy()) {
        cycle_main->stop_hover();
        Ref<UIElement> cm(cycle_main);
        G.E_MANAGER->add_event(ev_immediate([cm] {
            cm->hover();
            return true;
        }));
    }
    int to_key = from_key + (e->config->get(K("ref_value")).equals("l") ? -1 : 1);
    if (to_key <= 0) to_key = n;
    if (to_key > n) to_key = 1;
    rt->set("current_option", to_key);
    Value to_val = opts->get(to_key);
    rt->set("current_option_val", to_val);
    UIElement *new_pip = e->box->get_UIE_by_ID("pip_" + std::to_string(to_key), scope);
    if (old_pip) old_pip->config->set("colour", G.colour("BLACK"));
    if (new_pip) new_pip->config->set("colour", G.colour("WHITE"));
    if (rt->get(K("opt_callback")).truthy())
        call_t(rt->get(K("opt_callback")),
               vl::T({{"from_val", from_val}, {"to_val", to_val}, {"from_key", from_key}, {"to_key", to_key}, {"cycle_config", Value(TableRef(rt))}}));
}

void change_tab(UIElement *e) {
    if (!e) return;
    if (UIBox *o = overlay()) {
        if (UIElement *info = o->get_UIE_by_ID("overlay_menu_infotip")) {
            if (Node *obj = info->config->get(K("object")).as<Node>()) {
                obj->remove();
                info->config->set("object", Value((Object *)new Moveable(0, 0, 0, 0)));
            }
        }
    }
    UIElement *tab_contents = e->box->get_UIE_by_ID("tab_contents");
    if (!tab_contents) return;
    if (Node *old = tab_contents->config->get(K("object")).as<Node>()) old->remove();
    Value ref = e->config->get(K("ref_table"));
    Func *f = fld(ref, "tab_definition_function").as<Func>();
    TableRef def = f ? TableRef(f->call(fld(ref, "tab_definition_function_args")).table()) : TableRef();
    UIBox *box = new UIBox(def, vl::T({{"offset", Value(vl::T({{"x", 0}, {"y", 0}}))}, {"parent", Value((Object *)tab_contents)}, {"type", "cm"}}));
    tab_contents->config->set("object", Value((Object *)box));
    tab_contents->box->recalculate();
}

void set_button_pip(UIElement *e) {
    Value fa = e->config->get(K("focus_args"));
    if (G.CONTROLLER->HID.controller && fa.truthy() && !e->children.get("button_pip")) {
        Value orientation = fld(fa, "orientation");
        Value offset = fld(fa, "offset");
        if (!offset.truthy())
            offset = orientation.equals("bm") ? Value(vl::T({{"x", 0}, {"y", 0.02}})) : Value(vl::T({{"x", 0.1}, {"y", 0.02}}));
        UIBox *pip = new UIBox(create_button_binding_pip(vl::T({{"button", fld(fa, "button")}, {"scale", fld(fa, "scale")}})),
                               vl::T({{"align", orientation.truthy() ? orientation : Value("cr")},
                                      {"offset", offset},
                                      {"major", Value((Object *)e)},
                                      {"parent", Value((Object *)e)}}));
        pip->states.collide.can = false;
        e->children.set("button_pip", pip);
    }
    if (!G.CONTROLLER->HID.controller && e->children.get("button_pip")) {
        e->children.get("button_pip")->remove();
        e->children.set("button_pip", nullptr);
    }
}

void beta_lang_alert(UIElement *e) {
    if (e->children.get("alert")) return;
    LangDef *lang = e->config->get(K("ref_table")).as<LangDef>();
    if (lang && lang->beta) {
        UIBox *a = new UIBox(create_UIBox_card_alert(vl::T({{"no_bg", true}, {"text", "BETA"}, {"scale", 0.35}})),
                             vl::T({{"instance_type", "ALERT"},
                                    {"align", "tri"},
                                    {"offset", Value(vl::T({{"x", 0.07}, {"y", -0.07}}))},
                                    {"major", Value((Object *)e)},
                                    {"parent", Value((Object *)e)}}));
        a->states.collide.can = false;
        e->children.set("alert", a);
    }
}

void warn_lang(UIElement *e) {
    UIBox *o = overlay();
    UIElement *info = o ? o->get_UIE_by_ID("overlay_menu_infotip") : nullptr;
    LangDef *lang = e->config->get(K("ref_table")).as<LangDef>();
    if (!info || !lang) return;
    if (!(info->config->get(K("set")) == Value(lang->label)) && !G.F_NO_ACHIEVEMENTS) {
        if (Node *obj = info->config->get(K("object")).as<Node>()) obj->remove();
        TableRef rows = new_table();
        for (int i = 0; i < 3; i++) rows->set(i + 1, i < (int)lang->warning.size() ? Value(lang->warning[i]) : Value());
        rows->set("lang", Value((Object *)lang));
        UIBox *box = new UIBox(overlay_infotip(Value(rows)),
                               vl::T({{"offset", Value(vl::T({{"x", 0}, {"y", 0}}))}, {"align", "bm"}, {"parent", Value((Object *)info)}}));
        info->config->set("object", Value((Object *)box));
        box->UIRoot->juice_up();
        info->config->set("set", Value(lang->label));
        e->config->set("disable_button", true);  // (sic: the original sets the config field, not e.disable_button)
        EventConfig a = ev_after(0.06, [] {
            play_sound("tarot2", 0.76f, 0.4f);
            return true;
        });
        a.blockable = false;
        a.blocking = false;
        G.E_MANAGER->add_event(std::move(a));
        Ref<UIElement> self(e);
        EventConfig b = ev_after(0.35, [self] {
            self->config->set("disable_button", Value());
            return true;
        });
        b.blockable = false;
        b.blocking = false;
        G.E_MANAGER->add_event(std::move(b));
        e->config->set("button", "change_lang");
        play_sound("tarot2", 1, 0.4f);
    }
}

void change_lang(UIElement *e) {
    LangDef *lang = e->config->get(K("ref_table")).as<LangDef>();
    if (!lang || lang == G.LANG) {
        exit_overlay_menu();
        return;
    }
    G.SETTINGS->set("language", Value(lang->key));
    G.set_language();
    G.E_MANAGER->clear_queue();
    wipe_on();
    EventConfig ev = ev_immediate([] {
        G.delete_run();
        G.init_item_prototypes();
        G.main_menu();
        return true;
    });
    ev.no_delete = true;
    ev.blockable = true;
    ev.blocking = false;
    G.E_MANAGER->add_event(std::move(ev));
    wipe_off();
}

void refresh_contrast_mode() {
    Value proto = G.C->get(G.SETTINGS->get(K("colourblind_option")).truthy() ? "SO_2" : "SO_1");
    Table *suits = G.C->get(K("SUITS")).table();
    for (const char *s : {"Hearts", "Diamonds", "Spades", "Clubs"}) suits->set(s, fld(proto, s));
    G.I.CARD.each([](Moveable *m) {
        Card *c = dynamic_cast<Card *>(m);
        if (c && c->config->get(K("card")).truthy() && c->children.get("front") && !c->ability->get(K("effect")).equals("Stone Card"))
            c->set_sprites(Value(), c->config->get(K("card")));
    });
}
}  // namespace

void register_button_callbacks() {
    auto &F = G.FUNCS;
    F["exit_overlay_menu"] = [](UIElement *) { exit_overlay_menu(); };
    F["options"] = [](UIElement *) {
        G.SETTINGS->set("paused", true);
        overlay_menu(create_UIBox_options());
    };
    F["settings"] = [](UIElement *) {
        G.SETTINGS->set("paused", true);
        overlay_menu(create_UIBox_settings(), vl::T({{"offset", Value(vl::T({{"x", 0}, {"y", 10}}))}}));
    };
    F["language_selection"] = [](UIElement *) {
        G.SETTINGS->set("paused", true);
        overlay_menu(language_selector());
    };
    F["change_tab"] = change_tab;
    F["option_cycle"] = option_cycle;
    F["toggle_button"] = toggle_button;
    F["toggle"] = toggle;
    F["slider"] = slider;
    F["set_button_pip"] = set_button_pip;
    F["beta_lang_alert"] = beta_lang_alert;
    F["warn_lang"] = warn_lang;
    F["change_lang"] = change_lang;

    auto &T = G.FUNCS_T;
    T["change_gamespeed"] = [](const TableRef &a) { G.SETTINGS->set("GAMESPEED", a->get(K("to_val"))); };
    T["change_play_discard_position"] = [](const TableRef &a) {
        G.SETTINGS->set("play_button_pos", a->get(K("to_key")));
        // TODO(run): rebuild G.buttons (create_UIBox_buttons) when a run is active
    };
    T["change_shadows"] = [](const TableRef &a) {
        G.SETTINGS->get(K("GRAPHICS")).table()->set("shadows", a->get(K("to_key")).num() == 1 ? "On" : "Off");
        G.save_settings();
    };
    T["change_pixel_smoothing"] = [](const TableRef &a) {
        G.SETTINGS->get(K("GRAPHICS")).table()->set("texture_scaling", a->get(K("to_key")));
        G.set_render_settings();
        G.save_settings();
    };
    T["refresh_contrast_mode"] = [](const TableRef &) { refresh_contrast_mode(); };
}

}  // namespace vl
