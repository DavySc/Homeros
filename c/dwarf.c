#include "dwarf.h"
#include <elf.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct { const uint8_t *p, *end; } Rd;
static uint64_t rn(Rd *r, int n) { uint64_t v = 0; for (int i = 0; i < n && r->p < r->end; i++) v |= (uint64_t)*r->p++ << (8 * i); return v; }
static uint64_t uleb(Rd *r) { uint64_t v = 0; int s = 0; while (r->p < r->end) { uint8_t b = *r->p++; if (s < 64) v |= (uint64_t)(b & 0x7f) << s; s += 7; if (!(b & 0x80)) break; } return v; }
static int64_t sleb(Rd *r) {
    uint64_t v = 0; int s = 0; uint8_t b = 0;
    while (r->p < r->end) { b = *r->p++; if (s < 64) v |= (uint64_t)(b & 0x7f) << s; s += 7; if (!(b & 0x80)) break; }
    if (s < 64 && (b & 0x40)) v |= ~(uint64_t)0 << s;
    return (int64_t)v;
}
static const char *cstr(Rd *r) { const char *s = (const char *)r->p; while (r->p < r->end && *r->p) r->p++; r->p++; return s; }
static const char *sec_str(Sec s, uint64_t off) { return off < s.n ? (const char *)s.p + off : "?"; }

static uint64_t strx(const Dwarf *d, const Unit *u, uint64_t i) {
    Rd r = { d->str_offsets.p + u->str_off_base + 4 * i, d->str_offsets.p + d->str_offsets.n };
    return rn(&r, 4);
}
static uint64_t addrx(const Dwarf *d, const Unit *u, uint64_t i) {
    Rd r = { d->addr.p + u->addr_base + 8 * i, d->addr.p + d->addr.n };
    return rn(&r, 8);
}

/* Reads one attribute value of form `form`; implicit is the abbrev's constant for implicit_const. */
static void read_form(const Dwarf *d, const Unit *u, Rd *r, int form, int64_t implicit, Val *v) {
    memset(v, 0, sizeof *v);
    v->form = form;
    switch (form) {
    case 0x01: v->u = rn(r, u->addr_size); break;
    case 0x03: v->n = rn(r, 2); goto blk;
    case 0x04: v->n = rn(r, 4); goto blk;
    case 0x09: case 0x18: v->n = uleb(r); goto blk;
    case 0x0a: v->n = rn(r, 1);
    blk: v->b = r->p; r->p += v->n; break;
    case 0x0b: case 0x11: case 0x0c: case 0x25: case 0x29: v->u = rn(r, 1); break;
    case 0x05: case 0x12: case 0x26: case 0x2a: v->u = rn(r, 2); break;
    case 0x27: case 0x2b: v->u = rn(r, 3); break;
    case 0x06: case 0x13: case 0x0e: case 0x10: case 0x17: case 0x1f: case 0x28: case 0x2c: case 0x1c: case 0x1d: v->u = rn(r, 4); break;
    case 0x07: case 0x14: case 0x20: case 0x24: v->u = rn(r, 8); break;
    case 0x1e: v->b = r->p; v->n = 16; r->p += 16; break;
    case 0x0d: v->s = sleb(r); v->u = (uint64_t)v->s; break;
    case 0x0f: case 0x15: case 0x1a: case 0x1b: case 0x22: case 0x23: v->u = uleb(r); break;
    case 0x08: v->b = (const uint8_t *)cstr(r); break;
    case 0x19: v->u = 1; break;
    case 0x21: v->s = implicit; v->u = (uint64_t)implicit; break;
    case 0x16: read_form(d, u, r, (int)uleb(r), implicit, v); return;
    }
    if (form != 0x0d && form != 0x21) v->s = (int64_t)v->u;
    switch (form) {     /* make references absolute, strings and indexed addresses direct */
    case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: v->u += u->off; v->form = 0x10; break;
    case 0x0e: v->b = (const uint8_t *)sec_str(d->str, v->u); v->form = 0x08; break;
    case 0x1f: v->b = (const uint8_t *)sec_str(d->line_str, v->u); v->form = 0x08; break;
    case 0x1a: case 0x25: case 0x26: case 0x27: case 0x28: v->b = (const uint8_t *)sec_str(d->str, strx(d, u, v->u)); v->form = 0x08; break;
    case 0x1b: case 0x29: case 0x2a: case 0x2b: case 0x2c: v->u = addrx(d, u, v->u); v->form = 0x01; break;
    }
}

