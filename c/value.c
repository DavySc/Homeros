#include "value.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void out_add(Out *o, const char *fmt, ...) {
    va_list a, b;
    va_start(a, fmt);
    va_copy(b, a);
    int k = vsnprintf(NULL, 0, fmt, a);
    va_end(a);
    if (o->n + (size_t)k + 1 > o->cap) { o->cap = (o->n + (size_t)k + 1) * 2; o->s = realloc(o->s, o->cap); }
    vsnprintf(o->s + o->n, (size_t)k + 1, fmt, b);
    va_end(b);
    o->n += (size_t)k;
}

/* LLVM's DWARF for Zig is C-shaped (measured): a typedef named "?*T" is an optional pointer, kept */
static bool opt_ptr(const Dwarf *d, uint32_t t) { const char *n = die_name(d, t); return d->dies[t].ab->tag == TAG_typedef && n && n[0] == '?'; }

static uint32_t strip(const Dwarf *d, uint32_t t) {
    while (t && !opt_ptr(d, t) && (d->dies[t].ab->tag == TAG_typedef || d->dies[t].ab->tag == TAG_const || d->dies[t].ab->tag == TAG_volatile))
        t = die_ref(d, t, AT_type);
    return t;
}

static uint64_t array_count(const Dwarf *d, uint32_t t) {
    for (uint32_t k = die_kid(d, t); k; k = d->dies[k].next) {
        uint64_t c;
        if (d->dies[k].ab->tag != TAG_subrange) continue;
        if (die_uint(d, k, AT_count, &c)) return c;
        if (die_uint(d, k, AT_upper_bound, &c)) return c + 1;
    }
    return 0;
}

static uint64_t bit_size(const Dwarf *d, uint32_t t);
/* LLVM names the element type after the vector, with a wrong size: the vector's name says it */
static uint64_t vector_elem_bits(const Dwarf *d, uint32_t t) {
    const char *n = die_name(d, t);
    char c; unsigned bits;
    if (n && sscanf(n, "@Vector(%*u, %c%u)", &c, &bits) == 2 && (c == 'u' || c == 'i')) return bits;
    return bit_size(d, die_ref(d, t, AT_type));
}

static uint64_t pow2(uint64_t x) { uint64_t p = 1; while (p < x) p *= 2; return p; }

/* Vectors carry no size in Zig's DWARF and the two backends lay them out differently (measured,
   266 types each): LLVM packs the elements' bits and rounds the bytes up to a power of two; the
   self-hosted backend gives each element its own size (bools excepted: bits), and the whole at
   least 16 bytes, a power of two up to 64, then whole 64s. stride: bits per element. */
static uint64_t vector_layout(const Dwarf *d, uint32_t t, uint64_t *stride) {
    uint32_t et = die_ref(d, t, AT_type);
    uint64_t n = array_count(d, t), eb = vector_elem_bits(d, t), es = type_size(d, et), enc = 0;
    die_uint(d, strip(d, et), AT_encoding, &enc);
    if (!d->units[d->dies[t].unit].zig || enc == 0x02) {
        *stride = eb;
        uint64_t bytes = (n * eb + 7) / 8;
        return d->units[d->dies[t].unit].zig ? bytes : pow2(bytes);
    }
    *stride = es * 8;
    return n * es <= 64 ? (pow2(n * es) < 16 ? 16 : pow2(n * es)) : (n * es + 63) / 64 * 64;
}

uint64_t type_size(const Dwarf *d, uint32_t t) {
    t = strip(d, t);
    uint64_t n;
    if (!t) return 0;
    if (die_uint(d, t, AT_byte_size, &n)) return n;
    Val gv;
    if (d->dies[t].ab->tag == TAG_array && die_attr(d, t, AT_GNU_vector, &gv)) return vector_layout(d, t, &n);
    switch (d->dies[t].ab->tag) {
    case TAG_pointer: return 8;
    case TAG_array: return array_count(d, t) * type_size(d, die_ref(d, t, AT_type));
    case TAG_base: return die_uint(d, t, AT_bit_size, &n) ? (n + 7) / 8 : 0;
    default: return type_size(d, die_ref(d, t, AT_type));   /* enums and packed structs: their backing integer */
    }
}

