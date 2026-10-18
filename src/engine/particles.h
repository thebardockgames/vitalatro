#pragma once
// engine/particles.lua
#include <vector>

#include "engine/event.h"
#include "engine/node.h"

namespace vl {

class Particles : public Moveable {
public:
    // config keys: fill, padding, attach, timer, timer_type, lifespan, speed, max, pulse_max,
    // vel_variation, scale, colours, initialize
    Particles(float x, float y, float w, float h, TableRef config = nullptr, bool register_instance = true);

    void update(double dt) override;
    void move(double dt) override;
    void fade(double delay, double to = 1);
    void draw() override { draw(1); }
    void draw(float alpha);
    void remove() override;

    struct Particle {
        bool draw = false;
        double dir, facing, size, age = 0, velocity, r_vel;
        double e_prev = 0, e_curr = 0, e_vel = 0;
        bool has_e_vel = false;
        double scale = 0, visible_scale = 0, time;
        Value colour;
        XY offset;
    };

    bool fill = false;
    float padding = 0;
    double timer = 0.5;
    EventTimer timer_type = EventTimer::REAL;
    double last_real_time = 0, last_drawn = 0;
    double lifespan = 1, fade_alpha = 0, speed = 1, max = 1e15;
    int pulse_max = 0, pulsed = 0;
    double vel_variation = 1, pscale = 1;  // self.scale
    Value colours;
    std::vector<Particle> particles;
};

}  // namespace vl
