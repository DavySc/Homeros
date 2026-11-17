/* Reference undo: a list of groups; undoing then editing drops the redo part. */
#include <stdlib.h>
#include <string.h>
#include "undo.h"

typedef struct { size_t pos, ndel, nins; char *del, *ins; } Edit;
typedef struct { Edit *e; size_t n, cap; UndoCursor cur; unsigned long seq; unsigned after; } Group;
struct Undo { Group *g; size_t n, cap, done; int open; unsigned long last; UndoHook *hook; void *ctx; };

Undo *undo_new(void) { return calloc(1, sizeof(Undo)); }

static void group_free(Group *g)
{
	for (size_t i = 0; i < g->n; i++) { free(g->e[i].del); free(g->e[i].ins); }
	free(g->e);
}

void undo_free(Undo *u)
{
	if (!u) return;
	for (size_t i = 0; i < u->n; i++) group_free(&u->g[i]);
	free(u->g); free(u);
}

static char *dup(const char *s, size_t n)
{
	char *d = malloc(n ? n : 1);
	if (d && n) memcpy(d, s, n);
	return d;
}

int undo_record(Undo *u, size_t pos, const char *del, size_t ndel,
                const char *ins, size_t nins, UndoCursor cur, unsigned after)
{
	if (!u->open) {
		while (u->n > u->done) group_free(&u->g[--u->n]);
		if (u->n == u->cap) {
			size_t cap = u->cap ? 2 * u->cap : 16;
			Group *g = realloc(u->g, cap * sizeof *g);
			if (!g) return -1;
			u->g = g; u->cap = cap;
		}
		u->g[u->n++] = (Group){ .cur = cur, .seq = ++u->last };
		u->done = u->n; u->open = 1;
	}
	Group *g = &u->g[u->n - 1];
	g->after = after;
	if (g->n == g->cap) {
		size_t cap = g->cap ? 2 * g->cap : 4;
		Edit *e = realloc(g->e, cap * sizeof *e);
		if (!e) return -1;
		g->e = e; g->cap = cap;
	}
	Edit e = { pos, ndel, nins, dup(del, ndel), dup(ins, nins) };
	if (!e.del || !e.ins) { free(e.del); free(e.ins); return -1; }
	g->e[g->n++] = e;
	return 0;
}

void undo_close(Undo *u) { u->open = 0; }

void undo_hook(Undo *u, UndoHook *fn, void *ctx) { u->hook = fn; u->ctx = ctx; }

/* The changed span of the result: bytes before the lowest edit and after the
   shortest untouched tail are the same before and after the group. */
static void span(const Group *g, size_t len, int undo, UndoWhere *w)
{
	size_t lo = (size_t)-1, tail = (size_t)-1, l = len;
	if (undo) /* replay lengths forward from the result (the old text) */
		for (size_t i = 0; i < g->n; i++) {
			const Edit *e = &g->e[i];
			l = l - e->ndel + e->nins;
			if (e->pos < lo) lo = e->pos;
			if (l - e->pos - e->nins < tail) tail = l - e->pos - e->nins;
		}
	else
		for (size_t i = g->n; i-- > 0;) {
			const Edit *e = &g->e[i];
			if (e->pos < lo) lo = e->pos;
			if (l - e->pos - e->nins < tail) tail = l - e->pos - e->nins;
			l = l - e->nins + e->ndel;
		}
	w->lo = lo;
	w->hi = len - tail;
	w->cur = g->cur;
	w->flags = undo ? g->cur.flags : g->after;
}

int undo_undo(Undo *u, Text *t, UndoWhere *w)
{
	u->open = 0;
	if (!u->done) return 0;
	Group *g = &u->g[u->done - 1];
	for (size_t i = g->n; i-- > 0;) {
		Edit *e = &g->e[i];
		if (u->hook) u->hook(u->ctx, t, e->pos, e->nins, e->del, e->ndel);
		if (text_delete(t, e->pos, e->nins) || text_insert(t, e->pos, e->del, e->ndel))
			return -1;
	}
	u->done--;
	span(g, text_len(t), 1, w);
	return 1;
}

int undo_redo(Undo *u, Text *t, UndoWhere *w)
{
	u->open = 0;
	if (u->done == u->n) return 0;
	Group *g = &u->g[u->done];
	for (size_t i = 0; i < g->n; i++) {
		Edit *e = &g->e[i];
		if (u->hook) u->hook(u->ctx, t, e->pos, e->ndel, e->ins, e->nins);
		if (text_delete(t, e->pos, e->ndel) || text_insert(t, e->pos, e->ins, e->nins))
			return -1;
	}
	u->done++;
	span(g, text_len(t), 0, w);
	return 1;
}

unsigned long undo_seq(const Undo *u) { return u->done ? u->g[u->done - 1].seq : 0; }