static uint64_t bit_size(const Dwarf *d, uint32_t t) {
    t = strip(d, t);
    uint64_t n;
    if (d->dies[t].ab->tag == TAG_base && die_uint(d, t, AT_bit_size, &n)) return n;
    if (!die_uint(d, t, AT_byte_size, &n) && d->dies[t].ab->tag != TAG_base) { uint32_t b = die_ref(d, t, AT_type); if (b) return bit_size(d, b); }
    return type_size(d, t) * 8;
}

/* little-endian integer of `bits` bits at v, sign-extended when asked */
static uint64_t get_int(const uint8_t *v, uint64_t bits, bool sign) {
    uint64_t x = 0, bytes = (bits + 7) / 8;
    for (uint64_t i = 0; i < bytes && i < 8; i++) x |= (uint64_t)v[i] << (8 * i);
    if (bits < 64) { x &= ((uint64_t)1 << bits) - 1; if (sign && bits && (x >> (bits - 1)) & 1) x |= ~(uint64_t)0 << bits; }
    return x;
}

/* shortest digits that read back as the same float, written out positionally as std.fmt does */
static void print_float(Out *o, double x, int kind) {
    if (isnan(x)) { out_add(o, "nan"); return; }
    if (isinf(x)) { out_add(o, x < 0 ? "-inf" : "inf"); return; }
    if (x == 0) { out_add(o, signbit(x) ? "-0" : "0"); return; }
    char buf[64];
    for (int p = 1; p <= 17; p++) {
        snprintf(buf, sizeof buf, "%.*e", p - 1, x);
        double y = strtod(buf, NULL);
        if (kind == 16 ? (_Float16)y == (_Float16)x : kind == 32 ? strtof(buf, NULL) == (float)x : y == x) break;
    }
    char digits[32]; int nd = 0, neg = buf[0] == '-';
    const char *p = buf + neg;
    for (; *p && *p != 'e'; p++) if (*p != '.') digits[nd++] = *p;
    int e = atoi(p + 1);                 /* value = 0.d1d2... * 10^(e+1) */
    while (nd > 1 && digits[nd - 1] == '0') nd--;
    if (neg) out_add(o, "-");
    if (e < 0) {
        out_add(o, "0.");
        for (int i = -1; i > e; i--) out_add(o, "0");
        out_add(o, "%.*s", nd, digits);
    } else if (e + 1 >= nd) {
        out_add(o, "%.*s", nd, digits);
        for (int i = nd; i <= e; i++) out_add(o, "0");
    } else out_add(o, "%.*s.%.*s", e + 1, digits, nd - e - 1, digits + e + 1);
}

static bool starts(const char *s, const char *t) { return s && !strncmp(s, t, strlen(t)); }

static void fmt(const Dwarf *d, const Target *tg, uint32_t t, const uint8_t *v, uint64_t n, int depth, Out *o);

/* the elements at addr (in the target), printed as an array or slice is */
static void fmt_elems(const Dwarf *d, const Target *tg, uint32_t et, uint64_t addr, const uint8_t *local, uint64_t count, int depth, Out *o) {
    /* local: count elements already read (the caller checked they fit) */
    if (!depth) { out_add(o, "{ ... }"); return; }
    uint64_t es = type_size(d, et);
    uint8_t *buf = malloc(es ? es : 1);
    out_add(o, "{ ");
    for (uint64_t i = 0; i < count; i++) {
        if (i) out_add(o, ", ");
        if (local) fmt(d, tg, et, local + i * es, es, depth - 1, o);
        else if (target_read(tg, addr + i * es, buf, es)) fmt(d, tg, et, buf, es, depth - 1, o);
        else { out_add(o, "<unreadable>"); break; }
    }
    out_add(o, " }");
    free(buf);
}

static uint32_t member_type(const Dwarf *d, uint32_t m) { return die_ref(d, m, AT_type); }
static uint64_t member_off(const Dwarf *d, uint32_t m) { uint64_t x = 0; die_uint(d, m, AT_data_member_location, &x); return x; }

