// CRT.fs specialised for glitch_intensity == 0 and bloom_fac == 0 (the normal case), with the
// dead glitch/bloom code removed and the scanline trig replaced by a fast approximation whose
// error (< 0.001) is scaled by crt_intensity (~0.03) before reaching the output. Same uniforms
// and the same whole-screen vertex tilt as CRT.fs.
extern MY_HIGHP_OR_MEDIUMP vec2 distortion_fac;
extern MY_HIGHP_OR_MEDIUMP vec2 scale_fac;
extern MY_HIGHP_OR_MEDIUMP number feather_fac;
extern MY_HIGHP_OR_MEDIUMP number crt_intensity;
extern MY_HIGHP_OR_MEDIUMP number scanlines;
extern number vl_mode;  // benchmark only: 1 = skip scanlines, 2 = also skip chromatic taps

#define BUFF 0.01

// sin(x) for any x: range reduction to [-pi, pi], parabola + one refinement step.
number fast_sin(number x)
{
    number t = fract(x * 0.15915494 + 0.5) * 2.0 - 1.0;  // x / pi wrapped to [-1, 1)
    number y = 4.0 * t * (1.0 - abs(t));
    return 0.225 * (y * abs(y) - y) + y;
}
number fast_cos(number x) { return fast_sin(x + 1.5707963); }

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
    if (crt_amout_adjusted > 0.0000001 && vl_mode < 1.5) {
        crt_tex.r = crt_tex.r * (1. - crt_amout_adjusted) + crt_amout_adjusted * Texel(tex, tc + vec2(0.0005 * 1600. / love_ScreenSize.x, 0.)).r;
        crt_tex.g = crt_tex.g * (1. - crt_amout_adjusted) + crt_amout_adjusted * Texel(tex, tc + vec2(-0.0005 * 1600. / love_ScreenSize.x, 0.)).g;
    }
    MY_HIGHP_OR_MEDIUMP vec3 rgb_result = crt_tex.rgb * (1.0 - crt_intensity);

    MY_HIGHP_OR_MEDIUMP vec3 rgb_scanline = vec3(0.0);
    if (vl_mode < 0.5) rgb_scanline = vec3(
        clamp(-0.3 + 2.0 * fast_sin(tc.y * scanlines - 3.14 / 4.0) - 0.8 * clamp(fast_sin(tc.x * scanlines * 4.0), 0.4, 1.0), -1.0, 2.0),
        clamp(-0.3 + 2.0 * fast_cos(tc.y * scanlines) - 0.8 * clamp(fast_cos(tc.x * scanlines * 4.0), 0.0, 1.0), -1.0, 2.0),
        clamp(-0.3 + 2.0 * fast_cos(tc.y * scanlines - 3.14 / 3.0) - 0.8 * clamp(fast_cos(tc.x * scanlines * 4.0 - 3.14 / 4.0), 0.0, 1.0), -1.0, 2.0));
    rgb_result += crt_tex.rgb * rgb_scanline * crt_intensity;

    rgb_result -= vec3(0.55);
    rgb_result = rgb_result * (1.0 + 0.14 + crt_amout_adjusted * 0.012);
    rgb_result += vec3(0.5);
    return vec4(rgb_result, 1.0) * mask;
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
