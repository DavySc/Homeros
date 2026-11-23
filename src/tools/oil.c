/* oil.nvim's model, from its Lua (the comments name the file): the cache
   of entries by id, a directory's lines, and the lines back to actions.
   POSIX only so far: the Windows build leaves it out (the editor does not
   call it yet; tools_oil refuses on both). */
#ifndef _WIN32
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "match.h"
#include "oil.h"

/* cache.lua: entries by id; dir NULL once forgotten.  old marks the
   entries of a directory being read again (tmp_url_directory). */
typedef struct { char *dir, *name, *link; int type, linkdir, old; } Ent;
static Ent *E;
static int nE = 1, capE;           /* id 0 is ".." and never stored */

void oil_reset(void)
{
	for (int i = 1; i < nE; i++) { free(E[i].dir); free(E[i].name); free(E[i].link); }
	free(E);
	E = NULL; nE = 1; capE = 0;
}

static int find(const char *dir, const char *name, size_t n)
{
	for (int i = 1; i < nE; i++)
		if (E[i].dir && !strcmp(E[i].dir, dir) && strlen(E[i].name) == n && !memcmp(E[i].name, name, n)) return i;
	return -1;
}

/* create_and_store_entry: a name already in the directory keeps its id.
   oil keeps its type too, so a file replaced by a directory outside the
   editor stays a file; that is not copied: the type is what is there now. */
int oil_entry(const char *dir, const char *name, int type, const char *link, int linkdir)
{
	int i = find(dir, name, strlen(name));
	if (i < 0) {
		if (nE >= capE) {
			int c = capE ? capE * 2 : 64;
			Ent *t = realloc(E, (size_t)c * sizeof *t);
			if (!t) return -1;
			E = t; capE = c;
		}
		i = nE++;
		E[i] = (Ent){ .dir = strdup(dir), .name = strdup(name) };
		if (!E[i].dir || !E[i].name) return -1;
	}
	E[i].old = 0;
	E[i].type = type;
	free(E[i].link);
	E[i].link = link ? strdup(link) : NULL;
	/* a broken link keeps the target's type it had (read_link_data) */
	if (linkdir >= 0) E[i].linkdir = linkdir;
	return i;
}

int oil_list(const char *dir)
{
	DIR *d = opendir(dir);
	if (!d && errno != ENOENT) return -1;
	/* begin_update_url */
	for (int i = 1; i < nE; i++) if (E[i].dir && !strcmp(E[i].dir, dir)) E[i].old = 1;
	char p[4096], t[4096];
	/* a directory that does not exist lists as empty (files.lua's list) */
	for (struct dirent *de; d && (de = readdir(d));) {
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
		struct stat st;
		snprintf(p, sizeof p, "%s%s", dir, de->d_name);
		if (lstat(p, &st)) continue;
		int type = S_ISDIR(st.st_mode) ? OIL_DIR : S_ISLNK(st.st_mode) ? OIL_LINK : OIL_FILE;
		const char *link = NULL;
		if (type == OIL_LINK) {
			ssize_t k = readlink(p, t, sizeof t - 1);
			t[k > 0 ? k : 0] = 0;
			link = t;
		}
		if (oil_entry(dir, de->d_name, type, link, type != OIL_LINK ? 0 : stat(p, &st) ? -1 : S_ISDIR(st.st_mode)) < 0) {
			closedir(d);
			return -1;
		}
	}
	if (d) closedir(d);
	/* end_update_url */
	for (int i = 1; i < nE; i++)
		if (E[i].dir && E[i].old) { free(E[i].dir); E[i].dir = NULL; E[i].old = 0; }
	return 0;
}

/* columns.lua's name sort value with natural_order "fast" (up to 5,000
   entries): each run of digits, leading zeros dropped, as its length in
   three digits and then the digits */
static size_t natural(const char *s, char *out, size_t cap, int nat)
{
	size_t k = 0;
	for (size_t i = 0; s[i] && k + 8 < cap;) {
		if (nat && s[i] >= '0' && s[i] <= '9') {
			while (s[i] == '0' && s[i + 1] >= '0' && s[i + 1] <= '9') i++;
			/* gsub("0*(%d+)"): zeros before the last digit go, the digits stay */
			size_t j = i;
			while (s[j] >= '0' && s[j] <= '9') j++;
			k += (size_t)snprintf(out + k, cap - k, "%03zu", j - i);
			for (; i < j && k + 1 < cap; i++) out[k++] = s[i];
		} else out[k++] = s[i++];
	}
	out[k] = 0;
	return k;
}

