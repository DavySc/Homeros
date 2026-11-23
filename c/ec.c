#define _GNU_SOURCE
#include "ec.h"
#include <elf.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static bool rd(int fd, void *b, size_t n) { size_t k = 0; while (k < n) { ssize_t r = read(fd, (char *)b + k, n - k); if (r <= 0) return false; k += (size_t)r; } return true; }
static bool wr(int fd, const void *b, size_t n) { return write(fd, b, n) == (ssize_t)n; }

bool zc_start(Compiler *c, const char *zig, const char *src) {
    const char *base = strrchr(src, '/');
    snprintf(c->name, sizeof c->name, "%s", base ? base + 1 : src);
    char *dot = strrchr(c->name, '.');
    if (dot) *dot = 0;
    int in[2], out[2];
    if (pipe(in) || pipe(out)) return false;
    c->pid = fork();
    if (!c->pid) {
        dup2(in[0], 0); dup2(out[1], 1);
        close(in[1]); close(out[0]);
        execlp(zig, zig, "build-exe", src, "-fincremental", "--listen=-", (char *)NULL);
        _exit(127);
    }
    close(in[0]); close(out[1]);
    c->to = in[1]; c->from = out[0];
    char msg[256];
    return zc_update(c, msg, sizeof msg) == 0;
}

/* Messages are {u32 tag, u32 length, body}; tag 1 (error_bundle) ends an update, tag 2
   (emit_digest) names the output by the hash of its cache directory. */
int zc_update(Compiler *c, char *msg, size_t room) {
    uint32_t hdr[2] = { 1, 0 };                       /* Client.Message.update */
    if (!wr(c->to, hdr, 8)) return 2;
    for (;;) {
        if (!rd(c->from, hdr, 8)) return 2;
        uint8_t *body = malloc(hdr[1] + 1);
        if (!rd(c->from, body, hdr[1])) { free(body); return 2; }
        if (hdr[0] == 2 && hdr[1] >= 17) {
            char hex[33];
            for (int i = 0; i < 16; i++) snprintf(hex + 2 * i, 3, "%02x", body[1 + i]);
            const char *home = getenv("HOME");
            snprintf(c->out, sizeof c->out, "%.200s/.cache/zig/o/%s/%.200s", home ? home : "", hex, c->name);
        }
        if (hdr[0] == 1) {
            uint32_t extra = hdr[1] >= 8 ? *(uint32_t *)body : 0, strings = hdr[1] >= 8 ? ((uint32_t *)body)[1] : 0;
            msg[0] = 0;
            if (extra) {                              /* the first long string in the bundle: the first message */
                const char *s = (const char *)body + 8 + 4 * extra, *end = s + strings;
                for (; s < end; s += strlen(s) + 1) if (strlen(s) > 8) { snprintf(msg, room, "%s", s); break; }
            }
            free(body);
            return extra ? 1 : 0;
        }
        free(body);
    }
}

void zc_stop(Compiler *c) {
    uint32_t hdr[2] = { 0, 0 };                       /* Client.Message.exit */
    wr(c->to, hdr, 8);
    close(c->to); close(c->from);
    waitpid(c->pid, NULL, 0);
}

/* ---- the patch ---- */
static const Elf64_Phdr *phdrs(const Dwarf *d, int *n) {
    const Elf64_Ehdr *e = (const Elf64_Ehdr *)d->map;
    *n = e->e_phnum;
    return (const Elf64_Phdr *)(d->map + e->e_phoff);
}

/* the global variable (STT_OBJECT) of the old binary covering addr, if any */
static bool old_object_at(const Dwarf *d, uint64_t addr) {
    const Elf64_Ehdr *e = (const Elf64_Ehdr *)d->map;
    const Elf64_Shdr *sh = (const Elf64_Shdr *)(d->map + e->e_shoff);
    for (int i = 0; i < e->e_shnum; i++) {
        if (sh[i].sh_type != SHT_SYMTAB) continue;
        const Elf64_Sym *s = (const Elf64_Sym *)(d->map + sh[i].sh_offset);
        for (size_t k = 0; k < sh[i].sh_size / sizeof *s; k++)
            if (ELF64_ST_TYPE(s[k].st_info) == STT_OBJECT && addr >= s[k].st_value && addr < s[k].st_value + (s[k].st_size ? s[k].st_size : 1))
                return true;
    }
    return false;
}

/* A run inside an active function is harmless when it only retargets a call (E8 rel32): the
   instructions keep their places, so frames and return addresses stay right. */
static bool call_operand(const uint8_t *a, const uint8_t *b, uint64_t off, uint64_t n, uint64_t fstart) {
    for (uint64_t x = off >= 4 ? off - 4 : 0; x < off && x >= fstart; x++)
        if (a[x] == 0xe8 && b[x] == 0xe8 && off + n <= x + 5) return true;
    return false;
}

