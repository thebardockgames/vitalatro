#!/usr/bin/env python3
"""Offline study of Balatro's background.fs on a 960x544 target: exact output vs. computing the
expensive paint field at reduced resolution and colouring every pixel block afterwards.
Writes PNGs and prints the fraction of pixels that change."""
import sys
import numpy as np
from PIL import Image

W, H = 960, 544
PIXEL_SIZE_FAC = 700.0
SPIN_EASE = 0.5


def field(sx, sy, time, spin_time, spin_amount):
    """length(uv) after the paint loop, for screen coords (block-floored) sx, sy."""
    diag = np.hypot(W, H)
    uv_x = (sx - 0.5 * W) / diag - 0.12
    uv_y = (sy - 0.5 * H) / diag
    uv_len = np.hypot(uv_x, uv_y)
    speed = spin_time * SPIN_EASE * 0.2 + 302.2
    ang = np.arctan2(uv_y, uv_x) + speed - SPIN_EASE * 20.0 * (spin_amount * uv_len + (1.0 - spin_amount))
    mid_x, mid_y = (W / diag) / 2.0, (H / diag) / 2.0
    ux = uv_len * np.cos(ang) + mid_x - mid_x
    uy = uv_len * np.sin(ang) + mid_y - mid_y
    ux *= 30.0
    uy *= 30.0
    speed = time * 2.0
    u2x = ux + uy
    u2y = ux + uy
    for _ in range(5):
        m = np.sin(np.maximum(ux, uy))
        u2x = u2x + m + ux
        u2y = u2y + m + uy
        ux = ux + 0.5 * np.cos(5.1123314 + 0.353 * u2y + speed * 0.131121)
        uy = uy + 0.5 * np.sin(u2x - 0.113 * speed)
        t = 1.0 * np.cos(ux + uy) - 1.0 * np.sin(ux * 0.711 - uy)
        ux = ux - t
        uy = uy - t
    return np.hypot(ux, uy)


def colourise(L, contrast, spin_amount, c1, c2, c3):
    contrast_mod = 0.25 * contrast + 0.5 * spin_amount + 1.2
    paint = np.clip(L * 0.035 * contrast_mod, 0.0, 2.0)
    c1p = np.maximum(0.0, 1.0 - contrast_mod * np.abs(1.0 - paint))
    c2p = np.maximum(0.0, 1.0 - contrast_mod * np.abs(paint))
    c3p = 1.0 - np.minimum(1.0, c1p + c2p)
    k = 0.3 / contrast
    out = np.zeros(L.shape + (3,))
    for ch in range(3):
        out[..., ch] = k * c1[ch] + (1 - k) * (c1[ch] * c1p + c2[ch] * c2p + c3[ch] * c3p)
    return np.clip(out, 0, 1)


def blocks():
    px = np.hypot(W, H) / PIXEL_SIZE_FAC
    ys, xs = np.mgrid[0:H, 0:W] + 0.5
    bx = np.floor(xs / px) * px
    by = np.floor(ys / px) * px
    return px, bx, by


def main():
    out_dir = sys.argv[1]
    time, spin_time, spin_amount = 123.4, 0.0, 0.0
    contrast = 1.0
    # G.C.BACKGROUND for the default (red) small blind palette
    c1 = np.array([0xFE, 0x5F, 0x55]) / 255.0
    c2 = np.array([0x37, 0x42, 0x44]) / 255.0
    c3 = np.array([0x2a, 0x30, 0x3a]) / 255.0
    px, bx, by = blocks()
    exact = colourise(field(bx, by, time, spin_time, spin_amount), contrast, spin_amount, c1, c2, c3)
    Image.fromarray((exact * 255).astype(np.uint8)).save(f'{out_dir}/bg_exact.png')

    # reduced field: sample the block-centre grid every k blocks and bilinearly interpolate L
    nbx, nby = int(np.ceil(W / px)), int(np.ceil(H / px))
    for k in (2, 3, 4):
        gx = np.arange(0, nbx + k, k) * px
        gy = np.arange(0, nby + k, k) * px
        GX, GY = np.meshgrid(gx, gy)
        Lg = field(GX, GY, time, spin_time, spin_amount)
        fx = bx / (k * px)
        fy = by / (k * px)
        x0 = np.floor(fx).astype(int); y0 = np.floor(fy).astype(int)
        tx = fx - x0; ty = fy - y0
        L = (Lg[y0, x0] * (1 - tx) * (1 - ty) + Lg[y0, x0 + 1] * tx * (1 - ty) +
             Lg[y0 + 1, x0] * (1 - tx) * ty + Lg[y0 + 1, x0 + 1] * tx * ty)
        approx = colourise(L, contrast, spin_amount, c1, c2, c3)
        diff = np.abs(approx - exact).max(axis=2)
        Image.fromarray((approx * 255).astype(np.uint8)).save(f'{out_dir}/bg_approx_k{k}.png')
        print(f'k={k}: samples={Lg.size:6d}  mean|d|={diff.mean()*255:.2f}/255  '
              f'pixels >8/255: {100*(diff > 8/255).mean():.2f}%  >32/255: {100*(diff > 32/255).mean():.2f}%')


if __name__ == '__main__':
    main()
