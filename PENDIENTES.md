# Vitalatro: lo que falta

Auditoría completa contra el código original (todas las funciones de los .lua, `G.FUNCS`, `G.UIDEF`,
tipos de desbloqueo, centros, sonidos, shaders y los `TODO` del código). Se marca con `[x]` al terminar.

## 1. Rendimiento en la Vita
- [ ] Vista de baraja a 60 fps (estaba en 43–46; VPK con optimizaciones subido, falta medir)
- [ ] Medir en la Vita cada pantalla pesada: colección (todas las páginas), info de partida, lista de retos,
      configuración de partida, game over / victoria, tienda con sobres abiertos
- [ ] Coste por caja de la UI (~26 µs por caja en la Vita): seguir reduciendo copias de tablas en `draw_self`

## 2. Tutorial
- [x] `tutorial_controller` / `tutorial_part` / `tutorial_info` (state_events.lua): los pasos con Jimbo
- [x] `G.OVERLAY_TUTORIAL` (Jimbo, globo de texto, botón para avanzar) y su borrado en `delete_run`
- [x] Resaltados del tutorial (`OVERLAY_TUTORIAL.highlights`) en el dibujo
- [x] El foco del mando ignora a Jimbo (`node.jimbo` en el controlador)
- [x] `skip_tutorial_section` (saltar tutorial) y `start_tutorial`
- [x] Semilla `TUTORIAL` y su mazo/cartas forzadas en la primera partida

## 3. Desbloqueos y logros
- [x] `create_unlock_overlay`: pantalla de desbloqueo de joker/carta (`create_UIBox_card_unlock`)
- [x] Pantalla de desbloqueo de baraja (`create_UIBox_deck_unlock`)
- [x] `continue_unlock` / cola de varios desbloqueos seguidos (al terminar una partida)
- [x] Condiciones `round_win` (Matador, Troubadour, Hanging Chad)
- [x] Condiciones `hand_contents` (2 jokers)
- [x] Condiciones `discard_custom` (3 jokers)
- [x] `extra.playing_cards_created` en `playing_card_joker_effects` (contador para un logro)
- [x] Verificar que todos los logros se otorgan y muestran su aviso (`notify_alert`) como en el original

## 4. Reglas y partida
- [x] Baraja Anaglyph: Double Tag al vencer a un jefe (`Back:trigger_effect` context `eval`)
- [x] Aviso del jefe "La mano no puntuará" + texto del jefe sobre la zona de juego (`boss_warning_text`)
- [x] Stake dorada: búsqueda de semilla que evita leyendas ya ganadas (`get_first_legendary`)
- [x] Stake dorada: brillo animado del chip en `get_stake_sprite`
- [x] Música de sobres: `music2` (Arcana/Spectral) y `music3` (Celestial)
- [x] Menú de pausa: fila de la semilla en partidas con semilla (`current_seed`)
- [x] Reconstruir `G.buttons` al cambiar ajustes durante una partida (`create_UIBox_buttons`)
- [x] Reinicio rápido (mantener R; solo teclado, opcional en Vita)
- [x] Quitar el stub muerto de `can_use_consumeable` en state_events.cpp (el real está en consumables.cpp)

## 5. Fidelidad visual
- [ ] Vista de baraja: pequeña diferencia de inclinación por carta (ambient tilt / IDs)
- [ ] Mipmaps manuales para texturas (`TODO(manual-mips)`), cartas pequeñas igual que en PC
- [ ] Fondo: campo k=1 en núcleos de CPU en vez de k=2 a media tasa (decisión pendiente)
- [ ] Revisar los 15 idiomas en pantalla (fuentes CJK: SC/TC están a tamaño 47)
- [ ] Tutorial: texto del globo de Jimbo más grande en la Vita (casi no se lee; con la opción de menús grandes)

## 6. Regresión completa contra el original
- [x] `jokerbatch` (todos los jokers)
- [x] `tagbatch` (todas las etiquetas; Orbital pendiente: el original se cuelga si se agrega fuera de un salto de ciega)
- [x] `bossbatch` (los 28 jefes)
- [x] Consumibles: todos los tarots, planetas y espectrales
- [x] Vouchers: los 32
- [x] Barajas: las 15 y las 8 stakes
- [x] Retos: los 20 (inicio y reglas)
- [x] Sobres: los 5 tipos con sus variantes

## 7. Verificar en la Vita contigo
- [ ] Guardar y continuar partida (tienda, mitad de mano, sobre abierto)
- [ ] Ya no hay tirón al descartar contra un jefe
- [ ] Salir con el botón PS / suspender y volver (audio, guardado)
- [ ] Teclado en pantalla para la semilla y nombres
- [ ] Una partida completa hasta ante 8 + modo infinito

## 8. Distribución: web que genera el VPK en el navegador (sin datos de Balatro en nada que publiquemos)
- [ ] El juego carga todos los recursos desde `ux0:data/Vitalatro/` (o desde dentro del VPK generado), nada embebido
- [x] Biblioteca C++ "asset builder" que reemplaza `tools/build_assets.py`: leer el zip del `Balatro.exe` (LOVE fusionado)
      o del `.apk`, verificar versión (1.0.1o), extraer texturas/sonidos, datos con Lua 5.1 embebido (`extract_data.lua`),
      fuentes como LOVE (`bake_fonts.py`), iconos de botones Vita (`vita_pips.py`)
- [x] Compilar esa biblioteca a WebAssembly (Emscripten) + herramienta de línea de comandos para PC (mismo resultado byte a byte)
- [x] Armar el VPK en el navegador: zip con nuestro `eboot.bin`, `sce_sys`, shaders compilados y los recursos generados
- [ ] Shaders: reescribir los que aún son conversión directa del original (fórmulas propias equivalentes, verificadas),
      y publicar solo nuestros `.gxp` compilados
- [x] Página web estática (GitHub Pages): elegir archivo, progreso, descarga; todo local, nada se sube
- [ ] Verificar que el VPK generado por la web es idéntico al que genera el build local y funciona en la Vita
- [ ] `sce_sys` (icono, fondo y logo del LiveArea) es arte de Balatro: quitarlo del repo y de su historial antes de publicarlo
      (la web ya genera los suyos a partir de los archivos del usuario)
- [ ] Datos deterministas: generar `build/assets_*/data` con `vl_asset_builder` (el LuaJIT de WSL baraja el orden de las tablas
      en cada ejecución); revisar el orden de empates en los pools (Edition, Consumeables, Tarot_Planet) y su efecto en el RNG
- [ ] Copia de Steam (PC): probar el juego con sus datos (crt 70, sin rumble, `icons.png` distinto, textos de collabs)
- [ ] Recompilar el eboot de la Vita antes de armar el sitio (el de `build/vita` es viejo) y publicar en GitHub Pages
- [ ] Revisar que nada publicado (repo, web, eboot) contiene recursos o textos de Balatro

## Fuera de alcance (del original, a propósito)
Puntuaciones online, Discord/Twitter/Steam/linktree, encuestas y CTA de la demo, retos diarios
(`daily_overview`), resolución/ventana/vsync de PC, herramientas de depuración (`DT_*`, sandbox,
test framework), informes de errores, logros de Steam (`unlockAchievement`), copias en la nube de Android.
