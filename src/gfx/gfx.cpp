// API-agnostic part of the renderer: LOVE-compatible state, CPU-side vertex generation and
// automatic batching. Geometry generation follows LOVE 11.5 (zlib license) so that output
// matches it vertex for vertex.
#include "gfx/gfx.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <unordered_map>
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
FrameData P;  // the pre-pass, submitted before F
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

// Share of the axis-aligned bounds of n points that lies inside the current target (the GPU
// clips the rest): used for the shading estimate only.
float visible_share(const float *xy, int n, int stride = 2) {
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    for (int i = 0; i < n; i++) {
        float x = xy[i * stride], y = xy[i * stride + 1];
        x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y);
    }
    float w = x1 - x0, h = y1 - y0;
    if (w <= 0 || h <= 0) return 0;
    float tw = (float)(S.canvas ? S.canvas->w : backend_screen_width()), th = (float)(S.canvas ? S.canvas->h : backend_screen_height());
    float cw = std::min(x1, tw) - std::max(x0, 0.f), ch = std::min(y1, th) - std::max(y0, 0.f);
    return cw > 0 && ch > 0 ? cw * ch / (w * h) : 0.f;
}
float visible_share(const Vec2 *p, int n) { return visible_share(&p[0].x, n, sizeof(Vec2) / sizeof(float)); }

