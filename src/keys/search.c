/* Neovim's search.c: / ? n N * # and the last pattern with its offset.
   'ignorecase', 'smartcase' and 'wrapscan' are on (FEATURES.md 1). */
#include <stdio.h>
#include "k.h"
#include "uni.h"

static char spat[sizeof ((Editor *)0)->cmd + 1];   /* spats[0] */
static int has_spat, spat_noscs;
static struct { int dir, line, end; long off; } off = { '/', 0, 0, 0 };
int no_smartcase;   /* * and # take 'ignorecase' alone */

void search_reset(void) { has_spat = spat_noscs = no_smartcase = 0; off.dir = '/', off.line = off.end = 0, off.off = 0; }

/* pat_has_uppercase(): \x \_x \%x (_x %x after \v) are not letters */
static int has_upper(const char *p, int all)
{
	size_t n = strlen(p);
	for (size_t i = 0; i < n;) {
		size_t l = uni_glen((const unsigned char *)p, n, i);
		int c = (int)uni_cp((const unsigned char *)p, n, i);
		if (l > 1 || (c >= 'A' && c <= 'Z')) {
			if (map_case(c, 0) != c) return 1;
			i += l;
		} else if (c == '\\' && !all) i += !p[i + 1] ? 1 : (p[i + 1] == '_' || p[i + 1] == '%') && p[i + 2] ? 3 : 2;
		else if ((c == '%' || c == '_') && all) i += p[i + 1] ? 2 : 1;
		else i++;
	}
	return 0;
}

static int nul_at(Pos p) { return p.col >= ml_len(p.lnum); }
static long clen_at(Pos p) { return clen(ml_get(p.lnum), ml_len(p.lnum), p.col); }

/* the match on line lnum after the one at ms..me ('cpoptions' c: on from
   its end, a character on from an empty one) */
static int after(long lnum, Pos ms, Pos me, Pos *s, Pos *e)
{
	me.col += eq(me, ms) && !nul_at(me) ? clen_at(me) : 0;
	return nul_at(me) ? 0 : re_exec(lnum, me.col, s, e, NULL, NULL);
}

/* searchit() for the compiled pattern: the count-th match from *pos (line
   -1: before the first, nlines(): after the last); 0 for none.  end: the
   cursor goes to the match's last character (offset e). */
static int searchit(Pos *pos, int dir, long count, int end, int *wrapped)
{
	long n = nlines(), lnum = 0;
	int found, first = 1;
	do {
		Pos start = *pos, s = { 0 }, e = { 0 }, ms = { 0 }, me = { 0 };
		long extra = start.col == MAXCOL ? 0
		             : start.lnum >= 0 && start.lnum < n && start.col < MAXCOL - 2 ? (nul_at(start) ? 1 : clen_at(start)) : 1;
		if (dir == BACKWARD) extra = 0;
		int at_first = 1, r;
		found = 0;
		if (pos->lnum < 0) pos->lnum = 0, pos->col = 0, at_first = 0;
		if (dir == BACKWARD && start.col == 0) lnum = pos->lnum - 1, at_first = 0;
		else lnum = pos->lnum;
		for (int loop = 0; loop <= 1 && !found; loop++) {
			for (; lnum >= 0 && lnum < n; lnum += dir, at_first = 0) {
				if ((r = re_exec(lnum, 0, &s, &e, NULL, NULL)) < 0) goto timeout;
				if (r) {
					ms = s, me = e;
					int ok = 1;
					if (dir == FORWARD && at_first)
						while (ms.lnum == lnum && (end && first ? me.lnum == lnum && me.col - 1 < start.col + extra
						                                         : ms.col - nul_at(ms) < start.col + extra)) {
							if (me.lnum > lnum || (r = after(lnum, ms, me, &s, &e)) <= 0) { ok = me.lnum > lnum ? 0 : r; break; }
							ms = s, me = e;
						}
					if (dir == BACKWARD) {
						ok = 0;
						for (;;) {
							if (!(loop || (end ? e.lnum < start.lnum || (e.lnum == start.lnum && e.col - 1 < start.col + extra)
							                  : s.lnum < start.lnum || (s.lnum == start.lnum && s.col < start.col + extra))))
								break;
							ok = 1, ms = s, me = e;
							if (me.lnum > lnum || (r = after(lnum, ms, me, &s, &e)) <= 0) { if (r < 0) goto timeout; break; }
						}
					}
					if (ok < 0) goto timeout;
					if (!ok) continue;
					if (end && !eq(ms, me)) {
						*pos = me;
						if (!me.col) { if (pos->lnum > 0) pos->lnum--, pos->col = ml_len(pos->lnum); }
						else pos->col = head(ml_get(pos->lnum), me.col - 1);
					} else *pos = ms;
					pos->coladd = 0;
					found = 1, first = 0;
					break;
				}
				if (loop && lnum == start.lnum) break;
			}
			at_first = 0;
			if (!found && !loop) lnum = dir == BACKWARD ? n - 1 : 0, *wrapped = 1;
		}
	} while (--count > 0 && found);
	if (!found) { core_msg(E, "E486: Pattern not found: %s", spat); return 0; }
	if (pos->lnum >= n) pos->lnum = n - 1, pos->col = ml_len(n - 1) ? ml_len(n - 1) - 1 : 0;
	return 1;
timeout:
	core_msg(E, "E363: Pattern uses more memory than 'maxmempattern'");
	return 0;
}