static void fmt_union(const Dwarf *d, const Target *tg, uint32_t t, const uint8_t *v, uint64_t n, int depth, Out *o) {
    const char *name = die_name(d, t);
    uint32_t vp = 0;
    for (uint32_t k = die_kid(d, t); k; k = d->dies[k].next) if (d->dies[k].ab->tag == TAG_variant_part) vp = k;
    if (!vp) {   /* a one-field union: Zig's DWARF has no tag for it, tagged or not (measured); shown as tagged */
        uint32_t m = die_kid(d, t);
        if (!m || d->dies[m].next || !depth) { out_add(o, ".{ ... }"); return; }
        out_add(o, ".{ .%s = ", die_name(d, m));
        fmt(d, tg, member_type(d, m), v + member_off(d, m), n - member_off(d, m), depth - 1, o);
        out_add(o, " }");
        return;
    }
    uint32_t disc = die_ref(d, vp, AT_discr);
    uint32_t dt = member_type(d, disc);
    if (member_off(d, disc) + type_size(d, dt) > n) { out_add(o, "<bad tag place>"); return; }
    uint64_t dv = get_int(v + member_off(d, disc), bit_size(d, dt), false);
    uint32_t chosen = 0, dflt = 0;
    for (uint32_t k = die_kid(d, vp); k; k = d->dies[k].next) {
        uint64_t x;
        if (d->dies[k].ab->tag != TAG_variant) continue;
        if (!die_uint(d, k, AT_discr_value, &x)) dflt = k;
        else if (get_int((uint8_t *)&x, bit_size(d, dt), false) == dv) chosen = k;
    }
    if (!chosen) chosen = dflt;
    uint32_t m = chosen ? die_kid(d, chosen) : 0;
    if (!m) { out_add(o, "<bad tag %llu>", (unsigned long long)dv); return; }
    const char *field = die_name(d, m);
    uint32_t mt = member_type(d, m);
    if (starts(name, "?")) {                                  /* optional */
        if (!strcmp(field, "null")) out_add(o, "null");
        else fmt(d, tg, mt, v + member_off(d, m), n - member_off(d, m), depth, o);
    } else if (!strcmp(field, "value") || !strcmp(field, "error")) {   /* error union */
        fmt(d, tg, mt, v + member_off(d, m), n - member_off(d, m), depth, o);
    } else {
        if (!depth) { out_add(o, ".{ ... }"); return; }
        out_add(o, ".{ .%s = ", field);
        fmt(d, tg, mt, v + member_off(d, m), n - member_off(d, m), depth - 1, o);
        out_add(o, " }");
    }
}

static void fmt_struct(const Dwarf *d, const Target *tg, uint32_t t, const uint8_t *v, uint64_t n, int depth, Out *o) {
    const char *name = die_name(d, t);
    if (name && name[0] == '[') {                             /* slice: ptr and len */
        uint32_t pm = die_kid(d, t), lm = d->dies[pm].next;
        uint64_t ptr = get_int(v + member_off(d, pm), 64, false), len = get_int(v + member_off(d, lm), 64, false);
        uint32_t et = die_ref(d, member_type(d, pm), AT_type);
        if (len > 1u << 20) { out_add(o, "<len %llu>", (unsigned long long)len); return; }
        fmt_elems(d, tg, et, ptr, NULL, len, depth, o);
        return;
    }
    uint32_t k0 = die_kid(d, t), k1 = k0 ? d->dies[k0].next : 0;
    const char *n0 = k0 ? die_name(d, k0) : NULL, *n1 = k1 ? die_name(d, k1) : NULL;
    if (n0 && n1 && !d->dies[k1].next) {             /* LLVM's shapes: optional, error union, tagged union */
        uint32_t t0 = member_type(d, k0), t1 = member_type(d, k1);
        const uint8_t *v0 = v + member_off(d, k0), *v1 = v + member_off(d, k1);
        if (!strcmp(n0, "payload") && !strcmp(n1, "some")) {
            if (v1[0]) fmt(d, tg, t0, v0, n - member_off(d, k0), depth, o); else out_add(o, "null");
            return;
        }
        if (!strcmp(n0, "error") && !strcmp(n1, "payload")) {
            if (get_int(v0, bit_size(d, t0), false)) fmt(d, tg, t0, v0, n - member_off(d, k0), depth, o);
            else fmt(d, tg, t1, v1, n - member_off(d, k1), depth, o);
            return;
        }
        if (!strcmp(n0, "payload") && !strcmp(n1, "tag")) {
            uint32_t tt = strip(d, t1), pu = strip(d, t0);
            uint64_t x = get_int(v1, bit_size(d, tt), false);
            const char *field = NULL;
            for (uint32_t k = die_kid(d, tt); k; k = d->dies[k].next) {
                Val cv;
                if (die_attr(d, k, AT_const_value, &cv) && cv.u == x) field = die_name(d, k);
            }
            for (uint32_t k = pu ? die_kid(d, pu) : 0; k && field; k = d->dies[k].next)
                if (d->dies[k].ab->tag == TAG_member && !strcmp(die_name(d, k), field)) {
                    if (!depth) { out_add(o, ".{ ... }"); return; }
                    out_add(o, ".{ .%s = ", field);
                    fmt(d, tg, member_type(d, k), v0 + member_off(d, k), n - member_off(d, k0) - member_off(d, k), depth - 1, o);
                    out_add(o, " }");
                    return;
                }
            if (field && depth) { out_add(o, ".{ .%s = void }", field); return; }   /* void payloads have no member */
        }
    }
    if (!depth) { out_add(o, ".{ ... }"); return; }
    uint32_t backing = die_ref(d, t, AT_type);                /* packed: fields at bit offsets of a backing integer */
    bool tuple = k0 && die_name(d, k0) && !strcmp(die_name(d, k0), "0");
    out_add(o, ".{");
    int i = 0;
    for (uint32_t k = k0; k; k = d->dies[k].next) {
        if (d->dies[k].ab->tag != TAG_member) continue;
        out_add(o, i++ ? ", " : " ");
        if (!tuple) out_add(o, ".%s = ", die_name(d, k));
        uint32_t mt = member_type(d, k);
        uint64_t bo;
        if (backing && die_uint(d, k, AT_data_bit_offset, &bo)) {
            uint64_t bits = bit_size(d, mt), whole = get_int(v, bit_size(d, backing), false) >> bo;
            if (bits < 64) whole &= ((uint64_t)1 << bits) - 1;
            uint8_t tmp[8];
            for (int b = 0; b < 8; b++) tmp[b] = (uint8_t)(whole >> (8 * b));
            fmt(d, tg, mt, tmp, 8, depth - 1, o);
        } else if (member_off(d, k) <= n) fmt(d, tg, mt, v + member_off(d, k), n - member_off(d, k), depth - 1, o);
        else out_add(o, "<bad member place>");
    }
    out_add(o, " }");
}

