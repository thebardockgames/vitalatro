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
    return tmp


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


def main():
    game, out, target = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3]
    res = game / 'resources'
    copy_tree(res / 'textures' / '1x', out / 'textures' / '1x', '**/*.png')
    copy_tree(res / 'fonts', out / 'fonts', '*.ttf')
    copy_tree(res / 'sounds', out / 'sounds', '*.ogg')
    shaders = gather_shaders(res / 'shaders', out.parent / ('shaders_src_' + target))
    if target == 'vita':
        build_vita_shaders(shaders, out / 'shaders')
    else:
        copy_tree(shaders, out / 'shaders', '*.fs')
    print(f'assets ready in {out}')


if __name__ == '__main__':
    main()
