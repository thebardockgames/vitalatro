#!/usr/bin/env python3
"""Convert Balatro's LOVE GLSL shaders (*.fs) to Cg for the PS Vita (psp2cgc).

Each .fs is written to compile as both stages, like LOVE does: we emit <name>_f.cg (pixel
stage, effect()) and, if it defines position(), <name>_v.cg (vertex stage).

The conversion is purely syntactic; semantics that differ between GLSL and Cg are mapped
explicitly (mod -> floored modulo, vecN(scalar) broadcast, matrix * vector, bool uniforms).
"""
import re
import sys
from pathlib import Path

TYPES = {'vec2': 'float2', 'vec3': 'float3', 'vec4': 'float4', 'mat2': 'float2x2',
         'mat3': 'float3x3', 'mat4': 'float4x4', 'number': 'float', 'ivec2': 'int2',
         'ivec3': 'int3', 'ivec4': 'int4', 'bvec2': 'bool2', 'bvec3': 'bool3', 'bvec4': 'bool4'}
FUNCS = {'mix': 'lerp', 'fract': 'frac', 'inversesqrt': 'rsqrt', 'mod': 'vl_mod',
         'Texel': 'vl_texel', 'texture2D': 'vl_texel', 'dFdx': 'ddx', 'dFdy': 'ddy'}
RESERVED = {'texture': 'tex_', 'sampler': 'sampler_', 'half': 'half_', 'fixed': 'fixed_'}

PRELUDE_COMMON = """
#define MY_HIGHP_OR_MEDIUMP
#define LOVE_HIGHP_OR_MEDIUMP
#define highp
#define mediump
#define lowp
uniform float4 love_ScreenSize;
float  vl_mod(float a, float b)   { return a - b * floor(a / b); }
float2 vl_mod(float2 a, float2 b) { return a - b * floor(a / b); }
float3 vl_mod(float3 a, float3 b) { return a - b * floor(a / b); }
float4 vl_mod(float4 a, float4 b) { return a - b * floor(a / b); }
float2 vl_mod(float2 a, float b)  { return a - b * floor(a / b); }
float3 vl_mod(float3 a, float b)  { return a - b * floor(a / b); }
float4 vl_mod(float4 a, float b)  { return a - b * floor(a / b); }
"""

PRELUDE_PIXEL = """
float4 vl_texel(sampler2D s, float2 uv) { return tex2D(s, uv); }
"""

PRELUDE_VERTEX = """
float4 vl_texel(sampler2D s, float2 uv) { return float4(0.0, 0.0, 0.0, 0.0); }
"""

PIXEL_MAIN = """
float4 main(float4 vTexCoord : TEXCOORD0, float4 vColor : TEXCOORD1, float2 vl_wpos : WPOS,
            uniform sampler2D MainTex) : COLOR
{
    return effect(vColor, MainTex, vTexCoord.xy, vl_wpos);
}
"""

VERTEX_MAIN = """
void main(float2 aPosition, float2 aTexCoord, float4 aColor, uniform float4x4 vl_Proj,
          float4 out vPosition : POSITION, float4 out vTexCoord : TEXCOORD0,
          float4 out vColor : TEXCOORD1)
{
    vTexCoord = float4(aTexCoord, 0.0, 0.0);
    vColor = aColor;
    vPosition = position(vl_Proj, float4(aPosition, 0.0, 1.0));
}
"""

DEFAULT_POSITION = """
float4 position(float4x4 m, float4 p) { return mul(p, m); }
"""


def split_args(s, start):
    """s[start] is '('; return (args, end_index_after_close_paren)."""
    depth, i, cur, args = 0, start, '', []
    while i < len(s):
        c = s[i]
        if c == '(':
            depth += 1
            if depth > 1:
                cur += c
        elif c == ')':
            depth -= 1
            if depth == 0:
                args.append(cur)
                return args, i + 1
            cur += c
        elif c == ',' and depth == 1:
            args.append(cur)
            cur = ''
        else:
            cur += c
        i += 1
    raise ValueError('unbalanced parens')


def fix_constructors(src):
    """GLSL vecN(scalar) broadcasts; Cg needs an explicit cast."""
    out, i = '', 0
    pat = re.compile(r'\b(float[234])\s*\(')
    while True:
        m = pat.search(src, i)
        if not m:
            out += src[i:]
            return out
        out += src[i:m.start()]
        args, end = split_args(src, m.end() - 1)
        args = [fix_constructors(a) for a in args]
        if len(args) == 1:
            out += '((%s)(%s))' % (m.group(1), args[0])
        else:
            out += '%s(%s)' % (m.group(1), ','.join(args))
        i = end


