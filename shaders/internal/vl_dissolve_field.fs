#if defined(VERTEX) || __VERSION__ > 100 || defined(GL_FRAGMENT_PRECISION_HIGH)
	#define MY_HIGHP_OR_MEDIUMP highp
#else
	#define MY_HIGHP_OR_MEDIUMP mediump
#endif

// dissolve.fs's field for one sprite, baked into an RGBA16F canvas with one texel per sprite
// pixel (drawn as a full-canvas quad in the frame's pre-pass). The field only depends on the
// sprite's pixel and its ID (through t), so it is baked once per sprite; vl_dissolve_baked.fs and
// the vl_dm_* variants then only add the dissolve amount:
//   res = .5 + .5 * cos(ad / 82.612 + g) - border * k = .5 + .5 * (cos a * r - sin a * g) - b * k
// Output: r = cos(g), g = sin(g), b = border, g = (field - .5) * 3.14.
//   vl_dis_a = (field offset 1, field offset 2)  -- 50*vec2(sin(-t/143.634), cos(-t/99.4324)), ...
//   vl_dis_b = (field offset 3, sprite size in pixels)
extern MY_HIGHP_OR_MEDIUMP vec4 vl_dis_a;
extern MY_HIGHP_OR_MEDIUMP vec4 vl_dis_b;

vec4 effect( vec4 colour, Image texture, vec2 texture_coords, vec2 screen_coords )
{
    MY_HIGHP_OR_MEDIUMP number m = max(vl_dis_b.z, vl_dis_b.w);
    MY_HIGHP_OR_MEDIUMP vec2 floored_uv = floor(texture_coords * vl_dis_b.zw) / m;
    MY_HIGHP_OR_MEDIUMP vec2 uv_scaled_centered = (floored_uv - 0.5) * 2.3 * m;

    MY_HIGHP_OR_MEDIUMP vec2 field_part1 = uv_scaled_centered + vl_dis_a.xy;
    MY_HIGHP_OR_MEDIUMP vec2 field_part2 = uv_scaled_centered + vl_dis_a.zw;
    MY_HIGHP_OR_MEDIUMP vec2 field_part3 = uv_scaled_centered + vl_dis_b.xy;

    MY_HIGHP_OR_MEDIUMP float field = (1.+ (
        cos(length(field_part1) / 19.483) + sin(length(field_part2) / 33.155) * cos(field_part2.y / 15.73) +
        cos(length(field_part3) / 27.193) * sin(field_part3.x / 21.92) ))/2.;
    MY_HIGHP_OR_MEDIUMP vec2 borders = vec2(0.2, 0.8);
    MY_HIGHP_OR_MEDIUMP float border = max(floored_uv.x - borders.y, 0.) + max(floored_uv.y - borders.y, 0.) +
        max(borders.x - floored_uv.x, 0.) + max(borders.x - floored_uv.y, 0.);
    MY_HIGHP_OR_MEDIUMP float g = (field - .5) * 3.14;
    return vec4(cos(g), sin(g), border, 1.);
}
