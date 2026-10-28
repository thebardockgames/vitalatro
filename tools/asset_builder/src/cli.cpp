// vl_asset_builder <Balatro.exe | .apk | extracted game dir> <out_dir> [--vpk <file> --base <dir>]
// Writes the generated files under out_dir (the same tree as build/assets_vita minus shaders,
// plus sce_sys/), and optionally the whole VPK with the base package from <dir>
// (eboot.bin, sce_sys/param.sfo, sce_sys/livearea/contents/template.xml, shaders/*.gxp).
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "archive.h"
#include "builder.h"

namespace fs = std::filesystem;
using namespace vlab;

static void write_file(const fs::path &p, const std::vector<uint8_t> &d) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char *>(d.data()), std::streamsize(d.size()));
    if (!f) throw std::runtime_error("cannot write " + p.u8string());
}

int main(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <Balatro.exe|.apk|game dir> <out_dir> [--no-sce-sys] [--vpk file --base dir]\n",
                     argv[0]);
        return 2;
    }
    std::string in = argv[1], outdir = argv[2], vpk, base;
    bool sce_sys = true;  // --no-sce-sys: only build_assets.py's files (a drop-in for build/assets_vita)
    for (int i = 3; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--vpk" && i + 1 < argc) vpk = argv[++i];
        else if (a == "--base" && i + 1 < argc) base = argv[++i];
        else if (a == "--no-sce-sys") sce_sys = false;
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    try {
        auto t0 = std::chrono::steady_clock::now();
        std::string err;
        auto game = open_game_path(in, err);
        if (!game) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        std::printf("game: %s %s (%s), root '%s'\n", game->kind.c_str(), game->version.c_str(), game->build.c_str(),
                    game->prefix.c_str());
        err = check_game(*game);
        if (!err.empty()) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        auto files = build_assets(*game, [](double f, const std::string &label) {
            std::printf("[%3d%%] %s\n", int(f * 100), label.c_str());
            std::fflush(stdout);
        }, sce_sys);
        for (const auto &f : files) write_file(fs::u8path(outdir) / fs::u8path(f.path), f.data);
        std::printf("%zu files in %s\n", files.size(), outdir.c_str());
        if (!vpk.empty()) {
            std::vector<OutFile> basefiles;
            for (auto &e : fs::recursive_directory_iterator(fs::u8path(base))) {
                if (!e.is_regular_file()) continue;
                OutFile f;
                f.path = fs::relative(e.path(), fs::u8path(base)).generic_u8string();
                if (f.path == "manifest.json") continue;  // the site's list of the base files
                std::ifstream s(e.path(), std::ios::binary);
                f.data.assign(std::istreambuf_iterator<char>(s), std::istreambuf_iterator<char>());
                basefiles.push_back(std::move(f));
            }
            std::sort(basefiles.begin(), basefiles.end(), [](const OutFile &a, const OutFile &b) { return a.path < b.path; });
            write_file(fs::u8path(vpk), make_vpk(basefiles, files));
            std::printf("vpk: %s\n", vpk.c_str());
        }
        double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("done in %.1f s\n", s);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
