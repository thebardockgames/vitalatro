// functions/button_callbacks.lua: menus, settings and language selection.
#include "game/button_callbacks.h"
#include "game/challenges.h"

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
#include "game/card_ui.h"
#include "game/shop.h"
#include "game/functions.h"
#include "game/g.h"
#include "game/state_events.h"
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
    // G.OVERLAY_MENU = true while the UIBox is built (code run meanwhile checks it); the placeholder
    // is not a node of the original, so it gives its ID back (card tilts and the like follow IDs)
    G.STAGE_OBJECT_INTERRUPT = true;
    auto *detail_log = G.id_detail_log;
    G.id_detail_log = nullptr;
    const auto id = G.ID;
    Moveable *sentinel = new Moveable();  // not a Lua Moveable: kept out of G.I.MOVEABLE
    sentinel->init(0, 0, 0, 0);
    G.OVERLAY_MENU = sentinel;
    G.ID = id;
    G.id_detail_log = detail_log;
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
        float cx = e->zoomed ? G.overlay_zoom.unzoom({G.CURSOR->T->x, G.CURSOR->T->y}).x : G.CURSOR->T->x;
        double v = std::min(mx, std::max(mn, mn + (mx - mn) * (cx - e->parent->T->x - G.ROOM->T->x) / e->T->w));
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

// ------------------------------------------------------------------ text input
namespace {
UIElement *hook() { return dynamic_cast<UIElement *>(G.CONTROLLER->text_input_hook.get()); }
std::string config_str(UIElement *e, const char *k) { return e && e->config ? e->config->get(Value(intern(k))).to_string() : std::string(); }

// GET_TEXT_FROM_INPUT
std::string text_from_input() {
    std::string new_text;
    UIElement *h = hook();
    for (int i = 0; i < h->children.count(); i++) {
        UIElement *c = child_uie(h, i);
        if (c && config_str(c, "id").compare(0, 7, "letter_") == 0 && !config_str(c, "text").empty()) new_text += config_str(c, "text");
    }
    return new_text;
}

// MODIFY_TEXT_INPUT{letter, text_table, pos, delete}
void modify_text_input(const std::string &letter, const TableRef &text, int pos, bool del) {
    Table *letters = text->get(K("letters")).table();
    if (del && pos > 0) {
        if (pos >= letters->len()) {
            letters->set(pos, Value(""));
        } else {
            letters->set(pos, letters->get(pos + 1));
            modify_text_input(letter, text, pos + 1, del);
        }
        return;
    }
    Value swapped = letters->get(pos);
    letters->set(pos, Value(letter));
    if (swapped.truthy() && !swapped.to_string().empty()) modify_text_input(swapped.to_string(), text, pos + 1, false);
}

// TRANSPOSE_TEXT_INPUT(amount)
void transpose_text_input(int amount) {
    UIElement *h = hook();
    TableRef text(fld(h->config->get(K("ref_table")), "text").table());
    int position_child = -1;
    for (int i = 0; i < h->children.count(); i++)
        if (config_str(child_uie(h, i), "id") == "position") {
            position_child = i;
            break;
        }
    int dir = amount > 0 ? 1 : amount < 0 ? -1 : 0;  // amount/math.abs(amount) is nan for 0: no move
    while (amount != 0) {
        if (position_child + dir < 0 || position_child + dir >= h->children.count() - 1) break;
        UIElement *other = child_uie(h, position_child + dir);
        bool real_letter = config_str(other, "id").compare(0, 7, "letter_") == 0 && !config_str(other, "text").empty();
        h->children.swap(position_child, position_child + dir);
        if (real_letter) amount -= dir;
        position_child += dir;
    }
    Value rt = text->get(K("ref_table")), rv = text->get(K("ref_value"));
    text->set("current_position", (double)std::min<int>(position_child, (int)rt.get(rv).to_string().size()));
    h->box->recalculate();
    rt.table()->set(rv, Value(text_from_input()));
}
}  // namespace

