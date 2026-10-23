#include "core/time.h"
#include "game/g.h"
#include "game/run.h"
#include "gfx/gfx.h"
#include "m0/m0.h"
#include "platform/platform.h"

int main() {
    vl::platform::init();
    if (!vl::gfx::init()) vl::platform::fatal("gfx init failed");

    // holding SELECT while the game boots opens the M0 rendering benchmark/diagnostics
    vl::platform::Input in;
    vl::platform::poll_input(in);
    if (in.buttons & vl::platform::BTN_SELECT) {
        vl::m0::init();
        vl::gfx::set_vsync(false);
        uint32_t prev = 0;
        for (;;) {
            vl::platform::poll_input(in);
            vl::m0::frame(in.buttons & ~prev);
            prev = in.buttons;
        }
    }

    // holding R opens the run-like debug table instead of the menus (Globals::debug_table)
    if (in.buttons & vl::platform::BTN_R) vl::G.vl_debug_table = true;
    vl::game_boot();
    double last = vl::time_now();
    for (;;) {
        double now = vl::time_now();
        double dt = now - last;
        last = now;
        vl::game_frame(dt);
    }
}
