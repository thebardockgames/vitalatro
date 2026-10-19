// PC (Linux/WSL) platform for the headless test build: no window, no input. Data is read from
// the directory given by VL_DATA (defaults to ./data).
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>

#include "core/time.h"
#include "platform/platform.h"

namespace vl {

double time_now() {
    using namespace std::chrono;
    static const auto t0 = steady_clock::now();
    return duration<double>(steady_clock::now() - t0).count();
}
void time_sleep(double s) {
    if (s > 0) std::this_thread::sleep_for(std::chrono::duration<double>(s));
}

namespace platform {
namespace {
std::string g_root = "data/";
std::string g_save = "save/";

bool read_whole(const std::string &p, std::string &out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}
}  // namespace

void init() {
    if (const char *d = std::getenv("VL_DATA")) g_root = std::string(d) + "/";
    if (const char *s = std::getenv("VL_SAVE")) g_save = std::string(s) + "/";
    std::error_code ec;
    std::filesystem::create_directories(g_save, ec);
}

bool read_file(const char *path, std::string &out) { return read_whole(g_root + path, out); }
bool read_save(const char *path, std::string &out) { return read_whole(g_save + path, out); }
bool write_save(const char *path, const std::string &data) {
    std::string full = g_save + path, tmp = full + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary);
        if (!f) return false;
        f << data;
        if (!f) return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, full, ec);
    return !ec;
}

bool remove_save(const char *path) {
    std::error_code ec;
    return std::filesystem::remove(g_save + path, ec);
}

void make_save_dir(const char *name) {
    std::error_code ec;
    std::filesystem::create_directories(g_save + name, ec);
}

void log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void fatal(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "FATAL: ");
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    std::exit(1);
}

// Scripted input for the headless runner: VL_INPUT="1200:cross 1260:down 1300:touch:480:300"
// presses a button (or touches a point) for 6 frames starting at the given frame.
namespace {
struct ScriptEvent {
    int frame;
    uint32_t button;
    bool touch;
    float x, y;
};
std::vector<ScriptEvent> script;
bool script_loaded = false;
int frame_counter = 0;

void load_script() {
    script_loaded = true;
    const char *e = std::getenv("VL_INPUT");
    if (!e) return;
    std::istringstream ss(e);
    std::string tok;
    static const std::pair<const char *, uint32_t> names[] = {
        {"up", BTN_UP},       {"down", BTN_DOWN},       {"left", BTN_LEFT},         {"right", BTN_RIGHT}, {"cross", BTN_CROSS},
        {"circle", BTN_CIRCLE}, {"square", BTN_SQUARE}, {"triangle", BTN_TRIANGLE}, {"l", BTN_L},         {"r", BTN_R},
        {"start", BTN_START}, {"select", BTN_SELECT}};
    while (ss >> tok) {
        ScriptEvent ev{};
        size_t c1 = tok.find(':');
        if (c1 == std::string::npos) continue;
        ev.frame = std::atoi(tok.substr(0, c1).c_str());
        std::string rest = tok.substr(c1 + 1);
        if (rest.compare(0, 6, "touch:") == 0) {
            ev.touch = true;
            std::sscanf(rest.c_str() + 6, "%f:%f", &ev.x, &ev.y);
        } else {
            for (auto &n : names)
                if (rest == n.first) ev.button = n.second;
        }
        script.push_back(ev);
    }
}
}  // namespace

void poll_input(Input &in) {
    in = Input{};
    if (!script_loaded) load_script();
    frame_counter++;
    for (const ScriptEvent &ev : script) {
        if (frame_counter < ev.frame || frame_counter >= ev.frame + 6) continue;
        if (ev.touch) {
            in.touching = true;
            in.touch_x = ev.x;
            in.touch_y = ev.y;
        } else {
            in.buttons |= ev.button;
        }
    }
}

void run_async(std::function<void()> job) { std::thread(std::move(job)).detach(); }
void run_thread(std::function<void()> loop) { std::thread(std::move(loop)).detach(); }

Mutex::Mutex() : h_(new std::mutex) {}
Mutex::~Mutex() { delete (std::mutex *)h_; }
void Mutex::lock() { ((std::mutex *)h_)->lock(); }
void Mutex::unlock() { ((std::mutex *)h_)->unlock(); }

namespace {
struct Sema {
    std::mutex m;
    std::condition_variable cv;
    int count = 0;
};
}  // namespace
Semaphore::Semaphore() : h_(new Sema) {}
Semaphore::~Semaphore() { delete (Sema *)h_; }
void Semaphore::post() {
    Sema *s = (Sema *)h_;
    {
        std::lock_guard<std::mutex> l(s->m);
        s->count++;
    }
    s->cv.notify_one();
}
void Semaphore::wait() {
    Sema *s = (Sema *)h_;
    std::unique_lock<std::mutex> l(s->m);
    s->cv.wait(l, [s] { return s->count > 0; });
    s->count--;
}
bool Semaphore::try_wait() {
    Sema *s = (Sema *)h_;
    std::lock_guard<std::mutex> l(s->m);
    if (s->count == 0) return false;
    s->count--;
    return true;
}

void free_memory_mb(int &user, int &cdram) { user = cdram = -1; }

}  // namespace platform
}  // namespace vl
