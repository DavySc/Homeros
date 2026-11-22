/* A minimal TDS 7.4 client ([MS-TDS], read 2026-11-21; FreeTDS was the
   second reference).  The token parser restarts a token from its first
   byte when the bytes run out, so it can stop on any byte; it retries
   only after the waiting bytes have doubled (or the reply is complete),
   which keeps a large value linear. */
#define _POSIX_C_SOURCE 200809L
#include "tds.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define put buf_put
#define putc8 buf_c
static int fmt(Buf *b, const char *f, ...) __attribute__((format(printf, 2, 3)));
static int fmt(Buf *b, const char *f, ...)
{
	char s[160];
	va_list a;
	va_start(a, f);
	int n = vsnprintf(s, sizeof s, f, a);
	va_end(a);
	return n < 0 ? -1 : put(b, s, (size_t)n < sizeof s ? (size_t)n : sizeof s - 1);
}

/* reading the token stream: bad = 1 when the bytes ran out */
typedef struct { const uint8_t *p, *e; int bad; } Cur;
static const uint8_t *take(Cur *c, size_t n)
{
	if (c->bad || (size_t)(c->e - c->p) < n) { c->bad = 1; return NULL; }
	c->p += n;
	return c->p - n;
}
static uint64_t le(const uint8_t *b, size_t n)
{
	uint64_t v = 0;
	for (size_t i = n; i-- > 0;) v = v * 256 + b[i];
	return v;
}
static uint64_t num(Cur *c, size_t n) { const uint8_t *b = take(c, n); return b ? le(b, n) : 0; }

/* text out: code points as UTF-8, escaping what would break a row line */
static int put_u16(Buf *b, const uint8_t *s, size_t n)   /* n bytes of UTF-16LE */
{
	for (size_t i = 0; i + 1 < n; i += 2) {
		uint32_t u = (uint32_t)le(s + i, 2);
		if (u >= 0xD800 && u < 0xDC00 && i + 3 < n && le(s + i + 2, 2) >= 0xDC00 && le(s + i + 2, 2) < 0xE000)
			u = 0x10000 + (u - 0xD800) * 1024 + ((uint32_t)le(s + i + 2, 2) - 0xDC00), i += 2;
		else if (u >= 0xD800 && u < 0xE000) u = 0xFFFD;
		if (buf_cp(b, u)) return -1;
	}
	return 0;
}
static const uint16_t cp1252[32] = {
	0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD,
	0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178};
/* a varchar under collation co: UTF-8 collations as they are; every other
   one read as code page 1252 (WWI's and AW's; other code pages: KB 11 gap) */
/* single-byte text: the server converts UTF-8 collations to the client's
   code page, since the login does not ask for UTF-8 (measured: the utf8
   branch this replaced never ran) */
