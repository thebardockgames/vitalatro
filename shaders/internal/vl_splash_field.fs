// Pass 1 of the optimised splash.fs (title vortex): its smoke field, evaluated once per field
// texel into an RGBA16F canvas, like vl_bg_field.fs does for background.fs. MainTex holds each
// block's polar coordinates around the screen centre (RG = radius, BA = angle, 16-bit fixed point).
// Terms that only depend on uniforms are computed on the CPU:
//   swirl     = 2.2 + 0.4*min(6, speed)
//   angle_add = -1 - speed*0.05 - min(6, speed)*speed*0.02 + vort_offset
//   k1 = 5.1123314 + speed2*0.131121, k2 = 0.113*speed2   (speed2 = time*6*vort_speed + vort_offset + 1033)
extern number swirl;
extern number angle_add;
extern number k1;
extern number k2;

number fast_sin(number x)
{
    number t = fract(x * 0.15915494 + 0.5) * 2.0 - 1.0;
    number y = 4.0 * t * (1.0 - abs(t));
    return 0.225 * (y * abs(y) - y) + y;
}
number fast_cos(number x) { return fast_sin(x + 1.5707963); }

#define SMOKE_STEP \
    uv2 += fast_sin(max(sv.x, sv.y)) + sv; \
    sv += 0.5 * vec2(fast_cos(k1 + 0.353 * uv2.y), fast_sin(uv2.x - k2)); \
    sv -= fast_cos(sv.x + sv.y) - fast_sin(sv.x * 0.711 - sv.y);

vec4 effect(vec4 colour, Image texture, vec2 texture_coords, vec2 screen_coords)
{
    vec4 p = floor(Texel(texture, texture_coords) * 255.0 + 0.5);
    number uv_len = (p.r * 256.0 + p.g) / 65535.0;
    number angle = (p.b * 256.0 + p.a) / 65535.0 * 6.2831853 - 3.1415927;

    number new_pixel_angle = angle + swirl * uv_len + angle_add;
    vec2 sv = vec2(uv_len * cos(new_pixel_angle), uv_len * sin(new_pixel_angle)) * 30.;
    vec2 uv2 = vec2(sv.x + sv.y);
    SMOKE_STEP
    SMOKE_STEP
    SMOKE_STEP
    SMOKE_STEP
    SMOKE_STEP
    return vec4(length(sv), 0.0, 0.0, 1.0);
}
