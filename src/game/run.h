#pragma once
// love.run for the game: boot (love.load -> G:start_up) and one main loop iteration.
namespace vl {

void game_boot();
// dt is the raw frame time; returns the smoothed dt passed to G:update
double game_frame(double dt);

}  // namespace vl
