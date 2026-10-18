#pragma once
// engine/event.lua: timed/conditional/eased callbacks queued by the game.
#include <functional>
#include <string>
#include <vector>

#include "core/object.h"
#include "core/value.h"

namespace vl {

enum class Trigger : uint8_t { Immediate, After, Ease, Condition, Before };
enum class EventTimer : uint8_t { TOTAL, REAL, REAL_SHADER, UPTIME, BACKGROUND };
enum class EaseType : uint8_t { Lerp, Elastic, Quad };

// A value an 'ease' or 'condition' event reads/writes: a table field or a C++ variable.
struct ValueRef {
    TableRef table;
    Value key;
    float *f = nullptr;
    double *d = nullptr;
    Ref<RefCounted> keep_alive;  // owner of f/d, if any

    static ValueRef field(const TableRef &t, const Value &k) { ValueRef r; r.table = t; r.key = k; return r; }
    static ValueRef of(float *p, RefCounted *owner = nullptr) { ValueRef r; r.f = p; r.keep_alive = owner; return r; }
    static ValueRef of(double *p, RefCounted *owner = nullptr) { ValueRef r; r.d = p; r.keep_alive = owner; return r; }
    Value get() const;
    void set(const Value &v) const;
};

struct EventConfig {
    Trigger trigger = Trigger::Immediate;
    bool blocking = true, blockable = true;
    bool start_timer = false;
    double delay = 0;
    bool no_delete = false;
    bool pause_force = false;
    bool has_timer = false;
    EventTimer timer = EventTimer::TOTAL;
    // immediate/after/before/condition: return true when done
    std::function<bool()> func;
    // ease
    EaseType ease = EaseType::Lerp;
    ValueRef ref;
    double ease_to = 0;
    std::function<double(double)> ease_func;  // transforms the eased value
    // condition
    Value stop_val;
};

class Event : public RefCounted {
public:
    explicit Event(EventConfig c);

    struct Results {
        bool blocking = false, completed = false, time_done = false, pause_skip = false;
    };
    void handle(Results &r);

    Trigger trigger;
    bool blocking, blockable, complete = false, start_timer;
    double delay;
    bool no_delete;
    bool created_on_pause;
    EventTimer timer;
    double time = 0;
    std::function<bool()> func;
    // ease
    struct {
        EaseType type;
        ValueRef ref;
        double start_val = 0, end_val = 0, start_time = 0, end_time = 0;
        bool started = false;
    } ease;
    std::function<double(double)> ease_func;
    // condition
    ValueRef cond_ref;
    Value stop_val;
};
using EventRef = Ref<Event>;

class EventManager {
public:
    enum Queue { UNLOCK, BASE, TUTORIAL, ACHIEVEMENT, OTHER, QUEUE_COUNT };
    EventManager();
    void add_event(Event *e, Queue q = BASE, bool front = false);
    void add_event(EventConfig c, Queue q = BASE, bool front = false) { add_event(new Event(std::move(c)), q, front); }
    void clear_queue(int queue = -1, int exception = -1);
    void update(double dt, bool forced = false);
    std::vector<EventRef> queues[QUEUE_COUNT];

private:
    double queue_timer, queue_dt, queue_last_processed;
};

double &timer_ref(EventTimer t);

// Shorthands used throughout the port: G.E_MANAGER:add_event(Event({...}))
EventConfig ev_immediate(std::function<bool()> f);
EventConfig ev_after(double delay, std::function<bool()> f);

}  // namespace vl
