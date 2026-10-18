#pragma once
// Vitalatro's frame-level rendering helpers for Game:draw: the background shaders evaluated per
// block (splash.fs / background.fs) and the CRT pass, with the optimised Vita pipelines measured in
// M0. On PC the original shaders run unchanged unless VL_FAST=1 (to compare both).
#include "gfx/gfx.h"

namespace vl {
class Sprite;
}

namespace vl::render {

void init();
// One frame through every canvas so their render targets exist before the game's first frames.
void prewarm();
bool fast_paths();

// G.SPLASH_BACK:draw() when its draw step is the splash or background shader: these shaders only
// depend on the screen position, so they are rendered full screen by block. Returns false when the
// sprite has another shader (the caller draws it normally).
bool draw_background_sprite(Sprite *s);

// G.SPLASH_FRONT:draw() with flash.fs: its output is fully transparent outside a disc around the
// screen centre whose radius only depends on time, so only that disc's bounding box is drawn
// (same pixels). Returns false when the sprite has another shader.
bool draw_flash_sprite(Sprite *s);

struct CrtParams {
    float crt;  // G.SETTINGS.GRAPHICS.crt * 0.3
    float time;
    float mouse_x, mouse_y;
    float screen_scale;
};
void crt_pass(gfx::Canvas *canvas, const CrtParams &p);

}  // namespace vl::render