static int put_char(Buf *b, const uint8_t *s, size_t n)
{
	size_t a = 0;
	for (size_t i = 0; i < n; i++)
		if (s[i] >= 0x80) {
			if (buf_esc(b, s + a, i - a) || buf_cp(b, s[i] < 0xA0 ? cp1252[s[i] - 0x80] : s[i])) return -1;
			a = i + 1;
		}
	return buf_esc(b, s + a, n - a);
}
static int put_hex(Buf *b, const uint8_t *s, size_t n)
{
	if (put(b, "0x", 2)) return -1;
	for (size_t i = 0; i < n; i++)
		if (fmt(b, "%02X", s[i])) return -1;
	return 0;
}
/* an exact decimal: magnitude m (n bytes, little-endian), scale digits after the point */
static int put_dec(Buf *b, int neg, const uint8_t *m, size_t n, unsigned scale)
{
	uint32_t l[4] = {0};
	char d[48];
	unsigned nd = 0;
	for (size_t i = 0; i < n && i < 16; i++) l[i / 4] += (uint32_t)m[i] << (8 * (i % 4));
	do {
		uint64_t r = 0;
		for (int k = 3; k >= 0; k--) {
			uint64_t x = r * 4294967296u + l[k];
			l[k] = (uint32_t)(x / 10), r = x % 10;
		}
		d[nd++] = (char)('0' + r);
	} while (l[0] || l[1] || l[2] || l[3]);
	while (nd <= scale) d[nd++] = '0';
	if (neg && putc8(b, '-')) return -1;
	for (unsigned i = nd; i-- > 0;)
		if (putc8(b, d[i]) || (i == scale && scale && putc8(b, '.'))) return -1;
	return 0;
}
static int put_dbl(Buf *b, double v, int single)   /* the shortest text that reads back the same */
{
	char s[40];
	for (int p = single ? 6 : 15; p <= 17; p++) {
		snprintf(s, sizeof s, "%.*g", p, v);
		if (single ? strtof(s, NULL) == (float)v : strtod(s, NULL) == v) break;
	}
	return put(b, s, strlen(s));
}
/* days since 0001-01-01 as y-m-d (H. Hinnant's civil_from_days) */
static int put_date(Buf *b, int64_t days)
{
	int64_t z = days - 719162 + 719468, era = (z >= 0 ? z : z - 146096) / 146097, doe = z - era * 146097;
	int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365, doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	int64_t mp = (5 * doy + 2) / 153, d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
	return fmt(b, "%04lld-%02lld-%02lld", (long long)(yoe + era * 400 + (m <= 2)), (long long)m, (long long)d);
}
static uint64_t pow10u(unsigned s) { uint64_t p = 1; while (s--) p *= 10; return p; }
static int put_time(Buf *b, uint64_t u, unsigned scale)   /* u: 10^-scale seconds since midnight */
{
	uint64_t p = pow10u(scale), s = u / p;
	if (fmt(b, "%02u:%02u:%02u", (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60))) return -1;
	return scale ? fmt(b, ".%0*llu", (int)scale, (unsigned long long)(u % p)) : 0;
}

enum { NUL = 0x1F, INT1 = 0x30, BIT = 0x32, INT2 = 0x34, INT4 = 0x38, DATETIM4 = 0x3A, FLT4 = 0x3B, MONEY = 0x3C,
	DATETIME = 0x3D, FLT8 = 0x3E, MONEY4 = 0x7A, INT8 = 0x7F, GUID = 0x24, INTN = 0x26, BITN = 0x68, DECIMALN = 0x6A,
	NUMERICN = 0x6C, FLTN = 0x6D, MONEYN = 0x6E, DATETIMN = 0x6F, DATEN = 0x28, TIMEN = 0x29, DATETIME2N = 0x2A,
	DATETIMEOFFSETN = 0x2B, BIGVARBIN = 0xA5, BIGVARCHR = 0xA7, BIGBINARY = 0xAD, BIGCHAR = 0xAF, NVARCHAR = 0xE7,
	NCHAR = 0xEF, XML = 0xF1, UDT = 0xF0, TEXT = 0x23, IMAGE = 0x22, NTEXT = 0x63, SSVARIANT = 0x62 };

static int fixed(uint8_t t)   /* a fixed-length type's size, or -1 */
{
	switch (t) {
	case NUL: return 0;
	case INT1: case BIT: return 1;
	case INT2: return 2;
	case INT4: case DATETIM4: case FLT4: case MONEY4: return 4;
	case MONEY: case DATETIME: case FLT8: case INT8: return 8;
	}
	return -1;
}

