/* Neovim's ex_cmds.c do_sub() for :s :& :~, with regexp.c's regtilde()
   and vim_regsub_both() for the replacement.  Not built: \=,
   'inccommand', the p # l flags' printing. */
#include <stdio.h>
#include <stdlib.h>
#include "k.h"
#include "uni.h"

static char *old_sub, *prev_sub, *sub;   /* old_sub: the last :s string; reg_prev_sub: ~; sub: this one's */
static struct { int all, ask, count, error, ic; } fl = { 0, 0, 0, 1, -1 };   /* subflags (ic -1: the options) */
static Pos gs[10], ge[10];   /* the match (0) and its groups, lines from the line searched; coladd: set */
static int timed_out;
static int one, all;         /* regsub's func_one, func_all: 'u' upper, 'l' lower */

void sub_reset(void)
{
	free(old_sub), free(prev_sub), free(sub);
	old_sub = prev_sub = sub = NULL;
	fl.all = fl.ask = fl.count = 0, fl.error = 1, fl.ic = -1;
	sub_asking = 0;
}

/* c, through \u \l \U \L */
static void putc_(Sbuf *b, int c)
{
	int f = one ? one : all;
	one = 0;
	char u[4];
	bput(b, u, uni_put((unsigned char *)u, (unsigned)(f ? map_case(c, f == 'u') : c)));
}

/* vim_regexec_multi(): the match's lines (0 none) */
static long exec(long l, long col)
{
	Pos s, e;
	int r = re_exec(l, col, &s, &e, gs, ge);
	if (r < 0) timed_out = 1;
	if (r <= 0) return 0;
	gs[0] = s, ge[0] = e, gs[0].coladd = ge[0].coladd = 1;
	for (int i = 0; i < 10; i++) gs[i].lnum -= l, ge[i].lnum -= l;
	return ge[0].lnum + 1;
}

/* regtilde(): each ~ (not \~) becomes the last replacement, which this one
   then is */
static char *tilde(const char *src)
{
	Sbuf b = { 0 };
	bput(&b, "", 0);
	for (const char *p = src; *p; p++) {
		if (*p == '~') { if (prev_sub) bput(&b, prev_sub, (long)strlen(prev_sub)); continue; }
		long n = *p == '\\' && p[1] ? 2 : 1;
		bput(&b, p, n);
		p += n - 1;
	}
	free(prev_sub);
	prev_sub = b.n ? strdup(b.s) : NULL;
	return b.s;
}

/* vim_regsub_both() with REGSUB_MAGIC | REGSUB_BACKSLASH: sub with the
   match from line base on, onto b; a CR is a line break, and a backslash
   keeps the next character (both for do_sub()) */
static void regsub(Sbuf *b, const char *src, long base)
{
	one = all = 0;
	for (const char *p = src; *p;) {
		int c = (unsigned char)*p++, no = -1;
		if (c == '&') no = 0;
		else if (c == '\\' && *p) {
			if (*p >= '0' && *p <= '9') no = *p++ - '0';
			else if (strchr("uUlLeE", *p)) {
				if (*p == 'u' || *p == 'l') one = *p;
				else if (*p == 'U' || *p == 'L') all = *p + 32;
				else one = all = 0;
				p++;
				continue;
			}
		}
		if (no < 0) {
			/* an escaped character is its byte, the rest of it dropped */
			if (c == '\\' && *p) {
				c = *p == 'r' ? '\r' : *p == 'n' ? 0 : *p == 't' ? '\t' : *p == 'b' ? 8 : -1;
				if (c < 0) bput(b, "\\", 1), c = (unsigned char)*p;
				p++;
			} else c = (int)uni_cp((const unsigned char *)p - 1, strlen(p - 1), 0);
			size_t n = strlen(p - 1);
			long k = (long)uni_len((const unsigned char *)p - 1, n, 0), t = clen(p - 1, (long)n, 0);
			putc_(b, c);
			/* composing characters as they are */
			if (t > k) bput(b, p - 1 + k, t - k);
			p += t - 1;
			continue;
		}
		if (!gs[no].coladd || !ge[no].coladd) continue;
		for (long l = gs[no].lnum, col = gs[no].col;; l++, col = 0) {
			const char *s = ml_get(base + l);
			long n = ml_len(base + l), z = l == ge[no].lnum ? ge[no].col : n;
			while (col < z) {
				if (s[col] == '\r' || s[col] == '\\') { bput(b, "\\", 1), bput(b, s + col++, 1); continue; }
				putc_(b, (int)uni_cp((const unsigned char *)s, (size_t)n, (size_t)col));
				col += (long)uni_len((const unsigned char *)s, (size_t)n, (size_t)col);
			}
			if (l >= ge[no].lnum) break;
			bput(b, "\r", 1);
		}
	}
}

