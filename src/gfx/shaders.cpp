// Uniform layouts of Balatro's shaders (resources/shaders/*.fs). Names match the GLSL externs so
// that both backends can look them up; offsets define the per-draw snapshot block.
#include "gfx/gfx.h"

namespace vl::gfx {
namespace {

// Shared by every card shader: <name> vec2 is the shader-specific parameter.
#define CARD_LAYOUT(first)                                                                      \
    {first, 0, 2}, {"dissolve", 2, 1}, {"time", 3, 1}, {"texture_details", 4, 4},               \
        {"image_details", 8, 2}, {"shadow", 10, 1}, {"hovering", 11, 1}, {"burn_colour_1", 12, 4}, \
        {"burn_colour_2", 16, 4}, {"mouse_screen_pos", 20, 2}, { "screen_scale", 22, 1 }

const UniformDesc u_default[] = {{"_unused", 0, 1}};
const UniformDesc u_background[] = {
    {"time", 0, 1}, {"spin_time", 1, 1}, {"contrast", 2, 1}, {"spin_amount", 3, 1},
    {"colour_1", 4, 4}, {"colour_2", 8, 4}, {"colour_3", 12, 4}};
const UniformDesc u_crt[] = {
    {"time", 0, 1}, {"feather_fac", 1, 1}, {"bloom_fac", 2, 1}, {"crt_intensity", 3, 1},
    {"distortion_fac", 4, 2}, {"scale_fac", 6, 2}, {"glitch_intensity", 8, 1}, {"scanlines", 9, 1},
    {"hovering", 10, 1}, {"screen_scale", 11, 1}, {"mouse_screen_pos", 12, 2}};
const UniformDesc u_dissolve[] = {CARD_LAYOUT("_unused")};
const UniformDesc u_foil[] = {CARD_LAYOUT("foil")};
const UniformDesc u_holo[] = {CARD_LAYOUT("holo")};
const UniformDesc u_polychrome[] = {CARD_LAYOUT("polychrome")};
const UniformDesc u_negative[] = {CARD_LAYOUT("negative")};
const UniformDesc u_negative_shine[] = {CARD_LAYOUT("negative_shine")};
const UniformDesc u_booster[] = {CARD_LAYOUT("booster")};
const UniformDesc u_voucher[] = {CARD_LAYOUT("voucher")};
const UniformDesc u_debuff[] = {CARD_LAYOUT("debuff")};
const UniformDesc u_played[] = {CARD_LAYOUT("played")};
const UniformDesc u_hologram[] = {CARD_LAYOUT("hologram")};
const UniformDesc u_vortex[] = {{"vortex_amt", 0, 1}, {"DPI", 1, 1}};
const UniformDesc u_flame[] = {
    {"time", 0, 1}, {"amount", 1, 1}, {"id", 2, 1}, {"texture_details", 4, 4},
    {"image_details", 8, 2}, {"colour_1", 12, 4}, {"colour_2", 16, 4}};
const UniformDesc u_flash[] = {{"time", 0, 1}, {"mid_flash", 1, 1}};
const UniformDesc u_splash[] = {
    {"time", 0, 1}, {"vort_speed", 1, 1}, {"mid_flash", 2, 1}, {"vort_offset", 3, 1},
    {"colour_1", 4, 4}, {"colour_2", 8, 4}};
const UniformDesc u_gold_seal[] = {{"gold_seal", 0, 4}};
const UniformDesc u_bg_field[] = {{"time", 0, 1}, {"spin_time", 1, 1}, {"spin_amount", 2, 1}};
const UniformDesc u_bg_color[] = {{"contrast", 1, 1}, {"spin_amount", 2, 1}, {"colour_1", 8, 4},
                                   {"colour_2", 12, 4}, {"colour_3", 16, 4}};
const UniformDesc u_vl_polychrome[] = {CARD_LAYOUT("polychrome"), {"vl_poly_a", 24, 4}, {"vl_poly_b", 28, 4}};
const UniformDesc u_bg_merged[] = {{"pixel_size", 0, 1}, {"k", 1, 1}, {"field_size", 2, 2},
                                    {"contrast", 4, 1}, {"spin_amount", 5, 1}, {"colour_1", 8, 4},
                                    {"colour_2", 12, 4}, {"colour_3", 16, 4}};
const UniformDesc u_bg_color8[] = {{"k", 0, 1}, {"contrast", 1, 1}, {"spin_amount", 2, 1},
                                    {"field_size", 4, 2}, {"colour_1", 8, 4}, {"colour_2", 12, 4},
                                    {"colour_3", 16, 4}};
const UniformDesc u_skew[] = {{"hovering", 0, 1}, {"screen_scale", 1, 1}, {"mouse_screen_pos", 2, 2}};

#define N(a) (uint8_t)(sizeof(a) / sizeof(a[0]))
const ShaderInfo kShaders[(int)ShaderId::Count] = {
    {"default", u_default, N(u_default), 4, false},
    {"background", u_background, N(u_background), 16, false},
    {"CRT", u_crt, N(u_crt), 16, true},
    {"dissolve", u_dissolve, N(u_dissolve), 24, true},
    {"foil", u_foil, N(u_foil), 24, true},
    {"holo", u_holo, N(u_holo), 24, true},
    {"polychrome", u_polychrome, N(u_polychrome), 24, true},
    {"negative", u_negative, N(u_negative), 24, true},
    {"negative_shine", u_negative_shine, N(u_negative_shine), 24, true},
    {"booster", u_booster, N(u_booster), 24, true},
    {"voucher", u_voucher, N(u_voucher), 24, true},
    {"debuff", u_debuff, N(u_debuff), 24, true},
    {"played", u_played, N(u_played), 24, true},
    {"hologram", u_hologram, N(u_hologram), 24, true},
    {"vortex", u_vortex, N(u_vortex), 4, true},
    {"flame", u_flame, N(u_flame), 20, false},
    {"flash", u_flash, N(u_flash), 4, false},
    {"splash", u_splash, N(u_splash), 12, false},
    {"gold_seal", u_gold_seal, N(u_gold_seal), 4, false},
    {"skew", u_skew, N(u_skew), 4, true},
    {"vl_bg_field", u_bg_field, N(u_bg_field), 4, false, true},
    {"vl_bg_color", u_bg_color, N(u_bg_color), 20, false},
    {"vl_crt_fast", u_crt, N(u_crt), 16, true, false, false},  // CRT.fs uniforms, no blending
    {"vl_polychrome", u_vl_polychrome, N(u_vl_polychrome), 32, true},
    {"vl_crt_map", u_crt, N(u_crt), 16, true, false, false},  // CRT.fs uniforms, no blending
    {"vl_bg_merged", u_bg_merged, N(u_bg_merged), 20, false},
    {"vl_bg_field8", u_bg_field, N(u_bg_field), 4, false, false, false},  // no blending
    {"vl_bg_color8", u_bg_color8, N(u_bg_color8), 20, false},
};
#undef N

}  // namespace

const ShaderInfo &shader_info(ShaderId id) { return kShaders[(int)id]; }

}  // namespace vl::gfx
