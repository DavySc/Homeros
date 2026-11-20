/* Neovim's search.c: / ? n N * # and the last pattern with its offset.
   'ignorecase', 'smartcase' and 'wrapscan' are on (FEATURES.md 1). */
#include <stdio.h>
#include "k.h"
#include "loop.h"
#include "undo.h"
#include "uni.h"

/* spats[]: the last search pattern (RE_SEARCH) and the last :s one
   (RE_SUBST), with 'smartcase' off for * #; last_idx: which was used last */
static struct { char pat[sizeof ((Editor *)0)->cmd + 1]; int set, noscs; } spats[2];
static int last_idx;
static char used[sizeof spats[0].pat];   /* mr_pattern: the last compiled */
static int used_ic;
const char *get_search_pat(void) { return used; }
static struct { int dir, line, end; long off; } off = { '/', 0, 0, 0 };
int no_smartcase;   /* * and # take 'ignorecase' alone */
int no_hlsearch;    /* :noh, CTRL-L: matches not shown until the next search */

void search_reset(void)
{
	memset(spats, 0, sizeof spats);
	last_idx = no_smartcase = no_hlsearch = 0;
	off.dir = '/', off.line = off.end = 0, off.off = 0;
}

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

/* ic, or 'ignorecase' with 'smartcase' */
static int ic_of(char *pat, int noscs, int ic)
{
	int all;
	re_skip(pat, 0, &all);
	return ic >= 0 ? ic : noscs || !has_upper(pat, all);
}

/* search_regcomp(): pat, or for none spats[use] (RE_LAST: the last used),
   kept as spats[save] (-1: not kept); ic -1 takes 'ignorecase' and
   'smartcase'.  0 compiled; 1 no pattern, said; -1 *msg says why not. */
int search_regcomp(const char *pat, int save, int use, int ic, const char **msg)
{
	int noscs = no_smartcase, i = -1;
	no_smartcase = 0;
	if (!pat || !*pat) {
		i = use == RE_LAST ? last_idx : use;
		if (!spats[i].set) {
			core_msg(E, use == RE_SUBST ? "E33: No previous substitute regular expression" : "E35: No previous regular expression");
			return 1;
		}
		pat = spats[i].pat, noscs = spats[i].noscs;
	}
	if (pat != used) snprintf(used, sizeof used, "%s", pat);
	/* save_re_pat(): a pattern kept anew shows its matches */
	if (save >= 0) {
		no_hlsearch &= i == save;
		snprintf(spats[save].pat, sizeof spats[save].pat, "%s", used), spats[save].set = 1, spats[save].noscs = noscs, last_idx = save;
	}
	return re_compile(used, used_ic = ic_of(used, noscs, ic), msg);
}

/* mr_pattern compiled again as search_regcomp() did ('hlsearch' compiles
   its own while :s asks) */
void search_recompile(void)
{
	const char *msg;
	re_compile(used, used_ic, &msg);
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
	if (!found) { core_msg(E, "E486: Pattern not found: %s", used); return 0; }
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
	no_hlsearch = 0;
	if (!dirc) dirc = off.dir;
	else off.dir = dirc;
	if (rev) dirc = dirc == '/' ? '?' : '/';
	char buf[sizeof used];
	const char *sp = NULL;   /* the pattern typed, NULL for the last used */
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
		if (*buf) sp = buf;
	}
	/* no search pattern yet: the :s one */
	if (!sp && !spats[RE_SEARCH].set) {
		if (!spats[RE_SUBST].set) { no_smartcase = 0; core_msg(E, "E35: No previous regular expression"); return 0; }
		sp = spats[RE_SUBST].pat;
	}
	const char *msg;
	int r = search_regcomp(sp, RE_SEARCH, RE_LAST, -1, &msg);
	if (r > 0) return 0;
	if (r) {
		/* searchit(): the NFA engine's E866 E867 E869 leave it to say */
		if (!strncmp(msg, "E86", 3)) core_msg(E, "E383: Invalid search string: %s", used);
		else core_msg(E, "%s", msg);
		return 0;
	}
	char ob[40] = "";
	if (off.line || off.end || off.off) {
		int k = snprintf(ob, sizeof ob, "%c%s", dirc, off.end ? "e" : off.line ? "" : "s");
		if (off.off || off.line) snprintf(ob + k, sizeof ob - (size_t)k, "%+ld", off.off);
	}
	core_msg(E, "%c%s%s", dirc, spats[RE_SEARCH].pat, ob);
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

/* One window's lines drawn from the top down are one redraw.  It has
   'redrawtime' (2 s) for its matches, past which re_exec() gives up at
   once and the rest are not drawn (Neovim's search_hl tm), and a match that crosses lines is searched for
   from where the line before left off, not again from the top. */
typedef struct { const Win *w; Buf *b; size_t top, line; unsigned long seq; long long end; Pos p; } Frame;
static Frame fr;

/* next_search_hl() from the window's top line on, as prepare_search_hl()
   goes (a match that starts above the window is not drawn) */
static int hl(size_t line, size_t col, size_t *s, size_t *z)
{
	const char *msg;
	if (no_hlsearch || !spats[last_idx].set || re_compile(spats[last_idx].pat, ic_of(spats[last_idx].pat, spats[last_idx].noscs, -1), &msg)) return 0;
	if (!col) {
		buf_changed();   /* the text may have changed since the last key */
		if (fr.w != W || fr.b != B || fr.top != W->top || line <= fr.line || fr.seq != undo_seq(B->undo))
			fr = (Frame){ W, B, W->top, line, undo_seq(B->undo), loop_now() + 2000000000LL, { (long)W->top, 0, 0 } };
		fr.line = line;
	}
	long l = (long)line;
	Pos p = !col && line > W->top && re_hasnl() && fr.p.lnum <= l ? fr.p : (Pos){ l, (long)col, 0 }, ms, me;
	re_deadline = fr.end;
	for (int r;;) {
		fr.p = p;
		if ((r = re_exec(p.lnum, p.col, &ms, &me, NULL, NULL)) < 0 || (!r && p.lnum == l)) return re_deadline = 0;
		if (!r) p.lnum++, p.col = 0;
		/* one that \zs starts on a later line belongs to that line, which
		   draws it unless it is empty: then it searches on from its end */
		else if (ms.lnum > l) return re_deadline = 0;
		else if (ms.lnum > p.lnum && eq(ms, me)) p = me;
		else if (ms.lnum == l || me.lnum > l || (me.lnum == l && me.col)) {
			*s = (size_t)(ms.lnum < l ? 0 : ms.col), *z = (size_t)(me.lnum > l ? ml_len(l) + 1 : me.col);
			return re_deadline = 0, 1;
		} else if (!eq(ms, me)) p = me;
		else if (me.col < ml_len(me.lnum)) p.col = me.col + clen_at(me);
		else p.lnum = me.lnum + 1, p.col = 0;
	}
}

/* keys_hl(): the keys read the current window and buffer, so w is made
   them while it runs.  re_compile() keeps an unchanged pattern, so nothing
   allocates after the first call but the line copies growing to the
   longest line (buf.c, slot()). */
int keys_hl(Editor *e, const Win *w, size_t line, size_t col, size_t *s, size_t *z)
{
	Win *ow = e->win;
	Buf *ob = e->buf;
	E = e, e->win = (Win *)w, e->buf = w->buf;
	int r = hl(line, col, s, z);
	e->win = ow, e->buf = ob;
	return r;
}
