// The end of a run: Game:update_game_over, win_game (state_events.lua) and their screens
// (create_UIBox_game_over / create_UIBox_win / create_UIBox_round_scores_row, UI_definitions.lua).
#include <algorithm>
#include <cmath>

#include "engine/event.h"
#include "engine/random.h"
#include "engine/sprite.h"
#include "engine/text.h"
#include "engine/ui.h"
#include "game/blind.h"
#include "game/card.h"
#include "game/button_callbacks.h"
#include "game/card_character.h"
#include "game/functions.h"
#include "game/g.h"
#include "game/save_manager.h"
#include "game/sound.h"
#include "game/state_events.h"
#include "game/ui_definitions.h"
#include "platform/platform.h"

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
DynaText *dyna(std::initializer_list<std::pair<const char *, Value>> kv) { return new DynaText(vl::T(kv)); }
Value str_list(const Value &s) { return Value(A({s})); }

// the Jimbo spot left of the screen's contents (t.nodes[1] = {R, {C jimbo_spot, C t.nodes[1]}})
void add_jimbo_spot(TableRef t) {
    TableRef nodes(t->get(K("nodes")).table());
    Value first = nodes->get(1);
    nodes->set(1, node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.1}}),
                       A({node(UIT_C, vl::T({{"align", "cm"}, {"padding", 2}}),
                               A({node(UIT_R, vl::T({{"align", "cm"}}),
                                       A({node(UIT_O, vl::T({{"padding", 0}, {"id", "jimbo_spot"}, {"object", obj(new Moveable(0, 0, G.CARD_W * 1.1f, G.CARD_H * 1.1f))}}))}))})),
                          node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.1}}), A({first}))})));
}

// after 2.5 s, Jimbo takes the overlay's jimbo_spot and says a quip ('lq_' on a loss, 'wq_' on a win)
void jimbo_quip(const char *prefix, int count) {
    const std::string pre(prefix);
    EventConfig e = ev_after(2.5, [pre, count] {
        auto *o = dynamic_cast<UIBox *>(G.OVERLAY_MENU.get());
        UIElement *spot = o ? o->get_UIE_by_ID("jimbo_spot") : nullptr;
        if (spot) {
            auto *jimbo = new Card_Character(vl::T({{"x", 0}, {"y", 5}}));
            if (Node *old = spot->config->get(K("object")).as<Node>()) old->remove();
            spot->config->set("object", obj(jimbo));
            jimbo->set_var("ui_object_updated", true);
            jimbo->add_speech_bubble(pre + std::to_string((int)lua_math.random(1, count)), nullptr, vl::T({{"quip", true}}));
            jimbo->say_stuff(5);
        }
        return true;
    });
    e.blocking = false;
    G.E_MANAGER->add_event(e);
}
}  // namespace

