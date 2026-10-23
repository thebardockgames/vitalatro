// main.lua love.run / love.load / love.update / love.draw
#include "game/run.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "core/time.h"
#include "engine/font.h"
#include "game/g.h"
#include "game/input.h"
#include "game/render.h"
#include "gfx/gfx.h"
#include "platform/platform.h"

namespace vl {

void game_init();  // game.lua Game:init + set_globals + init_window

namespace {
double dt_smooth = 1.0 / 100;

// Performance overlay (SELECT toggles it) and a periodic log line.
bool show_fps = false;
uint32_t prev_buttons = 0;
double fps_acc = 0, fps_max = 0, fps_shown = 0, fps_max_shown = 0, log_timer = 0;
double cpu_upd = 0, cpu_draw = 0, cpu_upd_max = 0, cpu_draw_max = 0;
double last_upd = 0, last_draw = 0;  // CPU times of the previous frame (dt measures that frame)
int spikes_logged = 0;
double last_submit = 0, last_input = 0;
int fps_frames = 0;
Ref<Text> fps_text;

}  // namespace

// The estimated GPU shading time of the last submitted frame and its top shaders.
void log_gpu_estimate(const gfx::FrameStats *stats) {
    // estimated GPU shading of the last submitted frame (or `stats`), top shaders
    const gfx::FrameStats &fs = stats ? *stats : gfx::last_frame_stats();
    int order[(int)gfx::ShaderId::Count];
    float total = 0;
    for (int i = 0; i < (int)gfx::ShaderId::Count; i++) {
        order[i] = i;
        total += gfx::gpu_estimate_ms(fs, i);
    }
    std::sort(order, order + (int)gfx::ShaderId::Count,
              [&](int a, int b) { return gfx::gpu_estimate_ms(fs, a) > gfx::gpu_estimate_ms(fs, b); });
    char buf[256];
    int n = std::snprintf(buf, sizeof buf, "  gpu est %.1f ms:", total);
    for (int k = 0; k < 5 && n < (int)sizeof buf; k++) {
        int i = order[k];
        if (fs.px[i] <= 0) break;
        n += std::snprintf(buf + n, sizeof buf - n, " %s %.0fk/%.1f", gfx::shader_info((gfx::ShaderId)i).name, fs.px[i] / 1000,
                           gfx::gpu_estimate_ms(fs, i));
    }
    platform::log("%s", buf);
}

namespace {
// GPU bench: hold L+R for a second (or leave a menu or the debug table open 5 s, once per boot), then each mode
// runs 2 s with vsync off and its average frame time is logged
double bench_hold = 0, bench_t = 0, bench_sum = 0, bench_submit = 0;
int bench_frames = 0;
void bench_frame(double dt, uint32_t buttons) {
    const uint32_t LR = platform::BTN_L | platform::BTN_R;
    if (G.vl_bench < 0 || bench_t > 0) {
        bench_t += dt;
        bench_sum += dt;
        bench_submit += last_submit;
        bench_frames++;
        if (bench_t >= 2.0) {
            static const char *names[] = {"normal", "no CRT pass", "no background", "no overlay menu", "no cards", "overlay unzoomed", "no occlusion", "card effects plain",
                                          "polychrome plain", "holo plain", "foil/negative plain", "hologram plain", "no bg field pass", "bg merged pass"};
            int mode = G.vl_bench < 0 ? 0 : G.vl_bench;
            platform::log("bench %s: frame %.1f ms, submit %.1f ms", names[mode], bench_sum / bench_frames * 1000, bench_submit / bench_frames);
            log_gpu_estimate();
            bench_t = bench_sum = bench_submit = 0;
            bench_frames = 0;
            G.vl_bench = mode + 1;
            if (G.vl_bench > 13) {
                G.vl_bench = 0;
                bench_t = 0;
                gfx::set_vsync(true);
                return;
            }
            bench_t = 1e-9;  // keep running
        }
        return;
    }
    bench_hold = (buttons & LR) == LR ? bench_hold + dt : 0;
    // also once per boot by itself, when a menu (a collection page...) has been open for 5 s
    static double overlay_t = 0;
    static bool auto_done = false;
    overlay_t = (G.OVERLAY_MENU || G.STAGE == RUN) ? overlay_t + dt : 0;  // (the debug table is a RUN stage)
    if (!auto_done && overlay_t > 5) {
        auto_done = true;
        bench_hold = 2;
    }
    if (bench_hold > 1.0) {
        bench_hold = 0;
        G.vl_bench = -1;  // mode 0 (normal) first; -1 marks "running"
        bench_t = 1e-9;
        // without vsync, a GPU-bound frame takes the GPU's real time instead of a multiple of 16.7 ms
        gfx::set_vsync(false);
        platform::log("bench: start (vsync off)");
    }
}

// Per menu (each overlay, e.g. a collection page): its opening (first 0.7 s, cards materializing)
// and the rest, logged when it closes, with the estimate of the slowest steady frame.
struct PageStats {
    const void *box = nullptr;
    double t = 0, open_sum = 0, steady_sum = 0, steady_worst = 0;
    int open_frames = 0, open_slow = 0, steady_frames = 0, steady_slow = 0;
    gfx::FrameStats worst_stats{};
} page;
void page_frame(double dt) {
    const void *box = G.OVERLAY_MENU.get();
    if (box != page.box) {
        if (page.box && page.steady_frames + page.open_frames > 30) {
            platform::log("menu: open %.0f frames avg %.1f ms (%d slow) | then %.1f s avg %.1f ms, %d of %d frames slow",
                          (double)page.open_frames, page.open_sum / std::max(1, page.open_frames) * 1000, page.open_slow,
                          page.steady_sum, page.steady_sum / std::max(1, page.steady_frames) * 1000, page.steady_slow,
                          page.steady_frames);
            if (page.steady_slow) log_gpu_estimate(&page.worst_stats);
        }
        page = PageStats{};
        page.box = box;
    }
    if (!box) return;
    page.t += dt;
    const bool slow = dt > 0.020;
    if (page.t < 0.7) {
        page.open_frames++, page.open_sum += dt, page.open_slow += slow;
    } else {
        page.steady_frames++, page.steady_sum += dt, page.steady_slow += slow;
        if (dt > page.steady_worst) page.steady_worst = dt, page.worst_stats = gfx::last_frame_stats();
    }
}

void perf_frame(double dt, uint32_t buttons) {
    bench_frame(dt, buttons);
    if (G.vl_bench == 0) page_frame(dt);
    static const char *forced = std::getenv("VL_BENCH");  // PC: render one bench mode
    if (forced) G.vl_bench = std::atoi(forced);
    gfx::g_occlusion = G.vl_bench != 6 && !std::getenv("VL_NO_OCC");
    if ((buttons & platform::BTN_SELECT) && !(prev_buttons & platform::BTN_SELECT)) show_fps = !show_fps;
    prev_buttons = buttons;
    if (dt > 0.025 && spikes_logged < 40) {
        platform::log("spike: %.1f ms | input %.1f update %.1f draw %.1f submit %.1f | unaccounted %.1f | state %d t %.2f",
                      dt * 1000, last_input, last_upd, last_draw, last_submit,
                      dt * 1000 - last_input - last_upd - last_draw - last_submit, G.STATE,
                      G.TIMERS.REAL);
        spikes_logged++;
        log_gpu_estimate();
    }
    fps_acc += dt;
    fps_max = std::max(fps_max, dt);
    fps_frames++;
    if (fps_acc >= 0.5) {
        fps_shown = fps_frames / fps_acc;
        fps_max_shown = fps_max * 1000;
        log_timer += fps_acc;
        if (log_timer >= 5) {
            int mem_user, mem_cdram;
            platform::free_memory_mb(mem_user, mem_cdram);
            platform::log("perf: %.1f fps, worst frame %.1f ms | cpu update %.1f (max %.1f) draw %.1f (max %.1f) ms | state %d | free MB ram %d cdram %d",
                          fps_shown, fps_max_shown, cpu_upd / fps_frames, cpu_upd_max, cpu_draw / fps_frames, cpu_draw_max, G.STATE,
                          mem_user, mem_cdram);
            log_timer = 0;
        }
        fps_acc = fps_max = 0;
        fps_frames = 0;
        cpu_upd = cpu_draw = cpu_upd_max = cpu_draw_max = 0;
    }
    if (!show_fps) return;
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.1f fps  max %.1f ms", fps_shown, fps_max_shown);
    if (!fps_text) fps_text = new Text(Font::load("m6x11plus_200"), buf);
    else fps_text->set(buf);
    gfx::push();
    gfx::origin();
    gfx::set_shader(gfx::ShaderId::Default);
    gfx::set_color({0, 0, 0, 0.6f});
    gfx::rectangle_fill(4, 4, fps_text->width() * 0.11f + 12, 26);
    gfx::set_color({1, 1, 0.4f, 1});
    fps_text->draw(10, 5, 0, 0.11f, 0.11f);
    gfx::pop();
}
}

void game_boot() {
    render::init();
    game_init();
    render::prewarm();
    G.start_up();
}

double game_frame(double dt) {
    double ti = time_now();
    platform::Input in;
    platform::poll_input(in);
    input_frame(in);
    double input_now = (time_now() - ti) * 1000;
    dt_smooth = std::min(0.8 * dt_smooth + 0.2 * dt, 0.1);
    Font::pump();
    double t0 = time_now();
    G.update(dt_smooth);
    double t1 = time_now();
    gfx::begin_frame();
    G.draw();
    double t2 = time_now();
    cpu_upd += (t1 - t0) * 1000;
    cpu_draw += (t2 - t1) * 1000;
    cpu_upd_max = std::max(cpu_upd_max, (t1 - t0) * 1000);
    double upd_now = (t1 - t0) * 1000, draw_now = (t2 - t1) * 1000;
    cpu_draw_max = std::max(cpu_draw_max, (t2 - t1) * 1000);
    perf_frame(dt, in.buttons);
    last_upd = upd_now;
    last_draw = draw_now;
    double t3 = time_now();
    gfx::end_frame();
    last_submit = (time_now() - t3) * 1000;
    last_input = input_now;
    return dt_smooth;
}

}  // namespace vl
