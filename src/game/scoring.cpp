// functions/state_events.lua (playing and discarding hands, evaluate_play, end_round,
// evaluate_round) and the common_events.lua helpers they use (update_hand_text, eval_card,
// card_eval_status_text, the ease_* HUD counters, add_round_eval_row...).
#include <algorithm>
#include <cmath>
#include <string>

#include "core/lua_sort.h"
#include "core/object.h"
#include "engine/controller.h"
#include "engine/event.h"
#include "engine/particles.h"
#include "engine/random.h"
#include "engine/text.h"
#include "engine/ui.h"
#include "game/blind.h"
#include "game/button_callbacks.h"
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
inline Table *gt(const char *k) { return G.GAME->get(Value(intern(k))).table(); }
inline Value gv(const char *k) { return G.GAME->get(Value(intern(k))); }
UIBox *hud() { return dynamic_cast<UIBox *>(G.HUD.get()); }
UIBox *hud_blind() { return dynamic_cast<UIBox *>(G.HUD_blind.get()); }
UIBox *round_eval() { return dynamic_cast<UIBox *>(G.round_eval.get()); }
Blind *game_blind() { return gv("blind").as<Blind>(); }
Value obj(Object *o) { return Value(o); }
Table *current_hand() { return gt("current_round")->get(K("current_hand")).table(); }
DynaText *dyna_of(UIElement *e) { return e ? dynamic_cast<DynaText *>(e->config->get(K("object")).obj()) : nullptr; }
Value colour(const char *n) { return G.colour(n); }

// G.hand_text_area.<name>
UIElement *hta(const char *name) {
    static const struct { const char *k, *id; bool blind; } map[] = {
        {"chips", "hand_chips", false},        {"mult", "hand_mult", false},       {"ante", "ante_UI_count", false},
        {"round", "round_UI_count", false},    {"chip_total", "hand_chip_total", false}, {"handname", "hand_name", false},
        {"hand_level", "hand_level", false},   {"game_chips", "chip_UI_count", false},   {"blind_chips", "HUD_blind_count", true}};
    for (auto &m : map)
        if (std::string_view(m.k) == name) {
            UIBox *b = m.blind ? hud_blind() : hud();
            return b ? b->get_UIE_by_ID(m.id) : nullptr;
        }
    return nullptr;
}

std::vector<Card *> cards_of(const std::vector<CardRef> &v) {
    std::vector<Card *> r;
    for (auto &c : v) r.push_back(c.get());
    return r;
}
TableRef cards_table(const std::vector<CardRef> &v) {
    TableRef t = new_table();
    for (auto &c : v) t->push(obj(c.get()));
    return t;
}
std::vector<Card *> table_cards(const Value &t) {
    std::vector<Card *> r;
    if (Table *tt = t.table())
        for (int i = 1; i <= tt->len(); i++)
            if (Card *c = tt->get(i).as<Card>()) r.push_back(c);
    return r;
}
bool non_empty(const Value &t) {
    bool any = false;
    if (Table *tt = t.table()) tt->each([&](const Value &, const Value &) { any = true; });
    return any;
}
std::string num_str(const Value &v) { return v.to_string(); }

void juice_hud_blind() {
    if (UIBox *hb = hud_blind()) {
        float z = 0;
        hb->get_UIE_by_ID("HUD_blind_debuff_1")->juice_up(0.3f, &z);
        hb->get_UIE_by_ID("HUD_blind_debuff_2")->juice_up(0.3f, &z);
    }
    if (Blind *b = game_blind()) b->juice_up_blind();
    EventConfig e = ev_after(0.06 * G.SETTINGS->get(K("GAMESPEED")).num(), [] {
        play_sound("tarot2", 0.76f, 0.4f);
        return true;
    });
    e.blockable = false;
    e.blocking = false;
    G.E_MANAGER->add_event(e);
    play_sound("tarot2", 1, 0.4f);
}
}  // namespace

// ------------------------------------------------------------------ misc_functions.lua
double mod_chips(double chips) {
    if (fld(gv("modifiers"), "chips_dollar_cap").truthy()) chips = std::min(chips, std::max(gv("dollars").num(), 0.0));
    return chips;
}
double mod_mult(double mult) { return mult; }

// ------------------------------------------------------------------ common_events.lua: HUD counters
namespace {
// the popup next to a HUD counter
void counter_popup(UIElement *ui, const std::string &text, const Value &col, double scale = 0.8) {
    attention_text(vl::T({{"text", Value(text)}, {"scale", scale}, {"hold", 0.7}, {"cover", obj(ui ? ui->parent : nullptr)}, {"cover_colour", col}, {"align", "cm"}}));
}
void dyna_update(UIElement *e) {
    if (DynaText *d = dyna_of(e)) d->update(0);
}
}  // namespace

void ease_chips(double mod) {
    G.E_MANAGER->add_event(ev_immediate([mod] {
        UIElement *chip_UI = hud()->get_UIE_by_ID("chip_UI_count");
        EventConfig e;
        e.trigger = Trigger::Ease;
        e.blockable = false;
        e.ref = ValueRef::field(G.GAME, Value("chips"));
        e.ease_to = mod;
        e.delay = 0.3;
        e.ease_func = [](double t) { return std::floor(t); };
        G.E_MANAGER->add_event(e);
        chip_UI->juice_up();
        play_sound("chips2");
        return true;
    }));
}

void ease_dollars(double mod, bool instant) {
    auto _mod = [mod] {
        UIElement *dollar_UI = hud()->get_UIE_by_ID("dollar_text_UI");
        std::string text = "+" + localize("$").to_string();
        Value col = colour("MONEY");
        if (mod < 0) {
            text = "-" + localize("$").to_string();
            col = colour("RED");
        } else {
            inc_career_stat("c_dollars_earned", mod);
        }
        G.GAME->set("dollars", gv("dollars").num() + mod);
        check_and_set_high_score("most_money", gv("dollars"));
        check_for_unlock(vl::T({{"type", "money"}}));
        dyna_update(dollar_UI);
        hud()->recalculate();
        counter_popup(dollar_UI, text + Value(std::fabs(mod)).to_string(), col);
        play_sound("coin1");
    };
    if (instant) _mod();
    else
        G.E_MANAGER->add_event(ev_immediate([_mod] {
            _mod();
            return true;
        }));
}

void ease_discard(double mod, bool instant, bool silent) {
    auto _mod = [mod, silent] {
        Table *cr = gt("current_round");
        double left = cr->get(K("discards_left")).num();
        if (std::fabs(std::max(left, mod)) == 0) return;
        UIElement *discard_UI = hud()->get_UIE_by_ID("discard_UI_count");
        double m = std::max(-left, mod);
        std::string text = "+";
        Value col = colour("GREEN");
        if (m < 0) {
            text = "";
            col = colour("RED");
        }
        cr->set("discards_left", left + m);
        dyna_update(discard_UI);
        hud()->recalculate();
        counter_popup(discard_UI, text + Value(m).to_string(), col);
        if (!silent) play_sound("chips2");
    };
    if (instant) _mod();
    else
        G.E_MANAGER->add_event(ev_immediate([_mod] {
            _mod();
            return true;
        }));
}

void ease_hands_played(double mod, bool instant) {
    auto _mod = [mod] {
        UIElement *hand_UI = hud()->get_UIE_by_ID("hand_UI_count");
        std::string text = "+";
        Value col = colour("GREEN");
        if (mod < 0) {
            text = "";
            col = colour("RED");
        }
        Table *cr = gt("current_round");
        cr->set("hands_left", cr->get(K("hands_left")).num() + mod);
        dyna_update(hand_UI);
        hud()->recalculate();
        counter_popup(hand_UI, text + Value(mod).to_string(), col);
        play_sound("chips2");
    };
    if (instant) _mod();
    else
        G.E_MANAGER->add_event(ev_immediate([_mod] {
            _mod();
            return true;
        }));
}

void highlight_card(Card *card, double percent, const char *dir) {
    bool highlight = true;
    if (dir && std::string_view(dir) == "down") {
        percent = 1 - percent;
        highlight = false;
    }
    CardRef c(card);
    EventConfig e;
    e.trigger = Trigger::Before;
    e.delay = 0.1;
    e.func = [c, highlight, percent] {
        c->highlight(highlight);
        play_sound("cardSlide1", (float)(0.85 + percent * 0.2));
        return true;
    };
    G.E_MANAGER->add_event(e);
}

void play_area_status_text(const Value &text, bool silent, double delay_) {
    EventConfig e;
    e.trigger = delay_ == 0 ? Trigger::Immediate : Trigger::Before;
    e.delay = delay_;
    e.func = [text, silent] {
        attention_text(vl::T({{"scale", 0.9}, {"text", text}, {"hold", 0.9}, {"align", "tm"}, {"major", obj(G.play.get())},
                              {"offset", Value(vl::T({{"x", 0}, {"y", -1}}))}}));
        if (!silent) {
            G.ROOM_jiggle += 2;
            play_sound("cardFan2");
        }
        return true;
    };
    G.E_MANAGER->add_event(e);
}

void juice_card(Card *card) {
    CardRef c(card);
    G.E_MANAGER->add_event(ev_immediate([c] {
        c->juice_up_card(0.7f);
        return true;
    }));
}

