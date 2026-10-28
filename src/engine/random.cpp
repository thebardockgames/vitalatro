#include "engine/random.h"

#include <cmath>

#include "core/math.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace vl {

LuaRandom lua_math;

LuaRandom::LuaRandom() {
    // lj_prng_seed_fixed: precomputed random_seed(rs, 0.0)
    u_[0] = 0xa0d277570a345b8cULL;
    u_[1] = 0x764a296c5d4aa64fULL;
    u_[2] = 0x51220704070adeaaULL;
    u_[3] = 0x2a2717b5a7b7b927ULL;
}

#define TW223_GEN(z, r, i, k, q, s)                                                         \
    z = u_[i];                                                                              \
    z = (((z << q) ^ z) >> (k - s)) ^ ((z & ((uint64_t)(int64_t)-1 << (64 - k))) << s);     \
    r ^= z;                                                                                 \
    u_[i] = z;

uint64_t LuaRandom::next_u64() {
    uint64_t z, r = 0;
    TW223_GEN(z, r, 0, 63, 31, 18)
    TW223_GEN(z, r, 1, 58, 19, 28)
    TW223_GEN(z, r, 2, 55, 24, 7)
    TW223_GEN(z, r, 3, 47, 21, 8)
    return r;
}

uint64_t LuaRandom::next_u64d() {
    uint64_t r = next_u64();
    return (r & 0x000fffffffffffffULL) | 0x3ff0000000000000ULL;
}

void LuaRandom::seed(double d) {
    n_seed++;
    uint32_t r = 0x11090601;
    for (int i = 0; i < 4; i++) {
        uint32_t m = 1u << (r & 255);
        r >>= 8;
        d = d * 3.14159265358979323846 + 2.7182818284590452354;
        uint64_t u;
        std::memcpy(&u, &d, 8);
        if (u < m) u += m;
        u_[i] = u;
    }
    for (int i = 0; i < 10; i++) (void)next_u64();
}

double LuaRandom::random() {
    n_random++;
    uint64_t u = next_u64d();
    double d;
    std::memcpy(&d, &u, 8);
    return d - 1.0;
}

double LuaRandom::random(double m) { return std::floor(random() * m) + 1.0; }

double LuaRandom::random(double m, double n) { return std::floor(random() * (n - m + 1.0)) + m; }

double lua_fmod(double a, double b) { return a - std::floor(a / b) * b; }

double round13(double x) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.13f", x);
    return std::strtod(buf, nullptr);
}

double pseudohash(std::string_view s) {
    double num = 1;
    for (int i = (int)s.size(); i >= 1; i--)
        num = lua_fmod((1.1239285023 / num) * (unsigned char)s[i - 1] * PI + PI * i, 1.0);
    return num;
}

float pseudorandom_element_of2(float a, float b) {
    // keys {1, 2} sorted by key; math.random(2)
    return lua_math.random(2) == 1 ? a : b;
}

}  // namespace vl