/* one value of type ty, n bytes (not NULL) */
static int put_val(Buf *b, const TdsCol *c, uint8_t ty, const uint8_t *s, size_t n)
{
	/* the lengths that would read past the value (the fuzzer); a wrong but
	   in-bounds length only shows a wrong value of a broken stream */
	int f = fixed(ty);
	if (f >= 0 ? n != (size_t)f : ty == INTN ? n != 1 && n != 2 && n != 4 && n != 8
		: ty == FLTN || ty == MONEYN || ty == DATETIMN ? n != 4 && n != 8
		: ty == BITN ? n != 1 : ty == GUID ? n != 16 : ty == DATEN ? n != 3
		: ty == DATETIME2N ? n < 3 : ty == DATETIMEOFFSETN ? n < 5
		: ty == DECIMALN || ty == NUMERICN ? n < 1 || n > 17 : ty == SSVARIANT ? n < 2 || n < 2u + s[1] : 0) return -1;
	if (c->scale > (ty == DECIMALN || ty == NUMERICN ? 38 : ty == TIMEN || ty == DATETIME2N || ty == DATETIMEOFFSETN ? 7 : 255)) return -1;
	switch (ty) {
	case INT1: case INT2: case INT4: case INT8: case INTN: {
		uint64_t v = le(s, n);
		int neg = n > 1 && s[n - 1] >= 0x80;   /* tinyint is unsigned */
		return fmt(b, "%s%llu", neg ? "-" : "", (unsigned long long)(neg ? (n == 8 ? ~v + 1 : (1ull << (8 * n)) - v) : v));
	}
	case BIT: case BITN: return putc8(b, s[0] ? '1' : '0');
	case FLT4: case FLT8: case FLTN: {
		if (n == 4) { float f; memcpy(&f, s, 4); return put_dbl(b, f, 1); }
		double d; memcpy(&d, s, 8); return put_dbl(b, d, 0);
	}
	case MONEY: case MONEY4: case MONEYN: {
		int64_t v = n == 4 ? (int32_t)(uint32_t)le(s, 4) : (int64_t)(le(s, 4) << 32 | le(s + 4, 4));
		uint64_t m = v < 0 ? (uint64_t)0 - (uint64_t)v : (uint64_t)v;
		uint8_t mb[8];
		for (int i = 0; i < 8; i++) mb[i] = (uint8_t)(m >> (8 * i));
		return put_dec(b, v < 0, mb, 8, 4);
	}
	case DECIMALN: case NUMERICN: return put_dec(b, s[0] == 0, s + 1, n - 1, c->scale);
	case DATETIME: case DATETIM4: case DATETIMN:
		if (n == 4) return put_date(b, (int64_t)le(s, 2) + 693595) || putc8(b, ' ') || put_time(b, le(s + 2, 2) * 60, 0);
		{
			uint64_t ms = (le(s + 4, 4) * 10 + 1) / 3;   /* 1/300 s to the nearest ms, as the server shows it */
			return put_date(b, (int64_t)(int32_t)(uint32_t)le(s, 4) + 693595) || putc8(b, ' ') || put_time(b, ms, 3);
		}
	case DATEN: return put_date(b, (int64_t)le(s, 3));
	case TIMEN: return put_time(b, le(s, n), c->scale);
	case DATETIME2N: return put_date(b, (int64_t)le(s + n - 3, 3)) || putc8(b, ' ') || put_time(b, le(s, n - 3), c->scale);
	case DATETIMEOFFSETN: {
		int off = (int16_t)(uint16_t)le(s + n - 2, 2);   /* minutes; the value is UTC */
		uint64_t day = 86400 * pow10u(c->scale), u = le(s, n - 5) + (uint64_t)((int64_t)off * 60 * (int64_t)pow10u(c->scale) + (int64_t)day);
		int64_t d = (int64_t)le(s + n - 5, 3) + (int64_t)(u / day) - 1;
		int a = off < 0 ? -off : off;
		return put_date(b, d) || putc8(b, ' ') || put_time(b, u % day, c->scale) || fmt(b, " %c%02d:%02d", off < 0 ? '-' : '+', a / 60, a % 60);
	}
	case GUID:
		return fmt(b, "%08X-%04X-%04X-", (unsigned)le(s, 4), (unsigned)le(s + 4, 2), (unsigned)le(s + 6, 2))
			|| fmt(b, "%02X%02X-%02X%02X%02X%02X%02X%02X", s[8], s[9], s[10], s[11], s[12], s[13], s[14], s[15]);
	case BIGVARCHR: case BIGCHAR: case TEXT: return put_char(b, s, n);
	case NVARCHAR: case NCHAR: case NTEXT: case XML: return put_u16(b, s, n);
	case BIGVARBIN: case BIGBINARY: case IMAGE: case UDT: return put_hex(b, s, n);
	case SSVARIANT: {   /* base type, property bytes, properties, value */
		TdsCol v = {0};
		uint8_t pb = s[1];
		if ((s[0] == DECIMALN || s[0] == NUMERICN) && pb >= 2) v.scale = s[3];
		if ((s[0] == TIMEN || s[0] == DATETIME2N || s[0] == DATETIMEOFFSETN) && pb >= 1) v.scale = s[2];
		if ((s[0] == BIGVARCHR || s[0] == BIGCHAR) && pb >= 5) memcpy(v.coll, s + 2, 5);
		return put_val(b, &v, s[0], s + 2 + pb, n - 2 - pb);
	}
	}
	return -1;
}