static void fmt(const Dwarf *d, const Target *tg, uint32_t t, const uint8_t *v, uint64_t n, int depth, Out *o) {
    t = strip(d, t);
    if (!t) { out_add(o, "void"); return; }
    const char *name = die_name(d, t);
    if (type_size(d, t) > n) { out_add(o, "<%s: %llu bytes, %llu there>", name ? name : "?", (unsigned long long)type_size(d, t), (unsigned long long)n); return; }
    switch (d->dies[t].ab->tag) {
    case TAG_base: {
        uint64_t enc = 0, bits = bit_size(d, t);
        die_uint(d, t, AT_encoding, &enc);
        if (enc == 0x02) out_add(o, v[0] ? "true" : "false");
        else if (enc == 0x04) {
            if (bits == 16) { _Float16 h; memcpy(&h, v, 2); print_float(o, (double)h, 16); }
            else if (bits == 32) { float f; memcpy(&f, v, 4); print_float(o, f, 32); }
            else if (bits == 64) { double f; memcpy(&f, v, 8); print_float(o, f, 64); }
            else out_add(o, "<f%llu>", (unsigned long long)bits);
        } else if (bits > 64) out_add(o, "<%s>", name);
        else if (enc == 0x05 || enc == 0x06) out_add(o, "%lld", (long long)get_int(v, bits, true));
        else if (name && !strcmp(name, "void")) out_add(o, "void");
        else out_add(o, "%llu", (unsigned long long)get_int(v, bits, false));
        return;
    }
    case TAG_enum: {
        uint32_t bt = die_ref(d, t, AT_type);
        uint64_t enc = 0, bits = bit_size(d, t);
        if (bt) die_uint(d, strip(d, bt), AT_encoding, &enc);
        bool sign = enc == 0x05;
        uint64_t x = get_int(v, bits, sign);
        bool err = starts(name, "error{") || (name && !strcmp(name, "anyerror"));
        for (uint32_t k = die_kid(d, t); k; k = d->dies[k].next) {
            Val cv;
            if (d->dies[k].ab->tag == TAG_enumerator && die_attr(d, k, AT_const_value, &cv) &&
                get_int((uint8_t *)&cv.u, bits, sign) == x) {
                const char *en = die_name(d, k);
                out_add(o, starts(en, "error.") ? "%s" : err ? "error.%s" : ".%s", en);   /* LLVM's anyerror names carry "error." */
                return;
            }
        }
        if (err) out_add(o, "error.<%llu>", (unsigned long long)x);
        else if (sign) out_add(o, "@fromBackingInt(%lld)", (long long)x);
        else out_add(o, "@fromBackingInt(%llu)", (unsigned long long)x);
        return;
    }
    case TAG_array: {
        Val gv;
        if (!die_attr(d, t, AT_GNU_vector, &gv)) { fmt_elems(d, tg, die_ref(d, t, AT_type), 0, v, array_count(d, t), depth, o); return; }
        uint64_t stride, count = array_count(d, t);
        vector_layout(d, t, &stride);
        uint32_t et = die_ref(d, t, AT_type);
        uint64_t eb = vector_elem_bits(d, t);
        bool sign = name && strstr(name, ", i");
        if (!depth) { out_add(o, "{ ... }"); return; }
        out_add(o, "{ ");
        for (uint64_t i = 0; i < count; i++) {       /* each element's bits, gathered into a little buffer */
            uint8_t tmp[16] = { 0 };
            for (uint64_t b = 0; b < eb && b < 128; b++) {
                uint64_t at = i * stride + b;
                if (at / 8 < n && (v[at / 8] >> (at % 8)) & 1) tmp[b / 8] |= (uint8_t)(1u << (b % 8));
            }
            if (i) out_add(o, ", ");
            if (!d->units[d->dies[t].unit].zig && eb <= 64 && d->dies[strip(d, et)].ab->tag == TAG_base && eb != bit_size(d, et))
                out_add(o, sign ? "%lld" : "%llu", sign ? (long long)get_int(tmp, eb, true) : (long long)get_int(tmp, eb, false));
            else fmt(d, tg, et, tmp, 16, depth - 1, o);
        }
        out_add(o, " }");
        return;
    }
    case TAG_struct: fmt_struct(d, tg, t, v, n, depth, o); return;
    case TAG_union: fmt_union(d, tg, t, v, n, depth, o); return;
    case TAG_pointer: {
        uint64_t p = get_int(v, 64, false);
        uint32_t ct = strip(d, die_ref(d, t, AT_type));
        int ctag = ct ? d->dies[ct].ab->tag : 0;
        bool one = name && name[0] == '*';
        const char *cn = ct ? die_name(d, ct) : NULL;
        bool zig_union = ctag == TAG_union && cn && cn[0] != '?' && !strchr(cn, '!');
        if (one && (ctag == TAG_struct || ctag == TAG_enum || zig_union) && !(ctag == TAG_struct && cn && cn[0] == '[')
            && !(ctag == TAG_enum && (starts(cn, "error{") || !strcmp(cn, "anyerror")))) {
            uint64_t cn_size = type_size(d, ct);
            uint8_t *buf = malloc(cn_size ? cn_size : 1);
            if (target_read(tg, p, buf, cn_size)) fmt(d, tg, ct, buf, cn_size, depth, o); else out_add(o, "<unreadable %llx>", (unsigned long long)p);
            free(buf);
            return;
        }
        if (one && ctag == TAG_array) { fmt_elems(d, tg, die_ref(d, ct, AT_type), p, NULL, array_count(d, ct), depth, o); return; }
        /* "T@addr": T is the pointer's own name without its pointer prefix and qualifiers */
        const char *s = name ? name : "?";
        if (s[0] == '*') s++;
        else if (s[0] == '[') { s = strchr(s, ']'); s = s ? s + 1 : name; }
        for (;;) {
            if (starts(s, "const ")) s += 6;
            else if (starts(s, "volatile ")) s += 9;
            else if (starts(s, "allowzero ")) s += 10;
            else if (starts(s, "align(")) { s = strchr(s, ')'); s = s ? s + 2 : ""; }
            else break;
        }
        out_add(o, "%s@%llx", s, (unsigned long long)p);
        return;
    }
    case 0x3b: out_add(o, "%s", name ? name : "void"); return;       /* unspecified type: void */
    case TAG_typedef: {                                               /* LLVM's optional pointer */
        if (!get_int(v, n < 8 ? n * 8 : 64, false)) { out_add(o, "null"); return; }
        fmt(d, tg, die_ref(d, t, AT_type), v, n, depth, o);
        return;
    }
    default: out_add(o, "<tag %#x>", d->dies[t].ab->tag);
    }
}