static int sort_nat;
/* view.lua's get_sort_function with sort { type asc, name asc }:
   directories (and links to them) first, then the names */
static int by_view(const void *x, const void *y)
{
	const Ent *a = &E[*(const int *)x], *b = &E[*(const int *)y];
	int da = a->type == OIL_DIR || a->linkdir, db = b->type == OIL_DIR || b->linkdir;
	if (da != db) return da ? -1 : 1;
	char na[1024], nb[1024];
	natural(a->name, na, sizeof na, sort_nat);
	natural(b->name, nb, sizeof nb, sort_nat);
	int c = strcmp(na, nb);
	return c ? c : strcmp(a->name, b->name);
}

/* the id width: cache.format_id, at least 3 digits */
static int idw(void)
{
	int w = 1;
	for (int n = nE; n >= 10; n /= 10) w++;
	return w > 3 ? w : 3;
}

const char *oil_icon(int type, const char *name);

int oil_render(const char *dir, char **out, size_t *n)
{
	int *ids = malloc((size_t)nE * sizeof *ids), k = 0;
	if (!ids) return -1;
	for (int i = 1; i < nE; i++) if (E[i].dir && !strcmp(E[i].dir, dir)) ids[k++] = i;
	sort_nat = k <= 5000;
	qsort(ids, (size_t)k, sizeof *ids, by_view);
	size_t cap = 256, len = 0;
	char *b = malloc(cap);
	int w = idw();
	if (!b) { free(ids); return -1; }
	for (int j = -1; j < k; j++) {
		const Ent *e = j < 0 ? NULL : &E[ids[j]];
		int type = e ? (e->type == OIL_LINK && e->linkdir ? OIL_DIR : e->type) : OIL_DIR;
		const char *name = e ? e->name : "..";
		/* render_table: the id, the icon padded with a space, the name */
		size_t need = (size_t)w + 32 + strlen(name) + (e && e->link ? strlen(e->link) + 4 : 0);
		if (len + need > cap) {
			while (len + need > cap) cap *= 2;
			char *t = realloc(b, cap);
			if (!t) { free(b); free(ids); return -1; }
			b = t;
		}
		len += (size_t)snprintf(b + len, cap - len, "/%0*d %s  %s%s", w, e ? ids[j] : 0,
		                       oil_icon(type, e && e->type == OIL_LINK && e->link ? e->link : name), name,
		                       !e || e->type == OIL_DIR || (e->type == OIL_LINK && e->linkdir) ? "/" : "");
		if (e && e->type == OIL_LINK && e->link) {
			/* get_link_text: util.addslash, so "sub/" keeps one slash */
			size_t k = strlen(e->link);
			len += (size_t)snprintf(b + len, cap - len, " -> %s%s", e->link, e->linkdir && k && e->link[k - 1] != '/' ? "/" : "");
		}
		b[len++] = '\n';
	}
	free(ids);
	*out = b; *n = len;
	return 0;
}

/* --- parsing (mutator/parser.lua) and actions (mutator/init.lua) --- */

typedef struct { int kind; int buf; const char *dir; char *name; int etype; int id; char *link; } Diff;
enum { D_NEW = 'n', D_DELETE = 'd' };

/* vim.trim into a new string */
static char *trim(const char *s, size_t n)
{
	n = match_trim(&s, n);
	return strndup(s, n);
}

/* parsedir: a trailing '/' makes a directory */
static int parsedir(char *name)
{
	size_t n = strlen(name);
	if (n && name[n - 1] == '/') { name[n - 1] = 0; return 1; }
	return 0;
}

/* compare_link_target: equal but for one trailing separator each */
static size_t trimsep(const char *s)
{
	size_t n = strlen(s);
	return n && (s[n - 1] == '/' || s[n - 1] == '\\') ? n - 1 : n;
}
static int same_target(const char *a, const char *b)
{
	size_t n = trimsep(a);
	return n == trimsep(b) && !memcmp(a, b, n);
}

typedef struct { Diff *d; int n, cap; OilError *err; int nerr, ecap; } Out;

static int push_diff(Out *o, Diff d)
{
	if (o->n == o->cap) {
		int c = o->cap ? o->cap * 2 : 32;
		Diff *t = realloc(o->d, (size_t)c * sizeof *t);
		if (!t) return -1;
		o->d = t; o->cap = c;
	}
	o->d[o->n++] = d;
	return 0;
}

static void push_err(Out *o, const char *msg, int buf, long line)
{
	if (o->nerr == o->ecap) {
		int c = o->ecap ? o->ecap * 2 : 8;
		OilError *t = realloc(o->err, (size_t)c * sizeof *t);
		if (!t) return;
		o->err = t; o->ecap = c;
	}
	o->err[o->nerr++] = (OilError){ msg, buf, line };
}

