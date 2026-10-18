// PC (Linux/WSL) platform for the headless test build: no window, no input. Data is read from
// the directory given by VL_DATA (defaults to ./data).
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>

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
}

bool read_file(const char *path, std::string &out) { return read_whole(g_root + path, out); }
bool read_save(const char *path, std::string &out) { return read_whole(g_save + path, out); }
bool write_save(const char *path, const std::string &data) {
    std::ofstream f(g_save + path, std::ios::binary);
    if (!f) return false;
    f << data;
    return (bool)f;
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

void poll_input(Input &in) { in = Input{}; }

}  // namespace platform
}  // namespace vl
