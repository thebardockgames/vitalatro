#pragma once
// math.random / math.randomseed exactly as LuaJIT 2.1 implements them (Tausworthe TW223, see
// LuaJIT lib_math.c / lj_prng.c, MIT license). Balatro's seeded RNG (pseudorandom, pseudoseed...)
// is built on top of these, so matching them bit for bit keeps seeds identical to the original.
#include <cstdint>
#include <string_view>

namespace vl {

class LuaRandom {
public:
    LuaRandom();
    void seed(double d);       // math.randomseed(d)
    double random();           // math.random()        -> [0, 1)
    double random(double m);   // math.random(m)       -> [1, m]
    double random(double m, double n);  // math.random(m, n)

private:
    uint64_t next_u64d();
    uint64_t next_u64();
    uint64_t u_[4];
};

extern LuaRandom lua_math;  // the single math.random state of the Lua VM

// Lua's float modulo: a - floor(a/b)*b
double lua_fmod(double a, double b);
// tonumber(string.format("%.13f", x))
double round13(double x);

// functions/misc_functions.lua pseudohash(str)
double pseudohash(std::string_view s);

// pseudorandom_element({a, b}) without a seed (used by Moveable:juice_up): picks one of the two
float pseudorandom_element_of2(float a, float b);

}  // namespace vl
