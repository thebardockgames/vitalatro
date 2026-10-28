#include "archive.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include <zlib.h>

#ifndef __EMSCRIPTEN__
#include <filesystem>
#include <fstream>
#endif

namespace vlab {

bool Archive::exists(const std::string &name) const {
    const auto &n = names();
    return std::binary_search(n.begin(), n.end(), name);
}

namespace {

uint16_t rd16(const uint8_t *p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t rd32(const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

struct ZipEntry {
    uint64_t local_offset;
    uint32_t csize, usize;
    uint16_t method;
};

class ZipArchive : public Archive {
public:
    std::shared_ptr<const Bytes> data;
    std::map<std::string, ZipEntry> entries;
    std::vector<std::string> sorted;

    const std::vector<std::string> &names() const override { return sorted; }

    bool read(const std::string &name, Bytes &out) const override {
        auto it = entries.find(name);
        if (it == entries.end()) return false;
        const ZipEntry &e = it->second;
        const Bytes &d = *data;
        if (e.local_offset + 30 > d.size()) return false;
        const uint8_t *lh = d.data() + e.local_offset;
        if (rd32(lh) != 0x04034b50) return false;
        uint64_t start = e.local_offset + 30 + rd16(lh + 26) + rd16(lh + 28);
        if (start + e.csize > d.size()) return false;
        const uint8_t *src = d.data() + start;
        out.resize(e.usize);
        if (e.method == 0) {
            if (e.csize != e.usize) return false;
            if (e.usize) std::memcpy(out.data(), src, e.usize);
            return true;
        }
        if (e.method != 8) return false;
        z_stream zs{};
        if (inflateInit2(&zs, -15) != Z_OK) return false;
        zs.next_in = const_cast<Bytef *>(src);
        zs.avail_in = e.csize;
        zs.next_out = out.data();
        zs.avail_out = e.usize;
        int r = inflate(&zs, Z_FINISH);
        bool ok = (r == Z_STREAM_END || (r == Z_BUF_ERROR && e.usize == 0)) && zs.total_out == e.usize;
        inflateEnd(&zs);
        return ok;
    }
};

#ifndef __EMSCRIPTEN__
class DirArchive : public Archive {
public:
    std::filesystem::path root;
    std::vector<std::string> sorted;
    const std::vector<std::string> &names() const override { return sorted; }
    bool read(const std::string &name, Bytes &out) const override {
        std::ifstream f(root / std::filesystem::u8path(name), std::ios::binary);
        if (!f) return false;
        out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        return true;
    }
};
#endif

}  // namespace

std::unique_ptr<Archive> open_zip(std::shared_ptr<const Bytes> data, std::string &error) {
    const Bytes &d = *data;
    if (d.size() < 22) { error = "no es un archivo zip"; return nullptr; }
    // end of central directory: the last one in the file (a comment can follow it)
    size_t lo = d.size() > 65557 ? d.size() - 65557 : 0;
    size_t eocd = SIZE_MAX;
    for (size_t p = d.size() - 22 + 1; p-- > lo;) {
        if (rd32(&d[p]) == 0x06054b50 && p + 22 + rd16(&d[p + 20]) <= d.size()) { eocd = p; break; }
    }
    if (eocd == SIZE_MAX) { error = "no contiene un zip"; return nullptr; }
    uint32_t count = rd16(&d[eocd + 10]);
    uint32_t cd_size = rd32(&d[eocd + 12]);
    uint32_t cd_off = rd32(&d[eocd + 16]);
    if (cd_size > eocd) { error = "zip dañado"; return nullptr; }
    // offsets are relative to the zip's start: anything before it (the exe) shifts them
    uint64_t cd_start = eocd - cd_size;
    if (cd_off > cd_start) { error = "zip dañado"; return nullptr; }
    uint64_t base = cd_start - cd_off;
    auto z = std::make_unique<ZipArchive>();
    z->data = data;
    uint64_t p = cd_start;
    for (uint32_t i = 0; i < count; i++) {
        if (p + 46 > eocd || rd32(&d[p]) != 0x02014b50) { error = "zip dañado"; return nullptr; }
        uint16_t method = rd16(&d[p + 10]);
        uint32_t csize = rd32(&d[p + 20]), usize = rd32(&d[p + 24]);
        uint16_t nlen = rd16(&d[p + 28]), xlen = rd16(&d[p + 30]), clen = rd16(&d[p + 32]);
        uint32_t loff = rd32(&d[p + 42]);
        if (p + 46 + nlen > eocd) { error = "zip dañado"; return nullptr; }
        std::string name(reinterpret_cast<const char *>(&d[p + 46]), nlen);
        std::replace(name.begin(), name.end(), '\\', '/');
        if (!name.empty() && name.back() != '/')
            z->entries[name] = ZipEntry{base + loff, csize, usize, method};
        p += 46 + nlen + xlen + clen;
    }
    for (auto &kv : z->entries) z->sorted.push_back(kv.first);
    return z;
}

#ifndef __EMSCRIPTEN__
std::unique_ptr<Archive> open_dir(const std::string &path, std::string &error) {
    namespace fs = std::filesystem;
    auto a = std::make_unique<DirArchive>();
    a->root = fs::u8path(path);
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(a->root, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (it->is_regular_file()) {
            std::string rel = fs::relative(it->path(), a->root).generic_u8string();
            a->sorted.push_back(rel);
        }
    }
    if (ec) { error = "cannot read " + path; return nullptr; }
    std::sort(a->sorted.begin(), a->sorted.end());
    return a;
}
#endif

bool Game::read(const std::string &rel, Bytes &out) const {
    const Archive *a = inner ? inner.get() : archive.get();
    return a->read(prefix + rel, out);
}

bool Game::exists(const std::string &rel) const {
    const Archive *a = inner ? inner.get() : archive.get();
    return a->exists(prefix + rel);
}

std::string Game::text(const std::string &rel) const {
    Bytes b;
    if (!read(rel, b)) throw std::runtime_error("falta " + rel + " en el juego");
    return std::string(b.begin(), b.end());
}

std::vector<std::string> Game::list(const std::string &dir, const std::string &suffix, bool recursive) const {
    const Archive *a = inner ? inner.get() : archive.get();
    std::string pre = prefix + (dir.empty() ? "" : dir + "/");
    std::vector<std::string> out;
    for (const auto &n : a->names()) {
        if (n.compare(0, pre.size(), pre) != 0) continue;
        std::string rest = n.substr(pre.size());
        if (!recursive && rest.find('/') != std::string::npos) continue;
        if (rest.size() < suffix.size() || rest.compare(rest.size() - suffix.size(), suffix.size(), suffix) != 0) continue;
        out.push_back(n.substr(prefix.size()));
    }
    return out;
}

static std::vector<std::string> split_lines(const std::string &s) {
    std::vector<std::string> lines;
    std::string cur;
    for (char c : s) {
        if (c == '\n') { lines.push_back(cur); cur.clear(); }
        else if (c != '\r') cur += c;
    }
    lines.push_back(cur);
    return lines;
}

static std::unique_ptr<Game> find_game(std::unique_ptr<Archive> a, const std::string &kind, std::string &error) {
    auto g = std::make_unique<Game>();
    g->kind = kind;
    // the game's root: where main.lua and version.jkr are (the shallowest one)
    std::string best;
    bool found = false;
    for (const auto &n : a->names()) {
        if (n.size() >= 11 && n.compare(n.size() - 11, 11, "version.jkr") == 0 &&
            (n.size() == 11 || n[n.size() - 12] == '/')) {
            std::string pre = n.substr(0, n.size() - 11);
            if (a->exists(pre + "main.lua") && (!found || pre.size() < best.size())) { best = pre; found = true; }
        }
    }
    if (!found) {
        // a game.love inside (some Android packages)
        for (const auto &n : a->names()) {
            if (n == "game.love" || (n.size() > 10 && n.compare(n.size() - 10, 10, "/game.love") == 0)) {
                Bytes love;
                if (!a->read(n, love)) break;
                std::string err;
                auto inner = open_zip(std::make_shared<Bytes>(std::move(love)), err);
                if (!inner) break;
                auto g2 = find_game(std::move(inner), kind, error);
                if (!g2) return nullptr;
                g2->inner = std::move(g2->archive);
                g2->archive = std::move(a);
                return g2;
            }
        }
        error = "No se encontraron los archivos de Balatro (falta version.jkr / main.lua)";
        return nullptr;
    }
    g->prefix = best;
    g->archive = std::move(a);
    std::vector<std::string> v = split_lines(g->text("version.jkr"));
    g->version = v.size() > 1 ? v[1] : "";
    g->build = v.size() > 2 ? v[2] : "";
    return g;
}

std::unique_ptr<Game> open_game(std::shared_ptr<const Bytes> data, const std::string &file_name, std::string &error) {
    std::string lower = file_name;
    for (auto &c : lower) c = char(tolower((unsigned char)c));
    std::string kind = lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".apk") == 0 ? "apk" : "exe";
    auto a = open_zip(std::move(data), error);
    if (!a) {
        error = "El archivo no es Balatro.exe ni un .apk de Balatro (" + error + ")";
        return nullptr;
    }
    return find_game(std::move(a), kind, error);
}

#ifndef __EMSCRIPTEN__
std::unique_ptr<Game> open_game_path(const std::string &path, std::string &error) {
    namespace fs = std::filesystem;
    if (fs::is_directory(fs::u8path(path))) {
        auto a = open_dir(path, error);
        if (!a) return nullptr;
        return find_game(std::move(a), "dir", error);
    }
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) { error = "cannot open " + path; return nullptr; }
    auto data = std::make_shared<Bytes>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return open_game(data, fs::u8path(path).filename().u8string(), error);
}
#endif

}  // namespace vlab
