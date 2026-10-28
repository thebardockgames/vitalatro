// The user's copy of Balatro as a set of files: a zip in memory (the Android .apk, the
// LOVE-fused Balatro.exe = an exe with the game's zip appended) or, for the native CLI, a
// directory with the extracted game.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace vlab {

using Bytes = std::vector<uint8_t>;

class Archive {
public:
    virtual ~Archive() = default;
    // all files (relative, '/'-separated), sorted
    virtual const std::vector<std::string> &names() const = 0;
    virtual bool read(const std::string &name, Bytes &out) const = 0;
    bool exists(const std::string &name) const;
};

// A zip read from memory; `base` is found from the end of central directory, so data
// prepended to the zip (an exe) is skipped. Returns nullptr (and the reason) if not a zip.
std::unique_ptr<Archive> open_zip(std::shared_ptr<const Bytes> data, std::string &error);
#ifndef __EMSCRIPTEN__
std::unique_ptr<Archive> open_dir(const std::string &path, std::string &error);
#endif

// The game inside an archive: where its files are (the zip root for Balatro.exe, assets/ in
// an .apk, or a game.love inside either) and its version.
class Game {
public:
    std::string kind;      // "exe", "apk", "dir"
    std::string version;   // e.g. "1.0.1o"
    std::string build;     // version.jkr's third line: PROD_mobile, PROD_PC_Console...
    bool read(const std::string &rel, Bytes &out) const;
    bool exists(const std::string &rel) const;
    std::string text(const std::string &rel) const;  // throws if missing
    // files directly in a directory of the game (no subdirectories), sorted
    std::vector<std::string> list(const std::string &dir, const std::string &suffix, bool recursive) const;

    std::unique_ptr<Archive> archive;
    std::unique_ptr<Archive> inner;  // a game.love inside the archive
    std::string prefix;
};

// Finds the game in the file; on failure a message for the user (Spanish).
std::unique_ptr<Game> open_game(std::shared_ptr<const Bytes> data, const std::string &file_name, std::string &error);
#ifndef __EMSCRIPTEN__
std::unique_ptr<Game> open_game_path(const std::string &path, std::string &error);
#endif

}  // namespace vlab
