/* snacks.picker's matcher (core/matcher.lua, core/score.lua), line for line
   where it decides a score; the comments name the Lua function. */
#include <limits.h>
#include <string.h>
#include "match.h"

/* score.lua's constants, which are fzf's */
enum {
	SCORE_MATCH = 16, SCORE_GAP_START = -3, SCORE_GAP_EXTENSION = -1,
	BONUS_BOUNDARY = 8, BONUS_NONWORD = 8, BONUS_CAMEL_123 = 7,
	BONUS_CONSECUTIVE = 4, BONUS_NO_PATH_SEP = 6,
	BONUS_BOUNDARY_WHITE = 10, BONUS_BOUNDARY_DELIMITER = 9,   /* history_bonus off */
	INVERSE_SCORE = 1000
};
enum { NONE = INT_MIN };   /* nil: Lua counts a score of 0 as a match */
enum { C_WHITE, C_NONWORD, C_DELIMITER, C_LOWER, C_UPPER, C_LETTER, C_NUMBER };

static int lspace(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }   /* Lua's %s */
static int lalnum(int c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int llower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

/* CHAR_CLASS */
static int cls(unsigned char b)
{
	if (lspace(b)) return C_WHITE;
	if (b == '/' || b == '\\' || b == ',' || b == ':' || b == ';' || b == '|') return C_DELIMITER;
	if (b >= '0' && b <= '9') return C_NUMBER;
	if (b >= 'A' && b <= 'Z') return C_UPPER;
	if (b >= 'a' && b <= 'z') return C_LOWER;
	return C_NONWORD;
}

/* M:compute_bonus */
static int bonus(int prev, int curr)
{
	if (curr > C_NONWORD) {
		if (prev == C_WHITE) return BONUS_BOUNDARY_WHITE;
		if (prev == C_DELIMITER) return BONUS_BOUNDARY_DELIMITER;
		if (prev == C_NONWORD) return BONUS_BOUNDARY;
	}
	if ((prev == C_LOWER && curr == C_UPPER) || (prev != C_NUMBER && curr == C_NUMBER)) return BONUS_CAMEL_123;
	if (curr == C_NONWORD || curr == C_DELIMITER) return BONUS_NONWORD;
	if (curr == C_WHITE) return BONUS_BOUNDARY + 2;
	return 0;
}

/* score.lua's state; positions are 0-based here, 1-based there */
typedef struct {
	const unsigned char *s;
	int score, consecutive, prev_class, first_bonus;
	long prev;                 /* -1: nil */
} Score;

/* M:update */
static void update(Score *k, size_t pos)
{
	int c = cls(k->s[pos]), b;
	long gap = k->prev >= 0 ? (long)pos - k->prev - 1 : 0;
	if (gap > 0) {
		k->prev_class = cls(k->s[pos - 1]);
		b = bonus(k->prev_class, c);
		k->score += SCORE_GAP_START + (int)(gap - 1) * SCORE_GAP_EXTENSION;
		k->consecutive = 0;
		k->first_bonus = 0;
	} else {
		b = bonus(k->prev_class, c);
		if (k->consecutive == 0) k->first_bonus = b;
		else {
			if (b >= BONUS_BOUNDARY && b > k->first_bonus) k->first_bonus = b;
			if (k->first_bonus > b) b = k->first_bonus;
			if (BONUS_CONSECUTIVE > b) b = BONUS_CONSECUTIVE;
		}
		k->consecutive++;
	}
	if (k->prev < 0) b *= 2;
	k->score += SCORE_MATCH + b;
	k->prev_class = c;
	k->prev = (long)pos;
}

/* M:init: the filename bonus when no '/' follows the first matched byte */
static void start(Score *k, const char *s, size_t n, size_t first, int file_bonus)
{
	k->s = (const unsigned char *)s;
	k->score = k->consecutive = k->first_bonus = 0;
	k->prev_class = first ? cls(k->s[first - 1]) : C_WHITE;
	k->prev = -1;
	if (file_bonus && !memchr(s + first + 1, '/', n - first - 1)) k->score += BONUS_NO_PATH_SEP;
	update(k, first);
}

/* string.find(str, c, from, true) on str = s, or s:lower() if ic (the
   lowered copy is never made); n if none */
static size_t find1(const char *s, size_t n, size_t from, char c, int ic)
{
	if (!ic || !(c >= 'a' && c <= 'z')) {
		const char *p = from < n ? memchr(s + from, c, n - from) : NULL;
		return p ? (size_t)(p - s) : n;
	}
	for (; from < n; from++) if (llower((unsigned char)s[from]) == c) return from;
	return n;
}

static int eq(const char *s, const char *p, size_t n, int ic)
{
	for (size_t i = 0; i < n; i++)
		if ((ic ? llower((unsigned char)s[i]) : (unsigned char)s[i]) != (unsigned char)p[i]) return 0;
	return 1;
}

/* string.find(str, pat, from, true); n if none */
static size_t find(const char *s, size_t n, size_t from, const char *p, size_t pn, int ic)
{
	for (; from + pn <= n; from++) {
		from = find1(s, n, from, p[0], ic);
		if (from + pn > n) return n;
		if (eq(s + from, p, pn, ic)) return from;
	}
	return n;
}

/* M:fuzzy and M:fuzzy_find: the greedy match from each start of the first
   byte; the best score wins, the earliest on a tie */
static int fuzzy(const char *str, size_t n, const Term *t, int fb)
{
	int best = NONE, ic = t->icase;
	for (size_t from = find1(str, n, 0, t->s[0], ic); from < n; from = find1(str, n, from + 1, t->s[0], ic)) {
		Score k;
		start(&k, str, n, from, fb);
		size_t last = from;
		for (size_t i = 1; i < t->n; i++) {
			last = find1(str, n, last + 1, t->s[i], ic);
			if (last == n) return best;
			update(&k, last);
		}
		if (k.score > best) best = k.score;
	}
	return best;
}

/* M:_match: the term's score, NONE if it does not match */
static int term(const Term *t, const char *text, size_t n, const char *file, size_t fn, int filename_bonus)
{
	int fb = filename_bonus && file;      /* score.is_file */
	if (t->field == 2) return t->inverse ? INVERSE_SCORE : NONE;
	if (t->field == 1) {
		if (!file) return t->inverse ? INVERSE_SCORE : NONE;
		text = file; n = fn;
	}
	const char *str = text;
	int ic = t->icase;
	if (t->fuzzy) return fuzzy(str, n, t, fb);
	size_t from = n, to = 0;
	if (t->prefix) {
		if (n >= t->n && eq(str, t->s, t->n, ic)) from = 0;
	} else if (t->suffix) {
		if (n >= t->n && eq(str + n - t->n, t->s, t->n, ic)) from = n - t->n;
	} else {
		from = find(str, n, 0, t->s, t->n, ic);
		/* a word: bounded by a non-letter or the ends (is_left_boundary,
		   is_right_boundary); the next try starts after this one's end */
		while (t->word && from < n) {
			to = from + t->n - 1;
			if ((from == 0 || cls((unsigned char)str[from - 1]) < C_LOWER)
			    && (to == n - 1 || cls((unsigned char)str[to + 1]) < C_LOWER)) break;
			from = find(str, n, to + 1, t->s, t->n, ic);
		}
	}
	if (t->inverse) return from == n ? INVERSE_SCORE : NONE;
	if (from == n) return NONE;
	to = from + t->n - 1;
	Score k;
	start(&k, text, n, from, fb);
	for (size_t i = from + 1; i <= to; i++) update(&k, i);
	return k.score;
}

int match_score(const Match *m, const char *text, size_t n, const char *file, size_t fn)
{
	if (m->ng == 0) return MATCH_DEFAULT;
	int sum = 0;
	for (int g = 0; g < m->ng; g++) {
		int s = NONE;
		for (int i = m->grp[g]; i < m->grp[g + 1] && s == NONE; i++)
			s = term(&m->t[i], text, n, file, fn, m->filename_bonus);
		if (s == NONE) return 0;
		sum += s;
	}
	return sum;
}

static int digits(const char *s, size_t n)
{
	for (size_t i = 0; i < n; i++) if (s[i] < '0' || s[i] > '9') return 0;
	return 1;
}
static long num(const char *s, size_t n, long none)
{
	long v = 0;
	if (!n) return none;
	for (size_t i = 0; i < n; i++) v = v * 10 + (s[i] - '0');
	return v;
}

/* Does p end in .[a-z_]+ with at least one byte before the dot? (.+%.[a-z_]+) */
static int has_ext(const char *p, size_t n)
{
	size_t i = n;
	while (i > 0 && ((p[i - 1] >= 'a' && p[i - 1] <= 'z') || p[i - 1] == '_')) i--;
	return i < n && i >= 2 && p[i - 1] == '.';
}

/* M:_prepare.  The term is copied into m->buf at *used. */
static int prepare(Match *m, Term *t, const char *p, size_t n, size_t *used)
{
	memset(t, 0, sizeof *t);
	char *d = m->buf + *used;
	const char *q = p;
	size_t qn = n, extra = 0;
	/* file_patterns: path:line:col, path:line, name.ext:line:col, name.ext:line */
	const char *c2 = NULL, *c1 = NULL;
	for (size_t i = n; i-- > 0;) if (p[i] == ':') { c2 = p + i; break; }
	if (c2 && digits(c2 + 1, (size_t)(p + n - c2 - 1))) {
		for (const char *s = c2; s-- > p;) if (*s == ':') { c1 = s; break; }
		if (c1 && !digits(c1 + 1, (size_t)(c2 - c1 - 1))) c1 = NULL;
		size_t f3 = c1 ? (size_t)(c1 - p) : 0, f2 = (size_t)(c2 - p);
		int sep3 = c1 && (memchr(p, '/', f3) || memchr(p, '\\', f3));
		int sep2 = memchr(p, '/', f2) || memchr(p, '\\', f2);
		size_t f = 0;
		if (sep3) f = f3;
		else if (sep2) f = f2;
		else if (c1 && has_ext(p, f3)) f = f3;
		else if (has_ext(p, f2)) f = f2;
		if (f) {
			t->field = 1;
			q = p; qn = f; extra = 1;      /* file .. "$" */
			if (f == f3) { m->line = num(c1 + 1, (size_t)(c2 - c1 - 1), 1); m->col = num(c2 + 1, (size_t)(p + n - c2 - 1), 0); }
			else { m->line = num(c2 + 1, (size_t)(p + n - c2 - 1), 1); m->col = 0; }
		}
	}
	/* a field: ^([%w_][%w_]+):(.*)$ */
	size_t w = 0;
	while (w < n && (lalnum((unsigned char)p[w]) || p[w] == '_')) w++;
	if (w >= 2 && w < n && p[w] == ':') {
		t->field = w == 4 && !memcmp(p, "file", 4) ? 1 : w == 4 && !memcmp(p, "text", 4) ? 0 : 2;
		q = p + w + 1; qn = n - w - 1; extra = 0;
	}
	if (*used + qn + extra > MATCH_MAX) return -1;
	memcpy(d, q, qn);
	if (extra) d[qn++] = '$';
	*used += qn;
	int lower = 1;
	for (size_t i = 0; i < qn; i++) if (d[i] >= 'A' && d[i] <= 'Z') lower = 0;
	t->icase = (unsigned char)lower;      /* smartcase */
	t->fuzzy = 1;
	char *s = d;
	if (qn && s[0] == '!') { t->fuzzy = 0; t->inverse = 1; s++; qn--; t->entropy -= 1; }
	if (qn && s[0] == '\'') {
		t->fuzzy = 0; s++; qn--; t->entropy += 10;
		if (qn && s[qn - 1] == '\'') { t->word = 1; qn--; t->entropy += 10; }
	} else if (qn && s[0] == '^') { t->fuzzy = 0; t->prefix = 1; s++; qn--; t->entropy += 20; }
	if (qn && s[qn - 1] == '$') { t->fuzzy = 0; t->suffix = 1; qn--; t->entropy += 20; }
	int rare = 0;
	for (size_t i = 0; i < qn; i++) rare += !lalnum((unsigned char)s[i]) && !lspace((unsigned char)s[i]);
	t->entropy += (qn < 20 ? (int)qn : 20) + rare * 2;
	if (!t->icase) t->entropy *= 2;     /* not ignorecase and not is_lower */
	if (t->icase) for (size_t i = 0; i < qn; i++) s[i] = (char)llower((unsigned char)s[i]);
	t->s = s; t->n = qn;
	return 0;
}

/* LuaJIT's table.sort (lib_table.c, Lua 5.1's auxsort) on t[l..u] by
   entropy: not stable, so its exact steps decide which of two equally
   likely OR terms scores */
#define LT(a, b) (t[a].entropy < t[b].entropy)
static void swap(Term *t, int a, int b) { Term x = t[a]; t[a] = t[b]; t[b] = x; }
static void sort(Term *t, int l, int u)
{
	while (l < u) {
		if (LT(u, l)) swap(t, l, u);
		if (u - l == 1) break;
		int i = (l + u) / 2, j;
		if (LT(i, l)) swap(t, i, l);
		else if (LT(u, i)) swap(t, i, u);
		if (u - l == 2) break;
		Term p = t[i];
		swap(t, i, u - 1);
		i = l; j = u - 1;
		for (;;) {
			while (t[++i].entropy < p.entropy) ;
			while (p.entropy < t[--j].entropy) ;
			if (j < i) break;
			swap(t, i, j);
		}
		swap(t, u - 1, i);
		if (i - l < u - i) { j = l; i = i - 1; l = i + 2; }
		else { j = i + 1; i = u; u = j - 2; }
		sort(t, j, i);
	}
}
#undef LT

int match_init(Match *m, const char *q, size_t n)
{
	size_t used = 0;
	int fb = m->filename_bonus;
	memset(m, 0, sizeof *m);
	m->filename_bonus = fb;
	while (n && lspace((unsigned char)*q)) q++, n--;            /* vim.trim */
	while (n && lspace((unsigned char)q[n - 1])) n--;
	int is_or = 0;
	for (size_t i = 0; i < n;) {
		size_t j = i;
		while (j < n && q[j] != ' ') j++;        /* vim.split(pattern, " +") */
		if (j - i == 1 && q[i] == '|') is_or = 1;
		else {
			if (m->nt == MATCH_TERMS) goto full;
			Term *t = &m->t[m->nt];
			if (prepare(m, t, q + i, j - i, &used)) goto full;
			if (t->n) {
				if (!(is_or && m->ng)) m->grp[m->ng++] = m->nt;
				m->nt++;
			}
			is_or = 0;
		}
		while (j < n && q[j] == ' ') j++;
		i = j;
	}
	m->grp[m->ng] = m->nt;
	/* within an OR group the lower entropy is tried first; equal ones in
	   the order LuaJIT's table.sort leaves them */
	for (int g = 0; g < m->ng; g++) sort(m->t, m->grp[g], m->grp[g + 1] - 1);
	return 0;
full:
	m->nt = 0; m->ng = 1; m->grp[0] = m->grp[1] = 0;   /* an empty OR group matches nothing */
	return -1;
}