/* ---- locations ---- */
static uint64_t ru(const uint8_t **p) {
    uint64_t v = 0; int s = 0; uint8_t b;
    do { b = *(*p)++; if (s < 64) v |= (uint64_t)(b & 0x7f) << s; s += 7; } while (b & 0x80);
    return v;
}
static int64_t rs(const uint8_t **p) {
    uint64_t v = 0; int s = 0; uint8_t b;
    do { b = *(*p)++; if (s < 64) v |= (uint64_t)(b & 0x7f) << s; s += 7; } while (b & 0x80);
    if (s < 64 && (b & 0x40)) v |= ~(uint64_t)0 << s;
    return (int64_t)v;
}
typedef struct { uint8_t *b; uint64_t n; } Bytes;

static bool eval(const Dwarf *d, const Target *tg, const Regs *r, uint32_t func, uint64_t pc,
                 const uint8_t *e, size_t len, uint64_t want, Bytes *out);

static bool frame_base(const Dwarf *d, const Target *tg, const Regs *r, uint32_t func, uint64_t pc, uint64_t *fb) {
    const uint8_t *e; size_t n;
    Val v;
    if (!die_attr(d, func, AT_frame_base, &v) || !v.b) return false;
    e = v.b; n = v.n;
    if (n == 1 && e[0] >= 0x50 && e[0] <= 0x60) { *fb = r->r[e[0] - 0x50]; return true; }    /* DW_OP_regN: the register's value */
    Bytes b = { 0 };
    if (!eval(d, tg, r, func, pc, e, n, 8, &b)) return false;
    *fb = get_int(b.b, 64, false);
    free(b.b);
    return true;
}

