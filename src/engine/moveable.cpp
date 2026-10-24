// engine/moveable.lua
#include <cmath>

#include "core/math.h"

#include "engine/node.h"
#include "engine/controller.h"
#include "engine/random.h"
#include "engine/ui.h"
#include "game/g.h"
#include "gfx/gfx.h"

namespace vl {

void Moveable::init(float x, float y, float w, float h) {
    Args a;
    a.T = {x, y, w, h, 0, 1};
    init(a);
}

void Moveable::init(const Args &args) {
    Node::init(args);
    VT = *T;
    velocity = {};
    role = Role{};
    role.draw_major = this;
    alignment = Alignment{};
    pinch_store = {};
    pinch = &pinch_store;
    last_moved = last_aligned = -1;
    static_rotation = false;
    offset = {};
    Mid = this;
    shadow_parrallax_store = {0, -1.5f};
    shadow_parrallax = &shadow_parrallax_store;
    layered_parallax = {};
    layered_parallax_of = nullptr;
    layered_parallax_nil = false;
    shadow_height = 0.2f;
    calculate_parrallax();
    G.MOVEABLES.add(this);
    // plain Moveables go to G.I.MOVEABLE; subclasses register in their own init
}

void Moveable::draw() {
    Node::draw();
    draw_boundingrect();
}

void Moveable::set_alignment(const AlignArgs &args) {
    if (args.major) {
        RoleType minor = RoleType::Minor;
        Bond weak = Bond::Weak;
        RoleArgs ra;
        ra.role_type = &minor;
        ra.major = args.major;
        ra.xy_bond = args.bond ? args.bond : &weak;
        ra.wh_bond = args.wh_bond ? args.wh_bond : &role.wh_bond;
        ra.r_bond = args.r_bond ? args.r_bond : &role.r_bond;
        ra.scale_bond = args.scale_bond ? args.scale_bond : &role.scale_bond;
        set_role(ra);
    }
    if (args.type) alignment.type = args.type;
    if (args.offset) alignment.offset = *args.offset;
    alignment.lr_clamp = args.lr_clamp;
}

void Moveable::align_to_major() {
    if (alignment.type != alignment.prev_type) {
        const std::string &t = alignment.type;
        alignment.list.a = t == "a";
        alignment.list.m = t.find('m') != std::string::npos;
        alignment.list.c = t.find('c') != std::string::npos;
        alignment.list.b = t.find('b') != std::string::npos;
        alignment.list.t = t.find('t') != std::string::npos;
        alignment.list.l = t.find('l') != std::string::npos;
        alignment.list.r = t.find('r') != std::string::npos;
        alignment.list.i = t.find('i') != std::string::npos;
    }
    if (alignment.prev_offset.x == alignment.offset.x && alignment.prev_offset.y == alignment.offset.y &&
        alignment.prev_type == alignment.type)
        return;

    NEW_ALIGNMENT = true;
    if (alignment.type != alignment.prev_type) alignment.prev_type = alignment.type;
    if (alignment.list.a || !role.major) return;

    Transform &mj = *role.major->T;
    auto &L = alignment.list;
    if (L.m) role.offset.x = 0.5f * mj.w - Mid->T->w / 2 + alignment.offset.x - Mid->T->x + T->x;
    if (L.c) role.offset.y = 0.5f * mj.h - Mid->T->h / 2 + alignment.offset.y - Mid->T->y + T->y;
    if (L.b) role.offset.y = L.i ? alignment.offset.y + mj.h - T->h : alignment.offset.y + mj.h;
    if (L.r) role.offset.x = L.i ? alignment.offset.x + mj.w - T->w : alignment.offset.x + mj.w;
    if (L.t) role.offset.y = L.i ? alignment.offset.y : alignment.offset.y - T->h;
    if (L.l) role.offset.x = L.i ? alignment.offset.x : alignment.offset.x - T->w;

    T->x = mj.x + role.offset.x;
    T->y = mj.y + role.offset.y;
    alignment.prev_offset = alignment.offset;
}

void Moveable::hard_set_T(float x, float y, float w, float h) {
    T->x = x; T->y = y; T->w = w; T->h = h;
    velocity.x = velocity.y = velocity.r = velocity.scale = 0;
    VT.x = x; VT.y = y; VT.w = w; VT.h = h;
    VT.r = T->r;
    VT.scale = T->scale;
    calculate_parrallax();
}

void Moveable::hard_set_VT() {
    VT.x = T->x; VT.y = T->y; VT.w = T->w; VT.h = T->h;
}

void Moveable::drag(const XY *off) {
    if (states.drag.can || off) {
        float s = G.TILESCALE * G.TILESIZE;
        XY p{G.CONTROLLER->cursor_position.x / s, G.CONTROLLER->cursor_position.y / s};
        Transform &c = *container->T;
        point_translate(p, {-c.w / 2, -c.h / 2});
        point_rotate(p, c.r);
        point_translate(p, {c.w / 2 - c.x, c.h / 2 - c.y});
        const XY &o = off ? *off : click_offset;
        T->x = p.x - o.x;
        T->y = p.y - o.y;
        NEW_ALIGNMENT = true;
        XY oo = o;
        children.each([&](Str *, Node *ch) { ch->drag(&oo); });
    }
    if (states.drag.can) Node::drag(nullptr);
}

void Moveable::juice_up(float amount, const float *rot_amt) {
    if (G.reduced_motion()) return;
    double end = G.TIMERS.REAL + 0.4, start = G.TIMERS.REAL;
    has_juice = true;
    juice = Juice{};
    juice.scale_amt = amount;
    juice.r_amt = rot_amt ? *rot_amt : pseudorandom_element_of2(0.6f * amount, -0.6f * amount);
    juice.start_time = start;
    juice.end_time = end;
    VT.scale = 1 - 0.6f * amount;
}

void Moveable::move_juice(double) {
    if (has_juice && !juice.handled_elsewhere) {
        if (juice.end_time < G.TIMERS.REAL) {
            has_juice = false;
        } else {
            double t = G.TIMERS.REAL - juice.start_time;
            double rem = std::max(0.0, (juice.end_time - G.TIMERS.REAL) / (juice.end_time - juice.start_time));
            juice.scale = (float)(juice.scale_amt * std::sin(50.8 * t) * rem * rem * rem);
            juice.r = (float)(juice.r_amt * std::sin(40.8 * t) * rem * rem);
        }
    }
}

void Moveable::move(double dt) {
    if (FRAME.MOVE >= G.FRAMES.MOVE) return;
    frame_major_valid = false;  // FRAME.OLD_MAJOR = FRAME.MAJOR; FRAME.MAJOR = nil
    FRAME.MOVE = G.FRAMES.MOVE;
    if (!created_on_pause && G.paused()) return;

    align_to_major();
    CALCING = false;
    if (role.role_type == RoleType::Glued) {
        if (role.major) glue_to_major(role.major.get());
    } else if (role.role_type == RoleType::Minor && role.major) {
        if (role.major->FRAME.MOVE < G.FRAMES.MOVE) role.major->move(dt);
        STATIONARY = role.major->STATIONARY;
        if (!STATIONARY || NEW_ALIGNMENT || refresh_movement || has_juice || role.xy_bond == Bond::Weak ||
            role.r_bond == Bond::Weak) {
            CALCING = true;
            move_with_major(dt);
        }
    } else if (role.role_type == RoleType::Major) {
        STATIONARY = true;
        move_juice(dt);
        move_xy(dt);
        move_r(dt, {velocity.x, velocity.y});
        move_scale(dt);
        move_wh(dt);
        calculate_parrallax();
    }
    if (alignment.lr_clamp) lr_clamp();
    NEW_ALIGNMENT = false;
}

void Moveable::lr_clamp() {
    float rw = G.ROOM->T->w;
    if (T->x < 0) T->x = 0;
    if (VT.x < 0) VT.x = 0;
    if (T->x + T->w > rw) T->x = rw - T->w;
    if (VT.x + VT.w > rw) VT.x = rw - VT.w;
}

void Moveable::glue_to_major(Moveable *m) {
    T = m->T;
    VT.x = m->VT.x + (0.5f * (1 - m->VT.w / m->T->w) * T->w);
    VT.y = m->VT.y;
    VT.w = m->VT.w;
    VT.h = m->VT.h;
    VT.r = m->VT.r;
    VT.scale = m->VT.scale;
    pinch = m->pinch;
    shadow_parrallax = m->shadow_parrallax;
}

void Moveable::move_with_major(double dt) {
    if (role.role_type != RoleType::Minor) return;
    MajorInfo mt = role.major->get_major();
    move_juice(dt);

    XY ro;
    if (role.r_bond == Bond::Weak) {
        ro = {role.offset.x + mt.offset.x, role.offset.y + mt.offset.y};
    } else if (mt.major->VT.r < 0.0001f && mt.major->VT.r > -0.0001f) {
        ro = {role.offset.x + mt.offset.x, role.offset.y + mt.offset.y};
    } else {
        float cs = std::cos(mt.major->VT.r), sn = std::sin(mt.major->VT.r);
        float ww = -T->w / 2 + mt.major->T->w / 2, hh = -T->h / 2 + mt.major->T->h / 2;
        float ox = role.offset.x + mt.offset.x - ww, oy = role.offset.y + mt.offset.y - hh;
        ro = {ox * cs - oy * sn + ww, ox * sn + oy * cs + hh};
    }

    T->x = mt.major->T->x + ro.x;
    T->y = mt.major->T->y + ro.y;

    if (role.xy_bond == Bond::Strong) {
        VT.x = mt.major->VT.x + ro.x;
        VT.y = mt.major->VT.y + ro.y;
    } else {
        move_xy(dt);
    }

    if (role.r_bond == Bond::Strong) VT.r = T->r + mt.major->VT.r + (has_juice ? juice.r : 0);
    else move_r(dt, {velocity.x, velocity.y});

    if (role.scale_bond == Bond::Strong)
        VT.scale = T->scale * (mt.major->VT.scale / mt.major->T->scale) + (has_juice ? juice.scale : 0);
    else move_scale(dt);

    if (role.wh_bond == Bond::Strong) {
        VT.x = VT.x + (0.5f * (1 - mt.major->VT.w / mt.major->T->w) * T->w);
        VT.w = T->w * (mt.major->VT.w / mt.major->T->w);
        VT.h = T->h * (mt.major->VT.h / mt.major->T->h);
    } else {
        move_wh(dt);
    }
    calculate_parrallax();
}

void Moveable::move_xy(double dt) {
    if ((T->x != VT.x || std::fabs(velocity.x) > 0.01f) || (T->y != VT.y || std::fabs(velocity.y) > 0.01f)) {
        double e = G.exp_times.xy;
        velocity.x = (float)(e * velocity.x + (1 - e) * (T->x - VT.x) * 35 * dt);
        velocity.y = (float)(e * velocity.y + (1 - e) * (T->y - VT.y) * 35 * dt);
        double mv = G.exp_times.max_vel;
        if (velocity.x * velocity.x + velocity.y * velocity.y > mv * mv) {
            float actual = std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y);
            velocity.x = (float)(mv * velocity.x / actual);
            velocity.y = (float)(mv * velocity.y / actual);
        }
        STATIONARY = false;
        VT.x += velocity.x;
        VT.y += velocity.y;
        if (std::fabs(VT.x - T->x) < 0.01f && std::fabs(velocity.x) < 0.01f) { VT.x = T->x; velocity.x = 0; }
        if (std::fabs(VT.y - T->y) < 0.01f && std::fabs(velocity.y) < 0.01f) { VT.y = T->y; velocity.y = 0; }
    }
}