TableRef create_UIBox_round_scores_row(const std::string &score, const Value &text_colour) {
    const Value rs = gv("round_scores").get(Value(score));
    Value label = rs.truthy() ? localize("ph_score_" + score) : Value("");
    bool check_high_score = false;
    TableRef score_tab = new_table();
    const bool big = score == "hand" || score == "poker_hand";
    double label_w = big ? 3.5 : 2.9, score_w = big ? 3.5 : 1;
    const double h = 0.5;
    auto colour_or = [&](const char *d) { return text_colour.truthy() ? text_colour : col(d); };
    auto dt = [&](const Value &s, const char *dcol, double scale) {
        return node(UIT_O, vl::T({{"object", obj(dyna({{"string", str_list(s)}, {"colours", Value(A({colour_or(dcol)}))}, {"shadow", true}, {"float", true}, {"scale", scale}}))}}));
    };
    if (score == "furthest_ante") {
        label_w = 1.9;
        check_high_score = true;
        label = localize("k_ante");
        score_tab->push(dt(Value(number_format(fld(gv("round_resets"), "ante"))), "FILTER", 0.45));
    }
    if (score == "furthest_round") {
        label_w = 1.9;
        check_high_score = true;
        label = localize("k_round");
        score_tab->push(dt(Value(number_format(gv("round"))), "FILTER", 0.45));
    }
    if (score == "seed") {
        label_w = 1.9;
        score_w = 1.9;
        label = localize("k_seed");
        score_tab->push(dt(fld(gv("pseudorandom"), "seed"), "WHITE", 0.45));
    }
    if (score == "defeated_by") {
        label = localize("k_defeated_by");
        Blind *b = gv("blind").as<Blind>();
        Value bc = b && b->config->get(K("blind")).truthy() ? b->config->get(K("blind")) : G.P_BLINDS->get(K("bl_small"));
        SpritePos p;
        p.x = (float)fld(fld(bc, "pos"), "x").num();
        p.y = (float)fld(fld(bc, "pos"), "y").num();
        auto *anim = new AnimatedSprite(0, 0, 1.4f, 1.4f, &G.ANIMATION_ATLAS["blind_chips"], p);
        DrawStep a, s2;
        a.shader = gfx::ShaderId::Dissolve;
        a.has_shadow_height = true;
        a.shadow_height = 0.05f;
        s2.shader = gfx::ShaderId::Dissolve;
        anim->define_draw_steps({a, s2});
        score_tab->push(node(UIT_R, vl::T({{"align", "cm"}, {"minh", 0.6}}),
                             A({node(UIT_O, vl::T({{"object", obj(dyna({{"string", localize(vl::T({{"type", "name_text"}, {"key", fld(bc, "key")}, {"set", "Blind"}}))},
                                                                        {"colours", Value(A({col("WHITE")}))},
                                                                        {"shadow", true},
                                                                        {"float", true},
                                                                        {"maxw", 2.2},
                                                                        {"scale", 0.45}}))}}))})));
        score_tab->push(node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.1}}), A({node(UIT_O, vl::T({{"object", obj(anim)}}))})));
    }
    const double label_scale = 0.5;
    if (score == "poker_hand") {
        Value handname = localize("k_none");
        double amount = 0;
        if (Table *hu = gv("hand_usage").table())
            hu->each([&](const Value &, const Value &v) {
                if (fld(v, "count").num() > amount) {
                    handname = fld(v, "order");
                    amount = fld(v, "count").num();
                }
            });
        score_tab = A({node(UIT_O, vl::T({{"object", obj(dyna({{"string", str_list(amount < 1 ? handname : localize(handname.to_string(), "poker_hands"))},
                                                               {"colours", Value(A({colour_or("WHITE")}))},
                                                               {"shadow", true},
                                                               {"float", true},
                                                               {"scale", 0.45},
                                                               {"maxw", 2.5}}))}})),
                       node(UIT_T, vl::T({{"text", " (" + number_format(Value(amount)) + ")"}, {"scale", 0.35}, {"colour", col("JOKER_GREY")}}))});
    } else if (score == "hand") {
        check_high_score = true;
        SpritePos p;
        auto *chip = new Sprite(0, 0, 0.3f, 0.3f, &G.ASSET_ATLAS[G.SETTINGS->get(K("colourblind_option")).truthy() ? "ui_2" : "ui_1"], p);
        chip->states.drag.can = false;
        const Value amt = fld(rs, "amt");
        score_tab = A({node(UIT_C, vl::T({{"align", "cm"}}), A({node(UIT_O, vl::T({{"w", 0.3}, {"h", 0.3}, {"object", obj(chip)}}))})),
                       node(UIT_C, vl::T({{"align", "cm"}}),
                            A({node(UIT_O, vl::T({{"object", obj(dyna({{"string", str_list(Value(number_format(amt)))},
                                                                       {"colours", Value(A({colour_or("RED")}))},
                                                                       {"shadow", true},
                                                                       {"float", true},
                                                                       {"scale", std::min(0.6, score_number_scale(1.2, amt))}}))}}))}))});
    } else if (rs.truthy() && score_tab->len() == 0) {
        const Value amt = fld(rs, "amt");
        score_tab->push(node(UIT_O, vl::T({{"object", obj(dyna({{"string", str_list(Value(number_format(amt)))},
                                                                {"colours", Value(A({colour_or("FILTER")}))},
                                                                {"shadow", true},
                                                                {"float", true},
                                                                {"scale", score_number_scale(0.6, amt)}}))}})));
    }
    const int lt = score == "defeated_by" ? UIT_R : UIT_C;
    TableRef cfg = vl::T({{"align", "cm"}, {"padding", 0.05}, {"r", 0.1}, {"colour", Value(darken(col("JOKER_GREY"), 0.1f))}, {"emboss", 0.05}, {"id", Value(score)}});
    if (check_high_score) cfg->set("func", "high_score_alert");
    const bool red_seed = score == "seed" && gv("seeded").truthy();
    return TableRef(
        node(UIT_R, cfg,
             A({node(lt, vl::T({{"align", "cm"}, {"padding", 0.02}, {"minw", label_w}, {"maxw", label_w}}),
                     A({node(UIT_T, vl::T({{"text", label}, {"scale", label_scale}, {"colour", ui_col("TEXT_LIGHT")}, {"shadow", true}}))})),
                node(lt, vl::T({{"align", "cr"}}),
                     A({node(UIT_C, vl::T({{"align", "cm"}, {"minh", h}, {"r", 0.1}, {"minw", score == "defeated_by" ? label_w : score_w}, {"colour", red_seed ? col("RED") : col("BLACK")}, {"emboss", 0.05}}),
                             A({node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.05}, {"r", 0.1}, {"minw", score_w}}), score_tab)}))}))}))
            .table());
}

