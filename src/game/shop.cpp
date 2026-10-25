// The shop: G.UIDEF.shop, Game:update_shop, create_card_for_shop / create_shop_card_ui,
// change_shop_size, get_pack, the shop's G.FUNCS (buy, reroll, redeem, next round, use_card...),
// Card:redeem / Card:apply_to_run (vouchers), and the touch drag targets
// (create_drag_target_from_card / drag_target).
#include <algorithm>
#include <cmath>
#include <string>

#include "core/object.h"
#include "engine/controller.h"
#include "engine/event.h"
#include "engine/random.h"
#include "engine/text.h"
#include "engine/ui.h"
#include "game/blind.h"
#include "game/card.h"
#include "game/card_factory.h"
#include "game/card_ui.h"
#include "game/functions.h"
#include "game/g.h"
#include "game/shop.h"
#include "game/sound.h"
#include "game/state_events.h"
#include "game/tag.h"
#include "game/ui_definitions.h"

namespace vl {

namespace {
inline Value fld(const Value &t, const char *k) { return t.get(Value(intern(k))); }
inline Value gv(const char *k) { return G.GAME->get(Value(intern(k))); }
inline Table *gt(const char *k) { return G.GAME->get(Value(intern(k))).table(); }
Value colour(const char *n) { return G.colour(n); }
Value uic(const char *n) { return fld(colour("UI"), n); }
Value dyn(const char *n) { return fld(colour("DYN_UI"), n); }
Value obj(Object *o) { return Value(o); }
Value nd(int uit, TableRef cfg, TableRef nodes = nullptr) {
    TableRef t = vl::T({{"n", (double)uit}, {"config", Value(cfg)}});
    if (nodes) t->set("nodes", Value(nodes));
    return Value(t);
}
TableRef list(std::initializer_list<Value> items) {
    TableRef t = new_table();
    for (const Value &v : items)
        if (!v.is_nil()) t->push(v);
    return t;
}
UIBox *shop_box() { return dynamic_cast<UIBox *>(G.shop.get()); }
std::string ante_str() { return fld(gv("round_resets"), "ante").to_string(); }
double money_available() { return gv("dollars").num() - gv("bankrupt_at").num(); }
// tags: G.GAME.tags[i]:apply_to_run(args) for each, stopping at the first truthy answer when asked
Value tags_apply(const TableRef &args, bool stop_at_first) {
    Table *tags = gt("tags");
    for (int i = 1; tags && i <= tags->len(); i++)
        if (Tag *t = tags->get(i).as<Tag>()) {
            Value r = t->apply_to_run(args);
            if (r.truthy() && stop_at_first) return r;
        }
    return Value();
}
void all_set_cost() {
    G.I.CARD.each([](Moveable *v) {
        if (Card *c = dynamic_cast<Card *>(v)) c->set_cost();
    });
}
void remove_child(Node *n, const char *k) {
    if (Node *c = n->children.get(k)) {
        c->remove();
        n->children.set(k, nullptr);
    }
}
}  // namespace

// ------------------------------------------------------------------ cards for the shop
Value get_pack(const std::string &key, const std::string &type) {
    Value banned = gv("banned_keys");
    if (!gv("first_shop_buffoon").truthy() && !fld(banned, "p_buffoon_normal_1").truthy()) {
        G.GAME->set("first_shop_buffoon", true);
        return G.P_CENTERS->get(Value("p_buffoon_normal_" + std::to_string((int)lua_math.random(1, 2))));
    }
    Table *pool = G.P_CENTER_POOLS->get(K("Booster")).table();
    double cume = 0, it = 0;
    auto weight = [](const Value &v) { return fld(v, "weight").truthy() ? fld(v, "weight").num() : 1.0; };
    for (int i = 1; i <= pool->len(); i++) {
        Value v = pool->get(i);
        if ((type.empty() || fld(v, "kind").equals(type)) && !banned.get(fld(v, "key")).truthy()) cume += weight(v);
    }
    const double poll = pseudorandom(pseudoseed((key.empty() ? std::string("pack_generic") : key) + ante_str())) * cume;
    for (int i = 1; i <= pool->len(); i++) {
        Value v = pool->get(i);
        if (!banned.get(fld(v, "key")).truthy()) {
            if (type.empty() || fld(v, "kind").equals(type)) it += weight(v);
            if (it >= poll && it - weight(v) <= poll) return v;
        }
    }
    return Value();
}

void create_shop_card_ui(Card *card, const std::string &, CardArea *) {
    CardRef c(card);
    EventConfig e = ev_after(0.43, [c] {
        if (c->opening) return true;
        Card *card = c.get();
        Value self = obj(card);
        DynaText *price_text = new DynaText(vl::T({{"string", Value(A({Value(vl::T({{"prefix", localize("$")}, {"ref_table", self}, {"ref_value", "cost"}}))}))},
                                                   {"colours", Value(A({colour("MONEY")}))},
                                                   {"shadow", true},
                                                   {"silent", true},
                                                   {"bump", true},
                                                   {"pop_in", 0},
                                                   {"scale", 0.5}}));
        TableRef t1 = TableRef(nd(UIT_ROOT,
                                  vl::T({{"minw", 0.6}, {"align", "tm"}, {"colour", Value(darken(colour("BLACK"), 0.2f))}, {"shadow", true}, {"r", 0.05}, {"padding", 0.05},
                                         {"minh", 1}}),
                                  list({nd(UIT_R,
                                           vl::T({{"align", "cm"}, {"colour", Value(lighten(colour("BLACK"), 0.1f))}, {"r", 0.1}, {"minw", 1}, {"minh", 0.55}, {"emboss", 0.05},
                                                  {"padding", 0.03}}),
                                           list({nd(UIT_O, vl::T({{"object", obj(price_text)}}))}))}))
                                   .table());
        const Value set = card->ability->get(K("set"));
        auto button_root = [&](const char *colour_name, const char *func, const char *button, const char *text_key, double scale) {
            return TableRef(nd(UIT_ROOT,
                               vl::T({{"ref_table", self}, {"minw", 1.1}, {"maxw", 1.3}, {"padding", 0.1}, {"align", "bm"}, {"colour", colour(colour_name)}, {"shadow", true},
                                      {"r", 0.08}, {"minh", 0.94}, {"func", func}, {"one_press", true}, {"button", button}, {"hover", true}}),
                               list({nd(UIT_T, vl::T({{"text", localize(text_key)}, {"colour", colour("WHITE")}, {"scale", scale}}))}))
                                .table());
        };
        TableRef t2 = set.equals("Voucher")   ? button_root("GREEN", "can_redeem", "redeem_from_shop", "b_redeem", 0.4)
                      : set.equals("Booster") ? button_root("GREEN", "can_open", "open_booster", "b_open", 0.5)
                                              : button_root("GOLD", "buy_button_check", "buy_from_shop", "b_buy", 0.5);
        TableRef t3 = TableRef(
            nd(UIT_ROOT,
               vl::T({{"id", "buy_and_use"}, {"ref_table", self}, {"minh", 1.1}, {"padding", 0.1}, {"align", "cr"}, {"colour", colour("RED")}, {"shadow", true}, {"r", 0.08},
                      {"minw", 1.1}, {"func", "buy_and_use_button_check"}, {"one_press", true}, {"button", "buy_from_shop"}, {"hover", true},
                      {"focus_args", Value(vl::T({{"type", "none"}}))}}),
               list({nd(UIT_B, vl::T({{"w", 0.1}, {"h", 0.6}})),
                     nd(UIT_C, vl::T({{"align", "cm"}}),
                        list({nd(UIT_R, vl::T({{"align", "cm"}, {"maxw", 1}}), list({nd(UIT_T, vl::T({{"text", localize("b_buy")}, {"colour", colour("WHITE")}, {"scale", 0.5}}))})),
                              nd(UIT_R, vl::T({{"align", "cm"}, {"maxw", 1}}),
                                 list({nd(UIT_T, vl::T({{"text", localize("b_and_use")}, {"colour", colour("WHITE")}, {"scale", 0.3}}))}))}))}))
                .table());
        auto box = [&](TableRef def, const char *align, double ox, double oy) {
            return new UIBox(def, vl::T({{"align", align}, {"offset", Value(vl::T({{"x", ox}, {"y", oy}}))}, {"major", self}, {"bond", "Weak"}, {"parent", self}}));
        };
        UIBox *price = box(t1, "tm", 0, 1.5);
        card->children.set("price", price);
        card->children.set("buy_button", box(t2, "bm", 0, -0.3));
        if (card->ability->get(K("consumeable")).truthy()) card->children.set("buy_and_use_button", box(t3, "cr", -0.3, 0));
        price->alignment.offset.y = set.equals("Booster") ? 0.5f : 0.38f;
        return true;
    });
    e.blocking = false;
    e.blockable = false;
    G.E_MANAGER->add_event(e);
}

Card *create_card_for_shop(CardArea *area) {
    Value tp = G.SETTINGS->get(K("tutorial_progress"));
    Value forced = fld(tp, "forced_shop");
    if (area == G.shop_jokers.get() && tp.truthy() && forced.truthy() && forced.table()->len() > 0) {
        Table *t = forced.table();
        Value center = G.P_CENTERS->get(t->get(t->len()));
        if (!center.truthy()) center = G.P_CENTERS->get(K("c_empress"));
        Card *card = new Card(area->T->x + area->T->w / 2, area->T->y, G.CARD_W, G.CARD_H, G.P_CARDS->get(K("empty")), center,
                              vl::T({{"bypass_discovery_center", true}, {"bypass_discovery_ui", true}}));
        t->set(t->len(), Value());
        if (t->len() == 0) tp.table()->set("forced_shop", Value());
        create_shop_card_ui(card);
        return card;
    }
    Table *tags = gt("tags");
    for (int i = 1; tags && i <= tags->len(); i++) {
        Tag *t = tags->get(i).as<Tag>();
        Value forced_tag = t ? t->apply_to_run(vl::T({{"type", "store_joker_create"}, {"area", obj(area)}})) : Value();
        if (forced_tag.truthy()) {
            tags_apply(vl::T({{"type", "store_joker_modify"}, {"card", forced_tag}}), true);
            return forced_tag.as<Card>();
        }
    }
    if (!gv("spectral_rate").truthy()) G.GAME->set("spectral_rate", 0);
    const double total_rate = gv("joker_rate").num() + gv("tarot_rate").num() + gv("planet_rate").num() + gv("playing_card_rate").num() + gv("spectral_rate").num();
    const double polled_rate = pseudorandom(pseudoseed("cdt" + ante_str())) * total_rate;
    double check_rate = 0;
    const bool illusion = fld(gv("used_vouchers"), "v_illusion").truthy();
    struct Rate {
        std::string type;
        double val;
    };
    // (the playing card entry's type is decided while the list is built, as in the original)
    std::vector<Rate> rates = {{"Joker", gv("joker_rate").num()}, {"Tarot", gv("tarot_rate").num()}, {"Planet", gv("planet_rate").num()}};
    rates.push_back({(illusion && pseudorandom(pseudoseed("illusion")) > 0.6) ? "Enhanced" : "Base", gv("playing_card_rate").num()});
    rates.push_back({"Spectral", gv("spectral_rate").num()});
    for (const Rate &v : rates) {
        if (polled_rate > check_rate && polled_rate <= check_rate + v.val) {
            Card *card = create_card(v.type, area, false, Value(), false, false, "", "sho");
            create_shop_card_ui(card, v.type, area);
            CardRef c(card);
            G.E_MANAGER->add_event(ev_immediate([c] {
                tags_apply(vl::T({{"type", "store_joker_modify"}, {"card", obj(c.get())}}), true);
                return true;
            }));
            if ((v.type == "Base" || v.type == "Enhanced") && illusion && pseudorandom(pseudoseed("illusion")) > 0.8) {
                const double edition_poll = pseudorandom(pseudoseed("illusion"));
                const char *ed = edition_poll > 1 - 0.15 ? "polychrome" : edition_poll > 0.5 ? "holo" : "foil";
                card->set_edition(Value(vl::T({{ed, true}})));
            }
            return card;
        }
        check_rate += v.val;
    }
    return nullptr;
}

void change_shop_size(int mod) {
    Value shop = gv("shop");
    if (!shop.truthy()) return;
    shop.table()->set("joker_max", fld(shop, "joker_max").num() + mod);
    const int joker_max = (int)fld(shop, "joker_max").num();
    if (G.shop_jokers) {
        if (mod < 0)
            for (int i = (int)G.shop_jokers->cards.size(); i >= joker_max + 1; i--)
                if (i <= (int)G.shop_jokers->cards.size()) G.shop_jokers->cards[i - 1]->remove();
        G.shop_jokers->config->set("card_limit", joker_max);
        G.shop_jokers->T->w = joker_max * 1.01f * G.CARD_W;
        if (UIBox *s = shop_box()) s->recalculate();
        if (mod > 0)
            for (int i = 1; i <= joker_max - (int)G.shop_jokers->cards.size(); i++) G.shop_jokers->emplace(create_card_for_shop(G.shop_jokers.get()));
    }
}

// ------------------------------------------------------------------ G.UIDEF.shop
TableRef shop_definition() {
    const double joker_max = fld(gv("shop"), "joker_max").num();
    G.shop_jokers = new CardArea(G.hand->T->x, G.hand->T->y + G.ROOM->T->y + 9, (float)(joker_max * 1.02 * G.CARD_W), 1.05f * G.CARD_H,
                                 vl::T({{"card_limit", joker_max}, {"type", "shop"}, {"highlight_limit", 1}}));
    G.shop_vouchers = new CardArea(G.hand->T->x, G.hand->T->y + G.ROOM->T->y + 9, 2.1f * G.CARD_W, 1.05f * G.CARD_H,
                                   vl::T({{"card_limit", 1}, {"type", "shop"}, {"highlight_limit", 1}}));
    G.shop_booster = new CardArea(G.hand->T->x, G.hand->T->y + G.ROOM->T->y + 9, 2.4f * G.CARD_W, 1.15f * G.CARD_H,
                                  vl::T({{"card_limit", 2}, {"type", "shop"}, {"highlight_limit", 1}, {"card_w", 1.27 * G.CARD_W}}));

    AnimatedSprite *shop_sign = new AnimatedSprite(0, 0, 4.4f, 2.2f, &G.ANIMATION_ATLAS["shop_sign"], SpritePos{});
    DrawStep a, b;
    a.has_shadow_height = true;
    a.shadow_height = 0.05f;
    shop_sign->define_draw_steps({a, b});
    DynaText *improve = new DynaText(vl::T({{"string", Value(A({localize("ph_improve_run")}))},
                                            {"colours", Value(A({Value(lighten(colour("GOLD"), 0.3f))}))},
                                            {"shadow", true},
                                            {"rotate", true},
                                            {"float", true},
                                            {"bump", true},
                                            {"scale", 0.5},
                                            {"spacing", 1},
                                            {"pop_in", 1.5},
                                            {"maxw", 4.3}}));
    UIBox *hud = dynamic_cast<UIBox *>(G.HUD.get());
    G.SHOP_SIGN = new UIBox(TableRef(nd(UIT_ROOT, vl::T({{"colour", dyn("MAIN")}, {"emboss", 0.05}, {"align", "cm"}, {"r", 0.1}, {"padding", 0.1}}),
                                        list({nd(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.1}, {"minw", 4.72}, {"minh", 3.1}, {"colour", dyn("DARK")}, {"r", 0.1}}),
                                                 list({nd(UIT_R, vl::T({{"align", "cm"}}), list({nd(UIT_O, vl::T({{"object", obj(shop_sign)}}))})),
                                                       nd(UIT_R, vl::T({{"align", "cm"}}), list({nd(UIT_O, vl::T({{"object", obj(improve)}}))}))}))}))
                                         .table()),
                            vl::T({{"align", "cm"},
                                   {"offset", Value(vl::T({{"x", 0}, {"y", -15}}))},
                                   {"major", obj(hud ? hud->get_UIE_by_ID("row_blind") : nullptr)},
                                   {"bond", "Weak"}}));
    G.E_MANAGER->add_event(ev_immediate([] {
        if (auto *s = dynamic_cast<Moveable *>(G.SHOP_SIGN.get())) s->alignment.offset.y = 0;
        return true;
    }));

    auto text = [](const Value &t, double scale) { return nd(UIT_T, vl::T({{"text", t}, {"scale", scale}, {"colour", G.colour("WHITE")}, {"shadow", true}})); };
    Value next_round = nd(
        UIT_R,
        vl::T({{"id", "next_round_button"}, {"align", "cm"}, {"minw", 2.8}, {"minh", 1.5}, {"r", 0.15}, {"colour", colour("RED")}, {"one_press", true}, {"button", "toggle_shop"},
               {"hover", true}, {"shadow", true}}),
        list({nd(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.07}, {"focus_args", Value(vl::T({{"button", "y"}, {"orientation", "cr"}}))}, {"func", "set_button_pip"}}),
                 list({nd(UIT_R, vl::T({{"align", "cm"}, {"maxw", 1.3}}), list({text(localize("b_next_round_1"), 0.4)})),
                       nd(UIT_R, vl::T({{"align", "cm"}, {"maxw", 1.3}}), list({text(localize("b_next_round_2"), 0.4)}))}))}));
    Value reroll = nd(
        UIT_R,
        vl::T({{"align", "cm"}, {"minw", 2.8}, {"minh", 1.6}, {"r", 0.15}, {"colour", colour("GREEN")}, {"button", "reroll_shop"}, {"func", "can_reroll"}, {"hover", true},
               {"shadow", true}}),
        list({nd(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.07}, {"focus_args", Value(vl::T({{"button", "x"}, {"orientation", "cr"}}))}, {"func", "set_button_pip"}}),
                 list({nd(UIT_R, vl::T({{"align", "cm"}, {"maxw", 1.3}}), list({text(localize("k_reroll"), 0.4)})),
                       nd(UIT_R, vl::T({{"align", "cm"}, {"maxw", 1.3}, {"minw", 1}}),
                          list({text(localize("$"), 0.7),
                                nd(UIT_T, vl::T({{"ref_table", gv("current_round")}, {"ref_value", "reroll_cost"}, {"scale", 0.75}, {"colour", colour("WHITE")},
                                                 {"shadow", true}}))}))}))}));
    TableRef inner = list(
        {nd(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.1}, {"emboss", 0.05}, {"r", 0.1}, {"colour", dyn("BOSS_MAIN")}}),
            list({nd(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.05}}),
                     list({nd(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.1}}), list({next_round, reroll})),
                           nd(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.2}, {"r", 0.2}, {"colour", colour("L_BLACK")}, {"emboss", 0.05}, {"minw", 8.2}}),
                              list({nd(UIT_O, vl::T({{"object", obj(G.shop_jokers.get())}}))}))})),
                  nd(UIT_R, vl::T({{"align", "cm"}, {"minh", 0.2}}), new_table()),
                  nd(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.1}}),
                     list({nd(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.15}, {"r", 0.2}, {"colour", colour("L_BLACK")}, {"emboss", 0.05}}),
                              list({nd(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.2}, {"r", 0.2}, {"colour", colour("BLACK")}, {"maxh", G.shop_vouchers->T->h + 0.4}}),
                                       list({nd(UIT_T, vl::T({{"text", localize(vl::T({{"type", "variable"}, {"key", "ante_x_voucher"},
                                                                                      {"vars", Value(A({fld(gv("round_resets"), "ante")}))}}))},
                                                              {"scale", 0.45},
                                                              {"colour", colour("L_BLACK")},
                                                              {"vert", true}})),
                                             nd(UIT_O, vl::T({{"object", obj(G.shop_vouchers.get())}}))}))})),
                           nd(UIT_C, vl::T({{"align", "cm"}, {"padding", 0.15}, {"r", 0.2}, {"colour", colour("L_BLACK")}, {"emboss", 0.05}}),
                              list({nd(UIT_O, vl::T({{"object", obj(G.shop_booster.get())}}))}))}))}))});
    return TableRef(nd(UIT_ROOT, vl::T({{"align", "cl"}, {"colour", colour("CLEAR")}}), list({Value(UIBox_dyn_container(inner, false))})).table());
}

