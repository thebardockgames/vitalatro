// The WebAssembly module's C API (web/js/worker.js drives it):
//   vlab_open(ptr, size, name)  -> 0 / 1; vlab_info() is a JSON line about the file
//   vlab_build()                -> 0 / 1 (vlab_info() has the error), progress via Module.onProgress
//   vlab_base_add(path, ptr, size), vlab_make_vpk() -> 0 / 1; vlab_vpk_data()/vlab_vpk_size()
//   vlab_result_*: the built files one by one (for tests)
//   vlab_close()
#include <emscripten.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "archive.h"
#include "builder.h"

using namespace vlab;

EM_JS(void, js_progress, (double f, const char *label), {
    if (Module.onProgress) Module.onProgress(f, UTF8ToString(label));
});

namespace {
std::unique_ptr<Game> g_game;
std::vector<OutFile> g_files, g_base;
std::vector<uint8_t> g_vpk;
std::string g_info;

std::string json_str(const std::string &s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if ((unsigned char)c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += c;
    }
    return o + "\"";
}
}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE const char *vlab_info() { return g_info.c_str(); }

// Takes the file's bytes (malloc'd by the caller; freed here).
EMSCRIPTEN_KEEPALIVE int vlab_open(uint8_t *data, size_t size, const char *name) {
    g_game.reset();
    g_files.clear();
    auto bytes = std::make_shared<Bytes>(data, data + size);
    std::free(data);
    std::string err;
    try {
        g_game = open_game(bytes, name, err);
    } catch (const std::exception &e) {
        err = e.what();
    }
    if (!g_game) {
        g_info = "{\"ok\":false,\"error\":" + json_str(err) + "}";
        return 1;
    }
    err = check_game(*g_game);
    g_info = "{\"ok\":" + std::string(err.empty() ? "true" : "false") + ",\"kind\":" + json_str(g_game->kind) +
             ",\"version\":" + json_str(g_game->version) + ",\"build\":" + json_str(g_game->build) +
             (err.empty() ? "" : ",\"error\":" + json_str(err)) + "}";
    if (!err.empty()) { g_game.reset(); return 1; }
    return 0;
}

EMSCRIPTEN_KEEPALIVE int vlab_build() {
    if (!g_game) return 1;
    try {
        g_files = build_assets(*g_game, [](double f, const std::string &label) { js_progress(f, label.c_str()); });
    } catch (const std::exception &e) {
        g_info = "{\"ok\":false,\"error\":" + json_str(e.what()) + "}";
        return 1;
    }
    g_game.reset();  // the input is not needed any more
    return 0;
}

EMSCRIPTEN_KEEPALIVE int vlab_result_count() { return int(g_files.size()); }
EMSCRIPTEN_KEEPALIVE const char *vlab_result_path(int i) { return g_files[size_t(i)].path.c_str(); }
EMSCRIPTEN_KEEPALIVE const uint8_t *vlab_result_data(int i) { return g_files[size_t(i)].data.data(); }
EMSCRIPTEN_KEEPALIVE size_t vlab_result_size(int i) { return g_files[size_t(i)].data.size(); }

EMSCRIPTEN_KEEPALIVE void vlab_base_add(const char *path, uint8_t *data, size_t size) {
    g_base.push_back({path, std::vector<uint8_t>(data, data + size)});
    std::free(data);
}

EMSCRIPTEN_KEEPALIVE int vlab_make_vpk() {
    try {
        g_vpk = make_vpk(g_base, g_files);
    } catch (const std::exception &e) {
        g_info = "{\"ok\":false,\"error\":" + json_str(e.what()) + "}";
        return 1;
    }
    g_base.clear();
    g_files.clear();
    return 0;
}

EMSCRIPTEN_KEEPALIVE const uint8_t *vlab_vpk_data() { return g_vpk.data(); }
EMSCRIPTEN_KEEPALIVE size_t vlab_vpk_size() { return g_vpk.size(); }

EMSCRIPTEN_KEEPALIVE void vlab_close() {
    g_game.reset();
    g_files.clear();
    g_base.clear();
    std::vector<uint8_t>().swap(g_vpk);
}

}  // extern "C"