namespace {
Value row(const char *score, const char *colour = nullptr) { return Value(create_UIBox_round_scores_row(score, colour ? col(colour) : Value())); }
Value copy_seed_button(bool nav) {
    TableRef a = vl::T({{"button", "copy_seed"}, {"label", Value(A({localize("b_copy")}))}, {"colour", col("BLUE")}, {"scale", 0.3}, {"minw", 2.3}, {"minh", 0.4}});
    if (nav) a->set("focus_args", Value(vl::T({{"nav", "wide"}})));
    return Value(UIBox_button(a));
}
TableRef eased_alpha(const Value &c, double to) {
    TableRef t(copy_table(c).table());
    t->set(4, 0);
    ease_value(ValueRef::field(t, Value(4)), to, false, nullptr, 0.3);
    return t;
}
}  // namespace

TableRef create_UIBox_game_over() {
    const bool lost = fld(gv("round_resets"), "ante").num() <= gv("win_ante").num();
    TableRef eased_red = eased_alpha(lost ? col("RED") : col("BLUE"), 0.8);
    auto big_button = [](const char *id, const char *button, const char *label, bool snap) {
        TableRef cfg = vl::T({{"align", "cm"}, {"minw", 5}, {"padding", 0.1}, {"r", 0.1}, {"hover", true}, {"colour", col("RED")}, {"button", button}, {"shadow", true},
                              {"focus_args", Value(snap ? vl::T({{"nav", "wide"}, {"snap_to", true}}) : vl::T({{"nav", "wide"}}))}});
        if (id) cfg->set("id", id);
        return node(UIT_R, cfg,
                    A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}, {"no_fill", true}, {"maxw", 4.8}}),
                            A({node(UIT_T, vl::T({{"text", localize(label)}, {"scale", 0.5}, {"colour", ui_col("TEXT_LIGHT")}}))}))}));
    };
    TableRef contents = A(
        {node(UIT_R, vl::T({{"align", "cm"}}),
              A({node(UIT_O, vl::T({{"object", obj(dyna({{"string", str_list(localize("ph_game_over"))}, {"colours", Value(A({col("RED")}))}, {"shadow", true},
                                                         {"float", true}, {"scale", 1.5}, {"pop_in", 0.4}, {"maxw", 6.5}}))}}))})),
         node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.15}}),
              A({node(UIT_C, vl::T({{"align", "cm"}}),
                      A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.05}, {"colour", col("BLACK")}, {"emboss", 0.05}, {"r", 0.1}}),
                              A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.08}}), A({row("hand"), row("poker_hand")})),
                                 node(UIT_R, vl::T({{"align", "cm"}}),
                                      A({node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.08}}),
                                              A({row("cards_played", "BLUE"), row("cards_discarded", "RED"), row("cards_purchased", "MONEY"),
                                                 row("times_rerolled", "GREEN"), row("new_collection", "WHITE"), row("seed", "WHITE"), copy_seed_button(true)})),
                                         node(UIT_C, vl::T({{"align", "tr"}, {"padding", 0.08}}),
                                              A({row("furthest_ante", "FILTER"), row("furthest_round", "FILTER"), row("defeated_by")}))}))})),
                         node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.1}}),
                              A({big_button("from_game_over", "notify_then_setup_run", "b_start_new_run", true),
                                 big_button(nullptr, "go_to_menu", "b_main_menu", false)}))}))}))});
    TableRef t = create_UIBox_generic_options(vl::T({{"bg_colour", Value(eased_red)}, {"no_back", true}, {"padding", 0}, {"contents", Value(contents)}}));
    add_jimbo_spot(t);
    return t;
}

