/* Prints src/org's parse of each file as tools/org_dump.el prints Org's
   (tools/org_conform.py compares them). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "org.h"
#include "grid.h"

static const char *const names[] = { "headline", "section", "planning", "property-drawer",
	"node-property", "drawer", "clock", "comment", "keyword", "babel-call", "paragraph",
	"plain-list", "item", "table", "table-row", "src-block", "example-block", "export-block",
	"comment-block", "verse-block", "quote-block", "center-block", "special-block",
	"dynamic-block", "latex-environment", "fixed-width", "horizontal-rule",
	"footnote-definition", "diary-sexp" };

static void num(FILE *o, int v) { if (v < 0) fprintf(o, " -"); else fprintf(o, " %d", v); }
static const char *unit(char u) { return u == 'h' ? "hour" : u == 'd' ? "day" : u == 'w' ? "week" : u == 'm' ? "month" : "year"; }
/* a timestamp as tools/org_dump.el prints one */
static void ts(FILE *o, const char *k, const OrgTs *t)
{
	fprintf(o, "@ %s %s %s", k,
	        t->type == 'a' ? "active" : t->type == 'A' ? "active-range" : t->type == 'i' ? "inactive" :
	        t->type == 'I' ? "inactive-range" : "diary",
	        t->range == 'd' ? "daterange" : t->range == 't' ? "timerange" : "-");
	num(o, t->y); num(o, t->m); num(o, t->d); num(o, t->h); num(o, t->min);
	num(o, t->y2); num(o, t->m2); num(o, t->d2); num(o, t->h2); num(o, t->min2);
	if (t->rep) fprintf(o, " %s %d %s", t->rep == '+' ? "cumulate" : t->rep == '#' ? "catch-up" : "restart", t->rep_n, unit(t->rep_unit));
	else fprintf(o, " - - -");
	if (t->rep_dl_unit) fprintf(o, " %d %s", t->rep_dl_n, unit(t->rep_dl_unit)); else fprintf(o, " - -");
	if (t->warn) fprintf(o, " %s %d %s", t->warn == 'a' ? "all" : "first", t->warn_n, unit(t->warn_unit));
	else fprintf(o, " - - -");
	fprintf(o, " %zu %zu\n", t->begin, t->end);
}

static void pos(FILE *o, size_t p) { if (p == ORG_NONE) fprintf(o, " -"); else fprintf(o, " %zu", p); }

/* the nodes as tools/org_dump.el prints them; 0, or -1 out of memory */
static int dump(FILE *o, const char *b, size_t n)
{
	size_t k;
	OrgNode *e = org_parse(b, n, &k);
	if (!e) return -1;
	for (size_t j = 0; j < k; j++) {
		fprintf(o, "%s", names[e[j].type]);
		pos(o, e[j].begin); pos(o, e[j].aff); pos(o, e[j].cbeg); pos(o, e[j].cend); pos(o, e[j].end);
		fprintf(o, "\n");
		OrgTs t[3];
		size_t d[2];
		if (e[j].type == ORG_PLANNING) {
			int got = org_planning(b, n, &e[j], t);
			for (int i = 0; i < 3; i++) if (got >> i & 1) ts(o, (const char *[]){ "closed", "deadline", "scheduled" }[i], &t[i]);
		}
		if (e[j].type == ORG_CLOCK) {
			if (org_clock(b, n, &e[j], t, d)) ts(o, "clock", t);
			if (d[1] > d[0]) fprintf(o, "= %.*s\n", (int)(d[1] - d[0]), b + d[0]); else fprintf(o, "= running\n");
		}
		if (e[j].type != ORG_HEADLINE) continue;
		OrgHead h;
		org_head(b, n, e[j].begin, &h);
		fprintf(o, "+ %d %s ", h.level, h.todo < 0 ? "-" : org_todo[h.todo]);
		if (h.pri < 0) fprintf(o, "-"); else fprintf(o, "%d", h.pri);
		fprintf(o, " %s \"", h.commented ? "c" : "-");
		for (size_t x = h.ta; x < h.tz; x++) {
			if (b[x] == '"' || b[x] == '\\') fputc('\\', o);
			fputc(b[x], o);
		}
		fprintf(o, "\" ");
		fprintf(o, "%.*s", (int)(h.gz - h.ga), b + h.ga);   /* the tags joined by ":" */
		fprintf(o, "\n");
	}
	free(e);
	return 0;
}

