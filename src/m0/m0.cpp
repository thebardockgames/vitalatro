// Milestone 0 benchmark: reproduces the GPU/CPU load of a Balatro run frame on the target
// (shader background, ~25 cards with their dissolve/edition layers, UI panels, canvas + CRT)
// and measures several configurations so the renderer strategy can be decided on hardware.
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
void sprite_draw_self(const Atlas &a, float sx, float sy, const VT &vt) {
    prep_draw(vt, 1);
    scale(1 / (a.px / vt.w), 1 / (a.py / vt.h));
    set_color({1, 1, 1, 1});
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

void draw_card(const Card &c, double t) {
    // shadow: dissolve with shadow=true, offset by the shadow parallax (card.lua Card:draw)
    VT s = c.vt;
    float sh = 0.1f;
    float par_x = (s.x + s.w / 2 - 10.5f) / 10.5f * 1.5f;
    s.y -= -1.5f * sh;
    s.x -= par_x * sh;
    s.scale *= 1 - 0.2f * sh;
    card_shader(ShaderId::Dissolve, *c.center, c.cx, c.cy, c.id, true);
    sprite_draw_self(*c.center, c.cx, c.cy, s);
    card_shader(ShaderId::Dissolve, *c.center, c.cx, c.cy, c.id, false);
    sprite_draw_self(*c.center, c.cx, c.cy, c.vt);
    if (c.front) {
        card_shader(ShaderId::Dissolve, *c.front, c.fx, c.fy, c.id, false);
        sprite_draw_self(*c.front, c.fx, c.fy, c.vt);
    }
    if (c.edition != ShaderId::Default) {
        float e[2] = {(float)std::fmod(t / 28.0, 100.0), (float)t};
        send(c.edition, shader_info(c.edition).uniforms[0].name, e, 2);
        card_shader(c.edition, *c.center, c.cx, c.cy, c.id, false);
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

// ------------------------------------------------------------------ benchmark configurations
struct Config {
    const char *name;
    bool bg, cards, ui, crt;
};
const Config kConfigs[] = {
    {"full frame", true, true, true, true},
    {"no background", false, true, true, true},
    {"no CRT", true, true, true, false},
    {"no cards", true, false, true, true},
    {"no UI", true, true, false, true},
    {"clear only", false, false, false, false},
};
constexpr int kConfigCount = sizeof(kConfigs) / sizeof(kConfigs[0]);
constexpr int kFramesPerConfig = 240;

struct Result { double avg_ms, max_ms, cpu_ms; int draws; };
Result results[kConfigCount];
int cur_config = 0, cur_frame = 0;
double acc_ms, max_ms, acc_cpu;
double last_t;
bool done = false;
float fps_display = 0;
double t0;

void render(const Config &cfg, double t) {
    set_canvas(canvas);
    clear({0, 0, 0, 1});
    if (cfg.bg) {
        // G.SPLASH_BACK: Sprite(-30, -6, ROOM.w+60, ROOM.h+12) drawn with the background shader
        send(ShaderId::Background, "time", (float)t);
        send(ShaderId::Background, "spin_time", 0.0f);
        float c1[4] = {0xFE / 255.f, 0x5F / 255.f, 0x55 / 255.f, 1};
        float c2[4] = {0x37 / 255.f, 0x42 / 255.f, 0x44 / 255.f, 1};
        float c3[4] = {0x2a / 255.f, 0x30 / 255.f, 0x3a / 255.f, 1};
        send(ShaderId::Background, "colour_1", c1, 4);
        send(ShaderId::Background, "colour_2", c2, 4);
        send(ShaderId::Background, "colour_3", c3, 4);
        send(ShaderId::Background, "contrast", 1.0f);
        send(ShaderId::Background, "spin_amount", 0.0f);
        set_shader(ShaderId::Background);
        VT bg{-30 + 0.8f, -6 + 0.4f, 21 + 60, 11.2f + 12, 0, 1};
        sprite_draw_self(a_ui, 2, 0, bg);
        set_shader(ShaderId::Default);
    }
    if (cfg.ui) {
        // HUD column + panels similar to the in-run layout
        ui_panel(0.5f, 0.5f, 4.6f, 10.6f, {0.21f, 0.27f, 0.29f, 1});
        for (int i = 0; i < 12; i++) ui_panel(0.7f, 0.8f + i * 0.85f, 4.2f, 0.7f, {0.13f, 0.16f, 0.18f, 1});
        for (int i = 0; i < 40; i++)
            ui_panel(5.6f + (i % 10) * 1.5f, 0.3f + (i / 10) * 0.6f, 1.3f, 0.45f, {0.3f, 0.3f, 0.35f, 0.6f});
    }
    if (cfg.cards)
        for (const Card &c : cards) draw_card(c, t);

    set_canvas(nullptr);
    clear({0, 0, 0, 1});
    set_color({1, 1, 1, 1});
    if (cfg.crt) {
        // game.lua Game:draw() CRT settings with the default crt = 70 setting
        float crt = 70 * 0.3f;
        float df[2] = {1.0f + 0.07f * crt / 100, 1.0f + 0.1f * crt / 100};
        float sf[2] = {1.0f - 0.008f * crt / 100, 1.0f - 0.008f * crt / 100};
        send(ShaderId::CRT, "distortion_fac", df, 2);
        send(ShaderId::CRT, "scale_fac", sf, 2);
        send(ShaderId::CRT, "feather_fac", 0.01f);
        send(ShaderId::CRT, "bloom_fac", 0.0f);
        send(ShaderId::CRT, "time", (float)(400 + t));
        send(ShaderId::CRT, "crt_intensity", 0.16f * crt / 100);
        send(ShaderId::CRT, "glitch_intensity", 0.0f);
        send(ShaderId::CRT, "scanlines", 544 * 0.75f);
        float mp[2] = {480, 272};
        send(ShaderId::CRT, "mouse_screen_pos", mp, 2);
        send(ShaderId::CRT, "screen_scale", TILESCALE * TILESIZE);
        send(ShaderId::CRT, "hovering", 1.0f);
        set_shader(ShaderId::CRT);
    }
    draw(canvas, 0, 0);
    set_shader(ShaderId::Default);
}

}  // namespace

void init() {
    TILESCALE = 960.0f / (21 + 2 * 0.8f) / TILESIZE;
    a_jokers = {load_png("textures/1x/Jokers.png", 2, true), 71, 95};
    a_deck = {load_png("textures/1x/8BitDeck.png", 2, true), 71, 95};
    a_centers = {load_png("textures/1x/Enhancers.png", 2, true), 71, 95};
    a_ui = {load_png("textures/1x/ui_assets.png", 2, true), 18, 18};
    canvas = create_canvas(960, 544);
    load_font();

    const float cw = 2.4f * 35 / 41, ch = 2.4f * 47 / 41;
    int id = 1;
    // 8 cards in hand
    for (int i = 0; i < 8; i++)
        cards.push_back({{7.5f + i * 1.45f, 7.6f, cw, ch, 0.02f * (i - 3.5f), 1}, &a_centers, 1, 0,
                         &a_deck, (float)(i % 13), (float)(i % 4), ShaderId::Default, id++});
    // 5 jokers, some with editions
    const ShaderId eds[5] = {ShaderId::Polychrome, ShaderId::Holo, ShaderId::Foil, ShaderId::Default,
                             ShaderId::Default};
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
    platform::log("M0: %d cards, starting benchmark", (int)cards.size());
}

void frame() {
    double now = time_now();
    double dt_ms = (now - last_t) * 1000.0;
    last_t = now;
    double t = now - t0;

    begin_frame();
    double c0 = time_now();
    const Config &cfg = done ? kConfigs[0] : kConfigs[cur_config];
    render(cfg, t);

    // overlay
    char buf[2048];
    int n = std::snprintf(buf, sizeof buf, "Vitalatro M0  %s\n", done ? "results (frame ms, uncapped)" : cfg.name);
    for (int i = 0; i < kConfigCount; i++) {
        if (!done && i > cur_config) break;
        if (!done && i == cur_config)
            n += std::snprintf(buf + n, sizeof buf - n, "%-14s  measuring %d/%d\n", kConfigs[i].name, cur_frame, kFramesPerConfig);
        else
            n += std::snprintf(buf + n, sizeof buf - n, "%-14s  avg %6.2f  max %6.2f  cpu %5.2f  draws %d\n",
                               kConfigs[i].name, results[i].avg_ms, results[i].max_ms, results[i].cpu_ms, results[i].draws);
    }
    fps_display = fps_display * 0.95f + (float)(1000.0 / (dt_ms > 0.01 ? dt_ms : 0.01)) * 0.05f;
    std::snprintf(buf + n, sizeof buf - n, "fps %.1f", fps_display);
    set_color({0, 0, 0, 0.6f});
    rectangle_fill(8, 8, 560, 20.0f * (kConfigCount + 3));
    set_color({1, 1, 1, 1});
    text(16, 10, buf);
    double cpu_ms = (time_now() - c0) * 1000.0;
    int draws_before = last_frame_stats().draw_calls;
    end_frame();

    if (!done) {
        if (cur_frame > 10) {  // skip warm-up frames
            acc_ms += dt_ms;
            acc_cpu += cpu_ms + last_frame_stats().cpu_ms_submit;
            if (dt_ms > max_ms) max_ms = dt_ms;
        }
        if (++cur_frame == kFramesPerConfig) {
            int m = kFramesPerConfig - 11;
            results[cur_config] = {acc_ms / m, max_ms, acc_cpu / m, draws_before};
            platform::log("M0 %-14s avg %.2f ms  max %.2f ms  cpu %.2f ms  draws %d", kConfigs[cur_config].name,
                          acc_ms / m, max_ms, acc_cpu / m, draws_before);
            acc_ms = max_ms = acc_cpu = 0;
            cur_frame = 0;
            if (++cur_config == kConfigCount) done = true;
        }
    }
}

bool finished() { return done; }

}  // namespace vl::m0