TableRef create_UIBox_win() {
    TableRef eased_green = eased_alpha(col("GREEN"), 0.5);
    auto button = [](std::initializer_list<std::pair<const char *, Value>> kv) { return Value(UIBox_button(vl::T(kv))); };
    TableRef contents = A(
        {node(UIT_R, vl::T({{"align", "cm"}}),
              A({node(UIT_O, vl::T({{"object", obj(dyna({{"string", str_list(localize("ph_you_win"))}, {"colours", Value(A({col("EDITION")}))}, {"shadow", true},
                                                         {"float", true}, {"spacing", 10}, {"rotate", true}, {"scale", 1.5}, {"pop_in", 0.4}, {"maxw", 6.5}}))}}))})),
         node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.15}}),
              A({node(UIT_C, vl::T({{"align", "cm"}}),
                      A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.08}}), A({row("hand"), row("poker_hand")})),
                         node(UIT_R, vl::T({{"align", "cm"}}),
                              A({node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.08}}),
                                      A({row("cards_played", "BLUE"), row("cards_discarded", "RED"), row("cards_purchased", "MONEY"), row("times_rerolled", "GREEN"),
                                         row("new_collection", "WHITE"), row("seed", "WHITE"), copy_seed_button(false)})),
                                 node(UIT_C, vl::T({{"align", "tr"}, {"padding", 0.08}}),
                                      A({row("furthest_ante", "FILTER"), row("furthest_round", "FILTER"),
                                         node(UIT_R, vl::T({{"align", "cm"}, {"minh", 0.4}, {"minw", 0.1}}), new_table()),
                                         button({{"id", "from_game_won"}, {"button", "notify_then_setup_run"}, {"label", Value(A({localize("b_start_new_run")}))},
                                                 {"minw", 2.5}, {"maxw", 2.5}, {"minh", 1}, {"focus_args", Value(vl::T({{"nav", "wide"}, {"snap_to", true}}))}}),
                                         node(UIT_R, vl::T({{"align", "cm"}, {"minh", 0.2}, {"minw", 0.1}}), new_table()),
                                         button({{"button", "go_to_menu"}, {"label", Value(A({localize("b_main_menu")}))}, {"minw", 2.5}, {"maxw", 2.5}, {"minh", 1},
                                                 {"focus_args", Value(vl::T({{"nav", "wide"}}))}})}))})),
                         node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.08}}),
                              A({button({{"button", "exit_overlay_menu"}, {"label", Value(A({localize("b_endless")}))}, {"minw", 6.5}, {"maxw", 5}, {"minh", 1.2},
                                         {"scale", 0.7}, {"shadow", true}, {"colour", col("BLUE")},
                                         {"focus_args", Value(vl::T({{"nav", "wide"}, {"button", "x"}, {"set_button_pip", true}}))}})}))}))}))});
    TableRef t = create_UIBox_generic_options(vl::T({{"padding", 0}, {"bg_colour", Value(eased_green)}, {"colour", col("BLACK")}, {"outline_colour", col("EDITION")},
                                                     {"no_back", true}, {"no_esc", true}, {"contents", Value(contents)}}));
    add_jimbo_spot(t);
    t->get(K("config")).table()->set("id", "you_win_UI");
    return t;
}

// misc_functions.lua remove_save()
void remove_save() {
    const std::string f = G.SETTINGS->get(K("profile")).to_string() + "/save.jkr";
    platform::remove_save(f.c_str());
    files_clear(f);
    G.G_TABLE->set("SAVED_GAME", Value());
    G.FILE_HANDLER->set("run", Value());
}

// Game:update_game_over
void game_over_update() {
    if (G.STATE_COMPLETE) return;
    remove_save();
    const bool lost = fld(gv("round_resets"), "ante").num() <= gv("win_ante").num();
    if (lost && !gv("seeded").truthy() && !gv("challenge").truthy()) {
        inc_career_stat("c_losses", 1);
        set_deck_loss();
        set_joker_loss();
    }
    play_sound("negative", 0.5f, 0.7f);
    play_sound("whoosh2", 0.9f, 0.7f);
    G.SETTINGS->set("paused", true);
    overlay_menu(create_UIBox_game_over(), vl::T({{"no_esc", true}}));
    G.ROOM_jiggle += 3;
    // Jimbo only says a quip when the run was lost
    if (lost) jimbo_quip("lq_", 10);
    G.STATE_COMPLETE = true;
}

