// The card descriptions: common_events.lua generate_card_ui / get_type_colour,
// card.lua Card:generate_UIBox_ability_table / align_h_popup / hover, and the popup UI of
// UI_definitions.lua (G.UIDEF.card_h_popup, desc_from_rows, info_tip_from_rows, name_from_rows,
// create_badge, get_badge_colour, create_UIBox_detailed_tooltip).
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>

#include "engine/controller.h"
#include "engine/random.h"
#include "engine/text.h"
#include "engine/ui.h"
#include "game/blind.h"
#include "game/card.h"
#include "game/card_ui.h"
#include "game/functions.h"
#include "game/g.h"
#include "game/sound.h"
#include "game/ui_definitions.h"

namespace vl {

namespace {
inline Value fld(const Value &t, const char *k) { return t.get(Value(intern(k))); }
inline Value gv(const char *k) { return G.GAME ? G.GAME->get(Value(intern(k))) : Value(); }
Value colour(const char *n) { return G.colour(n); }
Value uic(const char *n) { return fld(colour("UI"), n); }
Value nd(int uit, TableRef cfg, TableRef nodes = nullptr) {
    TableRef t = vl::T({{"n", (double)uit}, {"config", Value(cfg)}});
    if (nodes) t->set("nodes", Value(nodes));
    return Value(t);
}
// a Lua node list where some entries may be nil (dropped, as a table constructor's trailing nils)
TableRef list(std::initializer_list<Value> items) {
    TableRef t = new_table();
    for (const Value &v : items)
        if (!v.is_nil()) t->push(v);
    return t;
}
std::string lower(std::string s) {
    for (char &c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
Value prob_text() { return Value("" + (G.GAME ? fld(gv("probabilities"), "normal").to_string() : std::string("1"))); }
Value lvars(std::initializer_list<Value> v) {
    TableRef t = new_table();
    for (const Value &x : v) t->push(x);
    return Value(t);
}
TableRef lvars_t(std::initializer_list<Value> v) {
    TableRef t = new_table();
    for (const Value &x : v) t->push(x);
    return t;
}
Value loc(const char *key, const char *cat) { return localize(key, cat); }
Value loc(const Value &key, const char *cat) { return localize(key.is_str() ? key.str() : std::string_view(), cat); }
Value career(const char *k) { return fld(fld(G.PROFILES->get(G.SETTINGS->get(K("profile"))), "career_stats"), k); }
}  // namespace

Value get_type_colour(const Value &c, Card *card) {
    if (fld(c, "unlocked").is_bool() && !fld(c, "unlocked").truthy() && !(card && card->bypass_lock)) return colour("BLACK");
    // (the original's `not _c.discoveredand and not ((_c.area ...) or not _c.area)` is never true)
    if (card && card->debuff) return Value(mix_colours(colour("RED"), colour("GREY"), 0.7f));
    const Value set = fld(c, "set");
    if (set.equals("Joker")) return colour("RARITY").get(fld(c, "rarity"));
    if (set.equals("Edition")) return colour("DARK_EDITION");
    if (set.equals("Booster")) return colour("BOOSTER");
    Value sec = colour("SECONDARY_SET").get(set);
    return sec.truthy() ? sec : lvars({0, 1, 1, 1});
}

Value get_badge_colour(const std::string &key) {
    static const std::pair<const char *, const char *> cols[] = {
        {"eternal", "ETERNAL"},      {"perishable", "PERISHABLE"}, {"rental", "RENTAL"},       {"foil", "DARK_EDITION"},
        {"holographic", "DARK_EDITION"}, {"polychrome", "DARK_EDITION"}, {"negative", "DARK_EDITION"}, {"gold_seal", "GOLD"},
        {"red_seal", "RED"},         {"blue_seal", "BLUE"},       {"purple_seal", "PURPLE"},  {"pinned_left", "ORANGE"}};
    for (auto &c : cols)
        if (key == c.first) return colour(c.second);
    return lvars({1, 0, 0, 1});
}

Value create_badge(const Value &str, const Value &badge_col, const Value &text_col, double scaling) {
    DynaText *dt = new DynaText(vl::T({{"string", str.truthy() ? str : Value("ERROR")},
                                       {"colours", Value(A({text_col.truthy() ? text_col : colour("WHITE")}))},
                                       {"float", true},
                                       {"shadow", true},
                                       {"offset_y", -0.05},
                                       {"silent", true},
                                       {"spacing", 1},
                                       {"scale", 0.33 * scaling}}));
    return nd(UIT_R, vl::T({{"align", "cm"}}),
              list({nd(UIT_R,
                       vl::T({{"align", "cm"}, {"colour", badge_col.truthy() ? badge_col : colour("GREEN")}, {"r", 0.1}, {"minw", 2}, {"minh", 0.4 * scaling},
                              {"emboss", 0.05}, {"padding", 0.03 * scaling}}),
                       list({nd(UIT_B, vl::T({{"h", 0.1}, {"w", 0.03}})), nd(UIT_O, vl::T({{"object", Value((Object *)dt)}})),
                             nd(UIT_B, vl::T({{"h", 0.1}, {"w", 0.03}}))}))}));
}

Value desc_from_rows(const Value &desc_nodes, bool empty, const Value &maxw) {
    TableRef t = new_table();
    if (Table *d = desc_nodes.table())
        for (int i = 1; i <= d->len(); i++) {
            TableRef cfg = vl::T({{"align", "cm"}});
            if (maxw.truthy()) cfg->set("maxw", maxw);
            t->push(nd(UIT_R, cfg, TableRef(d->get(i).table())));
        }
    TableRef cfg = vl::T({{"align", "cm"}, {"colour", empty ? colour("CLEAR") : uic("BACKGROUND_WHITE")}, {"r", 0.1}, {"padding", 0.04},
                          {"minw", 2}, {"minh", 0.8}, {"filler", true}});
    if (!empty) cfg->set("emboss", 0.05);
    return nd(UIT_R, cfg, list({nd(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.03}}), t)}));
}

Value info_tip_from_rows(const Value &desc_nodes, const Value &name) {
    TableRef t = new_table();
    if (Table *d = desc_nodes.table())
        for (int i = 1; i <= d->len(); i++) t->push(nd(UIT_R, vl::T({{"align", "cm"}}), TableRef(d->get(i).table())));
    return nd(UIT_R, vl::T({{"align", "cm"}, {"colour", Value(lighten(colour("GREY"), 0.15f))}, {"r", 0.1}}),
              list({nd(UIT_R, vl::T({{"align", "tm"}, {"minh", 0.36}, {"padding", 0.03}}),
                       list({nd(UIT_T, vl::T({{"text", name}, {"scale", 0.32}, {"colour", uic("TEXT_LIGHT")}}))})),
                    nd(UIT_R, vl::T({{"align", "cm"}, {"minw", 1.5}, {"minh", 0.4}, {"r", 0.1}, {"padding", 0.05}, {"colour", colour("WHITE")}}),
                       list({nd(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.03}}), t)}))}));
}

Value name_from_rows(const Value &name_nodes, const Value &background_colour) {
    Table *n = name_nodes.table();
    if (!n) return Value();
    bool any = false;
    n->each([&](const Value &, const Value &) { any = true; });
    if (!any) return Value();
    TableRef cfg = vl::T({{"align", "cm"}, {"padding", 0.05}, {"r", 0.1}});
    if (background_colour.truthy()) {
        cfg->set("colour", background_colour);
        cfg->set("emboss", 0.05);
    }
    return nd(UIT_R, cfg, TableRef(n));
}