static Abbrev *read_abbrevs(const Dwarf *d, uint64_t off, uint32_t *count) {
    Rd r = { d->abbrev.p + off, d->abbrev.p + d->abbrev.n };
    uint32_t n = 0, cap = 64;
    Abbrev *a = calloc(cap, sizeof *a);
    for (;;) {
        uint64_t code = uleb(&r);
        if (!code) break;
        if (code >= cap) { uint32_t c2 = (uint32_t)code * 2; a = realloc(a, c2 * sizeof *a); memset(a + cap, 0, (c2 - cap) * sizeof *a); cap = c2; }
        Abbrev *x = &a[code];
        x->code = code; x->tag = (uint16_t)uleb(&r); x->kids = rn(&r, 1); x->spec = r.p;
        for (;;) { uint64_t at = uleb(&r), form = uleb(&r); if (form == 0x21) sleb(&r); if (!at && !form) break; }
        if (code + 1 > n) n = (uint32_t)code + 1;
    }
    *count = n;
    return a;
}

bool die_attr(const Dwarf *d, uint32_t i, int at, Val *v) {
    if (!i) return false;
    const Die *e = &d->dies[i];
    const Unit *u = &d->units[e->unit];
    Rd s = { e->ab->spec, d->abbrev.p + d->abbrev.n }, r = { e->attrs, d->info.p + d->info.n };
    for (;;) {
        int a = (int)uleb(&s), form = (int)uleb(&s);
        int64_t imp = form == 0x21 ? sleb(&s) : 0;
        if (!a && !form) return false;
        read_form(d, u, &r, form, imp, v);
        if (a == at) return true;
    }
}

const char *die_name(const Dwarf *d, uint32_t i) { Val v; return die_attr(d, i, AT_name, &v) && v.form == 0x08 ? (const char *)v.b : NULL; }
bool die_uint(const Dwarf *d, uint32_t i, int at, uint64_t *out) {
    Val v;
    if (!die_attr(d, i, at, &v) || v.b) return false;
    *out = v.u;
    return true;
}
uint32_t die_at(const Dwarf *d, uint32_t off) {
    uint32_t lo = 1, hi = d->ndies;
    while (lo < hi) { uint32_t m = (lo + hi) / 2; if (d->dies[m].off < off) lo = m + 1; else hi = m; }
    return lo < d->ndies && d->dies[lo].off == off ? lo : 0;
}
uint32_t die_ref(const Dwarf *d, uint32_t i, int at) { Val v; return die_attr(d, i, at, &v) && v.form == 0x10 ? die_at(d, (uint32_t)v.u) : 0; }
uint32_t die_kid(const Dwarf *d, uint32_t i) { return d->dies[i].ab->kids && i + 1 < d->ndies && d->dies[i + 1].parent == i ? i + 1 : 0; }

/* Walks a range list (DWARF 5 rnglists or DWARF 4 ranges) and says whether pc is in it. */
static bool ranges_have(const Dwarf *d, const Unit *u, const Val *v, uint64_t pc) {
    uint64_t base = u->base;
    if (u->version < 5) {
        Rd r = { d->ranges.p + v->u, d->ranges.p + d->ranges.n };
        for (;;) {
            uint64_t a = rn(&r, 8), b = rn(&r, 8);
            if (!a && !b) return false;
            if (a == UINT64_MAX) { base = b; continue; }
            if (pc >= base + a && pc < base + b) return true;
        }
    }
    uint64_t off = v->form == 0x23 ? u->rng_base + rn(&(Rd){ d->rnglists.p + u->rng_base + 4 * v->u, d->rnglists.p + d->rnglists.n }, 4) : v->u;
    Rd r = { d->rnglists.p + off, d->rnglists.p + d->rnglists.n };
    for (;;) {
        uint64_t a = 0, b = 0;
        switch (rn(&r, 1)) {
        case 0: return false;
        case 1: base = addrx(d, u, uleb(&r)); continue;
        case 2: a = addrx(d, u, uleb(&r)); b = addrx(d, u, uleb(&r)); break;
        case 3: a = addrx(d, u, uleb(&r)); b = a + uleb(&r); break;
        case 4: a = base + uleb(&r); b = base + uleb(&r); break;
        case 5: base = rn(&r, 8); continue;
        case 6: a = rn(&r, 8); b = rn(&r, 8); break;
        case 7: a = rn(&r, 8); b = a + uleb(&r); break;
        default: return false;
        }
        if (pc >= a && pc < b) return true;
    }
}

