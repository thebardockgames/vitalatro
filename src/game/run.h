#pragma once
// love.run for the game: boot (love.load -> G:start_up) and one main loop iteration.
namespace vl::gfx {
struct FrameStats;
}
namespace vl {

void game_boot();
// dt is the raw frame time; returns the smoothed dt passed to G:update
double game_frame(double dt);
// logs the estimated GPU shading time of the last submitted frame and its top shaders
void log_gpu_estimate(const gfx::FrameStats *stats = nullptr);
// the GPU bench (every bench mode 2 s with vsync off, logged); what holding L+R starts
void start_gpu_bench();
bool gpu_bench_running();

}  // namespace vl
