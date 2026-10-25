#include "game/card_factory.h"

#include <functional>

#include "engine/event.h"
#include "game/blind.h"
#include "game/functions.h"
#include "game/g.h"
#include "game/state_events.h"

namespace vl {

namespace {
inline Value fld(const Value &t, const char *k) { return t.get(Value(intern(k))); }
inline Value gv(const char *k) { return G.GAME->get(Value(intern(k))); }
std::string ante_str() { return fld(gv("round_resets"), "ante").to_string(); }
}  // namespace

Value poll_edition(const std::string &key, double mod, bool no_neg, bool guaranteed) {
    const double poll = pseudorandom(pseudoseed(key));
    if (guaranteed) {
        if (poll > 1 - 0.003 * 25 && !no_neg) return Value(vl::T({{"negative", true}}));
        if (poll > 1 - 0.006 * 25) return Value(vl::T({{"polychrome", true}}));
        if (poll > 1 - 0.02 * 25) return Value(vl::T({{"holo", true}}));
        if (poll > 1 - 0.04 * 25) return Value(vl::T({{"foil", true}}));
    } else {
        const double rate = gv("edition_rate").num();
        if (poll > 1 - 0.003 * mod && !no_neg) return Value(vl::T({{"negative", true}}));
        if (poll > 1 - 0.006 * rate * mod) return Value(vl::T({{"polychrome", true}}));
        if (poll > 1 - 0.02 * rate * mod) return Value(vl::T({{"holo", true}}));
        if (poll > 1 - 0.04 * rate * mod) return Value(vl::T({{"foil", true}}));
    }
    return Value();
}

Card *create_card(const std::string &type_in, CardArea *area, bool legendary, const Value &rarity, bool skip_materialize, bool soulable,
                  const std::string &forced_key_in, const std::string &key_append) {
    std::string type = type_in, forced_key = forced_key_in;
    if (!area) area = G.jokers.get();
    Value center = G.P_CENTERS->get(K("b_red"));
    // (G.FTP_LOCKED is never set)
    Value banned = gv("banned_keys"), used = gv("used_jokers");
    if (forced_key.empty() && soulable && !fld(banned, "c_soul").truthy()) {
        const bool showman = !find_joker("Showman").empty();
        if ((type == "Tarot" || type == "Spectral" || type == "Tarot_Planet") && !(fld(used, "c_soul").truthy() && !showman))
            if (pseudorandom("soul_" + type + ante_str()) > 0.997) forced_key = "c_soul";
        if ((type == "Planet" || type == "Spectral") && !(fld(used, "c_black_hole").truthy() && !showman))
            if (pseudorandom("soul_" + type + ante_str()) > 0.997) forced_key = "c_black_hole";
    }
    if (type == "Base") forced_key = "c_base";

    if (!forced_key.empty() && !banned.get(Value(forced_key)).truthy()) {
        center = G.P_CENTERS->get(Value(forced_key));
        if (!fld(center, "set").equals("Default")) type = std::string(fld(center, "set").str());
    } else {
        auto [pool, pool_key] = get_current_pool(type.c_str(), rarity, legendary, key_append);
        double seed = pseudoseed(pool_key);
        Value key = pseudorandom_element(Value(pool), &seed);
        int it = 1;
        while (key.equals("UNAVAILABLE")) {
            it++;
            seed = pseudoseed(pool_key + "_resample" + std::to_string(it));
            key = pseudorandom_element(Value(pool), &seed);
        }
        center = G.P_CENTERS->get(key);
    }

    Value front;
    if (type == "Base" || type == "Enhanced") {
        double seed = pseudoseed("front" + key_append + ante_str());
        front = pseudorandom_element(Value(G.P_CARDS), &seed);
    }
    const bool shopish = area == G.shop_jokers.get() || area == G.pack_cards.get() || area == G.shop_vouchers.get();
    const bool owned = area == G.jokers.get() || area == G.consumeables.get();
    Back *back = gv("selected_back").as<Back>();
    TableRef params = vl::T({{"bypass_discovery_center", shopish || owned}, {"bypass_discovery_ui", shopish}, {"discover", owned}});
    if (back && back->pos) params->set("bypass_back", Value(back->pos));
    Card *card = new Card(area->T->x + area->T->w / 2, area->T->y, G.CARD_W, G.CARD_H, front, center, params);
    if (card->ability->get(K("consumeable")).truthy() && !skip_materialize) card->start_materialize();

    if (type == "Joker") {
        Value mods = gv("modifiers");
        if (fld(mods, "all_eternal").truthy()) card->set_eternal(true);
        if (area == G.shop_jokers.get() || area == G.pack_cards.get()) {
            const bool pack = area == G.pack_cards.get();
            const double poll = pseudorandom(std::string(pack ? "packetper" : "etperpoll") + ante_str());
            if (fld(mods, "enable_eternals_in_shop").truthy() && poll > 0.7) card->set_eternal(true);
            else if (fld(mods, "enable_perishables_in_shop").truthy() && poll > 0.4 && poll <= 0.7) card->set_perishable(true);
            if (fld(mods, "enable_rentals_in_shop").truthy() && pseudorandom(std::string(pack ? "packssjr" : "ssjr") + ante_str()) > 0.7)
                card->set_rental(true);
        }
        card->set_edition(poll_edition("edi" + key_append + ante_str()));
        check_for_unlock(vl::T({{"type", "have_edition"}}));
    }
    return card;
}

Card *copy_card(Card *other, Card *new_card, double card_scale, const Value &playing_card, bool strip_edition) {
    if (!new_card)
        new_card = new Card(other->T->x, other->T->y, G.CARD_W * (float)card_scale, G.CARD_H * (float)card_scale, G.P_CARDS->get(K("empty")),
                            G.P_CENTERS->get(K("c_base")), vl::T({{"playing_card", playing_card}}));
    new_card->set_ability(other->config->get(K("center")));
    new_card->ability->set("type", other->ability->get(K("type")));
    new_card->set_base(other->config->get(K("card")));
    other->ability->each([&](const Value &k, const Value &v) {
        if (v.is_table()) new_card->ability->set(k, copy_table(v));
        else new_card->ability->set(k, v);
    });
    if (!strip_edition) new_card->set_edition(other->edition ? Value(other->edition) : Value(new_table()), false, true);
    check_for_unlock(vl::T({{"type", "have_edition"}}));
    new_card->set_seal(other->seal, true);
    if (other->params) {
        new_card->params = other->params;
        new_card->params->set("playing_card", playing_card);
    }
    new_card->debuff = other->debuff;
    new_card->pinned = other->pinned;
    return new_card;
}

Card *create_playing_card(Value front, Value center, CardArea *area, bool skip_materialize, bool silent, TableRef colours) {
    if (!front.truthy()) {
        double seed = pseudoseed("front");
        front = pseudorandom_element(Value(G.P_CARDS), &seed);
    }
    if (!center.truthy()) center = G.P_CENTERS->get(K("c_base"));
    G.playing_card++;
    CardArea *a = area ? area : G.hand.get();
    Card *card = new Card(a->T->x, a->T->y, G.CARD_W, G.CARD_H, front, center, vl::T({{"playing_card", (double)G.playing_card}}));
    G.playing_cards.push_back(CardRef(card));
    card->playing_card = Value((double)G.playing_card);
    if (area) area->emplace(card);
    if (!skip_materialize) card->start_materialize(colours, silent);
    return card;
}

void juice_card_until(Card *card, std::function<bool(Card *)> eval, bool first, double delay) {
    CardRef c(card);
    EventConfig e = ev_after(delay, [c, eval, first] {
        if (eval(c.get())) {
            float r = 0.1f;
            c->juice_up_card(0.1f, &r, true);  // (`not first or first`: always)
            juice_card_until(c.get(), eval, false, 0.8);
        }
        return true;
    });
    e.blocking = false;
    e.blockable = false;
    e.has_timer = true;
    e.timer = EventTimer::REAL;
    G.E_MANAGER->add_event(e);
}

void playing_card_joker_effects(const std::vector<Card *> &cards) {
    TableRef t = new_table();
    for (Card *c : cards) t->push(c ? Value((Object *)c) : Value(true));
    for (auto &j : std::vector<CardRef>(G.jokers->cards)) j->calculate_joker(vl::T({{"playing_card_added", true}, {"cards", Value(t)}}));
}

void add_jokers(const std::string &keys) {
    size_t pos = 0;
    while (pos < keys.size()) {
        size_t comma = keys.find(',', pos);
        std::string key = keys.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        pos = comma == std::string::npos ? keys.size() : comma + 1;
        if (key.empty() || !G.P_CENTERS->get(Value(key)).truthy()) continue;
        const Value center = G.P_CENTERS->get(Value(key));
        const bool consumable = center.get(K("consumeable")).truthy();
        CardArea *area = consumable ? G.consumeables.get() : G.jokers.get();
        Card *card = create_card(consumable ? center.get(K("set")).to_string() : "Joker", area, false, Value(), false, false, key);
        card->add_to_deck();
        area->emplace(card);
    }
}

void combo_setup(int hand_delta) {
    for (auto &c : G.playing_cards) {
        c->set_base(G.P_CARDS->get(K("S_K")));
        c->set_ability(G.P_CENTERS->get(K("m_steel")));
        c->set_seal(Value("Red"), true, true);
    }
    if (hand_delta) G.hand->change_size(hand_delta);
    add_jokers("j_mime,j_blueprint,j_baron,j_brainstorm,j_sock_and_buskin");
}

}  // namespace vl
