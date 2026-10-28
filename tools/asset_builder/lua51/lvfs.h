/*
** Virtual files for the Vitalatro asset builder: Lua reads the game's files from the archive
** and writes its outputs to memory (implemented by the builder, see src/lua_data.cpp).
*/
#ifndef lvfs_h
#define lvfs_h

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 1 and the file's bytes (valid while the build runs) if it exists. */
int vl_vfs_read (const char *name, const char **data, size_t *size);
void vl_vfs_write (const char *name, const char *data, size_t size);
/* print() and io.write() output */
void vl_vfs_print (const char *text, size_t size);

#ifdef __cplusplus
}
#endif

#endif
