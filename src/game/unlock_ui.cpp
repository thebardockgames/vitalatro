// The unlock screens shown after a run (UI_definitions.lua create_UIBox_card_unlock / create_UIBox_deck_unlock).
#include "engine/event.h"
#include "engine/text.h"
#include "engine/ui.h"
#include "game/card.h"
#include "game/card_ui.h"
#include "game/functions.h"
#include "game/g.h"
#include "game/sound.h"
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

// G.E_MANAGER:add_event(Event({timer = 'REAL', blockable = false, blocking = false, trigger = 'after', delay = d, func = f}))
void real_event(double delay, std::function<bool()> f) {
    EventConfig e = delay > 0 ? ev_after(delay, std::move(f)) : ev_immediate(std::move(f));
    e.has_timer = true;
    e.timer = EventTimer::REAL;
    e.blockable = false;
    e.blocking = false;
    G.E_MANAGER->add_event(std::move(e));
}
}  // namespace

// G.OVERLAY_MENU.joker_unlock_table == id: the unlock screen of that card is still the one shown
static bool unlock_shown(int id) { return G.OVERLAY_MENU && G.OVERLAY_MENU->config->get(K("joker_unlock_table")) == Value((double)id); }

TableRef create_UIBox_card_unlock(const Value &card_center) {
    CardArea *yc = new CardArea(G.ROOM->T->x + G.ROOM->T->w / 2, G.ROOM->T->h, G.CARD_W, G.CARD_H,
                                vl::T({{"card_limit", 2}, {"type", "title_2"}, {"highlight_limit", 0}, {"flat", true}}));
    G.your_collection = {Ref<CardArea>(yc)};

    Ref<Card> card(new Card(yc->T->x + yc->T->w / 2 - G.CARD_W / 2, yc->T->y, G.CARD_W, G.CARD_H, G.P_CARDS->get(K("empty")), card_center,
                            vl::T({{"bypass_discovery_center", true}, {"bypass_discovery_ui", true}})));
    Ref<Card> locked_card(new Card(yc->T->x + yc->T->w / 2 - G.CARD_W / 2, yc->T->y, G.CARD_W, G.CARD_H, G.P_CARDS->get(K("empty")),
                                   fld(card_center, "set").equals("Voucher") ? G.v_locked : G.j_locked));
    locked_card->remove_UI();
    locked_card->ID = card->ID;
    locked_card->states.click.can = false;
    card->states.visible = false;
    card->no_ui = true;

    const int id = card->ID;
    real_event(0, [id] {
        G.OVERLAY_MENU->config->set("joker_unlock_table", (double)id);
        return true;
    });
    auto juice = [id, locked_card](double delay, float scale, float rot, float per) {
        real_event(delay, [id, locked_card, scale, rot, per] {
            if (unlock_shown(id)) {
                locked_card->juice_up_card(scale, &rot);
                play_sound("cancel", per);
            }
            return true;
        });
    };
    juice(0.6, 0.3f, 0.2f, 0.8f);
    juice(1.15, 0.45f, 0.3f, 0.92f);
    juice(1.8, 0.6f, 0.4f, 1.03f);
    real_event(2.3, [id, locked_card] {
        if (unlock_shown(id)) locked_card->start_dissolve(A({col("BLACK")}));
        return true;
    });
    real_event(2.7, [id, card] {
        if (unlock_shown(id)) {
            card->start_materialize(A({col("BLUE")}), true);
            play_sound("crumple1", 0.8f, 1);
        }
        return true;
    });
    real_event(2.78, [id, card] {
        if (unlock_shown(id)) {
            card->no_ui = false;
            play_sound("timpani", 0.8f, 1.8f);
        }
        return true;
    });
    real_event(2.95, [id] {
        if (unlock_shown(id)) play_sound("timpani", 1, 1.8f);
        return true;
    });

    yc->emplace(card.get());
    yc->emplace(locked_card.get());

    // (built in the order of the Lua table constructor: the DynaTexts, then the description)
    Value title = obj(new DynaText(vl::T({{"string", Value(A({fld(card_center, "set").equals("Voucher") ? localize("k_voucher") : localize("k_joker")}))},
                                          {"colours", Value(A({col("BLUE")}))},
                                          {"shadow", true},
                                          {"rotate", true},
                                          {"bump", true},
                                          {"pop_in", 0.3},
                                          {"pop_in_rate", 2},
                                          {"scale", 1.2}})));
    Value unlocked = obj(new DynaText(vl::T({{"string", Value(A({localize("k_unlocked_ex")}))},
                                             {"colours", Value(A({col("RED")}))},
                                             {"shadow", true},
                                             {"rotate", true},
                                             {"bump", true},
                                             {"pop_in", 0.6},
                                             {"pop_in_rate", 2},
                                             {"scale", 0.8}})));
    Value desc = desc_from_rows(card->generate_UIBox_unlock_table(true)->get(K("main")));

    TableRef contents = A({node(
        UIT_R, vl::T({{"align", "cm"}, {"padding", 0}}),
        A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.1}}),
                A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.1}}),
                        A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}}), A({node(UIT_O, vl::T({{"object", title}}))})),
                           node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}}), A({node(UIT_O, vl::T({{"object", unlocked}}))}))})),
                   node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}, {"draw_layer", 1}}), A({node(UIT_O, vl::T({{"object", obj(yc)}}))})),
                   node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.2}}),
                        A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.05}, {"emboss", 0.05}, {"colour", col("WHITE")}, {"r", 0.1}}),
                                A({desc}))}))}))}))});
    return create_UIBox_generic_options(vl::T({{"padding", 0},
                                               {"back_label", localize("b_continue")},
                                               {"no_pip", true},
                                               {"snap_back", true},
                                               {"back_func", "continue_unlock"},
                                               {"minw", 4.5},
                                               {"contents", Value(contents)}}));
}