/* parser.lua's M.parse for one buffer */
static int parse(Out *o, int buf, const char *dir, const char *text, size_t len)
{
	/* original_entries: name -> id of what the directory holds */
	int *orig = calloc((size_t)nE, sizeof *orig);
	char **seen = NULL;
	int nseen = 0, rc = -1;
	if (!orig) return -1;
	for (int i = 1; i < nE; i++) orig[i] = E[i].dir && !strcmp(E[i].dir, dir);
	long ln = 0;
	for (size_t p = 0; p < len; ln++) {
		const char *l = text + p, *z = memchr(l, '\n', len - p);
		size_t n = z ? (size_t)(z - l) : len - p;
		p += n + (z != NULL);
		char *name = NULL, *link = NULL;
		int etype, id = -1, isdir;
		if (n >= 2 && l[0] == '/' && l[1] >= '0' && l[1] <= '9') {
			/* parse_line: ^/(%d+) (.+)$, then the icon column ^(%S+)%s+(.*)$ */
			/* Lua's tonumber of a long id is a float no entry has: saturate */
			const char *d = l + 1;
			long v = match_num(&d, l + n);
			size_t i = (size_t)(d - l);
			if (i + 1 >= n || l[i] != ' ') { push_err(o, "Malformed ID at start of line", buf, ln); continue; }
			i++;
			size_t r = i;
			while (r < n && match_space((unsigned char)l[r])) r++;
			/* parse_col's empty column: "-" and blanks */
			size_t q = r;
			if (q < n && l[q] == '-' && q + 1 < n && match_space((unsigned char)l[q + 1])) {
				q++;
				while (q < n && match_space((unsigned char)l[q])) q++;
			} else {
				while (q < n && !match_space((unsigned char)l[q])) q++;
				if (q == r || q == n) { push_err(o, "Parsing icon failed", buf, ln); continue; }
				while (q < n && match_space((unsigned char)l[q])) q++;
			}
			if (!(name = trim(l + q, n - q))) goto out;
			isdir = parsedir(name);
			int noname = !name[0];       /* "/" alone too: parsedir first */
			etype = isdir ? OIL_DIR : OIL_FILE;
			id = v < nE && v > 0 && E[v].dir ? (int)v : -1;
			if (v == 0) { free(name); continue; }      /* the ../ entry */
			const char *bad = NULL;
			if (id > 0 && E[id].type == OIL_LINK && E[id].link) {
				/* name -> target */
				char *a = strstr(name, " -> ");
				/* parse_line returns without the entry here, so the
				   caller reports it missing */
				if (!a || strstr(a + 4, " -> ")) id = -1;
				else {
					*a = 0;
					char *t = trim(name, strlen(name));
					if (!t || !(link = strdup(a + 4))) { free(t); free(name); goto out; }
					free(name); name = t;
					parsedir(name);
					etype = OIL_LINK;
				}
			}
			if (id > 0 && E[id].type != OIL_DIR && E[id].type != OIL_FILE && etype != OIL_DIR) etype = E[id].type;
			if (noname) bad = "No filename found";
			else if (id < 0) bad = "Could not find existing entry (was the ID changed?)";
			else if (strchr(name, '/')) bad = "Filename cannot contain path separator";
			if (bad) { push_err(o, bad, buf, ln); free(name); free(link); continue; }
		} else {
			/* a new entry */
			if (!(name = trim(l, n))) goto out;
			isdir = parsedir(name);
			etype = isdir ? OIL_DIR : OIL_FILE;
			if (name[0] == '/') { push_err(o, "Paths cannot start with '/'", buf, ln); free(name); continue; }
			if (!name[0]) { free(name); continue; }
			char *a = strstr(name, " -> ");
			if (a && !strstr(a + 4, " -> ")) {
				*a = 0;
				if (!(link = strdup(a + 4))) { free(name); goto out; }
				etype = OIL_LINK;
			}
		}
		/* check_dupe: case matters on Linux */
		int dupe = 0;
		for (int s = 0; s < nseen && !dupe; s++) dupe = !strcmp(seen[s], name);
		if (dupe) push_err(o, "Duplicate filename", buf, ln);
		else {
			char **t = realloc(seen, (size_t)(nseen + 1) * sizeof *t);
			if (!t) { free(name); free(link); goto out; }
			seen = t; seen[nseen++] = name;
		}
		if (id > 0 && orig[id] && !strcmp(E[id].name, name)) {
			if (E[id].type == OIL_LINK && (!link || !E[id].link || !same_target(E[id].link, link))) {
				if (push_diff(o, (Diff){ D_NEW, buf, dir, strdup(name), OIL_LINK, -1, link })) goto out;
			} else if (E[id].type != etype) {
				if (push_diff(o, (Diff){ D_NEW, buf, dir, strdup(name), etype, -1, NULL })) goto out;
				free(link);
			} else { orig[id] = 0; free(link); }
		} else if (push_diff(o, (Diff){ D_NEW, buf, dir, strdup(name), etype, id, link })) goto out;
		if (dupe) free(name);
	}
	for (int i = 1; i < nE; i++)
		if (orig[i] && push_diff(o, (Diff){ D_DELETE, buf, dir, strdup(E[i].name), E[i].type, i, NULL })) goto out;
	rc = 0;
out:
	for (int s = 0; s < nseen; s++) free(seen[s]);
	free(seen);
	free(orig);
	return rc;
}

