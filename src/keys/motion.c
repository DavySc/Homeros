/* Motions and text objects: Neovim 0.12's textobject.c and search.c
   (fwd_word() and friends, findsent(), findpar(), findmatchlimit(),
   searchc(), current_*()), on the positions of buf.c. */
#include <string.h>
#include "k.h"

/* utf_class() (mbyte.c) for characters above Latin-1: the intervals where
   it is not 2 (a word character), generated from nvim 0.12's charclass()
   over every code point by gen_class.lua (emoji are 3, CJK and other
   scripts their own class). */
static const struct { unsigned first, last, cls; } classes[] = {
#include "class.inc"
};

/* cls() with 'iskeyword' @,48-57,_,192-255 below 0x100 */
int cls_of(int c)
{
	if (c == ' ' || c == '\t' || c == 0 || c == 0xa0) return 0;
	if (c < 0x100)
		return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c >= 0xc0 ? 2 : 1;
	int bot = 0, top = (int)(sizeof classes / sizeof *classes) - 1;
	while (top >= bot) {
		int mid = (bot + top) / 2;
		if (classes[mid].last < (unsigned)c) bot = mid + 1;
		else if (classes[mid].first > (unsigned)c) top = mid - 1;
		else return (int)classes[mid].cls;
	}
	return 2;
}

static int cls_bigword;

static int cls(void)
{
	int c = gchar_cursor();
	if (c == ' ' || c == '\t' || c == 0) return 0;
	c = cls_of(c);
	if (c != 0 && cls_bigword) return 1;
	return c;
}

static int cursor_lineempty(void) { return W->col == 0 && lineempty((long)W->line); }

int fwd_word(long count, int bigword, int eol)
{
	cls_bigword = bigword;
	setcur((Pos){ (long)W->line, (long)W->col, 0 });
	while (--count >= 0) {
		int sclass = cls(), last_line = (long)W->line + 1 == nlines();
		int i = inc_cursor();
		if (i == -1 || (i >= 1 && last_line)) return 0;
		if (i >= 1 && eol && count == 0) return 1;
		if (sclass != 0)
			while (cls() == sclass) {
				i = inc_cursor();
				if (i == -1 || (i >= 1 && eol && count == 0)) return 1;
			}
		while (cls() == 0) {
			if (cursor_lineempty()) break;
			i = inc_cursor();
			if (i == -1 || (i >= 1 && eol && count == 0)) return 1;
		}
	}
	return 1;
}

static int skip_chars(int cclass, int dir)
{
	while (cls() == cclass)
		if ((dir == FORWARD ? inc_cursor() : dec_cursor()) == -1) return 1;
	return 0;
}

int bck_word(long count, int bigword, int stop)
{
	cls_bigword = bigword;
	setcur((Pos){ (long)W->line, (long)W->col, 0 });
	while (--count >= 0) {
		int sclass = cls();
		if (dec_cursor() == -1) return 0;
		if (!stop || sclass == cls() || sclass == 0) {
			while (cls() == 0) {
				if (cursor_lineempty()) goto finished;
				if (dec_cursor() == -1) return 1;
			}
			if (skip_chars(cls(), BACKWARD)) return 1;
		}
		inc_cursor();
finished:
		stop = 0;
	}
	return 1;
}

int end_word(long count, int bigword, int stop, int empty)
{
	cls_bigword = bigword;
	setcur((Pos){ (long)W->line, (long)W->col, 0 });
	while (--count >= 0) {
		int sclass = cls();
		if (inc_cursor() == -1) return 0;
		if (cls() == sclass && sclass != 0) {
			if (skip_chars(sclass, FORWARD)) return 0;
		} else if (!stop || sclass == 0) {
			while (cls() == 0) {
				if (empty && cursor_lineempty()) goto finished;
				if (inc_cursor() == -1) return 0;
			}
			if (skip_chars(cls(), FORWARD)) return 0;
		}
		dec_cursor();
finished:
		stop = 0;
	}
	return 1;
}

int bckend_word(long count, int bigword, int eol)
{
	cls_bigword = bigword;
	setcur((Pos){ (long)W->line, (long)W->col, 0 });
	while (--count >= 0) {
		int i, sclass = cls();
		if ((i = dec_cursor()) == -1) return 0;
		if (eol && i == 1) return 1;
		if (sclass != 0)
			while (cls() == sclass)
				if ((i = dec_cursor()) == -1 || (eol && i == 1)) return 1;
		while (cls() == 0) {
			if (cursor_lineempty()) break;
			if ((i = dec_cursor()) == -1 || (eol && i == 1)) return 1;
		}
	}
	return 1;
}

static void back_in_line(void)
{
	int sclass = cls();
	for (;;) {
		if (W->col == 0) break;
		dec_cursor();
		if (cls() != sclass) { inc_cursor(); break; }
	}
}

