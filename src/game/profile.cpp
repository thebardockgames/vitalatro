// misc_functions.lua: the profile's joker/deck usage, wins and losses, and the challenge unlock.
#include <algorithm>

#include "game/card.h"
#include "game/functions.h"
#include "game/g.h"

namespace vl {

namespace {
inline Value fld(const Value &t, const char *k) { return t.get(Value(intern(k))); }
Table *profile() { return G.PROFILES->get(G.SETTINGS->get(K("profile"))).table(); }
// profile[name], created when missing
Table *profile_table(const char *name) {
    Table *p = profile();
    if (!p->get(Value(intern(name))).is_table()) p->set(name, Value(new_table()));
    return p->get(Value(intern(name))).table();
}
TableRef usage_entry(const Value &order) { return vl::T({{"count", 1}, {"order", order}, {"wins", Value(new_table())}, {"losses", Value(new_table())}}); }
// t[k] = (t[k] or 0) + 1
void bump(Table *t, const Value &k) { t->set(k, t->get(k).num() + 1); }
Table *sub(Table *entry, const char *name) {
    if (!entry->get(Value(intern(name))).is_table()) entry->set(name, Value(new_table()));
    return entry->get(Value(intern(name))).table();
}
template <class F> void each_joker(F &&f) {
    for (auto &v : G.jokers->cards) {
        const Value key = v->config->get(K("center_key"));
        if (key.truthy() && v->ability->get(K("set")).equals("Joker")) f(v.get(), key);
    }
}
// G.GAME.selected_back.effect.center
Value back_center() {
    Back *b = G.GAME->get(K("selected_back")).as<Back>();
    return b && b->effect ? b->effect->get(K("center")) : Value();
}
}  // namespace

void set_joker_usage() {
    Table *ju = profile_table("joker_usage");
    each_joker([&](Card *v, const Value &key) {
        if (ju->get(key).is_table()) bump(ju->get(key).table(), Value("count"));
        else ju->set(key, Value(usage_entry(fld(v->config->get(K("center")), "order"))));
    });
    G.save_settings();
}

void set_joker_win() {
    Table *ju = profile_table("joker_usage");
    const Value stake = G.GAME->get(K("stake"));
    each_joker([&](Card *v, const Value &key) {
        if (!ju->get(key).is_table()) ju->set(key, Value(usage_entry(fld(v->config->get(K("center")), "order"))));
        bump(sub(ju->get(key).table(), "wins"), stake);
    });
    G.save_settings();
}

void set_joker_loss() {
    Table *ju = profile_table("joker_usage");
    const Value stake = G.GAME->get(K("stake"));
    each_joker([&](Card *, const Value &key) {
        if (ju->get(key).is_table()) bump(sub(ju->get(key).table(), "losses"), stake);
    });
    G.save_settings();
}

void set_deck_usage() {
    const Value c = back_center();
    if (!fld(c, "key").truthy()) return;
    Table *du = profile_table("deck_usage");
    const Value key = fld(c, "key");
    if (du->get(key).is_table()) bump(du->get(key).table(), Value("count"));
    else du->set(key, Value(usage_entry(fld(c, "order"))));
    G.save_settings();
}

void set_challenge_unlock() {
    Table *p = profile();
    if (p->get(K("all_unlocked")).truthy()) return;
    if (p->get(K("challenges_unlocked")).truthy()) {
        int comp = 0;
        Table *ch = G.CHALLENGES.get();
        const Value completed = fld(p->get(K("challenge_progress")), "completed");
        for (int i = 1; i <= ch->len(); i++) {
            const Value id = fld(ch->get(i), "id");
            if (id.truthy() && completed.get(id).truthy()) comp++;
        }
        p->set("challenges_unlocked", (double)std::min(ch->len(), comp + 5));
    } else {
        int deck_wins = 0;
        profile_table("deck_usage")->each([&](const Value &, const Value &v) {
            if (fld(v, "wins").get(1).truthy()) deck_wins++;
        });
        if (deck_wins >= 5 /* G.CHALLENGE_WINS */) {
            p->set("challenges_unlocked", 5);
            notify_alert(Value("b_challenge"), Value("Back"));
        }
    }
}

void set_deck_win() {
    const Value c = back_center();
    if (!fld(c, "key").truthy()) return;
    Table *du = profile_table("deck_usage");
    const Value key = fld(c, "key");
    if (!du->get(key).is_table()) du->set(key, Value(usage_entry(fld(c, "order"))));
    Table *wins = sub(du->get(key).table(), "wins");
    const int stake = (int)G.GAME->get(K("stake")).num();
    bump(wins, Value((double)stake));
    for (int i = 1; i <= stake; i++)
        if (!wins->get(i).truthy()) wins->set(i, 1);
    set_challenge_unlock();
    G.save_settings();
}

void set_deck_loss() {
    const Value c = back_center();
    if (!fld(c, "key").truthy()) return;
    Table *du = profile_table("deck_usage");
    const Value key = fld(c, "key");
    if (!du->get(key).is_table()) du->set(key, Value(usage_entry(fld(c, "order"))));
    bump(sub(du->get(key).table(), "losses"), G.GAME->get(K("stake")));
    G.save_settings();
}

}  // namespace vl