// ------------------------------------------------------------------ Game:update_shop
void shop_update() {
    if (!G.STATE_COMPLETE) {
        stop_use();
        ease_background_colour_blind(SHOP);
        const bool shop_exists = (bool)G.shop;
        if (!G.shop)
            G.shop = new UIBox(shop_definition(), vl::T({{"align", "tmi"}, {"offset", Value(vl::T({{"x", 0}, {"y", G.ROOM->T->y + 11}}))},
                                                         {"major", obj(G.hand.get())}, {"bond", "Weak"}}));
        G.E_MANAGER->add_event(ev_immediate([shop_exists] {
            UIBox *s = shop_box();
            s->alignment.offset.y = -5.3f;
            s->alignment.offset.x = 0;
            EventConfig e = ev_after(0.2, [shop_exists] {
                UIBox *s = shop_box();
                if (!s || std::fabs(s->T->y - s->VT.y) >= 3) return false;
                G.ROOM_jiggle += 3;
                play_sound("cardFan2");
                tags_apply(vl::T({{"type", "shop_start"}}), false);
                bool nosave_shop = false;
                if (!shop_exists) {
                    // a continued run's shop (G.load_shop_jokers / vouchers / booster, kept by start_run)
                    auto saved = [](const char *k) { return G.G_TABLE->get(Value(intern(k))); };
                    if (saved("load_shop_jokers").truthy()) {
                        nosave_shop = true;
                        G.shop_jokers->load(TableRef(saved("load_shop_jokers").table()));
                        for (auto &v : std::vector<CardRef>(G.shop_jokers->cards)) {
                            create_shop_card_ui(v.get());
                            if (v->ability->get(K("consumeable")).truthy()) v->start_materialize();
                            tags_apply(vl::T({{"type", "store_joker_modify"}, {"card", Value((Object *)v.get())}}), true);
                        }
                        G.G_TABLE->set("load_shop_jokers", Value());
                    } else {
                        const int joker_max = (int)fld(gv("shop"), "joker_max").num();
                        for (int i = 1, n = joker_max - (int)G.shop_jokers->cards.size(); i <= n; i++) G.shop_jokers->emplace(create_card_for_shop(G.shop_jokers.get()));
                    }
                    Value cr = gv("current_round");
                    if (saved("load_shop_vouchers").truthy()) {
                        nosave_shop = true;
                        G.shop_vouchers->load(TableRef(saved("load_shop_vouchers").table()));
                        for (auto &v : std::vector<CardRef>(G.shop_vouchers->cards)) {
                            create_shop_card_ui(v.get());
                            v->start_materialize();
                        }
                        G.G_TABLE->set("load_shop_vouchers", Value());
                    } else {
                        Value voucher = fld(cr, "voucher");
                        if (voucher.truthy() && G.P_CENTERS->get(voucher).truthy()) {
                            Card *card = new Card(G.shop_vouchers->T->x + G.shop_vouchers->T->w / 2, G.shop_vouchers->T->y, G.CARD_W, G.CARD_H,
                                                  G.P_CARDS->get(K("empty")), G.P_CENTERS->get(voucher),
                                                  vl::T({{"bypass_discovery_center", true}, {"bypass_discovery_ui", true}}));
                            card->shop_voucher = true;
                            create_shop_card_ui(card, "Voucher", G.shop_vouchers.get());
                            card->start_materialize();
                            G.shop_vouchers->emplace(card);
                        }
                    }
                    if (saved("load_shop_booster").truthy()) {
                        nosave_shop = true;
                        G.shop_booster->load(TableRef(saved("load_shop_booster").table()));
                        for (auto &v : std::vector<CardRef>(G.shop_booster->cards)) {
                            create_shop_card_ui(v.get());
                            v->start_materialize();
                        }
                        G.G_TABLE->set("load_shop_booster", Value());
                    } else {
                        if (!fld(cr, "used_packs").truthy()) cr.table()->set("used_packs", Value(new_table()));
                        Table *used = fld(cr, "used_packs").table();
                        for (int i = 1; i <= 2; i++) {
                            if (!used->get(i).truthy()) used->set(i, fld(get_pack("shop_pack"), "key"));
                            if (!used->get(i).equals("USED")) {
                                Card *card = new Card(G.shop_booster->T->x + G.shop_booster->T->w / 2, G.shop_booster->T->y, G.CARD_W * 1.27f, G.CARD_H * 1.27f,
                                                      G.P_CARDS->get(K("empty")), G.P_CENTERS->get(used->get(i)),
                                                      vl::T({{"bypass_discovery_center", true}, {"bypass_discovery_ui", true}}));
                                create_shop_card_ui(card, "Booster", G.shop_booster.get());
                                card->ability->set("booster_pos", i);
                                card->start_materialize();
                                G.shop_booster->emplace(card);
                            }
                        }
                        tags_apply(vl::T({{"type", "voucher_add"}}), false);
                        tags_apply(vl::T({{"type", "shop_final_pass"}}), false);
                    }
                }
                if (UIBox *s2 = shop_box()) G.CONTROLLER->snap_to(s2->get_UIE_by_ID("next_round_button"));
                if (!nosave_shop)
                    G.E_MANAGER->add_event(ev_immediate([] {
                        save_run();
                        return true;
                    }));
                return true;
            });
            e.blockable = false;
            G.E_MANAGER->add_event(e);
            return true;
        }));
        G.STATE_COMPLETE = true;
    }
    if (G.buttons) {
        G.buttons->remove();
        G.buttons = nullptr;
    }
}

