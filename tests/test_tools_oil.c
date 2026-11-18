/* The file manager's model against oil.nvim's own, as recorded from oil by
   src/tools/conform/oil.lua (seed 42, 400 cases, then three its plants
   need: 1206 of seed 1, a broken link; 2278 of seed 2, a failed copy;
   43064 of seed 43, an empty path part; seeds 1 and 2 from the generator
   before it made "/" names and brace groups; 44020 and 44035 of seed 44
   with "slash", link targets ending in "/"): the
   rendered lines; the actions (or errors) for the edited lines; and once
   oil has run them, the tree, and both buffers listed again.  With "-" it
   is the live driver: oil's output on stdin, our differences on stderr.
   It rebuilds the oracle's tree in /tmp/oc. */
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include "check.h"
#include "../src/tools/oil.h"

#define REC "src/tools/conform/oil-recorded.txt"
enum { LINE = 1024 };

static char rend[2][1 << 16], edit[2][1 << 16], want[1 << 16];
static size_t nrend[2], nedit[2], nwant;
static long cases, bad;
static char seed[64];
static int kmax;  /* the highest id before oil runs the actions */

/* A relisted directory gives its new entries ids in the order the file
   system lists them (oil and oil_list alike), which differs between file
   systems: such an id becomes n's, in place, on both sides. */
static void unnew(char *s)
{
	for (char *l = s; *l; l = strchr(l, '\n') + 1) {
		if (l[0] == 'S' && l[4] == '/' && strtol(l + 5, NULL, 10) > kmax)
			for (char *d = l + 5; *d >= '0' && *d <= '9'; d++) *d = 'n';
		if (!strchr(l, '\n')) break;
	}
}

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

/* "T path type [-> link | contents]" for the tree under p, names sorted,
   contents with "|" between lines (the oracle's readfile) */
static int byname(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }
static size_t tree(const char *p, char *o, size_t n, size_t cap)
{
	char *nm[256], q[1024], t[1024];
	int k = 0;
	DIR *d = opendir(p);
	CHECK(d);
	for (struct dirent *de; (de = readdir(d)) && k < 256;)
		if (strcmp(de->d_name, ".") && strcmp(de->d_name, "..")) nm[k++] = strdup(de->d_name);
	closedir(d);
	qsort(nm, (size_t)k, sizeof *nm, byname);
	for (int i = 0; i < k; i++) {
		struct stat st;
		snprintf(q, sizeof q, "%s/%s", p, nm[i]);
		CHECK(!lstat(q, &st));
		if (S_ISLNK(st.st_mode)) {
			ssize_t m = readlink(q, t, sizeof t - 1);
			t[m > 0 ? m : 0] = 0;
			n += (size_t)snprintf(o + n, cap - n, "T %s link -> %s\n", q, t);
		} else if (S_ISDIR(st.st_mode)) {
			n += (size_t)snprintf(o + n, cap - n, "T %s directory\n", q);
			n = tree(q, o, n, cap);
		} else {
			FILE *f = fopen(q, "r");
			CHECK(f);
			size_t m = fread(t, 1, sizeof t - 1, f);
			fclose(f);
			if (m && t[m - 1] == '\n') m--;
			for (size_t j = 0; j < m; j++) if (t[j] == '\n') t[j] = '|';
			n += (size_t)snprintf(o + n, cap - n, "T %s file %.*s\n", q, (int)m, t);
		}
		free(nm[i]);
	}
	return n;
}

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
	if (na > 0) {
		/* oil runs them; then the tree, and both buffers listed again */
		int done;
		const char *err = oil_apply(act, na, &done);
		n += (size_t)snprintf(got + n, sizeof got - n, "D %s\n", err ? err : "ok");
		n = tree("/tmp/oc", got, n, sizeof got);
		char d[2][512];
		for (int b = 0; b < 2; b++) {
			snprintf(d[b], sizeof d[b], "%s", dirs[b]);
			for (int i = 0; i < done; i++) oil_follow(&act[i], d[b], sizeof d[b]);
			char *r;
			size_t rn;
			n += (size_t)snprintf(got + n, sizeof got - n, "B %d %s\n", b + 1, d[b]);
			/* render_buffer_async shows a listing's error instead */
			if (oil_list(d[b])) { n += (size_t)snprintf(got + n, sizeof got - n, "S %d Error: %s: %s\n", b + 1, oil_errname(errno), d[b]); continue; }
			CHECK(!oil_render(d[b], &r, &rn));
			for (char *l = r; l < r + rn; l = strchr(l, '\n') + 1)
				n += (size_t)snprintf(got + n, sizeof got - n, "S %d %.*s\n", b + 1, (int)(strchr(l, '\n') - l), l);
			free(r);
		}
		oil_free(act, na);
	}
	free(err);
	want[nwant] = 0;
	untmp(want);
	untmp(got);
	unnew(want);
	unnew(got);
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
			CHECK(!system("rm -rf /tmp/oc && mkdir -p /tmp/oc/sub"));
			nrend[0] = nrend[1] = nedit[0] = nedit[1] = nwant = kmax = 0;
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
			if (id > kmax) kmax = id;
			/* the oracle's tree again: a file holds its name */
			char p[1024];
			snprintf(p, sizeof p, "%s%s", dir, name);
			if (type[0] == 'd') CHECK(!mkdir(p, 0755) || !strcmp(name, "sub"));
			else if (type[0] == 'l') CHECK(!symlink(link, p));
			else {
				FILE *f = fopen(p, "w");
				CHECK(f && fprintf(f, "%s\n", name) > 0 && !fclose(f));
			}
		} else if (strchr("AXDTSB", ln[0])) { memcpy(want + nwant, ln, l); nwant += l; }
	}
}

