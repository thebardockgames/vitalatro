#!/usr/bin/env python3
"""Build the runtime data directory from the user's own copy of Balatro.

    build_assets.py <balatro_dir> <out_dir> <pc|vita>

Nothing from the game is stored in this repository: textures, fonts and shaders are taken from
<balatro_dir> at build time. For the Vita, the GLSL shaders are converted to Cg and compiled to
.gxp with psp2cgc from the PSP2 SDK (SCE_PSP2_SDK_DIR).
"""
import os
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import shader_to_cg  # noqa: E402


def copy_tree(src, dst, pattern):
    dst.mkdir(parents=True, exist_ok=True)
    for f in src.glob(pattern):
        if f.is_file():
            out = dst / f.relative_to(src)
            out.parent.mkdir(parents=True, exist_ok=True)
            if not out.exists() or out.stat().st_mtime < f.stat().st_mtime:
                shutil.copy2(f, out)


def gather_shaders(game_shaders, tmp):
    """Balatro's shaders plus Vitalatro's internal ones, in one directory."""
    shutil.rmtree(tmp, ignore_errors=True)
    tmp.mkdir(parents=True)
    for d in (game_shaders, HERE.parent / 'shaders' / 'internal'):
        for f in d.glob('*.fs'):
            shutil.copy2(f, tmp / f.name)
    for name in FAST_TRIG_SHADERS:
        src = (game_shaders / (name + '.fs')).read_text(encoding='utf-8')
        (tmp / ('vl_fast_' + name + '.fs')).write_text(fast_trig(src), encoding='utf-8')
    for name in D0_SHADERS:
        src = (game_shaders / (name + '.fs')).read_text(encoding='utf-8')
        d0 = branchless_hsl(dissolve_zero(src))
        if name in FAST_EFFECTS:
            d0 = replace_function(d0, 'vec4 effect(', FAST_EFFECTS[name])
        (tmp / ('vl_d0_' + name + '.fs')).write_text(fast_trig(d0), encoding='utf-8')
        if name != 'dissolve':
            dm = branchless_hsl(dissolve_fast(src))
            if name in FAST_EFFECTS:
                dm = replace_function(dm, 'vec4 effect(', FAST_EFFECTS[name])
            (tmp / ('vl_dm_' + name + '.fs')).write_text(fast_trig(dm), encoding='utf-8')
    return tmp


# Card shaders as drawn almost all the time: dissolve == 0, where dissolve_mask() is just its first
# return. Without the mask's dissolve > 0 path they compile to the GPU's parallel mode (booster:
# 477 cycles per-instance -> 127 parallel), and sin/cos use the fast approximation.
D0_SHADERS = ['dissolve', 'foil', 'holo', 'polychrome', 'negative', 'negative_shine', 'booster', 'voucher', 'debuff', 'played', 'hologram']


# The same shaders while dissolving (materialize/dissolve animations): dissolve_mask() reads the
# sprite's field, baked once per sprite on the GPU (vl_dissolve_field.fs, see Sprite::dissolve_field):
# rg = cos/sin((field - .5) * 3.14), b = the border term; vl_dis_d = cos/sin(ad / 82.612), ad, k.
DISSOLVE_BAKED_MASK = """{
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
}"""


def dissolve_fast(src):
    a = src.index('vec4 dissolve_mask(')
    i = src.index('{', a)
    depth = 0
    for b in range(i, len(src)):
        depth += {'{': 1, '}': -1}.get(src[b], 0)
        if depth == 0:
            break
    decls = 'extern MY_HIGHP_OR_MEDIUMP vec4 vl_dis_c;\nextern MY_HIGHP_OR_MEDIUMP vec4 vl_dis_d;\nuniform Image vl_Aux1;\n\n'
    return src[:a] + decls + src[a:i] + DISSOLVE_BAKED_MASK + src[b + 1:]


