// tools/bake_fonts.py in C++: Balatro's fonts rasterized the way LOVE 11.5 does
// (love::font::freetype::TrueTypeRasterizer) for the characters each font needs, into glyph
// atlases + metrics. Same FreeType version (2.14.2) and calls, same packing, same files.
//
// .fnt layout (little endian):
//     char[4] 'VLFN', u32 version=1
//     i32 size, ascent, descent, height        (FT size metrics >> 6, like LOVE)
//     u32 page_count, u32 page_w, u32 page_h
//     u32 glyph_count
//       glyph: u32 codepoint, u16 page, u16 x, u16 y, u16 w, u16 h,
//              i16 bearing_x, i16 bearing_y, i16 advance
//     u32 kerning_count
//       kerning: u32 left, u32 right, i16 amount   (only non-zero pairs)
#include "fonts.h"

#include <algorithm>
#include <cstring>
#include <set>
#include <stdexcept>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H

#include "png_io.h"

namespace vlab {

namespace {

const int PAGE_W = 2048, PAGE_H = 2048;
const int PAD = 2;  // transparent border around each glyph (LOVE extrudes quads by 1 px into it)

struct FontDef {
    const char *id, *file;
    int size;
    std::vector<const char *> langs;
};

// Font table from Game:set_language (file, LOVE size = floor(render_scale + 0.5)) and the
// languages that use each one.
const std::vector<FontDef> FONTS = {
    {"m6x11plus_200", "m6x11plus.ttf", 200, {"en-us", "de", "es_419", "es_ES", "fr", "id", "it", "nl", "pl", "pt_BR"}},
    {"NotoSansSC_47", "NotoSansSC-Bold.ttf", 47, {"zh_CN"}},
    {"NotoSansTC_47", "NotoSansTC-Bold.ttf", 47, {"zh_TW"}},
    {"NotoSansKR_140", "NotoSansKR-Bold.ttf", 140, {"ko"}},
    {"NotoSansJP_140", "NotoSansJP-Bold.ttf", 140, {"ja"}},
    {"NotoSans_140", "NotoSans-Bold.ttf", 140, {"ru"}},
    {"GoNotoCurrent_200", "GoNotoCurrent-Bold.ttf", 200, {}},
    {"GoNotoCJKCore_200", "GoNotoCJKCore.ttf", 200, {}},
};

// Python's read_text(): UTF-8 with universal newlines
std::u32string decode_text(const std::string &s) {
    std::u32string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        char32_t cp;
        int n;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c >> 5) == 6) { cp = c & 0x1f; n = 2; }
        else if ((c >> 4) == 14) { cp = c & 0x0f; n = 3; }
        else if ((c >> 3) == 30) { cp = c & 0x07; n = 4; }
        else throw std::runtime_error("invalid UTF-8");
        if (i + n > s.size()) throw std::runtime_error("invalid UTF-8");
        for (int k = 1; k < n; k++) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3f);
        i += n;
        if (cp == '\r') {
            if (i < s.size() && s[i] == '\n') i++;
            cp = '\n';
        }
        out.push_back(cp);
    }
    return out;
}

// chars_of_lua_strings: every character inside "..." / '...' as matched by
// r'"((?:[^"\\]|\\.)*)"|\'((?:[^\'\\]|\\.)*)\'' (no DOTALL: an escape can't take a newline)
void chars_of_lua_strings(const std::u32string &t, size_t a, size_t b, std::set<char32_t> &out) {
    size_t i = a;
    while (i < b) {
        char32_t q = t[i];
        if (q != '"' && q != '\'') { i++; continue; }
        size_t j = i + 1;
        bool ok = false;
        while (j < b) {
            char32_t c = t[j];
            if (c == q) { ok = true; break; }
            if (c == '\\') {
                if (j + 1 < b && t[j + 1] != '\n') { j += 2; continue; }
                break;
            }
            j++;
        }
        if (ok) {
            out.insert(t.begin() + i + 1, t.begin() + j);
            i = j + 1;
        } else {
            i++;
        }
    }
}

bool is_word(char32_t c) {  // [\w-] for the language keys (ASCII in practice)
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
           (c >= 0x80 && c != 0x2028 && c != 0x2029 && c != 0x85 && c != 0xa0);
}

