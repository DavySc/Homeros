/* T-SQL formatting, ported from SQLDiff's `sqldiff fmt` (DavySc/SQLDiff,
   crates/sqldiff-core/src/format and the T-SQL paths of
   crates/sqldiff-syntax/src/parser.rs; Davy, 2026-11-18: a native version
   of his formatter, T-SQL only, keywords lower case).  Only the whitespace
   between tokens and the case of reserved keywords change; every other
   byte stays.  The layout comes from SQLDiff's statement tree (blocks, IF,
   WHILE, TRY/CATCH, routines, statements ended without `;`), built here as
   SQLDiff's parser builds it, over package 12's T-SQL lexer.  SQLDiff's
   options are fixed at the defaults Davy runs (indent 4, clause breaks,
   space after commas and around comparisons, one blank line kept, line
   breaks kept, a final newline) with keyword_case lower, and line endings
   as Homeros keeps them (see newline()).

   The result is checked by step 1 of SQLDiff's check (docs/formatter.md):
   the output is lexed again and must give the same tokens (keywords may
   differ in case); verify() says why steps 2 and 3 are not needed.  A
   top-level statement that fails is kept as written; if a failure cannot
   be pinned to one, nothing changes.  The equivalence engine (step 4) is
   not needed for meaning: the tokens are the same, and the gaps whose
   shape matters (after a `--` comment, around GO, sqlcmd lines, between
   two strings) are kept as written.

   Known differences from sqldiff (measured, reviews/pr-60.md), each
   changing less or the same: line breaks are LF (newline()); a word after
   a label and a block comment is recased where SQLDiff keeps the
   statement; and on malformed text, as package 12's lexer reads it: a
   stray `]` keeps its statement, a bare `@` before `=` takes a space, `}<`
   and `<?` keep their gap, bytes that are not UTF-8 are formatted around
   (SQLDiff refuses the file), and a lone CR ends a `--` comment, as in SQL
   Server.  `<<` and `>>` are one operator, as SQL Server 2022 reads them,
   so `e->>'a'` takes no space (SQLDiff: `e->> 'a'`); and PostgreSQL's
   `CREATE RECURSIVE VIEW v AS VALUES ... UNION SELECT` gets no clause
   breaks (SQLDiff breaks them). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "sqlfmt.h"
#include "tsql.h"

/* SQLDiff's token kinds, as the parser and the layout tell them apart */
enum { WORD, QUOTED, VAR, STRING, NUMBER, OP, COMMA, LP, RP, SEMI, DOT, COLON, LCOMMENT, BCOMMENT, GO, ERR };
/* tree nodes (SQLDiff's SyntaxKind names) */
enum { N_FILE = 1, N_BATCH, N_STATEMENT, N_ROUTINE, N_ROUTINE_BODY, N_BLOCK, N_TRY_CATCH, N_TRY_BLOCK,
       N_CATCH_BLOCK, N_IF, N_LOOP, N_BRANCH, N_CONDITION, N_LABEL, N_ERROR };
/* why a line break goes before a token (layout.rs Brk) */
enum { B_NONE, B_UNIT, B_FOOTER, B_CLAUSE };

typedef struct {
	uint32_t s, e;
	uint8_t k, w, rc, brk, verb, gapk, kw;   /* w: the word (W_*), rc: SQLDiff recases it */
	int32_t level, depth, top;
} Tok;

/* one pass's tokens, tree and problems */
typedef struct {
	const char *src;
	size_t len;
	Tok *t;
	size_t n, cap;
	struct Ev { int node, tok, end; } *ev;   /* node 0: token tok; else a node, its events to end */
	size_t nev, evcap;
	size_t *diag;
	size_t ndiag, diagcap;
	size_t (*tops)[2];
	size_t ntops, topcap;
} Pass;

#define GROW(p, n, cap) ((n) < (cap) || grow((void **)&(p), &(cap), sizeof *(p)))
static int grow(void **p, size_t *cap, size_t size)
{
	size_t c = *cap ? *cap * 2 : 256;
	void *q = realloc(*p, c * size);
	if (!q) return 0;
	*p = q, *cap = c;
	return 1;
}
static int oom;

/* ---- words the parser and the layout look at ---- */

/* Their W_* numbers; package 12's word table gives each word an id, and
   wmap takes an id to its W_* (W_NONE for the words the layout ignores). */
#define WORDS \
	X(AFTER) X(ALL) X(ALTER) X(AND) X(APPLY) X(AS) X(BEFORE) X(BEGIN) X(BETWEEN) X(BREAK) X(BULK) X(BY) X(CASE) X(CATCH) \
	X(CLOSE) X(COMMIT) X(CONTINUE) X(CONVERSATION) X(CREATE) X(CROSS) X(DEALLOCATE) X(DECLARE) X(DEFAULT) \
	X(DELETE) X(DENY) X(DIALOG) X(DISTINCT) X(DISTRIBUTED) X(DROP) X(ELSE) X(END) X(EXCEPT) X(EXEC) X(EXECUTE) \
	X(FETCH) X(FOR) X(FROM) X(FULL) X(FUNCTION) X(GOTO) X(GRANT) X(GROUP) X(HASH) X(HAVING) X(IF) X(INNER) \
	X(INSERT) X(INSTEAD) X(INTERSECT) X(JOIN) X(LEFT) X(LIMIT) X(LOOP) X(MERGE) X(NATURAL) \
	X(NOT) X(OF) X(OFFSET) X(ON) X(OPEN) X(OR) X(ORDER) X(OUTER) X(OUTPUT) X(PRINT) X(PROC) X(PROCEDURE) \
	X(RAISERROR) X(REMOTE) X(RETURN) X(RETURNING) X(RETURNS) X(REVOKE) X(RIGHT) \
	X(ROLLBACK) X(ROW) X(ROWS) X(SAVE) X(SELECT) X(SET) X(THEN) X(THROW) X(TRAN) \
	X(TRANSACTION) X(TRIGGER) X(TRUNCATE) X(TRY) X(UNION) X(UPDATE) X(USE) X(USING) X(VALUES) X(VIEW) X(WAITFOR) \
	X(WHEN) X(WHERE) X(WHILE) X(WITH)
