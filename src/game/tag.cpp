// tag.lua
#include "game/tag.h"

#include "engine/controller.h"
#include "engine/event.h"
#include "engine/random.h"
#include "engine/ui.h"
#include "game/functions.h"
#include "game/g.h"
#include "game/sound.h"
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
    // TODO(run): the HUD tag's dissolve and removal (Tag:nope's second half)
}

void Tag::yep(const Value &, const Value &, std::function<bool()>) {
    // TODO(run): Tag:yep (the triggered tag's text, dissolve and callback)
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

Value Tag::apply_to_run(const TableRef &context) {
    if (triggered || !(config->get(K("type")) == context->get(K("type")))) return Value();
    // TODO(run): the tag effects (eval, immediate, new_blind_choice, voucher_add, tag_add, round_start_bonus,
    // store_joker_create, shop_start, store_joker_modify, shop_final_pass)
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
            // TODO(ui): the tag's description popup (get_uibox_table + card_h_popup)
            Node::hover();
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

void Tag::remove() {
    Ref<Tag> keep(this);
    remove_from_game();
    // TODO(run): G.HUD_tags realignment
    if (HUD_tag) HUD_tag->remove();
}

}  // namespace vl
