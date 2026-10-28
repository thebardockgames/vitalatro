# cmake -DIN=<file> -DOUT=<header> -P embed.cmake: the file as a C array
file(READ ${IN} HEX HEX)
string(LENGTH "${HEX}" N)
math(EXPR SIZE "${N} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," BODY "${HEX}")
string(REGEX REPLACE "(0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,)" "\\1\n" BODY "${BODY}")
get_filename_component(DIR ${OUT} DIRECTORY)
file(MAKE_DIRECTORY ${DIR})
file(WRITE ${OUT} "// generated from ${IN}\n#pragma once\n#include <cstddef>\nstatic const unsigned char EXTRACT_DATA_LUA[] = {\n${BODY}\n0};\nstatic const size_t EXTRACT_DATA_LUA_SIZE = ${SIZE};\n")