typedef struct { uint64_t va, n; const uint8_t *bytes; } Run;

/* old line -> new line: lines before and after the edited block keep their text; inside a block
   of the same length a line keeps its offset; otherwise there is no answer (0). */
static uint32_t map_line(const char *a, const char *b, uint32_t line) {
    const char *la[1 << 16], *lb[1 << 16];
    uint32_t na = 0, nb = 0;
    for (const char *s = a; *s && na < (1 << 16); s = strchr(s, '\n') ? strchr(s, '\n') + 1 : s + strlen(s)) la[na++] = s;
    for (const char *s = b; *s && nb < (1 << 16); s = strchr(s, '\n') ? strchr(s, '\n') + 1 : s + strlen(s)) lb[nb++] = s;
    #define LEN(x, i, n) ((i) + 1 < (n) ? (size_t)(x[(i) + 1] - x[i]) : strlen(x[i]))
    uint32_t pre = 0, suf = 0;
    while (pre < na && pre < nb && LEN(la, pre, na) == LEN(lb, pre, nb) && !memcmp(la[pre], lb[pre], LEN(la, pre, na))) pre++;
    while (suf < na - pre && suf < nb - pre && LEN(la, na - 1 - suf, na) == LEN(lb, nb - 1 - suf, nb) &&
           !memcmp(la[na - 1 - suf], lb[nb - 1 - suf], LEN(la, na - 1 - suf, na))) suf++;
    #undef LEN
    if (line <= pre) return line;
    if (line > na - suf) return line + nb - na;
    return na == nb ? line : 0;
}

static const Elf64_Phdr *phdrs(const Dwarf *d, int *n);

/* the bytes the binary loads at va, or NULL */
static const uint8_t *at_va(const Dwarf *d, uint64_t va, uint64_t n) {
    int k;
    const Elf64_Phdr *ph = phdrs(d, &k);
    for (int i = 0; i < k; i++)
        if (ph[i].p_type == PT_LOAD && va >= ph[i].p_vaddr && va + n <= ph[i].p_vaddr + ph[i].p_filesz) return d->map + ph[i].p_offset + (va - ph[i].p_vaddr);
    return NULL;
}

/* Function f of old can go on as g of new only with the same frame: the same prologue (its
   `sub rsp, N` sizes the frame) and every variable of f at the same place in g. */
static bool same_frame(const Dwarf *old, uint32_t f, const Dwarf *new, uint32_t g, char *why, size_t room) {
    Val lo, nlo;
    die_attr(old, f, AT_low_pc, &lo); die_attr(new, g, AT_low_pc, &nlo);
    const Row *ra = row_at(old, lo.u), *rb = row_at(new, nlo.u);
    uint64_t la = ra && ra + 1 < old->rows + old->nrows ? ra[1].addr - lo.u : 0, lb = rb && rb + 1 < new->rows + new->nrows ? rb[1].addr - nlo.u : 0;
    const uint8_t *pa = at_va(old, lo.u, la), *pb = at_va(new, nlo.u, lb);
    if (la != lb || !pa || !pb || memcmp(pa, pb, la)) { snprintf(why, room, "%s: its frame changed size", die_name(old, f)); return false; }
    uint32_t end = old->dies[f].next ? old->dies[f].next : old->ndies, nend = new->dies[g].next ? new->dies[g].next : new->ndies;
    for (uint32_t i = f + 1; i < end; i++) {
        if (old->dies[i].ab->tag != TAG_variable && old->dies[i].ab->tag != TAG_param) continue;
        const char *n = die_name(old, i);
        bool found = false;
        for (uint32_t k = g + 1; k < nend && !found; k++) {
            Val a, b;
            if ((new->dies[k].ab->tag == TAG_variable || new->dies[k].ab->tag == TAG_param) && n && die_name(new, k) && !strcmp(n, die_name(new, k)))
                found = die_attr(old, i, AT_location, &a) && die_attr(new, k, AT_location, &b) && a.b && b.b && a.n == b.n && !memcmp(a.b, b.b, a.n);
        }
        if (!found) { snprintf(why, room, "%s: the variable %s moved or went", die_name(old, f), n ? n : "?"); return false; }
    }
    return true;
}

static uint32_t func_named(const Dwarf *d, const char *linkage) {
    for (uint32_t i = 1; i < d->ndies; i++) {
        Val v;
        if (d->dies[i].ab->tag == TAG_subprogram && die_attr(d, i, 0x6e, &v) && v.b && !strcmp((const char *)v.b, linkage) && die_attr(d, i, AT_low_pc, &v)) return i;
    }
    return 0;
}

