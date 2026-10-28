/*
** A minimal io library over the builder's virtual files (replaces liolib.c):
** io.open(name, "r"|"rb") -> f:read("*a"|"*l"|n), f:lines(), f:close()
** io.open(name, "w"|"wb") -> f:write(...), f:close() (the file is stored on close)
** io.write(...) goes to the builder's log.
*/

#include <string.h>
#include <stdlib.h>

#define LUA_LIB

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "lvfs.h"

#define VFILE "VL_VFILE*"

typedef struct VFile {
  int writing, closed;
  char *name;
  const char *rdata;  /* reading */
  size_t rsize, pos;
  char *wdata;  /* writing */
  size_t wsize, wcap;
} VFile;

static VFile *tofile (lua_State *L) {
  VFile *f = (VFile *)luaL_checkudata(L, 1, VFILE);
  if (f->closed) luaL_error(L, "attempt to use a closed file");
  return f;
}

static int io_open (lua_State *L) {
  const char *name = luaL_checkstring(L, 1);
  const char *mode = luaL_optstring(L, 2, "r");
  VFile *f;
  int writing = mode[0] == 'w';
  const char *data = NULL;
  size_t size = 0;
  if (!writing && mode[0] != 'r') return luaL_argerror(L, 2, "invalid mode");
  if (!writing && !vl_vfs_read(name, &data, &size)) {
    lua_pushnil(L);
    lua_pushfstring(L, "%s: No such file or directory", name);
    return 2;
  }
  f = (VFile *)lua_newuserdata(L, sizeof(VFile));
  memset(f, 0, sizeof(*f));
  f->writing = writing;
  f->name = (char *)malloc(strlen(name) + 1);
  strcpy(f->name, name);
  f->rdata = data;
  f->rsize = size;
  luaL_getmetatable(L, VFILE);
  lua_setmetatable(L, -2);
  return 1;
}

static void putdata (VFile *f, const char *s, size_t l) {
  if (f->wsize + l > f->wcap) {
    f->wcap = (f->wsize + l) * 2 + 256;
    f->wdata = (char *)realloc(f->wdata, f->wcap);
  }
  memcpy(f->wdata + f->wsize, s, l);
  f->wsize += l;
}

static int f_write (lua_State *L) {
  VFile *f = tofile(L);
  int i, n = lua_gettop(L);
  if (!f->writing) return luaL_error(L, "file not open for writing");
  for (i = 2; i <= n; i++) {
    size_t l;
    const char *s = luaL_checklstring(L, i, &l);
    putdata(f, s, l);
  }
  lua_settop(L, 1);
  return 1;
}

static int readline (lua_State *L, VFile *f) {
  size_t start = f->pos;
  if (f->pos >= f->rsize) return 0;
  while (f->pos < f->rsize && f->rdata[f->pos] != '\n') f->pos++;
  lua_pushlstring(L, f->rdata + start, f->pos - start);
  if (f->pos < f->rsize) f->pos++;
  return 1;
}

static int f_read (lua_State *L) {
  VFile *f = tofile(L);
  if (f->writing) return luaL_error(L, "file not open for reading");
  if (lua_type(L, 2) == LUA_TNUMBER) {
    size_t n = (size_t)lua_tointeger(L, 2), left = f->rsize - f->pos;
    if (left == 0 && n > 0) { lua_pushnil(L); return 1; }
    if (n > left) n = left;
    lua_pushlstring(L, f->rdata + f->pos, n);
    f->pos += n;
    return 1;
  } else {
    const char *p = luaL_optstring(L, 2, "*l");
    if (p[0] == '*' && p[1] == 'a') {
      lua_pushlstring(L, f->rdata + f->pos, f->rsize - f->pos);
      f->pos = f->rsize;
      return 1;
    }
    if (p[0] == '*' && p[1] == 'l') {
      if (!readline(L, f)) lua_pushnil(L);
      return 1;
    }
    return luaL_argerror(L, 2, "invalid format");
  }
}

static int lines_iter (lua_State *L) {
  VFile *f = (VFile *)lua_touserdata(L, lua_upvalueindex(1));
  if (f->closed) return luaL_error(L, "file is already closed");
  return readline(L, f);
}

static int f_lines (lua_State *L) {
  tofile(L);
  lua_pushvalue(L, 1);
  lua_pushcclosure(L, lines_iter, 1);
  return 1;
}

static void doclose (VFile *f) {
  if (f->closed) return;
  f->closed = 1;
  if (f->writing) vl_vfs_write(f->name, f->wdata ? f->wdata : "", f->wsize);
  free(f->wdata);
  free(f->name);
  f->wdata = NULL;
  f->name = NULL;
}

static int f_close (lua_State *L) {
  doclose(tofile(L));
  lua_pushboolean(L, 1);
  return 1;
}

static int f_gc (lua_State *L) {
  VFile *f = (VFile *)luaL_checkudata(L, 1, VFILE);
  doclose(f);
  return 0;
}

static int io_write (lua_State *L) {
  int i, n = lua_gettop(L);
  for (i = 1; i <= n; i++) {
    size_t l;
    const char *s = luaL_checklstring(L, i, &l);
    vl_vfs_print(s, l);
  }
  return 0;
}

static const luaL_Reg flib[] = {
  {"write", f_write},
  {"read", f_read},
  {"lines", f_lines},
  {"close", f_close},
  {"__gc", f_gc},
  {NULL, NULL}
};

static const luaL_Reg iolib[] = {
  {"open", io_open},
  {"write", io_write},
  {NULL, NULL}
};

LUALIB_API int luaopen_io (lua_State *L) {
  luaL_newmetatable(L, VFILE);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  luaL_register(L, NULL, flib);
  lua_pop(L, 1);
  luaL_register(L, LUA_IOLIBNAME, iolib);
  return 1;
}
