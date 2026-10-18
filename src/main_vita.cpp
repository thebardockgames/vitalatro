#include "gfx/gfx.h"
#include "m0/m0.h"
#include "platform/platform.h"

int main() {
    vl::platform::init();
    if (!vl::gfx::init()) vl::platform::fatal("gfx init failed");
    vl::m0::init();
    vl::gfx::set_vsync(false);  // benchmark measures uncapped frame times
    uint32_t prev = 0;
    for (;;) {
        vl::platform::Input in;
        vl::platform::poll_input(in);
        vl::m0::frame(in.buttons & ~prev);
        prev = in.buttons;
    }
}
