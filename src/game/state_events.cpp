// functions/state_events.lua, and the run helpers of common_events.lua / misc_functions.lua that
// starting a run, choosing a blind and drawing a hand need.
#include "game/state_events.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <string>

#include "core/lua_sort.h"
#include "engine/controller.h"
#include "engine/event.h"
#include "engine/random.h"
#include "engine/text.h"
#include "engine/ui.h"
#include "game/blind.h"
#include "game/card.h"
#include "game/functions.h"
#include "game/g.h"
#include "game/sound.h"
#include "game/tag.h"
#include "game/button_callbacks.h"
#include "game/ui_definitions.h"

namespace vl {

namespace {
inline Value fld(const Value &t, const char *k) { return t.get(Value(intern(k))); }
inline Table *gt(const char *k) { return G.GAME->get(Value(intern(k))).table(); }
inline Value gv(const char *k) { return G.GAME->get(Value(intern(k))); }
UIBox *hud() { return dynamic_cast<UIBox *>(G.HUD.get()); }
Blind *game_blind() { return G.GAME->get(K("blind")).as<Blind>(); }

// G.hand_text_area.<name> (the HUD element)
UIElement *hand_text_area(const char *name) {
    static const struct { const char *k, *id; bool blind; } map[] = {
        {"chips", "hand_chips", false},        {"mult", "hand_mult", false},       {"ante", "ante_UI_count", false},
        {"round", "round_UI_count", false},    {"chip_total", "hand_chip_total", false}, {"handname", "hand_name", false},
        {"hand_level", "hand_level", false},   {"game_chips", "chip_UI_count", false},   {"blind_chips", "HUD_blind_count", true},
        {"blind_spacer", "blind_spacer", false}};
    for (auto &m : map)
        if (std::string_view(m.k) == name) {
            UIBox *b = m.blind ? dynamic_cast<UIBox *>(G.HUD_blind.get()) : hud();
            return b ? b->get_UIE_by_ID(m.id) : nullptr;
        }
    return nullptr;
}

void dec_stop_use(int depth) {
    EventConfig e = ev_immediate([depth] {
        if (depth > 0) dec_stop_use(depth - 1);
        else G.GAME->set("STOP_USE", std::max(gv("STOP_USE").num() - 1, 0.0));
        return true;
    });
    e.blocking = false;
    e.no_delete = true;
    G.E_MANAGER->add_event(e);
}

Value card_value(Card *c) { return Value((Object *)c); }
}  // namespace

// ------------------------------------------------------------------ misc_functions.lua
void stop_use() {
    G.GAME->set("STOP_USE", gv("STOP_USE").num() + 1);
    dec_stop_use(6);
}

void inc_career_stat(const char *stat, double mod) {
    if (gv("seeded").truthy() || gv("challenge").truthy()) return;
    Table *cs = G.PROFILES->get(G.SETTINGS->get(K("profile"))).get(K("career_stats")).table();
    if (!cs) return;
    if (!cs->get(Value(intern(stat))).truthy()) cs->set(stat, 0);
    cs->set(stat, cs->get(Value(intern(stat))).num() + mod);
    G.save_settings();
}

void check_and_set_high_score(const char *score, const Value &amt) {
    if (!amt.is_num()) return;
    Value rs = gt("round_scores")->get(Value(intern(score)));
    if (rs.truthy() && std::floor(amt.num()) > fld(rs, "amt").num()) rs.table()->set("amt", std::floor(amt.num()));
    if (gv("seeded").truthy()) return;
    // G.SETTINGS.COMP (competitions) does not exist on the Vita
    Value hs = G.PROFILES->get(G.SETTINGS->get(K("profile"))).get(K("high_scores")).get(Value(intern(score)));
    if (hs.truthy() && std::floor(amt.num()) > fld(hs, "amt").num()) {
        if (rs.truthy()) rs.table()->set("high_score", true);
        hs.table()->set("amt", std::floor(amt.num()));
        G.save_settings();
    }
}

void save_run() {
    // TODO(run): serialize the run (cardAreas, tags, GAME, STATE, BLIND, BACK) into G.ARGS.save_run
}

double scale_number(const Value &number, double scale, double max) {
    const double E_SWITCH_POINT = 100000000000;
    if (!number.is_num()) return scale;
    double n = number.num();
    if (n >= E_SWITCH_POINT) scale = scale * std::floor(std::log10(max * 10)) / std::floor(std::log10(1000000.0 * 10));
    else if (n >= max) scale = scale * std::floor(std::log10(max * 10)) / std::floor(std::log10(n * 10));
    return scale;
}

void pseudoshuffle(std::vector<CardRef> &list, double seed) {
    lua_math.seed(seed);
    if (!list.empty()) lua_sort(list, [](const CardRef &a, const CardRef &b) { return a->sort_id < b->sort_id; });
    for (int i = (int)list.size(); i >= 2; i--) {
        int j = (int)lua_math.random((double)i);
        std::swap(list[i - 1], list[j - 1]);
    }
}

std::string generate_starting_seed() {
    // TODO(run): stake 8's legendary-joker seed search
    const Moveable *ch = dynamic_cast<const Moveable *>(G.CONTROLLER->cursor_hover.target.get());
    double x = ch ? ch->T->x : 0, y = ch ? ch->T->y : 0, time = G.CONTROLLER->cursor_hover.time;
    return random_string(8, x * 0.33411983 + y * 0.874146 + 0.412311010 * time);
}

Value get_deck_from_name(const Value &name) {
    Value found;
    G.P_CENTERS->each([&](const Value &, const Value &v) {
        if (!found.truthy() && fld(v, "name") == name) found = v;
    });
    return found;
}

Sprite *get_stake_sprite(int stake, float scale) {
    Value pos = fld(G.P_CENTER_POOLS->get(K("Stake")).get(stake), "pos");
    SpritePos p;
    p.x = (float)fld(pos, "x").num();
    p.y = (float)fld(pos, "y").num();
    Sprite *s = new Sprite(0, 0, scale, scale, &G.ASSET_ATLAS["chips"], p);
    s->states.drag.can = false;
    // TODO(run): stake 8's voucher-shine draw
    return s;
}

void card_from_control(const TableRef &control) {
    G.playing_card = G.playing_card + 1;
    Value front = G.P_CARDS->get(Value(std::string(control->get(K("s")).str()) + "_" + std::string(control->get(K("r")).str())));
    Value e = control->get(K("e"));
    Card *c = new Card(G.deck->T->x, G.deck->T->y, G.CARD_W, G.CARD_H, front, G.P_CENTERS->get(e.truthy() ? e : Value("c_base")),
                       vl::T({{"playing_card", (double)G.playing_card}}));
    if (control->get(K("d")).truthy()) {
        TableRef ed = new_table();
        ed->set(control->get(K("d")), true);
        c->set_edition(Value(ed), true, true);
    }
    if (control->get(K("g")).truthy()) c->set_seal(control->get(K("g")), true, true);
    G.deck->emplace(c);
    G.playing_cards.push_back(CardRef(c));
    G.has_playing_cards = true;
}

// ------------------------------------------------------------------ common_events.lua
void calculate_reroll_cost(bool skip_increment) {
    Table *cr = gt("current_round");
    if (cr->get(K("free_rerolls")).num() < 0) cr->set("free_rerolls", 0);
    if (cr->get(K("free_rerolls")).num() > 0) {
        cr->set("reroll_cost", 0);
        return;
    }
    if (!cr->get(K("reroll_cost_increase")).truthy()) cr->set("reroll_cost_increase", 0);
    if (!skip_increment) cr->set("reroll_cost_increase", cr->get(K("reroll_cost_increase")).num() + 1);
    Table *rr = gt("round_resets");
    Value base = rr->get(K("temp_reroll_cost")).truthy() ? rr->get(K("temp_reroll_cost")) : rr->get(K("reroll_cost"));
    cr->set("reroll_cost", base.num() + cr->get(K("reroll_cost_increase")).num());
}

namespace {
// the playing cards that are not Stone Cards, as a Lua array (pseudorandom_element sorts by sort_id)
TableRef non_stone_cards() {
    TableRef t = new_table();
    for (auto &c : G.playing_cards)
        if (!c->ability->get(K("effect")).equals("Stone Card")) t->push(card_value(c.get()));
    return t;
}
std::string ante_str() { return fld(gv("round_resets"), "ante").to_string(); }
}  // namespace

void reset_idol_card() {
    Table *ic = gt("current_round")->get(K("idol_card")).table();
    ic->set("rank", "Ace");
    ic->set("suit", "Spades");
    TableRef valid = non_stone_cards();
    if (valid->len() > 0) {
        double s = pseudoseed("idol" + ante_str());
        Card *c = pseudorandom_element(Value(valid), &s).as<Card>();
        ic->set("rank", c->base->get(K("value")));
        ic->set("suit", c->base->get(K("suit")));
        ic->set("id", c->base->get(K("id")));
    }
}

void reset_mail_rank() {
    Table *mc = gt("current_round")->get(K("mail_card")).table();
    mc->set("rank", "Ace");
    TableRef valid = non_stone_cards();
    if (valid->len() > 0) {
        double s = pseudoseed("mail" + ante_str());
        Card *c = pseudorandom_element(Value(valid), &s).as<Card>();
        mc->set("rank", c->base->get(K("value")));
        mc->set("id", c->base->get(K("id")));
    }
}

void reset_ancient_card() {
    Table *ac = gt("current_round")->get(K("ancient_card")).table();
    TableRef suits = new_table();
    for (const char *s : {"Spades", "Hearts", "Clubs", "Diamonds"})
        if (!ac->get(K("suit")).equals(s)) suits->push(Value(s));
    double sd = pseudoseed("anc" + ante_str());
    ac->set("suit", pseudorandom_element(Value(suits), &sd));
}

void reset_castle_card() {
    Table *cc = gt("current_round")->get(K("castle_card")).table();
    cc->set("suit", "Spades");
    TableRef valid = non_stone_cards();
    if (valid->len() > 0) {
        double s = pseudoseed("cas" + ante_str());
        Card *c = pseudorandom_element(Value(valid), &s).as<Card>();
        cc->set("suit", c->base->get(K("suit")));
    }
}

void reset_blinds() {
    Table *rr = gt("round_resets");
    if (!rr->get(K("blind_states")).truthy())
        rr->set("blind_states", Value(vl::T({{"Small", "Select"}, {"Big", "Upcoming"}, {"Boss", "Upcoming"}})));
    Table *bs = rr->get(K("blind_states")).table();
    if (bs->get(K("Boss")).equals("Defeated")) {
        bs->set("Small", "Upcoming");
        bs->set("Big", "Upcoming");
        bs->set("Boss", "Upcoming");
        G.GAME->set("blind_on_deck", "Small");
        rr->get(K("blind_choices")).table()->set("Boss", get_new_boss());
        rr->set("boss_rerolled", false);
    }
}

Value get_new_boss() {
    if (!gv("perscribed_bosses").truthy()) G.GAME->set("perscribed_bosses", Value(new_table()));
    const double ante = fld(gv("round_resets"), "ante").num();
    Value pres = gt("perscribed_bosses")->get(ante);
    Table *used = gt("bosses_used");
    if (pres.truthy()) {
        gt("perscribed_bosses")->set(ante, Value());
        used->set(pres, used->get(pres).num() + 1);
        return pres;
    }
    // (G.FORCE_BOSS is a debug setting)
    const double win_ante = gv("win_ante").num();
    TableRef eligible = new_table();
    G.P_BLINDS->each([&](const Value &k, const Value &v) {
        Value b = fld(v, "boss");
        if (!b.truthy()) return;
        const bool showdown = fld(b, "showdown").truthy();
        if (!showdown && fld(b, "min").num() <= std::max(1.0, ante) && (lua_fmod(std::max(1.0, ante), win_ante) != 0 || ante < 2))
            eligible->set(k, true);
        else if (showdown && lua_fmod(ante, win_ante) == 0 && ante >= 2)
            eligible->set(k, true);
    });
    gt("banned_keys")->each([&](const Value &k, const Value &) {
        if (eligible->get(k).truthy()) eligible->set(k, Value());
    });
    double min_use = 100;
    used->each([&](const Value &k, const Value &v) {
        if (eligible->get(k).truthy()) {
            eligible->set(k, v);
            if (v.num() <= min_use) min_use = v.num();
        }
    });
    std::vector<Value> drop;
    eligible->each([&](const Value &k, const Value &v) {
        if (v.truthy() && v.is_num() && v.num() > min_use) drop.push_back(k);
    });
    for (auto &k : drop) eligible->set(k, Value());
    double s = pseudoseed("boss");
    Value boss;
    pseudorandom_element(Value(eligible), &s, &boss);
    used->set(boss, used->get(boss).num() + 1);
    return boss;
}

std::pair<TableRef, std::string> get_current_pool(const char *type, const Value &rarity_in, bool legendary, const std::string &append) {
    TableRef pool = new_table();
    Value starting;
    std::string pool_key;
    int pool_size = 0;
    const std::string ante = ante_str();
    if (std::string_view(type) == "Joker") {
        double rarity = rarity_in.truthy() ? rarity_in.num() : pseudorandom(pseudoseed("rarity" + ante + append));
        int r = legendary ? 4 : rarity > 0.95 ? 3 : rarity > 0.7 ? 2 : 1;
        starting = G.P_JOKER_RARITY_POOLS->get(r);
        pool_key = "Joker" + std::to_string(r) + (!legendary ? append : "");
    } else {
        starting = G.P_CENTER_POOLS->get(Value(type));
        pool_key = std::string(type) + append;
    }
    const std::string t(type);
    Table *used_jokers = gt("used_jokers"), *used_vouchers = gt("used_vouchers"), *banned = gt("banned_keys"), *pool_flags = gt("pool_flags");
    const bool showman = !find_joker("Showman").empty();
    if (Table *sp = starting.table())
        for (int i = 1; i <= sp->len(); i++) {
            Value v = sp->get(i);
            Value key = fld(v, "key");
            bool add = false;
            if (t == "Enhanced") {
                add = true;
            } else if (t == "Demo") {
                add = fld(v, "pos").truthy() && fld(v, "config").truthy();
            } else if (t == "Tag") {
                Value req = fld(v, "requires");
                add = (!req.truthy() || (G.P_CENTERS->get(req).truthy() && fld(G.P_CENTERS->get(req), "discovered").truthy())) &&
                      (!fld(v, "min_ante").truthy() || fld(v, "min_ante").num() <= fld(gv("round_resets"), "ante").num());
            } else if (!(used_jokers->get(key).truthy() && !showman) && (!(fld(v, "unlocked").is_bool() && !fld(v, "unlocked").boolean()) || fld(v, "rarity").num() == 4)) {
                Value set = fld(v, "set");
                if (set.equals("Voucher")) {
                    if (!used_vouchers->get(key).truthy()) {
                        bool include = true;
                        if (Table *req = fld(v, "requires").table())
                            req->each([&](const Value &, const Value &vv) {
                                if (!used_vouchers->get(vv).truthy()) include = false;
                            });
                        if (G.shop_vouchers)
                            for (auto &c : G.shop_vouchers->cards)
                                if (fld(c->config->get(K("center")), "key") == key) include = false;
                        add = include;
                    }
                } else if (set.equals("Planet")) {
                    Value soft = fld(fld(v, "config"), "softlock");
                    add = !soft.truthy() || fld(gt("hands")->get(fld(fld(v, "config"), "hand_type")), "played").num() > 0;
                } else if (fld(v, "enhancement_gate").truthy()) {
                    add = false;
                    for (auto &c : G.playing_cards)
                        if (fld(c->config->get(K("center")), "key") == fld(v, "enhancement_gate")) add = true;
                } else {
                    add = true;
                }
                if (fld(v, "name").equals("Black Hole") || fld(v, "name").equals("The Soul")) add = false;
            }
            if (fld(v, "no_pool_flag").truthy() && pool_flags->get(fld(v, "no_pool_flag")).truthy()) add = false;
            if (fld(v, "yes_pool_flag").truthy() && !pool_flags->get(fld(v, "yes_pool_flag")).truthy()) add = false;
            if (add && !banned->get(key).truthy()) {
                pool->push(key);
                pool_size++;
            } else {
                pool->push(Value("UNAVAILABLE"));
            }
        }
    if (pool_size == 0) {
        pool = new_table();
        const char *fb = (t == "Tarot" || t == "Tarot_Planet") ? "c_strength"
                         : t == "Planet"                       ? "c_pluto"
                         : t == "Spectral"                     ? "c_incantation"
                         : t == "Voucher"                      ? "v_blank"
                         : t == "Tag"                          ? "tag_handy"
                                                               : "j_joker";
        pool->push(Value(fb));
    }
    return {pool, pool_key + (!legendary ? ante : "")};
}

Value get_next_voucher_key(bool from_tag) {
    auto [pool, key] = get_current_pool("Voucher");
    if (from_tag) key = "Voucher_fromtag";
    double s = pseudoseed(key);
    Value center = pseudorandom_element(Value(pool), &s);
    for (int it = 2; center.equals("UNAVAILABLE"); it++) {
        s = pseudoseed(key + "_resample" + std::to_string(it));
        center = pseudorandom_element(Value(pool), &s);
    }
    return center;
}

Value get_next_tag_key(const std::string &append) {
    auto [pool, key] = get_current_pool("Tag", Value(), false, append);
    double s = pseudoseed(key);
    Value tag = pseudorandom_element(Value(pool), &s);
    for (int it = 2; tag.equals("UNAVAILABLE"); it++) {
        s = pseudoseed(key + "_resample" + std::to_string(it));
        tag = pseudorandom_element(Value(pool), &s);
    }
    return tag;
}

void ease_background_colour_blind(int state, const char *blind_override) {
    Value bn = blind_override ? Value(blind_override) : Value();
    if (!bn.truthy()) {
        Blind *b = game_blind();
        if (b && !b->name.empty()) bn = Value(b->name);
    }
    std::string blindname = bn.truthy() ? std::string(bn.str()) : "Small Blind";
    if (blindname.empty()) blindname = "Small Blind";

    Table *dyn = G.colour("DYN_UI").table();
    TableRef main(dyn->get(K("MAIN")).table());
    if (state == SHOP) ease_colour(main, Value(mix_colours(G.colour("RED"), G.colour("BLACK"), 0.9f)));
    else if (state == TAROT_PACK) ease_colour(main, Value(mix_colours(G.colour("WHITE"), G.colour("BLACK"), 0.9f)));
    else if (state == SPECTRAL_PACK) ease_colour(main, Value(mix_colours(fld(G.colour("SECONDARY_SET"), "Spectral"), G.colour("BLACK"), 0.9f)));
    else if (state == STANDARD_PACK) ease_colour(main, G.colour("RED"));
    else if (state == BUFFOON_PACK) ease_colour(main, G.colour("FILTER"));
    else if (state == PLANET_PACK) ease_colour(main, Value(mix_colours(fld(G.colour("SECONDARY_SET"), "Planet"), G.colour("BLACK"), 0.9f)));
    else if (Blind *b = game_blind()) b->change_colour();

    auto bg = [](Value nc, Value sc, double contrast) {
        TableRef a = vl::T({{"new_colour", nc}, {"contrast", contrast}});
        if (sc.truthy()) a->set("special_colour", sc);
        ease_background_colour(a);
    };
    if (state == TAROT_PACK) bg(G.colour("PURPLE"), Value(darken(G.colour("BLACK"), 0.2f)), 1.5);
    else if (state == SPECTRAL_PACK) bg(fld(G.colour("SECONDARY_SET"), "Spectral"), Value(darken(G.colour("BLACK"), 0.2f)), 2);
    else if (state == STANDARD_PACK) bg(Value(darken(G.colour("BLACK"), 0.2f)), G.colour("RED"), 3);
    else if (state == BUFFOON_PACK) bg(G.colour("FILTER"), G.colour("BLACK"), 2);
    else if (state == PLANET_PACK) bg(G.colour("BLACK"), Value(), 3);
    else if (gv("won").truthy()) bg(fld(G.colour("BLIND"), "won"), Value(), 1);
    else if (blindname == "Small Blind" || blindname == "Big Blind") bg(fld(G.colour("BLIND"), "Small"), Value(), 1);
    else {
        Value boss_col = G.colour("BLACK");
        bool showdown = false;
        G.P_BLINDS->each([&](const Value &, const Value &v) {
            if (fld(v, "name").equals(blindname)) {
                if (fld(fld(v, "boss"), "showdown").truthy()) showdown = true;
                boss_col = fld(v, "boss_colour").truthy() ? fld(v, "boss_colour") : G.colour("BLACK");
            }
        });
        if (showdown) {
            ease_background_colour(vl::T({{"new_colour", G.colour("BLUE")}, {"special_colour", G.colour("RED")},
                                          {"tertiary_colour", Value(darken(G.colour("BLACK"), 0.4f))}, {"contrast", 3}}));
            return;
        }
        bg(Value(lighten(Value(mix_colours(boss_col, G.colour("BLACK"), 0.3f)), 0.1f)), boss_col, 2);
    }
}

namespace {
// ease_round(mod) / ease_ante(mod): the HUD counter with its popup
void ease_counter(const char *ui_name, const char *field, double mod, bool ante) {
    G.E_MANAGER->add_event(ev_immediate([ui_name, field, mod, ante] {
        UIElement *ui = hand_text_area(ui_name);
        std::string text = "+";
        Value col = G.colour("IMPORTANT");
        if (mod < 0) {
            text = ante ? "-" : "";
            col = G.colour("RED");
        }
        if (ante) {
            Table *rr = gt("round_resets");
            rr->set("ante", rr->get(K("ante")).num() + mod);
            check_and_set_high_score("furthest_ante", rr->get(K("ante")));
        } else {
            G.GAME->set(field, gv(field).num() + mod);
            check_and_set_high_score("furthest_round", gv("round"));
            check_and_set_high_score("furthest_ante", fld(gv("round_resets"), "ante"));
        }
        if (ui)
            if (auto *dt = dynamic_cast<DynaText *>(ui->config->get(K("object")).obj())) dt->update(0);
        if (UIBox *h = hud()) h->recalculate();
        attention_text(vl::T({{"text", Value(text + Value(std::fabs(mod)).to_string())}, {"scale", 1}, {"hold", 0.7},
                              {"cover", Value((Object *)(ui ? ui->parent : nullptr))}, {"cover_colour", col}, {"align", "cm"}}));
        if (ante) {
            play_sound("highlight2", 0.685f, 0.2f);
        } else {
            play_sound("timpani", 0.8f);
        }
        play_sound("generic1");
        return true;
    }));
}
}  // namespace

void ease_round(double mod) { ease_counter("round", "round", mod, false); }
void ease_ante(double mod) { ease_counter("ante", "ante", mod, true); }

void draw_card(CardArea *from, CardArea *to, double percent, const char *dir, bool sort, Card *card, double delay_, bool mute, bool stay_flipped,
               double vol, bool discarded_only) {
    if (dir && std::string_view(dir) == "down") percent = 1 - percent;
    Ref<CardArea> rf(from), rt(to);
    CardRef rc(card);
    EventConfig e;
    e.trigger = Trigger::Before;
    e.delay = delay_;
    e.func = [rf, rt, rc, percent, sort, mute, stay_flipped, vol, discarded_only] {
        bool drawn = false;
        CardArea *from = rf.get(), *to = rt.get();
        if (Card *card = rc.get()) {
            if (from) card = from->remove_card(card);
            if (card) drawn = true;
            bool sf = false;  // TODO(run): G.GAME.blind:stay_flipped(to, card)
            Value fc = fld(gv("modifiers"), "flipped_cards");
            if (fc.truthy() && G.hand && to == G.hand.get())
                if (pseudorandom(pseudoseed("flipped_card")) < 1 / fc.num()) sf = true;
            to->emplace(card, nullptr, sf);
        } else {
            if (to->draw_card_from(from, stay_flipped, discarded_only)) drawn = true;
        }
        if (!mute && drawn) {
            // (G.VIBRATION: no rumble on the Vita)
            play_sound("card1", (float)(0.85 + percent * 0.2 / 100), (float)(0.6 * vol));
        }
        if (sort) to->sort();
        return true;
    };
    G.E_MANAGER->add_event(e);
}

// ------------------------------------------------------------------ state_events.lua
void new_round() {
    G.RESET_JIGGLES = false;
    delay(0.4);
    G.E_MANAGER->add_event(ev_immediate([] {
        Table *cr = gt("current_round"), *rr = gt("round_resets"), *rb = gt("round_bonus");
        cr->set("discards_left", std::max(0.0, rr->get(K("discards")).num() + rb->get(K("discards")).num()));
        cr->set("hands_left", std::max(1.0, rr->get(K("hands")).num() + rb->get(K("next_hands")).num()));
        cr->set("hands_played", 0);
        cr->set("discards_used", 0);
        cr->set("reroll_cost_increase", 0);
        cr->set("used_packs", Value(new_table()));
        gt("hands")->each([](const Value &, const Value &v) { v.table()->set("played_this_round", 0); });
        for (auto &c : G.playing_cards) c->ability->set("wheel_flipped", Value());
        cr->set("free_rerolls", (double)find_joker("Chaos the Clown").size());
        calculate_reroll_cost(true);
        rb->set("next_hands", 0);
        rb->set("discards", 0);

        std::string blhash;
        Table *bs = rr->get(K("blind_states")).table();
        Value rblind = rr->get(K("blind"));
        if (rblind.truthy() && fld(rblind, "name").equals("Small Blind")) {
            bs->set("Small", "Current");
            G.GAME->set("current_boss_streak", 0);
            blhash = "S";
        } else if (rblind.truthy() && fld(rblind, "name").equals("Big Blind")) {
            bs->set("Big", "Current");
            G.GAME->set("current_boss_streak", 0);
            blhash = "B";
        } else {
            bs->set("Boss", "Current");
            blhash = "L";
        }
        G.GAME->set("subhash", Value(ante_str() + blhash));
        game_blind()->set_blind(rblind);
        for (auto &j : std::vector<CardRef>(G.jokers->cards)) j->calculate_joker(vl::T({{"setting_blind", true}, {"blind", rblind}}));
        delay(0.4);
        G.E_MANAGER->add_event(ev_immediate([] {
            G.STATE = DRAW_TO_HAND;
            G.deck->shuffle("nr" + ante_str());
            G.deck->hard_set_T(G.deck->T->x, G.deck->T->y, G.deck->T->w, G.deck->T->h);
            G.STATE_COMPLETE = false;
            return true;
        }));
        return true;
    }));
}

bool draw_from_deck_to_hand(int e) {
    if (!(G.STATE == TAROT_PACK || G.STATE == SPECTRAL_PACK) && G.hand->config->get(K("card_limit")).num() <= 0 && G.hand->cards.empty()) {
        G.STATE = GAME_OVER;
        G.STATE_COMPLETE = false;
        return true;
    }
    int hand_space = e >= 0 ? e
                            : std::min((int)G.deck->cards.size(), (int)G.hand->config->get(K("card_limit")).num() - (int)G.hand->cards.size());
    Blind *b = game_blind();
    Table *cr = gt("current_round");
    if (b->name == "The Serpent" && !b->disabled && (cr->get(K("hands_played")).num() > 0 || cr->get(K("discards_used")).num() > 0))
        hand_space = std::min((int)G.deck->cards.size(), 3);
    delay(0.3);
    for (int i = 1; i <= hand_space; i++) draw_card(G.deck.get(), G.hand.get(), i * 100.0 / hand_space, "up", true);
    return false;
}

// ------------------------------------------------------------------ poker hands
// The hands are Lua-like tables of cards (Value(Card*)) because jokers read them by hand name.
namespace {
Card *card_at(const Value &t, int i) { return t.get(i).as<Card>(); }
TableRef cards_table(const std::vector<Card *> &v) {
    TableRef t = new_table();
    for (Card *c : v) t->push(Value((Object *)c));
    return t;
}
}  // namespace

TableRef get_X_same(int num, const std::vector<Card *> &hand) {
    std::vector<TableRef> vals(15);
    for (int i = (int)hand.size(); i >= 1; i--) {
        std::vector<Card *> curr{hand[i - 1]};
        for (int j = 1; j <= (int)hand.size(); j++)
            if (hand[i - 1]->get_id() == hand[j - 1]->get_id() && i != j) curr.push_back(hand[j - 1]);
        if ((int)curr.size() == num) {
            int id = curr[0]->get_id();
            if (id >= 1 && id <= 14) vals[id] = cards_table(curr);
            // (a Stone Card's random negative id lands outside vals[1..14])
        }
    }
    TableRef ret = new_table();
    for (int i = 14; i >= 1; i--)
        if (vals[i] && vals[i]->len() > 0) ret->push(Value(vals[i]));
    return ret;
}

TableRef get_flush(const std::vector<Card *> &hand) {
    TableRef ret = new_table();
    const bool four_fingers = !find_joker("Four Fingers").empty();
    const int need = 5 - (four_fingers ? 1 : 0);
    if ((int)hand.size() > 5 || (int)hand.size() < need) return ret;
    for (const char *suit : {"Spades", "Hearts", "Clubs", "Diamonds"}) {
        std::vector<Card *> t;
        for (Card *c : hand)
            if (c->is_suit(Value(suit), false, true)) t.push_back(c);
        if ((int)t.size() >= need) {
            ret->push(Value(cards_table(t)));
            return ret;
        }
    }
    return new_table();
}

TableRef get_straight(const std::vector<Card *> &hand) {
    TableRef ret = new_table();
    const bool four_fingers = !find_joker("Four Fingers").empty();
    const int need = 5 - (four_fingers ? 1 : 0);
    if ((int)hand.size() > 5 || (int)hand.size() < need) return ret;
    std::vector<std::vector<Card *>> IDS(15);
    std::vector<bool> has(15, false);
    for (Card *c : hand) {
        int id = c->get_id();
        if (id > 1 && id < 15) {
            IDS[id].push_back(c);
            has[id] = true;
        }
    }
    std::vector<Card *> t;
    int straight_length = 0;
    bool straight = false;
    const bool can_skip = !find_joker("Shortcut").empty();
    bool skipped_rank = false;
    for (int j = 1; j <= 14; j++) {
        int k = j == 1 ? 14 : j;
        if (has[k]) {
            straight_length++;
            skipped_rank = false;
            for (Card *c : IDS[k]) t.push_back(c);
        } else if (can_skip && !skipped_rank && j != 14) {
            skipped_rank = true;
        } else {
            straight_length = 0;
            skipped_rank = false;
            if (!straight) t.clear();
            if (straight) break;
        }
        if (straight_length >= need) straight = true;
    }
    if (!straight) return ret;
    ret->push(Value(cards_table(t)));
    return ret;
}

TableRef get_highest(const std::vector<Card *> &hand) {
    Card *highest = nullptr;
    for (Card *c : hand)
        if (!highest || c->get_nominal() > highest->get_nominal()) highest = c;
    if (!hand.empty()) return A({Value(cards_table({highest}))});
    return new_table();
}

TableRef evaluate_poker_hand(const std::vector<Card *> &hand) {
    static const char *names[] = {"Flush Five", "Flush House", "Five of a Kind", "Straight Flush", "Four of a Kind", "Full House",
                                  "Flush", "Straight", "Three of a Kind", "Two Pair", "Pair", "High Card"};
    TableRef results = new_table();
    for (const char *n : names) results->set(n, Value(new_table()));
    auto R = [&](const char *n) { return TableRef(results->get(Value(n)).table()); };
    auto set_top = [&](const char *n) {
        if (!results->get(K("top")).truthy()) results->set("top", results->get(Value(n)));
    };
    auto concat = [](const Value &a, const Value &b) {
        TableRef h = new_table();
        if (Table *ta = a.table())
            for (int i = 1; i <= ta->len(); i++) h->push(ta->get(i));
        if (Table *tb = b.table())
            for (int i = 1; i <= tb->len(); i++) h->push(tb->get(i));
        return h;
    };
    TableRef _5 = get_X_same(5, hand), _4 = get_X_same(4, hand), _3 = get_X_same(3, hand), _2 = get_X_same(2, hand);
    TableRef _flush = get_flush(hand), _straight = get_straight(hand), _highest = get_highest(hand);
    auto any = [](const TableRef &t) { return t->len() > 0; };

    if (any(_5) && any(_flush)) {
        results->set("Flush Five", Value(_5));
        set_top("Flush Five");
    }
    if (any(_3) && any(_2) && any(_flush)) {
        R("Flush House")->push(Value(concat(_3->get(1), _2->get(1))));
        set_top("Flush House");
    }
    if (any(_5)) {
        results->set("Five of a Kind", Value(_5));
        set_top("Five of a Kind");
    }
    if (any(_flush) && any(_straight)) {
        TableRef ret = new_table();
        Table *f = _flush->get(1).table(), *s = _straight->get(1).table();
        for (int i = 1; i <= f->len(); i++) ret->push(f->get(i));
        for (int i = 1; i <= s->len(); i++) {
            bool in = false;
            for (int j = 1; j <= f->len(); j++)
                if (f->get(j).obj() == s->get(i).obj()) in = true;
            if (!in) ret->push(s->get(i));
        }
        results->set("Straight Flush", Value(A({Value(ret)})));
        set_top("Straight Flush");
    }
    if (any(_4)) {
        results->set("Four of a Kind", Value(_4));
        set_top("Four of a Kind");
    }
    if (any(_3) && any(_2)) {
        R("Full House")->push(Value(concat(_3->get(1), _2->get(1))));
        set_top("Full House");
    }
    if (any(_flush)) {
        results->set("Flush", Value(_flush));
        set_top("Flush");
    }
    if (any(_straight)) {
        results->set("Straight", Value(_straight));
        set_top("Straight");
    }
    if (any(_3)) {
        results->set("Three of a Kind", Value(_3));
        set_top("Three of a Kind");
    }
    if (_2->len() == 2 || (_3->len() == 1 && _2->len() == 1)) {
        Value b = _2->get(2).truthy() ? _2->get(2) : _3->get(1);
        R("Two Pair")->push(Value(concat(_2->get(1), b)));
        set_top("Two Pair");
    }
    if (any(_2)) {
        results->set("Pair", Value(_2));
        set_top("Pair");
    }
    if (any(_highest)) {
        results->set("High Card", Value(_highest));
        set_top("High Card");
    }
    auto first_n = [&](const char *from, int n) {
        Table *t = R(from).get();
        TableRef r = new_table();
        for (int i = 1; i <= n; i++) r->set(i, t->get(i));  // {t[1], ..., t[n]}: trailing nils are dropped
        return r;
    };
    if (R("Five of a Kind")->get(1).truthy()) results->set("Four of a Kind", Value(first_n("Five of a Kind", 4)));
    if (R("Four of a Kind")->get(1).truthy()) results->set("Three of a Kind", Value(first_n("Four of a Kind", 3)));
    if (R("Three of a Kind")->get(1).truthy()) results->set("Pair", Value(first_n("Three of a Kind", 2)));
    return results;
}

void set_hand_usage(const std::string &hand_label) {
    std::string hand;
    for (char c : hand_label)
        if (!std::isspace((unsigned char)c)) hand += c;
    auto bump = [&](Table *usage) {
        if (!usage) return;
        Value u = usage->get(Value(hand));
        if (u.truthy()) u.table()->set("count", u.get(K("count")).num() + 1);
        else usage->set(Value(hand), Value(vl::T({{"count", 1}, {"order", Value(hand_label)}})));
    };
    bump(G.PROFILES->get(G.SETTINGS->get(K("profile"))).get(K("hand_usage")).table());
    bump(G.GAME->get(K("hand_usage")).table());
    G.save_settings();
}

// G.FUNCS.get_poker_hand_info(_cards): text, loc_disp_text, poker_hands, scoring_hand, disp_text
PokerHandInfo get_poker_hand_info(const std::vector<Card *> &cards) {
    PokerHandInfo info;
    info.poker_hands = evaluate_poker_hand(cards);
    info.scoring_hand = new_table();
    info.text = "NULL";
    static const char *order[] = {"Flush Five", "Flush House", "Five of a Kind", "Straight Flush", "Four of a Kind", "Full House",
                                  "Flush", "Straight", "Three of a Kind", "Two Pair", "Pair", "High Card"};
    for (const char *n : order) {
        Table *t = info.poker_hands->get(Value(n)).table();
        bool any = false;
        t->each([&](const Value &, const Value &) { any = true; });  // next(poker_hands[n])
        if (any) {
            info.text = n;
            info.scoring_hand = TableRef(t->get(1).table());
            if (!info.scoring_hand) info.scoring_hand = new_table();
            break;
        }
    }
    info.disp_text = info.text;
    if (info.text == "Straight Flush") {
        int min = 10;
        for (int j = 1; j <= info.scoring_hand->len(); j++) {
            int id = card_at(Value(info.scoring_hand), j)->get_id();
            if (id < min) min = id;
        }
        if (min >= 10) info.disp_text = "Royal Flush";
    }
    info.loc_disp_text = localize(info.disp_text, "poker_hands");
    return info;
}

// ------------------------------------------------------------------ G.FUNCS of the run
namespace {
UIBox *as_box(const NodeRef &n) { return dynamic_cast<UIBox *>(n.get()); }
Value cfgv(UIElement *e, const char *k) { return e->config->get(Value(intern(k))); }
UIElement *child(Node *n, int i) { return n && i < n->children.count() ? dynamic_cast<UIElement *>(n->children.at(i)) : nullptr; }
DynaText *dyna_of(UIElement *e) { return e ? dynamic_cast<DynaText *>(cfgv(e, "object").obj()) : nullptr; }

void text_super_juice(UIElement *e, double amount) {
    DynaText *t = dyna_of(e);
    if (!t) return;
    t->set_quiver(0.03 * amount);
    t->pulse(0.3 + 0.08 * amount);
    t->update_text();
    t->align_letters();
    e->update_object();
}

void select_blind(UIElement *e) {
    stop_use();
    if (!G.blind_select) return;
    G.GAME->set("facing_blind", true);
    UIBox *pb = as_box(G.blind_prompt_box);
    for (const char *id : {"prompt_dynatext1", "prompt_dynatext2"})
        if (DynaText *t = dyna_of(pb->get_UIE_by_ID(id))) {
            t->pop_delay = 0;
            t->has_pop_delay = true;
            t->pop_out(5);
        }
    EventConfig a;
    a.trigger = Trigger::Before;
    a.delay = 0.2;
    a.func = [] {
        as_box(G.blind_prompt_box)->alignment.offset.y = -10;
        as_box(G.blind_select)->alignment.offset.y = 40;
        as_box(G.blind_select)->alignment.offset.x = 0;
        return true;
    };
    G.E_MANAGER->add_event(a);
    Value blind = cfgv(e, "ref_table");
    G.E_MANAGER->add_event(ev_immediate([blind] {
        ease_round(1);
        inc_career_stat("c_rounds", 1);
        Table *rr = gt("round_resets");
        rr->set("blind", blind);
        rr->get(K("blind_states")).table()->set(gv("blind_on_deck"), "Current");
        G.blind_select->remove();
        G.blind_prompt_box->remove();
        G.blind_select = nullptr;
        delay(0.2);
        return true;
    }));
    G.E_MANAGER->add_event(ev_immediate([] {
        new_round();
        return true;
    }));
}

void blind_choice_handler(UIElement *e) {
    Table *ref = cfgv(e, "ref_table").table();
    UIBox *bs = as_box(G.blind_select);
    if (!ref || ref->get(K("run_info")).truthy() || !bs || bs->VT.y >= 10 || !cfgv(e, "id").truthy()) return;
    std::string id(cfgv(e, "id").str()), lid = id;
    lid[0] = (char)std::tolower(lid[0]);
    auto it = G.blind_select_opts.find(lid);
    if (it == G.blind_select_opts.end() || !it->second) return;
    UIBox *box = e->box;
    if (box->role.xy_bond != Bond::Weak) {
        static const Bond weak = Bond::Weak;
        RoleArgs ra;
        ra.xy_bond = &weak;
        box->set_role(ra);
    }
    const bool on_deck = gv("blind_on_deck").equals(id);
    Value deck = ref->get(K("deck"));
    if (!((!deck.equals("on") && on_deck) || (!deck.equals("off") && !on_deck))) return;
    UIBox *choice = as_box(it->second);
    UIElement *top = box->get_UIE_by_ID("select_blind_button");
    UIElement *border = child(child(box->UIRoot.get(), 0), 0);
    UIElement *tag = box->get_UIE_by_ID("tag_" + id);
    UIElement *tag_container = box->get_UIE_by_ID("tag_container");
    Value tp = G.SETTINGS->get(K("tutorial_progress"));
    if (tag_container && !G.SETTINGS->get(K("tutorial_complete")).truthy() && !fld(fld(tp, "completed_parts"), "shop_1").truthy())
        tag_container->states.visible = false;
    else if (tag_container)
        tag_container->states.visible = true;
    UIElement *bp = border ? border->parent_uie() : nullptr;
    if (on_deck) {
        ref->set("deck", "on");
        e->config->set("draw_after", false);
        e->config->set("colour", G.colour("CLEAR"));
        if (bp) {
            bp->config->set("outline", 2);
            bp->config->set("outline_colour", fld(G.colour("UI"), "TRANSPARENT_DARK"));
        }
        if (border) {
            border->config->set("outline_colour",
                                border->config->get(K("outline")).truthy() ? border->config->get(K("outline_colour")) : Value(get_blind_main_colour(Value(id))));
            border->config->set("outline", 1.5);
        }
        choice->alignment.offset.y = -0.9f;
        if (tag && tag_container) {
            UIElement *c2 = child(tag_container, 1), *t2 = child(tag, 1);
            c2->config->set("draw_after", false);
            c2->config->set("colour", G.colour("BLACK"));
            t2->config->set("button", "skip_blind");
            tag->config->set("outline_colour", Value(adjust_alpha(G.colour("BLUE"), 0.5f)));
            t2->config->set("hover", true);
            t2->config->set("colour", G.colour("RED"));
            child(t2, 0)->config->set("colour", fld(G.colour("UI"), "TEXT_LIGHT"));
            if (auto *s = dynamic_cast<Node *>(tag->config->get(K("ref_table")).obj())) s->config->set("force_focus", Value());
        }
        if (top) {
            Ref<UIElement> t(top);
            G.E_MANAGER->add_event(ev_immediate([t] {
                G.CONTROLLER->snap_to(t.get());
                return true;
            }));
            top->config->set("button", "select_blind");
            top->config->set("colour", G.colour("FILTER"));
            top->config->set("hover", true);
            child(top, 0)->config->set("colour", G.colour("WHITE"));
        }
    } else {
        ref->set("deck", "off");
        e->config->set("draw_after", true);
        Value st = gt("round_resets")->get(K("blind_states")).get(Value(id));
        e->config->set("colour",
                       Value(adjust_alpha(st.equals("Skipped") ? Value(mix_colours(G.colour("BLUE"), G.colour("L_BLACK"), 0.1f)) : G.colour("L_BLACK"), 0.5f)));
        if (bp) {
            bp->config->set("outline", Value());
            bp->config->set("outline_colour", Value());
        }
        if (border) {
            border->config->set("outline_colour", Value());
            border->config->set("outline", Value());
        }
        choice->alignment.offset.y = -0.2f;
        if (tag && tag_container) {
            if (st.equals("Skipped") || st.equals("Defeated")) {
                static const Bond weak = Bond::Weak;
                RoleArgs ra;
                ra.xy_bond = &weak;
                for (int i : {1, 0}) {
                    UIElement *c = child(tag_container, i);
                    c->set_role(ra);
                    c->align(0, 10);
                }
            }
            if (st.equals("Skipped")) {
                UIBox *alert = new UIBox(create_UIBox_card_alert(vl::T({{"text_rot", -0.35}, {"no_bg", true}, {"text", localize("k_skipped_cap")}, {"bump_amount", 1},
                                                                        {"scale", 0.9}, {"maxw", 3.4}})),
                                         vl::T({{"align", "tmi"}, {"offset", Value(vl::T({{"x", 0}, {"y", 2.2}}))}, {"major", Value((Object *)choice)},
                                                {"parent", Value((Object *)choice)}}));
                choice->children.set("alert", alert);
            }
            UIElement *t2 = child(tag, 1);
            t2->config->set("button", Value());
            tag->config->set("outline_colour", fld(G.colour("UI"), "BACKGROUND_INACTIVE"));
            t2->config->set("hover", false);
            t2->config->set("colour", fld(G.colour("UI"), "BACKGROUND_INACTIVE"));
            child(t2, 0)->config->set("colour", fld(G.colour("UI"), "TEXT_INACTIVE"));
            if (auto *s = dynamic_cast<Node *>(tag->config->get(K("ref_table")).obj())) s->config->set("force_focus", true);
        }
        if (top) {
            top->config->set("colour", fld(G.colour("UI"), "BACKGROUND_INACTIVE"));
            top->config->set("button", Value());
            top->config->set("hover", false);
            child(top, 0)->config->set("colour", fld(G.colour("UI"), "TEXT_INACTIVE"));
        }
    }
}

void skip_blind(UIElement *e) {
    stop_use();
    G.CONTROLLER->locks["skip_blind"] = true;
    EventConfig unlock = ev_after(2.5, [] {
        G.CONTROLLER->locks.erase("skip_blind");
        return true;
    });
    unlock.no_delete = true;
    unlock.blocking = false;
    unlock.blockable = false;
    unlock.has_timer = true;
    unlock.timer = EventTimer::TOTAL;
    G.E_MANAGER->add_event(unlock);
    UIElement *tag_el = e->box->get_UIE_by_ID("tag_container");
    G.GAME->set("skips", gv("skips").num() + 1);
    if (!tag_el) return;
    if (Tag *tag = tag_el->config->get(K("ref_table")).as<Tag>()) add_tag(tag);
    Value skipped = gv("blind_on_deck").truthy() ? gv("blind_on_deck") : Value("Small");
    Value skip_to = gv("blind_on_deck").equals("Small") ? Value("Big") : Value("Boss");
    Table *bs = gt("round_resets")->get(K("blind_states")).table();
    bs->set(skipped, "Skipped");
    bs->set(skip_to, "Select");
    G.GAME->set("blind_on_deck", skip_to);
    play_sound("generic1");
    G.E_MANAGER->add_event(ev_immediate([] {
        delay(0.3);
        for (auto &j : std::vector<CardRef>(G.jokers->cards)) j->calculate_joker(vl::T({{"skip_blind", true}}));
        save_run();
        Table *tags = gt("tags");
        for (int i = 1; i <= tags->len(); i++)
            if (Tag *t = tags->get(i).as<Tag>()) t->apply_to_run(vl::T({{"type", "immediate"}}));
        for (int i = 1; i <= tags->len(); i++)
            if (Tag *t = tags->get(i).as<Tag>())
                if (t->apply_to_run(vl::T({{"type", "new_blind_choice"}})).truthy()) break;
        return true;
    }));
}

void start_run_fn(UIElement *e) { start_run_with(e, nullptr); }
}  // namespace

void start_run_with(UIElement *e, const TableRef &args) {
    // G.FUNCS.start_run(e, args). TODO(run): the run setup screen (G.FUNCS.setup_run) to pick the deck and stake
    G.SETTINGS->set("paused", true);
    if (e && e->config->get(K("id")).equals("restart_button")) G.GAME->set("viewed_back", Value());
    G.E_MANAGER->clear_queue();
    wipe_on();
    EventConfig a = ev_immediate([] {
        G.delete_run();
        return true;
    });
    a.no_delete = true;
    G.E_MANAGER->add_event(a);
    EventConfig b = ev_immediate([args] {
        G.start_run(args);
        return true;
    });
    b.no_delete = true;
    G.E_MANAGER->add_event(b);
    wipe_off();
}

namespace {

std::function<void(UIElement *)> hand_set(const char *field, const char *text_field, double scale, double max) {
    return [=](UIElement *e) {
        Table *ch = gt("current_round")->get(K("current_hand")).table();
        std::string t = number_format(ch->get(Value(intern(field))));
        if (!ch->get(Value(intern(text_field))).equals(t)) {
            ch->set(text_field, Value(t));
            if (DynaText *d = dyna_of(e)) {
                d->scale = (float)scale_number(ch->get(Value(intern(field))), scale, max);
                d->update_text();
            }
            Value v = ch->get(Value(intern(field)));
            if (!G.TAROT_INTERRUPT_PULSE) text_super_juice(e, std::max(0.0, std::floor(std::log10(v.is_num() ? v.num() : 1))));
        }
    };
}
}  // namespace

void register_run_callbacks() {
    auto &F = G.FUNCS;
    F["HUD_blind_visible"] = [](UIElement *) {
        if (Blind *b = game_blind()) b->states.visible = !b->name.empty() && b->blind_set;
    };
    F["HUD_blind_debuff"] = [](UIElement *e) {
        Blind *b = game_blind();
        UIElement *p = e->parent_uie();
        if (!p) return;
        Value txt = b ? b->vars->get(K("loc_debuff_text")) : Value();
        if (b && txt.truthy() && !txt.equals("")) {
            if (p->config->get(K("minh")).num() == 0 || !(e->config->get(K("prev_loc")) == txt)) {
                p->config->set("minh", 0.35);
                e->config->set("scale", 0.36);
                if (b->vars->get(K("loc_debuff_lines")).get(e->config->get(K("ref_value"))).equals("")) {
                    e->config->set("scale", 0.0);
                    p->config->set("minh", 0.001);
                }
                e->config->set("prev_loc", txt);
                e->box->recalculate();
            }
        } else if (p->config->get(K("minh")).num() > 0) {
            p->config->set("minh", 0);
            e->config->set("scale", 0);
            e->box->recalculate();
        }
    };
    F["HUD_blind_debuff_prefix"] = [](UIElement *e) {
        Blind *b = game_blind();
        Table *rt = e->config->get(K("ref_table")).table();
        if ((b && b->name == "The Wheel" && !b->disabled) || e->config->get(K("id")).equals("bl_wheel")) {
            rt->set("val", Value(fld(gv("probabilities"), "normal").to_string()));
            e->config->set("scale", 0.32);
        } else {
            rt->set("val", "");
            e->config->set("scale", 0);
        }
    };
    F["HUD_blind_reward"] = [](UIElement *e) {
        Blind *b = game_blind();
        Value nbr = fld(gv("modifiers"), "no_blind_reward");
        UIElement *c1 = child(e, 0);
        if (nbr.truthy() && b && b->get_type() && nbr.get(Value(b->get_type())).truthy()) {
            if (e->config->get(K("minh")).num() > 0.44) {
                e->config->set("minh", 0.4);
                if (c1) c1->config->set("text", localize("k_no_reward"));
                e->box->recalculate();
            }
        } else if (e->config->get(K("minh")).num() < 0.45) {
            e->config->set("minh", 0.45);
            if (c1) c1->config->set("text", Value(localize("k_reward").to_string() + ": "));
            e->box->recalculate();
        }
    };
    F["chip_UI_set"] = [](UIElement *e) {
        std::string t = number_format(gv("chips"));
        if (!gv("chips_text").equals(t)) {
            e->config->set("scale", std::min(0.8, scale_number(gv("chips"), 1.1)));
            G.GAME->set("chips_text", Value(t));
        }
    };
    F["blind_chip_UI_scale"] = [](UIElement *e) {
        if (Blind *b = game_blind()) e->config->set("scale", scale_number(Value(b->chips), 0.7, 100000));
    };
    F["hand_mult_UI_set"] = hand_set("mult", "mult_text", 0.9, 1000);
    F["hand_chip_UI_set"] = hand_set("chips", "chip_text", 0.9, 1000);
    F["hand_chip_total_UI_set"] = [](UIElement *e) {
        Table *ch = gt("current_round")->get(K("current_hand")).table();
        double total = ch->get(K("chip_total")).num();
        if (total < 1) {
            ch->set("chip_total_text", "");
            return;
        }
        std::string t = number_format(Value(total));
        if (!ch->get(K("chip_total_text")).equals(t)) {
            if (DynaText *d = dyna_of(e)) d->scale = (float)scale_number(Value(total), 0.95, 100000000);
            ch->set("chip_total_text", Value(t));
            Value prev = G.ARGS->get(K("hand_chip_total_UI_set"));
            if (!prev.truthy() || prev.num() < total) text_super_juice(e, std::floor(std::log10(total)));
            G.ARGS->set("hand_chip_total_UI_set", total);
        }
    };
    F["flame_handler"] = [](UIElement *e) {
        Table *C = G.C.get();
        if (!C->get(K("UI_CHIPLICK")).truthy()) C->set("UI_CHIPLICK", Value(A({1, 1, 1, 1})));
        if (!C->get(K("UI_MULTLICK")).truthy()) C->set("UI_MULTLICK", Value(A({1, 1, 1, 1})));
        Table *chl = C->get(K("UI_CHIPLICK")).table(), *mul = C->get(K("UI_MULTLICK")).table();
        Table *uc = C->get(K("UI_CHIPS")).table(), *um = C->get(K("UI_MULT")).table(), *y = C->get(K("YELLOW")).table();
        for (int i = 1; i <= 3; i++) {
            auto lick = [&](Table *c) { return std::min(std::max(std::pow(c->get(i).num() * 0.5 + y->get(i).num() * 0.5 + 0.1, 2.0), 0.1), 1.0); };
            chl->set(i, lick(uc));
            mul->set(i, lick(um));
        }
        struct FH {
            const char *id, *arg_tab, *colour, *accent;
            int ID = -1;
        };
        static FH handlers[2] = {{"flame_chips", "chip_flames", "UI_CHIPS", "UI_CHIPLICK"}, {"flame_mult", "mult_flames", "UI_MULT", "UI_MULTLICK"}};
        for (FH &v : handlers) {
            if (!cfgv(e, "id").equals(v.id)) continue;
            Node *o = e->config->get(K("object")).obj() ? dynamic_cast<Node *>(e->config->get(K("object")).obj()) : nullptr;
            Sprite *sp = dynamic_cast<Sprite *>(o);
            if (!sp || sp->ID != v.ID) {
                if (o) o->remove();
                SpritePos pos;
                pos.x = 2;
                pos.y = 0;
                sp = new Sprite(0, 0, 2.5f, 2.5f, &G.ASSET_ATLAS["ui_1"], pos);
                e->config->set("object", Value((Object *)sp));
                v.ID = sp->ID;
                TableRef F = vl::T({{"intensity", 0}, {"real_intensity", 0}, {"intensity_vel", 0}, {"colour_1", C->get(Value(v.colour))},
                                    {"colour_2", C->get(Value(v.accent))}, {"timer", G.TIMERS.REAL}});
                G.ARGS->set(v.arg_tab, Value(F));
                AlignArgs al;
                al.major = dynamic_cast<Moveable *>(e->parent);
                static const Bond weak = Bond::Weak;
                al.bond = &weak;
                al.type = "bmi";
                XY off{0, 0};
                al.offset = &off;
                sp->set_alignment(al);
                DrawStep st;
                st.shader = gfx::ShaderId::Flame;
                Ref<Sprite> sr(sp);
                const float id = (float)sp->ID;
                auto fget = [F](const char *k) { return [F, k] { return F->get(Value(intern(k))); }; };
                st.send = {{"time", fget("timer")},
                           {"amount", fget("real_intensity")},
                           {"image_details", [sr] { return Value(A({sr->image_dims[0], sr->image_dims[1]})); }},
                           {"texture_details",
                            [sr] {
                                float p[4];
                                sr->get_pos_pixel(p);
                                return Value(A({p[0], p[1], p[2], p[3]}));
                            }},
                           {"colour_1", fget("colour_1")},
                           {"colour_2", fget("colour_2")},
                           {"id", [id] { return Value(id); }}};
                sp->define_draw_steps({st});
            }
            Table *F = G.ARGS->get(Value(v.arg_tab)).table();
            const double dt = G.real_dt;
            const double exptime = std::exp(-0.4 * dt);
            // G.ARGS.score_intensity, set by modulate_sound before the frame's events
            const Value si = G.ARGS->get(K("score_intensity"));
            const double earned = fld(si, "earned_score").num(), required = fld(si, "required_score").num();
            double intensity = 0;
            if (earned >= required && required > 0)
                intensity = ((G.pack_cards && !G.pack_cards->REMOVED) || G.TAROT_INTERRUPT) ? 0 : std::max(0.0, std::log(earned) / std::log(5.0) - 2);
            F->set("intensity", intensity);
            double timer = F->get(K("timer")).num() + dt * (1 + intensity * 0.2);
            F->set("timer", timer);
            double vel = F->get(K("intensity_vel")).num(), real = F->get(K("real_intensity")).num();
            if (vel < 0) vel *= (1 - 10 * dt);
            vel = (1 - exptime) * (intensity - real) * dt * 25 + exptime * vel;
            real = std::max(0.0, real + vel);
            F->set("intensity_vel", vel);
            F->set("real_intensity", real);
            // flame.fs returns transparent below amount 0.1: skip the costly draw then (same image)
            if (auto *fs = dynamic_cast<Sprite *>(e->config->get(K("object")).obj())) fs->states.visible = real >= 0.1;
            Value chg = F->get(K("change"));
            F->set("change", (chg.truthy() ? chg.num() : 0) * (1 - 4. * dt) + (4. * dt) * (real < intensity - 0.0 ? 1 : 0) * real);
        }
    };
    F["hand_text_UI_set"] = [](UIElement *e) {
        Table *ch = gt("current_round")->get(K("current_hand")).table();
        Value hn = ch->get(K("handname"));
        if (!(hn == ch->get(K("handname_text")))) {
            ch->set("handname_text", hn);
            std::string s(hn.str());
            if (DynaText *d = dyna_of(e)) {
                d->scale = (float)(s.size() >= 13 ? 12 * 0.56 / s.size() : 2.4 / std::sqrt(s.size() + 5.0));
                d->update_text();
            }
        }
    };
    F["can_play"] = [](UIElement *e) {
        Blind *b = game_blind();
        size_t n = G.hand->highlighted.size();
        if (n == 0 || (b && b->block_play) || n > 5) {
            e->config->set("colour", fld(G.colour("UI"), "BACKGROUND_INACTIVE"));
            e->config->set("button", Value());
        } else {
            e->config->set("colour", G.colour("BLUE"));
            e->config->set("button", "play_cards_from_highlighted");
        }
    };
    F["can_discard"] = [](UIElement *e) {
        if (fld(gv("current_round"), "discards_left").num() <= 0 || G.hand->highlighted.empty()) {
            e->config->set("colour", fld(G.colour("UI"), "BACKGROUND_INACTIVE"));
            e->config->set("button", Value());
        } else {
            e->config->set("colour", G.colour("RED"));
            e->config->set("button", "discard_cards_from_highlighted");
        }
    };
    F["sort_hand_suit"] = [](UIElement *) {
        G.hand->sort("suit desc");
        play_sound("paper1");
    };
    F["sort_hand_value"] = [](UIElement *) {
        G.hand->sort("desc");
        play_sound("paper1");
    };
    F["select_blind"] = select_blind;
    F["blind_choice_handler"] = blind_choice_handler;
    F["hover_tag_proxy"] = [](UIElement *) {
        // TODO(ui): the tag's description popup
    };
    F["skip_blind"] = skip_blind;
    F["run_info"] = [](UIElement *) {
        // TODO(run): the run info overlay
    };
    F["play_cards_from_highlighted"] = [](UIElement *) { play_cards_from_highlighted(); };
    F["discard_cards_from_highlighted"] = [](UIElement *) { discard_cards_from_highlighted(); };
    F["cash_out"] = cash_out;
    F["can_sell_card"] = [](UIElement *e) {
        Card *c = cfgv(e, "ref_table").as<Card>();
        if (c && c->can_sell_card()) {
            e->config->set("colour", G.colour("GREEN"));
            e->config->set("button", "sell_card");
        } else {
            e->config->set("colour", fld(G.colour("UI"), "BACKGROUND_INACTIVE"));
            e->config->set("button", Value());
        }
    };
    F["sell_card"] = [](UIElement *e) {
        Card *card = cfgv(e, "ref_table").as<Card>();
        card->sell_card();
        for (auto &j : std::vector<CardRef>(G.jokers->cards))
            if (j.get() != card) j->calculate_joker(vl::T({{"selling_card", true}, {"card", Value((Object *)card)}}));
    };
    F["can_use_consumeable"] = [](UIElement *e) {
        // TODO(run): Card:can_use_consumeable (hito 4); until then the Use button stays inactive
        e->config->set("colour", fld(G.colour("UI"), "BACKGROUND_INACTIVE"));
        e->config->set("button", Value());
    };
    F["go_to_menu"] = [](UIElement *) {
        G.SETTINGS->set("paused", true);
        G.E_MANAGER->clear_queue();
        wipe_on();
        EventConfig a = ev_immediate([] {
            G.delete_run();
            return true;
        });
        a.no_delete = true;
        G.E_MANAGER->add_event(std::move(a));
        EventConfig b = ev_immediate([] {
            G.main_menu("game");
            return true;
        });
        b.no_delete = true;
        b.blockable = true;
        b.blocking = false;
        G.E_MANAGER->add_event(std::move(b));
        wipe_off();
    };
    F["start_run"] = start_run_fn;
    F["setup_run"] = start_run_fn;
}

}  // namespace vl
