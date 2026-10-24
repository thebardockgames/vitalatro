#pragma once
// Interface between the API-agnostic recorder (gfx.cpp) and a GPU backend (GXM or GL).
#include <string>
#include <utility>
#include <vector>
#include "gfx/gfx.h"

namespace vl::gfx {

struct Texture {
    int pw = 0, ph = 0;    // pixels stored on the GPU
    float lw = 0, lh = 0;  // love "dimensions"
    int virtual_scale = 1;
    int mip_levels = 1;
    Filter filter = Filter::Linear;
    bool is_canvas = false;
    void *impl = nullptr;
};

struct Canvas {
    int w = 0, h = 0;
    CanvasFormat format = CanvasFormat::RGBA8;
    Filter filter = Filter::Linear;
    Texture tex;
    void *impl = nullptr;
};

struct Vertex {
    float x, y;  // target pixel space (transform already applied)
    float u, v;
    uint32_t color;  // RGBA8
};

enum class CmdType : uint8_t { Target, Clear, Draw };

struct Cmd {
    CmdType type;
    ShaderId shader;
    Canvas *target;      // Target
    Color clear_color;   // Clear
    Texture *texture;    // Draw
    Texture *aux[kAuxSlots];  // Draw: vl_Aux1..vl_Aux3
    uint32_t uniforms;   // Draw: offset into FrameData::uniforms
    uint32_t first_vertex, vertex_count;
    uint32_t first_index, index_count;  // indices are relative to first_vertex
};

// An axis-aligned rectangle (integer pixel edges) that draw command `cmd` paints fully opaque:
// whatever earlier commands of the same target draw inside it is never seen. Backends may skip
// shading it (depth pre-pass).
struct Occluder {
    uint32_t cmd;
    float x0, y0, x1, y1;
    bool all = false;  // hides `cmd` itself too (a canvas region nobody will see)
};

// Depth of command i in a scene with occluders: later commands are nearer.
inline float occlusion_depth(uint32_t cmd) { return 1.0f - (float)(cmd + 1) * (1.0f / 1048576.0f); }

struct FrameData {
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;
    std::vector<float> uniforms;
    std::vector<Cmd> cmds;
    std::vector<Occluder> occluders;  // in cmd order
    void reset() { vertices.clear(); indices.clear(); uniforms.clear(); cmds.clear(); occluders.clear(); }
};

// Implemented by each backend.
bool backend_init();
void backend_set_vsync(bool on);
int backend_screen_width();
int backend_screen_height();
void backend_create_texture(Texture *t, const TextureDesc &d);
bool backend_texture_thread_safe();
void backend_destroy_texture(Texture *t);
void backend_create_canvas(Canvas *c);
void backend_submit(const FrameData &f);  // render everything, then present
void backend_read_screen(std::vector<uint8_t> &out);  // last presented frame, RGBA top-down
// Dev server: replaces compiled shader files (by name, e.g. "flame_f") and rebuilds every program.
// Returns "" or what went wrong (nothing is replaced then).
std::string backend_reload_shaders(const std::vector<std::pair<std::string, std::string>> &files);

}  // namespace vl::gfx
