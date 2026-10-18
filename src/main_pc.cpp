// Headless PC runner.
//   VL_DATA=<assets dir> vitalatro_pc game <out_prefix> <frame> [<frame>...]
//       runs the game at a fixed 60 fps and writes screenshots after the given frames
//   VL_DATA=<assets dir> vitalatro_pc m0 <out_prefix> [time]
//       renders every M0 benchmark configuration and diagnostic page
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "game/run.h"
#include "gfx/backend.h"
#include "gfx/gfx.h"
#include "m0/m0.h"
#include "platform/platform.h"

#include "stb/stb_image_write.h"

namespace {
void shot(const std::string &out) {
    std::vector<uint8_t> px;
    vl::gfx::backend_read_screen(px);
    stbi_write_png(out.c_str(), vl::gfx::screen_width(), vl::gfx::screen_height(), 4, px.data(), vl::gfx::screen_width() * 4);
}
}  // namespace

int main(int argc, char **argv) {
    std::string mode = argc > 1 ? argv[1] : "game";
    std::string prefix = argc > 2 ? argv[2] : "shot";
    vl::platform::init();
    if (!vl::gfx::init()) vl::platform::fatal("gfx init failed");

    if (mode == "m0") {
        double t = argc > 3 ? std::atof(argv[3]) : 123.4;
        vl::m0::init();
        for (int i = 0; i < vl::m0::config_count(); i++) {
            vl::m0::render_config(i, t);
            shot(prefix + "_" + std::to_string(i) + ".png");
        }
        for (int i = 0; i < vl::m0::diag_page_count(); i++) {
            vl::m0::render_diag(i, t);
            shot(prefix + "_diag" + std::to_string(i) + ".png");
        }
        return 0;
    }

    std::set<int> frames;
    for (int i = 3; i < argc; i++) frames.insert(std::atoi(argv[i]));
    int last = frames.empty() ? 600 : *frames.rbegin();
    vl::game_boot();
    for (int f = 1; f <= last; f++) {
        vl::game_frame(1.0 / 60);
        if (frames.count(f)) {
            std::string out = prefix + "_" + std::to_string(f) + ".png";
            shot(out);
            vl::platform::log("frame %d -> %s", f, out.c_str());
        }
    }
    return 0;
}
