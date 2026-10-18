// Vita implementation of the polychrome edition effect (same output as polychrome.fs).
// - The per-draw field offsets (six sin/cos of the time only) are computed on the CPU and passed
//   in vl_poly_a / vl_poly_b.
// - HSL <-> RGB conversions without branches (branches force per-instance execution).
// - Remaining trig uses a fast sin/cos (max error 0.001).
extern MY_HIGHP_OR_MEDIUMP vec2 polychrome;
extern MY_HIGHP_OR_MEDIUMP number dissolve;
extern MY_HIGHP_OR_MEDIUMP number time;
extern MY_HIGHP_OR_MEDIUMP vec4 texture_details;
extern MY_HIGHP_OR_MEDIUMP vec2 image_details;
extern bool shadow;
extern MY_HIGHP_OR_MEDIUMP vec4 burn_colour_1;
extern MY_HIGHP_OR_MEDIUMP vec4 burn_colour_2;
// xy: offset of field part 1, zw: part 2
extern MY_HIGHP_OR_MEDIUMP vec4 vl_poly_a;
// xy: offset of field part 3, z: polychrome.x * 2.612 - 0.5 * 3.14, w: polychrome.y * 0.04
extern MY_HIGHP_OR_MEDIUMP vec4 vl_poly_b;

number fsin(number x)
{
    number t = fract(x * 0.15915494 + 0.5) * 2.0 - 1.0;
    number y = 4.0 * t * (1.0 - abs(t));
    return 0.225 * (y * abs(y) - y) + y;
}
number fcos(number x) { return fsin(x + 1.5707963); }

vec3 to_hsl(vec3 c)
{
    number lo = min(c.r, min(c.g, c.b));
    number hi = max(c.r, max(c.g, c.b));
    number delta = hi - lo;
    number sum = hi + lo;
    number l = 0.5 * sum;
    number d = max(delta, 1e-20);
    number s = l < 0.5 ? delta / max(sum, 1e-20) : delta / max(2.0 - sum, 1e-20);
    number h = hi == c.r ? (c.g - c.b) / d : (hi == c.g ? (c.b - c.r) / d + 2.0 : (c.r - c.g) / d + 4.0);
    h = fract(h / 6.0);
    return delta == 0.0 ? vec3(0.0, 0.0, l) : vec3(h, s, l);
}

number hue_channel(number s, number t, number h)
{
    number hs = fract(h) * 6.0;
    return s + (t - s) * clamp(min(hs, 4.0 - hs), 0.0, 1.0);
}

vec3 to_rgb(vec3 c)
{
    // saturation is always >= 0.5 here, so the grey special case of the original never applies
    number t = (c.z < 0.5) ? c.y * c.z + c.z : -c.y * c.z + (c.y + c.z);
    number s = 2.0 * c.z - t;
    return vec3(hue_channel(s, t, c.x + 1.0 / 3.0), hue_channel(s, t, c.x), hue_channel(s, t, c.x - 1.0 / 3.0));
}

