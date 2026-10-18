#pragma once
// main.lua's love.* input handlers, fed from the Vita pad and front touch screen.
#include "platform/platform.h"

namespace vl {

void input_frame(const platform::Input &in);

}  // namespace vl
