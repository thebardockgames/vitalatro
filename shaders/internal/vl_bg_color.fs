// Pass 2: one fragment per background block. The field (RGBA16F canvas, linear filtering) is
// drawn so that each block's texture coordinate lands on its field position: the hardware does the
// bilinear interpolation from an interpolated (non-dependent) coordinate. Then background.fs'
// colour mapping.
extern number contrast;
extern number spin_amount;
extern vec4 colour_1;
extern vec4 colour_2;
extern vec4 colour_3;

vec4 effect(vec4 colour, Image texture, vec2 texture_coords, vec2 screen_coords)
{
    number L = Texel(texture, texture_coords).r;
    number contrast_mod = (0.25 * contrast + 0.5 * spin_amount + 1.2);
    number paint_res = min(2., max(0., L * (0.035) * contrast_mod));
    number c1p = max(0., 1. - contrast_mod * abs(1. - paint_res));
    number c2p = max(0., 1. - contrast_mod * abs(paint_res));
    number c3p = 1. - min(1., c1p + c2p);
    return (0.3 / contrast) * colour_1 + (1. - 0.3 / contrast) * (colour_1 * c1p + colour_2 * c2p + vec4(c3p * colour_3.rgb, c3p * colour_1.a));
}
