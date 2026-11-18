/* oil's icon column: mini.icons' glyph for a name (icons.inc, generated
   from mini.icons and Neovim's filetype tables).  Neovim's filetype
   patterns (vim.filetype.match on a full path) are not in the table. */
#include <stdlib.h>
#include <string.h>
#include "oil.h"

typedef struct { const char *k, *icon; } Icon;
#include "icons.inc"

/* k is n bytes, not terminated */
static const char *get(const Icon *t, size_t tn, const char *k, size_t n)
{
	size_t lo = 0, hi = tn;
	while (lo < hi) {
		size_t m = (lo + hi) / 2;
		int c = strncmp(k, t[m].k, n);
		if (!c && t[m].k[n]) c = -1;
		if (!c) return t[m].icon;
		if (c < 0) hi = m; else lo = m + 1;
	}
	return NULL;
}
#define GET(t, k, n) get(t, sizeof t / sizeof *t, k, n)

/* H.get_impl.directory and .file */
const char *oil_icon(int type, const char *name)
{
	/* H.fs_basename: drop one trailing "/", then fnamemodify's :t */
	size_t n = strlen(name), b;
	int slash = n && name[n - 1] == '/';
	n -= (size_t)slash;
	for (b = n; b && name[b - 1] != '/'; b--) ;
	const char *base = name + b, *r;
	n -= b;
	if (type == OIL_DIR) return (r = GET(dir_icons, base, n)) ? r : icon_dir;
	if ((r = GET(file_icons, base, n))) return r;
	/* vim.filetype.match on the whole name, which "name/" never matches */
	if (!slash && (r = GET(ft_icons, base, n))) return r;
	/* the extension after the first dot past the first byte, lowered; then
	   its shorter parts (a.tar.gz: tar.gz, then gz) */
	const char *dot = n ? memchr(base + 1, '.', n - 1) : NULL;
	if (dot && dot + 1 < base + n) {
		char ext[256];
		size_t i = 0;
		for (const char *s = dot + 1; s < base + n && i + 1 < sizeof ext; s++) ext[i++] = (char)(*s >= 'A' && *s <= 'Z' ? *s + 32 : *s);
		ext[i] = 0;
		for (char *e = ext; e; e = strchr(e, '.') ? strchr(e, '.') + 1 : NULL)
			if (*e && (r = GET(ext_icons, e, strlen(e)))) return r;
	}
	return icon_file;
}
