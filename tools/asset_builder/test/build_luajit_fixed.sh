#!/bin/bash
# Builds the reference LuaJIT for test/compare_native.sh in ~/vlweb_src/luajit_fixed:
# the OpenResty LuaJIT the pipeline uses (v2.1-20251030) with a reproducible table order:
# LUAJIT_TEST_FIXED_ORDER (a string's table hash is its string hash), fixed PRNG seed, no
# secondary string hash, linear string ids, and the portable string hash instead of the
# SSE4.2 CRC32 one (which is seeded at random). lua51/ in the asset builder emulates exactly
# this (string seed 0xae9cb511a8445040: the first PRNG output after the fixed seed).
set -e
mkdir -p ~/vlweb_src && cd ~/vlweb_src
if [ ! -d luajit2 ]; then
  mkdir luajit2
  curl -sSL https://github.com/openresty/luajit2/archive/refs/tags/v2.1-20251030.tar.gz | tar xz -C luajit2 --strip-components=1
fi
rm -rf luajit_fixed && cp -r luajit2 luajit_fixed && cd luajit_fixed
sed -i 's/^#define LJ_HAS_OPTIMISED_HASH  1$/\/* LJ_HAS_OPTIMISED_HASH off: portable string hash *\//' src/lj_arch.h
nice -n 19 make -j4 XCFLAGS="-DLUAJIT_TEST_FIXED_ORDER=1 -DLUAJIT_SECURITY_STRID=0 -DLUAJIT_SECURITY_STRHASH=0 -DLUAJIT_SECURITY_PRNG=0 -DLUAJIT_SECURITY_MCODE=0" > /dev/null
echo "built $(pwd)/src/luajit"