void update_hand_text(const TableRef &config, const TableRef &vals) {
    EventConfig e;
    e.trigger = Trigger::Before;
    e.blockable = !config->get(K("immediate")).truthy();
    e.delay = config->get(K("delay")).is_num() ? config->get(K("delay")).num() : 0.8;
    e.func = [config, vals] {
        Table *ch = current_hand();
        Value col = colour("GREEN");
        auto delta_text = [&](const Value &nv, const Value &ov) {
            double delta = (nv.is_num() && ov.is_num()) ? nv.num() - ov.num() : 0;
            std::string d;
            if (delta < 0) {
                d = Value(delta).to_string();
                col = colour("RED");
            } else if (delta > 0) {
                d = "+" + Value(delta).to_string();
            } else {
                d = Value(delta).to_string();
            }
            if (nv.is_str()) d = std::string(nv.str());
            return d;
        };
        Value vc = vals->get(K("chips"));
        if (vc.truthy() && !(ch->get(K("chips")) == vc)) {
            std::string d = delta_text(vc, ch->get(K("chips")));
            ch->set("chips", vc);
            if (UIElement *u = hta("chips")) u->update(0);
            if (vals->get(K("StatusText")).truthy())
                attention_text(vl::T({{"text", Value(d)}, {"scale", 0.8}, {"hold", 1}, {"cover", obj(hta("chips")->parent)},
                                      {"cover_colour", Value(mix_colours(colour("CHIPS"), col, 0.1f))}, {"emboss", 0.05}, {"align", "cm"}, {"cover_align", "cr"}}));
        }
        Value vm = vals->get(K("mult"));
        if (vm.truthy() && !(ch->get(K("mult")) == vm)) {
            std::string d = delta_text(vm, ch->get(K("mult")));
            ch->set("mult", vm);
            if (UIElement *u = hta("mult")) u->update(0);
            if (vals->get(K("StatusText")).truthy())
                attention_text(vl::T({{"text", Value(d)}, {"scale", 0.8}, {"hold", 1}, {"cover", obj(hta("mult")->parent)},
                                      {"cover_colour", Value(mix_colours(colour("MULT"), col, 0.1f))}, {"emboss", 0.05}, {"align", "cm"}, {"cover_align", "cl"}}));
            if (!G.TAROT_INTERRUPT) hta("mult")->juice_up();
        }
        Value hn = vals->get(K("handname"));
        if (hn.truthy() && !(ch->get(K("handname")) == hn)) {
            ch->set("handname", hn);
            if (!config->get(K("nopulse")).truthy())
                if (DynaText *d = dyna_of(hta("handname"))) d->pulse(0.2);
        }
        Value ct = vals->get(K("chip_total"));
        if (ct.truthy()) {
            ch->set("chip_total", ct);
            if (DynaText *d = dyna_of(hta("chip_total"))) d->pulse(0.5);
        }
        Value lv = vals->get(K("level"));
        if (lv.truthy()) {
            std::string lvl = " " + localize("k_lvl").to_string() + lv.to_string();
            if (!ch->get(K("hand_level")).equals(lvl)) {
                if (lv.equals("")) {
                    ch->set("hand_level", lv);
                } else {
                    ch->set("hand_level", Value(lvl));
                    UIElement *hl = hta("hand_level");
                    Table *levels = colour("HAND_LEVELS").table();
                    hl->config->set("colour", lv.is_num() ? levels->get(std::min(lv.num(), 7.0)) : levels->get(1));
                    hl->juice_up();
                }
            }
        }
        Value snd = config->get(K("sound"));
        if (snd.truthy() && !config->get(K("modded")).truthy())
            play_sound(std::string(snd.str()).c_str(), config->get(K("pitch")).is_num() ? (float)config->get(K("pitch")).num() : 1,
                       config->get(K("volume")).is_num() ? (float)config->get(K("volume")).num() : 1);
        if (config->get(K("modded")).truthy()) juice_hud_blind();
        return true;
    };
    G.E_MANAGER->add_event(e);
}

void level_up_hand(Moveable *card, const std::string &hand, bool instant, double amount) {
    Table *h = gt("hands")->get(Value(hand)).table();
    h->set("level", std::max(0.0, h->get(K("level")).num() + amount));
    h->set("mult", std::max(h->get(K("s_mult")).num() + h->get(K("l_mult")).num() * (h->get(K("level")).num() - 1), 1.0));
    h->set("chips", std::max(h->get(K("s_chips")).num() + h->get(K("l_chips")).num() * (h->get(K("level")).num() - 1), 0.0));
    if (!instant) {
        Ref<Moveable> c(card);
        auto juice = [c] {
            play_sound("tarot1");
            float r = 0.5f;
            if (c) c->juice_up(0.8f, &r);
        };
        G.E_MANAGER->add_event(ev_after(0.2, [juice] {
            juice();
            G.TAROT_INTERRUPT_PULSE = true;
            return true;
        }));
        update_hand_text(vl::T({{"delay", 0}}), vl::T({{"mult", h->get(K("mult"))}, {"StatusText", true}}));
        G.E_MANAGER->add_event(ev_after(0.9, [juice] {
            juice();
            return true;
        }));
        update_hand_text(vl::T({{"delay", 0}}), vl::T({{"chips", h->get(K("chips"))}, {"StatusText", true}}));
        G.E_MANAGER->add_event(ev_after(0.9, [juice] {
            juice();
            G.TAROT_INTERRUPT_PULSE = false;
            return true;
        }));
        update_hand_text(vl::T({{"sound", "button"}, {"volume", 0.7}, {"pitch", 0.9}, {"delay", 0}}), vl::T({{"level", h->get(K("level"))}}));
        delay(1.3);
    }
    std::string hs = hand;
    G.E_MANAGER->add_event(ev_immediate([hs] {
        check_for_unlock(vl::T({{"type", "upgrade_hand"}, {"hand", Value(hs)}, {"level", fld(gt("hands")->get(Value(hs)), "level")}}));
        return true;
    }));
}

TableRef eval_card(Card *card, const TableRef &context) {
    TableRef ret = new_table();
    if (context->get(K("repetition_only")).truthy()) {
        Value seals = card->calculate_seal(context);
        if (seals.truthy()) ret->set("seals", seals);
        return ret;
    }
    Object *area = context->get(K("cardarea")).obj();
    if (G.play && area == G.play.get()) {
        double chips = card->get_chip_bonus();
        if (chips > 0) ret->set("chips", chips);
        double mult = card->get_chip_mult();
        if (mult > 0) ret->set("mult", mult);
        double x_mult = card->get_chip_x_mult();
        if (x_mult > 0) ret->set("x_mult", x_mult);
        double p_dollars = card->get_p_dollars();
        if (p_dollars > 0) ret->set("p_dollars", p_dollars);
        Value jokers = card->calculate_joker(context);
        if (jokers.truthy()) ret->set("jokers", jokers);
        Value edition = card->get_edition();
        if (edition.truthy()) ret->set("edition", edition);
    }
    if (G.hand && area == G.hand.get()) {
        double h_mult = card->get_chip_h_mult();
        if (h_mult > 0) ret->set("h_mult", h_mult);
        double h_x_mult = card->get_chip_h_x_mult();
        if (h_x_mult > 0) ret->set("x_mult", h_x_mult);
        Value jokers = card->calculate_joker(context);
        if (jokers.truthy()) ret->set("jokers", jokers);
    }
    if ((G.jokers && area == G.jokers.get()) || (G.consumeables && context->get(K("card")).obj() == G.consumeables.get())) {
        Value jokers;
        if (context->get(K("edition")).truthy()) jokers = card->get_edition();
        else if (Card *oj = context->get(K("other_joker")).as<Card>()) jokers = oj->calculate_joker(context);
        else jokers = card->calculate_joker(context);
        if (jokers.truthy()) ret->set("jokers", jokers);
    }
    return ret;
}

void card_eval_status_text(Card *card, const char *eval_type, double amt, double percent, const char *dir, const TableRef &extra) {
    if (percent < 0) percent = 0.9 + 0.2 * lua_math.random();
    if (dir && std::string_view(dir) == "down") percent = 1 - percent;
    if (extra && extra->get(K("focus")).as<Card>()) card = extra->get(K("focus")).as<Card>();
    std::string text;
    const char *sound = nullptr;
    float volume = 1;
    const char *card_aligned = "bm";
    double y_off = 0.15 * G.CARD_H;
    const bool in_jokers = G.jokers && card->area == G.jokers.get(), in_cons = G.consumeables && card->area == G.consumeables.get();
    if (in_jokers || in_cons) {
        y_off = 0.05 * card->T->h;
    } else if ((G.hand && card->area == G.hand.get()) || (G.play && card->area == G.play.get())) {
        y_off = -0.05 * G.CARD_H;
        card_aligned = "tm";
    }
    // (card.jimbo: the tutorial character)
    double scale = -1;
    double delay_ = 0.65;
    Value col = extra && extra->get(K("colour")).truthy() ? extra->get(K("colour")) : colour("FILTER");
    auto var = [](const char *key, double a) {
        return localize(vl::T({{"type", "variable"}, {"key", key}, {"vars", Value(A({a}))}})).to_string();
    };
    const std::string et(eval_type);
    if (et == "debuff") {
        sound = "cancel";
        amt = 1;
        col = colour("RED");
        scale = 0.6;
        text = localize("k_debuffed").to_string();
    } else if (et == "chips") {
        sound = "chips1";
        col = colour("CHIPS");
        text = var("a_chips", amt);
        delay_ = 0.6;
    } else if (et == "mult") {
        sound = "multhit1";
        text = var("a_mult", amt);
        col = colour("MULT");
        scale = 0.7;
    } else if (et == "x_mult" || et == "h_x_mult") {
        sound = "multhit2";
        volume = 0.7f;
        text = var("a_xmult", amt);
        col = colour("XMULT");
        scale = 0.7;
    } else if (et == "h_mult") {
        sound = "multhit1";
        text = var("a_mult", amt);
        col = colour("MULT");
        scale = 0.7;
    } else if (et == "dollars") {
        sound = "coin3";
        text = (amt < -0.01 ? "-" : "") + localize("$").to_string() + Value(std::fabs(amt)).to_string();
        col = amt < -0.01 ? colour("RED") : colour("MONEY");
    } else if (et == "swap") {
        sound = "generic1";
        text = localize("k_swapped_ex").to_string();
        col = colour("PURPLE");
    } else if (et == "extra" || et == "jokers") {
        const bool ed = extra->get(K("edition")).truthy();
        sound = ed ? "foil2" : extra->get(K("mult_mod")).truthy() ? "multhit1" : extra->get(K("Xmult_mod")).truthy() ? "multhit2" : "generic1";
        if (ed) col = colour("DARK_EDITION");
        volume = ed ? 0.3f : std::string_view(sound) == "multhit2" ? 0.7f : 1;
        delay_ = extra->get(K("delay")).is_num() ? extra->get(K("delay")).num() : 0.75;
        amt = 1;
        if (extra->get(K("message")).truthy()) text = extra->get(K("message")).to_string();
        if (!ed && (extra->get(K("mult_mod")).truthy() || extra->get(K("Xmult_mod")).truthy())) col = colour("MULT");
        scale = 0.7;
        if (extra->get(K("chip_mod")).truthy()) col = colour("CHIPS");
        else if (extra->get(K("swap")).truthy()) col = colour("PURPLE");
    }
    delay_ *= 1.25;
    if (amt > 0 || amt < 0) {
        CardRef c(card);
        TableRef ex = extra;
        auto show = [c, text, scale, delay_, col, card_aligned, y_off, sound, percent, volume, ex] {
            attention_text(vl::T({{"text", Value(text)}, {"scale", scale >= 0 ? scale : 1.0}, {"hold", delay_ - 0.2}, {"backdrop_colour", col},
                                  {"align", card_aligned}, {"major", obj(c.get())}, {"offset", Value(vl::T({{"x", 0}, {"y", y_off}}))}}));
            if (sound) play_sound(sound, (float)(0.8 + percent * 0.2), volume);
            if (!ex || !ex->get(K("no_juice")).truthy()) {
                float r = 0.1f;
                c->juice_up(0.6f, &r);
                G.ROOM_jiggle += 0.7;
            }
        };
        if (extra && extra->get(K("instant")).truthy()) {
            show();
        } else {
            EventConfig e;
            e.trigger = Trigger::Before;
            e.delay = delay_;
            e.func = [show] {
                show();
                return true;
            };
            G.E_MANAGER->add_event(e);
        }
    }
    // TODO(run): extra.playing_cards_created (playing_card_joker_effects)
}

