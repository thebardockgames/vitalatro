// functions/misc_functions.lua
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/lua_sort.h"
#include "engine/random.h"
#include "engine/text.h"
#include "game/card.h"
#include "game/functions.h"
#include "game/g.h"

namespace vl {

namespace {
inline Value fld(const Value &t, const char *k) { return t.get(Value(intern(k))); }
Value profile() { return Value(G.PROFILES).get(G.SETTINGS->get(K("profile"))); }

// Lua's '<' between two keys (numbers numerically, strings with strcmp in the C locale)
bool lua_less(const Value &a, const Value &b) {
    if (a.is_num() && b.is_num()) return a.num() < b.num();
    if (a.is_str() && b.is_str()) return std::strcmp(std::string(a.str()).c_str(), std::string(b.str()).c_str()) < 0;
    return false;
}
int sort_id_of(const Value &v) {
    if (Card *c = v.as<Card>()) return c->sort_id;
    return (int)fld(v, "sort_id").num();
}
bool has_sort_id(const Value &v) {
    if (v.as<Card>()) return true;
    return v.is_table() && fld(v, "sort_id").truthy();
}
}  // namespace

// ------------------------------------------------------------------ seeded randomness
Value pseudorandom_element(const Value &t, const double *seed, Value *key_out) {
    if (seed) lua_math.seed(*seed);
    struct KV { Value k, v; };
    std::vector<KV> keys;
    if (Table *tt = t.table()) tt->each([&](const Value &k, const Value &v) { keys.push_back({k, v}); });
    if (!keys.empty() && keys[0].v.truthy() && has_sort_id(keys[0].v))
        lua_sort(keys, [](const KV &a, const KV &b) { return sort_id_of(a.v) < sort_id_of(b.v); });
    else
        lua_sort(keys, [](const KV &a, const KV &b) { return lua_less(a.k, b.k); });
    if (keys.empty()) return Value();
    Value key = keys[(size_t)lua_math.random((double)keys.size()) - 1].k;
    if (key_out) *key_out = key;
    return t.get(key);
}

double pseudoseed(std::string_view key) {
    if (key == "seed") return lua_math.random();
    Table *pr = G.GAME->get(K("pseudorandom")).table();
    Value k(key);
    if (!pr->get(k).truthy()) {
        Value s = pr->get(K("seed"));
        pr->set(k, pseudohash(std::string(key) + (s.truthy() ? s.to_string() : "")));
    }
    double v = std::fabs(round13(lua_fmod(2.134453429141 + pr->get(k).num() * 1.72431234, 1)));
    pr->set(k, v);
    return (v + pr->get(K("hashed_seed")).num()) / 2;
}

double pseudoseed(std::string_view key, std::string_view predict_seed) {
    double p = pseudohash(std::string(key) + std::string(predict_seed));
    p = std::fabs(round13(lua_fmod(2.134453429141 + p * 1.72431234, 1)));
    return (p + pseudohash(predict_seed)) / 2;
}

double pseudorandom(double seed) {
    lua_math.seed(seed);
    return lua_math.random();
}

double pseudorandom(double seed, double min, double max) {
    lua_math.seed(seed);
    return lua_math.random(min, max);
}

std::string random_string(int length, double seed) {
    lua_math.seed(seed);
    std::string ret;
    for (int i = 1; i <= length; i++) {
        int c;
        if (lua_math.random() > 0.7) c = (int)lua_math.random('1', '9');
        else if (lua_math.random() > 0.45) c = (int)lua_math.random('A', 'N');
        else c = (int)lua_math.random('P', 'Z');
        ret += (char)c;
    }
    return ret;  // already upper case
}

// ------------------------------------------------------------------ jokers / profile progress
std::vector<Card *> find_joker(std::string_view name, bool non_debuff) {
    std::vector<Card *> r;
    if (!G.jokers || G.jokers->cards_nil) return r;
    for (CardArea *a : {G.jokers.get(), G.consumeables.get()}) {
        if (!a) continue;
        for (auto &v : a->cards)
            if (v->ability->get(K("name")).equals(name) && (non_debuff || !v->debuff)) r.push_back(v.get());
    }
    return r;
}

int get_joker_win_sticker_index(const Value &center) {
    Value ju = fld(profile(), "joker_usage");
    Value u = ju.get(fld(center, "key"));
    if (ju.truthy() && u.truthy() && fld(u, "wins").truthy()) {
        double w = 0;
        fld(u, "wins").table()->each([&](const Value &k, const Value &) { w = std::max(k.num(), w); });
        return (int)w;
    }
    return 0;
}

Value get_joker_win_sticker(const Value &center) {
    Value ju = fld(profile(), "joker_usage");
    Value u = ju.get(fld(center, "key"));
    if (ju.truthy() && u.truthy() && fld(u, "wins").truthy()) {
        int w = get_joker_win_sticker_index(center);
        if (w > 0 && w <= (int)G.sticker_map.size()) return Value(G.sticker_map[w - 1]);
    }
    return Value();
}

int get_deck_win_stake(const Value &deck_key) {
    Value du = fld(profile(), "deck_usage");
    if (!deck_key.truthy()) {
        double w = 0;
        if (Table *t = du.table())
            t->each([&](const Value &, const Value &deck) {
                if (Table *wins = fld(deck, "wins").table()) wins->each([&](const Value &k, const Value &) { w = std::max(k.num(), w); });
            });
        return (int)w;  // (the second return value, the lowest win, is only used by the stake progress UI)
    }
    Value d = du.get(deck_key);
    if (d.truthy() && fld(d, "wins").truthy()) {
        double w = 0;
        fld(d, "wins").table()->each([&](const Value &k, const Value &) { w = std::max(k.num(), w); });
        return (int)w;
    }
    return 0;
}

void set_profile_progress() {
    if (!G.PROGRESS)
        G.PROGRESS = vl::T({{"joker_stickers", Value(vl::T({{"tally", 0}, {"of", 0}}))},
                            {"deck_stakes", Value(vl::T({{"tally", 0}, {"of", 0}}))},
                            {"challenges", Value(vl::T({{"tally", 0}, {"of", 0}}))}});
    G.PROGRESS->each([](const Value &, const Value &v) {
        if (Table *t = v.table()) {
            t->set("tally", 0);
            t->set("of", 0);
        }
    });
    Table *ds = G.PROGRESS->get(K("deck_stakes")).table(), *js = G.PROGRESS->get(K("joker_stickers")).table();
    double n_stakes = G.P_CENTER_POOLS->get(K("Stake")).table()->len();
    G.P_CENTERS->each([&](const Value &, const Value &v) {
        if (fld(v, "set").equals("Back") && !fld(v, "omit").truthy()) {
            ds->set("of", ds->get(K("of")).num() + n_stakes);
            ds->set("tally", ds->get(K("tally")).num() + get_deck_win_stake(fld(v, "key")));
        }
        if (fld(v, "set").equals("Joker")) {
            js->set("of", js->get(K("of")).num() + n_stakes);
            js->set("tally", js->get(K("tally")).num() + get_joker_win_sticker_index(v));
        }
    });
    Table *ch = G.PROGRESS->get(K("challenges")).table();
    Value completed = fld(fld(profile(), "challenge_progress"), "completed");
    G.CHALLENGES->each([&](const Value &, const Value &v) {
        ch->set("of", ch->get(K("of")).num() + 1);
        if (completed.get(fld(v, "id")).truthy()) ch->set("tally", ch->get(K("tally")).num() + 1);
    });
    Table *prog = fld(profile(), "progress").table();
    prog->set("joker_stickers", Value(js->deep_copy()));
    prog->set("deck_stakes", Value(ds->deep_copy()));
    prog->set("challenges", Value(ch->deep_copy()));
}

void set_discover_tallies() {
    static const char *names[] = {"blinds",   "tags",     "jokers",   "consumeables", "tarots", "planets",
                                  "spectrals", "vouchers", "boosters", "editions",     "backs",  "total"};
    if (!G.DISCOVER_TALLIES) {
        G.DISCOVER_TALLIES = new_table();
        for (const char *n : names) G.DISCOVER_TALLIES->set(n, Value(vl::T({{"tally", 0}, {"of", 0}})));
    }
    G.DISCOVER_TALLIES->each([](const Value &, const Value &v) {
        v.table()->set("tally", 0);
        v.table()->set("of", 0);
    });
    auto bump = [](const char *name, bool tally) {
        Table *t = G.DISCOVER_TALLIES->get(name).table();
        t->set("of", t->get(K("of")).num() + 1);
        if (tally) t->set("tally", t->get(K("tally")).num() + 1);
    };
    G.P_CENTERS->each([&](const Value &, const Value &v) {
        if (fld(v, "omit").truthy()) return;
        Value set = fld(v, "set");
        bool disc = fld(v, "discovered").truthy();
        bool cons = fld(v, "consumeable").truthy();
        if (set.truthy() && (set.equals("Joker") || cons || set.equals("Edition") || set.equals("Voucher") || set.equals("Back") ||
                             set.equals("Booster")))
            bump("total", disc);
        if (set.equals("Joker")) bump("jokers", disc);
        if (set.equals("Back")) bump("backs", fld(v, "unlocked").truthy());
        if (set.truthy() && cons) {
            bump("consumeables", disc);
            if (set.equals("Planet")) bump("planets", disc);
            else if (set.equals("Spectral")) bump("spectrals", disc);
            else if (set.equals("Tarot")) bump("tarots", disc);
        }
        if (set.equals("Voucher")) bump("vouchers", disc);
        if (set.equals("Booster")) bump("boosters", disc);
        if (set.equals("Edition")) bump("editions", disc);
    });
    G.P_BLINDS->each([&](const Value &, const Value &v) {
        bool d = fld(v, "discovered").truthy();
        bump("total", d);
        bump("blinds", d);
    });
    G.P_TAGS->each([&](const Value &, const Value &v) {
        bool d = fld(v, "discovered").truthy();
        bump("total", d);
        bump("tags", d);
    });
    Value total = G.DISCOVER_TALLIES->get(K("total"));
    Value hs = fld(fld(profile(), "high_scores"), "collection");
    hs.table()->set("amt", fld(total, "tally"));
    hs.table()->set("tot", fld(total, "of"));
    fld(profile(), "progress").table()->set("discovered", Value(total.table()->deep_copy()));
    check_for_unlock(vl::T({{"type", "discover_amount"},
                            {"amount", fld(total, "tally")},
                            {"planet_count", fld(G.DISCOVER_TALLIES->get(K("planets")), "tally")},
                            {"tarot_count", fld(G.DISCOVER_TALLIES->get(K("tarots")), "tally")}}));
}

// ------------------------------------------------------------------ localization
TableRef loc_parse_string(std::string_view line) {
    TableRef parsed = new_table();
    TableRef control = new_table();
    bool c = false, c_gather = false, s_gather = false;
    std::string c_name, c_val, s_ref;
    bool has_c_name = false, has_c_val = false, has_s_ref = false;
    TableRef str_parts = new_table();
    int str_it = 1;
    auto flush = [&] {
        if (str_parts->get(1).truthy()) parsed->push(Value(vl::T({{"strings", Value(str_parts)}, {"control", Value(control)}})));
    };
    for (size_t i = 0; i < line.size(); i++) {
        char ch = line[i];
        if (ch == '{') {
            flush();
            str_parts = new_table();
            str_it = 1;
            control = new_table();
            c = true;
            c_gather = false;
            has_c_name = has_c_val = false;
            c_name.clear();
            c_val.clear();
            s_gather = false;
            has_s_ref = false;
            s_ref.clear();
        } else if (c && !(ch == ':' || ch == '}') && !c_gather) {
            c_name += ch;
            has_c_name = true;
        } else if (c && ch == ':') {
            c_gather = true;
        } else if (c && !(ch == ',' || ch == '}') && c_gather) {
            c_val += ch;
            has_c_val = true;
        } else if (c && (ch == ',' || ch == '}')) {
            c_gather = false;
            if (has_c_name) control->set(Value(c_name), has_c_val ? Value(c_val) : Value());
            has_c_name = has_c_val = false;
            c_name.clear();
            c_val.clear();
            if (ch == '}') c = false;
        } else if (!c && ch != '#' && !s_gather) {
            Value cur = str_parts->get(str_it);
            std::string add(1, ch);
            if (control->get(K("X")).truthy() && std::isspace((unsigned char)ch)) add.clear();  // char:gsub("%s+", "")
            str_parts->set(str_it, Value((cur.is_str() ? std::string(cur.str()) : std::string()) + add));
        } else if (!c && ch == '#' && !s_gather) {
            s_gather = true;
            if (str_parts->get(str_it).truthy()) str_it++;
        } else if (!c && ch == '#' && s_gather) {
            s_gather = false;
            if (has_s_ref) {
                str_parts->set(str_it, Value(A({Value(s_ref)})));
                str_it++;
                has_s_ref = false;
                s_ref.clear();
            }
        } else if (!c && s_gather) {
            s_ref += ch;
            has_s_ref = true;
        }
        if (i == line.size() - 1) {
            flush();
            return parsed;
        }
    }
    return TableRef();  // empty line: the original returns nil
}

void init_localization() {
    Value L(G.localization);
    Table *misc = fld(L, "misc").table();
    TableRef vdp = new_table();
    fld(Value(misc), "v_dictionary").table()->each([&](const Value &k, const Value &v) {
        if (Table *vt = v.table()) {
            TableRef m = vl::T({{"multi_line", true}});
            for (int i = 1; i <= vt->len(); i++) m->set(i, Value(loc_parse_string(vt->get(i).str())));
            vdp->set(k, Value(m));
        } else {
            vdp->set(k, Value(loc_parse_string(v.str())));
        }
    });
    misc->set("v_dictionary_parsed", Value(vdp));
    TableRef vtp = new_table();
    fld(Value(misc), "v_text").table()->each([&](const Value &k, const Value &v) {
        TableRef m = new_table();
        Table *vt = v.table();
        for (int i = 1; vt && i <= vt->len(); i++) m->set(i, Value(loc_parse_string(vt->get(i).str())));
        vtp->set(k, Value(m));
    });
    misc->set("v_text_parsed", Value(vtp));
    auto multi = [&](const Value &src) {
        TableRef out = new_table();
        if (Table *t = src.table())
            t->each([&](const Value &k, const Value &v) {
                TableRef m = vl::T({{"multi_line", true}});
                Table *vt = v.table();
                for (int i = 1; vt && i <= vt->len(); i++) m->set(i, Value(loc_parse_string(vt->get(i).str())));
                out->set(k, Value(m));
            });
        return out;
    };
    G.localization->set("tutorial_parsed", Value(multi(fld(Value(misc), "tutorial"))));
    G.localization->set("quips_parsed", Value(multi(fld(Value(misc), "quips"))));
    fld(L, "descriptions").table()->each([&](const Value &, const Value &set) {
        set.table()->each([&](const Value &, const Value &center) {
            Table *c = center.table();
            TableRef tp = new_table();
            c->set("text_parsed", Value(tp));
            Value text = c->get(K("text"));
            if (!text.truthy()) return;
            for (int i = 1; i <= text.table()->len(); i++) tp->push(Value(loc_parse_string(text[i].str())));
            TableRef np = new_table();
            Value name = c->get(K("name"));
            if (Table *nt = name.table()) {
                for (int i = 1; i <= nt->len(); i++) np->push(Value(loc_parse_string(nt->get(i).str())));
            } else {
                np->push(Value(loc_parse_string(name.str())));
            }
            c->set("name_parsed", Value(np));
            if (Table *u = c->get(K("unlock")).table()) {
                TableRef up = new_table();
                for (int i = 1; i <= u->len(); i++) up->push(Value(loc_parse_string(u->get(i).str())));
                c->set("unlock_parsed", Value(up));
            }
        });
    });
}

Value loc_colour(const Value &name, const Value &def) {
    static TableRef cols;
    if (!cols) {
        Value C(G.C);
        cols = vl::T({{"red", fld(C, "RED")},
                      {"mult", fld(C, "MULT")},
                      {"blue", fld(C, "BLUE")},
                      {"chips", fld(C, "CHIPS")},
                      {"green", fld(C, "GREEN")},
                      {"money", fld(C, "MONEY")},
                      {"gold", fld(C, "GOLD")},
                      {"attention", fld(C, "FILTER")},
                      {"purple", fld(C, "PURPLE")},
                      {"white", fld(C, "WHITE")},
                      {"inactive", fld(fld(C, "UI"), "TEXT_INACTIVE")},
                      {"spades", fld(fld(C, "SUITS"), "Spades")},
                      {"hearts", fld(fld(C, "SUITS"), "Hearts")},
                      {"clubs", fld(fld(C, "SUITS"), "Clubs")},
                      {"diamonds", fld(fld(C, "SUITS"), "Diamonds")},
                      {"tarot", fld(fld(C, "SECONDARY_SET"), "Tarot")},
                      {"planet", fld(fld(C, "SECONDARY_SET"), "Planet")},
                      {"spectral", fld(fld(C, "SECONDARY_SET"), "Spectral")},
                      {"edition", fld(C, "EDITION")},
                      {"dark_edition", fld(C, "DARK_EDITION")},
                      {"legendary", fld(C, "RARITY")[4]},
                      {"enhanced", fld(fld(C, "SECONDARY_SET"), "Enhanced")}});
    }
    Value r = name.truthy() ? cols->get(name) : Value();
    if (r.truthy()) return r;
    if (def.truthy()) return def;
    return G.colour("BLACK");
}

Value localize(std::string_view key, const char *misc_cat) {
    Value misc = fld(Value(G.localization), "misc");
    if (misc_cat) {
        Value cat = fld(misc, misc_cat);
        if (cat.truthy()) {
            Value r = cat.get(Value(key));
            return r.truthy() ? r : Value("ERROR");
        }
    }
    Value r = fld(misc, "dictionary").get(Value(key));
    return r.truthy() ? r : Value("ERROR");
}

namespace {
std::string assemble(const Value &strings, const Value &vars, const char *missing) {
    std::string out;
    Table *s = strings.table();
    for (int i = 1; s && i <= s->len(); i++) {
        Value sub = s->get(i);
        if (sub.is_str()) out += sub.str();
        else {
            Value v = vars.get(Value(std::strtod(std::string(sub[1].str()).c_str(), nullptr)));
            if (v.truthy()) out += v.to_string();
            else if (missing) out += missing;
        }
    }
    return out;
}
}  // namespace

Value localize(const TableRef &args) {
    Value L(G.localization);
    Value type = args->get(K("type"));
    Value vars = args->get(K("vars"));
    Value loc_target;
    if (type.equals("other")) loc_target = fld(fld(L, "descriptions"), "Other").get(args->get(K("key")));
    else if (type.equals("descriptions") || type.equals("unlocks"))
        loc_target = fld(L, "descriptions").get(args->get(K("set"))).get(args->get(K("key")));
    else if (type.equals("tutorial")) loc_target = fld(L, "tutorial_parsed").get(args->get(K("key")));
    else if (type.equals("quips")) loc_target = fld(L, "quips_parsed").get(args->get(K("key")));
    else if (type.equals("raw_descriptions")) {
        loc_target = fld(L, "descriptions").get(args->get(K("set"))).get(args->get(K("key")));
        TableRef multi_line = new_table();
        if (loc_target.truthy()) {
            Table *lines = fld(loc_target, "text_parsed").table();
            for (int i = 1; lines && i <= lines->len(); i++) {
                std::string final_line;
                Table *parts = lines->get(i).table();
                for (int j = 1; parts && j <= parts->len(); j++) final_line += assemble(fld(parts->get(j), "strings"), vars, "ERROR");
                multi_line->push(Value(final_line));
            }
        }
        return Value(multi_line);
    } else if (type.equals("text")) {
        loc_target = fld(fld(L, "misc"), "v_text_parsed").get(args->get(K("key")));
    } else if (type.equals("variable")) {
        loc_target = fld(fld(L, "misc"), "v_dictionary_parsed").get(args->get(K("key")));
        if (!loc_target.truthy()) return Value("ERROR");
        if (fld(loc_target, "multi_line").truthy()) {
            TableRef out = new_table();
            for (int k = 1; k <= loc_target.table()->len(); k++) out->set(k, Value(assemble(fld(loc_target[k][1], "strings"), vars, nullptr)));
            return Value(out);
        }
        return Value(assemble(fld(loc_target[1], "strings"), vars, nullptr));
    } else if (type.equals("name_text")) {
        Value set = args->get(K("set")), key = args->get(K("key"));
        if (!set.truthy() || !key.truthy()) {
            Value node_center;
            if (Node *n = args->get(K("node")).as<Node>()) node_center = n->config->get(K("center"));
            if (!set.truthy()) set = fld(node_center, "set");
            if (!key.truthy()) key = fld(node_center, "key");
        }
        Value s = fld(L, "descriptions").get(set);
        Value e = s.get(key);
        if (!s.is_table() || !e.is_table()) return Value("ERROR");
        return fld(e, "name");  // (nil when the entry has no name, as in the original)
    } else if (type.equals("name")) {
        Value set = args->get(K("set")), key = args->get(K("key"));
        if (!set.truthy() || !key.truthy()) {
            Value node_center;
            if (Node *n = args->get(K("node")).as<Node>()) node_center = n->config->get(K("center"));
            if (!set.truthy()) set = fld(node_center, "set");
            if (!key.truthy()) key = fld(node_center, "key");
        }
        loc_target = fld(L, "descriptions").get(set).get(key);
    }
    if (!loc_target.truthy()) return Value();

    Value line_src = type.equals("unlocks")                                                     ? fld(loc_target, "unlock_parsed")
                     : type.equals("name")                                                      ? fld(loc_target, "name_parsed")
                     : (type.equals("text") || type.equals("tutorial") || type.equals("quips")) ? loc_target
                                                                                                : fld(loc_target, "text_parsed");
    Table *lines = line_src.table();
    for (int li = 1; lines && li <= lines->len(); li++) {
        TableRef final_line = new_table();
        Table *parts = lines->get(li).table();
        for (int pi = 1; parts && pi <= parts->len(); pi++) {
            Value part = parts->get(pi);
            Value control = fld(part, "control");
            std::string s = assemble(fld(part, "strings"), vars, "ERROR");
            double desc_scale = G.LANG->font->DESCSCALE;
            if (G.F_MOBILE) desc_scale *= type.equals("tutorial") ? 1.5 : 1.45;
            auto num_or = [](const Value &v, double d) { return v.truthy() ? std::strtod(std::string(v.str()).c_str(), nullptr) : d; };
            Value cV = fld(control, "V"), cC = fld(control, "C"), cs = fld(control, "s");
            auto var_colour = [&](const Value &idx) { return fld(vars, "colours").get(Value(num_or(idx, 0))); };
            if (type.equals("name")) {
                Value col = cV.truthy() ? var_colour(cV) : Value();
                if (!col.truthy()) col = cC.truthy() ? loc_colour(cC) : fld(fld(Value(G.C), "UI"), "TEXT_LIGHT");
                DynaText *dt = new DynaText(vl::T({{"string", Value(A({Value(s)}))},
                                                   {"colours", Value(A({col}))},
                                                   {"bump", true},
                                                   {"silent", true},
                                                   {"pop_in", 0},
                                                   {"pop_in_rate", 4},
                                                   {"maxw", 5},
                                                   {"shadow", true},
                                                   {"y_offset", -0.6},
                                                   {"spacing", std::max(0.0, 0.32 * (17 - (double)s.size()))},
                                                   {"scale", (0.55 - 0.004 * s.size()) * num_or(cs, 1)}}));
                final_line->push(Value(vl::T({{"n", (double)UIT_O}, {"config", Value(vl::T({{"object", Value((Object *)dt)}}))}})));
            } else if (fld(control, "E").truthy()) {
                Value E = fld(control, "E");
                TableRef cfg = vl::T({{"string", Value(A({Value(s)}))}});
                Value col = cV.truthy() ? var_colour(cV) : loc_colour(cC);
                cfg->set("colours", Value(A({col})));
                cfg->set("silent", true);
                if (E.equals("1")) {
                    cfg->set("float", true);
                    cfg->set("pop_in", 0);
                } else if (E.equals("2")) {
                    cfg->set("bump", true);
                    cfg->set("spacing", 1);
                }
                cfg->set("scale", 0.32 * num_or(cs, 1) * desc_scale);
                DynaText *dt = new DynaText(cfg);
                final_line->push(Value(vl::T({{"n", (double)UIT_O}, {"config", Value(vl::T({{"object", Value((Object *)dt)}}))}})));
            } else if (fld(control, "X").truthy()) {
                final_line->push(Value(vl::T(
                    {{"n", (double)UIT_C},
                     {"config", Value(vl::T({{"align", "m"}, {"colour", loc_colour(fld(control, "X"))}, {"r", 0.05}, {"padding", 0.03}, {"res", 0.15}}))},
                     {"nodes", Value(A({Value(vl::T({{"n", (double)UIT_T},
                                                     {"config", Value(vl::T({{"text", Value(s)},
                                                                             {"colour", loc_colour(cC)},
                                                                             {"scale", 0.32 * num_or(cs, 1) * desc_scale}}))}}))}))}})));
            } else {
                Value col = cV.truthy() ? var_colour(cV) : loc_colour(cC, args->get(K("default_col")));
                Value tip;
                if (fld(control, "T").truthy()) {
                    tip = G.P_CENTERS->get(fld(control, "T"));
                    if (!tip.truthy()) tip = G.P_TAGS->get(fld(control, "T"));
                }
                final_line->push(Value(vl::T({{"n", (double)UIT_T},
                                              {"config", Value(vl::T({{"detailed_tooltip", tip},
                                                                      {"text", Value(s)},
                                                                      {"shadow", args->get(K("shadow"))},
                                                                      {"colour", col},
                                                                      {"scale", 0.32 * num_or(cs, 1) * desc_scale}}))}})));
            }
        }
        if (type.equals("name") || type.equals("text")) return Value(final_line);
        if (Table *nodes = args->get(K("nodes")).table()) nodes->push(Value(final_line));
    }
    return Value();
}

// ------------------------------------------------------------------ profiles / cloud flags
TableRef profile_init_table() {
    auto hs = [](const char *label) { return Value(vl::T({{"label", label}, {"amt", 0}})); };
    TableRef career = new_table();
    for (const char *k : {"c_round_interest_cap_streak", "c_dollars_earned", "c_shop_dollars_spent", "c_tarots_bought",
                          "c_planets_bought", "c_playing_cards_bought", "c_vouchers_bought", "c_tarot_reading_used",
                          "c_planetarium_used", "c_shop_rerolls", "c_cards_played", "c_cards_discarded", "c_losses", "c_wins",
                          "c_rounds", "c_hands_played", "c_face_cards_played", "c_jokers_sold", "c_cards_sold",
                          "c_single_hand_round_streak"})
        career->set(k, 0);
    return vl::T({{"MEMORY", Value(vl::T({{"deck", "Red Deck"}, {"stake", 1}}))},
                  {"stake", 1},
                  {"high_scores", Value(vl::T({{"hand", hs("Best Hand")},
                                               {"furthest_round", hs("Highest Round")},
                                               {"furthest_ante", hs("Highest Ante")},
                                               {"most_money", hs("Most Money")},
                                               {"boss_streak", hs("Most Bosses in a Row")},
                                               {"collection", Value(vl::T({{"label", "Collection"}, {"amt", 0}, {"tot", 1}}))},
                                               {"win_streak", hs("Best Win Streak")},
                                               {"current_streak", hs("")},
                                               {"poker_hand", hs("Most Played Hand")}}))},
                  {"career_stats", Value(career)},
                  {"progress", Value(new_table())},
                  {"joker_usage", Value(new_table())},
                  {"consumeable_usage", Value(new_table())},
                  {"voucher_usage", Value(new_table())},
                  {"hand_usage", Value(new_table())},
                  {"deck_usage", Value(new_table())},
                  {"deck_stakes", Value(new_table())},
                  {"challenge_progress", Value(vl::T({{"completed", Value(new_table())}, {"unlocked", Value(new_table())}}))}});
}

bool get_cloud_flag(std::string_view flag) {
    Value f(flag);
    if (G.SETTINGS->get(f).truthy()) {
        set_cloud_flag(flag);
        return true;
    }
    bool r = false;
    if (G.PROFILES)
        G.PROFILES->each([&](const Value &, const Value &v) {
            if (r || !v.is_table()) return;
            if (!fld(v, "flags").truthy()) v.table()->set("flags", Value(new_table()));
            if (fld(v, "flags").get(f).truthy()) r = true;
        });
    return r;
}

void set_cloud_flag(std::string_view flag) {
    Value f(flag);
    G.SETTINGS->set(f, true);
    if (G.PROFILES)
        G.PROFILES->each([&](const Value &, const Value &v) {
            if (!v.is_table()) return;
            if (!fld(v, "flags").truthy()) v.table()->set("flags", Value(new_table()));
            fld(v, "flags").table()->set(f, true);
        });
}

// ------------------------------------------------------------------ sprites / misc
std::pair<Atlas *, Value> get_front_spriteinfo(const Value &front) {
    auto atlas = [](const std::string &n) -> Atlas * {
        auto it = G.ASSET_ATLAS.find(n);
        return it == G.ASSET_ATLAS.end() ? nullptr : &it->second;
    };
    const char *cb = G.SETTINGS->get(K("colourblind_option")).truthy() ? "2" : "1";
    Value value = fld(front, "value");
    if (fld(front, "suit").truthy() && (value.equals("Jack") || value.equals("Queen") || value.equals("King"))) {
        Value cd = G.SETTINGS->get(K("CUSTOM_DECK"));
        Value collab = fld(cd, "Collabs").get(fld(front, "suit"));
        if (cd.truthy() && collab.truthy() && !collab.equals("default")) {
            if (Atlas *a = atlas(collab.to_string() + "_" + cb)) return {a, fld(fld(Value(G.COLLABS), "pos"), std::string(value.str()).c_str())};
        }
    }
    Value fa = fld(front, "atlas");
    Atlas *a = fa.truthy() ? atlas(fa.to_string()) : nullptr;
    if (!a) a = atlas(std::string("cards_") + cb);
    return {a, fld(front, "pos")};
}

void reset_drawhash() { G.DRAW_HASH.clear(); }

// With G.F_SOUND_THREAD the sources live on the sound thread: the main thread's SOURCES is the
// empty table from Game:init, so this is a no-op in the original (splash sounds keep fading with
// G.SPLASH_VOL after the splash ends).
void RESET_STATES(int) {}

Value get_stake_col(int stake) {
    static TableRef stakes;
    if (!stakes) {
        stakes = A({G.colour("WHITE"), G.colour("RED"), G.colour("GREEN"), G.colour("BLACK"), G.colour("BLUE"), G.colour("PURPLE"),
                    G.colour("ORANGE"), G.colour("GOLD")});
        G.C->set("STAKES", Value(stakes));
    }
    return stakes->get(stake);
}

std::string number_format(const Value &num) {
    if (!num.is_num()) return num.truthy() ? num.to_string() : "";
    double n = num.num();
    const double E_SWITCH_POINT = 100000000000.0;
    char buf[64];
    if (n >= E_SWITCH_POINT) {
        std::snprintf(buf, sizeof buf, "%.4g", n);
        double x = std::strtod(buf, nullptr);
        int fac = (int)std::floor(std::log(x) / std::log(10.0));
        char out[64];
        std::snprintf(out, sizeof out, "%.3f", x / std::pow(10.0, fac));
        return std::string(out) + "e" + std::to_string(fac);
    }
    const char *fmt = n != std::floor(n) ? (n >= 100 ? "%.0f" : n >= 10 ? "%.1f" : "%.2f") : "%.0f";
    std::snprintf(buf, sizeof buf, fmt, n);
    // :reverse():gsub("(%d%d%d)", "%1,"):gsub(",$", ""):reverse()
    std::string s(buf);
    std::reverse(s.begin(), s.end());
    std::string r;
    size_t i = 0;
    while (i < s.size()) {
        if (i + 3 <= s.size() && std::isdigit((unsigned char)s[i]) && std::isdigit((unsigned char)s[i + 1]) &&
            std::isdigit((unsigned char)s[i + 2])) {
            r += s.substr(i, 3);
            r += ',';
            i += 3;
        } else {
            r += s[i++];
        }
    }
    if (!r.empty() && r.back() == ',') r.pop_back();
    std::reverse(r.begin(), r.end());
    return r;
}

}  // namespace vl