// ------------------------------------------------------------------ vouchers
void set_voucher_usage(Card *card) {
    Value ck = card->config->get(K("center_key"));
    if (ck.truthy() && card->ability->get(K("set")).equals("Voucher")) {
        Table *vu = fld(G.PROFILES->get(G.SETTINGS->get(K("profile"))), "voucher_usage").table();
        Value u = vu->get(ck);
        if (u.truthy()) u.table()->set("count", fld(u, "count").num() + 1);
        else vu->set(ck, Value(vl::T({{"count", 1}, {"order", fld(card->config->get(K("center")), "order")}})));
    }
    G.save_settings();
}

// Card:apply_to_run(center): a voucher's effect (self = the redeemed card, or nil with a center)
void card_apply_to_run(Card *self, const Value &center) {
    const Value name = center.truthy() ? fld(center, "name") : self ? self->ability->get(K("name")) : Value();
    const Value extra = center.truthy() ? fld(fld(center, "config"), "extra") : self ? self->ability->get(K("extra")) : Value();
    auto is = [&](const char *a, const char *b = nullptr) { return name.equals(a) || (b && name.equals(b)); };
    auto later = [](std::function<void()> f) {
        G.E_MANAGER->add_event(ev_immediate([f] {
            f();
            return true;
        }));
    };
    const double x = extra.is_num() ? extra.num() : 0;
    if (is("Overstock", "Overstock Plus")) later([] { change_shop_size(1); });
    if (is("Tarot Merchant", "Tarot Tycoon")) later([x] { G.GAME->set("tarot_rate", 4 * x); });
    if (is("Planet Merchant", "Planet Tycoon")) later([x] { G.GAME->set("planet_rate", 4 * x); });
    if (is("Hone", "Glow Up")) later([x] { G.GAME->set("edition_rate", x); });
    if (is("Magic Trick", "Illusion")) later([x] { G.GAME->set("playing_card_rate", x); });
    if (is("Crystal Ball")) later([] { G.consumeables->config->set("card_limit", G.consumeables->config->get(K("card_limit")).num() + 1); });
    if (is("Clearance Sale", "Liquidation"))
        later([x] {
            G.GAME->set("discount_percent", x);
            all_set_cost();
        });
    if (is("Reroll Surplus", "Reroll Glut")) {
        // (the original reads self.ability.extra here, even when applied from a center)
        const double sx = self ? self->ability->get(K("extra")).num() : x;
        later([sx] {
            Table *rr = gt("round_resets"), *cr = gt("current_round");
            rr->set("reroll_cost", rr->get(K("reroll_cost")).num() - sx);
            cr->set("reroll_cost", std::max(0.0, cr->get(K("reroll_cost")).num() - sx));
        });
    }
    if (is("Seed Money", "Money Tree")) later([x] { G.GAME->set("interest_cap", x); });
    if (is("Grabber", "Nacho Tong")) {
        Table *rr = gt("round_resets");
        rr->set("hands", rr->get(K("hands")).num() + x);
        ease_hands_played(x);
    }
    if (is("Paint Brush", "Palette")) G.hand->change_size(1);
    if (is("Wasteful", "Recyclomancy")) {
        Table *rr = gt("round_resets");
        rr->set("discards", rr->get(K("discards")).num() + x);
        ease_discard(x);
    }
    if (is("Blank")) check_for_unlock(vl::T({{"type", "blank_redeems"}}));
    if (is("Antimatter"))
        later([] {
            if (G.jokers) G.jokers->config->set("card_limit", G.jokers->config->get(K("card_limit")).num() + 1);
        });
    if (is("Hieroglyph", "Petroglyph")) {
        ease_ante(-x);
        Table *rr = gt("round_resets");
        if (!rr->get(K("blind_ante")).truthy()) rr->set("blind_ante", rr->get(K("ante")));
        rr->set("blind_ante", rr->get(K("blind_ante")).num() - x);
        if (is("Hieroglyph")) {
            rr->set("hands", rr->get(K("hands")).num() - x);
            ease_hands_played(-x);
        }
        if (is("Petroglyph")) {
            rr->set("discards", rr->get(K("discards")).num() - x);
            ease_discard(-x);
        }
    }
}