// ------------------------------------------------------------------ G.FUNCS: discarding and playing
void discard_cards_from_highlighted(bool hook) {
    stop_use();
    G.CONTROLLER->interrupt.focus = true;
    G.CONTROLLER->save_cardarea_focus("hand");
    for (auto &c : G.playing_cards) c->ability->set("forced_selection", Value());
    // (G.card_area_focus_reset: controller focus restore)
    int highlighted_count = std::min((int)G.hand->highlighted.size(), (int)G.discard->config->get(K("card_limit")).num() - (int)G.play->cards.size());
    if (highlighted_count <= 0) return;
    update_hand_text(vl::T({{"immediate", true}, {"nopulse", true}, {"delay", 0}}), vl::T({{"mult", 0}, {"chips", 0}, {"level", ""}, {"handname", ""}}));
    lua_sort(G.hand->highlighted, [](const CardRef &a, const CardRef &b) { return a->T->x < b->T->x; });
    inc_career_stat("c_cards_discarded", highlighted_count);
    for (auto &j : std::vector<CardRef>(G.jokers->cards))
        j->calculate_joker(vl::T({{"pre_discard", true}, {"full_hand", Value(cards_table(G.hand->highlighted))}, {"hook", hook}}));
    std::vector<CardRef> cards, destroyed;
    std::vector<CardRef> hl(G.hand->highlighted);
    for (int i = 1; i <= highlighted_count; i++) {
        Card *c = hl[i - 1].get();
        c->calculate_seal(vl::T({{"discard", true}}));
        bool removed = false;
        for (auto &j : std::vector<CardRef>(G.jokers->cards)) {
            Value eval = j->calculate_joker(vl::T({{"discard", true}, {"other_card", obj(c)}, {"full_hand", Value(cards_table(hl))}}));
            if (eval.truthy()) {
                if (eval.get(K("remove")).truthy()) removed = true;
                card_eval_status_text(j.get(), "jokers", 0, 1, nullptr, TableRef(eval.table()));
            }
        }
        cards.push_back(hl[i - 1]);
        if (removed) {
            destroyed.push_back(hl[i - 1]);
            if (c->ability->get(K("name")).equals("Glass Card")) c->shatter();
            else c->start_dissolve();
        } else {
            c->ability->set("discarded", true);
            draw_card(G.hand.get(), G.discard.get(), i * 100.0 / highlighted_count, "down", false, c);
        }
    }
    if (!destroyed.empty())
        for (auto &j : std::vector<CardRef>(G.jokers->cards))
            eval_card(j.get(), vl::T({{"cardarea", obj(G.jokers.get())}, {"remove_playing_cards", true}, {"removed", Value(cards_table(destroyed))}}));
    Table *cd = gt("round_scores")->get(K("cards_discarded")).table();
    cd->set("amt", cd->get(K("amt")).num() + cards.size());
    check_for_unlock(vl::T({{"type", "discard_custom"}, {"cards", Value(cards_table(cards))}}));
    if (!hook) {
        Value dc = fld(gv("modifiers"), "discard_cost");
        if (dc.truthy()) ease_dollars(-dc.num());
        ease_discard(-1);
        Table *cr = gt("current_round");
        cr->set("discards_used", cr->get(K("discards_used")).num() + 1);
        G.STATE = DRAW_TO_HAND;
        G.E_MANAGER->add_event(ev_immediate([] {
            G.STATE_COMPLETE = false;
            return true;
        }));
    }
}

void play_cards_from_highlighted() {
    if (!G.play->cards.empty()) return;
    stop_use();
    game_blind()->triggered = Value(false);
    G.CONTROLLER->interrupt.focus = true;
    G.CONTROLLER->save_cardarea_focus("hand");
    for (auto &c : G.playing_cards) c->ability->set("forced_selection", Value());
    lua_sort(G.hand->highlighted, [](const CardRef &a, const CardRef &b) { return a->T->x < b->T->x; });
    G.E_MANAGER->add_event(ev_immediate([] {
        G.STATE = HAND_PLAYED;
        G.STATE_COMPLETE = true;
        return true;
    }));
    inc_career_stat("c_cards_played", (double)G.hand->highlighted.size());
    inc_career_stat("c_hands_played", 1);
    ease_hands_played(-1);
    delay(0.4);
    std::vector<CardRef> hl(G.hand->highlighted);
    for (size_t i = 1; i <= hl.size(); i++) {
        Card *c = hl[i - 1].get();
        if (c->is_face()) inc_career_stat("c_face_cards_played", 1);
        c->base->set("times_played", c->base->get(K("times_played")).num() + 1);
        c->ability->set("played_this_ante", true);
        Table *cp = gt("round_scores")->get(K("cards_played")).table();
        cp->set("amt", cp->get(K("amt")).num() + 1);
        draw_card(G.hand.get(), G.play.get(), i * 100.0 / hl.size(), "up", false, c);
    }
    check_for_unlock(vl::T({{"type", "run_card_replays"}}));
    if (game_blind()->press_play()) {
        G.E_MANAGER->add_event(ev_immediate([] {
            juice_hud_blind();
            return true;
        }));
        delay(0.4);
    }
    G.E_MANAGER->add_event(ev_immediate([] {
        check_for_unlock(vl::T({{"type", "hand_contents"}, {"cards", Value(cards_table(G.play->cards))}}));
        G.E_MANAGER->add_event(ev_immediate([] {
            evaluate_play();
            return true;
        }));
        G.E_MANAGER->add_event(ev_after(0.1, [] {
            check_for_unlock(vl::T({{"type", "play_all_hearts"}}));
            draw_from_play_to_discard();
            G.GAME->set("hands_played", gv("hands_played").num() + 1);
            Table *cr = gt("current_round");
            cr->set("hands_played", cr->get(K("hands_played")).num() + 1);
            return true;
        }));
        G.E_MANAGER->add_event(ev_immediate([] {
            G.STATE_COMPLETE = false;
            return true;
        }));
        return true;
    }));
}