static void name(Cur *c, int wide, Buf *out)   /* B_VARCHAR (wide 0) or US_VARCHAR (1), to out if given */
{
	size_t n = (size_t)num(c, wide ? 2 : 1) * 2;
	const uint8_t *s = take(c, n);
	if (s && out) put_u16(out, s, n);
}

static int typeinfo(Cur *c, TdsCol *k)
{
	k->type = (uint8_t)num(c, 1);
	int f = fixed(k->type);
	if (f >= 0) { k->len = (uint32_t)f; return 0; }
	switch (k->type) {
	case GUID: case INTN: case BITN: case FLTN: case MONEYN: case DATETIMN: k->len = (uint32_t)num(c, 1); return 0;
	case DECIMALN: case NUMERICN: k->len = (uint32_t)num(c, 1); k->prec = (uint8_t)num(c, 1); k->scale = (uint8_t)num(c, 1); return 0;
	case DATEN: return 0;
	case TIMEN: case DATETIME2N: case DATETIMEOFFSETN: k->scale = (uint8_t)num(c, 1); return 0;
	case BIGVARBIN: case BIGBINARY: k->len = (uint32_t)num(c, 2); k->plp = k->len == 0xFFFF; return 0;
	case BIGVARCHR: case BIGCHAR: case NVARCHAR: case NCHAR: {
		k->len = (uint32_t)num(c, 2), k->plp = k->len == 0xFFFF;
		const uint8_t *co = take(c, 5);
		if (co) memcpy(k->coll, co, 5);
		return 0;
	}
	case XML: k->plp = 1;
		if (num(c, 1)) name(c, 0, NULL), name(c, 0, NULL), name(c, 1, NULL);
		return 0;
	case UDT: k->plp = 1; k->len = (uint32_t)num(c, 2);
		name(c, 0, NULL), name(c, 0, NULL), name(c, 0, NULL), name(c, 1, NULL);
		return 0;
	case TEXT: case NTEXT: case IMAGE: {
		k->len = (uint32_t)num(c, 4);
		const uint8_t *co = k->type == IMAGE ? NULL : take(c, 5);
		if (co) memcpy(k->coll, co, 5);
		for (uint64_t p = num(c, 1); p > 0 && !c->bad; p--) name(c, 1, NULL);
		return 0;
	}
	case SSVARIANT: k->len = (uint32_t)num(c, 4); return 0;
	}
	return c->bad ? 0 : -1;   /* no type byte yet is a short token, not an unknown type (found by the fuzzer) */
}

