cmake_minimum_required(VERSION 3.16)

# VL_TARGET: vita (VitaSDK toolchain) or pc (headless Linux test build)
set(VL_TARGET "pc" CACHE STRING "vita or pc")
if(VL_TARGET STREQUAL "vita")
  if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain")
  endif()
endif()

project(Vitalatro CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Runtime data built from the user's copy of the game (tools/build_assets.py).
set(VL_ASSETS "${CMAKE_SOURCE_DIR}/build/assets_${VL_TARGET}" CACHE PATH "assets directory")

set(COMMON_SRC
  src/audio/mixer.cpp
  src/core/stb_impl.cpp
  src/core/value.cpp
  src/dev/devserver.cpp
  src/gfx/gfx.cpp
  src/gfx/shaders.cpp
  src/engine/controller.cpp
  src/engine/event.cpp
  src/engine/font.cpp
  src/engine/moveable.cpp
  src/engine/node.cpp
  src/engine/random.cpp
  src/engine/particles.cpp
  src/engine/sprite.cpp
  src/engine/text.cpp
  src/engine/ui.cpp
  src/game/button_callbacks.cpp
  src/game/blind.cpp
  src/game/card.cpp
  src/game/tag.cpp
  src/game/common_events.cpp
  src/game/g.cpp
  src/game/game.cpp
  src/game/input.cpp
  src/game/misc_functions.cpp
  src/game/render.cpp
  src/game/run.cpp
  src/game/save_manager.cpp
  src/game/sound.cpp
  src/game/state_events.cpp
  src/game/scoring.cpp
  src/game/ui_definitions.cpp
  src/game/ui_run.cpp
  src/m0/m0.cpp
)

if(VL_TARGET STREQUAL "vita")
  include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
  # VL_DEV: the dev build (dev server on port 7777, tools/vitactl.py), installed next to the game
  option(VL_DEV "dev server build" OFF)
  if(VL_DEV)
    set(VITA_APP_NAME "Vitalatro Dev")
    set(VITA_TITLEID  "VLTR00002")
  else()
    set(VITA_APP_NAME "Vitalatro")
    set(VITA_TITLEID  "VLTR00001")
  endif()
  set(VITA_VERSION  "00.01")

  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -mcpu=cortex-a9 -mfpu=neon -fno-exceptions -Wall")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -mcpu=cortex-a9 -mfpu=neon")

  add_executable(vitalatro ${COMMON_SRC}
    src/audio/output_vita.cpp
    src/gfx/gxm/backend_gxm.cpp
    src/platform/vita/platform_vita.cpp
    src/main_vita.cpp)
  target_include_directories(vitalatro PRIVATE src third_party)
  target_link_options(vitalatro PRIVATE -Wl,-T,${CMAKE_SOURCE_DIR}/src/platform/vita/vitalatro.ld)
  target_compile_definitions(vitalatro PRIVATE VL_VITA=1)
  if(VL_DEV)
    target_compile_definitions(vitalatro PRIVATE VL_DEV=1)
  endif()
  target_link_libraries(vitalatro
    SceGxm_stub SceDisplay_stub SceCtrl_stub SceTouch_stub SceAudio_stub
    ScePower_stub SceSysmodule_stub SceAppUtil_stub SceAppMgr_stub SceCommonDialog_stub
    SceNet_stub SceNetCtl_stub
    pthread m)

  vita_create_self(eboot.bin vitalatro UNSAFE)

  file(GLOB_RECURSE ASSET_FILES RELATIVE "${VL_ASSETS}" "${VL_ASSETS}/*")
  set(VPK_FILES)
  foreach(f ${ASSET_FILES})
    list(APPEND VPK_FILES FILE "${VL_ASSETS}/${f}" "${f}")
  endforeach()
  vita_create_vpk(vitalatro.vpk ${VITA_TITLEID} eboot.bin
    VERSION ${VITA_VERSION}
    NAME ${VITA_APP_NAME}
    FILE sce_sys/icon0.png sce_sys/icon0.png
    FILE sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
    FILE sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
    FILE sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
    ${VPK_FILES}
  )
else()
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -g -Wall")
  add_executable(vitalatro_pc ${COMMON_SRC}
    src/audio/output_pc.cpp
    src/gfx/gl/backend_gl.cpp
    src/platform/pc/platform_pc.cpp
    src/main_pc.cpp)
  target_include_directories(vitalatro_pc PRIVATE src third_party)
  target_compile_definitions(vitalatro_pc PRIVATE VL_PC=1)
  target_link_libraries(vitalatro_pc EGL OpenGL pthread m)
endif()