static int is_white(int c) { return c == ' ' || c == '\t'; }

static void find_first_blank(Pos *posp)
{
	while (decl(posp) != -1) {
		int c = gchar(*posp);
		if (!is_white(c)) { incl(posp); break; }
	}
}

/* 'paragraphs' and 'sections' at their defaults: nroff macros */
static int inmacro(const char *opt, const char *s)
{
	const char *m;
	for (m = opt; m[0]; m++) {
		if ((m[0] == s[0] || (m[0] == ' ' && (s[0] == 0 || s[0] == ' ')))
		    && (m[1] == s[1] || ((m[1] == 0 || m[1] == ' ') && (s[0] == 0 || s[1] == 0 || s[1] == ' '))))
			break;
		m++;
		if (m[0] == 0) break;
	}
	return m[0] != 0;
}

int startPS(long l, int para, int both)
{
	const char *s = ml_get(l);
	if ((unsigned char)*s == para || *s == '\f' || (both && *s == '}')) return 1;
	if (*s == '.' && (inmacro("SHNHH HUnhsh", s + 1) || (!para && inmacro("IPLPPPQPP TPHPLIPpLpItpplpipbp", s + 1))))
		return 1;
	return 0;
}

int findsent(int dir, long count)
{
	int c, noskip = 0;
	int (*func)(Pos *) = dir == FORWARD ? incl : decl;
	Pos pos = cur();
	while (count--) {
		Pos prev_pos = pos;
		if (gchar(pos) == 0) {
			do if (func(&pos) == -1) break;
			while (gchar(pos) == 0);
			if (dir == FORWARD) goto found;
		} else if (dir == FORWARD && pos.col == 0 && startPS(pos.lnum, 0, 0)) {
			if (pos.lnum + 1 == nlines()) return 0;
			pos.lnum++;
			goto found;
		} else if (dir == BACKWARD) decl(&pos);
		int found_dot = 0;
		while (c = gchar(pos), is_white(c) || has(".!?)]\"'", c)) {
			Pos tpos = pos;
			if (decl(&tpos) == -1 || (lineempty(tpos.lnum) && dir == FORWARD)) break;
			if (found_dot) break;
			if (has(".!?", c)) found_dot = 1;
			if (has(")]\"'", c) && !has(".!?)]\"'", gchar(tpos))) break;
			decl(&pos);
		}
		long startlnum = pos.lnum;
		for (;;) {
			c = gchar(pos);
			if (c == 0 || (pos.col == 0 && startPS(pos.lnum, 0, 0))) {
				if (dir == BACKWARD && pos.lnum != startlnum) pos.lnum++;
				break;
			}
			if (c == '.' || c == '!' || c == '?') {
				Pos tpos = pos;
				do if ((c = inc(&tpos)) == -1) break;
				while (has(")]\"'", c = gchar(tpos)));
				if (c == -1 || c == ' ' || c == '\t' || c == 0) {
					pos = tpos;
					if (gchar(pos) == 0) inc(&pos);
					break;
				}
			}
			if (func(&pos) == -1) {
				if (count) return 0;
				noskip = 1;
				break;
			}
		}
found:
		while (!noskip && ((c = gchar(pos)) == ' ' || c == '\t'))
			if (incl(&pos) == -1) break;
		if (eq(prev_pos, pos)) {
			if (func(&pos) == -1) {
				if (count) return 0;
				break;
			}
			count++;
		}
	}
	setcur(pos);
	return 1;
}

int findpar(int *pincl, int dir, long count, int what, int both)
{
	long curr = (long)W->line;
	while (count--) {
		int did_skip = 0;
		for (int first = 1;; first = 0) {
			if (*ml_get(curr)) did_skip = 1;
			if (!first && did_skip && startPS(curr, what, both)) break;
			if ((curr += dir) < 0 || curr >= nlines()) {
				if (count) return 0;
				curr -= dir;
				break;
			}
		}
	}
	if (both && *ml_get(curr) == '}') curr++;
	W->line = (size_t)curr;
	if (curr + 1 == nlines() && what != '}' && dir == FORWARD) {
		long n = ml_len(curr);
		W->col = (size_t)n;
		if (n != 0) {
			W->col = (size_t)head(ml_get(curr), n - 1);
			*pincl = 1;
		}
	} else W->col = 0;
	return 1;
}

/* ---- % ---- */

/* 'matchpairs', each pair followed by a comma for the loop below; the
   text objects add <:> (nv_object()) */
const char *p_mps = "(:),{:},[:],";

