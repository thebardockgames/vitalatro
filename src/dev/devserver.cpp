#include "dev/devserver.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <sstream>
#include <string>
#include <vector>

#include "core/time.h"
#include "game/card_factory.h"
#include "game/g.h"
#include "game/run.h"
#include "game/state_events.h"
#include "gfx/backend.h"
#include "gfx/gfx.h"

namespace vl::dev {
namespace {

bool g_running = false;

// ------------------------------------------------------------------ requests
struct Request {
    std::string cmd;
    std::vector<std::string> args;
    std::string payload;
    bool ok = true;
    std::string reply;
    platform::Semaphore done;
};

platform::Mutex g_queue_mutex;
std::deque<Request *> g_queue;

// the command that answers frames later (one at a time: the client waits for each answer)
Request *g_pending = nullptr;
enum class Wait { None, Frames, Perf, Shot, Bench, Input } g_wait = Wait::None;
int g_wait_frames = 0;
double g_wait_secs = 0;

void finish(Request *r, bool ok, std::string reply) {
    r->ok = ok;
    r->reply = std::move(reply);
    r->done.post();
}

// ------------------------------------------------------------------ log capture
platform::Mutex g_log_mutex;
std::deque<std::string> g_log_lines;  // since the last `log` command (bounded)
std::string g_bench_capture;
bool g_capture_bench = false;

void log_tap(const char *line) {
    g_log_mutex.lock();
    g_log_lines.emplace_back(line);
    if (g_log_lines.size() > 2000) g_log_lines.pop_front();
    if (g_capture_bench) {
        g_bench_capture += line;
        g_bench_capture += '\n';
    }
    g_log_mutex.unlock();
}

// ------------------------------------------------------------------ network thread
bool recv_exact(int s, char *buf, size_t n) {
    while (n > 0) {
        int r = platform::tcp_recv(s, buf, (int)std::min<size_t>(n, 1 << 16));
        if (r <= 0) return false;
        buf += r;
        n -= (size_t)r;
    }
    return true;
}

bool recv_line(int s, std::string &line) {
    line.clear();
    char c;
    while (line.size() < 4096) {
        if (platform::tcp_recv(s, &c, 1) <= 0) return false;
        if (c == '\n') return true;
        line += c;
    }
    return false;
}

void serve_client(int c) {
    std::string header;
    while (recv_line(c, header)) {
        std::istringstream ss(header);
        size_t len = 0;
        Request *r = new Request();
        ss >> len >> r->cmd;
        std::string a;
        while (ss >> a) r->args.push_back(a);
        if (len > 64u * 1024 * 1024) break;
        r->payload.resize(len);
        if (len && !recv_exact(c, &r->payload[0], len)) {
            delete r;
            break;
        }
        g_queue_mutex.lock();
        g_queue.push_back(r);
        g_queue_mutex.unlock();
        r->done.wait();
        std::string head = std::string(r->ok ? "OK " : "ERR ") + std::to_string(r->reply.size()) + "\n";
        bool sent = platform::tcp_send_all(c, head.data(), head.size()) && platform::tcp_send_all(c, r->reply.data(), r->reply.size());
        delete r;
        if (!sent) break;
    }
    platform::tcp_close(c);
}

void server_loop() {
    int l = -1;
    for (int tries = 0; l < 0; tries++) {
        l = platform::tcp_listen(kPort);
        if (l < 0) {
            if (tries == 0) platform::log("dev: cannot listen on %d yet, retrying", kPort);
            time_sleep(1);
        }
    }
    platform::log("dev: listening on %s:%d", platform::local_ip().c_str(), kPort);
    for (;;) {
        int c = platform::tcp_accept(l);
        if (c < 0) {
            time_sleep(0.2);
            continue;
        }
        serve_client(c);  // one client at a time
    }
}

// ------------------------------------------------------------------ scripted input
struct InputEvent {
    long frame;
    uint32_t button;
    bool touch;
    float x, y;
};
std::vector<InputEvent> g_inputs;
long g_frame = 0;
long g_input_end = -1;

std::string parse_inputs(const std::vector<std::string> &args) {
    static const std::pair<const char *, uint32_t> names[] = {
        {"up", platform::BTN_UP},         {"down", platform::BTN_DOWN},     {"left", platform::BTN_LEFT},
        {"right", platform::BTN_RIGHT},   {"cross", platform::BTN_CROSS},   {"circle", platform::BTN_CIRCLE},
        {"square", platform::BTN_SQUARE}, {"triangle", platform::BTN_TRIANGLE}, {"l", platform::BTN_L},
        {"r", platform::BTN_R},           {"start", platform::BTN_START},   {"select", platform::BTN_SELECT}};
    std::vector<InputEvent> evs;
    for (const std::string &tok : args) {
        // <frames from now>:<button>  or  <frames from now>:touch:<x>:<y> (screen pixels)
        size_t c1 = tok.find(':');
        if (c1 == std::string::npos) return "bad input token " + tok;
        InputEvent ev{g_frame + 1 + std::atol(tok.substr(0, c1).c_str()), 0, false, 0, 0};
        std::string rest = tok.substr(c1 + 1);
        if (rest.compare(0, 6, "touch:") == 0) {
            ev.touch = true;
            if (std::sscanf(rest.c_str() + 6, "%f:%f", &ev.x, &ev.y) != 2) return "bad touch " + tok;
        } else {
            for (auto &n : names)
                if (rest == n.first) ev.button = n.second;
            if (!ev.button) return "unknown button " + rest;
        }
        evs.push_back(ev);
    }
    long end = g_frame;
    for (auto &e : evs) end = std::max(end, e.frame + 6);
    g_inputs.insert(g_inputs.end(), evs.begin(), evs.end());
    g_input_end = std::max(g_input_end, end);
    return "";
}

void inject(platform::Input &in) {
    for (const InputEvent &ev : g_inputs) {
        if (g_frame < ev.frame || g_frame >= ev.frame + 6) continue;  // held 6 frames, like VL_INPUT
        if (ev.touch) {
            in.touching = true;
            in.touch_x = ev.x;
            in.touch_y = ev.y;
        } else {
            in.buttons |= ev.button;
        }
    }
    g_inputs.erase(std::remove_if(g_inputs.begin(), g_inputs.end(), [](const InputEvent &e) { return g_frame >= e.frame + 6; }),
                   g_inputs.end());
}

// ------------------------------------------------------------------ perf sampling
struct Perf {
    int frames = 0, slow = 0;
    double t = 0, dt_max = 0, upd = 0, upd_max = 0, draw = 0, draw_max = 0, submit = 0, submit_max = 0, wgpu = 0, wgpu_max = 0, wq = 0, wq_max = 0;
    gfx::FrameStats worst{};
} g_perf;

std::string gpu_line(const gfx::FrameStats &fs) {
    int order[(int)gfx::ShaderId::Count];
    float total = 0;
    for (int i = 0; i < (int)gfx::ShaderId::Count; i++) {
        order[i] = i;
        total += gfx::gpu_estimate_ms(fs, i);
    }
    std::sort(order, order + (int)gfx::ShaderId::Count, [&](int a, int b) { return gfx::gpu_estimate_ms(fs, a) > gfx::gpu_estimate_ms(fs, b); });
    char buf[512];
    int n = std::snprintf(buf, sizeof buf, "gpu est %.1f ms, %d draws:", total, fs.draw_calls);
    for (int k = 0; k < 8 && n < (int)sizeof buf; k++) {
        int i = order[k];
        if (fs.px[i] <= 0) break;
        n += std::snprintf(buf + n, sizeof buf - n, " %s %.0fk/%.2f", gfx::shader_info((gfx::ShaderId)i).name, fs.px[i] / 1000,
                           gfx::gpu_estimate_ms(fs, i));
    }
    return buf;
}

std::string perf_report() {
    const Perf &p = g_perf;
    const int n = std::max(1, p.frames);
    int ram, cdram;
    platform::free_memory_mb(ram, cdram);
    char buf[1024];
    std::snprintf(buf, sizeof buf,
                  "%.1f fps over %.2f s (%d frames, %d over 17.5 ms), worst %.1f ms\n"
                  "cpu update %.2f (max %.2f) draw %.2f (max %.2f) submit %.2f (max %.2f) ms\n"
                  "blocked on gpu %.2f (max %.2f) display queue %.2f (max %.2f) ms\n"
                  "state %d stage %d | free MB ram %d cdram %d\n",
                  p.frames / std::max(1e-9, p.t), p.t, p.frames, p.slow, p.dt_max * 1000, p.upd / n, p.upd_max, p.draw / n, p.draw_max,
                  p.submit / n, p.submit_max, p.wgpu / n, p.wgpu_max, p.wq / n, p.wq_max, G.STATE, G.STAGE, ram, cdram);
    char dp[256];
    const double *d = G.draw_prof;
    std::snprintf(dp, sizeof dp, "draw: background %.2f nodes %.2f uiboxes %.2f areas+cards %.2f texts %.2f overlay %.2f ms\n", d[0] / n, d[1] / n, d[2] / n,
                  d[3] / n, d[4] / n, d[5] / n);
    char up[200];
    std::snprintf(up, sizeof up, "ui draw_self: text %.2f ms (%d) boxes %.2f ms (%d) objects %.2f ms (%d) per frame\n", G.ui_prof[0] / n, G.ui_prof_n[0] / n,
                  G.ui_prof[1] / n, G.ui_prof_n[1] / n, G.ui_prof[2] / n, G.ui_prof_n[2] / n);
    return std::string(buf) + dp + up + "last frame " + gpu_line(gfx::last_frame_stats()) + "\nworst frame " + gpu_line(p.worst) + "\n";
}

// ------------------------------------------------------------------ commands
Value parse_value(const std::string &s) {
    if (s == "true") return Value(true);
    if (s == "false") return Value(false);
    if (s == "nil") return Value();
    char *end = nullptr;
    double d = std::strtod(s.c_str(), &end);
    if (end && *end == 0 && !s.empty()) return Value(d);
    return Value(s);
}

std::string join(const std::vector<std::string> &v, size_t from) {
    std::string r;
    for (size_t i = from; i < v.size(); i++) r += (i > from ? " " : "") + v[i];
    return r;
}

void run_command(Request *r) {
    const std::string &c = r->cmd;
    const auto &a = r->args;
    auto arg = [&](size_t i, const char *def = "") { return i < a.size() ? a[i] : std::string(def); };

    if (c == "ping") {
        finish(r, true, "Vitalatro dev " + G.VERSION + " " + platform::local_ip() + " frame " + std::to_string(g_frame));
    } else if (c == "state") {
        char buf[256];
        std::snprintf(buf, sizeof buf, "frame %ld state %d stage %d real %.2f total %.2f overlay %d", g_frame, G.STATE, G.STAGE, G.TIMERS.REAL,
                      G.TIMERS.TOTAL, G.OVERLAY_MENU ? 1 : 0);
        finish(r, true, buf);
    } else if (c == "perf") {
        g_perf = Perf{};
        for (double &v : G.draw_prof) v = 0;
        for (int i = 0; i < 3; i++) G.ui_prof[i] = 0, G.ui_prof_n[i] = 0;
        g_wait_secs = a.empty() ? 2.0 : std::atof(a[0].c_str());
        g_pending = r;
        g_wait = Wait::Perf;
    } else if (c == "wait") {
        g_wait_frames = std::max(1, std::atoi(arg(0, "1").c_str()));
        g_pending = r;
        g_wait = Wait::Frames;
    } else if (c == "shot") {
        g_pending = r;
        g_wait = Wait::Shot;
    } else if (c == "input") {
        std::string err = parse_inputs(a);
        if (!err.empty()) return finish(r, false, err);
        g_pending = r;
        g_wait = Wait::Input;
    } else if (c == "bench") {
        g_log_mutex.lock();
        g_bench_capture.clear();
        g_capture_bench = true;
        g_log_mutex.unlock();
        start_gpu_bench();
        g_pending = r;
        g_wait = Wait::Bench;
    } else if (c == "set") {
        if (a.size() < 2) return finish(r, false, "set <key> <value>");
        const std::string &k = a[0];
        if (k == "vsync") gfx::set_vsync(a[1] != "0" && a[1] != "false" && a[1] != "off");
        else if (k == "bench") G.vl_bench = std::atoi(a[1].c_str());
        else if (k == "occlusion") gfx::g_occlusion = a[1] != "0" && a[1] != "false";
        else G.SETTINGS->set(Value(k), parse_value(join(a, 1)));
        finish(r, true, "");
    } else if (c == "get") {
        if (a.empty()) return finish(r, false, "get <setting>");
        finish(r, true, G.SETTINGS->get(Value(a[0])).to_string());
    } else if (c == "run") {
        // a seeded run, as VL_RUN does on PC: G.FUNCS.start_run(nil, {seed = seed, stake = 1})
        TableRef args = vl::T({{"stake", a.size() > 1 ? std::atof(a[1].c_str()) : 1.0}});
        if (!a.empty()) args->set("seed", Value(a[0]));
        // run <seed> <stake> <deck key>: that deck, as picked on the run setup screen
        if (a.size() > 2 && G.P_CENTERS->get(Value(a[2])).truthy())
            G.GAME->set("viewed_back", Value((Object *)new Back(G.P_CENTERS->get(Value(a[2])))));
        start_run_with(nullptr, args);
        finish(r, true, "");
    } else if (c == "joker") {
        // joker <key>[,<key>...]: as the tests' VL_JOKERS
        if (!G.jokers) return finish(r, false, "no run");
        add_jokers(join(a, 0));
        finish(r, true, "");
    } else if (c == "skip") {
        // skip [shader...]: the GPU stops running those shaders' draws (no args: run them all again)
        uint64_t mask = 0;
        for (const std::string &name : a) {
            int found = -1;
            for (int i = 0; i < (int)gfx::ShaderId::Count; i++)
                if (name == gfx::shader_info((gfx::ShaderId)i).name) found = i;
            if (found < 0) return finish(r, false, "unknown shader " + name);
            mask |= 1ull << found;
        }
        gfx::g_skip_shaders = mask;
        finish(r, true, "");
    } else if (c == "combo") {
        // combo [hand size delta]: the naneinf stress test (combo_setup), before drawing the hand
        if (!G.jokers) return finish(r, false, "no run");
        combo_setup(a.empty() ? 44 : std::atoi(a[0].c_str()));
        finish(r, true, "");
    } else if (c == "menu") {
        G.FUNCS["go_to_menu"](nullptr);
        finish(r, true, "");
    } else if (c == "tutorial") {
        // G.FUNCS.start_tutorial: a new run (replaces the profile's saved run) with the tutorial
        G.FUNCS["start_tutorial"](nullptr);
        finish(r, true, "");
    } else if (c == "debugtable") {
        G.delete_run();
        G.debug_table();
        finish(r, true, "");
    } else if (c == "log") {
        g_log_mutex.lock();
        std::string out;
        for (auto &l : g_log_lines) out += l + "\n";
        g_log_lines.clear();
        g_log_mutex.unlock();
        finish(r, true, out);
    } else if (c == "shader") {
        // payload: repeated "<name> <len>\n<bytes>"
        std::vector<std::pair<std::string, std::string>> files;
        size_t pos = 0;
        const std::string &p = r->payload;
        while (pos < p.size()) {
            size_t nl = p.find('\n', pos);
            if (nl == std::string::npos) return finish(r, false, "bad shader payload");
            std::istringstream hs(p.substr(pos, nl - pos));
            std::string name;
            size_t len = 0;
            hs >> name >> len;
            if (nl + 1 + len > p.size()) return finish(r, false, "truncated shader " + name);
            files.emplace_back(name, p.substr(nl + 1, len));
            pos = nl + 1 + len;
        }
        double t0 = time_now();
        std::string err = gfx::backend_reload_shaders(files);
        if (!err.empty()) return finish(r, false, err);
        char buf[96];
        std::snprintf(buf, sizeof buf, "reloaded %d file(s) in %.0f ms", (int)files.size(), (time_now() - t0) * 1000);
        finish(r, true, buf);
    } else if (c == "put") {
        if (a.empty()) return finish(r, false, "put <path>");
        bool ok = platform::write_any(a[0].c_str(), r->payload);
        finish(r, ok, ok ? "" : "cannot write " + a[0]);
    } else if (c == "fetch") {
        std::string data;
        if (a.empty() || !platform::read_any(a[0].c_str(), data)) return finish(r, false, "cannot read " + arg(0));
        finish(r, true, data);
    } else if (c == "restart") {
        finish(r, true, "");
        time_sleep(0.3);  // let the answer leave
        platform::restart_app();
    } else {
        finish(r, false, "unknown command " + c);
    }
}

}  // namespace

void start() {
    if (g_running) return;
    if (!platform::net_init()) {
        platform::log("dev: no network");
        return;
    }
    g_running = true;
    platform::set_log_tap(log_tap);
    platform::run_thread(server_loop);
}

bool running() { return g_running; }

void frame_begin(platform::Input &in) {
    if (!g_running) return;
    g_frame++;
    platform::keep_awake();
    inject(in);
    if (!g_pending) {
        g_queue_mutex.lock();
        Request *r = nullptr;
        if (!g_queue.empty()) {
            r = g_queue.front();
            g_queue.pop_front();
        }
        g_queue_mutex.unlock();
        if (r) run_command(r);
    }
}

void frame_end(const FrameTimes &t) {
    if (!g_running || !g_pending) return;
    Request *r = g_pending;
    auto done = [&](bool ok, std::string reply) {
        g_pending = nullptr;
        g_wait = Wait::None;
        finish(r, ok, std::move(reply));
    };
    switch (g_wait) {
    case Wait::Frames:
        if (--g_wait_frames <= 0) done(true, std::to_string(g_frame));
        break;
    case Wait::Perf: {
        Perf &p = g_perf;
        p.frames++;
        p.t += t.dt;
        if (t.dt > 0.0175) p.slow++;
        if (t.dt > p.dt_max) p.dt_max = t.dt, p.worst = gfx::last_frame_stats();
        p.upd += t.update, p.upd_max = std::max(p.upd_max, t.update);
        p.draw += t.draw, p.draw_max = std::max(p.draw_max, t.draw);
        p.submit += t.submit, p.submit_max = std::max(p.submit_max, t.submit);
        p.wgpu += gfx::g_wait_gpu_ms, p.wgpu_max = std::max(p.wgpu_max, (double)gfx::g_wait_gpu_ms);
        p.wq += gfx::g_wait_queue_ms, p.wq_max = std::max(p.wq_max, (double)gfx::g_wait_queue_ms);
        if (p.t >= g_wait_secs) done(true, perf_report());
        break;
    }
    case Wait::Shot: {
        std::vector<uint8_t> px;
        gfx::backend_read_screen(px);
        std::string head = std::to_string(gfx::screen_width()) + " " + std::to_string(gfx::screen_height()) + "\n";
        done(true, head + std::string((const char *)px.data(), px.size()));
        break;
    }
    case Wait::Input:
        if (g_frame >= g_input_end) done(true, std::to_string(g_frame));
        break;
    case Wait::Bench:
        if (!gpu_bench_running()) {
            g_log_mutex.lock();
            g_capture_bench = false;
            std::string out = g_bench_capture;
            g_log_mutex.unlock();
            done(true, out);
        }
        break;
    case Wait::None:
        break;
    }
}

}  // namespace vl::dev
