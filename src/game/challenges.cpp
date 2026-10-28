// Challenges (UI_definitions.lua G.UIDEF.challenges: the run setup's Challenges tab).
#include <algorithm>
#include <map>

#include "core/lua_sort.h"
#include "engine/controller.h"
#include "engine/random.h"
#include "engine/sprite.h"
#include "engine/text.h"
#include "engine/ui.h"
#include "game/blind.h"
#include "game/challenges.h"
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
Value node(int uit, const TableRef &cfg, const TableRef &nodes = nullptr) {
    TableRef t = vl::T({{"n", (double)uit}, {"config", Value(cfg)}});
    if (nodes) t->set("nodes", Value(nodes));
    return Value(t);
}
Value col(const char *n) { return G.colour(n); }
Value ui_col(const char *n) { return fld(G.colour("UI"), n); }
Table *profile() { return G.PROFILES->get(G.SETTINGS->get(K("profile"))).table(); }

// transparent_multiline_text(desc_nodes) (its maxw is an undefined global: nil)
Value transparent_multiline_text(const TableRef &desc_nodes) {
    TableRef t = new_table();
    for (int k = 1; k <= desc_nodes->len(); k++) t->push(node(UIT_R, vl::T({{"align", "cm"}}), TableRef(desc_nodes->get(k).table())));
    return node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.03}}), t);
}

TableRef locked_root(const char *key, const Value &vars) {
    TableRef loc_nodes = new_table();
    localize(vl::T({{"type", "descriptions"}, {"key", key}, {"set", "Other"}, {"nodes", Value(loc_nodes)}, {"vars", vars}, {"default_col", col("WHITE")}}));
    return TableRef(node(UIT_ROOT, vl::T({{"align", "cm"}, {"padding", 0.1}, {"colour", col("CLEAR")}, {"minh", 8.02}, {"minw", 7}}),
                         A({transparent_multiline_text(loc_nodes)}))
                        .table());
}
}  // namespace

// G.UIDEF.challenges(from_game_over)
TableRef challenges_overview(bool from_game_over) {
    Table *p = profile();
    const int total = G.CHALLENGES->len();
    if (p->get(K("all_unlocked")).truthy()) p->set("challenges_unlocked", (double)total);
    if (!p->get(K("challenges_unlocked")).truthy()) {
        int deck_wins = 0;
        if (Table *du = p->get(K("deck_usage")).table())
            du->each([&](const Value &, const Value &v) {
                if (fld(v, "wins").get(1).truthy()) deck_wins++;
            });
        return locked_root("challenge_locked", Value(A({Value(5) /* G.CHALLENGE_WINS */, Value((double)deck_wins)})));
    }
    G.G_TABLE->set("run_setup_seed", Value());
    if (auto *ov = dynamic_cast<UIBox *>(G.OVERLAY_MENU.get()))
        if (UIElement *seed_toggle = ov->get_UIE_by_ID("run_setup_seed")) seed_toggle->states.visible = false;
    int comp = 0;
    const Value completed = fld(p->get(K("challenge_progress")), "completed");
    for (int i = 1; i <= total; i++) {
        const Value id = fld(G.CHALLENGES->get(i), "id");
        if (id.truthy() && completed.get(id).truthy()) comp++;
    }
    TableRef ch_tab = vl::T({{"comp", (double)comp}, {"unlocked", p->get(K("challenges_unlocked"))}});
    auto bar = [&](const char *ref_value, const Value &filled, const char *key, const Value &a) {
        return node(UIT_R,
                    vl::T({{"align", "cm"}, {"minh", 0.8}, {"r", 0.1}, {"minw", 4.5}, {"colour", col("L_BLACK")}, {"emboss", 0.05},
                           {"progress_bar", Value(vl::T({{"max", (double)total}, {"ref_table", Value(ch_tab)}, {"ref_value", ref_value}, {"empty_col", col("L_BLACK")},
                                                         {"filled_col", filled}}))}}),
                    A({node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.05}, {"r", 0.1}, {"minw", 4.5}}),
                            A({node(UIT_T, vl::T({{"text", localize(vl::T({{"type", "variable"}, {"key", key}, {"vars", Value(A({a, Value((double)total)}))}}))},
                                                  {"scale", 0.3}, {"colour", col("WHITE")}, {"shadow", true}}))}))}));
    };
    TableRef button = vl::T({{"label", Value(A({localize("b_new_challenge")}))}, {"button", "challenge_list"}, {"minw", 4}, {"scale", 0.4}, {"minh", 0.6}});
    if (from_game_over) button->set("id", "from_game_over");
    // (G.F_DAILIES is off)
    return TableRef(
        node(UIT_ROOT, vl::T({{"align", "cm"}, {"padding", 0.1}, {"colour", col("CLEAR")}, {"minh", 8}, {"minw", 7}}),
             A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.1}, {"r", 0.1}, {"colour", col("BLACK")}}),
                     A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.1}}),
                             A({node(UIT_T, vl::T({{"text", localize("k_challenge_mode")}, {"scale", 0.4}, {"colour", ui_col("TEXT_LIGHT")}, {"shadow", true}}))})),
                        node(UIT_R, vl::T({{"align", "cm"}, {"minw", 8.5}, {"minh", 1.5}, {"padding", 0.2}}), A({Value(UIBox_button(button))})),
                        bar("unlocked", col("FILTER"), "unlocked", ch_tab->get(K("unlocked"))),
                        bar("comp", Value(adjust_alpha(col("GREEN"), 0.5f)), "completed", Value((double)comp))}))}))
            .table());
}

TableRef challenge_list_page(int page);
TableRef challenge_description_tab(const Value &args);

// the run setup's Challenges tab entry
TableRef challenges_tab(bool from_game_over, bool chosen) {
    return vl::T({{"label", localize("b_challenges")},
                  {"chosen", chosen},
                  {"tab_definition_function", Value((Object *)new Func([](const Value &a) { return Value(challenges_overview(a.truthy())); }))},
                  {"tab_definition_function_args", from_game_over}});
}

// get_challenge_rule(_challenge, _type, _id)
Value get_challenge_rule(const Value &challenge, const char *type, const char *id) {
    if (Table *list = fld(fld(challenge, "rules"), type).table())
        for (int k = 1; k <= list->len(); k++)
            if (fld(list->get(k), "id").equals(id)) return fld(list->get(k), "value");
    return Value();
}

