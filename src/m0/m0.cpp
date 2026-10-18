// Milestone 0 benchmark: reproduces the GPU/CPU load of a Balatro run frame on the target
// (shader background, ~25 cards with their dissolve/edition layers, UI panels, canvas + CRT)
// and measures the original pipeline against the optimised one on hardware.
#include "m0/m0.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "core/time.h"
#include "gfx/gfx.h"
#include "platform/platform.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb/stb_image.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_truetype.h"

namespace vl::m0 {
namespace {

using namespace vl::gfx;

constexpr float TILESIZE = 20.0f;
constexpr int SW = 960, SH = 544;
float TILESCALE;  // 960 / (TILE_W + 2*ROOM_PADDING_W) / TILESIZE

struct Atlas {
    Texture *tex;
    float px, py;
};
Atlas a_jokers, a_deck, a_centers, a_ui;
Canvas *canvas;

// Debug text: m6x11plus baked with stb_truetype.
Texture *font_tex;
stbtt_bakedchar font_chars[96];
constexpr int kFontAtlas = 256;
constexpr float kFontPx = 18.0f;

Texture *load_png(const char *path, int virtual_scale, bool mipmaps) {
    std::string data;
    if (!platform::read_file(path, data)) platform::fatal("missing %s", path);
    int w, h, n;
    unsigned char *px = stbi_load_from_memory((const unsigned char *)data.data(), (int)data.size(), &w, &h, &n, 4);
    if (!px) platform::fatal("bad png %s", path);
    TextureDesc d{w, h, px};
    d.mipmaps = mipmaps;
    d.virtual_scale = virtual_scale;
    d.dpi_scale = (float)virtual_scale;  // love dpiscale = texture_scaling
    Texture *t = create_texture(d);
    stbi_image_free(px);
    return t;
}

void load_font() {
    std::string ttf;
    if (!platform::read_file("fonts/m6x11plus.ttf", ttf)) platform::fatal("missing font");
    std::vector<uint8_t> alpha(kFontAtlas * kFontAtlas), rgba(kFontAtlas * kFontAtlas * 4);
    stbtt_BakeFontBitmap((const unsigned char *)ttf.data(), 0, kFontPx, alpha.data(), kFontAtlas,
                         kFontAtlas, 32, 96, font_chars);
    for (int i = 0; i < kFontAtlas * kFontAtlas; i++) {
        rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = alpha[i];
    }
    TextureDesc d{kFontAtlas, kFontAtlas, rgba.data()};
    d.filter = Filter::Nearest;
    font_tex = create_texture(d);
}

void text(float x, float y, const char *s) {
    static std::vector<GlyphVertex> v;
    v.clear();
    float cx = x, cy = y + kFontPx;
    for (; *s; s++) {
        if (*s == '\n') { cx = x; cy += kFontPx + 2; continue; }
        if (*s < 32 || *s >= 128) continue;
        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(font_chars, kFontAtlas, kFontAtlas, *s - 32, &cx, &cy, &q, 1);
        v.push_back({q.x0, q.y0, q.s0, q.t0});
        v.push_back({q.x0, q.y1, q.s0, q.t1});
        v.push_back({q.x1, q.y0, q.s1, q.t0});
        v.push_back({q.x1, q.y1, q.s1, q.t1});
    }
    draw_glyph_quads(font_tex, v.data(), (int)v.size() / 4);
}

// ------------------------------------------------------------------ Balatro-equivalent drawing
struct VT { float x, y, w, h, r, scale; };

// functions/misc_functions.lua prep_draw()
void prep_draw(const VT &vt, float scl) {
    push();
    scale(TILESCALE * TILESIZE);
    translate(vt.x + vt.w / 2, vt.y + vt.h / 2);
    if (vt.r != 0) rotate(vt.r);
    translate(-scl * vt.w * vt.scale / 2, -scl * vt.h * vt.scale / 2);
    scale(vt.scale * scl);
}

// engine/sprite.lua Sprite:draw_self() for a sprite whose T == VT
void sprite_draw_self(const Atlas &a, float sx, float sy, const VT &vt, Color col = {1, 1, 1, 1}) {
    prep_draw(vt, 1);
    scale(1 / (a.px / vt.w), 1 / (a.py / vt.h));
    set_color(col);
    float iw, ih;
    texture_dimensions(a.tex, iw, ih);
    Quad q{sx * a.px, sy * a.py, a.px, a.py, iw, ih};
    draw(a.tex, q, 0, 0, 0, 1, 1);
    pop();
}

// Sprite:draw_shader() uniform setup for the card shaders
void card_shader(ShaderId sh, const Atlas &a, float sx, float sy, int id, bool shadow) {
    float iw, ih;
    texture_dimensions(a.tex, iw, ih);
    float mouse[2] = {480, 272};
    send(sh, "mouse_screen_pos", mouse, 2);
    send(sh, "screen_scale", TILESCALE * TILESIZE);
    send(sh, "hovering", 0.0f);
    send(sh, "dissolve", 0.0f);
    send(sh, "time", std::fmod(123.33412f * (id / 1.14212f), 3000.0f));
    float td[4] = {sx, sy, a.px, a.py};
    send(sh, "texture_details", td, 4);
    float idim[2] = {iw, ih};
    send(sh, "image_details", idim, 2);
    float clear4[4] = {0, 0, 0, 0};
    send(sh, "burn_colour_1", clear4, 4);
    send(sh, "burn_colour_2", clear4, 4);
    send(sh, "shadow", shadow ? 1.0f : 0.0f);
    set_shader(sh);
}

struct Card {
    VT vt;
    const Atlas *center;
    float cx, cy;
    const Atlas *front;  // playing cards only
    float fx, fy;
    ShaderId edition;
    int id;
};
std::vector<Card> cards;

// fast: with dissolve == 0 and no hover tilt, dissolve.fs reduces exactly to the default shader
// (shadow: rgb 0 / alpha * 0.3; normal: the texel), so those layers skip it.
void draw_card(const Card &c, double t, bool fast, int editions) {
    VT s = c.vt;
    float sh = 0.1f;
    float par_x = (s.x + s.w / 2 - 10.5f) / 10.5f * 1.5f;
    s.y -= -1.5f * sh;
    s.x -= par_x * sh;
    s.scale *= 1 - 0.2f * sh;
    if (fast) {
        set_shader(ShaderId::Default);
        sprite_draw_self(*c.center, c.cx, c.cy, s, {0, 0, 0, 0.3f});
        sprite_draw_self(*c.center, c.cx, c.cy, c.vt);
        if (c.front) sprite_draw_self(*c.front, c.fx, c.fy, c.vt);
    } else {
        card_shader(ShaderId::Dissolve, *c.center, c.cx, c.cy, c.id, true);
        sprite_draw_self(*c.center, c.cx, c.cy, s);
        card_shader(ShaderId::Dissolve, *c.center, c.cx, c.cy, c.id, false);
        sprite_draw_self(*c.center, c.cx, c.cy, c.vt);
        if (c.front) {
            card_shader(ShaderId::Dissolve, *c.front, c.fx, c.fy, c.id, false);
            sprite_draw_self(*c.front, c.fx, c.fy, c.vt);
        }
    }
    if (c.edition != ShaderId::Default && editions) {
        ShaderId ed = editions == 2 ? ShaderId::VlPolychrome : ShaderId::Polychrome;
        float e[2] = {(float)std::fmod(t / 28.0, 100.0), (float)t};
        send(ed, "polychrome", e, 2);
        if (ed == ShaderId::VlPolychrome) {
            // per-draw terms of polychrome.fs that only depend on uniforms
            float time_u = std::fmod(123.33412f * (c.id / 1.14212f), 3000.0f);
            float pt = e[1] * 2.221f + time_u;
            float a[4] = {50 * std::sin(-pt / 143.6340f), 50 * std::cos(-pt / 99.4324f),
                          50 * std::cos(pt / 53.1532f), 50 * std::cos(pt / 61.4532f)};
            float b[4] = {50 * std::sin(-pt / 87.53218f), 50 * std::sin(-pt / 49.0f), e[0] * 2.612f - 0.5f * 3.14f,
                          e[1] * 0.04f};
            send(ed, "vl_poly_a", a, 4);
            send(ed, "vl_poly_b", b, 4);
        }
        card_shader(ed, *c.center, c.cx, c.cy, c.id, false);
        sprite_draw_self(*c.center, c.cx, c.cy, c.vt);
    }
    set_shader(ShaderId::Default);
}

// ui.lua draw_pixellated_rect('fill') geometry for a panel
void ui_panel(float x, float y, float w, float h, Color col) {
    push();
    scale(TILESCALE * TILESIZE);
    translate(x, y);
    scale(1 / TILESIZE);
    float res = std::fmin(w, h) > 3.5f ? 0.8f : std::fmin(w, h) > 0.3f ? 0.6f : 0.15f;
    float totw = w * TILESIZE, toth = h * TILESIZE, subw = totw - 4 * res, subh = toth - 4 * res;
    const float v[] = {subw / 2, subh / 2, 0, 4 * res, 1 * res, 4 * res, 1 * res, 2 * res, 2 * res, 2 * res,
                       2 * res, 1 * res, 4 * res, 1 * res, 4 * res, 0, subw, 0, subw, 1 * res,
                       subw + 2 * res, 1 * res, subw + 2 * res, 2 * res, subw + 3 * res, 2 * res,
                       subw + 3 * res, 4 * res, totw, 4 * res, totw, subh, subw + 3 * res, subh,
                       subw + 3 * res, subh + 2 * res, subw + 2 * res, subh + 2 * res, subw + 2 * res,
                       subh + 3 * res, subw, subh + 3 * res, subw, toth, 4 * res, toth, 4 * res,
                       subh + 3 * res, 2 * res, subh + 3 * res, 2 * res, subh + 2 * res, 1 * res,
                       subh + 2 * res, 1 * res, subh, 0, subh, 0, 4 * res};
    set_color({0, 0, 0, 0.3f * col.a});
    polygon_fill(v, 30);
    set_color(col);
    polygon_fill(v, 30);
    pop();
}

// ------------------------------------------------------------------ background
const float kBg1[4] = {0xFE / 255.f, 0x5F / 255.f, 0x55 / 255.f, 1};
const float kBg2[4] = {0x37 / 255.f, 0x42 / 255.f, 0x44 / 255.f, 1};
const float kBg3[4] = {0x2a / 255.f, 0x30 / 255.f, 0x3a / 255.f, 1};
const VT kBgSprite{-30 + 0.8f, -6 + 0.4f, 21 + 60, 11.2f + 12, 0, 1};

float pixel_size;  // background.fs: length(love_ScreenSize.xy) / PIXEL_SIZE_FAC
int nbx, nby;      // number of background blocks
Canvas *bg_field[5];      // per k
Texture *bg_polar[5];     // per k: block polar coordinates for the field pass
Canvas *bg_color;
int frame_no;

// Polar coordinates (radius, angle) around the swirl centre of the block each field texel stands
// for, exactly as background.fs computes them before its swirl, as 16-bit fixed point.
Texture *build_polar(int k, int fw, int fh) {
    std::vector<uint8_t> px((size_t)fw * fh * 4);
    double diag = std::sqrt((double)SW * SW + (double)SH * SH);
    for (int y = 0; y < fh; y++)
        for (int x = 0; x < fw; x++) {
            float bx = (float)((x - 1) * k) * pixel_size, by = (float)((y - 1) * k) * pixel_size;
            float ux = (float)((bx - 0.5 * SW) / diag) - 0.12f, uy = (float)((by - 0.5 * SH) / diag);
            float r = std::sqrt(ux * ux + uy * uy), a = std::atan2(uy, ux);
            uint32_t er = (uint32_t)std::lround(std::fmin(r, 1.0f) * 65535);
            uint32_t ea = (uint32_t)std::lround((a + 3.1415927f) / 6.2831853f * 65535);
            uint8_t *o = &px[((size_t)y * fw + x) * 4];
            o[0] = er >> 8; o[1] = er & 255; o[2] = ea >> 8; o[3] = ea & 255;
        }
    TextureDesc d{fw, fh, px.data()};
    d.filter = Filter::Nearest;
    return create_texture(d);
}


void background_original(double t) {
    send(ShaderId::Background, "time", (float)t);
    send(ShaderId::Background, "spin_time", 0.0f);
    send(ShaderId::Background, "colour_1", kBg1, 4);
    send(ShaderId::Background, "colour_2", kBg2, 4);
    send(ShaderId::Background, "colour_3", kBg3, 4);
    send(ShaderId::Background, "contrast", 1.0f);
    send(ShaderId::Background, "spin_amount", 0.0f);
    set_shader(ShaderId::Background);
    sprite_draw_self(a_ui, 2, 0, kBgSprite);
    set_shader(ShaderId::Default);
}

// Three passes: paint field every k blocks (RGBA16F) -> per-block colour, sampling the field with
// hardware bilinear filtering -> expand blocks to pixels with nearest filtering. No pass does a
// dependent texture read.
void background_blocks(double t, int k, int field_every) {
    Canvas *field = bg_field[k];
    Texture *ft = canvas_texture(field);
    float fw, fh;
    texture_dimensions(ft, fw, fh);

    if (frame_no % field_every == 0) {
        set_canvas(field);
        send(ShaderId::BgField, "time", (float)t);
        send(ShaderId::BgField, "spin_time", 0.0f);
        send(ShaderId::BgField, "spin_amount", 0.0f);
        set_shader(ShaderId::BgField);
        set_color({1, 1, 1, 1});
        draw(bg_polar[k], 0, 0);
    }

    // block b's colour pixel centre (b + 0.5) must sample field texel coordinate b/k + 1.5
    set_canvas(bg_color);
    send(ShaderId::BgColor, "contrast", 1.0f);
    send(ShaderId::BgColor, "spin_amount", 0.0f);
    send(ShaderId::BgColor, "colour_1", kBg1, 4);
    send(ShaderId::BgColor, "colour_2", kBg2, 4);
    send(ShaderId::BgColor, "colour_3", kBg3, 4);
    set_shader(ShaderId::BgColor);
    float x0 = 1.5f - 0.5f / k;
    Quad q{x0, x0, (float)nbx / k, (float)nby / k, fw, fh};
    draw(ft, q, 0, 0, 0, (float)k, (float)k);

    // original block grid: block = floor(pixel_centre / pixel_size)
    set_canvas(canvas);
    set_shader(ShaderId::Default);
    draw(canvas_texture(bg_color), 0, 0, 0, pixel_size, pixel_size);
}

// ------------------------------------------------------------------ CRT
float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
const float kCrt = 70 * 0.3f;  // G.SETTINGS.GRAPHICS.crt default (70) * 0.3 as sent in Game:draw

// Game:draw() CRT uniforms; the fast variant takes the same ones.
Texture *crt_lut_f, *crt_lut_g;

// CRT.fs scanline terms, 2*pi-periodic in their argument: f(a) for tc.y*scanlines, g(a) for
// tc.x*scanlines*4 (rgb_scanline = clamp(-0.3 + f - g, -1, 2)).
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

void crt_uniforms(ShaderId id, double t) {
    float df[2] = {1.0f + 0.07f * kCrt / 100, 1.0f + 0.1f * kCrt / 100};
    float sf[2] = {1.0f - 0.008f * kCrt / 100, 1.0f - 0.008f * kCrt / 100};
    send(id, "distortion_fac", df, 2);
    send(id, "scale_fac", sf, 2);
    send(id, "feather_fac", 0.01f);
    send(id, "bloom_fac", 0.0f);
    send(id, "time", (float)(400 + t));
    send(id, "crt_intensity", 0.16f * kCrt / 100);
    send(id, "glitch_intensity", 0.0f);
    send(id, "scanlines", SH * 0.75f);
    float mp[2] = {480, 272};
    send(id, "mouse_screen_pos", mp, 2);
    send(id, "screen_scale", TILESCALE * TILESIZE);
    send(id, "hovering", 1.0f);
    set_shader(id);
}

// ------------------------------------------------------------------ benchmark configurations
enum Bg { BG_NONE, BG_ORIG, BG_BLOCKS };
enum Crt { CRT_NONE, CRT_ORIG, CRT_FAST };
struct Config {
    const char *name;
    Bg bg;
    int k;
    int field_every;     // recompute the background field every N frames
    bool cards, fast_cards, ui;
    Crt crt;
    int editions;        // 0 none, 1 original polychrome.fs, 2 vl_polychrome
};
const Config kConfigs[] = {
    {"original", BG_ORIG, 0, 1, true, false, true, CRT_ORIG, 1},
    {"opt k=1", BG_BLOCKS, 1, 1, true, true, true, CRT_FAST, 2},
    {"opt k=2", BG_BLOCKS, 2, 1, true, true, true, CRT_FAST, 2},
    {"opt k=3", BG_BLOCKS, 3, 1, true, true, true, CRT_FAST, 2},
    {"opt k=1 30Hz", BG_BLOCKS, 1, 2, true, true, true, CRT_FAST, 2},
    {"opt k=2 30Hz", BG_BLOCKS, 2, 2, true, true, true, CRT_FAST, 2},
    {"bg k=1 only", BG_BLOCKS, 1, 1, false, false, false, CRT_NONE, 0},
    {"bg k=2 only", BG_BLOCKS, 2, 1, false, false, false, CRT_NONE, 0},
    {"crt only", BG_NONE, 0, 1, false, false, false, CRT_FAST, 0},
    {"cards ed opt", BG_NONE, 0, 1, true, true, false, CRT_NONE, 2},
    {"cards no ed", BG_NONE, 0, 1, true, true, false, CRT_NONE, 0},
    {"ui only", BG_NONE, 0, 1, false, false, true, CRT_NONE, 0},
    {"clear only", BG_NONE, 0, 1, false, false, false, CRT_NONE, 0},
};
constexpr int kConfigCount = sizeof(kConfigs) / sizeof(kConfigs[0]);
constexpr int kFramesPerConfig = 150;

struct Result { double avg_ms, max_ms, cpu_ms; int draws; };
Result results[kConfigCount];
int cur_config = 0, cur_frame = 0;
double acc_ms, max_ms, acc_cpu;
double last_t;
bool done = false;
float fps_display = 0;
double t0;
int shown_config = 2;


void render(const Config &cfg, double t) {
    frame_no++;
    set_canvas(canvas);
    if (cfg.bg == BG_NONE) clear({0, 0, 0, 1});  // the background covers the whole canvas
    if (cfg.bg == BG_ORIG) background_original(t);
    if (cfg.bg == BG_BLOCKS) background_blocks(t, cfg.k, cfg.field_every);
    set_canvas(canvas);
    if (cfg.ui) {
        // HUD column + panels similar to the in-run layout
        ui_panel(0.5f, 0.5f, 4.6f, 10.6f, {0.21f, 0.27f, 0.29f, 1});
        for (int i = 0; i < 12; i++) ui_panel(0.7f, 0.8f + i * 0.85f, 4.2f, 0.7f, {0.13f, 0.16f, 0.18f, 1});
        for (int i = 0; i < 40; i++)
            ui_panel(5.6f + (i % 10) * 1.5f, 0.3f + (i / 10) * 0.6f, 1.3f, 0.45f, {0.3f, 0.3f, 0.35f, 0.6f});
    }
    if (cfg.cards)
        for (const Card &c : cards) draw_card(c, t, cfg.fast_cards, cfg.editions);

    set_canvas(nullptr);
    set_color({1, 1, 1, 1});
    if (cfg.crt == CRT_FAST) {
        // writes final pixels without blending: no backbuffer clear needed
        set_aux_texture(0, crt_lut_f);
        set_aux_texture(1, crt_lut_g);
        crt_uniforms(ShaderId::CrtFast, t);
    } else {
        clear({0, 0, 0, 1});
        if (cfg.crt == CRT_ORIG) crt_uniforms(ShaderId::CRT, t);
    }
    draw(canvas, 0, 0);
    set_shader(ShaderId::Default);
    set_aux_texture(0, nullptr);
    set_aux_texture(1, nullptr);
}

}  // namespace

void init() {
    TILESCALE = 960.0f / (21 + 2 * 0.8f) / TILESIZE;
    a_jokers = {load_png("textures/1x/Jokers.png", 2, true), 71, 95};
    a_deck = {load_png("textures/1x/8BitDeck.png", 2, true), 71, 95};
    a_centers = {load_png("textures/1x/Enhancers.png", 2, true), 71, 95};
    a_ui = {load_png("textures/1x/ui_assets.png", 2, true), 18, 18};
    canvas = create_canvas(SW, SH);

    pixel_size = std::sqrt((float)(SW * SW + SH * SH)) / 700.0f;
    nbx = (int)std::ceil(SW / pixel_size);
    nby = (int)std::ceil(SH / pixel_size);
    bg_color = create_canvas(nbx, nby, CanvasFormat::RGBA8, Filter::Nearest);
    for (int k = 1; k <= 4; k++)
    {
        int fw = (nbx + k - 1) / k + 3, fh = (nby + k - 1) / k + 3;
        bg_field[k] = create_canvas(fw, fh, CanvasFormat::RGBA16F);
        bg_polar[k] = build_polar(k, fw, fh);
    }
    build_crt_luts();
    load_font();

    const float cw = 2.4f * 35 / 41, ch = 2.4f * 47 / 41;
    int id = 1;
    // 8 cards in hand
    for (int i = 0; i < 8; i++)
        cards.push_back({{7.5f + i * 1.45f, 7.6f, cw, ch, 0.02f * (i - 3.5f), 1}, &a_centers, 1, 0,
                         &a_deck, (float)(i % 13), (float)(i % 4), ShaderId::Default, id++});
    // 5 jokers, some with editions
    const ShaderId eds[5] = {ShaderId::Polychrome, ShaderId::Polychrome, ShaderId::Polychrome,
                             ShaderId::Default, ShaderId::Default};
    for (int i = 0; i < 5; i++)
        cards.push_back({{6.2f + i * 2.2f, 0.9f, cw, ch, 0.01f * (i - 2), 1}, &a_jokers, (float)i, 0,
                         nullptr, 0, 0, eds[i], id++});
    // 2 consumables, 5 played cards, deck stack top cards
    for (int i = 0; i < 2; i++)
        cards.push_back({{17.5f + i * 1.9f, 0.9f, cw, ch, 0, 1}, &a_jokers, 3, (float)(i + 2), nullptr, 0, 0,
                         ShaderId::Default, id++});
    for (int i = 0; i < 5; i++)
        cards.push_back({{8.5f + i * 2.1f, 4.2f, cw, ch, 0, 1}, &a_centers, 1, 0, &a_deck,
                         (float)((i * 3) % 13), (float)(i % 4), ShaderId::Default, id++});
    for (int i = 0; i < 6; i++)
        cards.push_back({{19.2f - i * 0.02f, 8.0f - i * 0.03f, cw, ch, 0, 1}, &a_centers, 0, 0, nullptr, 0,
                         0, ShaderId::Default, id++});
    t0 = last_t = time_now();
    platform::log("M0b: %d cards, %dx%d bg blocks (px %.4f), starting benchmark", (int)cards.size(), nbx, nby,
                  pixel_size);
}

int config_count() { return kConfigCount; }
const char *config_name(int i) { return kConfigs[i].name; }

void render_config(int i, double t) {
    begin_frame();
    render(kConfigs[i], t);
    end_frame();
}

void frame(uint32_t pressed) {
    double now = time_now();
    double dt_ms = (now - last_t) * 1000.0;
    last_t = now;
    double t = now - t0;

    // after the benchmark: L/R cycles through the configurations to compare them by eye
    if (done) {
        if (pressed & platform::BTN_R) shown_config = (shown_config + 1) % kConfigCount;
        if (pressed & platform::BTN_L) shown_config = (shown_config + kConfigCount - 1) % kConfigCount;
    }

    begin_frame();
    double c0 = time_now();
    const Config &cfg = done ? kConfigs[shown_config] : kConfigs[cur_config];
    render(cfg, t);

    // overlay
    char buf[2048];
    int n = std::snprintf(buf, sizeof buf, "Vitalatro M0b  %s\n",
                          done ? "frame ms uncapped | L/R: view config" : cfg.name);
    for (int i = 0; i < kConfigCount; i++) {
        if (!done && i > cur_config) break;
        const char *mark = done && i == shown_config ? ">" : " ";
        if (!done && i == cur_config)
            n += std::snprintf(buf + n, sizeof buf - n, " %-15s measuring %d/%d\n", kConfigs[i].name, cur_frame,
                               kFramesPerConfig);
        else
            n += std::snprintf(buf + n, sizeof buf - n, "%s%-15s avg %6.2f max %6.2f rec %5.2f draws %d\n", mark,
                               kConfigs[i].name, results[i].avg_ms, results[i].max_ms, results[i].cpu_ms,
                               results[i].draws);
    }
    fps_display = fps_display * 0.95f + (float)(1000.0 / (dt_ms > 0.01 ? dt_ms : 0.01)) * 0.05f;
    std::snprintf(buf + n, sizeof buf - n, "fps %.1f", fps_display);
    set_color({0, 0, 0, 0.6f});
    rectangle_fill(8, 8, 600, 20.0f * (kConfigCount + 3));
    set_color({1, 1, 1, 1});
    text(16, 10, buf);
    double cpu_ms = (time_now() - c0) * 1000.0;
    end_frame();
    int draws = last_frame_stats().draw_calls;

    if (!done) {
        if (cur_frame > 10) {  // skip warm-up frames
            acc_ms += dt_ms;
            acc_cpu += cpu_ms;
            if (dt_ms > max_ms) max_ms = dt_ms;
        }
        if (++cur_frame == kFramesPerConfig) {
            int m = kFramesPerConfig - 11;
            results[cur_config] = {acc_ms / m, max_ms, acc_cpu / m, draws};
            platform::log("M0b %-15s avg %.2f ms  max %.2f ms  record %.2f ms  draws %d", kConfigs[cur_config].name,
                          acc_ms / m, max_ms, acc_cpu / m, draws);
            acc_ms = max_ms = acc_cpu = 0;
            cur_frame = 0;
            if (++cur_config == kConfigCount) done = true;
        }
    }
}

bool finished() { return done; }

}  // namespace vl::m0
