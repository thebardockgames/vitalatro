// Pass 2: one fragment per background block. Interpolates the field bilinearly between the field
// texels around the block and applies background.fs' colour mapping.
extern number k;
extern number contrast;
extern number spin_amount;
extern vec2 field_size;
extern vec4 colour_1;
extern vec4 colour_2;
extern vec4 colour_3;

number field_at(Image f, vec2 t)
{
    vec4 v = Texel(f, (t + 0.5) / field_size);
    return (floor(v.r * 255.0 + 0.5) + v.g) / 255.0 * 64.0;
}

vec4 effect(vec4 colour, Image texture, vec2 texture_coords, vec2 screen_coords)
{
    vec2 f = floor(screen_coords) / k + 1.0;
    vec2 t0 = floor(f);
    vec2 w = f - t0;
    number L = mix(mix(field_at(texture, t0), field_at(texture, t0 + vec2(1.0, 0.0)), w.x),
                   mix(field_at(texture, t0 + vec2(0.0, 1.0)), field_at(texture, t0 + vec2(1.0, 1.0)), w.x), w.y);

    number contrast_mod = (0.25 * contrast + 0.5 * spin_amount + 1.2);
    number paint_res = min(2., max(0., L * (0.035) * contrast_mod));
    number c1p = max(0., 1. - contrast_mod * abs(1. - paint_res));
    number c2p = max(0., 1. - contrast_mod * abs(paint_res));
    number c3p = 1. - min(1., c1p + c2p);
    return (0.3 / contrast) * colour_1 + (1. - 0.3 / contrast) * (colour_1 * c1p + colour_2 * c2p + vec4(c3p * colour_3.rgb, c3p * colour_1.a));
}
