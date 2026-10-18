// Pass 1 of the optimised background: the paint field of background.fs, evaluated once per field
// texel into an RGBA16F canvas. Field texel i (1 texel margin) stands for block (i-1)*k, at the
// block origin the original shader floors its screen coordinates to.
//
// The block's polar coordinates around the swirl centre never change, so they are precomputed on
// the CPU (MainTex, 16-bit fixed point: RG = radius, BA = angle) instead of calling atan2, which
// forces the slow per-instance execution mode on the Vita. The paint loop is unrolled and uses a
// fast sin/cos (max error 0.001; measured colour difference 0.04/255 on average).
extern number time;
extern number spin_time;
extern number spin_amount;

#define SPIN_EASE 0.5

number fast_sin(number x)
{
    number t = fract(x * 0.15915494 + 0.5) * 2.0 - 1.0;
    number y = 4.0 * t * (1.0 - abs(t));
    return 0.225 * (y * abs(y) - y) + y;
}
number fast_cos(number x) { return fast_sin(x + 1.5707963); }

#define PAINT_STEP \
    uv2 += fast_sin(max(uv.x, uv.y)) + uv; \
    uv += 0.5 * vec2(fast_cos(k1 + 0.353 * uv2.y), fast_sin(uv2.x - k2)); \
    uv -= fast_cos(uv.x + uv.y) - fast_sin(uv.x * 0.711 - uv.y);

vec4 effect(vec4 colour, Image texture, vec2 texture_coords, vec2 screen_coords)
{
    vec4 p = floor(Texel(texture, texture_coords) * 255.0 + 0.5);
    number uv_len = (p.r * 256.0 + p.g) / 65535.0;
    number angle = (p.b * 256.0 + p.a) / 65535.0 * 6.2831853 - 3.1415927;

    number speed = (spin_time * SPIN_EASE * 0.2) + 302.2;
    number new_pixel_angle = angle + speed - SPIN_EASE * 20. * (1. * spin_amount * uv_len + (1. - 1. * spin_amount));
    vec2 uv = vec2(uv_len * cos(new_pixel_angle), uv_len * sin(new_pixel_angle)) * 30.;

    speed = time * (2.);
    number k1 = 5.1123314 + speed * 0.131121;
    number k2 = 0.113 * speed;
    vec2 uv2 = vec2(uv.x + uv.y);
    PAINT_STEP
    PAINT_STEP
    PAINT_STEP
    PAINT_STEP
    PAINT_STEP
    return vec4(length(uv), 0.0, 0.0, 1.0);
}