int get_challenge_int_from_id(const Value &id) {
    for (int k = 1; k <= G.CHALLENGES->len(); k++)
        if (fld(G.CHALLENGES->get(k), "id") == id) return k;
    return 0;
}

// the starting deck's card prototypes {s, r, e, d, g}: a challenge's deck (cards, or the ranks and
// suits it keeps with its enhancement/edition/seal), else every card (erratic: random ones);
// with_seal_key: the run uses deck.gold_seal, the challenge screen deck.seal (as the original)
std::vector<TableRef> challenge_card_protos(const Value &challenge, bool for_run) {
    std::vector<TableRef> protos;
    const Value de = fld(challenge, "deck");
    if (Table *cards = fld(de, "cards").table()) {
        for (int k = 1; k <= cards->len(); k++) protos.push_back(TableRef(cards->get(k).table()));
        return protos;
    }
    const Value sp = G.GAME ? G.GAME->get(K("starting_params")) : Value();
    G.P_CARDS->each([&](const Value &k0, const Value &) {
        std::string key = k0.to_string();
        if (for_run && fld(sp, "erratic_suits_and_ranks").truthy()) {
            Value rk;
            const double seed = pseudoseed("erratic");
            pseudorandom_element(Value(G.P_CARDS), &seed, &rk);
            key = rk.to_string();
        }
        const std::string r = key.substr(2, 1), s = key.substr(0, 1);
        bool keep = true;
        Value e, d, g;
        if (de.truthy()) {
            if (fld(de, "yes_ranks").truthy() && !fld(de, "yes_ranks").get(Value(r)).truthy()) keep = false;
            if (fld(de, "no_ranks").truthy() && fld(de, "no_ranks").get(Value(r)).truthy()) keep = false;
            if (fld(de, "yes_suits").truthy() && !fld(de, "yes_suits").get(Value(s)).truthy()) keep = false;
            if (fld(de, "no_suits").truthy() && fld(de, "no_suits").get(Value(s)).truthy()) keep = false;
            e = fld(de, "enhancement");
            d = fld(de, "edition");
            g = for_run ? fld(de, "gold_seal") : fld(de, "seal");
        }
        if (for_run && fld(sp, "no_faces").truthy() && (r == "K" || r == "Q" || r == "J")) keep = false;
        if (keep) protos.push_back(vl::T({{"s", Value(s)}, {"r", Value(r)}, {"e", e}, {"d", d}, {"g", g}}));
    });
    return protos;
}

// create_UIBox_blind_popup(blind, discovered, vars)
TableRef create_UIBox_blind_popup(const Value &blind, bool discovered, const Value &vars) {
    TableRef blind_text = new_table();
    const double dollars = fld(blind, "dollars").num();
    const Value loc_target = localize(vl::T({{"type", "raw_descriptions"}, {"key", fld(blind, "key")}, {"set", "Blind"}, {"vars", vars.truthy() ? vars : fld(blind, "vars")}}));
    const Value loc_name = localize(vl::T({{"type", "name_text"}, {"key", fld(blind, "key")}, {"set", "Blind"}}));
    if (discovered) {
        TableRef ability = new_table();
        if (Table *lt = loc_target.table())
            for (int k = 1; k <= lt->len(); k++) {
                const std::string prefix = (k == 1 && fld(blind, "name").equals("The Wheel")) ? "1" : "";
                ability->push(node(UIT_R, vl::T({{"align", "cm"}}),
                                   A({node(UIT_T, vl::T({{"text", Value(prefix + lt->get(k).to_string())}, {"scale", 0.35}, {"shadow", true}, {"colour", col("WHITE")}}))})));
            }
        Sprite *stake = get_stake_sprite(G.GAME && G.GAME->get(K("stake")).truthy() ? (int)G.GAME->get(K("stake")).num() : 1, 0.4f);
        std::string money;
        for (int i = 0; i < (int)dollars; i++) money += localize("$").to_string();
        TableRef box = A({node(UIT_R, vl::T({{"align", "cm"}, {"maxw", 2.4}}),
                               A({node(UIT_T, vl::T({{"text", localize("ph_blind_score_at_least")}, {"scale", 0.35}, {"colour", ui_col("TEXT_DARK")}}))})),
                          node(UIT_R, vl::T({{"align", "cm"}}),
                               A({node(UIT_O, vl::T({{"object", Value((Object *)stake)}})),
                                  node(UIT_T, vl::T({{"text", Value(fld(blind, "mult").to_string() + localize("k_x_base").to_string())}, {"scale", 0.4}, {"colour", col("RED")}}))})),
                          node(UIT_R, vl::T({{"align", "cm"}}),
                               A({node(UIT_T, vl::T({{"text", localize("ph_blind_reward")}, {"scale", 0.35}, {"colour", ui_col("TEXT_DARK")}})),
                                  node(UIT_O, vl::T({{"object", Value((Object *)new DynaText(vl::T({{"string", Value(A({Value(dollars > 0 ? money : std::string("-"))}))},
                                                                                                      {"colours", Value(A({col("MONEY")}))},
                                                                                                      {"rotate", true},
                                                                                                      {"bump", true},
                                                                                                      {"silent", true},
                                                                                                      {"scale", 0.45}})))}}))}))});
        if (ability->len() > 0)
            box->push(node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.08}, {"colour", Value(mix_colours(fld(blind, "boss_colour"), col("GREY"), 0.4f))}, {"r", 0.1}, {"emboss", 0.05},
                                         {"minw", 2.5}, {"minh", 0.9}}),
                           ability));
        blind_text->push(node(UIT_R, vl::T({{"align", "cm"}, {"emboss", 0.05}, {"r", 0.1}, {"minw", 2.5}, {"padding", 0.07}, {"colour", col("WHITE")}}), box));
    } else {
        blind_text->push(node(UIT_R, vl::T({{"align", "cm"}, {"emboss", 0.05}, {"r", 0.1}, {"minw", 2.5}, {"padding", 0.1}, {"colour", col("WHITE")}}),
                              A({node(UIT_R, vl::T({{"align", "cm"}}),
                                      A({node(UIT_T, vl::T({{"text", localize("ph_defeat_this_blind_1")}, {"scale", 0.4}, {"colour", ui_col("TEXT_DARK")}}))})),
                                 node(UIT_R, vl::T({{"align", "cm"}}),
                                      A({node(UIT_T, vl::T({{"text", localize("ph_defeat_this_blind_2")}, {"scale", 0.4}, {"colour", ui_col("TEXT_DARK")}}))}))})));
    }
    const Value title_col = !discovered ? col("JOKER_GREY") : fld(blind, "boss_colour").truthy() ? fld(blind, "boss_colour") : col("GREY");
    return TableRef(
        node(UIT_ROOT, vl::T({{"align", "cm"}, {"padding", 0.05}, {"colour", Value(lighten(col("JOKER_GREY"), 0.5f))}, {"r", 0.1}, {"emboss", 0.05}}),
             A({node(UIT_R, vl::T({{"align", "cm"}, {"emboss", 0.05}, {"r", 0.1}, {"minw", 2.5}, {"padding", 0.1}, {"colour", title_col}}),
                     A({node(UIT_O, vl::T({{"object", Value((Object *)new DynaText(vl::T({{"string", discovered ? loc_name : localize("k_not_discovered")},
                                                                                            {"colours", Value(A({ui_col("TEXT_LIGHT")}))},
                                                                                            {"shadow", true},
                                                                                            {"rotate", !discovered},
                                                                                            {"spacing", discovered ? 2 : 0},
                                                                                            {"bump", true},
                                                                                            {"scale", 0.4}})))}}))})),
                node(UIT_R, vl::T({{"align", "cm"}}), blind_text)}))
            .table());
}

