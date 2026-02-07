#ifndef UAE_ZFILE_H
#define UAE_ZFILE_H

#include "sysdeps.h"
#include "uae/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZFD_NORMAL 0

struct zfile {
	FILE *file;
	uae_u8 *data;
	uae_u64 size;
	uae_u64 pos;
	bool is_memory;
	bool owns_data;
	TCHAR *name;
};

struct zfile *zfile_fopen(const TCHAR *name, const TCHAR *mode, int mask);
struct zfile *zfile_fopen(const TCHAR *name, const TCHAR *mode);
struct zfile *zfile_fopen(const TCHAR *name, const TCHAR *mode, int mask, int index);
struct zfile *zfile_fopen_data(const TCHAR *name, uae_u64 size, const uae_u8 *data);
void zfile_fclose(struct zfile *z);
uae_s64 zfile_fseek(struct zfile *z, uae_s64 offset, int mode);
uae_s64 zfile_ftell(struct zfile *z);
uae_s32 zfile_ftell32(struct zfile *z);
uae_s64 zfile_size(struct zfile *z);
uae_s32 zfile_size32(struct zfile *z);
size_t zfile_fread(void *b, size_t l1, size_t l2, struct zfile *z);
uae_s32 zfile_fread32(void *b, size_t l1, size_t l2, struct zfile *z);
TCHAR *zfile_getname(struct zfile *z);
bool zfile_exists(const TCHAR *path);
uae_u8 *zfile_load_file(const TCHAR *path, int *outlen);
struct zfile *zfile_gunzip(struct zfile *z);

#ifdef __cplusplus
}
#endif

#endif
