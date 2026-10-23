#if defined(VERTEX) || __VERSION__ > 100 || defined(GL_FRAGMENT_PRECISION_HIGH)
	#define MY_HIGHP_OR_MEDIUMP highp
#else
	#define MY_HIGHP_OR_MEDIUMP mediump
#endif

// hologram.fs's glow (81 alpha samples around the pixel) only depends on the sprite's image, so it
// is baked once per sprite into an RGBA16F canvas covering the sprite's atlas cell, one texel per
// atlas pixel (drawn as a full-canvas quad in the frame's pre-pass, the atlas as MainTex). The
// vl_d0/vl_dm hologram variants read it back with linear filtering.
//   vl_glow_rect = the cell in atlas texture coordinates (x, y, w, h)
extern MY_HIGHP_OR_MEDIUMP vec4 vl_glow_rect;

vec4 effect( vec4 colour, Image texture, vec2 texture_coords, vec2 screen_coords )
{
    MY_HIGHP_OR_MEDIUMP vec2 tc = vl_glow_rect.xy + texture_coords * vl_glow_rect.zw;
    MY_HIGHP_OR_MEDIUMP number glow = 0.;
    MY_HIGHP_OR_MEDIUMP int glow_samples = 4;
    MY_HIGHP_OR_MEDIUMP int actual_glow_samples = 0;
    MY_HIGHP_OR_MEDIUMP number glow_dist = 0.0015;
    MY_HIGHP_OR_MEDIUMP number _a = 0.;
    for (int i = -glow_samples; i <= glow_samples; ++i){
        for (int j = -glow_samples; j <= glow_samples; ++j){
            _a = Texel( texture, tc + (glow_dist)*vec2(float(i), float(j))).a;
            if (_a < 0.9){
                actual_glow_samples += 1;
                glow = glow + _a;
            }
        }
    }
    glow /= 0.7*float(actual_glow_samples);
    return vec4(glow, 0., 0., 1.);
}