// G.FUNCS.text_input_key{key, caps}
void text_input_key(std::string key, bool caps) {
    if (key == "[" || key == "]") return;
    if (key == "0") key = "o";
    UIElement *h = hook();
    TableRef hook_config(h->config->get(K("ref_table")).table());
    if (!hook_config->get(K("orig_colour")).truthy())
        hook_config->set("orig_colour", Value(hook_config->get(K("colour")).table()->deep_copy()));
    if (key.empty()) key = "%";
    caps = caps || G.CONTROLLER->capslock || hook_config->get(K("all_caps")).truthy();

    static const std::pair<const char *, const char *> keymap[] = {{"space", " "},     {"backspace", "BACKSPACE"}, {"delete", "DELETE"},
                                                                    {"return", "RETURN"}, {"right", "RIGHT"},       {"left", "LEFT"}};
    bool extended = hook_config->get(K("extended_corpus")).truthy();
    std::string corpus = std::string("123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz") + (extended ? " 0!$&()<>?:{}+-=,.[]_" : "");
    if (extended && key.size() == 1) {
        static const std::string lower_ext = "1234567890-=;',./", upper_ext = "!@#$%^&*()_+:\"<>?";
        size_t at = lower_ext.find(key);
        if (at != std::string::npos && caps) key = upper_ext.substr(at, 1);
    }
    TableRef text(hook_config->get(K("text")).table());
    bool mapped = false;
    for (auto &m : keymap)
        if (key == m.first) {
            key = m.second;
            mapped = true;
        }
    if (!mapped && caps)
        for (char &c : key) c = (char)std::toupper((unsigned char)c);

    transpose_text_input(0);
    Value rt = text->get(K("ref_table")), rv = text->get(K("ref_value"));
    size_t len = rt.get(rv).to_string().size();
    int pos = (int)text->get(K("current_position")).num();
    if (len > 0 && key == "BACKSPACE") {
        modify_text_input("", text, pos, true);
        transpose_text_input(-1);
    } else if (len > 0 && key == "DELETE") {
        modify_text_input("", text, pos + 1, true);
        transpose_text_input(0);
    } else if (key == "RETURN") {
        if (Func *cb = hook_config->get(K("callback")).as<Func>()) cb->call(Value());
        UIElement *pp = dynamic_cast<UIElement *>(h->parent ? h->parent->parent : nullptr);
        if (pp) pp->config->set("colour", hook_config->get(K("colour")));
        Value temp_colour = Value(hook_config->get(K("orig_colour")).table()->deep_copy());
        Table *c = hook_config->get(K("colour")).table();
        Value white = G.colour("WHITE");
        for (int i = 1; i <= 3; i++) c->set(i, white[i]);
        ease_colour(TableRef(c), temp_colour);
        G.CONTROLLER->text_input_hook = nullptr;
    } else if (key == "LEFT") {
        transpose_text_input(-1);
    } else if (key == "RIGHT") {
        transpose_text_input(1);
    } else if (hook_config->get(K("max_length")).num() > (double)len && key.size() == 1 && corpus.find(key) != std::string::npos) {
        modify_text_input(key, text, pos + 1, false);
        transpose_text_input(1);
    }
}

