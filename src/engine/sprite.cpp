// engine/sprite.lua + engine/animatedsprite.lua
#include "engine/sprite.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

#include "engine/controller.h"
#include "engine/random.h"
#include "game/g.h"
#include "platform/platform.h"

namespace vl {

gfx::ShaderId shader_by_name(std::string_view n) {
    for (int i = 0; i < (int)gfx::ShaderId::Count; i++)
        if (n == gfx::shader_info((gfx::ShaderId)i).name) return (gfx::ShaderId)i;
    return gfx::ShaderId::Dissolve;
}

static Color to_color(const Value &c) {
    Table *t = c.table();
    if (!t) return {1, 1, 1, 1};
    return {(float)t->get(1).num(), (float)t->get(2).num(), (float)t->get(3).num(), (float)t->get(4).num(1)};
}

// ------------------------------------------------------------------ Sprite
void Sprite::init(float x, float y, float w, float h, Atlas *a, SpritePos pos) {
    Moveable::init(x, y, w, h);
    CT = &VT;
    atlas = a;
    scale.x = atlas->px;
    scale.y = atlas->py;
    scale_mag = std::min(scale.x / w, scale.y / h);
    zoom = true;
    set_sprite_pos(pos);
    G.I.SPRITE.add(this);
}

void Sprite::reset() {
    atlas = &G.ASSET_ATLAS[atlas->name];
    set_sprite_pos(sprite_pos);
}

void Sprite::set_sprite_pos(SpritePos pos) {
    if (pos.v) {
        sprite_pos = {(float)(lua_math.random(pos.v) - 1), pos.y, 0};
    } else {
        sprite_pos = pos;
    }
    sprite_pos_copy = sprite_pos;
    float iw, ih;
    gfx::texture_dimensions(atlas->image, iw, ih);
    quad = {sprite_pos.x * atlas->px, sprite_pos.y * atlas->py, scale.x, scale.y, iw, ih};
    image_dims[0] = iw;
    image_dims[1] = ih;
}

void Sprite::get_pos_pixel(float out[4]) const {
    out[0] = sprite_pos.x;
    out[1] = sprite_pos.y;
    out[2] = atlas->px;
    out[3] = atlas->py;
}

// The uniform-only terms of dissolve.fs, for vl_dissolve.fs and vl_dissolve_baked.fs.
static float dissolve_time(Moveable *dm) { return (float)std::fmod(123.33412 * (dm->ID / 1.14212), 3000.0); }

static void send_fast_dissolve(gfx::ShaderId sh, Moveable *dm, bool shadow) {
    const double d = std::fabs(dm->dissolve);
    const double ad = (d * d * (3. - 2. * d)) * 1.02 - 0.01;
    const double k = (5. + 5. * d) * d;
    if (sh == gfx::ShaderId::VlDissolve) {
        const float t = dissolve_time(dm) * 10.0f + 2003.f;
        float a[4] = {(float)(50 * std::sin(-t / 143.6340f)), (float)(50 * std::cos(-t / 99.4324f)),
                      (float)(50 * std::cos(t / 53.1532f)), (float)(50 * std::cos(t / 61.4532f))};
        float b[4] = {(float)(50 * std::sin(-t / 87.53218f)), (float)(50 * std::sin(-t / 49.0000f)), (float)ad, (float)k};
        gfx::send(sh, "vl_dis_a", a, 4);
        gfx::send(sh, "vl_dis_b", b, 4);
    } else {
        float dd[4] = {(float)std::cos(ad / 82.612), (float)std::sin(ad / 82.612), (float)ad, (float)k};
        gfx::send(sh, "vl_dis_d", dd, 4);
    }
    Color b1{0, 0, 0, 0}, b2{0, 0, 0, 0};
    if (dm->dissolve_colours) {
        b1 = to_color(dm->dissolve_colours->get(1));
        b2 = to_color(dm->dissolve_colours->get(2));
    }
    const double w = 0.5 - std::fabs(ad - 0.5);
    float c[4] = {(float)(ad + 0.8 * w), (float)(ad + 0.5 * w), (b1.a > 0.01f && !shadow) ? 1.f : 0.f, b2.a > 0.01f ? 1.f : 0.f};
    float tint[4] = {0, 0, 0, 0};
    if (!shadow && d > 0.01 && (b2.a > 0.01f || b1.a > 0.01f)) {
        const Color &tc = b2.a > 0.01f ? b2 : b1;
        tint[0] = tc.r;
        tint[1] = tc.g;
        tint[2] = tc.b;
        tint[3] = (float)(0.6 * d);
    }
    gfx::send(sh, "vl_dis_c", c, 4);
    gfx::send(sh, "vl_dis_tint", tint, 4);
}

struct Sprite::DissolveFieldJob {
    std::atomic<bool> done{false};
    int w = 0, h = 0;
    std::vector<uint16_t> px;
};

gfx::Texture *Sprite::dissolve_field(int id) {
    if (dissolve_field_id == id && dissolve_field_pos.x == sprite_pos.x && dissolve_field_pos.y == sprite_pos.y) {
        if (!dissolve_field_tex && dissolve_job && dissolve_job->done.load(std::memory_order_acquire)) {
            gfx::TextureDesc d{dissolve_job->w, dissolve_job->h, (const uint8_t *)dissolve_job->px.data()};
            d.half_float = true;
            d.filter = gfx::Filter::Nearest;
            dissolve_field_tex = gfx::create_texture(d);
            dissolve_job = nullptr;
        }
        return dissolve_field_tex;
    }
    if (dissolve_field_tex) {
        gfx::destroy_texture(dissolve_field_tex);
        dissolve_field_tex = nullptr;
    }
    dissolve_job = nullptr;
    dissolve_field_id = id;
    dissolve_field_pos = sprite_pos;
    auto job = std::make_shared<DissolveFieldJob>();
    job->w = (int)atlas->px;
    job->h = (int)atlas->py;
    dissolve_job = job;
    platform::run_async([job, id] {
        const int w = job->w, h = job->h;
        const float m = (float)std::max(w, h);
        // dissolve.fs: t and the three moving field centres (constant for a given ID), then per texel
        const float t = (float)std::fmod(123.33412 * (id / 1.14212), 3000.0) * 10.0f + 2003.f;
        const float o[6] = {50 * std::sin(-t / 143.6340f), 50 * std::cos(-t / 99.4324f), 50 * std::cos(t / 53.1532f),
                            50 * std::cos(t / 61.4532f),   50 * std::sin(-t / 87.53218f), 50 * std::sin(-t / 49.0000f)};
        job->px.resize((size_t)w * h * 4);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                float fx = x / m, fy = y / m;
                float cx = (fx - 0.5f) * 2.3f * m, cy = (fy - 0.5f) * 2.3f * m;
                float p1x = cx + o[0], p1y = cy + o[1], p2x = cx + o[2], p2y = cy + o[3], p3x = cx + o[4], p3y = cy + o[5];
                float field = (1.f + (std::cos(std::sqrt(p1x * p1x + p1y * p1y) / 19.483f) +
                                      std::sin(std::sqrt(p2x * p2x + p2y * p2y) / 33.155f) * std::cos(p2y / 15.73f) +
                                      std::cos(std::sqrt(p3x * p3x + p3y * p3y) / 27.193f) * std::sin(p3x / 21.92f))) /
                              2.f;
                float g = (field - .5f) * 3.14f;
                float border = std::max(fx - 0.8f, 0.f) + std::max(fy - 0.8f, 0.f) + std::max(0.2f - fx, 0.f) + std::max(0.2f - fy, 0.f);
                uint16_t *o4 = &job->px[((size_t)y * w + x) * 4];
                o4[0] = gfx::float_to_half(std::cos(g));
                o4[1] = gfx::float_to_half(std::sin(g));
                o4[2] = gfx::float_to_half(border);
                o4[3] = gfx::float_to_half(1);
            }
        job->done.store(true, std::memory_order_release);
    });
    return nullptr;
}