void Card::redeem() {
    if (!ability->get(K("set")).equals("Voucher")) return;
    stop_use();
    const Value center = config->get(K("center"));
    if (!fld(center, "discovered").truthy()) discover_card(center);
    if (shop_voucher) gt("current_round")->set("voucher", Value());
    states.hover.can = false;
    gt("used_vouchers")->set(config->get(K("center_key")), true);
    auto top = std::make_shared<Ref<DynaText>>(), bot = std::make_shared<Ref<DynaText>>();
    CardRef self(this);
    G.E_MANAGER->add_event(ev_after(0.4, [self, top, bot, center] {
        *top = new DynaText(vl::T({{"string", localize(vl::T({{"type", "name_text"}, {"set", fld(center, "set")}, {"key", fld(center, "key")}}))},
                                   {"colours", Value(A({G.colour("WHITE")}))},
                                   {"rotate", 1},
                                   {"shadow", true},
                                   {"bump", true},
                                   {"float", true},
                                   {"scale", 0.9},
                                   {"pop_in", 0.6 / G.SPEEDFACTOR},
                                   {"pop_in_rate", 1.5 * G.SPEEDFACTOR}}));
        *bot = new DynaText(vl::T({{"string", localize("k_redeemed_ex")},
                                   {"colours", Value(A({G.colour("WHITE")}))},
                                   {"rotate", 2},
                                   {"shadow", true},
                                   {"bump", true},
                                   {"float", true},
                                   {"scale", 0.9},
                                   {"pop_in", 1.4 / G.SPEEDFACTOR},
                                   {"pop_in_rate", 1.5 * G.SPEEDFACTOR},
                                   {"pitch_shift", 0.25}}));
        float r = 0.5f;
        self->juice_up_card(0.3f, &r);
        play_sound("card1");
        play_sound("coin1");
        auto disp = [&](DynaText *t, const char *align) {
            return new UIBox(TableRef(nd(UIT_ROOT, vl::T({{"align", "tm"}, {"r", 0.15}, {"colour", G.colour("CLEAR")}, {"padding", 0.15}}),
                                         list({nd(UIT_O, vl::T({{"object", obj(t)}}))}))
                                          .table()),
                             vl::T({{"align", align}, {"offset", Value(vl::T({{"x", 0}, {"y", 0}}))}, {"parent", obj(self.get())}}));
        };
        self->children.set("top_disp", disp(top->get(), "tm"));
        self->children.set("bot_disp", disp(bot->get(), "bm"));
        return true;
    }));
    ease_dollars(-cost);
    inc_career_stat("c_shop_dollars_spent", cost);
    inc_career_stat("c_vouchers_bought", 1);
    set_voucher_usage(this);
    check_for_unlock(vl::T({{"type", "run_redeem"}}));
    gt("current_round")->set("voucher", Value());
    card_apply_to_run(this, Value());
    delay(0.6);
    for (auto &j : std::vector<CardRef>(G.jokers->cards)) j->calculate_joker(vl::T({{"buying_card", true}, {"card", obj(this)}}));
    if (fld(gv("modifiers"), "inflation").truthy()) {
        G.GAME->set("inflation", gv("inflation").num() + 1);
        G.E_MANAGER->add_event(ev_immediate([] {
            all_set_cost();
            return true;
        }));
    }
    G.E_MANAGER->add_event(ev_after(2.6, [top, bot] {
        if (*top) (*top)->pop_out(4);
        if (*bot) (*bot)->pop_out(4);
        return true;
    }));
    G.E_MANAGER->add_event(ev_after(0.5, [self] {
        remove_child(self.get(), "top_disp");
        remove_child(self.get(), "bot_disp");
        return true;
    }));
}