bool die_has_pc(const Dwarf *d, uint32_t i, uint64_t pc) {
    Val lo, hi;
    if (die_attr(d, i, AT_low_pc, &lo) && die_attr(d, i, AT_high_pc, &hi))
        return pc >= lo.u && pc < (hi.form == 0x01 ? hi.u : lo.u + hi.u);
    return die_attr(d, i, AT_ranges, &lo) && ranges_have(d, &d->units[d->dies[i].unit], &lo, pc);
}

uint32_t func_at(const Dwarf *d, uint64_t pc) {
    for (uint32_t i = 1; i < d->ndies; i++)
        if (d->dies[i].ab->tag == TAG_subprogram && die_has_pc(d, i, pc)) return i;
    return 0;
}

bool die_loc(const Dwarf *d, uint32_t i, uint64_t pc, const uint8_t **expr, size_t *n) {
    Val v;
    if (!die_attr(d, i, AT_location, &v)) return false;
    if (v.b) { *expr = v.b; *n = v.n; return true; }
    const Unit *u = &d->units[d->dies[i].unit];
    uint64_t base = u->base;
    if (u->version < 5) {
        Rd r = { d->loc.p + v.u, d->loc.p + d->loc.n };
        for (;;) {
            uint64_t a = rn(&r, 8), b = rn(&r, 8);
            if (!a && !b) return false;
            if (a == UINT64_MAX) { base = b; continue; }
            size_t len = rn(&r, 2);
            if (pc >= base + a && pc < base + b) { *expr = r.p; *n = len; return true; }
            r.p += len;
        }
    }
    uint64_t off = v.form == 0x22 ? u->loc_base + rn(&(Rd){ d->loclists.p + u->loc_base + 4 * v.u, d->loclists.p + d->loclists.n }, 4) : v.u;
    Rd r = { d->loclists.p + off, d->loclists.p + d->loclists.n };
    for (;;) {
        uint64_t a = 0, b = UINT64_MAX;
        switch (rn(&r, 1)) {
        case 0: return false;
        case 1: base = addrx(d, u, uleb(&r)); continue;
        case 2: a = addrx(d, u, uleb(&r)); b = addrx(d, u, uleb(&r)); break;
        case 3: a = addrx(d, u, uleb(&r)); b = a + uleb(&r); break;
        case 4: a = base + uleb(&r); b = base + uleb(&r); break;
        case 5: break;
        case 6: base = rn(&r, 8); continue;
        case 7: a = rn(&r, 8); b = rn(&r, 8); break;
        case 8: a = rn(&r, 8); b = a + uleb(&r); break;
        default: return false;
        }
        size_t len = uleb(&r);
        if (pc >= a && pc < b) { *expr = r.p; *n = len; return true; }
        r.p += len;
    }
}

/* ---- line programs ---- */
static uint32_t add_file(Dwarf *d, const char *name) {
    if (!(d->nfiles & (d->nfiles + 1)) || !d->files) d->files = realloc(d->files, 2 * (d->nfiles + 1) * sizeof *d->files);
    d->files[d->nfiles].name = name;
    return d->nfiles++;
}
static void add_row(Dwarf *d, Row row) {
    if (!(d->nrows & (d->nrows + 1)) || !d->rows) d->rows = realloc(d->rows, 2 * (d->nrows + 1) * sizeof *d->rows);
    d->rows[d->nrows++] = row;
}

