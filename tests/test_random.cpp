// Prints the same sequence as tests/test_random.lua; the two outputs must be identical.
#include <cstdio>
#include "engine/random.h"
using namespace vl;
int main() {
    const double seeds[] = {0, 1, 0.5, 123.456, 0.123456789, 1e-9, 98765.4321};
    for (double s : seeds) {
        lua_math.seed(s);
        for (int i = 0; i < 4; i++) std::printf("%.17g\n", lua_math.random());
        double a = lua_math.random(52);  // separate statements: argument order is unspecified
        double b = lua_math.random(3, 9);
        std::printf("%.17g %.17g\n", a, b);
    }
    const char *strs[] = {"Joker1", "shop_pack", "VITABENCH", "cashout1", "tarot2rerollsAAAAAAA"};
    for (const char *s : strs) std::printf("%.17g %.17g\n", pseudohash(s), round13(pseudohash(s) * 1.72431234 + 2.134453429141));
    return 0;
}
