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
const UniformDesc u_splash_field[] = {{"swirl", 0, 1}, {"angle_add", 1, 1}, {"k1", 2, 1}, {"k2", 3, 1}};
const UniformDesc u_splash_color[] = {{"smoke_t", 0, 1}, {"mid_flash", 1, 1}, {"colour_1", 4, 4}, {"colour_2", 8, 4}};
const UniformDesc u_vl_dissolve[] = {CARD_LAYOUT("_unused"), {"vl_dis_a", 24, 4}, {"vl_dis_b", 28, 4}, {"vl_dis_c", 32, 4},
                                      {"vl_dis_tint", 36, 4}};
const UniformDesc u_vl_dissolve_baked[] = {CARD_LAYOUT("_unused"), {"vl_dis_c", 24, 4}, {"vl_dis_d", 28, 4}, {"vl_dis_tint", 32, 4}};
const UniformDesc u_vl_dm_foil[] = {CARD_LAYOUT("foil"), {"vl_dis_c", 24, 4}, {"vl_dis_d", 28, 4}};
const UniformDesc u_vl_dm_holo[] = {CARD_LAYOUT("holo"), {"vl_dis_c", 24, 4}, {"vl_dis_d", 28, 4}};
const UniformDesc u_vl_dm_polychrome[] = {CARD_LAYOUT("polychrome"), {"vl_dis_c", 24, 4}, {"vl_dis_d", 28, 4}};
const UniformDesc u_vl_dm_negative[] = {CARD_LAYOUT("negative"), {"vl_dis_c", 24, 4}, {"vl_dis_d", 28, 4}};
const UniformDesc u_vl_dm_negative_shine[] = {CARD_LAYOUT("negative_shine"), {"vl_dis_c", 24, 4}, {"vl_dis_d", 28, 4}};
const UniformDesc u_vl_dm_booster[] = {CARD_LAYOUT("booster"), {"vl_dis_c", 24, 4}, {"vl_dis_d", 28, 4}};
const UniformDesc u_vl_dm_voucher[] = {CARD_LAYOUT("voucher"), {"vl_dis_c", 24, 4}, {"vl_dis_d", 28, 4}};
const UniformDesc u_vl_dm_debuff[] = {CARD_LAYOUT("debuff"), {"vl_dis_c", 24, 4}, {"vl_dis_d", 28, 4}};
const UniformDesc u_vl_dm_played[] = {CARD_LAYOUT("played"), {"vl_dis_c", 24, 4}, {"vl_dis_d", 28, 4}};
const UniformDesc u_vl_dm_hologram[] = {CARD_LAYOUT("hologram"), {"vl_dis_c", 24, 4}, {"vl_dis_d", 28, 4}, {"vl_glow_rect", 32, 4}};
const UniformDesc u_vl_d0_hologram[] = {CARD_LAYOUT("hologram"), {"vl_glow_rect", 24, 4}};
const UniformDesc u_vl_hologram_glow[] = {{"vl_glow_rect", 0, 4}};
const UniformDesc u_vl_dissolve_field[] = {{"vl_dis_a", 0, 4}, {"vl_dis_b", 4, 4}};
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
    {"vl_splash_field", u_splash_field, N(u_splash_field), 4, false, true},
    {"vl_splash_color", u_splash_color, N(u_splash_color), 12, false},
    {"vl_dissolve", u_vl_dissolve, N(u_vl_dissolve), 40, true},
    {"vl_dissolve_baked", u_vl_dissolve_baked, N(u_vl_dissolve_baked), 36, true},
    {"vl_dissolve_field", u_vl_dissolve_field, N(u_vl_dissolve_field), 8, false, true},
    {"vl_hologram_glow", u_vl_hologram_glow, N(u_vl_hologram_glow), 4, false, true},
    {"vl_d0_dissolve", u_dissolve, N(u_dissolve), 24, true},
    {"vl_d0_foil", u_foil, N(u_foil), 24, true},
    {"vl_d0_holo", u_holo, N(u_holo), 24, true},
    {"vl_d0_polychrome", u_polychrome, N(u_polychrome), 24, true},
    {"vl_d0_negative", u_negative, N(u_negative), 24, true},
    {"vl_d0_negative_shine", u_negative_shine, N(u_negative_shine), 24, true},
    {"vl_d0_booster", u_booster, N(u_booster), 24, true},
    {"vl_d0_voucher", u_voucher, N(u_voucher), 24, true},
    {"vl_d0_debuff", u_debuff, N(u_debuff), 24, true},
    {"vl_d0_played", u_played, N(u_played), 24, true},
    {"vl_d0_hologram", u_vl_d0_hologram, N(u_vl_d0_hologram), 28, true},
    {"vl_dm_foil", u_vl_dm_foil, N(u_vl_dm_foil), 32, true},
    {"vl_dm_holo", u_vl_dm_holo, N(u_vl_dm_holo), 32, true},
    {"vl_dm_polychrome", u_vl_dm_polychrome, N(u_vl_dm_polychrome), 32, true},
    {"vl_dm_negative", u_vl_dm_negative, N(u_vl_dm_negative), 32, true},
    {"vl_dm_negative_shine", u_vl_dm_negative_shine, N(u_vl_dm_negative_shine), 32, true},
    {"vl_dm_booster", u_vl_dm_booster, N(u_vl_dm_booster), 32, true},
    {"vl_dm_voucher", u_vl_dm_voucher, N(u_vl_dm_voucher), 32, true},
    {"vl_dm_debuff", u_vl_dm_debuff, N(u_vl_dm_debuff), 32, true},
    {"vl_dm_played", u_vl_dm_played, N(u_vl_dm_played), 32, true},
    {"vl_dm_hologram", u_vl_dm_hologram, N(u_vl_dm_hologram), 36, true},
};
#undef N

}  // namespace