def fix_atan(src):
    out, i = '', 0
    pat = re.compile(r'\batan\s*\(')
    while True:
        m = pat.search(src, i)
        if not m:
            return out + src[i:]
        out += src[i:m.start()]
        args, end = split_args(src, m.end() - 1)
        args = [fix_atan(a) for a in args]
        out += ('atan2(%s)' if len(args) == 2 else 'atan(%s)') % ','.join(args)
        i = end


def convert(code, stage):
    code = code.replace('\r\n', '\n')
    # LOVE's precision prelude in every Balatro shader
    code = re.sub(r'#if defined\(VERTEX\) \|\| __VERSION__.*?#endif', '', code, flags=re.S)

    bool_uniforms = re.findall(r'extern\s+(?:\w+\s+)?bool\s+(\w+)\s*;', code)
    code = re.sub(r'\bextern\s+(?:MY_HIGHP_OR_MEDIUMP\s+)?bool\s+(\w+)\s*;', r'uniform float \1;', code)
    code = re.sub(r'\bextern\b', 'uniform', code)

    # stage selection, same as LOVE (#ifdef VERTEX / PIXEL)
    defines = '#define %s 1\n' % ('VERTEX' if stage == 'v' else 'PIXEL')

    for k, v in RESERVED.items():
        code = re.sub(r'\b%s\b' % k, v, code)
    for k, v in TYPES.items():
        code = re.sub(r'\b%s\b' % k, v, code)
    code = re.sub(r'\bImage\b', 'sampler2D', code)
    for k, v in FUNCS.items():
        code = re.sub(r'\b%s\s*\(' % k, v + '(', code)
    code = fix_atan(code)
    # matrix * vector in position()
    code = re.sub(r'\btransform_projection\s*\*\s*vertex_position\b',
                  'mul(vertex_position, transform_projection)', code)
    code = fix_constructors(code)
    for b in bool_uniforms:
        code = re.sub(r'(?<!uniform float )\b%s\b(?!\s*;)' % b, '(%s > 0.5)' % b, code)

    has_position = re.search(r'float4\s+position\s*\(', code) is not None
    if stage == 'v':
        return defines + PRELUDE_COMMON + PRELUDE_VERTEX + code + \
            ('' if has_position else DEFAULT_POSITION) + VERTEX_MAIN
    has_effect = re.search(r'float4\s+effect\s*\(', code) is not None
    # vertex-only shaders get LOVE's default effect()
    return defines + PRELUDE_COMMON + PRELUDE_PIXEL + code + \
        ('' if has_effect else DEFAULT_EFFECT) + PIXEL_MAIN


DEFAULT_EFFECT = """
float4 effect(float4 vcolor, sampler2D tex_, float2 texcoord, float2 pixcoord) {
    return vl_texel(tex_, texcoord) * vcolor;
}
"""
CLEAR_F = """
float4 main(uniform float4 vl_Clear) : COLOR { return vl_Clear; }
"""


def main():
    src_dir, out_dir = Path(sys.argv[1]), Path(sys.argv[2])
    out_dir.mkdir(parents=True, exist_ok=True)
    for fs in sorted(src_dir.glob('*.fs')):
        code = fs.read_text(encoding='utf-8')
        (out_dir / (fs.stem + '_f.cg')).write_text(convert(code, 'f'), encoding='utf-8')
        if re.search(r'vec4\s+position\s*\(', code):
            (out_dir / (fs.stem + '_v.cg')).write_text(convert(code, 'v'), encoding='utf-8')
    (out_dir / 'default_f.cg').write_text('#define PIXEL 1\n' + PRELUDE_COMMON + PRELUDE_PIXEL +
                                          DEFAULT_EFFECT + PIXEL_MAIN, encoding='utf-8')
    (out_dir / 'default_v.cg').write_text('#define VERTEX 1\n' + PRELUDE_COMMON + PRELUDE_VERTEX +
                                          DEFAULT_POSITION + VERTEX_MAIN, encoding='utf-8')
    (out_dir / 'clear_f.cg').write_text(CLEAR_F, encoding='utf-8')


if __name__ == '__main__':
    main()