/* one column value of a row: its bytes, or s = NULL for NULL */
static int value(Tds *t, Cur *c, const TdsCol *k, Buf *tmp, const uint8_t **s, size_t *n)
{
	int f = fixed(k->type);
	*s = NULL;
	if (k->plp) {
		uint64_t total = num(c, 8);
		if (total == ~(uint64_t)0) return 0;
		tmp->n = 0;
		for (uint64_t m; (m = num(c, 4)) > 0 && !c->bad;) {
			const uint8_t *p = take(c, m);
			if (p && put(tmp, p, m)) return -1;
		}
		*s = tmp->p ? tmp->p : (const uint8_t *)"", *n = tmp->n;
		return 0;
	}
	if (f >= 0) { *n = (size_t)f; *s = take(c, *n); return 0; }
	switch (k->type) {
	case BIGVARBIN: case BIGBINARY: case BIGVARCHR: case BIGCHAR: case NVARCHAR: case NCHAR:
		*n = (size_t)num(c, 2);
		if (*n == 0xFFFF) return 0;
		break;
	case TEXT: case NTEXT: case IMAGE: {
		uint64_t p = num(c, 1);
		if (!p) return 0;
		take(c, p + 8);   /* text pointer, timestamp */
		*n = (size_t)num(c, 4);
		break;
	}
	case SSVARIANT: *n = (size_t)num(c, 4); if (!*n) return 0; break;
	default: *n = (size_t)num(c, 1); if (!*n) return 0;
	}
	(void)t;
	*s = take(c, *n);
	return 0;
}

static int row(Tds *t, Cur *c, int nbc)
{
	const uint8_t *bits = nbc ? take(c, (t->ncol + 7) / 8) : NULL;
	Buf tmp = {0};
	int r = 0;
	for (size_t i = 0; i < t->ncol && !c->bad && !r; i++) {
		const uint8_t *s = NULL;
		size_t n = 0;
		if (i && !t->quiet && putc8(&t->out, '\t')) r = -1;
		if (bits && bits[i / 8] >> (i % 8) & 1) s = NULL;
		else if (value(t, c, &t->col[i], &tmp, &s, &n)) r = -1;
		if (r || c->bad || t->quiet) continue;
		r = s ? put_val(&t->out, &t->col[i], t->col[i].type, s, n) : put(&t->out, "NULL", 4);
		if (r) snprintf(t->net.err, sizeof t->net.err, "cannot show column %zu (type 0x%02X)", i + 1, t->col[i].type);
	}
	free(tmp.p);
	if (!r && !c->bad) t->rows++;
	return r ? r : t->quiet ? 0 : putc8(&t->out, '\n');
}

