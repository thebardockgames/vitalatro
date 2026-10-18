// Headless PC runner: renders the M0 scene for a number of frames and writes a screenshot.
//   VL_DATA=<assets dir> vitalatro_pc [frames] [out.png]
#include <cstdlib>
#include <vector>

#include "gfx/backend.h"
#include "gfx/gfx.h"
#include "m0/m0.h"
#include "platform/platform.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

namespace vl::gfx {
void backend_read_screen(std::vector<uint8_t> &out);
}

int main(int argc, char **argv) {
    int frames = argc > 1 ? std::atoi(argv[1]) : 60;
    const char *out = argc > 2 ? argv[2] : "m0.png";
    vl::platform::init();
    if (!vl::gfx::init()) vl::platform::fatal("gfx init failed");
    vl::m0::init();
    for (int i = 0; i < frames; i++) vl::m0::frame();
    std::vector<uint8_t> px;
    vl::gfx::backend_read_screen(px);
    stbi_write_png(out, vl::gfx::screen_width(), vl::gfx::screen_height(), 4, px.data(),
                   vl::gfx::screen_width() * 4);
    vl::platform::log("wrote %s", out);
    return 0;
}