static void read_lines(Dwarf *d, const Unit *u, uint64_t off) {
    Rd r = { d->line.p + off, d->line.p + d->line.n };
    uint64_t len = rn(&r, 4);
    const uint8_t *end = r.p + len;
    r.end = end;
    unsigned ver = (unsigned)rn(&r, 2);
    if (ver >= 5) r.p += 2;                       /* address size, segment selector size */
    uint64_t hlen = rn(&r, 4);
    const uint8_t *prog = r.p + hlen;
    unsigned minlen = (unsigned)rn(&r, 1);
    if (ver >= 4) rn(&r, 1);                      /* max ops per instruction */
    bool defstmt = rn(&r, 1);
    int lbase = (int8_t)rn(&r, 1);
    unsigned lrange = (unsigned)rn(&r, 1), opbase = (unsigned)rn(&r, 1);
    uint8_t oplens[256] = { 0 };
    for (unsigned i = 1; i < opbase; i++) oplens[i] = (uint8_t)rn(&r, 1);
    uint32_t file0 = d->nfiles;                   /* this unit's file n is d->files[file0 + n] (v5) or [file0 + n - 1] (v4) */
    if (ver >= 5) {
        const char *dirs[512] = { 0 };
        for (int pass = 0; pass < 2; pass++) {
            unsigned nfmt = (unsigned)rn(&r, 1);
            uint64_t fmt[16][2];
            for (unsigned i = 0; i < nfmt && i < 16; i++) { fmt[i][0] = uleb(&r); fmt[i][1] = uleb(&r); }
            uint64_t count = uleb(&r);
            for (uint64_t k = 0; k < count; k++) {
                const char *path = "?"; uint64_t dir = 0;
                for (unsigned i = 0; i < nfmt; i++) {
                    Val v;
                    read_form(d, u, &r, (int)fmt[i][1], 0, &v);
                    if (fmt[i][0] == 1 && v.b) path = (const char *)v.b;
                    if (fmt[i][0] == 2) dir = v.u;
                }
                if (!pass) { if (k < 512) dirs[k] = path; }
                else {
                    const char *dn = dir < 512 && dirs[dir] ? dirs[dir] : "";
                    char *full = malloc(strlen(dn) + strlen(path) + 2);
                    strcpy(full, path[0] == '/' ? "" : dn);
                    if (path[0] != '/' && *dn) strcat(full, "/");
                    strcat(full, path);
                    add_file(d, full);
                }
            }
        }
    } else {
        while (*r.p) cstr(&r);
        r.p++;
        while (*r.p) { const char *f = cstr(&r); uleb(&r); uleb(&r); uleb(&r); add_file(d, f); }
        file0--;
    }
    r.p = prog;
    Row row = { 0, 1, 1, defstmt, false };
    while (r.p < end) {
        unsigned op = (unsigned)rn(&r, 1);
        if (op >= opbase) {
            unsigned adj = op - opbase;
            row.addr += (adj / lrange) * minlen;
            row.line += lbase + (int)(adj % lrange);
            Row o = row; o.file += file0; add_row(d, o);
            continue;
        }
        switch (op) {
        case 0: {
            uint64_t n = uleb(&r);
            const uint8_t *next = r.p + n;
            unsigned ext = (unsigned)rn(&r, 1);
            if (ext == 1) { Row o = row; o.file += file0; o.end = true; add_row(d, o); row = (Row){ 0, 1, 1, defstmt, false }; }
            else if (ext == 2) row.addr = rn(&r, 8);
            r.p = next;
            break;
        }
        case 1: { Row o = row; o.file += file0; add_row(d, o); break; }
        case 2: row.addr += uleb(&r) * minlen; break;
        case 3: row.line += (int32_t)sleb(&r); break;
        case 4: row.file = (uint32_t)uleb(&r); break;
        case 5: uleb(&r); break;
        case 6: row.stmt = !row.stmt; break;
        case 7: break;
        case 8: row.addr += ((255 - opbase) / lrange) * minlen; break;
        case 9: row.addr += rn(&r, 2); break;
        default: for (unsigned i = 0; i < oplens[op]; i++) uleb(&r);
        }
    }
}

static bool ends_with(const char *s, const char *t) {
    size_t a = strlen(s), b = strlen(t);
    return a >= b && !strcmp(s + a - b, t) && (a == b || s[a - b - 1] == '/');
}

uint32_t line_addrs(const Dwarf *d, const char *file, uint32_t line, uint64_t *out, uint32_t room) {
    uint32_t n = 0;
    for (uint32_t i = 0; i + 1 < d->nrows; i++) {
        const Row *w = &d->rows[i];
        if (w->end || !w->stmt || w->line != line || !ends_with(d->files[w->file].name, file)) continue;
        if (i && !d->rows[i - 1].end && d->rows[i - 1].line == line && d->rows[i - 1].file == w->file) continue;   /* a run of the same line: its first row */
        bool dup = false;
        for (uint32_t k = 0; k < n; k++) dup |= out[k] == w->addr;
        if (!dup && n < room) out[n++] = w->addr;
    }
    return n;
}

const Row *row_at(const Dwarf *d, uint64_t pc) {
    for (uint32_t i = 0; i + 1 < d->nrows; i++)
        if (!d->rows[i].end && d->rows[i].addr <= pc && pc < d->rows[i + 1].addr) return &d->rows[i];
    return NULL;
}