static int token(Tds *t, Cur *c)
{
	uint8_t k = (uint8_t)num(c, 1);
	switch (k) {
	case 0x81: {   /* COLMETADATA */
		uint64_t n = num(c, 2);
		if (n == 0xFFFF) n = 0;
		TdsCol *col = n ? calloc(n, sizeof *col) : NULL;
		if (n && !col) return -1;
		free(t->col), t->col = col, t->ncol = n;
		for (size_t i = 0; i < n && !c->bad; i++) {
			take(c, 6);   /* user type, flags */
			if (typeinfo(c, &col[i])) { snprintf(t->net.err, sizeof t->net.err, "unknown type 0x%02X", col[i].type); return -1; }
			if (i && !t->quiet) putc8(&t->out, '\t');
			name(c, 0, t->quiet ? NULL : &t->out);
		}
		if (!c->bad) t->sets++;
		return t->quiet || c->bad ? 0 : putc8(&t->out, '\n');
	}
	case 0xD1: return row(t, c, 0);
	case 0xD2: return row(t, c, 1);
	case 0xFD: case 0xFE: case 0xFF: {   /* DONE, DONEPROC, DONEINPROC */
		unsigned st = (unsigned)num(c, 2);
		num(c, 2);
		uint64_t cnt = num(c, 8);
		if (c->bad) return 0;
		if (st & 0x10 && !t->quiet) fmt(&t->out, "(%llu row%s affected)\n", (unsigned long long)cnt, cnt == 1 ? "" : "s");
		if (k == 0xFD && !(st & 1) && (t->state == TDS_BUSY || (t->state == TDS_CANCEL && st & 0x20))) t->state = TDS_READY, t->quiet = 0;
		if (k == 0xFD && !(st & 1) && t->state == TDS_LOGIN) t->state = t->net.err[0] ? TDS_DEAD : TDS_READY;
		return 0;
	}
	case 0xAA: case 0xAB: {   /* ERROR, INFO */
		size_t len = (size_t)num(c, 2);
		Cur m = {take(c, len), NULL, 0};
		if (!m.p) return 0;
		m.e = m.p + len;
		unsigned no = (unsigned)num(&m, 4), state = (unsigned)num(&m, 1), cls = (unsigned)num(&m, 1);
		size_t mark = t->out.n;
		if (k == 0xAA) fmt(&t->out, "Msg %u, Level %u, State %u", no, cls, state);
		size_t head = t->out.n;
		name(&m, 1, &t->out);   /* the text */
		size_t end = t->out.n;
		name(&m, 0, NULL), name(&m, 0, NULL);
		unsigned line = (unsigned)num(&m, 4);
		if (k == 0xAA) {   /* sqlcmd's layout: the head line, then the text */
			char head_line[64];
			int hn = snprintf(head_line, sizeof head_line, ", Line %u\n", line);
			if (put(&t->out, head_line, (size_t)hn)) return -1;
			memmove(t->out.p + head + hn, t->out.p + head, end - head);
			memcpy(t->out.p + head, head_line, (size_t)hn);
		}
		if (t->state == TDS_LOGIN && k == 0xAA)
			snprintf(t->net.err, sizeof t->net.err, "%.*s", (int)(t->out.n - mark), (const char *)t->out.p + mark);
		return putc8(&t->out, '\n');
	}
	case 0xE3: {   /* ENVCHANGE: packet size, transaction begin/commit/rollback */
		size_t len = (size_t)num(c, 2);
		Cur e = {take(c, len), NULL, 0};
		if (!e.p) return 0;
		e.e = e.p + len;
		uint8_t ty = (uint8_t)num(&e, 1);
		if (ty == 4) {
			Buf v = {0};
			name(&e, 0, &v);
			if (!putc8(&v, 0)) t->pkt = (uint32_t)strtoul((char *)v.p, NULL, 10);   /* digits, then the old value's length byte */
			free(v.p);
		}
		if (ty == 8 && num(&e, 1) == 8) t->txn = num(&e, 8);
		if (ty == 9 || ty == 10) t->txn = 0;
		return 0;
	}
	case 0xAD: case 0xA9: case 0xA4: case 0xA5: case 0xED:   /* LOGINACK, ORDER, TABNAME, COLINFO, SSPI */
		take(c, (size_t)num(c, 2));
		return 0;
	case 0x79: take(c, 4); return 0;   /* RETURNSTATUS */
	case 0xE4: take(c, (size_t)num(c, 4)); return 0;   /* SESSIONSTATE */
	case 0xAE:   /* FEATUREEXTACK */
		while (!c->bad && num(c, 1) != 0xFF) take(c, (size_t)num(c, 4));
		return 0;
	}
	snprintf(t->net.err, sizeof t->net.err, "unknown token 0x%02X", k);
	return -1;
}

static int parse(Tds *t)
{
	while (t->at < t->msg.n) {
		Cur c = {t->msg.p + t->at, t->msg.p + t->msg.n, 0};
		size_t mark = t->out.n;
		uint64_t rows = t->rows;
		if (token(t, &c)) return -1;
		if (c.bad) { t->out.n = mark, t->rows = rows, t->tried = t->msg.n - t->at; break; }
		t->at = (size_t)(c.p - t->msg.p), t->tried = 0;
	}
	/* the whole reply is here: a short token or a missing last DONE is a
	   broken stream, not a wait (a misread null bitmap hung tdsq for 3 h) */
	if (t->eom && (t->at < t->msg.n || t->state == TDS_BUSY)) { snprintf(t->net.err, sizeof t->net.err, "the reply ends inside a token or before its last DONE"); return -1; }
	if (t->at == t->msg.n) t->msg.n = t->at = 0;
	else if (t->at > 65536) memmove(t->msg.p, t->msg.p + t->at, t->msg.n - t->at), t->msg.n -= t->at, t->at = 0;
	return 0;
}

