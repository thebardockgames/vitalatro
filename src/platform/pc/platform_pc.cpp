// PC (Linux/WSL) platform for the headless test build: no window, no input. Data is read from
// the directory given by VL_DATA (defaults to ./data).
#include <algorithm>
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

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

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

namespace {
void (*g_log_tap)(const char *) = nullptr;
}

void set_log_tap(void (*tap)(const char *line)) { g_log_tap = tap; }

void log(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    fprintf(stderr, "%s\n", buf);
    if (auto tap = g_log_tap) tap(buf);
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
// presses a button (or touches a point) for 6 frames starting at the given frame;
// "f:drag:x1:y1:x2:y2:n" touches (x1, y1), moves to (x2, y2) over n frames, holds 6 and releases.
namespace {
struct ScriptEvent {
    int frame;
    uint32_t button;
    bool touch;
    float x, y;
    bool drag;
    float x2, y2;
    int n;
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
        } else if (rest.compare(0, 5, "drag:") == 0) {
            ev.touch = ev.drag = true;
            std::sscanf(rest.c_str() + 5, "%f:%f:%f:%f:%d", &ev.x, &ev.y, &ev.x2, &ev.y2, &ev.n);
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
        if (ev.drag) {
            if (frame_counter < ev.frame || frame_counter >= ev.frame + ev.n + 6) continue;
            const float t = std::min(1.0f, (float)(frame_counter - ev.frame) / (float)std::max(1, ev.n));
            in.touching = true;
            in.touch_x = ev.x + (ev.x2 - ev.x) * t;
            in.touch_y = ev.y + (ev.y2 - ev.y) * t;
            continue;
        }
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

// ---- dev server
bool net_init() { return true; }
std::string local_ip() { return "127.0.0.1"; }

int tcp_listen(int port) {
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(s, (sockaddr *)&a, sizeof a) < 0 || ::listen(s, 2) < 0) {
        ::close(s);
        return -1;
    }
    return s;
}
int tcp_accept(int listener) { return ::accept(listener, nullptr, nullptr); }
int tcp_recv(int s, void *buf, int n) { return (int)::recv(s, buf, (size_t)n, 0); }
bool tcp_send_all(int s, const void *buf, size_t n) {
    const char *p = (const char *)buf;
    while (n > 0) {
        ssize_t w = ::send(s, p, n, MSG_NOSIGNAL);
        if (w <= 0) return false;
        p += w;
        n -= (size_t)w;
    }
    return true;
}
void tcp_close(int s) { ::close(s); }
void keep_awake() {}
bool read_any(const char *path, std::string &out) { return read_whole(path, out); }
bool write_any(const char *path, const std::string &data) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << data;
    return (bool)f;
}
void restart_app() { fatal("restart_app: not on PC"); }

}  // namespace platform
}  // namespace vl
