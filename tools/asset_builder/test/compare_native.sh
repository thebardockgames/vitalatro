#!/bin/bash
# Native asset builder vs the current pipeline (WSL):
#   tools/asset_builder/test/compare_native.sh <game: dir | Balatro.exe | .apk> [reference assets dir]
# - textures/, sounds/, fonts/ must be byte-identical to the reference (build/assets_vita);
# - data/ must be byte-identical to tools/extract_data.lua run by a LuaJIT built for a fixed
#   iteration order (stock LuaJIT randomizes string hashing per run, so its own output is not
#   reproducible), and equal as Lua values (key order aside) to the reference's data/.
# Needs ~/vlweb_src/luajit_fixed (test/build_luajit_fixed.sh: OpenResty LuaJIT v2.1-20251030 with
# LUAJIT_TEST_FIXED_ORDER, PRNG/STRHASH/STRID security off and the SSE4.2 string hash off).
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
GAME=$1
REF=${2:-/mnt/d/Proyectos/Vitalatro/build/assets_vita}
LJ=${LUAJIT_FIXED:-$HOME/vlweb_src/luajit_fixed/src/luajit}
BIN=$ROOT/build/ab_native/vl_asset_builder
OUT=$(mktemp -d /tmp/vlab_cmp.XXXXXX)
fail=0

nice -n 19 "$BIN" "$GAME" "$OUT/assets" | grep -v '%]' || exit 1

for d in textures sounds fonts; do
  if diff -rq "$OUT/assets/$d" "$REF/$d" > "$OUT/$d.diff"; then echo "OK   $d: byte-identical"
  else echo "DIFF $d:"; cat "$OUT/$d.diff"; fail=1; fi
done

# the fixed-order LuaJIT reference reads the game from a directory
SRC=$GAME
if [ -f "$GAME" ]; then
  SRC=$OUT/game
  python3 - "$GAME" "$SRC" <<'EOF'
import sys, zipfile
z = zipfile.ZipFile(sys.argv[1])
names = z.namelist()
root = next(n[:-11] for n in sorted(names, key=len) if n.endswith('version.jkr'))
for n in names:
    if n.startswith(root) and not n.endswith('/'):
        p = sys.argv[2] + '/' + n[len(root):]
        import os; os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, 'wb').write(z.read(n))
EOF
fi
mkdir -p "$OUT/ljdata"
"$LJ" "$ROOT/tools/extract_data.lua" "$SRC" "$OUT/ljdata" > /dev/null || { echo "fixed-order LuaJIT failed"; exit 1; }
if diff -rq "$OUT/assets/data" "$OUT/ljdata" > "$OUT/data.diff"; then echo "OK   data: byte-identical to fixed-order LuaJIT"
else echo "DIFF data vs fixed-order LuaJIT:"; cat "$OUT/data.diff"; fail=1; fi
(cd "$OUT/assets/data" && luajit "$HERE/same_data.lua" "$OUT/assets/data" "$REF/data" *.lua | sed 's/^/     /')

echo "output kept in $OUT"
exit $fail