/* a message as packets of the agreed size */
static int send_msg(Tds *t, uint8_t type, const uint8_t *s, size_t n)
{
	size_t room = t->pkt - 8;
	uint8_t id = 1;
	do {
		size_t k = n < room ? n : room;
		uint8_t p[65536], h[8] = {type, k == n, (uint8_t)((k + 8) >> 8), (uint8_t)(k + 8), 0, 0, id++, 0};
		memcpy(p, h, 8), memcpy(p + 8, s, k);   /* one TLS record per packet: SQL Server closes the connection on a header sealed alone (measured) */
		if (net_send(&t->net, p, k + 8)) return -1;
		s += k, n -= k;
	} while (n);
	t->eom = 0;
	return 0;
}

static int put_le(Buf *b, uint64_t v, size_t n)
{
	uint8_t s[8];
	for (size_t i = 0; i < n; i++) s[i] = (uint8_t)(v >> (8 * i));
	return put(b, s, n);
}
static int put_utf16(Buf *b, const char *s)   /* UTF-8 in, UTF-16LE out */
{
	const uint8_t *p = (const uint8_t *)s;
	while (*p) {
		uint32_t u = *p++;
		int more = u >= 0xF0 ? 3 : u >= 0xE0 ? 2 : u >= 0xC0 ? 1 : 0;
		u &= more == 3 ? 7 : more == 2 ? 15 : more == 1 ? 31 : 127;
		while (more-- && (*p & 0xC0) == 0x80) u = u * 64 + (*p++ & 63u);
		if (u >= 0x10000 && put_le(b, 0xD800 + ((u - 0x10000) >> 10), 2)) return -1;
		if (put_le(b, u >= 0x10000 ? 0xDC00 + ((u - 0x10000) & 1023) : u, 2)) return -1;
	}
	return 0;
}

int tds_open(Tds *t, const char *host, int port, const char *user, const char *pass, const char *db, int trust)
{
	memset(t, 0, sizeof *t);
	t->pkt = 4096, t->state = TDS_PRELOGIN;
	if (net_open(&t->net, host, port, trust)) return -1;
	/* PRELOGIN: version, encryption ON (always TLS) */
	static const uint8_t pre[] = {0, 0, 11, 0, 6, 1, 0, 17, 0, 1, 0xFF, 16, 0, 0, 0, 0, 0, 1};
	if (send_msg(t, 0x12, pre, sizeof pre)) return -1;
	/* LOGIN7, sent when the prelogin reply arrives */
	const char *str[9] = {"homeros", user, pass, "homeros", host, "", "homeros", "", db};
	Buf v = {0}, *l = &t->login;
	uint16_t off = 94, len[9];
	for (int i = 0; i < 9; i++) {
		size_t at = v.n;
		if (put_utf16(&v, str[i])) return -1;
		len[i] = (uint16_t)((v.n - at) / 2);
		if (i == 2)
			for (size_t j = at; j < v.n; j++) v.p[j] = (uint8_t)((v.p[j] << 4 | v.p[j] >> 4) ^ 0xA5);
	}
	int r = put_le(l, 94 + v.n, 4) || put_le(l, 0x74000004, 4) || put_le(l, 4096, 4) || put_le(l, 7, 4) || put_le(l, (uint64_t)getpid(), 4)
		|| put_le(l, 0, 4) || put_le(l, 0x0003E0, 4) /* USE_DB, INIT_DB_FATAL, SET_LANG; INIT_LANG_FATAL, ODBC (SSMS's ANSI settings) */
		|| put_le(l, 0, 4) || put_le(l, 0x409, 4);
	for (int i = 0; i < 9 && !r; i++) r = put_le(l, off, 2) || put_le(l, len[i], 2), off = (uint16_t)(off + 2 * len[i]);
	r = r || put(l, "\0\0\0\0\0\0", 6) || put_le(l, off, 4) || put_le(l, off, 4) || put_le(l, off, 4) || put_le(l, 0, 4) || put(l, v.p, v.n);
	free(v.p);
	return r ? -1 : 0;
}