static void find_mps_values(int *initc, int *findc, int *backwards, int switchit)
{
	for (const char *p = p_mps; *p; p += 4) {
		if (p[0] == *initc) {
			if (switchit) { *findc = *initc; *initc = p[2]; *backwards = 1; }
			else { *findc = p[2]; *backwards = 0; }
			return;
		}
		if (p[2] == *initc) {
			if (switchit) { *findc = *initc; *initc = p[0]; *backwards = 0; }
			else { *findc = p[0]; *backwards = 1; }
			return;
		}
	}
}

static int check_prevcol(const char *linep, long col, int ch, long *prevcol)
{
	col--;
	if (col > 0) col = head(linep, col);
	if (prevcol) *prevcol = col;
	return col >= 0 && (unsigned char)linep[col] == ch;
}

/* check_linecomment(): where a "//" comment starts, outside a string */
static long check_linecomment(const char *line)
{
	const char *p = line;
	while ((p = strchr(p, '/')) != NULL) {
		if (p[1] == '/' && (p == line || p[-1] != '*' || p[1] != '*')) {
			/* an odd number of double quotes before it: inside a string */
			int inq = 0;
			for (const char *q = line; q < p; q++) {
				if (*q == '\\' && q[1]) q++;
				else if (*q == '"') inq = !inq;
			}
			if (!inq) break;
		}
		p++;
	}
	return p ? (long)(p - line) : MAXCOL;
}

static const char *skipwhite(const char *s) { while (*s == ' ' || *s == '\t') s++; return s; }

int findmatch(Pos *out, int initc) { return findmatchlimit(out, initc, 0); }