typedef struct { OilAction *a; int n, cap; } Acts;

static char *join(const char *dir, const char *name)
{
	size_t a = strlen(dir), b = strlen(name);
	char *s = malloc(a + b + 1);
	if (s) { memcpy(s, dir, a); memcpy(s + a, name, b + 1); }
	return s;
}

/* The first {[^}]+} in p[from..k) as Lua finds it: from each '{' the
   bytes up to the next '}', at least one. */
static int brace(const char *p, size_t k, size_t from, size_t *o, size_t *c)
{
	for (size_t i = from; i < k; i++) {
		if (p[i] != '{') continue;
		size_t j = i + 1;
		while (j < k && p[j] != '}') j++;
		if (j < k && j > i + 1) { *o = i; *c = j; return 1; }
	}
	return 0;
}

/* base's first u bytes, '/', then p[0..k) with every {...} group put as
   a[0..an) when a is not NULL (malloc'd, or NULL) */
static char *alt(const char *base, size_t u, const char *p, size_t k, const char *a, size_t an)
{
	size_t n = u + 1 + k, o, c;
	if (a) for (size_t i = 0; brace(p, k, i, &o, &c); i = c + 1) n = n - (c - o + 1) + an;
	char *s = malloc(n + 1), *w = s;
	if (!s) return NULL;
	memcpy(w, base, u); w += u;
	*w++ = '/';
	size_t i = 0;
	if (a) for (; brace(p, k, i, &o, &c); i = c + 1) { memcpy(w, p + i, o - i); w += o - i; memcpy(w, a, an); w += an; }
	memcpy(w, p + i, k - i); w += k - i;
	*w = 0;
	return s;
}

/* *at (if not NULL): the stored path, which outlives the call */
static int add_action(Acts *v, OilAction a, const char **at)
{
	/* seen_creates: one create per path */
	if (a.type == OIL_CREATE)
		for (int i = 0; i < v->n; i++)
			if (v->a[i].type == OIL_CREATE && !strcmp(v->a[i].src, a.src)) {
				if (at) *at = v->a[i].src;
				free(a.src); free(a.link);
				return 0;
			}
	if (at) *at = a.src;
	if (v->n == v->cap) {
		int c = v->cap ? v->cap * 2 : 16;
		OilAction *t = realloc(v->a, (size_t)c * sizeof *t);
		if (!t) return -1;
		v->a = t; v->cap = c;
	}
	v->a[v->n++] = a;
	return 0;
}

void oil_free(OilAction *a, int n)
{
	for (int i = 0; i < n; i++) { free(a[i].src); free(a[i].dst); free(a[i].link); }
	free(a);
}

/* --- enforce_action_order: each action waits for the ones it depends on.
   The tries are kept as the action list; a key is a path. --- */

static const char *key_src(const OilAction *a) { return a->type == OIL_MOVE || a->type == OIL_COPY ? a->src : a->type == OIL_CREATE ? NULL : a->src; }
static const char *key_dst(const OilAction *a) { return a->type == OIL_CREATE ? a->src : a->type == OIL_MOVE || a->type == OIL_COPY ? a->dst : NULL; }

/* Is c one level below p (trie.lua's accum_children_of only reaches the
   direct children: its _dfs walks children with ipairs, which skips their
   string keys)? */
static int child(const char *c, const char *p)
{
	size_t n = strlen(p);
	if (strncmp(c, p, n) || c[n] != '/') return 0;
	return !strchr(c + n + 1, '/');          /* "a/" is a's child "": vim.split keeps it */
}