bool Card::check_use() {
    if (ability->get(K("name")).equals("Ankh") && G.jokers->cards.size() >= G.jokers->config->get(K("card_limit")).num()) {
        alert_no_space(this, G.jokers.get());
        return true;
    }
    return false;
}

void alert_no_space(Card *card, CardArea *area) {
    G.CONTROLLER->locks["no_space"] = true;
    attention_text(vl::T({{"scale", 0.9}, {"text", localize("k_no_space_ex")}, {"hold", 0.9}, {"align", "cm"}, {"cover", obj(area)}, {"cover_padding", 0.1},
                          {"cover_colour", Value(adjust_alpha(G.colour("BLACK"), 0.7f))}}));
    float r = 0.2f;
    card->juice_up_card(0.3f, &r);
    for (auto &c : area->cards) c->juice_up_card(0.15f);
    EventConfig e = ev_after(0.06 * G.SETTINGS->get(K("GAMESPEED")).num(), [] {
        play_sound("tarot2", 0.76f, 0.4f);
        return true;
    });
    e.blockable = false;
    e.blocking = false;
    G.E_MANAGER->add_event(e);
    play_sound("tarot2", 1, 0.4f);
    EventConfig e2 = ev_after(0.5 * G.SETTINGS->get(K("GAMESPEED")).num(), [] {
        G.CONTROLLER->locks.erase("no_space");
        return true;
    });
    e2.blockable = false;
    e2.blocking = false;
    G.E_MANAGER->add_event(e2);
}

// ------------------------------------------------------------------ shop G.FUNCS
bool can_buy(Card *c) { return !(c->cost > money_available() && c->cost > 0); }
bool can_buy_and_use(Card *c) { return !((c->cost > money_available() && c->cost > 0) || !c->can_use_consumeable()); }

bool check_for_buy_space(Card *card) {
    const Value set = card->ability->get(K("set"));
    const double neg = card->edition && card->edition->get(K("negative")).truthy() ? 1 : 0;
    if (!set.equals("Voucher") && !set.equals("Enhanced") && !set.equals("Default") &&
        !(set.equals("Joker") && G.jokers->cards.size() < G.jokers->config->get(K("card_limit")).num() + neg) &&
        !(card->ability->get(K("consumeable")).truthy() && G.consumeables->cards.size() < G.consumeables->config->get(K("card_limit")).num() + neg)) {
        alert_no_space(card, card->ability->get(K("consumeable")).truthy() ? G.consumeables.get() : G.jokers.get());
        return false;
    }
    return true;
}

// G.FUNCS.buy_from_shop(e): e = {config = {ref_table = card, id = ...}}
void buy_from_shop(Card *c1, const std::string &id, UIElement *e) {
    if (!c1) return;
    if (id != "buy_and_use" && !check_for_buy_space(c1)) {
        if (e) e->disable_button = false;
        return;
    }
    CardRef c(c1);
    G.E_MANAGER->add_event(ev_after(0.1, [c, id, e] {
        Card *c1 = c.get();
        c1->area->remove_card(c1);
        c1->add_to_deck();
        remove_child(c1, "price");
        remove_child(c1, "buy_button");
        const Value set = c1->ability->get(K("set"));
        if (set.equals("Default") || set.equals("Enhanced")) {
            inc_career_stat("c_playing_cards_bought", 1);
            G.playing_card++;
            G.deck->emplace(c1);
            c1->playing_card = Value((double)G.playing_card);
            playing_card_joker_effects({c1});
            G.playing_cards.push_back(CardRef(c1));
        } else if (id != "buy_and_use") {
            if (c1->ability->get(K("consumeable")).truthy()) G.consumeables->emplace(c1);
            else G.jokers->emplace(c1);
            G.E_MANAGER->add_event(ev_immediate([c] {
                c->calculate_joker(vl::T({{"buying_card", true}, {"card", obj(c.get())}}));
                return true;
            }));
        }
        Table *cp = gt("round_scores")->get(K("cards_purchased")).table();
        cp->set("amt", cp->get(K("amt")).num() + 1);
        if (c1->ability->get(K("consumeable")).truthy()) {
            const Value cs = fld(c1->config->get(K("center")), "set");
            if (cs.equals("Planet")) inc_career_stat("c_planets_bought", 1);
            else if (cs.equals("Tarot")) inc_career_stat("c_tarots_bought", 1);
        } else if (set.equals("Joker")) {
            Table *cr = gt("current_round");
            cr->set("jokers_purchased", cr->get(K("jokers_purchased")).num() + 1);
        }
        for (auto &j : std::vector<CardRef>(G.jokers->cards)) j->calculate_joker(vl::T({{"buying_card", true}, {"card", obj(c1)}}));
        if (fld(gv("modifiers"), "inflation").truthy()) {
            G.GAME->set("inflation", gv("inflation").num() + 1);
            G.E_MANAGER->add_event(ev_immediate([] {
                all_set_cost();
                return true;
            }));
        }
        play_sound("card1");
        inc_career_stat("c_shop_dollars_spent", c1->cost);
        if (c1->cost != 0) ease_dollars(-c1->cost);
        G.CONTROLLER->save_cardarea_focus("jokers");
        G.CONTROLLER->recall_cardarea_focus("jokers");
        if (id == "buy_and_use") use_card(c1, e, true);
        return true;
    }));
}

