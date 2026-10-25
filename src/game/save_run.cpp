// Saving a run (misc_functions.lua save_run / recursive_table_cull) and the save/load methods of
// Card, CardArea, Blind and Back. The file is the original's: <profile>/save.jkr, a STR_PACKed
// table compressed with DEFLATE (save_manager.cpp writes it from G.ARGS.save_run).
#include "engine/text.h"
#include "engine/ui.h"
#include "game/blind.h"
#include "game/card.h"
#include "game/functions.h"
#include "game/g.h"
#include "game/state_events.h"
#include "game/tag.h"

namespace vl {

namespace {
inline Value fld(const Value &t, const char *k) { return t.get(Value(intern(k))); }
}  // namespace

// recursive_table_cull(t): a copy of t where every object becomes the string '"MANUAL_REPLACE"'
Value recursive_table_cull(const Value &t) {
    TableRef ret = new_table();
    if (Table *src = t.table())
        src->each([&](const Value &k, const Value &v) {
            if (v.is_table()) ret->set(k, recursive_table_cull(v));
            else if (v.obj()) ret->set(k, Value("\"MANUAL_REPLACE\""));
            else ret->set(k, v);
        });
    return Value(ret);
}

// ------------------------------------------------------------------ Card
TableRef Card::save() const {
    return vl::T({{"sort_id", (double)sort_id},
                  {"save_fields", Value(vl::T({{"center", config->get(K("center_key"))}, {"card", config->get(K("card_key"))}}))},
                  {"params", Value(params)},
                  {"no_ui", no_ui},
                  {"base_cost", base_cost},
                  {"extra_cost", extra_cost},
                  {"cost", cost},
                  {"sell_cost", sell_cost},
                  {"facing", Value(facing)},
                  {"sprite_facing", Value(facing)},
                  // (the original saves self.highligted, a typo: never saved)
                  {"debuff", debuff},
                  {"rank", rank ? Value((double)rank) : Value()},
                  {"added_to_deck", added_to_deck},
                  {"label", label},
                  {"playing_card", playing_card},
                  {"base", Value(base)},
                  {"ability", Value(ability)},
                  {"pinned", pinned},
                  {"edition", edition ? Value(edition) : Value()},
                  {"seal", seal},
                  {"bypass_discovery_center", bypass_discovery_center},
                  {"bypass_discovery_ui", bypass_discovery_ui},
                  {"bypass_lock", bypass_lock}});
}

void Card::load(const TableRef &t) {
    const Value sf = t->get(K("save_fields"));
    const Value center_key = fld(sf, "center");
    const Value center = G.P_CENTERS->get(center_key);
    config = vl::T({{"center_key", center_key}, {"center", center}});
    params = t->get(K("params")).is_table() ? TableRef(t->get(K("params")).table()) : new_table();
    sticker_run = Value();
    const float H = G.CARD_H, W = G.CARD_W;
    const std::string name = fld(center, "name").to_string();
    if (name == "Half Joker") T->h = H / 1.7f, T->w = W;
    else if (name == "Wee Joker") T->h = H * 0.7f, T->w = W * 0.7f;
    else if (name == "Photograph") T->h = H / 1.2f, T->w = W;
    else if (name == "Square Joker") T->h = W, T->w = W;
    else if (fld(center, "set").equals("Booster")) T->h = H * 1.27f, T->w = W * 1.27f;
    else T->h = H, T->w = W;
    VT.w = T->w;
    config->set("card_key", fld(sf, "card"));
    config->set("card", G.P_CARDS->get(fld(sf, "card")));
    no_ui = t->get(K("no_ui")).truthy();
    base_cost = t->get(K("base_cost")).num();
    extra_cost = t->get(K("extra_cost")).num();
    cost = t->get(K("cost")).num();
    sell_cost = t->get(K("sell_cost")).num();
    facing = t->get(K("facing")).truthy() ? t->get(K("facing")).to_string() : "front";
    sprite_facing = t->get(K("sprite_facing")).truthy() ? t->get(K("sprite_facing")).to_string() : "front";
    flipping.clear();
    highlighted = t->get(K("highlighted")).truthy();
    debuff = t->get(K("debuff")).truthy();
    rank = (int)t->get(K("rank")).num();
    added_to_deck = t->get(K("added_to_deck")).truthy();
    label = t->get(K("label"));
    playing_card = t->get(K("playing_card"));
    base = t->get(K("base")).is_table() ? TableRef(t->get(K("base")).table()) : new_table();
    sort_id = (int)t->get(K("sort_id")).num();
    bypass_discovery_center = t->get(K("bypass_discovery_center")).truthy();
    bypass_discovery_ui = t->get(K("bypass_discovery_ui")).truthy();
    bypass_lock = t->get(K("bypass_lock")).truthy();
    ability = t->get(K("ability")).is_table() ? TableRef(t->get(K("ability")).table()) : new_table();
    pinned = t->get(K("pinned")).truthy();
    edition = t->get(K("edition")).is_table() ? TableRef(t->get(K("edition")).table()) : TableRef();
    seal = t->get(K("seal"));
    remove_all(children);
    children.clear();
    children.set("shadow", new Moveable(0, 0, 0, 0));
    set_sprites(center, config->get(K("card")));
}

// ------------------------------------------------------------------ CardArea
TableRef CardArea::save() const {
    TableRef cs = new_table();
    for (auto &c : cards) cs->push(Value(c->save()));
    return vl::T({{"cards", Value(cs)}, {"config", Value(config)}});
}

void CardArea::load(const TableRef &t) {
    for (auto &c : std::vector<CardRef>(cards)) c->remove();
    cards.clear();
    highlighted.clear();
    remove_all(children);
    children.clear();
    config = t->get(K("config")).is_table() ? TableRef(t->get(K("config")).table()) : new_table();
    if (Table *cs = t->get(K("cards")).table())
        for (int i = 1; i <= cs->len(); i++) {
            Card *card = new Card(0, 0, G.CARD_W, G.CARD_H, G.P_CENTERS->get(K("j_joker")), G.P_CENTERS->get(K("c_base")));
            card->load(TableRef(cs->get(i).table()));
            cards.push_back(CardRef(card));
            if (card->highlighted) highlighted.push_back(CardRef(card));
            card->set_card_area(this);
        }
    set_ranks();
    align_cards();
    hard_set_cards();
}

// ------------------------------------------------------------------ Blind / Back
TableRef Blind::save() const {
    Value config_blind("");
    const std::string nm = name;
    G.P_BLINDS->each([&](const Value &k, const Value &v) {
        if (fld(v, "name").truthy() && fld(v, "name").equals(nm)) config_blind = k;
    });
    return vl::T({{"name", Value(name)},
                  {"dollars", dollars},
                  {"debuff", debuff ? Value(debuff) : Value()},
                  {"mult", mult},
                  {"disabled", disabled},
                  {"discards_sub", discards_sub},
                  {"hands_sub", hands_sub},
                  {"boss", boss},
                  {"config_blind", config_blind},
                  {"chips", chips},
                  {"chip_text", vars->get(K("chip_text"))},
                  {"hands", hands ? Value(hands) : Value()},
                  {"only_hand", only_hand},
                  {"triggered", triggered}});
}

void Blind::load(const TableRef &t) {
    const Value bl = G.P_BLINDS->get(t->get(K("config_blind")));
    config->set("blind", bl.truthy() ? bl : Value(new_table()));
    name = t->get(K("name")).to_string();
    has_name = t->get(K("name")).is_str();
    dollars = t->get(K("dollars")).num();
    debuff = t->get(K("debuff")).is_table() ? TableRef(t->get(K("debuff")).table()) : TableRef();
    mult = t->get(K("mult")).num();
    disabled = t->get(K("disabled")).truthy();
    discards_sub = t->get(K("discards_sub"));
    hands_sub = t->get(K("hands_sub"));
    boss = t->get(K("boss")).truthy();
    chips = t->get(K("chips")).num();
    vars->set("chip_text", t->get(K("chip_text")));
    hands = t->get(K("hands")).is_table() ? TableRef(t->get(K("hands")).table()) : TableRef();
    only_hand = t->get(K("only_hand"));
    triggered = t->get(K("triggered"));
    const Value cb = config->get(K("blind"));
    const Value bb = fld(cb, "boss");
    G.ARGS->get(K("spin")).table()->set("real", (G.reduced_motion() ? 0 : 1) * (bb.truthy() ? (fld(bb, "showdown").truthy() ? 1 : 0.3) : 0));
    AnimatedSprite *s = sprite();
    if (bl.truthy()) {
        blind_set = true;
        s->states.visible = true;
        SpritePos p;
        p.x = (float)fld(fld(cb, "pos"), "x").num();
        p.y = (float)fld(fld(cb, "pos"), "y").num();
        s->set_sprite_pos(p);
        float r = 0.2f;
        s->juice_up(0.3f, &r);
        align();
        s->hard_set_VT();
    } else {
        s->states.visible = false;
    }
    s->states = states;
    change_colour(Value());
    if (dollars > 0) {
        std::string dl;
        for (int i = 0; i < (int)dollars; i++) dl += localize("$").to_string();
        G.GAME->get(K("current_round")).table()->set("dollars_to_be_earned", Value(dl));
        if (auto *hb = dynamic_cast<UIBox *>(G.HUD_blind.get())) {
            if (UIElement *de = hb->get_UIE_by_ID("dollars_to_be_earned"))
                if (auto *dt = dynamic_cast<DynaText *>(de->config->get(K("object")).obj())) dt->pop_in(0);
            hb->alignment.offset.y = 0;
        }
    }
    set_text();
}

TableRef Back::save() const {
    return vl::T({{"name", Value(name)}, {"pos", Value(pos)}, {"effect", Value(effect)},
                  {"key", fld(effect->get(K("center")), "key").truthy() ? fld(effect->get(K("center")), "key") : Value("b_red")}});
}

// misc_functions.lua save_run()
void save_run() {
    if (!G.GAME || !G.GAME->get(K("blind")).as<Blind>()) return;
    TableRef areas = new_table();
    const std::pair<const char *, CardArea *> named[] = {{"hand", G.hand.get()},           {"deck", G.deck.get()},
                                                        {"jokers", G.jokers.get()},       {"consumeables", G.consumeables.get()},
                                                        {"play", G.play.get()},           {"discard", G.discard.get()},
                                                        {"shop_jokers", G.shop_jokers.get()}, {"shop_vouchers", G.shop_vouchers.get()},
                                                        {"shop_booster", G.shop_booster.get()}, {"pack_cards", G.pack_cards.get()}};
    for (auto &[k, a] : named)
        if (a && !a->REMOVED) areas->set(k, Value(a->save()));
    TableRef tags = new_table();
    if (Table *ts = G.GAME->get(K("tags")).table())
        for (int i = 1; i <= ts->len(); i++)
            if (Tag *t = ts->get(i).as<Tag>()) tags->push(Value(t->save()));
    Blind *blind = G.GAME->get(K("blind")).as<Blind>();
    Back *back = G.GAME->get(K("selected_back")).as<Back>();
    Value culled = recursive_table_cull(Value(vl::T({{"cardAreas", Value(areas)},
                                                    {"tags", Value(tags)},
                                                    {"GAME", Value(G.GAME)},
                                                    {"STATE", (double)G.STATE},
                                                    {"ACTION", G.G_TABLE->get(K("action"))},
                                                    {"BLIND", Value(blind->save())},
                                                    {"BACK", back ? Value(back->save()) : Value()},
                                                    {"VERSION", Value(G.VERSION)}})));
    G.ARGS->set("save_run", culled);
    if (!G.FILE_HANDLER) G.FILE_HANDLER = new_table();
    G.FILE_HANDLER->set("run", true);
    G.FILE_HANDLER->set("update_queued", true);
}

// save_with_action(action): a save that resumes the action (a pack being opened) on load
void save_with_action(const TableRef &action) {
    G.G_TABLE->set("action", Value(action));
    save_run();
    G.G_TABLE->set("action", Value());
}

}  // namespace vl
