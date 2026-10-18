#include "gfx/gfx.h"
#include "m0/m0.h"
#include "platform/platform.h"

int main() {
    vl::platform::init();
    if (!vl::gfx::init()) vl::platform::fatal("gfx init failed");
    vl::m0::init();
    vl::gfx::set_vsync(false);  // benchmark measures uncapped frame times
    bool capped = false;
    for (;;) {
        vl::m0::frame();
        if (!capped && vl::m0::finished()) {
            vl::gfx::set_vsync(true);
            capped = true;
        }
    }
}