void use_card(Card *card, UIElement *e, bool mute, bool nosave) {
    if (e) e->config->set("button", Value());
    CardArea *area = card->area;
    const int prev_state = G.STATE;
    bool dont_dissolve = false;
    double delay_fac = 1;
    if (card->check_use()) {
        G.E_MANAGER->add_event(ev_immediate([e] {
            if (e) {
                e->disable_button = false;
                e->config->set("button", "use_card");
            }
            return true;
        }));
        return;
    }
    if (card->ability->get(K("set")).equals("Booster") && !nosave && G.STATE == SHOP)
        save_with_action(vl::T({{"type", "use_card"}, {"card", (double)card->sort_id}}));
    G.TAROT_INTERRUPT = G.STATE;
    if (card->ability->get(K("set")).equals("Booster")) G.GAME->set("PACK_INTERRUPT", G.STATE);
    const int s = G.STATE;
    G.STATE = (s == TAROT_PACK || s == PLANET_PACK || s == SPECTRAL_PACK || s == STANDARD_PACK || s == BUFFOON_PACK) ? s : PLAY_TAROT;
    G.CONTROLLER->locks["use"] = true;
    auto park = [](Node *n, float y) {
        if (auto *m = dynamic_cast<Moveable *>(n); m && !m->alignment.has_py) {
            m->alignment.has_py = true;
            m->alignment.py = m->alignment.offset.y;
            m->alignment.offset.y = y;
        }
    };
    const bool cons = card->ability->get(K("consumeable")).truthy();
    if (G.booster_pack && (cons || !(gv("pack_choices").truthy() && gv("pack_choices").num() > 1))) park(G.booster_pack.get(), G.ROOM->T->y + 29);
    if (G.shop) park(G.shop.get(), G.ROOM->T->y + 29);
    if (G.blind_select) park(G.blind_select.get(), G.ROOM->T->y + 39);
    if (G.round_eval) park(G.round_eval.get(), G.ROOM->T->y + 29);
    remove_child(card, "use_button");
    remove_child(card, "sell_button");
    remove_child(card, "price");
    if (card->area) card->area->remove_card(card);
    const Value set = card->ability->get(K("set"));
    if (cons) {
        if (G.STATE == TAROT_PACK || G.STATE == PLANET_PACK || G.STATE == SPECTRAL_PACK) {
            card->T->x = G.hand->T->x + G.hand->T->w / 2 - card->T->w / 2;
            card->T->y = G.hand->T->y + G.hand->T->h / 2 - card->T->h / 2 - 0.5f;
            discover_card(card->config->get(K("center")));
        } else {
            draw_card(G.hand.get(), G.play.get(), 1, "up", true, card, 0.1, mute);
        }
        delay(0.2);
        card->use_consumeable(area);
        for (auto &j : std::vector<CardRef>(G.jokers->cards)) j->calculate_joker(vl::T({{"using_consumeable", true}, {"consumeable", obj(card)}}));
    } else if (set.equals("Enhanced") || set.equals("Default")) {
        G.playing_card++;
        G.deck->emplace(card);
        play_sound("card1", 0.8f, 0.6f);
        play_sound("generic1");
        card->playing_card = Value((double)G.playing_card);
        playing_card_joker_effects({card});
        card->add_to_deck();
        G.playing_cards.push_back(CardRef(card));
        dont_dissolve = true;
        delay_fac = 0.2;
    } else if (set.equals("Joker")) {
        card->add_to_deck();
        G.jokers->emplace(card);
        play_sound("card1", 0.8f, 0.6f);
        play_sound("generic1");
        dont_dissolve = true;
        delay_fac = 0.2;
    } else if (set.equals("Booster")) {
        delay(0.1);
        if (card->ability->get(K("booster_pos")).truthy()) fld(gv("current_round"), "used_packs").table()->set(card->ability->get(K("booster_pos")), "USED");
        draw_card(G.hand.get(), G.play.get(), 1, "up", true, card, 0.1, true);
        if (!card->from_tag) {
            Table *cp = gt("round_scores")->get(K("cards_purchased")).table();
            cp->set("amt", cp->get(K("amt")).num() + 1);
        }
        card->open();
    } else if (set.equals("Voucher")) {
        delay(0.1);
        draw_card(G.hand.get(), G.play.get(), 1, "up", true, card, 0.1, true);
        Table *cp = gt("round_scores")->get(K("cards_purchased")).table();
        cp->set("amt", cp->get(K("amt")).num() + 1);
        card->redeem();
    }
    if (set.equals("Booster")) {
        G.CONTROLLER->locks["use"] = false;
        G.TAROT_INTERRUPT = 0;
        return;
    }
    CardRef c(card);
    Ref<CardArea> ar(area);
    G.E_MANAGER->add_event(ev_after(0.2, [c, ar, prev_state, dont_dissolve, delay_fac] {
        if (!dont_dissolve) c->start_dissolve();
        G.E_MANAGER->add_event(ev_after(0.1, [c, ar, prev_state, delay_fac] {
            G.STATE = prev_state;
            G.TAROT_INTERRUPT = 0;
            G.CONTROLLER->locks["use"] = false;
            auto unpark = [](Node *n) {
                if (auto *m = dynamic_cast<Moveable *>(n); m && m->alignment.has_py) {
                    m->alignment.offset.y = m->alignment.py;
                    m->alignment.has_py = false;
                }
            };
            const bool pack_state = prev_state == TAROT_PACK || prev_state == PLANET_PACK || prev_state == SPECTRAL_PACK || prev_state == STANDARD_PACK ||
                                    prev_state == BUFFOON_PACK;
            if (pack_state && G.booster_pack) {
                if (ar && ar.get() == G.consumeables.get()) {
                    unpark(G.booster_pack.get());
                } else if (gv("pack_choices").truthy() && gv("pack_choices").num() > 1) {
                    unpark(G.booster_pack.get());
                    G.GAME->set("pack_choices", gv("pack_choices").num() - 1);
                } else {
                    G.CONTROLLER->interrupt.focus = true;
                    if (prev_state == TAROT_PACK) inc_career_stat("c_tarot_reading_used", 1);
                    if (prev_state == PLANET_PACK) inc_career_stat("c_planetarium_used", 1);
                    end_consumeable(delay_fac);
                }
            } else {
                if (G.shop) unpark(G.shop.get());
                if (G.blind_select) unpark(G.blind_select.get());
                if (G.round_eval) unpark(G.round_eval.get());
                if (ar && !ar->cards.empty()) {
                    const bool voucher = c->ability->get(K("set")).equals("Voucher");
                    G.E_MANAGER->add_event(ev_immediate([ar, voucher] {
                        G.E_MANAGER->add_event(ev_immediate([ar, voucher] {
                            G.CONTROLLER->interrupt.focus = false;
                            if (voucher) {
                                if (UIBox *s = shop_box()) G.CONTROLLER->snap_to(s->get_UIE_by_ID("next_round_button"));
                            } else if (ar) {
                                const char *n = ar.get() == G.jokers.get() ? "jokers" : ar.get() == G.consumeables.get() ? "consumeables"
                                                : ar.get() == G.shop_jokers.get() ? "shop_jokers" : ar.get() == G.pack_cards.get() ? "pack_cards" : "hand";
                                G.CONTROLLER->recall_cardarea_focus(n);
                            }
                            return true;
                        }));
                        return true;
                    }));
                }
            }
            return true;
        }));
        return true;
    }));
}

