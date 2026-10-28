#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "archive.h"
#include "builder.h"

namespace vlab {

// bake_fonts.py: fonts/<id>.fnt + fonts/<id>_<page>.png for every font the game uses.
// data_files: the extracted data/*.lua (the loc_* files give each language's characters).
void bake_fonts(const Game &game, const std::map<std::string, std::string> &data_files, std::vector<OutFile> &out,
                const std::function<void(const std::string &, double)> &progress);

}  // namespace vlab
