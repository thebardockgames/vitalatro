#include "livearea.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <stdexcept>

#include <zlib.h>

#include "png_io.h"

namespace vlab {

namespace {

// Only + - * / and floor: the same pixels natively and in WebAssembly (no libm differences).
const double PI = 3.14159265358979323846;

double msin(double x) {
    double k = std::floor(x / (2 * PI) + 0.5);
    double r = x - k * 2 * PI;  // [-pi, pi]
    double r2 = r * r, term = r, sum = r;
    for (int n = 1; n <= 9; n++) {
        term *= -r2 / ((2 * n) * (2 * n + 1));
        sum += term;
    }
    return sum;
}
double mcos(double x) { return msin(x + PI / 2); }

struct RGB {
    double r, g, b;
};

// The paint swirl (as the web page's background): a twist that tightens towards the centre,
// sine domain warping, red and blue paint with dark seams, flat tones.
RGB swirl(double u, double v, double t) {
    const RGB RED = {0.996, 0.373, 0.333}, BLUE = {0.0, 0.612, 0.992}, DEEP = {0.10, 0.12, 0.17};
    double r2 = u * u + v * v;
    double ang = 2.6 / (1.0 + 2.2 * r2) - t;
    double c = mcos(ang), s = msin(ang);
    double qx = (c * u - s * v) * 3.2, qy = (s * u + c * v) * 3.2;
    for (int i = 0; i < 6; i++) {
        double nx = qx + 0.38 * msin(qy * 1.9 + t * 2.1 + i * 1.7);
        double ny = qy + 0.38 * mcos(qx * 1.6 - t * 1.7 + i * 0.9);
        qx = nx;
        qy = ny;
    }
    double f = 0.5 + 0.5 * msin(qx * 1.3 + qy * 0.7 + t * 0.7);
    double g = 0.5 + 0.5 * msin(qx * 3.1 - qy * 2.3 - t * 1.9);
    auto smooth = [](double a, double b, double x) {
        double k = std::min(1.0, std::max(0.0, (x - a) / (b - a)));
        return k * k * (3 - 2 * k);
    };
    double m = smooth(0.42, 0.58, f);
    RGB col = {BLUE.r + (RED.r - BLUE.r) * m, BLUE.g + (RED.g - BLUE.g) * m, BLUE.b + (RED.b - BLUE.b) * m};
    double seam = smooth(0.0, 0.16, std::fabs(f - 0.5));
    double w = 0.35 + 0.65 * seam;
    col = {DEEP.r + (col.r - DEEP.r) * w, DEEP.g + (col.g - DEEP.g) * w, DEEP.b + (col.b - DEEP.b) * w};
    double dark = 1.0 - 0.28 * smooth(0.55, 0.75, g) * seam;
    double light = 0.13 * smooth(0.9, 1.0, g) * seam;
    col = {col.r * dark + light, col.g * dark + light, col.b * dark + light};
    double vig = 1.0 - 0.25 * r2;
    auto tone = [&](double x) { return std::floor(std::min(1.0, std::max(0.0, x)) * 6.0 + 0.5) / 6.0 * vig; };
    return {tone(col.r), tone(col.g), tone(col.b)};
}

// w x h RGBA, the swirl in blocks of `block` pixels
Image background(int w, int h, int block, double t, double zoom) {
    Image im;
    im.w = w;
    im.h = h;
    im.px.assign(size_t(w) * h * 4, 255);
    double m = std::min(w, h);
    for (int by = 0; by < h; by += block)
        for (int bx = 0; bx < w; bx += block) {
            double u = ((bx + block * 0.5) - w * 0.5) / m * zoom, v = ((by + block * 0.5) - h * 0.5) / m * zoom;
            RGB c = swirl(u, v, t);
            uint8_t px[4] = {uint8_t(std::floor(c.r * 255 + 0.5)), uint8_t(std::floor(c.g * 255 + 0.5)),
                             uint8_t(std::floor(c.b * 255 + 0.5)), 255};
            for (int y = by; y < std::min(h, by + block); y++)
                for (int x = bx; x < std::min(w, bx + block); x++) std::memcpy(im.at(x, y), px, 4);
        }
    return im;
}

Image crop(const Image &src, int x0, int y0, int w, int h) {
    Image o;
    o.w = w;
    o.h = h;
    o.px.resize(size_t(w) * h * 4);
    for (int y = 0; y < h; y++) std::memcpy(o.at(0, y), src.at(x0, y0 + y), size_t(w) * 4);
    return o;
}

// Area-average downscale (premultiplied alpha).
Image downscale(const Image &src, int w, int h) {
    Image o;
    o.w = w;
    o.h = h;
    o.px.resize(size_t(w) * h * 4);
    double sx = double(src.w) / w, sy = double(src.h) / h;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            double x0 = x * sx, x1 = (x + 1) * sx, y0 = y * sy, y1 = (y + 1) * sy;
            double acc[4] = {0, 0, 0, 0}, area = 0;
            for (int j = int(y0); j < int(std::ceil(y1)) && j < src.h; j++) {
                double wy = std::min(y1, j + 1.0) - std::max(y0, double(j));
                for (int i = int(x0); i < int(std::ceil(x1)) && i < src.w; i++) {
                    double wx = std::min(x1, i + 1.0) - std::max(x0, double(i));
                    const uint8_t *p = src.at(i, j);
                    double a = p[3] / 255.0, k = wx * wy;
                    acc[0] += p[0] * a * k;
                    acc[1] += p[1] * a * k;
                    acc[2] += p[2] * a * k;
                    acc[3] += a * k;
                    area += k;
                }
            }
            uint8_t *q = o.at(x, y);
            double a = acc[3] / area;
            for (int c = 0; c < 3; c++) q[c] = a > 0 ? uint8_t(std::floor(acc[c] / acc[3] + 0.5)) : 0;
            q[3] = uint8_t(std::floor(a * 255 + 0.5));
        }
    return o;
}

void blend(Image &dst, const Image &src, int ox, int oy, const uint8_t *tint = nullptr, double opacity = 1.0) {
    for (int y = 0; y < src.h; y++)
        for (int x = 0; x < src.w; x++) {
            int dx = ox + x, dy = oy + y;
            if (dx < 0 || dy < 0 || dx >= dst.w || dy >= dst.h) continue;
            const uint8_t *s = src.at(x, y);
            uint8_t *d = dst.at(dx, dy);
            int a = int(std::floor(s[3] * opacity + 0.5));
            for (int c = 0; c < 3; c++) {
                int sc = tint ? tint[c] : s[c];
                d[c] = uint8_t((sc * a + d[c] * (255 - a) + 127) / 255);
            }
        }
}

Image scale_nearest(const Image &src, int k) {
    Image o;
    o.w = src.w * k;
    o.h = src.h * k;
    o.px.resize(size_t(o.w) * o.h * 4);
    for (int y = 0; y < o.h; y++)
        for (int x = 0; x < o.w; x++) std::memcpy(o.at(x, y), src.at(x / k, y / k), 4);
    return o;
}

// Median cut to <= 256 colours, then an 8-bit paletted PNG (as the LiveArea wants).
std::vector<uint8_t> encode_paletted(const Image &im) {
    std::map<uint32_t, uint32_t> hist;
    for (int y = 0; y < im.h; y++)
        for (int x = 0; x < im.w; x++) {
            const uint8_t *p = im.at(x, y);
            hist[uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2]]++;
        }
    struct Col { uint8_t c[3]; uint32_t n; };
    std::vector<Col> cols;
    for (auto &kv : hist) cols.push_back({{uint8_t(kv.first >> 16), uint8_t(kv.first >> 8), uint8_t(kv.first)}, kv.second});
    struct Box { size_t a, b; };
    std::vector<Box> boxes = {{0, cols.size()}};
    while (boxes.size() < 256) {
        // split the box with the widest channel range (ties: the first)
        int best = -1, best_ch = 0, best_range = 0;
        for (size_t i = 0; i < boxes.size(); i++) {
            if (boxes[i].b - boxes[i].a < 2) continue;
            for (int ch = 0; ch < 3; ch++) {
                int lo = 255, hi = 0;
                for (size_t k = boxes[i].a; k < boxes[i].b; k++) {
                    lo = std::min<int>(lo, cols[k].c[ch]);
                    hi = std::max<int>(hi, cols[k].c[ch]);
                }
                if (hi - lo > best_range) { best = int(i); best_ch = ch; best_range = hi - lo; }
            }
        }
        if (best < 0) break;
        Box bx = boxes[best];
        std::stable_sort(cols.begin() + bx.a, cols.begin() + bx.b,
                         [&](const Col &p, const Col &q) { return p.c[best_ch] < q.c[best_ch]; });
        uint64_t total = 0, acc = 0;
        for (size_t k = bx.a; k < bx.b; k++) total += cols[k].n;
        size_t mid = bx.a + 1;
        for (size_t k = bx.a; k < bx.b - 1; k++) {
            acc += cols[k].n;
            mid = k + 1;
            if (acc * 2 >= total) break;
        }
        boxes[best] = {bx.a, mid};
        boxes.push_back({mid, bx.b});
    }
    std::vector<uint8_t> pal;
    std::map<uint32_t, uint8_t> index;
    for (size_t i = 0; i < boxes.size(); i++) {
        uint64_t s[3] = {0, 0, 0}, n = 0;
        for (size_t k = boxes[i].a; k < boxes[i].b; k++) {
            for (int ch = 0; ch < 3; ch++) s[ch] += uint64_t(cols[k].c[ch]) * cols[k].n;
            n += cols[k].n;
        }
        for (int ch = 0; ch < 3; ch++) pal.push_back(uint8_t((s[ch] + n / 2) / n));
        for (size_t k = boxes[i].a; k < boxes[i].b; k++)
            index[uint32_t(cols[k].c[0]) << 16 | uint32_t(cols[k].c[1]) << 8 | cols[k].c[2]] = uint8_t(i);
    }
    // rows: filter type 0 + indices
    std::vector<uint8_t> raw;
    raw.reserve(size_t(im.w + 1) * im.h);
    for (int y = 0; y < im.h; y++) {
        raw.push_back(0);
        for (int x = 0; x < im.w; x++) {
            const uint8_t *p = im.at(x, y);
            raw.push_back(index[uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2]]);
        }
    }
    uLongf zlen = compressBound(uLong(raw.size()));
    std::vector<uint8_t> z(zlen);
    if (compress2(z.data(), &zlen, raw.data(), uLong(raw.size()), 9) != Z_OK) throw std::runtime_error("compress2");
    z.resize(zlen);

    std::vector<uint8_t> o = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    auto put32 = [&](uint32_t v) { for (int s = 24; s >= 0; s -= 8) o.push_back(uint8_t(v >> s)); };
    auto chunk = [&](const char *type, const std::vector<uint8_t> &d) {
        put32(uint32_t(d.size()));
        size_t st = o.size();
        o.insert(o.end(), type, type + 4);
        o.insert(o.end(), d.begin(), d.end());
        put32(uint32_t(crc32(0, o.data() + st, uInt(d.size() + 4))));
    };
    std::vector<uint8_t> ihdr = {uint8_t(im.w >> 24), uint8_t(im.w >> 16), uint8_t(im.w >> 8), uint8_t(im.w),
                                 uint8_t(im.h >> 24), uint8_t(im.h >> 16), uint8_t(im.h >> 8), uint8_t(im.h),
                                 8, 3, 0, 0, 0};
    chunk("IHDR", ihdr);
    chunk("PLTE", pal);
    chunk("IDAT", z);
    chunk("IEND", {});
    return o;
}

