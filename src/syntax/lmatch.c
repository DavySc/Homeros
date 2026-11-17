/* A port of the matcher in LuaJIT's lib_string.c (itself Lua 5.1's
   lstrlib.c): same recursion, same backtracking, same classes.  Reads the
   subject through a callback, so a node of any size needs no copy.  A
   malformed pattern, an error in Lua, matches nothing here. */
#include <ctype.h>
#include "lmatch.h"

enum { MAXCAP = 32, UNFINISHED = -1, POSITION = -2, MAXDEPTH = 200 };

typedef struct {
	int (*at)(void *, size_t);
	void *ctx;
	size_t init, end;           /* the subject */
	const char *pe;             /* end of the pattern */
	int level, depth, bad;
	struct { size_t init; long len; } cap[MAXCAP];
} MS;

/* the byte at s, 0 past the end as Lua's terminating NUL */
static int S(MS *ms, size_t s) { return s < ms->end ? ms->at(ms->ctx, s) : 0; }
static int P(MS *ms, const char *p) { return p < ms->pe ? (unsigned char)*p : 0; }

static const char *class_end(MS *ms, const char *p)
{
	switch (P(ms, p++)) {
	case '%':
		if (p >= ms->pe) { ms->bad = 1; return ms->pe; }
		return p + 1;
	case '[':
		if (P(ms, p) == '^') p++;
		do {
			if (p >= ms->pe) { ms->bad = 1; return ms->pe; }
			if (*(p++) == '%' && p < ms->pe) p++;
		} while (P(ms, p) != ']');
		return p + 1;
	default:
		return p;
	}
}

static int match_class(int c, int cl)
{
	int r;
	switch (tolower(cl)) {
	case 'a': r = isalpha(c); break;
	case 'c': r = iscntrl(c); break;
	case 'd': r = isdigit(c); break;
	case 'g': r = isgraph(c); break;
	case 'l': r = islower(c); break;
	case 'p': r = ispunct(c); break;
	case 's': r = isspace(c); break;
	case 'u': r = isupper(c); break;
	case 'w': r = isalnum(c); break;
	case 'x': r = isxdigit(c); break;
	case 'z': r = c == 0; break;
	default: return cl == c;
	}
	if (isupper(cl)) r = !r;
	return r != 0;
}

/* p at '[', ec at its ']' */
static int bracket(MS *ms, int c, const char *p, const char *ec)
{
	int sig = 1;
	if (P(ms, p + 1) == '^') { sig = 0; p++; }
	while (++p < ec) {
		if (*p == '%') {
			p++;
			if (match_class(c, P(ms, p))) return sig;
		} else if (P(ms, p + 1) == '-' && p + 2 < ec) {
			p += 2;
			if ((unsigned char)*(p - 2) <= c && c <= (unsigned char)*p) return sig;
		} else if ((unsigned char)*p == c) return sig;
	}
	return !sig;
}

static int single(MS *ms, int c, const char *p, const char *ep)
{
	switch (P(ms, p)) {
	case '.': return 1;
	case '%': return match_class(c, P(ms, p + 1));
	case '[': return bracket(ms, c, p, ep - 1);
	default: return P(ms, p) == c;
	}
}

static long match(MS *ms, size_t s, const char *p);

static long max_expand(MS *ms, size_t s, const char *p, const char *ep)
{
	size_t i = 0;
	while (s + i < ms->end && single(ms, S(ms, s + i), p, ep)) i++;
	for (;;) {
		long r = match(ms, s + i, ep + 1);
		if (r >= 0 || ms->bad) return r;
		if (!i--) return -1;
	}
}

static long min_expand(MS *ms, size_t s, const char *p, const char *ep)
{
	for (;;) {
		long r = match(ms, s, ep + 1);
		if (r >= 0 || ms->bad) return r;
		if (s < ms->end && single(ms, S(ms, s), p, ep)) s++;
		else return -1;
	}
}

static long start_cap(MS *ms, size_t s, const char *p, long what)
{
	if (ms->level >= MAXCAP) { ms->bad = 1; return -1; }
	ms->cap[ms->level].init = s;
	ms->cap[ms->level].len = what;
	ms->level++;
	long r = match(ms, s, p);
	if (r < 0) ms->level--;
	return r;
}

