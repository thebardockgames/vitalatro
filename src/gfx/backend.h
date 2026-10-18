#pragma once
// Interface between the API-agnostic recorder (gfx.cpp) and a GPU backend (GXM or GL).
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

struct FrameData {
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;
    std::vector<float> uniforms;
    std::vector<Cmd> cmds;
    void reset() { vertices.clear(); indices.clear(); uniforms.clear(); cmds.clear(); }
};

// Implemented by each backend.
bool backend_init();
void backend_set_vsync(bool on);
int backend_screen_width();
int backend_screen_height();
void backend_create_texture(Texture *t, const TextureDesc &d);
void backend_destroy_texture(Texture *t);
void backend_create_canvas(Canvas *c);
void backend_submit(const FrameData &f);  // render everything, then present

}  // namespace vl::gfx