# Effects rewritten for the GPU with the same formulas: every sin/cos argument is affine in uv, so
# the vertex stage computes it (as a phase, x / 2pi + 0.5) and the pixel stage only evaluates the
# sines, four at a time. vl_varyings()/vl_effect() replace LOVE's effect() (see shader_to_cg.py).
SIN_PHASE4 = """vec4 vl_sin_phase4(vec4 p) { vec4 t = fract(p) * 2.0 - 1.0; vec4 y = 4.0 * t * (1.0 - abs(t)); return 0.225 * (y * abs(y) - y) + y; }
"""
# voucher.fs and booster.fs: the same shine, booster's delta keeps some of the colour
PACK_SHINE = """#ifdef VERTEX
void vl_varyings(vec2 texture_coords, out vec4 pin, out vec4 pout, out vec4 p5)
{
    MY_HIGHP_OR_MEDIUMP vec2 uv = (((texture_coords)*(image_details)) - texture_details.xy*texture_details.ba)/texture_details.ba;
    MY_HIGHP_OR_MEDIUMP number r = {U}.r;
    pin = vec4(r*5.3 + uv.y*4.2 - uv.x*4. + 1.5707963, r*2.3 + uv.x*8.2 + 1.5707963, r*5.3 + uv.y*3.2, r*1.3 + uv.y*13.2) * 0.15915494 + 0.5;
    pout = vec4(13.*uv.x+5.32*uv.y + r*12., 10.*uv.x+2.32*uv.y + r*5., 12.*uv.x+6.32*uv.y + r*6.111, 4.*uv.x+2.32*uv.y + r*8.111) * 0.15915494 + 0.5;
    p5 = vec4((0.5*16.*uv.x+5.32*uv.y + r*12.) * 0.15915494 + 0.5, 0., uv);
}
#endif
#ifdef PIXEL
""" + SIN_PHASE4 + """vec4 vl_effect(vec4 colour, Image texture, vec2 texture_coords, vec4 pin, vec4 pout, vec4 p5)
{
    MY_HIGHP_OR_MEDIUMP vec4 tex = Texel( texture, texture_coords);
    MY_HIGHP_OR_MEDIUMP number low = min(tex.r, min(tex.g, tex.b));
    MY_HIGHP_OR_MEDIUMP number high = max(tex.r, max(tex.g, tex.b));
    MY_HIGHP_OR_MEDIUMP number delta = {DELTA};
    // cos(A), cos(B), sin(C), sin(D): the terms nested inside fac..fac5
    MY_HIGHP_OR_MEDIUMP vec4 inner = vl_sin_phase4(pin);
    MY_HIGHP_OR_MEDIUMP vec4 outer = vl_sin_phase4(pout + inner * vec4(0.15915494, -0.15915494, 0.15915494, 0.15915494));
    MY_HIGHP_OR_MEDIUMP number fac5 = vl_sin_phase4(p5 + inner.x * 0.15915494).x;
    MY_HIGHP_OR_MEDIUMP number fac = 0.8 + 0.9*outer.x;
    MY_HIGHP_OR_MEDIUMP vec3 f234 = 0.5 + 0.5*outer.yzw;
    MY_HIGHP_OR_MEDIUMP number maxfac = 0.6*max(max(fac, max(f234.x, max(f234.y,0.0))) + (fac+f234.x+f234.y*f234.z), 0.);
    tex.rgb = tex.rgb*0.5 + vec3(0.4, 0.4, 0.8);
    tex.rgb = tex.rgb - delta + delta*maxfac*vec3(0.7 + fac5*0.07, 0.7 - fac5*0.17, 0.7) - 0.1;
    tex.a = tex.a*(0.8*max(min(1., max(0.,0.3*max(low*0.2, delta)+ min(max(maxfac*0.1,0.), 0.4)) ), 0.) + 0.15*maxfac*(0.1+delta));
    return dissolve_mask(tex*colour, texture_coords, p5.zw);
}
#endif"""


