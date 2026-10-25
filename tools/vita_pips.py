#!/usr/bin/env python3
"""vita_pips.py <original gamepad_ui.png (1x)> <out png>: the button pips as a Vita shows them.

Balatro's PlayStation rows (1: PS3, 3: PS4) draw L1/L2 rounded on the right and R1/R2 on the
left, the way a right-hand button would look, and row 3 labels start/back OPTIONS and SHARE,
which a Vita does not have. Here the shoulder silhouettes are mirrored (their text is not) and
the labels become START and SELECT in the atlas's own 3x6 pixel font. Only the Vita build uses
this; the original file is never modified.
"""
import sys

from PIL import Image

CELL = 32
WHITE = (255, 255, 255, 255)
FILL = (79, 99, 103, 255)    # the shoulder buttons' face
BANNER = (57, 71, 74, 255)   # the label banners

# 3x6 glyphs as in the atlas's OPTIONS/SHARE labels
GLYPHS = {
    'S': ['.##', '#..', '.#.', '..#', '..#', '##.'],
    'T': ['###', '.#.', '.#.', '.#.', '.#.', '.#.'],
    'A': ['.#.', '#.#', '#.#', '###', '#.#', '#.#'],
    'R': ['##.', '#.#', '#.#', '##.', '#.#', '#.#'],
    'E': ['###', '#..', '##.', '#..', '#..', '###'],
    'L': ['#..', '#..', '#..', '#..', '#..', '###'],
    'C': ['.##', '#..', '#..', '#..', '#..', '.##'],
}


def mirror_shoulder(im, cx, cy):
    x0, y0 = cx * CELL, cy * CELL
    cell = im.crop((x0, y0, x0 + CELL, y0 + CELL))
    text = [(x, y) for y in range(CELL) for x in range(CELL) if cell.getpixel((x, y)) == WHITE]
    for p in text:
        cell.putpixel(p, FILL)
    cell = cell.transpose(Image.FLIP_LEFT_RIGHT)
    for p in text:
        cell.putpixel(p, WHITE)
    im.paste(cell, (x0, y0))


def relabel(im, cx, cy, word):
    x0, y0 = cx * CELL, cy * CELL
    # the label's text rows (2..7) on the banner, cleared, then the word centred
    for y in range(2, 8):
        for x in range(3, 29):
            if im.getpixel((x0 + x, y0 + y))[3] == 255:
                im.putpixel((x0 + x, y0 + y), BANNER)
    w = 4 * len(word) - 1
    left = (CELL - w) // 2
    for i, ch in enumerate(word):
        for row, bits in enumerate(GLYPHS[ch]):
            for col, b in enumerate(bits):
                if b == '#':
                    im.putpixel((x0 + left + 4 * i + col, y0 + 2 + row), WHITE)


def main():
    im = Image.open(sys.argv[1]).convert('RGBA')
    for row in (1, 3):
        for col in (4, 5, 6, 7):  # L1, R1, L2, R2
            mirror_shoulder(im, col, row)
    relabel(im, 8, 3, 'START')   # 'start' (OPTIONS on a PS4)
    relabel(im, 9, 3, 'SELECT')  # 'back' (SHARE on a PS4)
    im.save(sys.argv[2])


if __name__ == '__main__':
    main()
