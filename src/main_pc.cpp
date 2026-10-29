// Headless PC runner.
//   VL_DATA=<assets dir> vitalatro_pc game <out_prefix> <frame> [<frame>...]
//       runs the game at a fixed 60 fps and writes screenshots after the given frames
//   VL_DATA=<assets dir> vitalatro_pc m0 <out_prefix> [time]
//       renders every M0 benchmark configuration and diagnostic page
#include <cstdio>
#include <functional>
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
#include "engine/random.h"
#include "engine/text.h"
#include "engine/ui.h"
#include "game/card.h"
#include "game/functions.h"
#include "game/g.h"
#include "game/run.h"
#include "game/shop.h"
#include "game/state_events.h"
#include "game/tag.h"
#include "game/ui_definitions.h"
#include "gfx/backend.h"
#include "gfx/gfx.h"
#include "m0/m0.h"
#include "platform/platform.h"

#include "stb/stb_image_write.h"

#if __has_include(<valgrind/callgrind.h>)
#include <valgrind/callgrind.h>
#define VL_HAVE_CALLGRIND 1
#endif

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
    // VL_TAGS="frame:tag_a,tag_b": add_tag(Tag(key)) for each (test hook, as REF_TAGS)
    int tags_frame = -1;
    std::string tags_list;
    if (const char *t = std::getenv("VL_TAGS"))
        if (const char *c = std::strchr(t, ':')) {
            tags_frame = std::atoi(t);
            tags_list = c + 1;
        }
    for (int f = 1; f <= last; f++) {
        if (f == jokers_frame) vl::add_jokers(jokers_list);
        if (f == tags_frame)
            for (size_t p = 0; p < tags_list.size();) {
                size_t q = tags_list.find(',', p);
                if (q == std::string::npos) q = tags_list.size();
                vl::add_tag(new vl::Tag(vl::Value(tags_list.substr(p, q - p))));
                p = q + 1;
            }
        if (dev) {
            next_frame += 1.0 / 60;
            vl::time_sleep(next_frame - vl::time_now());
        }
        // VL_GAME_SET="frame:key=number[,key=number]": G.GAME[key] = number at that frame (test hook)
        if (const char *gs = std::getenv("VL_GAME_SET"); gs && std::strchr(gs, ':') && std::atoi(gs) == f) {
            // (key=false / key=true set a boolean: e.g. seeded=false so a seeded test run can unlock)
            std::string all = std::strchr(gs, ':') + 1;
            for (size_t p = 0; p < all.size();) {
                size_t q = all.find(',', p);
                if (q == std::string::npos) q = all.size();
                const std::string kv = all.substr(p, q - p);
                p = q + 1;
                const size_t eq = kv.find('=');
                if (eq == std::string::npos) continue;
                const std::string v = kv.substr(eq + 1);
                vl::G.GAME->set(vl::Value(kv.substr(0, eq)), v == "false" ? vl::Value(false) : v == "true" ? vl::Value(true) : vl::Value(std::atof(v.c_str())));
            }
        }
        // VL_PROFILE_SET="frame:key=number": the current profile's key = number (test hook)
        if (const char *ps = std::getenv("VL_PROFILE_SET"); ps && std::strchr(ps, ':') && std::atoi(ps) == f) {
            std::string kv = std::strchr(ps, ':') + 1;
            size_t eq = kv.find('=');
            if (eq != std::string::npos)
                vl::G.PROFILES->get(vl::G.SETTINGS->get(vl::Value("profile"))).table()->set(vl::Value(kv.substr(0, eq)), std::atof(kv.c_str() + eq + 1));
        }
        // VL_UNLOCK="frame:key,key": create_unlock_overlay(key) for each key at that frame (test hook)
        if (const char *u = std::getenv("VL_UNLOCK"); u && std::strchr(u, ':') && std::atoi(u) == f) {
            std::string keys = std::strchr(u, ':') + 1;
            for (size_t p = 0; p <= keys.size();) {
                size_t q = keys.find(',', p);
                if (q == std::string::npos) q = keys.size();
                vl::create_unlock_overlay(keys.substr(p, q - p));
                p = q + 1;
            }
        }
        // VL_HAND="frame:S_7[:m_gold],H_J,...|frame:...": the hand's cards (in order) become those cards at that frame
        // (set_base / set_ability, as REF_PRE "G.hand.cards[i]:set_base(G.P_CARDS[k])")
        if (const char *h = std::getenv("VL_HAND")) {
            std::string all = h;
            for (size_t p = 0; p < all.size();) {
                size_t q = all.find('|', p);
                if (q == std::string::npos) q = all.size();
                std::string grp = all.substr(p, q - p);
                p = q + 1;
                if (std::atoi(grp.c_str()) != f || grp.find(':') == std::string::npos) continue;
                grp = grp.substr(grp.find(':') + 1);
                int i = 0;
                for (size_t a = 0; a <= grp.size() && vl::G.hand;) {
                    size_t b = grp.find(',', a);
                    if (b == std::string::npos) b = grp.size();
                    std::string c = grp.substr(a, b - a), enh;
                    a = b + 1;
                    if (size_t col = c.find(':'); col != std::string::npos) enh = c.substr(col + 1), c = c.substr(0, col);
                    if (i >= (int)vl::G.hand->cards.size()) break;
                    vl::Card *card = vl::G.hand->cards[i++].get();
                    card->set_base(vl::G.P_CARDS->get(vl::Value(c)));
                    if (!enh.empty()) card->set_ability(vl::G.P_CENTERS->get(vl::Value(enh)));
                }
            }
        }
        // VL_DOLLARS="frame:n": G.GAME.dollars = n at that frame (test hook)
        if (const char *d = std::getenv("VL_DOLLARS"); d && std::strchr(d, ':') && std::atoi(d) == f)
            vl::G.GAME->set("dollars", std::atof(std::strchr(d, ':') + 1));
        if (f == chips_frame)
            if (auto *b = vl::G.GAME->get(vl::Value("blind")).as<vl::Blind>()) b->chips = chips_val;
        // VL_BLIND_TAG="frame:tag_x": the small blind's skip tag, from that frame on once the run has its
        // blind tags (test hook, as REF_BLIND_TAG)
        static bool bt_done = false;
        if (const char *bt = std::getenv("VL_BLIND_TAG"); bt && !bt_done && std::strchr(bt, ':') && f >= std::atoi(bt))
            if (vl::Table *tags = vl::G.GAME->get(vl::Value("round_resets")).get(vl::Value("blind_tags")).table()) {
                tags->set("Small", vl::Value(std::strchr(bt, ':') + 1));
                bt_done = true;
            }
        // VL_DECK=b_plasma: the run's deck; VL_COMBO="frame:hand delta": combo_setup at that frame
        if (const char *d = std::getenv("VL_DECK"); d && f == run_frame)
            vl::G.GAME->set("viewed_back", vl::Value((vl::Object *)new vl::Back(vl::G.P_CENTERS->get(vl::Value(d)))));
        if (const char *c = std::getenv("VL_COMBO"); c && std::atoi(c) == f) vl::combo_setup(std::strchr(c, ':') ? std::atoi(std::strchr(c, ':') + 1) : 44);
        // VL_STAKE=n: the run's stake (default 1, as REF_STAKE)
        if (f == run_frame)
            vl::start_run_with(nullptr, vl::T({{"seed", vl::Value(run_seed)}, {"stake", std::getenv("VL_STAKE") ? std::atof(std::getenv("VL_STAKE")) : 1.0}}));
        // VL_SET="frame:a.b.c=value|...": G.GAME.a.b.c = value (a number, or else a string) before that
        // frame's update (test hook, as REF_PRE "frame:G.GAME.a.b.c = value")
        if (const char *vs = std::getenv("VL_SET"))
            for (const char *p = vs; *p;) {
                const char *e = std::strchr(p, '|');
                std::string ent(p, e ? e - p : std::strlen(p));
                p = e ? e + 1 : p + ent.size();
                size_t colon = ent.find(':'), eq = ent.find('=');
                if (colon == std::string::npos || eq == std::string::npos || std::atoi(ent.c_str()) != f) continue;
                std::string path = ent.substr(colon + 1, eq - colon - 1), val = ent.substr(eq + 1);
                vl::Table *tab = vl::G.GAME.get();
                for (size_t dot; (dot = path.find('.')) != std::string::npos; path = path.substr(dot + 1))
                    tab = tab->get(vl::Value(path.substr(0, dot))).table();
                char *end = nullptr;
                double num = std::strtod(val.c_str(), &end);
                if (!val.empty() && end && *end == 0) tab->set(vl::Value(path), num);
                else tab->set(vl::Value(path), vl::Value(val));
            }
        // VL_USE="frame:area:i|...": G.FUNCS.use_card({config = {ref_table = G[area].cards[i]}}) before that
        // frame's update, as tapping its USE button (test hook; areas: consumeables, jokers, pack_cards,
        // shop_jokers, shop_vouchers, shop_booster)
        if (const char *vu = std::getenv("VL_USE"))
            for (const char *p = vu; *p;) {
                const char *e = std::strchr(p, '|');
                std::string ent(p, e ? e - p : std::strlen(p));
                p = e ? e + 1 : p + ent.size();
                size_t c1 = ent.find(':'), c2 = ent.rfind(':');
                if (c1 == std::string::npos || c2 == c1 || std::atoi(ent.c_str()) != f) continue;
                std::string an = ent.substr(c1 + 1, c2 - c1 - 1);
                int idx = std::atoi(ent.c_str() + c2 + 1) - 1;
                vl::CardArea *a = an == "consumeables" ? vl::G.consumeables.get() : an == "jokers" ? vl::G.jokers.get() : an == "pack_cards" ? vl::G.pack_cards.get()
                                : an == "shop_jokers" ? vl::G.shop_jokers.get() : an == "shop_vouchers" ? vl::G.shop_vouchers.get() : an == "shop_booster" ? vl::G.shop_booster.get() : nullptr;
                if (a && idx >= 0 && idx < (int)a->cards.size()) vl::use_card(a->cards[idx].get(), nullptr);
                else std::printf("VL_USE: no card %s %d\n", an.c_str(), idx + 1);
            }
        // VL_OPEN_PACK="frame:p_key": that booster opened for free as a tag opens it (test hook)
        if (const char *op = std::getenv("VL_OPEN_PACK"); op && std::strchr(op, ':') && std::atoi(op) == f) vl::open_tag_pack(std::strchr(op, ':') + 1);
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
#ifdef VL_HAVE_CALLGRIND
        // VL_CALLGRIND="from:to": callgrind instruments only those frames (valgrind --instr-atstart=no)
        if (const char *cg = std::getenv("VL_CALLGRIND"); cg && std::strchr(cg, ':')) {
            if (f == std::atoi(cg)) CALLGRIND_START_INSTRUMENTATION;
            if (f == std::atoi(std::strchr(cg, ':') + 1)) CALLGRIND_STOP_INSTRUMENTATION;
        }
#endif
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
        // VL_DUMP_UI=frame: the overlay menu's UI tree (as REF_DUMP_UI): depth, UIT, T x y w h, id
        if (const char *du = std::getenv("VL_DUMP_UI"); du && std::atoi(du) == f)
            if (auto *ov = dynamic_cast<vl::UIBox *>(vl::G.OVERLAY_MENU.get())) {
                std::function<void(vl::Node *, int)> walk = [&](vl::Node *n, int d) {
                    auto *e = dynamic_cast<vl::UIElement *>(n);
                    if (!e) return;
                    vl::Value id = e->config->get(vl::Value("id"));
                    std::printf("UI %d %d %.3f %.3f %.3f %.3f %s\n", d, e->UIT, e->T->x, e->T->y, e->T->w, e->T->h, id.truthy() ? id.to_string().c_str() : "-");
                    for (int i = 0; i < e->children.count(); i++) walk(e->children.at(i), d + 1);
                    if (auto *o = dynamic_cast<vl::UIBox *>(e->config->get(vl::Value("object")).obj())) walk(o->UIRoot.get(), d + 1);
                };
                walk(ov->UIRoot.get(), 0);
            }
        // VL_PROBE_DECK=frame: the first title-type card area's cards (deck view), as REF_PROBE prints them
        if (const char *pd = std::getenv("VL_PROBE_DECK"); pd && std::atoi(pd) == f)
            for (size_t i = 0; i < vl::G.I.CARDAREA.size(); i++) {
                auto *a = dynamic_cast<vl::CardArea *>(vl::G.I.CARDAREA[i]);
                if (!a || !a->config->get(vl::Value("view_deck")).truthy()) continue;
                std::printf("PROBE area %.4f %.4f %.4f %.4f cw %.4f\n", a->T->x, a->T->y, a->T->w, a->T->h, a->card_w);
                for (auto &c : a->cards)
                    std::printf("PROBE card %d VT %.4f %.4f %.4f %.4f r %.4f tilt %.2f %.2f %.4f\n", c->ID, c->VT.x, c->VT.y, c->VT.w, c->VT.h,
                                c->VT.r, c->tilt_var.mx, c->tilt_var.my, c->tilt_var.amt);
                for (auto &c : a->cards) std::printf("PROBE debuff %d %d\n", c->ID, (int)c->debuff);
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
        // VL_DUMP_RNG=1: math.random / math.randomseed calls so far, per frame (as REF_DUMP_RNG)
        static const bool dump_rng = std::getenv("VL_DUMP_RNG") != nullptr;
        if (dump_rng) std::printf("RNG %d %llu %llu\n", f, (unsigned long long)vl::lua_math.n_random, (unsigned long long)vl::lua_math.n_seed);
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