# holo.fs and polychrome.fs: the swirl field's time-only offsets come from the vertex stage, the
# sprite-pixel coordinates (for floor()) and every affine sin/cos phase too.
def swirl_effect(name, scale, tmul, extra_vary, extra_pixel, body):
    return """#ifdef VERTEX
void vl_varyings(vec2 texture_coords, out vec4 v0, out vec4 v1, out vec4 v2)
{
    MY_HIGHP_OR_MEDIUMP vec2 uv = (((texture_coords)*(image_details)) - texture_details.xy*texture_details.ba)/texture_details.ba;
    MY_HIGHP_OR_MEDIUMP number t = NAME.y*TMUL + time;
    v0 = vec4(((texture_coords)*(image_details)) - texture_details.xy*texture_details.ba, 0., 0.);
    v1 = vec4(50.*vec2(sin(-t / 143.6340), cos(-t / 99.4324)), 50.*vec2(cos( t / 53.1532),  cos( t / 61.4532)));
    v2 = vec4(50.*vec2(sin(-t / 87.53218), sin(-t / 49.0000)), 0., 0.);
EXTRA_VARY}
#endif
#ifdef PIXEL
""" .replace('NAME', name).replace('TMUL', tmul).replace('EXTRA_VARY', extra_vary) + SIN_PHASE4 + """vec4 vl_effect(vec4 colour, Image texture, vec2 texture_coords, vec4 v0, vec4 v1, vec4 v2)
{
    MY_HIGHP_OR_MEDIUMP vec4 tex = Texel(texture, texture_coords);
    MY_HIGHP_OR_MEDIUMP vec2 uv = v0.xy / texture_details.ba;
    MY_HIGHP_OR_MEDIUMP vec2 floored_uv = floor(v0.xy)/texture_details.ba;
    MY_HIGHP_OR_MEDIUMP vec2 uv_scaled_centered = (floored_uv - 0.5) * SCALE;
    MY_HIGHP_OR_MEDIUMP vec2 field_part1 = uv_scaled_centered + v1.xy;
    MY_HIGHP_OR_MEDIUMP vec2 field_part2 = uv_scaled_centered + v1.zw;
    MY_HIGHP_OR_MEDIUMP vec2 field_part3 = uv_scaled_centered + v2.xy;
    // cos(|p1|/19.483), sin(|p2|/33.155), cos(p2.y/15.73), cos(|p3|/27.193), as phases
    MY_HIGHP_OR_MEDIUMP vec4 w = vl_sin_phase4(vec4(length(field_part1) / 19.483 + 1.5707963, length(field_part2) / 33.155,
        field_part2.y / 15.73 + 1.5707963, length(field_part3) / 27.193 + 1.5707963) * 0.15915494 + 0.5);
EXTRA_PIXEL    MY_HIGHP_OR_MEDIUMP float field = (1.+ (w.x + w.y * w.z + w.w * g.w))/2.;
    MY_HIGHP_OR_MEDIUMP float res = (.5 + .5* vl_sin_phase4(vec4(((NAME.x) * 2.612 + ( field + -.5 ) *3.14 + 1.5707963) * 0.15915494 + 0.5)).x);
BODY
    tex.a = mix(tex.a, tex.a/3., 1. - step(0.7, tex.a));
    return dissolve_mask(tex*colour, texture_coords, uv);
}
#endif""".replace('NAME', name).replace('SCALE', scale).replace('EXTRA_PIXEL', extra_pixel).replace('BODY', body)


