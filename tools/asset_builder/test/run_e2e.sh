#!/bin/bash
# End to end (WSL, headless, no window): serve build/site, build the VPK in headless Chromium
# with the given game file, build the same VPK with the native CLI, compare.
#   tools/asset_builder/test/run_e2e.sh <Balatro.exe | .apk> [tag]
# Needs: tools/asset_builder/build_site.sh run first, build/ab_native built, Playwright in
# $PW_DIR (default ~/vlweb_tools: npm i playwright@1.49.1 && npx playwright install chromium),
# node (emsdk's is fine) and luajit.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
GAME=$1
TAG=${2:-test}
PW_DIR=${PW_DIR:-$HOME/vlweb_tools}
PORT=${PORT:-8766}
OUT=${E2E_OUT:-/tmp/vlab_e2e}
mkdir -p "$OUT"
source ~/emsdk/emsdk_env.sh > /dev/null 2>&1
export NODE_PATH=$PW_DIR/node_modules

(cd "$ROOT/build/site" && exec python3 -m http.server "$PORT" > /dev/null 2>&1) &
SERVER=$!
trap 'kill $SERVER 2>/dev/null' EXIT
sleep 1
cp "$HERE/e2e.mjs" "$PW_DIR/e2e.mjs"  # resolves 'playwright' from there
node "$PW_DIR/e2e.mjs" "http://localhost:$PORT/" "$GAME" "$OUT/web_$TAG.vpk" "$OUT" | grep -v '^progress' || exit 1
nice -n 19 "$ROOT/build/ab_native/vl_asset_builder" "$GAME" "$OUT/native_$TAG" --vpk "$OUT/native_$TAG.vpk" \
  --base "$ROOT/build/site/base" | tail -1
python3 "$HERE/compare_vpk.py" "$OUT/web_$TAG.vpk" "$OUT/native_$TAG.vpk" \
  "${REF_ASSETS:-/mnt/d/Proyectos/Vitalatro/build/assets_vita}"