bool is_space(char32_t c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

// language_strings: r"\['([\w-]+)'\]\s*=\s*\{font = \d+,(.*?)\}\},?\n" over game.lua
std::map<std::u32string, std::set<char32_t>> language_strings(const std::u32string &t) {
    std::map<std::u32string, std::set<char32_t>> per;
    size_t n = t.size(), i = 0;
    while (i < n) {
        size_t p = i;
        auto fail = [&] { i++; };
        if (!(p + 1 < n && t[p] == '[' && t[p + 1] == '\'')) { fail(); continue; }
        p += 2;
        size_t ks = p;
        while (p < n && is_word(t[p])) p++;
        if (p == ks || !(p + 1 < n && t[p] == '\'' && t[p + 1] == ']')) { fail(); continue; }
        std::u32string key = t.substr(ks, p - ks);
        p += 2;
        while (p < n && is_space(t[p])) p++;
        if (!(p < n && t[p] == '=')) { fail(); continue; }
        p++;
        while (p < n && is_space(t[p])) p++;
        static const std::u32string lit = U"{font = ";
        if (t.compare(p, lit.size(), lit) != 0) { fail(); continue; }
        p += lit.size();
        size_t ds = p;
        while (p < n && t[p] >= '0' && t[p] <= '9') p++;
        if (p == ds || !(p < n && t[p] == ',')) { fail(); continue; }
        p++;
        size_t gs = p, e = std::u32string::npos, end = 0;
        for (size_t q = p; q < n && t[q] != '\n'; q++) {  // lazy .*? within the line
            if (q + 1 < n && t[q] == '}' && t[q + 1] == '}') {
                size_t r = q + 2;
                if (r < n && t[r] == ',') r++;
                if (r < n && t[r] == '\n') { e = q; end = r + 1; break; }
                // ",?" may also match empty: "}}\n" handled above; "}},x" is no match here
            }
        }
        if (e == std::u32string::npos) { fail(); continue; }
        std::set<char32_t> chars;
        chars_of_lua_strings(t, gs, e, chars);
        per[key] = chars;
        i = end;
    }
    return per;
}

void le16(std::vector<uint8_t> &o, uint32_t v) { o.push_back(uint8_t(v)); o.push_back(uint8_t(v >> 8)); }
void le32(std::vector<uint8_t> &o, uint32_t v) { le16(o, v & 0xffff); le16(o, v >> 16); }

struct Glyph {
    uint32_t cp;
    uint16_t page, x, y, w, h;
    int16_t left, top, advance;
};

void bake(FT_Library lib, const Bytes &font_file, int size, const std::set<uint32_t> &chars, const std::string &font_id,
          std::vector<OutFile> &out) {
    FT_Face face;
    if (FT_New_Memory_Face(lib, font_file.data(), FT_Long(font_file.size()), 0, &face))
        throw std::runtime_error("FreeType cannot open " + font_id);
    FT_Set_Pixel_Sizes(face, FT_UInt(size), FT_UInt(size));
    const FT_Size_Metrics &m = face->size->metrics;
    int ascent = int(m.ascender >> 6), descent = int(m.descender >> 6), height = int(m.height >> 6);

    std::vector<Glyph> glyphs;
    std::vector<Image> pages;
    auto new_page = [&] {
        Image p;
        p.w = PAGE_W;
        p.h = PAGE_H;
        p.channels = 1;
        p.px.assign(size_t(PAGE_W) * PAGE_H, 0);
        pages.push_back(std::move(p));
    };
    new_page();
    int x = PAD, y = PAD, row_h = 0;
    for (uint32_t cp : chars) {  // sorted
        if (FT_Load_Glyph(face, FT_Get_Char_Index(face, cp), FT_LOAD_DEFAULT | FT_LOAD_TARGET_NORMAL))
            throw std::runtime_error("FT_Load_Glyph");
        FT_Glyph glyph;
        if (FT_Get_Glyph(face->glyph, &glyph)) throw std::runtime_error("FT_Get_Glyph");
        FT_Vector origin = {0, 0};
        if (FT_Glyph_To_Bitmap(&glyph, FT_RENDER_MODE_NORMAL, &origin, 1)) throw std::runtime_error("FT_Glyph_To_Bitmap");
        FT_BitmapGlyph bg = reinterpret_cast<FT_BitmapGlyph>(glyph);
        const FT_Bitmap &bm = bg->bitmap;
        int w = int(bm.width), h = int(bm.rows);
        int advance = int(face->glyph->advance.x >> 6);
        int page = 0, gx = 0, gy = 0;
        if (w > 0 && h > 0) {
            if (x + w + PAD > PAGE_W) { x = PAD; y = y + row_h + PAD; row_h = 0; }
            if (y + h + PAD > PAGE_H) { new_page(); x = PAD; y = PAD; row_h = 0; }
            page = int(pages.size()) - 1;
            gx = x;
            gy = y;
            Image &pg = pages[page];
            for (int r = 0; r < h; r++)
                std::memcpy(&pg.px[size_t(gy + r) * PAGE_W + gx], bm.buffer + ptrdiff_t(r) * bm.pitch, size_t(w));
            x += w + PAD;
            row_h = std::max(row_h, h);
        }
        glyphs.push_back({cp, uint16_t(page), uint16_t(gx), uint16_t(gy), uint16_t(w), uint16_t(h), int16_t(bg->left),
                          int16_t(bg->top), int16_t(advance)});
        FT_Done_Glyph(glyph);
    }

    // Kerning pairs. bake_fonts.py walks a Python set (its order is not reproducible); none of
    // Balatro's fonts has a 'kern' table, so this stays empty in practice.
    std::vector<std::tuple<uint32_t, uint32_t, int>> kern;
    if (FT_HAS_KERNING(face) && chars.size() < 1500) {
        for (uint32_t a : chars)
            for (uint32_t b : chars) {
                FT_Vector k;
                FT_Get_Kerning(face, FT_Get_Char_Index(face, a), FT_Get_Char_Index(face, b), FT_KERNING_DEFAULT, &k);
                int amount = int(k.x >> 6);
                if (amount) kern.emplace_back(a, b, amount);
            }
    }
    FT_Done_Face(face);

    // trim the last page's unused height
    int last_h = std::min(PAGE_H, y + row_h + PAD);
    if (pages.size() == 1) {
        pages[0].h = std::max(last_h, 4);
        pages[0].px.resize(size_t(PAGE_W) * pages[0].h);
    }
    int page_h = pages[0].h;
    for (size_t i = 0; i < pages.size(); i++) {
        Image &p = pages[i];
        if (p.h != page_h) { p.h = page_h; p.px.resize(size_t(PAGE_W) * page_h); }
        out.push_back({"fonts/" + font_id + "_" + std::to_string(i) + ".png", png_encode_pil(p, true)});
    }

    std::vector<uint8_t> f = {'V', 'L', 'F', 'N'};
    le32(f, 1);
    le32(f, uint32_t(size)); le32(f, uint32_t(ascent)); le32(f, uint32_t(descent)); le32(f, uint32_t(height));
    le32(f, uint32_t(pages.size())); le32(f, PAGE_W); le32(f, uint32_t(page_h));
    le32(f, uint32_t(glyphs.size()));
    for (const Glyph &g : glyphs) {
        le32(f, g.cp);
        le16(f, g.page); le16(f, g.x); le16(f, g.y); le16(f, g.w); le16(f, g.h);
        le16(f, uint16_t(g.left)); le16(f, uint16_t(g.top)); le16(f, uint16_t(g.advance));
    }
    le32(f, uint32_t(kern.size()));
    for (auto &k : kern) {
        le32(f, std::get<0>(k));
        le32(f, std::get<1>(k));
        le16(f, uint16_t(int16_t(std::get<2>(k))));
    }
    out.push_back({"fonts/" + font_id + ".fnt", std::move(f)});
}

}  // namespace