HOLO = swirl_effect('holo', '250.', '7.221',
    """    MY_HIGHP_OR_MEDIUMP number gridsize = 0.79;
    // cos(uv.x*g*20), cos(uv.y*g*45 + uv.x*g*20), cos(uv.y*g*45 - uv.x*g*20) as phases
    v0.zw = (vec2(uv.x*gridsize*20., uv.y*gridsize*45. + uv.x*gridsize*20.) + 1.5707963) * 0.15915494 + 0.5;
    v2.z = (uv.y*gridsize*45. - uv.x*gridsize*20. + 1.5707963) * 0.15915494 + 0.5;
""",
    """    // the three grid cosines, sin(p3.x/21.92)
    MY_HIGHP_OR_MEDIUMP vec4 g = vl_sin_phase4(vec4(v0.zw, v2.z, field_part3.x / 21.92 * 0.15915494 + 0.5));
""",
    """    MY_HIGHP_OR_MEDIUMP vec4 hsl = HSL(0.5*tex + 0.5*vec4(0.,0.,1.,tex.a));
    MY_HIGHP_OR_MEDIUMP number low = min(tex.r, min(tex.g, tex.b));
    MY_HIGHP_OR_MEDIUMP number high = max(tex.r, max(tex.g, tex.b));
    MY_HIGHP_OR_MEDIUMP number delta = 0.2+0.3*(high- low) + 0.1*high;
    MY_HIGHP_OR_MEDIUMP number fac = 0.5*max(max(max(0., 7.*abs(g.x)-6.),max(0., 7.*g.y-6.)), max(0., 7.*g.z-6.));
    hsl.x = hsl.x + res + fac;
    hsl.y = hsl.y*1.3;
    hsl.z = hsl.z*0.6+0.4;
    tex =(1.-delta)*tex + delta*RGB(hsl)*vec4(0.9,0.8,1.2,tex.a);""")

POLYCHROME = swirl_effect('polychrome', '50.', '2.221', '',
    """    MY_HIGHP_OR_MEDIUMP vec4 g = vec4(0., 0., 0., vl_sin_phase4(vec4(field_part3.x / 21.92 * 0.15915494 + 0.5)).x);
""",
    """    MY_HIGHP_OR_MEDIUMP number low = min(tex.r, min(tex.g, tex.b));
    MY_HIGHP_OR_MEDIUMP number high = max(tex.r, max(tex.g, tex.b));
    MY_HIGHP_OR_MEDIUMP number delta = high - low;
    MY_HIGHP_OR_MEDIUMP number saturation_fac = 1. - max(0., 0.05*(1.1-delta));
    MY_HIGHP_OR_MEDIUMP vec4 hsl = HSL(vec4(tex.r*saturation_fac, tex.g*saturation_fac, tex.b, tex.a));
    hsl.x = hsl.x+ res + polychrome.y*0.04;
    hsl.y = min(0.6,hsl.y+0.5);
    tex.rgb = RGB(hsl).rgb;""")

