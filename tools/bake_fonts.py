#!/usr/bin/env python3
"""Rasterize Balatro's fonts the way LOVE 11.5 does (love::font::freetype::TrueTypeRasterizer),
for the characters each font needs, into glyph atlases + metrics.

    bake_fonts.py <balatro_dir> <data_dir (extracted loc_*.lua)> <out_dir>

Per font id writes <id>.fnt (binary, see below) and <id>_<page>.png (8-bit alpha atlas pages).

.fnt layout (little endian):
    char[4] 'VLFN', u32 version=1
    i32 size, ascent, descent, height        (FT size metrics >> 6, like LOVE)
    u32 page_count, u32 page_w, u32 page_h
    u32 glyph_count
      glyph: u32 codepoint, u16 page, u16 x, u16 y, u16 w, u16 h,
             i16 bearing_x, i16 bearing_y, i16 advance
    u32 kerning_count
      kerning: u32 left, u32 right, i16 amount   (only non-zero pairs)
"""
import re
import struct
import sys
from pathlib import Path

import freetype
from PIL import Image

PAGE_W, PAGE_H = 2048, 2048
PAD = 2  # transparent border around each glyph (LOVE extrudes quads by 1 px into it)

# Font table from Game:set_language (file, LOVE size = floor(render_scale + 0.5)) and the
# languages that use each one.
FONTS = {
    'm6x11plus_200': ('m6x11plus.ttf', 200, ['en-us', 'de', 'es_419', 'es_ES', 'fr', 'id', 'it', 'nl', 'pl', 'pt_BR']),
    'NotoSansSC_47': ('NotoSansSC-Bold.ttf', 47, ['zh_CN']),
    'NotoSansTC_47': ('NotoSansTC-Bold.ttf', 47, ['zh_TW']),
    'NotoSansKR_140': ('NotoSansKR-Bold.ttf', 140, ['ko']),
    'NotoSansJP_140': ('NotoSansJP-Bold.ttf', 140, ['ja']),
    'NotoSans_140': ('NotoSans-Bold.ttf', 140, ['ru']),
    'GoNotoCurrent_200': ('GoNotoCurrent-Bold.ttf', 200, []),
    'GoNotoCJKCore_200': ('GoNotoCJKCore.ttf', 200, []),
}

# Language labels/buttons/warnings from Game:set_language, shown in each language's own font.
LANG_TEXT_FROM = 'game.lua'


def chars_of_lua_strings(text):
    out = set()
    for m in re.finditer(r'"((?:[^"\\]|\\.)*)"|\'((?:[^\'\\]|\\.)*)\'', text):
        s = m.group(1) if m.group(1) is not None else m.group(2)
        out.update(s)
    return out


def language_strings(game_lua):
    """chars of the LANGUAGES table entries, per language key."""
    per = {}
    for m in re.finditer(r"\['([\w-]+)'\]\s*=\s*\{font = \d+,(.*?)\}\},?\n", game_lua):
        per[m.group(1)] = chars_of_lua_strings(m.group(2))
    return per


