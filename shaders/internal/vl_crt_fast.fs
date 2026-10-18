// CRT.fs specialised for glitch_intensity == 0 and bloom_fac == 0 (the normal case).
//
// The scanline mask is separable: each channel is clamp(-0.3 + f(tc.y * scanlines)
// - g(tc.x * scanlines * 4), -1, 2) with f and g 2*pi-periodic, so f and g are read from two
// 256-texel repeating lookup textures (vl_Aux1 = f, vl_Aux2 = g) instead of six sin/cos per
// pixel. Blending is disabled: the result is written as it would end up after alpha-blending onto
// the cleared black backbuffer, so the backbuffer needs no clear.
// Same uniforms and the same whole-screen vertex tilt towards the cursor as CRT.fs.
extern MY_HIGHP_OR_MEDIUMP vec2 distortion_fac;
extern MY_HIGHP_OR_MEDIUMP vec2 scale_fac;
extern MY_HIGHP_OR_MEDIUMP number feather_fac;
extern MY_HIGHP_OR_MEDIUMP number crt_intensity;
extern MY_HIGHP_OR_MEDIUMP number scanlines;
uniform Image vl_Aux1;
uniform Image vl_Aux2;

#define BUFF 0.01

vec4 effect(vec4 color, Image tex, vec2 tc, vec2 pc)
{
    tc = tc * 2.0 - vec2(1.0);
    tc *= scale_fac;
    tc += (tc.yx * tc.yx) * tc * (distortion_fac - 1.0);
    MY_HIGHP_OR_MEDIUMP number mask = (1.0 - smoothstep(1.0 - feather_fac, 1.0, abs(tc.x) - BUFF))
                * (1.0 - smoothstep(1.0 - feather_fac, 1.0, abs(tc.y) - BUFF));
    tc = (tc + vec2(1.0)) / 2.0;

    MY_HIGHP_OR_MEDIUMP vec4 crt_tex = Texel(tex, tc);
    MY_HIGHP_OR_MEDIUMP float crt_amout_adjusted = max(0., (crt_intensity) / (0.16 * 0.3));
    if (crt_amout_adjusted > 0.0000001) {
        crt_tex.r = crt_tex.r * (1. - crt_amout_adjusted) + crt_amout_adjusted * Texel(tex, tc + vec2(0.0005 * 1600. / love_ScreenSize.x, 0.)).r;
        crt_tex.g = crt_tex.g * (1. - crt_amout_adjusted) + crt_amout_adjusted * Texel(tex, tc + vec2(-0.0005 * 1600. / love_ScreenSize.x, 0.)).g;
    }
    MY_HIGHP_OR_MEDIUMP vec3 rgb_result = crt_tex.rgb * (1.0 - crt_intensity);

    // f in [-2, 2] stored as (f + 2) / 4, g in [0, 0.8] stored as g / 0.8
    vec3 f = Texel(vl_Aux1, vec2(tc.y * scanlines * 0.15915494, 0.5)).rgb * 4.0 - 2.0;
    vec3 g = Texel(vl_Aux2, vec2(tc.x * scanlines * 4.0 * 0.15915494, 0.5)).rgb * 0.8;
    MY_HIGHP_OR_MEDIUMP vec3 rgb_scanline = clamp(vec3(-0.3) + f - g, -1.0, 2.0);
    rgb_result += crt_tex.rgb * rgb_scanline * crt_intensity;

    rgb_result -= vec3(0.55);
    rgb_result = rgb_result * (1.0 + 0.14 + crt_amout_adjusted * 0.012);
    rgb_result += vec3(0.5);
    // = what alpha blending (rgb * mask) with alpha mask over black stores: the fragment output
    // is saturated before blending
    return vec4(clamp(rgb_result * mask, 0.0, 1.0) * mask, 1.0);
}

#ifdef VERTEX
extern MY_HIGHP_OR_MEDIUMP vec2 mouse_screen_pos;
extern MY_HIGHP_OR_MEDIUMP float hovering;
extern MY_HIGHP_OR_MEDIUMP float screen_scale;

vec4 position( mat4 transform_projection, vec4 vertex_position )
{
    if (hovering <= 0.){
        return transform_projection * vertex_position;
    }
    MY_HIGHP_OR_MEDIUMP float mid_dist = screen_scale*length(vertex_position.xy/screen_scale - 0.5*love_ScreenSize.xy)/length(love_ScreenSize.xy);
    MY_HIGHP_OR_MEDIUMP vec2 mouse_offset = (vertex_position.xy - mouse_screen_pos.xy)/screen_scale;
    MY_HIGHP_OR_MEDIUMP float scale = 0.002*(-0.03 - 0.3*max(0., 0.3-mid_dist))
                *hovering*(length(mouse_offset)*length(mouse_offset))/(2. -mid_dist);

    return transform_projection * vertex_position + vec4(0,0,0,scale);
}
#endif