void Moveable::move_scale(double) {
    float des = T->scale + (zoom ? ((states.drag.is ? 0.1f : 0) + (states.hover.is ? 0.05f : 0)) : 0) +
                (has_juice ? juice.scale : 0);
    if (des != VT.scale || std::fabs(velocity.scale) > 0.001f) {
        STATIONARY = false;
        double e = G.exp_times.scale;
        velocity.scale = (float)(e * velocity.scale + (1 - e) * (des - VT.scale));
        VT.scale += velocity.scale;
    }
}

void Moveable::move_wh(double dt) {
    if ((T->w != VT.w && !pinch->x) || (T->h != VT.h && !pinch->y) || (VT.w > 0 && pinch->x) || (VT.h > 0 && pinch->y)) {
        STATIONARY = false;
        VT.w = (float)(VT.w + (8 * dt) * (pinch->x ? -1 : 1) * T->w);
        VT.h = (float)(VT.h + (8 * dt) * (pinch->y ? -1 : 1) * T->h);
        VT.w = std::max(std::min(VT.w, T->w), 0.0f);
        VT.h = std::max(std::min(VT.h, T->h), 0.0f);
    }
}

void Moveable::move_r(double dt, const XY &vel) {
    float des = (float)(T->r + 0.015 * vel.x / dt + (has_juice ? juice.r * 2 : 0));
    if (des != VT.r || std::fabs(velocity.r) > 0.001f) {
        STATIONARY = false;
        double e = G.exp_times.r;
        velocity.r = (float)(e * velocity.r + (1 - e) * (des - VT.r));
        VT.r += velocity.r;
    }
    if (std::fabs(VT.r - T->r) < 0.001f && std::fabs(velocity.r) < 0.001f) { VT.r = T->r; velocity.r = 0; }
}

