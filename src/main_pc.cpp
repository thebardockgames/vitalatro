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

#include "game/g.h"
#include "game/run.h"
#include "game/state_events.h"
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
    const char *ge = std::getenv("VL_GPU_EST");
    // VL_TUTORIAL_DONE=1: the profile's tutorial counts as complete; VL_RUN="frame:seed" starts a
    // seeded run (G.FUNCS.start_run(nil, {seed = seed, stake = 1})) at that frame
    if (std::getenv("VL_TUTORIAL_DONE")) vl::G.SETTINGS->set("tutorial_complete", true);
    int run_frame = -1;
    std::string run_seed;
    if (const char *r = std::getenv("VL_RUN")) {
        run_frame = std::atoi(r);
        if (const char *c = std::strchr(r, ':')) run_seed = c + 1;
    }
    for (int f = 1; f <= last; f++) {
        if (f == run_frame) vl::start_run_with(nullptr, vl::T({{"seed", vl::Value(run_seed)}, {"stake", 1}}));
        // VL_GPU_EST=2 lists the large draws of each captured frame
        vl::gfx::g_draw_debug = ge && ge[0] == '2' && frames.count(f);
        vl::game_frame(1.0 / 60);
        if (frames.count(f)) {
            std::string out = prefix + "_" + std::to_string(f) + ".png";
            shot(out);
            vl::platform::log("frame %d -> %s", f, out.c_str());
            if (std::getenv("VL_GPU_EST")) vl::log_gpu_estimate();
            if (std::getenv("VL_DUMP_BG")) {
                vl::Value b = vl::G.C->get(vl::Value("BACKGROUND"));
                auto c = [&](const char *k, int i) { return b.get(vl::Value(k)).get(i).num(); };
                std::printf("BG C=%.4f,%.4f,%.4f L=%.4f,%.4f,%.4f D=%.4f,%.4f,%.4f contrast=%.4f\n", c("C", 1), c("C", 2), c("C", 3), c("L", 1), c("L", 2), c("L", 3),
                            c("D", 1), c("D", 2), c("D", 3), b.get(vl::Value("contrast")).num());
                vl::Value sp = vl::G.ARGS->get(vl::Value("spin"));
                std::printf("SPIN amount=%f real=%f eased=%f BG_T=%f REAL_SHADER=%f\n", sp.get(vl::Value("amount")).num(), sp.get(vl::Value("real")).num(),
                            sp.get(vl::Value("eased")).num(), vl::G.TIMERS.BACKGROUND, vl::G.TIMERS.REAL_SHADER);
            }
        }
    }
    // the save/sound/audio threads run until the process ends (as on the Vita): skip the static
    // destructors they would race with
    std::fflush(nullptr);
    std::_Exit(0);
}
