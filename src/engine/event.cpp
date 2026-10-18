// engine/event.lua
#include "engine/event.h"

#include <cmath>

#include "core/math.h"

#include "game/g.h"

namespace vl {

double &timer_ref(EventTimer t) {
    switch (t) {
    case EventTimer::REAL: return G.TIMERS.REAL;
    case EventTimer::REAL_SHADER: return G.TIMERS.REAL_SHADER;
    case EventTimer::UPTIME: return G.TIMERS.UPTIME;
    case EventTimer::BACKGROUND: return G.TIMERS.BACKGROUND;
    default: return G.TIMERS.TOTAL;
    }
}

Value ValueRef::get() const {
    if (f) return Value((double)*f);
    if (d) return Value(*d);
    if (table) return table->get(key);
    return Value();
}

void ValueRef::set(const Value &v) const {
    if (f) *f = (float)v.num();
    else if (d) *d = v.num();
    else if (table) table->set(key, v);
}

Event::Event(EventConfig c) {
    trigger = c.trigger;
    blocking = c.blocking;
    blockable = c.blockable;
    start_timer = c.start_timer;
    func = c.func ? std::move(c.func) : []() { return true; };
    delay = c.delay;
    no_delete = c.no_delete;
    created_on_pause = c.pause_force || G.paused();
    timer = c.has_timer ? c.timer : (created_on_pause ? EventTimer::REAL : EventTimer::TOTAL);
    if (trigger == Trigger::Ease) {
        ease.type = c.ease;
        ease.ref = c.ref;
        ease.start_val = c.ref.get().num();
        ease.end_val = c.ease_to;
        ease_func = c.ease_func ? std::move(c.ease_func) : [](double t) { return t; };
    }
    if (trigger == Trigger::Condition) {
        cond_ref = c.ref;
        stop_val = c.stop_val;
        if (!c.func) {
            ValueRef r = cond_ref;
            Value sv = stop_val;
            func = [r, sv]() { return r.get() == sv; };
        }
    }
    time = timer_ref(timer);
}

void Event::handle(Results &r) {
    r.blocking = blocking;
    r.completed = complete;
    if (!created_on_pause && G.paused()) {
        r.pause_skip = true;
        return;
    }
    double now = timer_ref(timer);
    if (!start_timer) {
        time = now;
        start_timer = true;
    }
    if (trigger == Trigger::After) {
        if (time + delay <= now) {
            r.time_done = true;
            r.completed = func();
        }
    }
    if (trigger == Trigger::Ease) {
        if (!ease.started) {
            ease.started = true;
            ease.start_time = now;
            ease.end_time = now + delay;
            ease.start_val = ease.ref.get().num();
        }
        if (!complete) {
            if (ease.end_time >= now) {
                double pd = (ease.end_time - now) / (ease.end_time - ease.start_time);
                if (ease.type == EaseType::Elastic)
                    pd = -std::pow(2.0, 10 * pd - 10) * std::sin((pd * 10 - 10.75) * 2 * PI / 3);
                else if (ease.type == EaseType::Quad)
                    pd = pd * pd;
                ease.ref.set(Value(ease_func(pd * ease.start_val + (1 - pd) * ease.end_val)));
            } else {
                ease.ref.set(Value(ease_func(ease.end_val)));
                complete = true;
                r.completed = true;
                r.time_done = true;
            }
        }
    }
    if (trigger == Trigger::Condition) {
        if (!complete) r.completed = func();
        r.time_done = true;
    }
    if (trigger == Trigger::Before) {
        if (!complete) r.completed = func();
        if (time + delay <= now) r.time_done = true;
    }
    if (trigger == Trigger::Immediate) {
        r.completed = func();
        r.time_done = true;
    }
    if (r.completed) complete = true;
}

EventManager::EventManager() {
    queue_timer = G.TIMERS.REAL;
    queue_dt = 1.0 / 60;
    queue_last_processed = G.TIMERS.REAL;
}

void EventManager::add_event(Event *e, Queue q, bool front) {
    if (front) queues[q].insert(queues[q].begin(), EventRef(e));
    else queues[q].push_back(EventRef(e));
}

void EventManager::clear_queue(int queue, int exception) {
    auto clear = [](std::vector<EventRef> &v) {
        size_t i = 0;
        while (i < v.size()) {
            if (!v[i]->no_delete) v.erase(v.begin() + i);
            else i++;
        }
    };
    if (queue < 0) {
        for (int k = 0; k < QUEUE_COUNT; k++)
            if (exception < 0 || k != exception) clear(queues[k]);
    } else {
        clear(queues[queue]);
    }
}

void EventManager::update(double dt, bool forced) {
    queue_timer += dt;
    if (queue_timer >= queue_last_processed + queue_dt || forced) {
        queue_last_processed += forced ? 0 : queue_dt;
        for (int k = 0; k < QUEUE_COUNT; k++) {
            auto &v = queues[k];
            bool blocked = false;
            size_t i = 0;
            while (i < v.size()) {
                EventRef e = v[i];  // keep alive while handled
                Event::Results r;
                if (!blocked || !e->blockable) e->handle(r);
                if (r.pause_skip) {
                    i++;
                } else {
                    if (!blocked && r.blocking) blocked = true;
                    if (r.completed && r.time_done) {
                        // table.remove(v, i): by index, like the original (a no-op when out of range)
                        if (i < v.size()) v.erase(v.begin() + i);
                    } else {
                        i++;
                    }
                }
            }
        }
    }
}

EventConfig ev_immediate(std::function<bool()> f) {
    EventConfig c;
    c.func = std::move(f);
    return c;
}

EventConfig ev_after(double delay, std::function<bool()> f) {
    EventConfig c;
    c.trigger = Trigger::After;
    c.delay = delay;
    c.func = std::move(f);
    return c;
}

}  // namespace vl