int findmatchlimit(Pos *out, int initc, int flags)
{
	Pos pos = cur(), match_pos = { 0, 0, 0 };
	pos.coladd = 0;
	int findc = 0, count = 0, backwards = 0, inquote = 0, hash_dir = 0, comment_dir = 0;
	int ignore_cend = 0, match_escaped = 0, dir;
	long comment_col = MAXCOL;
	const char *linep = ml_get(pos.lnum), *ptr;
	dir = flags & FM_BACKWARD ? BACKWARD : flags & FM_FORWARD ? FORWARD : 0;
	if (initc == '/' || initc == '*') {
		comment_dir = dir;
		if (initc == '/') ignore_cend = 1;
		backwards = dir != FORWARD;
		initc = 0;
	} else if (initc != '#' && initc != 0) {
		find_mps_values(&initc, &findc, &backwards, 1);
		if (dir) backwards = dir != FORWARD;
		if (findc == 0) return 0;
	} else {
		if (initc == '#') hash_dir = dir;
		else {
			ptr = skipwhite(linep);
			if (*ptr == '#' && pos.col <= (long)(ptr - linep)) {
				ptr = skipwhite(ptr + 1);
				if (!strncmp(ptr, "if", 2) || !strncmp(ptr, "endif", 5) || !strncmp(ptr, "el", 2)) hash_dir = 1;
			} else if (linep[pos.col] == '/') {
				if (linep[pos.col + 1] == '*') { comment_dir = FORWARD; backwards = 0; pos.col++; }
				else if (pos.col > 0 && linep[pos.col - 1] == '*') { comment_dir = BACKWARD; backwards = 1; pos.col--; }
			} else if (linep[pos.col] == '*') {
				if (linep[pos.col + 1] == '/') { comment_dir = BACKWARD; backwards = 1; }
				else if (pos.col > 0 && linep[pos.col - 1] == '/') { comment_dir = FORWARD; backwards = 0; }
			}
			if (!hash_dir && !comment_dir) {
				long len = ml_len(pos.lnum);
				if (linep[pos.col] == 0 && pos.col) pos.col--;
				for (;;) {
					initc = (unsigned char)linep[pos.col];
					if (initc == 0) break;
					find_mps_values(&initc, &findc, &backwards, 0);
					if (findc) break;
					pos.col += clen(linep, len, pos.col);
				}
				if (!findc) {
					if (*skipwhite(linep) == '#') hash_dir = 1;
					else return 0;
				} else {
					int bslcnt = 0;
					for (long col = pos.col; check_prevcol(linep, col, '\\', &col);) bslcnt++;
					match_escaped = bslcnt & 1;
				}
			}
		}
		if (hash_dir) {
			if (initc != '#') {
				ptr = skipwhite(skipwhite(linep) + 1);
				if (!strncmp(ptr, "if", 2) || !strncmp(ptr, "el", 2)) hash_dir = 1;
				else if (!strncmp(ptr, "endif", 5)) hash_dir = -1;
				else return 0;
			}
			pos.col = 0;
			for (;;) {
				if (hash_dir > 0) { if (pos.lnum + 1 == nlines()) break; }
				else if (pos.lnum == 0) break;
				pos.lnum += hash_dir;
				linep = ml_get(pos.lnum);
				ptr = skipwhite(linep);
				if (*ptr != '#') continue;
				pos.col = (long)(ptr - linep);
				ptr = skipwhite(ptr + 1);
				if (hash_dir > 0) {
					if (!strncmp(ptr, "if", 2)) count++;
					else if (!strncmp(ptr, "el", 2)) { if (count == 0) goto ok_line; }
					else if (!strncmp(ptr, "endif", 5)) { if (count == 0) goto ok_line; count--; }
				} else {
					if (!strncmp(ptr, "if", 2)) { if (count == 0) goto ok_line; count--; }
					else if (initc == '#' && !strncmp(ptr, "el", 2)) { if (count == 0) goto ok_line; }
					else if (!strncmp(ptr, "endif", 5)) count++;
				}
			}
			return 0;
ok_line:
			*out = pos;
			return 2;     /* linewise (oap->motion_type = kMTLineWise) */
		}
	}
	int do_quotes = -1, at_start = 0, start_in_quotes = -1;
	if (backwards && comment_dir) comment_col = check_linecomment(linep);
	for (;;) {
		if (backwards) {
			if (pos.col == 0) {
				if (pos.lnum == 0) break;
				pos.lnum--;
				linep = ml_get(pos.lnum);
				pos.col = ml_len(pos.lnum);
				do_quotes = -1;
				if (comment_dir) comment_col = check_linecomment(linep);
			} else {
				pos.col--;
				pos.col = head(linep, pos.col);
			}
		} else {
			if (linep[pos.col] == 0) {
				if (pos.lnum + 1 == nlines()) break;
				pos.lnum++;
				linep = ml_get(pos.lnum);
				pos.col = 0;
				do_quotes = -1;
			} else pos.col += clen(linep, ml_len(pos.lnum), pos.col);
		}
		if (pos.col == 0 && (flags & FM_BLOCKSTOP) && (linep[0] == '{' || linep[0] == '}')) {
			if (linep[0] == findc && count == 0) goto found;
			break;
		}
		if (comment_dir) {
			if (comment_dir == FORWARD) {
				if (linep[pos.col] == '*' && linep[pos.col + 1] == '/') { pos.col++; goto found; }
			} else {
				if (pos.col == 0) continue;
				else if (linep[pos.col - 1] == '/' && linep[pos.col] == '*'
				         && (pos.col == 1 || linep[pos.col - 2] != '*') && pos.col < comment_col) {
					count++;
					match_pos = pos;
					match_pos.col--;
				} else if (linep[pos.col - 1] == '*' && linep[pos.col] == '/') {
					if (count > 0) pos = match_pos;
					else if (pos.col > 1 && linep[pos.col - 2] == '/' && pos.col <= comment_col) pos.col -= 2;
					else if (ignore_cend) continue;
					else return 0;
					goto found;
				}
			}
			continue;
		}
		if (do_quotes == -1) {
			const char *p;
			at_start = do_quotes;
			for (p = linep; *p; p++) {
				if (p == linep + pos.col + backwards) at_start = do_quotes & 1;
				if (*p == '"' && (p == linep || p[-1] != '\'' || p[1] != '\'')) do_quotes++;
				if (*p == '\\' && p[1] != 0) p++;
			}
			do_quotes &= 1;
			if (!do_quotes) {
				inquote = 0;
				if (p > linep && p[-1] == '\\') {
					do_quotes = 1;
					if (start_in_quotes == -1) { inquote = 1; start_in_quotes = 1; }
					else if (backwards) inquote = 1;
				}
				if (pos.lnum > 0) {
					const char *pl = ml_get(pos.lnum - 1);
					long pn = ml_len(pos.lnum - 1);
					if (*pl && pl[pn - 1] == '\\') {
						do_quotes = 1;
						if (start_in_quotes == -1) {
							inquote = at_start;
							if (inquote) start_in_quotes = 1;
						} else if (!backwards) inquote = 1;
					}
					linep = ml_get(pos.lnum);
				}
			}
		}
		if (start_in_quotes == -1) start_in_quotes = 0;
		int c = (unsigned char)linep[pos.col];
		switch (c) {
		case 0:
			if (pos.col == 0 || linep[pos.col - 1] != '\\') { inquote = 0; start_in_quotes = 0; }
			break;
		case '"':
			if (do_quotes) {
				long col;
				for (col = pos.col - 1; col >= 0; col--) if (linep[col] != '\\') break;
				if (((pos.col - 1 - col) & 1) == 0) { inquote = !inquote; start_in_quotes = 0; }
			}
			break;
		case '\'':
			if (initc != '\'' && findc != '\'') {
				if (backwards) {
					if (pos.col > 1) {
						if (linep[pos.col - 2] == '\'') { pos.col -= 2; break; }
						else if (linep[pos.col - 2] == '\\' && pos.col > 2 && linep[pos.col - 3] == '\'') { pos.col -= 3; break; }
					}
				} else if (linep[pos.col + 1]) {
					if (linep[pos.col + 1] == '\\' && linep[pos.col + 2] && linep[pos.col + 3] == '\'') { pos.col += 3; break; }
					else if (linep[pos.col + 2] == '\'') { pos.col += 2; break; }
				}
			}
			/* fallthrough */
		default:
			if ((!inquote || start_in_quotes == 1) && (c == initc || c == findc)) {
				int bslcnt = 0;
				for (long col = pos.col; check_prevcol(linep, col, '\\', &col);) bslcnt++;
				if ((bslcnt & 1) == match_escaped) {
					if (c == initc) count++;
					else {
						if (count == 0) goto found;
						count--;
					}
				}
			}
		}
	}
	if (comment_dir == BACKWARD && count > 0) { pos = match_pos; goto found; }
	return 0;
found:
	*out = pos;
	return 1;
}