// ------------------------------------------------------------------ generate_card_ui
TableRef generate_card_ui(const Value &c, TableRef full, const Value &specific_vars, const std::string &card_type_in, const TableRef &badges,
                          bool hide_desc, const Value &main_start_in, const Value &main_end_in) {
    const bool first_pass = !full;
    if (!full) full = vl::T({{"main", Value(new_table())}, {"info", Value(new_table())}, {"type", Value(new_table())}, {"badges", Value(badges ? badges : new_table())}});
    Value main_start = main_start_in, main_end = main_end_in;
    std::string card_type = card_type_in;
    TableRef desc_nodes = full->get(K("name")).truthy() ? TableRef(full->get(K("info")).table()) : TableRef(full->get(K("main")).table());
    if (full->get(K("name")).is_nil()) desc_nodes = TableRef(full->get(K("main")).table());
    std::string name_override;
    std::vector<Value> info_queue;
    const Value set = fld(c, "set"), name = fld(c, "name"), key = fld(c, "key"), cfg = fld(c, "config");
    auto nm = [&](const char *n) { return name.equals(n); };

    if (full->get(K("name")).truthy()) {
        Table *info = full->get(K("info")).table();
        info->push(Value(new_table()));
        desc_nodes = TableRef(info->get(info->len()).table());
    }

    if (!full->get(K("name")).truthy()) {
        if (specific_vars.truthy() && fld(specific_vars, "no_name").truthy()) {
            full->set("name", true);
        } else if (card_type == "Locked") {
            full->set("name", localize(vl::T({{"type", "name"}, {"set", "Other"}, {"key", "locked"}, {"nodes", Value(new_table())}})));
        } else if (card_type == "Undiscovered") {
            full->set("name", localize(vl::T({{"type", "name"}, {"set", "Other"}, {"key", Value("undiscovered_" + lower(set.to_string()))}, {"name_nodes", Value(new_table())}})));
        } else if (specific_vars.truthy() && (card_type == "Default" || card_type == "Enhanced")) {
            if (nm("Stone Card")) full->set("name", true);
            if (fld(specific_vars, "playing_card").truthy() && !nm("Stone Card")) {
                TableRef nodes = new_table();
                TableRef pv = lvars_t({loc(fld(specific_vars, "value"), "ranks"), loc(fld(specific_vars, "suit"), "suits_plural")});
                pv->set("colours", Value(A({fld(specific_vars, "colour")})));
                localize(vl::T({{"type", "other"}, {"key", "playing_card"}, {"set", "Other"}, {"nodes", Value(nodes)}, {"vars", Value(pv)}}));
                full->set("name", nodes->get(1));
            }
        } else if (card_type == "Booster") {
        } else {
            full->set("name", localize(vl::T({{"type", "name"}, {"set", set}, {"key", key}})));
        }
        full->set("card_type", Value(card_type.empty() ? set.to_string() : card_type));
    }

    Value loc_vars = Value(new_table());
    if (main_start.truthy()) desc_nodes->push(main_start);

    auto loc_desc = [&](const char *type, const Value &k, const Value &s, const Value &vars) {
        TableRef a = vl::T({{"type", type}, {"key", k}, {"nodes", Value(desc_nodes)}, {"vars", vars}});
        if (s.truthy()) a->set("set", s);
        localize(a);
    };
    auto ext = [&](const char *k) { return fld(cfg, k); };
    auto uc = [&](const char *k) { return fld(fld(c, "unlock_condition"), k); };
    auto ucx = [&](const char *k) { return fld(fld(fld(c, "unlock_condition"), "extra"), k); };

    if (set.equals("Other")) {
        loc_desc("other", key, Value(), specific_vars.truthy() ? specific_vars : fld(c, "vars"));
    } else if (card_type == "Locked") {
        if (fld(c, "wip").truthy()) loc_desc("other", Value("wip_locked"), Value("Other"), loc_vars);
        else if (fld(c, "demo").truthy() && specific_vars.truthy()) loc_desc("other", Value("demo_shop_locked"), Value(), loc_vars);
        else if (fld(c, "demo").truthy()) loc_desc("other", Value("demo_locked"), Value(), loc_vars);
        else {
            auto nf = [](const Value &v) { return Value(number_format(v)); };
            auto lpok = [&](const Value &v) { return loc(v, "poker_hands"); };
            auto suit = [&]() { return loc(ucx("suit"), "suits_singular"); };
            auto ename = [&]() { return localize(vl::T({{"type", "name_text"}, {"key", ucx("e_key")}, {"set", "Enhanced"}})); };
            const Value ex = uc("extra");
            if (nm("Mr. Bones")) loc_vars = lvars({ex, career("c_losses")});
            else if (nm("Acrobat")) loc_vars = lvars({ex, career("c_hands_played")});
            else if (nm("Sock and Buskin")) loc_vars = lvars({ex, career("c_face_cards_played")});
            else if (nm("Swashbuckler")) loc_vars = lvars({ex, career("c_jokers_sold")});
            else if (nm("Troubadour")) loc_vars = lvars({ex});
            else if (nm("Smeared Joker")) loc_vars = lvars({ucx("count"), ename()});
            else if (nm("Hanging Chad")) loc_vars = lvars({lpok(ex)});
            else if (nm("Rough Gem") || nm("Bloodstone") || nm("Arrowhead") || nm("Onyx Agate")) loc_vars = lvars({ucx("count"), suit()});
            else if (nm("Glass Joker")) loc_vars = lvars({ucx("count"), ename()});
            else if (nm("Showman") || nm("Flower Pot")) loc_vars = lvars({uc("ante")});
            else if (nm("Wee Joker") || nm("Merry Andy")) loc_vars = lvars({uc("n_rounds")});
            else if (nm("Oops! All 6s") || nm("The Idol") || nm("Stuntman")) loc_vars = lvars({nf(uc("chips"))});
            else if (nm("Seeing Double")) loc_vars = lvars({localize("ph_4_7_of_clubs")});
            else if (nm("The Duo") || nm("The Trio") || nm("The Family") || nm("The Order") || nm("The Tribe")) loc_vars = lvars({lpok(ex)});
            else if (nm("Satellite")) loc_vars = lvars({ex});
            else if (nm("Driver's License")) loc_vars = lvars({ucx("count")});
            else if (nm("Cartomancer")) loc_vars = lvars({uc("tarot_count")});
            else if (nm("Burnt Joker")) loc_vars = lvars({ex, career("c_cards_sold")});
            else if (nm("Bootstraps")) loc_vars = lvars({ucx("count")});
            else if (nm("Overstock Plus")) loc_vars = lvars({ex, career("c_shop_dollars_spent")});
            else if (nm("Liquidation") || nm("Glow Up") || nm("Petroglyph") || nm("Retcon") || nm("Palette")) loc_vars = lvars({ex});
            else if (nm("Tarot Tycoon")) loc_vars = lvars({ex, career("c_tarots_bought")});
            else if (nm("Planet Tycoon")) loc_vars = lvars({ex, career("c_planets_bought")});
            else if (nm("Reroll Glut")) loc_vars = lvars({ex, career("c_shop_rerolls")});
            else if (nm("Omen Globe")) loc_vars = lvars({ex, career("c_tarot_reading_used")});
            else if (nm("Observatory")) loc_vars = lvars({ex, career("c_planetarium_used")});
            else if (nm("Nacho Tong")) loc_vars = lvars({ex, career("c_cards_played")});
            else if (nm("Recyclomancy")) loc_vars = lvars({ex, career("c_cards_discarded")});
            else if (nm("Money Tree")) loc_vars = lvars({ex, career("c_round_interest_cap_streak")});
            else if (nm("Antimatter")) {
                Value vb = fld(fld(G.PROFILES->get(G.SETTINGS->get(K("profile"))), "voucher_usage"), "v_blank");
                loc_vars = lvars({ex, vb.truthy() ? fld(vb, "count") : Value(0)});
            } else if (nm("Illusion")) loc_vars = lvars({ex, career("c_playing_cards_bought")});
            if (fld(c, "rarity") == Value(4) && specific_vars.truthy() && !fld(specific_vars, "not_hidden").truthy())
                loc_desc("unlocks", Value("joker_locked_legendary"), Value("Other"), loc_vars);
            else
                loc_desc("unlocks", key, set, loc_vars);
        }
    } else if (hide_desc) {
        loc_desc("other", Value("undiscovered_" + lower(set.to_string())), set, Value());
    } else if (specific_vars.truthy() && fld(specific_vars, "debuffed").truthy()) {
        loc_desc("other", Value(std::string("debuffed_") + (fld(specific_vars, "playing_card").truthy() ? "playing_card" : "default")), Value(), Value());
    } else if (set.equals("Joker")) {
        auto center = [](const char *k) { return G.P_CENTERS->get(Value(intern(k))); };
        if (nm("Stone Joker") || nm("Marble Joker")) info_queue.push_back(center("m_stone"));
        else if (nm("Steel Joker")) info_queue.push_back(center("m_steel"));
        else if (nm("Glass Joker")) info_queue.push_back(center("m_glass"));
        else if (nm("Golden Ticket") || nm("Midas Mask")) info_queue.push_back(center("m_gold"));
        else if (nm("Lucky Cat")) info_queue.push_back(center("m_lucky"));
        else if (nm("Invisible Joker")) {
            if (G.jokers)
                for (auto &v : G.jokers->cards)
                    if (v->edition && v->edition->get(K("negative")).truthy() && fld(fld(fld(Value(G.localization), "descriptions"), "Other"), "remove_negative").truthy()) {
                        TableRef me = new_table();
                        localize(vl::T({{"type", "other"}, {"key", "remove_negative"}, {"nodes", Value(me)}, {"vars", Value(new_table())}}));
                        main_end = me->get(1);
                        break;
                    }
        } else if (nm("Diet Cola")) info_queue.push_back(Value(vl::T({{"key", "tag_double"}, {"set", "Tag"}})));
        else if (nm("Perkeo"))
            info_queue.push_back(Value(vl::T({{"key", "e_negative_consumable"}, {"set", "Edition"}, {"config", Value(vl::T({{"extra", 1}}))}})));
        if (specific_vars.truthy() && fld(specific_vars, "pinned").truthy()) info_queue.push_back(Value(vl::T({{"key", "pinned_left"}, {"set", "Other"}})));
        if (specific_vars.truthy() && fld(specific_vars, "sticker").truthy())
            info_queue.push_back(Value(vl::T({{"key", Value(lower(fld(specific_vars, "sticker").to_string()) + "_sticker")}, {"set", "Other"}})));
        loc_desc("descriptions", key, set, specific_vars.truthy() ? specific_vars : Value(new_table()));
    } else if (set.equals("Tag")) {
        static const std::pair<const char *, const char *> q[] = {{"Negative Tag", "e_negative"},       {"Foil Tag", "e_foil"},
                                                                  {"Holographic Tag", "e_holo"},       {"Polychrome Tag", "e_polychrome"},
                                                                  {"Charm Tag", "p_arcana_mega_1"},    {"Meteor Tag", "p_celestial_mega_1"},
                                                                  {"Ethereal Tag", "p_spectral_normal_1"}, {"Standard Tag", "p_standard_mega_1"},
                                                                  {"Buffoon Tag", "p_buffoon_mega_1"}};
        for (auto &e : q)
            if (nm(e.first)) {
                info_queue.push_back(G.P_CENTERS->get(Value(e.second)));
                break;
            }
        loc_desc("descriptions", key, Value("Tag"), specific_vars.truthy() ? specific_vars : Value(new_table()));
    } else if (set.equals("Voucher")) {
        if (nm("Tarot Merchant") || nm("Tarot Tycoon") || nm("Planet Merchant") || nm("Planet Tycoon")) loc_vars = lvars({ext("extra_disp")});
        else if (nm("Hone") || nm("Glow Up") || nm("Reroll Surplus") || nm("Reroll Glut") || nm("Grabber") || nm("Nacho Tong") || nm("Wasteful") ||
                 nm("Recyclomancy") || nm("Hieroglyph") || nm("Petroglyph") || nm("Director's Cut") || nm("Retcon") || nm("Paint Brush") ||
                 nm("Palette") || nm("Telescope") || nm("Observatory") || nm("Clearance Sale") || nm("Liquidation"))
            loc_vars = lvars({ext("extra")});
        else if (nm("Seed Money") || nm("Money Tree")) loc_vars = lvars({ext("extra").num() / 5});
        loc_desc("descriptions", key, set, loc_vars);
    } else if (set.equals("Edition")) {
        loc_desc("descriptions", key, set, lvars({ext("extra")}));
    } else if (set.equals("Default") && specific_vars.truthy()) {
        if (fld(specific_vars, "nominal_chips").truthy()) loc_desc("other", Value("card_chips"), Value(), lvars({fld(specific_vars, "nominal_chips")}));
        if (fld(specific_vars, "bonus_chips").truthy()) loc_desc("other", Value("card_extra_chips"), Value(), lvars({fld(specific_vars, "bonus_chips")}));
    } else if (set.equals("Enhanced")) {
        if (specific_vars.truthy() && !nm("Stone Card") && fld(specific_vars, "nominal_chips").truthy())
            loc_desc("other", Value("card_chips"), Value(), lvars({fld(specific_vars, "nominal_chips")}));
        const Value eff = fld(c, "effect");
        const Value pn = fld(gv("probabilities"), "normal");
        const Value bonus = specific_vars.truthy() && fld(specific_vars, "bonus_chips").truthy() ? fld(specific_vars, "bonus_chips") : ext("bonus");
        if (eff.equals("Mult Card")) loc_vars = lvars({ext("mult")});
        else if (eff.equals("Glass Card")) loc_vars = lvars({ext("Xmult"), pn, ext("extra")});
        else if (eff.equals("Steel Card")) loc_vars = lvars({ext("h_x_mult")});
        else if (eff.equals("Stone Card")) loc_vars = lvars({bonus});
        else if (eff.equals("Gold Card")) loc_vars = lvars({ext("h_dollars")});
        else if (eff.equals("Lucky Card")) loc_vars = lvars({pn, ext("mult"), 5, ext("p_dollars"), 15});
        loc_desc("descriptions", key, set, loc_vars);
        if (!nm("Stone Card") && bonus.truthy()) loc_desc("other", Value("card_extra_chips"), Value(), lvars({bonus}));
    } else if (set.equals("Booster")) {
        static const std::pair<const char *, const char *> packs[] = {
            {"Arcana Pack", "p_arcana_normal"},       {"Jumbo Arcana Pack", "p_arcana_jumbo"},       {"Mega Arcana Pack", "p_arcana_mega"},
            {"Celestial Pack", "p_celestial_normal"}, {"Jumbo Celestial Pack", "p_celestial_jumbo"}, {"Mega Celestial Pack", "p_celestial_mega"},
            {"Spectral Pack", "p_spectral_normal"},   {"Jumbo Spectral Pack", "p_spectral_jumbo"},   {"Mega Spectral Pack", "p_spectral_mega"},
            {"Standard Pack", "p_standard_normal"},   {"Jumbo Standard Pack", "p_standard_jumbo"},   {"Mega Standard Pack", "p_standard_mega"},
            {"Buffoon Pack", "p_buffoon_normal"},     {"Jumbo Buffoon Pack", "p_buffoon_jumbo"},     {"Mega Buffoon Pack", "p_buffoon_mega"}};
        std::string desc_override = "p_arcana_normal";
        for (auto &p : packs)
            if (nm(p.first)) {
                desc_override = p.second;
                loc_vars = lvars({ext("choose"), ext("extra")});
            }
        name_override = desc_override;
        if (!full->get(K("name")).truthy()) full->set("name", localize(vl::T({{"type", "name"}, {"set", "Other"}, {"key", Value(name_override)}})));
        loc_desc("other", Value(desc_override), Value(), loc_vars);
    } else if (set.equals("Spectral")) {
        auto center = [](const char *k) { return G.P_CENTERS->get(Value(intern(k))); };
        auto other = [](const char *k) { return Value(vl::T({{"key", k}, {"set", "Other"}})); };
        if (nm("Familiar") || nm("Grim") || nm("Incantation")) loc_vars = lvars({ext("extra")});
        else if (nm("Immolate")) loc_vars = lvars({fld(ext("extra"), "destroy"), fld(ext("extra"), "dollars")});
        else if (nm("Hex")) info_queue.push_back(center("e_polychrome"));
        else if (nm("Talisman")) info_queue.push_back(other("gold_seal"));
        else if (nm("Deja Vu")) info_queue.push_back(other("red_seal"));
        else if (nm("Trance")) info_queue.push_back(other("blue_seal"));
        else if (nm("Medium")) info_queue.push_back(other("purple_seal"));
        else if (nm("Ankh")) {
            if (G.jokers)
                for (auto &v : G.jokers->cards)
                    if (v->edition && v->edition->get(K("negative")).truthy() && fld(fld(fld(Value(G.localization), "descriptions"), "Other"), "remove_negative").truthy()) {
                        info_queue.push_back(center("e_negative"));
                        TableRef me = new_table();
                        localize(vl::T({{"type", "other"}, {"key", "remove_negative"}, {"nodes", Value(me)}, {"vars", Value(new_table())}}));
                        main_end = me->get(1);
                        break;
                    }
        } else if (nm("Cryptid")) loc_vars = lvars({ext("extra")});
        if (nm("Ectoplasm")) {
            info_queue.push_back(center("e_negative"));
            loc_vars = lvars({gv("ecto_minus").truthy() ? gv("ecto_minus") : Value(1)});
        }
        if (nm("Aura")) {
            info_queue.push_back(center("e_foil"));
            info_queue.push_back(center("e_holo"));
            info_queue.push_back(center("e_polychrome"));
        }
        loc_desc("descriptions", key, set, loc_vars);
    } else if (set.equals("Planet")) {
        Value h = fld(gv("hands"), std::string(ext("hand_type").str()).c_str());
        const double lvl = fld(h, "level").num();
        TableRef v = lvars_t({fld(h, "level"), loc(ext("hand_type"), "poker_hands"), fld(h, "l_mult"), fld(h, "l_chips")});
        v->set("colours", Value(A({lvl == 1 ? uic("TEXT_DARK") : colour("HAND_LEVELS").get(std::min(7.0, lvl))})));
        loc_desc("descriptions", key, set, Value(v));
    } else if (set.equals("Tarot")) {
        auto center = [](const Value &k) { return G.P_CENTERS->get(k); };
        auto ename = [&]() { return localize(vl::T({{"type", "name_text"}, {"set", "Enhanced"}, {"key", ext("mod_conv")}})); };
        auto suits = [&]() {
            TableRef v = lvars_t({ext("max_highlighted"), loc(ext("suit_conv"), "suits_plural")});
            v->set("colours", Value(A({colour("SUITS").get(ext("suit_conv"))})));
            return Value(v);
        };
        if (nm("The Fool")) {
            Value fool_c = gv("last_tarot_planet").truthy() ? center(gv("last_tarot_planet")) : Value();
            Value last = fool_c.truthy() ? localize(vl::T({{"type", "name_text"}, {"key", fld(fool_c, "key")}, {"set", fld(fool_c, "set")}})) : localize("k_none");
            const bool bad = !fool_c.truthy() || fld(fool_c, "name").equals("The Fool");
            main_end = Value(list({nd(UIT_C, vl::T({{"align", "bm"}, {"padding", 0.02}}),
                                      list({nd(UIT_C, vl::T({{"align", "m"}, {"colour", colour(bad ? "RED" : "GREEN")}, {"r", 0.05}, {"padding", 0.05}}),
                                               list({nd(UIT_T, vl::T({{"text", Value(" " + last.to_string() + " ")}, {"colour", uic("TEXT_LIGHT")}, {"scale", 0.3},
                                                                      {"shadow", true}}))}))}))}));
            loc_vars = lvars({last});
            if (!bad) info_queue.push_back(fool_c);
        } else if (nm("The Magician") || nm("The Empress") || nm("The Hierophant") || nm("The Lovers") || nm("The Chariot") || nm("Justice") ||
                   nm("The Devil") || nm("The Tower")) {
            loc_vars = lvars({ext("max_highlighted"), ename()});
            info_queue.push_back(center(ext("mod_conv")));
        } else if (nm("The High Priestess")) loc_vars = lvars({ext("planets")});
        else if (nm("The Emperor")) loc_vars = lvars({ext("tarots")});
        else if (nm("The Hermit")) loc_vars = lvars({ext("extra")});
        else if (nm("The Wheel of Fortune")) {
            loc_vars = lvars({fld(gv("probabilities"), "normal"), ext("extra")});
            for (const char *e : {"e_foil", "e_holo", "e_polychrome"}) info_queue.push_back(G.P_CENTERS->get(Value(e)));
        } else if (nm("Strength") || nm("The Hanged Man") || nm("Death")) loc_vars = lvars({ext("max_highlighted")});
        else if (nm("Temperance")) {
            double money = 0;
            if (G.jokers)
                for (auto &j : G.jokers->cards)
                    if (j->ability->get(K("set")).equals("Joker")) money += j->sell_cost;
            loc_vars = lvars({ext("extra"), std::min(ext("extra").num(), money)});
        } else if (nm("The Star") || nm("The Moon") || nm("The Sun") || nm("The World")) loc_vars = suits();
        loc_desc("descriptions", key, set, loc_vars);
    }

    if (main_end.truthy()) desc_nodes->push(main_end);

    if (!((specific_vars.truthy() && !fld(specific_vars, "sticker").truthy()) && (card_type == "Default" || card_type == "Enhanced"))) {
        if (desc_nodes.get() == full->get(K("main")).table() && !full->get(K("name")).truthy()) {
            // (the original localizes without nodes and drops the result)
            if (!full->get(K("name")).truthy()) full->set("name", Value(new_table()));
        } else if (desc_nodes.get() != full->get(K("main")).table()) {
            desc_nodes->set("name", localize(vl::T({{"type", "name_text"},
                                                    {"key", name_override.empty() ? key : Value(name_override)},
                                                    {"set", name_override.empty() ? set : Value("Other")}})));
        }
    }

    if (first_pass && !set.equals("Edition") && badges) {
        for (int i = 1; i <= badges->len(); i++) {
            const std::string v = badges->get(i).to_string();
            auto other = [](const char *k) { return Value(vl::T({{"key", k}, {"set", "Other"}})); };
            if (v == "foil") info_queue.push_back(G.P_CENTERS->get(K("e_foil")));
            if (v == "holographic") info_queue.push_back(G.P_CENTERS->get(K("e_holo")));
            if (v == "polychrome") info_queue.push_back(G.P_CENTERS->get(K("e_polychrome")));
            if (v == "negative") info_queue.push_back(G.P_CENTERS->get(K("e_negative")));
            if (v == "negative_consumable")
                info_queue.push_back(Value(vl::T({{"key", "e_negative_consumable"}, {"set", "Edition"}, {"config", Value(vl::T({{"extra", 1}}))}})));
            if (v == "gold_seal" || v == "blue_seal" || v == "red_seal" || v == "purple_seal" || v == "eternal" || v == "pinned_left")
                info_queue.push_back(other(v.c_str()));
            if (v == "perishable") {
                Value pr = gv("perishable_rounds").truthy() ? gv("perishable_rounds") : Value(1);
                Value pt = fld(specific_vars, "perish_tally").truthy() ? fld(specific_vars, "perish_tally") : gv("perishable_rounds");
                info_queue.push_back(Value(vl::T({{"key", "perishable"}, {"set", "Other"}, {"vars", lvars({pr, pt})}})));
            }
            if (v == "rental")
                info_queue.push_back(Value(vl::T({{"key", "rental"}, {"set", "Other"}, {"vars", lvars({gv("rental_rate").truthy() ? gv("rental_rate") : Value(1)})}})));
        }
    }
    for (const Value &v : info_queue) generate_card_ui(v, full);
    return full;
}