void Sprite::draw_shader(gfx::ShaderId sh, const ShaderArgs &a) {
    bool no_tilt = a.no_tilt || G.reduced_motion();
    Moveable *dm = role.draw_major ? role.draw_major : this;
    if (a.shadow_height) {
        VT.y -= dm->shadow_parrallax->y * *a.shadow_height;
        VT.x -= dm->shadow_parrallax->x * *a.shadow_height;
        VT.scale *= (1 - 0.2f * *a.shadow_height);
    }

    // hovering = ((_shadow_height and not tilt_shadow) or _no_tilt) and 0 or
    //            ((touch_collide_tilt and collide.is) and hover_tilt or 0) * (tilt_shadow or 1)
    float hovering = 0;
    if (!((a.shadow_height && !a.tilt_shadow) || no_tilt))
        hovering = ((dm->touch_collide_tilt && dm->states.collide.is) ? dm->hover_tilt : 0) *
                   (a.tilt_shadow ? *a.tilt_shadow : 1);

    bool identity = false;
    if (a.custom_send) {
        for (const ShaderSend &s : *a.custom_send) {
            Value v = s.get();
            if (v.is_num()) {
                gfx::send(sh, s.name.c_str(), (float)v.num());
            } else if (Table *t = v.table()) {
                float f[4];
                int n = std::min(4, t->len());
                for (int i = 0; i < n; i++) f[i] = (float)t->get(i + 1).num();
                gfx::send(sh, s.name.c_str(), f, n);
            } else if (v.is_bool()) {
                gfx::send(sh, s.name.c_str(), v.boolean() ? 1.0f : 0.0f);
            }
        }
    } else if (sh == gfx::ShaderId::Vortex) {
        gfx::send(sh, "vortex_amt", (float)(G.TIMERS.REAL - G.vortex_time));
        gfx::send(sh, "DPI", 1.0f);  // love.graphics.getDPIScale()
    } else {
        // dissolve.fs with dissolve == 0 and no tilt is exactly the default shader (measured on
        // device: this is the dominant GPU cost of cards otherwise)
        identity = sh == gfx::ShaderId::Dissolve && std::fabs(dm->dissolve) < 0.001f && hovering <= 0;
        // a dissolving sprite: same output from the parallel-mode variant (vl_dissolve.fs)
        if (!identity && sh == gfx::ShaderId::Dissolve && G.FAST_SHADERS && std::fabs(dm->dissolve) >= 0.001f) {
            // fully dissolved (adjusted_dissolve >= 1): every pixel has res <= 1 <= ad, alpha 0
            const double d = std::fabs(dm->dissolve);
            if ((d * d * (3. - 2. * d)) * 1.02 - 0.01 >= 1.0) {
                if (!a.other_obj) add_to_drawhash(this);
                draw_boundingrect();
                if (a.shadow_height) {
                    VT.y += dm->shadow_parrallax->y * *a.shadow_height;
                    VT.x += dm->shadow_parrallax->x * *a.shadow_height;
                    VT.scale /= (1 - 0.2f * *a.shadow_height);
                }
                return;
            }
            // large sprites (the title logo) use the field baked once per sprite, when it is ready
            sh = (atlas->px * atlas->py >= 20000 && dissolve_field(dm->ID)) ? gfx::ShaderId::VlDissolveBaked
                                                                             : gfx::ShaderId::VlDissolve;
        }
        if (!identity) {
            float cursor[2];
            cursor[0] = (dm->has_tilt_var ? dm->tilt_var.mx : G.CONTROLLER->cursor_position.x) * G.CANV_SCALE;
            cursor[1] = (dm->has_tilt_var ? dm->tilt_var.my : G.CONTROLLER->cursor_position.y) * G.CANV_SCALE;
            gfx::send(sh, "mouse_screen_pos", cursor, 2);
            gfx::send(sh, "screen_scale", G.TILESCALE * G.TILESIZE * (dm->has_mouse_damping ? dm->mouse_damping : 1) * G.CANV_SCALE);
            gfx::send(sh, "hovering", hovering);
            gfx::send(sh, "dissolve", std::fabs(dm->dissolve));
            gfx::send(sh, "time", (float)std::fmod(123.33412 * (dm->ID / 1.14212), 3000.0));
            float td[4];
            get_pos_pixel(td);
            gfx::send(sh, "texture_details", td, 4);
            float idims[2];
            get_image_dims(idims);
            gfx::send(sh, "image_details", idims, 2);
            const float clear[4] = {0, 0, 0, 0};
            auto burn = [&](int i, const char *name) {
                if (dm->dissolve_colours) {
                    Color c = to_color(dm->dissolve_colours->get(i));
                    float f[4] = {c.r, c.g, c.b, c.a};
                    gfx::send(sh, name, f, 4);
                } else {
                    gfx::send(sh, name, clear, 4);
                }
            };
            burn(1, "burn_colour_1");
            burn(2, "burn_colour_2");
            gfx::send(sh, "shadow", a.shadow_height ? 1.0f : 0.0f);
            if (sh == gfx::ShaderId::VlDissolve || sh == gfx::ShaderId::VlDissolveBaked)
                send_fast_dissolve(sh, dm, a.shadow_height != nullptr);
            if (a.send) gfx::send(sh, gfx::shader_info(sh).name, a.send, 2);
        }
    }

    if (identity) {
        gfx::set_shader(gfx::ShaderId::Default);
        static const float white[4] = {1, 1, 1, 1}, shadow[4] = {0, 0, 0, 0.3f};
        const float *col = a.shadow_height ? shadow : white;
        if (a.other_obj) {
            const float *saved = G.BRUTE_OVERLAY;
            G.BRUTE_OVERLAY = col;
            draw_from(a.other_obj, a.ms, a.mr, a.mx, a.my);
            G.BRUTE_OVERLAY = saved;
        } else {
            draw_self(col);
        }
    } else {
        gfx::set_shader(sh);
        if (sh == gfx::ShaderId::VlDissolveBaked) gfx::set_aux_texture(0, dissolve_field(dm->ID));
        if (a.other_obj) draw_from(a.other_obj, a.ms, a.mr, a.mx, a.my);
        else draw_self();
        if (sh == gfx::ShaderId::VlDissolveBaked) gfx::set_aux_texture(0, nullptr);
    }
    gfx::set_shader(gfx::ShaderId::Default);

    if (a.shadow_height) {
        VT.y += dm->shadow_parrallax->y * *a.shadow_height;
        VT.x += dm->shadow_parrallax->x * *a.shadow_height;
        VT.scale /= (1 - 0.2f * *a.shadow_height);
    }
}