TableRef create_UIBox_deck_unlock(const Value &deck_center) {
    G.GAME->set("viewed_back", obj(new Back(deck_center)));
    Back *viewed_back = G.GAME->get(K("viewed_back")).as<Back>();
    CardArea *area = new CardArea(G.ROOM->T->x + 0.2f * G.ROOM->T->w / 2, G.ROOM->T->h, 1.2f * G.CARD_W, 1.2f * G.CARD_H,
                                  vl::T({{"card_limit", 52}, {"type", "deck"}, {"highlight_limit", 0}}));
    for (int i = 1; i <= 52; i++) {
        Card *card = new Card(G.ROOM->T->x + 0.2f * G.ROOM->T->w / 2, G.ROOM->T->h, G.CARD_W * 1.2f, G.CARD_H * 1.2f,
                              pseudorandom_element(Value(G.P_CARDS)), G.P_CENTERS->get(K("c_base")),
                              vl::T({{"bypass_back", fld(deck_center, "pos")}, {"playing_card", i}, {"viewed_back", true}}));
        area->emplace(card);
        // (card[deck_center.key] = true: read by nothing)
        card->sprite_facing = "back";
        card->facing = "back";
    }
    TableRef deck_criteria = new_table();
    const Value uc = fld(deck_center, "unlock_condition");
    auto criteria = [&](const char *key, const Value &vars) {
        localize(vl::T({{"type", "descriptions"},
                        {"key", key},
                        {"set", "Other"},
                        {"nodes", Value(deck_criteria)},
                        {"vars", vars},
                        {"default_col", col("WHITE")},
                        {"shadow", true}}));
    };
    if (fld(uc, "type").equals("win_deck")) {
        Value other_name = localize(vl::T({{"type", "name_text"}, {"set", "Back"}, {"key", fld(uc, "deck")}}));
        criteria("deck_locked_win", Value(A({other_name})));
    } else if (fld(uc, "type").equals("win_stake")) {
        const int st = (int)fld(uc, "stake").num();
        Value other_name = localize(vl::T({{"type", "name_text"}, {"set", "Stake"}, {"key", fld(G.P_CENTER_POOLS->get(K("Stake")).get(st), "key")}}));
        TableRef vars = A({other_name});
        vars->set("colours", Value(A({get_stake_col(st)})));
        criteria("deck_locked_stake", Value(vars));
    } else if (fld(uc, "type").equals("discover_amount")) {
        criteria("deck_locked_discover", Value(A({fld(uc, "amount")})));
    }
    TableRef deck_criteria_cols = new_table();
    for (int k = 1; k <= deck_criteria->len(); k++) {
        if (k > 1) deck_criteria_cols->push(node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0}, {"minw", 0.1}}), new_table()));
        deck_criteria_cols->push(node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0}}), TableRef(deck_criteria->get(k).table())));
    }

    TableRef name_part = vl::T({{"string", localize(vl::T({{"type", "name_text"}, {"set", "Back"}, {"key", fld(deck_center, "key")}}))},
                                {"suffix", Value(" " + localize("k_unlocked_ex").to_string())},
                                {"outer_colour", ui_col("TEXT_LIGHT")}});
    Value title = obj(new DynaText(vl::T({{"string", Value(A({Value(name_part)}))},
                                          {"colours", Value(A({col("BLUE")}))},
                                          {"shadow", true},
                                          {"rotate", true},
                                          {"float", true},
                                          {"scale", 0.7},
                                          {"pop_in", 0.1}})));
    Value back_nodes = viewed_back->generate_UI(deck_center)->get(K("nodes"));
    TableRef contents = A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}}), A({node(UIT_O, vl::T({{"object", title}}))})),
                           node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}}), deck_criteria_cols),
                           node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.2}, {"colour", col("BLACK")}, {"r", 0.2}}),
                                A({node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0}}), A({node(UIT_O, vl::T({{"object", obj(area)}}))})),
                                   node(UIT_C, vl::T({{"align", "cm"}, {"r", 0.2}, {"colour", col("WHITE")}, {"emboss", 0.05}, {"padding", 0.2}, {"minw", 4}}),
                                        A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}}), TableRef(back_nodes.table()))}))}))});
    return create_UIBox_generic_options(vl::T({{"back_label", localize("b_continue")},
                                               {"no_pip", true},
                                               {"snap_back", true},
                                               {"back_func", "continue_unlock"},
                                               {"minw", 7},
                                               {"contents", Value(contents)}}));
}

}  // namespace vl