/* sub_parse_flags() */
static char *flags(char *p, int *which)
{
	if (*p == '&') p++;
	else fl.all = fl.ask = fl.count = 0, fl.error = 1, fl.ic = -1;
	for (;; p++) {
		if (*p == 'g') fl.all = !fl.all;
		else if (*p == 'c') fl.ask = !fl.ask;
		else if (*p == 'n') fl.count = 1;
		else if (*p == 'e') fl.error = !fl.error;
		else if (*p == 'r') *which = RE_LAST;
		else if (*p == 'i' || *p == 'I') fl.ic = *p == 'i';
		else if (*p != 'p' && *p != '#' && *p != 'l') return p;
	}
}

/* do_sub()'s state, kept while the c flag waits for a key */
int sub_asking;
static Buf *asked;   /* the buffer the question is in */
static long first, lnum, line2, nsubs, nlns, nmatch, copycol, matchcol, prev_matchcol, nmatch_tl, sub_firstlnum;
static long change, l2, c2;   /* the question's: the line's change in length, the match's end */
static int endcolumn, hasnl, save_all, save_ask, got_match, got_quit, did_save, did_sub, skip_match, do_again, has_new, lastone;
static Sbuf line, nb, orig;   /* sub_firstline, new_start, the line shown changed while asking */
static Pos old;

static void loop(int typed);

/* do_sub(): lines l1 to l2 (from 0); cmd 's', '&' or '~' with what
   follows its name */
void ex_sub(long l1, long l2, int cmd, char *arg)
{
	int which = cmd == '~' ? RE_LAST : RE_SUBST;
	char *pat = NULL;
	endcolumn = 0;
	if (cmd == 's' && *arg && !strchr("0123456789cegriIp|\"", *arg)) {
		if ((*arg >= 'a' && *arg <= 'z') || (*arg >= 'A' && *arg <= 'Z')) { core_msg(E, "E146: Regular expressions can't be delimited by letters"); return; }
		int delim;
		if (*arg == '\\') {
			if (!*++arg || !strchr("/?&", *arg)) { core_msg(E, "E10: \\ should be followed by /, ? or &"); return; }
			if (*arg != '&') which = RE_SEARCH;
			pat = "", delim = *arg++;
		} else {
			which = RE_LAST, delim = *arg++, pat = arg;
			arg = re_skip(arg, delim, NULL);
			if (*arg == delim) *arg++ = 0;
		}
		/* skip_substitute() */
		char *rep = arg;
		while (*arg && *arg != delim) arg += *arg == '\\' && arg[1] ? 1 + clen(arg + 1, (long)strlen(arg + 1), 0) : clen(arg, (long)strlen(arg), 0);
		if (*arg) *arg++ = 0;
		free(old_sub);
		old_sub = strdup(rep);
	} else {
		if (!old_sub) { core_msg(E, "E33: No previous substitute regular expression"); return; }
		endcolumn = W->want == WANT_EOL;
	}
	/* sub_joining_lines(): :s/\n// joins the lines and the one below */
	if (pat && !strcmp(pat, "\\n") && !*old_sub && (!*arg || (!arg[1] && strchr("glp#", *arg)))) {
		long n = l2 - l1 + 1 + (l2 < nlines() - 1);
		W->line = (size_t)l1;
		if (n > 1) {
			do_join(n, 0);
			if (n - 1 > 2) core_msg(E, "%ld substitutions on 1 line", n - 1);
		}
		const char *msg;
		search_regcomp(pat, RE_SUBST, RE_SUBST, -1, &msg);
		return;
	}
	arg = flags(arg, &which);
	while (*arg == ' ' || *arg == '\t') arg++;
	if (*arg >= '0' && *arg <= '9') {
		char *d = arg;
		long i = strtol(arg, &arg, 10);
		if (i <= 0) { if (fl.error) core_msg(E, "E939: Positive count required"); return; }
		if (i >= 2147483647) { core_msg(E, "E1510: Value too large: %.*s", (int)(arg - d), d); return; }
		l1 = l2, l2 = l2 + i - 1 < nlines() - 1 ? l2 + i - 1 : nlines() - 1;
	}
	while (*arg == ' ' || *arg == '\t') arg++;
	if (*arg && *arg != '"') { core_msg(E, "E488: Trailing characters: %s", arg); return; }
	const char *msg;
	int r = search_regcomp(pat, RE_SUBST, which, fl.ic, &msg);
	if (r) {
		if (r < 0) core_msg(E, "%s", msg);
		if (fl.error) core_msg(E, "E476: Invalid command");
		return;
	}
	hasnl = re_hasnl(), save_all = fl.all, save_ask = fl.ask;   /* as typed, for :&& */
	sub = tilde(old_sub);
	old = cur();
	first = l1, line2 = l2, nsubs = nlns = 0;
	got_match = got_quit = did_save = timed_out = 0;
	loop(-1);
}