// ------------------------------------------------------------------ Card:generate_UIBox_ability_table
TableRef Card::generate_UIBox_ability_table() {
    std::string card_type = ability->get(K("set")).truthy() ? ability->get(K("set")).to_string() : "None";
    bool hide_desc = false, no_badge = false;
    Value loc_vars, main_start, main_end;
    const Value center = config->get(K("center"));
    const Value set = ability->get(K("set"));
    const bool unlocked_false = fld(center, "unlocked").is_bool() && !fld(center, "unlocked").truthy();
    const bool in_owned = area && ((G.jokers && area == G.jokers.get()) || (G.consumeables && area == G.consumeables.get()));
    if (!bypass_lock && !unlocked_false &&
        (set.equals("Joker") || set.equals("Edition") || ability->get(K("consumeable")).truthy() || set.equals("Voucher") || set.equals("Booster")) &&
        !fld(center, "discovered").truthy() && ((!in_owned && area) || !area))
        card_type = "Undiscovered";
    auto ab = [&](const char *k) { return ability->get(Value(intern(k))); };
    auto ex = [&](const char *k) { return fld(ab("extra"), k); };
    auto lv = [&](std::initializer_list<Value> v) { loc_vars = lvars(v); };
    const std::string name = ab("name").is_str() ? std::string(ab("name").str()) : std::string();
    auto is = [&](std::initializer_list<const char *> ns) {
        for (const char *n : ns)
            if (name == n) return true;
        return false;
    };
    auto cr = [&](const char *k) { return fld(gv("current_round"), k); };
    auto suit_col = [&](const Value &suit) { return Value(A({colour("SUITS").get(suit)})); };

    if (unlocked_false && !bypass_lock) {
        card_type = "Locked";
    } else if (card_type == "Undiscovered" && !bypass_discovery_ui) {
        hide_desc = true;
    } else if (debuff) {
        loc_vars = Value(vl::T({{"debuffed", true}, {"playing_card", base->get(K("colour")).truthy()}, {"value", base->get(K("value"))},
                                {"suit", base->get(K("suit"))}, {"colour", base->get(K("colour"))}}));
    } else if (card_type == "Default" || card_type == "Enhanced") {
        TableRef v = vl::T({{"playing_card", base->get(K("colour")).truthy()}, {"value", base->get(K("value"))}, {"suit", base->get(K("suit"))},
                            {"colour", base->get(K("colour"))}});
        if (base->get(K("nominal")).num() > 0) v->set("nominal_chips", base->get(K("nominal")));
        const double bonus = ab("bonus").num() + (ab("perma_bonus").truthy() ? ab("perma_bonus").num() : 0);
        if (bonus > 0) v->set("bonus_chips", bonus);
        loc_vars = Value(v);
    } else if (set.equals("Joker")) {
        const Value prob = prob_text();
        if (name == "Joker") lv({ab("mult")});
        else if (is({"Jolly Joker", "Zany Joker", "Mad Joker", "Crazy Joker", "Droll Joker"})) lv({ab("t_mult"), loc(ab("type"), "poker_hands")});
        else if (is({"Sly Joker", "Wily Joker", "Clever Joker", "Devious Joker", "Crafty Joker"})) lv({ab("t_chips"), loc(ab("type"), "poker_hands")});
        else if (name == "Half Joker") lv({ex("mult"), ex("size")});
        else if (name == "Fortune Teller") {
            Value cut = gv("consumeable_usage_total");
            lv({ab("extra"), cut.truthy() ? fld(cut, "tarot") : Value(0)});
        } else if (name == "Steel Joker") lv({ab("extra"), 1 + ab("extra").num() * (ab("steel_tally").truthy() ? ab("steel_tally").num() : 0)});
        else if (name == "Chaos the Clown") lv({ab("extra")});
        else if (name == "Space Joker") lv({prob, ab("extra")});
        else if (name == "Stone Joker") lv({ab("extra"), ab("extra").num() * (ab("stone_tally").truthy() ? ab("stone_tally").num() : 0)});
        else if (name == "Drunkard") lv({ab("d_size")});
        else if (name == "Green Joker") lv({ex("hand_add"), ex("discard_sub"), ab("mult")});
        else if (name == "Credit Card") lv({ab("extra")});
        else if (is({"Greedy Joker", "Lusty Joker", "Wrathful Joker", "Gluttonous Joker"})) lv({ex("s_mult"), loc(ex("suit"), "suits_singular")});
        else if (name == "Blue Joker") lv({ab("extra"), ab("extra").num() * (G.deck ? (double)G.deck->cards.size() : 52)});
        else if (name == "Sixth Sense") loc_vars = Value(new_table());
        else if (name == "Hack") lv({ab("extra").num() + 1});
        else if (name == "Faceless Joker") lv({ex("dollars"), ex("faces")});
        else if (name == "Juggler") lv({ab("h_size")});
        else if (name == "Golden Joker") lv({ab("extra")});
        else if (name == "Joker Stencil") lv({ab("x_mult")});
        else if (name == "Ceremonial Dagger") lv({ab("mult")});
        else if (name == "Banner") lv({ab("extra")});
        else if (name == "Misprint") {
            const double text_scale = G.F_MOBILE ? 1.45 : 1;
            TableRef r_mults = new_table();
            for (int i = (int)ex("min").num(); i <= (int)ex("max").num(); i++) r_mults->push(Value(std::to_string(i)));
            const std::string loc_mult = " " + localize("k_mult").to_string() + " ";
            std::string last;
            if (G.deck && !G.deck->cards.empty()) {
                Card *lc = G.deck->cards.back().get();
                last = lc->base->get(K("id")).to_string() + lc->base->get(K("suit")).to_string().substr(0, 1);
            } else {
                last = "11D";
            }
            TableRef strs = A({Value(vl::T({{"string", "rand()"}, {"colour", colour("JOKER_GREY")}})),
                               Value(vl::T({{"string", Value("#@" + last)}, {"colour", colour("RED")}}))});
            for (int i = 0; i < 13; i++) strs->push(Value(loc_mult));
            main_start = Value(list(
                {nd(UIT_T, vl::T({{"text", "   +"}, {"colour", colour("MULT")}, {"scale", 0.32 * text_scale}})),
                 nd(UIT_O, vl::T({{"object", Value((Object *)new DynaText(vl::T({{"string", Value(r_mults)},
                                                                                 {"colours", Value(A({colour("RED")}))},
                                                                                 {"pop_in_rate", 9999999},
                                                                                 {"silent", true},
                                                                                 {"random_element", true},
                                                                                 {"pop_delay", 0.5},
                                                                                 {"scale", 0.32 * text_scale},
                                                                                 {"min_cycle_time", 0}})))}})),
                 nd(UIT_O, vl::T({{"object", Value((Object *)new DynaText(vl::T({{"string", Value(strs)},
                                                                                 {"colours", Value(A({uic("TEXT_DARK")}))},
                                                                                 {"pop_in_rate", 9999999},
                                                                                 {"silent", true},
                                                                                 {"random_element", true},
                                                                                 {"pop_delay", 0.2011},
                                                                                 {"scale", 0.32 * text_scale},
                                                                                 {"min_cycle_time", 0}})))}}))}));
        } else if (name == "Mystic Summit") lv({ex("mult"), ex("d_remaining")});
        else if (name == "Loyalty Card")
            lv({ex("Xmult"), ex("every").num() + 1,
                localize(vl::T({{"type", "variable"}, {"key", ab("loyalty_remaining") == Value(0) ? "loyalty_active" : "loyalty_inactive"},
                                {"vars", lvars({ab("loyalty_remaining")})}}))});
        else if (name == "8 Ball") lv({prob, ab("extra")});
        else if (name == "Dusk") lv({ab("extra").num() + 1});
        else if (name == "Fibonacci" || name == "Scary Face") lv({ab("extra")});
        else if (name == "Abstract Joker") lv({ab("extra"), (G.jokers ? (double)G.jokers->cards.size() : 0) * ab("extra").num()});
        else if (name == "Delayed Gratification") lv({ab("extra")});
        else if (name == "Gros Michel") lv({ex("mult"), prob, ex("odds")});
        else if (name == "Even Steven" || name == "Odd Todd") lv({ab("extra")});
        else if (name == "Scholar") lv({ex("mult"), ex("chips")});
        else if (name == "Business Card") lv({prob, ab("extra")});
        else if (name == "Spare Trousers") lv({ab("extra"), localize("Two Pair", "poker_hands"), ab("mult")});
        else if (name == "Superposition") lv({ab("extra")});
        else if (name == "Ride the Bus") lv({ab("extra"), ab("mult")});
        else if (name == "Egg" || name == "Burglar") lv({ab("extra")});
        else if (name == "Blackboard") lv({ab("extra"), localize("Spades", "suits_plural"), localize("Clubs", "suits_plural")});
        else if (name == "Runner" || name == "Ice Cream") lv({ex("chips"), ex("chip_mod")});
        else if (name == "DNA") lv({ab("extra")});
        else if (name == "Constellation") lv({ab("extra"), ab("x_mult")});
        else if (name == "Hiker") lv({ab("extra")});
        else if (name == "To Do List") lv({ex("dollars"), loc(ab("to_do_poker_hand"), "poker_hands")});
        else if (name == "Blueprint" || name == "Brainstorm") {
            if (!ab("blueprint_compat_ui").truthy()) ability->set("blueprint_compat_ui", "");
            ability->set("blueprint_compat_check", Value());
            if (area && G.jokers && area == G.jokers.get())
                main_end = Value(list({nd(UIT_C, vl::T({{"align", "bm"}, {"minh", 0.4}}),
                                          list({nd(UIT_C,
                                                   vl::T({{"ref_table", Value((Object *)this)}, {"align", "m"}, {"colour", colour("JOKER_GREY")}, {"r", 0.05},
                                                          {"padding", 0.06}, {"func", "blueprint_compat"}}),
                                                   list({nd(UIT_T, vl::T({{"ref_table", Value(ability)}, {"ref_value", "blueprint_compat_ui"},
                                                                          {"colour", uic("TEXT_LIGHT")}, {"scale", 0.32 * 0.8}}))}))}))}));
        } else if (name == "Astronomer" || name == "Golden Ticket" || name == "Acrobat") lv({ab("extra")});
        else if (name == "Sock and Buskin") lv({ab("extra").num() + 1});
        else if (name == "Swashbuckler") lv({ab("mult")});
        else if (name == "Troubadour") lv({ex("h_size"), -ex("h_plays").num()});
        else if (name == "Certificate") lv({ab("extra")});
        else if (name == "Throwback") lv({ab("extra"), ab("x_mult")});
        else if (name == "Hanging Chad" || name == "Rough Gem") lv({ab("extra")});
        else if (name == "Bloodstone") lv({prob, ex("odds"), ex("Xmult")});
        else if (name == "Arrowhead" || name == "Onyx Agate") lv({ab("extra")});
        else if (name == "Glass Joker") lv({ab("extra"), ab("x_mult")});
        else if (name == "Flower Pot") lv({ab("extra")});
        else if (name == "Wee Joker") lv({ex("chips"), ex("chip_mod")});
        else if (name == "Merry Andy") lv({ab("d_size"), ab("h_size")});
        else if (name == "The Idol") {
            Value idol = cr("idol_card");
            TableRef v = lvars_t({ab("extra"), loc(fld(idol, "rank"), "ranks"), loc(fld(idol, "suit"), "suits_plural")});
            v->set("colours", suit_col(fld(idol, "suit")));
            loc_vars = Value(v);
        } else if (name == "Seeing Double" || name == "Matador") lv({ab("extra")});
        else if (name == "Hit the Road") lv({ab("extra"), ab("x_mult")});
        else if (is({"The Duo", "The Trio", "The Family", "The Order", "The Tribe"})) lv({ab("x_mult"), loc(ab("type"), "poker_hands")});
        else if (name == "Cavendish") lv({ex("Xmult"), prob, ex("odds")});
        else if (name == "Card Sharp") lv({ex("Xmult")});
        else if (name == "Red Card") lv({ab("extra"), ab("mult")});
        else if (name == "Madness") lv({ab("extra"), ab("x_mult")});
        else if (name == "Square Joker") lv({ex("chips"), ex("chip_mod")});
        else if (name == "Seance") lv({loc(ex("poker_hand"), "poker_hands")});
        else if (name == "Riff-raff") lv({ab("extra")});
        else if (name == "Vampire" || name == "Hologram") lv({ab("extra"), ab("x_mult")});
        else if (name == "Vagabond" || name == "Baron") lv({ab("extra")});
        else if (name == "Cloud 9") lv({ab("extra"), ab("extra").num() * (ab("nine_tally").truthy() ? ab("nine_tally").num() : 0)});
        else if (name == "Rocket") lv({ex("dollars"), ex("increase")});
        else if (name == "Obelisk") lv({ab("extra"), ab("x_mult")});
        else if (name == "Luchador") {
            if (G.GAME && area && G.jokers && area == G.jokers.get()) {
                Blind *b = gv("blind").as<Blind>();
                const bool disableable = b && !b->disabled && b->get_type() && std::string_view(b->get_type()) == "Boss";
                main_end = Value(list({nd(UIT_C, vl::T({{"align", "bm"}, {"minh", 0.4}}),
                                          list({nd(UIT_C,
                                                   vl::T({{"ref_table", Value((Object *)this)}, {"align", "m"}, {"colour", colour(disableable ? "GREEN" : "RED")},
                                                          {"r", 0.05}, {"padding", 0.06}}),
                                                   list({nd(UIT_T, vl::T({{"text", Value(" " + localize(disableable ? "k_active" : "ph_no_boss_active").to_string() + " ")},
                                                                          {"colour", uic("TEXT_LIGHT")},
                                                                          {"scale", 0.32 * 0.9}}))}))}))}));
            }
        } else if (name == "Photograph" || name == "Gift Card") lv({ab("extra")});
        else if (name == "Turtle Bean") lv({ex("h_size"), ex("h_mod")});
        else if (name == "Erosion")
            lv({ab("extra"), std::max(0.0, ab("extra").num() * (G.has_playing_cards ? gv("starting_deck_size").num() - G.playing_cards.size() : 0)),
                gv("starting_deck_size")});
        else if (name == "Reserved Parking") lv({ex("dollars"), prob, ex("odds")});
        else if (name == "Mail-In Rebate") lv({ab("extra"), loc(fld(cr("mail_card"), "rank"), "ranks")});
        else if (name == "To the Moon") lv({ab("extra")});
        else if (name == "Hallucination") lv({fld(gv("probabilities"), "normal"), ab("extra")});
        else if (name == "Lucky Cat") lv({ab("extra"), ab("x_mult")});
        else if (name == "Baseball Card") lv({ab("extra")});
        else if (name == "Bull") lv({ab("extra"), ab("extra").num() * std::max(0.0, gv("dollars").num())});
        else if (name == "Diet Cola") lv({localize(vl::T({{"type", "name_text"}, {"set", "Tag"}, {"key", "tag_double"}, {"nodes", Value(new_table())}}))});
        else if (name == "Trading Card") lv({ab("extra")});
        else if (name == "Flash Card") lv({ab("extra"), ab("mult")});
        else if (name == "Popcorn") lv({ab("mult"), ab("extra")});
        else if (name == "Ramen") lv({ab("x_mult"), ab("extra")});
        else if (name == "Ancient Joker") {
            Value s = fld(cr("ancient_card"), "suit");
            TableRef v = lvars_t({ab("extra"), loc(s, "suits_singular")});
            v->set("colours", suit_col(s));
            loc_vars = Value(v);
        } else if (name == "Walkie Talkie") lv({ex("chips"), ex("mult")});
        else if (name == "Seltzer") lv({ab("extra")});
        else if (name == "Castle") {
            Value s = fld(cr("castle_card"), "suit");
            TableRef v = lvars_t({ex("chip_mod"), loc(s, "suits_singular"), ex("chips")});
            v->set("colours", suit_col(s));
            loc_vars = Value(v);
        } else if (name == "Smiley Face") lv({ab("extra")});
        else if (name == "Campfire") lv({ab("extra"), ab("x_mult")});
        else if (name == "Stuntman") lv({ex("chip_mod"), ex("h_size")});
        else if (name == "Invisible Joker") lv({ab("extra"), ab("invis_rounds")});
        else if (name == "Satellite") {
            int planets_used = 0;
            if (Table *u = gv("consumeable_usage").table())
                u->each([&](const Value &, const Value &v) {
                    if (fld(v, "set").equals("Planet")) planets_used++;
                });
            lv({ab("extra"), planets_used * ab("extra").num()});
        } else if (name == "Shoot the Moon") lv({ab("extra")});
        else if (name == "Driver's License") lv({ab("extra"), ab("driver_tally").truthy() ? ab("driver_tally") : Value("0")});
        else if (name == "Bootstraps") {
            const double d = gv("dollars").num() + (gv("dollar_buffer").truthy() ? gv("dollar_buffer").num() : 0);
            lv({ex("mult"), ex("dollars"), ex("mult").num() * std::floor(d / ex("dollars").num())});
        } else if (name == "Caino") lv({ab("extra"), ab("caino_xmult")});
        else if (name == "Triboulet") lv({ab("extra")});
        else if (name == "Yorick") lv({ex("xmult"), ex("discards"), ab("yorick_discards"), ab("x_mult")});
        else if (name == "Perkeo") lv({ab("extra")});
    }

    TableRef badges = new_table();
    if ((card_type != "Locked" && card_type != "Undiscovered" && card_type != "Default") || debuff) badges->set("card_type", Value(card_type));
    if (set.equals("Joker") && bypass_discovery_ui && !no_badge) badges->set("force_rarity", true);
    if (edition) {
        const Value et = edition->get(K("type"));
        if (et.equals("negative") && ab("consumeable").truthy()) badges->push(Value("negative_consumable"));
        else badges->push(et.equals("holo") ? Value("holographic") : et);
    }
    if (seal.truthy()) badges->push(Value(lower(seal.to_string()) + "_seal"));
    if (ab("eternal").truthy()) badges->push(Value("eternal"));
    if (ab("perishable").truthy()) {
        if (!loc_vars.truthy()) loc_vars = Value(new_table());
        loc_vars.table()->set("perish_tally", ab("perish_tally"));
        badges->push(Value("perishable"));
    }
    if (ab("rental").truthy()) badges->push(Value("rental"));
    if (pinned) badges->push(Value("pinned_left"));
    if (sticker.truthy() || (sticker_run.truthy() && !sticker_run.equals("NONE") && G.SETTINGS->get(K("run_stake_stickers")).truthy())) {
        if (!loc_vars.truthy()) loc_vars = Value(new_table());
        loc_vars.table()->set("sticker", sticker.truthy() ? sticker : sticker_run);
    }
    return generate_card_ui(center, nullptr, loc_vars, card_type, badges, hide_desc, main_start, main_end);
}