static bool eval(const Dwarf *d, const Target *tg, const Regs *r, uint32_t func, uint64_t pc,
                 const uint8_t *e, size_t len, uint64_t want, Bytes *out) {
    uint64_t st[64]; int sp = 0;
    const uint8_t *p = e, *end = e + len;
    int place = 0;              /* 0 memory at top of stack, 1 register `reg`, 2 value is top of stack, 3 implicit bytes */
    unsigned reg = 0;
    const uint8_t *imp = NULL; uint64_t impn = 0;
    out->b = calloc(1, want ? want : 1); out->n = want;
    uint64_t filled = 0;
    bool pieces = false;
#define U() ru(&p)
#define S() rs(&p)
#define PUSH(x) do { if (sp < 64) st[sp++] = (x); } while (0)
    while (p < end) {
        uint8_t op = *p++;
        if (op >= 0x30 && op <= 0x4f) { PUSH(op - 0x30); continue; }
        if (op >= 0x50 && op <= 0x6f) { place = 1; reg = op - 0x50; continue; }
        if (op >= 0x70 && op <= 0x8f) { int64_t o = S(); if (op - 0x70 > 16) return false; PUSH(r->r[op - 0x70] + (uint64_t)o); continue; }
        uint64_t a, b;
        switch (op) {
        case 0x03: a = 0; memcpy(&a, p, 8); p += 8; PUSH(a); break;
        case 0x06: if (!sp || !target_read(tg, st[sp - 1], &a, 8)) return false; st[sp - 1] = a; break;
        case 0x08: PUSH(*p); p++; break;
        case 0x09: PUSH((uint64_t)(int8_t)*p); p++; break;
        case 0x0a: a = 0; memcpy(&a, p, 2); p += 2; PUSH(a); break;
        case 0x0b: { int16_t x; memcpy(&x, p, 2); p += 2; PUSH((uint64_t)(int64_t)x); break; }
        case 0x0c: a = 0; memcpy(&a, p, 4); p += 4; PUSH(a); break;
        case 0x0d: { int32_t x; memcpy(&x, p, 4); p += 4; PUSH((uint64_t)(int64_t)x); break; }
        case 0x0e: case 0x0f: memcpy(&a, p, 8); p += 8; PUSH(a); break;
        case 0x10: PUSH(U()); break;
        case 0x11: PUSH((uint64_t)S()); break;
        case 0x12: if (!sp) return false; a = st[sp - 1]; PUSH(a); break;
        case 0x13: if (sp) sp--; break;
        case 0x1c: if (sp < 2) return false; b = st[--sp]; st[sp - 1] -= b; break;
        case 0x22: if (sp < 2) return false; b = st[--sp]; st[sp - 1] += b; break;
        case 0x23: if (!sp) return false; st[sp - 1] += U(); break;
        case 0x90: place = 1; reg = (unsigned)U(); break;
        case 0x91: { int64_t o = S(); uint64_t fb; if (!frame_base(d, tg, r, func, pc, &fb)) return false; PUSH(fb + (uint64_t)o); break; }
        case 0x92: { unsigned rg = (unsigned)U(); int64_t o = S(); if (rg > 16) return false; PUSH(r->r[rg] + (uint64_t)o); break; }
        case 0x9c: PUSH(r->r[6] + 16); break;       /* CFA with frame pointers (hypothesis until CFI is read) */
        case 0x9e: impn = U(); imp = p; p += impn; place = 3; break;
        case 0x9f: place = 2; break;
        case 0x93: {                                 /* piece: the current place gives the next n bytes */
            uint64_t n = U();
            pieces = true;
            uint8_t tmp[64] = { 0 };
            if (n > 64) return false;
            if (place == 1) { if (reg > 16) return false; memcpy(tmp, &r->r[reg], n < 8 ? n : 8); }
            else if (place == 2) { if (sp) memcpy(tmp, &st[sp - 1], n < 8 ? n : 8); }
            else if (place == 3) memcpy(tmp, imp, n < impn ? n : impn);
            else if (sp && !target_read(tg, st[sp - 1], tmp, n)) return false;
            if (filled + n <= want) memcpy(out->b + filled, tmp, n);
            filled += n; sp = 0; place = 0;
            break;
        }
        default: return false;     /* entry values and the rest: not built */
        }
    }
    if (pieces) return true;
    if (place == 1) { if (reg > 16) return false; memcpy(out->b, &r->r[reg], want < 8 ? want : 8); return true; }
    if (place == 2) { if (!sp) return false; memcpy(out->b, &st[sp - 1], want < 8 ? want : 8); return true; }
    if (place == 3) { memcpy(out->b, imp, want < impn ? want : impn); return true; }
    return sp && target_read(tg, st[sp - 1], out->b, want);
#undef U
#undef S
#undef PUSH
}

