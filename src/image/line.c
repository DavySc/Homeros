/* image_line (image.h): which image a line of a note shows below it.  The
   core asks for every line it counts rows of, many times a frame, so the
   answer is cached by a hash of the line's bytes and the box: an edit
   makes a new key and needs no hook, and a frame stats no file after the
   first.  An image file changed on disk with its line unchanged keeps the
   old picture until the line changes or the file is reopened, as Org's
   inline images (org-redisplay-inline-images). */
#include <string.h>
#include "image.h"
#include "syntax.h"

/* latex.c's: FNV-1a over p[0, n) from h; counts LaTeX jobs ended */
unsigned long long image_fnv(unsigned long long h, const void *p, size_t n);
extern unsigned image_latex_ends;
/* image.c's: the generation of image n's slot, which a reused slot changes */
unsigned char image_gen(int n);

enum { LINE = 4096, CACHE = 1024 };
static struct { unsigned long long key; int n; unsigned char gen; unsigned ends; } cache[CACHE];

static int ends_with(const char *s, size_t n, const char *x)
{
	size_t k = strlen(x);
	if (n < k) return 0;
	for (size_t i = 0; i < k; i++)
		if ((s[n - k + i] | 0x20) != x[i]) return 0;
	return 1;
}

static int image_ext(const char *s, size_t n)
{
	return ends_with(s, n, ".png") || ends_with(s, n, ".jpg") || ends_with(s, n, ".jpeg");
}

/* the first of a, b in s[0, n): its start, *len its length; NULL if none */
static const char *find(const char *s, size_t n, const char *a, size_t *len)
{
	size_t k = strlen(a);
	for (size_t i = 0; i + k <= n; i++)
		if (!memcmp(s + i, a, k)) { *len = k; return s + i; }
	return NULL;
}

/* The link target or the fragment in line b[0, n): *tex set for a LaTeX
   fragment.  Markdown: ![alt](target), a path not a URL.  Org
   (org-display-inline-images, its default: links without a description):
   [[file:x.png]] or [[./x.png]], and display math \[...\] or $$...$$ on
   one line; inline math stays text, since the image goes below the line,
   not in place of the fragment as Org's overlay does. */
static const char *target(const char *b, size_t n, int org, size_t *len, int *tex)
{
	const char *p, *q;
	size_t k;
	*tex = 0;
	if (!org) {
		if (!(p = find(b, n, "![", &k)) || !(q = find(p, (size_t)(b + n - p), "](", &k))) return NULL;
		p = q + 2;
		for (q = p; q < b + n && *q != ')' && *q != ' '; q++) {}
		*len = (size_t)(q - p);
		return q < b + n && *len && !find(p, *len, "://", &k) && image_ext(p, *len) ? p : NULL;
	}
	if ((p = find(b, n, "[[", &k))) {
		p += 2;
		if ((size_t)(b + n - p) > 5 && !memcmp(p, "file:", 5)) p += 5;
		else if (*p != '/' && *p != '.') p = NULL;
		if (p && (q = find(p, (size_t)(b + n - p), "]]", &k)) && image_ext(p, (size_t)(q - p)) && !memchr(p, ']', (size_t)(q - p))) {
			*len = (size_t)(q - p);
			return p;
		}
	}
	const char *open[] = { "\\[", "$$" }, *close[] = { "\\]", "$$" };
	for (int i = 0; i < 2; i++)
		if ((p = find(b, n, open[i], &k)) && (q = find(p + 2, (size_t)(b + n - p - 2), close[i], &k)) && q > p + 2) {
			*tex = 1;
			*len = (size_t)(q + 2 - p);
			return p;
		}
	return NULL;
}

int image_line(const Text *t, struct Syntax *s, const char *path, size_t l, int cols, int rows, int cw, int ch, uint32_t fg, uint32_t bg)
{
	const char *ft = path && cw > 0 ? syntax_filetype(path) : NULL;
	int org = ft && !strcmp(ft, "org");
	if (!ft || (!org && strcmp(ft, "markdown"))) return 0;
	char b[LINE];
	size_t a = text_line_start(t, l), n = text_line_start(t, l + 1) - a, len;
	n = text_read(t, a, b, n < LINE ? n : LINE);
	int tex;
	const char *p = target(b, n, org, &len, &tex);
	SyntaxMd md;
	if (!p || (!org && syntax_md(s, t, l, &md) && md.code)) return 0;
	/* the line, the file's directory and the box */
	unsigned long long key = image_fnv(14695981039346656037ULL, b, n);
	const char *slash = strrchr(path, '/');
#ifdef _WIN32
	const char *bs = strrchr(path, '\\');
	if (bs > slash) slash = bs;
#endif
	size_t dl = slash ? (size_t)(slash - path) : 0;
	key = image_fnv(key, path, dl);
	int box[] = { cols, rows, cw, ch, (int)fg, (int)bg };
	key = image_fnv(key, box, sizeof box);
	key += !key;
	unsigned i = (unsigned)(key % CACHE);
	/* a hit; a miss of a LaTeX fragment is tried again once a job ends */
	if (cache[i].key == key && (cache[i].n ? image_gen(cache[i].n) == cache[i].gen : cache[i].ends == image_latex_ends))
		return cache[i].n;
	char f[LINE + 600];
	const char *img = f;
	size_t dn = slash ? dl : 1;   /* no directory: "." */
	if (dn + 8 + len >= sizeof f) return 0;
	memcpy(f, slash ? path : ".", dn);
	f[dn] = '/';
	if (tex) {
		memcpy(f + dn + 1, "ltximg", 7);   /* org-preview-latex-image-directory */
		img = image_latex(p, len, fg, ch * 7, f);   /* 140 dpi at 20-pixel cells, Org's at scale 1 */
	} else if (*p == '/') {
		memcpy(f, p, len);
		f[len] = 0;
	} else {
		memcpy(f + dn + 1, p, len);
		f[dn + 1 + len] = 0;
	}
	int m = img ? image_load(img, cols, rows, cw, ch, bg) : 0;
	cache[i].key = key;
	cache[i].n = m;
	cache[i].gen = image_gen(m);
	cache[i].ends = image_latex_ends;
	return m;
}