// a blind chip that shows its popup while hovered (the challenge restrictions and blind lists)
void BlindChipSprite::hover() {
    if (!G.CONTROLLER->dragging.target || G.CONTROLLER->HID.touch) {
        if (!hovering && states.visible) {
            hovering = true;
            hover_tilt = 3;
            float r = 0.02f;
            juice_up(0.05f, &r);
            play_sound("chips1", (float)(lua_math.random() * 0.1 + 0.55), 0.12f);
            config->set("h_popup", Value(create_UIBox_blind_popup(blind, discovered)));
            config->set("h_popup_config", Value(vl::T({{"align", "cl"}, {"offset", Value(vl::T({{"x", -0.1}, {"y", 0}}))}, {"parent", Value((Object *)this)}})));
            Node::hover();
            if (Node *alert = children.get("alert")) {
                alert->remove();
                children.set("alert", nullptr);
                blind.table()->set("alerted", true);
                G.save_progress();
            }
        }
    }
}

void BlindChipSprite::stop_hover() {
    hovering = false;
    Node::stop_hover();
    hover_tilt = 0;
}

BlindChipSprite::BlindChipSprite(float x, float y, float w, float h, Atlas *a, SpritePos p, const Value &b) : AnimatedSprite(x, y, w, h, a, p), blind(b) {
    DrawStep s1, s2;
    s1.shader = gfx::ShaderId::Dissolve;
    s1.has_shadow_height = true;
    s1.shadow_height = 0.05f;
    s2.shader = gfx::ShaderId::Dissolve;
    define_draw_steps({s1, s2});
    float_ = true;
    states.hover.can = true;
    states.drag.can = false;
    states.collide.can = true;
    touch_collide_tilt = true;
    config->set("blind", b);
    config->set("force_focus", true);
}

namespace {
const double kPageSize = 10;  // G.CHALLENGE_PAGE_SIZE

Value text_rows(const char *key, const Value &vars, const Value &default_col = Value()) {
    TableRef args = vl::T({{"type", "text"}, {"key", key}, {"vars", vars.truthy() ? vars : Value(new_table())}});
    if (default_col.truthy()) args->set("default_col", default_col);
    return localize(args);
}

Card *challenge_card(const Value &v, float size) {
    Card *card = new Card(0, 0, G.CARD_W * size, G.CARD_H * size, Value(), G.P_CENTERS->get(fld(v, "id")),
                          vl::T({{"bypass_discovery_center", true}, {"bypass_discovery_ui", true}, {"bypass_lock", true}}));
    if (fld(v, "edition").truthy()) card->set_edition(Value(vl::T({{fld(v, "edition").to_string().c_str(), true}})), true, true);
    if (fld(v, "eternal").truthy()) card->set_eternal(true);
    return card;
}

Value card_column(const char *label, double label_scale, CardArea *area, double minw) {
    TableRef inner_cfg = vl::T({{"align", "cm"}, {"minh", 0.6 * G.CARD_H}, {"r", 0.1}, {"colour", ui_col("TRANSPARENT_DARK")}});
    if (minw > 0) inner_cfg->set("minw", minw);
    return node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.05}, {"colour", col("L_BLACK")}, {"r", 0.1}, {"maxh", 1.8}}),
                A({node(UIT_T, vl::T({{"text", localize(label)}, {"scale", label_scale}, {"colour", ui_col("TEXT_LIGHT")}, {"vert", true}, {"shadow", true}})),
                   node(UIT_C, inner_cfg, A({node(UIT_O, vl::T({{"object", Value((Object *)area)}}))}))}));
}

Value row_of(const Value &rows) { return node(UIT_R, vl::T({{"align", "cl"}}), TableRef(rows.table())); }
}  // namespace

