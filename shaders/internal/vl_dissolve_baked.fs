#if defined(VERTEX) || __VERSION__ > 100 || defined(GL_FRAGMENT_PRECISION_HIGH)
	#define MY_HIGHP_OR_MEDIUMP highp
#else
	#define MY_HIGHP_OR_MEDIUMP mediump
#endif

// dissolve.fs for large sprites (the title logo): its noise field depends only on the sprite's ID
// and the source texel, never on real time, so the CPU bakes it once per sprite into vl_Aux1
// (RGBA16F, one texel per source texel): R = cos(g), G = sin(g), B = border term, with
// g = (field - 0.5)*3.14. Per pixel only cos(ad/82.612 + g) is left, as an angle sum.
//   vl_dis_c = (outer burn limit, inner burn limit, burn enabled, burn_colour_2 enabled)
//   vl_dis_d = (cos(ad/82.612), sin(ad/82.612), adjusted_dissolve, (5 + 5*dissolve)*dissolve)
//   vl_dis_tint = (burn tint colour, tint amount)
extern MY_HIGHP_OR_MEDIUMP vec4 texture_details;
extern MY_HIGHP_OR_MEDIUMP vec2 image_details;
extern bool shadow;
extern MY_HIGHP_OR_MEDIUMP vec4 burn_colour_1;
extern MY_HIGHP_OR_MEDIUMP vec4 burn_colour_2;
extern MY_HIGHP_OR_MEDIUMP vec4 vl_dis_c;
extern MY_HIGHP_OR_MEDIUMP vec4 vl_dis_d;
extern MY_HIGHP_OR_MEDIUMP vec4 vl_dis_tint;
uniform Image vl_Aux1;

vec4 effect( vec4 colour, Image texture, vec2 texture_coords, vec2 screen_coords )
{
    MY_HIGHP_OR_MEDIUMP vec4 tex = Texel( texture, texture_coords);
    MY_HIGHP_OR_MEDIUMP vec2 uv = (((texture_coords)*(image_details)) - texture_details.xy*texture_details.ba)/texture_details.ba;
    tex.rgb = mix(tex.rgb, vl_dis_tint.rgb, vl_dis_tint.a);

    MY_HIGHP_OR_MEDIUMP vec4 f = Texel(vl_Aux1, uv);
    MY_HIGHP_OR_MEDIUMP number ad = vl_dis_d.z;
    MY_HIGHP_OR_MEDIUMP number res = .5 + .5 * (vl_dis_d.x * f.r - vl_dis_d.y * f.g) - f.b * vl_dis_d.w;

    MY_HIGHP_OR_MEDIUMP number above = 1. - step(res, ad);
    MY_HIGHP_OR_MEDIUMP number band_outer = above * (1. - step(vl_dis_c.x, res));
    MY_HIGHP_OR_MEDIUMP number band_inner = above * (1. - step(vl_dis_c.y, res));
    MY_HIGHP_OR_MEDIUMP vec4 burned = mix(mix(tex, burn_colour_2, vl_dis_c.w), burn_colour_1, band_inner);
    tex = mix(tex, burned, band_outer * vl_dis_c.z * step(0.0100001, tex.a));

    MY_HIGHP_OR_MEDIUMP number s = shadow ? 1. : 0.;
    return vec4(tex.rgb * (1. - s), above * tex.a * (1. - 0.7 * s));
}
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