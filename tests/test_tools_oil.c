/* The file manager's model against oil.nvim's own, as recorded from oil by
   src/tools/conform/oil.lua: the rendered lines, and the actions (or
   errors) for the edited lines.  With "-" it is the live driver: oil's
   output on stdin, our differences on stderr. */
#include <string.h>
#include "check.h"
#include "../src/tools/oil.h"

#define REC "src/tools/conform/oil-recorded.txt"
enum { LINE = 1024 };

static char rend[2][1 << 16], edit[2][1 << 16], want[1 << 16];
static size_t nrend[2], nedit[2], nwant;
static long cases, bad;
static char seed[64];

/* A move cycle goes through "<src>__oil_tmp_<random number>": drops the
   number, in place. */
static void untmp(char *s)
{
	char *w = s;
	for (const char *r = s; *r;) {
		if (!strncmp(r, "__oil_tmp_", 10)) {
			memmove(w, r, 10); w += 10; r += 10;
			while (*r >= '0' && *r <= '9') r++;
		} else *w++ = *r++;
	}
	*w = 0;
}

static const char *TYPE(int t) { return t == OIL_CREATE ? "create" : t == OIL_DELETE ? "delete" : t == OIL_MOVE ? "move" : "copy"; }
static const char *ETYPE(int t) { return t == OIL_DIR ? "directory" : t == OIL_LINK ? "link" : "file"; }

static void check_case(void)
{
	static const char *dirs[2] = { "/tmp/oc/", "/tmp/oc/sub/" };
	static char got[1 << 16];
	size_t n = 0;
	cases++;
	for (int i = 0; i < 2; i++) {
		char *r;
		size_t rn;
		CHECK(!oil_render(dirs[i], &r, &rn));
		if (rn != nrend[i] || memcmp(r, rend[i], rn)) {
			if (bad++ < 5) fprintf(stderr, "case %s buffer %d: oil rendered\n%.*sours\n%.*s", seed, i + 1, (int)nrend[i], rend[i], (int)rn, r);
		}
		free(r);
	}
	const char *t[2] = { edit[0], edit[1] };
	OilAction *act;
	OilError *err;
	int nerr, na = oil_actions(2, dirs, t, nedit, &act, &err, &nerr);
	got[0] = 0;
	for (int i = 0; i < na; i++)
		n += (size_t)snprintf(got + n, sizeof got - n, "A %s %s %s %s\n", TYPE(act[i].type), ETYPE(act[i].etype), act[i].src, act[i].dst ? act[i].dst : "-");
	for (int i = 0; i < nerr; i++)
		n += (size_t)snprintf(got + n, sizeof got - n, "X %d %ld %s\n", err[i].buf + 1, err[i].line, err[i].msg);
	if (na > 0) oil_free(act, na);
	free(err);
	want[nwant] = 0;
	untmp(want);
	untmp(got);
	if (strcmp(want, got) && bad++ < 5) fprintf(stderr, "case %s: oil\n%sours\n%s", seed, want, got);
}

static void run(FILE *in)
{
	static char ln[LINE];
	int have = 0;
	while (fgets(ln, sizeof ln, in)) {
		size_t l = strlen(ln);
		if (ln[0] == 'C' || ln[0] == '.') {
			if (have) check_case();
			have = ln[0] == 'C';
			snprintf(seed, sizeof seed, "%.*s", (int)strcspn(ln + 2, "\n"), ln + 2);
			oil_reset();
			nrend[0] = nrend[1] = nedit[0] = nedit[1] = nwant = 0;
		} else if (ln[0] == 'R' || ln[0] == 'E') {
			int b = ln[2] - '1';
			char *d = ln[0] == 'R' ? rend[b] : edit[b];
			size_t *dn = ln[0] == 'R' ? &nrend[b] : &nedit[b];
			memcpy(d + *dn, ln + 4, l - 4); *dn += l - 4;
		} else if (ln[0] == 'K') {
			/* K id dir type linkdir name [-> link] */
			int id, ld, off;
			char dir[512], type[16];
			CHECK(sscanf(ln, "K %d %511s %15s %d %n", &id, dir, type, &ld, &off) == 4);
			ln[l - 1] = 0;
			char *name = ln + off, *link = strstr(name, " -> ");
			if (link && type[0] == 'l') { *link = 0; link += 4; } else link = NULL;
			CHECK(oil_entry(dir, name, type[0] == 'd' ? OIL_DIR : type[0] == 'l' ? OIL_LINK : OIL_FILE, link, ld) == id);
		} else if (ln[0] == 'A' || ln[0] == 'X') { memcpy(want + nwant, ln, l); nwant += l; }
	}
}

int main(int argc, char **argv)
{
	FILE *in = argc == 2 && !strcmp(argv[1], "-") ? stdin : fopen(REC, "r");
	CHECK(in);
	run(in);
	printf("test_tools_oil: %ld cases from oil.nvim, %ld differ\n", cases, bad);
	CHECK(cases > 0 && !bad);
	oil_reset();
	return 0;
}