#define X(w) W_##w,
enum { W_NONE, WORDS NW };
#undef X
static uint8_t *wmap;

static int word_of(const Pass *p, size_t i) { return p->t[i].w; }

/* once: tsql_word reads any case, so the names are the enum's */
static int words(void)
{
	if (wmap) return 1;
	if (!(wmap = calloc(tsql_nwords + 1, 1))) return 0;
#define X(w) #w,
	static const char *const name[] = { WORDS };
#undef X
	for (size_t i = 0, id; i < NW - 1; i++)
		if (tsql_word(name[i], strlen(name[i]), &id), id) wmap[id] = (uint8_t)(i + 1);
	return 1;
}

/* ---- lexing: package 12's tokens in SQLDiff's kinds ---- */

/* a gap the layout may rewrite holds only these */
static int blank(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

/* GO [count] alone on its line, before a line break, the end or a `--` */
static size_t go_end(const char *s, size_t len, size_t a, size_t e)
{
	size_t b = a;
	while (b && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\f' || s[b - 1] == '\v')) b--;
	if (b && s[b - 1] != '\n') return 0;
	size_t i = e;
	while (i < len && (s[i] == ' ' || s[i] == '\t')) i++;
	size_t d = i;
	while (i < len && s[i] >= '0' && s[i] <= '9') i++;
	size_t end = i > d ? i : e;
	while (i < len && (s[i] == ' ' || s[i] == '\t')) i++;
	if (i == len || s[i] == '\n' || (s[i] == '\r' && i + 1 < len && s[i + 1] == '\n') || (s[i] == '-' && i + 1 < len && s[i + 1] == '-'))
		return end;
	return 0;
}

static void diag(Pass *p, size_t off)
{
	if (!GROW(p->diag, p->ndiag, p->diagcap)) { oom = 1; return; }
	p->diag[p->ndiag++] = off;
}

static void lex(Pass *p, const Text *t)
{
	p->n = p->ndiag = 0;
	/* a byte-order mark at the start is a gap, as SQLDiff lexes it */
	size_t bom = p->len >= 3 && !memcmp(p->src, "\xEF\xBB\xBF", 3) ? 3 : 0;
	static TsqlRd r;
	r.t = t, r.n = 0;
	for (size_t pos = bom, end; pos < p->len; pos = end) {
		int k, kind;
		end = tsql_token(&r, pos, &kind);
		if (end <= pos) break;
		const char *s = p->src + pos;
		size_t n = end - pos;
		switch (kind) {
		case TQ_SPACE: continue;
		case TQ_WORD: k = WORD; break;
		case TQ_IDENT: k = QUOTED; break;
		case TQ_VAR: k = s[0] == '#' ? WORD : VAR; break;
		case TQ_STRING: k = STRING; break;
		case TQ_NUMBER: k = NUMBER; break;
		case TQ_PUNCT: k = s[0] == ',' ? COMMA : s[0] == '(' ? LP : s[0] == ')' ? RP : s[0] == ';' ? SEMI : DOT; break;
		case TQ_COMMENT: k = s[0] == '-' ? LCOMMENT : BCOMMENT; break;
		default:
			k = s[0] == ':' && n == 1 ? COLON : s[0] && strchr("+-*/<>=~!@#%^&|`?:{}", s[0]) ? OP : ERR;
		}
		if (k == WORD && n == 2 && (s[0] | 32) == 'g' && (s[1] | 32) == 'o') {
			size_t g = go_end(p->src, p->len, pos, end);
			if (g) k = GO, end = g;
		}
		if (!GROW(p->t, p->n, p->cap)) { oom = 1; return; }
		size_t id = 0;
		int rc = k == WORD && tsql_word(s, n, &id) & TQ_RECASE;
		p->t[p->n++] = (Tok){ (uint32_t)pos, (uint32_t)end, (uint8_t)k, wmap[id], (uint8_t)rc, 0, 0, 0, 0, 0, 0, -1 };
		/* an unclosed string, name or comment: SQLDiff reports it, so its
		   statement is kept as written */
		if (k == ERR || r.open) diag(p, pos);
	}
}

/* ---- the statement tree, as SQLDiff's parser.rs builds it for T-SQL ---- */

static Pass *P;          /* the pass being parsed */
static size_t pos;       /* next token */
static int nest, too_deep, stop_end, stop_else;

static int comment(size_t i) { return P->t[i].k == LCOMMENT || P->t[i].k == BCOMMENT; }
/* index of the n-th significant token from pos, or P->n */
static size_t sig(int n)
{
	for (size_t i = pos; i < P->n; i++)
		if (!comment(i) && !n--) return i;
	return P->n;
}
static int kind_at(int n) { size_t i = sig(n); return i < P->n ? P->t[i].k : -1; }
static int kw(int n) { size_t i = sig(n); return i < P->n ? word_of(P, i) : W_NONE; }

static void ev(int node, int tok)
{
	if (!GROW(P->ev, P->nev, P->evcap)) { oom = 1; return; }
	P->ev[P->nev++] = (struct Ev){ node, tok, 0 };
}
static size_t open_(int node) { ev(node, -1); return P->nev - 1; }
static void close_(size_t at) { if (at < P->nev) P->ev[at].end = (int)P->nev; }
static void trivia(void) { while (pos < P->n && comment(pos)) ev(0, (int)pos++); }
static void bump(void) { trivia(); if (pos < P->n) ev(0, (int)pos++); }
static void diag_here(void)
{
	if (too_deep) return;
	size_t i = sig(0);
	diag(P, i < P->n ? P->t[i].s : P->len);
}
static int at_stop(void)
{
	int k = kind_at(0), w = kw(0);
	return k < 0 || k == GO || (w == W_END && stop_end) || (w == W_ELSE && stop_else);
}
static int expect(int w)
{
	if (kw(0) == w) return bump(), 1;
	diag_here();
	return 0;
}

static void statement(void);

static void list(void)
{
	while (!at_stop() && !oom) {
		size_t before = pos;
		statement();
		if (pos == before) {
			trivia();
			diag_here();
			size_t e = open_(N_ERROR);
			bump();
			close_(e);
		}
	}
}

static void one(void)
{
	if (at_stop()) diag_here();
	else statement();
}

static void end_(int w)
{
	if (kw(0) != W_END) { diag_here(); return; }
	bump();
	if (w) expect(w);
	if (kind_at(0) == SEMI) bump();
}

/* T-SQL keywords that can begin a statement (TSQL_STARTERS) */
static int starter(int w)
{
	switch (w) {
	case W_SELECT: case W_INSERT: case W_UPDATE: case W_DELETE: case W_MERGE: case W_SET: case W_DECLARE:
	case W_IF: case W_WHILE: case W_BEGIN: case W_RETURN: case W_THROW: case W_RAISERROR: case W_PRINT:
	case W_EXEC: case W_EXECUTE: case W_COMMIT: case W_ROLLBACK: case W_SAVE: case W_CREATE: case W_ALTER:
	case W_DROP: case W_TRUNCATE: case W_GOTO: case W_BREAK: case W_CONTINUE: case W_WAITFOR: case W_FETCH:
	case W_OPEN: case W_CLOSE: case W_DEALLOCATE: case W_USE: case W_GRANT: case W_REVOKE: case W_DENY:
		return 1;
	}
	return 0;
}

static void condition(void)
{
	trivia();
	size_t c = open_(N_CONDITION);
	int depth = 0, cs = 0, first = 1, prevw = W_NONE, prevk = -1, k;
	while ((k = kind_at(0)) >= 0 && k != SEMI && k != GO) {
		int w = kw(0);
		if (!first && !depth && !cs && k == WORD) {
			int st = starter(w) || w == W_ELSE || w == W_END || (w == W_BULK && kw(1) == W_INSERT);
			int cont = prevw == W_AND || prevw == W_OR || prevw == W_NOT || prevk == COMMA || prevk == LP;
			if (st && !cont) break;
		}
		if (k == LP) depth++;
		else if (k == RP) depth -= depth > 0;
		else if (w == W_CASE) cs++;
		else if (w == W_END && cs) cs--;
		prevw = w, prevk = k;
		bump();
		first = 0;
	}
	close_(c);
}

/* whether w begins a new statement (tsql_starts_statement) */
static int begins(int w, int head, int prevw, int prevk, const char *seen, int next_insert)
{
	if (!(starter(w) || (w == W_BULK && next_insert))) return 0;
	if (prevk == COMMA || prevk == LP || prevk == DOT) return 0;
	switch (prevw) {
	case W_FOR: case W_THEN: case W_OF: case W_AFTER: case W_BEFORE: case W_INSTEAD: case W_UNION: case W_ALL:
	case W_EXCEPT: case W_INTERSECT: case W_OR: case W_AND: case W_NOT: case W_WITH: case W_GRANT: case W_REVOKE:
	case W_DENY:
		return 0;
	}
	if (prevw == W_ON && (head == W_CREATE || head == W_ALTER)) return 0;
	int dml = seen[W_SELECT] || seen[W_INSERT] || seen[W_UPDATE] || seen[W_DELETE] || seen[W_MERGE];
	switch (w) {
	case W_SELECT:
		return !((head == W_INSERT && !(seen[W_SELECT] || seen[W_VALUES] || seen[W_EXEC] || seen[W_EXECUTE]))
		         || (head == W_WITH && !dml));
	case W_INSERT:
		if (head == W_BULK && prevw == W_BULK) return 0;
		/* fall through */
	case W_UPDATE: case W_DELETE: case W_MERGE:
		return !(head == W_WITH && !dml);
	case W_SET:
		return !((head == W_UPDATE && !seen[W_SET]) || prevw == W_UPDATE || head == W_ALTER);
	case W_EXEC: case W_EXECUTE:
		return !(head == W_INSERT && !(seen[W_SELECT] || seen[W_VALUES]));
	case W_IF: return !(head == W_DROP || head == W_CREATE || head == W_ALTER);
	case W_FETCH: return !(prevw == W_ROWS || prevw == W_ROW);
	case W_ALTER: case W_DROP: return head != W_ALTER;
	}
	return 1;
}

/* a line break between the last token and the next significant one */
static int break_ahead(void)
{
	size_t a = pos ? P->t[pos - 1].e : 0, z = sig(0) < P->n ? P->t[sig(0)].s : P->len;
	return memchr(P->src + a, '\n', z - a) != NULL;
}

static void simple(void)
{
	size_t st = open_(N_STATEMENT);
	int depth = 0, cs = 0, first = 1, head = W_NONE, headset = 0, prevw = W_NONE, prevk = -1, k;
	char seen[NW] = { 0 };   /* the words seen at depth 0 */
	int sqlcmd = kind_at(0) == COLON;
	while ((k = kind_at(0)) >= 0 && k != GO) {
		if (sqlcmd && !first && break_ahead()) break;
		if (k == SEMI) { bump(); break; }
		int w = kw(0);
		if (!first && !depth && !cs && k == WORD) {
			if (w == W_END || w == W_ELSE) break;
			if (begins(w, head, prevw, prevk, seen, kw(1) == W_INSERT)) break;
		}
		if (k == LP) depth++;
		else if (k == RP) depth -= depth > 0;
		if (k == WORD) {
			if (w == W_CASE) cs++;
			else if (w == W_END && cs) cs--;
			if (!depth) {
				/* `WITH cte AS (...) UPDATE ...` continues as an UPDATE does */
				if (!headset || (head == W_WITH && (w == W_SELECT || w == W_INSERT || w == W_UPDATE || w == W_DELETE || w == W_MERGE)))
					head = w, headset = 1;
				seen[w] = 1;
			}
			prevw = w;
		} else prevw = W_NONE;
		prevk = k;
		bump();
		first = 0;
	}
	close_(st);
}

static void routine(void)
{
	size_t r = open_(N_ROUTINE);
	int depth = 0, prevw = W_NONE, prevk = -1, fn = 0, returns = 0, k;
	while ((k = kind_at(0)) >= 0 && k != GO) {
		int w = kw(0);
		if (!depth && w == W_AS && prevw != W_EXECUTE && prevk != VAR) break;
		if (!depth && k == WORD) {
			if (w == W_FUNCTION) fn = 1;
			else if (w == W_RETURNS) returns = 1;
			else if (fn && returns && (w == W_BEGIN || w == W_RETURN)) break;
		}
		if (k == LP) depth++;
		else if (k == RP) depth -= depth > 0;
		prevw = k == WORD ? w : W_NONE, prevk = k;
		bump();
	}
	int w = kw(0);
	if (w == W_AS || (fn && returns && (w == W_BEGIN || w == W_RETURN))) {
		if (w == W_AS) bump();
		trivia();
		size_t b = open_(N_ROUTINE_BODY);
		list();
		close_(b);
	} else diag_here();
	close_(r);
}

/* a terminator right after BEGIN, BEGIN TRY or BEGIN CATCH is the opener's */
static void opener_semi(void) { if (kind_at(0) == SEMI) bump(); }

static void block_list(int w)
{
	stop_end++;
	list();
	stop_end--;
	end_(w);
}

static void statement(void)
{
	if (nest >= 1000) {
		/* SQLDiff's MAX_STATEMENT_NESTING: the rest of the batch is one error */
		trivia();
		diag_here();
		too_deep = 1;
		size_t e = open_(N_ERROR);
		while (kind_at(0) >= 0 && kind_at(0) != GO) bump();
		close_(e);
		return;
	}
	nest++;
	trivia();
	int w = kw(0), w1 = kw(1);
	if (kind_at(0) == WORD && kind_at(1) == COLON) {
		size_t l = open_(N_LABEL);
		bump(), bump();
		close_(l);
	} else if (w == W_BEGIN && (w1 == W_TRAN || w1 == W_TRANSACTION || w1 == W_DISTRIBUTED || w1 == W_DIALOG || w1 == W_CONVERSATION)) {
		simple();
	} else if (w == W_BEGIN && w1 == W_TRY) {
		size_t tc = open_(N_TRY_CATCH), b = open_(N_TRY_BLOCK);
		bump(), bump(), opener_semi();
		block_list(W_TRY);
		close_(b);
		trivia();
		if (kw(0) == W_BEGIN && kw(1) == W_CATCH) {
			b = open_(N_CATCH_BLOCK);
			bump(), bump(), opener_semi();
			block_list(W_CATCH);
			close_(b);
		} else diag_here();
		close_(tc);
	} else if (w == W_BEGIN) {
		size_t b = open_(N_BLOCK);
		bump(), opener_semi();
		block_list(0);
		close_(b);
	} else if (w == W_IF || w == W_WHILE) {
		size_t s = open_(w == W_IF ? N_IF : N_LOOP);
		bump();
		condition();
		trivia();
		size_t b = open_(N_BRANCH);
		stop_else += w == W_IF;
		one();
		stop_else -= w == W_IF;
		close_(b);
		if (w == W_IF && kw(0) == W_ELSE) {
			bump();
			trivia();
			b = open_(N_BRANCH);
			one();
			close_(b);
		}
		close_(s);
	} else if ((w == W_CREATE || w == W_ALTER)
	           && ((w1 = kw(w1 == W_OR ? 3 : 1)) == W_PROCEDURE || w1 == W_PROC || w1 == W_FUNCTION || w1 == W_TRIGGER || w1 == W_VIEW)) {
		routine();
	} else simple();
	if (!--nest) too_deep = 0;
}

static void parse(Pass *p)
{
	P = p, pos = 0, nest = too_deep = stop_end = stop_else = 0;
	p->nev = 0;
	size_t f = open_(N_FILE);
	for (;;) {
		trivia();
		int k = kind_at(0);
		if (k < 0 || oom) break;
		if (k == GO) { bump(); continue; }
		size_t b = open_(N_BATCH);
		list();
		close_(b);
	}
	while (pos < p->n) ev(0, (int)pos++);
	close_(f);
}

/* ---- layout (layout.rs collect) ---- */

static int unit_node(int n) { return n == N_STATEMENT || n == N_ROUTINE || n == N_BLOCK || n == N_TRY_CATCH || n == N_IF || n == N_LOOP || n == N_LABEL || n == N_ERROR; }
static int container_node(int n) { return n == N_FILE || n == N_BATCH || n == N_ROUTINE_BODY || n == N_TRY_BLOCK || n == N_CATCH_BLOCK || n == N_BRANCH; }

static int depth_, base_, top_;

static void token(int i, int level)
{
	Tok *t = &P->t[i];
	if (t->k == RP) depth_ -= depth_ > 0;
	t->level = level;
	t->depth = depth_ > base_ ? depth_ - base_ : 0;
	t->brk = B_NONE;
	t->top = top_;
	if (t->k == LP) depth_++;
}

/* the last significant token so far, or -1 */
static int last_sig(int before)
{
	for (int i = before - 1; i >= 0; i--)
		if (!comment((size_t)i)) return i;
	return -1;
}

/* the first significant token of events [a, z), or -1 */
static int first_sig(size_t a, size_t z)
{
	for (size_t j = a; j < z; j++)
		if (!P->ev[j].node && !comment((size_t)P->ev[j].tok)) return P->ev[j].tok;
	return -1;
}

/* the events that are node at's children: its tokens and its child nodes */
#define CHILDREN(j, at) for (size_t j = (at) + 1; j < (size_t)P->ev[at].end; j = P->ev[j].node ? (size_t)P->ev[j].end : j + 1)

static void unit(size_t at, int level, int brk);
static void container(size_t at, int level);
static void clauses(size_t a, size_t z);

static void own_token(size_t j, int level, int *after_body)
{
	int i = P->ev[j].tok;
	token(i, level);
	if (!comment((size_t)i)) {
		if (*after_body && P->t[i].k != SEMI && P->t[i].k != COMMA) P->t[i].brk = B_FOOTER;
		*after_body = 0;
	}
}

static void inline_(size_t at, int level)
{
	CHILDREN(j, at) {
		int n = P->ev[j].node;
		if (!n) token(P->ev[j].tok, level);
		else if (container_node(n)) container(j, level);
		else if (unit_node(n) && n != N_LABEL) unit(j, level + 1, 1);
		else inline_(j, level);
	}
}

static void unit(size_t at, int level, int brk)
{
	int after = 0, saved = base_;
	base_ = depth_;
	CHILDREN(j, at) {
		int n = P->ev[j].node;
		if (!n) own_token(j, level, &after);
		else if (n == N_CONDITION || n == N_LABEL) inline_(j, level);
		else if (container_node(n)) container(j, level), after = 1;
		else if (unit_node(n)) unit(j, level + 1, 1), after = 1;
		else inline_(j, level);
	}
	base_ = saved;
	int f = first_sig(at + 1, (size_t)P->ev[at].end);
	if (brk && f >= 0) P->t[f].brk = B_UNIT;
	if (P->ev[at].node == N_STATEMENT) clauses(at + 1, (size_t)P->ev[at].end);
	if (P->ev[at].node == N_ERROR)
		for (size_t j = at + 1; j < (size_t)P->ev[at].end; j++)
			if (!P->ev[j].node) P->t[P->ev[j].tok].verb = 1;
}

static void container(size_t at, int level)
{
	int k = P->ev[at].node, units = 0, children = 0, only = 0;
	size_t z = (size_t)P->ev[at].end;
	CHILDREN(j, at)
		if (P->ev[j].node) {
			children++;
			if (unit_node(P->ev[j].node)) units++, only = P->ev[j].node;
		}
	int ls = last_sig(first_sig(at + 1, z) >= 0 ? first_sig(at + 1, z) : (int)P->n);
	/* T-SQL `ELSE IF`: the nested IF stays on the ELSE line */
	if (k == N_BRANCH && units == 1 && only == N_IF && children == 1 && ls >= 0 && word_of(P, (size_t)ls) == W_ELSE) {
		CHILDREN(j, at)
			if (!P->ev[j].node) token(P->ev[j].tok, level);
			else unit(j, level, 0);
		return;
	}
	/* a body that is one BEGIN ... END block lines up with its header */
	int inner = (k == N_BRANCH || k == N_ROUTINE_BODY) && units == 1 && only == N_BLOCK && children == 1 ? level : level + 1;
	int after = 0;
	CHILDREN(j, at) {
		int n = P->ev[j].node;
		if (!n) own_token(j, level, &after);
		else if (unit_node(n) && n != N_LABEL) unit(j, inner, 1), after = 1;
		else if (n == N_BRANCH) container(j, level), after = 1;
		else if (container_node(n)) container(j, inner), after = 1;
		else inline_(j, level);
	}
	if (k == N_TRY_BLOCK || k == N_CATCH_BLOCK) {
		int f = first_sig(at + 1, z);
		if (f >= 0 && P->t[f].brk == B_NONE) P->t[f].brk = B_FOOTER;
	}
}

static int is_join_prefix(int w)
{
	return w == W_LEFT || w == W_RIGHT || w == W_FULL || w == W_INNER || w == W_CROSS || w == W_OUTER || w == W_NATURAL
	    || w == W_LOOP || w == W_HASH || w == W_MERGE || w == W_REMOTE;
}

/* the next token of events [*j, z) that is no comment, or -1 */
static int next_sig(size_t *j, size_t z)
{
	for (; *j < z; ++*j)
		if (!P->ev[*j].node && !comment((size_t)P->ev[*j].tok)) return P->ev[(*j)++].tok;
	return -1;
}

/* line breaks before the clauses of a query or DML statement */
static void clauses(size_t a, size_t z)
{
	int head = -1, i;
	for (size_t j = a; (i = next_sig(&j, z)) >= 0; ) {
		if (P->t[i].verb) return;
		if (head < 0) head = word_of(P, (size_t)i);
	}
	/* SQLDiff also breaks a CREATE VIEW's clauses, but in T-SQL the view's
	   query is a statement of its own (begins()), so for T-SQL that rule
	   never applies (measured: without it 0 of 1,687 texts, SQLDiff's files
	   and tsql.py, change) */
	if (!(head == W_SELECT || head == W_INSERT || head == W_UPDATE || head == W_DELETE || head == W_MERGE || head == W_WITH))
		return;
	int case_open = 0;
	size_t j = a;
	int pi = next_sig(&j, z), ci = next_sig(&j, z);
	for (int ni; ci >= 0; pi = ci, ci = ni) {
		ni = next_sig(&j, z);
		Tok *t = &P->t[ci];
		int w = word_of(P, (size_t)ci), prev = word_of(P, (size_t)pi);
		int next = ni >= 0 ? word_of(P, (size_t)ni) : W_NONE;
		if (!t->depth) {
			if (w == W_CASE) case_open++;
			else if (w == W_END && case_open) case_open--;
		}
		if (t->depth || case_open || t->brk != B_NONE) continue;
		/* `t.select` is a name */
		if (t->k != WORD || P->t[pi].k == DOT) continue;
		int c;
		switch (w) {
		case W_FROM: c = prev != W_DELETE && prev != W_DISTINCT; break;
		case W_WHERE: case W_HAVING: case W_UNION: case W_INTERSECT: case W_EXCEPT: case W_RETURNING: case W_LIMIT:
		case W_OFFSET: case W_SELECT: c = 1; break;
		case W_GROUP: case W_ORDER: c = next == W_BY; break;
		case W_VALUES: c = prev != W_DEFAULT; break;
		case W_SET: c = head == W_UPDATE || head == W_MERGE; break;
		case W_OUTPUT: c = head == W_INSERT || head == W_UPDATE || head == W_DELETE || head == W_MERGE; break;
		case W_USING: case W_WHEN: c = head == W_MERGE; break;
		case W_JOIN: c = !is_join_prefix(prev); break;
		case W_LEFT: case W_RIGHT: case W_FULL: case W_INNER: case W_CROSS: case W_NATURAL:
			c = (next == W_JOIN || next == W_OUTER || next == W_APPLY || next == W_HASH || next == W_LOOP || next == W_MERGE || next == W_REMOTE)
			    && !is_join_prefix(prev);
			break;
		case W_OUTER: c = next == W_APPLY && !is_join_prefix(prev); break;
		default: c = 0;
		}
		if (c) t->brk = B_CLAUSE;
	}
}

static void file(size_t at)
{
	CHILDREN(j, at) {
		int n = P->ev[j].node;
		if (!n) token(P->ev[j].tok, 0);
		else if (n == N_BATCH) file(j);
		else if (unit_node(n)) {
			int f = -1, l = -1;
			for (size_t q = j + 1; q < (size_t)P->ev[j].end; q++)
				if (!P->ev[q].node) { if (f < 0) f = P->ev[q].tok; l = P->ev[q].tok; }
			if (!GROW(P->tops, P->ntops, P->topcap)) { oom = 1; return; }
			P->tops[P->ntops][0] = f >= 0 ? P->t[f].s : 0;
			P->tops[P->ntops][1] = l >= 0 ? P->t[l].e : 0;
			top_ = (int)P->ntops++;
			unit(j, 0, 1);
			top_ = -1;
		} else inline_(j, 0);
	}
}

/* the first token starting at or after off */
static size_t tok_at(size_t off)
{
	size_t lo = 0, hi = P->n;
	while (lo < hi) {
		size_t m = lo + (hi - lo) / 2;
		if (P->t[m].s < off) lo = m + 1;
		else hi = m;
	}
	return lo;
}

/* sqlcmd lines (`:setvar`, `:r`) and lines starting with `\`, and every
   top-level statement with a parse problem, stay exactly as written */
static void protect(void)
{
	const char *s = P->src;
	for (size_t i = 0; i < P->n; i++) {
		Tok *t = &P->t[i];
		size_t b = t->s;
		while (b && (s[b - 1] == ' ' || s[b - 1] == '\t')) b--;
		size_t rs, re;
		if ((!b || s[b - 1] == '\n') && (s[t->s] == '\\' || s[t->s] == ':')) {
			rs = t->s;
			const char *nl = memchr(s + rs, '\n', P->len - rs);
			re = nl ? (size_t)(nl - s) : P->len;
		} else continue;
		size_t j = i;
		for (; j < P->n && P->t[j].s < re; j++) P->t[j].verb = 1;
		if (j < P->n) P->t[j].gapk = 1;
	}
	for (size_t d = 0; d < P->ndiag; d++) {
		/* the last top-level statement starting at or before the problem */
		size_t lo = 0, hi = P->ntops;
		while (lo < hi) {
			size_t m = lo + (hi - lo) / 2;
			if (P->tops[m][0] <= P->diag[d]) lo = m + 1;
			else hi = m;
		}
		if (!lo || P->diag[d] >= P->tops[lo - 1][1]) continue;
		size_t j = tok_at(P->tops[lo - 1][0]);
		for (; j < P->n && P->t[j].s < P->tops[lo - 1][1]; j++) P->t[j].verb = 1;
		if (j < P->n) P->t[j].gapk = 1;
	}
}

/* keywords whose case changes (layout.rs keyword_set, T-SQL) */
static void keywords(void)
{
	int prev = -1, prev2 = -1;
	for (size_t k = 0; k < P->n; k++) {
		if (comment(k)) continue;
		Tok *t = &P->t[k];
		size_t nx = k + 1;
		while (nx < P->n && comment(nx)) nx++;
		int nk = nx < P->n ? P->t[nx].k : -1;
		t->kw = 0;
		int ok = t->k == WORD && !t->verb && !(prev >= 0 && P->t[prev].k == DOT) && nk != DOT && nk != COLON;
		/* `:name` is a variable; only `name: word`, with a gap, is a label */
		if (ok && prev >= 0 && P->t[prev].k == COLON) {
			const Tok *c = &P->t[prev];
			ok = c->e < t->s && prev2 >= 0 && (P->t[prev2].k == WORD || P->t[prev2].k == QUOTED) && P->t[prev2].e == c->s;
		}
		for (size_t j = t->s; ok && j < t->e; j++) ok = (unsigned char)P->src[j] < 0x80;
		t->kw = ok && t->rc;
		prev2 = prev, prev = (int)k;
	}
}

static int layout(Pass *p, const Text *t)
{
	oom = 0;
	p->ntops = 0;
	if (!words()) return -1;
	lex(p, t);
	parse(p);
	depth_ = base_ = 0, top_ = -1;
	if (!oom) file(0);
	if (!oom) protect();
	keywords();
	return oom ? -1 : 0;
}

/* ---- rendering (layout.rs render) ---- */

static char *out;
static size_t nout, outcap;
static size_t *starts;
static size_t startcap;

static void put(const char *s, size_t n)
{
	while (nout + n >= outcap)   /* never NULL, even for an empty text */
		if (!grow((void **)&out, &outcap, 1)) { oom = 1; return; }
	memcpy(out + nout, s, n);
	nout += n;
}

static int comparison(const Pass *p, const Tok *t)
{
	size_t n = t->e - t->s;
	const char *s = p->src + t->s;
	return t->k == OP && ((n == 1 && (*s == '=' || *s == '<' || *s == '>'))
	    || (n == 2 && (!memcmp(s, "<>", 2) || !memcmp(s, "!=", 2) || !memcmp(s, "<=", 2) || !memcmp(s, ">=", 2))));
}

/* whether the empty gap before token i takes a space: next to another
   operator it stays empty, since T-SQL reads `*=` as one operator */
static int comparison_gap(const Pass *p, size_t i)
{
	const Tok *a = &p->t[i - 1], *b = &p->t[i];
	int sign = b->k == OP && b->e - b->s == 1 && (p->src[b->s] == '-' || p->src[b->s] == '+');
	return (comparison(p, b) && a->k != OP) || (comparison(p, a) && (b->k != OP || sign));
}

/* Davy's layout rules (2026-11-19, his screenshots; SQLDiff PR 516,
   layout.rs Depth, hangs() and column()): a `(` that ends its line hangs:
   what it holds sits one step in from the level of the line that opened
   it, and its `)` at that level, as does the `(` when alone on its line.
   AND, OR and ON that start a line of a WHERE, HAVING or join clause end
   where the clause's first word ends (a river), at every depth.  ds holds,
   per parenthesis depth of the statement: inner, the indent of a line in
   the `(` that opened it (-1: none seen); hang, where that `(` and its `)`
   sit (-1: it does not hang); head, the last river head that started a
   line there (NOHEAD: none), at its indent; between, BETWEENs waiting for
   their AND, which is no conjunction.  Indents only add, so an unclosed
   `(` cannot push one below its level. */
#define NOHEAD ((size_t)-1)
static struct Depth { int inner, hang, at; size_t head; int between; } *ds;
static size_t nds, dscap;
static int line_;    /* the level, in spaces, of the last line starting with code */
static int lined_;   /* the indent newline() gave the token being rendered, or -1 */

static int head_word(int w)
{
	return w == W_WHERE || w == W_HAVING || w == W_JOIN || w == W_LEFT || w == W_RIGHT || w == W_FULL
	    || w == W_INNER || w == W_CROSS;
}

/* whether `(` i hangs: it ends its line (the gap after it holds a line
   break, or what follows starts a line of its own) */
static int hangs(const Pass *p, size_t i)
{
	const Tok *t = &p->t[i];
	if (t->k != LP || t->brk != B_NONE || i + 1 >= p->n) return 0;
	return p->t[i + 1].brk != B_NONE || memchr(p->src + t->e, '\n', p->t[i + 1].s - t->e);
}

static int inner(const Tok *t)
{
	return (size_t)t->depth < nds && ds[t->depth].inner >= 0 ? ds[t->depth].inner : (t->level + 1 + t->depth) * 4;
}

/* the river indent of token c, or -1 */
static int river(const Pass *p, size_t c)
{
	const Tok *t = &p->t[c];
	int w = word_of(p, c);
	if ((size_t)t->depth >= nds || (w != W_AND && w != W_OR && w != W_ON)) return -1;
	const struct Depth *d = &ds[t->depth];
	if (d->head == NOHEAD || d->between || p->t[d->head].level != t->level) return -1;
	int r = (int)(p->t[d->head].e - p->t[d->head].s) - (int)(t->e - t->s);
	return d->at + (r > 0 ? r : 0);
}

/* the indent, in spaces, of code token c starting a line */
static int column(const Pass *p, size_t c)
{
	const Tok *t = &p->t[c];
	if (t->brk != B_NONE) return t->level * 4;
	int r = river(p, c);
	if (r >= 0) return r;
	if (hangs(p, c)) return t->depth ? inner(t) : line_;
	if (t->k == RP && (size_t)t->depth + 1 < nds && ds[t->depth + 1].hang >= 0) return ds[t->depth + 1].hang;
	return inner(t);
}

/* token i's indent; c is the first token at or after i that is no comment,
   and a comment on its own line lines up with the code after it */
static int indent_of(const Pass *p, size_t i, size_t c)
{
	if (c < p->n) return c != i && p->t[c].brk == B_FOOTER ? (p->t[c].level + 1) * 4 : column(p, c);
	return p->t[i].level * 4;
}

/* Line breaks are "\n": a CR LF file is read without its CRs ('fileformat'
   dos, PR 53), so a CR left in the text is a character, as in Neovim.
   SQLDiff keeps the ending most of its input uses instead, which a string
   holding line breaks can flip between one run and the next. */
static void newline(const Pass *p, size_t i, size_t c, size_t n)
{
	while (n--) put("\n", 1);
	for (int k = lined_ = indent_of(p, i, c); k-- > 0; ) put(" ", 1);
}

/* reverted: one byte per top-level statement, kept as written if set */
static void render(const Pass *p, const unsigned char *reverted)
{
	const char *s = p->src;
	nout = nds = 0, line_ = 0;
	/* c: the next token that is no comment, found once per run of comments */
	for (size_t i = 0, c = 0; i < p->n; i++) {
		const Tok *t = &p->t[i];
		for (c = c > i ? c : i; c < p->n && (p->t[c].k == LCOMMENT || p->t[c].k == BCOMMENT); c++) ;
		size_t pe = i ? p->t[i - 1].e : 0;
		const char *g = s + pe;
		size_t gn = t->s - pe, nls = 0;
		int plain = 1;
		for (size_t j = 0; j < gn; j++) {
			plain &= blank(g[j]);
			nls += g[j] == '\n';
		}
		int in_rev = t->top >= 0 && reverted[t->top];
		int kept = t->verb || (in_rev && i && p->t[i - 1].top == t->top);
		const Tok *a = i ? &p->t[i - 1] : NULL;
		int strings = a && (a->k == STRING && t->k == STRING);
		size_t n = nls < 2 ? nls : 2;
		lined_ = -1;
		if (!i) {
			if (!plain) put(g, gn);
		} else if (kept || t->gapk || !plain || strings) put(g, gn);
		else if (a->k == LCOMMENT) newline(p, i, c, n ? n : 1);
		else if (t->k == GO) for (size_t k = n ? n : 1; k--; ) put("\n", 1);
		else if (a->k == GO) {
			if ((t->k == LCOMMENT || t->k == BCOMMENT) && !nls) put(g, gn);
			else newline(p, i, c, n ? n : 1);
		} else if (t->brk != B_NONE) newline(p, i, c, n ? n : 1);
		else if (nls) newline(p, i, c, n);
		else if (!gn) { if (a->k == COMMA || comparison_gap(p, i)) put(" ", 1); }
		else put(" ", 1);
		if (!GROW(starts, i, startcap)) { oom = 1; return; }
		starts[i] = nout;
		size_t at = nout;
		put(s + t->s, t->e - t->s);
		size_t d = (size_t)t->depth;
		/* a statement starts afresh: nothing of the last one carries over */
		if (t->brk == B_UNIT || (i && p->t[i - 1].k == SEMI)) nds = 0;
		for (; nds <= d; nds++) {
			if (!GROW(ds, nds, dscap)) { oom = 1; return; }
			ds[nds] = (struct Depth){ -1, -1, 0, NOHEAD, 0 };
		}
		int w = word_of(p, i);
		if (lined_ >= 0 && t->k != LCOMMENT && t->k != BCOMMENT) {
			line_ = river(p, i) >= 0 ? ds[d].at : lined_;
			if (head_word(w)) ds[d].head = i, ds[d].at = lined_, ds[d].between = 0;
			else if (t->brk != B_NONE) ds[d].head = NOHEAD;
		}
		if (w == W_BETWEEN) ds[d].between++;
		else if (w == W_AND && ds[d].between) ds[d].between--;
		if (t->k == RP) nds = d + 1;
		if (t->k == LP) {
			int hang = hangs(p, i) ? line_ : -1, in = hang >= 0 ? hang : inner(t);
			nds = d + 1;
			if (!GROW(ds, nds, dscap)) { oom = 1; return; }
			ds[nds++] = (struct Depth){ in + 4, hang, 0, NOHEAD, 0 };
		}
		/* a statement kept as written keeps the case of its first word too */
		if (t->kw && !kept && !in_rev && !oom)
			for (size_t j = at; j < nout; j++)
				if (out[j] >= 'A' && out[j] <= 'Z') out[j] = (char)(out[j] + 32);
	}
	size_t te = p->n ? p->t[p->n - 1].e : 0;
	int plain = 1;
	for (size_t j = te; j < p->len; j++) plain &= blank(s[j]);
	/* a token kept as written may run to the end (an unclosed string): a
	   line ending after it would land inside it */
	if (!plain || !p->n || p->t[p->n - 1].verb) put(s + te, p->len - te);
	else put("\n", 1);
}

/* ---- the check: the output read again must give the same ---- */

static Pass in, again;
static char *src;
static size_t srccap;
static unsigned char *rev;
static size_t revcap;

/* The check: the output lexed again gives the same tokens, a keyword's
   case aside.  Then it also parses into the same tree with the same
   problems (SQLDiff's steps 2 and 3, left out): the parser reads only the
   tokens' kinds and words, and line breaks only inside a statement that
   starts with `:`, where the layout adds none and removes none (its first
   word is no clause head, nothing in it is a unit, and join_lines is off).
   Hypothesis, measured: with steps 2 and 3 run as aborts, none failed in
   378,028 fuzz_sqlfmt inputs, 4,000 generated texts (oracle/) and the 186
   .sql files of SQLDiff's repository.  Returns 0, 1 with new top-level statements to keep as
   written set in rev, or -1: the failure cannot be pinned to a new one. */
static int verify(const Text *ot)
{
	Pass *a = &in, *b = &again;
	b->src = out, b->len = nout;
	oom = 0;
	lex(b, ot);
	if (oom) return -1;
	int pinned = 0;
	for (size_t k = 0; k < a->n || k < b->n; k++) {
		if (k < a->n && k < b->n) {
			const Tok *x = &a->t[k], *y = &b->t[k];
			size_t n = x->e - x->s;
			if (x->k == y->k && n == y->e - y->s) {
				if (!memcmp(a->src + x->s, b->src + y->s, n)) continue;
				int same = x->kw;
				for (size_t j = 0; same && j < n; j++)
					same = (a->src[x->s + j] | 32) == (b->src[y->s + j] | 32);
				if (same) continue;
			}
		}
		/* the output reads differently at token k */
		for (size_t q = k ? k - 1 : 0; q <= k && q < a->n; q++)
			if (a->t[q].top >= 0 && !rev[a->t[q].top]) rev[a->t[q].top] = 1, pinned = 1;
		return pinned ? 1 : -1;
	}
	return 0;
}

static int run(const Text *t, size_t *a, size_t *z, const char **res, size_t *n)
{
	size_t len = text_len(t);
	while (srccap < len + 1)
		if (!grow((void **)&src, &srccap, 1)) return -1;
	in.src = src, in.len = text_read(t, 0, src, len);
	if (layout(&in, t)) return -1;
	while (revcap < in.ntops + 1)
		if (!grow((void **)&rev, &revcap, 1)) return -1;
	memset(rev, 0, in.ntops + 1);
	int kept = -1;
	for (int round = 0; round < 8; round++) {
		oom = 0;
		render(&in, rev);
		if (oom) return -1;
		Text *ot = text_new(out, nout);
		if (!ot) return -1;
		int v = verify(ot);
		text_free(ot);
		P = &in;
		if (oom) return -1;
		if (!v) { kept = 0; break; }
		if (v < 0) break;
	}
	if (kept < 0) {
		/* the whole file as written */
		*a = *z = 0, *res = out, *n = 0;
		return in.ntops ? (int)in.ntops : 1;
	}
	for (size_t u = 0; u < in.ntops; u++) kept += rev[u];
	/* the change to the gaps and words of tokens starting in [a, z) */
	size_t i0 = 0, i1;
	while (i0 < in.n && in.t[i0].s < *a) i0++;
	for (i1 = i0; i1 < in.n && in.t[i1].s < *z; i1++) ;
	size_t ia = i0 ? in.t[i0 - 1].e : 0, oa = i0 ? starts[i0 - 1] + (in.t[i0 - 1].e - in.t[i0 - 1].s) : 0;
	size_t ib, ob;
	if (i1 == in.n && *z >= len) ib = len, ob = nout;          /* the tail too */
	else if (i1 > i0) ib = in.t[i1 - 1].e, ob = starts[i1 - 1] + (in.t[i1 - 1].e - in.t[i1 - 1].s);
	else ib = ia, ob = oa;
	/* the smallest edit: drop the bytes both ends share */
	while (ia < ib && oa < ob && src[ia] == out[oa]) ia++, oa++;
	while (ib > ia && ob > oa && src[ib - 1] == out[ob - 1]) ib--, ob--;
	*a = ia, *z = ib, *res = out + oa, *n = ob - oa;
	return kept;
}

/* Only the result lives on until the next call: the tokens, events and
   copies take 20 times a large text (11.8 MB for 585 KB, measured), and
   growing them again each call shows no measurable difference (measured:
   303 KB, 7 alternating runs, median 8.7 ms freed, 8.5 ms kept, ranges
   8.6 to 10.4 and 7.9 to 11.7). */
int sqlfmt(const Text *t, size_t *a, size_t *z, const char **res, size_t *n)
{
	int r = run(t, a, z, res, n);
	Pass *ps[] = { &in, &again };
	for (int k = 0; k < 2; k++) {
		free(ps[k]->t), free(ps[k]->ev), free(ps[k]->diag), free(ps[k]->tops);
		*ps[k] = (Pass){ 0 };
	}
	free(src), free(rev), free(starts), free(ds);
	src = NULL, rev = NULL, starts = NULL, ds = NULL, srccap = revcap = startcap = dscap = 0;
	return r;
}