# hologram.fs: the glow (81 texture reads) comes baked per sprite (vl_hologram_glow.fs) from
# vl_Aux2, the glitch offsets' eight sines have affine phases (vertex stage), and the branches are
# step() selects.
HOLOGRAM = """#ifdef VERTEX
void vl_varyings(vec2 texture_coords, out vec4 v0, out vec4 v1, out vec4 v2)
{
    MY_HIGHP_OR_MEDIUMP number timefac = 1.0*hologram.g;
    v0 = vec4(timefac*0.512 + texture_coords.y*14.0, -timefac*0.8233 + texture_coords.y*11.532,
              timefac*0.333 + texture_coords.y*13.3, -timefac*0.1112331 + texture_coords.y*4.044343) * 0.15915494 + 0.5;
    v1 = vec4(timefac*0.6924 + texture_coords.y*19.0, -timefac*0.9661 + texture_coords.y*21.532,
              timefac*0.4423 + texture_coords.y*30.3, -timefac*0.13321312 + texture_coords.y*3.011) * 0.15915494 + 0.5;
    v2 = vec4(0.4*(0.3*sin(2.*hologram.g) + 0.6 + 0.3*sin(hologram.r*3.) + 0.9), 0., 0., 0.);
}
#endif
#ifdef PIXEL
extern MY_HIGHP_OR_MEDIUMP vec4 vl_glow_rect;
uniform Image vl_Aux2;
""" + SIN_PHASE4 + """vec4 vl_effect(vec4 colour, Image texture, vec2 texture_coords, vec4 v0, vec4 v1, vec4 v2)
{
    MY_HIGHP_OR_MEDIUMP number glow = Texel(vl_Aux2, (texture_coords - vl_glow_rect.xy) / vl_glow_rect.zw).r;
    MY_HIGHP_OR_MEDIUMP vec4 sl = vl_sin_phase4(v0);
    MY_HIGHP_OR_MEDIUMP vec4 sr = vl_sin_phase4(v1);
    MY_HIGHP_OR_MEDIUMP number offset_l = -10.0*(-0.5 + sl.x + sl.y + sl.z + sl.w);
    MY_HIGHP_OR_MEDIUMP number offset_r = -10.0*(-0.5 + sr.x + sr.y + sr.z + sr.w);
    offset_r = offset_r * (1. - step(1.5, offset_r)) * (1. - step(offset_r, 0.));
    offset_l = offset_l * (1. - step(1.5, offset_l)) * (1. - step(offset_l, 0.));
    texture_coords.x = texture_coords.x + 0.002*(-offset_l + offset_r);
    MY_HIGHP_OR_MEDIUMP vec4 tex = Texel( texture, texture_coords);
    tex = tex * step(tex.a, 0.999);
    MY_HIGHP_OR_MEDIUMP number clr = 1. - step(0.001, tex.a);
    tex.rgb = mix(tex.rgb, vec3(0.,1.,1.), clr);
    MY_HIGHP_OR_MEDIUMP vec2 uv = (((texture_coords)*(image_details)) - texture_details.xy*texture_details.ba)/texture_details.ba;
    MY_HIGHP_OR_MEDIUMP number inside = step(uv.x, 0.95) * step(0.05, uv.x) * step(uv.y, 0.95) * step(0.05, uv.y);
    MY_HIGHP_OR_MEDIUMP number light_strength = v2.x;
    MY_HIGHP_OR_MEDIUMP vec4 final_col = tex*colour + mix(vec4(0., 0.3, 0.2,0.3), vec4(0., 1., .5,0.6), clr)*light_strength*(1.+abs(offset_l)+abs(offset_r))*glow;
    return dissolve_mask(final_col, texture_coords, uv) * inside;
}
#endif"""

# negative_shine.fs: the same shine with its own constants
NEG_SHINE = PACK_SHINE
for a, b in [('13.*uv.x+5.32*uv.y', '11.*uv.x+4.32*uv.y'), ('10.*uv.x+2.32*uv.y + r*5.', '8.*uv.x+2.32*uv.y + r*5.'),
             ('12.*uv.x+6.32*uv.y', '10.*uv.x+5.32*uv.y'), ('4.*uv.x+2.32*uv.y', '3.*uv.x+2.32*uv.y'),
             ('r*1.3 + uv.y*13.2', 'r*1.3 + uv.y*11.2'), ('0.5*16.*uv.x', '0.9*16.*uv.x'), ('0.6*max(', '0.7*max('),
             ('vec3(0.7 + fac5*0.07, 0.7 - fac5*0.17, 0.7)', 'vec3(0.7 + fac5*0.27, 0.7 - fac5*0.27, 0.7)'),
             ('tex.a*(0.8*max(', 'tex.a*(0.5*max(')]:
    assert NEG_SHINE.count(a) == 1, a
    NEG_SHINE = NEG_SHINE.replace(a, b)

