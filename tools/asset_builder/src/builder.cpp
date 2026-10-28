#include "builder.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include <zlib.h>

#include "archive.h"
#include "fonts.h"
#include "livearea.h"
#include "lua_data.h"
#include "pips.h"
#include "png_io.h"

namespace vlab {

const char *const SUPPORTED_VERSION = "1.0.1o";

std::string check_game(const Game &game) {
    if (game.version != SUPPORTED_VERSION)
        return "esta copia es la versión " + (game.version.empty() ? std::string("(desconocida)") : game.version) +
               " de Balatro; Vitalatro necesita exactamente la " + SUPPORTED_VERSION + ".";
    for (const char *f : {"game.lua", "globals.lua", "functions/misc_functions.lua", "resources/textures/1x/Jokers.png",
                          "resources/fonts/m6x11plus.ttf"})
        if (!game.exists(f)) return std::string("a esta copia le falta ") + f + ".";
    return "";
}

std::vector<OutFile> build_assets(const Game &game, const Progress &progress) {
    std::vector<OutFile> out;
    auto step = [&](double f, const std::string &label) {
        if (progress) progress(f, label);
    };

    // textures (the Vita build uses the 1x set), the button pips redrawn for the Vita
    step(0.0, "Texturas");
    for (const auto &rel : game.list("resources/textures/1x", ".png", true)) {
        OutFile f;
        f.path = "textures/1x/" + rel.substr(std::strlen("resources/textures/1x/"));
        if (!game.read(rel, f.data)) throw std::runtime_error("no se pudo leer " + rel);
        if (f.path == "textures/1x/gamepad_ui.png") {
            Image im;
            if (!png_decode_rgba(f.data, im)) throw std::runtime_error("gamepad_ui.png ilegible");
            vita_pips(im);
            f.data = png_encode_pil(im, false);
        }
        out.push_back(std::move(f));
    }

    // sounds, and their list (love.filesystem.getDirectoryItems at runtime)
    step(0.04, "Sonidos");
    std::string index;
    for (const auto &rel : game.list("resources/sounds", ".ogg", false)) {
        OutFile f;
        std::string name = rel.substr(std::strlen("resources/sounds/"));
        f.path = "sounds/" + name;
        if (!game.read(rel, f.data)) throw std::runtime_error("no se pudo leer " + rel);
        out.push_back(std::move(f));
        index += name.substr(0, name.size() - 4) + "\n";
    }
    out.push_back({"sounds/index.txt", std::vector<uint8_t>(index.begin(), index.end())});

    // game data tables (prototypes, localization...) from the game's Lua files
    step(0.08, "Datos del juego");
    auto data = extract_data(game, [](const std::string &) {});
    for (const auto &kv : data) out.push_back({"data/" + kv.first, std::vector<uint8_t>(kv.second.begin(), kv.second.end())});

    // fonts rasterized like LOVE; the TTF itself only for the M0 diagnostics overlay
    step(0.2, "Fuentes");
    bake_fonts(game, data, out, [&](const std::string &id, double f) { step(0.2 + 0.72 * f, "Fuentes (" + id + ")"); });
    {
        OutFile f;
        f.path = "fonts/m6x11plus.ttf";
        if (!game.read("resources/fonts/m6x11plus.ttf", f.data)) throw std::runtime_error("falta m6x11plus.ttf");
        out.push_back(std::move(f));
    }

    // the home screen bubble and LiveArea, drawn from the user's own textures
    step(0.93, "Icono y LiveArea");
    make_livearea(game, out);

    std::sort(out.begin(), out.end(), [](const OutFile &a, const OutFile &b) { return a.path < b.path; });
    step(1.0, "Recursos listos");
    return out;
}

namespace {

void le16(std::vector<uint8_t> &o, uint32_t v) { o.push_back(uint8_t(v)); o.push_back(uint8_t(v >> 8)); }
void le32(std::vector<uint8_t> &o, uint32_t v) { le16(o, v & 0xffff); le16(o, v >> 16); }

bool stored_type(const std::string &p) {
    auto ends = [&](const char *s) {
        size_t n = std::strlen(s);
        return p.size() >= n && p.compare(p.size() - n, n, s) == 0;
    };
    return ends(".png") || ends(".ogg");
}

}  // namespace

std::vector<uint8_t> make_vpk(const std::vector<OutFile> &base, const std::vector<OutFile> &assets) {
    std::vector<const OutFile *> files;
    for (const auto &f : base) files.push_back(&f);
    for (const auto &f : assets) files.push_back(&f);
    std::vector<uint8_t> zip, cd;
    const uint16_t dos_time = 0, dos_date = uint16_t(((2024 - 1980) << 9) | (1 << 5) | 1);  // fixed: reproducible
    for (const OutFile *f : files) {
        uint32_t crc = uint32_t(crc32(0, f->data.data(), uInt(f->data.size())));
        std::vector<uint8_t> comp;
        uint16_t method = 0;
        if (!stored_type(f->path) && !f->data.empty()) {
            z_stream zs{};
            deflateInit2(&zs, 6, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
            comp.resize(deflateBound(&zs, uLong(f->data.size())));
            zs.next_in = const_cast<Bytef *>(f->data.data());
            zs.avail_in = uInt(f->data.size());
            zs.next_out = comp.data();
            zs.avail_out = uInt(comp.size());
            if (deflate(&zs, Z_FINISH) != Z_STREAM_END) throw std::runtime_error("deflate");
            comp.resize(zs.total_out);
            deflateEnd(&zs);
            if (comp.size() < f->data.size()) method = 8;
        }
        const std::vector<uint8_t> &payload = method == 8 ? comp : f->data;
        uint32_t offset = uint32_t(zip.size());
        le32(zip, 0x04034b50);
        le16(zip, 20); le16(zip, 0x0800 /* UTF-8 names */); le16(zip, method);
        le16(zip, dos_time); le16(zip, dos_date);
        le32(zip, crc); le32(zip, uint32_t(payload.size())); le32(zip, uint32_t(f->data.size()));
        le16(zip, uint16_t(f->path.size())); le16(zip, 0);
        zip.insert(zip.end(), f->path.begin(), f->path.end());
        zip.insert(zip.end(), payload.begin(), payload.end());

        le32(cd, 0x02014b50);
        le16(cd, 20); le16(cd, 20); le16(cd, 0x0800); le16(cd, method);
        le16(cd, dos_time); le16(cd, dos_date);
        le32(cd, crc); le32(cd, uint32_t(payload.size())); le32(cd, uint32_t(f->data.size()));
        le16(cd, uint16_t(f->path.size())); le16(cd, 0); le16(cd, 0); le16(cd, 0); le16(cd, 0);
        le32(cd, 0); le32(cd, offset);
        cd.insert(cd.end(), f->path.begin(), f->path.end());
    }
    uint32_t cd_off = uint32_t(zip.size());
    zip.insert(zip.end(), cd.begin(), cd.end());
    le32(zip, 0x06054b50);
    le16(zip, 0); le16(zip, 0);
    le16(zip, uint16_t(files.size())); le16(zip, uint16_t(files.size()));
    le32(zip, uint32_t(cd.size())); le32(zip, cd_off);
    le16(zip, 0);
    return zip;
}

}  // namespace vlab