void register_shop_callbacks() {
    auto &F = G.FUNCS;
    auto ref_card = [](UIElement *e) { return e->config->get(K("ref_table")).as<Card>(); };
    auto inactive = [](UIElement *e) {
        e->config->set("colour", fld(G.colour("UI"), "BACKGROUND_INACTIVE"));
        e->config->set("button", Value());
    };
    F["buy_button_check"] = [ref_card, inactive](UIElement *e) {
        if (!can_buy(ref_card(e))) inactive(e);
        else {
            e->config->set("colour", G.colour("ORANGE"));
            e->config->set("button", "buy_from_shop");
        }
        if (Node *rp = e->config->get(K("ref_parent")).as<Node>())
            if (Node *bu = rp->children.get("buy_and_use"))
                if (e->box) e->box->alignment.offset.y = bu->states.visible ? (float)(-0.6 * focus_button_scale()) : 0;
    };
    F["buy_and_use_button_check"] = [ref_card, inactive](UIElement *e) {
        Card *c = ref_card(e);
        if (!can_buy_and_use(c)) {
            if (e->box) e->box->states.visible = false;
            inactive(e);
        } else {
            if (c->highlighted && e->box) e->box->states.visible = true;
            e->config->set("colour", fld(G.colour("SECONDARY_SET"), "Voucher"));
            e->config->set("button", "buy_from_shop");
        }
    };
    F["can_redeem"] = [ref_card, inactive](UIElement *e) {
        if (ref_card(e)->cost > money_available()) inactive(e);
        else {
            e->config->set("colour", G.colour("GREEN"));
            e->config->set("button", "use_card");
        }
    };
    F["can_open"] = [ref_card, inactive](UIElement *e) {
        Card *c = ref_card(e);
        if (c->cost > 0 && c->cost > money_available()) inactive(e);
        else {
            e->config->set("colour", G.colour("GREEN"));
            e->config->set("button", "use_card");
        }
    };
    F["can_reroll"] = [inactive](UIElement *e) {
        const double rc = fld(gv("current_round"), "reroll_cost").num();
        if (money_available() - rc < 0 && rc != 0) inactive(e);
        else {
            e->config->set("colour", G.colour("GREEN"));
            e->config->set("button", "reroll_shop");
        }
    };
    F["buy_from_shop"] = [ref_card](UIElement *e) {
        Value id = e->config->get(K("id"));
        buy_from_shop(ref_card(e), id.is_str() ? std::string(id.str()) : std::string(), e);
    };
    F["use_card"] = [ref_card](UIElement *e) { use_card(ref_card(e), e); };
    F["shop_voucher_empty"] = [](UIElement *e) {
        if (G.shop_vouchers && (!G.shop_vouchers->cards.empty() || fld(gv("current_round"), "voucher").truthy())) e->states.visible = false;
        else if (!G.SETTINGS->get(K("language")).equals("en-us")) e->states.visible = false;
        else e->states.visible = true;
    };
    F["toggle_shop"] = [](UIElement *) {
        stop_use();
        G.CONTROLLER->locks["toggle_shop"] = true;
        if (!G.shop) return;
        for (auto &j : std::vector<CardRef>(G.jokers->cards)) j->calculate_joker(vl::T({{"ending_shop", true}}));
        G.E_MANAGER->add_event(ev_immediate([] {
            if (UIBox *s = shop_box()) s->alignment.offset.y = G.ROOM->T->y + 29;
            if (auto *sg = dynamic_cast<Moveable *>(G.SHOP_SIGN.get())) sg->alignment.offset.y = -15;
            return true;
        }));
        G.E_MANAGER->add_event(ev_after(0.5, [] {
            if (G.shop) G.shop->remove();
            G.shop = nullptr;
            if (G.SHOP_SIGN) G.SHOP_SIGN->remove();
            G.SHOP_SIGN = nullptr;
            G.shop_jokers = nullptr;
            G.shop_vouchers = nullptr;
            G.shop_booster = nullptr;
            G.STATE_COMPLETE = false;
            G.STATE = BLIND_SELECT;
            G.CONTROLLER->locks.erase("toggle_shop");
            return true;
        }));
    };
    F["reroll_shop"] = [](UIElement *) {
        stop_use();
        G.CONTROLLER->locks["shop_reroll"] = true;
        if (G.CONTROLLER->save_cardarea_focus("shop_jokers")) G.CONTROLLER->interrupt.focus = true;
        const double rc = fld(gv("current_round"), "reroll_cost").num();
        if (rc > 0) {
            inc_career_stat("c_shop_dollars_spent", rc);
            inc_career_stat("c_shop_rerolls", 1);
            ease_dollars(-rc);
        }
        G.E_MANAGER->add_event(ev_immediate([] {
            Table *cr = gt("current_round");
            const bool final_free = cr->get(K("free_rerolls")).num() > 0;
            cr->set("free_rerolls", std::max(cr->get(K("free_rerolls")).num() - 1, 0.0));
            Table *tr = gt("round_scores")->get(K("times_rerolled")).table();
            tr->set("amt", tr->get(K("amt")).num() + 1);
            calculate_reroll_cost(final_free);
            for (int i = (int)G.shop_jokers->cards.size(); i >= 1; i--) {
                Card *c = G.shop_jokers->remove_card(G.shop_jokers->cards[i - 1].get());
                if (c) c->remove();
            }
            play_sound("coin2");
            play_sound("other1");
            const int joker_max = (int)fld(gv("shop"), "joker_max").num();
            for (int i = 1, n = joker_max - (int)G.shop_jokers->cards.size(); i <= n; i++) {
                Card *nc = create_card_for_shop(G.shop_jokers.get());
                G.shop_jokers->emplace(nc);
                nc->juice_up_card();
            }
            return true;
        }));
        G.E_MANAGER->add_event(ev_after(0.3, [] {
            G.E_MANAGER->add_event(ev_immediate([] {
                G.CONTROLLER->interrupt.focus = false;
                G.CONTROLLER->locks["shop_reroll"] = false;
                G.CONTROLLER->recall_cardarea_focus("shop_jokers");
                for (auto &j : std::vector<CardRef>(G.jokers->cards)) j->calculate_joker(vl::T({{"reroll_shop", true}}));
                return true;
            }));
            return true;
        }));
        G.E_MANAGER->add_event(ev_immediate([] {
            save_run();
            return true;
        }));
    };
    F["check_drag_target_active"] = [](UIElement *e) {
        Value args = e->config->get(K("args"));
        Func *check = fld(args, "active_check").as<Func>();
        const bool active = check && check->call(fld(args, "card")).truthy();
        Table *tc = fld(args, "text_colour").table();
        if (active) {
            if (!e->config->get(K("pulse_border")).truthy() || !fld(args, "init").truthy()) {
                e->config->set("pulse_border", true);
                e->config->set("colour", fld(args, "colour"));
                if (tc) tc->set(4, 1);
                e->config->set("release_func", fld(args, "release_func"));
            }
        } else if (e->config->get(K("pulse_border")).truthy() || !fld(args, "init").truthy()) {
            e->config->set("pulse_border", Value());
            e->config->set("colour", Value(adjust_alpha(G.colour("L_BLACK"), 0.9f)));
            if (tc) tc->set(4, 0.5);
            e->config->set("release_func", Value());
        }
        args.table()->set("init", true);
    };
}

