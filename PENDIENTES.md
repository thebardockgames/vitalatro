# Vitalatro: lo que falta

Auditoría completa contra el código original (todas las funciones de los .lua, `G.FUNCS`, `G.UIDEF`,
tipos de desbloqueo, centros, sonidos, shaders y los `TODO` del código). Se marca con `[x]` al terminar.

## 1. Rendimiento en la Vita
- [ ] Vista de baraja a 60 fps: tienda 59.4, ciega grande 59.9; contra un jefe que debilita 55.2
      (CPU al límite dibujando las 52 cartas: optimizar el dibujo de cartas)
- [x] Medir en la Vita cada pantalla pesada: colección (todas las páginas), info de partida,
      configuración de partida, game over / victoria, tienda con sobre jumbo abierto: 59.9
      (la página de ediciones de la colección va a 56.9: GPU, shaders de edición + fondo del menú)
- [x] Partidas largas: el guardado periódico costaba 132 ms en el hilo principal (ahora 12 ms); fugas de
      objetos por ronda (botón de cobrar, partículas del jefe, elementos huérfanos de la tienda) corregidas
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
- [x] Revisar los 15 idiomas en pantalla contra el original (`VL_LANG`/`REF_LANG`: selección de ciega, mano,
      vista de baraja): 13 iguales; chino simplificado/tradicional ~0.5 px más abajo (métrica de la fuente CJK)
- [x] Tutorial: texto del globo de Jimbo más grande en la Vita (x1.5 con la opción de menús grandes)

## 6. Regresión completa contra el original
- [x] `jokerbatch` (todos los jokers)
- [x] `tagbatch` (todas las etiquetas; Orbital comprobada con un salto de ciega real: `VL_BLIND_TAG`)
- [x] `bossbatch` (los 28 jefes)
- [x] Consumibles: todos los tarots, planetas y espectrales
- [x] Vouchers: los 32
- [x] Barajas: las 15 y las 8 stakes
- [x] Retos: los 20 (inicio y reglas)
- [x] Sobres: los 5 tipos con sus variantes

## 7. Verificar en la Vita (con el MCP de la Vita)
- [x] Guardar y continuar partida (tienda, mitad de mano, sobre abierto)
- [x] Ya no hay tirón al descartar contra un jefe
- [x] Salir con el botón PS / suspender y volver (audio, guardado)
- [x] Teclado en pantalla para la semilla (igual que el original con las mismas entradas)
- [x] Una partida completa hasta ante 8 + modo infinito (24 rondas + 4 del infinito, 59.9 fps)

## 8. Distribución: web que genera el VPK en el navegador (sin datos de Balatro en nada que publiquemos)
- [x] El juego carga todos los recursos desde dentro del VPK generado, nada embebido (el eboot y la base de la web no
      contienen textos del original)
- [x] Biblioteca C++ "asset builder" que reemplaza `tools/build_assets.py`: leer el zip del `Balatro.exe` (LOVE fusionado)
      o del `.apk`, verificar versión (1.0.1o), extraer texturas/sonidos, datos con Lua 5.1 embebido (`extract_data.lua`),
      fuentes como LOVE (`bake_fonts.py`), iconos de botones Vita (`vita_pips.py`)
- [x] Compilar esa biblioteca a WebAssembly (Emscripten) + herramienta de línea de comandos para PC (mismo resultado byte a byte)
- [x] Armar el VPK en el navegador: zip con nuestro `eboot.bin`, `sce_sys`, shaders compilados y los recursos generados
- [ ] Shaders: reescribir los que aún son conversión directa del original (fórmulas propias equivalentes, verificadas),
      y publicar solo nuestros `.gxp` compilados
- [x] Página web estática (GitHub Pages): elegir archivo, progreso, descarga; todo local, nada se sube
- [ ] Verificar que el VPK generado por la web es idéntico al que genera el build local y funciona en la Vita
      (idéntico byte a byte al nativo con el APK; con el `Balatro.exe` de Steam se genera y abre en Vita3K;
      falta probarlo en la Vita real)
- [ ] `sce_sys` (icono, fondo y logo del LiveArea) es arte de Balatro: quitarlo del repo y de su historial antes de publicarlo
      (la web ya genera los suyos a partir de los archivos del usuario)
- [x] Datos deterministas: `build/assets_*/data` generados con `vl_asset_builder` (dos ejecuciones idénticas byte a
      byte; `jokerbatch` completo pasa). Los empates de orden en los pools (Edition, Tarot_Planet, Consumeables) no
      afectan al azar: Tarot_Planet solo se usa con clave forzada (El Loco) y la colección de ediciones usa una lista fija
- [x] Copia de Steam: el `Balatro.exe` (1.0.1o) genera el VPK en la web y abre en Vita3K; en PC con sus datos
      menú, ciegas, mano y vista de baraja cargan sin errores
- [ ] Recompilar el eboot de la Vita antes de armar el sitio (el de `build/vita` es viejo) y publicar en GitHub Pages
- [ ] Revisar que nada publicado (repo, web, eboot) contiene recursos o textos de Balatro

## Fuera de alcance (del original, a propósito)
Puntuaciones online, Discord/Twitter/Steam/linktree, encuestas y CTA de la demo, retos diarios
(`daily_overview`), resolución/ventana/vsync de PC, herramientas de depuración (`DT_*`, sandbox,
test framework), informes de errores, logros de Steam (`unlockAchievement`), copias en la nube de Android.
