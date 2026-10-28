#pragma once

#include <vector>

#include "archive.h"
#include "builder.h"

namespace vlab {

// sce_sys/icon0.png (128x128), sce_sys/livearea/contents/bg.png (840x500) and startup.png
// (280x158), all 8-bit paletted as the Vita wants them. Our own paint-swirl background with
// sprites from the user's own textures on top (a Joker card for the bubble, the game's logo on
// the startup gate), so the published package holds no game artwork.
void make_livearea(const Game &game, std::vector<OutFile> &out);

}  // namespace vlab
