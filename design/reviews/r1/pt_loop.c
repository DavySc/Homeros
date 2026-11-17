#include "pt.h"
#include <stdlib.h>
#include <string.h>

static int grow(void **a, size_t *cap, size_t need, size_t sz) {
  if (need <= *cap) return 0;
  size_t c = *cap ? *cap : 16;
  while (c < need) { if (c > ((size_t)-1) / 2 / sz) return -1; c *= 2; }
  void *q = realloc(*a, c * sz); if (!q) return -1;
  *a = q; *cap = c; return 0;
}
static const char *src(const PT *t, const Piece *p) { return p->buf ? t->add : t->orig; }

int pt_init(PT *t, const char *orig, size_t n) {
  memset(t, 0, sizeof *t); t->orig = orig; t->orig_len = n;
  if (n) { if (grow((void **)&t->p, &t->cap, 1, sizeof(Piece))) return -1;
           t->p[0] = (Piece){0, 0, n}; t->n = 1; }
  t->len = n; return 0;
}
void pt_free(PT *t) { free(t->add); free(t->p); memset(t, 0, sizeof *t); }

/* Split so that a piece boundary sits at pos; returns index of first piece at/after pos. */
static long split(PT *t, size_t pos) {
  size_t off = 0, i = 0;
  for (; i < t->n; i++) {
    if (pos == off) return (long)i;
    if (pos < off + t->p[i].len) break;
    off += t->p[i].len;
  }
  if (i == t->n) return pos == off ? (long)i : -1;
  if (grow((void **)&t->p, &t->cap, t->n + 1, sizeof(Piece))) return -1;
  for (size_t j = t->n; j > i; j--) t->p[j] = t->p[j - 1];
  size_t k = pos - off;
  t->p[i].len = k;
  t->p[i + 1].start += k; t->p[i + 1].len -= k;
  t->n++; return (long)i + 1;
}

int pt_insert(PT *t, size_t pos, const char *s, size_t n) {
  if (pos > t->len) return -1;
  if (!n) return 0;
  if (grow((void **)&t->add, &t->add_cap, t->add_len + n, 1)) return -1;
  /* Fast path: typing at the end of the last-added piece extends it. */
  long i = split(t, pos); if (i < 0) return -1;
  if (i > 0 && t->p[i - 1].buf == 1 && t->p[i - 1].start + t->p[i - 1].len == t->add_len) {
    t->p[i - 1].len += n;
  } else {
    if (grow((void **)&t->p, &t->cap, t->n + 1, sizeof(Piece))) return -1;
    for (size_t j = t->n; j > (size_t)i; j--) t->p[j] = t->p[j - 1];
    t->p[i] = (Piece){1, t->add_len, n}; t->n++;
  }
  memcpy(t->add + t->add_len, s, n); t->add_len += n; t->len += n;
  return 0;
}

int pt_delete(PT *t, size_t pos, size_t n, char *out) {
  if (pos > t->len || n > t->len - pos) return -1;
  if (!n) return 0;
  if (out) pt_read(t, pos, out, n);
  long a = split(t, pos);     if (a < 0) return -1;
  long b = split(t, pos + n); if (b < 0) return -1;
  for (size_t j = 0; j < t->n - (size_t)b; j++) t->p[a + j] = t->p[b + j];
  t->n -= (size_t)(b - a); t->len -= n; return 0;
}

size_t pt_read(const PT *t, size_t pos, char *out, size_t n) {
  size_t off = 0, w = 0;
  for (size_t i = 0; i < t->n && w < n; i++) {
    const Piece *p = &t->p[i];
    if (pos < off + p->len) {
      size_t k = pos > off ? pos - off : 0, m = p->len - k;
      if (m > n - w) m = n - w;
      memcpy(out + w, src(t, p) + p->start + k, m); w += m; pos += m;
    }
    off += p->len;
  }
  return w;
}