bool ec_patch(const Target *t, const Dwarf *old, const Dwarf *new, const uint64_t *active, int nactive,
              const char *old_src, const char *new_src, Patch *p) {
    memset(p, 0, sizeof *p);
    uint32_t fk[64]; uint64_t flo[64];
    if (nactive > 64) nactive = 64;
    for (int k = 0; k < nactive; k++) {               /* each frame's function, found once */
        Val lo;
        fk[k] = func_at(old, active[k]);
        flo[k] = fk[k] && die_attr(old, fk[k], AT_low_pc, &lo) ? lo.u : 0;
    }
    bool top_edited = false;
    int no, nn;
    const Elf64_Phdr *po = phdrs(old, &no), *pn = phdrs(new, &nn);
    Run *runs = NULL; uint32_t nr = 0, cap = 0;
    for (int i = 0; i < nn; i++) {
        const Elf64_Phdr *s = &pn[i], *o = NULL;
        if (s->p_type != PT_LOAD) continue;
        for (int k = 0; k < no; k++) if (po[k].p_type == PT_LOAD && po[k].p_vaddr == s->p_vaddr) o = &po[k];
        if (!o || o->p_memsz != s->p_memsz || o->p_filesz != s->p_filesz || o->p_flags != s->p_flags) {
            snprintf(p->why, sizeof p->why, "the segment at %#llx changed size or place: restart", (unsigned long long)s->p_vaddr);
            p->refused = true; free(runs); return false;
        }
        const uint8_t *a = old->map + o->p_offset, *b = new->map + s->p_offset;
        for (uint64_t x = 0; x < s->p_filesz;) {
            if (a[x] == b[x]) { x++; continue; }
            uint64_t y = x;
            while (y < s->p_filesz && a[y] != b[y]) y++;
            uint64_t va = s->p_vaddr + x;
            if (s->p_flags & PF_X) {
                for (int k = 0; k < nactive; k++) {                  /* an active frame's function edited? */
                    uint32_t f = fk[k];
                    if (!f || !flo[k] || !die_has_pc(old, f, va)) continue;
                    if (!call_operand(a, b, x, y - x, flo[k] - s->p_vaddr)) {
                        if (k == 0 || fk[k] == fk[0]) { top_edited = true; continue; }
                        snprintf(p->why, sizeof p->why, "%s is running (a frame is in it) and was changed in place: restart or step out first",
                                 die_name(old, f) ? die_name(old, f) : "a function");
                        p->refused = true; free(runs); return false;
                    }
                }
                p->code += y - x;
            } else if (s->p_flags & PF_W) {
                if (old_object_at(old, va)) { p->kept += y - x; x = y; continue; }   /* a global's runtime value wins over its new initial value */
                p->data += y - x;
            } else p->rodata += y - x;
            if (nr == cap) runs = realloc(runs, (cap = cap ? 2 * cap : 16) * sizeof *runs);
            runs[nr++] = (Run){ va, y - x, b + x };
            x = y;
        }
    }
    if (top_edited) {                                                 /* the stopped function: where does it go on? */
        Val ln;
        const Row *w = row_at(old, active[0]);
        uint32_t g = die_attr(old, fk[0], 0x6e, &ln) && ln.b ? func_named(new, (const char *)ln.b) : 0;
        for (int k = 1; k < nactive; k++)
            if (fk[k] == fk[0]) { snprintf(p->why, sizeof p->why, "%s was changed and is running in more than one frame (recursion): restart", die_name(old, fk[0])); goto refuse; }
        if (!w || w->addr != active[0]) { snprintf(p->why, sizeof p->why, "stopped inside a statement, not at its start: step to the next line first"); goto refuse; }
        if (!g) { snprintf(p->why, sizeof p->why, "%s is gone from the new program", die_name(old, fk[0])); goto refuse; }
        if (!same_frame(old, fk[0], new, g, p->why, sizeof p->why)) goto refuse;
        p->from_line = w->line;
        p->to_line = map_line(old_src, new_src, w->line);
        if (!p->to_line) { snprintf(p->why, sizeof p->why, "line %u is inside the edit and the edit changed the number of lines", w->line); goto refuse; }
        for (uint32_t i = 0; i + 1 < new->nrows && !p->new_pc; i++) {
            const Row *v = &new->rows[i];
            if (!v->end && v->stmt && v->line == p->to_line && die_has_pc(new, g, v->addr) && (i == 0 || new->rows[i - 1].line != v->line || new->rows[i - 1].end))
                p->new_pc = v->addr;
        }
        if (!p->new_pc) { snprintf(p->why, sizeof p->why, "line %u has no code in the new %s", p->to_line, die_name(new, g)); goto refuse; }
    }
    for (uint32_t i = 0; i < nr; i++)                                 /* checked all first: nothing is half written */
        if (!target_write(t, runs[i].va, runs[i].bytes, runs[i].n)) { snprintf(p->why, sizeof p->why, "write failed at %#llx", (unsigned long long)runs[i].va); p->refused = true; free(runs); return false; }
    p->runs = nr;
    free(runs);
    return true;
refuse:
    p->refused = true;
    free(runs);
    return false;
}