void evaluate_play() {
    PokerHandInfo info = get_poker_hand_info(cards_of(G.play->cards));
    const std::string text = info.text, disp_text = info.loc_disp_text.to_string();
    TableRef poker_hands = info.poker_hands;
    Table *ht = gt("hands")->get(Value(text)).table();
    ht->set("played", ht->get(K("played")).num() + 1);
    ht->set("played_this_round", ht->get(K("played_this_round")).num() + 1);
    G.GAME->set("last_hand_played", Value(text));
    set_hand_usage(text);
    ht->set("visible", true);

    // the scoring hand, plus the pure bonus (Stone) cards
    std::vector<Card *> scoring_hand = table_cards(Value(info.scoring_hand));
    std::vector<Card *> pures;
    const bool splash = !find_joker("Splash").empty();
    for (size_t i = 0; i < G.play->cards.size(); i++) {
        Card *pc = G.play->cards[i].get();
        if (splash) {
            if (i < scoring_hand.size()) scoring_hand[i] = pc;
            else scoring_hand.push_back(pc);
        } else if (pc->ability->get(K("effect")).equals("Stone Card")) {
            if (std::find(scoring_hand.begin(), scoring_hand.end(), pc) == scoring_hand.end()) pures.push_back(pc);
        }
    }
    for (Card *p : pures) scoring_hand.push_back(p);
    lua_sort(scoring_hand, [](Card *a, Card *b) { return a->T->x < b->T->x; });
    TableRef sh_t = new_table();
    for (Card *c : scoring_hand) sh_t->push(obj(c));
    TableRef full_hand = cards_table(G.play->cards);
    delay(0.2);
    for (size_t i = 1; i <= scoring_hand.size(); i++) highlight_card(scoring_hand[i - 1], (i - 0.999) / 5, "up");

    double percent = 0.3;
    const double percent_delta = 0.08;
    const bool new_name = !current_hand()->get(K("handname")).equals(disp_text);
    if (new_name) delay(0.3);
    {
        TableRef cfg = vl::T({{"volume", 0.4}, {"immediate", true}, {"delay", new_name ? 0.4 : 0}});
        if (new_name) cfg->set("sound", "button");
        update_hand_text(cfg, vl::T({{"handname", Value(disp_text)}, {"level", ht->get(K("level"))}, {"mult", ht->get(K("mult"))}, {"chips", ht->get(K("chips"))}}));
    }
    auto ctx = [&](std::initializer_list<std::pair<const char *, Value>> kv) {
        TableRef c = vl::T({{"full_hand", Value(full_hand)}, {"scoring_hand", Value(sh_t)}, {"scoring_name", Value(text)}, {"poker_hands", Value(poker_hands)}});
        for (auto &p : kv) c->set(p.first, p.second);
        return c;
    };
    double mult = 0, hand_chips = 0;
    Blind *blind = game_blind();
    std::vector<Card *> jokers = cards_of(G.jokers->cards);

    if (!blind->debuff_hand(cards_of(G.play->cards), poker_hands, text, false)) {
        mult = mod_mult(ht->get(K("mult")).num());
        hand_chips = mod_chips(ht->get(K("chips")).num());
        check_for_unlock(vl::T({{"type", "hand"}, {"handname", Value(text)}, {"disp_text", Value(info.disp_text)}, {"scoring_hand", Value(sh_t)},
                                {"full_hand", Value(full_hand)}}));
        delay(0.4);
        Value fuhl = gv("first_used_hand_level");
        if (fuhl.truthy() && fuhl.num() > 0) {
            level_up_hand(G.deck->cards.empty() ? nullptr : G.deck->cards[0].get(), text, false, fuhl.num());
            G.GAME->set("first_used_hand_level", Value());
        }
        for (Card *j : jokers) {
            TableRef effects = eval_card(j, ctx({{"cardarea", obj(G.jokers.get())}, {"before", true}}));
            if (effects->get(K("jokers")).truthy()) {
                card_eval_status_text(j, "jokers", 0, percent, nullptr, TableRef(effects->get(K("jokers")).table()));
                percent += percent_delta;
                if (effects->get(K("jokers")).get(K("level_up")).truthy()) level_up_hand(j, text);
            }
        }
        mult = mod_mult(ht->get(K("mult")).num());
        hand_chips = mod_chips(ht->get(K("chips")).num());
        bool modded = false;
        blind->modify_hand(cards_of(G.play->cards), poker_hands, text, mult, hand_chips, modded);
        mult = mod_mult(mult);
        hand_chips = mod_chips(hand_chips);
        if (modded) update_hand_text(vl::T({{"sound", "chips2"}, {"modded", modded}}), vl::T({{"chips", hand_chips}, {"mult", mult}}));

        for (Card *sc : scoring_hand) {
            if (!sc->ability->get(K("effect")).equals("Stone Card")) {
                Table *cp = gt("cards_played")->get(sc->base->get(K("value"))).table();
                cp->set("total", cp->get(K("total")).num() + 1);
                cp->get(K("suits")).table()->set(sc->base->get(K("suit")), true);
            }
            if (sc->debuff) {
                blind->triggered = Value(true);
                G.E_MANAGER->add_event(ev_immediate([] {
                    if (UIBox *hb = hud_blind()) {
                        float z = 0;
                        hb->get_UIE_by_ID("HUD_blind_debuff_1")->juice_up(0.3f, &z);
                        hb->get_UIE_by_ID("HUD_blind_debuff_2")->juice_up(0.3f, &z);
                    }
                    game_blind()->juice_up_blind();
                    return true;
                }));
                card_eval_status_text(sc, "debuff");
                continue;
            }
            // play doubling: the Red seal, then jokers
            std::vector<Value> reps{Value(1)};
            TableRef eval = eval_card(sc, ctx({{"repetition_only", true}, {"cardarea", obj(G.play.get())}, {"repetition", true}}));
            if (non_empty(Value(eval)))
                for (int h = 1; h <= (int)eval->get(K("seals")).get(K("repetitions")).num(); h++) reps.push_back(Value(eval));
            for (Card *j : jokers) {
                TableRef ev = eval_card(j, ctx({{"cardarea", obj(G.play.get())}, {"other_card", obj(sc)}, {"repetition", true}}));
                if (non_empty(Value(ev)) && ev->get(K("jokers")).truthy())
                    for (int h = 1; h <= (int)ev->get(K("jokers")).get(K("repetitions")).num(); h++) reps.push_back(Value(ev));
            }
            for (size_t rj = 0; rj < reps.size(); rj++) {
                percent += percent_delta;
                if (reps[rj].is_table()) {
                    Value r = reps[rj].get(K("jokers")).truthy() ? reps[rj].get(K("jokers")) : reps[rj].get(K("seals"));
                    card_eval_status_text(r.get(K("card")).as<Card>(), "jokers", 0, -1, nullptr, TableRef(r.table()));
                }
                std::vector<TableRef> effects{eval_card(sc, vl::T({{"cardarea", obj(G.play.get())}, {"full_hand", Value(full_hand)}, {"scoring_hand", Value(sh_t)},
                                                                   {"poker_hand", Value(text)}}))};
                for (Card *j : jokers) {
                    Value ev = j->calculate_joker(ctx({{"cardarea", obj(G.play.get())}, {"other_card", obj(sc)}, {"individual", true}}));
                    if (ev.truthy()) effects.push_back(TableRef(ev.table()));
                }
                sc->lucky_trigger = false;
                for (TableRef &ef : effects) {
                    Card *efc = ef->get(K("card")).as<Card>();
                    if (ef->get(K("chips")).truthy()) {
                        if (efc) juice_card(efc);
                        hand_chips = mod_chips(hand_chips + ef->get(K("chips")).num());
                        update_hand_text(vl::T({{"delay", 0}}), vl::T({{"chips", hand_chips}}));
                        card_eval_status_text(sc, "chips", ef->get(K("chips")).num(), percent);
                    }
                    if (ef->get(K("mult")).truthy()) {
                        if (efc) juice_card(efc);
                        mult = mod_mult(mult + ef->get(K("mult")).num());
                        update_hand_text(vl::T({{"delay", 0}}), vl::T({{"mult", mult}}));
                        card_eval_status_text(sc, "mult", ef->get(K("mult")).num(), percent);
                    }
                    if (ef->get(K("p_dollars")).truthy()) {
                        if (efc) juice_card(efc);
                        ease_dollars(ef->get(K("p_dollars")).num());
                        card_eval_status_text(sc, "dollars", ef->get(K("p_dollars")).num(), percent);
                    }
                    if (ef->get(K("dollars")).truthy()) {
                        if (efc) juice_card(efc);
                        ease_dollars(ef->get(K("dollars")).num());
                        card_eval_status_text(sc, "dollars", ef->get(K("dollars")).num(), percent);
                    }
                    if (Table *ex = ef->get(K("extra")).table()) {
                        if (efc) juice_card(efc);
                        bool em = false, ec = false;
                        if (ex->get(K("mult_mod")).truthy()) {
                            mult = mod_mult(mult + ex->get(K("mult_mod")).num());
                            em = true;
                        }
                        if (ex->get(K("chip_mod")).truthy()) {
                            hand_chips = mod_chips(hand_chips + ex->get(K("chip_mod")).num());
                            ec = true;
                        }
                        if (ex->get(K("swap")).truthy()) {
                            double old_mult = mult;
                            mult = mod_mult(hand_chips);
                            hand_chips = mod_chips(old_mult);
                            em = ec = true;
                        }
                        if (Func *f = ex->get(K("func")).as<Func>()) f->call();
                        update_hand_text(vl::T({{"delay", 0}}), vl::T({{"chips", ec ? Value(hand_chips) : Value(false)}, {"mult", em ? Value(mult) : Value(false)}}));
                        card_eval_status_text(sc, "extra", 0, percent, nullptr, TableRef(ex));
                    }
                    if (ef->get(K("x_mult")).truthy()) {
                        if (efc) juice_card(efc);
                        mult = mod_mult(mult * ef->get(K("x_mult")).num());
                        update_hand_text(vl::T({{"delay", 0}}), vl::T({{"mult", mult}}));
                        card_eval_status_text(sc, "x_mult", ef->get(K("x_mult")).num(), percent);
                    }
                    if (Table *ed = ef->get(K("edition")).table()) {
                        Value cm = ed->get(K("chip_mod")), mm = ed->get(K("mult_mod")), xm = ed->get(K("x_mult_mod"));
                        hand_chips = mod_chips(hand_chips + (cm.truthy() ? cm.num() : 0));
                        mult = mult + (mm.truthy() ? mm.num() : 0);
                        mult = mod_mult(mult * (xm.truthy() ? xm.num() : 1));
                        TableRef v = new_table();
                        if (cm.truthy()) v->set("chips", hand_chips);
                        if (mm.truthy() || xm.truthy()) v->set("mult", mult);
                        update_hand_text(vl::T({{"delay", 0}}), v);
                        auto var = [](const char *key, const Value &a) { return localize(vl::T({{"type", "variable"}, {"key", key}, {"vars", Value(A({a}))}})); };
                        Value msg = cm.truthy() ? var("a_chips", cm) : mm.truthy() ? var("a_mult", mm) : var("a_xmult", xm);
                        TableRef ex = vl::T({{"message", msg}, {"colour", colour("DARK_EDITION")}, {"edition", true}});
                        if (cm.truthy()) ex->set("chip_mod", cm);
                        if (mm.truthy()) ex->set("mult_mod", mm);
                        if (xm.truthy()) ex->set("x_mult_mod", xm);
                        card_eval_status_text(sc, "extra", 0, percent, nullptr, ex);
                    }
                }
            }
        }

        delay(0.3);
        bool mod_percent = false;
        std::vector<CardRef> hand_cards(G.hand->cards);
        for (auto &hcr : hand_cards) {
            Card *hc = hcr.get();
            if (mod_percent) percent += percent_delta;
            mod_percent = false;
            std::vector<Value> reps{Value(1)};
            for (size_t rj = 0; rj < reps.size(); rj++) {
                if (reps[rj].is_table()) {
                    Value r = reps[rj].get(K("jokers")).truthy() ? reps[rj].get(K("jokers")) : reps[rj].get(K("seals"));
                    card_eval_status_text(r.get(K("card")).as<Card>(), "jokers", 0, -1, nullptr, TableRef(r.table()));
                    percent += percent_delta;
                }
                std::vector<TableRef> effects{eval_card(hc, ctx({{"cardarea", obj(G.hand.get())}}))};
                for (Card *j : jokers) {
                    Value ev = j->calculate_joker(ctx({{"cardarea", obj(G.hand.get())}, {"other_card", obj(hc)}, {"individual", true}}));
                    if (ev.truthy()) {
                        mod_percent = true;
                        effects.push_back(TableRef(ev.table()));
                    }
                }
                TableRef effects_t = new_table();
                for (auto &ef : effects) effects_t->push(Value(ef));
                if (reps[rj].is_num()) {
                    TableRef ev = eval_card(hc, ctx({{"repetition_only", true}, {"cardarea", obj(G.hand.get())}, {"repetition", true}, {"card_effects", Value(effects_t)}}));
                    if (non_empty(Value(ev)) && (non_empty(Value(effects[0])) || effects.size() > 1))
                        for (int h = 1; h <= (int)ev->get(K("seals")).get(K("repetitions")).num(); h++) reps.push_back(Value(ev));
                    for (Card *j : jokers) {
                        TableRef ej = eval_card(j, ctx({{"cardarea", obj(G.hand.get())}, {"other_card", obj(hc)}, {"repetition", true}, {"card_effects", Value(effects_t)}}));
                        if (non_empty(Value(ej)))
                            for (int h = 1; h <= (int)ej->get(K("jokers")).get(K("repetitions")).num(); h++) reps.push_back(Value(ej));
                    }
                }
                for (TableRef &ef : effects) {
                    if (Card *efc = ef->get(K("card")).as<Card>()) {
                        mod_percent = true;
                        CardRef r(efc);
                        G.E_MANAGER->add_event(ev_immediate([r] {
                            r->juice_up_card(0.7f);
                            return true;
                        }));
                    }
                    if (ef->get(K("dollars")).truthy()) {
                        ease_dollars(ef->get(K("dollars")).num());
                        card_eval_status_text(hc, "dollars", ef->get(K("dollars")).num(), percent);
                    }
                    if (ef->get(K("h_mult")).truthy()) {
                        mod_percent = true;
                        mult = mod_mult(mult + ef->get(K("h_mult")).num());
                        update_hand_text(vl::T({{"delay", 0}}), vl::T({{"mult", mult}}));
                        card_eval_status_text(hc, "h_mult", ef->get(K("h_mult")).num(), percent);
                    }
                    if (ef->get(K("x_mult")).truthy()) {
                        mod_percent = true;
                        mult = mod_mult(mult * ef->get(K("x_mult")).num());
                        update_hand_text(vl::T({{"delay", 0}}), vl::T({{"mult", mult}}));
                        card_eval_status_text(hc, "x_mult", ef->get(K("x_mult")).num(), percent);
                    }
                    if (ef->get(K("message")).truthy()) {
                        mod_percent = true;
                        update_hand_text(vl::T({{"delay", 0}}), vl::T({{"mult", mult}}));
                        card_eval_status_text(hc, "extra", 0, percent, nullptr, ef);
                    }
                }
            }
        }

        // joker effects (and the consumables' editions)
        percent += percent_delta;
        std::vector<Card *> jc = jokers;
        for (auto &c : G.consumeables->cards) jc.push_back(c.get());
        auto var = [](const char *key, const Value &a) { return localize(vl::T({{"type", "variable"}, {"key", key}, {"vars", Value(A({a}))}})); };
        for (Card *_card : jc) {
            TableRef edition_effects = eval_card(_card, ctx({{"cardarea", obj(G.jokers.get())}, {"edition", true}}));
            Table *eej = edition_effects->get(K("jokers")).table();
            if (eej) {
                eej->set("edition", true);
                if (eej->get(K("chip_mod")).truthy()) {
                    hand_chips = mod_chips(hand_chips + eej->get(K("chip_mod")).num());
                    update_hand_text(vl::T({{"delay", 0}}), vl::T({{"chips", hand_chips}}));
                    card_eval_status_text(_card, "jokers", 0, percent, nullptr,
                                          vl::T({{"message", var("a_chips", eej->get(K("chip_mod")))}, {"chip_mod", eej->get(K("chip_mod"))},
                                                 {"colour", colour("EDITION")}, {"edition", true}}));
                }
                if (eej->get(K("mult_mod")).truthy()) {
                    mult = mod_mult(mult + eej->get(K("mult_mod")).num());
                    update_hand_text(vl::T({{"delay", 0}}), vl::T({{"mult", mult}}));
                    card_eval_status_text(_card, "jokers", 0, percent, nullptr,
                                          vl::T({{"message", var("a_mult", eej->get(K("mult_mod")))}, {"mult_mod", eej->get(K("mult_mod"))},
                                                 {"colour", colour("DARK_EDITION")}, {"edition", true}}));
                }
                percent += percent_delta;
            }
            TableRef effects = eval_card(_card, ctx({{"cardarea", obj(G.jokers.get())}, {"joker_main", true}}));
            if (Table *ej = effects->get(K("jokers")).table()) {
                bool em = false, ec = false;
                if (ej->get(K("mult_mod")).truthy()) {
                    mult = mod_mult(mult + ej->get(K("mult_mod")).num());
                    em = true;
                }
                if (ej->get(K("chip_mod")).truthy()) {
                    hand_chips = mod_chips(hand_chips + ej->get(K("chip_mod")).num());
                    ec = true;
                }
                if (ej->get(K("Xmult_mod")).truthy()) {
                    mult = mod_mult(mult * ej->get(K("Xmult_mod")).num());
                    em = true;
                }
                update_hand_text(vl::T({{"delay", 0}}), vl::T({{"chips", ec ? Value(hand_chips) : Value(false)}, {"mult", em ? Value(mult) : Value(false)}}));
                card_eval_status_text(_card, "jokers", 0, percent, nullptr, TableRef(ej));
                percent += percent_delta;
            }
            for (Card *v : jokers) {
                Value effect = v->calculate_joker(ctx({{"other_joker", obj(_card)}}));
                if (Table *e = effect.table()) {
                    bool em = false, ec = false;
                    if (e->get(K("mult_mod")).truthy()) {
                        mult = mod_mult(mult + e->get(K("mult_mod")).num());
                        em = true;
                    }
                    if (e->get(K("chip_mod")).truthy()) {
                        hand_chips = mod_chips(hand_chips + e->get(K("chip_mod")).num());
                        ec = true;
                    }
                    if (e->get(K("Xmult_mod")).truthy()) {
                        mult = mod_mult(mult * e->get(K("Xmult_mod")).num());
                        em = true;
                    }
                    if (em || ec) {
                        update_hand_text(vl::T({{"delay", 0}}), vl::T({{"chips", ec ? Value(hand_chips) : Value(false)}, {"mult", em ? Value(mult) : Value(false)}}));
                        card_eval_status_text(v, "jokers", 0, percent, nullptr, TableRef(e));
                    }
                    percent += percent_delta;
                }
            }
            if (eej) {
                if (eej->get(K("x_mult_mod")).truthy()) {
                    mult = mod_mult(mult * eej->get(K("x_mult_mod")).num());
                    update_hand_text(vl::T({{"delay", 0}}), vl::T({{"mult", mult}}));
                    card_eval_status_text(_card, "jokers", 0, percent, nullptr,
                                          vl::T({{"message", var("a_xmult", eej->get(K("x_mult_mod")))}, {"x_mult_mod", eej->get(K("x_mult_mod"))},
                                                 {"colour", colour("EDITION")}, {"edition", true}}));
                }
                percent += percent_delta;
            }
        }

        if (Back *back = gv("selected_back").as<Back>()) back->trigger_effect_final_scoring(hand_chips, mult);
        mult = mod_mult(mult);
        hand_chips = mod_chips(hand_chips);

        std::vector<Card *> cards_destroyed;
        for (size_t i = 1; i <= scoring_hand.size(); i++) {
            Card *sc = scoring_hand[i - 1];
            Value destroyed;
            highlight_card(sc, (i - 0.999) / (scoring_hand.size() - 0.998), "down");
            for (Card *j : jokers) {
                destroyed = j->calculate_joker(vl::T({{"destroying_card", obj(sc)}, {"full_hand", Value(full_hand)}}));
                if (destroyed.truthy()) break;
            }
            if (sc->ability->get(K("name")).equals("Glass Card") && !sc->debuff &&
                pseudorandom("glass") < fld(gv("probabilities"), "normal").num() / sc->ability->get(K("extra")).num())
                destroyed = Value(true);
            if (destroyed.truthy()) {
                if (sc->ability->get(K("name")).equals("Glass Card")) sc->shattered = true;
                else sc->destroyed = true;
                cards_destroyed.push_back(sc);
            }
        }
        TableRef cd_t = new_table();
        for (Card *c : cards_destroyed) cd_t->push(obj(c));
        for (Card *j : jokers) eval_card(j, vl::T({{"cardarea", obj(G.jokers.get())}, {"remove_playing_cards", true}, {"removed", Value(cd_t)}}));
        TableRef glass = new_table();
        for (Card *c : cards_destroyed)
            if (c->shattered) glass->push(obj(c));
        check_for_unlock(vl::T({{"type", "shatter"}, {"shattered", Value(glass)}}));
        for (Card *c : cards_destroyed) {
            CardRef r(c);
            G.E_MANAGER->add_event(ev_immediate([r] {
                if (r->ability->get(K("name")).equals("Glass Card")) r->shatter();
                else r->start_dissolve();
                return true;
            }));
        }
    } else {
        mult = mod_mult(0);
        hand_chips = mod_chips(0);
        G.E_MANAGER->add_event(ev_immediate([] {
            juice_hud_blind();
            return true;
        }));
        play_area_status_text(Value("Not Allowed!"));
        for (Card *j : jokers) {
            TableRef effects = eval_card(j, ctx({{"cardarea", obj(G.jokers.get())}, {"debuffed_hand", true}}));
            if (effects->get(K("jokers")).truthy()) {
                card_eval_status_text(j, "jokers", 0, percent, nullptr, TableRef(effects->get(K("jokers")).table()));
                percent += percent_delta;
            }
        }
    }
    const double total = std::floor(hand_chips * mult);
    G.E_MANAGER->add_event(ev_after(0.4, [total] {
        update_hand_text(vl::T({{"delay", 0}, {"immediate", true}}), vl::T({{"mult", 0}, {"chips", 0}, {"chip_total", total}, {"level", ""}, {"handname", ""}}));
        play_sound("button", 0.9f, 0.6f);
        return true;
    }));
    check_and_set_high_score("hand", Value(hand_chips * mult));
    check_for_unlock(vl::T({{"type", "chip_score"}, {"chips", total}}));
    if (hand_chips * mult > 0) {
        delay(0.8);
        G.E_MANAGER->add_event(ev_immediate([] {
            play_sound("chips2");
            return true;
        }));
    }
    {
        EventConfig e;
        e.trigger = Trigger::Ease;
        e.blocking = false;
        e.ref = ValueRef::field(G.GAME, Value("chips"));
        e.ease_to = gv("chips").num() + total;
        e.delay = 0.5;
        e.ease_func = [](double t) { return std::floor(t); };
        G.E_MANAGER->add_event(e);
    }
    {
        EventConfig e;
        e.trigger = Trigger::Ease;
        e.blocking = true;
        e.ref = ValueRef::field(TableRef(current_hand()), Value("chip_total"));
        e.ease_to = 0;
        e.delay = 0.5;
        e.ease_func = [](double t) { return std::floor(t); };
        G.E_MANAGER->add_event(e);
    }
    G.E_MANAGER->add_event(ev_immediate([] {
        current_hand()->set("handname", "");
        return true;
    }));
    delay(0.3);
    for (Card *j : jokers) {
        TableRef effects = eval_card(j, ctx({{"cardarea", obj(G.jokers.get())}, {"after", true}}));
        if (effects->get(K("jokers")).truthy()) {
            card_eval_status_text(j, "jokers", 0, percent, nullptr, TableRef(effects->get(K("jokers")).table()));
            percent += percent_delta;
        }
    }
    TableRef sh_keep = sh_t;
    G.E_MANAGER->add_event(ev_immediate([sh_keep] {
        if (fld(gv("modifiers"), "debuff_played_cards").truthy())
            for (Card *c : table_cards(Value(sh_keep))) c->ability->set("perma_debuff", true);
        return true;
    }));
}