# foil.fs: |adjusted_uv| once (the original takes three lengths of its multiples), the time-only
# terms and the affine sine phases from the vertex stage, the five sines in two rounds.
FOIL = """#ifdef VERTEX
void vl_varyings(vec2 texture_coords, out vec4 v0, out vec4 v1, out vec4 v2)
{
    MY_HIGHP_OR_MEDIUMP vec2 uv = (((texture_coords)*(image_details)) - texture_details.xy*texture_details.ba)/texture_details.ba;
    MY_HIGHP_OR_MEDIUMP vec2 adjusted_uv = uv - vec2(0.5, 0.5);
    adjusted_uv.x = adjusted_uv.x*texture_details.b/texture_details.a;
    MY_HIGHP_OR_MEDIUMP vec2 rotater = vec2(cos(foil.r*0.1221), sin(foil.r*0.3512));
    // sin(foil.r*5. + uv.x*3. + 3.*(1.+0.5*cos(foil.r*7.))), sin(foil.r*6.66 + uv.y*3.8 + 3.*(1.+0.5*cos(foil.r*3.414))) as phases
    v0 = vec4(adjusted_uv, vec2(foil.r*5. + uv.x*3. + 3.*(1.+0.5*cos(foil.r*7.)), foil.r*6.66 + uv.y*3.8 + 3.*(1.+0.5*cos(foil.r*3.414))) * 0.15915494 + 0.5);
    v1 = vec4(rotater / length(rotater), 3.14*(2.2+0.9*sin(foil.r*1.65 + 0.2*foil.g)), 0.);
    v2 = vec4(uv, 0., 0.);
}
#endif
#ifdef PIXEL
""" + SIN_PHASE4 + """vec4 vl_effect(vec4 colour, Image texture, vec2 texture_coords, vec4 v0, vec4 v1, vec4 v2)
{
    MY_HIGHP_OR_MEDIUMP vec4 tex = Texel( texture, texture_coords);
    MY_HIGHP_OR_MEDIUMP vec2 adjusted_uv = v0.xy;
    MY_HIGHP_OR_MEDIUMP number L = length(adjusted_uv);
    MY_HIGHP_OR_MEDIUMP number low = min(tex.r, min(tex.g, tex.b));
    MY_HIGHP_OR_MEDIUMP number high = max(tex.r, max(tex.g, tex.b));
    MY_HIGHP_OR_MEDIUMP number delta = min(high, max(0.5, 1. - low));
    MY_HIGHP_OR_MEDIUMP number angle = dot(v1.xy, adjusted_uv)/L;
    // cos(|113.1121 uv| - foil.r*3.121), cos(foil.g*0.3 + angle*...), and the two affine sines
    MY_HIGHP_OR_MEDIUMP vec4 w = vl_sin_phase4(vec4(vec2(113.1121*L - foil.r*3.121, foil.g*0.3 + angle*v1.z) * 0.15915494 + 0.25 + 0.5, v0.zw));
    MY_HIGHP_OR_MEDIUMP number s = vl_sin_phase4(vec4(((90.*L + foil.r*2.) + 3.*(1.+0.8*w.x)) * 0.15915494 + 0.5)).x;
    MY_HIGHP_OR_MEDIUMP number fac = max(min(2.*s - 1. - max(5.-90.*L, 0.), 1.), 0.);
    MY_HIGHP_OR_MEDIUMP number fac2 = max(min(5.*w.y - 4. - max(2.-20.*L, 0.), 1.), 0.);
    MY_HIGHP_OR_MEDIUMP number fac3 = 0.3*max(min(2.*w.z - 1., 1.), -1.);
    MY_HIGHP_OR_MEDIUMP number fac4 = 0.3*max(min(2.*w.w - 1., 1.), -1.);

    MY_HIGHP_OR_MEDIUMP number maxfac = max(max(fac, max(fac2, max(fac3, max(fac4, 0.0)))) + 2.2*(fac+fac2+fac3+fac4), 0.);

    tex.r = tex.r-delta + delta*maxfac*0.3;
    tex.g = tex.g-delta + delta*maxfac*0.3;
    tex.b = tex.b + delta*maxfac*1.9;
    tex.a = min(tex.a, 0.3*tex.a + 0.9*min(0.5, maxfac*0.1));

    return dissolve_mask(tex, texture_coords, v2.xy);
}
#endif"""

FAST_EFFECTS = {
    'voucher': PACK_SHINE.replace('{U}', 'voucher').replace('{DELTA}', 'high-low'),
    'booster': PACK_SHINE.replace('{U}', 'booster').replace('{DELTA}', 'max(high-low, low*0.7)'),
    'holo': HOLO,
    'polychrome': POLYCHROME,
    'hologram': HOLOGRAM,
    'negative_shine': NEG_SHINE.replace('{U}', 'negative_shine').replace('{DELTA}', 'high-low -0.1'),
    'foil': FOIL,
}