void bake_fonts(const Game &game, const std::map<std::string, std::string> &data_files, std::vector<OutFile> &out,
                const std::function<void(const std::string &, double)> &progress) {
    std::u32string game_lua = decode_text(game.text("game.lua"));
    auto lang_text = language_strings(game_lua);
    // strings in the game's code (UI labels etc.) are rendered with the current language's font
    // (general punctuation such as the credits' em dash included: a glyph the font lacks bakes
    // as its .notdef, as LOVE draws it)
    std::set<char32_t> code_chars;
    std::vector<std::string> code_files = game.list("", ".lua", false);
    for (const char *dir : {"functions", "engine"}) {
        auto more = game.list(dir, ".lua", false);
        code_files.insert(code_files.end(), more.begin(), more.end());
    }
    for (const auto &p : code_files) {
        std::set<char32_t> s;
        std::u32string t = decode_text(game.text(p));
        chars_of_lua_strings(t, 0, t.size(), s);
        for (char32_t c : s)
            if (c < 0x3000) code_chars.insert(c);
    }

    FT_Library lib;
    if (FT_Init_FreeType(&lib)) throw std::runtime_error("FT_Init_FreeType");
    size_t done = 0;
    for (const FontDef &fd : FONTS) {
        if (progress) progress(fd.id, double(done) / FONTS.size());
        std::set<char32_t> chars = code_chars;
        for (char32_t c = 32; c < 127; c++) chars.insert(c);
        for (const char *lang : fd.langs) {
            auto it = data_files.find(std::string("loc_") + lang + ".lua");
            if (it == data_files.end()) throw std::runtime_error(std::string("missing loc_") + lang);
            std::u32string loc = decode_text(it->second);
            chars_of_lua_strings(loc, 0, loc.size(), chars);
            std::u32string key(lang, lang + std::strlen(lang));
            auto lt = lang_text.find(key);
            if (lt != lang_text.end()) chars.insert(lt->second.begin(), lt->second.end());
        }
        if (std::strcmp(fd.id, "m6x11plus_200") == 0) {
            // every language's label is listed in the language menu
            for (auto &kv : lang_text)
                for (char32_t c : kv.second)
                    if (c < 0x3000) chars.insert(c);
        }
        std::set<uint32_t> cps;
        for (char32_t c : chars)
            if (c != '\n' && c != '\r' && c != '\t') cps.insert(uint32_t(c));
        Bytes file;
        if (!game.read(std::string("resources/fonts/") + fd.file, file))
            throw std::runtime_error(std::string("falta resources/fonts/") + fd.file);
        bake(lib, file, fd.size, cps, fd.id, out);
        done++;
    }
    FT_Done_FreeType(lib);
}

}  // namespace vlab
