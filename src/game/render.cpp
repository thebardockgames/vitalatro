// Frame-level rendering for Game:draw (see render.h). The block pipeline and the CRT map are the
// ones measured in M0 (m0.cpp has the benchmark and the diagnostic pages).
#include "game/render.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "engine/sprite.h"
#include "platform/platform.h"
#include "game/g.h"

namespace vl::render {

using namespace vl::gfx;

namespace {
bool g_fast = false;
int SW, SH;
float pixel_size;  // splash.fs / background.fs: length(love_ScreenSize.xy) / PIXEL_SIZE_FAC
int nbx, nby;      // background blocks
constexpr int kK = 2;  // field texel every kK blocks (k=1 later on the CPU cores)
Canvas *field, *color;
Texture *polar_bg, *polar_splash;
int frame_no = 0;

// Polar coordinates (radius, angle) of each field texel's block origin around the swirl centre,
// as 16-bit fixed point. background.fs shifts its centre by 0.12, splash.fs does not.
Texture *build_polar(int fw, int fh, float x_shift) {
    std::vector<uint8_t> px((size_t)fw * fh * 4);
    double diag = std::sqrt((double)SW * SW + (double)SH * SH);
    for (int y = 0; y < fh; y++)
        for (int x = 0; x < fw; x++) {
            float bx = (float)((x - 1) * kK) * pixel_size, by = (float)((y - 1) * kK) * pixel_size;
            float ux = (float)((bx - 0.5 * SW) / diag) - x_shift, uy = (float)((by - 0.5 * SH) / diag);
            float r = std::sqrt(ux * ux + uy * uy), a = std::atan2(uy, ux);
            uint32_t er = (uint32_t)std::lround(std::fmin(r, 1.0f) * 65535);
            uint32_t ea = (uint32_t)std::lround((a + 3.1415927f) / 6.2831853f * 65535);
            uint8_t *o = &px[((size_t)y * fw + x) * 4];
            o[0] = er >> 8;
            o[1] = er & 255;
            o[2] = ea >> 8;
            o[3] = ea & 255;
        }
    TextureDesc d{fw, fh, px.data()};
    d.filter = Filter::Nearest;
    return create_texture(d);
}

void colour_uniform(ShaderId id, const char *name, const Value &c) {
    float v[4] = {(float)c[1].num(), (float)c[2].num(), (float)c[3].num(), (float)c[4].num(1)};
    send(id, name, v, 4);
}

// field pass (half of the rows per frame) + colour pass + expansion to pixels
void draw_blocks(ShaderId field_shader, Texture *polar, ShaderId color_shader) {
    frame_no++;
    Texture *ft = canvas_texture(field);
    float fw, fh;
    texture_dimensions(ft, fw, fh);
    Canvas *target = G.CANVAS;
    set_color({1, 1, 1, 1});
    set_canvas(field);
    push();
    origin();
    set_shader(field_shader);
    int half = (int)fh / 2;
    int y0 = (frame_no & 1) ? half : 0, y1 = (frame_no & 1) ? (int)fh : half;
    Quad q{0, (float)y0, fw, (float)(y1 - y0), fw, fh};
    if (G.vl_bench != 12) draw(polar, q, 0, (float)y0);  // GPU bench mode 12: the field is not updated

    if (G.vl_bench == 13 && color_shader == ShaderId::BgColor) {
        // GPU bench: colour pass and upscale merged, per screen pixel (vl_bg_merged.fs)
        set_canvas(target);
        send(ShaderId::BgMerged, "pixel_size", pixel_size);
        send(ShaderId::BgMerged, "k", (float)kK);
        float fs[2] = {fw, fh};
        send(ShaderId::BgMerged, "field_size", fs, 2);
        set_shader(ShaderId::BgMerged);
        draw(ft, 0, 0, 0, SW / fw, SH / fh);
        set_shader(ShaderId::Default);
        pop();
        return;
    }
    set_canvas(color);
    set_shader(color_shader);
    float x0 = 1.5f - 0.5f / kK;
    Quad qc{x0, x0, (float)nbx / kK, (float)nby / kK, fw, fh};
    draw(ft, qc, 0, 0, 0, (float)kK, (float)kK);

    set_canvas(target);
    set_shader(ShaderId::Default);
    draw(canvas_texture(color), 0, 0, 0, pixel_size, pixel_size);
    pop();
}

// ------------------------------------------------------------------ CRT
float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
Texture *crt_lut_f, *crt_lut_g, *crt_map, *crt_scan;
float crt_map_value = -1, crt_map_scanlines = -1;

void build_crt_luts() {
    const int n = 256;
    std::vector<uint8_t> f(n * 4), g(n * 4);
    auto q = [](float v) { return (uint8_t)std::lround(clampf(v, 0, 1) * 255); };
    for (int i = 0; i < n; i++) {
        float a = (i + 0.5f) / n * 6.2831853f;
        f[i * 4 + 0] = q((2 * std::sin(a - 3.14f / 4) + 2) / 4);
        f[i * 4 + 1] = q((2 * std::cos(a) + 2) / 4);
        f[i * 4 + 2] = q((2 * std::cos(a - 3.14f / 3) + 2) / 4);
        f[i * 4 + 3] = 255;
        g[i * 4 + 0] = q(clampf(std::sin(a), 0.4f, 1));
        g[i * 4 + 1] = q(clampf(std::cos(a), 0, 1));
        g[i * 4 + 2] = q(clampf(std::cos(a - 3.14f / 4), 0, 1));
        g[i * 4 + 3] = 255;
    }
    TextureDesc df{n, 1, f.data()};
    df.repeat = true;
    crt_lut_f = create_texture(df);
    TextureDesc dg{n, 1, g.data()};
    dg.repeat = true;
    crt_lut_g = create_texture(dg);
}

float smoothstepf(float e0, float e1, float x) {
    float t = clampf((x - e0) / (e1 - e0), 0, 1);
    return t * t * (3 - 2 * t);
}

// CRT.fs barrel distortion (offset from tc) and edge mask at every screen pixel centre, for the
// current crt setting (the distortion only depends on it).
// The distortion/mask map (RGBA16F) and the scanline mask at each pixel's distorted tc (RGBA8,
// (mask + 1) / 3 per channel: CRT.fs's rgb_scanline, which only depends on the pixel).
struct CrtMapData {
    std::vector<uint16_t> px;
    std::vector<uint8_t> scan;
    float crt, scanlines;
    Texture *map = nullptr, *scan_tex = nullptr;  // created from px/scan
};
void fill_crt_map(CrtMapData &m, float crt, float scanlines) {
    m.px.resize((size_t)SW * SH * 4);
    m.scan.resize((size_t)SW * SH * 4);
    m.crt = crt;
    m.scanlines = scanlines;
    std::vector<uint16_t> &px = m.px;
    std::vector<uint8_t> &scan = m.scan;
    float dfx = 1.0f + 0.07f * crt / 100, dfy = 1.0f + 0.1f * crt / 100;
    float sf = 1.0f - 0.008f * crt / 100, feather = 0.01f;
    for (int y = 0; y < SH; y++)
        for (int x = 0; x < SW; x++) {
            float u = (x + 0.5f) / SW, v = (y + 0.5f) / SH;
            float tx = (u * 2 - 1) * sf, ty = (v * 2 - 1) * sf;
            float nx = tx + (ty * ty) * tx * (dfx - 1), ny = ty + (tx * tx) * ty * (dfy - 1);
            float mask = (1 - smoothstepf(1 - feather, 1, std::fabs(nx) - 0.01f)) * (1 - smoothstepf(1 - feather, 1, std::fabs(ny) - 0.01f));
            uint16_t *o = &px[((size_t)y * SW + x) * 4];
            o[0] = float_to_half((nx + 1) / 2 - u);
            o[1] = float_to_half((ny + 1) / 2 - v);
            o[2] = float_to_half(mask);
            o[3] = float_to_half(1);
            const double ay = (ny + 1) / 2 * (double)scanlines, ax = (nx + 1) / 2 * (double)scanlines * 4;
            const double sc[3] = {
                -0.3 + 2.0 * std::sin(ay - 3.14 / 4.0) - 0.8 * std::clamp(std::sin(ax), 0.4, 1.0),
                -0.3 + 2.0 * std::cos(ay) - 0.8 * std::clamp(std::cos(ax), 0.0, 1.0),
                -0.3 + 2.0 * std::cos(ay - 3.14 / 3.0) - 0.8 * std::clamp(std::cos(ax - 3.14 / 4.0), 0.0, 1.0)};
            uint8_t *so = &scan[((size_t)y * SW + x) * 4];
            for (int c = 0; c < 3; c++) so[c] = (uint8_t)std::lround((std::clamp(sc[c], -1.0, 2.0) + 1) / 3 * 255);
            so[3] = 255;
        }
}

void create_crt_textures(CrtMapData &m) {
    TextureDesc d{SW, SH, (const uint8_t *)m.px.data()};
    d.half_float = true;
    m.map = create_texture(d);
    TextureDesc ds{SW, SH, m.scan.data()};
    m.scan_tex = create_texture(ds);
}

void use_crt_map(CrtMapData &m) {
    if (!m.map) create_crt_textures(m);
    destroy_texture(crt_map);
    destroy_texture(crt_scan);
    crt_map = m.map;
    crt_scan = m.scan_tex;
    m.map = m.scan_tex = nullptr;
    crt_map_value = m.crt;
    crt_map_scanlines = m.scanlines;
}

void build_crt_map(float crt, float scanlines) {
    CrtMapData m;
    fill_crt_map(m, crt, scanlines);
    use_crt_map(m);
}

// A new crt setting (the options slider) rebuilds the map on the worker thread, textures included
// where the backend allows it (~0.3 s of CPU plus the upload: a hitch on every slider step if done
// in the frame); the frame keeps the previous map until the new one is ready.
CrtMapData crt_async;
std::atomic<int> crt_async_state{0};  // 0 idle, 1 building, 2 ready
}  // namespace

void init() {
#ifdef __vita__
    g_fast = true;
#else
    const char *e = std::getenv("VL_FAST");
    g_fast = e && *e == '1';
#endif
    G.FAST_SHADERS = g_fast;
    SW = screen_width();
    SH = screen_height();
    pixel_size = std::sqrt((float)(SW * SW + SH * SH)) / 700.0f;
    nbx = (int)std::ceil(SW / pixel_size);
    nby = (int)std::ceil(SH / pixel_size);
    color = create_canvas(nbx, nby, CanvasFormat::RGBA8, Filter::Nearest);
    int fw = (nbx + kK - 1) / kK + 3, fh = (nby + kK - 1) / kK + 3;
    field = create_canvas(fw, fh, CanvasFormat::RGBA16F);
    polar_bg = build_polar(fw, fh, 0.12f);
    polar_splash = build_polar(fw, fh, 0.0f);
    build_crt_luts();
    // the default crt setting's distortion map (otherwise ~0.3 s of CPU in the first frame)
    if (g_fast) build_crt_map(70 * 0.3f, SH * 0.75f / G.CANV_SCALE);
}

bool fast_paths() { return g_fast; }

void prewarm() {
    begin_frame();
    for (Canvas *c : {G.CANVAS, field, color}) {
        set_canvas(c);
        clear({0, 0, 0, 1});
    }
    set_canvas(nullptr);
    clear({0, 0, 0, 1});
    end_frame();
}

bool draw_background_sprite(Sprite *s) {
    if (!g_fast || !s->states.visible || !s->has_draw_steps || s->draw_steps.empty()) return false;
    const DrawStep &st = s->draw_steps[0];
    auto get = [&](const char *n) -> Value {
        for (const ShaderSend &x : st.send)
            if (x.name == n) return x.get();
        return Value();
    };
    if (st.shader == ShaderId::Splash) {
        double time = get("time").num(), vort_speed = get("vort_speed").num(), vort_offset = get("vort_offset").num();
        double speed = time * vort_speed;
        double m6 = std::fmin(6., speed);
        send(ShaderId::SplashField, "swirl", (float)(2.2 + 0.4 * m6));
        send(ShaderId::SplashField, "angle_add", (float)(-1. - speed * 0.05 - m6 * speed * 0.02 + vort_offset));
        double speed2 = time * 6. * vort_speed + vort_offset + 1033.;
        send(ShaderId::SplashField, "k1", (float)(5.1123314 + speed2 * 0.131121));
        send(ShaderId::SplashField, "k2", (float)(0.113 * speed2));
        send(ShaderId::SplashColor, "smoke_t", (float)(0.17 * std::fmin(10., time * 1.2 - 4.)));
        send(ShaderId::SplashColor, "mid_flash", (float)get("mid_flash").num());
        colour_uniform(ShaderId::SplashColor, "colour_1", get("colour_1"));
        colour_uniform(ShaderId::SplashColor, "colour_2", get("colour_2"));
        draw_blocks(ShaderId::SplashField, polar_splash, ShaderId::SplashColor);
    } else if (st.shader == ShaderId::Background) {
        send(ShaderId::BgField, "time", (float)get("time").num());
        send(ShaderId::BgField, "spin_time", (float)get("spin_time").num());
        send(ShaderId::BgField, "spin_amount", (float)get("spin_amount").num());
        send(ShaderId::BgColor, "contrast", (float)get("contrast").num());
        send(ShaderId::BgColor, "spin_amount", (float)get("spin_amount").num());
        colour_uniform(ShaderId::BgColor, "colour_1", get("colour_1"));
        colour_uniform(ShaderId::BgColor, "colour_2", get("colour_2"));
        colour_uniform(ShaderId::BgColor, "colour_3", get("colour_3"));
        send(ShaderId::BgMerged, "contrast", (float)get("contrast").num());
        send(ShaderId::BgMerged, "spin_amount", (float)get("spin_amount").num());
        colour_uniform(ShaderId::BgMerged, "colour_1", get("colour_1"));
        colour_uniform(ShaderId::BgMerged, "colour_2", get("colour_2"));
        colour_uniform(ShaderId::BgMerged, "colour_3", get("colour_3"));
        draw_blocks(ShaderId::BgField, polar_bg, ShaderId::BgColor);
    } else {
        return false;
    }
    add_to_drawhash(s);
    add_to_drawhash(s);
    s->draw_boundingrect();
    return true;
}

bool draw_flash_sprite(Sprite *s) {
    if (!g_fast || !s->states.visible || !s->has_draw_steps || s->draw_steps.empty()) return false;
    const DrawStep &st = s->draw_steps[0];
    if (st.shader != ShaderId::Flash) return false;
    double time = 0, mid_flash = 0;
    for (const ShaderSend &x : st.send) {
        if (x.name == "time") time = x.get().num();
        if (x.name == "mid_flash") mid_flash = x.get().num();
    }
    // flash.fs: mid_white = min(1, (t > 2.5 ? max(0, sqrt(t - 2.5) - 60*len) : 0)
    //                            + (t > 11 ? max(0, (t - 11)^2 - 5*len) : 0)), len = |uv| (diagonal units)
    double r = 0;
    if (time > 2.5) r = std::max(r, std::sqrt(time - 2.5) / 60);
    if (time > 11) r = std::max(r, (time - 11) * (time - 11) / 5);
    if (r > 0 && mid_flash != 0) {
        const float diag = std::sqrt((float)SW * SW + (float)SH * SH);
        // uv comes from the block origin: cover every block whose origin lies inside the disc
        float half = (float)(r * diag) + pixel_size;
        float x0 = std::max(0.f, SW * 0.5f - half), y0 = std::max(0.f, SH * 0.5f - half);
        float x1 = std::min((float)SW, SW * 0.5f + half), y1 = std::min((float)SH, SH * 0.5f + half);
        if (x1 > x0 && y1 > y0) {
            push();
            origin();
            send(ShaderId::Flash, "time", (float)time);
            send(ShaderId::Flash, "mid_flash", (float)mid_flash);
            set_shader(ShaderId::Flash);
            set_color({1, 1, 1, 1});
            rectangle_fill(x0, y0, x1 - x0, y1 - y0);
            set_shader(ShaderId::Default);
            pop();
        }
    }
    add_to_drawhash(s);
    add_to_drawhash(s);
    s->draw_boundingrect();
    return true;
}

void crt_pass(Canvas *canvas, const CrtParams &p) {
    ShaderId id = ShaderId::CRT;
    if (g_fast) {
        const float scanlines = SH * 0.75f / G.CANV_SCALE;
        if (crt_async_state.load() == 2) {
            use_crt_map(crt_async);
            crt_async_state.store(0);
        }
        if ((p.crt != crt_map_value || scanlines != crt_map_scanlines) && crt_async_state.load() == 0) {
            if (!crt_map) {
                build_crt_map(p.crt, scanlines);
            } else {
                crt_async_state.store(1);
                const float crt = p.crt;
                platform::run_async([crt, scanlines] {
                    fill_crt_map(crt_async, crt, scanlines);
                    if (create_texture_any_thread()) create_crt_textures(crt_async);
                    crt_async_state.store(2);
                });
            }
        }
        set_aux_texture(0, crt_scan);
        set_aux_texture(2, crt_map);
        id = ShaderId::CrtMap;
    } else {
        clear({0, 0, 0, 1});
    }
    push();
    origin();
    set_color({1, 1, 1, 1});
    float df[2] = {1.0f + 0.07f * p.crt / 100, 1.0f + 0.1f * p.crt / 100};
    float sf[2] = {1.0f - 0.008f * p.crt / 100, 1.0f - 0.008f * p.crt / 100};
    send(id, "distortion_fac", df, 2);
    send(id, "scale_fac", sf, 2);
    send(id, "feather_fac", 0.01f);
    send(id, "bloom_fac", 0.0f);
    send(id, "time", p.time);
    send(id, "crt_intensity", 0.16f * p.crt / 100);
    send(id, "glitch_intensity", 0.0f);
    send(id, "scanlines", SH * 0.75f / G.CANV_SCALE);
    float mp[2] = {p.mouse_x, p.mouse_y};
    send(id, "mouse_screen_pos", mp, 2);
    send(id, "screen_scale", p.screen_scale);
    send(id, "hovering", 1.0f);
    if (G.vl_bench == 1) id = ShaderId::Default;  // GPU bench: a plain copy
    set_shader(id);
    draw(canvas, 0, 0);
    set_shader(ShaderId::Default);
    pop();
    set_aux_texture(0, nullptr);
    set_aux_texture(1, nullptr);
    set_aux_texture(2, nullptr);
}

}  // namespace vl::render