namespace {
// ------------------------------------------------------------------ profiles
std::pair<bool, double> check_profile(const Value &profile_tab) {
    bool deleted = true;
    double version = -1;
    if (profile_tab.truthy()) {
        if (fld(profile_tab, "deleted").truthy()) {
            deleted = true;
            version = fld(profile_tab, "ver").num();
        } else if (fld(profile_tab, "progress").truthy()) {
            deleted = false;
            version = fld(profile_tab, "ver").num();
        }
    }
    return {deleted, version};
}

void load_profile_fn() {
    G.E_MANAGER->clear_queue();
    wipe_on();
    EventConfig a = ev_immediate([] {
        G.delete_run();
        Value _name;
        G.SETTINGS->set("profile", G.focused_profile ? G.focused_profile : 1);
        Value name = fld(G.PROFILES->get(G.focused_profile), "name");
        if (name.truthy() && !name.to_string().empty()) _name = name;
        G.PROFILES->set(G.focused_profile, Value(new_table()));
        G.DISCOVER_TALLIES = nullptr;
        G.PROGRESS = nullptr;
        G.load_profile(G.focused_profile);
        G.PROFILES->get(G.focused_profile).table()->set("name", _name);
        G.PROFILES->get(G.focused_profile).table()->set("deleted", Value());
        G.init_item_prototypes();
        return true;
    });
    a.no_delete = true;
    G.E_MANAGER->add_event(std::move(a));
    EventConfig b = ev_immediate([] {
        G.main_menu();
        G.FILE_HANDLER->set("force", true);
        return true;
    });
    b.no_delete = true;
    b.blockable = true;
    b.blocking = false;
    G.E_MANAGER->add_event(std::move(b));
    wipe_off();
}

void delete_profile(UIElement *e) {
    UIElement *warning_text = e->box->get_UIE_by_ID("warning_text");
    if (warning_text->config->get(K("colour")) != G.colour("WHITE")) {
        warning_text->juice_up();
        warning_text->config->set("colour", G.colour("WHITE"));
        warning_text->config->set("shadow", true);
        e->config->set("disable_button", true);
        EventConfig s = ev_after(0.06, [] {
            play_sound("tarot2", 0.76f, 0.4f);
            return true;
        });
        s.blockable = false;
        s.blocking = false;
        G.E_MANAGER->add_event(std::move(s));
        NodeRef er(e);
        EventConfig d = ev_after(0.35, [er] {
            if (auto *u = dynamic_cast<UIElement *>(er.get())) u->config->set("disable_button", Value());
            return true;
        });
        d.blockable = false;
        d.blocking = false;
        G.E_MANAGER->add_event(std::move(d));
        play_sound("tarot2", 1, 0.4f);
        return;
    }
    std::string p = std::to_string(G.focused_profile);
    for (const char *f : {"/save.jkr", "/meta.jkr", "/unlock_notify.jkr"}) {
        platform::remove_save((p + f).c_str());
        G.FILES->set(Value(p + f), Value());
    }
    G.DISCOVER_TALLIES = nullptr;
    G.PROGRESS = nullptr;
    Table *counters = G.SETTINGS->get(K("profile_counters")).table();
    counters->set(G.focused_profile, counters->get(G.focused_profile).num() + 1);
    G.empty_profile(G.focused_profile);
    if (G.focused_profile == (int)G.SETTINGS->get(K("profile")).num()) {
        load_profile_fn();
    } else {
        change_tab(static_cast<UIBox *>(G.OVERLAY_MENU.get())->get_UIE_by_ID(("tab_but_" + p).c_str()));
    }
}
}  // namespace

