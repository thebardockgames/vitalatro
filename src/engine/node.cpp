// engine/node.lua
#include "engine/node.h"

#include <cmath>

#include "core/math.h"

#include "engine/controller.h"
#include "game/g.h"
#include "gfx/gfx.h"

namespace vl {

// ------------------------------------------------------------------ Children
Node *Children::get(Str *name) const {
    for (auto &p : named_)
        if (p.first == name) return p.second.get();
    return nullptr;
}

void Children::set(Str *name, Node *n) {
    for (size_t i = 0; i < named_.size(); i++)
        if (named_[i].first == name) {
            if (n) named_[i].second = n;
            else named_.erase(named_.begin() + i);
            return;
        }
    if (n) named_.push_back({name, NodeRef(n)});
}

void Children::push(Node *n) { array_.push_back(NodeRef(n)); }

void Children::remove_index(int i) {
    if (i >= 0 && i < (int)array_.size()) array_.erase(array_.begin() + i);
}

void Children::remove(Node *n) {
    for (size_t i = 0; i < array_.size(); i++)
        if (array_[i].get() == n) { array_.erase(array_.begin() + i); return; }
    for (size_t i = 0; i < named_.size(); i++)
        if (named_[i].second.get() == n) { named_.erase(named_.begin() + i); return; }
}

// ------------------------------------------------------------------ Node
Node::~Node() = default;

void Node::init(const Args &args) {
    *T = args.T;
    CT = T;
    click_offset = {};
    hover_offset = {};
    created_on_pause = G.paused();
    ID = G.ID++;
    FRAME.DRAW = -1;
    FRAME.MOVE = -1;
    states = States{};
    container = args.container ? args.container : G.ROOM.get();
    // Node:init inserts plain Nodes into G.I.NODE; subclasses register themselves
    if (!G.STAGE_OBJECT_INTERRUPT) G.STAGE_OBJECTS[G.STAGE].add(this);
}

void Node::set_var(std::string_view name, Value v) {
    if (!vars_) vars_ = new_table();
    vars_->set(Value(intern(name)), std::move(v));
}

void Node::draw_boundingrect() {
    under_overlay = G.under_overlay;
    // debug outlines (G.DEBUG) are not part of the release game
}

void Node::draw() {
    draw_boundingrect();
    if (states.visible) {
        add_to_drawhash(this);
        children.each([](Str *, Node *c) { c->draw(); });
    }
}

bool Node::collides_with_point(XY point) {
    if (!container) return false;
    Transform *t = CT ? CT : T;
    float b = states.hover.is ? G.COLLISION_BUFFER : 0;
    XY p = point;
    if (container.get() != this) {
        Transform &c = *container->T;
        if (std::fabs(c.r) < 0.1f) {
            point_translate(p, {-c.w / 2, -c.h / 2});
            point_rotate(p, c.r);
            point_translate(p, {c.w / 2 - c.x, c.h / 2 - c.y});
        } else {
            point_translate(p, {-c.x, -c.y});
        }
    }
    if (std::fabs(t->r) < 0.1f) {
        return p.x >= t->x - b && p.y >= t->y - b && p.x <= t->x + t->w + b && p.y <= t->y + t->h + b;
    }
    float cs = std::cos(t->r + (float)PI / 2), sn = std::sin(t->r + (float)PI / 2);
    p.x = p.x - (t->x + 0.5f * t->w);
    p.y = p.y - (t->y + 0.5f * t->h);
    XY r{p.y * cs - p.x * sn, p.y * sn + p.x * cs};
    p.x = r.x + (t->x + 0.5f * t->w);
    p.y = r.y + (t->y + 0.5f * t->h);
    return p.x >= t->x - b && p.y >= t->y - b && p.x <= t->x + t->w + b && p.y <= t->y + t->h + b;
}

void Node::set_offset(XY point, std::string_view type) {
    XY p = point;
    Transform &c = *container->T;
    point_translate(p, {-c.w / 2, -c.h / 2});
    point_rotate(p, c.r);
    point_translate(p, {c.w / 2 - c.x, c.h / 2 - c.y});
    if (type == "Click") click_offset = {p.x - T->x, p.y - T->y};
    else if (type == "Hover") hover_offset = {p.x - T->x, p.y - T->y};
}

Node *Node::can_drag() { return states.drag.can ? this : nullptr; }

void Node::translate_container() {
    if (container && container.get() != this) {
        Transform &c = *container->T;
        float s = G.TILESCALE * G.TILESIZE;
        gfx::translate(c.w * s * 0.5f, c.h * s * 0.5f);
        gfx::rotate(c.r);
        gfx::translate(-c.w * s * 0.5f + c.x * s, -c.h * s * 0.5f + c.y * s);
    }
}

void Node::set_container(Node *c) {
    children.each([c](Str *, Node *ch) { ch->set_container(c); });
    container = c;
}

XY Node::put_focused_cursor() {
    float s = G.TILESCALE * G.TILESIZE;
    return {(T->x + T->w / 2 + container->T->x) * s, (T->y + T->h / 2 + container->T->y) * s};
}

float Node::fast_mid_dist(Node *o) {
    // (sic) the original takes the sqrt of the x term only
    float dx = (o->T->x + 0.5f * o->T->w) - (T->x + T->w);
    float dy = (o->T->y + 0.5f * o->T->h) - (T->y + T->h);
    return std::sqrt(dx) * std::sqrt(dx) + dy * dy;
}

void Node::remove() {
    G.I.POPUP.remove(this);
    G.I.NODE.remove(this);
    G.STAGE_OBJECTS[G.STAGE].remove(this);
    children.each([](Str *, Node *c) { c->remove(); });
    if (G.CONTROLLER) G.CONTROLLER->forget(this);
    REMOVED = true;
}

// drag/hover popups (Node:drag / Node:hover / stop_*) need UIBox; implemented in ui.cpp
}  // namespace vl