void Moveable::calculate_parrallax() {
    if (!G.ROOM) return;
    float rw = G.ROOM->T->w;
    shadow_parrallax->x = (T->x + T->w / 2 - rw / 2) / (rw / 2) * 1.5f;
}

void Moveable::set_role(const RoleArgs &a) {
    // (the original also ignores args.major that cannot set_role; every Moveable can)
    Role r;
    r.role_type = a.role_type ? *a.role_type : role.role_type;
    r.offset = a.offset ? *a.offset : role.offset;
    r.major = a.major ? Ref<Moveable>(a.major) : role.major;
    r.xy_bond = a.xy_bond ? *a.xy_bond : role.xy_bond;
    r.wh_bond = a.wh_bond ? *a.wh_bond : role.wh_bond;
    r.r_bond = a.r_bond ? *a.r_bond : role.r_bond;
    r.scale_bond = a.scale_bond ? *a.scale_bond : role.scale_bond;
    r.draw_major = a.draw_major ? a.draw_major : role.draw_major;
    role = r;
    if (role.role_type == RoleType::Major) role.major = nullptr;
}

MajorInfo Moveable::get_major() {
    if (role.role_type != RoleType::Major && role.major.get() != this && role.xy_bond != Bond::Weak &&
        role.r_bond != Bond::Weak) {
        if (!frame_major_valid || G.REFRESH_FRAME_MAJOR_CACHE) {
            MajorInfo m = role.major->get_major();
            frame_major.major = m.major;
            const XY *l = lp();
            frame_major.offset = {m.offset.x + role.offset.x + (l ? l->x : 0), m.offset.y + role.offset.y + (l ? l->y : 0)};
            frame_major_valid = true;
        }
        return frame_major;
    }
    return MajorInfo{this, {0, 0}};
}

