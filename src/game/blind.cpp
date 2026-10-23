// blind.lua
#include "game/blind.h"

#include <cmath>
#include <string>

#include "engine/controller.h"
#include "engine/event.h"
#include "engine/random.h"
#include "engine/text.h"
#include "engine/ui.h"
#include "game/card.h"
#include "game/functions.h"
#include "game/g.h"
#include "game/sound.h"
#include "game/ui_definitions.h"

namespace vl {

namespace {
inline Value fld(const Value &t, const char *k) { return t.get(Value(intern(k))); }
inline TableRef tab(const Value &v) { return TableRef(v.table()); }
UIBox *hud_blind() { return dynamic_cast<UIBox *>(G.HUD_blind.get()); }
}  // namespace

Blind::Blind(float x, float y, float w, float h) {
    init(x, y, w, h);
    config = new_table();
    colour = G.colour("BLACK").table()->deep_copy();
    dark_colour = darken(Value(colour), 0.2f);
    vars = vl::T({{"loc_name", ""}, {"chip_text", ""}, {"loc_debuff_text", ""}, {"loc_debuff_lines", Value(A({"", ""}))}});
    zoom = true;
    SpritePos p;
    Value pos = fld(G.P_BLINDS->get(K("bl_small")), "pos");
    p.x = (float)fld(pos, "x").num();
    p.y = (float)fld(pos, "y").num();
    AnimatedSprite *s = new AnimatedSprite(T->x, T->y, T->w, T->h, &G.ANIMATION_ATLAS["blind_chips"], p);
    children.set("animatedSprite", s);
    // the sprite shares the blind's states table
    states.visible = false;
    s->states = states;
    s->states.drag.can = true;
    states.collide.can = true;
    states.drag.can = true;
    G.I.CARD.add(this);
}

void Blind::change_colour(const Value &blind_col_in) {
    Value blind_col = blind_col_in.truthy() ? blind_col_in : Value(get_blind_main_colour(fld(config->get(K("blind")), "key")));
    Value dark_col = Value(mix_colours(blind_col, G.colour("BLACK"), 0.4f));
    Table *dyn = G.colour("DYN_UI").table();
    ease_colour(TableRef(dyn->get(K("MAIN")).table()), blind_col);
    ease_colour(TableRef(dyn->get(K("DARK")).table()), dark_col);
    if (!boss && has_name) {
        blind_col = Value(darken(G.colour("BLACK"), 0.05f));
        dark_col = Value(lighten(G.colour("BLACK"), 0.07f));
    } else {
        dark_col = Value(mix_colours(blind_col, G.colour("BLACK"), 0.2f));
    }
    ease_colour(TableRef(dyn->get(K("BOSS_MAIN")).table()), blind_col);
    ease_colour(TableRef(dyn->get(K("BOSS_DARK")).table()), dark_col);
}

void Blind::set_text() {
    Value bl = config->get(K("blind"));
    if (!bl.truthy()) return;
    Table *lines = vars->get(K("loc_debuff_lines")).table();
    auto name_text = [&] {
        return name.empty() ? Value(name) : localize(vl::T({{"type", "name_text"}, {"key", fld(bl, "key")}, {"set", "Blind"}}));
    };
    if (disabled) {
        vars->set("loc_name", name_text());
        vars->set("loc_debuff_text", "");
        lines->set(1, "");
        lines->set(2, "");
        return;
    }
    Value loc_vars = fld(bl, "vars");
    if (name == "The Ox")
        loc_vars = Value(A({localize(fld(G.GAME->get(K("current_round")), "most_played_poker_hand").str(), "poker_hands")}));
    Value loc_target = localize(vl::T({{"type", "raw_descriptions"}, {"key", fld(bl, "key")}, {"set", "Blind"}, {"vars", loc_vars}}));
    if (Table *lt = loc_target.table()) {
        vars->set("loc_name", name_text());
        std::string txt;
        for (int k = 1; k <= lt->len(); k++) txt += lt->get(k).to_string() + (k <= lt->len() ? " " : "");
        vars->set("loc_debuff_text", txt);
        lines->set(1, lt->get(1).truthy() ? lt->get(1) : Value(""));
        lines->set(2, lt->get(2).truthy() ? lt->get(2) : Value(""));
    } else {
        vars->set("loc_name", "");
        vars->set("loc_debuff_text", "");
        lines->set(1, "");
        lines->set(2, "");
    }
}

const char *Blind::get_type() const {
    if (name == "Small Blind") return "Small";
    if (name == "Big Blind") return "Big";
    if (!name.empty()) return "Boss";
    return nullptr;
}

void Blind::set_blind(const Value &blind, bool reset, bool silent) {
    if (!reset) {
        config->set("blind", blind.truthy() ? blind : Value(new_table()));
        name = blind.truthy() && fld(blind, "name").truthy() ? std::string(fld(blind, "name").str()) : "";
        has_name = true;
        dollars = blind.truthy() ? fld(blind, "dollars").num() : 0;
        sound_pings = (int)dollars + 2;
        Value nbr = fld(fld(G.GAME->get(K("modifiers")), "no_blind_reward"), get_type() ? get_type() : "");
        if (get_type() && nbr.truthy()) dollars = 0;
        debuff = blind.truthy() && fld(blind, "debuff").truthy() ? tab(fld(blind, "debuff")) : new_table();
        mult = blind.truthy() ? fld(blind, "mult").num() : 0;
        disabled = false;
        discards_sub = Value();
        hands_sub = Value();
        boss = blind.truthy() && fld(blind, "boss").truthy();
        blind_set = false;
        triggered = Value();
        prepped = true;
        set_text();

        Table *game = G.GAME.get();
        if (!game->get(K("last_blind")).truthy()) game->set("last_blind", Value(new_table()));
        game->get(K("last_blind")).table()->set("boss", boss);
        game->get(K("last_blind")).table()->set("name", Value(name));

        if (blind.truthy() && fld(blind, "name").truthy()) change_colour();
        else change_colour(G.colour("BLACK"));

        chips = get_blind_amount((int)fld(game->get(K("round_resets")), "ante").num()) * mult *
                fld(game->get(K("starting_params")), "ante_scaling").num();
        vars->set("chip_text", Value(number_format(Value(chips))));
        if (!blind.truthy()) chips = 0;

        std::string dl;
        for (int i = 0; i < (int)dollars; i++) dl += localize("$").str();
        game->get(K("current_round")).table()->set("dollars_to_be_earned", Value(dl));
        if (UIBox *hb = hud_blind()) {
            hb->alignment.offset.y = -10;
            hb->recalculate();
        }

        if (blind.truthy() && fld(blind, "name").truthy() && !fld(blind, "name").equals("")) {
            alert_debuff(true);
            Ref<Blind> self(this);
            EventConfig e = ev_after(0.05, [self, reset, silent, blind] {
                UIBox *hb = hud_blind();
                if (!hb) return true;
                UIElement *bn = hb->get_UIE_by_ID("HUD_blind_name"), *de = hb->get_UIE_by_ID("dollars_to_be_earned");
                bn->states.visible = false;
                Node *dp = de->parent ? de->parent->parent : nullptr;
                if (dp) dp->states.visible = false;
                hb->alignment.offset.y = 0;
                EventConfig e2 = ev_after(0.15, [self, reset, silent, blind] {
                    UIBox *hb = hud_blind();
                    if (!hb) return true;
                    UIElement *bn = hb->get_UIE_by_ID("HUD_blind_name"), *de = hb->get_UIE_by_ID("dollars_to_be_earned");
                    bn->states.visible = true;
                    Node *dp = de->parent ? de->parent->parent : nullptr;
                    if (dp) dp->states.visible = true;
                    if (auto *t = dynamic_cast<DynaText *>(de->config->get(K("object")).obj())) t->pop_in(0);
                    if (auto *t = dynamic_cast<DynaText *>(bn->config->get(K("object")).obj())) t->pop_in(0);
                    hb->get_UIE_by_ID("HUD_blind_count")->juice_up();
                    Value pos = fld(self->config->get(K("blind")), "pos");
                    SpritePos p;
                    p.x = (float)fld(pos, "x").num();
                    p.y = (float)fld(pos, "y").num();
                    self->sprite()->set_sprite_pos(p);
                    self->blind_set = true;
                    G.ROOM_jiggle += 3;
                    if (!reset && !silent) {
                        self->juice_up_blind();
                        if (blind.truthy()) {
                            play_sound("chips1", (float)(lua_math.random() * 0.1 + 0.55), 0.42f);
                            play_sound("gold_seal", (float)(lua_math.random() * 0.1 + 1.85), 0.26f);
                        }
                    }
                    return true;
                });
                e2.blockable = false;
                G.E_MANAGER->add_event(e2);
                return true;
            });
            e.blockable = false;
            G.E_MANAGER->add_event(e);
        }
        // config.h_popup_config: the blind's hover popup (TODO(run): its UI)
    }

    // TODO(run): The Eye, The Mouth, The Fish, The Water, The Needle, The Manacle, Amber Acorn

    // add new debuffs
    for (auto &c : std::vector<CardRef>(G.playing_cards)) debuff_card(c.get());
    if (G.jokers)
        for (auto &c : std::vector<CardRef>(G.jokers->cards))
            if (!reset) debuff_card(c.get(), true);

    const Value bb = fld(config->get(K("blind")), "boss");
    double spin = bb.truthy() ? (fld(bb, "showdown").truthy() ? 0.5 : 0.25) : 0;
    G.ARGS->get(K("spin")).table()->set("real", (G.reduced_motion() ? 0 : 1) * spin);
}

void Blind::alert_debuff(bool) {
    if (vars->get(K("loc_debuff_text")).str().empty()) return;
    block_play = true;
    Ref<Blind> self(this);
    EventConfig e = ev_immediate([self] {
        if (self->disabled) {
            self->block_play = false;
            return true;
        }
        if (G.STATE != SELECTING_HAND) return false;
        EventConfig e2 = ev_after(G.SETTINGS->get(K("GAMESPEED")).num() * 0.05, [self] {
            play_sound("whoosh1", 0.55f, 0.62f);
            for (int i = 1; i <= 4; i++) {
                EventConfig e3 = ev_after(G.SETTINGS->get(K("GAMESPEED")).num() * 0.1 * (i - 1), [self, i] {
                    if (i == 1) self->juice_up_blind();
                    play_sound("cancel", 0.7f + 0.05f * i, 0.7f);
                    return true;
                });
                e3.blockable = false;
                G.E_MANAGER->add_event(e3);
            }
            double hold = G.SETTINGS->get(K("GAMESPEED")).num() * (self->vars->get(K("loc_debuff_text")).str().size() * 0.035 + 1.3);
            attention_text(vl::T({{"scale", 0.7}, {"text", Value(self->get_loc_debuff_text())}, {"maxw", 12}, {"hold", hold}, {"align", "cm"},
                                  {"offset", Value(vl::T({{"x", 0}, {"y", -1}}))}, {"major", Value((Object *)G.play.get())}}));
            EventConfig e4 = ev_after(1, [self] {
                self->block_play = false;
                if (auto *b = dynamic_cast<UIBox *>(G.buttons.get()))
                    if (UIElement *pb = b->get_UIE_by_ID("play_button")) pb->disable_button = false;
                return true;
            });
            e4.blocking = false;
            e4.blockable = false;
            G.E_MANAGER->add_event(e4);
            return true;
        });
        e2.blockable = false;
        G.E_MANAGER->add_event(e2);
        return true;
    });
    e.blockable = false;
    e.blocking = false;
    G.E_MANAGER->add_event(e);
}

std::string Blind::get_loc_debuff_text() const {
    Value bl = config->get(K("blind"));
    std::string disp = (fld(bl, "name").equals("The Wheel") ? fld(G.GAME->get(K("probabilities")), "normal").to_string() : std::string()) +
                       std::string(vars->get(K("loc_debuff_text")).str());
    // TODO(run): The Mouth's only_hand
    return disp;
}

void Blind::wiggle() {
    sprite()->juice_up(0.3f);
    EventConfig e = ev_after(0.06 * G.SETTINGS->get(K("GAMESPEED")).num(), [] {
        play_sound("tarot2", 0.76f, 0.4f);
        return true;
    });
    e.blockable = false;
    e.blocking = false;
    G.E_MANAGER->add_event(e);
    play_sound("tarot2", 1, 0.4f);
}

void Blind::juice_up(float a, const float *b) { juice_up_blind(a, b ? *b : 0.2f); }

void Blind::juice_up_blind(float a, float b) { sprite()->juice_up(a, &b); }

void Blind::hover() {
    if (!G.CONTROLLER->dragging.target || G.CONTROLLER->HID.touch) {
        if (!hovering && states.visible && sprite()->states.visible) {
            hovering = true;
            hover_tilt = 2;
            float r = 0.02f;
            sprite()->juice_up(0.05f, &r);
            play_sound("chips1", (float)(lua_math.random() * 0.1 + 0.55), 0.12f);
            Node::hover();
        }
    }
}

void Blind::stop_hover() {
    hovering = false;
    hover_tilt = 0;
    Node::stop_hover();
}

void Blind::draw() {
    if (!states.visible) return;
    tilt_var.mx = G.CONTROLLER->cursor_position.x;
    tilt_var.my = G.CONTROLLER->cursor_position.y;
    AnimatedSprite *s = sprite();
    s->states = states;
    RoleArgs ra;
    s->role.draw_major = this;
    SpriteShaderArgs sh;
    const float h = 0.1f;
    sh.shadow_height = &h;
    s->draw_shader(gfx::ShaderId::Dissolve, sh);
    s->draw_shader(gfx::ShaderId::Dissolve, SpriteShaderArgs{});
    children.each([&](Str *k, Node *v) {
        if (v == s) return;
        if (auto *m = dynamic_cast<Moveable *>(v)) m->VT.scale = VT.scale;
        v->draw();
    });
    add_to_drawhash(this);
}

void Blind::move(double dt) {
    Moveable::move(dt);
    align();
}

void Blind::change_dim(float w, float h) {
    T->w = w;
    T->h = h;
    AnimatedSprite *s = sprite();
    s->T->w = w;
    s->T->h = h;
    s->rescale();
}

void Blind::align() {
    const double R = G.TIMERS.REAL;
    children.each([&](Str *, Node *n) {
        auto *v = dynamic_cast<Moveable *>(n);
        if (!v) return;
        if (v == sprite()) {
            if (!v->states.drag.is) {
                v->T->r = (float)(0.02 * std::sin(2 * R + T->x));
                v->T->y = (float)(T->y + 0.03 * std::sin(0.666 * R + T->x));
                shadow_height = (float)(0.1 - (0.04 + 0.03 * std::sin(0.666 * R + T->x)));
                v->T->x = (float)(T->x + 0.03 * std::sin(0.436 * R + T->x));
            }
        } else {
            v->T->x = T->x;
            v->T->y = T->y;
            v->T->r = T->r;
        }
    });
}

void Blind::drawn_to_hand() {
    // TODO(run): Cerulean Bell, Crimson Heart
    prepped = false;
}

void Blind::debuff_card(Card *card, bool) {
    const bool in_jokers = G.jokers && card->area == G.jokers.get();
    if (debuff && !disabled && !in_jokers) {
        Value s = debuff->get(K("suit"));
        if (s.truthy() && card->is_suit(s, true)) {
            card->set_debuff(true);
            return;
        }
        if (debuff->get(K("is_face")).equals("face") && card->is_face(true)) {
            card->set_debuff(true);
            return;
        }
        if (name == "The Pillar" && card->ability->get(K("played_this_ante")).truthy()) {
            card->set_debuff(true);
            return;
        }
        Value v = debuff->get(K("value"));
        if (v.truthy() && v == card->base->get(K("value"))) {
            card->set_debuff(true);
            return;
        }
        Value nm = debuff->get(K("nominal"));
        if (nm.truthy() && nm == card->base->get(K("nominal"))) {
            card->set_debuff(true);
            return;
        }
    }
    if (name == "Crimson Heart" && !disabled && in_jokers) return;
    if (name == "Verdant Leaf" && !disabled && !in_jokers) {
        card->set_debuff(true);
        return;
    }
    card->set_debuff(false);
}

void Blind::remove() {
    G.I.CARD.remove(this);
    Moveable::remove();
}

}  // namespace vl
