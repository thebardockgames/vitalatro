#pragma once
#include <cstdint>

namespace vl::m0 {

void init();
void frame(uint32_t pressed_buttons);
bool finished();

// For the headless PC build: render one configuration at a fixed time.
int config_count();
const char *config_name(int i);
void render_config(int i, double t);

}  // namespace vl::m0
