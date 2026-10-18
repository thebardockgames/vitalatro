#pragma once
// love.graphics Font / Text equivalents for fonts baked at build time by tools/bake_fonts.py
// (same FreeType rasterization, metrics and glyph geometry as LOVE 11.5).
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/object.h"
#include "core/value.h"
#include "gfx/gfx.h"

namespace vl {

class Font : public RefCounted {
public:
    // id: baked font id, e.g. "m6x11plus_200"
    // Fonts are loaded once and shared by id.
    static Ref<Font> load(const std::string &id);
    // LOVE loads every font at startup. Here a worker thread decodes their pages in the background
    // and pump() turns one decoded font per call into textures, so neither costs a visible frame.
    static void preload_async(const std::vector<std::string> &ids);
    static void pump();

    // love Font:getWidth(text) / getHeight() / getBaseline()
    int width(std::string_view utf8) const;
    float height() const { return (float)height_; }
    float baseline() const { return (float)ascent_; }

    struct Glyph {
        int page = -1;  // -1: empty glyph (no quad)
        float x0, y0, x1, y1;  // quad relative to the pen position (LOVE extrudes 1 px)
        float u0, v0, u1, v1;
        int spacing;
    };
    const Glyph &glyph(uint32_t cp) const;
    int kerning(uint32_t left, uint32_t right) const;
    gfx::Texture *page(int i) const { return pages_[i]; }
    int page_count() const { return (int)pages_.size(); }

private:
    using DecodedFontRef = const void *;
    static Ref<Font> build(const DecodedFontRef &decoded);
    int size_ = 0, ascent_ = 0, descent_ = 0, height_ = 0;
    std::vector<gfx::Texture *> pages_;
    std::unordered_map<uint32_t, Glyph> glyphs_;
    std::unordered_map<uint64_t, int> kerning_;
    Glyph missing_;
};

// love Text: a string laid out once with a font, drawn with the current transform and colour.
class Text : public Object {
public:
    Text(Ref<Font> f, std::string_view s) : font_(std::move(f)) { set(s); }
    void set(std::string_view s);
    void draw(float x, float y, float r = 0, float sx = 1, float sy = 1, float ox = 0, float oy = 0) const;
    int width() const { return width_; }
    Font *font() const { return font_.get(); }

private:
    Ref<Font> font_;
    std::vector<std::vector<gfx::GlyphVertex>> per_page_;
    int width_ = 0;
};

// Decode one UTF-8 codepoint, advancing i. Invalid bytes decode as themselves.
uint32_t utf8_next(std::string_view s, size_t &i);

}  // namespace vl