/* Cases the two-buffer oracle cannot make, each as live oil.nvim answered:
   reviews/pr-23-repro/parent.lua (a directory copied two levels below
   itself), and columns.lua's natural order, kept up to 5,000 entries
   (f1 f2 ... f10), byte order past it (f1 f10 f100 ...). */
static void fixed(void)
{
	const char *dirs[2] = { "/tmp/op/", "/tmp/op/sub/deep/" };
	const char *text[2] = { "/000 x  ../\n/001 x  sub/\n", "/000 x  ../\n/001 x  sub/\n" };
	size_t len[2] = { strlen(text[0]), strlen(text[1]) };
	OilAction *act;
	OilError *err;
	int nerr;
	oil_reset();
	oil_entry("/tmp/op/", "sub", OIL_DIR, NULL, 0);
	oil_entry("/tmp/op/sub/", "deep", OIL_DIR, NULL, 0);
	CHECK(oil_actions(2, dirs, text, len, &act, &err, &nerr) < 0 && nerr == 1);
	CHECK(!strcmp(err[0].msg, "Cannot move or copy parent into itself: oil:///tmp/op/sub -> oil:///tmp/op/sub/deep/sub"));
	free(err);
	for (int n = 5000; n <= 5001; n++) {
		char d[32], p[160], *r;
		size_t rn;
		snprintf(d, sizeof d, "/tmp/onat%d/", n);
		snprintf(p, sizeof p, "rm -rf %s && mkdir %s", d, d);
		CHECK(!system(p));
		for (int i = 1; i <= n; i++) {
			snprintf(p, sizeof p, "%sf%d", d, i);
			FILE *f = fopen(p, "w");
			CHECK(f && !fclose(f));
		}
		oil_reset();
		CHECK(!oil_list(d) && !oil_render(d, &r, &rn));
		/* the third line's name: f2, or f10 */
		char *l = strchr(strchr(r, '\n') + 1, '\n') + 1;
		l = strstr(l, "  ") + 2;
		CHECK(!strncmp(l, n == 5000 ? "f2\n" : "f10\n", n == 5000 ? 3 : 4));
		free(r);
		snprintf(p, sizeof p, "rm -rf %s", d);
		CHECK(!system(p));
	}
	oil_reset();
}

int main(int argc, char **argv)
{
	fixed();
	FILE *in = argc == 2 && !strcmp(argv[1], "-") ? stdin : fopen(REC, "r");
	CHECK(in);
	run(in);
	printf("test_tools_oil: %ld cases from oil.nvim, %ld differ\n", cases, bad);
	CHECK(cases > 0 && !bad);
	oil_reset();
	return 0;
}