// Reserves room for a draw, merging with the previous command when the state is identical.
// Returns the base vertex index (relative to the command) for the caller's indices.
uint16_t begin_draw(Texture *tex, uint32_t nverts, uint32_t nindices, uint16_t **out_idx,
                    Vertex **out_vtx, bool split = false) {
    ensure_target();
    const ShaderInfo &si = shader_info(S.shader);
    const float *u = S.uniforms[(int)S.shader];

    Cmd *last = F.cmds.empty() ? nullptr : &F.cmds.back();
    bool merge = last && last->type == CmdType::Draw && last->shader == S.shader &&
                 last->texture == tex && std::memcmp(last->aux, S.aux, sizeof S.aux) == 0 &&
                 last->vertex_count + nverts <= 65535 && !split;
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

// An opaque fan (UI panels: Balatro's pixel-stepped rounded rectangles in alpha 1) occludes what
// was drawn under it: record a large centred axis-aligned rectangle it covers, snapped inwards to
// whole pixels.
bool find_occluder(const Vertex *v, int n, float r[4]) {
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    for (int i = 0; i < n; i++)
        x0 = std::min(x0, v[i].x), x1 = std::max(x1, v[i].x), y0 = std::min(y0, v[i].y), y1 = std::max(y1, v[i].y);
    if ((x1 - x0) * (y1 - y0) < 16384) return false;
    // All fan triangles wound the same way: the area they cover is where the closed outline
    // v0..v[n-1] has a nonzero winding number.
    float sign = 0;
    for (int i = 2; i < n; i++) {
        float cr = (v[i - 1].x - v[0].x) * (v[i].y - v[0].y) - (v[i - 1].y - v[0].y) * (v[i].x - v[0].x);
        if (std::fabs(cr) < 1e-3f) continue;
        if (sign == 0) sign = cr;
        else if ((cr > 0) != (sign > 0)) return false;
    }
    if (sign == 0) return false;
    // outline edges, without degenerate ones and edge pairs traversed both ways (the spokes to
    // a centre vertex), which cancel out
    static std::vector<std::array<float, 4>> E;
    E.clear();
    for (int i = 0; i < n; i++) {
        const Vertex &a = v[i], &b = v[(i + 1) % n];
        if (a.x == b.x && a.y == b.y) continue;
        bool cancelled = false;
        for (auto &e : E)
            if (e[0] == b.x && e[1] == b.y && e[2] == a.x && e[3] == a.y) {
                e = E.back();
                E.pop_back();
                cancelled = true;
                break;
            }
        if (!cancelled) E.push_back({a.x, a.y, b.x, b.y});
    }
    auto winding = [&](float px, float py) {
        int w = 0;
        for (auto &e : E) {
            float cr = (e[2] - e[0]) * (py - e[1]) - (e[3] - e[1]) * (px - e[0]);
            if (e[1] <= py) { if (e[3] > py && cr > 0) w++; }
            else if (e[3] <= py && cr < 0) w--;
        }
        return w;
    };
    // does a segment enter the open box (Liang-Barsky)?
    auto crosses = [](const std::array<float, 4> &e, float bx0, float by0, float bx1, float by1) {
        float t0 = 0, t1 = 1, dx = e[2] - e[0], dy = e[3] - e[1];
        const float p[4] = {-dx, dx, -dy, dy}, q[4] = {e[0] - bx0, bx1 - e[0], e[1] - by0, by1 - e[1]};
        for (int k = 0; k < 4; k++) {
            if (p[k] == 0) {
                if (q[k] <= 0) return false;
                continue;
            }
            float t = q[k] / p[k];
            if (p[k] < 0) t0 = std::max(t0, t);
            else t1 = std::min(t1, t);
            if (t0 >= t1) return false;
        }
        return true;
    };
    // a rectangle is covered when no edge enters it and its centre has a nonzero winding (the
    // winding number is then the same all over it); pixel centres lie 0.5 inside its edges
    auto covered = [&](const float r[4]) {
        if (winding((r[0] + r[2]) * 0.5f, (r[1] + r[3]) * 0.5f) == 0) return false;
        for (auto &e : E)
            if (crosses(e, r[0] + 0.25f, r[1] + 0.25f, r[2] - 0.25f, r[3] - 0.25f)) return false;
        return true;
    };
    auto snapped = [&](float t, float r[4]) {
        r[0] = std::ceil(x0 + t), r[1] = std::ceil(y0 + t), r[2] = std::floor(x1 - t), r[3] = std::floor(y1 - t);
        return r[2] > r[0] + 1 && r[3] > r[1] + 1 && covered(r);
    };
    // the smallest uniform inset that is covered (about a rounded rectangle's corner radius)
    float lo = 0, hi = std::min(x1 - x0, y1 - y0) * 0.5f;
    if (!snapped(hi * 0.9f, r)) return false;
    hi *= 0.9f;
    for (int it = 0; it < 12; it++) {
        float mid = (lo + hi) * 0.5f;
        if (snapped(mid, r)) hi = mid;
        else lo = mid;
    }
    snapped(hi, r);
    // only what lies inside the target matters
    float tw = (float)(S.canvas ? S.canvas->w : backend_screen_width()), th = (float)(S.canvas ? S.canvas->h : backend_screen_height());
    r[0] = std::max(r[0], 0.f), r[1] = std::max(r[1], 0.f), r[2] = std::min(r[2], tw), r[3] = std::min(r[3], th);
    return (r[2] - r[0]) * (r[3] - r[1]) >= 8192;
}

// Untextured geometry in local space, drawn as a triangle fan or strip.
enum class Topo { Fan, Strip };
void draw_untextured(const Vec2 *pts, int n, Topo topo) {
    if (n < 3) return;
    static std::vector<Vertex> tv;
    tv.resize(n);
    const Affine &t = top();
    uint32_t col = S.color.packed();
    float area2 = 0;
    for (int i = 0; i < n; i++) {
        Vec2 p = t.apply(pts[i]);
        tv[i] = {p.x, p.y, 0.5f, 0.5f, col};
        if (i >= 2) area2 += (tv[i - 1].x - tv[0].x) * (p.y - tv[0].y) - (tv[i - 1].y - tv[0].y) * (p.x - tv[0].x);
    }
    // an occluder starts its own command, so that it also hides what its batch drew before it
    float r[4];
    bool occ = g_occlusion && topo == Topo::Fan && S.shader == ShaderId::Default && S.color.a >= 1.0f &&
               find_occluder(tv.data(), n, r);
    uint16_t *idx;
    Vertex *v;
    uint16_t base = begin_draw(white_tex, (uint32_t)n, (uint32_t)(3 * (n - 2)), &idx, &v, occ);
    std::memcpy(v, tv.data(), n * sizeof(Vertex));
    if (occ) {
        F.occluders.push_back({(uint32_t)F.cmds.size() - 1, r[0], r[1], r[2], r[3]});
        if (g_draw_debug) std::fprintf(stderr, "  occluder cmd %u: %.0f,%.0f - %.0f,%.0f\n", (unsigned)F.cmds.size() - 1, r[0], r[1], r[2], r[3]);
    }
    float vis = std::fabs(area2) * 0.5f * visible_share(&v[0].x, n, sizeof(Vertex) / sizeof(float));
    stats_cur.px[(int)S.shader] += vis;
    if (g_draw_debug && vis > 20000)
        std::fprintf(stderr, "  poly %s %.0fk n %d col %.2f,%.2f,%.2f,%.2f\n", shader_info(S.shader).name, vis / 1000, n, S.color.r, S.color.g,
                     S.color.b, S.color.a);
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
    if (!desc.alpha8 && !desc.half_float) {
        t->opaque_w = desc.width;
        t->opaque_h = desc.height;
        t->opaque_words = (desc.width + 63) / 64;
        t->opaque.assign((size_t)t->opaque_words * desc.height, 0);
        for (int y = 0; y < desc.height; y++)
            for (int x = 0; x < desc.width; x++)
                if (desc.rgba[((size_t)y * desc.width + x) * 4 + 3] == 255) t->opaque[(size_t)y * t->opaque_words + x / 64] |= 1ull << (x % 64);
    }
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

namespace {
struct CanvasPool {
    std::vector<Canvas *> free, released;  // released: available after this frame
} canvas_pool;
}  // namespace

Canvas *acquire_canvas(int w, int h, CanvasFormat fmt, Filter filter) {
    auto &fr = canvas_pool.free;
    for (size_t i = 0; i < fr.size(); i++)
        if (fr[i]->w == w && fr[i]->h == h && fr[i]->format == fmt && fr[i]->filter == filter) {
            Canvas *c = fr[i];
            fr.erase(fr.begin() + i);
            return c;
        }
    return create_canvas(w, h, fmt, filter);
}

void release_canvas(Canvas *c) {
    if (c) canvas_pool.released.push_back(c);
}

void prepass_fill(Canvas *c, ShaderId sh, Texture *tex) {
    std::swap(F, P);
    Canvas *canvas = S.canvas;
    const bool dirty = S.target_dirty;
    const ShaderId shader = S.shader;
    const Color color = S.color;
    S.stack.push_back(Affine{});
    S.canvas = c;
    S.target_dirty = true;
    S.shader = sh;
    S.color = {1, 1, 1, 1};
    if (tex) draw(tex, Quad{0, 0, 1, 1, 1, 1}, 0, 0, 0, (float)c->w, (float)c->h);  // texture coordinates 0..1
    else draw(white_tex, 0, 0, 0, (float)c->w, (float)c->h);
    S.stack.pop_back();
    S.canvas = canvas;
    S.target_dirty = dirty;  // F's own target is unchanged
    S.shader = shader;
    S.color = color;
    std::swap(F, P);
}

bool init() {
    if (!backend_init()) return false;
    const uint8_t white[4] = {255, 255, 255, 255};
    TextureDesc d{1, 1, white};
    white_tex = create_texture(d);
    return true;
}

void set_vsync(bool on) { backend_set_vsync(on); }

namespace {
uint32_t frame_counter = 0;
}
uint32_t frame_index() { return frame_counter; }

void begin_frame() {
    frame_counter++;
    F.reset();
    P.reset();
    stats_cur = {};
    S.stack.assign(1, Affine{});
    S.canvas = nullptr;
    S.target_dirty = true;
}

// A canvas drawn once per frame, by a plain axis-aligned quad, into a target where opaque panels
// cover part of it afterwards: those parts of the canvas are never seen, so its own scene can skip
// them too (the background under a menu).
void propagate_occlusion() {
    if (F.occluders.empty()) return;
    const int n = (int)F.cmds.size();
    auto scene_end = [&](int t) {
        int e = t + 1;
        while (e < n && F.cmds[e].type != CmdType::Target) e++;
        return e;
    };
    size_t base_count = F.occluders.size();
    for (int t = 0; t < n; t++) {
        const Cmd &tc = F.cmds[t];
        if (tc.type != CmdType::Target || !tc.target) continue;
        const Texture *ct = &tc.target->tex;
        const int te = scene_end(t);
        int consumer = -1, uses = 0;
        for (int j = 0; j < n; j++) {
            const Cmd &c = F.cmds[j];
            if (c.type != CmdType::Draw) continue;
            bool use = c.texture == ct;
            for (const Texture *a : c.aux) use = use || a == ct;
            if (use) uses++, consumer = j;
        }
        if (uses != 1 || consumer < te) continue;
        const Cmd &c = F.cmds[consumer];
        if (c.shader != ShaderId::Default || c.vertex_count != 4) continue;
        const Vertex *q = &F.vertices[c.first_vertex];
        float qx0 = 1e30f, qx1 = -1e30f, qy0 = 1e30f, qy1 = -1e30f, u0 = 0, u1 = 0, v0 = 0, v1 = 0;
        for (int i = 0; i < 4; i++) qx0 = std::min(qx0, q[i].x), qx1 = std::max(qx1, q[i].x), qy0 = std::min(qy0, q[i].y), qy1 = std::max(qy1, q[i].y);
        bool aligned = qx1 > qx0 && qy1 > qy0;
        for (int i = 0; i < 4 && aligned; i++) {
            bool l = q[i].x == qx0, top = q[i].y == qy0;
            aligned = (l || q[i].x == qx1) && (top || q[i].y == qy1);
            (l ? u0 : u1) = q[i].u;
            (top ? v0 : v1) = q[i].v;
        }
        if (!aligned) continue;
        // the consumer's scene and the occluders drawn over the quad after it
        int cs = consumer;
        while (cs > 0 && F.cmds[cs].type != CmdType::Target) cs--;
        const int ce = scene_end(cs);
        const float cw = (float)tc.target->w, ch = (float)tc.target->h;
        for (size_t k = 0; k < base_count; k++) {
            const Occluder &o = F.occluders[k];
            if ((int)o.cmd <= consumer || (int)o.cmd >= ce) continue;
            float x0 = std::max(o.x0, qx0), x1 = std::min(o.x1, qx1), y0 = std::max(o.y0, qy0), y1 = std::min(o.y1, qy1);
            if (x1 <= x0 || y1 <= y0) continue;
            // to canvas pixels, one pixel in for the bilinear footprint
            auto cx = [&](float x) { return (u0 + (u1 - u0) * (x - qx0) / (qx1 - qx0)) * cw; };
            auto cy = [&](float y) { return (v0 + (v1 - v0) * (y - qy0) / (qy1 - qy0)) * ch; };
            float a = cx(x0), b = cx(x1), d = cy(y0), e = cy(y1);
            Occluder r;
            r.cmd = (uint32_t)te - 1;
            r.x0 = std::max(0.f, std::ceil(std::min(a, b) + 1)), r.x1 = std::min(cw, std::floor(std::max(a, b) - 1));
            r.y0 = std::max(0.f, std::ceil(std::min(d, e) + 1)), r.y1 = std::min(ch, std::floor(std::max(d, e) - 1));
            r.all = true;
            if ((r.x1 - r.x0) * (r.y1 - r.y0) < 1024) continue;
            F.occluders.push_back(r);
            if (g_draw_debug) std::fprintf(stderr, "  canvas occluder scene %d: %.0f,%.0f - %.0f,%.0f\n", t, r.x0, r.y0, r.x1, r.y1);
        }
    }
    if (F.occluders.size() != base_count)
        std::stable_sort(F.occluders.begin(), F.occluders.end(), [](const Occluder &a, const Occluder &b) { return a.cmd < b.cmd; });
}

// the pre-pass goes first: F is appended to it
void merge_prepass() {
    const uint32_t nc = (uint32_t)P.cmds.size(), nv = (uint32_t)P.vertices.size(), ni = (uint32_t)P.indices.size(),
                   nu = (uint32_t)P.uniforms.size();
    for (Cmd c : F.cmds) {
        if (c.type == CmdType::Draw) c.first_vertex += nv, c.first_index += ni, c.uniforms += nu;
        P.cmds.push_back(c);
    }
    P.vertices.insert(P.vertices.end(), F.vertices.begin(), F.vertices.end());
    P.indices.insert(P.indices.end(), F.indices.begin(), F.indices.end());
    P.uniforms.insert(P.uniforms.end(), F.uniforms.begin(), F.uniforms.end());
    for (Occluder o : F.occluders) {
        o.cmd += nc;
        P.occluders.push_back(o);
    }
    std::swap(F, P);
    P.reset();
}

float g_wait_gpu_ms = 0, g_wait_queue_ms = 0;
uint64_t g_skip_shaders = 0;

void end_frame() {
    if (g_draw_debug) {
        float area = 0;
        for (const Occluder &o : F.occluders) area += (o.x1 - o.x0) * (o.y1 - o.y0);
        std::fprintf(stderr, "  occluders %d, %.0fk px, cmds %d\n", (int)F.occluders.size(), area / 1000, (int)F.cmds.size());
    }
    if (g_occlusion) propagate_occlusion();
    if (!P.cmds.empty()) merge_prepass();
    for (Canvas *c : canvas_pool.released) canvas_pool.free.push_back(c);
    canvas_pool.released.clear();
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

// Is every texel of the rectangle [x0, x1) x [y0, y1) (texture data pixels) opaque?
bool texels_opaque(const Texture *t, int x0, int y0, int x1, int y1) {
    if (x0 < 0 || y0 < 0 || x1 > t->opaque_w || y1 > t->opaque_h || x0 >= x1 || y0 >= y1) return false;
    for (int y = y0; y < y1; y++) {
        const uint64_t *row = &t->opaque[(size_t)y * t->opaque_words];
        for (int x = x0; x < x1;) {
            const int bit = x % 64, n = std::min(64 - bit, x1 - x);
            const uint64_t want = (n == 64 ? ~0ull : ((1ull << n) - 1)) << bit;
            if ((row[x / 64] & want) != want) return false;
            x += n;
        }
    }
    return true;
}

// An opaque sprite (a card face, drawn white with the default shader) occludes what was drawn
// under it, like an opaque UI panel: the texels of its quad are inset until all of them are opaque
// (cached per texture cell), 2 more for the filtering of the smaller atlas levels, and the screen
// box inside that rectangle (which may be rotated: cards in a fanned hand) is the occluder.
bool sprite_occluder(Texture *t, const Quad &q, const Affine &m, float r[4]) {
    if (t->opaque.empty() || q.sw <= 0 || q.sh <= 0) return false;
    const float kx = t->opaque_w / q.sw, ky = t->opaque_h / q.sh;
    const int x0 = (int)std::lround(q.x * kx), y0 = (int)std::lround(q.y * ky), x1 = (int)std::lround((q.x + q.w) * kx),
              y1 = (int)std::lround((q.y + q.h) * ky);
    struct Key {
        const Texture *t;
        int x0, y0, x1, y1;
        bool operator==(const Key &o) const { return t == o.t && x0 == o.x0 && y0 == o.y0 && x1 == o.x1 && y1 == o.y1; }
    };
    struct Hash {
        size_t operator()(const Key &k) const { return std::hash<const void *>()(k.t) ^ ((size_t)k.x0 * 73856093u) ^ ((size_t)k.y0 * 19349663u) ^ ((size_t)k.x1 * 83492791u) ^ (size_t)k.y1; }
    };
    static std::unordered_map<Key, int, Hash> inset_cache;  // texels of inset, -1 = never opaque
    auto it = inset_cache.find({t, x0, y0, x1, y1});
    int d;
    if (it != inset_cache.end()) {
        d = it->second;
    } else {
        int lo = 0, hi = std::min(x1 - x0, y1 - y0) * 9 / 20;  // smallest opaque inset in (lo, hi]
        if (hi < 4 || !texels_opaque(t, x0 + hi, y0 + hi, x1 - hi, y1 - hi)) {
            d = -1;
        } else {
            while (hi - lo > 1) {
                int mid = (lo + hi) / 2;
                if (texels_opaque(t, x0 + mid, y0 + mid, x1 - mid, y1 - mid)) hi = mid;
                else lo = mid;
            }
            d = hi + 2;
            if (x1 - x0 - 2 * d < 4 || y1 - y0 - 2 * d < 4) d = -1;
        }
        inset_cache[{t, x0, y0, x1, y1}] = d;
    }
    if (d < 0) return false;
    // the opaque rectangle's corners on screen, in outline order
    const float ix = d / kx, iy = d / ky;
    const Vec2 p[4] = {m.apply(ix, iy), m.apply(q.w - ix, iy), m.apply(q.w - ix, q.h - iy), m.apply(ix, q.h - iy)};
    float xs[4] = {p[0].x, p[1].x, p[2].x, p[3].x}, ys[4] = {p[0].y, p[1].y, p[2].y, p[3].y};
    std::sort(xs, xs + 4);
    std::sort(ys, ys + 4);
    auto inside = [&](float px, float py) {
        float sign = 0;
        for (int i = 0; i < 4; i++) {
            const Vec2 &a = p[i], &b = p[(i + 1) % 4];
            const float cr = (b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x);
            if (sign == 0) sign = cr;
            else if ((cr > 0) != (sign > 0) && cr != 0) return false;
        }
        return true;
    };
    // the box between the middle coordinates (exact for an unrotated sprite), shrunk about its
    // centre until all its corners lie inside, then snapped inwards to whole pixels
    const float cx = (xs[1] + xs[2]) * 0.5f, cy = (ys[1] + ys[2]) * 0.5f;
    float hw = (xs[2] - xs[1]) * 0.5f, hh = (ys[2] - ys[1]) * 0.5f;
    for (int k = 0;; k++) {
        r[0] = std::ceil(cx - hw), r[1] = std::ceil(cy - hh), r[2] = std::floor(cx + hw), r[3] = std::floor(cy + hh);
        if (r[2] - r[0] < 16 || r[3] - r[1] < 16) return false;
        if (inside(r[0], r[1]) && inside(r[2], r[1]) && inside(r[2], r[3]) && inside(r[0], r[3])) break;
        if (k == 6) return false;
        hw *= 0.9f, hh *= 0.9f;
    }
    const float tw = (float)(S.canvas ? S.canvas->w : backend_screen_width()), th = (float)(S.canvas ? S.canvas->h : backend_screen_height());
    r[0] = std::max(r[0], 0.f), r[1] = std::max(r[1], 0.f), r[2] = std::min(r[2], tw), r[3] = std::min(r[3], th);
    return r[2] - r[0] >= 16 && r[3] - r[1] >= 16 && (r[2] - r[0]) * (r[3] - r[1]) >= 1024;
}

void draw(Texture *t, const Quad &q, float x, float y, float r, float sx, float sy, float ox,
          float oy) {
    uint16_t *idx;
    Vertex *v;
    Affine m = top() * Affine::from_transformation(x, y, r, sx, sy, ox, oy);
    float occ_r[4];
    const bool occ = g_occlusion && S.shader == ShaderId::Default && S.color.r == 1 && S.color.g == 1 && S.color.b == 1 &&
                     S.color.a == 1 && sprite_occluder(t, q, m, occ_r);
    uint16_t base = begin_draw(t, 4, 6, &idx, &v, occ);
    if (occ) F.occluders.push_back({(uint32_t)F.cmds.size() - 1, occ_r[0], occ_r[1], occ_r[2], occ_r[3]});
    uint32_t col = S.color.packed();
    float u0 = q.x / q.sw, v0 = q.y / q.sh, u1 = (q.x + q.w) / q.sw, v1 = (q.y + q.h) / q.sh;
    Vec2 p0 = m.apply(0, 0), p1 = m.apply(0, q.h), p2 = m.apply(q.w, 0), p3 = m.apply(q.w, q.h);
    {
        // shading estimate: the quad's area, scaled by the share of its bounds inside the target
        float area = std::fabs((p2.x - p0.x) * (p1.y - p0.y) - (p2.y - p0.y) * (p1.x - p0.x));
        const Vec2 corners[4] = {p0, p1, p2, p3};
        float vis = area * visible_share(corners, 4);
        stats_cur.px[(int)S.shader] += vis;
        if (g_draw_debug && vis > 20000)
            std::fprintf(stderr, "  draw %s %.0fk tex %dx%d col %.2f,%.2f,%.2f,%.2f\n", shader_info(S.shader).name, vis / 1000, (int)t->lw, (int)t->lh,
                         S.color.r, S.color.g, S.color.b, S.color.a);
    }
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
bool g_draw_debug = false;
bool g_occlusion = true;

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
