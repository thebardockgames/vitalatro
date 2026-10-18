#pragma once
// engine/sprite.lua + engine/animatedsprite.lua
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "engine/node.h"
#include "gfx/gfx.h"

namespace vl {

struct Atlas;

struct SpritePos {
    float x = 0, y = 0;
    int v = 0;  // when set, x is picked at random in [0, v-1]
};

// A uniform sent by a custom draw step: {name = ..., val/func/ref_table+ref_value}
struct ShaderSend {
    std::string name;
    std::function<Value()> get;
};

struct DrawStep {
    gfx::ShaderId shader = gfx::ShaderId::Dissolve;
    bool has_shadow_height = false;
    float shadow_height = 0;
    std::vector<ShaderSend> send;
    bool no_tilt = false;
    Ref<Moveable> other_obj;
    float ms = 0, mr = 0, mx = 0, my = 0;
};

gfx::ShaderId shader_by_name(std::string_view name);

// Sprite:draw_shader(_shader, _shadow_height, _send, _no_tilt, other_obj, ms, mr, mx, my,
//                    custom_shader, tilt_shadow)
struct SpriteShaderArgs {
    const float *shadow_height = nullptr;
    const float *send = nullptr;  // float[2] sent as the uniform named after the shader
    const std::vector<ShaderSend> *custom_send = nullptr;
    bool no_tilt = false;
    Moveable *other_obj = nullptr;
    float ms = 0, mr = 0, mx = 0, my = 0;
    const float *tilt_shadow = nullptr;
};

class Sprite : public Moveable {
public:
    Sprite() = default;
    Sprite(float x, float y, float w, float h, Atlas *atlas, SpritePos pos) { init(x, y, w, h, atlas, pos); }
    void init(float x, float y, float w, float h, Atlas *atlas, SpritePos pos);

    virtual void reset();
    virtual void set_sprite_pos(SpritePos pos);
    virtual void get_pos_pixel(float out[4]) const;
    void get_image_dims(float out[2]) const { out[0] = image_dims[0]; out[1] = image_dims[1]; }
    void define_draw_steps(std::vector<DrawStep> steps) { draw_steps = std::move(steps); has_draw_steps = true; }

    using ShaderArgs = SpriteShaderArgs;
    void draw_shader(gfx::ShaderId shader, const ShaderArgs &a);
    void draw_shader(gfx::ShaderId shader) { draw_shader(shader, ShaderArgs{}); }
    void draw_shader(gfx::ShaderId shader, float shadow_height) {
        ShaderArgs a;
        a.shadow_height = &shadow_height;
        draw_shader(shader, a);
    }
    virtual void draw_self(const float *overlay = nullptr);
    void draw_from(Moveable *other, float ms, float mr, float mx, float my);
    void draw() override { draw_with(nullptr); }
    void draw_with(const float *overlay);
    void remove() override;

    Atlas *atlas = nullptr;
    struct { float x = 0, y = 0; } scale;
    float scale_mag = 1;
    SpritePos sprite_pos, sprite_pos_copy;
    gfx::Quad quad{};
    float image_dims[2] = {0, 0};
    std::vector<DrawStep> draw_steps;
    bool has_draw_steps = false;

    // vl_dissolve_baked.fs: the dissolve noise field of this sprite's cell for a draw major's ID,
    // computed on a worker thread. nullptr until it is ready (the caller falls back to vl_dissolve).
    gfx::Texture *dissolve_field(int id);
    struct DissolveFieldJob;
    std::shared_ptr<DissolveFieldJob> dissolve_job;
    gfx::Texture *dissolve_field_tex = nullptr;
    int dissolve_field_id = -1;
    SpritePos dissolve_field_pos;
};

class AnimatedSprite : public Sprite {
public:
    AnimatedSprite() = default;
    AnimatedSprite(float x, float y, float w, float h, Atlas *atlas, SpritePos pos) { init(x, y, w, h, atlas, pos); }
    void init(float x, float y, float w, float h, Atlas *atlas, SpritePos pos);

    void rescale();
    void reset() override;
    void set_sprite_pos(SpritePos pos) override;
    void get_pos_pixel(float out[4]) const override;
    void draw_self(const float *overlay = nullptr) override;
    void animate() override;
    void remove() override;

    struct {
        float x = 0, y = 0;
        int frames = 1;
        float w = 0, h = 0;
    } animation;
    int current_frame = 0;
    float frame_offset = 0;
    double offset_seconds = 0;
    bool float_ = false;  // self.float
};

}  // namespace vl