enum { F_ANY, F_MOVEDEL, F_COPY };
static int pass(const OilAction *a, int f)
{
	return f == F_ANY || (f == F_MOVEDEL && (a->type == OIL_MOVE || a->type == OIL_DELETE)) || (f == F_COPY && a->type == OIL_COPY);
}

typedef struct { int *d; int n; } Deps;
static void dep(Deps *d, int i) { d->d[d->n++] = i; }

/* trie:accum_actions_at, trie:accum_children_of, trie:accum_first_parents_of */
/* the actions at url's first n bytes */
static void at(const Acts *v, const char *alive, int dst, const char *url, size_t n, Deps *d, int f)
{
	for (int i = 0; i < v->n; i++) {
		const char *k = dst ? key_dst(&v->a[i]) : key_src(&v->a[i]);
		if (alive[i] && k && !strncmp(k, url, n) && !k[n] && pass(&v->a[i], f)) dep(d, i);
	}
}
/* in the order of the children's names (oil walks them with pairs, in
   hash order; the conformance oracle fixes that to name order), each
   child's actions in list order */
static void children(const Acts *v, const char *alive, int dst, const char *url, Deps *d)
{
	int from = d->n;
	for (int i = 0; i < v->n; i++) {
		const char *k = dst ? key_dst(&v->a[i]) : key_src(&v->a[i]);
		if (!alive[i] || !k || !child(k, url)) continue;
		int j = d->n++;
		for (; j > from && strcmp(dst ? key_dst(&v->a[d->d[j - 1]]) : key_src(&v->a[d->d[j - 1]]), k) > 0; j--) d->d[j] = d->d[j - 1];
		d->d[j] = i;
	}
}
static void first_parents(const Acts *v, const char *alive, const char *url, Deps *d)
{
	for (size_t n = strlen(url); n-- > 1;) {
		if (url[n] != '/') continue;
		int before = d->n;
		at(v, alive, 1, url, n, d, F_ANY);
		if (d->n > before) return;
	}
}

/* get_deps; d->d has room for every action four times over */
static void deps(const Acts *v, const char *alive, int i, Deps *d)
{
	const OilAction *a = &v->a[i];
	d->n = 0;
	switch (a->type) {
	case OIL_DELETE: children(v, alive, 0, a->src, d); break;
	case OIL_CREATE:
		first_parents(v, alive, a->src, d);
		at(v, alive, 0, a->src, strlen(a->src), d, F_MOVEDEL);
		break;
	case OIL_MOVE:
		first_parents(v, alive, a->dst, d);
		children(v, alive, 1, a->src, d);
		children(v, alive, 0, a->src, d);
		at(v, alive, 0, a->dst, strlen(a->dst), d, F_MOVEDEL);
		break;
	case OIL_COPY:
		first_parents(v, alive, a->dst, d);
		children(v, alive, 1, a->src, d);
		at(v, alive, 0, a->dst, strlen(a->dst), d, F_MOVEDEL);
		break;
	}
}

/* find_leaf: follow dependencies to an action with none; in a loop, the
   first action met twice goes to *loop.  -2: no memory. */
static int leaf(const Acts *v, const char *alive, int i, char *seen, int *loop)
{
	if (seen[i]) { if (*loop < 0) *loop = i; return -1; }
	seen[i] = 1;
	Deps d = { malloc((size_t)v->n * 4 * sizeof(int)), 0 };
	if (!d.d) return -2;
	deps(v, alive, i, &d);
	int r = d.n ? -1 : i;
	for (int k = 0; k < d.n && r == -1; k++) r = leaf(v, alive, d.d[k], seen, loop);
	free(d.d);
	return r;
}

static int is_under(const char *dst, const char *src)
{
	size_t n = strlen(src);
	return !strncmp(dst, src, n) && dst[n] == '/';
}

/* enforce_action_order: v's actions into *out; -1 with *msg on a cycle
   (or no memory). */