// G.UIDEF.challenge_description_tab(args)
TableRef challenge_description_tab(const Value &args) {
    const Value challenge = G.CHALLENGES->get(fld(args, "_id"));
    const std::string tab = fld(args, "_tab").to_string();
    if (tab == "Rules") {
        struct Mod {
            std::string key;
            Value value, old_val;
            double order = 0;
            bool custom = false;
        };
        std::map<std::string, Mod> mods;
        const std::pair<const char *, double> base[] = {{"dollars", 6}, {"discards", 2}, {"hands", 1}, {"reroll_cost", 7}, {"joker_slots", 4}, {"consumable_slots", 5},
                                                        {"hand_size", 3}};
        // get_starting_params()
        const std::map<std::string, double> start = {{"dollars", 4}, {"hand_size", 8}, {"discards", 3}, {"hands", 4}, {"reroll_cost", 5}, {"joker_slots", 5},
                                                     {"consumable_slots", 2}};
        for (auto &[k, o] : base) mods[k] = Mod{k, Value(start.at(k)), Value(), o, false};
        double bonus = 100;
        if (Table *m = fld(fld(challenge, "rules"), "modifiers").table())
            for (int k = 1; k <= m->len(); k++) {
                const std::string id = fld(m->get(k), "id").to_string();
                const bool had = mods.count(id) > 0;
                mods[id] = Mod{id, fld(m->get(k), "value"), had ? mods[id].value : Value(), had ? mods[id].order : bonus, true};
                bonus++;
            }
        std::vector<Mod> sorted;
        for (auto &[k, v] : mods) sorted.push_back(v);
        std::stable_sort(sorted.begin(), sorted.end(), [](const Mod &a, const Mod &b) { return a.order < b.order; });
        TableRef start_rules = new_table(), modded = new_table();
        for (auto &v : sorted) {
            const std::string key = "ch_m_" + v.key;
            if (v.old_val.truthy())
                modded->push(node(UIT_R, vl::T({{"align", "cl"}, {"maxw", 3.5}}), TableRef(text_rows(key.c_str(), Value(A({v.value})), col("L_BLACK")).table())));
            else
                start_rules->push(node(UIT_R, vl::T({{"align", "cl"}, {"maxw", 3.5}}),
                                       TableRef(text_rows(key.c_str(), Value(A({v.value})), v.custom ? Value() : ui_col("TEXT_INACTIVE")).table())));
        }
        if (modded->len() > 0)
            start_rules = A({node(UIT_R, vl::T({{"align", "cl"}, {"padding", 0.05}}), modded), node(UIT_R, vl::T({{"align", "cl"}, {"padding", 0.05}, {"colour", col("GREY")}}), new_table()),
                             node(UIT_R, vl::T({{"align", "cl"}, {"padding", 0.05}}), start_rules)});
        TableRef game_rules = new_table();
        if (Table *c = fld(fld(challenge, "rules"), "custom").table())
            for (int k = 1; k <= c->len(); k++) {
                const std::string key = "ch_c_" + fld(c->get(k), "id").to_string();
                game_rules->push(row_of(text_rows(key.c_str(), Value(A({fld(c->get(k), "value")})))));
            }
        if (start_rules->len() == 0) start_rules->push(row_of(text_rows("ch_m_none", Value())));
        if (game_rules->len() == 0) game_rules->push(row_of(text_rows("ch_c_none", Value())));
        auto list = [](const char *title, double minw, const TableRef &rows, double maxw) {
            TableRef body = vl::T({{"align", "cm"}, {"minh", 4.1}, {"minw", minw}, {"padding", 0.05}, {"r", 0.1}, {"colour", col("WHITE")}});
            if (maxw > 0) body->set("maxw", maxw);
            return node(UIT_C, vl::T({{"align", "cm"}, {"minw", 3}, {"r", 0.1}, {"colour", col("BLUE")}}),
                        A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.08}, {"minh", 0.6}}),
                                A({node(UIT_T, vl::T({{"text", localize(title)}, {"scale", 0.4}, {"colour", ui_col("TEXT_LIGHT")}, {"shadow", true}}))})),
                           node(UIT_R, body, rows)}));
        };
        return TableRef(node(UIT_ROOT, vl::T({{"align", "cm"}, {"padding", 0.05}, {"colour", col("CLEAR")}}),
                             A({node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.1}, {"colour", col("L_BLACK")}, {"r", 0.1}, {"minw", 3}}),
                                     A({list("k_custom_rules", 6.8, game_rules, 6.7), list("k_game_modifiers", 4.2, start_rules, 0)}))}))
                            .table());
    }
    if (tab == "Restrictions") {
        TableRef banned_cards = new_table(), banned_tags = new_table(), banned_other = new_table();
        const Value r = fld(challenge, "restrictions");
        if (Table *bc = fld(r, "banned_cards").table()) {
            const int n = bc->len();
            const int n_rows = std::max(1, (int)std::floor(n / 10.0) + 2 - (int)std::floor(std::log(6.0) / std::log((double)n)));
            std::vector<std::vector<Value>> rows(n_rows + 2);
            size_t max_width = 1;
            for (int k = 1; k <= n; k++) {
                const int row = (int)std::floor((k - 1) * n_rows / (double)n + 1);
                rows[row].push_back(bc->get(k));
                max_width = std::max(max_width, rows[row].size());
            }
            const double card_size = std::max(0.3, 0.75 - 0.01 * (double)(max_width * n_rows));
            for (int i = 1; i <= n_rows; i++) {
                if (rows[i].empty()) continue;
                auto *area = new CardArea(0, 0, 6.7f, 3.3f / n_rows,
                                          vl::T({{"type", "title_2"}, {"view_deck", true}, {"highlight_limit", 0}, {"card_w", G.CARD_W * card_size}}));
                banned_cards->push(node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}}), A({node(UIT_O, vl::T({{"object", Value((Object *)area)}}))})));
                for (const Value &v : rows[i])
                    area->emplace(new Card(0, 0, G.CARD_W * (float)card_size, G.CARD_H * (float)card_size, Value(), G.P_CENTERS->get(fld(v, "id")),
                                           vl::T({{"bypass_discovery_center", true}, {"bypass_discovery_ui", true}})));
            }
        }
        if (Table *bt = fld(r, "banned_tags").table()) {
            std::vector<Value> tags;
            bt->each([&](const Value &, const Value &v) { tags.push_back(G.P_TAGS->get(fld(v, "id"))); });
            std::stable_sort(tags.begin(), tags.end(), [](const Value &a, const Value &b) { return fld(a, "order").num() < fld(b, "order").num(); });
            for (const Value &v : tags) {
                Tag *t = new Tag(fld(v, "key"));
                banned_tags->push(node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}}), A({Value(t->generate_UI((float)(1.1 - 0.25 * std::sqrt((double)bt->len()))))})));
            }
        }
        if (Table *bo = fld(r, "banned_other").table()) {
            std::vector<Value> blinds;
            bo->each([&](const Value &, const Value &v) {
                if (fld(v, "type").equals("blind")) blinds.push_back(G.P_BLINDS->get(fld(v, "id")));
            });
            std::stable_sort(blinds.begin(), blinds.end(), [](const Value &a, const Value &b) { return fld(a, "order").num() < fld(b, "order").num(); });
            for (const Value &v : blinds) {
                SpritePos p;
                p.x = (float)fld(fld(v, "pos"), "x").num();
                p.y = (float)fld(fld(v, "pos"), "y").num();
                auto *b = new BlindChipSprite(0, 0, 1, 1, &G.ANIMATION_ATLAS["blind_chips"], p, v);
                banned_other->push(node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}}), A({node(UIT_O, vl::T({{"object", Value((Object *)b)}}))})));
            }
        }
        if (banned_cards->len() == 0) banned_cards->push(row_of(text_rows("ch_m_none", Value())));
        if (banned_tags->len() == 0) banned_tags->push(row_of(text_rows("ch_c_none", Value())));
        if (banned_other->len() == 0) banned_other->push(row_of(text_rows("ch_c_none", Value())));
        auto box = [](const char *title, double ts, double maxw, double minw, const TableRef &rows) {
            TableRef head = vl::T({{"align", "cm"}, {"padding", 0.08}, {"minh", 0.6}});
            if (maxw > 0) head->set("maxw", maxw);
            return node(UIT_C, vl::T({{"align", "cm"}, {"r", 0.1}, {"colour", col("RED")}}),
                        A({node(UIT_R, head, A({node(UIT_T, vl::T({{"text", localize(title)}, {"scale", ts}, {"colour", ui_col("TEXT_LIGHT")}, {"shadow", true}}))})),
                           node(UIT_R, vl::T({{"align", "cm"}, {"minh", 4.1}, {"minw", minw}, {"padding", 0.05}, {"r", 0.1}, {"colour", col("WHITE")}}), rows)}));
        };
        return TableRef(node(UIT_ROOT, vl::T({{"align", "cm"}, {"padding", 0.05}, {"colour", col("CLEAR")}}),
                             A({node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.1}, {"colour", col("L_BLACK")}, {"r", 0.1}}),
                                     A({box("k_banned_cards", 0.4, 0, 7.33, banned_cards), box("k_banned_tags", 0.3, 1.48, 1.48, banned_tags),
                                        box("k_other", 0.4, 1.84, 2, banned_other)}))}))
                            .table());
    }
    // Deck
    TableRef deck_tables = new_table();
    std::map<std::string, std::vector<Card *>> SUITS;
    for (auto &v : challenge_card_protos(challenge, false)) {
        const std::string s = v->get(K("s")).to_string(), rk = v->get(K("r")).to_string();
        const Value e = v->get(K("e"));
        Card *c = new Card(0, 0, G.CARD_W * 0.45f, G.CARD_H * 0.45f, G.P_CARDS->get(Value(s + "_" + rk)), G.P_CENTERS->get(e.truthy() ? e : Value("c_base")));
        if (v->get(K("d")).truthy()) c->set_edition(Value(vl::T({{v->get(K("d")).to_string().c_str(), true}})), true, true);
        if (v->get(K("g")).truthy()) c->set_seal(v->get(K("g")), true, true);
        SUITS[s].push_back(c);
    }
    for (const char *s : {"S", "H", "C", "D"}) {
        auto &list = SUITS[s];
        if (list.empty()) continue;
        lua_sort(list, [](Card *a, Card *b) { return a->get_nominal() > b->get_nominal(); });
        auto *area = new CardArea(0, 0, 5.5f * G.CARD_W, 0.42f * G.CARD_H,
                                  vl::T({{"card_limit", (double)list.size()}, {"type", "title_2"}, {"view_deck", true}, {"highlight_limit", 0}, {"card_w", G.CARD_W * 0.5},
                                         {"draw_layers", Value(A({"card"}))}}));
        deck_tables->push(node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}}), A({node(UIT_O, vl::T({{"object", Value((Object *)area)}}))})));
        for (Card *c : list) area->emplace(c);
    }
    return TableRef(node(UIT_ROOT, vl::T({{"align", "cm"}, {"padding", 0}, {"colour", col("BLACK")}, {"r", 0.1}, {"minw", 11.4}, {"minh", 4.2}}), deck_tables).table());
}