# Balatro's HSL()/RGB() helpers (holo, polychrome, negative...) without branches, so the shaders
# run in the GPU's parallel mode. Same results: hue()'s four cases are clamp(min(hs, 4 - hs), 0, 1),
# and the delta == 0 / tiny saturation cases are step() selects (?: compiles to per-instance code).
BRANCHLESS_HSL = """vec4 HSL(vec4 c)
{
	MY_HIGHP_OR_MEDIUMP number low = min(c.r, min(c.g, c.b));
	MY_HIGHP_OR_MEDIUMP number high = max(c.r, max(c.g, c.b));
	MY_HIGHP_OR_MEDIUMP number delta = high - low;
	MY_HIGHP_OR_MEDIUMP number sum = high+low;
	MY_HIGHP_OR_MEDIUMP number d = max(delta, 1e-20);
	MY_HIGHP_OR_MEDIUMP number l = .5 * sum;
	MY_HIGHP_OR_MEDIUMP number sat = delta / max(mix(2.0 - sum, sum, 1. - step(.5, l)), 1e-6);
	MY_HIGHP_OR_MEDIUMP number isr = step(c.r, high) * step(high, c.r);
	MY_HIGHP_OR_MEDIUMP number isg = (1. - isr) * step(c.g, high) * step(high, c.g);
	MY_HIGHP_OR_MEDIUMP number h = (isr * (c.g - c.b) + isg * (c.b - c.r + 2.0 * d) + (1. - isr - isg) * (c.r - c.g + 4.0 * d)) / d;
	h = mod(h / 6., 1.);
	MY_HIGHP_OR_MEDIUMP number nz = 1. - step(delta, 0.);
	return vec4(h * nz, sat * nz, l, c.a);
}"""
BRANCHLESS_RGB = """vec4 RGB(vec4 c)
{
	MY_HIGHP_OR_MEDIUMP number t = mix(-c.y*c.z + (c.y+c.z), c.y*c.z + c.z, 1. - step(.5, c.z));
	MY_HIGHP_OR_MEDIUMP number s = 2.0 * c.z - t;
	MY_HIGHP_OR_MEDIUMP vec3 hs = fract(vec3(c.x + 1./3., c.x, c.x - 1./3.)) * 6.;
	MY_HIGHP_OR_MEDIUMP vec3 rgb = s + (t - s) * clamp(min(hs, 4. - hs), 0., 1.);
	return mix(vec4(rgb, c.w), vec4(vec3(c.z), c.a), 1. - step(0.0001, c.y));
}"""


def branchless_hsl(src):
    if 'vec4 HSL(' in src:
        src = replace_function(src, 'vec4 HSL(', BRANCHLESS_HSL)
    if 'vec4 RGB(' in src:
        src = replace_function(src, 'vec4 RGB(', BRANCHLESS_RGB)
    return src


def replace_function(src, signature, new):
    a = src.index(signature)
    i = src.index('{', a)
    depth = 0
    for b in range(i, len(src)):
        depth += {'{': 1, '}': -1}.get(src[b], 0)
        if depth == 0:
            break
    return src[:a] + new + src[b + 1:]


def dissolve_zero(src):
    a = src.index('vec4 dissolve_mask(')
    i = src.index('{', a)
    depth = 0
    for b in range(i, len(src)):
        depth += {'{': 1, '}': -1}.get(src[b], 0)
        if depth == 0:
            break
    return src[:i] + '''{
    return vec4(shadow ? vec3(0.,0.,0.) : tex.xyz, shadow ? tex.a*0.3: tex.a);
}''' + src[b + 1:]


