// Challenges (UI_definitions.lua G.UIDEF.challenges: the run setup's Challenges tab).
#include "engine/ui.h"
#include "game/functions.h"
#include "game/g.h"
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

// the run setup's Challenges tab entry
TableRef challenges_tab(bool from_game_over, bool chosen) {
    return vl::T({{"label", localize("b_challenges")},
                  {"chosen", chosen},
                  {"tab_definition_function", Value((Object *)new Func([](const Value &a) { return Value(challenges_overview(a.truthy())); }))},
                  {"tab_definition_function_args", from_game_over}});
}

}  // namespace vl