vec4 dissolve_mask(vec4 tex, vec2 uv)
{
    if (dissolve < 0.001) {
        return vec4(shadow ? vec3(0., 0., 0.) : tex.xyz, shadow ? tex.a * 0.3 : tex.a);
    }
    number adjusted_dissolve = (dissolve * dissolve * (3. - 2. * dissolve)) * 1.02 - 0.01;
    number t = time * 10.0 + 2003.;
    vec2 floored_uv = (floor((uv * texture_details.ba))) / max(texture_details.b, texture_details.a);
    vec2 uv_scaled_centered = (floored_uv - 0.5) * 2.3 * max(texture_details.b, texture_details.a);
    vec2 field_part1 = uv_scaled_centered + 50. * vec2(fsin(-t / 143.6340), fcos(-t / 99.4324));
    vec2 field_part2 = uv_scaled_centered + 50. * vec2(fcos(t / 53.1532), fcos(t / 61.4532));
    vec2 field_part3 = uv_scaled_centered + 50. * vec2(fsin(-t / 87.53218), fsin(-t / 49.0000));
    number field = (1. + (
        fcos(length(field_part1) / 19.483) + fsin(length(field_part2) / 33.155) * fcos(field_part2.y / 15.73) +
        fcos(length(field_part3) / 27.193) * fsin(field_part3.x / 21.92))) / 2.;
    vec2 borders = vec2(0.2, 0.8);
    number res = (.5 + .5 * fcos((adjusted_dissolve) / 82.612 + (field + -.5) * 3.14))
        - (floored_uv.x > borders.y ? (floored_uv.x - borders.y) * (5. + 5. * dissolve) : 0.) * (dissolve)
        - (floored_uv.y > borders.y ? (floored_uv.y - borders.y) * (5. + 5. * dissolve) : 0.) * (dissolve)
        - (floored_uv.x < borders.x ? (borders.x - floored_uv.x) * (5. + 5. * dissolve) : 0.) * (dissolve)
        - (floored_uv.y < borders.x ? (borders.x - floored_uv.y) * (5. + 5. * dissolve) : 0.) * (dissolve);
    if (tex.a > 0.01 && burn_colour_1.a > 0.01 && !shadow && res < adjusted_dissolve + 0.8 * (0.5 - abs(adjusted_dissolve - 0.5)) && res > adjusted_dissolve) {
        if (!shadow && res < adjusted_dissolve + 0.5 * (0.5 - abs(adjusted_dissolve - 0.5)) && res > adjusted_dissolve) {
            tex.rgba = burn_colour_1.rgba;
        } else if (burn_colour_2.a > 0.01) {
            tex.rgba = burn_colour_2.rgba;
        }
    }
    return vec4(shadow ? vec3(0., 0., 0.) : tex.xyz, res > adjusted_dissolve ? (shadow ? tex.a * 0.3 : tex.a) : .0);
}

vec4 effect(vec4 colour, Image texture, vec2 texture_coords, vec2 screen_coords)
{
    vec4 tex = Texel(texture, texture_coords);
    vec2 uv = (((texture_coords) * (image_details)) - texture_details.xy * texture_details.ba) / texture_details.ba;

    number delta = max(tex.r, max(tex.g, tex.b)) - min(tex.r, min(tex.g, tex.b));
    number saturation_fac = 1. - max(0., 0.05 * (1.1 - delta));
    vec3 hsl = to_hsl(vec3(tex.r * saturation_fac, tex.g * saturation_fac, tex.b));

    vec2 uv_scaled_centered = ((floor((uv * texture_details.ba))) / texture_details.ba - 0.5) * 50.;
    vec2 field_part1 = uv_scaled_centered + vl_poly_a.xy;
    vec2 field_part2 = uv_scaled_centered + vl_poly_a.zw;
    vec2 field_part3 = uv_scaled_centered + vl_poly_b.xy;
    number field = (1. + (
        fcos(length(field_part1) / 19.483) + fsin(length(field_part2) / 33.155) * fcos(field_part2.y / 15.73) +
        fcos(length(field_part3) / 27.193) * fsin(field_part3.x / 21.92))) / 2.;
    number res = .5 + .5 * fcos(vl_poly_b.z + field * 3.14);

    hsl.x = hsl.x + res + vl_poly_b.w;
    hsl.y = min(0.6, hsl.y + 0.5);
    tex.rgb = to_rgb(hsl);
    if (tex.a < 0.7) tex.a = tex.a / 3.;
    return dissolve_mask(tex * colour, uv);
}

extern MY_HIGHP_OR_MEDIUMP vec2 mouse_screen_pos;
extern MY_HIGHP_OR_MEDIUMP float hovering;
extern MY_HIGHP_OR_MEDIUMP float screen_scale;

#ifdef VERTEX
vec4 position(mat4 transform_projection, vec4 vertex_position)
{
    if (hovering <= 0.) {
        return transform_projection * vertex_position;
    }
    float mid_dist = length(vertex_position.xy - 0.5 * love_ScreenSize.xy) / length(love_ScreenSize.xy);
    vec2 mouse_offset = (vertex_position.xy - mouse_screen_pos.xy) / screen_scale;
    float scale = 0.2 * (-0.03 - 0.3 * max(0., 0.3 - mid_dist))
                * hovering * (length(mouse_offset) * length(mouse_offset)) / (2. - mid_dist);
    return transform_projection * vertex_position + vec4(0, 0, 0, scale);
}
#endif
