// engine/particles.lua
#include "engine/particles.h"

#include <algorithm>
#include <cmath>

#include "engine/random.h"
#include "engine/ui.h"
#include "game/g.h"

namespace vl {

namespace {
EventTimer parse_timer(const Value &v, EventTimer def) {
    if (v.equals("REAL")) return EventTimer::REAL;
    if (v.equals("TOTAL")) return EventTimer::TOTAL;
    if (v.equals("REAL_SHADER")) return EventTimer::REAL_SHADER;
    if (v.equals("UPTIME")) return EventTimer::UPTIME;
    if (v.equals("BACKGROUND")) return EventTimer::BACKGROUND;
    return def;
}
// pseudorandom_element(t) without a seed: keys 1..n in order, math.random(n)
Value random_element(const Value &list) {
    Table *t = list.table();
    if (!t || t->len() == 0) return Value();
    return t->get((int)lua_math.random((double)t->len()));
}
}  // namespace

Particles::Particles(float x, float y, float w, float h, TableRef c, bool register_instance) {
    if (!c) c = new_table();
    Moveable::init(x, y, w, h);

    fill = c->get(K("fill")).truthy();
    padding = c->get(K("padding")).is_num() ? (float)c->get(K("padding")).num() : 0;

    if (Moveable *attach = c->get(K("attach")).as<Moveable>()) {
        Bond strong = Bond::Strong;
        AlignArgs al;
        al.major = attach;
        al.type = "cm";
        al.bond = &strong;
        set_alignment(al);
        role.major->children.push(this);
        parent = role.major.get();
        T->x = role.major->T->x + padding;
        T->y = role.major->T->y + padding;
        if (fill) {
            T->w = role.major->T->w - padding;
            T->h = role.major->T->h - padding;
        }
    }

    states.hover.can = false;
    states.click.can = false;
    states.collide.can = false;
    states.drag.can = false;
    states.release_on.can = false;

    auto num = [&](const char *k, double d) {
        Value v = c->get(Value(intern(k)));
        return v.is_num() ? v.num() : d;
    };
    timer = num("timer", 0.5);
    timer_type = created_on_pause ? EventTimer::REAL : parse_timer(c->get(K("timer_type")), EventTimer::REAL);
    last_real_time = timer_ref(timer_type) - timer;
    last_drawn = 0;
    lifespan = num("lifespan", 1);
    fade_alpha = 0;
    speed = num("speed", 1);
    max = num("max", 1e15);
    pulse_max = (int)std::min(20.0, num("pulse_max", 0));
    pulsed = 0;
    vel_variation = num("vel_variation", 1);
    pscale = num("scale", 1);
    colours = c->get(K("colours")).truthy() ? c->get(K("colours")) : Value(A({G.colour("BACKGROUND")["D"]}));

    if (c->get(K("initialize")).truthy()) {
        for (int i = 1; i <= 60; i++) {
            last_real_time -= 15.0 / 60;
            update(15.0 / 60);
            move(15.0 / 60);
        }
    }
    if (register_instance) G.I.MOVEABLE.add(this);
}

void Particles::update(double) {
    double &tm = timer_ref(timer_type);
    if (G.paused() && !created_on_pause) {
        last_real_time = tm;
        return;
    }
    int added = 0;
    while (tm > last_real_time + timer && ((double)particles.size() < max || pulsed < pulse_max) && added < 20) {
        last_real_time += timer;
        XY off{fill ? (float)((0.5 - lua_math.random()) * T->w) : 0.f, 0};
        off.y = fill ? (float)((0.5 - lua_math.random()) * T->h) : 0.f;
        if (fill && T->r < 0.1f && T->r > -0.1f) {
            XY n{std::sin(T->r) * off.y + std::cos(T->r) * off.x, std::sin(T->r) * off.x + std::cos(T->r) * off.y};
            off = n;
        }
        Particle p;
        p.dir = lua_math.random() * 2 * PI;
        p.facing = lua_math.random() * 2 * PI;
        p.size = lua_math.random() * 0.5 + 0.1;
        p.velocity = speed * (vel_variation * lua_math.random() + (1 - vel_variation)) * 0.7;
        p.r_vel = 0.2 * (0.5 - lua_math.random());
        p.time = tm;
        p.colour = random_element(colours);
        p.offset = off;
        particles.push_back(std::move(p));
        added++;
        if (pulsed <= pulse_max) pulsed++;
    }
}

void Particles::move(double dt) {
    if (G.paused() && !created_on_pause) return;
    Moveable::move(dt);
    if (timer_type != EventTimer::REAL) dt *= G.SPEEDFACTOR;

    for (int i = (int)particles.size() - 1; i >= 0; i--) {
        Particle &p = particles[i];
        p.draw = true;
        if (!p.has_e_vel) {
            p.e_vel = dt * pscale;
            p.has_e_vel = true;
        }
        p.e_prev = p.e_curr;
        p.age += dt;
        p.e_curr = std::min(2 * std::min((p.age / lifespan) * pscale, pscale * ((lifespan - p.age) / lifespan)), pscale);
        p.e_vel = (p.e_curr - p.e_prev) * pscale * dt + (1 - pscale * dt) * p.e_vel;
        p.scale = p.scale + p.e_vel;
        p.scale = std::min(2 * std::min((p.age / lifespan) * pscale, pscale * ((lifespan - p.age) / lifespan)), pscale);
        if (p.scale < 0) {
            particles.erase(particles.begin() + i);
        } else {
            p.offset.x = (float)(p.offset.x + p.velocity * std::sin(p.dir) * dt);
            p.offset.y = (float)(p.offset.y + p.velocity * std::cos(p.dir) * dt);
            p.facing = p.facing + p.r_vel * dt;
            p.velocity = std::max(0.0, p.velocity - p.velocity * 0.07 * dt);
        }
    }
}

void Particles::fade(double delay, double to) {
    EventConfig c;
    c.trigger = Trigger::Ease;
    c.has_timer = true;
    c.timer = timer_type;
    c.blockable = false;
    c.blocking = false;
    c.ref = ValueRef::of(&fade_alpha, this);
    c.ease_to = to;
    c.delay = delay;
    G.E_MANAGER->add_event(std::move(c));
}

void Particles::draw(float alpha) {
    prep_draw(this, 1);
    gfx::translate(T->w / 2, T->h / 2);
    for (const Particle &p : particles) {
        if (!p.draw) continue;
        gfx::push();
        const Value &c = p.colour;
        gfx::set_color({(float)c[1].num(), (float)c[2].num(), (float)c[3].num(), (float)(c[4].num() * alpha * (1 - fade_alpha))});
        gfx::translate(p.offset.x, p.offset.y);
        gfx::rotate((float)p.facing);
        float s = (float)p.scale;
        gfx::rectangle_fill(-s / 2, -s / 2, s, s);
        gfx::pop();
    }
    gfx::pop();
    add_to_drawhash(this);
    draw_boundingrect();
}

void Particles::remove() {
    if (role.major) role.major->children.remove(this);
    remove_all(children);
    Moveable::remove();
}

}  // namespace vl
