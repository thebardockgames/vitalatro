// The Vitalatro asset builder: from the user's own copy of Balatro to the files the Vita build
// reads (what tools/build_assets.py makes for the "vita" target, minus the shaders, which are
// ours and ship prebuilt), and the VPK around them.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace vlab {

struct OutFile {
    std::string path;
    std::vector<uint8_t> data;
};

class Game;

// The only version Vitalatro's data matches.
extern const char *const SUPPORTED_VERSION;

// "" when the game can be used, else the reason (Spanish, shown to the user).
std::string check_game(const Game &game);

using Progress = std::function<void(double fraction, const std::string &label)>;

// textures/, sounds/, data/, fonts/ and our sce_sys images, sorted by path.
// with_sce_sys false: only what tools/build_assets.py makes (a drop-in for build/assets_vita).
std::vector<OutFile> build_assets(const Game &game, const Progress &progress, bool with_sce_sys = true);

// A VPK (zip) with the base package (eboot.bin, sce_sys/param.sfo, livearea template,
// shaders/*.gxp) and the built files. Deflate level 6 except for already compressed formats
// (png, ogg), which are stored.
std::vector<uint8_t> make_vpk(const std::vector<OutFile> &base, const std::vector<OutFile> &assets);

}  // namespace vlab
