#!/bin/bash
# Usage (inside WSL): ./build.sh vita|pc
set -e
export VITASDK=${VITASDK:-/usr/local/vitasdk}
export PATH=$VITASDK/bin:$PATH
T=${1:-vita}
cd "$(dirname "$0")"
cmake -S . -B build/$T -G Ninja -DVL_TARGET=$T -DCMAKE_BUILD_TYPE=Release > build/cmake_$T.log
cmake --build build/$T
