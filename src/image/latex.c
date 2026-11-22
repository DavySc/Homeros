/* LaTeX previews (image.h's image_latex), as Org's dvipng process
   (org-preview-latex-process-alist): latex, then dvipng -T tight with the
   text's colour on a transparent background, named by a hash of what was
   asked, so a render is never repeated and a stale one never shown.  Each
   job works under its own names (with the pid: two editors may render the
   same formula) and renames its PNG into place last, so the PNG a lookup
   sees is always whole (LatexRender.tla). */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "core.h"
#include "file.h"
#include "image.h"
#include "loop.h"

/* TIMEOUT: a fragment TeX never finishes (\loop\iftrue\repeat) is
   killed, so it does not hold its job and a core for good; ms, for each of
   latex and dvipng (a Hasse diagram in TikZ takes about a second) */
enum { JOBS = 8, TIMEOUT = 10000 };

static struct job {
	unsigned long long h;   /* 0: free */
	int dpi, id;            /* id: the running tool's process */
	uint32_t fg;
	char dir[PATH_MAX - 64];   /* room for the names in it */
} job[JOBS];
/* renders that failed, never tried again: all of them, or a screen of
   failing fragments (no latex installed) would start one after another
   for ever, each failure asking for the frame that starts the next */
static unsigned long long *failed;
static size_t nfailed;
unsigned image_latex_ends;   /* jobs ended: line.c tries its misses again */

/* dir/ltximg-<hash>[-<pid>].ext into f */
static char *name(char *f, const struct job *j, int own, const char *ext)
{
	char pid[24] = "";
	if (own) snprintf(pid, sizeof pid, "-%ld", (long)getpid());
	snprintf(f, PATH_MAX, "%s/ltximg-%016llx%s.%s", j->dir, j->h, pid, ext);
	return f;
}

static void stop(void *ctx) { proc_kill(((struct job *)ctx)->id); }

/* runs a tool for job j in its dir, under the timeout */
static int run(struct job *j, char **argv, ProcFn fn)
{
	if ((j->id = proc_spawn(argv, j->dir, fn, j)) < 0) return -1;
	proc_close(j->id);   /* TeX reads its input on an error: end it */
	loop_timer(stop, j, TIMEOUT);
	return 0;
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
			"-o", strrchr(name(f, j, 1, "png"), '/') + 1, strrchr(name(g, j, 1, "dvi"), '/') + 1, NULL };   /* run in dir */
		j->dpi = -j->dpi;
		if (!run(j, argv, step)) return;
		n = 1;
	}
	loop_timer(stop, j, -1);
	/* a lookup finds another editor's PNG before this list */
	unsigned long long *more;
	if ((n || rename(name(f, j, 1, "png"), name(g, j, 0, "png"))) && (more = realloc(failed, (nfailed + 1) * sizeof *failed)))
		(failed = more)[nfailed++] = j->h;
	static const char *const ext[] = { "tex", "aux", "log", "dvi", "png" };
	for (int i = 0; i < 5; i++) remove(name(f, j, 1, ext[i]));
	j->h = 0;
	core_changed = 1;
	image_latex_ends++;
}

unsigned long long image_fnv(unsigned long long h, const void *p, size_t n)
{
	for (size_t i = 0; i < n; i++) h = (h ^ ((const unsigned char *)p)[i]) * 1099511628211ULL;
	return h;
}

const char *image_latex(const char *tex, size_t n, uint32_t fg, int dpi, const char *dir)
{
	static char png[PATH_MAX];
	struct job k = { 14695981039346656037ULL, dpi, 0, fg, "" }, *j = NULL;   /* FNV-1a */
	unsigned long long fd = (unsigned long long)fg << 32 | (unsigned)dpi;
	k.h = image_fnv(image_fnv(k.h, tex, n), &fd, sizeof fd);
	k.h += !k.h;
	if ((size_t)snprintf(k.dir, sizeof k.dir, "%s", dir) >= sizeof k.dir) return NULL;
	FileId id;
	if (!file_id(name(png, &k, 0, "png"), &id) && id.exists) return png;
	for (size_t i = 0; i < nfailed; i++) if (failed[i] == k.h) return NULL;
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
	if (run(j, argv, step)) step(j, 0, NULL, 1);
	return NULL;
}