// ------------------------------------------------------------------ G.UIDEF.card_h_popup
TableRef card_h_popup(Card *card) {
    TableRef AUT = card->ability_UIBox_table;
    if (!AUT) return nullptr;
    const bool debuffed = card->debuff;
    const Value center = card->config->get(K("center")).truthy() ? card->config->get(K("center")) : Value(card->config);
    const Value card_type_colour = get_type_colour(center, card);
    const std::string act = AUT->get(K("card_type")).to_string();
    Value card_type_background;
    if (act == "Locked") card_type_background = colour("BLACK");
    else if (act == "Undiscovered") card_type_background = Value(darken(colour("JOKER_GREY"), 0.3f));
    else if (act == "Enhanced" || act == "Default" || debuffed || card_type_colour.truthy()) card_type_background = Value(darken(colour("BLACK"), 0.1f));
    else card_type_background = colour("SET").get(Value(act)).truthy() ? colour("SET").get(Value(act)) : lvars({0, 1, 1, 1});

    const double outer_padding = 0.05;
    Value card_type = localize("k_" + lower(act));
    Value AUTb = AUT->get(K("badges"));
    if (act == "Joker" || fld(AUTb, "force_rarity").truthy()) {
        static const char *rar[] = {"k_common", "k_uncommon", "k_rare", "k_legendary"};
        const int r = (int)fld(center, "rarity").num();
        card_type = r >= 1 && r <= 4 ? localize(rar[r - 1]) : Value();
    }
    if (act == "Enhanced") card_type = localize(vl::T({{"type", "name_text"}, {"key", fld(center, "key")}, {"set", "Enhanced"}}));
    if (debuffed && act != "Enhanced") card_type = localize("k_debuffed");
    const bool is_playing_card = act == "Enhanced" || act == "Default";

    TableRef info_boxes = new_table();
    TableRef badges = new_table();
    if (fld(AUTb, "card_type").truthy() || fld(AUTb, "force_rarity").truthy()) {
        const Value an = card->ability->get(K("name"));
        Value label = (an.equals("Pluto") || an.equals("Ceres") || an.equals("Eris")) ? localize("k_dwarf_planet")
                      : an.equals("Planet X")                                         ? localize("k_planet_q")
                                                                                       : card_type;
        badges->push(create_badge(label, card_type_colour, Value(), 1.2));
    }
    if (Table *b = AUTb.table())
        for (int i = 1; i <= b->len(); i++) {
            std::string v = b->get(i).to_string();
            if (v == "negative_consumable") v = "negative";
            badges->push(create_badge(localize(v, "labels"), get_badge_colour(v)));
        }
    if (Table *info = AUT->get(K("info")).table())
        for (int i = 1; i <= info->len(); i++) {
            Value v = info->get(i);
            info_boxes->push(nd(UIT_R, vl::T({{"align", "cm"}}),
                                list({nd(UIT_R, vl::T({{"align", "cm"}, {"colour", Value(lighten(colour("JOKER_GREY"), 0.5f))}, {"r", 0.1}, {"padding", 0.05}, {"emboss", 0.05}}),
                                         list({info_tip_from_rows(v, fld(v, "name"))}))})));
        }
    TableRef col_cfg = vl::T({{"align", "cm"}, {"func", "show_infotip"}, {"object", Value((Object *)new Moveable())}});
    if (info_boxes->len() > 0) col_cfg->set("ref_table", Value(info_boxes));
    return TableRef(nd(UIT_ROOT, vl::T({{"align", "cm"}, {"colour", colour("CLEAR")}}),
                       list({nd(UIT_C, col_cfg,
                                list({nd(UIT_R, vl::T({{"padding", outer_padding}, {"r", 0.12}, {"colour", Value(lighten(colour("JOKER_GREY"), 0.5f))}, {"emboss", 0.07}}),
                                         list({nd(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.07}, {"r", 0.1}, {"colour", Value(adjust_alpha(card_type_background, 0.8f))}}),
                                                  list({name_from_rows(AUT->get(K("name")), is_playing_card ? colour("WHITE") : Value()),
                                                        desc_from_rows(AUT->get(K("main"))),
                                                        badges->len() > 0 ? nd(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.03}}), badges) : Value()}))}))}))}))
                        .table());
}