static int order(Acts *v, Acts *out, const char **msg)
{
	Acts after = { 0 };
	char *alive = NULL, *seen = NULL;
	int rc = -1;
	for (;;) {
		char *a = realloc(alive, (size_t)v->n + 1), *s = realloc(seen, (size_t)v->n + 1);
		if (a) alive = a;
		if (s) seen = s;
		if (!a || !s) goto out;
		break;
	}
	memset(alive, 1, (size_t)v->n);
	for (int left = v->n; left;) {
		int first = 0, loop = -1;
		while (!alive[first]) first++;
		memset(seen, 0, (size_t)v->n);
		int sel = leaf(v, alive, first, seen, &loop);
		if (sel == -2) goto out;
		if (sel >= 0) {
			OilAction *a = &v->a[sel];
			if ((a->type == OIL_MOVE || a->type == OIL_COPY) && is_under(a->dst, a->src)) {
				static char m[8256];
				snprintf(m, sizeof m, "Cannot move or copy parent into itself: oil://%s -> oil://%s", a->src, a->dst);
				*msg = m;
				goto out;
			}
			if (add_action(out, *a, NULL)) goto out;
			*a = (OilAction){ 0 };
			alive[sel] = 0;
			left--;
			continue;
		}
		if (loop < 0 || v->a[loop].type != OIL_MOVE || !strncmp(v->a[loop].dst, v->a[loop].src, strlen(v->a[loop].src))) {
			*msg = "Detected cycle in desired paths";
			goto out;
		}
		/* a move cycle (a -> b, b -> a): one move goes through a temporary name */
		OilAction *a = &v->a[loop];
		size_t tn = strlen(a->src) + 20;
		char *tmp = malloc(tn);
		if (!tmp) goto out;
		snprintf(tmp, tn, "%s__oil_tmp_%05d", a->src, rand() % 999999);
		OilAction m1 = { OIL_MOVE, a->etype, a->src, tmp, NULL };
		OilAction m2 = { OIL_MOVE, a->etype, strdup(tmp), a->dst, NULL };
		*a = (OilAction){ 0 };
		alive[loop] = 0;
		if (add_action(&after, m2, NULL) || add_action(v, m1, NULL)) goto out;
		char *na = realloc(alive, (size_t)v->n + 1), *ns = realloc(seen, (size_t)v->n + 1);
		if (na) alive = na;
		if (ns) seen = ns;
		if (!na || !ns) goto out;
		alive[v->n - 1] = 1;
	}
	for (int i = 0; i < after.n; i++) {
		if (add_action(out, after.a[i], NULL)) goto out;
		after.a[i] = (OilAction){ 0 };
	}
	rc = 0;
out:
	oil_free(after.a, after.n);
	free(alive); free(seen);
	return rc;
}

int oil_actions(int nbuf, const char *const *dirs, const char *const *text, const size_t *len,
                OilAction **res, OilError **err, int *nerr)
{
	Out o = { 0 };
	Acts v = { 0 }, out = { 0 };
	int rc = -1;
	*res = NULL; *err = NULL; *nerr = 0;
	for (int b = 0; b < nbuf; b++) if (parse(&o, b, dirs[b], text[b], len[b])) goto done;
	if (o.nerr) { *err = o.err; *nerr = o.nerr; o.err = NULL; goto done; }
	/* the creates, in buffer and line order */
	for (int i = 0; i < o.n; i++) {
		Diff *d = &o.d[i];
		if (d->kind != D_NEW || d->id > 0) continue;
		/* nested names: foo/bar/baz makes the directories on the way */
		const char *base = d->dir;               /* the url so far */
		size_t u = strlen(d->dir) - 1;           /* without the trailing '/' */
		for (char *p = d->name, *z; p; p = z ? z + 1 : NULL) {
			z = strchr(p, '/');
			size_t k = z ? (size_t)(z - p) : strlen(p), o, c;
			if (!z && brace(p, k, 0, &o, &c)) {
				/* foo.{js,test.js}: one create per alternative, put in
				   every group (v:gsub("{[^}]+}", alt)) */
				for (const char *a = p + o + 1, *e;; a = e + 1) {
					e = memchr(a, ',', (size_t)(p + c - a));
					if (!e) e = p + c;
					char *url = alt(base, u, p, k, a, (size_t)(e - a));
					if (!url || add_action(&v, (OilAction){ OIL_CREATE, d->etype, url, NULL, d->link ? strdup(d->link) : NULL }, NULL)) { free(url); goto done; }
					if (e == p + c) break;
				}
				break;
			}
			char *url = alt(base, u, p, k, NULL, 0);
			if (!url || add_action(&v, (OilAction){ OIL_CREATE, z ? OIL_DIR : d->etype, url, NULL, !z && d->link ? strdup(d->link) : NULL }, &base)) { free(url); goto done; }
			u += 1 + k;
		}
	}
	/* then by id: moves (and copies) where the entry also went, deletes,
	   copies where it stayed */
	for (int id = 1; id < nE; id++) {
		int creates = 0, del = 0;
		for (int i = 0; i < o.n; i++) if (o.d[i].id == id) { if (o.d[i].kind == D_DELETE) del = 1; else creates++; }
		if (!creates && !del) continue;
		char *src = join(E[id].dir, E[id].name);
		if (!src) goto done;
		if (del && !creates) {
			if (add_action(&v, (OilAction){ OIL_DELETE, E[id].type, src, NULL, NULL }, NULL)) goto done;
			continue;
		}
		int k = 0;
		for (int i = 0; i < o.n; i++) {
			if (o.d[i].id != id || o.d[i].kind != D_NEW) continue;
			k++;
			int type = del && k == creates ? OIL_MOVE : OIL_COPY;
			if (add_action(&v, (OilAction){ type, E[id].type, strdup(src), join(o.d[i].dir, o.d[i].name), NULL }, NULL)) goto done;
		}
		free(src);
	}
	const char *msg = NULL;
	if (order(&v, &out, &msg)) {
		if (msg) { push_err(&o, msg, -1, -1); *err = o.err; *nerr = o.nerr; o.err = NULL; }
		goto done;
	}
	*res = out.a;
	rc = out.n;
	out.a = NULL; out.n = 0;
done:
	for (int i = 0; i < o.n; i++) { free(o.d[i].name); free(o.d[i].link); }
	free(o.d);
	free(o.err);
	oil_free(v.a, v.n);
	oil_free(out.a, out.n);
	return rc;
}