void Sprite::draw_self(const float *overlay) {
    if (!states.visible) return;
    if (sprite_pos.x != sprite_pos_copy.x || sprite_pos.y != sprite_pos_copy.y) set_sprite_pos(sprite_pos);
    prep_draw(this, 1);
    gfx::scale(1 / (scale.x / VT.w), 1 / (scale.y / VT.h));
    const float *c = overlay ? overlay : (G.BRUTE_OVERLAY ? G.BRUTE_OVERLAY : nullptr);
    gfx::set_color(c ? Color{c[0], c[1], c[2], c[3]} : Color{1, 1, 1, 1});
    gfx::draw(atlas->image, quad, 0, 0, 0, VT.w / T->w, VT.h / T->h);
    gfx::pop();
    add_to_drawhash(this);
    draw_boundingrect();
}

void Sprite::draw_with(const float *overlay) {
    if (!states.visible) return;
    if (has_draw_steps) {
        for (const DrawStep &s : draw_steps) {
            ShaderArgs a;
            if (s.has_shadow_height) a.shadow_height = &s.shadow_height;
            if (!s.send.empty()) a.custom_send = &s.send;
            a.no_tilt = s.no_tilt;
            a.other_obj = s.other_obj.get();
            a.ms = s.ms; a.mr = s.mr; a.mx = s.mx; a.my = s.my;
            draw_shader(s.shader, a);
        }
    } else {
        draw_self(overlay);
    }
    add_to_drawhash(this);
    children.each([](Str *name, Node *c) {
        if (name != K("h_popup")) c->draw();
    });
    add_to_drawhash(this);
    draw_boundingrect();
}

