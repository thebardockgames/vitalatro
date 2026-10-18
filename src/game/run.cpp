// main.lua love.run / love.load / love.update / love.draw
#include "game/run.h"

#include <algorithm>
#include <cstdio>

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

void perf_frame(double dt, uint32_t buttons) {
    if ((buttons & platform::BTN_SELECT) && !(prev_buttons & platform::BTN_SELECT)) show_fps = !show_fps;
    prev_buttons = buttons;
    if (dt > 0.025 && spikes_logged < 40) {
        platform::log("spike: %.1f ms | input %.1f update %.1f draw %.1f submit %.1f | unaccounted %.1f | state %d t %.2f",
                      dt * 1000, last_input, last_upd, last_draw, last_submit,
                      dt * 1000 - last_input - last_upd - last_draw - last_submit, G.STATE,
                      G.TIMERS.REAL);
        spikes_logged++;
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