/* ---- the file ---- */
bool dwarf_open(Dwarf *d, const char *path) {
    memset(d, 0, sizeof *d);
    int fd = open(path, O_RDONLY);
    struct stat st;
    if (fd < 0 || fstat(fd, &st)) return false;
    d->size = (size_t)st.st_size;
    d->map = mmap(NULL, d->size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (d->map == MAP_FAILED) return false;
    const Elf64_Ehdr *eh = (const Elf64_Ehdr *)d->map;
    const Elf64_Shdr *sh = (const Elf64_Shdr *)(d->map + eh->e_shoff);
    const char *names = (const char *)d->map + sh[eh->e_shstrndx].sh_offset;
    static const char *want[] = { ".debug_info", ".debug_abbrev", ".debug_str", ".debug_line_str", ".debug_line", ".debug_loc",
                                  ".debug_loclists", ".debug_ranges", ".debug_rnglists", ".debug_addr", ".debug_str_offsets",
                                  ".debug_frame", ".eh_frame" };
    Sec *secs[] = { &d->info, &d->abbrev, &d->str, &d->line_str, &d->line, &d->loc, &d->loclists, &d->ranges, &d->rnglists,
                    &d->addr, &d->str_offsets, &d->frame, &d->eh_frame };
    for (int i = 0; i < eh->e_shnum; i++)
        for (int k = 0; k < 13; k++)
            if (!strcmp(names + sh[i].sh_name, want[k])) {
                *secs[k] = (Sec){ d->map + sh[i].sh_offset, sh[i].sh_size };
                if (k == 12) d->eh_frame_addr = sh[i].sh_addr;
            }
    /* units and every DIE, in file order; dies[0] is a sentinel so that 0 means none */
    uint32_t cap = 1024, ucap = 16;
    d->dies = calloc(cap, sizeof *d->dies);
    d->units = calloc(ucap, sizeof *d->units);
    d->ndies = 1;
    Rd r = { d->info.p, d->info.p + d->info.n };
    while (r.p < r.end) {
        if (d->nunits == ucap) d->units = realloc(d->units, (ucap *= 2) * sizeof *d->units);
        Unit *u = &d->units[d->nunits];
        memset(u, 0, sizeof *u);
        u->off = (uint32_t)(r.p - d->info.p);
        uint64_t len = rn(&r, 4);
        u->end = (uint32_t)(r.p - d->info.p + len);
        u->version = (uint32_t)rn(&r, 2);
        uint64_t aoff;
        if (u->version >= 5) { rn(&r, 1); u->addr_size = (uint32_t)rn(&r, 1); aoff = rn(&r, 4); }
        else { aoff = rn(&r, 4); u->addr_size = (uint32_t)rn(&r, 1); }
        u->ab = read_abbrevs(d, aoff, &u->nab);
        u->die0 = d->ndies;
        Rd ur = { r.p, d->info.p + u->end };
        uint32_t parent = 0, last = 0;       /* last: the previous sibling at this depth */
        uint32_t stack[256], depth = 0;
        while (ur.p < ur.end) {
            uint32_t off = (uint32_t)(ur.p - d->info.p);
            uint64_t code = uleb(&ur);
            if (!code) { if (!depth) break; last = stack[--depth]; parent = last ? d->dies[last].parent : 0; continue; }
            if (code >= u->nab || !u->ab[code].code) break;
            if (d->ndies == cap) d->dies = realloc(d->dies, (cap *= 2) * sizeof *d->dies);
            uint32_t i = d->ndies++;
            Die *e = &d->dies[i];
            *e = (Die){ off, d->nunits, parent, 0, &u->ab[code], ur.p };
            if (last) d->dies[last].next = i;
            Rd s = { e->ab->spec, d->abbrev.p + d->abbrev.n };
            for (;;) {
                int a = (int)uleb(&s), form = (int)uleb(&s);
                int64_t imp = form == 0x21 ? sleb(&s) : 0;
                if (!a && !form) break;
                Val v;
                read_form(d, u, &ur, form, imp, &v);
            }
            if (e->ab->kids) { if (depth < 256) stack[depth++] = i; parent = i; last = 0; }
            else last = i;
        }
        uint32_t cu = u->die0;
        uint64_t x;
        if (cu < d->ndies) {
            if (die_uint(d, cu, AT_low_pc, &x)) u->base = x;
            u->zig = die_uint(d, cu, AT_language, &x) && x == 0x27;
            if (die_uint(d, cu, AT_str_offsets_base, &x)) u->str_off_base = x;
            if (die_uint(d, cu, AT_addr_base, &x)) u->addr_base = x;
            if (die_uint(d, cu, AT_loclists_base, &x)) u->loc_base = x;
            if (die_uint(d, cu, AT_rnglists_base, &x)) u->rng_base = x;
        }
        d->nunits++;
        r.p = d->info.p + u->end;
        if (cu < d->ndies && die_uint(d, cu, AT_stmt_list, &x)) read_lines(d, &d->units[d->nunits - 1], x);
    }
    return true;
}