/* ---- f t F T ; , ---- */

static int lastc, lastcdir = FORWARD, last_t_cmd;
void motion_reset(void) { lastc = last_t_cmd = 0; lastcdir = FORWARD; }
extern int key_stuffed;
extern int searchc_inclusive;

/* repeat: 0 for f/t/F/T, 1 for ";", -1 for "," */
int searchc(int c, int dir, int t_cmd, long count, int repeat)
{
	int stop = 1;
	if (!repeat) {
		if (!key_stuffed) { lastc = c; lastcdir = dir; last_t_cmd = t_cmd; }
	} else {
		if (lastc == 0) return 0;
		dir = repeat < 0 ? -lastcdir : lastcdir;
		t_cmd = last_t_cmd;
		c = lastc;
		/* without cpo-; a ";" after "t" does not stay put */
		if (count == 1 && t_cmd) stop = 0;
	}
	searchc_inclusive = dir != BACKWARD;
	const char *p = ml_get((long)W->line);
	long col = (long)W->col, len = ml_len((long)W->line);
	while (count--) {
		for (;;) {
			if (dir > 0) {
				col += clen(p, len, col);
				if (col >= len) return 0;
			} else {
				if (col == 0) return 0;
				col = head(p, col - 1);
			}
			Pos q = { (long)W->line, col, 0 };
			if (gchar(q) == c && stop) break;
			stop = 1;
		}
	}
	if (t_cmd) {
		if (dir < 0) col += clen(p, len, col);
		else col = head(p, col - 1);
	}
	W->col = (size_t)col;
	return 1;
}

/* ---- text objects ---- */

int current_word(Oparg *oap, long count, int include, int bigword)
{
	Pos start_pos = { 0, 0, 0 };
	int inclusive = 1, include_white = 0;
	cls_bigword = bigword;
	if (!VIsual_active || eq(cur(), VIsual)) {
		back_in_line();
		start_pos = cur();
		if ((cls() == 0) == include) {
			if (!end_word(1, bigword, 1, 1)) return 0;
		} else {
			fwd_word(1, bigword, 1);
			if (W->col == 0) { Pos p = cur(); decl(&p); setcur(p); }
			else oneleft();
			if (include) include_white = 1;
		}
		if (VIsual_active) VIsual = start_pos;
		else { oap->start = start_pos; oap->motion_type = MCHAR; }
		count--;
	}
	while (count > 0) {
		inclusive = 1;
		if (VIsual_active && lt(cur(), VIsual)) {
			Pos p = cur();
			if (decl(&p) == -1) return 0;
			setcur(p);
			if (include != (cls() != 0)) {
				if (!bck_word(1, bigword, 1)) return 0;
			} else {
				if (!bckend_word(1, bigword, 1)) return 0;
				p = cur(); incl(&p); setcur(p);
			}
		} else {
			Pos p = cur();
			if (incl(&p) == -1) return 0;
			setcur(p);
			if (include != (cls() == 0)) {
				if (!fwd_word(1, bigword, 1) && count > 1) return 0;
				if (!oneleft()) inclusive = 0;
			} else {
				if (!end_word(1, bigword, 1, 1)) return 0;
			}
		}
		count--;
	}
	if (include_white && (cls() != 0 || (W->col == 0 && !inclusive))) {
		/* no trailing white space: include white space before the word */
		Pos pos = cur();
		setcur(start_pos);
		if (oneleft()) {
			back_in_line();
			if (cls() == 0 && W->col > 0) {
				if (VIsual_active) VIsual = cur();
				else oap->start = cur();
			}
		}
		setcur(pos);
	}
	if (VIsual_active) {
		if (VIsual_mode == 'V') VIsual_mode = 'v';
	} else oap->inclusive = inclusive;
	return 1;
}