static uint32_t find_in(const Dwarf *d, uint32_t scope, uint64_t pc, const char *name) {
    uint32_t found = 0;
    for (uint32_t k = die_kid(d, scope); k; k = d->dies[k].next) {
        int tag = d->dies[k].ab->tag;
        if ((tag == TAG_variable || tag == TAG_param) && die_name(d, k) && !strcmp(die_name(d, k), name)) found = k;
        if (tag == TAG_lexblock && die_has_pc(d, k, pc)) { uint32_t in = find_in(d, k, pc, name); if (in) return in; }
    }
    return found;
}

uint32_t global_named(const Dwarf *d, const char *name) {
    for (uint32_t i = 1; i < d->ndies; i++) {
        if (d->dies[i].ab->tag != TAG_variable || !die_name(d, i) || strcmp(die_name(d, i), name)) continue;
        uint32_t up = d->dies[i].parent;
        while (up && d->dies[up].ab->tag != TAG_subprogram) up = d->dies[up].parent;
        Val loc;
        if (!up && die_attr(d, i, AT_location, &loc)) return i;
    }
    return 0;
}

uint32_t var_at(const Dwarf *d, uint64_t pc, const char *name) {
    uint32_t f = func_at(d, pc);
    return f ? find_in(d, f, pc, name) : 0;
}

bool print_var(const Dwarf *d, const Target *tg, const Regs *r, uint32_t var, Out *o) {
    uint64_t pc = r->r[16];
    uint32_t t = die_ref(d, var, AT_type), func = var;
    while (func && d->dies[func].ab->tag != TAG_subprogram) func = d->dies[func].parent;
    const uint8_t *e; size_t n;
    if (!die_loc(d, var, pc, &e, &n)) { out_add(o, "<no location>"); return false; }
    Bytes b;
    if (!eval(d, tg, r, func, pc, e, n, type_size(d, t), &b)) { free(b.b); out_add(o, "<location not built>"); return false; }
    fmt(d, tg, t, b.b, type_size(d, t), 3, o);     /* std.fmt.default_max_depth */
    free(b.b);
    return true;
}
