#pragma once
#include <cstdint>
#include <functional>
#include <string>

namespace vl::platform {

void init();
// Paths are relative to the game data root (app0: on Vita, the assets dir on PC).
bool read_file(const char *path, std::string &out);
// Save data lives elsewhere (ux0:data/Vitalatro on Vita).
bool read_save(const char *path, std::string &out);
bool write_save(const char *path, const std::string &data);
[[noreturn]] void fatal(const char *fmt, ...);
void log(const char *fmt, ...);

struct Input {
    uint32_t buttons;       // Button bitmask, held this frame
    float lx, ly, rx, ry;   // sticks, -1..1
    bool touching;
    float touch_x, touch_y; // in screen pixels
};
enum Button : uint32_t {
    BTN_UP = 1u << 0, BTN_DOWN = 1u << 1, BTN_LEFT = 1u << 2, BTN_RIGHT = 1u << 3,
    BTN_CROSS = 1u << 4, BTN_CIRCLE = 1u << 5, BTN_SQUARE = 1u << 6, BTN_TRIANGLE = 1u << 7,
    BTN_L = 1u << 8, BTN_R = 1u << 9, BTN_START = 1u << 10, BTN_SELECT = 1u << 11,
};
void poll_input(Input &in);

// Runs job on a worker thread (a native thread with a small stack on the Vita). When no thread
// can be created the job runs right away on the calling thread.
void run_async(std::function<void()> job);

// Free memory in MB (user RAM, CDRAM); -1 when unknown.
void free_memory_mb(int &user, int &cdram);

}  // namespace vl::platform