static void findsent_forward(long count, int at_start_sent)
{
	while (count--) {
		findsent(FORWARD, 1);
		if (at_start_sent) { Pos p = cur(); find_first_blank(&p); setcur(p); }
		if (count == 0 || at_start_sent) { Pos p = cur(); decl(&p); setcur(p); }
		at_start_sent = !at_start_sent;
	}
}

int current_sent(Oparg *oap, long count, int include)
{
	int start_blank, c, at_start_sent;
	long ncount;
	Pos start_pos = cur(), pos = start_pos;
	findsent(FORWARD, 1);
	if (VIsual_active && !eq(start_pos, VIsual)) {
extend:
		if (lt(start_pos, VIsual)) {
			at_start_sent = 1;
			decl(&pos);
			while (lt(pos, cur())) {
				c = gchar(pos);
				if (!is_white(c)) { at_start_sent = 0; break; }
				incl(&pos);
			}
			if (!at_start_sent) {
				findsent(BACKWARD, 1);
				if (eq(cur(), start_pos)) at_start_sent = 1;
				else findsent(FORWARD, 1);
			}
			if (include) count *= 2;
			while (count--) {
				if (at_start_sent) { Pos p = cur(); find_first_blank(&p); setcur(p); }
				c = gchar_cursor();
				if (!at_start_sent || (!include && !is_white(c))) findsent(BACKWARD, 1);
				at_start_sent = !at_start_sent;
			}
		} else {
			incl(&pos);
			at_start_sent = 1;
			if (!eq(pos, cur())) {
				at_start_sent = 0;
				while (lt(pos, cur())) {
					c = gchar(pos);
					if (!is_white(c)) { at_start_sent = 1; break; }
					incl(&pos);
				}
				if (at_start_sent) findsent(BACKWARD, 1);
				else setcur(start_pos);
			}
			if (include) count *= 2;
			findsent_forward(count, at_start_sent);
		}
		return 1;
	}
	while (c = gchar(pos), is_white(c)) incl(&pos);
	if (eq(pos, cur())) {
		start_blank = 1;
		find_first_blank(&start_pos);
	} else {
		start_blank = 0;
		findsent(BACKWARD, 1);
		start_pos = cur();
	}
	if (include) ncount = count * 2;
	else {
		ncount = count;
		if (start_blank) ncount--;
	}
	if (ncount > 0) findsent_forward(ncount, 1);
	else { Pos p = cur(); decl(&p); setcur(p); }
	if (include) {
		if (start_blank) {
			Pos p = cur();
			find_first_blank(&p);
			setcur(p);
			c = gchar(p);
			if (is_white(c)) { decl(&p); setcur(p); }
		} else if (c = gchar_cursor(), !is_white(c)) find_first_blank(&start_pos);
	}
	if (VIsual_active) {
		if (eq(start_pos, cur())) goto extend;
		VIsual = start_pos;
		VIsual_mode = 'v';
	} else {
		Pos p = cur();
		oap->inclusive = incl(&p) == -1;
		setcur(p);
		oap->start = start_pos;
		oap->motion_type = MCHAR;
	}
	return 1;
}

static int inindent(int extra)
{
	const char *s = ml_get((long)W->line);
	long col = 0;
	while (is_white(s[col])) col++;
	return col >= (long)W->col + extra;
}

int current_block(Oparg *oap, long count, int include, int what, int other)
{
	Pos start_pos = { 0, 0, 0 }, end_pos, p, old_pos = cur(), old_end = cur(), old_start = old_end;
	int sol = 0, ok;
	if (!VIsual_active || eq(VIsual, cur())) {
		if (what == '{')
			while (inindent(1)) if (inc_cursor() != 0) break;
		if (gchar_cursor() == what) W->col++;
	} else if (lt(VIsual, cur())) {
		old_start = VIsual;
		setcur(VIsual);
	} else old_end = VIsual;
	/* findmatch() with 'cpoptions' "%": quotes are not special */
	if ((ok = findmatch(&p, what))) {
		while (count-- > 0) {
			if (!(ok = findmatch(&p, what))) break;
			setcur(p);
			start_pos = p;
		}
	} else {
		while (count-- > 0) {
			if (!(ok = findmatchlimit(&p, what, FM_FORWARD))) break;
			setcur(p);
			start_pos = p;
		}
	}
	if (!ok || !findmatch(&end_pos, other)) { setcur(old_pos); return 0; }
	setcur(end_pos);
	while (!include) {
		incl(&start_pos);
		sol = W->col == 0;
		p = cur(); decl(&p); setcur(p);
		while (inindent(1)) {
			sol = 1;
			p = cur();
			if (decl(&p) != 0) { setcur(p); break; }
			setcur(p);
		}
		if (eq(start_pos, end_pos) && VIsual_active) { setcur(old_pos); return 0; }
		if (!lt(start_pos, old_start) && !lt(old_end, cur()) && !eq(start_pos, cur()) && VIsual_active) {
			setcur(old_start);
			p = cur(); decl(&p); setcur(p);
			if (!findmatch(&p, what)) { setcur(old_pos); return 0; }
			start_pos = p;
			setcur(p);
			if (!findmatch(&end_pos, other)) { setcur(old_pos); return 0; }
			setcur(end_pos);
		} else break;
	}
	if (VIsual_active) {
		if (sol && gchar_cursor() != 0) inc_cursor();
		VIsual = start_pos;
		VIsual_mode = 'v';
	} else {
		oap->start = start_pos;
		oap->motion_type = MCHAR;
		oap->inclusive = 0;
		if (sol) { p = cur(); incl(&p); setcur(p); }
		else if (!lt(cur(), start_pos)) oap->inclusive = 1;
		else setcur(start_pos);
	}
	return 1;
}