// G.UIDEF.challenge_description(_id, daily, is_row)
TableRef challenge_description(const Value &id, bool is_row) {
    const Value challenge = G.CHALLENGES->get(id);
    if (!challenge.truthy())
        return TableRef(node(UIT_ROOT, vl::T({{"align", "cm"}, {"colour", col("BLACK")}, {"minh", 8.82}, {"minw", 11.5}, {"r", 0.1}}),
                             A({node(UIT_T, vl::T({{"text", localize("ph_select_challenge")}, {"scale", 0.3}, {"colour", ui_col("TEXT_LIGHT")}}))}))
                            .table());
    const float js = 0.6f;
    const Value jl = get_challenge_rule(challenge, "modifiers", "joker_limit"), cl = get_challenge_rule(challenge, "modifiers", "consumable_limit");
    auto *jokers = new CardArea(0, 0, 10 * js, 0.6f * G.CARD_H,
                                vl::T({{"card_limit", jl.truthy() ? jl : Value(5)}, {"card_w", js * G.CARD_W}, {"type", "title_2"}, {"highlight_limit", 0}}));
    if (Table *j = fld(challenge, "jokers").table())
        for (int k = 1; k <= j->len(); k++) {
            Card *c = challenge_card(j->get(k), js);
            if (fld(j->get(k), "pinned").truthy()) c->pinned = true;
            jokers->emplace(c);
        }
    auto *consumeables = new CardArea(0, 0, 3 * js, 0.6f * G.CARD_H,
                                      vl::T({{"card_limit", cl.truthy() ? cl : Value(2)}, {"card_w", js * G.CARD_W}, {"type", "title_2"}, {"spread", true}, {"highlight_limit", 0}}));
    if (Table *c = fld(challenge, "consumeables").table())
        for (int k = 1; k <= c->len(); k++) consumeables->emplace(challenge_card(c->get(k), js));
    auto *vouchers = new CardArea(0, 0, 3 * js, 0.6f * G.CARD_H, vl::T({{"card_w", js * G.CARD_W}, {"type", "title_2"}, {"spread", true}, {"highlight_limit", 0}}));
    if (Table *v = fld(challenge, "vouchers").table())
        for (int k = 1; k <= v->len(); k++) vouchers->emplace(challenge_card(v->get(k), js));
    auto tab = [&](const char *label, const char *t, bool chosen) {
        TableRef e = vl::T({{"label", localize(label)},
                            {"tab_definition_function", Value((Object *)new Func([](const Value &a) { return Value(challenge_description_tab(a)); }))},
                            {"tab_definition_function_args", Value(vl::T({{"_id", id}, {"_tab", t}}))}});
        if (chosen) e->set("chosen", true);
        return Value(e);
    };
    TableRef nodes = A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.1}}),
                             A({card_column("k_jokers_cap", 0.5, jokers, 5), card_column("k_cap_consumables", 0.3, consumeables, 0),
                                card_column("k_vouchers_cap", 0.33, vouchers, 0)})),
                        node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}}),
                             A({Value(create_tabs(vl::T({{"tabs", Value(A({tab("b_rules", "Rules", true), tab("b_restrictions", "Restrictions", false), tab("b_deck", "Deck", false)}))},
                                                         {"tab_h", 5},
                                                         {"padding", 0},
                                                         {"text_scale", 0.36},
                                                         {"scale", 0.85},
                                                         {"no_shoulders", true},
                                                         {"no_loop", true}})))}))});
    if (!is_row)
        nodes->push(node(UIT_R, vl::T({{"align", "cm"}, {"minh", 0.9}}),
                         A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.1}, {"minh", 0.7}, {"minw", 9}, {"r", 0.1}, {"hover", true}, {"colour", col("BLUE")},
                                               {"button", "start_challenge_run"}, {"shadow", true}, {"id", id}}),
                                 A({node(UIT_T, vl::T({{"text", localize("b_play_cap")}, {"scale", 0.5}, {"colour", ui_col("TEXT_LIGHT")}, {"func", "set_button_pip"},
                                                       {"focus_args", Value(vl::T({{"button", "x"}, {"set_button_pip", true}}))}}))}))})));
    return TableRef(node(is_row ? UIT_R : UIT_ROOT, vl::T({{"align", "cm"}, {"r", 0.1}, {"colour", col("BLACK")}}), nodes).table());
}

