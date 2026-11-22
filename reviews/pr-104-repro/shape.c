/* What the renamed pass calls the same function (copied to src/tools/zz.c). */
#include <stdbool.h>
#include <stddef.h>
typedef struct { int n; bool on; char *s; } A;
typedef struct { int n; bool on; char *s; } B;
extern int ga, gb; void note(void *, bool); size_t strlen(const char *);
/* 1: differ only in true/false: flagged, though they do opposite things */
void a_on(A *a)  { note(a, true);  a->on = true;  if (a->n) a->n = 0; }
void a_off(A *a) { note(a, false); a->on = false; if (a->n) a->n = 0; }
/* 2: differ only in the global they touch: flagged */
int bump_a(int by) { ga += by; if (ga > 100) ga = 100; return ga * 2; }
int bump_b(int by) { gb += by; if (gb > 100) gb = 100; return gb * 2; }
/* 3: the same but for one statement moved: not flagged */
size_t len1(A *a) { size_t n = 0; if (!a->s) return 0; n = strlen(a->s); return n + 1; }
size_t len2(B *b) { if (!b->s) return 0; size_t n = 0; n = strlen(b->s); return n + 1; }
/* 4: a copy with a one-letter upper-case local renamed: not flagged */
size_t up1(A *P) { size_t n = 0; if (!P->s) return 9; n = strlen(P->s); return n + 3; }
size_t up2(A *Q) { size_t n = 0; if (!Q->s) return 9; n = strlen(Q->s); return n + 3; }
/* 5: a real renamed copy: flagged */
size_t cp1(A *a) { size_t n = 0; if (!a->s) return 7; n = strlen(a->s); return n + 5; }
size_t cp2(B *b) { size_t k = 0; if (!b->s) return 7; k = strlen(b->s); return k + 5; }