void register_button_callbacks() {
    auto &F = G.FUNCS;
    register_run_callbacks();
    register_card_ui_callbacks();
    register_shop_callbacks();
    register_game_over_callbacks();
    register_run_setup_callbacks();
    register_run_info_callbacks();
    register_challenge_callbacks();
    register_consumable_callbacks();
    register_booster_callbacks();
    F["exit_overlay_menu"] = [](UIElement *) { exit_overlay_menu(); };
    // G.FUNCS.continue_unlock: removes the overlay menu and immediately shows the next unlock screen if there is one
    F["continue_unlock"] = [](UIElement *) {
        exit_overlay_menu();
        G.CONTROLLER->mod_cursor_context_layer(-2000);
        G.E_MANAGER->update(0, true);
    };
    F["options"] = [](UIElement *) {
        G.SETTINGS->set("paused", true);
        overlay_menu(create_UIBox_options());
    };
    F["settings"] = [](UIElement *) {
        G.SETTINGS->set("paused", true);
        overlay_menu(create_UIBox_settings(), vl::T({{"offset", Value(vl::T({{"x", 0}, {"y", 10}}))}}));
    };
    F["flash"] = [](UIElement *e) {
        Table *c = e->config->get(K("colour")).table();
        if (G.CONTROLLER->text_input_hook) {
            c->set(4, (int)std::floor(G.TIMERS.REAL * 2) % 2 == 1 ? 0 : 1);
            if (e->config->get(K("w")).num() != 0.1) {
                e->config->set("w", 0.1);
                e->box->recalculate();
            }
        } else {
            c->set(4, 0);
            if (e->config->get(K("w")).num() != 0) {
                e->config->set("w", 0);
                e->box->recalculate();
            }
        }
    };
    F["text_input"] = [](UIElement *e) {
        Value args = e->config->get(K("ref_table"));
        UIElement *pp = dynamic_cast<UIElement *>(e->parent->parent);
        bool hooked = G.CONTROLLER->text_input_hook.get() == e;
        Table *a = args.table();
        if (hooked) {
            pp->config->set("colour", a->get(K("hooked_colour")));
            a->set("current_prompt_text", "");
            a->set("current_position_text", a->get(K("position_text")));
        } else {
            pp->config->set("colour", a->get(K("colour")));
            Value text = a->get(K("text"));
            a->set("current_prompt_text", fld(text, "ref_table").get(fld(text, "ref_value")).to_string().empty() ? a->get(K("prompt_text")) : Value(""));
            a->set("current_position_text", "");
        }
        // The Vita has no physical keyboard: the on-screen one opens whatever the input device
        // (the original opens it for controllers and on mobile)
        UIElement *OSkeyboard_e = dynamic_cast<UIElement *>(pp->parent);
        if (hooked) {
            if (!OSkeyboard_e->children.get("controller_keyboard")) {
                Value off = fld(e->config->get(K("ref_table")), "keyboard_offset");
                UIBox *kb = new UIBox(create_keyboard_input(vl::T({{"backspace_key", true}, {"return_key", true}, {"space_key", false}})),
                                      vl::T({{"align", "cm"},
                                             {"offset", Value(vl::T({{"x", 0}, {"y", off.truthy() ? off : Value(-4)}}))},
                                             {"major", Value((Object *)e->box)},
                                             {"parent", Value((Object *)OSkeyboard_e)}}));
                OSkeyboard_e->children.set("controller_keyboard", kb);
                G.CONTROLLER->screen_keyboard = kb;
                G.CONTROLLER->mod_cursor_context_layer(1);
            }
        } else if (Node *kb = OSkeyboard_e->children.get("controller_keyboard")) {
            kb->remove();
            OSkeyboard_e->children.set("controller_keyboard", nullptr);
            G.CONTROLLER->screen_keyboard = nullptr;
            G.CONTROLLER->mod_cursor_context_layer(-1);
        }
    };
    F["select_text_input"] = [](UIElement *e) {
        G.CONTROLLER->text_input_hook = e->children.at(0)->children.at(0);
        transpose_text_input(0);
        e->box->recalculate();
    };
    F["key_button"] = [](UIElement *e) {
        Value key = fld(e->config->get(K("ref_table")), "key");
        if (key.truthy()) G.CONTROLLER->key_press_update(key.to_string(), 0);
    };
    F["profile_select"] = [](UIElement *) {
        G.SETTINGS->set("paused", true);
        G.focused_profile = (int)G.SETTINGS->get(K("profile")).num();
        for (int i = 1; i <= 3; i++)
            if (i != G.focused_profile && G.FILES->get(Value(std::to_string(i) + "/profile.jkr")).truthy()) G.load_profile(i);
        G.load_profile(G.focused_profile);
        overlay_menu(profile_select());
    };
    F["can_load_profile"] = [](UIElement *e) {
        if ((int)G.SETTINGS->get(K("profile")).num() == G.focused_profile) {
            e->config->set("colour", fld(G.colour("UI"), "BACKGROUND_INACTIVE"));
            e->config->set("button", Value());
        } else {
            e->config->set("colour", G.colour("BLUE"));
            e->config->set("button", "load_profile");
        }
    };
    F["load_profile"] = [](UIElement *) { load_profile_fn(); };
    F["can_delete_profile"] = [](UIElement *e) {
        if (G.CHECK_PROFILE_DATA != 1)  // not G.CHECK_PROFILE_DATA: nil and false both re-check
            G.CHECK_PROFILE_DATA = check_profile(G.FILES->get(Value(std::to_string(G.focused_profile) + "/profile.jkr"))).first ? 0 : 1;
        if (G.CHECK_PROFILE_DATA == 0 || e->config->get(K("disable_button")).truthy()) {
            G.CHECK_PROFILE_DATA = 0;
            e->config->set("colour", fld(G.colour("UI"), "BACKGROUND_INACTIVE"));
            e->config->set("button", Value());
        } else {
            e->config->set("colour", G.colour("RED"));
            e->config->set("button", "delete_profile");
        }
    };
    F["delete_profile"] = delete_profile;
    F["can_unlock_all"] = [](UIElement *e) {
        if (fld(G.PROFILES->get(G.SETTINGS->get(K("profile"))), "all_unlocked").truthy() || e->config->get(K("disable_button")).truthy()) {
            e->config->set("colour", fld(G.colour("UI"), "BACKGROUND_INACTIVE"));
            e->config->set("button", Value());
        } else {
            e->config->set("colour", G.colour("GREY"));
            e->config->set("button", "unlock_all");
        }
    };
    F["unlock_all"] = [](UIElement *e) {
        UIElement *infotip = static_cast<UIBox *>(G.OVERLAY_MENU.get())->get_UIE_by_ID("overlay_menu_infotip");
        if (!infotip->config->get(K("set")).truthy() && !G.F_NO_ACHIEVEMENTS) {
            if (Node *o = infotip->config->get(K("object")).as<Node>()) o->remove();
            UIBox *box = new UIBox(overlay_infotip(localize("ml_unlock_all_explanation")),
                                   vl::T({{"offset", Value(vl::T({{"x", 0}, {"y", 0}}))}, {"align", "bm"}, {"parent", Value((Object *)infotip)}}));
            infotip->config->set("object", Value((Object *)box));
            box->UIRoot->juice_up();
            infotip->config->set("set", true);
            e->config->set("disable_button", true);
            EventConfig s = ev_after(0.06, [] {
                play_sound("tarot2", 0.76f, 0.4f);
                return true;
            });
            s.blockable = false;
            s.blocking = false;
            G.E_MANAGER->add_event(std::move(s));
            NodeRef er(e);
            EventConfig d = ev_after(0.35, [er] {
                if (auto *u = dynamic_cast<UIElement *>(er.get())) u->config->set("disable_button", Value());
                return true;
            });
            d.blockable = false;
            d.blocking = false;
            G.E_MANAGER->add_event(std::move(d));
            play_sound("tarot2", 1, 0.4f);
            return;
        }
        G.PROFILES->get(G.SETTINGS->get(K("profile"))).table()->set("all_unlocked", true);
        for (Table *pool : {G.P_CENTERS.get(), G.P_BLINDS.get(), G.P_TAGS.get()})
            pool->each([](const Value &, const Value &v) {
                if (!fld(v, "demo").truthy() && !fld(v, "wip").truthy()) {
                    v.table()->set("alerted", true);
                    v.table()->set("discovered", true);
                    v.table()->set("unlocked", true);
                }
            });
        set_profile_progress();
        set_discover_tallies();
        G.save_progress();
        G.FILE_HANDLER->set("force", true);
        change_tab(static_cast<UIBox *>(G.OVERLAY_MENU.get())->get_UIE_by_ID(("tab_but_" + std::to_string(G.focused_profile)).c_str()));
    };
    F["your_collection"] = [](UIElement *) {
        G.SETTINGS->set("paused", true);
        overlay_menu(create_UIBox_your_collection());
    };
    for (auto &p : std::vector<std::pair<const char *, const char *>>{{"your_collection_jokers", "Joker"},
                                                                     {"your_collection_tarots", "Tarot"},
                                                                     {"your_collection_planets", "Planet"},
                                                                     {"your_collection_spectrals", "Spectral"},
                                                                     {"your_collection_vouchers", "Voucher"},
                                                                     {"your_collection_boosters", "Booster"}}) {
        std::string kind = p.second;
        F[p.first] = [kind](UIElement *) {
            G.SETTINGS->set("paused", true);
            overlay_menu(create_UIBox_your_collection_cards(kind));
        };
    }
    for (auto &p : std::vector<std::pair<const char *, const char *>>{
             {"your_collection_seals", "Seal"}, {"your_collection_enhancements", "Enhanced"}, {"your_collection_editions", "Edition"}}) {
        std::string kind = p.second;
        F[p.first] = [kind](UIElement *) {
            G.SETTINGS->set("paused", true);
            overlay_menu(create_UIBox_your_collection_special(kind));
        };
    }
    F["high_scores"] = [](UIElement *) {
        G.SETTINGS->set("paused", true);
        overlay_menu(create_UIBox_high_scores());
    };
    F["usage"] = [](UIElement *) {
        G.SETTINGS->set("paused", true);
        overlay_menu(usage_tabs());
    };
    F["customize_deck"] = [](UIElement *) {
        G.SETTINGS->set("paused", true);
        overlay_menu(create_UIBox_customize_deck());
    };
    F["show_credits"] = [](UIElement *) {
        G.SETTINGS->set("paused", true);
        overlay_menu(credits());
    };
    F["CREDITS_check_collab"] = [](UIElement *e) {
        if (!e->config->get(K("id")).equals(G.viewed_collab.c_str())) {
            if (Node *o = e->config->get(K("object")).as<Node>()) o->remove();
            e->config->set("object", Value((Object *)new UIBox(viewed_collab_option(),
                                                                 vl::T({{"offset", Value(vl::T({{"x", 0}, {"y", 0}}))},
                                                                        {"align", "cm"},
                                                                        {"parent", Value((Object *)e)}}))));
            e->config->set("id", Value(G.viewed_collab));
        }
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
    for (auto &p : std::vector<std::pair<const char *, const char *>>{{"your_collection_joker_page", "Joker"},
                                                                     {"your_collection_tarot_page", "Tarot"},
                                                                     {"your_collection_spectral_page", "Spectral"},
                                                                     {"your_collection_booster_page", "Booster"},
                                                                     {"your_collection_voucher_page", "Voucher"}}) {
        std::string kind = p.second;
        T[p.first] = [kind](const TableRef &a) {
            Value cc = a ? a->get(K("cycle_config")) : Value();
            if (!cc.truthy()) return;
            for (auto &area : G.your_collection)
                for (int i = (int)area->cards.size() - 1; i >= 0; i--) {
                    Card *c = area->remove_card(area->cards[i].get());
                    if (c) c->remove();
                }
            collection_fill(kind, (int)fld(cc, "current_option").num());
        };
    }
    T["change_collab"] = [](const TableRef &a) {
        Value suit = fld(a->get(K("cycle_config")), "curr_suit");
        Value collab = fld(fld(Value(G.COLLABS), "options"), std::string(suit.str()).c_str()).get(a->get(K("to_key")));
        fld(fld(Value(G.SETTINGS), "CUSTOM_DECK"), "Collabs").table()->set(suit, collab.truthy() ? collab : Value("default"));
        G.I.CARD.each([](Moveable *m) {
            Card *v = dynamic_cast<Card *>(m);
            if (v && v->config && v->config->get(K("card")).truthy() && v->children.get("front") &&
                !fld(Value(v->ability), "effect").equals("Stone Card"))
                v->set_sprites(Value(), v->config->get(K("card")));
        });
        G.save_settings();
    };
    T["change_viewed_collab"] = [](const TableRef &a) { G.viewed_collab = a->get(K("to_val")).to_string(); };
    T["change_gamespeed"] = [](const TableRef &a) { G.SETTINGS->set("GAMESPEED", a->get(K("to_val"))); };
    T["change_play_discard_position"] = [](const TableRef &a) {
        G.SETTINGS->set("play_button_pos", a->get(K("to_key")));
        if (G.buttons) {
            G.buttons->remove();
            G.buttons = new UIBox(create_UIBox_buttons(), vl::T({{"align", "bm"}, {"offset", Value(vl::T({{"x", 0}, {"y", 0.3}}))},
                                                                 {"major", Value((Object *)G.hand.get())}, {"bond", "Weak"}}));
        }
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
