/* LaTeX previews (image.h's image_latex), as Org's dvipng process
   (org-preview-latex-process-alist): latex, then dvipng -T tight with the
   text's colour on a transparent background, named by a hash of what was
   asked, so a render is never repeated and a stale one never shown.  Each
   job works under its own names (with the pid: two editors may render the
   same formula) and renames its PNG into place last, so the PNG a lookup
   sees is always whole (LatexRender.tla). */
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "core.h"
#include "file.h"
#include "image.h"
#include "loop.h"

enum { JOBS = 8, FAILED = 64 };

static struct job {
	unsigned long long h;   /* 0: free */
	int dpi;
	uint32_t fg;
	char dir[PATH_MAX - 64];   /* room for the names in it */
} job[JOBS];
static unsigned long long failed[FAILED];   /* renders that failed: not tried again */
static unsigned nfailed;

/* dir/ltximg-<hash>[-<pid>].ext into f */
static char *name(char *f, const struct job *j, int own, const char *ext)
{
	char pid[24] = "";
	if (own) snprintf(pid, sizeof pid, "-%ld", (long)getpid());
	snprintf(f, PATH_MAX, "%s/ltximg-%016llx%s.%s", j->dir, j->h, pid, ext);
	return f;
}

static void step(void *ctx, int stream, const char *b, size_t n)
{
	struct job *j = ctx;
	char f[PATH_MAX], g[PATH_MAX];
	(void)b;
	if (stream) return;
	if (!n && j->dpi > 0) {   /* latex is done: dvipng */
		char fg[48], dpi[16];
		snprintf(fg, sizeof fg, "rgb %.3f %.3f %.3f", (j->fg >> 16) / 255., (j->fg >> 8 & 0xff) / 255., (j->fg & 0xff) / 255.);
		snprintf(dpi, sizeof dpi, "%d", j->dpi);
		char *argv[] = { "dvipng", "-q", "-D", dpi, "-T", "tight", "-bg", "Transparent", "-fg", fg,
			"-o", name(f, j, 1, "png"), name(g, j, 1, "dvi"), NULL };
		j->dpi = -j->dpi;
		int id = proc_spawn(argv, j->dir, step, j);
		if (id >= 0) { proc_close(id); return; }
		n = 1;
	}
	/* a lookup finds another editor's PNG before this list */
	if (n || rename(name(f, j, 1, "png"), name(g, j, 0, "png"))) failed[nfailed++ % FAILED] = j->h;
	static const char *const ext[] = { "tex", "aux", "log", "dvi", "png" };
	for (int i = 0; i < 5; i++) remove(name(f, j, 1, ext[i]));
	j->h = 0;
	core_changed = 1;
}

const char *image_latex(const char *tex, size_t n, uint32_t fg, int dpi, const char *dir)
{
	static char png[PATH_MAX];
	struct job k = { 14695981039346656037ULL, dpi, fg, "" }, *j = NULL;   /* FNV-1a */
	for (size_t i = 0; i < n; i++) k.h = (k.h ^ (unsigned char)tex[i]) * 1099511628211ULL;
	for (int i = 0; i < 64; i += 8) k.h = (k.h ^ (((unsigned long long)fg << 32 | (unsigned)dpi) >> i & 0xff)) * 1099511628211ULL;
	k.h += !k.h;
	if ((size_t)snprintf(k.dir, sizeof k.dir, "%s", dir) >= sizeof k.dir) return NULL;
	FileId id;
	if (!file_id(name(png, &k, 0, "png"), &id) && id.exists) return png;
	for (int i = 0; i < FAILED; i++) if (failed[i] == k.h) return NULL;
	for (int i = 0; i < JOBS; i++) {
		if (job[i].h == k.h) return NULL;   /* rendering */
		if (!job[i].h && !j) j = &job[i];
	}
	if (!j) return NULL;                    /* busy: asked again when one is done */
	*j = k;
	char f[PATH_MAX];
	file_mkdirs(name(f, j, 1, "tex"));
	FILE *o = fopen(f, "w");
	if (!o) { step(j, 0, NULL, 1); return NULL; }
	/* Org's default packages for formulas (org-latex-default-packages-alist) */
	fprintf(o, "\\documentclass{article}\n\\usepackage{amsmath,amssymb}\n\\pagestyle{empty}\n"
		"\\begin{document}\n%.*s\n\\end{document}\n", (int)n, tex);
	fclose(o);
	char *argv[] = { "latex", "-interaction=nonstopmode", "-halt-on-error", strrchr(f, '/') + 1, NULL };
	int p = proc_spawn(argv, j->dir, step, j);
	if (p < 0) step(j, 0, NULL, 1);
	else proc_close(p);   /* TeX reads its input on an error: end it */
	return NULL;
}
