// Alternative to vl_bg_color + nearest expansion: one full-screen pass that finds the pixel's
// background block (same floor(pixel / pixel_size) grid as background.fs), samples the field
// (RGBA16F, bilinear) at the block's field position and applies background.fs' colour mapping.
extern number pixel_size;
extern number k;
extern vec2 field_size;
extern number contrast;
extern number spin_amount;
extern vec4 colour_1;
extern vec4 colour_2;
extern vec4 colour_3;

vec4 effect(vec4 colour, Image texture, vec2 texture_coords, vec2 screen_coords)
{
    vec2 b = floor(screen_coords * (1. / pixel_size));
    number L = Texel(texture, (b / k + 1.5) / field_size).r;
    number contrast_mod = (0.25 * contrast + 0.5 * spin_amount + 1.2);
    number paint_res = min(2., max(0., L * (0.035) * contrast_mod));
    number c1p = max(0., 1. - contrast_mod * abs(1. - paint_res));
    number c2p = max(0., 1. - contrast_mod * abs(paint_res));
    number c3p = 1. - min(1., c1p + c2p);
    return (0.3 / contrast) * colour_1 + (1. - 0.3 / contrast) * (colour_1 * c1p + colour_2 * c2p + vec4(c3p * colour_3.rgb, c3p * colour_1.a));
}