// G.UIDEF.challenge_list_page(_page)
TableRef challenge_list_page(int page) {
    bool snapped = false;
    TableRef list = new_table();
    Table *p = profile();
    const Value completed = fld(p->get(K("challenge_progress")), "completed");
    const double unlocked = p->get(K("challenges_unlocked")).num();
    for (int k = 1; k <= G.CHALLENGES->len(); k++) {
        if (!(k > kPageSize * page && k <= kPageSize * (page + 1))) continue;
        const Value v = G.CHALLENGES->get(k);
        auto *fe = dynamic_cast<UIElement *>(G.CONTROLLER->focused.target.get());
        if (fe && fe->config->get(K("id")).equals("challenge_page")) snapped = true;
        const bool done = completed.get(fld(v, "id")).truthy();
        const bool open = p->get(K("challenges_unlocked")).truthy() && unlocked >= k;
        TableRef check = new_table();
        if (done) {
            SpritePos sp;
            sp.x = 1;
            check->push(node(UIT_O, vl::T({{"object", Value((Object *)new Sprite(0, 0, 0.4f, 0.4f, &G.ASSET_ATLAS["icons"], sp))}})));
        }
        list->push(node(UIT_R, vl::T({{"align", "cm"}}),
                        A({node(UIT_C, vl::T({{"align", "cl"}, {"minw", 0.8}}), A({node(UIT_T, vl::T({{"text", Value(std::to_string(k))}, {"scale", 0.4}, {"colour", col("WHITE")}}))})),
                           Value(UIBox_button(vl::T({{"id", k},
                                                     {"col", true},
                                                     {"label", Value(A({open ? localize(fld(v, "id").to_string(), "challenge_names") : localize("k_locked")}))},
                                                     {"button", open ? "change_challenge_description" : "nil"},
                                                     {"colour", open ? col("RED") : col("GREY")},
                                                     {"minw", 4},
                                                     {"scale", 0.4},
                                                     {"minh", 0.6},
                                                     {"focus_args", Value(vl::T({{"snap_to", !snapped}}))}}))),
                           node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.05}, {"minw", 0.6}}),
                                A({node(UIT_C, vl::T({{"minh", 0.4}, {"minw", 0.4}, {"emboss", 0.05}, {"r", 0.1}, {"colour", done ? col("GREEN") : col("BLACK")}}), check)}))})));
        snapped = true;
    }
    return TableRef(node(UIT_ROOT, vl::T({{"align", "cm"}, {"padding", 0.1}, {"colour", col("CLEAR")}}), list).table());
}

// G.UIDEF.challenge_list(from_game_over)
TableRef challenge_list() {
    const int n = G.CHALLENGES->len();
    const int pages = (int)std::ceil(n / kPageSize);
    TableRef options = new_table();
    for (int i = 1; i <= pages; i++) options->push(Value(localize("k_page").to_string() + " " + std::to_string(i) + "/" + std::to_string(pages)));
    G.E_MANAGER->add_event(ev_immediate([] {
        auto it = G.FUNCS_T.find("change_challenge_list_page");
        if (it != G.FUNCS_T.end()) it->second(vl::T({{"cycle_config", Value(vl::T({{"current_option", 1}}))}}));
        return true;
    }));
    int comp = 0;
    const Value completed = fld(profile()->get(K("challenge_progress")), "completed");
    for (int i = 1; i <= n; i++)
        if (fld(G.CHALLENGES->get(i), "id").truthy() && completed.get(fld(G.CHALLENGES->get(i), "id")).truthy()) comp++;
    // (the original's back_id = from_game_over and 'from_game_over' is overwritten by back_id = 'challenge_list')
    return create_UIBox_generic_options(vl::T(
        {{"back_id", "challenge_list"},
         {"back_func", "setup_run"},
         {"contents", Value(A({node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.0}}),
                                    A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.1}, {"minh", 7}, {"minw", 4.2}}),
                                            A({node(UIT_O, vl::T({{"id", "challenge_list"}, {"object", Value((Object *)new Moveable(0, 0, 0, 0))}}))})),
                                       node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.1}}),
                                            A({Value(create_option_cycle(vl::T({{"id", "challenge_page"}, {"scale", 0.9}, {"h", 0.3}, {"w", 3.5}, {"options", Value(options)},
                                                                                {"cycle_shoulders", true}, {"opt_callback", "change_challenge_list_page"}, {"current_option", 1},
                                                                                {"colour", col("RED")}, {"no_pips", true},
                                                                                {"focus_args", Value(vl::T({{"snap_to", true}}))}})))})),
                                       node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.1}}),
                                            A({node(UIT_T, vl::T({{"text", localize(vl::T({{"type", "variable"}, {"key", "challenges_completed"},
                                                                                           {"vars", Value(A({Value((double)comp), Value((double)n)}))}}))},
                                                                  {"scale", 0.4},
                                                                  {"colour", col("WHITE")}}))}))})),
                               node(UIT_C, vl::T({{"align", "cm"}, {"minh", 9}, {"minw", 11.5}}),
                                    A({node(UIT_O, vl::T({{"id", "challenge_area"}, {"object", Value((Object *)new Moveable(0, 0, 0, 0))}}))}))}))}}));
}

