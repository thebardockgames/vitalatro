// Headless PC runner: renders every M0 configuration at a fixed time and writes screenshots.
//   VL_DATA=<assets dir> vitalatro_pc <out_prefix> [time]
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "gfx/gfx.h"
#include "m0/m0.h"
#include "platform/platform.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

namespace vl::gfx {
void backend_read_screen(std::vector<uint8_t> &out);
}

int main(int argc, char **argv) {
    std::string prefix = argc > 1 ? argv[1] : "m0";
    double t = argc > 2 ? std::atof(argv[2]) : 123.4;
    vl::platform::init();
    if (!vl::gfx::init()) vl::platform::fatal("gfx init failed");
    vl::m0::init();
    std::vector<uint8_t> px;
    for (int i = 0; i < vl::m0::config_count(); i++) {
        vl::m0::render_config(i, t);
        vl::gfx::backend_read_screen(px);
        std::string out = prefix + "_" + std::to_string(i) + ".png";
        stbi_write_png(out.c_str(), vl::gfx::screen_width(), vl::gfx::screen_height(), 4, px.data(),
                       vl::gfx::screen_width() * 4);
        vl::platform::log("%s -> %s", vl::m0::config_name(i), out.c_str());
    }
    return 0;
}