void draw_from_play_to_discard() {
    const int play_count = (int)G.play->cards.size();
    int it = 1;
    for (auto &v : std::vector<CardRef>(G.play->cards))
        if (!v->shattered && !v->destroyed) {
            draw_card(G.play.get(), G.discard.get(), it * 100.0 / play_count, "down", false, v.get());
            it++;
        }
}

void draw_from_discard_to_deck() {
    G.E_MANAGER->add_event(ev_immediate([] {
        const int n = (int)G.discard->cards.size();
        for (int i = 1; i <= n; i++)
            draw_card(G.discard.get(), G.deck.get(), i * 100.0 / n, "up", false, nullptr, 0.005, i % 2 == 0, false, std::max((21 - i) / 20.0, 0.7));
        return true;
    }));
}

void draw_from_hand_to_deck() {
    const int n = (int)G.hand->cards.size();
    for (int i = 1; i <= n; i++) draw_card(G.hand.get(), G.deck.get(), i * 100.0 / n, "down", false, nullptr, 0.08);
}

void draw_from_hand_to_discard() {
    const int n = (int)G.hand->cards.size();
    for (int i = 1; i <= n; i++) draw_card(G.hand.get(), G.discard.get(), i * 100.0 / n, "down", false, nullptr, 0.07);
}

