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
    BgField, BgColor, CrtFast, VlPolychrome, CrtMap, BgMerged, BgField8, BgColor8, SplashField, SplashColor, VlDissolve, VlDissolveBaked,
    VlDissolveField,  // bakes a sprite's dissolve field (the pre-pass of vl_dissolve_baked and vl_dm_*)
    VlHologramGlow,   // bakes hologram.fs's glow for a sprite (the pre-pass of the hologram variants)
    // the card shaders with dissolve == 0 (vl_d0_*: no dissolve mask, fast trig), then while
    // dissolving (vl_dm_*: the mask from the baked field); same order as kShaders in shaders.cpp
    VlD0Dissolve, VlD0Foil, VlD0Holo, VlD0Polychrome, VlD0Negative, VlD0NegativeShine, VlD0Booster, VlD0Voucher, VlD0Debuff, VlD0Played, VlD0Hologram,
    VlDmFoil, VlDmHolo, VlDmPolychrome, VlDmNegative, VlDmNegativeShine, VlDmBooster, VlDmVoucher, VlDmDebuff, VlDmPlayed, VlDmHologram,
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
    bool f16_target = false;  // renders into an RGBA16F canvas (no blending)
    bool blend = true;        // false: writes replace the target (love 'replace' blend)
};
const ShaderInfo &shader_info(ShaderId id);
// The vl_d0_* variant of a card shader (same output while dissolve == 0), or the shader itself.
ShaderId dissolve_zero_variant(ShaderId id);
// The vl_dm_* variant of a card effect shader (vl_dissolve's mask while dissolving), or the shader itself.
ShaderId dissolve_fast_variant(ShaderId id);
// Shaders that take vl_dissolve's CPU-side terms (vl_dis_*).
bool is_fast_dissolve(ShaderId id);
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
    bool repeat = false;          // wrap mode: repeat instead of clamp
    bool half_float = false;      // rgba holds 4 IEEE halfs per texel (RGBA16F, no mipmaps)
    bool alpha8 = false;          // rgba holds 1 byte per texel: sampled as (1, 1, 1, a)
    int gxm_layout = 0;           // Vita layout diagnostics (see backend_gxm.cpp), 0 = default
    // When >1, the data is a 1x source of an atlas that is a nearest-neighbour upscale by this
    // factor (Balatro's 2x atlases are exactly that); it is expanded at load time.
    int virtual_scale = 1;
    float dpi_scale = 1;          // love Image dpiscale: logical size = uploaded size / dpi
};

bool init();  // creates the display / context
void set_vsync(bool on);

Texture *create_texture(const TextureDesc &desc);
void destroy_texture(Texture *t);
// true when create_texture may be called from a worker thread (GXM: yes, GL: no)
bool create_texture_any_thread();
uint16_t float_to_half(float f);  // IEEE half, round to nearest, tiny values flushed to zero
void texture_dimensions(const Texture *t, float &w, float &h);  // love getDimensions()

enum class CanvasFormat : uint8_t { RGBA8, RGBA16F };
Canvas *create_canvas(int w, int h, CanvasFormat fmt = CanvasFormat::RGBA8, Filter filter = Filter::Linear);
Texture *canvas_texture(Canvas *c);
// Small canvases reused across owners: a released canvas becomes available again next frame (this
// frame's draws may still read it).
Canvas *acquire_canvas(int w, int h, CanvasFormat fmt, Filter filter);
void release_canvas(Canvas *c);
// Records one full-canvas quad with shader `sh` (its current uniforms) into the frame's pre-pass,
// which the GPU renders before everything else of the frame: c can be sampled by any draw of
// this frame.
void prepass_fill(Canvas *c, ShaderId sh, Texture *tex = nullptr);
// Counts frames (begin_frame).
uint32_t frame_index();

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
void transform_by(const Affine &m);  // current = current * m
inline void scale(float s) { scale(s, s); }
const Affine &transform();

void set_color(Color c);
Color color();
void set_line_width(float w);

// Shaders
void set_shader(ShaderId id);  // ShaderId::Default == love.graphics.setShader()
ShaderId current_shader();
// Extra samplers vl_Aux1..vl_Aux3 for internal shaders (slots 0..2).
constexpr int kAuxSlots = 3;
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
    float px[(int)ShaderId::Count];  // pixels covered per shader (overdraw included)
};
// psp2shaderperf cycles per pixel of a fragment shader, per-instance mode counted 3x (measured ratio)
float shader_cycles(ShaderId id);
// GPU time a frame's shading would take, from px * cycles (1 cycle over 960x544 = 0.147 ms on device)
inline float gpu_estimate_ms(const FrameStats &s, int i) { return s.px[i] * shader_cycles((ShaderId)i) * 2.815e-7f; }
const FrameStats &last_frame_stats();
extern bool g_draw_debug;  // PC diagnostics: print the large draws
// Opaque UI panels hide what was drawn under them from the GPU (depth pre-pass); same image.
extern bool g_occlusion;

}  // namespace vl::gfx
