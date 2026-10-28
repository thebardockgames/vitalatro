#include "png_io.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

#include <zlib.h>

#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace vlab {

bool png_decode_rgba(const std::vector<uint8_t> &file, Image &out) {
    int w, h, n;
    unsigned char *p = stbi_load_from_memory(file.data(), int(file.size()), &w, &h, &n, 4);
    if (!p) return false;
    out.w = w;
    out.h = h;
    out.channels = 4;
    out.px.assign(p, p + size_t(w) * h * 4);
    stbi_image_free(p);
    return true;
}

namespace {

void put32(std::vector<uint8_t> &o, uint32_t v) {
    o.push_back(uint8_t(v >> 24));
    o.push_back(uint8_t(v >> 16));
    o.push_back(uint8_t(v >> 8));
    o.push_back(uint8_t(v));
}

void chunk(std::vector<uint8_t> &o, const char *type, const uint8_t *data, size_t n) {
    put32(o, uint32_t(n));
    size_t start = o.size();
    o.insert(o.end(), type, type + 4);
    if (n) o.insert(o.end(), data, data + n);
    uint32_t crc = uint32_t(crc32(0, o.data() + start, uInt(n + 4)));
    put32(o, crc);
}

inline int dist(uint8_t v) { return v < 128 ? v : 256 - v; }

}  // namespace

std::vector<uint8_t> png_encode_pil(const Image &img, bool optimize) {
    const int bpp = img.channels;
    const size_t bytes = size_t(img.w) * bpp;
    std::vector<uint8_t> buffer(bytes + 1), previous(bytes + 1, 0), prior(bytes + 1), up(bytes + 1),
        average(bytes + 1), paeth(bytes + 1);
    buffer[0] = 0;
    prior[0] = 1;
    up[0] = 2;
    average[0] = 3;
    paeth[0] = 4;

    z_stream zs{};
    if (deflateInit2(&zs, optimize ? Z_BEST_COMPRESSION : Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15, 9, Z_FILTERED) != Z_OK)
        throw std::runtime_error("deflateInit2");
    std::vector<uint8_t> idat;
    uint8_t outbuf[65536];
    auto drain = [&](int flush) {
        int r;
        do {
            zs.next_out = outbuf;
            zs.avail_out = sizeof(outbuf);
            r = deflate(&zs, flush);
            if (r == Z_STREAM_ERROR) throw std::runtime_error("deflate");
            idat.insert(idat.end(), outbuf, outbuf + (sizeof(outbuf) - zs.avail_out));
        } while (zs.avail_out == 0 || (flush == Z_FINISH && r != Z_STREAM_END));
    };

    for (int y = 0; y < img.h; y++) {
        std::memcpy(&buffer[1], img.at(0, y), bytes);
        uint8_t *output = buffer.data();
        int sum = 0, s;
        size_t i;
        // 0. None
        for (i = 1; i <= bytes; i++) sum += dist(buffer[i]);
        // 2. Up (first: cheap when a row repeats)
        if (sum > 0) {
            for (i = 1, s = 0; i <= bytes; i++) {
                uint8_t v = uint8_t(buffer[i] - previous[i]);
                up[i] = v;
                s += dist(v);
            }
            if (s < sum) { output = up.data(); sum = s; }
        }
        // 1. Sub
        if (sum > 0) {
            for (i = 1, s = 0; i <= size_t(bpp); i++) {
                uint8_t v = buffer[i];
                prior[i] = v;
                s += dist(v);
            }
            for (; i <= bytes; i++) {
                uint8_t v = uint8_t(buffer[i] - buffer[i - bpp]);
                prior[i] = v;
                s += dist(v);
            }
            if (s < sum) { output = prior.data(); sum = s; }
        }
        // 3. Average (only when optimizing)
        if (optimize && sum > 0) {
            for (i = 1, s = 0; i <= size_t(bpp); i++) {
                uint8_t v = uint8_t(buffer[i] - previous[i] / 2);
                average[i] = v;
                s += dist(v);
            }
            for (; i <= bytes; i++) {
                uint8_t v = uint8_t(buffer[i] - (buffer[i - bpp] + previous[i]) / 2);
                average[i] = v;
                s += dist(v);
            }
            if (s < sum) { output = average.data(); sum = s; }
        }
        // 4. Paeth
        if (sum > 0) {
            for (i = 1, s = 0; i <= size_t(bpp); i++) {
                uint8_t v = uint8_t(buffer[i] - previous[i]);
                paeth[i] = v;
                s += dist(v);
            }
            for (; i <= bytes; i++) {
                int a = buffer[i - bpp], b = previous[i], c = previous[i - bpp];
                int pa = std::abs(b - c), pb = std::abs(a - c), pc = std::abs(a + b - 2 * c);
                uint8_t v = uint8_t(buffer[i] - ((pa <= pb && pa <= pc) ? a : (pb <= pc) ? b : c));
                paeth[i] = v;
                s += dist(v);
            }
            if (s < sum) { output = paeth.data(); sum = s; }
        }
        zs.next_in = output;
        zs.avail_in = uInt(bytes + 1);
        while (zs.avail_in > 0) drain(Z_NO_FLUSH);
        std::swap(buffer, previous);
    }
    drain(Z_FINISH);
    deflateEnd(&zs);

    std::vector<uint8_t> o = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    uint8_t ihdr[13];
    ihdr[0] = uint8_t(img.w >> 24); ihdr[1] = uint8_t(img.w >> 16); ihdr[2] = uint8_t(img.w >> 8); ihdr[3] = uint8_t(img.w);
    ihdr[4] = uint8_t(img.h >> 24); ihdr[5] = uint8_t(img.h >> 16); ihdr[6] = uint8_t(img.h >> 8); ihdr[7] = uint8_t(img.h);
    ihdr[8] = 8;
    ihdr[9] = img.channels == 1 ? 0 : 6;
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    chunk(o, "IHDR", ihdr, 13);
    for (size_t p = 0; p < idat.size(); p += 65536)
        chunk(o, "IDAT", idat.data() + p, std::min<size_t>(65536, idat.size() - p));
    chunk(o, "IEND", nullptr, 0);
    return o;
}

}  // namespace vlab
