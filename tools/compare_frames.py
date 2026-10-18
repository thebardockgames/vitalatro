"""Compare Vitalatro frames with reference frames from the original game.

    python3 compare_frames.py <dir> <frame> [<frame>...]

Reads <dir>/ref_<frame>.png and <dir>/s_<frame>.png, prints the mean/max absolute difference and
the share of differing pixels, and writes <dir>/cmp_<frame>.png: reference | ours | diff x4.
"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image


def main():
    d = Path(sys.argv[1])
    for f in sys.argv[2:]:
        ref = np.asarray(Image.open(d / f'ref_{f}.png').convert('RGB')).astype(np.int16)
        ours = np.asarray(Image.open(d / f's_{f}.png').convert('RGB')).astype(np.int16)
        diff = np.abs(ref - ours)
        px = diff.max(axis=2)
        print(f'frame {f}: mean {diff.mean():.2f}  max {diff.max()}  >8: {100 * (px > 8).mean():.1f}%  >32: {100 * (px > 32).mean():.1f}%')
        vis = np.clip(diff * 4, 0, 255).astype(np.uint8)
        out = np.concatenate([ref.astype(np.uint8), ours.astype(np.uint8), vis], axis=1)
        Image.fromarray(out).save(d / f'cmp_{f}.png')


if __name__ == '__main__':
    main()
