![Vitalatro: Balatro for PS Vita](docs/banner.webp)

# Vitalatro

Reimplementación nativa en C++ de Balatro (1.0.1o) para PlayStation Vita, a 60 fps. Port por **TheBardockGames**.

Este repositorio no incluye los recursos del juego: texturas, sonidos, textos, fuentes y datos se generan
a partir de la copia de cada usuario (el `Balatro.exe` de Steam o el `.apk` de Android). Necesitas tu propia
copia legal del juego.

Proyecto de fans sin relación con LocalThunk ni Playstack. Balatro es de sus respectivos dueños.

## Para jugar

1. Abre la página del generador (https://thebardockgames.github.io/vitalatro-web/) y elige tu `Balatro.exe` o `.apk`.
   Todo se procesa en tu navegador; no se sube nada.
2. Descarga el `.vpk` que genera e instálalo en la Vita con VitaShell.

## Compilar

Todo se compila en WSL:

- Vita: VitaSDK en `/usr/local/vitasdk`, `./build.sh vita` (eboot y VPK en `build/vita`).
- PC (pruebas y comparación con el original): `./build.sh pc`.
- Recursos para el build local: `python tools/build_assets.py "<carpeta del juego>" build/assets_vita vita`
  (los shaders de la Vita necesitan el compilador de shaders del SDK de Sony).
- Generador de VPK (WebAssembly, Emscripten en `~/emsdk`): `tools/asset_builder/build_site.sh` arma la página en
  `build/site`; `tools/asset_builder/publish_site.sh` (Git Bash) la sube a la rama `gh-pages` del repositorio
  `vitalatro-web` (solo la página).
- Build de desarrollo (VLTR00002, servidor de control en el puerto 7777): `-DVL_DEV=ON` y `tools/vitactl.py`.

El estado del trabajo está en [PENDIENTES.md](PENDIENTES.md).
