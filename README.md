![Vitalatro: Balatro for PS Vita](docs/banner.webp)

# Vitalatro

A native C++ reimplementation of Balatro (1.0.1o) for the PlayStation Vita, running at 60 fps. Port by **TheBardockGames**.

This repository does not include any of the game's assets: textures, sounds, text, fonts and data are generated
from each user's own copy (the Steam `Balatro.exe` or the Android `.apk`). You need your own legal copy of the game.

Fan project, not affiliated with LocalThunk or Playstack. Balatro belongs to its respective owners.

## How to play

1. Open the generator page (https://thebardockgames.github.io/vitalatro-web/) and pick your `Balatro.exe` or `.apk`.
   Everything is processed in your browser; nothing is uploaded.
2. Download the `.vpk` it generates and install it on your Vita with VitaShell.

## Building

Everything is built in WSL:

- Vita: VitaSDK in `/usr/local/vitasdk`, `./build.sh vita` (eboot and VPK in `build/vita`).
- PC (testing and comparison with the original): `./build.sh pc`.
- Assets for the local build: `python tools/build_assets.py "<game folder>" build/assets_vita vita`
  (the Vita shaders need the shader compiler from Sony's SDK).
- VPK generator (WebAssembly, Emscripten in `~/emsdk`): `tools/asset_builder/build_site.sh` builds the page in
  `build/site`; `tools/asset_builder/publish_site.sh` (Git Bash) pushes it to the `gh-pages` branch of the
  `vitalatro-web` repository (the page only).
- Dev build (VLTR00002, control server on port 7777): `-DVL_DEV=ON` and `tools/vitactl.py`.

Work status is tracked in [PENDIENTES.md](PENDIENTES.md) (in Spanish).
