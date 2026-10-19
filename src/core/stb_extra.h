#pragma once
// stb_image_write functions that its header only declares in the implementation section
// (implemented in core/stb_impl.cpp). Results are freed with std::free.
extern "C" {
unsigned char *stbi_zlib_compress(unsigned char *data, int data_len, int *out_len, int quality);
unsigned char *stbi_write_png_to_mem(const unsigned char *pixels, int stride_bytes, int x, int y, int n, int *out_len);
}