TableRef create_UIBox_detailed_tooltip(const Value &center) {
    TableRef full = vl::T({{"main", Value(new_table())}, {"info", Value(new_table())}, {"type", Value(new_table())}, {"name", "done"}, {"badges", Value(new_table())}});
    TableRef desc = generate_card_ui(center, full, Value(), fld(center, "set").to_string(), nullptr);
    Value i1 = desc->get(K("info")).get(1);
    return TableRef(nd(UIT_ROOT, vl::T({{"align", "cm"}, {"colour", colour("CLEAR")}}),
                       list({nd(UIT_R, vl::T({{"align", "cm"}, {"colour", Value(lighten(colour("JOKER_GREY"), 0.5f))}, {"r", 0.1}, {"padding", 0.05}, {"emboss", 0.05}}),
                                list({info_tip_from_rows(i1, fld(i1, "name"))}))}))
                        .table());
}

// ------------------------------------------------------------------ Card hover / popup alignment
Value Card::get_field(const Value &key) const {
    if (key.equals("sell_cost_label")) return sell_cost_label;
    if (key.equals("sell_cost")) return Value(sell_cost);
    if (key.equals("cost")) return Value(cost);
    if (key.equals("ability")) return Value(ability);
    if (key.equals("base")) return Value(base);
    if (key.equals("edition")) return edition ? Value(edition) : Value();
    if (key.equals("config")) return Value(config);
    return Value();
}

