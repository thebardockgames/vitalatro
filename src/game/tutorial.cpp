// The tutorial: state_events.lua G.FUNCS.tutorial_controller / tutorial_part, common_events.lua
// tutorial_info, button_callbacks.lua tut_next / skip_tutorial_section / start_tutorial and the
// G.OVERLAY_TUTORIAL parts of game.lua.
#include "game/tutorial.h"

#include <algorithm>
#include <functional>
#include <string>

#include "engine/controller.h"
#include "engine/event.h"
#include "engine/ui.h"
#include "game/button_callbacks.h"
#include "game/card.h"
#include "game/card_character.h"
#include "game/functions.h"
#include "game/g.h"
#include "game/state_events.h"
#include "gfx/gfx.h"

namespace vl {

void add_to_drawhash(Node *n);

namespace {
inline Value fld(const Value &t, const char *k) { return t.get(Value(intern(k))); }
Value node(int uit, const TableRef &cfg, const TableRef &nodes = nullptr) {
    TableRef t = vl::T({{"n", (double)uit}, {"config", Value(cfg)}});
    if (nodes) t->set("nodes", Value(nodes));
    return Value(t);
}
Table *progress() { return G.SETTINGS->get(K("tutorial_progress")).table(); }
Table *parts(const char *which) { return progress()->get(Value(intern(which))).table(); }
bool done(const char *part) { return parts("completed_parts")->get(Value(intern(part))).truthy(); }
bool held(const char *part) { return parts("hold_parts")->get(Value(intern(part))).truthy(); }
void complete(const char *part) { parts("completed_parts")->set(Value(intern(part)), true); }

UIBox *box(const NodeRef &n) { return dynamic_cast<UIBox *>(n.get()); }
UIElement *hud(const char *id) { return box(G.HUD) ? box(G.HUD)->get_UIE_by_ID(id) : nullptr; }
// G.blind_select.UIRoot.children[1].children[i]
UIElement *blind_slot(int i) {
    UIBox *b = box(G.blind_select);
    if (!b || !b->UIRoot || b->UIRoot->children.count() < 1) return nullptr;
    Node *row = b->UIRoot->children.at(0);
    return row->children.count() >= i ? dynamic_cast<UIElement *>(row->children.at(i - 1)) : nullptr;
}
// G.blind_select.UIRoot.children[1].children[i].config.object:get_UIE_by_ID(id)
UIElement *blind_uie(int i, const char *id) {
    UIElement *s = blind_slot(i);
    UIBox *o = s ? dynamic_cast<UIBox *>(s->object()) : nullptr;
    return o ? o->get_UIE_by_ID(id) : nullptr;
}
Node *parent_of(Node *n, int levels = 1) {
    for (int i = 0; n && i < levels; i++) n = n->parent;
    return n;
}
// G.HUD:get_UIE_by_ID('dollar_text_UI').parent.parent.parent
Node *dollars_box() { return parent_of(hud("dollar_text_UI"), 3); }
Node *card_at(const Ref<CardArea> &a, int i) {
    if (!a) return nullptr;
    if (i < 0) i = (int)a->cards.size();
    return i >= 1 && i <= (int)a->cards.size() ? a->cards[i - 1].get() : nullptr;
}

using NodeList = std::vector<Node *>;

// tutorial_info's args
struct TutInfo {
    std::string text_key;
    NodeList highlight;                       // args.highlight (a list)
    std::function<NodeList()> highlight_fn;   // args.highlight (a function, read when the step shows)
    Moveable *attach_major = nullptr;         // args.attach (nil: row_dollars_chips, 'tm', {0, -0.5})
    const char *attach_type = "tm";
    XY attach_offset{0, -0.5f};
    bool hard_set = false, no_button = false;
    std::function<Node *()> snap_to;
    const char *button_listen = nullptr;
    TableRef loc_vars, disabled_buttons;
    int step = 1;
};
TutInfo at(Moveable *major, const char *type, float x, float y) {
    TutInfo a;
    a.attach_major = major;
    a.attach_type = type;
    a.attach_offset = {x, y};
    return a;
}

// tutorial_info(args): queues one of Jimbo's steps; returns the next step's number
int tutorial_info(TutInfo args) {
    static const EventTimer real = EventTimer::REAL;
    TableRef overlay_colour = A({0.32, 0.36, 0.41, 0});
    ease_value(ValueRef::field(overlay_colour, Value(4)), 0.6, false, &real, 0.4);
    if (!G.OVERLAY_TUTORIAL) {
        // (the Skip button's text colour is G.C.TEXT_DARK, which does not exist: the default colour)
        TableRef skip = vl::T({{"ref_table", Value(new_table())},
                               {"align", "tm"},
                               {"padding", 0.1},
                               {"r", 0.15},
                               {"colour", G.colour("JOKER_GREY")},
                               {"minw", 1.6},
                               {"minh", 1.2},
                               {"focus_args", Value(vl::T({{"button", "b"}, {"orientation", "bmi"}, {"offset", Value(vl::T({{"x", 0}, {"y", -0.1}}))}}))},
                               {"func", "set_button_pip"},
                               {"button", "skip_tutorial_section"},
                               {"hover", true},
                               {"shadow", true}});
        TableRef def = TableRef(
            node(UIT_ROOT, vl::T({{"align", "cm"}, {"padding", 32.05}, {"r", 0.1}, {"colour", Value(overlay_colour)}, {"emboss", 0.05}}),
                 A({node(UIT_R, vl::T({{"align", "br"}, {"minh", G.ROOM->T->h}, {"minw", G.ROOM->T->w}}),
                         A({node(UIT_R, skip, A({node(UIT_T, vl::T({{"text", Value(localize("b_skip").to_string() + " >")}, {"scale", 0.5}}))}))}))}))
                .table());
        G.OVERLAY_TUTORIAL = new TutorialOverlay(def, vl::T({{"align", "cm"},
                                                             {"offset", Value(vl::T({{"x", -0.5}, {"y", -1}}))},
                                                             {"major", Value((Object *)G.ROOM_ATTACH.get())},
                                                             {"bond", "Weak"}}));
    }
    TutorialOverlay *o = tutorial_overlay();
    o->step_complete = false;
    if (!args.attach_major) {
        args.attach_major = hud("row_dollars_chips");
        args.attach_type = "tm";
        args.attach_offset = {0, -0.5f};
    }
    const int step = args.step;
    Transform &mt = *args.attach_major->T;
    TableRef pos = vl::T({{"x", mt.x + mt.w / 2}, {"y", mt.y + mt.h / 2}});
    std::vector<NodeRef> highlight;
    for (Node *n : args.highlight)
        if (n) highlight.push_back(NodeRef(n));
    Ref<Moveable> attach_major(args.attach_major);

    EventConfig e = ev_after(0.3, [args, step, pos, highlight, attach_major]() mutable {
        TutorialOverlay *o = tutorial_overlay();
        if (o && o->step == step && !o->step_complete) {
            Value tut = fld(fld(Value(G.localization), "misc"), "tutorial");
            if (G.F_MOBILE && tut.get(Value(args.text_key + "_touch")).truthy()) args.text_key += "_touch";
            G.CONTROLLER->interrupt.focus = true;
            if (!o->Jimbo) o->Jimbo = new Card_Character(pos);
            Card_Character *jimbo = o->Jimbo.get();
            if (args.highlight_fn) {
                highlight.clear();
                for (Node *n : args.highlight_fn())
                    if (n) highlight.push_back(NodeRef(n));
            }
            highlight.push_back(NodeRef(jimbo));
            jimbo->add_speech_bubble(args.text_key, "tm", args.loc_vars);
            AlignArgs al;
            al.major = attach_major.get();
            al.type = args.attach_type;
            al.offset = &args.attach_offset;
            jimbo->set_alignment(al);
            if (args.hard_set) jimbo->hard_set_VT();
            o->set_var("button_listen", Value());
            // (G.OVERLAY_TUTORIAL.content: no step has args.content)
            G.DISABLED_BUTTONS = args.disabled_buttons;
            if (args.button_listen) o->set_var("button_listen", Value(args.button_listen));
            if (!args.no_button) jimbo->add_button(localize("b_next"), Value("tut_next"), Value(), Value(), true);
            Table *lines = tut.get(Value(args.text_key)).table();
            jimbo->say_stuff(2 * (lines ? lines->len() : 0) + 1);
            if (args.snap_to) {
                std::function<Node *()> snap_to = args.snap_to;
                EventConfig s = ev_immediate([snap_to] {
                    TutorialOverlay *o = tutorial_overlay();
                    if (o && o->Jimbo && !o->Jimbo->talking) {
                        if (snap_to()) {
                            G.CONTROLLER->interrupt.focus = false;
                            G.CONTROLLER->snap_to(snap_to());
                        }
                        return true;
                    }
                    return false;
                });
                s.blocking = false;
                s.blockable = false;
                G.E_MANAGER->add_event(std::move(s), EventManager::TUTORIAL);
            }
            o->highlights = highlight;
            o->has_highlights = true;
            o->step_complete = true;
        }
        return !o || o->step > step || o->skip_steps;
    });
    G.E_MANAGER->add_event(std::move(e), EventManager::TUTORIAL);
    return step + 1;
}

// G.FUNCS.tutorial_part(_part)
void tutorial_part(const std::string &part) {
    int step = 1;
    auto info = [&](TutInfo a, const char *key) {
        a.text_key = key;
        a.step = step;
        step = tutorial_info(std::move(a));
    };
    Moveable *room = G.ROOM_ATTACH.get();
    Moveable *hud_box = box(G.HUD);
    Moveable *shop = box(G.shop);
    if (part == "small_blind") {
        info(at(room, "cm", 0, 0), "sb_1");
        info(at(room, "cm", 0, 0), "sb_2");
        TutInfo a = at(blind_slot(1), "tr", 2, 4);
        a.highlight = {blind_uie(1, "blind_name"), blind_uie(1, "blind_desc")};
        info(a, "sb_3");
        a = at(blind_slot(1), "tr", 2, 4);
        a.highlight = {blind_slot(1)};
        a.snap_to = [] { return (Node *)blind_uie(1, "select_blind_button"); };
        a.no_button = true;
        a.button_listen = "select_blind";
        info(a, "sb_4");
    } else if (part == "big_blind") {
        TutInfo a = at(hud_box, "cm", 0.7f, -0.5f);
        a.highlight = {blind_uie(2, "blind_name"), blind_uie(2, "blind_desc")};
        a.hard_set = true;
        info(a, "bb_1");
        a = at(hud_box, "cm", 0.7f, -0.5f);
        a.highlight = {blind_uie(2, "blind_name"), parent_of(blind_uie(2, "tag_desc"))};
        a.disabled_buttons = A({Value("skip_blind")});
        info(a, "bb_2");
        a = at(hud_box, "cm", 0.7f, -0.5f);
        a.highlight = {blind_uie(3, "blind_name"), blind_uie(3, "blind_desc")};
        info(a, "bb_3");
        a = at(hud_box, "cm", 0.7f, -0.5f);
        a.highlight = {blind_uie(3, "blind_name"), blind_uie(3, "blind_desc"), blind_uie(3, "blind_extras"), hud("hud_ante")};
        info(a, "bb_4");
        a = at(hud_box, "cm", 0.7f, -0.5f);
        a.loc_vars = A({G.GAME->get(K("win_ante"))});
        a.highlight = {G.blind_select.get(), hud("hud_ante")};
        a.no_button = true;
        a.snap_to = [] { return (Node *)blind_uie(2, "select_blind_button"); };
        a.button_listen = "select_blind";
        info(a, "bb_5");
    } else if (part == "first_hand") {
        info(at(room, "cm", 0, 0), "fh_1");
        TutInfo a = at(room, "cm", 0, 0);
        a.highlight = {hud("hand_text_area")};
        info(a, "fh_2");
        a = at(room, "cm", 0, 0);
        a.highlight = {hud("run_info_button")};
        a.no_button = true;
        a.button_listen = "run_info";
        a.snap_to = [] { return (Node *)hud("run_info_button"); };
        info(a, "fh_3");
    } else if (part == "first_hand_2") {
        TutInfo a = at(G.hand.get(), "cl", -1, -0.3f);
        a.hard_set = true;
        a.highlight = {G.hand.get(), hud("run_info_button")};
        a.snap_to = [] { return card_at(G.hand, 1); };
        info(a, "fh_4");
        a = at(G.hand.get(), "cl", -1, -0.3f);
        a.highlight = {G.hand.get(), box(G.buttons)->get_UIE_by_ID("play_button"), hud("run_info_button")};
        a.no_button = true;
        a.button_listen = "play_cards_from_highlighted";
        info(a, "fh_5");
    } else if (part == "first_hand_3") {
        TutInfo a = at(G.hand.get(), "cl", -1, -0.3f);
        a.hard_set = true;
        a.highlight = {G.hand.get(), box(G.buttons)->get_UIE_by_ID("discard_button"), hud("run_info_button")};
        a.no_button = true;
        a.button_listen = "discard_cards_from_highlighted";
        info(a, "fh_6");
    } else if (part == "first_hand_4") {
        TutInfo a = at(room, "cm", 0, 0);
        a.hard_set = true;
        a.highlight = {parent_of(hud("hud_hands"))};
        info(a, "fh_7");
        a = at(room, "cm", 0, 0);
        a.highlight = {parent_of(hud("hud_hands")), hud("row_dollars_chips"), G.HUD_blind.get()};
        info(a, "fh_8");
    } else if (part == "second_hand") {
        TutInfo a = at(hud_box, "cm", 0.7f, -0.5f);
        a.hard_set = true;
        a.highlight = {G.jokers.get()};
        info(a, "sh_1");
        std::vector<Card *> found = find_joker("The Empress");
        if (!found.empty()) {
            Card *empress = found[0];
            a = at(hud_box, "cm", 0.7f, -0.5f);
            a.highlight = {empress};
            info(a, "sh_2");
            a = at(hud_box, "cm", 0.7f, -0.5f);
            a.highlight = {empress, G.hand.get()};
            a.no_button = true;
            a.button_listen = "use_card";
            a.snap_to = [] { return card_at(G.hand, 1); };
            info(a, "sh_3");
        }
    } else if (part == "shop_1") {
        TutInfo a = at(shop, "tm", 0, 4);
        a.hard_set = true;
        a.highlight = {G.SHOP_SIGN.get(), dollars_box()};
        info(a, "s_1");
        // the shop's steps read their highlights when they show
        auto sign_and = [](std::function<Node *()> f) {
            return [f] { return NodeList{G.SHOP_SIGN.get(), dollars_box(), f()}; };
        };
        a = at(shop, "tr", -0.5f, 5);
        a.highlight_fn = sign_and([] { return card_at(G.shop_jokers, 2); });
        a.snap_to = [] { return card_at(G.shop_jokers, 2); };
        a.no_button = true;
        a.button_listen = "buy_from_shop";
        info(a, "s_2");
        a = at(shop, "tm", 0, 6);
        a.loc_vars = A({(double)G.P_CENTER_POOLS->get(K("Joker")).table()->len()});
        a.highlight_fn = sign_and([] { return card_at(G.jokers, 1); });
        info(a, "s_3");
        a = at(shop, "tm", 0, 6);
        a.highlight_fn = sign_and([] { return card_at(G.jokers, 1); });
        info(a, "s_4");
        a = at(shop, "tm", 0, 6);
        a.highlight_fn = sign_and([] { return (Node *)G.jokers.get(); });
        info(a, "s_5");
        a = at(shop, "tr", -0.5f, 5);
        a.highlight_fn = sign_and([] { return card_at(G.shop_jokers, 1); });
        a.snap_to = [] { return card_at(G.shop_jokers, 1); };
        a.no_button = true;
        a.button_listen = "buy_from_shop";
        info(a, "s_6");
        a = at(shop, "tm", 0, 6);
        a.highlight_fn = sign_and([] { return card_at(G.consumeables, -1); });
        info(a, "s_7");
        a = at(shop, "tm", 0, 6);
        a.highlight_fn = sign_and([] { return (Node *)G.consumeables.get(); });
        info(a, "s_8");
        a = at(shop, "tr", -4, 5);
        a.highlight_fn = sign_and([] { return (Node *)G.shop_vouchers.get(); });
        a.snap_to = [] { return card_at(G.shop_vouchers, 1); };
        info(a, "s_9");
        a = at(shop, "tr", -4, 5);
        a.highlight_fn = sign_and([] { return (Node *)G.shop_vouchers.get(); });
        info(a, "s_10");
        a = at(shop, "tl", 3, 6);
        a.highlight_fn = sign_and([] { return (Node *)G.shop_booster.get(); });
        a.snap_to = [] { return card_at(G.shop_booster, 1); };
        info(a, "s_11");
        a = at(shop, "tm", 0, 6);
        a.highlight_fn = [] { return NodeList{box(G.shop) ? box(G.shop)->get_UIE_by_ID("next_round_button") : nullptr}; };
        a.snap_to = [] { return box(G.shop) ? (Node *)box(G.shop)->get_UIE_by_ID("next_round_button") : nullptr; };
        a.no_button = true;
        a.button_listen = "toggle_shop";
        info(a, "s_12");
    }

    // once the last step is done (or the section skipped) the overlay goes and the part is held
    EventConfig e = ev_immediate([step, part] {
        TutorialOverlay *o = tutorial_overlay();
        if (!o) return true;
        if ((o->step == step && !o->step_complete) || o->skip_steps) {
            remove_tutorial_overlay();
            parts("hold_parts")->set(Value(part), true);
            return true;
        }
        return o->step > step || o->skip_steps;
    });
    e.blockable = false;
    e.has_timer = true;
    e.timer = EventTimer::REAL;
    G.E_MANAGER->add_event(std::move(e), EventManager::TUTORIAL);
    G.SETTINGS->set("paused", false);
}

// Node:draw on a UI element (Moveable.draw): every visible descendant joins the draw hash, now
// above the overlay
void node_draw(Node *n) {
    n->draw_boundingrect();
    if (n->states.visible) {
        add_to_drawhash(n);
        n->children.each([](Str *, Node *c) {
            if (dynamic_cast<UIElement *>(c)) node_draw(c);
            else c->draw();
        });
    }
    n->draw_boundingrect();
}

TableRef new_progress() {
    return vl::T({{"forced_shop", Value(A({Value("j_joker"), Value("c_empress")}))},
                  {"forced_voucher", "v_grabber"},
                  {"forced_tags", Value(A({Value("tag_handy"), Value("tag_garbage")}))},
                  {"hold_parts", Value(new_table())},
                  {"completed_parts", Value(new_table())}});
}
}  // namespace

TutorialOverlay *tutorial_overlay() { return dynamic_cast<TutorialOverlay *>(G.OVERLAY_TUTORIAL.get()); }

void tutorial_controller() {
    if (G.F_SKIP_TUTORIAL) {
        G.SETTINGS->set("tutorial_complete", true);
        G.SETTINGS->set("tutorial_progress", Value());
        return;
    }
    if (!G.SETTINGS->get(K("tutorial_progress")).truthy()) G.SETTINGS->set("tutorial_progress", Value(new_progress()));
    if (G.paused() || G.SETTINGS->get(K("tutorial_complete")).truthy()) return;
    Table *tp = progress();
    if (G.STATE == BLIND_SELECT && G.blind_select && !done("small_blind")) {
        tp->set("section", "small_blind");
        tutorial_part("small_blind");
        complete("small_blind");
        G.save_progress();
    }
    if (G.STATE == BLIND_SELECT && G.blind_select && !done("big_blind") && G.GAME->get(K("round")).num() > 0) {
        tp->set("section", "big_blind");
        tutorial_part("big_blind");
        complete("big_blind");
        tp->set("forced_tags", Value());
        G.save_progress();
    }
    if (G.STATE == SELECTING_HAND && !done("second_hand") && held("big_blind")) {
        tp->set("section", "second_hand");
        tutorial_part("second_hand");
        complete("second_hand");
        G.save_progress();
    }
    if (held("second_hand")) set_cloud_flag("tutorial_complete");
    if (!done("first_hand_section")) {
        if (G.STATE == SELECTING_HAND && !done("first_hand")) {
            tp->set("section", "first_hand");
            tutorial_part("first_hand");
            complete("first_hand");
            G.save_progress();
        }
        if (G.STATE == SELECTING_HAND && !done("first_hand_2") && held("first_hand")) {
            tutorial_part("first_hand_2");
            complete("first_hand_2");
            G.save_progress();
        }
        if (G.STATE == SELECTING_HAND && !done("first_hand_3") && held("first_hand_2")) {
            tutorial_part("first_hand_3");
            complete("first_hand_3");
            G.save_progress();
        }
        if (G.STATE == SELECTING_HAND && !done("first_hand_4") && held("first_hand_3")) {
            tutorial_part("first_hand_4");
            complete("first_hand_4");
            complete("first_hand_section");
            G.save_progress();
        }
    }
    if (G.STATE == SHOP && G.shop && static_cast<Moveable *>(G.shop.get())->VT.y < 5 && !done("shop_1")) {
        tp->set("section", "shop");
        tutorial_part("shop_1");
        complete("shop_1");
        tp->set("forced_voucher", Value());
        G.save_progress();
    }
}

void tut_next() {
    if (TutorialOverlay *o = tutorial_overlay()) {
        if (o->Jimbo) {
            o->Jimbo->remove_button();
            o->Jimbo->remove_speech_bubble();
        }
        o->step_complete = false;
        o->step++;
    }
}

bool tutorial_listening(const char *button) {
    return G.OVERLAY_TUTORIAL && G.OVERLAY_TUTORIAL->var("button_listen").equals(button);
}

void remove_tutorial_overlay() {
    TutorialOverlay *o = tutorial_overlay();
    if (!o) return;
    Ref<TutorialOverlay> keep(o);
    if (o->Jimbo) o->Jimbo->remove();
    o->remove();
    G.OVERLAY_TUTORIAL = nullptr;
}

void draw_tutorial_overlay() {
    TutorialOverlay *o = tutorial_overlay();
    if (!o) return;
    Ref<TutorialOverlay> keep(o);
    gfx::push();
    o->translate_container();
    o->draw();
    gfx::pop();
    if (!o->has_highlights) return;
    const std::vector<NodeRef> hs = o->highlights;
    for (const NodeRef &v : hs) {
        gfx::push();
        v->translate_container();
        if (auto *u = dynamic_cast<UIElement *>(v.get())) {
            node_draw(u);
            u->draw_self();
            u->draw_children();
        } else {
            v->draw();
        }
        gfx::pop();
    }
}

void tutorial_highlight_add(Node *n) {
    if (TutorialOverlay *o = tutorial_overlay(); o && o->has_highlights) o->highlights.push_back(NodeRef(n));
}

void tutorial_highlight_remove(Node *n) {
    if (TutorialOverlay *o = tutorial_overlay(); o && o->has_highlights) {
        auto it = std::find_if(o->highlights.begin(), o->highlights.end(), [n](const NodeRef &r) { return r.get() == n; });
        if (it != o->highlights.end()) o->highlights.erase(it);
    }
}

void register_tutorial_callbacks() {
    auto &F = G.FUNCS;
    F["tut_next"] = [](UIElement *) { tut_next(); };
    F["skip_tutorial_section"] = [](UIElement *) {
        TutorialOverlay *o = tutorial_overlay();
        if (!o) return;
        o->skip_steps = true;
        remove_tutorial_overlay();
        G.E_MANAGER->clear_queue(EventManager::TUTORIAL);
        Table *tp = progress();
        const Value section = tp->get(K("section"));
        if (section.equals("small_blind")) {
            complete("small_blind");
        } else if (section.equals("big_blind")) {
            complete("big_blind");
            tp->set("forced_tags", Value());
        } else if (section.equals("second_hand")) {
            complete("second_hand");
            parts("hold_parts")->set("second_hand", true);
        } else if (section.equals("first_hand")) {
            for (const char *p : {"first_hand", "first_hand_2", "first_hand_3", "first_hand_4", "first_hand_section"}) complete(p);
        } else if (section.equals("shop")) {
            complete("shop_1");
            tp->set("forced_voucher", Value());
        }
    };
    F["start_tutorial"] = [](UIElement *e) {
        if (G.OVERLAY_MENU) exit_overlay_menu();
        G.SETTINGS->set("tutorial_progress", Value(new_progress()));
        G.SETTINGS->set("tutorial_complete", false);
        G.FUNCS["start_run"](e);
    };
}

}  // namespace vl
