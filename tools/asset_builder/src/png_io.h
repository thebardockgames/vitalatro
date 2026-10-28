// PNG reading (8-bit RGBA) and writing byte-identical to Pillow's PNG encoder
// (PIL/PngImagePlugin.py + libImaging/ZipEncode.c, Pillow 12.1) with zlib 1.3.1.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vlab {

struct Image {
    int w = 0, h = 0, channels = 4;  // 1 = 'L', 4 = 'RGBA'
    std::vector<uint8_t> px;
    uint8_t *at(int x, int y) { return &px[(size_t(y) * w + x) * channels]; }
    const uint8_t *at(int x, int y) const { return &px[(size_t(y) * w + x) * channels]; }
};

bool png_decode_rgba(const std::vector<uint8_t> &file, Image &out);

// Image.save(path) for 'L' / 'RGBA': adaptive filter per row (sum of |signed bytes|; Average
// only with optimize), deflate with Z_FILTERED, memLevel 9, level 9 if optimize else 6,
// IDAT chunks of 65536 bytes (ImageFile.MAXBLOCK), no ancillary chunks.
std::vector<uint8_t> png_encode_pil(const Image &img, bool optimize);

}  // namespace vlab
