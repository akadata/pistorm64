#include "sysconfig.h"
#include "sysdeps.h"

#include "zfile.h"

#include <sys/stat.h>

static TCHAR *zfile_strdup(const TCHAR *name)
{
	if (!name) {
		return nullptr;
	}
	const size_t len = strlen(name) + 1;
	auto *copy = xmalloc(TCHAR, len);
	memcpy(copy, name, len);
	return copy;
}

static struct zfile *zfile_create(const TCHAR *name)
{
	auto *z = xmalloc(struct zfile, 1);
	memset(z, 0, sizeof(*z));
	z->name = zfile_strdup(name);
	return z;
}

struct zfile *zfile_fopen(const TCHAR *name, const TCHAR *mode, int mask)
{
	(void)mask;
	FILE *file = fopen(name, mode);
	if (!file) {
		return nullptr;
	}
	struct zfile *z = zfile_create(name);
	z->file = file;
	z->is_memory = false;
	return z;
}

struct zfile *zfile_fopen(const TCHAR *name, const TCHAR *mode)
{
	return zfile_fopen(name, mode, ZFD_NORMAL);
}

struct zfile *zfile_fopen(const TCHAR *name, const TCHAR *mode, int mask, int index)
{
	(void)index;
	return zfile_fopen(name, mode, mask);
}

struct zfile *zfile_fopen_data(const TCHAR *name, uae_u64 size, const uae_u8 *data)
{
	if (!data || size == 0) {
		return nullptr;
	}
	struct zfile *z = zfile_create(name);
	z->data = const_cast<uae_u8 *>(data);
	z->size = size;
	z->pos = 0;
	z->is_memory = true;
	z->owns_data = false;
	return z;
}

void zfile_fclose(struct zfile *z)
{
	if (!z) {
		return;
	}
	if (z->file) {
		fclose(z->file);
		z->file = nullptr;
	}
	if (z->owns_data && z->data) {
		xfree(z->data);
	}
	xfree(z->name);
	xfree(z);
}

static uae_s64 zfile_seek_memory(struct zfile *z, uae_s64 offset, int mode)
{
	uae_s64 base = 0;
	switch (mode) {
	case SEEK_CUR:
		base = static_cast<uae_s64>(z->pos);
		break;
	case SEEK_END:
		base = static_cast<uae_s64>(z->size);
		break;
	case SEEK_SET:
	default:
		base = 0;
		break;
	}
	uae_s64 next = base + offset;
	if (next < 0) {
		next = 0;
	}
	if (static_cast<uae_u64>(next) > z->size) {
		next = static_cast<uae_s64>(z->size);
	}
	z->pos = static_cast<uae_u64>(next);
	return next;
}

uae_s64 zfile_fseek(struct zfile *z, uae_s64 offset, int mode)
{
	if (!z) {
		return -1;
	}
	if (z->is_memory) {
		return zfile_seek_memory(z, offset, mode);
	}
#if defined(_WIN32)
	return _fseeki64(z->file, offset, mode);
#else
	return fseeko(z->file, offset, mode);
#endif
}

uae_s64 zfile_ftell(struct zfile *z)
{
	if (!z) {
		return 0;
	}
	if (z->is_memory) {
		return static_cast<uae_s64>(z->pos);
	}
#if defined(_WIN32)
	return _ftelli64(z->file);
#else
	return ftello(z->file);
#endif
}

uae_s32 zfile_ftell32(struct zfile *z)
{
	return static_cast<uae_s32>(zfile_ftell(z));
}

uae_s64 zfile_size(struct zfile *z)
{
	if (!z) {
		return 0;
	}
	if (z->is_memory) {
		return static_cast<uae_s64>(z->size);
	}
	const uae_s64 current = zfile_ftell(z);
	zfile_fseek(z, 0, SEEK_END);
	const uae_s64 endpos = zfile_ftell(z);
	zfile_fseek(z, current, SEEK_SET);
	return endpos;
}

uae_s32 zfile_size32(struct zfile *z)
{
	return static_cast<uae_s32>(zfile_size(z));
}

size_t zfile_fread(void *b, size_t l1, size_t l2, struct zfile *z)
{
	if (!z) {
		return 0;
	}
	if (!z->is_memory) {
		return fread(b, l1, l2, z->file);
	}
	const uae_u64 bytes = static_cast<uae_u64>(l1) * static_cast<uae_u64>(l2);
	const uae_u64 remaining = (z->pos < z->size) ? (z->size - z->pos) : 0;
	const uae_u64 to_copy = (bytes < remaining) ? bytes : remaining;
	if (to_copy == 0) {
		return 0;
	}
	memcpy(b, z->data + z->pos, static_cast<size_t>(to_copy));
	z->pos += to_copy;
	return static_cast<size_t>(to_copy / l1);
}

uae_s32 zfile_fread32(void *b, size_t l1, size_t l2, struct zfile *z)
{
	return static_cast<uae_s32>(zfile_fread(b, l1, l2, z));
}

TCHAR *zfile_getname(struct zfile *z)
{
	if (!z || !z->name) {
		return const_cast<TCHAR *>("");
	}
	return z->name;
}

bool zfile_exists(const TCHAR *path)
{
	if (!path || !*path) {
		return false;
	}
	struct stat st;
	return stat(path, &st) == 0;
}

uae_u8 *zfile_load_file(const TCHAR *path, int *outlen)
{
	if (outlen) {
		*outlen = 0;
	}
	FILE *file = fopen(path, "rb");
	if (!file) {
		return nullptr;
	}
	if (fseek(file, 0, SEEK_END) != 0) {
		fclose(file);
		return nullptr;
	}
	long len = ftell(file);
	if (len <= 0) {
		fclose(file);
		return nullptr;
	}
	if (fseek(file, 0, SEEK_SET) != 0) {
		fclose(file);
		return nullptr;
	}
	auto *buffer = xmalloc(uae_u8, static_cast<size_t>(len));
	const size_t read = fread(buffer, 1, static_cast<size_t>(len), file);
	fclose(file);
	if (read != static_cast<size_t>(len)) {
		xfree(buffer);
		return nullptr;
	}
	if (outlen) {
		*outlen = static_cast<int>(len);
	}
	return buffer;
}

struct zfile *zfile_gunzip(struct zfile *z)
{
	return z;
}
