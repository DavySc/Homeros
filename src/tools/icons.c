/* oil's icon column: mini.icons' glyph for a name (icons.inc, generated
   from mini.icons and Neovim's filetype tables).  Neovim's filetype
   patterns (vim.filetype.match on a full path) are not in the table. */
#include <stdlib.h>
#include <string.h>
#include "oil.h"

typedef struct { const char *k, *icon; } Icon;
#include "icons.inc"

static const char *get(const Icon *t, size_t n, const char *k)
{
	size_t lo = 0, hi = n;
	while (lo < hi) {
		size_t m = (lo + hi) / 2;
		int c = strcmp(k, t[m].k);
		if (!c) return t[m].icon;
		if (c < 0) hi = m; else lo = m + 1;
	}
	return NULL;
}
#define GET(t, k) get(t, sizeof t / sizeof *t, k)

/* H.get_impl.directory and .file */
const char *oil_icon(int type, const char *name)
{
	const char *base = strrchr(name, '/'), *r;
	base = base && base[1] ? base + 1 : name;
	if (type == OIL_DIR) return (r = GET(dir_icons, base)) ? r : icon_dir;
	if ((r = GET(file_icons, base))) return r;
	/* the extension after the first dot past the first byte, lowered; then
	   its shorter parts (a.tar.gz: tar.gz, then gz) */
	const char *dot = base[0] ? strchr(base + 1, '.') : NULL;
	if (dot && dot[1]) {
		char ext[256];
		size_t i = 0;
		for (const char *s = dot + 1; *s && i + 1 < sizeof ext; s++) ext[i++] = (char)(*s >= 'A' && *s <= 'Z' ? *s + 32 : *s);
		ext[i] = 0;
		for (char *e = ext; e; e = strchr(e, '.') ? strchr(e, '.') + 1 : NULL)
			if (*e && (r = GET(ext_icons, e))) return r;
	}
	return icon_file;
}
