// tag.lua
#include "game/tag.h"

#include <algorithm>

#include "engine/controller.h"
#include "engine/event.h"
#include "engine/random.h"
#include "engine/ui.h"
#include "game/card.h"
#include "game/card_factory.h"
#include "game/card_ui.h"
#include "game/functions.h"
#include "game/g.h"
#include "game/shop.h"
#include "game/sound.h"
#include "game/state_events.h"
#include "game/ui_definitions.h"

namespace vl {

namespace {
inline Value fld(const Value &t, const char *k) { return t.get(Value(intern(k))); }
int tagid = 0;  // G.tagid
}  // namespace

Tag::Tag(const Value &k, bool for_collection, const Value &blind_type) {
    key = std::string(k.str());
    Value proto = G.P_TAGS->get(k);
    if (!proto.truthy()) proto = G.tag_undiscovered;
    config = TableRef(copy_table(fld(proto, "config")).table());
    if (!config) config = new_table();
    pos = fld(proto, "pos");
    name = std::string(fld(proto, "name").str());
    Value tt = G.GAME->get(K("tag_tally"));
    tally = tt.truthy() ? tt.num() : 0;
    triggered = false;
    ID = tagid++;
    ability = vl::T({{"orbital_hand", Value("[" + localize("k_poker_hand").to_string() + "]")}, {"blind_type", blind_type}});
    G.GAME->set("tag_tally", tt.truthy() ? tt.num() + 1 : 1);
    if (!for_collection) set_ability();
}

void Tag::nope() {
    Ref<Tag> self(this);
    G.E_MANAGER->add_event(ev_after(0.2, [self] {
        attention_text(vl::T({{"text", "NOPE"}, {"colour", G.colour("WHITE")}, {"scale", 0.7}, {"hold", 0.3 / G.SETTINGS->get(K("GAMESPEED")).num()},
                              {"cover", Value((Object *)self->HUD_tag.get())}, {"cover_colour", G.colour("BLACK")}, {"align", "cm"}}));
        play_sound("cancel", 1.4f, 0.5f);
        return true;
    }));
    G.E_MANAGER->add_event(ev_after(0.1, [self] {
        self->HUD_tag->states.visible = false;
        play_sound("cancel", 1.26f, 0.5f);
        return true;
    }));
    G.E_MANAGER->add_event(ev_after(0.5, [self] {
        self->remove();
        return true;
    }));
}

void Tag::yep(const Value &message, const Value &colour, std::function<bool()> func) {
    stop_use();
    Ref<Tag> self(this);
    G.E_MANAGER->add_event(ev_after(0.4, [self, message, colour] {
        attention_text(vl::T({{"text", message},
                              {"colour", G.colour("WHITE")},
                              {"scale", 1},
                              {"hold", 0.3 / G.SETTINGS->get(K("GAMESPEED")).num()},
                              {"cover", Value((Object *)self->HUD_tag.get())},
                              {"cover_colour", colour.truthy() ? colour : G.colour("GREEN")},
                              {"align", "cm"}}));
        play_sound("generic1", (float)(0.9 + lua_math.random() * 0.1), 0.8f);
        play_sound("holo1", (float)(1.2 + lua_math.random() * 0.1), 0.4f);
        return true;
    }));
    G.E_MANAGER->add_event(ev_immediate([self] {
        self->HUD_tag->states.visible = false;
        return true;
    }));
    G.E_MANAGER->add_event(ev_immediate(std::move(func)));
    G.E_MANAGER->add_event(ev_after(0.7, [self] {
        self->remove();
        return true;
    }));
}

void Tag::set_ability() {
    if (name != "Orbital Tag") return;
    Value oh = G.G_TABLE->get(K("orbital_hand"));
    if (oh.truthy()) {
        ability->set("orbital_hand", oh);
    } else if (ability->get(K("blind_type")).truthy()) {
        Value oc = G.GAME->get(K("orbital_choices"));
        Value choice = oc.get(fld(G.GAME->get(K("round_resets")), "ante")).get(ability->get(K("blind_type")));
        if (oc.truthy() && choice.truthy()) ability->set("orbital_hand", choice);
    }
}

namespace {
// a booster pack opened for free in the middle of the play area (Charm, Meteor... tags)
void open_tag_pack(const std::string &key) {
    const float w = G.CARD_W * 1.27f, h = G.CARD_H * 1.27f;
    Card *card = new Card(G.play->T->x + G.play->T->w / 2 - w / 2, G.play->T->y + G.play->T->h / 2 - h / 2, w, h, G.P_CARDS->get(K("empty")),
                          G.P_CENTERS->get(Value(key)), vl::T({{"bypass_discovery_center", true}, {"bypass_discovery_ui", true}}));
    card->cost = 0;
    card->from_tag = true;
    use_card(card, nullptr);
    card->start_materialize();
}
}  // namespace

Value Tag::apply_to_run(const TableRef &context) {
    if (triggered || !(config->get(K("type")) == context->get(K("type")))) return Value();
    const std::string type = context->get(K("type")).to_string();
    Ref<Tag> self(this);
    // G.CONTROLLER.locks[self.ID]
    const std::string lock = "tag" + std::to_string(ID);
    auto unlock = [lock] {
        G.CONTROLLER->locks.erase(lock);
        return true;
    };
    auto cfg = [this](const char *k) { return config->get(Value(intern(k))).num(); };
    const Value plus("+");
    if (type == "eval") {
        const Value lb = G.GAME->get(K("last_blind"));
        if (name == "Investment Tag" && lb.truthy() && fld(lb, "boss").truthy()) {
            yep(plus, G.colour("GOLD"), [] { return true; });
            triggered = true;
            return Value(vl::T({{"dollars", config->get(K("dollars"))}, {"condition", localize("ph_defeat_the_boss")}, {"pos", pos}, {"tag", Value((Object *)this)}}));
        }
    } else if (type == "immediate") {
        G.CONTROLLER->locks[lock] = true;
        if (name == "Top-up Tag") {
            yep(plus, G.colour("PURPLE"), [self, unlock] {
                for (int i = 1; i <= (int)self->config->get(K("spawn_jokers")).num(); i++)
                    if (G.jokers && (double)G.jokers->cards.size() < G.jokers->config->get(K("card_limit")).num()) {
                        Card *card = create_card("Joker", G.jokers.get(), false, Value(0.0), false, false, "", "top");
                        card->add_to_deck();
                        G.jokers->emplace(card);
                    }
                return unlock();
            });
            triggered = true;
            return Value(true);
        }
        auto money = [&](double amount) {
            yep(plus, G.colour("MONEY"), unlock);
            ease_dollars(amount);
            triggered = true;
            return Value(true);
        };
        if (name == "Skip Tag") return money(G.GAME->get(K("skips")).num() * cfg("skip_bonus"));
        if (name == "Garbage Tag") return money(G.GAME->get(K("unused_discards")).num() * cfg("dollars_per_discard"));
        if (name == "Handy Tag") return money(G.GAME->get(K("hands_played")).num() * cfg("dollars_per_hand"));
        if (name == "Economy Tag") {
            yep(plus, G.colour("MONEY"), unlock);
            const double max = cfg("max");
            G.E_MANAGER->add_event(ev_immediate([max] {
                ease_dollars(std::min(max, std::max(0.0, G.GAME->get(K("dollars")).num())), true);
                return true;
            }));
            triggered = true;
            return Value(true);
        }
        if (name == "Orbital Tag") {
            const Value hand = ability->get(K("orbital_hand"));
            const Value h = G.GAME->get(K("hands")).get(hand);
            update_hand_text(vl::T({{"sound", "button"}, {"volume", 0.7}, {"pitch", 0.8}, {"delay", 0.3}}),
                             vl::T({{"handname", hand}, {"chips", fld(h, "chips")}, {"mult", fld(h, "mult")}, {"level", fld(h, "level")}}));
            // (the original hands the tag itself as the card: level_up_hand only juices it)
            level_up_hand(tag_sprite.get(), hand.to_string(), false, cfg("levels"));
            update_hand_text(vl::T({{"sound", "button"}, {"volume", 0.7}, {"pitch", 1.1}, {"delay", 0}}),
                             vl::T({{"mult", 0}, {"chips", 0}, {"handname", ""}, {"level", ""}}));
            yep(plus, G.colour("MONEY"), unlock);
            triggered = true;
            return Value(true);
        }
    } else if (type == "new_blind_choice") {
        G.CONTROLLER->locks[lock] = true;
        auto pack = [&](const Value &colour, std::function<std::string()> key) {
            yep(plus, colour, [key, unlock] {
                open_tag_pack(key());
                return unlock();
            });
            triggered = true;
            return Value(true);
        };
        const Value spectral = G.colour("SECONDARY_SET").get(K("Spectral"));
        if (name == "Charm Tag")
            return pack(G.colour("PURPLE"), [] { return "p_arcana_mega_" + std::to_string((int)lua_math.random(1, 2)); });
        if (name == "Meteor Tag")
            return pack(G.colour("SECONDARY_SET").get(K("Planet")), [] { return "p_celestial_mega_" + std::to_string((int)lua_math.random(1, 2)); });
        if (name == "Ethereal Tag") return pack(spectral, [] { return std::string("p_spectral_normal_1"); });
        if (name == "Standard Tag") return pack(spectral, [] { return std::string("p_standard_mega_1"); });
        if (name == "Buffoon Tag") return pack(spectral, [] { return std::string("p_buffoon_mega_1"); });
        if (name == "Boss Tag") {
            yep(plus, G.colour("GREEN"), [lock] {
                G.G_TABLE->set("from_boss_tag", true);
                reroll_boss();
                G.E_MANAGER->add_event(ev_immediate([lock] {
                    G.E_MANAGER->add_event(ev_immediate([lock] {
                        G.CONTROLLER->locks.erase(lock);
                        return true;
                    }));
                    return true;
                }));
                return true;
            });
            triggered = true;
            return Value(true);
        }
    } else if (type == "voucher_add") {
        if (name == "Voucher Tag") {
            yep(plus, G.colour("SECONDARY_SET").get(K("Voucher")), [] {
                Value vt = G.ARGS->get(K("voucher_tag"));
                if (!vt.is_table()) {
                    vt = Value(new_table());
                    G.ARGS->set(K("voucher_tag"), vt);
                }
                const Value voucher_key = get_next_voucher_key(true);
                vt.table()->set(voucher_key, true);
                CardArea *sv = G.shop_vouchers.get();
                sv->config->set("card_limit", sv->config->get(K("card_limit")).num() + 1);
                Card *card = new Card(sv->T->x + sv->T->w / 2, sv->T->y, G.CARD_W, G.CARD_H, G.P_CARDS->get(K("empty")), G.P_CENTERS->get(voucher_key),
                                      vl::T({{"bypass_discovery_center", true}, {"bypass_discovery_ui", true}}));
                create_shop_card_ui(card, "Voucher", sv);
                card->start_materialize();
                sv->emplace(card);
                G.ARGS->set(K("voucher_tag"), Value());
                return true;
            });
            triggered = true;
        }
    } else if (type == "tag_add") {
        Tag *added = context->get(K("tag")).as<Tag>();
        if (name == "Double Tag" && added && added->key != "tag_double") {
            G.CONTROLLER->locks[lock] = true;
            Ref<Tag> other(added);
            yep(plus, G.colour("BLUE"), [other, unlock] {
                const Value oh = other->ability ? other->ability->get(K("orbital_hand")) : Value();
                if (oh.truthy()) G.G_TABLE->set("orbital_hand", oh);
                add_tag(new Tag(Value(other->key)));
                G.G_TABLE->set("orbital_hand", Value());
                return unlock();
            });
            triggered = true;
        }
    } else if (type == "round_start_bonus") {
        if (name == "Juggle Tag") {
            yep(plus, G.colour("BLUE"), [] { return true; });
            G.hand->change_size((int)cfg("h_size"));
            Table *rr = G.GAME->get(K("round_resets")).table();
            rr->set("temp_handsize", rr->get(K("temp_handsize")).num() + cfg("h_size"));
            triggered = true;
            return Value(true);
        }
    } else if (type == "store_joker_create") {
        CardArea *area = context->get(K("area")).as<CardArea>();
        Card *card = nullptr;
        auto offer = [&](const Value &colour) {
            create_shop_card_ui(card, "Joker", area);
            card->states.visible = false;
            CardRef c(card);
            yep(plus, colour, [c] {
                c->start_materialize();
                c->ability->set("couponed", true);
                c->set_cost();
                return true;
            });
        };
        if (name == "Rare Tag") {
            // the rare jokers owned (each key once), against the rare pool's size
            int owned = 0;
            std::vector<std::string> seen;
            for (auto &j : G.jokers->cards) {
                const Value c = j->config->get(K("center"));
                const std::string k = fld(c, "key").to_string();
                if (fld(c, "rarity").num() == 3 && std::find(seen.begin(), seen.end(), k) == seen.end()) {
                    owned++;
                    seen.push_back(k);
                }
            }
            if (G.P_JOKER_RARITY_POOLS->get(3).table()->len() > owned) {
                card = create_card("Joker", area, false, Value(1.0), false, false, "", "rta");
                offer(G.colour("RED"));
            } else {
                nope();
            }
        } else if (name == "Uncommon Tag") {
            card = create_card("Joker", area, false, Value(0.9), false, false, "", "uta");
            offer(G.colour("GREEN"));
        }
        triggered = true;
        return card ? Value((Object *)card) : Value();
    } else if (type == "shop_start") {
        if (name == "D6 Tag" && !G.GAME->get(K("shop_d6ed")).truthy()) {
            G.GAME->set("shop_d6ed", true);
            yep(plus, G.colour("GREEN"), [] {
                G.GAME->get(K("round_resets")).table()->set("temp_reroll_cost", 0);
                calculate_reroll_cost(true);
                return true;
            });
            triggered = true;
            return Value(true);
        }
    } else if (type == "store_joker_modify") {
        Card *card = context->get(K("card")).as<Card>();
        bool applied = false;
        if (card && !card->edition && !card->temp_edition && card->ability->get(K("set")).equals("Joker")) {
            G.CONTROLLER->locks[lock] = true;
            const char *edition = name == "Foil Tag"          ? "foil"
                                  : name == "Holographic Tag" ? "holo"
                                  : name == "Polychrome Tag"  ? "polychrome"
                                  : name == "Negative Tag"    ? "negative"
                                                              : nullptr;
            if (edition) {
                card->temp_edition = true;
                CardRef c(card);
                const std::string ed(edition);
                yep(plus, G.colour("DARK_EDITION"), [c, ed, unlock] {
                    c->temp_edition = false;
                    c->set_edition(Value(vl::T({{ed.c_str(), true}})), true);
                    c->ability->set("couponed", true);
                    c->set_cost();
                    return unlock();
                });
                applied = true;
            }
            triggered = true;
        }
        return applied ? Value(true) : Value();
    } else if (type == "shop_final_pass") {
        if (name == "Coupon Tag" && G.shop && !G.GAME->get(K("shop_free")).truthy()) {
            G.GAME->set("shop_free", true);
            yep(plus, G.colour("GREEN"), [] {
                if (G.shop_jokers && G.shop_booster)
                    for (CardArea *a : {G.shop_jokers.get(), G.shop_booster.get()})
                        for (auto &c : a->cards) {
                            c->ability->set("couponed", true);
                            c->set_cost();
                        }
                return true;
            });
            triggered = true;
            return Value(true);
        }
    }
    return Value();
}

TableRef Tag::save() const { return vl::T({{"key", Value(key)}, {"tally", tally}, {"ability", Value(ability)}}); }

void Tag::load(const TableRef &t) {
    key = std::string(t->get(K("key")).str());
    Value proto = G.P_TAGS->get(Value(key));
    if (!proto.truthy()) proto = G.tag_undiscovered;
    config = TableRef(copy_table(fld(proto, "config")).table());
    pos = fld(proto, "pos");
    name = std::string(fld(proto, "name").str());
    tally = t->get(K("tally")).num();
    ability = TableRef(t->get(K("ability")).table());
    G.GAME->set("tag_tally", std::max(tally, G.GAME->get(K("tag_tally")).num()) + 1);
}

void Tag::juice_up(float scale, const float *rot) {
    if (tag_sprite) tag_sprite->juice_up(scale, rot);
}

TableRef Tag::generate_UI(float size, TagSprite **sprite_out) {
    Value p = hide_ability ? fld(G.tag_undiscovered, "pos") : pos;
    SpritePos sp;
    sp.x = (float)fld(p, "x").num();
    sp.y = (float)fld(p, "y").num();
    TagSprite *s = new TagSprite(0, 0, size, size, &G.ASSET_ATLAS["tags"], sp, this);
    s->T->scale = 1;
    TableRef tab = vl::T({{"n", (double)UIT_C},
                          {"config", Value(vl::T({{"align", "cm"}, {"ref_table", Value((Object *)this)}, {"group", tally}}))},
                          {"nodes", Value(A({Value(vl::T({{"n", (double)UIT_O},
                                                          {"config", Value(vl::T({{"w", size}, {"h", size}, {"colour", G.colour("BLUE")},
                                                                                  {"object", Value((Object *)s)}, {"focus_with_object", true}}))}}))}))}});
    DrawStep a, b;
    a.has_shadow_height = true;
    a.shadow_height = 0.05f;
    s->define_draw_steps({a, b});
    s->states.hover.can = true;
    s->states.drag.can = false;
    s->states.collide.can = true;
    s->config->set("tag", Value((Object *)this));
    s->config->set("force_focus", true);
    s->touch_collide_tilt = true;
    s->juice_up();
    tag_sprite = s;
    if (sprite_out) *sprite_out = s;
    return tab;
}

void TagSprite::hover() {
    if (!G.CONTROLLER->dragging.target || G.CONTROLLER->HID.touch) {
        if (!hovering && states.visible) {
            hovering = true;
            hover_tilt = 3;
            float r = 0.02f;
            juice_up(0.05f, &r);
            play_sound("paper1", (float)(lua_math.random() * 0.1 + 0.55), 0.42f);
            play_sound("tarot2", (float)(lua_math.random() * 0.1 + 0.55), 0.09f);
            tag->get_uibox_table(this);
            config->set("h_popup", Value(card_h_popup(ability_UIBox_table, Value(config))));
            config->set("h_popup_config", Value(vl::T({{"align", "cl"}, {"offset", Value(vl::T({{"x", -0.1}, {"y", 0}}))}, {"parent", Value((Object *)this)}})));
            Node::hover();
            if (Node *alert = children.get("alert")) {
                alert->remove();
                children.set("alert", nullptr);
                if (!tag->key.empty() && G.P_TAGS->get(Value(tag->key)).truthy()) G.P_TAGS->get(Value(tag->key)).table()->set("alerted", true);
                G.save_progress();
            }
        }
    }
}

void TagSprite::stop_hover() {
    hovering = false;
    Node::stop_hover();
    hover_tilt = 0;
}

void Tag::remove_from_game() {
    Table *tags = G.GAME->get(K("tags")).table();
    if (!tags) return;
    for (int i = 1; i <= tags->len(); i++)
        if (tags->get(i).obj() == this) {
            tags->remove(i);
            return;
        }
}

void Tag::get_uibox_table(TagSprite *sprite) {
    if (!sprite) sprite = tag_sprite.get();
    auto cfg = [this](const char *k) { return config->get(Value(intern(k))); };
    auto num = [&](const char *k) { return cfg(k).num(); };
    TableRef vars = new_table();
    if (name == "Investment Tag") vars = A({cfg("dollars")});
    else if (name == "Handy Tag") vars = A({cfg("dollars_per_hand"), num("dollars_per_hand") * G.GAME->get(K("hands_played")).num()});
    else if (name == "Garbage Tag") vars = A({cfg("dollars_per_discard"), num("dollars_per_discard") * G.GAME->get(K("unused_discards")).num()});
    else if (name == "Juggle Tag") vars = A({cfg("h_size")});
    else if (name == "Top-up Tag") vars = A({cfg("spawn_jokers")});
    else if (name == "Skip Tag") vars = A({cfg("skip_bonus"), num("skip_bonus") * (G.GAME->get(K("skips")).num() + 1)});
    else if (name == "Orbital Tag") {
        const Value oh = ability->get(K("orbital_hand"));
        const bool placeholder = oh.equals("[" + localize("k_poker_hand").to_string() + "]");
        vars = A({placeholder ? oh : localize(oh.to_string(), "poker_hands"), cfg("levels")});
    } else if (name == "Economy Tag") vars = A({cfg("max")});
    sprite->ability_UIBox_table = generate_card_ui(G.P_TAGS->get(Value(key)), nullptr, Value(vars), hide_ability ? "Undiscovered" : "Tag", nullptr, hide_ability);
}

void Tag::remove() {
    Ref<Tag> keep(this);
    remove_from_game();
    // the HUD tags hang from each other: the next one takes this one's place
    auto &H = G.HUD_tags;
    int k = -1;
    for (int i = 0; i < (int)H.size(); i++)
        if (H[i].get() == HUD_tag.get()) k = i;
    if (k >= 0) {
        if (k + 1 < (int)H.size()) {
            auto *next = dynamic_cast<Moveable *>(H[k + 1].get());
            static const Bond weak = Bond::Weak;
            if (k == 0) {
                AlignArgs al;
                al.type = "bri";
                XY off{0.7f, 0};
                al.offset = &off;
                al.bond = &weak;
                al.major = dynamic_cast<Moveable *>(G.ROOM_ATTACH.get());
                next->set_alignment(al);
            } else {
                RoleArgs ra;
                ra.xy_bond = &weak;
                ra.major = dynamic_cast<Moveable *>(H[k - 1].get());
                next->set_role(ra);
            }
        }
        H.erase(H.begin() + k);
    }
    if (HUD_tag) HUD_tag->remove();
}

}  // namespace vl
