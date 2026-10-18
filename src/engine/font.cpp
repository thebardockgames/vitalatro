#include "engine/font.h"

#include <cmath>
#include <cstring>

#include "platform/platform.h"
#include "stb/stb_image.h"

namespace vl {

uint32_t utf8_next(std::string_view s, size_t &i) {
    unsigned char c = (unsigned char)s[i++];
    if (c < 0x80) return c;
    int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    uint32_t cp = c & (0x3F >> extra);
    for (int k = 0; k < extra && i < s.size(); k++) cp = (cp << 6) | ((unsigned char)s[i++] & 0x3F);
    return cp;
}

namespace {
template <class T> T rd(const std::string &d, size_t &p) {
    T v;
    std::memcpy(&v, d.data() + p, sizeof(T));
    p += sizeof(T);
    return v;
}
}  // namespace

Ref<Font> Font::load(const std::string &id) {
    std::string d;
    if (!platform::read_file(("fonts/" + id + ".fnt").c_str(), d) || d.size() < 8 || d.compare(0, 4, "VLFN") != 0)
        platform::fatal("missing font %s", id.c_str());
    Ref<Font> f(new Font());
    size_t p = 8;
    f->size_ = rd<int32_t>(d, p);
    f->ascent_ = rd<int32_t>(d, p);
    f->descent_ = rd<int32_t>(d, p);
    f->height_ = rd<int32_t>(d, p);
    uint32_t npages = rd<uint32_t>(d, p);
    uint32_t pw = rd<uint32_t>(d, p), ph = rd<uint32_t>(d, p);
    for (uint32_t i = 0; i < npages; i++) {
        std::string png;
        std::string path = "fonts/" + id + "_" + std::to_string(i) + ".png";
        if (!platform::read_file(path.c_str(), png)) platform::fatal("missing %s", path.c_str());
        int w, h, n;
        unsigned char *px = stbi_load_from_memory((const unsigned char *)png.data(), (int)png.size(), &w, &h, &n, 1);
        gfx::TextureDesc td{w, h, px};
        td.alpha8 = true;  // LOVE's LA8 glyph data: white with coverage as alpha
        f->pages_.push_back(gfx::create_texture(td));
        stbi_image_free(px);
    }
    uint32_t nglyphs = rd<uint32_t>(d, p);
    for (uint32_t i = 0; i < nglyphs; i++) {
        uint32_t cp = rd<uint32_t>(d, p);
        uint16_t page = rd<uint16_t>(d, p), x = rd<uint16_t>(d, p), y = rd<uint16_t>(d, p);
        uint16_t w = rd<uint16_t>(d, p), h = rd<uint16_t>(d, p);
        int16_t bx = rd<int16_t>(d, p), by = rd<int16_t>(d, p), adv = rd<int16_t>(d, p);
        Glyph g{};
        g.spacing = adv;  // floorf(advance / dpiscale + 0.5) with dpiscale 1
        if (w > 0 && h > 0) {
            // love::graphics::Font::addGlyph: quad extruded by o = 1 px, shifted by the bearings
            const int o = 1;
            g.page = page;
            g.x0 = (float)(-o + bx);
            g.y0 = (float)(-o - by);
            g.x1 = (float)(w + o + bx);
            g.y1 = (float)(h + o - by);
            g.u0 = (float)(x - o) / pw;
            g.v0 = (float)(y - o) / ph;
            g.u1 = (float)(x + w + o) / pw;
            g.v1 = (float)(y + h + o) / ph;
        }
        f->glyphs_[cp] = g;
    }
    uint32_t nkern = rd<uint32_t>(d, p);
    for (uint32_t i = 0; i < nkern; i++) {
        uint32_t a = rd<uint32_t>(d, p), b = rd<uint32_t>(d, p);
        int16_t k = rd<int16_t>(d, p);
        f->kerning_[((uint64_t)a << 32) | b] = k;
    }
    // characters that were not baked render like LOVE's .notdef (use '?' metrics as a stand-in)
    auto it = f->glyphs_.find('?');
    f->missing_ = it != f->glyphs_.end() ? it->second : Glyph{};
    return f;
}

const Font::Glyph &Font::glyph(uint32_t cp) const {
    auto it = glyphs_.find(cp);
    return it != glyphs_.end() ? it->second : missing_;
}

int Font::kerning(uint32_t left, uint32_t right) const {
    if (kerning_.empty() || left == 0) return 0;
    auto it = kerning_.find(((uint64_t)left << 32) | right);
    return it != kerning_.end() ? it->second : 0;
}

int Font::width(std::string_view s) const {
    // love::graphics::Font::getWidth: max over lines of sum(spacing + kerning)
    int max_w = 0, w = 0;
    uint32_t prev = 0;
    for (size_t i = 0; i < s.size();) {
        uint32_t c = utf8_next(s, i);
        if (c == '\n') {
            max_w = std::max(max_w, w);
            w = 0;
            prev = 0;
            continue;
        }
        if (c == '\r') continue;
        w += glyph(c).spacing + kerning(prev, c);
        prev = c;
    }
    return std::max(max_w, w);
}

void Text::set(std::string_view s) {
    // love::graphics::Font::generateVertices
    for (auto &v : per_page_) v.clear();
    per_page_.resize(font_->page_count());
    float dx = 0, dy = 0, base = font_->baseline();
    int maxw = 0;
    uint32_t prev = 0;
    for (size_t i = 0; i < s.size();) {
        uint32_t c = utf8_next(s, i);
        if (c == '\n') {
            maxw = std::max(maxw, (int)dx);
            dy += std::floor(font_->height() * 1.0f + 0.5f);  // line height 1
            dx = 0;
            prev = 0;
            continue;
        }
        if (c == '\r') continue;
        const Font::Glyph &g = font_->glyph(c);
        dx += font_->kerning(prev, c);
        if (g.page >= 0) {
            auto &v = per_page_[g.page];
            float y = dy + base;
            v.push_back({g.x0 + dx, g.y0 + y, g.u0, g.v0});
            v.push_back({g.x0 + dx, g.y1 + y, g.u0, g.v1});
            v.push_back({g.x1 + dx, g.y0 + y, g.u1, g.v0});
            v.push_back({g.x1 + dx, g.y1 + y, g.u1, g.v1});
        }
        dx += g.spacing;
        prev = c;
    }
    width_ = std::max(maxw, (int)dx);
}

void Text::draw(float x, float y, float r, float sx, float sy, float ox, float oy) const {
    gfx::push();
    // love Text:draw(x, y, r, sx, sy, ox, oy) == transform * setTransformation(...)
    Affine m = Affine::from_transformation(x, y, r, sx, sy, ox, oy);
    gfx::transform_by(m);
    for (int p = 0; p < (int)per_page_.size(); p++)
        if (!per_page_[p].empty())
            gfx::draw_glyph_quads(font_->page(p), per_page_[p].data(), (int)per_page_[p].size() / 4);
    gfx::pop();
}

}  // namespace vl