int current_par(Oparg *oap, long count, int include)
{
	long start_lnum = (long)W->line, end_lnum, last = nlines() - 1;
	int retval = 1, do_white = 0, dir;
	if (VIsual_active && start_lnum != VIsual.lnum) {
extend:
		dir = start_lnum < VIsual.lnum ? BACKWARD : FORWARD;
		for (long i = count; --i >= 0;) {
			if (start_lnum == (dir == BACKWARD ? 0 : last)) { retval = 0; break; }
			int prev_start_is_white = -1;
			for (int t = 0; t < 2; t++) {
				start_lnum += dir;
				int start_is_white = linewhite(start_lnum);
				if (prev_start_is_white == start_is_white) { start_lnum -= dir; break; }
				for (;;) {
					if (start_lnum == (dir == BACKWARD ? 0 : last)) break;
					if (start_is_white != linewhite(start_lnum + dir)
					    || (!start_is_white && startPS(start_lnum + (dir > 0 ? 1 : 0), 0, 0)))
						break;
					start_lnum += dir;
				}
				if (!include) break;
				if (start_lnum == (dir == BACKWARD ? 0 : last)) break;
				prev_start_is_white = start_is_white;
			}
		}
		W->line = (size_t)start_lnum;
		W->col = 0;
		return retval;
	}
	int white_in_front = linewhite(start_lnum);
	while (start_lnum > 0) {
		if (white_in_front) { if (!linewhite(start_lnum - 1)) break; }
		else if (linewhite(start_lnum - 1) || startPS(start_lnum, 0, 0)) break;
		start_lnum--;
	}
	end_lnum = start_lnum;
	while (end_lnum <= last && linewhite(end_lnum)) end_lnum++;
	end_lnum--;
	long i = count;
	if (!include && white_in_front) i--;
	while (i--) {
		if (end_lnum == last) return 0;
		if (!include) do_white = linewhite(end_lnum + 1);
		if (include || !do_white) {
			end_lnum++;
			while (end_lnum < last && !linewhite(end_lnum + 1) && !startPS(end_lnum + 1, 0, 0)) end_lnum++;
		}
		if (i == 0 && white_in_front && include) break;
		if (include || do_white)
			while (end_lnum < last && linewhite(end_lnum + 1)) end_lnum++;
	}
	if (!white_in_front && !linewhite(end_lnum) && include)
		while (start_lnum > 0 && linewhite(start_lnum - 1)) start_lnum--;
	if (VIsual_active) {
		if (VIsual_mode == 'V' && start_lnum == (long)W->line) goto extend;
		if (VIsual.lnum != start_lnum) { VIsual.lnum = start_lnum; VIsual.col = 0; }
		VIsual_mode = 'V';
	} else {
		oap->start.lnum = start_lnum;
		oap->start.col = 0;
		oap->motion_type = MLINE;
	}
	W->line = (size_t)end_lnum;
	W->col = 0;
	return 1;
}

static long find_next_quote(const char *line, long col, int quotechar, const char *escape)
{
	long n = (long)strlen(line);
	for (;;) {
		int c = (unsigned char)line[col];
		if (c == 0) return -1;
		else if (escape && has(escape, c)) {
			col++;
			if (line[col] == 0) return -1;
		} else if (c == quotechar) break;
		col += clen(line, n, col);
	}
	return col;
}

static long find_prev_quote(const char *line, long col_start, int quotechar, const char *escape)
{
	while (col_start > 0) {
		col_start--;
		col_start = head(line, col_start);
		long n = 0;
		if (escape)
			while (col_start - n > 0 && strchr(escape, (unsigned char)line[col_start - n - 1])) n++;
		if (n & 1) col_start -= n;
		else if ((unsigned char)line[col_start] == quotechar) break;
	}
	return col_start;
}