namespace {
// the alert badge on a newly discovered tag or blind in the collection (an immediate event)
void alert_later(std::vector<Ref<Moveable>> targets) {
    G.E_MANAGER->add_event(ev_immediate([targets] {
        for (auto &v : targets) {
            UIBox *a = new UIBox(create_UIBox_card_alert(), vl::T({{"align", "tri"}, {"offset", Value(vl::T({{"x", 0.1}, {"y", 0.1}}))}, {"parent", Value((Object *)v.get())}}));
            v->children.set("alert", a);
            a->states.collide.can = false;
        }
        return true;
    }));
}

std::vector<Value> sorted_by_order(Table *pool) {
    std::vector<Value> tab;
    pool->each([&](const Value &, const Value &v) { tab.push_back(v); });
    std::sort(tab.begin(), tab.end(), [](const Value &a, const Value &b) { return fld(a, "order").num() < fld(b, "order").num(); });
    return tab;
}
}  // namespace

// create_UIBox_your_collection_tags()
TableRef create_UIBox_your_collection_tags() {
    TableRef tag_matrix[4] = {new_table(), new_table(), new_table(), new_table()};
    std::vector<Ref<Moveable>> tags_to_be_alerted;
    std::vector<Value> tag_tab = sorted_by_order(G.P_TAGS.get());
    for (int k = 1; k <= (int)tag_tab.size(); k++) {
        const Value &v = tag_tab[k - 1];
        const bool discovered = fld(v, "discovered").truthy();
        Tag *temp_tag = new Tag(fld(v, "key"), true);
        if (!discovered) temp_tag->hide_ability = true;
        TagSprite *temp_tag_sprite = nullptr;
        TableRef temp_tag_ui = temp_tag->generate_UI(0.8f, &temp_tag_sprite);
        tag_matrix[(k - 1) / 6]->push(node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.1}}), A({Value(temp_tag_ui)})));
        if (discovered && !fld(v, "alerted").truthy()) tags_to_be_alerted.push_back(Ref<Moveable>(temp_tag_sprite));
    }
    alert_later(tags_to_be_alerted);

    TableRef rows = new_table();
    for (auto &m : tag_matrix) rows->push(node(UIT_R, vl::T({{"align", "cm"}}), m));
    return create_UIBox_generic_options(vl::T(
        {{"back_func", "your_collection"},
         {"contents", Value(A({node(UIT_C, vl::T({{"align", "cm"}, {"r", 0.1}, {"colour", col("BLACK")}, {"padding", 0.1}, {"emboss", 0.05}}),
                                    A({node(UIT_C, vl::T({{"align", "cm"}}), A({node(UIT_R, vl::T({{"align", "cm"}}), rows)}))}))}))}}));
}