/* the rest of do_sub() once the lines are done */
static void finish(void)
{
	free(sub);
	sub = NULL;
	int ask = fl.ask;
	fl.all = save_all, fl.ask = save_ask;
	if (fl.count) setcur(old);
	if (timed_out) core_msg(E, "E363: Pattern uses more memory than 'maxmempattern'");
	else if (nsubs) {
		b_op_start = (Pos){ first, 0, 0 }, b_op_end = (Pos){ line2, 0, 0 };
		/* asked: the cursor stays on the last match */
		if (!ask) {
			if (endcolumn) coladvance(MAXCOL);
			else beginline(BL_WHITE | BL_FIX);
		}
		if (nsubs > 2 || fl.count)
			core_msg(E, "%ld %s%s on %ld line%s", nsubs, fl.count ? "match" : "substitution", nsubs == 1 ? "" : fl.count ? "es" : "s", nlns, nlns == 1 ? "" : "s");
	} else if (got_match) core_msg(E, "");
	else if (fl.error) core_msg(E, "E486: Pattern not found: %s", get_search_pat());
}

/* the line shown while asking: the substitutions made in it so far
   (do_sub() puts it in the buffer and back without an undo step) */
static void show(int on)
{
	if (on) {
		orig.n = 0, bput(&orig, ml_get(lnum), ml_len(lnum));
		Sbuf t = { 0 };
		bput(&t, nb.s, nb.n), bput(&t, line.s + copycol, line.n - copycol);
		replace_bytes(lnum, 0, ml_len(lnum), t.s, t.n);
		free(t.s);
	} else replace_bytes(lnum, 0, ml_len(lnum), orig.s, orig.n);
}

/* do_sub()'s loop over the lines; typed: the key answering the c flag's
   question, where it goes on */