void Moveable::remove() {
    G.MOVEABLES.remove(this);
    G.I.MOVEABLE.remove(this);
    Node::remove();
    // break the parent <-> child reference cycles the Lua GC would have collected. Other majors are
    // kept: a removed major is still moved by its minors (Moveable:move), e.g. the cash out button
    // keeps following G.round_eval down off screen after it is removed.
    // Only free-standing UIBoxes keep theirs (their majors do not reference them back).
    if (role.major) {
        bool owned = !dynamic_cast<UIBox *>(this) || parent == role.major.get();
        role.major->children.each([&](Str *, Node *c) { owned |= c == this; });
        if (owned) role.major = nullptr;
    }
}

// ------------------------------------------------------------------ misc helpers
void point_translate(XY &p, const XY &d) {
    p.x = p.x + d.x;
    p.y = p.y + d.y;
}

void point_rotate(XY &p, float angle) {
    float cs = std::cos(angle + (float)PI / 2), sn = std::sin(angle + (float)PI / 2);
    float ox = p.x, oy = p.y;
    p.x = -oy * cs + ox * sn;
    p.y = oy * sn + ox * cs;
}

// functions/misc_functions.lua prep_draw()
void prep_draw(Moveable *m, float scale, float rotate, const XY *off, bool force_rotate) {
    gfx::push();
    gfx::scale(G.TILESCALE * G.TILESIZE);
    // (moveable.layered_parallax and .x) or (parent.layered_parallax and .x) or 0
    const XY *l = m->lp();
    if (!l) {
        const Moveable *pm = dynamic_cast<const Moveable *>(m->parent);
        l = pm ? pm->lp() : nullptr;
    }
    float lpx = l ? l->x : 0, lpy = l ? l->y : 0;
    gfx::translate(m->VT.x + m->VT.w / 2 + (off ? off->x : 0) + lpx, m->VT.y + m->VT.h / 2 + (off ? off->y : 0) + lpy);
    if (m->VT.r != 0 || m->has_juice || force_rotate) gfx::rotate(m->VT.r + rotate);
    gfx::translate(-scale * m->VT.w * m->VT.scale / 2, -scale * m->VT.h * m->VT.scale / 2);
    gfx::scale(m->VT.scale * scale);
}

void add_to_drawhash(Node *n) {
    if (!n) return;
    n->zoomed = G.drawing_zoomed;
    G.DRAW_HASH.push_back(NodeRef(n));
}

}  // namespace vl