/* org_todo_step at pos (clamped to the text) for each of dirs ('r' right,
   else left), at tools/org_todo.el's clock, printing each text and "--";
   the new length, or -1 out of memory or room */
static long steps(FILE *o, char *b, size_t n, size_t cap, size_t pos, const char *dirs)
{
	for (size_t a, z, len; *dirs; dirs++) {
		const char *r = org_todo_step(b, n, pos < n ? pos : n, *dirs == 'r' ? 1 : -1, 29918490, &a, &z, &len);   /* 2026-11-19 17:30 */
		if (!r || n - (z - a) + len > cap) return -1;
		memmove(b + a + len, b + z, n - z);
		memcpy(b + a, r, len);
		n = n - (z - a) + len;
		fprintf(o, "%.*s--\n", (int)n, b);
	}
	return (long)n;
}

#ifndef ORG_DRV_LIB
/* The colours as tools/org_faces.el prints Org's: runs of one face, read
   through the syntax module's calls in windows of w bytes (the scan goes on
   from one window to the next, as the core draws) */
static int colours(FILE *o, const char *b, size_t n, size_t w)
{
	static const struct { int f; const char *name; } nm[] = {
		{ FACE_COMMENT, "comment" }, { FACE_KEYWORD, "keyword" }, { FACE_MARKUP_RAW, "markup.raw" },
		{ FACE_MARKUP_HEADING, "markup.heading" }, { FACE_MARKUP_HEADING_1, "markup.heading.1" },
		{ FACE_MARKUP_HEADING_2, "markup.heading.2" }, { FACE_MARKUP_HEADING_3, "markup.heading.3" },
		{ FACE_MARKUP_HEADING_4, "markup.heading.4" }, { FACE_MARKUP_HEADING_5, "markup.heading.5" },
		{ FACE_MARKUP_HEADING_6, "markup.heading.6" }, { FACE_ORG_LEVEL_7, "org.level.7" },
		{ FACE_ORG_LEVEL_8, "org.level.8" }, { FACE_ORG_TODO, "org.todo" }, { FACE_ORG_PRIORITY, "org.priority" },
		{ FACE_ORG_TAG, "org.tag" }, { FACE_ORG_DATE, "org.date" }, { FACE_ORG_TABLE, "org.table" },
		{ FACE_ORG_FORMULA, "org.formula" }, { FACE_ORG_DRAWER, "org.drawer" }, { FACE_ORG_CHECKBOX, "org.checkbox" },
		{ FACE_ORG_INFO, "org.info" }, { FACE_ORG_BLOCK, "org.block" }, { FACE_ORG_BLOCK_LINE, "org.block.line" },
	};
	static unsigned char f[1 << 24];
	Text *t = text_new(b, n);
	OrgFx *x = t ? orgfx_new(t) : NULL;
	if (!x) return text_free(t), -1;
	orgfx_update(x, NULL);
	for (size_t a = 0; a < n; a += w) {
		size_t z = a + w < n ? a + w : n;
		memset(f + a, 0, z - a);
		orgfx_faces(x, a, z, f + a);
	}
	for (size_t a = 0, z; a < n; a = z) {
		for (z = a + 1; z < n && f[z] == f[a]; z++) ;
		if (!f[a]) continue;
		size_t i = 0;
		while (i < sizeof nm / sizeof *nm && nm[i].f != f[a]) i++;
		fprintf(o, "%zu %zu %s\n", a, z, i < sizeof nm / sizeof *nm ? nm[i].name : "?");
	}
	orgfx_free(x);
	text_free(t);
	return 0;
}

