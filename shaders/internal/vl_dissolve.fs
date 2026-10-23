#if defined(VERTEX) || __VERSION__ > 100 || defined(GL_FRAGMENT_PRECISION_HIGH)
	#define MY_HIGHP_OR_MEDIUMP highp
#else
	#define MY_HIGHP_OR_MEDIUMP mediump
#endif

// dissolve.fs while a card or sprite is dissolving/materializing (dissolve > 0), rewritten to run
// in the Vita's parallel mode: the time-only terms are computed on the CPU, trig uses the fast
// approximation and the branches are selects. psp2shaderperf: the original is 277 cycles in
// per-instance mode (~45 ms for the title logo alone).
//   vl_dis_a = (field offset 1, field offset 2)  -- 50*vec2(sin(-t/143.634), cos(-t/99.4324)), ...
//   vl_dis_b = (field offset 3, adjusted_dissolve, (5 + 5*dissolve)*dissolve)
//   vl_dis_c = (outer burn limit, inner burn limit, burn enabled, burn_colour_2 enabled)
//   vl_dis_tint = (burn tint colour, tint amount)    -- 0.6*dissolve, 0 for shadows
extern MY_HIGHP_OR_MEDIUMP vec4 texture_details;
extern MY_HIGHP_OR_MEDIUMP vec2 image_details;
extern bool shadow;
extern MY_HIGHP_OR_MEDIUMP vec4 burn_colour_1;
extern MY_HIGHP_OR_MEDIUMP vec4 burn_colour_2;
extern MY_HIGHP_OR_MEDIUMP vec4 vl_dis_a;
extern MY_HIGHP_OR_MEDIUMP vec4 vl_dis_b;
extern MY_HIGHP_OR_MEDIUMP vec4 vl_dis_c;
extern MY_HIGHP_OR_MEDIUMP vec4 vl_dis_tint;

number fast_sin(number x)
{
    number t = fract(x * 0.15915494 + 0.5) * 2.0 - 1.0;
    number y = 4.0 * t * (1.0 - abs(t));
    return 0.225 * (y * abs(y) - y) + y;
}
number fast_cos(number x) { return fast_sin(x + 1.5707963); }

#ifdef VERTEX
void vl_varyings(vec2 texture_coords, out vec4 v0, out vec4 v1, out vec4 v2)
{
    // sprite pixel coordinates (affine in texture_coords)
    v0 = vec4(((texture_coords)*(image_details)) - texture_details.xy*texture_details.ba, 0., 0.);
    v1 = vec4(0.);
    v2 = vec4(0.);
}
#endif
#ifdef PIXEL
vec4 fast_sin4(vec4 x)
{
    vec4 t = fract(x * 0.15915494 + 0.5) * 2.0 - 1.0;
    vec4 y = 4.0 * t * (1.0 - abs(t));
    return 0.225 * (y * abs(y) - y) + y;
}
vec4 vl_effect(vec4 colour, Image texture, vec2 texture_coords, vec4 v0, vec4 v1, vec4 v2)
{
    MY_HIGHP_OR_MEDIUMP vec4 tex = Texel( texture, texture_coords);
    tex.rgb = mix(tex.rgb, vl_dis_tint.rgb, vl_dis_tint.a);

    MY_HIGHP_OR_MEDIUMP number m = max(texture_details.b, texture_details.a);
    MY_HIGHP_OR_MEDIUMP vec2 floored_uv = floor(v0.xy)/m;
    MY_HIGHP_OR_MEDIUMP vec2 c = (floored_uv - 0.5) * 2.3 * m;
    MY_HIGHP_OR_MEDIUMP vec2 p1 = c + vl_dis_a.xy;
    MY_HIGHP_OR_MEDIUMP vec2 p2 = c + vl_dis_a.zw;
    MY_HIGHP_OR_MEDIUMP vec2 p3 = c + vl_dis_b.xy;
    // cos(|p1|/19.483), sin(|p2|/33.155), cos(p2.y/15.73), cos(|p3|/27.193); then sin(p3.x/21.92)
    MY_HIGHP_OR_MEDIUMP vec4 w = fast_sin4(vec4(length(p1) / 19.483 + 1.5707963, length(p2) / 33.155, p2.y / 15.73 + 1.5707963, length(p3) / 27.193 + 1.5707963));
    MY_HIGHP_OR_MEDIUMP number ad = vl_dis_b.z;
    MY_HIGHP_OR_MEDIUMP number s3 = fast_sin4(vec4(p3.x / 21.92)).x;
    MY_HIGHP_OR_MEDIUMP number field = (1. + (w.x + w.y * w.z + w.w * s3)) / 2.;
    MY_HIGHP_OR_MEDIUMP vec2 out_hi = max(floored_uv - 0.8, 0.);
    MY_HIGHP_OR_MEDIUMP vec2 out_lo = max(0.2 - floored_uv, 0.);
    MY_HIGHP_OR_MEDIUMP number res = (.5 + .5 * fast_sin4(vec4(ad / 82.612 + (field - .5) * 3.14 + 1.5707963)).x)
        - (out_hi.x + out_hi.y + out_lo.x + out_lo.y) * vl_dis_b.w;

    MY_HIGHP_OR_MEDIUMP number above = 1. - step(res, ad);           // res > ad
    MY_HIGHP_OR_MEDIUMP number band_outer = above * (1. - step(vl_dis_c.x, res));
    MY_HIGHP_OR_MEDIUMP number band_inner = above * (1. - step(vl_dis_c.y, res));
    MY_HIGHP_OR_MEDIUMP vec4 burned = mix(mix(tex, burn_colour_2, vl_dis_c.w), burn_colour_1, band_inner);
    tex = mix(tex, burned, band_outer * vl_dis_c.z * step(0.0100001, tex.a));

    MY_HIGHP_OR_MEDIUMP number s = shadow ? 1. : 0.;
    return vec4(tex.rgb * (1. - s), above * tex.a * (1. - 0.7 * s));
}
#endif

extern MY_HIGHP_OR_MEDIUMP vec2 mouse_screen_pos;
extern MY_HIGHP_OR_MEDIUMP float hovering;
extern MY_HIGHP_OR_MEDIUMP float screen_scale;

#ifdef VERTEX
vec4 position( mat4 transform_projection, vec4 vertex_position )
{
    if (hovering <= 0.){
        return transform_projection * vertex_position;
    }
    MY_HIGHP_OR_MEDIUMP float mid_dist = length(vertex_position.xy - 0.5*love_ScreenSize.xy)/length(love_ScreenSize.xy);
    MY_HIGHP_OR_MEDIUMP vec2 mouse_offset = (vertex_position.xy - mouse_screen_pos.xy)/screen_scale;
    MY_HIGHP_OR_MEDIUMP float scale = 0.2*(-0.03 - 0.3*max(0., 0.3-mid_dist))
                *hovering*(length(mouse_offset)*length(mouse_offset))/(2. -mid_dist);

    return transform_projection * vertex_position + vec4(0,0,0,scale);
}
#endif