void Sprite::draw_from(Moveable *o, float ms, float mr, float mx, float my) {
    XY off{mx, my};
    prep_draw(o, 1 + ms, mr, &off, true);
    Sprite *os = dynamic_cast<Sprite *>(o);
    gfx::scale(1 / (os ? os->scale_mag : o->VT.scale));
    const float *c = G.BRUTE_OVERLAY;
    gfx::set_color(c ? Color{c[0], c[1], c[2], c[3]} : Color{1, 1, 1, 1});
    gfx::draw(atlas->image, quad, -(o->T->w / 2 - o->VT.w / 2) * 10, 0, 0, o->VT.w / o->T->w, o->VT.h / o->T->h);
    draw_boundingrect();
    gfx::pop();
}

void Sprite::remove() {
    if (dissolve_field_tex) {
        gfx::destroy_texture(dissolve_field_tex);
        dissolve_field_tex = nullptr;
    }
    G.ANIMATIONS.remove(this);
    G.I.SPRITE.remove(this);
    Moveable::remove();
}

// ------------------------------------------------------------------ AnimatedSprite
void AnimatedSprite::init(float x, float y, float w, float h, Atlas *a, SpritePos pos) {
    Sprite::init(x, y, w, h, a, pos);
    offset = {0, 0};
    G.ANIMATIONS.add(this);
}

