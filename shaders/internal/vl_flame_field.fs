#if defined(VERTEX) || __VERSION__ > 100 || defined(GL_FRAGMENT_PRECISION_HIGH)
	#define MY_HIGHP_OR_MEDIUMP highp
#else
	#define MY_HIGHP_OR_MEDIUMP mediump
#endif

// flame.fs's smoke field, baked per frame into a 60x60 RGBA16F canvas. The original quantizes the
// sprite to a 60x60 grid (floored_uv = floor(uv*60)/60) and its expensive part (the 5-step swirl)
// only reads floored_uv, time, amount and id, so it is computed once per grid cell here instead of
// once per pixel; vl_flame.fs reads the cell back and adds the per-pixel terms.
// Texel i (0..59) is the cell floor(uv*60) = i - 30. Output: r = smoke_res - 1 (f16 is finest near 0,
// where the smoke_res > 1 test is decided).
//   vl_flame_a = (time, amount, id, 0)
extern MY_HIGHP_OR_MEDIUMP vec4 vl_flame_a;

#define PIXEL_SIZE_FAC 60.

vec4 effect( vec4 colour, Image texture, vec2 texture_coords, vec2 screen_coords )
{
    MY_HIGHP_OR_MEDIUMP float time = vl_flame_a.x;
    MY_HIGHP_OR_MEDIUMP float id = vl_flame_a.z;
    MY_HIGHP_OR_MEDIUMP float intensity = 1.0*min(10.,vl_flame_a.y);

    MY_HIGHP_OR_MEDIUMP vec2 floored_uv = (floor(texture_coords*PIXEL_SIZE_FAC) - 30.)/PIXEL_SIZE_FAC;
    MY_HIGHP_OR_MEDIUMP vec2 uv_scaled_centered = (floored_uv);
    uv_scaled_centered += uv_scaled_centered*0.01*(sin(-1.123*floored_uv.x + 0.2*time)*cos(5.3332*floored_uv.y + time*0.931));
    MY_HIGHP_OR_MEDIUMP vec2 flame_up_vec = vec2(0., mod(4.*time, 10000.) - 5000. + mod(1.781*id, 1000.) );

    MY_HIGHP_OR_MEDIUMP float scale_fac = (7.5 + 3./(2. + 2.*intensity));
    MY_HIGHP_OR_MEDIUMP vec2 sv = uv_scaled_centered*scale_fac + flame_up_vec;
    MY_HIGHP_OR_MEDIUMP float speed = mod(20.781*id, 100.) + 1.*sin(time+id)*cos(time*0.151+id);
    MY_HIGHP_OR_MEDIUMP vec2 sv2 = vec2(0.,0.);

    // (the original's (mod(float(i), 2.)>1.?-1.:1.) is always 1.: mod gives 0 or 1)
    for(int i=0; i < 5; i++) {
        sv2 += sv + 0.05*sv2.yx + 0.3*(cos(length(sv)*0.411) + 0.3344*sin(length(sv)) - 0.23*cos(length(sv)));
        sv  += 0.5*vec2(
                    cos(cos(sv2.y) + speed*0.0812)*sin(3.22 + (sv2.x) - speed*0.1531),
                    sin(-sv2.x*1.21222 + 0.113785*speed)*cos(sv2.y*0.91213 - 0.13582*speed));
    }

    MY_HIGHP_OR_MEDIUMP float smoke_res = max(0.,((length((sv - flame_up_vec)/scale_fac*5.)+ 0.1*(length(uv_scaled_centered) - 0.5))*(2./(2.+ intensity*0.2)))) ;
    smoke_res = smoke_res + max(0., 2. - 0.3*intensity)*max(0., 2.*(uv_scaled_centered.y - 0.5)*(uv_scaled_centered.y - 0.5));
    return vec4(smoke_res - 1., 0., 0., 1.);
}