TableRef Card::align_h_popup() {
    const bool focused_ui = children.get("focused_ui") != nullptr;
    const bool shop = area && area->config->get(K("type")).equals("shop");
    const bool view_deck = area && area->config->get(K("view_deck")).truthy();
    const std::string dir = (children.get("buy_button") || view_deck || shop) ? "cl" : (T->y < G.CARD_H * 1.4f) ? "bm" : "tm";
    double x = 0, y = 0;
    if (dir == "cl") {
        x = focused_ui ? -0.05 : ability->get(K("consumeable")).truthy() ? 0.0 : ability->get(K("set")).equals("Voucher") ? 0.0 : -0.05;
    }
    if (focused_ui) y = dir == "tm" ? ((area && G.hand && area == G.hand.get()) ? -0.08 : -0.15) : dir == "bm" ? 0.12 : 0;
    else y = dir == "tm" ? -0.13 : dir == "bm" ? 0.1 : 0;
    Node *major = children.get("focused_ui") ? children.get("focused_ui") : this;
    return vl::T({{"major", Value((Object *)major)},
                  {"parent", Value((Object *)this)},
                  {"xy_bond", "Strong"},
                  {"r_bond", "Weak"},
                  {"wh_bond", "Weak"},
                  {"offset", Value(vl::T({{"x", x}, {"y", y}}))},
                  {"type", Value(dir)}});
}