/* do_search(): dirc '/' or '?' with the command line pat (pattern, then
   an offset after the delimiter), or 0 and NULL for n (rev: N).  1 found,
   2 found with a line offset added (linewise), 0 not. */
int do_search(Oparg *oap, int dirc, const char *pat, long count, int rev, int *wrapped)
{
	Pos pos = cur();
	int delim = dirc;
	if (!dirc) dirc = off.dir;
	else off.dir = dirc;
	if (rev) dirc = dirc == '/' ? '?' : '/';
	char buf[sizeof spat];
	if (pat) {
		/* the pattern is copied: skip_regexp_ex() may change "\?", and the
		   caller keeps pat for redo */
		snprintf(buf, sizeof buf, "%s", pat);
		char *p = re_skip(buf, delim, NULL);
		if (*p == delim) *p++ = 0;
		off.line = off.end = 0, off.off = 0;
		if (*p == '+' || *p == '-' || (*p >= '0' && *p <= '9')) off.line = 1;
		else if (*p == 'e' || *p == 's' || *p == 'b') off.end = *p++ == 'e';
		if ((*p >= '0' && *p <= '9') || *p == '+' || *p == '-')
			off.off = (*p >= '0' && *p <= '9') || (p[1] >= '0' && p[1] <= '9') ? strtol(p, NULL, 10) : *p == '-' ? -1 : 1;
		if (*buf) snprintf(spat, sizeof spat, "%s", buf), has_spat = 1, spat_noscs = no_smartcase;
	}
	no_smartcase = 0;
	if (!has_spat) { core_msg(E, "E35: No previous regular expression"); return 0; }
	int all;
	re_skip(spat, 0, &all);
	const char *msg;
	if (re_compile(spat, spat_noscs || !has_upper(spat, all), &msg)) {
		/* searchit(): the NFA engine's E866 E867 E869 leave it to say */
		if (!strncmp(msg, "E86", 3)) core_msg(E, "E383: Invalid search string: %s", spat);
		else core_msg(E, "%s", msg);
		return 0;
	}
	char ob[40] = "";
	if (off.line || off.end || off.off) {
		int k = snprintf(ob, sizeof ob, "%c%s", dirc, off.end ? "e" : off.line ? "" : "s");
		if (off.off || off.line) snprintf(ob + k, sizeof ob - (size_t)k, "%+ld", off.off);
	}
	core_msg(E, "%c%s%s", dirc, spat, ob);
	/* a character offset was added after the last match: start before it */
	if (!off.line && off.off && pos.col < MAXCOL - 2) {
		long c = off.off;
		if (c > 0) { for (; c && decl(&pos) != -1; c--) {} if (c) pos.lnum = -1, pos.col = MAXCOL; }
		else { for (; c && incl(&pos) != -1; c++) {} if (c) pos.lnum = nlines(), pos.col = 0; }
	}
	int w = 0;
	if (!searchit(&pos, dirc == '/' ? FORWARD : BACKWARD, count, off.end, &w)) return 0;
	if (w) core_msg(E, dirc == '/' ? "search hit BOTTOM, continuing at TOP" : "search hit TOP, continuing at BOTTOM");
	if (wrapped) *wrapped = w;
	if (off.end && oap) oap->inclusive = 1;
	int ret = 1;
	if (off.line) {
		long l = pos.lnum + off.off;
		pos.lnum = l < 0 ? 0 : l >= nlines() ? nlines() - 1 : l, pos.col = 0, ret = 2;
	} else if (pos.col < MAXCOL - 2) {
		for (long c = off.off; c > 0 && incl(&pos) != -1; c--) {}
		for (long c = off.off; c < 0 && decl(&pos) != -1; c++) {}
	}
	setcur(pos);
	set_curswant = 1;
	return ret;
}