// ------------------------------------------------------------------ end of the round
void end_round() {
    G.E_MANAGER->add_event(ev_after(0.2, [] {
        bool game_over = true, game_won = false;
        G.RESET_BLIND_STATES = true;
        G.RESET_JIGGLES = true;
        Blind *blind = game_blind();
        if (gv("chips").num() - blind->chips >= 0) game_over = false;
        for (auto &j : std::vector<CardRef>(G.jokers->cards)) {
            Value eval = j->calculate_joker(vl::T({{"end_of_round", true}, {"game_over", game_over}}));
            if (eval.truthy()) {
                if (eval.get(K("saved")).truthy()) game_over = false;
                card_eval_status_text(j.get(), "jokers", 0, -1, nullptr, TableRef(eval.table()));
            }
            j->calculate_rental();
            j->calculate_perishable();
        }
        Table *rr = gt("round_resets");
        if (rr->get(K("ante")).num() == gv("win_ante").num() && blind->get_type() && std::string_view(blind->get_type()) == "Boss") {
            game_won = true;
            G.GAME->set("won", true);
        }
        if (game_over) {
            G.STATE = GAME_OVER;
            if (!gv("won").truthy() && !gv("seeded").truthy() && !gv("challenge").truthy())
                G.PROFILES->get(G.SETTINGS->get(K("profile"))).get(K("high_scores")).get(K("current_streak")).table()->set("amt", 0);
            G.save_settings();
            G.FILE_HANDLER->set("force", true);
            G.STATE_COMPLETE = false;
            return true;
        }
        Table *cr = gt("current_round");
        G.GAME->set("unused_discards", gv("unused_discards").num() + cr->get(K("discards_left")).num());
        if (blind->config->get(K("blind")).truthy()) discover_card(blind->config->get(K("blind")));
        const bool boss = blind->get_type() && std::string_view(blind->get_type()) == "Boss";
        if (boss) {
            Value handname("High Card");
            double played = -1, order = 100;
            gt("hands")->each([&](const Value &k, const Value &v) {
                double p = fld(v, "played").num(), o = fld(v, "order").num();
                if (p > played || (p == played && order > o)) {
                    played = p;
                    handname = k;
                    // (the original never updates _order: only a strictly higher count wins after the first)
                }
            });
            cr->set("most_played_poker_hand", handname);
        }
        if (boss && !gv("seeded").truthy() && !gv("challenge").truthy()) {
            G.GAME->set("current_boss_streak", gv("current_boss_streak").num() + 1);
            check_and_set_high_score("boss_streak", gv("current_boss_streak"));
        }
        if (cr->get(K("hands_played")).num() == 1) {
            inc_career_stat("c_single_hand_round_streak", 1);
        } else if (!gv("seeded").truthy() && !gv("challenge").truthy()) {
            G.PROFILES->get(G.SETTINGS->get(K("profile"))).get(K("career_stats")).table()->set("c_single_hand_round_streak", 0);
            G.save_settings();
        }
        check_for_unlock(vl::T({{"type", "round_win"}}));
        set_joker_usage();
        if (game_won && !gv("win_notified").truthy()) {
            G.GAME->set("win_notified", true);
            EventConfig w = ev_immediate([] {
                if (G.STATE != ROUND_EVAL) return false;
                win_game();
                // (G.FTP_LOCKED: never on the Vita)
                G.GAME->set("won", true);
                return true;
            });
            w.blocking = false;
            w.blockable = false;
            G.E_MANAGER->add_event(w);
        }
        std::vector<CardRef> hand_cards(G.hand->cards);
        const int n = (int)hand_cards.size();
        for (int i = 1; i <= n; i++) {
            Card *hc = hand_cards[i - 1].get();
            std::vector<Value> reps{Value(1)};
            for (size_t j = 0; j < reps.size(); j++) {
                double percent = (i - 0.999) / (n - 0.998) + j * 0.1;
                if (reps[j].is_table()) {
                    Value r = reps[j].get(K("jokers")).truthy() ? reps[j].get(K("jokers")) : reps[j].get(K("seals"));
                    card_eval_status_text(r.get(K("card")).as<Card>(), "jokers", 0, -1, nullptr, TableRef(r.table()));
                }
                std::vector<TableRef> effects{hc->get_end_of_round_effect()};
                for (auto &jk : std::vector<CardRef>(G.jokers->cards)) {
                    Value ev = jk->calculate_joker(vl::T({{"cardarea", obj(G.hand.get())}, {"other_card", obj(hc)}, {"individual", true}, {"end_of_round", true}}));
                    if (ev.truthy()) effects.push_back(TableRef(ev.table()));
                }
                TableRef effects_t = new_table();
                for (auto &ef : effects) effects_t->push(Value(ef));
                if (reps[j].is_num()) {
                    TableRef ev = eval_card(hc, vl::T({{"end_of_round", true}, {"cardarea", obj(G.hand.get())}, {"repetition", true}, {"repetition_only", true}}));
                    if (non_empty(Value(ev)) && (non_empty(Value(effects[0])) || effects.size() > 1))
                        for (int h = 1; h <= (int)ev->get(K("seals")).get(K("repetitions")).num(); h++) reps.push_back(Value(ev));
                    for (auto &jk : std::vector<CardRef>(G.jokers->cards)) {
                        TableRef ej = eval_card(jk.get(), vl::T({{"cardarea", obj(G.hand.get())}, {"other_card", obj(hc)}, {"repetition", true}, {"end_of_round", true},
                                                                {"card_effects", Value(effects_t)}}));
                        if (non_empty(Value(ej)))
                            for (int h = 1; h <= (int)ej->get(K("jokers")).get(K("repetitions")).num(); h++) reps.push_back(Value(ej));
                    }
                }
                for (TableRef &ef : effects) {
                    if (Card *efc = ef->get(K("card")).as<Card>()) {
                        CardRef r(efc);
                        G.E_MANAGER->add_event(ev_immediate([r] {
                            r->juice_up_card(0.7f);
                            return true;
                        }));
                    }
                    if (ef->get(K("h_dollars")).truthy()) {
                        ease_dollars(ef->get(K("h_dollars")).num());
                        card_eval_status_text(hc, "dollars", ef->get(K("h_dollars")).num(), percent);
                    }
                    if (Table *ex = ef->get(K("extra")).table()) card_eval_status_text(hc, "extra", 0, percent, nullptr, TableRef(ex));
                }
            }
        }
        delay(0.3);
        draw_from_hand_to_discard();
        if (boss) {
            G.GAME->set("voucher_restock", Value());
            // TODO(run): set_eternal_ante / set_joker_slots_ante (challenges)
            delay(0.4);
            ease_ante(1);
            delay(0.4);
            check_for_unlock(vl::T({{"type", "ante_up"}, {"ante", rr->get(K("ante")).num() + 1}}));
        }
        draw_from_discard_to_deck();
        G.E_MANAGER->add_event(ev_after(0.3, [] {
            G.STATE = ROUND_EVAL;
            G.STATE_COMPLETE = false;
            Table *rr = gt("round_resets");
            Table *bs = rr->get(K("blind_states")).table();
            Value b = rr->get(K("blind"));
            if (b.truthy() && fld(b, "name").equals("Small Blind")) {
                bs->set("Small", "Defeated");
            } else if (b.truthy() && fld(b, "name").equals("Big Blind")) {
                bs->set("Big", "Defeated");
            } else {
                gt("current_round")->set("voucher", get_next_voucher_key());
                bs->set("Boss", "Defeated");
                for (auto &c : G.playing_cards) c->ability->set("played_this_ante", Value());
            }
            if (rr->get(K("temp_handsize")).truthy()) {
                G.hand->change_size(-(int)rr->get(K("temp_handsize")).num());
                rr->set("temp_handsize", Value());
            }
            if (rr->get(K("temp_reroll_cost")).truthy()) {
                rr->set("temp_reroll_cost", Value());
                calculate_reroll_cost(true);
            }
            reset_idol_card();
            reset_mail_rank();
            reset_ancient_card();
            reset_castle_card();
            for (auto &c : G.playing_cards) {
                c->ability->set("discarded", Value());
                c->ability->set("forced_selection", Value());
            }
            return true;
        }));
        return true;
    }));
}