static void loop(int typed)
{
	if (typed >= 0) goto answer;
	for (lnum = first; lnum <= line2 && !timed_out && !got_quit; lnum++) {
		nmatch = exec(lnum, 0);
		if (!nmatch) continue;
		copycol = 0, matchcol = 0, prev_matchcol = MAXCOL, nmatch_tl = 0, sub_firstlnum = lnum;
		did_sub = skip_match = has_new = 0;
		line.n = -1;   /* none yet */
		got_match = 1;
		for (;;) {
			if (gs[0].lnum > 0) {
				lnum += gs[0].lnum, sub_firstlnum += gs[0].lnum, nmatch -= gs[0].lnum;
				line.n = -1;
			}
			if (lnum >= nlines()) break;
			if (line.n < 0) line.n = 0, bput(&line, ml_get(sub_firstlnum), ml_len(sub_firstlnum));
			W->line = (size_t)lnum;
			do_again = 0;
			/* an empty match where the last one ended: one character on */
			if (matchcol == prev_matchcol && ge[0].lnum == 0 && matchcol == ge[0].col) {
				if (matchcol >= line.n) skip_match = 1;
				else matchcol += clen(line.s, line.n, matchcol);
				goto skip;
			}
			matchcol = prev_matchcol = ge[0].col;
			if (fl.count) {
				if (nmatch > 1) matchcol = line.n, nmatch = 1, skip_match = 1;
				nsubs++, did_sub = 1;
				goto skip;
			}
			if (fl.ask) {
				/* the match in IncSearch (at least one character, for /^/),
				   the cursor on it, then the question */
				W->col = (size_t)gs[0].col;
ask:
				/* the line with this line's substitutions so far; the cursor
				   moves by their change in length, again each time it asks */
				change = has_new ? nb.n + line.n - copycol - ml_len(lnum) : 0;
				if (has_new) show(1);
				W->col = (long)W->col + change > 0 ? (size_t)((long)W->col + change) : 0;
				l2 = lnum + ge[0].lnum - gs[0].lnum, c2 = ge[0].col + (l2 == lnum ? change : 0);
				if (l2 > lnum && !c2) l2--, c2 = ml_len(l2) + 1;
				E->ks.inc = (KeyArea){ 'v', (size_t)lnum, W->col, (size_t)l2, (size_t)(c2 > (long)W->col || l2 > lnum ? head(ml_get(l2), c2 - 1) : (long)W->col) };
				core_msg(E, "replace with %s? (y)es/(n)o/(a)ll/(q)uit/(l)ast/scroll up(^E)/down(^Y)", sub);
				sub_asking = 1, asked = B;
				return;
answer:
				if (has_new) show(0);
				/* scrollup_clamp(), scrolldown_clamp(): only while the cursor
				   stays 'scrolloff' lines inside the window */
				if (typed == 5 && (long)W->top + 1 < nlines() && (long)(W->line - W->top) - 1 >= SCROLLOFF) W->top++;
				if (typed == 25 && W->top && (long)(W->line - W->top) + 1 < W->h - SCROLLOFF) W->top--;
				if (!has("ynalq", typed) && typed != KEY_ESC && typed != KEY_CTRL_C) goto ask;
				sub_asking = 0;
				E->ks.inc.type = 0;
				core_msg(E, "");
				/* 'hlsearch' may have compiled its pattern meanwhile */
				search_recompile();
				got_quit = typed == 'q' || typed == KEY_ESC || typed == KEY_CTRL_C;
				if (typed == 'l') fl.all = 0, line2 = lnum;
				if (typed == 'a') fl.ask = 0;
				if (typed == 'n') {
					/* past a match of more lines: on to the next line */
					if (nmatch > 1) matchcol = line.n, skip_match = 1;
					goto skip;
				}
				if (got_quit) goto skip;
			}
			W->col = (size_t)gs[0].col;
			/* a match that takes the last line's line break */
			if (nmatch > nlines() - sub_firstlnum) {
				nmatch = nlines() - sub_firstlnum, skip_match = 1;
				if (nmatch < 0) goto skip;
			}
			long base = sub_firstlnum - gs[0].lnum;
			if (nmatch > 1) nmatch_tl += nmatch - 1;
			if (!has_new) nb.n = 0, bput(&nb, "", 0), has_new = 1;
			bput(&nb, line.s + copycol, gs[0].col - copycol);
			long p = nb.n;
			regsub(&nb, sub, base);
			nsubs++, did_sub = 1;
			W->col = 0;
			copycol = ge[0].col;
			if (nmatch > 1) {
				/* go on in a copy of the match's last line */
				sub_firstlnum += nmatch - 1;
				line.n = 0, bput(&line, ml_get(sub_firstlnum), ml_len(sub_firstlnum));
				if (sub_firstlnum <= line2) do_again = 1;
				else fl.all = 0;
			}
			if (skip_match) line.n = 0, bput(&line, "", 0), copycol = 0;
			/* a backslash keeps the next character; a CR breaks the line */
			for (; p < nb.n; p++) {
				if (nb.s[p] == '\\' && p + 1 < nb.n) memmove(nb.s + p, nb.s + p + 1, (size_t)(nb.n-- - p));
				else if (nb.s[p] == '\r') {
					append_lines(lnum - 1, nb.s, p);
					sub_firstlnum++, lnum++, line2++, W->line++;
					memmove(nb.s, nb.s + p + 1, (size_t)(nb.n - p));
					nb.n -= p + 1, p = -1;
				}
			}
			if (!did_save) u_save_cursor(), did_save = 1;
skip:
			lastone = skip_match || got_quit || lnum > line2 || !(fl.all || do_again) || (matchcol >= line.n && nmatch <= 1 && !hasnl);
			nmatch = -1;
			if (lastone || nmatch_tl > 0 || (nmatch = exec(sub_firstlnum, matchcol)) == 0 || gs[0].lnum > 0) {
				if (has_new) {
					bput(&nb, line.s + copycol, line.n - copycol);
					matchcol = line.n - matchcol, prev_matchcol = line.n - prev_matchcol;
					replace_bytes(lnum, 0, ml_len(lnum), nb.s, nb.n);
					if (nmatch_tl > 0) del_lines(lnum + 1, nmatch_tl), line2 -= nmatch_tl, nmatch_tl = 0;
					sub_firstlnum = lnum;
					Sbuf t = line;
					line = nb, nb = t, has_new = 0;
					matchcol = line.n - matchcol, prev_matchcol = line.n - prev_matchcol;
					copycol = 0;
				}
				if (nmatch == -1 && !lastone) nmatch = exec(sub_firstlnum, matchcol);
				if (nmatch <= 0) {
					if (nmatch == -1) lnum -= gs[0].lnum;
					break;
				}
			}
		}
		nlns += did_sub;
	}
	finish();
}

/* the key answering "replace with ...?".  When another buffer came into
   the window meanwhile, not by a key, the question ends as with Esc, in
   the buffer it asked in, and the window keeps its cursor. */
void sub_key(int k)
{
	Buf *b = B;
	if (b == asked) { loop(k); return; }
	size_t line = W->line, col = W->col, top = W->top;
	E->buf = asked;
	loop(KEY_ESC);
	E->buf = b, W->line = line, W->col = col, W->top = top;
}
