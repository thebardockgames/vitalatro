# Vitalatro asset builder: native CLI (vl_asset_builder) and, with Emscripten, the WebAssembly
# module the web page runs (vl_asset_builder.js/.wasm). See build.sh.
cmake_minimum_required(VERSION 3.16)
project(vl_asset_builder C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
# identical floating point natively and in wasm (no fused multiply-add)
add_compile_options(-ffp-contract=off)

include(FetchContent)
if(POLICY CMP0169)
  cmake_policy(SET CMP0169 OLD)  # FetchContent_Populate + our own targets for zlib
endif()
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()
set(FETCHCONTENT_QUIET ON)

# zlib 1.3.1: the PNGs must deflate exactly as Pillow's (built with zlib 1.3.1) do
FetchContent_Declare(zlib
  URL https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
  URL_HASH SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23)
FetchContent_GetProperties(zlib)
if(NOT zlib_POPULATED)
  FetchContent_Populate(zlib)
endif()
add_library(vl_zlib STATIC
  ${zlib_SOURCE_DIR}/adler32.c ${zlib_SOURCE_DIR}/compress.c ${zlib_SOURCE_DIR}/crc32.c
  ${zlib_SOURCE_DIR}/deflate.c ${zlib_SOURCE_DIR}/infback.c ${zlib_SOURCE_DIR}/inffast.c
  ${zlib_SOURCE_DIR}/inflate.c ${zlib_SOURCE_DIR}/inftrees.c ${zlib_SOURCE_DIR}/trees.c
  ${zlib_SOURCE_DIR}/uncompr.c ${zlib_SOURCE_DIR}/zutil.c)
target_include_directories(vl_zlib PUBLIC ${zlib_SOURCE_DIR})
target_compile_definitions(vl_zlib PRIVATE HAVE_UNISTD_H)

# FreeType 2.14.2, default options, as the reference fonts were rasterized with
FetchContent_Declare(freetype
  URL https://download.savannah.gnu.org/releases/freetype/freetype-2.14.2.tar.gz
  URL_HASH SHA256=752c2671f85c54a84b7f0dd2b5cd26b6b741117033886ffbc5ac89a68464b848)
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
FetchContent_GetProperties(freetype)
if(NOT freetype_POPULATED)
  FetchContent_Populate(freetype)
  add_subdirectory(${freetype_SOURCE_DIR} ${freetype_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

# Lua 5.1.5 with LuaJIT's table semantics (lua51/ltable.c and friends)
file(GLOB LUA_SRC ${CMAKE_CURRENT_SOURCE_DIR}/lua51/*.c)
add_library(vl_lua STATIC ${LUA_SRC})
target_include_directories(vl_lua PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lua51)
target_compile_options(vl_lua PRIVATE -w)

# tools/extract_data.lua, embedded
set(EXTRACT_LUA ${CMAKE_CURRENT_SOURCE_DIR}/../extract_data.lua)
set(EXTRACT_H ${CMAKE_CURRENT_BINARY_DIR}/gen/extract_data_lua.h)
add_custom_command(OUTPUT ${EXTRACT_H}
  COMMAND ${CMAKE_COMMAND} -DIN=${EXTRACT_LUA} -DOUT=${EXTRACT_H} -P ${CMAKE_CURRENT_SOURCE_DIR}/embed.cmake
  DEPENDS ${EXTRACT_LUA} ${CMAKE_CURRENT_SOURCE_DIR}/embed.cmake)

set(CORE_SRC
  src/archive.cpp src/builder.cpp src/fonts.cpp src/livearea.cpp src/lua_data.cpp src/pips.cpp src/png_io.cpp
  ${EXTRACT_H})
add_library(vl_builder STATIC ${CORE_SRC})
target_include_directories(vl_builder PUBLIC src ${CMAKE_CURRENT_BINARY_DIR}/gen
  ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/stb)
target_link_libraries(vl_builder PUBLIC vl_lua vl_zlib freetype)

if(EMSCRIPTEN)
  add_executable(vl_asset_builder src/wasm_api.cpp)
  target_link_libraries(vl_asset_builder vl_builder)
  target_link_options(vl_asset_builder PRIVATE
    -sMODULARIZE=1 -sEXPORT_NAME=VLAssetBuilder -sENVIRONMENT=worker,node
    -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=4GB -sINITIAL_MEMORY=64MB -sSTACK_SIZE=4MB
    -sEXPORTED_FUNCTIONS=_malloc,_free
    -sEXPORTED_RUNTIME_METHODS=UTF8ToString,stringToNewUTF8,HEAPU8
    -sNO_EXIT_RUNTIME=1)
else()
  add_executable(vl_asset_builder src/cli.cpp)
  target_link_libraries(vl_asset_builder vl_builder)
endif()