void add_round_eval_row(const TableRef &config) {
    UIBox *re = round_eval();
    const double width = re->T->w - 0.51;
    const double num_dollars = config->get(K("dollars")).is_num() ? config->get(K("dollars")).num() : 1;
    const double scale = 0.9;
    const std::string name(config->get(K("name")).str());
    const bool bonus = config->get(K("bonus")).truthy();
    const double pitch = config->get(K("pitch")).is_num() ? config->get(K("pitch")).num() : 1;
    auto node = [](int uit, TableRef cfg, TableRef nodes = nullptr) {
        TableRef t = vl::T({{"n", (double)uit}, {"config", Value(cfg)}});
        if (nodes) t->set("nodes", Value(nodes));
        return t;
    };
    auto dyna = [](std::initializer_list<std::pair<const char *, Value>> kv) { return obj(new DynaText(vl::T(kv))); };
    Value en_font((Object *)G.LANGUAGES["en-us"]->font);
    if (name != "bottom") {
        if (name != "blind1") {
            if (!G.round_eval_divider_added) {
                G.E_MANAGER->add_event(ev_after(0.25, [width, bonus, node, dyna, en_font] {
                    TableRef spacer = node(UIT_R, vl::T({{"align", "cm"}, {"minw", width}}),
                                           A({Value(node(UIT_O, vl::T({{"object", dyna({{"string", Value(A({"......................................"}))},
                                                                                       {"colours", Value(A({G.colour("WHITE")}))},
                                                                                       {"shadow", true},
                                                                                       {"float", true},
                                                                                       {"y_offset", -30},
                                                                                       {"scale", 0.45},
                                                                                       {"spacing", 13.5},
                                                                                       {"font", en_font},
                                                                                       {"pop_in", 0}})}})))}));
                    round_eval()->add_child(spacer, round_eval()->get_UIE_by_ID(bonus ? "bonus_round_eval" : "base_round_eval"));
                    return true;
                }));
                delay(0.6);
                G.round_eval_divider_added = true;
            }
        } else {
            delay(0.2);
        }
        delay(0.2);
        TableRef cfg = config;
        EventConfig e;
        e.trigger = Trigger::Before;
        e.delay = 0.5;
        e.func = [cfg, name, width, scale, pitch, num_dollars, bonus, node, dyna] {
            TableRef left_text = new_table();
            auto ui = [](const char *n) { return fld(G.colour("UI"), n); };
            if (name == "blind1") {
                Sprite *stake_sprite = get_stake_sprite(gv("stake").truthy() ? (int)gv("stake").num() : 1, 0.5f);
                Value pos = fld(game_blind()->config->get(K("blind")), "pos");
                SpritePos sp;
                sp.x = (float)fld(pos, "x").num();
                sp.y = (float)fld(pos, "y").num();
                AnimatedSprite *bs = new AnimatedSprite(0, 0, 1.2f, 1.2f, &G.ANIMATION_ATLAS["blind_chips"], sp);
                DrawStep a, b;
                a.has_shadow_height = true;
                a.shadow_height = 0.05f;
                bs->define_draw_steps({a, b});
                left_text->push(Value(node(UIT_O, vl::T({{"w", 1.2}, {"h", 1.2}, {"object", obj(bs)}, {"hover", true}, {"can_collide", false}}))));
                if (cfg->get(K("saved")).truthy()) {
                    left_text->push(Value(node(UIT_C, vl::T({{"padding", 0.05}, {"align", "cm"}}),
                                               A({Value(node(UIT_R, vl::T({{"align", "cm"}}),
                                                             A({Value(node(UIT_O, vl::T({{"object", dyna({{"string", Value(A({Value(" " + localize("ph_mr_bones").to_string() + " ")}))},
                                                                                                         {"colours", Value(A({G.colour("FILTER")}))},
                                                                                                         {"shadow", true},
                                                                                                         {"pop_in", 0},
                                                                                                         {"scale", 0.5 * scale},
                                                                                                         {"silent", true}})}})))})))}))));
                } else {
                    Blind *bl = game_blind();
                    left_text->push(Value(node(
                        UIT_C, vl::T({{"padding", 0.05}, {"align", "cm"}}),
                        A({Value(node(UIT_R, vl::T({{"align", "cm"}}),
                                      A({Value(node(UIT_O, vl::T({{"object", dyna({{"string", Value(A({Value(" " + localize("ph_score_at_least").to_string() + " ")}))},
                                                                                  {"colours", Value(A({ui("TEXT_LIGHT")}))},
                                                                                  {"shadow", true},
                                                                                  {"pop_in", 0},
                                                                                  {"scale", 0.4 * scale},
                                                                                  {"silent", true}})}})))}))),
                           Value(node(UIT_R, vl::T({{"align", "cm"}, {"minh", 0.8}}),
                                      A({Value(node(UIT_O, vl::T({{"w", 0.5}, {"h", 0.5}, {"object", obj(stake_sprite)}, {"hover", true}, {"can_collide", false}}))),
                                         Value(node(UIT_T, vl::T({{"text", bl->vars->get(K("chip_text"))}, {"scale", scale_number(Value(bl->chips), scale, 100000)},
                                                                  {"colour", G.colour("RED")}, {"shadow", true}})))})))}))));
                }
            } else if (name.find("tag") != std::string::npos) {
                Value pos = cfg->get(K("pos"));
                SpritePos sp;
                sp.x = (float)fld(pos, "x").num();
                sp.y = (float)fld(pos, "y").num();
                Sprite *ts = new Sprite(0, 0, 0.7f, 0.7f, &G.ASSET_ATLAS["tags"], sp);
                DrawStep a, b;
                a.has_shadow_height = true;
                a.shadow_height = 0.05f;
                ts->define_draw_steps({a, b});
                ts->juice_up();
                left_text->push(Value(node(UIT_O, vl::T({{"w", 0.7}, {"h", 0.7}, {"object", obj(ts)}, {"hover", true}, {"can_collide", false}}))));
                left_text->push(Value(node(UIT_O, vl::T({{"object", dyna({{"string", Value(A({cfg->get(K("condition"))}))}, {"colours", Value(A({ui("TEXT_LIGHT")}))},
                                                                          {"shadow", true}, {"pop_in", 0}, {"scale", 0.4 * scale}, {"silent", true}})}}))));
            } else if (name == "hands" || name == "discards" || name == "interest") {
                const bool hands = name == "hands", interest = name == "interest";
                Value disp = cfg->get(K("disp")).truthy() ? cfg->get(K("disp")) : cfg->get(K("dollars"));
                if (interest) disp = Value(num_dollars);
                Value vars = hands ? Value(A({fld(gv("modifiers"), "money_per_hand").truthy() ? fld(gv("modifiers"), "money_per_hand") : Value(1)}))
                             : interest ? Value(A({gv("interest_amount"), 5, gv("interest_amount").num() * gv("interest_cap").num() / 5}))
                                        : Value(A({fld(gv("modifiers"), "money_per_discard").truthy() ? fld(gv("modifiers"), "money_per_discard") : Value(0)}));
                const char *key = hands ? "remaining_hand_money" : interest ? "interest" : "remaining_discard_money";
                left_text->push(Value(node(UIT_T, vl::T({{"text", disp}, {"scale", 0.8 * scale}, {"colour", G.colour(hands ? "BLUE" : interest ? "MONEY" : "RED")},
                                                         {"shadow", true}, {"juice", true}}))));
                left_text->push(Value(node(UIT_O, vl::T({{"object", dyna({{"string", Value(A({Value(" " + localize(vl::T({{"type", "variable"}, {"key", key}, {"vars", vars}})).to_string())}))},
                                                                          {"colours", Value(A({ui("TEXT_LIGHT")}))},
                                                                          {"shadow", true},
                                                                          {"pop_in", 0},
                                                                          {"scale", 0.4 * scale},
                                                                          {"silent", true}})}}))));
            } else if (name.find("joker") != std::string::npos) {
                Card *c = cfg->get(K("card")).as<Card>();
                Value center = c->config->get(K("center"));
                left_text->push(Value(node(UIT_O, vl::T({{"object", dyna({{"string", localize(vl::T({{"type", "name_text"}, {"set", fld(center, "set")}, {"key", fld(center, "key")}}))},
                                                                          {"colours", Value(A({G.colour("FILTER")}))},
                                                                          {"shadow", true},
                                                                          {"pop_in", 0},
                                                                          {"scale", 0.6 * scale},
                                                                          {"silent", true}})}}))));
            }
            TableRef full_row = node(UIT_R, vl::T({{"align", "cm"}, {"minw", 5}}),
                                     A({Value(node(UIT_C, vl::T({{"padding", 0.05}, {"minw", width * 0.55}, {"minh", 0.61}, {"align", "cl"}}), left_text)),
                                        Value(node(UIT_C, vl::T({{"padding", 0.05}, {"minw", width * 0.45}, {"align", "cr"}}),
                                                   A({Value(node(UIT_C, vl::T({{"align", "cm"}, {"id", Value("dollar_" + name)}}), A({})))})))}));
            if (name == "blind1") game_blind()->juice_up_blind();
            round_eval()->add_child(full_row, round_eval()->get_UIE_by_ID(bonus ? "bonus_round_eval" : "base_round_eval"));
            play_sound("cancel", (float)pitch);
            play_sound("highlight1", (float)(1.5 * pitch), 0.2f);
            if (Card *c = cfg->get(K("card")).as<Card>()) {
                float r = 0.46f;
                c->juice_up(0.7f, &r);
            }
            return true;
        };
        G.E_MANAGER->add_event(e);

        auto dollar_row = std::make_shared<int>(0);
        if (num_dollars > 60) {
            EventConfig d;
            d.trigger = Trigger::Before;
            d.delay = 0.38;
            d.func = [name, num_dollars, node, dyna, dollar_row] {
                round_eval()->add_child(node(UIT_R, vl::T({{"align", "cm"}, {"id", Value("dollar_row_" + std::to_string(*dollar_row + 1) + "_" + name)}}),
                                             A({Value(node(UIT_O, vl::T({{"object", dyna({{"string", Value(A({Value(localize("$").to_string() + Value(num_dollars).to_string())}))},
                                                                                          {"colours", Value(A({G.colour("MONEY")}))},
                                                                                          {"shadow", true},
                                                                                          {"pop_in", 0},
                                                                                          {"scale", 0.65},
                                                                                          {"float", true}})}})))})),
                                        round_eval()->get_UIE_by_ID("dollar_" + name));
                play_sound("coin3", (float)(0.9 + 0.2 * lua_math.random()), 0.7f);
                play_sound("coin6", 1.3f, 0.8f);
                return true;
            };
            G.E_MANAGER->add_event(d);
        } else {
            for (int i = 1; i <= (int)num_dollars; i++) {
                EventConfig d;
                d.trigger = Trigger::Before;
                d.delay = 0.18 - (num_dollars > 20 ? 0.13 : num_dollars > 9 ? 0.1 : 0);
                d.func = [i, name, num_dollars, node, dollar_row] {
                    if (i % 30 == 1) {
                        round_eval()->add_child(node(UIT_R, vl::T({{"align", "cm"}, {"id", Value("dollar_row_" + std::to_string(*dollar_row + 1) + "_" + name)}}), A({})),
                                                round_eval()->get_UIE_by_ID("dollar_" + name));
                        (*dollar_row)++;
                    }
                    TableRef r = node(UIT_T, vl::T({{"text", localize("$")}, {"colour", G.colour("MONEY")},
                                                    {"scale", num_dollars > 20 ? 0.28 : num_dollars > 9 ? 0.43 : 0.58}, {"shadow", true}, {"hover", true},
                                                    {"can_collide", false}, {"juice", true}}));
                    play_sound("coin3", (float)(0.9 + 0.2 * lua_math.random()), (float)(0.7 - (num_dollars > 20 ? 0.2 : 0)));
                    if (name == "blind1") {
                        Table *cr = gt("current_round");
                        std::string s(cr->get(K("dollars_to_be_earned")).str());
                        cr->set("dollars_to_be_earned", Value(s.size() > 0 ? s.substr(1) : s));
                    }
                    round_eval()->add_child(r, round_eval()->get_UIE_by_ID("dollar_row_" + std::to_string(*dollar_row) + "_" + name));
                    return true;
                };
                G.E_MANAGER->add_event(d);
            }
        }
    } else {
        delay(0.4);
        TableRef cfg = config;
        EventConfig e;
        e.trigger = Trigger::Before;
        e.delay = 0.5;
        e.func = [cfg, node, scale] {
            auto ui = [](const char *n) { return fld(G.colour("UI"), n); };
            TableRef def = node(UIT_ROOT, vl::T({{"align", "cm"}, {"colour", G.colour("CLEAR")}}),
                                A({Value(node(UIT_R, vl::T({{"id", "cash_out_button"}, {"align", "cm"}, {"padding", 0.1}, {"minw", 7}, {"r", 0.15}, {"colour", G.colour("ORANGE")},
                                                            {"shadow", true}, {"hover", true}, {"one_press", true}, {"button", "cash_out"},
                                                            {"focus_args", Value(vl::T({{"snap_to", true}}))}}),
                                              A({Value(node(UIT_T, vl::T({{"text", Value(localize("b_cash_out").to_string() + ": ")}, {"scale", 1}, {"colour", ui("TEXT_LIGHT")},
                                                                          {"shadow", true}}))),
                                                 Value(node(UIT_T, vl::T({{"text", Value(localize("$").to_string() + cfg->get(K("dollars")).to_string())}, {"scale", 1.2 * scale},
                                                                          {"colour", G.colour("WHITE")}, {"shadow", true}, {"juice", true}})))})))}));
            new UIBox(def, vl::T({{"align", "tmi"}, {"offset", Value(vl::T({{"x", 0}, {"y", 0.4}}))}, {"major", obj(round_eval())}}));
            gt("current_round")->set("dollars", cfg->get(K("dollars")));
            play_sound("coin6", cfg->get(K("pitch")).is_num() ? (float)cfg->get(K("pitch")).num() : 1);
            return true;
        };
        G.E_MANAGER->add_event(e);
        // (G.FUNCS.rate_us_flow at ante 3: not on the Vita)
    }
}

