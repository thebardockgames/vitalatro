// API-agnostic part of the renderer: LOVE-compatible state, CPU-side vertex generation and
// automatic batching. Geometry generation follows LOVE 11.5 (zlib license) so that output
// matches it vertex for vertex.
#include "gfx/gfx.h"

#include <cstring>
#include <vector>

#include "core/time.h"
#include "gfx/backend.h"

namespace vl::gfx {
namespace {

struct State {
    std::vector<Affine> stack{Affine{}};
    Color color{1, 1, 1, 1};
    float line_width = 1;
    ShaderId shader = ShaderId::Default;
    Canvas *canvas = nullptr;
    Texture *aux[kAuxSlots] = {};
    float uniforms[(int)ShaderId::Count][kMaxUniformFloats] = {};
    bool target_dirty = true;
} S;

FrameData F;
FrameStats stats_last{}, stats_cur{};
Texture *white_tex = nullptr;

Affine &top() { return S.stack.back(); }

void ensure_target() {
    if (!S.target_dirty) return;
    S.target_dirty = false;
    Cmd c{};
    c.type = CmdType::Target;
    c.target = S.canvas;
    F.cmds.push_back(c);
}

// Reserves room for a draw, merging with the previous command when the state is identical.
// Returns the base vertex index (relative to the command) for the caller's indices.
uint16_t begin_draw(Texture *tex, uint32_t nverts, uint32_t nindices, uint16_t **out_idx,
                    Vertex **out_vtx) {
    ensure_target();
    const ShaderInfo &si = shader_info(S.shader);
    const float *u = S.uniforms[(int)S.shader];

    Cmd *last = F.cmds.empty() ? nullptr : &F.cmds.back();
    bool merge = last && last->type == CmdType::Draw && last->shader == S.shader &&
                 last->texture == tex && std::memcmp(last->aux, S.aux, sizeof S.aux) == 0 &&
                 last->vertex_count + nverts <= 65535;
    if (merge && S.shader != ShaderId::Default)
        merge = std::memcmp(&F.uniforms[last->uniforms], u, si.block_floats * sizeof(float)) == 0;

    if (!merge) {
        Cmd c{};
        c.type = CmdType::Draw;
        c.shader = S.shader;
        c.texture = tex;
        std::memcpy(c.aux, S.aux, sizeof S.aux);
        c.uniforms = (uint32_t)F.uniforms.size();
        F.uniforms.insert(F.uniforms.end(), u, u + si.block_floats);
        c.first_vertex = (uint32_t)F.vertices.size();
        c.first_index = (uint32_t)F.indices.size();
        F.cmds.push_back(c);
        last = &F.cmds.back();
        stats_cur.draw_calls++;
    } else {
        stats_cur.batches_merged++;
    }
    uint16_t base = (uint16_t)last->vertex_count;
    last->vertex_count += nverts;
    last->index_count += nindices;
    size_t vo = F.vertices.size(), io = F.indices.size();
    F.vertices.resize(vo + nverts);
    F.indices.resize(io + nindices);
    *out_vtx = &F.vertices[vo];
    *out_idx = &F.indices[io];
    stats_cur.vertices += nverts;
    return base;
}

void emit_quads_indices(uint16_t *idx, uint16_t base, int quads) {
    for (int i = 0; i < quads; i++) {
        uint16_t v = (uint16_t)(base + i * 4);
        idx[0] = v; idx[1] = v + 1; idx[2] = v + 2;
        idx[3] = v + 2; idx[4] = v + 1; idx[5] = v + 3;
        idx += 6;
    }
}

// Untextured geometry in local space, drawn as a triangle fan or strip.
enum class Topo { Fan, Strip };
void draw_untextured(const Vec2 *pts, int n, Topo topo) {
    if (n < 3) return;
    uint16_t *idx;
    Vertex *v;
    uint16_t base = begin_draw(white_tex, (uint32_t)n, (uint32_t)(3 * (n - 2)), &idx, &v);
    const Affine &t = top();
    uint32_t col = S.color.packed();
    for (int i = 0; i < n; i++) {
        Vec2 p = t.apply(pts[i]);
        v[i] = {p.x, p.y, 0.5f, 0.5f, col};
    }
    int k = 0;
    if (topo == Topo::Fan) {
        for (int i = 2; i < n; i++) {
            idx[k++] = base;
            idx[k++] = (uint16_t)(base + i - 1);
            idx[k++] = (uint16_t)(base + i);
        }
    } else {
        for (int i = 0; i < n - 2; i++) {
            idx[k++] = (uint16_t)(base + i);
            idx[k++] = (uint16_t)(base + i + 1 + (i & 1));
            idx[k++] = (uint16_t)(base + i + 2 - (i & 1));
        }
    }
}

// love::graphics::MiterJoinPolyline, 'rough' line style (no overdraw).
const float LINES_PARALLEL_EPS = 0.05f;
void miter_edge(std::vector<Vec2> &anchors, std::vector<Vec2> &normals, Vec2 &segment,
                float &seg_len, Vec2 &seg_normal, Vec2 a, Vec2 b, float hw) {
    Vec2 ns = b - a;
    float nlen = ns.length();
    if (nlen == 0.0f) return;
    Vec2 nn = ns.normal(hw / nlen);
    anchors.push_back(a);
    anchors.push_back(a);
    float det = Vec2::cross(segment, ns);
    if (std::fabs(det) / (seg_len * nlen) < LINES_PARALLEL_EPS) {
        normals.push_back(seg_normal);
        normals.push_back(-seg_normal);
        if (Vec2::dot(segment, ns) < 0) {
            anchors.push_back(a);
            anchors.push_back(a);
            normals.push_back(-seg_normal);
            normals.push_back(seg_normal);
        }
    } else {
        float lambda = Vec2::cross(nn - seg_normal, ns) / det;
        Vec2 d = seg_normal + segment * lambda;
        normals.push_back(d);
        normals.push_back(-d);
    }
    segment = ns;
    seg_normal = nn;
    seg_len = nlen;
}

void polyline(const Vec2 *c, int count) {
    if (count < 2) return;
    static std::vector<Vec2> anchors, normals, verts;
    anchors.clear(); normals.clear(); verts.clear();
    float hw = S.line_width * 0.5f;
    bool looping = c[0] == c[count - 1];
    Vec2 segment = looping ? c[0] - c[count - 2] : c[1] - c[0];
    float seg_len = segment.length();
    Vec2 seg_normal = segment.normal(hw / seg_len);
    Vec2 pa, pb = c[0];
    for (int i = 0; i + 1 < count; i++) {
        pa = pb;
        pb = c[i + 1];
        miter_edge(anchors, normals, segment, seg_len, seg_normal, pa, pb, hw);
    }
    pa = pb;
    pb = looping ? c[1] : pb + segment;
    miter_edge(anchors, normals, segment, seg_len, seg_normal, pa, pb, hw);
    for (size_t i = 0; i < normals.size(); i++) verts.push_back(anchors[i] + normals[i]);
    draw_untextured(verts.data(), (int)verts.size(), Topo::Strip);
}

}  // namespace

// ---------------------------------------------------------------------------------------------

Texture *create_texture(const TextureDesc &desc) {
    // Balatro's 2x atlases are exact nearest-neighbour upscales of the 1x ones, so they ship as
    // 1x and are expanded here; the GPU then filters a real 2x image exactly like LOVE does.
    std::vector<uint8_t> up;
    TextureDesc d = desc;
    if (d.virtual_scale > 1 && !d.alpha8 && !d.half_float) {
        int s = d.virtual_scale, w = d.width * s, h = d.height * s;
        up.resize((size_t)w * h * 4);
        const uint32_t *src = (const uint32_t *)d.rgba;
        uint32_t *dst = (uint32_t *)up.data();
        for (int y = 0; y < h; y++) {
            const uint32_t *row = src + (size_t)(y / s) * d.width;
            for (int x = 0; x < w; x++) dst[(size_t)y * w + x] = row[x / s];
        }
        d.width = w;
        d.height = h;
        d.rgba = up.data();
    }
    Texture *t = new Texture();
    t->pw = d.width;
    t->ph = d.height;
    t->virtual_scale = desc.virtual_scale;
    t->lw = (float)d.width / d.dpi_scale;
    t->lh = (float)d.height / d.dpi_scale;
    t->filter = d.filter;
    backend_create_texture(t, d);
    return t;
}

bool create_texture_any_thread() { return backend_texture_thread_safe(); }

void destroy_texture(Texture *t) {
    if (!t) return;
    backend_destroy_texture(t);
    delete t;
}

void texture_dimensions(const Texture *t, float &w, float &h) { w = t->lw; h = t->lh; }

Canvas *create_canvas(int w, int h, CanvasFormat fmt, Filter filter) {
    Canvas *c = new Canvas();
    c->w = w;
    c->h = h;
    c->format = fmt;
    c->filter = filter;
    c->tex.filter = filter;
    c->tex.pw = w;
    c->tex.ph = h;
    c->tex.lw = (float)w;
    c->tex.lh = (float)h;
    c->tex.is_canvas = true;
    backend_create_canvas(c);
    return c;
}

Texture *canvas_texture(Canvas *c) { return &c->tex; }

bool init() {
    if (!backend_init()) return false;
    const uint8_t white[4] = {255, 255, 255, 255};
    TextureDesc d{1, 1, white};
    white_tex = create_texture(d);
    return true;
}

void set_vsync(bool on) { backend_set_vsync(on); }

void begin_frame() {
    F.reset();
    stats_cur = {};
    S.stack.assign(1, Affine{});
    S.canvas = nullptr;
    S.target_dirty = true;
}

void end_frame() {
    double t0 = time_now();
    backend_submit(F);
    stats_cur.cpu_ms_submit = (float)((time_now() - t0) * 1000.0);
    stats_last = stats_cur;
}

int screen_width() { return backend_screen_width(); }
int screen_height() { return backend_screen_height(); }

void set_canvas(Canvas *c) {
    if (c == S.canvas && !S.target_dirty) return;
    S.canvas = c;
    S.target_dirty = true;
}

void clear(Color c) {
    ensure_target();
    Cmd cmd{};
    cmd.type = CmdType::Clear;
    cmd.clear_color = c;
    F.cmds.push_back(cmd);
}

void push() { S.stack.push_back(top()); }
void pop() { if (S.stack.size() > 1) S.stack.pop_back(); }
void origin() { top() = Affine{}; }
void translate(float x, float y) { top().translate(x, y); }
void rotate(float r) { top().rotate(r); }
void scale(float sx, float sy) { top().scale(sx, sy); }
void transform_by(const Affine &m) { top() = top() * m; }
const Affine &transform() { return top(); }

void set_color(Color c) { S.color = c; }
Color color() { return S.color; }
void set_line_width(float w) { S.line_width = w; }

void set_shader(ShaderId id) {
    if (id != S.shader) stats_cur.shader_switches++;
    S.shader = id;
}
ShaderId current_shader() { return S.shader; }
void set_aux_texture(int slot, Texture *t) { S.aux[slot] = t; }

void send(ShaderId id, const char *name, const float *v, int n) {
    const ShaderInfo &si = shader_info(id);
    for (int i = 0; i < si.uniform_count; i++) {
        const UniformDesc &u = si.uniforms[i];
        if (std::strcmp(u.name, name) == 0) {
            int cnt = n < u.components ? n : u.components;
            std::memcpy(&S.uniforms[(int)id][u.offset], v, cnt * sizeof(float));
            return;
        }
    }
}

void draw(Texture *t, const Quad &q, float x, float y, float r, float sx, float sy, float ox,
          float oy) {
    uint16_t *idx;
    Vertex *v;
    uint16_t base = begin_draw(t, 4, 6, &idx, &v);
    Affine m = top() * Affine::from_transformation(x, y, r, sx, sy, ox, oy);
    uint32_t col = S.color.packed();
    float u0 = q.x / q.sw, v0 = q.y / q.sh, u1 = (q.x + q.w) / q.sw, v1 = (q.y + q.h) / q.sh;
    Vec2 p0 = m.apply(0, 0), p1 = m.apply(0, q.h), p2 = m.apply(q.w, 0), p3 = m.apply(q.w, q.h);
    v[0] = {p0.x, p0.y, u0, v0, col};
    v[1] = {p1.x, p1.y, u0, v1, col};
    v[2] = {p2.x, p2.y, u1, v0, col};
    v[3] = {p3.x, p3.y, u1, v1, col};
    emit_quads_indices(idx, base, 1);
}

void draw(Texture *t, float x, float y, float r, float sx, float sy, float ox, float oy) {
    Quad q{0, 0, t->lw, t->lh, t->lw, t->lh};
    draw(t, q, x, y, r, sx, sy, ox, oy);
}

void draw(Canvas *c, float x, float y) { draw(&c->tex, x, y); }

void rectangle_fill(float x, float y, float w, float h) {
    Vec2 c[5] = {{x, y}, {x, y + h}, {x + w, y + h}, {x + w, y}, {x, y}};
    draw_untextured(c, 5, Topo::Fan);
}

void rectangle_line(float x, float y, float w, float h) {
    Vec2 c[5] = {{x, y}, {x, y + h}, {x + w, y + h}, {x + w, y}, {x, y}};
    polyline(c, 5);
}

void polygon_fill(const float *xy, int n) {
    static std::vector<Vec2> pts;
    pts.resize(n + 1);
    for (int i = 0; i < n; i++) pts[i] = {xy[i * 2], xy[i * 2 + 1]};
    pts[n] = pts[0];
    draw_untextured(pts.data(), n + 1, Topo::Fan);
}

void polygon_line(const float *xy, int n) {
    static std::vector<Vec2> pts;
    pts.resize(n + 1);
    for (int i = 0; i < n; i++) pts[i] = {xy[i * 2], xy[i * 2 + 1]};
    pts[n] = pts[0];
    polyline(pts.data(), n + 1);
}

void draw_glyph_quads(Texture *t, const GlyphVertex *gv, int quad_count) {
    const Affine &m = top();
    uint32_t col = S.color.packed();
    while (quad_count > 0) {
        int nq = quad_count > 4096 ? 4096 : quad_count;
        uint16_t *idx;
        Vertex *v;
        uint16_t base = begin_draw(t, nq * 4, nq * 6, &idx, &v);
        for (int i = 0; i < nq * 4; i++) {
            Vec2 p = m.apply(gv[i].x, gv[i].y);
            v[i] = {p.x, p.y, gv[i].u, gv[i].v, col};
        }
        emit_quads_indices(idx, base, nq);
        gv += nq * 4;
        quad_count -= nq;
    }
}

const FrameStats &last_frame_stats() { return stats_last; }

uint16_t float_to_half(float f) {
    uint32_t x;
    std::memcpy(&x, &f, 4);
    uint32_t sign = (x >> 16) & 0x8000, mant = x & 0x7fffff;
    int exp = (int)((x >> 23) & 0xff) - 127 + 15;
    if (exp <= 0) return (uint16_t)sign;
    if (exp >= 31) return (uint16_t)(sign | 0x7c00);
    uint32_t h = sign | ((uint32_t)exp << 10) | (mant >> 13);
    if (mant & 0x1000) h++;
    return (uint16_t)h;
}

}  // namespace vl::gfx
