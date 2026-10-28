// engine/controller.lua
#include "engine/controller.h"

#include <algorithm>
#include <cmath>

#include "engine/event.h"
#include "engine/sprite.h"
#include "engine/ui.h"
#include "core/lua_sort.h"
#include "game/card.h"
#include "game/shop.h"
#include "game/button_callbacks.h"
#include "game/g.h"

namespace vl {

namespace {
float vector_dist(XY a, XY b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }
float px_per_unit() { return G.TILESCALE * G.TILESIZE; }
double px_per_unit_d() { return G.TILESCALE_D * 20; }
UIElement *as_uie(Node *n) { return dynamic_cast<UIElement *>(n); }
Value focus_args(Node *n) { return n ? n->config->get(K("focus_args")) : Value(); }
void call_named(const char *name, UIElement *e = nullptr) {
    auto it = G.FUNCS.find(name);
    if (it != G.FUNCS.end()) it->second(e);
}
}  // namespace

Controller::Controller() {
    cursor_up.time = 0.1;
}

void Controller::set_gamepad() {
    if (GAMEPAD_CONSOLE == "Playstation") return;
    GAMEPAD_CONSOLE = "Playstation";
    auto it = G.ASSET_ATLAS.find("gamepad_ui");
    if (it == G.ASSET_ATLAS.end()) return;
    G.I.SPRITE.each([&](Moveable *m) {
        Sprite *s = dynamic_cast<Sprite *>(m);
        if (s && s->atlas == &it->second) {
            SpritePos p = s->sprite_pos;
            p.y = G.F_PS4_PLAYSTATION_GLYPHS ? 3 : 1;
            s->set_sprite_pos(p);
        }
    });
}

void Controller::set_HID_flags(const std::string &type, const std::string &) {
    if (type == "axis") {
        HID.controller = true;
        HID.last_type = "axis";
    } else if (!type.empty() && type != HID.last_type) {
        HID.dpad = type == "button";
        HID.pointer = type == "mouse" || type == "axis_cursor";
        HID.controller = type == "button" || type == "axis_cursor";
        HID.mouse = type == "mouse";
        if (HID.touch != (type == "touch")) {
            HID.touch = type == "touch";
            G.I.CARDAREA.each([](Moveable *v) {
                if (auto *a = dynamic_cast<CardArea *>(v)) a->set_ranks();
            });
        }
        HID.touch = type == "touch";
        HID.axis_cursor = type == "axis_cursor";
        HID.last_type = type;
        // love.mouse.setVisible: there is no OS cursor on the Vita
    }
    if (!HID.controller) GAMEPAD_CONSOLE = "";
}

void Controller::set_cursor_position() {
    if (HID.mouse || HID.touch) {
        interrupt.focus = false;
        if (focused.target) {
            focused.target->states.focus.is = false;
            focused.target = nullptr;
        }
        cursor_position = mouse_position;
        G.CURSOR->T->x = cursor_position.x / px_per_unit();
        G.CURSOR->T->y = cursor_position.y / px_per_unit();
        cursor_x = cursor_position.x / px_per_unit_d();
        cursor_y = cursor_position.y / px_per_unit_d();
        G.CURSOR->VT.x = G.CURSOR->T->x;
        G.CURSOR->VT.y = G.CURSOR->T->y;
    }
}

void Controller::update(double dt) {
    locked = false;
    locks["wipe"] = (bool)G.screenwipe;
    for (auto &kv : locks)
        if (kv.second) locked = true;

    if (locks["frame_set"]) {
        locks.erase("frame_set");
        overlay_timer = 0;
        EventConfig c = ev_after(0.1, [this] {
            locks.erase("frame");
            return true;
        });
        c.has_timer = true;
        c.timer = EventTimer::UPTIME;
        c.blocking = false;
        c.blockable = false;
        c.no_delete = true;
        G.E_MANAGER->add_event(std::move(c));
    }

    if (G.OVERLAY_MENU) overlay_timer += dt;
    else overlay_timer = 0;
    if (overlay_timer > 1.5) locks.erase("frame");

    cull_registry();
    set_HID_flags(update_axis(dt));

    G.CURSOR->states.visible = HID.pointer && !HID.mouse && !interrupt.focus;

    set_cursor_position();

    if (!G.screenwipe) {
        // pairs() over these sets: callbacks may mutate them, so iterate over snapshots
        auto run = [&](std::map<std::string, bool> &m, void (Controller::*fn)(const std::string &, double)) {
            std::vector<std::string> keys;
            for (auto &kv : m)
                if (kv.second) keys.push_back(kv.first);
            for (auto &k : keys) (this->*fn)(k, dt);
        };
        run(pressed_keys, &Controller::key_press_update);
        run(held_keys, &Controller::key_hold_update);
        run(released_keys, &Controller::key_release_update);
        run(pressed_buttons, &Controller::button_press_update);
        run(held_buttons, &Controller::button_hold_update);
        run(released_buttons, &Controller::button_release_update);
    }

    frame_buttonpress = false;
    pressed_keys.clear();
    released_keys.clear();
    pressed_buttons.clear();
    released_buttons.clear();

    if (HID.controller) {
        auto ctx = cursor_context.stack.find(cursor_context.layer);
        if (ctx != cursor_context.stack.end() && ctx->second.valid) {
            ContextEntry e = ctx->second;
            Node *n = e.node && !e.node->REMOVED ? e.node.get() : nullptr;
            snap_to(n, &e.cursor_pos);
            interrupt.stack = e.interrupt;
            cursor_context.stack.erase(ctx);
        }
        if (dragging.prev_target && !dragging.target && dragging.prev_target->is_card() && !dragging.prev_target->REMOVED) {
            if (!COYOTE_FOCUS) snap_to(dragging.prev_target.get());
            else COYOTE_FOCUS = false;
        }
        if (snap_cursor_to.active) {
            interrupt.focus = interrupt.stack;
            interrupt.stack = false;
            if (snap_cursor_to.is_node && !snap_cursor_to.node->REMOVED) {
                focused.prev_target = focused.target;
                focused.target = snap_cursor_to.node;
                update_cursor();
            } else if (!snap_cursor_to.is_node) {
                update_cursor(&snap_cursor_to.T);
            }
            if (focused.prev_target != focused.target && focused.prev_target) focused.prev_target->states.focus.is = false;
            snap_cursor_to = Snap{};
        }
    }

    get_cursor_collision({G.CURSOR->T->x, G.CURSOR->T->y});
    update_focus();
    set_cursor_hover();

    if (has_L_cursor_queue) {
        L_cursor_press(&L_cursor_queue.x, &L_cursor_queue.y);
        has_L_cursor_queue = false;
    }

    finger_cursor = cursor_hover.target;
    dragging.prev_target = dragging.target;
    released_on.prev_target = released_on.target;
    clicked.prev_target = clicked.target;
    hovering.prev_target = hovering.target;

    if (!cursor_down.handled) {
        if (HID.touch) {
            if (cursor_hover.target && cursor_hover.target->states.hover.can) finger_focus = cursor_hover.target;
            touch_control.clear_touch = true;
        }
        cursor_down.distance = 0;
        cursor_down.has_distance = true;
        cursor_down.duration = 0;
        cursor_down.has_duration = true;
        if (cursor_down.target && cursor_down.target->states.drag.can) {
            cursor_down.target->states.drag.is = true;
            cursor_down.target->set_offset(cursor_down.T, "Click");
            dragging.target = cursor_down.target;
            dragging.handled = false;
        }
        cursor_down.handled = true;
    }

    if (!cursor_up.handled) {
        if (dragging.target) {
            NodeRef d = dragging.target;
            d->stop_drag();
            finger_cursor = d;
            d->states.drag.is = false;
            dragging.target = nullptr;
        }
        auto find_releasable = [&]() {
            for (auto &v : collision_list) {
                if (v->states.hover.can && !v->states.drag.is && v != dragging.prev_target) {
                    if (v->states.release_on.can) {
                        released_on.target = v;
                        released_on.handled = false;
                    }
                    return;
                }
            }
        };
        if (HID.touch) {
            if (cursor_down.target) {
                if (cursor_down.distance < G.MIN_CLICK_DIST && cursor_down.duration < 0.2) {
                    if (cursor_down.target->states.click.can && !touch_control.l_press.target) {
                        touch_control.s_tap.target = cursor_down.target;
                        touch_control.s_tap.time = cursor_up.time - cursor_down.time;
                        touch_control.s_tap.handled = false;
                    }
                } else if (dragging.prev_target) {
                    find_releasable();
                }
            }
        } else if (cursor_down.target) {
            Value ct = cursor_down.target->var("click_timeout");
            if (!ct.truthy() || ct.num() > cursor_up.time - cursor_down.time) {
                if (vector_dist(cursor_down.T, cursor_up.T) < G.MIN_CLICK_DIST) {
                    if (cursor_down.target->states.click.can) {
                        clicked.target = cursor_down.target;
                        clicked.time = cursor_up.time - cursor_down.time;
                        clicked.handled = false;
                    }
                } else if (dragging.prev_target) {
                    find_releasable();
                }
            }
        }
        cursor_up.handled = true;
    }

    if (HID.touch) {
        if (!finger_cursor) finger_cursor = G.ROOM;
        if (finger_focus) {
            if ((!is_cursor_down && finger_focus != finger_cursor) || finger_focus.get() == G.ROOM.get()) finger_focus = nullptr;
        }
        if (finger_focus) {
            if (is_cursor_down && finger_focus->can_long_press() && cursor_down.duration > 0.2 &&
                !touch_control.l_press.target && !touch_control.l_pressed) {
                touch_control.l_press.target = finger_focus;
                touch_control.l_press.handled = false;
            }
        }
    } else {
        if (cursor_hover.target && cursor_hover.target->states.hover.can) {
            hovering.target = cursor_hover.target;
            if (hovering.prev_target && hovering.prev_target != hovering.target) hovering.prev_target->states.hover.is = false;
            hovering.target->states.hover.is = true;
            hovering.target->set_offset(cursor_hover.T, "Hover");
        } else if (!cursor_hover.target && hovering.target) {
            hovering.target->states.hover.is = false;
            hovering.target = nullptr;
        }
    }

    if (HID.touch) {
        if (finger_focus && !finger_focus->can_long_press()) {
            if (finger_focus != hovering.target && finger_focus->can_hover_on_drag()) {
                hovering.target = finger_focus;
                hovering.handled = false;
            }
        }
        if (!touch_control.s_tap.handled) {
            NodeRef t = touch_control.s_tap.target;
            if (t->has_single_tap()) {
                t->single_tap();
                if (finger_focus && !finger_focus->can_long_press()) {
                    hovering.target = t;
                    hovering.handled = false;
                }
            } else {
                t->click();
            }
            touch_control.s_tap.handled = true;
            touch_control.clear_touch = false;
        }
        if (!touch_control.l_press.handled) {
            hovering.target = touch_control.l_press.target;
            hovering.handled = false;
            touch_control.l_pressed = finger_focus;
            touch_control.l_press.handled = true;
        }
        if (touch_control.clear_touch || !finger_focus) {
            touch_control.l_press.target = nullptr;
            touch_control.l_pressed = nullptr;
            touch_control.s_tap.target = nullptr;
            hovering.target = nullptr;
            hovering.handled = false;
            touch_control.clear_touch = false;
        }
        if (touch_control.l_pressed) hovering.target = finger_focus;

        if (!dragging.handled && cursor_down.has_duration && cursor_down.duration > 0.1) {
            create_drag_target_from_card(dragging.target && dragging.target->is_card() ? static_cast<Card *>(dragging.target.get()) : nullptr);
            dragging.handled = true;
        }
        if (dragging.target) dragging.target->drag();

        if (!released_on.handled && dragging.prev_target) {
            released_on.target->release(dragging.prev_target.get());
            released_on.handled = true;
        }
        if (!hovering.handled) {
            if (hovering.target) {
                NodeRef h = hovering.target;
                h->hover();
                h->states.hover.is = true;
                h->set_offset(cursor_hover.T, "Hover");
                if (hovering.prev_target && hovering.prev_target != h) {
                    hovering.prev_target->states.hover.is = false;
                    hovering.prev_target->stop_hover();
                }
            } else if (hovering.prev_target) {
                hovering.prev_target->states.hover.is = false;
                hovering.prev_target->stop_hover();
            }
            hovering.handled = true;
        }
    } else {
        if (!clicked.handled) {
            NodeRef c = clicked.target;
            c->click();
            clicked.handled = true;
        }
        process_registry();
        if (dragging.target) dragging.target->drag();

        if (!released_on.handled && dragging.prev_target) {
            if (dragging.prev_target == hovering.target) {
                hovering.target->stop_hover();
                hovering.target = nullptr;
            }
            released_on.target->release(dragging.prev_target.get());
            released_on.handled = true;
        }
        if (hovering.target) {
            hovering.target->set_offset(cursor_hover.T, "Hover");
            if (hovering.prev_target != hovering.target) {
                if (hovering.target != dragging.target) {
                    hovering.target->hover();
                } else if (HID.touch) {
                    // unreachable in this branch (kept from the original)
                }
                if (hovering.prev_target) hovering.prev_target->stop_hover();
            }
        } else if (hovering.prev_target) {
            hovering.prev_target->stop_hover();
        }
        if (hovering.target && hovering.target == dragging.target) hovering.target->stop_hover();
    }

    if (is_cursor_down) {
        cursor_down.distance = std::max(vector_dist(cursor_down.T, cursor_hover.T), cursor_down.has_distance ? cursor_down.distance : 0.f);
        cursor_down.has_distance = true;
        cursor_down.duration = G.TIMERS.UPTIME - cursor_down.time;
        cursor_down.has_duration = true;
        if (cursor_up.target) cursor_up.target = nullptr;
    } else if (cursor_down.target) {
        cursor_down.target = nullptr;
        cursor_down.has_distance = false;
        cursor_down.has_duration = false;
    }
}

void Controller::cull_registry() {
    for (auto &kv : button_registry) {
        auto &reg = kv.second;
        for (int i = (int)reg.size() - 1; i >= 0; i--)
            if (reg[i].node->REMOVED) reg.erase(reg.begin() + i);
    }
}

void Controller::add_to_registry(Node *node, const std::string &registry) {
    RegistryEntry e;
    e.node = node;
    e.menu = (bool)G.OVERLAY_MENU || G.paused();
    auto &reg = button_registry[registry];
    reg.insert(reg.begin(), e);
}

void Controller::process_registry() {
    for (auto &kv : button_registry) {
        auto &reg = kv.second;
        for (size_t i = 0; i < reg.size(); i++) {
            if (reg[i].click) {
                Node *n = reg[i].node.get();
                if (reg[i].menu == (bool)G.OVERLAY_MENU && n->T->x > -2 && n->T->x < G.ROOM->T->w + 2 && n->T->y > -2 &&
                    n->T->y < G.ROOM->T->h + 2) {
                    NodeRef keep = reg[i].node;
                    keep->click();
                }
                if (i < reg.size()) reg[i].click = false;
            }
        }
    }
}

void Controller::mod_cursor_context_layer(int delta) {
    if (delta == 1) {
        ContextEntry e;
        e.node = focused.target;
        e.cursor_pos = {G.CURSOR->T->x, G.CURSOR->T->y};
        e.interrupt = interrupt.focus;
        e.valid = true;
        cursor_context.stack[cursor_context.layer] = e;
        cursor_context.layer++;
    } else if (delta == -1) {
        cursor_context.stack.erase(cursor_context.layer);
        cursor_context.layer--;
    } else if (delta == -1000) {
        cursor_context.layer = 1;
        auto it = cursor_context.stack.find(1);
        ContextEntry base = it != cursor_context.stack.end() ? it->second : ContextEntry{};
        cursor_context.stack.clear();
        if (base.valid) cursor_context.stack[1] = base;
    } else if (delta == -2000) {
        cursor_context.layer = 1;
        cursor_context.stack.clear();
    }
    navigate_focus();
}

void Controller::snap_to(Node *node, const XY *T) {
    snap_cursor_to.active = true;
    snap_cursor_to.node = node;
    snap_cursor_to.T = T ? *T : XY{};
    snap_cursor_to.is_node = node != nullptr;
}

namespace {
CardArea *named_area(const std::string &n) {
    if (n == "hand") return G.hand.get();
    if (n == "jokers") return G.jokers.get();
    if (n == "consumeables") return G.consumeables.get();
    if (n == "shop_jokers") return G.shop_jokers.get();
    if (n == "shop_vouchers") return G.shop_vouchers.get();
    if (n == "shop_booster") return G.shop_booster.get();
    if (n == "pack_cards") return G.pack_cards.get();
    if (n == "play") return G.play.get();
    if (n == "deck") return G.deck.get();
    return nullptr;
}
}  // namespace

bool Controller::save_cardarea_focus(const std::string &name) {
    if (CardArea *a = named_area(name)) {
        Card *c = focused.target && focused.target->is_card() ? static_cast<Card *>(focused.target.get()) : nullptr;
        if (c && c->area && c->area == a) {
            cardarea_context[name] = c->rank;
            return true;
        }
        cardarea_context.erase(name);
    }
    return false;
}

void Controller::recall_cardarea_focus(const std::string &name) {
    CardArea *a = named_area(name);
    Card *ft = focused.target && focused.target->is_card() ? static_cast<Card *>(focused.target.get()) : nullptr;
    if (a && (!focused.target || interrupt.focus || (!interrupt.focus && ft && ft->area && ft->area == a))) {
        auto it = cardarea_context.find(name);
        if (it != cardarea_context.end()) {
            for (int i = it->second; i >= 1; i--)
                if (i <= (int)a->cards.size()) {
                    snap_to(a->cards[i - 1].get());
                    interrupt.focus = false;
                    break;
                }
        } else if (!a->cards.empty()) {
            snap_to(a->cards[0].get());
            interrupt.focus = false;
        }
    }
    cardarea_context.erase(name);
}

void Controller::update_cursor(const XY *hard_set_T) {
    if (hard_set_T) {
        G.CURSOR->T->x = hard_set_T->x;
        G.CURSOR->T->y = hard_set_T->y;
        cursor_x = hard_set_T->x;
        cursor_y = hard_set_T->y;
        cursor_position.x = G.CURSOR->T->x * px_per_unit();
        cursor_position.y = G.CURSOR->T->y * px_per_unit();
        G.CURSOR->VT.x = G.CURSOR->T->x;
        G.CURSOR->VT.y = G.CURSOR->T->y;
        return;
    }
    if (focused.target) {
        cursor_position = focused.target->put_focused_cursor();
        G.CURSOR->T->x = cursor_position.x / px_per_unit();
        G.CURSOR->T->y = cursor_position.y / px_per_unit();
        cursor_x = cursor_position.x / px_per_unit_d();
        cursor_y = cursor_position.y / px_per_unit_d();
        G.CURSOR->VT.x = G.CURSOR->T->x;
        G.CURSOR->VT.y = G.CURSOR->T->y;
    }
}

void Controller::handle_axis_buttons() {
    for (AxisButton *v : {&l_stick, &r_stick, &l_trig, &r_trig}) {
        if (!v->previous.empty() && (v->current.empty() || v->previous != v->current)) button_release(v->previous);
        if (!v->current.empty() && v->previous != v->current) button_press(v->current);
    }
}

std::string Controller::update_axis(double dt) {
    std::string interp;
    for (AxisButton *v : {&l_stick, &r_stick, &l_trig, &r_trig}) {
        v->previous = v->current;
        v->current = "";
    }
    if (HID.controller) {
        float lx = axes.leftx, ly = axes.lefty;
        if (dragging.target && std::fabs(lx) + std::fabs(ly) > 0.1f) {
            interp = "axis_cursor";
            if (std::fabs(lx) < 0.1f) lx = 0;
            if (std::fabs(ly) < 0.1f) ly = 0;
            lx = lx + (lx > 0 ? -0.1f : 0) + (lx < 0 ? 0.1f : 0);
            ly = ly + (ly > 0 ? -0.1f : 0) + (ly < 0 ? 0.1f : 0);
            G.CURSOR->T->x += (float)(dt * lx * axis_cursor_speed);
            G.CURSOR->T->y += (float)(dt * ly * axis_cursor_speed);
            cursor_x += dt * lx * axis_cursor_speed;
            cursor_y += dt * ly * axis_cursor_speed;
            G.CURSOR->VT.x = G.CURSOR->T->x;
            G.CURSOR->VT.y = G.CURSOR->T->y;
            cursor_position.x = G.CURSOR->T->x * px_per_unit();
            cursor_position.y = G.CURSOR->T->y * px_per_unit();
        } else {
            l_stick.current = l_stick.previous;
            if (std::fabs(lx) + std::fabs(ly) > 0.5f) {
                interp = "button";
                l_stick.current = std::fabs(lx) > std::fabs(ly) ? (lx > 0 ? "dpright" : "dpleft") : (ly > 0 ? "dpdown" : "dpup");
            } else if (std::fabs(lx) + std::fabs(ly) < 0.3f) {
                l_stick.current = "";
            }
        }

        float rx = axes.rightx, ry = axes.righty;
        const float DEADZONE = 0.2f;
        float mag = std::sqrt(rx * rx + ry * ry);
        if (mag > DEADZONE) {
            interp = "axis_cursor";
            if (std::fabs(rx) < DEADZONE) rx = 0;
            if (std::fabs(ry) < DEADZONE) ry = 0;
            rx = rx + (rx > 0 ? -DEADZONE : 0) + (rx < 0 ? DEADZONE : 0);
            ry = ry + (ry > 0 ? -DEADZONE : 0) + (ry < 0 ? DEADZONE : 0);
            G.CURSOR->T->x += (float)(dt * rx * axis_cursor_speed);
            G.CURSOR->T->y += (float)(dt * ry * axis_cursor_speed);
            cursor_x += dt * rx * axis_cursor_speed;
            cursor_y += dt * ry * axis_cursor_speed;
            G.CURSOR->VT.x = G.CURSOR->T->x;
            G.CURSOR->VT.y = G.CURSOR->T->y;
            cursor_position.x = G.CURSOR->T->x * px_per_unit();
            cursor_position.y = G.CURSOR->T->y * px_per_unit();
        }

        float lt = axes.triggerleft, rt = axes.triggerright;
        l_trig.current = l_trig.previous;
        r_trig.current = r_trig.previous;
        if (lt > 0.5f) l_trig.current = "triggerleft";
        else if (lt < 0.3f) l_trig.current = "";
        if (rt > 0.5f) r_trig.current = "triggerright";
        else if (rt < 0.3f) r_trig.current = "";
        if ((!r_trig.current.empty() || !l_trig.current.empty()) && interp.empty()) interp = "button";

        handle_axis_buttons();
    }
    if (!interp.empty()) interrupt.focus = false;
    return interp;
}

void Controller::button_press_update(const std::string &button, double dt) {
    if (locks["frame"]) return;
    held_button_times[button] = 0;
    interrupt.focus = false;

    if (!capture_focused_input(button, "press", dt)) {
        if (button == "dpup") navigate_focus('U');
        if (button == "dpdown") navigate_focus('D');
        if (button == "dpleft") navigate_focus('L');
        if (button == "dpright") navigate_focus('R');
    }

    if ((locked && !G.paused()) || locks["frame"] || frame_buttonpress) return;
    frame_buttonpress = true;

    auto reg = button_registry.find(button);
    if (reg != button_registry.end() && !reg->second.empty() && !reg->second[0].node->under_overlay) {
        reg->second[0].click = true;
    } else {
        if (button == "start" && G.STATE == SPLASH) {
            G.delete_run();
            G.main_menu();
        }
        if (button == "a") {
            Value fa = focus_args(focused.target.get());
            if (focused.target && fa.is_table() && fa["type"].equals("slider") && !HID.mouse && !HID.axis_cursor) {
            } else {
                L_cursor_press();
            }
        }
        if (button == "b") {
            Card *c = focused.target && focused.target->is_card() ? static_cast<Card *>(focused.target.get()) : nullptr;
            if (G.hand && c && c->area == G.hand.get()) queue_R_cursor_press();
            else interrupt.focus = true;
        }
    }
}

void Controller::button_hold_update(const std::string &button, double dt) {
    if ((locked && !G.paused()) || locks["frame"] || frame_buttonpress) return;
    frame_buttonpress = true;
    auto it = held_button_times.find(button);
    if (it != held_button_times.end()) {
        it->second += dt;
        capture_focused_input(button, "hold", dt);
    }
    if ((button == "dpleft" || button == "dpright" || button == "dpup" || button == "dpdown") && !no_holdcap) {
        it = held_button_times.find(button);
        if (it != held_button_times.end() && it->second > repress_timer) {
            repress_timer = 0.1;
            it->second = 0;
            button_press_update(button, dt);
        }
    }
}

void Controller::button_release_update(const std::string &button, double) {
    auto it = held_button_times.find(button);
    if (it == held_button_times.end()) return;
    repress_timer = 0.3;
    held_button_times.erase(it);
    if (button == "a") L_cursor_release();
}

void Controller::key_press_update(const std::string &key_in, double) {
    if (locks["frame"]) return;
    std::string key = key_in;
    if (key.compare(0, 2, "kp") == 0) key = key.substr(2);
    if (key == "enter") key = "return";

    if (text_input_hook) {
        if (key == "escape") text_input_hook = nullptr;
        else if (key == "capslock") capslock = !capslock;
        else text_input_key(key, held_keys["lshift"] || held_keys["rshift"]);
        return;
    }

    if (key == "escape") {
        if (G.STATE == SPLASH) {
            G.delete_run();
            G.main_menu();
        } else if (!G.OVERLAY_MENU) {
            call_named("options");
        } else if (!G.OVERLAY_MENU->config->get(K("no_esc")).truthy()) {
            call_named("exit_overlay_menu");
        }
    }

    if ((locked && !G.paused()) || locks["frame"] || frame_buttonpress) return;
    frame_buttonpress = true;
    held_key_times[key] = 0;
    // (debug keys are compiled out: _RELEASE_MODE = true)
}

void Controller::key_hold_update(const std::string &key, double dt) {
    if ((locked && !G.paused()) || locks["frame"] || frame_buttonpress) return;
    auto it = held_key_times.find(key);
    if (it == held_key_times.end()) return;
    if (key == "r" && !G.paused()) {
        if (it->second > 0.7) {
            // quick restart: a new run with the same deck and stake (and seed / challenge)
            if (!G.GAME->get(K("won")).truthy() && !G.GAME->get(K("seeded")).truthy() && !G.GAME->get(K("challenge")).truthy())
                G.PROFILES->get(G.SETTINGS->get(K("profile"))).get(K("high_scores")).get(K("current_streak")).table()->set("amt", 0);
            G.save_settings();
            held_key_times.erase(it);
            G.SETTINGS->set("current_setup", "New Run");
            G.GAME->set("viewed_back", Value());
            Table *g = G.G_TABLE.get();
            g->set("run_setup_seed", G.GAME->get(K("seeded")));
            g->set("challenge_tab", G.GAME->get(K("challenge")).truthy() ? G.GAME->get(K("challenge_tab")) : Value());
            g->set("forced_seed", Value());
            g->set("setup_seed", Value());
            if (G.GAME->get(K("seeded")).truthy()) g->set("forced_seed", G.GAME->get(K("pseudorandom")).get(K("seed")));
            g->set("forced_stake", G.GAME->get(K("stake")));
            if (G.STAGE == RUN) call_named("start_setup_run");
            g->set("forced_stake", Value());
            g->set("challenge_tab", Value());
            g->set("forced_seed", Value());
            return;
        } else {
            it->second += dt;
        }
    }
}

void Controller::key_release_update(const std::string &, double) {
    if ((locked && !G.paused()) || locks["frame"] || frame_buttonpress) return;
    frame_buttonpress = true;
}

void Controller::key_press(const std::string &key) {
    pressed_keys[key] = true;
    held_keys[key] = true;
}

void Controller::key_release(const std::string &key) {
    held_keys.erase(key);
    released_keys[key] = true;
}

void Controller::button_press(const std::string &button) {
    pressed_buttons[button] = true;
    held_buttons[button] = true;
}

void Controller::button_release(const std::string &button) {
    held_buttons.erase(button);
    released_buttons[button] = true;
}

void Controller::get_cursor_collision(const XY &cursor) {
    collision_list.clear();
    nodes_at_cursor.clear();
    if (COYOTE_FOCUS) return;
    if (dragging.target) {
        dragging.target->states.collide.is = true;
        nodes_at_cursor.push_back(dragging.target);
        collision_list.push_back(dragging.target);
    }
    if (G.DRAW_HASH.empty() || cursor.x - G.ROOM->T->x < -G.DRAW_HASH_BUFF ||
        cursor.x - G.ROOM->T->x > G.TILE_W + G.DRAW_HASH_BUFF || cursor.y - G.ROOM->T->y < -G.DRAW_HASH_BUFF ||
        cursor.y - G.ROOM->T->y > G.TILE_H + G.DRAW_HASH_BUFF)
        return;
    for (int i = (int)G.DRAW_HASH.size() - 1; i >= 0; i--) {
        Node *v = G.DRAW_HASH[i].get();
        if (v->collides_with_point(cursor) && !v->REMOVED) {
            nodes_at_cursor.push_back(v);
            if (v->states.collide.can) {
                v->states.collide.is = true;
                collision_list.push_back(v);
            }
        }
    }
}

void Controller::set_cursor_hover() {
    cursor_hover.T = {G.CURSOR->T->x, G.CURSOR->T->y};
    hover_x = cursor_x;
    hover_y = cursor_y;
    cursor_hover.time = G.TIMERS.UPTIME;
    NodeRef prev = cursor_hover.target;
    cursor_hover.target = nullptr;

    if (interrupt.focus || (locked && (!G.paused() || G.screenwipe)) || locks["frame"] || COYOTE_FOCUS) {
        cursor_hover.target = G.ROOM;
        return;
    }
    if (HID.controller && focused.target && focused.target->states.hover.can) {
        if ((HID.dpad || HID.axis_cursor) && focused.target->states.collide.is) {
            cursor_hover.target = focused.target;
        } else {
            for (auto &v : collision_list)
                if (v->states.hover.can) {
                    cursor_hover.target = v;
                    break;
                }
        }
    } else {
        for (auto &v : collision_list)
            if (v->states.hover.can && !v->states.drag.is) {
                cursor_hover.target = v;
                break;
            }
    }
    if (!cursor_hover.target || dragging.target) cursor_hover.target = G.ROOM;
    if (cursor_hover.target != prev) cursor_hover.handled = false;
}

void Controller::queue_L_cursor_press(float x, float y) {
    if (locks["frame"]) return;
    if (G.STATE == SPLASH) key_press("escape");
    L_cursor_queue = {x, y};
    has_L_cursor_queue = true;
}

void Controller::queue_R_cursor_press() {
    if (locks["frame"]) return;
    if (!G.paused() && G.hand && !G.hand->highlighted.empty()) {
        Value su = G.GAME->get(K("STOP_USE"));
        if ((G.play && !G.play->cards.empty()) || locked || locks["frame"] || (su.truthy() && su.num() > 0)) return;
        G.hand->unhighlight_all();
    }
}

void Controller::L_cursor_press(const float *px, const float *py) {
    float x = px ? *px : cursor_position.x;
    float y = py ? *py : cursor_position.y;
    if ((locked && (!G.paused() || G.screenwipe)) || locks["frame"]) return;

    cursor_down.T = {x / px_per_unit(), y / px_per_unit()};
    cursor_down.time = G.TIMERS.UPTIME;
    cursor_down.handled = false;
    cursor_down.target = nullptr;
    is_cursor_down = true;

    Node *press_node = cursor_hover.target ? cursor_hover.target.get()
                       : hovering.target   ? hovering.target.get()
                                           : focused.target.get();
    if (press_node) cursor_down.target = press_node->states.click.can ? press_node : press_node->can_drag();
    if (!cursor_down.target) cursor_down.target = G.ROOM;
}

void Controller::L_cursor_release(const float *px, const float *py) {
    float x = px ? *px : cursor_position.x;
    float y = py ? *py : cursor_position.y;
    if ((locked && (!G.paused() || G.screenwipe)) || locks["frame"]) return;

    cursor_up.T = {x / px_per_unit(), y / px_per_unit()};
    cursor_up.time = G.TIMERS.UPTIME;
    cursor_up.handled = false;
    is_cursor_down = false;
    cursor_up.target = hovering.target ? hovering.target : focused.target ? focused.target : dragging.target;
    if (!cursor_up.target) cursor_up.target = G.ROOM;
}

bool Controller::is_node_focusable(Node *node) {
    bool ret = false;
    if (node->T->y > G.ROOM->T->h + 3) return false;
    UIElement *u = as_uie(node);
    if (!node->REMOVED && !node->under_overlay &&
        ((node->states.hover.can && !dragging.target) || dragging.target.get() == node) &&
        node->created_on_pause == G.paused() && node->states.visible && (!u || !u->box || u->box->states.visible)) {
        if (screen_keyboard) {
            if (u && u->box == screen_keyboard.get() && node->config->get(K("button")).truthy()) ret = true;
        } else {
            if (node->is_card()) {
                Card *c = static_cast<Card *>(node);
                // (the deck itself is a CardArea, never a Card: `node == G.deck` cannot hold)
                if ((c->facing == "front" || (G.hand && c->area == G.hand.get()) || (G.jokers && c->area == G.jokers.get())) && c->states.hover.can &&
                    !c->jimbo)
                    ret = true;
            }
            if (node->config->get(K("force_focus")).truthy()) ret = true;
            if (node->config->get(K("button")).truthy() || (u && u->tutorial_disabled.truthy())) ret = true;
            Value fa = focus_args(node);
            if (fa.truthy()) ret = !(fa["type"].equals("none") || fa["funnel_from"].truthy());
        }
    }
    return ret;
}

void Controller::update_focus(char dir) {
    focused.prev_target = focused.target;

    if (!HID.controller || interrupt.focus || (locked && (!G.paused() || G.screenwipe))) {
        if (focused.target) focused.target->states.focus.is = false;
        focused.target = nullptr;
        return;
    }

    struct FocusItem {
        NodeRef node;
        float dist;
    };
    std::vector<FocusItem> focus_list;
    std::vector<NodeRef> focusables;

    if (focused.target) {
        focused.target->states.focus.is = false;
        if (!is_node_focusable(focused.target.get()) ||
            !focused.target->collides_with_point({G.CURSOR->T->x, G.CURSOR->T->y}) || HID.axis_cursor)
            focused.target = nullptr;
    }

    if (!dir && focused.target) {
        focused.target->states.focus.can = true;
        focusables.push_back(focused.target);
    } else if (!dir) {
        for (auto &v : nodes_at_cursor) {
            v->states.focus.can = false;
            v->states.focus.is = false;
            if (focusables.empty() && is_node_focusable(v.get())) {
                v->states.focus.can = true;
                focusables.push_back(v);
            }
        }
    } else {
        G.MOVEABLES.each([&](Moveable *v) {
            v->states.focus.can = false;
            v->states.focus.is = false;
            if (is_node_focusable(v)) {
                v->states.focus.can = true;
                focusables.push_back(NodeRef(v));
            }
        });
    }

    if (!focusables.empty()) {
        if (dir) {
            Card *fc = focused.target && focused.target->is_card() ? static_cast<Card *>(focused.target.get()) : nullptr;
            if ((dir == 'L' || dir == 'R') && fc && G.hand && fc->area == G.hand.get()) {
                int nu_rank = fc->rank + (dir == 'L' ? -1 : 1);
                if (nu_rank > (int)G.hand->cards.size()) nu_rank = 1;
                if (nu_rank == 0) nu_rank = (int)G.hand->cards.size();
                if (nu_rank != fc->rank) focus_list.push_back({NodeRef(G.hand->cards[nu_rank - 1].get()), 0});
            } else {
            XY cp{G.CURSOR->T->x - G.ROOM->T->x, G.CURSOR->T->y - G.ROOM->T->y};
            if (focused.target) {
                Node *ft = focused.target.get();
                Value fa = focus_args(ft);
                if (fa.is_table() && fa["funnel_to"].truthy()) ft = fa["funnel_to"].as<Node>();
                cp = {ft->T->x + 0.5f * ft->T->w, ft->T->y + 0.5f * ft->T->h};
            } else if (hovering.target && hovering.target->states.focus.can) {
                cp = hovering.target->put_focused_cursor();
                cp = {cp.x / px_per_unit(), cp.y / px_per_unit()};
                if (hovering.target->zoomed) cp = G.overlay_zoom.unzoom(cp);
                cp.x -= G.ROOM->T->x;
                cp.y -= G.ROOM->T->y;
            }
            for (auto &vr : focusables) {
                Node *v = vr.get();
                if (v == hovering.target.get() || v == focused.target.get()) continue;
                bool eligible = false;
                Value fa = focus_args(v);
                if (fa.is_table() && fa["funnel_to"].truthy()) {
                    v = fa["funnel_to"].as<Node>();
                    fa = focus_args(v);
                }
                XY fv{v->T->x + 0.5f * v->T->w - cp.x, v->T->y + 0.5f * v->T->h - cp.y};
                Value nav = fa.is_table() ? fa["nav"] : Value();
                if (nav.truthy()) {
                    if (nav.equals("wide")) {
                        if (fv.y > 0.1f && dir == 'D') eligible = true;
                        else if (fv.y < -0.1f && dir == 'U') eligible = true;
                        else if (std::fabs(fv.y) < v->T->h / 2) eligible = true;
                    } else if (nav.equals("tall")) {
                        if (fv.x > 0.1f && dir == 'R') eligible = true;
                        else if (fv.x < -0.1f && dir == 'L') eligible = true;
                        else if (std::fabs(fv.x) < v->T->w / 2) eligible = true;
                    }
                } else if (std::fabs(fv.x) > std::fabs(fv.y)) {
                    if (fv.x > 0 && dir == 'R') eligible = true;
                    else if (fv.x < 0 && dir == 'L') eligible = true;
                } else {
                    if (fv.y > 0 && dir == 'D') eligible = true;
                    else if (fv.y < 0 && dir == 'U') eligible = true;
                }
                if (eligible) focus_list.push_back({NodeRef(v), std::fabs(fv.x) + std::fabs(fv.y)});
            }
            if (focus_list.empty()) {
                if (focused.target) focused.target->states.focus.is = true;
                return;
            }
            std::stable_sort(focus_list.begin(), focus_list.end(), [](const FocusItem &a, const FocusItem &b) { return a.dist < b.dist; });
            }
        } else {
            focus_list.push_back({focused.target ? focused.target : focusables[0], 0});
        }
    }

    if (!focus_list.empty()) {
        Node *first = focus_list[0].node.get();
        Value fa = focus_args(first);
        if (fa.is_table() && fa["funnel_from"].truthy()) focused.target = fa["funnel_from"].as<Node>();
        else focused.target = first;
        if (focused.target != focused.prev_target) G.VIBRATION += 0.7;
    } else {
        focused.target = nullptr;
    }
    if (focused.target) focused.target->states.focus.is = true;
}

bool Controller::capture_focused_input(const std::string &button, const std::string &input_type, double dt) {
    bool ret = false;
    Node *focused_n = focused.target.get();
    bool extern_button = false;
    no_holdcap = false;

    // 'coyote time': a card in hand counts as selected when the dpad moves on while A is down
    Card *fc = focused_n && focused_n->is_card() ? static_cast<Card *>(focused_n) : nullptr;
    auto a_held = held_button_times.find("a");
    if (input_type == "press" && (button == "dpleft" || button == "dpright") && fc && dragging.target && a_held != held_button_times.end() &&
        a_held->second < 0.12 && fc->area && fc->area->can_highlight(fc)) {
        L_cursor_release();
        navigate_focus(button == "dpleft" ? 'L' : 'R');
        held_button_times.erase("a");
        COYOTE_FOCUS = true;
        ret = true;
    } else if (input_type == "press" && fc && fc->area && focused_n == dragging.target.get()) {
        // dpad reordering of the card being dragged
        CardArea *area = fc->area;
        fc->states.drag.is = false;
        auto by_rank = [](const CardRef &a, const CardRef &b) { return a->rank < b->rank; };
        if (button == "dpleft" && fc->rank > 1) {
            fc->rank--;
            area->cards[fc->rank - 1]->rank = fc->rank + 1;
            lua_sort(area->cards, by_rank);
            area->align_cards();
            update_cursor();
        } else if (button == "dpright" && fc->rank < (int)area->cards.size()) {
            fc->rank++;
            area->cards[fc->rank - 1]->rank = fc->rank - 1;
            lua_sort(area->cards, by_rank);
            area->align_cards();
            update_cursor();
        }
        fc->states.drag.is = true;
        ret = true;
    }

    UIBox *overlay = dynamic_cast<UIBox *>(G.OVERLAY_MENU.get());
    bool shoulder = button == "leftshoulder" || button == "rightshoulder";
    if (overlay && !screen_keyboard && input_type == "press" && shoulder) {
        if (UIElement *t = overlay->get_UIE_by_ID("tab_shoulders")) {
            focused_n = t;
            extern_button = true;
        }
        if (UIElement *c = overlay->get_UIE_by_ID("cycle_shoulders")) {
            focused_n = c->children.count() > 0 ? c->children.at(0) : nullptr;
            extern_button = true;
        }
    }
    Value fa = focus_args(focused_n);
    if (focused_n && fa.is_table()) {
        bool left = (extern_button && button == "leftshoulder") || (!extern_button && button == "dpleft");
        bool right = (extern_button && button == "rightshoulder") || (!extern_button && button == "dpright");
        if (fa["type"].equals("cycle") && input_type == "press") {
            if (left && focused_n->children.count() > 0) {
                focused_n->children.at(0)->click();
                ret = true;
            }
            if (right && focused_n->children.count() > 2) {
                focused_n->children.at(2)->click();
                ret = true;
            }
        }
        UIElement *fu = as_uie(focused_n);
        if (fa["type"].equals("tab") && input_type == "press" && fu && fu->children.count() > 0 &&
            fu->children.at(0)->children.count() > 0) {
            Value group = fu->children.at(0)->children.at(0)->config->get(K("group"));
            std::vector<UIElement *> proto, choices;
            fu->box->get_group(nullptr, group, proto);
            for (UIElement *v : proto)
                if (v->config->get(K("choice")).truthy() && v->config->get(K("button")).truthy()) choices.push_back(v);
            int n = (int)choices.size();
            for (int k = 1; k <= n; k++) {
                if (!choices[k - 1]->config->get(K("chosen")).truthy()) continue;
                bool no_loop = fa["no_loop"].truthy();
                if (left) {
                    int next_i = k != 1 ? k - 1 : n;
                    if (no_loop && next_i > k) ret = false;
                    else {
                        choices[next_i - 1]->click();
                        snap_to(choices[next_i - 1]);
                        update_cursor();
                        ret = true;
                    }
                } else if (right) {
                    int next_i = k != n ? k + 1 : 1;
                    if (no_loop && next_i < k) ret = false;
                    else {
                        choices[next_i - 1]->click();
                        snap_to(choices[next_i - 1]);
                        update_cursor();
                        ret = true;
                    }
                }
                break;
            }
        }
        if (fa["type"].equals("slider")) {
            UIElement *bar = fu && fu->children.count() > 0 ? dynamic_cast<UIElement *>(fu->children.at(0)) : nullptr;
            if (button == "dpleft" || button == "dpright") {
                double sign = button == "dpleft" ? -1 : 1;
                no_holdcap = true;
                double held = held_button_times.count(button) ? held_button_times[button] : 0;
                if (input_type == "hold" && held > 0.2) slider_descreet(bar, sign * dt * held * 0.6);
                if (input_type == "press") slider_descreet(bar, sign * 0.01);
                ret = true;
            }
        }
    }
    if (ret) {
        G.MOBILE_VIBRATION_QUEUE = std::max(G.MOBILE_VIBRATION_QUEUE, 1.0);
        G.VIBRATION += 1;
    }
    (void)dt;
    return ret;
}

void Controller::navigate_focus(char dir) {
    update_focus(dir);
    update_cursor();
}

void Controller::forget(Node *n) {
    // Node:remove(): drop every reference the controller holds to n
    for (TargetState *t : {&clicked, &focused, &dragging, &hovering, &released_on})
        if (t->target.get() == n) t->target = nullptr;
    for (CursorState *c : {&cursor_down, &cursor_up, &cursor_hover})
        if (c->target.get() == n) c->target = nullptr;
}

}  // namespace vl