int tds_batch(Tds *t, const char *sql)
{
	if (t->state != TDS_READY) return -1;
	Buf b = {0};
	int r = put_le(&b, 22, 4) || put_le(&b, 18, 4) || put_le(&b, 2, 2) || put_le(&b, t->txn, 8) || put_le(&b, 1, 4)
		|| put_utf16(&b, sql) || send_msg(t, 0x01, b.p, b.n);
	free(b.p);
	if (!r) t->state = TDS_BUSY;
	return r ? -1 : 0;
}

int tds_cancel(Tds *t)   /* an attention packet; the server ends with a DONE that has DONE_ATTN */
{
	if (t->state != TDS_BUSY) return 0;
	static const uint8_t h[8] = {6, 1, 0, 8, 0, 0, 1, 0};
	t->state = TDS_CANCEL, t->quiet = 1;
	return net_send(&t->net, h, 8);
}

int tds_pending(const Tds *t) { return t->net.snd.n > 0; }

int tds_io(Tds *t, int readable)
{
	if (net_flush(&t->net)) { t->state = TDS_DEAD; return -1; }
	if (!readable) return 0;
	if (net_read(&t->net, &t->raw)) { t->state = TDS_DEAD; return -1; }
	size_t at = 0;
	while (t->raw.n - at >= 8) {   /* whole packets into the message stream */
		size_t n = (size_t)t->raw.p[at + 2] * 256 + t->raw.p[at + 3];
		if (n < 8) { snprintf(t->net.err, sizeof t->net.err, "bad packet"); t->state = TDS_DEAD; return -1; }
		if (t->raw.n - at < n) break;
		if (put(&t->msg, t->raw.p + at + 8, n - 8)) return -1;
		t->eom |= t->raw.p[at + 1] & 1;
		at += n;
	}
	memmove(t->raw.p, t->raw.p + at, t->raw.n - at), t->raw.n -= at;
	if (t->state == TDS_PRELOGIN) {
		if (!t->eom) return 0;
		Cur c = {t->msg.p, t->msg.p + t->msg.n, 0};   /* option table: token, offset, length */
		int enc = -1;
		for (uint8_t o; !c.bad && (o = (uint8_t)num(&c, 1)) != 0xFF;) {
			const uint8_t *e = take(&c, 4);
			size_t at2 = e ? (size_t)e[0] * 256 + e[1] : 0;
			if (e && o == 1 && at2 < t->msg.n) enc = t->msg.p[at2];
		}
		if (enc != 1 && enc != 3) { snprintf(t->net.err, sizeof t->net.err, "the server cannot encrypt (encryption %d)", enc); t->state = TDS_DEAD; return -1; }
		t->state = TDS_TLS, t->msg.n = 0;
	}
	if (t->state == TDS_TLS) {   /* handshake bytes in, our reply out as one PRELOGIN message */
		Buf b = {0};
		int r = net_shake(&t->net, t->msg.p, t->msg.n, &b);
		t->msg.n = 0;
		r = r < 0 || (b.n && send_msg(t, 0x12, b.p, b.n)) ? -1 : r;
		free(b.p);
		if (r < 0) { t->state = TDS_DEAD; return -1; }
		if (r) t->net.tls = 1, t->state = TDS_LOGIN, r = send_msg(t, 0x10, t->login.p, t->login.n);
		return r || tds_io(t, 0) ? -1 : 0;
	}
	if (t->eom || t->msg.n - t->at >= 2 * t->tried)
		if (parse(t)) { t->state = TDS_DEAD; return -1; }
	return t->state == TDS_DEAD ? -1 : 0;
}

void tds_close(Tds *t)
{
	net_close(&t->net);
	free(t->raw.p), free(t->msg.p), free(t->out.p), free(t->login.p), free(t->col);
	memset(t, 0, sizeof *t);
	t->net.fd = -1;
}
