/* oil.nvim's model, from its Lua (the comments name the file): the cache
   of entries by id, a directory's lines, and the lines back to actions. */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
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

/* create_and_store_entry: a name already in the directory keeps its id */
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
	E[i].linkdir = linkdir;
	return i;
}

int oil_list(const char *dir)
{
	DIR *d = opendir(dir);
	if (!d) return -1;
	/* begin_update_url */
	for (int i = 1; i < nE; i++) if (E[i].dir && !strcmp(E[i].dir, dir)) E[i].old = 1;
	char p[4096], t[4096];
	for (struct dirent *de; (de = readdir(d));) {
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
		if (oil_entry(dir, de->d_name, type, link, type == OIL_LINK && !stat(p, &st) && S_ISDIR(st.st_mode)) < 0) {
			closedir(d);
			return -1;
		}
	}
	closedir(d);
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
	for (int j = -1; b && j < k; j++) {
		const Ent *e = j < 0 ? NULL : &E[ids[j]];
		int type = e ? (e->type == OIL_LINK && e->linkdir ? OIL_DIR : e->type) : OIL_DIR;
		const char *name = e ? e->name : "..";
		/* render_table: the id, the icon padded with a space, the name */
		size_t need = (size_t)w + 32 + strlen(name) + (e && e->link ? strlen(e->link) + 4 : 0);
		if (len + need > cap) {
			while (len + need > cap) cap *= 2;
			char *t = realloc(b, cap);
			if (!t) { free(b); b = NULL; break; }
			b = t;
		}
		len += (size_t)sprintf(b + len, "/%0*d %s  %s%s", w, e ? ids[j] : 0,
		                       oil_icon(type, e && e->type == OIL_LINK ? e->link : name), name,
		                       !e || e->type == OIL_DIR || (e->type == OIL_LINK && e->linkdir) ? "/" : "");
		if (e && e->type == OIL_LINK) len += (size_t)sprintf(b + len, " -> %s%s", e->link, e->linkdir ? "/" : "");
		b[len++] = '\n';
	}
	free(ids);
	if (!b) return -1;
	*out = b; *n = len;
	return 0;
}

/* --- parsing (mutator/parser.lua) and actions (mutator/init.lua) --- */

typedef struct { int kind; int buf; const char *dir; char *name; int etype; int id; char *link; } Diff;
enum { D_NEW = 'n', D_DELETE = 'd' };

static int lspace(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }

/* vim.trim into a new string */
static char *trim(const char *s, size_t n)
{
	while (n && lspace((unsigned char)*s)) s++, n--;
	while (n && lspace((unsigned char)s[n - 1])) n--;
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
			size_t i = 1;
			long v = 0;
			while (i < n && l[i] >= '0' && l[i] <= '9') v = v * 10 + (l[i++] - '0');
			if (i + 1 >= n || l[i] != ' ') { push_err(o, "Malformed ID at start of line", buf, ln); continue; }
			i++;
			size_t r = i;
			while (r < n && lspace((unsigned char)l[r])) r++;
			/* parse_col's empty column: "-" and blanks */
			size_t q = r;
			if (q < n && l[q] == '-' && q + 1 < n && lspace((unsigned char)l[q + 1])) {
				q++;
				while (q < n && lspace((unsigned char)l[q])) q++;
			} else {
				while (q < n && !lspace((unsigned char)l[q])) q++;
				if (q == r || q == n) { push_err(o, "Parsing icon failed", buf, ln); continue; }
				while (q < n && lspace((unsigned char)l[q])) q++;
			}
			if (!(name = trim(l + q, n - q))) goto out;
			int noname = !name[0];
			isdir = parsedir(name);
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

static int add_action(Acts *v, OilAction a)
{
	/* seen_creates: one create per path */
	if (a.type == OIL_CREATE)
		for (int i = 0; i < v->n; i++)
			if (v->a[i].type == OIL_CREATE && !strcmp(v->a[i].src, a.src)) { free(a.src); free(a.link); return 0; }
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
	return c[n + 1] && !strchr(c + n + 1, '/');
}

enum { F_ANY, F_MOVEDEL, F_COPY };
static int pass(const OilAction *a, int f)
{
	return f == F_ANY || (f == F_MOVEDEL && (a->type == OIL_MOVE || a->type == OIL_DELETE)) || (f == F_COPY && a->type == OIL_COPY);
}

typedef struct { int *d; int n; } Deps;
static void dep(Deps *d, int i) { d->d[d->n++] = i; }

/* trie:accum_actions_at, trie:accum_children_of, trie:accum_first_parents_of */
static void at(const Acts *v, const char *alive, int dst, const char *url, Deps *d, int f)
{
	for (int i = 0; i < v->n; i++) {
		const char *k = dst ? key_dst(&v->a[i]) : key_src(&v->a[i]);
		if (alive[i] && k && !strcmp(k, url) && pass(&v->a[i], f)) dep(d, i);
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
	char p[4096];
	snprintf(p, sizeof p, "%s", url);
	for (char *s = strrchr(p, '/'); s && s > p; s = strrchr(p, '/')) {
		*s = 0;
		int before = d->n;
		at(v, alive, 1, p, d, F_ANY);
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
		at(v, alive, 0, a->src, d, F_MOVEDEL);
		break;
	case OIL_MOVE:
		first_parents(v, alive, a->dst, d);
		children(v, alive, 1, a->src, d);
		children(v, alive, 0, a->src, d);
		at(v, alive, 0, a->dst, d, F_MOVEDEL);
		break;
	case OIL_COPY:
		first_parents(v, alive, a->dst, d);
		children(v, alive, 1, a->src, d);
		at(v, alive, 0, a->dst, d, F_MOVEDEL);
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
				*msg = "Cannot move or copy parent into itself";
				goto out;
			}
			if (add_action(out, *a)) goto out;
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
		char tmp[4096];
		snprintf(tmp, sizeof tmp, "%s__oil_tmp_%05d", a->src, rand() % 999999);
		OilAction m1 = { OIL_MOVE, a->etype, a->src, strdup(tmp), NULL };
		OilAction m2 = { OIL_MOVE, a->etype, strdup(tmp), a->dst, NULL };
		*a = (OilAction){ 0 };
		alive[loop] = 0;
		if (add_action(&after, m2) || add_action(v, m1)) goto out;
		char *na = realloc(alive, (size_t)v->n + 1), *ns = realloc(seen, (size_t)v->n + 1);
		if (na) alive = na;
		if (ns) seen = ns;
		if (!na || !ns) goto out;
		alive[v->n - 1] = 1;
	}
	for (int i = 0; i < after.n; i++) {
		if (add_action(out, after.a[i])) goto out;
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
		char url[4096];
		size_t u = strlen(d->dir) - 1;           /* without the trailing '/' */
		memcpy(url, d->dir, u);
		for (char *s = d->name, *z; s; s = z ? z + 1 : NULL) {
			z = strchr(s, '/');
			size_t k = z ? (size_t)(z - s) : strlen(s);
			int last = !z;
			char *ob = last ? memchr(s, '{', k) : NULL, *cb = ob ? memchr(ob, '}', k - (size_t)(ob - s)) : NULL;
			if (ob && cb && cb > ob + 1) {
				/* foo.{js,test.js}: one create per alternative */
				for (char *a = ob + 1, *e; a < cb; a = e + 1) {
					e = memchr(a, ',', (size_t)(cb - a));
					if (!e) e = cb;
					int m = snprintf(url + u, sizeof url - u, "/%.*s%.*s%.*s", (int)(ob - s), s, (int)(e - a), a, (int)(s + k - cb - 1), cb + 1);
					(void)m;
					if (add_action(&v, (OilAction){ OIL_CREATE, d->etype, strdup(url), NULL, d->link ? strdup(d->link) : NULL })) goto done;
				}
				break;
			}
			u += (size_t)snprintf(url + u, sizeof url - u, "/%.*s", (int)k, s);
			if (add_action(&v, (OilAction){ OIL_CREATE, last ? d->etype : OIL_DIR, strdup(url), NULL, last && d->link ? strdup(d->link) : NULL })) goto done;
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
			if (add_action(&v, (OilAction){ OIL_DELETE, E[id].type, src, NULL, NULL })) goto done;
			continue;
		}
		int k = 0;
		for (int i = 0; i < o.n; i++) {
			if (o.d[i].id != id || o.d[i].kind != D_NEW) continue;
			k++;
			int type = del && k == creates ? OIL_MOVE : OIL_COPY;
			if (add_action(&v, (OilAction){ type, E[id].type, strdup(src), join(o.d[i].dir, o.d[i].name), NULL })) goto done;
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