void Card::hover() {
    float r = 0.03f;
    juice_up_card(0.05f, &r);
    play_sound("paper1", (float)(lua_math.random() * 0.2 + 0.9), 0.35f);
    if (states.focus.is && !children.get("focused_ui")) children.set("focused_ui", card_focus_ui(this));
    if (facing == "front" && (!states.drag.is || G.CONTROLLER->HID.touch) && !no_ui) {
        if (children.get("alert") && !fld(config->get(K("center")), "alerted").truthy()) {
            config->get(K("center")).table()->set("alerted", true);
            G.save_progress();
        }
        ability_UIBox_table = generate_UIBox_ability_table();
        config->set("h_popup", Value(card_h_popup(this)));
        config->set("h_popup_config", Value(align_h_popup()));
        Node::hover();
    }
}

void Card::move(double dt) {
    Moveable::move(dt);
    if (auto *p = dynamic_cast<Moveable *>(children.get("h_popup"))) {
        // set_alignment(self:align_h_popup()): major, xy_bond Strong, r/wh_bond Weak, type, offset
        TableRef a = align_h_popup();
        static const Bond strong = Bond::Strong, weak = Bond::Weak;
        AlignArgs al;
        al.major = a->get(K("major")).as<Moveable>();
        al.bond = &strong;
        al.r_bond = &weak;
        al.wh_bond = &weak;
        std::string type(a->get(K("type")).str());
        al.type = type.c_str();
        XY off{(float)fld(a->get(K("offset")), "x").num(), (float)fld(a->get(K("offset")), "y").num()};
        al.offset = &off;
        p->set_alignment(al);
    }
}

void register_card_ui_callbacks() {
    auto &F = G.FUNCS;
    F["show_infotip"] = [](UIElement *e) {
        Value rt = e->config->get(K("ref_table"));
        if (rt.truthy()) {
            UIBox *info = new UIBox(vl::T({{"n", (double)UIT_ROOT}, {"config", Value(vl::T({{"align", "cm"}, {"colour", G.colour("CLEAR")}, {"padding", 0.02}}))},
                                           {"nodes", rt}}),
                                    vl::T({{"offset", Value(vl::T({{"x", -0.03}, {"y", 0}}))}, {"align", "cl"}, {"parent", Value((Object *)e)}}));
            if (Node *old = e->children.get("info")) old->remove();
            e->children.set("info", info);
            info->align_to_major();
            e->config->set("ref_table", Value());
        }
    };
    F["blueprint_compat"] = [](UIElement *e) {
        Card *c = e->config->get(K("ref_table")).as<Card>();
        if (!c) return;
        Value bc = c->ability->get(K("blueprint_compat"));
        if (!(bc == c->ability->get(K("blueprint_compat_check")))) {
            if (bc.equals("compatible")) e->config->set("colour", Value(mix_colours(G.colour("GREEN"), G.colour("JOKER_GREY"), 0.8f)));
            else if (bc.equals("incompatible")) e->config->set("colour", Value(mix_colours(G.colour("RED"), G.colour("JOKER_GREY"), 0.8f)));
            c->ability->set("blueprint_compat_ui", Value(" " + localize("k_" + bc.to_string()).to_string() + " "));
            c->ability->set("blueprint_compat_check", bc);
        }
    };
}