// create_UIBox_your_collection_blinds(exit)
TableRef create_UIBox_your_collection_blinds(const char *exit) {
    TableRef blind_matrix[6] = {new_table(), new_table(), new_table(), new_table(), new_table(), new_table()};
    std::vector<Ref<Moveable>> blinds_to_be_alerted;
    std::vector<Value> blind_tab = sorted_by_order(G.P_BLINDS.get());
    for (int k = 1; k <= (int)blind_tab.size(); k++) {
        const Value &v = blind_tab[k - 1];
        const bool discovered = fld(v, "discovered").truthy();
        const Value p = discovered ? fld(v, "pos") : fld(G.b_undiscovered, "pos");
        SpritePos sp;
        sp.x = (float)fld(p, "x").num();
        sp.y = (float)fld(p, "y").num();
        auto *temp_blind = new BlindChipSprite(0, 0, 1.3f, 1.3f, &G.ANIMATION_ATLAS["blind_chips"], sp, v);
        temp_blind->discovered = discovered;
        temp_blind->config->set("force_focus", true);
        if (k == 1) {
            Ref<Moveable> first(temp_blind);
            G.E_MANAGER->add_event(ev_immediate([first] {
                G.CONTROLLER->snap_to(first.get());
                return true;
            }));
        }
        if (discovered && !fld(v, "alerted").truthy()) blinds_to_be_alerted.push_back(Ref<Moveable>(temp_blind));
        TableRef nodes = new_table();
        if (k == 6 || k == 16 || k == 26) nodes->push(node(UIT_B, vl::T({{"h", 0.2}, {"w", 0.5}})));
        nodes->push(node(UIT_O, vl::T({{"object", Value((Object *)temp_blind)}, {"focus_with_object", true}})));
        if (k == 5 || k == 15 || k == 25) nodes->push(node(UIT_B, vl::T({{"h", 0.2}, {"w", 0.5}})));
        blind_matrix[(k - 1) / 5]->push(node(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.1}}), nodes));
    }
    alert_later(blinds_to_be_alerted);

    const double furthest = fld(fld(Value(profile()), "high_scores"), "furthest_ante").get(K("amt")).num();
    TableRef ante_amounts = new_table();
    for (int i = 1; i <= (int)std::min(16.0, std::max(16.0, furthest)); i++) {
        const double spacing = 1 - std::min(20.0, std::max(15.0, furthest)) * 0.06;
        if (spacing > 0 && i > 1) ante_amounts->push(node(UIT_R, vl::T({{"minh", spacing}}), new_table()));
        auto *blind_chip = new Sprite(0, 0, 0.2f, 0.2f, &G.ASSET_ATLAS[G.SETTINGS->get(K("colourblind_option")).truthy() ? "ui_2" : "ui_1"], SpritePos{});
        blind_chip->states.drag.can = false;
        ante_amounts->push(node(
            UIT_R, vl::T({{"align", "cm"}, {"padding", 0.03}}),
            A({node(UIT_C, vl::T({{"align", "cm"}, {"minw", 0.7}}),
                    A({node(UIT_T, vl::T({{"text", Value((double)i)}, {"scale", 0.4}, {"colour", col("FILTER")}, {"shadow", true}}))})),
               node(UIT_C, vl::T({{"align", "cr"}, {"minw", 2.8}}),
                    A({node(UIT_O, vl::T({{"object", Value((Object *)blind_chip)}})), node(UIT_C, vl::T({{"align", "cm"}, {"minw", 0.03}, {"minh", 0.01}}), new_table()),
                       node(UIT_T, vl::T({{"text", Value(number_format(Value(get_blind_amount(i))))},
                                          {"scale", 0.4},
                                          {"colour", i <= furthest ? col("RED") : col("JOKER_GREY")},
                                          {"shadow", true}}))}))})));
    }

    TableRef matrix_rows = new_table();
    for (auto &m : blind_matrix) matrix_rows->push(node(UIT_R, vl::T({{"align", "cm"}}), m));
    return create_UIBox_generic_options(vl::T(
        {{"back_func", exit ? exit : "your_collection"},
         {"contents",
          Value(A({node(UIT_C, vl::T({{"align", "cm"}, {"r", 0.1}, {"colour", col("BLACK")}, {"padding", 0.1}, {"emboss", 0.05}}),
                        A({node(UIT_C, vl::T({{"align", "cm"}, {"r", 0.1}, {"colour", col("L_BLACK")}, {"padding", 0.1}, {"force_focus", true}, {"focus_args", Value(vl::T({{"nav", "tall"}}))}}),
                                A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.05}}),
                                        A({node(UIT_C, vl::T({{"align", "cm"}, {"minw", 0.7}}),
                                                A({node(UIT_T, vl::T({{"text", localize("k_ante_cap")}, {"scale", 0.4}, {"colour", Value(lighten(col("FILTER"), 0.2f))}, {"shadow", true}}))})),
                                           node(UIT_C, vl::T({{"align", "cr"}, {"minw", 2.8}}),
                                                A({node(UIT_T, vl::T({{"text", localize("k_base_cap")}, {"scale", 0.4}, {"colour", Value(lighten(col("RED"), 0.2f))}, {"shadow", true}}))}))})),
                                   node(UIT_R, vl::T({{"align", "cm"}}), ante_amounts)})),
                           node(UIT_C, vl::T({{"align", "cm"}}), A({node(UIT_R, vl::T({{"align", "cm"}}), matrix_rows)}))}))}))}}));
}

void register_challenge_callbacks() {
    auto &F = G.FUNCS;
    F["your_collection_tags"] = [](UIElement *) {
        G.SETTINGS->set("paused", true);
        overlay_menu(create_UIBox_your_collection_tags());
    };
    F["your_collection_blinds"] = [](UIElement *) {
        G.SETTINGS->set("paused", true);
        overlay_menu(create_UIBox_your_collection_blinds(nullptr));
    };
    F["challenge_list"] = [](UIElement *e) {
        G.SETTINGS->set("paused", true);
        overlay_menu(challenge_list());
        if (e && e->config->get(K("id")).equals("from_game_over") && G.OVERLAY_MENU) dynamic_cast<UIBox *>(G.OVERLAY_MENU.get())->config->set("no_esc", true);
    };
    F["change_challenge_description"] = [](UIElement *e) {
        auto *ov = dynamic_cast<UIBox *>(G.OVERLAY_MENU.get());
        UIElement *area = ov ? ov->get_UIE_by_ID("challenge_area") : nullptr;
        const Value id = e ? e->config->get(K("id")) : Value("nil");
        if (!area || area->config->get(K("oid")) == id) return;
        if (auto *old = area->config->get(K("old_chosen")).as<UIElement>()) old->config->set("chosen", Value());
        if (e) e->config->set("chosen", "vert");
        if (Node *o = area->config->get(K("object")).as<Node>()) o->remove();
        area->config->set("object", Value((Object *)new UIBox(challenge_description(id, false),
                                                              vl::T({{"offset", Value(vl::T({{"x", 0}, {"y", 0}}))}, {"align", "cm"}, {"parent", Value((Object *)area)}}))));
        area->config->set("oid", id);
        area->config->set("old_chosen", e ? Value((Object *)e) : Value());
    };
    G.FUNCS_T["change_challenge_list_page"] = [](const TableRef &args) {
        if (!args || !args->get(K("cycle_config")).truthy()) return;
        auto *ov = dynamic_cast<UIBox *>(G.OVERLAY_MENU.get());
        UIElement *list = ov ? ov->get_UIE_by_ID("challenge_list") : nullptr;
        if (!list) return;
        if (Node *o = list->config->get(K("object")).as<Node>()) o->remove();
        const int page = (int)fld(args->get(K("cycle_config")), "current_option").num() - 1;
        list->config->set("object", Value((Object *)new UIBox(challenge_list_page(page),
                                                              vl::T({{"offset", Value(vl::T({{"x", 0}, {"y", 0}}))}, {"align", "cm"}, {"parent", Value((Object *)list)}}))));
        G.FUNCS["change_challenge_description"](nullptr);
    };
    F["start_challenge_run"] = [](UIElement *e) {
        if (G.OVERLAY_MENU) exit_overlay_menu();
        start_run_with(e, vl::T({{"stake", 1}, {"challenge", G.CHALLENGES->get(e->config->get(K("id")))}}));
    };
    F["deck_view_challenge"] = [](UIElement *e) {
        const Value ch = e->config->get(K("id"));
        overlay_menu(create_UIBox_generic_options(
            vl::T({{"back_func", "deck_info"}, {"contents", Value(A({Value(challenge_description(Value((double)get_challenge_int_from_id(fld(ch, "id"))), true))}))}})));
    };
}

}  // namespace vl
