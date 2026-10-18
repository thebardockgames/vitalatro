#pragma once
// Immediate-mode 2D renderer that reproduces the subset of love.graphics (LOVE 11.5)
// Balatro uses, with identical geometry: transforms are applied on the CPU exactly like
// LOVE's automatic batching does, so shaders see vertex positions in target pixel space.
#include <cstddef>
#include <cstdint>
#include "core/math.h"

namespace vl::gfx {

struct Texture;
struct Canvas;

// Every shader Balatro ships, plus our own internal programs.
enum class ShaderId : uint8_t {
    Default,  // love's default: Texel(tex, uv) * color
    Background, CRT, Dissolve, Foil, Holo, Polychrome, Negative, NegativeShine,
    Booster, Voucher, Debuff, Played, Hologram, Vortex, Flame, Flash, Splash, GoldSeal, Skew,
    // Vitalatro's own programs (shaders/internal)
    BgField, BgColor, BgFinal, CrtFast,
    Count
};

// Uniform storage. Values persist per shader across draws exactly like Shader:send does in LOVE.
struct UniformDesc {
    const char *name;
    uint8_t offset;      // in floats inside the shader's uniform block
    uint8_t components;  // 1..4
};
struct ShaderInfo {
    const char *name;
    const UniformDesc *uniforms;
    uint8_t uniform_count;
    uint8_t block_floats;  // total floats in the block
    bool custom_vertex;    // has the 'hovering' tilt position() function
};
const ShaderInfo &shader_info(ShaderId id);
constexpr int kMaxUniformFloats = 48;

struct Quad {  // love Quad: viewport in texture "dimension" units
    float x, y, w, h;
    float sw, sh;  // reference dimensions used for the texcoords
};

enum class Filter : uint8_t { Linear, Nearest };

struct TextureDesc {
    int width, height;            // pixel size of the uploaded data
    const uint8_t *rgba;          // width*height*4
    bool mipmaps = false;
    Filter filter = Filter::Linear;
    // When >1, the data is a 1x source of an atlas that is a nearest-neighbour upscale by this
    // factor (Balatro's 2x atlases are exactly that); it is expanded at load time.
    int virtual_scale = 1;
    float dpi_scale = 1;          // love Image dpiscale: logical size = uploaded size / dpi
};

bool init();  // creates the display / context
void set_vsync(bool on);

Texture *create_texture(const TextureDesc &desc);
void destroy_texture(Texture *t);
void texture_dimensions(const Texture *t, float &w, float &h);  // love getDimensions()

Canvas *create_canvas(int w, int h);
Texture *canvas_texture(Canvas *c);

// Frame
void begin_frame();
void end_frame();  // submits and presents
int screen_width();
int screen_height();

// love.graphics.setCanvas / clear
void set_canvas(Canvas *c);  // nullptr = backbuffer
void clear(Color c);

// Transform stack (love.graphics.push/pop/... with the 'transform' stack type)
void push();
void pop();
void origin();
void translate(float x, float y);
void rotate(float r);
void scale(float sx, float sy);
inline void scale(float s) { scale(s, s); }
const Affine &transform();

void set_color(Color c);
Color color();
void set_line_width(float w);

// Shaders
void set_shader(ShaderId id);  // ShaderId::Default == love.graphics.setShader()
ShaderId current_shader();
// Extra samplers vl_Aux1 / vl_Aux2 for internal shaders (slot 0 / 1).
void set_aux_texture(int slot, Texture *t);
void send(ShaderId id, const char *uniform, const float *v, int n);
inline void send(ShaderId id, const char *u, float v) { send(id, u, &v, 1); }
inline void send(ShaderId id, const char *u, float x, float y) { float v[2] = {x, y}; send(id, u, v, 2); }

// Drawing
void draw(Texture *t, const Quad &q, float x, float y, float r = 0, float sx = 1, float sy = 1,
          float ox = 0, float oy = 0);
void draw(Texture *t, float x, float y, float r = 0, float sx = 1, float sy = 1, float ox = 0,
          float oy = 0);
void draw(Canvas *c, float x, float y);
void rectangle_fill(float x, float y, float w, float h);
void rectangle_line(float x, float y, float w, float h);
// coords: n points (x,y pairs); closed like love.graphics.polygon
void polygon_fill(const float *xy, int n);
void polygon_line(const float *xy, int n);

// Raw textured triangles in local space, used by text rendering.
struct GlyphVertex { float x, y, u, v; };
void draw_glyph_quads(Texture *t, const GlyphVertex *v, int quad_count);

struct FrameStats {
    int draw_calls, batches_merged, vertices, shader_switches;
    float cpu_ms_submit;
};
const FrameStats &last_frame_stats();

}  // namespace vl::gfx