static_assert(sizeof(kShaders) / sizeof(kShaders[0]) == (size_t)ShaderId::Count, "kShaders must list every ShaderId");

const ShaderInfo &shader_info(ShaderId id) { return kShaders[(int)id]; }

float shader_cycles(ShaderId id) {
    switch (id) {
    case ShaderId::Default: return 4.f;
    case ShaderId::Background: return 1473.f;
    case ShaderId::CRT: return 1731.f;
    case ShaderId::Dissolve: return 831.f;
    case ShaderId::Foil: return 1497.f;
    case ShaderId::Holo: return 1746.f;
    case ShaderId::Polychrome: return 1584.f;
    case ShaderId::Negative: return 1023.f;
    case ShaderId::NegativeShine: return 1419.f;
    case ShaderId::Booster: return 1431.f;
    case ShaderId::Voucher: return 1428.f;
    case ShaderId::Debuff: return 1122.f;
    case ShaderId::Played: return 1008.f;
    case ShaderId::Hologram: return 3102.f;
    case ShaderId::Vortex: return 4.f;
    case ShaderId::Flame: return 915.f;
    case ShaderId::Flash: return 25.5f;
    case ShaderId::Splash: return 1488.f;
    case ShaderId::GoldSeal: return 48.f;
    case ShaderId::Skew: return 4.f;
    case ShaderId::BgField: return 203.f;
    case ShaderId::BgColor: return 15.f;
    case ShaderId::CrtFast: return 51.25f;
    case ShaderId::VlPolychrome: return 963.f;
    case ShaderId::CrtMap: return 25.25f;
    case ShaderId::BgMerged: return 21.f;
    case ShaderId::BgField8: return 263.f;
    case ShaderId::BgColor8: return 49.f;
    case ShaderId::SplashField: return 255.f;
    case ShaderId::SplashColor: return 23.f;
    case ShaderId::VlDissolve: return 77.f;
    case ShaderId::VlDissolveBaked: return 35.f;
    case ShaderId::VlD0Dissolve: return 19.f;
    case ShaderId::VlD0Foil: return 76.f;
    case ShaderId::VlD0Holo: return 154.f;
    case ShaderId::VlD0Polychrome: return 131.f;
    case ShaderId::VlD0Negative: return 82.f;
    case ShaderId::VlD0NegativeShine: return 65.f;
    case ShaderId::VlD0Booster: return 67.f;
    case ShaderId::VlD0Voucher: return 65.f;
    case ShaderId::VlD0Debuff: return 369.f;
    case ShaderId::VlD0Played: return 74.f;
    case ShaderId::VlD0Hologram: return 77.f;
    default:
        if (id == ShaderId::VlDmVoucher || id == ShaderId::VlDmBooster) return 90.f;
        if (id == ShaderId::VlDmHolo) return 181.f;
        if (id == ShaderId::VlDmFoil) return 98.f;
        if (id == ShaderId::VlDmNegativeShine) return 89.f;
        if (id == ShaderId::VlDmPolychrome) return 156.f;
        if (id == ShaderId::VlDissolveField) return 124.f;
        if (id == ShaderId::VlDmHologram) return 103.f;
        if (id >= ShaderId::VlDmFoil && id <= ShaderId::VlDmHologram) return 150.f;
        return 4;
    }
}

ShaderId dissolve_fast_variant(ShaderId id) {
    switch (id) {
    case ShaderId::Foil: return ShaderId::VlDmFoil;
    case ShaderId::Holo: return ShaderId::VlDmHolo;
    case ShaderId::Polychrome: return ShaderId::VlDmPolychrome;
    case ShaderId::Negative: return ShaderId::VlDmNegative;
    case ShaderId::NegativeShine: return ShaderId::VlDmNegativeShine;
    case ShaderId::Booster: return ShaderId::VlDmBooster;
    case ShaderId::Voucher: return ShaderId::VlDmVoucher;
    case ShaderId::Debuff: return ShaderId::VlDmDebuff;
    case ShaderId::Played: return ShaderId::VlDmPlayed;
    case ShaderId::Hologram: return ShaderId::VlDmHologram;
    default: return id;
    }
}

bool is_fast_dissolve(ShaderId id) {
    return id == ShaderId::VlDissolve || id == ShaderId::VlDissolveBaked || (id >= ShaderId::VlDmFoil && id <= ShaderId::VlDmHologram);
}

ShaderId dissolve_zero_variant(ShaderId id) {
    switch (id) {
    case ShaderId::Dissolve: return ShaderId::VlD0Dissolve;
    case ShaderId::Foil: return ShaderId::VlD0Foil;
    case ShaderId::Holo: return ShaderId::VlD0Holo;
    case ShaderId::Polychrome: return ShaderId::VlD0Polychrome;
    case ShaderId::Negative: return ShaderId::VlD0Negative;
    case ShaderId::NegativeShine: return ShaderId::VlD0NegativeShine;
    case ShaderId::Booster: return ShaderId::VlD0Booster;
    case ShaderId::Voucher: return ShaderId::VlD0Voucher;
    case ShaderId::Debuff: return ShaderId::VlD0Debuff;
    case ShaderId::Played: return ShaderId::VlD0Played;
    case ShaderId::Hologram: return ShaderId::VlD0Hologram;
    default: return id;
    }
}

}  // namespace vl::gfx
