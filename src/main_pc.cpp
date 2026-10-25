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

#include "core/time.h"
#include "dev/devserver.h"
#include "game/blind.h"
#include "game/card_factory.h"
#include "engine/particles.h"
#include "engine/text.h"
#include "engine/ui.h"
#include "game/card.h"
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
    if (std::getenv("VL_DEV")) vl::dev::start();  // the dev server, to test tools/vitactl.py locally
    const char *ge = std::getenv("VL_GPU_EST");
    // VL_TUTORIAL_DONE=1: the profile's tutorial counts as complete; VL_RUN="frame:seed" starts a
    // seeded run (G.FUNCS.start_run(nil, {seed = seed, stake = 1})) at that frame
    if (std::getenv("VL_TUTORIAL_DONE")) vl::G.SETTINGS->set("tutorial_complete", true);
    if (const char *gs = std::getenv("VL_GAMESPEED")) vl::G.SETTINGS->set("GAMESPEED", std::atof(gs));
    int run_frame = -1;
    std::string run_seed;
    if (const char *r = std::getenv("VL_RUN")) {
        run_frame = std::atoi(r);
        if (const char *c = std::strchr(r, ':')) run_seed = c + 1;
    }
    // VL_SET_CHIPS="frame:chips": G.GAME.blind.chips = chips at that frame (test hook)
    int chips_frame = -1;
    double chips_val = 0;
    if (const char *c = std::getenv("VL_SET_CHIPS"))
        if (std::strchr(c, ':')) {
            chips_frame = std::atoi(c);
            chips_val = std::atof(std::strchr(c, ':') + 1);
        }
    // with the dev server the runner keeps going in real time (60 fps) until it is killed
    const bool dev = std::getenv("VL_DEV") != nullptr;
    if (dev) last = 1 << 30;
    double next_frame = vl::time_now();
    // VL_JOKERS="frame:j_a,j_b": create_card('Joker', G.jokers, nil, nil, nil, nil, key) for each (test hook)
    int jokers_frame = -1;
    std::string jokers_list;
    if (const char *j = std::getenv("VL_JOKERS"))
        if (const char *c = std::strchr(j, ':')) {
            jokers_frame = std::atoi(j);
            jokers_list = c + 1;
        }
    for (int f = 1; f <= last; f++) {
        if (f == jokers_frame) vl::add_jokers(jokers_list);
        if (dev) {
            next_frame += 1.0 / 60;
            vl::time_sleep(next_frame - vl::time_now());
        }
        // VL_DOLLARS="frame:n": G.GAME.dollars = n at that frame (test hook)
        if (const char *d = std::getenv("VL_DOLLARS"); d && std::strchr(d, ':') && std::atoi(d) == f)
            vl::G.GAME->set("dollars", std::atof(std::strchr(d, ':') + 1));
        if (f == chips_frame)
            if (auto *b = vl::G.GAME->get(vl::Value("blind")).as<vl::Blind>()) b->chips = chips_val;
        // VL_DECK=b_plasma: the run's deck; VL_COMBO="frame:hand delta": combo_setup at that frame
        if (const char *d = std::getenv("VL_DECK"); d && f == run_frame)
            vl::G.GAME->set("viewed_back", vl::Value((vl::Object *)new vl::Back(vl::G.P_CENTERS->get(vl::Value(d)))));
        if (const char *c = std::getenv("VL_COMBO"); c && std::atoi(c) == f) vl::combo_setup(std::strchr(c, ':') ? std::atoi(std::strchr(c, ':') + 1) : 44);
        if (f == run_frame) vl::start_run_with(nullptr, vl::T({{"seed", vl::Value(run_seed)}, {"stake", 1}}));
        // VL_GPU_EST=2 lists the large draws of each captured frame
        vl::gfx::g_draw_debug = ge && ge[0] == '2' && frames.count(f);
        // VL_ID_DETAIL="f,f": every Node created during those frames, by class (as REF_ID_DETAIL)
        static std::set<int> detail_frames = [] {
            std::set<int> s;
            if (const char *d = std::getenv("VL_ID_DETAIL"))
                for (const char *p = d; *p;) {
                    s.insert(std::atoi(p));
                    while (*p && *p != ',') p++;
                    if (*p) p++;
                }
            return s;
        }();
        std::vector<vl::NodeRef> detail_log;
        if (detail_frames.count(f)) vl::G.id_detail_log = &detail_log;
        vl::game_frame(1.0 / 60);
        vl::G.id_detail_log = nullptr;
        // VL_PROBE_FLAMES="f,f": the flame shaders' inputs (as REF_PROBE on G.ARGS.chip_flames...)
        if (const char *pf = std::getenv("VL_PROBE_FLAMES"))
            for (const char *p = pf; *p;) {
                if (std::atoi(p) == f)
                    for (const char *k : {"chip_flames", "mult_flames"}) {
                        vl::Value fl = vl::G.ARGS->get(vl::Value(k));
                        if (!fl.truthy()) continue;
                        std::printf("FLAMES %d %s timer %.6f real_intensity %.6f intensity %.6f REAL %.6f\n", f, k, fl.get(vl::Value("timer")).num(),
                                    fl.get(vl::Value("real_intensity")).num(), fl.get(vl::Value("intensity")).num(), vl::G.TIMERS.REAL);
                    }
                while (*p && *p != ',') p++;
                if (*p) p++;
            }
        if (const char *pf = std::getenv("VL_PROBE_SIGN"); pf && std::atoi(pf) == f)
            if (auto *box = dynamic_cast<vl::UIBox *>(vl::G.SHOP_SIGN.get())) {
                auto *e = dynamic_cast<vl::UIElement *>(box->UIRoot->children.at(0)->children.at(0)->children.at(0));
                auto *o = dynamic_cast<vl::Moveable *>(e->config->get(vl::Value("object")).obj());
                std::printf("PROBE spriteT %.4f %.4f %.4f %.4f par %.4f %.4f uie %.4f %.4f roomw %.4f\n", o->T->x, o->T->y, o->T->w, o->T->h,
                            o->shadow_parrallax->x, o->shadow_parrallax->y, e->T->x, e->T->y, vl::G.ROOM->T->w);
            }
        for (auto &n : detail_log) {
            std::string tag = "Node";
            if (dynamic_cast<vl::Card *>(n.get())) tag = "Card";
            else if (dynamic_cast<vl::CardArea *>(n.get())) tag = "CardArea";
            else if (dynamic_cast<vl::UIBox *>(n.get())) tag = "UIBox";
            else if (auto *e = dynamic_cast<vl::UIElement *>(n.get())) {
                tag = "UIE" + std::to_string(e->UIT);
                vl::Value id = e->config->get(vl::Value("id")), text = e->config->get(vl::Value("text"));
                if (id.truthy()) tag += ":" + id.to_string();
                if (text.truthy()) tag += ":" + text.to_string();
            } else if (dynamic_cast<vl::DynaText *>(n.get())) tag = "DynaText";
            else if (dynamic_cast<vl::AnimatedSprite *>(n.get())) tag = "AnimatedSprite";
            else if (dynamic_cast<vl::Sprite *>(n.get())) tag = "Sprite";
            else if (dynamic_cast<vl::Particles *>(n.get())) tag = "Particles";
            else if (dynamic_cast<vl::Moveable *>(n.get())) tag = "Moveable";
            std::printf("NODE %d %d %s\n", f, n->ID, tag.c_str());
        }
        static const bool dump_id = std::getenv("VL_DUMP_ID") != nullptr;
        if (dump_id) std::printf("ID %d %d\n", f, vl::G.ID);  // node IDs (VL_DUMP_ID, vs REF_DUMP_ID)
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