// ------------------------------------------------------------------ G.UIDEF.card_focus_ui (controller)
namespace {
// Vitalatro: the controller's card buttons are 1.4x with large menus on (not in frame comparisons)
}  // namespace
double focus_button_scale() {
    static const bool plain = std::getenv("VL_NO_ZOOM") != nullptr;
    return !plain && G.SETTINGS->get(K("vl_large_menus")).truthy() ? 1.4 : 1;
}
namespace {

// every size in a UI definition (text scale, min/max sizes, padding, corner radius, button pip) times k
void scale_ui_tree(const Value &node, double k) {
    Table *n = node.table();
    if (!n) return;
    if (Table *cfg = n->get(K("config")).table()) {
        for (const char *key : {"scale", "minw", "minh", "maxw", "maxh", "padding", "w", "h", "r"}) {
            Value v = cfg->get(Value(intern(key)));
            if (v.is_num()) cfg->set(Value(intern(key)), v.num() * k);
        }
        if (Table *fa = cfg->get(K("focus_args")).table())
            if (fa->get(K("scale")).is_num()) fa->set("scale", fa->get(K("scale")).num() * k);
    }
    if (Table *nodes = n->get(K("nodes")).table())
        for (int i = 1; i <= nodes->len(); i++) scale_ui_tree(nodes->get(i), k);
}

UIBox *card_focus_button(Card *card, UIElement *parent, const std::string &type, const char *func, const char *button, double card_width,
                         bool buy_and_use = false) {
    Value contents;
    if (type == "sell") {
        contents = nd(UIT_C, vl::T({{"align", "cl"}}),
                      list({nd(UIT_R, vl::T({{"align", "cl"}, {"maxw", 1}}),
                               list({nd(UIT_T, vl::T({{"text", localize("b_sell")}, {"colour", uic("TEXT_LIGHT")}, {"scale", 0.4}, {"shadow", true}}))})),
                            nd(UIT_R, vl::T({{"align", "cl"}}),
                               list({nd(UIT_T, vl::T({{"text", localize("$")}, {"colour", colour("WHITE")}, {"scale", 0.4}, {"shadow", true}})),
                                     nd(UIT_T, vl::T({{"ref_table", Value((Object *)card)}, {"ref_value", "sell_cost_label"}, {"colour", colour("WHITE")},
                                                      {"scale", 0.55}, {"shadow", true}}))}))}));
    } else if (type == "buy" || type == "redeem" || type == "use" || type == "select") {
        const char *k = type == "buy" ? "b_buy" : type == "redeem" ? "b_redeem" : type == "use" ? "b_use" : "b_select";
        contents = nd(UIT_T, vl::T({{"text", localize(k)}, {"colour", colour("WHITE")}, {"scale", type == "select" ? 0.3 : 0.5}}));
    } else if (type == "buy_and_use") {
        contents = nd(UIT_C, vl::T({{"align", "cr"}}),
                      list({nd(UIT_R, vl::T({{"align", "cr"}, {"maxw", 1}}),
                               list({nd(UIT_T, vl::T({{"text", localize("b_buy")}, {"colour", uic("TEXT_LIGHT")}, {"scale", 0.4}, {"shadow", true}}))})),
                            nd(UIT_R, vl::T({{"align", "cr"}, {"maxw", 1}}),
                               list({nd(UIT_T, vl::T({{"text", localize("b_and_use")}, {"colour", colour("WHITE")}, {"scale", 0.3}, {"shadow", true}}))}))}));
    } else {
        contents = Value(new_table());
    }
    const bool sell = type == "sell";
    const char *pip = sell ? "leftshoulder" : type == "buy_and_use" ? "leftshoulder" : "rightshoulder";
    TableRef outer = vl::T({{"ref_table", Value((Object *)card)}, {"ref_parent", Value((Object *)parent)}, {"align", sell ? "cl" : "cr"},
                            {"colour", colour("BLACK")}, {"shadow", true}, {"r", 0.08}, {"func", func}, {"one_press", true}, {"button", button},
                            {"focus_args", Value(vl::T({{"type", "none"}}))}, {"hover", true}});
    if (type == "buy_and_use") outer->set("id", "buy_and_use");
    TableRef inner = vl::T({{"align", sell ? "cl" : "cr"},
                            {"minw", 1 + (type == "select" ? 0.1 : 0)},
                            {"minh", sell ? 1.5 : 1},
                            {"padding", 0.08},
                            {"focus_args", Value(vl::T({{"button", pip},
                                                        {"scale", 0.55},
                                                        {"orientation", sell ? "tli" : "tri"},
                                                        {"offset", Value(vl::T({{"x", sell ? 0.1 : -0.1}, {"y", 0}}))},
                                                        {"type", "none"}}))},
                            {"func", "set_button_pip"}});
    Value spacer = nd(UIT_C, vl::T({{"align", "cm"}, {"minw", 0.2}, {"minh", 0.6}}), new_table());
    TableRef def = TableRef(
        nd(UIT_ROOT, vl::T({{"align", "cm"}, {"colour", colour("CLEAR")}}),
           list({nd(UIT_R, outer,
                    list({nd(UIT_R, inner,
                             list({nd(UIT_R, vl::T({{"align", "cm"}, {"minh", 0.3}}), new_table()),
                                   nd(UIT_R, vl::T({{"align", "cm"}}),
                                      list({sell ? Value() : spacer, nd(UIT_C, vl::T({{"align", "cm"}, {"maxw", 1}}), list({contents})),
                                            sell ? spacer : Value()}))}))}))}))
            .table());
    Transform t0;
    t0.x = card->VT.x;
    t0.y = card->VT.y;
    t0.w = t0.h = 0;
    const double ox = (sell ? -1 : 1) * (card_width - 0.17 - card->T->w / 2);
    const double k = focus_button_scale();
    const double oy = (type == "buy_and_use" ? 0.6 : buy_and_use ? -0.6 : 0) * k;
    // Vitalatro: with large menus on (not in frame comparisons), the button is built 1.4x larger;
    // it grows away from the card (aligned to the card's edge)
    if (k != 1) scale_ui_tree(Value(def), k);
    return new UIBox(def, vl::T({{"align", sell ? "cl" : "cr"}, {"offset", Value(vl::T({{"x", ox}, {"y", oy}}))}, {"parent", Value((Object *)parent)}}), &t0);
}
}  // namespace

UIBox *card_focus_ui(Card *card) {
    const bool cons = card->ability->get(K("consumeable")).truthy();
    const double card_width = card->T->w + (cons ? -0.1 : card->ability->get(K("set")).equals("Voucher") ? -0.16 : 0);
    const bool in_hand = G.hand && card->area == G.hand.get();
    TableRef playing_card_colour = copy_table(colour("WHITE")).table();
    playing_card_colour->set(4, 1.5);
    if (in_hand) {
        static const EventTimer real = EventTimer::REAL;
        ease_value(ValueRef::field(playing_card_colour, Value(4)), -1.5, false, &real, 0.2, EaseType::Quad);
    }
    const double tcnx = card->T->x + card->T->w / 2 - G.ROOM->T->w / 2, tcny = card->T->y + card->T->h / 2 - G.ROOM->T->h / 2;
    TableRef def;
    if (!in_hand)
        def = TableRef(nd(UIT_ROOT,
                          vl::T({{"align", "cm"}, {"minw", card_width + 0.3}, {"minh", card->T->h + 0.3}, {"r", 0.1},
                                 {"colour", Value(adjust_alpha(colour("BLACK"), 0.7f))}, {"outline_colour", Value(lighten(colour("JOKER_GREY"), 0.5f))},
                                 {"outline", 1.5}, {"line_emboss", 0.8}}),
                          list({nd(UIT_R, vl::T({{"id", "ATTACH_TO_ME"}}), new_table())}))
                           .table());
    else
        def = TableRef(nd(UIT_ROOT, vl::T({{"align", "cm"}, {"minw", card_width}, {"minh", card->T->h}, {"r", 0.1}, {"colour", Value(playing_card_colour)}}),
                          list({nd(UIT_R, vl::T({{"id", "ATTACH_TO_ME"}}), new_table())}))
                           .table());
    Transform t0;
    t0.x = card->VT.x;
    t0.y = card->VT.y;
    t0.w = t0.h = 0;
    UIBox *base = new UIBox(def,
                            vl::T({{"align", "cm"},
                                   {"offset", Value(vl::T({{"x", 0.007 * tcnx * card->T->w}, {"y", 0.007 * tcny * card->T->h}}))},
                                   {"parent", Value((Object *)card)},
                                   {"r_bond", in_hand ? "Strong" : "Weak"}}),
                            &t0);
    UIElement *attach = base->get_UIE_by_ID("ATTACH_TO_ME");
    auto add = [&](const char *child, const std::string &type, const char *func, const char *button, double w, bool bau = false) {
        attach->children.set(child, card_focus_button(card, attach, type, func, button, w, bau));
    };
    if (G.shop_jokers && card->area == G.shop_jokers.get()) {
        bool bau = false;
        if (cons) {
            add("buy_and_use", "buy_and_use", "buy_and_use_button_check", "buy_from_shop", card_width);
            bau = true;
        }
        add("buy", "buy", "buy_button_check", "buy_from_shop", card_width, bau);
    }
    if (G.shop_vouchers && card->area == G.shop_vouchers.get()) add("redeem", "buy", "can_redeem", "redeem_from_shop", card_width);
    if (G.shop_booster && card->area == G.shop_booster.get()) add("redeem", "buy", "can_open", "open_booster", card_width * 0.85);
    const bool in_cons = G.consumeables && card->area == G.consumeables.get(), in_pack = G.pack_cards && card->area == G.pack_cards.get();
    if ((in_cons || in_pack) && cons) add("use", "use", "can_use_consumeable", "use_card", card_width);
    if (in_pack && !cons) add("use", "select", "select_button_check", "use_card", card_width);
    if (((G.jokers && card->area == G.jokers.get()) || in_cons) && G.STATE != TUTORIAL) add("sell", "sell", "can_sell_card", "sell_card", card_width);
    return base;
}

// base_background.set_alignment, replaced in the original: only the offset follows the card
void realign_focused_ui(Card *card) {
    auto *fu = dynamic_cast<Moveable *>(card->children.get("focused_ui"));
    if (!fu) return;
    const double cnx = card->T->x + card->T->w / 2 - G.ROOM->T->w / 2, cny = card->T->y + card->T->h / 2 - G.ROOM->T->h / 2;
    AlignArgs al;
    XY off{(float)(0.007 * cnx * card->T->w), (float)(0.007 * cny * card->T->h)};
    al.offset = &off;
    fu->set_alignment(al);
}

}  // namespace vl
