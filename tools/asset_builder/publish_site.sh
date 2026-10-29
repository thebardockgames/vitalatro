#!/bin/bash
# Publishes build/site (made by build_site.sh) to the gh-pages branch of origin, as a single commit
# with no history (each publish replaces the previous one):
#   tools/asset_builder/publish_site.sh   (Git Bash on Windows: it pushes with the gh credentials)
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SITE=$ROOT/build/site
[ -f "$SITE/index.html" ] && [ -f "$SITE/base/manifest.json" ] || { echo "run build_site.sh first"; exit 1; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
cp -r "$SITE/." "$TMP/"
cd "$TMP"
git init -q -b gh-pages
git add -A
git -c user.name="$(git -C "$ROOT" config user.name)" -c user.email="$(git -C "$ROOT" config user.email)" commit -q -m "Site from $(git -C "$ROOT" rev-parse --short HEAD)"
git push -q -f "$(git -C "$ROOT" remote get-url origin)" gh-pages
echo "published $(git rev-parse --short HEAD) to gh-pages"