void AnimatedSprite::rescale() { scale_mag = std::min(scale.x / T->w, scale.y / T->h); }

void AnimatedSprite::reset() {
    atlas = &G.ANIMATION_ATLAS[atlas->name];
    SpritePos p{animation.x, animation.y, 0};
    set_sprite_pos(p);
}

void AnimatedSprite::set_sprite_pos(SpritePos pos) {
    animation.x = pos.x;
    animation.y = pos.y;
    animation.frames = atlas->frames;
    animation.w = scale.x;
    animation.h = scale.y;
    current_frame = 0;
    frame_offset = 0;
    float iw, ih;
    gfx::texture_dimensions(atlas->image, iw, ih);
    image_dims[0] = iw;
    image_dims[1] = ih;
    quad = {0, animation.h * animation.y, animation.w, animation.h, iw, ih};
    offset_seconds = G.TIMERS.REAL;
    sprite_pos = pos;
    sprite_pos_copy = pos;
}

void AnimatedSprite::get_pos_pixel(float out[4]) const {
    out[0] = (float)current_frame;
    out[1] = animation.y;
    out[2] = animation.w;
    out[3] = animation.h;
}

void AnimatedSprite::draw_self(const float *) {
    if (!states.visible) return;
    prep_draw(this, 1);
    gfx::scale(1 / scale_mag);
    gfx::set_color({1, 1, 1, 1});
    gfx::draw(atlas->image, quad, 0, 0, 0, VT.w / T->w, VT.h / T->h);
    gfx::pop();
}

void AnimatedSprite::animate() {
    int nf = (int)std::floor(G.ANIMATION_FPS * (G.TIMERS.REAL - offset_seconds));
    nf = ((nf % animation.frames) + animation.frames) % animation.frames;
    if (nf != current_frame) {
        current_frame = nf;
        frame_offset = std::floor(animation.w * current_frame);
        quad.x = frame_offset;
        quad.y = animation.h * animation.y;
        quad.w = animation.w;
        quad.h = animation.h;
    }
    if (float_) {
        T->r = (float)(0.02 * std::sin(2 * G.TIMERS.REAL + T->x));
        offset.y = (float)(-(1 + 0.3 * std::sin(0.666 * G.TIMERS.REAL + T->y)) * shadow_parrallax->y);
        offset.x = (float)(-(0.7 + 0.2 * std::sin(0.666 * G.TIMERS.REAL + T->x)) * shadow_parrallax->x);
    }
}

void AnimatedSprite::remove() {
    // (the original's loop here uses an undefined index and removes nothing)
    Sprite::remove();
}

}  // namespace vl