Image load(const Game &game, const std::string &rel) {
    Bytes b;
    Image im;
    if (!game.read(rel, b) || !png_decode_rgba(b, im)) throw std::runtime_error("no se pudo leer " + rel);
    return im;
}

}  // namespace

void make_livearea(const Game &game, std::vector<OutFile> &out) {
    const uint8_t SHADOW[3] = {0, 0, 0};
    Image jokers = load(game, "resources/textures/1x/Jokers.png");
    Image logo = load(game, "resources/textures/1x/balatro.png");
    Image joker = crop(jokers, 0, 0, std::min(71, jokers.w), std::min(95, jokers.h));  // the Joker

    // bubble: a Joker over the swirl
    Image icon = background(128, 128, 4, 0.4, 1.0);
    blend(icon, joker, (128 - joker.w) / 2 + 3, (128 - joker.h) / 2 + 4, SHADOW, 0.45);
    blend(icon, joker, (128 - joker.w) / 2, (128 - joker.h) / 2);
    out.push_back({"sce_sys/icon0.png", encode_paletted(icon)});

    // LiveArea background: the swirl alone
    Image bg = background(840, 500, 10, 1.3, 1.0);
    out.push_back({"sce_sys/livearea/contents/bg.png", encode_paletted(bg)});

    // startup gate: the game's logo over the swirl
    Image gate = background(280, 158, 4, 2.1, 1.3);
    int lh = 140, lw = int(std::floor(double(logo.w) * lh / logo.h + 0.5));
    if (lw > 268) { lw = 268; lh = int(std::floor(double(logo.h) * lw / logo.w + 0.5)); }
    Image small = downscale(logo, lw, lh);
    blend(gate, small, (280 - lw) / 2 + 2, (158 - lh) / 2 + 3, SHADOW, 0.4);
    blend(gate, small, (280 - lw) / 2, (158 - lh) / 2);
    out.push_back({"sce_sys/livearea/contents/startup.png", encode_paletted(gate)});
    (void)scale_nearest;
}

}  // namespace vlab
