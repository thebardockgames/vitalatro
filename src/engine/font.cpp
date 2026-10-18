#include "engine/font.h"

#include <cmath>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <unordered_map>

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

namespace {
// A font file read and its pages decoded (no GPU work), ready to become a Font.
struct DecodedFont {
    std::string id;
    std::string fnt;
    std::vector<std::vector<uint8_t>> pages;
    int page_w = 0, page_h = 0;
};

DecodedFont decode_font(const std::string &id) {
    DecodedFont r;
    r.id = id;
    if (!platform::read_file(("fonts/" + id + ".fnt").c_str(), r.fnt) || r.fnt.size() < 8 || r.fnt.compare(0, 4, "VLFN") != 0)
        platform::fatal("missing font %s", id.c_str());
    size_t p = 8 + 4 * 4;
    uint32_t npages = rd<uint32_t>(r.fnt, p);
    for (uint32_t i = 0; i < npages; i++) {
        std::string png;
        std::string path = "fonts/" + id + "_" + std::to_string(i) + ".png";
        if (!platform::read_file(path.c_str(), png)) platform::fatal("missing %s", path.c_str());
        int w, h, n;
        unsigned char *px = stbi_load_from_memory((const unsigned char *)png.data(), (int)png.size(), &w, &h, &n, 1);
        r.pages.emplace_back(px, px + (size_t)w * h);
        r.page_w = w;
        r.page_h = h;
        stbi_image_free(px);
    }
    return r;
}

std::unordered_map<std::string, Ref<Font>> font_cache;

struct Preload {
    std::vector<std::string> ids;
    std::vector<DecodedFont> decoded;  // filled by the worker, in ids order
    std::vector<Ref<Font>> built;      // the worker also creates the textures when the backend allows it
    std::atomic<int> ready{0};         // number of entries of decoded that are complete
    int consumed = 0;                  // decoded fonts already turned into textures (main thread)
};
std::shared_ptr<Preload> preload;
}  // namespace

Ref<Font> Font::build(const DecodedFontRef &dref) {
    const DecodedFont &df = *static_cast<const DecodedFont *>(dref);
    const std::string &d = df.fnt;
    Ref<Font> f(new Font());
    size_t p = 8;
    f->size_ = rd<int32_t>(d, p);
    f->ascent_ = rd<int32_t>(d, p);
    f->descent_ = rd<int32_t>(d, p);
    f->height_ = rd<int32_t>(d, p);
    uint32_t npages = rd<uint32_t>(d, p);
    uint32_t pw = rd<uint32_t>(d, p), ph = rd<uint32_t>(d, p);
    for (uint32_t i = 0; i < npages; i++) {
        gfx::TextureDesc td{df.page_w, df.page_h, df.pages[i].data()};
        td.alpha8 = true;  // LOVE's LA8 glyph data: white with coverage as alpha
        f->pages_.push_back(gfx::create_texture(td));
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

Ref<Font> Font::load(const std::string &id) {
    auto it = font_cache.find(id);
    if (it != font_cache.end()) return it->second;
    // a font the preload already decoded is used from there; otherwise it is decoded right now
    if (preload) {
        int ready = preload->ready.load(std::memory_order_acquire);
        for (int i = 0; i < ready; i++)
            if (preload->decoded[i].id == id)
                return font_cache[id] = preload->built[i] ? preload->built[i] : build(&preload->decoded[i]);
    }
    DecodedFont df = decode_font(id);
    return font_cache[id] = build(&df);
}

void Font::preload_async(const std::vector<std::string> &ids) {
    auto job = std::make_shared<Preload>();
    for (const std::string &id : ids)
        if (!font_cache.count(id) && std::find(job->ids.begin(), job->ids.end(), id) == job->ids.end()) job->ids.push_back(id);
    job->decoded.resize(job->ids.size());
    job->built.resize(job->ids.size());
    preload = job;
    const bool build_on_worker = gfx::create_texture_any_thread();
    platform::run_async([job, build_on_worker] {
        for (size_t i = 0; i < job->ids.size(); i++) {
            job->decoded[i] = decode_font(job->ids[i]);
            if (build_on_worker) {
                job->built[i] = build(&job->decoded[i]);
                job->decoded[i] = DecodedFont{job->decoded[i].id, {}, {}, 0, 0};
            }
            job->ready.store((int)i + 1, std::memory_order_release);
        }
    });
}

void Font::pump() {
    if (!preload) return;
    int ready = preload->ready.load(std::memory_order_acquire);
    if (preload->consumed < ready) {
        int i = preload->consumed++;
        DecodedFont &df = preload->decoded[i];
        if (!font_cache.count(df.id)) font_cache[df.id] = preload->built[i] ? preload->built[i] : build(&df);
        df = DecodedFont{};  // free the decoded pages
        preload->built[i] = nullptr;
    }
    if (preload->consumed == (int)preload->ids.size()) preload = nullptr;
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
