// card_character.lua
#include "game/card_character.h"

#include "engine/controller.h"
#include "engine/event.h"
#include "engine/particles.h"
#include "engine/random.h"
#include "engine/ui.h"
#include "game/card.h"
#include "game/functions.h"
#include "game/g.h"
#include "game/sound.h"

namespace vl {

namespace {
inline Value fld(const Value &t, const char *k) { return t.get(Value(intern(k))); }
Value node(int uit, const TableRef &cfg, const TableRef &nodes = nullptr) {
    TableRef t = vl::T({{"n", (double)uit}, {"config", Value(cfg)}});
    if (nodes) t->set("nodes", Value(nodes));
    return Value(t);
}
// set_role{role_type = 'Minor', xy_bond = 'Weak', r_bond = 'Strong', major = major}
void minor_of(Moveable *m, Moveable *major) {
    static const RoleType minor = RoleType::Minor;
    static const Bond weak = Bond::Weak, strong = Bond::Strong;
    RoleArgs ra;
    ra.role_type = &minor;
    ra.xy_bond = &weak;
    ra.r_bond = &strong;
    ra.major = major;
    m->set_role(ra);
}
}  // namespace

Card_Character::Card_Character(const TableRef &args) {
    auto num = [&](const char *k, double d) { return args->get(Value(intern(k))).truthy() ? args->get(Value(intern(k))).num() : d; };
    init((float)num("x", 1), (float)num("y", 1), (float)num("w", G.CARD_W * 1.1), (float)num("h", G.CARD_H * 1.1));
    states.collide.can = false;
    config = vl::T({{"args", Value(args)}});
    const Value center = args->get(K("center")).truthy() ? args->get(K("center")) : G.P_CENTERS->get(K("j_joker"));
    Card *card = new Card(T->x, T->y, G.CARD_W, G.CARD_H, G.P_CARDS->get(K("empty")), center, vl::T({{"bypass_discovery_center", true}}));
    children.set("card", card);
    card->states.visible = false;
    card->start_materialize(A({G.colour("BLUE"), G.colour("WHITE"), G.colour("RED")}));
    AlignArgs al;
    al.major = this;
    al.type = "cm";
    XY off{0, 0};
    al.offset = &off;
    card->set_alignment(al);
    card->jimbo = this;
    card->states.collide.can = true;
    card->states.focus.can = false;
    card->states.hover.can = true;
    card->states.drag.can = false;

    auto *p = new Particles(0, 0, 0, 0,
                            vl::T({{"timer", 0.03},
                                   {"scale", 0.3},
                                   {"speed", 1.2},
                                   {"lifespan", 2},
                                   {"attach", Value((Object *)this)},
                                   {"colours", Value(A({G.colour("RED"), G.colour("BLUE"), G.colour("ORANGE")}))},
                                   {"fill", true}}));
    children.set("particles", p);
    p->static_rotation = true;
    minor_of(p, this);
    G.I.CARD.add(this);
}

void Card_Character::move(double dt) { Moveable::move(dt); }

void Card_Character::hard_set_VT() {
    align_to_major();
    Moveable::hard_set_VT();
    align();
    if (auto *c = dynamic_cast<Moveable *>(children.get("card"))) c->hard_set_VT();
}

void Card_Character::align() {
    if (auto *c = dynamic_cast<Moveable *>(children.get("card"))) {
        c->T->x = T->x + (T->w - c->T->w) / 2;
        c->T->y = T->y + (T->h - c->T->h) / 2;
    }
}

void Card_Character::add_button(const Value &button, const Value &func, const Value &colour, const Value &update_func, bool snap_to, const Value &yoff) {
    if (Node *b = children.get("button")) b->remove();
    TableRef ba = vl::T({{"align", "bm"},
                         {"offset", Value(vl::T({{"x", 0}, {"y", yoff.truthy() ? yoff : Value(0.3)}}))},
                         {"major", Value((Object *)this)},
                         {"parent", Value((Object *)this)}});
    config->set("button_align", Value(ba));
    UIBox *box = new UIBox(create_UIBox_character_button(vl::T({{"button", button}, {"func", func}, {"colour", colour}, {"update_func", update_func}, {"maxw", 3}})), ba);
    children.set("button", box);
    if (snap_to) G.CONTROLLER->snap_to(box);
}

void Card_Character::add_speech_bubble(const std::string &text_key, const char *align, const TableRef &loc_vars) {
    if (Node *b = children.get("speech_bubble")) b->remove();
    TableRef sa = vl::T({{"align", align ? align : "bm"}, {"offset", Value(vl::T({{"x", 0}, {"y", 0}}))}, {"parent", Value((Object *)this)}});
    config->set("speech_bubble_align", Value(sa));
    UIBox *box = new UIBox(speech_bubble(text_key, loc_vars), sa);
    children.set("speech_bubble", box);
    minor_of(box, this);
    box->states.visible = false;
}

void Card_Character::remove_button() {
    if (Node *b = children.get("button")) {
        b->remove();
        children.set("button", nullptr);
    }
}

void Card_Character::remove_speech_bubble() {
    if (Node *b = children.get("speech_bubble")) {
        b->remove();
        children.set("speech_bubble", nullptr);
    }
}

void Card_Character::say_stuff(int n, bool not_first) {
    talking = true;
    Ref<Card_Character> self(this);
    if (!not_first) {
        G.E_MANAGER->add_event(ev_after(0.1, [self, n] {
            if (Node *b = self->children.get("speech_bubble")) b->states.visible = true;
            self->say_stuff(n, true);
            return true;
        }));
        return;
    }
    if (n <= 0) {
        talking = false;
        return;
    }
    int said = (int)lua_math.random(1, 11);
    while (said == last_said) said = (int)lua_math.random(1, 11);
    last_said = said;
    play_sound(("voice" + std::to_string((int)lua_math.random(1, 11))).c_str(), (float)(G.SPEEDFACTOR * (lua_math.random() * 0.2 + 1)), 0.5f);
    if (auto *c = dynamic_cast<Card *>(children.get("card"))) c->juice_up_card();
    EventConfig e = ev_after(0.13, [self, n] {
        self->say_stuff(n - 1, true);
        return true;
    });
    e.blockable = false;
    e.blocking = false;
    G.E_MANAGER->add_event(e, EventManager::TUTORIAL);
}

void Card_Character::draw() {
    if (Node *p = children.get("particles")) p->draw();
    if (Node *b = children.get("speech_bubble")) b->draw();
    if (Node *b = children.get("button"); b && !talking) b->draw();
    if (Node *c = children.get("card")) c->draw();
    add_to_drawhash(this);
    draw_boundingrect();
}

void Card_Character::remove() {
    Ref<Card_Character> keep(this);
    G.G_TABLE->set("jimboed", Value());
    remove_all(children);
    G.I.CARD.remove(this);
    Moveable::remove();
}

TableRef speech_bubble(const std::string &text_key, const TableRef &loc_vars) {
    TableRef text = new_table();
    const bool quip = loc_vars && loc_vars->get(K("quip")).truthy();
    localize(vl::T({{"type", quip ? "quips" : "tutorial"},
                    {"key", text_key.empty() ? (quip ? "lq_1" : "sb_1") : text_key},
                    {"vars", loc_vars ? Value(loc_vars) : Value(new_table())},
                    {"nodes", Value(text)}}));
    // Vitalatro: with large menus the text is bigger (it is hard to read on the Vita's screen)
    if (G.large_menus())
        for (int k = 1; k <= text->len(); k++)
            if (Table *line = text->get(k).table())
                for (int i = 1; i <= line->len(); i++)
                    if (Table *conf = fld(line->get(i), "config").table(); conf && conf->get(K("scale")).is_num())
                        conf->set("scale", conf->get(K("scale")).num() * 1.5);
    TableRef row = new_table();
    for (int k = 1; k <= text->len(); k++) row->push(node(UIT_R, vl::T({{"align", "cl"}}), TableRef(text->get(k).table())));
    return TableRef(
        node(UIT_ROOT, vl::T({{"align", "cm"}, {"minh", 1}, {"r", 0.3}, {"padding", 0.07}, {"minw", 1}, {"colour", G.colour("JOKER_GREY")}, {"shadow", true}}),
             A({node(UIT_C, vl::T({{"align", "cm"}, {"minh", 1}, {"r", 0.2}, {"padding", 0.1}, {"minw", 1}, {"colour", G.colour("WHITE")}}),
                     A({node(UIT_C, vl::T({{"align", "cm"}, {"minh", 1}, {"r", 0.2}, {"padding", 0.03}, {"minw", 1}, {"colour", G.colour("WHITE")}}), row)}))}))
            .table());
}

TableRef create_UIBox_character_button(const TableRef &args) {
    const Value button = args->get(K("button")).truthy() ? args->get(K("button")) : Value("NONE");
    const Value colour = args->get(K("colour")).truthy() ? args->get(K("colour")) : G.colour("RED");
    TableRef c = vl::T({{"align", "tm"}, {"minw", 2.0}, {"padding", 0.2}, {"minh", 1.5}, {"r", 0.1}, {"hover", true}, {"colour", colour},
                        {"button", args->get(K("func"))}, {"func", args->get(K("update_func"))}, {"shadow", true}, {"maxw", args->get(K("maxw"))}});
    return TableRef(
        node(UIT_ROOT, vl::T({{"align", "cm"}, {"padding", 0.1}, {"colour", G.colour("CLEAR")}}),
             A({node(UIT_C, c,
                     A({node(UIT_R, vl::T({{"align", "cm"}, {"padding", 0}}),
                             A({node(UIT_T, vl::T({{"text", button}, {"scale", 0.55}, {"colour", fld(G.colour("UI"), "TEXT_LIGHT")},
                                                   {"focus_args", Value(vl::T({{"button", "x"}, {"orientation", "bm"}}))}, {"func", "set_button_pip"}}))}))}))}))
            .table());
}

}  // namespace vl
