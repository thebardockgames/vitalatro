#!/bin/bash
# Builds the deployable static site (GitHub Pages) into build/site (WSL):
#   tools/asset_builder/build_site.sh
# - web/ (page, background, worker)
# - builder/: the asset builder compiled to WebAssembly (Emscripten from ~/emsdk)
# - base/: our own prebuilt files that go into every VPK, from a Vita build (read-only):
#     eboot.bin, sce_sys/param.sfo   <- $VITA_BUILD (default: the main checkout's build/vita)
#     shaders/*.gxp                  <- $VITA_ASSETS/shaders (default: build/assets_vita)
#     sce_sys/livearea/contents/template.xml <- this repo
#   listed in base/manifest.json. No Balatro file goes into the site: the textures, sounds,
#   data, fonts and sce_sys images are built in the browser from the user's copy.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
VITA_BUILD=${VITA_BUILD:-/mnt/d/Proyectos/Vitalatro/build/vita}
VITA_ASSETS=${VITA_ASSETS:-/mnt/d/Proyectos/Vitalatro/build/assets_vita}
OUT=${SITE_OUT:-$ROOT/build/site}

source ~/emsdk/emsdk_env.sh > /dev/null 2>&1
if [ ! -f "$ROOT/build/ab_wasm/build.ninja" ]; then
  emcmake cmake -S "$HERE" -B "$ROOT/build/ab_wasm" -G Ninja -DCMAKE_BUILD_TYPE=Release > /dev/null
fi
nice -n 19 cmake --build "$ROOT/build/ab_wasm" -j4

rm -rf "$OUT"
mkdir -p "$OUT/builder" "$OUT/base/sce_sys/livearea/contents" "$OUT/base/shaders"
cp -r "$ROOT/web/." "$OUT/"
rm -rf "$OUT/tests"
touch "$OUT/.nojekyll"
cp "$ROOT/build/ab_wasm/vl_asset_builder.js" "$ROOT/build/ab_wasm/vl_asset_builder.wasm" "$OUT/builder/"

cp "$VITA_BUILD/eboot.bin" "$OUT/base/eboot.bin"
cp "$VITA_BUILD/vitalatro.vpk_param.sfo" "$OUT/base/sce_sys/param.sfo"
cp "$ROOT/sce_sys/livearea/contents/template.xml" "$OUT/base/sce_sys/livearea/contents/template.xml"
cp "$VITA_ASSETS"/shaders/*.gxp "$OUT/base/shaders/"

python3 - "$OUT/base" <<'EOF'
import hashlib, json, os, sys
base = sys.argv[1]
files = []
for root, _, names in os.walk(base):
    for n in names:
        p = os.path.join(root, n)
        rel = os.path.relpath(p, base).replace(os.sep, '/')
        if rel == 'manifest.json':
            continue
        data = open(p, 'rb').read()
        files.append({'path': rel, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
files.sort(key=lambda f: f['path'])
json.dump({'files': files}, open(os.path.join(base, 'manifest.json'), 'w'), indent=1)
print(f'base: {len(files)} files, {sum(f["size"] for f in files)} bytes')
EOF
echo "site ready in $OUT"
