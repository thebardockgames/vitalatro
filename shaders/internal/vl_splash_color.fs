// Pass 2 of the optimised splash.fs: one fragment per block, the smoke field sampled with hardware
// bilinear filtering (see vl_bg_color.fs), then splash.fs' colour mapping.
//   smoke_t = 0.17*min(10, time*1.2 - 4)
extern number smoke_t;
extern number mid_flash;
extern vec4 colour_1;
extern vec4 colour_2;

#define BLACK 0.6*vec4(79./255.,99./255., 103./255., 1./0.6)

vec4 effect(vec4 colour, Image texture, vec2 texture_coords, vec2 screen_coords)
{
    number L = Texel(texture, texture_coords).r;
    number smoke_res = min(2., max(-2., 1.5 + L * 0.12 - smoke_t));
    if (smoke_res < 0.2) {
        smoke_res = (smoke_res - 0.2) * 0.6 + 0.2;
    }
    number c1p = max(0., 1. - 2. * abs(1. - smoke_res));
    number c2p = max(0., 1. - 2. * (smoke_res));
    number cb = 1. - min(1., c1p + c2p);
    vec4 ret_col = colour_1 * c1p + colour_2 * c2p + vec4(cb * BLACK.rgb, cb * colour_1.a);
    number mod_flash = max(mid_flash * 0.8, max(c1p, c2p) * 5. - 4.4) + mid_flash * max(c1p, c2p);
    return ret_col * (1. - mod_flash) + mod_flash * vec4(1., 1., 1., 1.);
}
