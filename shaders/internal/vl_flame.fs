#if defined(VERTEX) || __VERSION__ > 100 || defined(GL_FRAGMENT_PRECISION_HIGH)
	#define MY_HIGHP_OR_MEDIUMP highp
#else
	#define MY_HIGHP_OR_MEDIUMP mediump
#endif

// flame.fs with its grid field read from vl_flame_field's bake (vl_Aux1), plus the per-pixel terms
// the original adds afterwards, written without branches (a conditional sends the Vita's GPU into
// its per-instance mode). Only drawn when amount >= 0.1 (below that flame.fs is fully transparent
// and the sprite is not drawn at all).
extern MY_HIGHP_OR_MEDIUMP float amount;
extern MY_HIGHP_OR_MEDIUMP vec4 texture_details;
extern MY_HIGHP_OR_MEDIUMP vec2 image_details;
extern MY_HIGHP_OR_MEDIUMP vec4 colour_1;
extern MY_HIGHP_OR_MEDIUMP vec4 colour_2;
uniform Image vl_Aux1;

#define PIXEL_SIZE_FAC 60.

vec4 effect( vec4 colour, Image texture, vec2 texture_coords, vec2 screen_coords )
{
    MY_HIGHP_OR_MEDIUMP float intensity = 1.0*min(10.,amount);
    MY_HIGHP_OR_MEDIUMP vec2 uv = (((texture_coords)*(image_details)) - texture_details.xy*texture_details.ba)/texture_details.ba - 0.5;
    MY_HIGHP_OR_MEDIUMP vec2 cell = floor(uv*PIXEL_SIZE_FAC);
    MY_HIGHP_OR_MEDIUMP float smoke_res = Texel(vl_Aux1, (cell + 30.5)/PIXEL_SIZE_FAC).r + 1.;

    // if(abs(uv.x) > 0.4) smoke_res += 10.*(abs(uv.x) - 0.4)
    smoke_res = smoke_res + 10.*max(abs(uv.x) - 0.4, 0.);
    // if(length(...) < min(0.1, intensity*0.5) && smoke_res > 1.) smoke_res += min(8.5,intensity*10.)*(length(...)-0.1)
    MY_HIGHP_OR_MEDIUMP float l = length((uv - vec2(0., 0.1))*vec2(0.19, 1.));
    MY_HIGHP_OR_MEDIUMP float inner = (1. - step(min(0.1, intensity*0.5), l))*(1. - step(smoke_res, 1.));
    smoke_res = smoke_res + inner*min(8.5,intensity*10.)*(l-0.1);

    // smoke_res > 1.: transparent; else uv.y < 0.12: the hot base
    MY_HIGHP_OR_MEDIUMP float visible = step(smoke_res, 1.);
    MY_HIGHP_OR_MEDIUMP float base = 1. - step(0.12, uv.y);
    MY_HIGHP_OR_MEDIUMP float d = 0.12 - uv.y;
    MY_HIGHP_OR_MEDIUMP vec4 hot = colour_1*(1. - 0.5*d) + 2.5*d*colour_2;
    hot += hot*(-2.+0.5*intensity*smoke_res)*d;
    return vec4(mix(colour_1.rgb, hot.rgb, base*visible), visible);
}