int current_quote(Oparg *oap, long count, int include, int quotechar)
{
	const char *qe = "\\";     /* 'quoteescape' */
	long l = (long)W->line;
	const char *line = ml_get(l);
	long col_end = 0, col_start = (long)W->col, i;
	int inclusive = 0, vis_empty = 1, vis_bef_curs = 0, inside_quotes = 0, selected_quote = 0;
	if (VIsual_active) {
		if (VIsual.lnum != l) return 0;
		vis_bef_curs = lt(VIsual, cur());
		vis_empty = eq(VIsual, cur());
	}
	if (!vis_empty) {
		if (vis_bef_curs) {
			inside_quotes = VIsual.col > 0 && (unsigned char)line[VIsual.col - 1] == quotechar
			                && line[W->col] != 0 && (unsigned char)line[W->col + 1] == quotechar;
			i = VIsual.col;
			col_end = (long)W->col;
		} else {
			inside_quotes = W->col > 0 && (unsigned char)line[W->col - 1] == quotechar
			                && line[VIsual.col] != 0 && (unsigned char)line[VIsual.col + 1] == quotechar;
			i = (long)W->col;
			col_end = VIsual.col;
		}
		while (i <= col_end) {
			if (line[i] == 0) break;
			if ((unsigned char)line[i++] == quotechar) { selected_quote = 1; break; }
		}
	}
	if (!vis_empty && (unsigned char)line[col_start] == quotechar) {
		if (vis_bef_curs) {
			col_start = find_next_quote(line, col_start + 1, quotechar, NULL);
			if (col_start < 0) return 0;
			col_end = find_next_quote(line, col_start + 1, quotechar, qe);
			if (col_end < 0) { col_end = col_start; col_start = (long)W->col; }
		} else {
			col_end = find_prev_quote(line, col_start, quotechar, NULL);
			if ((unsigned char)line[col_end] != quotechar) return 0;
			col_start = find_prev_quote(line, col_end, quotechar, qe);
			if ((unsigned char)line[col_start] != quotechar) { col_start = col_end; col_end = (long)W->col; }
		}
	} else if ((unsigned char)line[col_start] == quotechar || !vis_empty) {
		long first_col = col_start;
		if (!vis_empty) {
			if (vis_bef_curs) first_col = find_next_quote(line, col_start, quotechar, NULL);
			else first_col = find_prev_quote(line, col_start, quotechar, NULL);
		}
		col_start = 0;
		for (;;) {
			col_start = find_next_quote(line, col_start, quotechar, NULL);
			if (col_start < 0 || col_start > first_col) return 0;
			col_end = find_next_quote(line, col_start + 1, quotechar, qe);
			if (col_end < 0) return 0;
			if (col_start <= first_col && first_col <= col_end) break;
			col_start = col_end + 1;
		}
	} else {
		col_start = find_prev_quote(line, col_start, quotechar, qe);
		if ((unsigned char)line[col_start] != quotechar) {
			col_start = find_next_quote(line, col_start, quotechar, NULL);
			if (col_start < 0) return 0;
		}
		col_end = find_next_quote(line, col_start + 1, quotechar, qe);
		if (col_end < 0) return 0;
	}
	if (include) {
		if (is_white((unsigned char)line[col_end + 1]))
			while (is_white((unsigned char)line[col_end + 1])) col_end++;
		else
			while (col_start > 0 && is_white((unsigned char)line[col_start - 1])) col_start--;
	}
	if (!include && count < 2 && (vis_empty || !inside_quotes)) col_start++;
	W->col = (size_t)col_start;
	if (VIsual_active) {
		if (vis_empty || (vis_bef_curs && !selected_quote
		                  && (inside_quotes || ((unsigned char)line[VIsual.col] != quotechar
		                                        && (VIsual.col == 0 || (unsigned char)line[VIsual.col - 1] != quotechar)))))
			VIsual = cur();
	} else {
		oap->start = cur();
		oap->motion_type = MCHAR;
	}
	W->col = (size_t)col_end;
	if ((include || count > 1 || (!vis_empty && inside_quotes)) && inc_cursor() == 2) inclusive = 1;
	if (VIsual_active) {
		if (vis_empty || vis_bef_curs) dec_cursor();
		else {
			if (inside_quotes || (!selected_quote && (unsigned char)line[VIsual.col] != quotechar
			                      && (line[VIsual.col] == 0 || (unsigned char)line[VIsual.col + 1] != quotechar))) {
				dec_cursor();
				VIsual = cur();
			}
			W->col = (size_t)col_start;
		}
		if (VIsual_mode == 'V') VIsual_mode = 'v';
	} else oap->inclusive = inclusive;
	return 1;
}