/* What Org shows on each line, the spans of orgfx_conceal applied, as
   tools/org_conform.py -s builds Org's own from tools/org_show.el */
static int show(FILE *o, const char *b, size_t n)
{
	Text *t = text_new(b, n);
	OrgFx *x = t ? orgfx_new(t) : NULL;
	if (!x) return 1;
	for (size_t l = 0; l <= text_lines(t); l++) {
		Conceal c[4096];
		size_t k = orgfx_conceal(x, l, c, 4096), p = text_line_start(t, l), e = p;
		while (e < n && b[e] != '\n') e++;
		for (size_t i = 0; i < k; p = c[i].z, i++) {
			if (c[i].a < p || c[i].z > e || c[i].a >= c[i].z) {   /* syntax.h: in order, on the line */
				fprintf(stderr, "error: line %zu: span %zu %zu out of order\n", l, c[i].a, c[i].z);
				orgfx_free(x);
				text_free(t);
				return 1;
			}
			fprintf(o, "%.*s%s", (int)(c[i].a - p), b + p, c[i].s);
		}
		fprintf(o, "%.*s\n", (int)(e - p), b + p);
	}
	orgfx_free(x);
	text_free(t);
	return 0;
}

int main(int argc, char **argv)
{
	static char b[1 << 24];
	size_t w = 0;   /* -f W: the colours, in windows of W bytes */
	int todo = argc > 1 && !strcmp(argv[1], "-t");   /* -t: org_todo_step as FILE.cmd says, as tools/org_todo.el */
	int conceal = argc > 1 && !strcmp(argv[1], "-s");   /* -s: what each line shows */
	argv += conceal, argc -= conceal;
	if (argc > 2 && !strcmp(argv[1], "-f")) w = strtoul(argv[2], NULL, 10), argv += 2, argc -= 2;
	argv += todo, argc -= todo;
	for (int i = 1; i < argc; i++) {
		FILE *f = fopen(argv[i], "rb");
		if (!f) return 2;
		size_t n = fread(b, 1, sizeof b, f);
		int more = fgetc(f) != EOF;
		fclose(f);
		if (more) { fprintf(stderr, "%s: over %zu bytes\n", argv[i], sizeof b); return 4; }
		const char *base = strrchr(argv[i], '/');
		printf("== %s\n", base ? base + 1 : argv[i]);
		if (todo) {
			static char c[4096], path[4096];
			size_t pos;
			snprintf(path, sizeof path, "%s.cmd", argv[i]);
			FILE *g = fopen(path, "rb");
			if (!g || fscanf(g, "%zu %4000s", &pos, c) != 2) return 2;
			fclose(g);
			if (steps(stdout, b, n, sizeof b, pos, c) < 0) return 3;
			continue;
		}
		if (conceal) { if (show(stdout, b, n)) return 3; continue; }
		if (w ? colours(stdout, b, n, w) : dump(stdout, b, n)) return 3;
		for (size_t p = 0; !w && p < n; p++) {   /* org_ts at every character */
			OrgTs t;
			if ((b[p] & 0xc0) != 0x80 && org_ts(b, n, p, &t)) ts(stdout, "ts", &t);
		}
		Text *t = w ? NULL : text_new(b, n);   /* orgfx_foldexpr at every line */
		OrgFx *x = t ? orgfx_new(t) : NULL;
		if (!w && !x) return 3;
		for (size_t l = 0; x && l <= text_lines(t); l++) {
			int st, v = orgfx_foldexpr(x, l, &st);
			printf("fold %zu %d %d\n", l, v, st);
		}
		orgfx_free(x);
		text_free(t);
	}
	return 0;
}
#endif