/* --- running the actions: adapters/files.lua's perform_action and fs.lua,
   one after another, stopping at the first error (process_actions); each
   one done is also made in the cache (cache.perform_action) --- */

/* libuv's name and message for an error (uv_err_name, uv_strerror), as
   oil shows them */
const char *oil_errname(int e)
{
	static const struct { int e; const char *s; } t[] = {
		{ EACCES, "EACCES: permission denied" }, { EBUSY, "EBUSY: resource busy or locked" },
		{ EEXIST, "EEXIST: file already exists" }, { EINVAL, "EINVAL: invalid argument" },
		{ EIO, "EIO: i/o error" }, { EISDIR, "EISDIR: illegal operation on a directory" },
		{ ELOOP, "ELOOP: too many symbolic links encountered" }, { EMLINK, "EMLINK: too many links" },
		{ ENAMETOOLONG, "ENAMETOOLONG: name too long" }, { ENOENT, "ENOENT: no such file or directory" },
		{ ENOSPC, "ENOSPC: no space left on device" }, { ENOTDIR, "ENOTDIR: not a directory" },
		{ ENOTEMPTY, "ENOTEMPTY: directory not empty" }, { EPERM, "EPERM: operation not permitted" },
		{ EROFS, "EROFS: read-only file system" }, { EXDEV, "EXDEV: cross-device link not permitted" },
	};
	for (size_t i = 0; i < sizeof t / sizeof *t; i++) if (t[i].e == e) return t[i].s;
	return strerror(e);
}

/* the failed call's error as luv words it: "NAME: message: path", or
   "...: path -> new path" for a call with two */
static char msg[8192];
static int fail(const char *p, const char *q)
{
	snprintf(msg, sizeof msg, "%s: %s%s%s", oil_errname(errno), p, q ? " -> " : "", q ? q : "");
	return -1;
}

static int rm_r(int type, const char *p)
{
	if (type != OIL_DIR) return unlink(p) ? fail(p, NULL) : 0;
	DIR *d = opendir(p);
	if (!d) return fail(p, NULL);
	int rc = 0;
	char q[4096];
	for (struct dirent *de; !rc && (de = readdir(d));) {
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
		struct stat st;
		snprintf(q, sizeof q, "%s/%s", p, de->d_name);
		rc = lstat(q, &st) ? fail(q, NULL) : rm_r(S_ISDIR(st.st_mode) ? OIL_DIR : OIL_FILE, q);
	}
	closedir(d);
	return rc ? rc : rmdir(p) ? fail(p, NULL) : 0;
}

/* uv_fs_copyfile with excl: a new file with the source's bytes and mode */
static int copyfile(const char *src, const char *dst)
{
	struct stat st;
	int in = open(src, O_RDONLY), out = -1, rc = -1;
	if (in < 0 || fstat(in, &st) || (out = open(dst, O_WRONLY | O_CREAT | O_EXCL, st.st_mode & 07777)) < 0) goto out;
	char b[65536];
	ssize_t n;
	while ((n = read(in, b, sizeof b)) > 0)
		for (ssize_t w = 0, k; w < n; w += k)
			if ((k = write(out, b + w, (size_t)(n - w))) < 0) goto out;
	rc = n < 0 || fchmod(out, st.st_mode & 07777) ? -1 : 0;
out:
	if (rc) fail(src, dst);
	if (in >= 0) close(in);
	if (out >= 0 && close(out) && !rc) rc = fail(src, dst);
	/* uv__fs_copyfile removes what it made when the copy fails (a
	   directory's read fails only after the new file is there) */
	if (rc && out >= 0) unlink(dst);
	return rc;
}