static long end_cap(MS *ms, size_t s, const char *p)
{
	int l = ms->level - 1;
	while (l >= 0 && ms->cap[l].len != UNFINISHED) l--;
	if (l < 0) { ms->bad = 1; return -1; }
	ms->cap[l].len = (long)(s - ms->cap[l].init);
	long r = match(ms, s, p);
	if (r < 0) ms->cap[l].len = UNFINISHED;
	return r;
}

static long back_ref(MS *ms, size_t s, int l)
{
	l -= '1';
	if (l < 0 || l >= ms->level || ms->cap[l].len == UNFINISHED) { ms->bad = 1; return -1; }
	size_t len = ms->cap[l].len == POSITION ? 0 : (size_t)ms->cap[l].len;
	if (ms->end - s < len) return -1;
	for (size_t i = 0; i < len; i++)
		if (S(ms, ms->cap[l].init + i) != S(ms, s + i)) return -1;
	return (long)(s + len);
}

static long match(MS *ms, size_t s, const char *p)
{
	if (++ms->depth > MAXDEPTH) { ms->bad = 1; ms->depth--; return -1; }
	long r;
	for (;;) {
		if (ms->bad) { r = -1; break; }
		switch (P(ms, p)) {
		case '(':
			r = P(ms, p + 1) == ')' ? start_cap(ms, s, p + 2, POSITION)
			                        : start_cap(ms, s, p + 1, UNFINISHED);
			goto out;
		case ')':
			r = end_cap(ms, s, p + 1);
			goto out;
		case 0:
			if (p >= ms->pe) { r = (long)s; goto out; }
			goto dflt;
		case '$':
			if (p + 1 == ms->pe) { r = s == ms->end ? (long)s : -1; goto out; }
			goto dflt;
		case '%':
			switch (P(ms, p + 1)) {
			case 'b': {
				int b = P(ms, p + 2), e = P(ms, p + 3);
				if (p + 3 >= ms->pe) { ms->bad = 1; r = -1; goto out; }
				if (s >= ms->end || S(ms, s) != b) { r = -1; goto out; }
				int cont = 1;
				size_t t = s;
				while (++t < ms->end) {
					int c = S(ms, t);
					if (c == e) { if (--cont == 0) break; }
					else if (c == b) cont++;
				}
				if (t >= ms->end) { r = -1; goto out; }
				s = t + 1;
				p += 4;
				continue;
			}
			case 'f': {
				p += 2;
				if (P(ms, p) != '[') { ms->bad = 1; r = -1; goto out; }
				const char *ep = class_end(ms, p);
				if (ms->bad) { r = -1; goto out; }
				int prev = s == ms->init ? 0 : S(ms, s - 1);
				if (bracket(ms, prev, p, ep - 1) || !bracket(ms, S(ms, s), p, ep - 1)) { r = -1; goto out; }
				p = ep;
				continue;
			}
			default:
				if (isdigit(P(ms, p + 1))) {
					r = back_ref(ms, s, P(ms, p + 1));
					if (r < 0) goto out;
					s = (size_t)r;
					p += 2;
					continue;
				}
				goto dflt;
			}
		default: dflt: {
			const char *ep = class_end(ms, p);
			if (ms->bad) { r = -1; goto out; }
			int m = s < ms->end && single(ms, S(ms, s), p, ep);
			switch (P(ms, ep)) {
			case '?':
				if (m && (r = match(ms, s + 1, ep + 1)) >= 0) goto out;
				p = ep + 1;
				continue;
			case '*':
				r = max_expand(ms, s, p, ep);
				goto out;
			case '+':
				r = m ? max_expand(ms, s + 1, p, ep) : -1;
				goto out;
			case '-':
				r = min_expand(ms, s, p, ep);
				goto out;
			default:
				if (!m) { r = -1; goto out; }
				s++;
				p = ep;
				continue;
			}
		}
		}
	}
out:
	ms->depth--;
	return r;
}

int lmatch_find(int (*at)(void *, size_t), void *ctx, size_t a, size_t z, const char *p, size_t n)
{
	MS ms = { at, ctx, a, z, p + n, 0, 0, 0, { { 0, 0 } } };
	int anchor = n && *p == '^';
	if (anchor) p++;
	size_t s = a;
	do {
		ms.level = ms.depth = 0;
		if (match(&ms, s, p) >= 0) return 1;
		if (ms.bad) return 0;
	} while (s++ < z && !anchor);
	return 0;
}
