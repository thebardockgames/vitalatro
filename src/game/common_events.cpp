// functions/common_events.lua (the parts used outside a run; run events are ported with the rules)
#include <algorithm>
#include <cmath>

#include "core/lua_sort.h"
#include "engine/controller.h"
#include "engine/ui.h"
#include "game/blind.h"
#include "game/button_callbacks.h"
#include "game/card.h"
#include "game/functions.h"
#include "game/g.h"
#include "game/save_manager.h"
#include "platform/platform.h"
#include "game/sound.h"
#include "game/state_events.h"
#include "game/ui_definitions.h"

namespace vl {

namespace {
inline Value fld(const Value &t, const char *k) { return t.get(Value(intern(k))); }
Value profile() { return Value(G.PROFILES).get(G.SETTINGS->get(K("profile"))); }
void unlock_achievement(const char *name);
}  // namespace

void set_screen_positions() {
    if (G.STAGE == RUN) {
        G.hand->T->x = G.TILE_W - G.hand->T->w - 3.55f;
        G.hand->T->y = G.TILE_H - G.hand->T->h;
        G.play->T->x = G.hand->T->x + (G.hand->T->w - G.play->T->w) / 2;
        G.play->T->y = G.hand->T->y - 3.6f;
        G.jokers->T->x = G.hand->T->x - 0.1f;
        G.jokers->T->y = 0;
        G.consumeables->T->x = G.jokers->T->x + G.jokers->T->w + 0.8f;
        G.consumeables->T->y = 0;
        G.deck->T->x = G.TILE_W - G.deck->T->w - 0.5f;
        G.deck->T->y = G.TILE_H - G.deck->T->h;
        G.discard->T->x = G.jokers->T->x + G.jokers->T->w / 2 + 0.3f + 15;
        G.discard->T->y = 4.2f;
        for (CardArea *a : {G.hand.get(), G.play.get(), G.jokers.get(), G.consumeables.get(), G.deck.get(), G.discard.get()})
            a->hard_set_VT();
    }
    if (G.STAGE == MAIN_MENU) {
        G.title_top->T->x = G.TILE_W / 2 - G.title_top->T->w / 2;
        if (G.STATE == DEMO_CTA) G.title_top->T->y = G.TILE_H / 2 - G.title_top->T->h / 2 - 2;
        else G.title_top->T->y = G.TILE_H / 2 - G.title_top->T->h / 2 - 1.2f;
        G.title_top->hard_set_VT();
    }
}

void ease_value(ValueRef ref, double mod, bool floored, const EventTimer *timer, double delay, EaseType ease) {
    EventConfig e;
    e.trigger = Trigger::Ease;
    e.blockable = false;
    e.blocking = false;
    e.ease_to = ref.get().num() + mod;
    if (timer) {
        e.has_timer = true;
        e.timer = *timer;
    }
    e.delay = delay;
    // ease_value passes its ease type as 'type', but Event:init reads config.ease: every
    // ease_value is linear in the original
    (void)ease;
    if (floored) e.ease_func = [](double t) { return std::floor(t); };
    e.ref = std::move(ref);
    G.E_MANAGER->add_event(std::move(e));
}

void ease_colour(const TableRef &old_colour, const Value &new_colour, double delay) {
    static const EventTimer real = EventTimer::REAL;
    for (int i = 1; i <= 4; i++)
        ease_value(ValueRef::field(old_colour, i), new_colour[i].num() - old_colour->get(i).num(), false, &real, delay);
}

void ease_background_colour(const TableRef &args) {
    Value bg = G.C->get(K("BACKGROUND"));
    Value nc = args->get(K("new_colour")), sc = args->get(K("special_colour")), tc = args->get(K("tertiary_colour"));
    bg.table()->each([&](const Value &k, const Value &v) {
        if (!(nc.truthy() && (k.equals("C") || k.equals("L") || k.equals("D")))) return;
        TableRef vt(v.table());
        auto ease3 = [&](const Value &target, double mul) {
            for (int i = 1; i <= 3; i++) ease_value(ValueRef::field(vt, i), target[i].num() * mul - v[i].num(), false, nullptr, 0.6);
        };
        if (sc.truthy() && tc.truthy()) {
            ease3(k.equals("L") ? nc : k.equals("C") ? sc : tc, 1);
        } else {
            double brightness = k.equals("L") ? 1.3 : k.equals("D") ? (sc.truthy() ? 0.4 : 0.7) : 0.9;
            if (k.equals("C") && sc.truthy()) ease3(sc, 1);
            else ease3(nc, brightness);
        }
    });
    if (args->get(K("contrast")).truthy())
        ease_value(ValueRef::field(TableRef(bg.table()), Value("contrast")), args->get(K("contrast")).num() - bg["contrast"].num(),
                   false, nullptr, 0.6);
}

void delay(double time, EventManager::Queue queue) {
    G.E_MANAGER->add_event(ev_after(time, [] { return true; }), queue);
}

void set_alerts() {
    if (!G.REFRESH_ALERTS) return;
    G.REFRESH_ALERTS = false;
    bool a_joker = false, a_voucher = false, a_tarot = false, a_planet = false, a_spectral = false, a_blind = false,
         a_edition = false, a_tag = false, a_seal = false, a_booster = false;
    G.P_CENTERS->each([&](const Value &, const Value &v) {
        if (fld(v, "discovered").truthy() && !fld(v, "alerted").truthy()) {
            Value s = fld(v, "set");
            if (s.equals("Voucher")) a_voucher = true;
            if (s.equals("Tarot")) a_tarot = true;
            if (s.equals("Planet")) a_planet = true;
            if (s.equals("Spectral")) a_spectral = true;
            if (s.equals("Joker")) a_joker = true;
            if (s.equals("Edition")) a_edition = true;
            if (s.equals("Booster")) a_booster = true;
        }
    });
    auto any_unalerted = [](const TableRef &t) {
        bool r = false;
        t->each([&](const Value &, const Value &v) {
            if (fld(v, "discovered").truthy() && !fld(v, "alerted").truthy()) r = true;
        });
        return r;
    };
    a_blind = any_unalerted(G.P_BLINDS);
    a_tag = any_unalerted(G.P_TAGS);
    a_seal = any_unalerted(G.P_SEALS);
    bool a_any = a_voucher || a_joker || a_tarot || a_planet || a_spectral || a_blind || a_edition || a_seal || a_tag;

    struct Alertable { const char *id, *name; bool should; };
    Alertable list[] = {{"your_collection", "your_collection_alert", a_any},
                        {"your_collection_jokers", "your_collection_jokers_alert", a_joker},
                        {"your_collection_tarots", "your_collection_tarots_alert", a_tarot},
                        {"your_collection_planets", "your_collection_planets_alert", a_planet},
                        {"your_collection_spectrals", "your_collection_spectrals_alert", a_spectral},
                        {"your_collection_vouchers", "your_collection_vouchers_alert", a_voucher},
                        {"your_collection_editions", "your_collection_editions_alert", a_edition},
                        {"your_collection_blinds", "your_collection_blinds_alert", a_blind},
                        {"your_collection_tags", "your_collection_tags_alert", a_tag},
                        {"your_collection_seals", "your_collection_seals_alert", a_seal},
                        {"your_collection_boosters", "your_collection_boosters_alert", a_booster}};
    UIBox *overlay = dynamic_cast<UIBox *>(G.OVERLAY_MENU.get());
    for (const Alertable &v : list) {
        NodeRef &box = G.alert_uiboxes[v.name];
        UIElement *target = overlay ? overlay->get_UIE_by_ID(v.id) : nullptr;
        if (target && v.should) {
            if (!box) {
                UIBox *b = new UIBox(create_UIBox_card_alert(vl::T({{"red_bad", true}})),
                                     vl::T({{"align", "tri"},
                                            {"offset", Value(vl::T({{"x", 0.05}, {"y", -0.05}}))},
                                            {"major", Value((Object *)target)},
                                            {"instance_type", "ALERT"}}));
                b->states.collide.can = false;
                box = b;
            }
        } else if (box) {
            box->remove();
            box = nullptr;
        }
    }
    UIBox *mm = dynamic_cast<UIBox *>(G.MAIN_MENU_UI.get());
    if (mm) {
        if (a_any) {
            if (!G.collection_alert) {
                UIBox *b = new UIBox(create_UIBox_card_alert(),
                                     vl::T({{"align", "tri"},
                                            {"offset", Value(vl::T({{"x", 0.05}, {"y", -0.05}}))},
                                            {"major", Value((Object *)mm->get_UIE_by_ID("collection_button"))}}));
                b->states.collide.can = false;
                G.collection_alert = b;
            }
        } else if (G.collection_alert) {
            G.collection_alert->remove();
            G.collection_alert = nullptr;
        }
    } else if (G.collection_alert) {
        G.collection_alert->remove();
        G.collection_alert = nullptr;
    }
}

void set_main_menu_UI() {
    UIBox *mm = new UIBox(create_UIBox_main_menu_buttons(), vl::T({{"align", "bmi"},
                                                                  {"offset", Value(vl::T({{"x", 0}, {"y", 10}}))},
                                                                  {"major", Value((Object *)G.ROOM_ATTACH.get())},
                                                                  {"bond", "Weak"}}));
    G.MAIN_MENU_UI = mm;
    mm->alignment.offset.y = 0;
    mm->align_to_major();
    EventConfig e = ev_immediate([] {
        UIBox *pb = new UIBox(create_UIBox_profile_button(), vl::T({{"align", "bli"},
                                                                    {"offset", Value(vl::T({{"x", -10}, {"y", 0}}))},
                                                                    {"major", Value((Object *)G.ROOM_ATTACH.get())},
                                                                    {"bond", "Weak"}}));
        G.PROFILE_BUTTON = pb;
        pb->alignment.offset.x = 0;
        pb->align_to_major();
        return true;
    });
    e.blockable = false;
    e.blocking = false;
    G.E_MANAGER->add_event(std::move(e));
    G.CONTROLLER->snap_to(mm->get_UIE_by_ID("main_menu_play"));
}

void update_canvas_juice(double dt) {
    G.JIGGLE_VIBRATION = G.ROOM_jiggle;
    Value ss = G.SETTINGS->get(K("screenshake"));
    if (!ss.truthy() || !ss.is_num()) G.SETTINGS->set("screenshake", G.reduced_motion() ? 0 : 50);
    double screenshake = G.SETTINGS->get(K("screenshake")).num();
    double motion = G.reduced_motion() ? 0 : 1;
    double shake_amt = motion * std::max(0.0, screenshake - 30) / 100;
    auto &ec = G.eased_cursor_pos;
    if (!ec.set) {
        ec = {G.CURSOR->T->x, G.CURSOR->T->y, G.CONTROLLER->cursor_position.x, G.CONTROLLER->cursor_position.y, true};
    }
    ec.x = ec.x * (1 - 3 * dt) + 3 * dt * (shake_amt * G.CURSOR->T->x + (1 - shake_amt) * G.ROOM->T->w / 2);
    ec.y = ec.y * (1 - 3 * dt) + 3 * dt * (shake_amt * G.CURSOR->T->y + (1 - shake_amt) * G.ROOM->T->h / 2);
    ec.sx = ec.sx * (1 - 3 * dt) + 3 * dt * (shake_amt * G.CONTROLLER->cursor_position.x + (1 - shake_amt) * G.WINDOWTRANS.real_window_w / 2);
    ec.sy = ec.sy * (1 - 3 * dt) + 3 * dt * (shake_amt * G.CONTROLLER->cursor_position.y + (1 - shake_amt) * G.WINDOWTRANS.real_window_h / 2);

    shake_amt = motion * screenshake / 100 * 3;
    if (shake_amt < 0.05) shake_amt = 0;
    G.ROOM_jiggle = G.ROOM_jiggle * (1 - 5 * dt) * (shake_amt > 0.05 ? 1 : 0);
    const double R = G.TIMERS.REAL;
    G.ROOM->T->r = (float)((0.001 * std::sin(0.3 * R) + 0.002 * G.ROOM_jiggle * std::sin(39.913 * R)) * shake_amt);
    G.ROOM->T->x = (float)(G.ROOM_ORIG.x + shake_amt * (0.015 * std::sin(0.913 * R) + 0.01 * (G.ROOM_jiggle * shake_amt) * std::sin(19.913 * R) +
                                                       (ec.x - 0.5 * (G.ROOM->T->w + G.ROOM_ORIG.x)) * 0.01));
    G.ROOM->T->y = (float)(G.ROOM_ORIG.y + shake_amt * (0.015 * std::sin(0.952 * R) + 0.01 * (G.ROOM_jiggle * shake_amt) * std::sin(21.913 * R) +
                                                       (ec.y - 0.5 * (G.ROOM->T->h + G.ROOM_ORIG.y)) * 0.01));
    G.JIGGLE_VIBRATION = G.JIGGLE_VIBRATION * (1 - 5 * dt);
    G.CURR_VIBRATION = std::min(1.0, G.CURR_VIBRATION + G.VIBRATION + G.JIGGLE_VIBRATION * 0.2);
    G.VIBRATION = 0;
    G.CURR_VIBRATION = (1 - 15 * dt) * G.CURR_VIBRATION;
    if (!G.SETTINGS->get(K("rumble")).truthy()) G.CURR_VIBRATION = 0;
    // (the Vita has no rumble motor)
    G.VIBRATION = 0;
}

// ------------------------------------------------------------------ unlocks / achievements
void check_for_unlock(const TableRef &args) {
    bool any = false;
    args->each([&](const Value &, const Value &) { any = true; });
    if (!any) return;
    Value game(G.GAME);
    if (fld(game, "seeded").truthy()) return;
    Value type = args->get(K("type"));
    Value prof = profile();
    if (type.equals("win_challenge")) {
        unlock_achievement("rule_bender");
        bool c = true;
        G.CHALLENGES->each([&](const Value &, const Value &v) {
            if (!fld(fld(prof, "challenge_progress"), "completed").get(fld(v, "id")).truthy()) c = false;
        });
        if (c) unlock_achievement("rule_breaker");
    }
    if (fld(game, "challenge").truthy()) return;

    Value career = fld(prof, "career_stats");
    if (type.equals("career_stat")) {
        Value sn = args->get(K("statname"));
        if (sn.equals("c_cards_played") && career.get(sn).num() >= 2500) unlock_achievement("card_player");
        if (sn.equals("c_cards_discarded") && career.get(sn).num() >= 2500) unlock_achievement("card_discarder");
    }
    if (type.equals("ante_up")) {
        if (args->get(K("ante")).num() >= 4) unlock_achievement("ante_up");
        if (args->get(K("ante")).num() >= 8) unlock_achievement("ante_upper");
    }
    if (type.equals("win")) {
        unlock_achievement("heads_up");
        if (fld(game, "round").num() <= 12) unlock_achievement("speedrunner");
        if (fld(fld(fld(game, "round_scores"), "times_rerolled"), "amt").num() <= 0) unlock_achievement("you_get_what_you_get");
    }
    if (type.equals("win_stake")) {
        int highest = get_deck_win_stake(Value());
        if (highest >= 2) unlock_achievement("low_stakes");
        if (highest >= 4) unlock_achievement("mid_stakes");
        if (highest >= 8) unlock_achievement("high_stakes");
        Value P(G.PROGRESS);
        if (G.PROGRESS && fld(fld(P, "deck_stakes"), "tally").num() / fld(fld(P, "deck_stakes"), "of").num() >= 1)
            unlock_achievement("completionist_plus");
        if (G.PROGRESS && fld(fld(P, "joker_stickers"), "tally").num() / fld(fld(P, "joker_stickers"), "of").num() >= 1)
            unlock_achievement("completionist_plus_plus");
    }
    if (type.equals("money") && fld(game, "dollars").num() >= 400) unlock_achievement("nest_egg");
    if (type.equals("hand")) {
        Value sh = args->get(K("scoring_hand"));
        if (args->get(K("handname")).equals("Flush") && sh.truthy()) {
            int w = 0, n = sh.table()->len();
            for (int i = 1; i <= n; i++) {
                Card *c = sh[i].as<Card>();
                if (c && c->ability->get(K("name")).equals("Wild Card")) w++;
            }
            if (w == n) unlock_achievement("flushed");
        }
        if (args->get(K("disp_text")).equals("Royal Flush")) unlock_achievement("royale");
    }
    if (type.equals("shatter") && args->get(K("shattered")).table() && args->get(K("shattered")).table()->len() >= 2)
        unlock_achievement("shattered");
    if (type.equals("run_redeem")) {
        double v = -fld(game, "starting_voucher_count").num();
        if (Table *uv = fld(game, "used_vouchers").table()) uv->each([&](const Value &, const Value &) { v += 1; });
        if (v >= 5 && fld(fld(game, "round_resets"), "ante").num() <= 4) unlock_achievement("roi");
    }
    if (type.equals("upgrade_hand") && args->get(K("level")).num() >= 10) unlock_achievement("retrograde");
    if (type.equals("chip_score")) {
        double chips = args->get(K("chips")).num();
        if (chips >= 10000) unlock_achievement("_10k");
        if (chips >= 1000000) unlock_achievement("_1000k");
        if (chips >= 100000000) unlock_achievement("_100000k");
    }
    if (type.equals("modify_deck")) {
        if (G.deck && G.deck->config->get(K("card_limit")).num() <= 20) unlock_achievement("tiny_hands");
        if (G.deck && G.deck->config->get(K("card_limit")).num() >= 80) unlock_achievement("big_hands");
    }
    if (type.equals("spawn_legendary")) unlock_achievement("legendary");
    if (type.equals("discover_amount")) {
        Value D(G.DISCOVER_TALLIES);
        auto full = [&](const char *k) { return fld(fld(D, k), "tally").num() / fld(fld(D, k), "of").num() >= 1; };
        if (full("vouchers")) unlock_achievement("extreme_couponer");
        if (full("spectrals")) unlock_achievement("clairvoyance");
        if (full("tarots")) unlock_achievement("cartomancy");
        if (full("planets")) unlock_achievement("astronomy");
        if (full("total")) unlock_achievement("completionist");
    }

    int i = 1;
    while (i <= G.P_LOCKED->len()) {
        bool ret = false;
        Value card = G.P_LOCKED->get(i);
        Value uc = fld(card, "unlock_condition");
        bool locked = !fld(card, "unlocked").truthy();
        auto unlock = [&] {
            ret = true;
            unlock_card(card);
        };
        if (locked && uc.truthy() && type.equals("career_stat")) {
            Value sn = args->get(K("statname"));
            if (sn == fld(uc, "type") && career.get(sn).num() >= fld(uc, "extra").num()) unlock();
        }
        if (locked && uc.truthy() && fld(uc, "type") == type) {
            Value extra = fld(uc, "extra");
            if (type.equals("hand") && args->get(K("handname")) == extra) unlock();
            if (type.equals("min_hand_size") && G.hand && G.hand->config->get(K("card_limit")).num() <= extra.num()) unlock();
            if (type.equals("interest_streak") && extra.num() <= fld(career, "c_round_interest_cap_streak").num()) unlock();
            if (type.equals("run_card_replays")) {
                for (auto &v : G.playing_cards)
                    if (v->base->get(K("times_played")).num() >= extra.num()) {
                        unlock();
                        break;
                    }
            }
            if (type.equals("play_all_hearts")) {
                bool played = true;
                for (CardArea *a : {G.deck.get(), G.hand.get()})
                    if (a)
                        for (auto &v : a->cards)
                            if (!v->ability->get(K("name")).equals("Stone Card") && v->base->get(K("suit")).equals("Hearts")) played = false;
                if (played) unlock();
            }
            if (type.equals("run_redeem")) {
                double n = 0;
                if (Table *uv = fld(game, "used_vouchers").table()) uv->each([&](const Value &, const Value &) { n += 1; });
                if (n >= extra.num()) unlock();
            }
            if (type.equals("have_edition")) {
                int shiny = 0;
                if (G.jokers)
                    for (auto &v : G.jokers->cards)
                        if (v->edition) shiny++;
                if (shiny >= extra.num()) unlock();
            }
            if (type.equals("double_gold")) unlock();
            if (type.equals("continue_game")) unlock();
            if (type.equals("blank_redeems")) {
                Value vb = fld(fld(prof, "voucher_usage"), "v_blank");
                if (vb.truthy() && fld(vb, "count").num() >= extra.num()) unlock_card(card);
            }
            if (type.equals("modify_deck")) {
                auto count_if = [&](auto pred) {
                    int c = 0;
                    for (auto &v : G.playing_cards)
                        if (pred(v.get())) c++;
                    return c;
                };
                if (extra.truthy() && fld(extra, "suit").truthy()) {
                    if (count_if([&](Card *v) { return v->base->get(K("suit")) == fld(extra, "suit"); }) >= fld(extra, "count").num()) unlock();
                }
                if (extra.truthy() && fld(extra, "enhancement").truthy()) {
                    if (count_if([&](Card *v) { return v->ability->get(K("name")) == fld(extra, "enhancement"); }) >= fld(extra, "count").num())
                        unlock();
                }
                if (extra.truthy() && fld(extra, "tally").truthy()) {
                    if (count_if([&](Card *v) { return v->ability->get(K("set")).equals("Enhanced"); }) >= fld(extra, "count").num()) unlock();
                }
            }
            if (type.equals("discover_amount")) {
                if (fld(uc, "amount").truthy() && fld(uc, "amount").num() <= args->get(K("amount")).num()) unlock();
                if (fld(uc, "tarot_count").truthy() && fld(uc, "tarot_count").num() <= args->get(K("tarot_count")).num()) unlock();
                if (fld(uc, "planet_count").truthy() && fld(uc, "planet_count").num() <= args->get(K("planet_count")).num()) unlock();
            }
            if (type.equals("win_deck") && fld(uc, "deck").truthy() && get_deck_win_stake(fld(uc, "deck")) > 0) unlock();
            if (type.equals("win_stake") && fld(uc, "stake").truthy() && get_deck_win_stake(Value()) >= fld(uc, "stake").num()) unlock();
            if (type.equals("discover_planets")) {
                int c = 0;
                G.P_CENTERS->each([&](const Value &, const Value &v) {
                    if (fld(v, "set").equals("Planet") && fld(v, "discovered").truthy()) c++;
                });
                if (c >= 9) unlock();
            }
            if (type.equals("blind_discoveries")) {
                int c = 0;
                G.P_BLINDS->each([&](const Value &, const Value &v) {
                    if (fld(v, "discovered").truthy()) c++;
                });
                if (c >= extra.num()) unlock();
            }
            if (type.equals("modify_jokers") && G.jokers) {
                if (fld(extra, "count").truthy()) {
                    int c = 0;
                    for (auto &v : G.jokers->cards)
                        if (v->ability->get(K("set")).equals("Joker") && v->edition && v->edition->get(K("polychrome")).truthy() &&
                            fld(extra, "polychrome").truthy())
                            c++;
                    if (c >= fld(extra, "count").num()) unlock();
                }
            }
            if (type.equals("money") && extra.num() <= fld(game, "dollars").num()) unlock();
            if (type.equals("round_win")) {
                Value cr = fld(game, "current_round");
                Blind *bl = fld(game, "blind").as<Blind>();
                bool boss = bl && bl->get_type() && std::string(bl->get_type()) == "Boss";
                if (fld(card, "name").equals("Matador")) {
                    if (fld(cr, "hands_played").num() == 1 && fld(cr, "discards_left").num() == fld(fld(game, "round_resets"), "discards").num() &&
                        boss)
                        unlock();
                }
                if (fld(card, "name").equals("Troubadour")) {
                    if (fld(career, "c_single_hand_round_streak").num() >= extra.num()) unlock();
                }
                if (fld(card, "name").equals("Hanging Chad")) {
                    if (fld(game, "last_hand_played") == extra && boss) unlock();
                }
            }
            if (type.equals("ante_up") && fld(uc, "ante").truthy() && args->get(K("ante")) == fld(uc, "ante")) unlock();
            if (type.equals("hand_contents")) {
                Table *cs = args->get(K("cards")).table();
                int n = cs ? cs->len() : 0;
                if (fld(card, "name").equals("Seeing Double")) {
                    int tally = 0;
                    for (int j = 1; j <= n; j++) {
                        Card *c = cs->get(j).as<Card>();
                        if (c->get_id() == 7 && c->is_suit(Value("Clubs"))) tally++;
                    }
                    if (tally >= 4) unlock();
                }
                if (fld(card, "name").equals("Golden Ticket")) {
                    int tally = 0;
                    for (int j = 1; j <= n; j++)
                        if (cs->get(j).as<Card>()->ability->get(K("name")).equals("Gold Card")) tally++;
                    if (tally >= 5) unlock();
                }
            }
            if (type.equals("discard_custom")) {
                Table *cs = args->get(K("cards")).table();
                std::vector<Card *> cards;
                for (int j = 1; cs && j <= cs->len(); j++) cards.push_back(cs->get(j).as<Card>());
                if (fld(card, "name").equals("Hit the Road")) {
                    int tally = 0;
                    for (Card *c : cards)
                        if (c->get_id() == 11) tally++;
                    if (tally >= 5) unlock();
                }
                if (fld(card, "name").equals("Brainstorm")) {
                    TableRef eval = evaluate_poker_hand(cards);
                    Table *sf = eval->get(K("Straight Flush")).table();
                    if (sf && sf->len() > 0) {
                        int mn = 10;
                        for (Card *c : cards)
                            if (c->get_id() < mn) mn = c->get_id();
                        if (mn == 10) unlock();
                    }
                }
            }
            if (type.equals("win_no_hand") && fld(fld(fld(game, "hands"), std::string(extra.str()).c_str()), "played").num() == 0) unlock();
            if (type.equals("win_custom")) {
                if (fld(card, "name").equals("Invisible Joker") && fld(game, "max_jokers").num() <= 4) unlock();
                if (fld(card, "name").equals("Blueprint")) unlock();
            }
            if (type.equals("win") && fld(uc, "n_rounds").num() >= fld(game, "round").num()) unlock();
            if (type.equals("chip_score") && fld(uc, "chips").num() <= args->get(K("chips")).num()) {
                ret = true;
                G.E_MANAGER->add_event(ev_immediate([card] {
                    G.E_MANAGER->add_event(ev_immediate([card] {
                        unlock_card(card);
                        return true;
                    }));
                    return true;
                }));
            }
        }
        if (ret) G.P_LOCKED->remove(i);
        else i++;
    }
}

void unlock_card(const Value &card) {
    Table *c = card.table();
    if (!c) return;
    Value u = c->get(K("unlocked"));
    if (!(u.is_bool() && !u.boolean())) return;
    Value game(G.GAME);
    if (fld(game, "seeded").truthy() || fld(game, "challenge").truthy()) return;
    if (c->get(K("wip")).truthy()) return;
    G.save_notify(card);
    c->set("unlocked", true);
    if (c->get(K("set")).equals("Back")) discover_card(card);
    Table *backs = G.P_CENTER_POOLS->get(K("Back")).table();
    std::vector<Value> v(backs->array().begin(), backs->array().end());
    lua_sort(v, [](const Value &a, const Value &b) {
        return fld(a, "order").num() - (fld(a, "unlocked").truthy() ? 100 : 0) < fld(b, "order").num() - (fld(b, "unlocked").truthy() ? 100 : 0);
    });
    for (size_t i = 0; i < v.size(); i++) backs->set((int)i + 1, v[i]);
    G.save_progress();
    if (G.FILE_HANDLER) G.FILE_HANDLER->set("force", true);
    // notify_alert(card.key, card.set)
    notify_alert(c->get(K("key")), c->get(K("set")));
}

void fetch_achievements() {
    if (!G.ACHIEVEMENTS) {
        G.ACHIEVEMENTS = new_table();
        const char *names[] = {"ante_up",     "ante_upper",      "heads_up",     "low_stakes",  "mid_stakes",           "high_stakes",
                               "card_player", "card_discarder",  "nest_egg",     "flushed",     "speedrunner",          "roi",
                               "shattered",   "royale",          "retrograde",   "_10k",        "_1000k",               "_100000k",
                               "tiny_hands",  "big_hands",       "you_get_what_you_get", "rule_bender", "rule_breaker", "legendary",
                               "astronomy",   "cartomancy",      "clairvoyance", "extreme_couponer", "completionist",  "completionist_plus",
                               "completionist_plus_plus"};
        const int tiers[] = {3, 3, 2, 2, 2, 2, 3, 3, 2, 3, 2, 3, 3, 3, 2, 3, 2, 1, 2, 2, 3, 3, 1, 3, 3, 3, 2, 1, 1, 1, 1};
        for (int i = 0; i < 31; i++) {
            char sid[16];
            std::snprintf(sid, sizeof sid, "BAL_%02d", i + 1);
            G.ACHIEVEMENTS->set(names[i], Value(vl::T({{"order", i + 1}, {"tier", tiers[i]}, {"earned", false}, {"steamid", sid}})));
        }
    }
    if (G.F_NO_ACHIEVEMENTS) return;
    if (!G.SETTINGS->get(K("ACHIEVEMENTS_EARNED")).truthy()) G.SETTINGS->set("ACHIEVEMENTS_EARNED", Value(new_table()));
    G.SETTINGS->get(K("ACHIEVEMENTS_EARNED")).table()->each([&](const Value &k, const Value &) {
        if (Table *a = G.ACHIEVEMENTS->get(k).table()) a->set("earned", true);
    });
}

namespace {
void unlock_achievement(const char *name) {
    if (fld(profile(), "all_unlocked").truthy()) return;
    std::string n = name;
    EventConfig e = ev_immediate([n] {
        if (G.STATE == HAND_PLAYED) return false;
        if (fld(profile(), "all_unlocked").truthy()) return false;
        if (G.F_NO_ACHIEVEMENTS) return false;
        if (!G.ACHIEVEMENTS) fetch_achievements();
        G.SETTINGS->get(K("ACHIEVEMENTS_EARNED")).table()->set(n.c_str(), true);
        G.save_progress();
        bool set = false;
        if (Table *a = G.ACHIEVEMENTS->get(n.c_str()).table()) {
            if (!a->get(K("earned")).truthy()) {
                set = true;
                if (G.FILE_HANDLER) G.FILE_HANDLER->set("force", true);
            }
            a->set("earned", true);
        }
        if (set) notify_alert(Value(n), Value());
        return true;
    });
    e.no_delete = true;
    e.blockable = false;
    e.blocking = false;
    G.E_MANAGER->add_event(std::move(e), EventManager::ACHIEVEMENT);
}
}  // namespace

void notify_alert(const Value &achievement, const Value &type_in) {
    bool left = false;  // notify_left(): no notch on the Vita
    Value type = type_in.truthy() ? type_in : Value("achievement");
    auto ev = [](EventConfig e) {
        e.no_delete = true;
        e.pause_force = true;
        e.has_timer = true;
        e.timer = EventTimer::UPTIME;
        G.E_MANAGER->add_event(std::move(e), EventManager::ACHIEVEMENT);
    };
    ev(ev_immediate([achievement, type, left] {
        if (G.achievement_notification) {
            G.achievement_notification->remove();
            G.achievement_notification = nullptr;
        }
        G.achievement_notification = new UIBox(create_UIBox_notify_alert(achievement, type, left),
                                               vl::T({{"align", left ? "cl" : "cr"},
                                                      {"offset", Value(vl::T({{"x", left ? -20 : 20}, {"y", 0}}))},
                                                      {"major", Value((Object *)G.ROOM_ATTACH.get())},
                                                      {"bond", "Weak"}}));
        return true;
    }));
    ev(ev_after(0.1, [left] {
        UIBox *n = dynamic_cast<UIBox *>(G.achievement_notification.get());
        if (!n) return true;
        Node *c = n->UIRoot->children.count() ? n->UIRoot->children.at(0) : nullptr;
        Node *cc = c && c->children.count() ? c->children.at(0) : nullptr;
        float w = cc ? cc->T->w : 0;
        n->alignment.offset.x = left ? (-G.ROOM->T->x + w + 0.8f) : (G.ROOM->T->x - w - 0.8f);
        return true;
    }));
    ev(ev_after(0.1, [] {
        play_sound("highlight1", 1, 0.5f);
        play_sound("foil2", 0.5f, 0.4f);
        return true;
    }));
    ev(ev_after(3, [left] {
        if (auto *n = dynamic_cast<UIBox *>(G.achievement_notification.get())) n->alignment.offset.x = left ? -20.f : 20.f;
        return true;
    }));
    ev(ev_after(0.5, [] {
        if (G.achievement_notification) {
            G.achievement_notification->remove();
            G.achievement_notification = nullptr;
        }
        return true;
    }));
}

void unlock_notify() {
    std::string file = G.SETTINGS->get(K("profile")).to_string() + "/unlock_notify.jkr";
    std::string un;
    if (get_compressed(file, un)) {
        // string.gmatch(_UN .. "\n", "(.-)\n")
        un += "\n";
        size_t pos = 0, nl;
        while ((nl = un.find('\n', pos)) != std::string::npos) {
            create_unlock_overlay(un.substr(pos, nl - pos));
            pos = nl + 1;
        }
        platform::remove_save(file.c_str());
    }
}

void create_unlock_overlay(const std::string &key) {
    if (!G.P_CENTERS->get(Value(key)).truthy()) return;
    EventConfig e = ev_immediate([key] {
        if (G.OVERLAY_MENU) return false;
        G.SETTINGS->set("paused", true);
        const Value c = G.P_CENTERS->get(Value(key));
        overlay_menu(fld(c, "set").equals("Back") ? create_UIBox_deck_unlock(c) : create_UIBox_card_unlock(c));
        play_sound("foil1", 0.7f, 0.3f);
        play_sound("gong", 1.4f, 0.15f);
        return true;
    });
    e.no_delete = true;
    G.E_MANAGER->add_event(std::move(e), EventManager::UNLOCK);
}

void discover_card(const Value &card_in) {
    Value game(G.GAME);
    if (fld(game, "seeded").truthy() || fld(game, "challenge").truthy()) return;
    Table *c = card_in.table();
    if (!c) return;
    if (c->get(K("discovered")).truthy() || c->get(K("wip")).truthy()) return;
    c->set("alert", true);
    Value nc = fld(fld(game, "round_scores"), "new_collection");
    if (Table *t = nc.table()) t->set("amt", t->get(K("amt")).num() + 1);
    c->set("discovered", true);
    set_discover_tallies();
    G.E_MANAGER->add_event(ev_immediate([] {
        G.save_progress();
        return true;
    }));
}

}  // namespace vl