// ------------------------------------------------------------------ touch drag targets
namespace {
void drag_target(Moveable *cover, const Value &colour_v, TableRef text, Card *card, std::function<bool(Card *)> active_check,
                 std::function<void(Card *)> release_func) {
    TableRef args = vl::T({{"text", Value(text)},
                           {"colour", copy_table(colour_v)},
                           {"card", obj(card)},
                           {"text_colour", copy_table(G.colour("WHITE"))},
                           {"active_check", Value(new Func([active_check](const Value &o) { return Value(active_check(o.as<Card>())); }))},
                           {"release_func", Value(new Func([release_func](const Value &o) {
                                release_func(o.as<Card>());
                                return Value();
                            }))}});
    const double drag_area_width = cover ? cover->T->w : 0.001;
    TableRef rows = new_table();
    for (int i = 1; i <= text->len(); i++) {
        DynaText *dt = new DynaText(vl::T({{"string", Value(A({text->get(i)}))},
                                           {"maxw", drag_area_width - 0.1},
                                           {"colours", Value(A({args->get(K("text_colour"))}))},
                                           {"float", true},
                                           {"shadow", true},
                                           {"silent", true},
                                           {"pop_in", 0},
                                           {"pop_in_rate", 6}}));
        rows->push(nd(UIT_R, vl::T({{"align", "cm"}, {"padding", 0.05}, {"maxw", drag_area_width - 0.1}}), list({nd(UIT_O, vl::T({{"object", obj(dt)}}))})));
    }
    Transform t0;
    t0.x = t0.y = t0.w = t0.h = 0;
    UIBox *DT = new UIBox(TableRef(nd(UIT_ROOT,
                                      vl::T({{"align", "cm"}, {"args", Value(args)}, {"can_collide", true}, {"hover", true}, {"release_func", args->get(K("release_func"))},
                                             {"func", "check_drag_target_active"}, {"minw", drag_area_width}, {"minh", cover ? cover->T->h : 0.001}, {"padding", 0.03},
                                             {"r", 0.1}, {"colour", G.colour("CLEAR")}}),
                                      rows)
                                       .table()),
                          vl::T({{"align", "tli"}, {"offset", Value(vl::T({{"x", 0}, {"y", 0}}))}, {"major", obj(cover)}}), &t0);
    DT->attention_text = true;
    Ref<UIBox> dref(DT);
    EventConfig e = ev_after(0, [dref] {
        if (!G.CONTROLLER->dragging.target && dref) {
            dref->remove();
            return true;
        }
        return false;
    });
    e.blockable = false;
    e.blocking = false;
    G.E_MANAGER->add_event(e);
}

TableRef loc_lines(const char *key) {
    Value v = localize(key);
    TableRef t = new_table();
    if (Table *l = v.table())
        for (int i = 1; i <= l->len(); i++) t->push(l->get(i));
    else
        t->push(v);
    return t;
}
}  // namespace

void create_drag_target_from_card(Card *card) {
    if (!card || G.STAGE != RUN) return;
    if (G.DRAG_TARGETS.empty()) {
        auto mk = [](float x, float y, float w, float h) {
            Moveable *m = new Moveable(x, y, w, h);
            return Ref<Moveable>(m);
        };
        G.DRAG_TARGETS["S_buy"] = mk(G.jokers->T->x, G.jokers->T->y - 0.1f, G.consumeables->T->x + G.consumeables->T->w - G.jokers->T->x, G.jokers->T->h + 0.6f);
        G.DRAG_TARGETS["S_buy_and_use"] = mk(G.deck->T->x + 0.2f, G.deck->T->y - 5.1f, G.deck->T->w - 0.1f, 4.5f);
        G.DRAG_TARGETS["C_sell"] = mk(G.jokers->T->x, G.jokers->T->y - 0.2f, G.jokers->T->w, G.jokers->T->h + 0.6f);
        G.DRAG_TARGETS["J_sell"] = mk(G.consumeables->T->x + 0.3f, G.consumeables->T->y - 0.2f, G.consumeables->T->w - 0.3f, G.consumeables->T->h + 0.6f);
        G.DRAG_TARGETS["C_use"] = mk(G.deck->T->x + 0.2f, G.deck->T->y - 5.1f, G.deck->T->w - 0.1f, 4.5f);
        G.DRAG_TARGETS["P_select"] = mk(G.play->T->x, G.play->T->y - 2, G.play->T->w + 2, G.play->T->h + 1);
    }
    CardArea *a = card->area;
    if (a && (a == G.shop_jokers.get() || a == G.shop_vouchers.get() || a == G.shop_booster.get())) {
        TableRef buy_loc = loc_lines(a == G.shop_vouchers.get() ? "ml_redeem_target" : a == G.shop_booster.get() ? "ml_open_target" : "ml_buy_target");
        buy_loc->push(Value("$" + Value(card->cost).to_string()));
        drag_target(G.DRAG_TARGETS["S_buy"].get(), Value(adjust_alpha(G.colour("GREEN"), 0.9f)), buy_loc, card, [](Card *o) { return can_buy(o); },
                    [](Card *o) {
                        if (o->area == G.shop_jokers.get() && can_buy(o)) buy_from_shop(o, "buy", nullptr);
                        else if ((o->area == G.shop_vouchers.get() || o->area == G.shop_booster.get()) && can_buy(o)) use_card(o, nullptr);
                    });
        if (can_buy_and_use(card)) {
            TableRef bu = loc_lines("ml_buy_and_use_target");
            bu->push(Value("$" + Value(card->cost).to_string()));
            drag_target(G.DRAG_TARGETS["S_buy_and_use"].get(), Value(adjust_alpha(G.colour("ORANGE"), 0.9f)), bu, card, [](Card *o) { return can_buy_and_use(o); },
                        [](Card *o) {
                            if (can_buy_and_use(o)) buy_from_shop(o, "buy_and_use", nullptr);
                        });
        }
    }
    if (a && a == G.pack_cards.get()) {
        if (card->ability->get(K("consumeable")).truthy() && !card->ability->get(K("set")).equals("Planet"))
            drag_target(G.DRAG_TARGETS["C_use"].get(), Value(adjust_alpha(G.colour("RED"), 0.9f)), A({localize("b_use")}), card,
                        [](Card *o) { return o->can_use_consumeable(); },
                        [](Card *o) {
                            if (o->can_use_consumeable()) use_card(o, nullptr);
                        });
        else
            drag_target(G.DRAG_TARGETS["P_select"].get(), Value(adjust_alpha(G.colour("GREEN"), 0.9f)), A({localize("b_select")}), card,
                        [](Card *o) { return can_select_card(o); },
                        [](Card *o) {
                            if (can_select_card(o)) use_card(o, nullptr);
                        });
    }
    if (a && (a == G.jokers.get() || a == G.consumeables.get())) {
        TableRef sell_loc = loc_lines("ml_sell_target");
        sell_loc->push(Value("$" + (card->facing == "back" ? std::string("?") : Value(card->sell_cost).to_string())));
        drag_target(a == G.consumeables.get() ? G.DRAG_TARGETS["C_sell"].get() : G.DRAG_TARGETS["J_sell"].get(), Value(adjust_alpha(G.colour("GOLD"), 0.9f)),
                    sell_loc, card, [](Card *o) { return o->can_sell_card(); },
                    [](Card *o) {
                        o->sell_card();
                        for (auto &j : std::vector<CardRef>(G.jokers->cards))
                            if (j.get() != o) j->calculate_joker(vl::T({{"selling_card", true}, {"card", obj(o)}}));
                    });
        if (a == G.consumeables.get())
            drag_target(G.DRAG_TARGETS["C_use"].get(), Value(adjust_alpha(G.colour("RED"), 0.9f)), A({localize("b_use")}), card,
                        [](Card *o) { return o->can_use_consumeable(); },
                        [](Card *o) {
                            if (o->can_use_consumeable()) use_card(o, nullptr);
                        });
    }
}

}  // namespace vl