void win_game() {
    if (!gv("seeded").truthy() && !gv("challenge").truthy()) {
        set_joker_win();
        set_deck_win();
        Value cs = G.PROFILES->get(G.SETTINGS->get(K("profile"))).get(K("high_scores")).get(K("current_streak"));
        check_and_set_high_score("win_streak", Value(fld(cs, "amt").num() + 1));
        check_and_set_high_score("current_streak", Value(fld(cs, "amt").num() + 1));
        for (const char *t : {"win_no_hand", "win_no", "win_custom", "win_deck", "win_stake", "win"}) check_for_unlock(vl::T({{"type", t}}));
        inc_career_stat("c_wins", 1);
    }
    set_profile_progress();
    if (gv("challenge").truthy()) {
        Table *p = G.PROFILES->get(G.SETTINGS->get(K("profile"))).table();
        fld(p->get(K("challenge_progress")), "completed").table()->set(gv("challenge"), true);
        set_challenge_unlock();
        check_for_unlock(vl::T({{"type", "win_challenge"}}));
        G.save_settings();
    }
    G.E_MANAGER->add_event(ev_immediate([] {
        G.I.CARD.each([](Moveable *c) {
            if (auto *card = dynamic_cast<Card *>(c)) card->sticker_run = Value();
        });
        play_sound("win");
        G.SETTINGS->set("paused", true);
        overlay_menu(create_UIBox_win(), vl::T({{"no_esc", true}}));
        // (G.F_JAN_CTA's wishlist button is off: no Steam on the Vita)
        jimbo_quip("wq_", 7);
        return true;
    }));
    if (!gv("seeded").truthy() && !gv("challenge").truthy()) {
        Table *p = G.PROFILES->get(G.SETTINGS->get(K("profile"))).table();
        p->set("stake", std::max(p->get(K("stake")).truthy() ? p->get(K("stake")).num() : 1, (gv("stake").truthy() ? gv("stake").num() : 1) + 1));
    }
    G.save_progress();
    G.FILE_HANDLER->set("force", true);
    G.E_MANAGER->add_event(ev_immediate([] {
        if (!G.SETTINGS->get(K("paused")).truthy()) {
            gv("current_round").table()->set("round_text", "Endless Round ");
            return true;
        }
        return false;
    }));
}

void register_game_over_callbacks() {
    auto &F = G.FUNCS;
    F["high_score_alert"] = [](UIElement *e) {
        const Value id = e->config->get(K("id"));
        if (!id.truthy() || e->children.get("alert")) return;
        if (fld(gv("round_scores").get(id), "high_score").truthy()) {
            UIBox *alert = new UIBox(create_UIBox_card_alert(vl::T({{"no_bg", true}, {"text", localize("k_high_score_ex")}, {"scale", 0.3}})),
                                     vl::T({{"instance_type", "ALERT"}, {"align", "tri"}, {"offset", Value(vl::T({{"x", 0.3}, {"y", -0.18}}))},
                                            {"major", obj(e)}, {"parent", obj(e)}}));
            alert->states.collide.can = false;
            e->children.set("alert", alert);
        }
    };
    // G.FUNCS.notify_then_setup_run: the unlock notifications, then the run setup screen
    F["notify_then_setup_run"] = [](UIElement *e) {
        if (G.OVERLAY_MENU) G.OVERLAY_MENU->remove();
        G.OVERLAY_MENU = nullptr;
        EventConfig a = ev_immediate([] {
            unlock_notify();
            return true;
        });
        a.blockable = false;
        G.E_MANAGER->add_event(a);
        const Value id = e ? e->config->get(K("id")) : Value();
        EventConfig b = ev_immediate([id] {
            if (!G.E_MANAGER->queues[EventManager::UNLOCK].empty() || G.OVERLAY_MENU) return false;
            open_run_setup(id.equals("from_game_over") || id.equals("from_game_won") ? id.to_string() : std::string());
            return true;
        });
        b.blockable = false;
        G.E_MANAGER->add_event(b);
    };
    // (the Vita has no clipboard: G.F_LOCAL_CLIPBOARD's in-game one)
    F["copy_seed"] = [](UIElement *) { G.G_TABLE->set("CLIPBOARD", fld(gv("pseudorandom"), "seed")); };
}

}  // namespace vl
