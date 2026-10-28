# Vitalatro: lo que falta

Auditoría completa contra el código original (todas las funciones de los .lua, `G.FUNCS`, `G.UIDEF`,
tipos de desbloqueo, centros, sonidos, shaders y los `TODO` del código). Se marca con `[x]` al terminar.

## 1. Rendimiento en la Vita
- [ ] Vista de baraja a 60 fps (estaba en 43–46; VPK con optimizaciones subido, falta medir)
- [ ] Medir en la Vita cada pantalla pesada: colección (todas las páginas), info de partida, lista de retos,
      configuración de partida, game over / victoria, tienda con sobres abiertos
- [ ] Coste por caja de la UI (~26 µs por caja en la Vita): seguir reduciendo copias de tablas en `draw_self`

## 2. Tutorial
- [ ] `tutorial_controller` / `tutorial_part` / `tutorial_info` (state_events.lua): los pasos con Jimbo
- [ ] `G.OVERLAY_TUTORIAL` (Jimbo, globo de texto, botón para avanzar) y su borrado en `delete_run`
- [ ] Resaltados del tutorial (`OVERLAY_TUTORIAL.highlights`) en el dibujo
- [ ] El foco del mando ignora a Jimbo (`node.jimbo` en el controlador)
- [ ] `skip_tutorial_section` (saltar tutorial) y `start_tutorial`
- [ ] Semilla `TUTORIAL` y su mazo/cartas forzadas en la primera partida

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
- [ ] Baraja Anaglyph: Double Tag al vencer a un jefe (`Back:trigger_effect` context `eval`)
- [ ] Aviso del jefe "La mano no puntuará" + texto del jefe sobre la zona de juego (`boss_warning_text`)
- [ ] Stake dorada: búsqueda de semilla que evita leyendas ya ganadas (`get_first_legendary`)
- [ ] Stake dorada: brillo animado del chip en `get_stake_sprite`
- [ ] Música de sobres: `music2` (Arcana/Spectral) y `music3` (Celestial)
- [ ] Menú de pausa: fila de la semilla en partidas con semilla (`current_seed`)
- [ ] Reconstruir `G.buttons` al cambiar ajustes durante una partida (`create_UIBox_buttons`)
- [ ] Reinicio rápido (mantener R; solo teclado, opcional en Vita)
- [ ] Quitar el stub muerto de `can_use_consumeable` en state_events.cpp (el real está en consumables.cpp)

## 5. Fidelidad visual
- [ ] Vista de baraja: pequeña diferencia de inclinación por carta (ambient tilt / IDs)
- [ ] Mipmaps manuales para texturas (`TODO(manual-mips)`), cartas pequeñas igual que en PC
- [ ] Fondo: campo k=1 en núcleos de CPU en vez de k=2 a media tasa (decisión pendiente)
- [ ] Revisar los 15 idiomas en pantalla (fuentes CJK: SC/TC están a tamaño 47)

## 6. Regresión completa contra el original
- [ ] `jokerbatch` (todos los jokers)
- [ ] `tagbatch` (todas las etiquetas)
- [ ] `bossbatch` (los 28 jefes)
- [ ] Consumibles: todos los tarots, planetas y espectrales
- [ ] Vouchers: los 32
- [ ] Barajas: las 15 y las 8 stakes
- [ ] Retos: los 20 (inicio y reglas)
- [ ] Sobres: los 5 tipos con sus variantes

## 7. Verificar en la Vita contigo
- [ ] Guardar y continuar partida (tienda, mitad de mano, sobre abierto)
- [ ] Ya no hay tirón al descartar contra un jefe
- [ ] Salir con el botón PS / suspender y volver (audio, guardado)
- [ ] Teclado en pantalla para la semilla y nombres
- [ ] Una partida completa hasta ante 8 + modo infinito

## Fuera de alcance (del original, a propósito)
Puntuaciones online, Discord/Twitter/Steam/linktree, encuestas y CTA de la demo, retos diarios
(`daily_overview`), resolución/ventana/vsync de PC, herramientas de depuración (`DT_*`, sandbox,
test framework), informes de errores, logros de Steam (`unlockAchievement`), copias en la nube de Android.