void evaluate_round() {
    double pitch = 0.95, dollars = 0;
    Blind *blind = game_blind();
    if (gv("chips").num() - blind->chips >= 0) {
        add_round_eval_row(vl::T({{"dollars", blind->dollars}, {"name", "blind1"}, {"pitch", pitch}}));
        pitch += 0.06;
        dollars += blind->dollars;
    } else {
        add_round_eval_row(vl::T({{"dollars", 0}, {"name", "blind1"}, {"pitch", pitch}, {"saved", true}}));
        pitch += 0.06;
    }
    EventConfig e;
    e.trigger = Trigger::Before;
    e.delay = 1.3 * std::min(blind->dollars + 2, 7.0) / 2 * 0.15 + 0.5;
    e.func = [] {
        game_blind()->defeat();
        return true;
    };
    G.E_MANAGER->add_event(e);
    delay(0.2);
    G.E_MANAGER->add_event(ev_immediate([] {
        ease_background_colour_blind(ROUND_EVAL, "");
        return true;
    }));
    // TODO(run): G.GAME.selected_back:trigger_effect{context = 'eval'} (Anaglyph Deck)
    Table *cr = gt("current_round");
    Value mods = gv("modifiers");
    if (cr->get(K("hands_left")).num() > 0 && !fld(mods, "no_extra_hand_money").truthy()) {
        double mph = fld(mods, "money_per_hand").truthy() ? fld(mods, "money_per_hand").num() : 1;
        add_round_eval_row(vl::T({{"dollars", cr->get(K("hands_left")).num() * mph}, {"disp", cr->get(K("hands_left"))}, {"bonus", true}, {"name", "hands"}, {"pitch", pitch}}));
        pitch += 0.06;
        dollars += cr->get(K("hands_left")).num() * mph;
    }
    if (cr->get(K("discards_left")).num() > 0 && fld(mods, "money_per_discard").truthy()) {
        double mpd = fld(mods, "money_per_discard").num();
        add_round_eval_row(vl::T({{"dollars", cr->get(K("discards_left")).num() * mpd}, {"disp", cr->get(K("discards_left"))}, {"bonus", true}, {"name", "discards"}, {"pitch", pitch}}));
        pitch += 0.06;
        dollars += cr->get(K("discards_left")).num() * mpd;
    }
    int ji = 1;
    for (auto &j : std::vector<CardRef>(G.jokers->cards)) {
        Value ret = j->calculate_dollar_bonus();
        if (ret.truthy()) {
            add_round_eval_row(vl::T({{"dollars", ret}, {"bonus", true}, {"name", Value("joker" + std::to_string(ji))}, {"pitch", pitch}, {"card", obj(j.get())}}));
            pitch += 0.06;
            dollars += ret.num();
        }
        ji++;
    }
    Table *tags = gt("tags");
    for (int i = 1; i <= tags->len(); i++) {
        Tag *t = tags->get(i).as<Tag>();
        Value ret = t ? t->apply_to_run(vl::T({{"type", "eval"}})) : Value();
        if (ret.truthy()) {
            add_round_eval_row(vl::T({{"dollars", ret.get(K("dollars"))}, {"bonus", true}, {"name", Value("tag" + std::to_string(i))}, {"pitch", pitch},
                                      {"condition", ret.get(K("condition"))}, {"pos", ret.get(K("pos"))}, {"tag", ret.get(K("tag"))}}));
            pitch += 0.06;
            dollars += ret.get(K("dollars")).num();
        }
    }
    if (gv("dollars").num() >= 5 && !fld(mods, "no_interest").truthy()) {
        double interest = gv("interest_amount").num() * std::min(std::floor(gv("dollars").num() / 5), gv("interest_cap").num() / 5);
        add_round_eval_row(vl::T({{"bonus", true}, {"name", "interest"}, {"pitch", pitch}, {"dollars", interest}}));
        pitch += 0.06;
        if (!gv("seeded").truthy() && !gv("challenge").truthy()) {
            Table *cs = G.PROFILES->get(G.SETTINGS->get(K("profile"))).get(K("career_stats")).table();
            if (interest == gv("interest_amount").num() * gv("interest_cap").num() / 5)
                cs->set("c_round_interest_cap_streak", cs->get(K("c_round_interest_cap_streak")).num() + 1);
            else
                cs->set("c_round_interest_cap_streak", 0);
        }
        check_for_unlock(vl::T({{"type", "interest_streak"}}));
        dollars += interest;
    }
    pitch += 0.06;
    add_round_eval_row(vl::T({{"name", "bottom"}, {"dollars", dollars}}));
}

void cash_out(UIElement *e) {
    stop_use();
    if (UIBox *re = round_eval()) {
        e->config->set("button", Value());
        re->alignment.offset.y = G.ROOM->T->y + 15;
        re->alignment.offset.x = 0;
        G.deck->shuffle("cashout" + fld(gv("round_resets"), "ante").to_string());
        G.deck->hard_set_T(G.deck->T->x, G.deck->T->y, G.deck->T->w, G.deck->T->h);
        delay(0.3);
        G.E_MANAGER->add_event(ev_immediate([] {
            if (G.round_eval) {
                G.round_eval->remove();
                G.round_eval = nullptr;
            }
            Table *cr = gt("current_round"), *rr = gt("round_resets"), *rb = gt("round_bonus");
            cr->set("jokers_purchased", 0);
            cr->set("discards_left", std::max(0.0, rr->get(K("discards")).num() + rb->get(K("discards")).num()));
            cr->set("hands_left", std::max(1.0, rr->get(K("hands")).num() + rb->get(K("next_hands")).num()));
            G.STATE = SHOP;
            G.GAME->set("shop_free", Value());
            G.GAME->set("shop_d6ed", Value());
            G.STATE_COMPLETE = false;
            return true;
        }));
        ease_dollars(gt("current_round")->get(K("dollars")).num());
        G.E_MANAGER->add_event(ev_immediate([] {
            gt("previous_round")->set("dollars", gv("dollars"));
            return true;
        }));
        play_sound("coin7");
    }
    ease_chips(0);
    Table *rr = gt("round_resets");
    if (rr->get(K("blind_states")).get(K("Boss")).equals("Defeated")) {
        rr->set("blind_ante", rr->get(K("ante")));
        rr->get(K("blind_tags")).table()->set("Small", get_next_tag_key());
        rr->get(K("blind_tags")).table()->set("Big", get_next_tag_key());
    }
    reset_blinds();
    delay(0.6);
}

void parse_hand_highlight(CardArea *area) {
    G.boss_throw_hand = false;
    PokerHandInfo info = get_poker_hand_info(cards_of(area->highlighted));
    if (info.text == "NULL") {
        update_hand_text(vl::T({{"immediate", true}, {"nopulse", true}, {"delay", 0}}), vl::T({{"mult", 0}, {"chips", 0}, {"level", ""}, {"handname", ""}}));
        return;
    }
    Blind *b = game_blind();
    if (b && b->debuff_hand(cards_of(area->highlighted), info.poker_hands, info.text, true)) G.boss_throw_hand = true;
    bool backwards = false;
    for (auto &c : area->highlighted)
        if (c->facing == "back") backwards = true;
    if (backwards) {
        update_hand_text(vl::T({{"immediate", true}, {"delay", 0}}), vl::T({{"handname", "????"}, {"level", "?"}, {"mult", "?"}, {"chips", "?"}}));
    } else {
        Table *h = gt("hands")->get(Value(info.text)).table();
        update_hand_text(vl::T({{"immediate", true}, {"delay", 0}}),
                         vl::T({{"handname", info.loc_disp_text}, {"level", h->get(K("level"))}, {"mult", h->get(K("mult"))}, {"chips", h->get(K("chips"))}}));
    }
}

}  // namespace vl
