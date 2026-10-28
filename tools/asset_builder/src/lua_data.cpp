#include "lua_data.h"

#include <cstring>
#include <stdexcept>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
#include "lvfs.h"
}

#include "extract_data_lua.h"  // generated: const char EXTRACT_DATA_LUA[], size_t EXTRACT_DATA_LUA_SIZE

namespace vlab {

namespace {

struct Ctx {
    const Game *game;
    std::map<std::string, Bytes> cache;  // files read (kept alive while Lua runs)
    std::map<std::string, std::string> out;
    std::string line;
    const std::function<void(const std::string &)> *log;
};
Ctx *ctx = nullptr;

const char GAME_DIR[] = "game";
const char OUT_DIR[] = "out";

}  // namespace

extern "C" int vl_vfs_read(const char *name, const char **data, size_t *size) {
    if (!ctx) return 0;
    std::string n = name;
    std::string pre = std::string(GAME_DIR) + "/";
    if (n.compare(0, pre.size(), pre) != 0) return 0;
    std::string rel = n.substr(pre.size());
    auto it = ctx->cache.find(rel);
    if (it == ctx->cache.end()) {
        Bytes b;
        if (!ctx->game->read(rel, b)) return 0;
        it = ctx->cache.emplace(rel, std::move(b)).first;
    }
    *data = reinterpret_cast<const char *>(it->second.data());
    *size = it->second.size();
    return 1;
}

extern "C" void vl_vfs_write(const char *name, const char *data, size_t size) {
    if (!ctx) return;
    std::string n = name;
    std::string pre = std::string(OUT_DIR) + "/";
    if (n.compare(0, pre.size(), pre) == 0) n = n.substr(pre.size());
    ctx->out[n] = std::string(data, size);
}

extern "C" void vl_vfs_print(const char *text, size_t size) {
    if (!ctx) return;
    for (size_t i = 0; i < size; i++) {
        if (text[i] == '\n') {
            if (*ctx->log) (*ctx->log)(ctx->line);
            ctx->line.clear();
        } else {
            ctx->line += text[i];
        }
    }
}

std::map<std::string, std::string> extract_data(const Game &game, const std::function<void(const std::string &)> &log) {
    Ctx c;
    c.game = &game;
    c.log = &log;
    ctx = &c;
    lua_State *L = luaL_newstate();
    if (!L) throw std::runtime_error("lua_newstate");
    luaL_openlibs(L);
    // arg as the standalone interpreter sets it: luajit extract_data.lua <game_dir> <out_dir>
    lua_createtable(L, 2, 1);
    lua_pushstring(L, "extract_data.lua");
    lua_rawseti(L, -2, 0);
    lua_pushstring(L, GAME_DIR);
    lua_rawseti(L, -2, 1);
    lua_pushstring(L, OUT_DIR);
    lua_rawseti(L, -2, 2);
    lua_setglobal(L, "arg");
    std::string err;
    if (luaL_loadbuffer(L, reinterpret_cast<const char *>(EXTRACT_DATA_LUA), EXTRACT_DATA_LUA_SIZE, "@extract_data.lua") != 0 ||
        lua_pcall(L, 0, 0, 0) != 0) {
        const char *m = lua_tostring(L, -1);
        err = m ? m : "error";
    }
    lua_close(L);
    ctx = nullptr;
    if (!err.empty()) throw std::runtime_error("extract_data.lua: " + err);
    return std::move(c.out);
}

}  // namespace vlab
