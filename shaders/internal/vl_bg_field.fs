// Pass 1 of the optimised background: the expensive paint field of background.fs, evaluated once
// per field texel into an RGBA16F canvas. Field texel i (1 texel margin) samples block (i-1)*k, at
// the block origin the original shader floors its screen coordinates to. The paint loop uses a
// fast sin/cos (max error 0.001; measured colour difference 0.04/255 on average).
extern number time;
extern number spin_time;
extern number spin_amount;
extern number k;
extern vec2 screen;
extern number pixel_size;

#define SPIN_EASE 0.5

number fast_sin(number x)
{
    number t = fract(x * 0.15915494 + 0.5) * 2.0 - 1.0;
    number y = 4.0 * t * (1.0 - abs(t));
    return 0.225 * (y * abs(y) - y) + y;
}
number fast_cos(number x) { return fast_sin(x + 1.5707963); }

vec4 effect(vec4 colour, Image texture, vec2 texture_coords, vec2 screen_coords)
{
    vec2 blk = (floor(screen_coords) - 1.0) * k;
    number diag = length(screen);
    vec2 uv = (blk * pixel_size - 0.5 * screen) / diag - vec2(0.12, 0.);
    number uv_len = length(uv);

    number speed = (spin_time * SPIN_EASE * 0.2) + 302.2;
    number new_pixel_angle = (atan(uv.y, uv.x)) + speed - SPIN_EASE * 20. * (1. * spin_amount * uv_len + (1. - 1. * spin_amount));
    vec2 mid = (screen / diag) / 2.;
    uv = (vec2((uv_len * cos(new_pixel_angle) + mid.x), (uv_len * sin(new_pixel_angle) + mid.y)) - mid);

    uv *= 30.;
    speed = time * (2.);
    vec2 uv2 = vec2(uv.x + uv.y);

    for (int i = 0; i < 5; i++) {
        uv2 += fast_sin(max(uv.x, uv.y)) + uv;
        uv += 0.5 * vec2(fast_cos(5.1123314 + 0.353 * uv2.y + speed * 0.131121), fast_sin(uv2.x - 0.113 * speed));
        uv -= 1.0 * fast_cos(uv.x + uv.y) - 1.0 * fast_sin(uv.x * 0.711 - uv.y);
    }

    return vec4(length(uv), 0.0, 0.0, 1.0);
}
