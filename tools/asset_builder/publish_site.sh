#!/bin/bash
# Publishes build/site (made by build_site.sh) to the gh-pages branch of the public site repository
# ($SITE_REPO, default thebardockgames/vitalatro-web), as a single
# commit with no history (each publish replaces the previous one):
#   tools/asset_builder/publish_site.sh   (Git Bash on Windows: it pushes with the gh credentials)
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SITE=$ROOT/build/site
SITE_REPO=${SITE_REPO:-https://github.com/thebardockgames/vitalatro-web.git}
[ -f "$SITE/index.html" ] && [ -f "$SITE/base/manifest.json" ] || { echo "run build_site.sh first"; exit 1; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
cp -r "$SITE/." "$TMP/"
cd "$TMP"
git init -q -b gh-pages
git config core.autocrlf false  # the files go up byte for byte (base/manifest.json holds their hashes)
git add -A
git -c user.name="$(git -C "$ROOT" config user.name)" -c user.email="$(git -C "$ROOT" config user.email)" commit -q -m "Site from $(git -C "$ROOT" rev-parse --short HEAD)"
git push -q -f "$SITE_REPO" gh-pages
echo "published $(git rev-parse --short HEAD) to gh-pages"