static int cp_r(int type, const char *src, const char *dst)
{
	char q[4096], r[4096];
	if (type == OIL_LINK) {
		ssize_t k = readlink(src, q, sizeof q - 1);
		if (k < 0) return fail(src, NULL);
		q[k] = 0;
		return symlink(q, dst) ? fail(q, dst) : 0;
	}
	if (type != OIL_DIR) return copyfile(src, dst);
	struct stat st;
	if (stat(src, &st)) return fail(src, NULL);
	if (mkdir(dst, st.st_mode & 07777)) return fail(dst, NULL);
	DIR *d = opendir(src);
	if (!d) return fail(src, NULL);
	int rc = 0;
	for (struct dirent *de; !rc && (de = readdir(d));) {
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
		snprintf(q, sizeof q, "%s/%s", src, de->d_name);
		snprintf(r, sizeof r, "%s/%s", dst, de->d_name);
		rc = lstat(q, &st) ? fail(q, NULL) : cp_r(S_ISDIR(st.st_mode) ? OIL_DIR : S_ISLNK(st.st_mode) ? OIL_LINK : OIL_FILE, q, r);
	}
	closedir(d);
	return rc;
}

static int run(const OilAction *a)
{
	switch (a->type) {
	case OIL_CREATE:
		if (a->etype == OIL_DIR) return mkdir(a->src, 0755) && errno != EEXIST ? fail(a->src, NULL) : 0;
		if (a->etype == OIL_LINK && a->link) return symlink(a->link, a->src) ? fail(a->link, a->src) : 0;
		int fd = open(a->src, O_WRONLY | O_CREAT | O_APPEND, 0644);
		return fd < 0 || close(fd) ? fail(a->src, NULL) : 0;
	case OIL_DELETE: return rm_r(a->etype, a->src);
	/* recursive_move: a rename, or else (another device, but any error
	   does it) a copy and a delete */
	case OIL_MOVE: return rename(a->src, a->dst) && (cp_r(a->etype, a->src, a->dst) || rm_r(a->etype, a->src)) ? -1 : 0;
	default: return cp_r(a->etype, a->src, a->dst);
	}
}

/* "/a/b/c" as "/a/b/" and "c" in p */
static const char *split(const char *path, char *p, size_t cap)
{
	const char *s = strrchr(path, '/');
	snprintf(p, cap, "%.*s", (int)(s - path + 1), path);
	return s + 1;
}

static int cache_do(const OilAction *a)
{
	char d[4096], s[4096];
	const char *n = split(a->type == OIL_COPY ? a->dst : a->src, d, sizeof d);
	int i = find(d, n, strlen(n));
	if (a->type == OIL_CREATE || a->type == OIL_COPY) return i > 0 || oil_entry(d, n, a->etype, NULL, 0) > 0 ? 0 : -1;
	if (i < 0) return 0;
	if (a->type == OIL_DELETE) { free(E[i].dir); E[i].dir = NULL; return 0; }
	/* a move: the entry goes, metadata cleared */
	n = split(a->dst, s, sizeof s);
	char *dir = strdup(s), *name = strdup(n);
	if (!dir || !name) { free(dir); free(name); return -1; }
	free(E[i].dir); free(E[i].name); free(E[i].link);
	E[i].dir = dir; E[i].name = name; E[i].link = NULL; E[i].linkdir = 0;
	return 0;
}

const char *oil_apply(const OilAction *a, int n, int *done)
{
	for (*done = 0; *done < n; ++*done) {
		if (run(&a[*done])) return msg;
		if (cache_do(&a[*done])) return "out of memory";
	}
	return NULL;
}

/* util.update_moved_buffers: a buffer's path after move a, in place; 1 if
   it moved.  oil compares a directory's path as a bare prefix, so moving
   "a" would rename the buffer of "ab/" too; this takes "a/" only. */
int oil_follow(const OilAction *a, char *path, size_t cap)
{
	size_t n = strlen(a->src);
	if (a->type != OIL_MOVE || strncmp(path, a->src, n) || (path[n] && (a->etype != OIL_DIR || path[n] != '/'))) return 0;
	char t[4096];
	snprintf(t, sizeof t, "%s%s", a->dst, path + n);
	snprintf(path, cap, "%s", t);
	return 1;
}
#endif