# Edition shaders whose sin/cos are swapped for vl_fsin/vl_fcos (|error| < 0.001).
FAST_TRIG_SHADERS = []
FAST_TRIG_FUNCS = """
number vl_fsin(number x) { number t = fract(x * 0.15915494 + 0.5) * 2.0 - 1.0; number y = 4.0 * t * (1.0 - abs(t)); return 0.225 * (y * abs(y) - y) + y; }
number vl_fcos(number x) { return vl_fsin(x + 1.5707963); }
vec4 vl_fsin4(vec4 x) { vec4 t = fract(x * 0.15915494 + 0.5) * 2.0 - 1.0; vec4 y = 4.0 * t * (1.0 - abs(t)); return 0.225 * (y * abs(y) - y) + y; }
"""


def fast_trig(src):
    import re
    src = re.sub(r'\bsin\s*\(', 'vl_fsin(', src)
    src = re.sub(r'\bcos\s*\(', 'vl_fcos(', src)
    end = src.find('#endif') + len('#endif')  # after LOVE's precision prelude
    return src[:end] + FAST_TRIG_FUNCS + src[end:]


def build_vita_shaders(src, out):
    sdk = os.environ.get('SCE_PSP2_SDK_DIR', r'C:\PSVITA\sdk')
    cgc = Path(sdk) / 'host_tools' / 'bin' / 'psp2cgc.exe'
    if not cgc.exists():
        sys.exit(f'psp2cgc not found at {cgc}')
    cg_dir = out.parent.parent / 'shaders_cg'  # outside the asset dir (not packaged)
    shutil.rmtree(cg_dir, ignore_errors=True)
    sys.argv = ['shader_to_cg', str(src), str(cg_dir)]
    shader_to_cg.main()
    out.mkdir(parents=True, exist_ok=True)
    for cg in sorted(cg_dir.glob('*.cg')):
        profile = 'sce_vp_psp2' if cg.stem.endswith('_v') else 'sce_fp_psp2'
        gxp = out / (cg.stem + '.gxp')
        r = subprocess.run([str(cgc), '-profile', profile, '-O4', str(cg), '-o', str(gxp)],
                           capture_output=True, text=True)
        if r.returncode != 0 or not gxp.exists():
            sys.exit(f'{cg.name}: {r.stdout}{r.stderr}')


def wsl_path(p):
    p = str(Path(p).resolve())
    return '/mnt/' + p[0].lower() + p[2:].replace('\\', '/')


def run_wsl(args):
    r = subprocess.run(['wsl', '-d', 'Ubuntu', '--'] + args, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f'{args[0]} failed:\n{r.stdout}{r.stderr}')


def main():
    game, out, target = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3]
    res = game / 'resources'
    copy_tree(res / 'textures' / '1x', out / 'textures' / '1x', '**/*.png')
    copy_tree(res / 'sounds', out / 'sounds', '*.ogg')
    # love.filesystem.getDirectoryItems("resources/sounds") at runtime: the list ships as a file
    (out / 'sounds' / 'index.txt').write_text(
        ''.join(p.stem + '\n' for p in sorted((res / 'sounds').glob('*.ogg'))), encoding='utf-8', newline='\n')
    shaders = gather_shaders(res / 'shaders', out.parent / ('shaders_src_' + target))
    if target == 'vita':
        build_vita_shaders(shaders, out / 'shaders')
    else:
        copy_tree(shaders, out / 'shaders', '*.fs')
    # game data tables (prototypes, localization...) and fonts rasterized like LOVE does
    (out / 'data').mkdir(parents=True, exist_ok=True)
    run_wsl(['luajit', wsl_path(HERE / 'extract_data.lua'), wsl_path(game), wsl_path(out / 'data')])
    run_wsl(['python3', wsl_path(HERE / 'bake_fonts.py'), wsl_path(game), wsl_path(out / 'data'), wsl_path(out / 'fonts')])
    # the game draws with the baked fonts; only the M0 diagnostics overlay still rasterizes a TTF
    # (and stale TTFs from older builds would otherwise end up in the VPK)
    for ttf in (out / 'fonts').glob('*.ttf'):
        ttf.unlink()
    shutil.copy2(res / 'fonts' / 'm6x11plus.ttf', out / 'fonts' / 'm6x11plus.ttf')
    print(f'assets ready in {out}')


if __name__ == '__main__':
    main()
