#pragma once

#include <functional>
#include <map>
#include <string>

#include "archive.h"

namespace vlab {

// Runs tools/extract_data.lua (embedded) on the game's Lua files with the builder's Lua 5.1
// whose tables behave like LuaJIT's (lua51/ltable.c): returns data/<name>.lua contents by name.
std::map<std::string, std::string> extract_data(const Game &game, const std::function<void(const std::string &)> &log);

}  // namespace vlab
