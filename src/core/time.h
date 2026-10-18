#pragma once

namespace vl {

// Monotonic time in seconds (love.timer.getTime).
double time_now();
void time_sleep(double seconds);

}  // namespace vl