def bake(font_path, size, chars, out_dir, font_id):
    face = freetype.Face(str(font_path))
    face.set_pixel_sizes(size, size)
    m = face.size
    ascent, descent, height = m.ascender >> 6, m.descender >> 6, m.height >> 6

    glyphs = []
    pages = [Image.new('L', (PAGE_W, PAGE_H), 0)]
    x, y, row_h = PAD, PAD, 0
    flags = freetype.FT_LOAD_DEFAULT | freetype.FT_LOAD_TARGET_NORMAL
    for cp in sorted(chars):
        face.load_glyph(face.get_char_index(cp), flags)
        glyph = face.glyph.get_glyph()
        bglyph = glyph.to_bitmap(freetype.FT_RENDER_MODE_NORMAL, freetype.Vector(0, 0), True)
        bm = bglyph.bitmap
        w, h = bm.width, bm.rows
        advance = face.glyph.advance.x >> 6  # == FT_Glyph advance (16.16) >> 16
        page, gx, gy = 0, 0, 0
        if w > 0 and h > 0:
            if x + w + PAD > PAGE_W:
                x, y, row_h = PAD, y + row_h + PAD, 0
            if y + h + PAD > PAGE_H:
                pages.append(Image.new('L', (PAGE_W, PAGE_H), 0))
                x, y, row_h = PAD, PAD, 0
            page, gx, gy = len(pages) - 1, x, y
            buf = bytes(bm.buffer)
            img = Image.frombytes('L', (w, h), bytes(b for r in range(h) for b in buf[r * bm.pitch:r * bm.pitch + w]))
            pages[page].paste(img, (gx, gy))
            x += w + PAD
            row_h = max(row_h, h)
        glyphs.append((cp, page, gx, gy, w, h, bglyph.left, bglyph.top, advance))

    kern = []
    if face.has_kerning and len(chars) < 1500:
        idx = {cp: face.get_char_index(cp) for cp in chars}
        for a in chars:
            for b in chars:
                k = face.get_kerning(idx[a], idx[b], freetype.FT_KERNING_DEFAULT).x >> 6
                if k:
                    kern.append((a, b, k))

    # trim the last page's unused height
    last_h = min(PAGE_H, y + row_h + PAD)
    if len(pages) == 1:
        pages[0] = pages[0].crop((0, 0, PAGE_W, max(last_h, 4)))
    page_h = pages[0].size[1]
    for i, p in enumerate(pages):
        if p.size[1] != page_h:
            p = p.crop((0, 0, PAGE_W, page_h))
        p.save(out_dir / f'{font_id}_{i}.png', optimize=True)

    with open(out_dir / f'{font_id}.fnt', 'wb') as f:
        f.write(b'VLFN' + struct.pack('<I', 1))
        f.write(struct.pack('<iiii', size, ascent, descent, height))
        f.write(struct.pack('<III', len(pages), PAGE_W, page_h))
        f.write(struct.pack('<I', len(glyphs)))
        for g in glyphs:
            f.write(struct.pack('<IHHHHHhhh', *g))
        f.write(struct.pack('<I', len(kern)))
        for k in kern:
            f.write(struct.pack('<IIh', *k))
    print(f'{font_id}: {len(glyphs)} glyphs, {len(pages)} page(s), {len(kern)} kerning pairs')


def main():
    game_dir, data_dir, out_dir = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    out_dir.mkdir(parents=True, exist_ok=True)
    game_lua = (game_dir / 'game.lua').read_text(encoding='utf-8')
    lang_text = language_strings(game_lua)
    # strings in the game's code (UI labels etc.) are rendered with the current language's font
    code_chars = set()
    for p in list(game_dir.glob('*.lua')) + list((game_dir / 'functions').glob('*.lua')) + list((game_dir / 'engine').glob('*.lua')):
        # (general punctuation such as the credits' em dash included: a glyph the font lacks bakes as its .notdef, as LOVE draws it)
        code_chars |= {c for c in chars_of_lua_strings(p.read_text(encoding='utf-8')) if ord(c) < 0x3000}
    ascii_set = {chr(c) for c in range(32, 127)}

    for font_id, (file, size, langs) in FONTS.items():
        chars = set(ascii_set) | code_chars
        for lang in langs:
            loc = (data_dir / f'loc_{lang}.lua').read_text(encoding='utf-8')
            chars |= chars_of_lua_strings(loc)
            chars |= lang_text.get(lang, set())
        if font_id == 'm6x11plus_200':
            # every language's label is listed in the language menu
            for t in lang_text.values():
                chars |= {c for c in t if ord(c) < 0x3000}
        chars = {ord(c) for c in chars if c not in '\n\r\t'}
        bake(game_dir / 'resources' / 'fonts' / file, size, chars, out_dir, font_id)


if __name__ == '__main__':
    main()
