/* Review of PR 136: the in-process zig fmt (build/zig/libzigNN.a) against
   `zig fmt --stdin` of the same release, on texts changed so that the
   formatter has work to do.  usage: fmtcmp NN /path/to/zig tmpdir mode file...
   modes: 0 as is, 1 no indent, 2 some newlines joined, 3 no comma before a
   newline, 4 spaces doubled and tabs for indent, 5 CR LF, 6 random bytes deleted */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct ZigH ZigH;
#define REL(v) ZigH *zig##v##_parse(const char *, size_t, unsigned char *); void zig##v##_free(ZigH *); const char *zig##v##_fmt(ZigH *, size_t *); size_t zig##v##_errors(ZigH *);
REL(15) REL(16) REL(17)
static char *slurp(const char *p, size_t *n) { FILE *f = fopen(p, "rb"); if (!f) return NULL; fseek(f, 0, 2); long l = ftell(f); rewind(f); char *b = malloc(l + 1); if (fread(b, 1, l, f) != (size_t)l) exit(3); fclose(f); b[l] = 0; *n = l; return b; }
static unsigned rs = 12345;
static unsigned rnd(unsigned n) { rs = rs * 1103515245u + 12345u; return (rs >> 8) % n; }
int main(int c, char **v)
{
	int rel = atoi(v[1]), mode = atoi(v[4]);
	ZigH *(*P)(const char *, size_t, unsigned char *) = rel == 15 ? zig15_parse : rel == 16 ? zig16_parse : zig17_parse;
	void (*F)(ZigH *) = rel == 15 ? zig15_free : rel == 16 ? zig16_free : zig17_free;
	const char *(*FM)(ZigH *, size_t *) = rel == 15 ? zig15_fmt : rel == 16 ? zig16_fmt : zig17_fmt;
	long same = 0, diff = 0, both = 0, one = 0, changed = 0;
	char in[512], out[512], cmd[2048];
	snprintf(in, sizeof in, "%s/in.zig", v[3]); snprintf(out, sizeof out, "%s/out.zig", v[3]);
	for (int i = 5; i < c; i++) {
		size_t n, m = 0; char *s = slurp(v[i], &n); if (!s) continue;
		char *b = malloc(2 * n + 2);
		for (size_t k = 0; k < n; k++) {
			char ch = s[k]; int bol = !m || b[m - 1] == '\n';
			if (mode == 1 && ch == ' ' && bol) continue;
			if (mode == 2 && ch == '\n' && rnd(4) == 0) { size_t q = k; while (q && s[q - 1] != '\n') q--; if (!memmem(s + q, k - q, "//", 2) && !memmem(s + q, k - q, "\\\\", 2)) { b[m++] = ' '; continue; } }
			if (mode == 3 && ch == ',' && s[k + 1] == '\n') continue;
			if (mode == 4 && ch == ' ') { if (bol || b[m - 1] == '\t') { b[m++] = '\t'; continue; } b[m++] = ' '; }
			if (mode == 5 && ch == '\n') b[m++] = '\r';
			if (mode == 6 && rnd(2000) == 0) continue;
			b[m++] = ch;
		}
		b[m] = 0;
		FILE *f = fopen(in, "wb"); fwrite(b, 1, m, f); fclose(f);
		snprintf(cmd, sizeof cmd, "%s fmt --stdin < %s > %s 2>/dev/null", v[2], in, out);
		int rc = system(cmd);
		size_t rn = 0, on = 0; char *r = slurp(out, &rn);
		ZigH *h = P(b, m, NULL); const char *o = h ? FM(h, &on) : NULL;
		if (rc && !o) both++;
		else if (rc || !o) { one++; fprintf(stderr, "ONE REFUSES %s mode %d rc %d ours %s\n", v[i], mode, rc, o ? "ok" : "refused"); }
		else if (rn == on && !memcmp(r, o, on)) { same++; if (on != m || memcmp(o, b, m)) changed++; }
		else { diff++; fprintf(stderr, "DIFF %s mode %d\n", v[i], mode); }
		if (h) F(h);
		free(s); free(b); free(r);
	}
	printf("zig 0.%d mode %d: identical %ld (of which the formatter changed %ld), different %ld, both refuse %ld, only one refuses %ld\n", rel, mode, same, changed, diff, both, one);
	return diff || one;
}
