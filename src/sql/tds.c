/* A minimal TDS 7.4 client ([MS-TDS], read 2026-11-21; FreeTDS was the
   second reference).  The token parser restarts a token from its first
   byte when the bytes run out, so it can stop on any byte; it retries only
   after the waiting bytes have doubled (or the reply is complete), which
   keeps a large value linear. */
#include "tds.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "date.h"
#include "match.h"
#include "uni.h"

static int ch(Str *b, char c) { return str_add(b, &c, 1); }

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

/* a message as packets of the agreed size, each sealed as one TLS record
   (SQL Server closes the connection when a header is sealed alone,
   measured), sent as far as the socket takes them (net_send) */
static int send_msg(Tds *t, uint8_t type, const char *s, size_t n)
{
	size_t room = t->pkt - 8;
	uint8_t id = 1;
	do {
		size_t k = n < room ? n : room;
		char p[32768] = {(char)type, k == n, (char)((k + 8) / 256), (char)(k + 8), 0, 0, (char)id++, 0};
		memcpy(p + 8, s, k);
		if (net_send(&t->net, p, k + 8)) return -1;
		s += k, n -= k;
	} while (n);
	t->eom = 0;
	return net_flush(&t->net);
}

/* n bytes of UTF-16LE as text (net_cp's raw); a lone surrogate is U+FFFD */
static int put_u16(Str *b, const uint8_t *s, size_t n, int raw)
{
	for (size_t i = 0; i + 1 < n; i += 2) {
		unsigned u = (unsigned)le(s + i, 2), v = i + 3 < n ? (unsigned)le(s + i + 2, 2) : 0;
		if (u >= 0xD800 && u < 0xDC00 && v >= 0xDC00 && v < 0xE000) u = 0x10000 + (u - 0xD800) * 1024 + (v - 0xDC00), i += 2;
		else if (u >= 0xD800 && u < 0xE000) u = 0xFFFD;
		if (net_cp(b, u, raw)) return -1;
	}
	return 0;
}
/* single-byte text, read as code page 1252, the code page of Davy's
   servers' collations.  The server does not convert: a varchar in another
   code page (Greek_CI_AS) shows wrong (PR 133's review, measured); UTF-8
   collations arrive as nvarchar. */
static const uint16_t cp1252[32] = {
	0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD,
	0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178};
static int put_char(Str *b, const uint8_t *s, size_t n)
{
	size_t a = 0;
	for (size_t i = 0; i < n; i++)
		if (s[i] >= 128) {
			if (net_esc(b, (const char *)s + a, i - a) || net_cp(b, s[i] < 160 ? cp1252[s[i] - 128] : s[i], 0)) return -1;
			a = i + 1;
		}
	return net_esc(b, (const char *)s + a, n - a);
}
static int put_hex(Str *b, const uint8_t *s, size_t n)
{
	char h[512];   /* 256 bytes at a time: one str_addf a byte was 4 times text's cost (PR 133's review) */
	int r = str_add(b, "0x", 2);
	for (size_t i = 0; i < n && !r; i += 256) {
		size_t k = n - i < 256 ? n - i : 256;
		for (size_t j = 0; j < k; j++) h[2 * j] = "0123456789ABCDEF"[s[i + j] / 16], h[2 * j + 1] = "0123456789ABCDEF"[s[i + j] % 16];
		r = str_add(b, h, 2 * k);
	}
	return r;
}
/* an exact decimal: magnitude m (n <= 16 bytes, little-endian), scale digits after the point */
static int put_dec(Str *b, int neg, const uint8_t *m, size_t n, unsigned scale)
{
	uint32_t l[4] = {0};
	char d[48];
	unsigned nd = 0;
	for (size_t i = 0; i < n; i++) l[i / 4] += (uint32_t)m[i] << (8 * (i % 4));
	do {
		uint64_t r = 0;
		for (int k = 3; k >= 0; k--) {
			uint64_t x = r * 4294967296u + l[k];
			l[k] = (uint32_t)(x / 10), r = x % 10;
		}
		d[nd++] = (char)('0' + r);
	} while (l[0] || l[1] || l[2] || l[3]);
	while (nd <= scale) d[nd++] = '0';
	if (neg && ch(b, '-')) return -1;
	for (unsigned i = nd; i-- > 0;)
		if (ch(b, d[i]) || (i == scale && scale && ch(b, '.'))) return -1;
	return 0;
}
static int put_dbl(Str *b, double v, int single)   /* the shortest text that reads back the same */
{
	char s[40];
	for (int p = single ? 6 : 15; p <= 17; p++) {
		snprintf(s, sizeof s, "%.*g", p, v);
		if (single ? strtof(s, NULL) == (float)v : strtod(s, NULL) == v) break;
	}
	return str_addz(b, s);
}
static int put_date(Str *b, long days)   /* days since 0001-01-01 */
{
	int y, m, d;
	date_civil(days - 719162, &y, &m, &d);
	return str_addf(b, "%04d-%02d-%02d", y, m, d);
}
static uint64_t pow10u(unsigned s) { uint64_t p = 1; while (s--) p *= 10; return p; }
static int put_time(Str *b, uint64_t u, unsigned scale)   /* u: 10^-scale seconds since midnight */
{
	uint64_t p = pow10u(scale), s = u / p;
	if (str_addf(b, "%02u:%02u:%02u", (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60))) return -1;
	return scale ? str_addf(b, ".%0*llu", (int)scale, (unsigned long long)(u % p)) : 0;
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

/* one value of type ty, n bytes (not NULL), with the column's scale */
static int put_val(Str *b, unsigned scale, uint8_t ty, const uint8_t *s, size_t n)
{
	/* the lengths that would read past the value (the fuzzer); a wrong but
	   in-bounds length only shows a wrong value of a broken stream */
	int f = fixed(ty);
	if (f >= 0 ? n != (size_t)f : ty == INTN ? n != 1 && n != 2 && n != 4 && n != 8
		: ty == FLTN || ty == MONEYN || ty == DATETIMN ? n != 4 && n != 8
		: ty == BITN ? n != 1 : ty == GUID ? n != 16 : ty == DATEN ? n != 3
		: ty == DATETIME2N ? n < 3 : ty == DATETIMEOFFSETN ? n < 5
		: ty == DECIMALN || ty == NUMERICN ? n < 1 || n > 17 : ty == SSVARIANT ? n < 2 || n < 2u + s[1] : 0) return -1;
	if (scale > (ty == DECIMALN || ty == NUMERICN ? 38u : ty == TIMEN || ty == DATETIME2N || ty == DATETIMEOFFSETN ? 7u : 255u)) return -1;
	switch (ty) {
	case INT1: case INT2: case INT4: case INT8: case INTN: {
		uint64_t v = le(s, n);
		int neg = n > 1 && s[n - 1] >= 128;   /* tinyint is unsigned */
		return str_addf(b, "%s%llu", neg ? "-" : "", (unsigned long long)(neg ? (n == 8 ? ~v + 1 : (1ull << (8 * n)) - v) : v));
	}
	case BIT: case BITN: return ch(b, s[0] ? '1' : '0');
	case FLT4: case FLT8: case FLTN: {
		if (n == 4) { float x; memcpy(&x, s, 4); return put_dbl(b, x, 1); }
		double x; memcpy(&x, s, 8); return put_dbl(b, x, 0);
	}
	case MONEY: case MONEY4: case MONEYN: {   /* 8 bytes: the high half first */
		uint64_t v = n == 4 ? (uint64_t)(int64_t)(int32_t)(uint32_t)le(s, 4) : le(s, 4) << 32 | le(s + 4, 4);
		int neg = v >> 63;
		uint8_t m[8];
		for (int i = 0; i < 8; i++) m[i] = (uint8_t)((neg ? 0 - v : v) >> (8 * i));
		return put_dec(b, neg, m, 8, 4);
	}
	case DECIMALN: case NUMERICN: return put_dec(b, s[0] == 0, s + 1, n - 1, scale);
	case DATETIME: case DATETIM4: case DATETIMN:
		if (n == 4) return put_date(b, (long)le(s, 2) + 693595) || ch(b, ' ') || put_time(b, le(s + 2, 2) * 60, 0);
		/* 1/300 s, to the nearest ms as the server shows it */
		return put_date(b, (long)(int32_t)(uint32_t)le(s, 4) + 693595) || ch(b, ' ') || put_time(b, (le(s + 4, 4) * 10 + 1) / 3, 3);
	case DATEN: return put_date(b, (long)le(s, 3));
	case TIMEN: return put_time(b, le(s, n), scale);
	case DATETIME2N: return put_date(b, (long)le(s + n - 3, 3)) || ch(b, ' ') || put_time(b, le(s, n - 3), scale);
	case DATETIMEOFFSETN: {   /* UTC, then the offset in minutes: shown in local time */
		int off = (int16_t)(uint16_t)le(s + n - 2, 2), a = off < 0 ? -off : off;
		uint64_t day = 86400 * pow10u(scale), u = le(s, n - 5) + (uint64_t)((int64_t)off * 60 * (int64_t)pow10u(scale) + (int64_t)day);
		return put_date(b, (long)le(s + n - 5, 3) + (long)(u / day) - 1) || ch(b, ' ') || put_time(b, u % day, scale)
			|| str_addf(b, " %c%02d:%02d", off < 0 ? '-' : '+', a / 60, a % 60);
	}
	case GUID:
		return str_addf(b, "%08X-%04X-%04X-", (unsigned)le(s, 4), (unsigned)le(s + 4, 2), (unsigned)le(s + 6, 2))
			|| str_addf(b, "%02X%02X-%02X%02X%02X%02X%02X%02X", s[8], s[9], s[10], s[11], s[12], s[13], s[14], s[15]);
	case BIGVARCHR: case BIGCHAR: case TEXT: return put_char(b, s, n);
	case NVARCHAR: case NCHAR: case NTEXT: case XML: return put_u16(b, s, n, 0);
	case BIGVARBIN: case BIGBINARY: case IMAGE: case UDT: return put_hex(b, s, n);
	case SSVARIANT: {   /* base type, property bytes, properties, value */
		uint8_t pb = s[1];
		unsigned sc = (s[0] == DECIMALN || s[0] == NUMERICN) && pb >= 2 ? s[3]
			: (s[0] == TIMEN || s[0] == DATETIME2N || s[0] == DATETIMEOFFSETN) && pb >= 1 ? s[2] : 0;
		return put_val(b, sc, s[0], s + 2 + pb, n - 2 - pb);
	}
	}
	return -1;
}

static void name(Cur *c, int wide, Str *out)   /* B_VARCHAR (wide 0) or US_VARCHAR (1), to out if given */
{
	size_t n = (size_t)num(c, wide ? 2 : 1) * 2;
	const uint8_t *s = take(c, n);
	if (s && out) put_u16(out, s, n, 0);
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
	case BIGVARCHR: case BIGCHAR: case NVARCHAR: case NCHAR:
		k->len = (uint32_t)num(c, 2), k->plp = k->len == 0xFFFF;
		take(c, 5);   /* the collation: put_char's comment */
		return 0;
	case XML: k->plp = 1;
		if (num(c, 1)) name(c, 0, NULL), name(c, 0, NULL), name(c, 1, NULL);
		return 0;
	case UDT: k->plp = 1; k->len = (uint32_t)num(c, 2);
		name(c, 0, NULL), name(c, 0, NULL), name(c, 0, NULL), name(c, 1, NULL);
		return 0;
	case TEXT: case NTEXT: case IMAGE:
		k->len = (uint32_t)num(c, 4);
		if (k->type != IMAGE) take(c, 5);
		for (uint64_t p = num(c, 1); p > 0 && !c->bad; p--) name(c, 1, NULL);
		return 0;
	case SSVARIANT: k->len = (uint32_t)num(c, 4); return 0;
	}
	return c->bad ? 0 : -1;   /* no type byte yet is a short token, not an unknown type (the fuzzer) */
}

/* one column value of a row: its bytes, or s = NULL for NULL */
static int value(Cur *c, const TdsCol *k, Str *plp, const uint8_t **s, size_t *n)
{
	int f = fixed(k->type);
	*s = NULL;
	if (k->plp) {
		if (num(c, 8) == ~(uint64_t)0) return 0;
		plp->n = 0;
		for (uint64_t m; (m = num(c, 4)) > 0 && !c->bad;) {
			const uint8_t *p = take(c, m);
			if (p && str_add(plp, (const char *)p, m)) return -1;
		}
		*s = plp->s ? (const uint8_t *)plp->s : (const uint8_t *)"", *n = plp->n;
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
	*s = take(c, *n);
	return 0;
}

static int row(Tds *t, Cur *c, int nbc)
{
	const uint8_t *bits = nbc ? take(c, (t->ncol + 7) / 8) : NULL;
	int r = 0;
	for (size_t i = 0; i < t->ncol && !c->bad && !r; i++) {
		const uint8_t *s = NULL;
		size_t n = 0;
		if (!t->quiet && str_add(&t->out, i ? "\t" : NET_ROW, 1)) return -1;
		if (!(bits && bits[i / 8] >> (i % 8) & 1) && value(c, &t->col[i], &t->plp, &s, &n)) return -1;
		if (c->bad || t->quiet) continue;
		r = s ? put_val(&t->out, t->col[i].scale, t->col[i].type, s, n) : str_add(&t->out, NET_NULL, 5);
		if (r) snprintf(t->net.err, sizeof t->net.err, "cannot show column %zu (type 0x%02X)", i + 1, t->col[i].type);
	}
	if (r || c->bad) return r;
	t->rows++;
	return t->quiet ? 0 : ch(&t->out, '\n');
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
			if (!t->quiet && str_add(&t->out, i ? "\t" : NET_HEAD, 1)) return -1;
			name(c, 0, t->quiet ? NULL : &t->out);
		}
		if (!c->bad) t->sets++;
		return t->quiet || c->bad ? 0 : ch(&t->out, '\n');
	}
	case 0xD1: return row(t, c, 0);
	case 0xD2: return row(t, c, 1);
	case 0xFD: case 0xFE: case 0xFF: {   /* DONE, DONEPROC, DONEINPROC */
		unsigned st = (unsigned)num(c, 2);
		num(c, 2);
		uint64_t cnt = num(c, 8);
		if (c->bad) return 0;
		if (!t->quiet && str_add(&t->out, NET_END, 2)) return -1;
		if (st & 0x10 && !t->quiet && str_addf(&t->out, "(%llu row%s affected)\n", (unsigned long long)cnt, cnt == 1 ? "" : "s")) return -1;
		/* the last DONE or DONEPROC (a batch ending in EXEC) of a reply; a
		   cancel's reply ends at the one with DONE_ATTN */
		if (k != 0xFF && !(st & 1) && (t->state == TDS_BUSY || (t->state == TDS_CANCEL && st & 0x20))) t->state = TDS_READY, t->quiet = 0;
		if (k == 0xFD && !(st & 1) && t->state == TDS_LOGIN) t->state = t->net.err[0] ? TDS_DEAD : TDS_READY;
		return 0;
	}
	case 0xAA: case 0xAB: {   /* ERROR, INFO, laid out as sqlcmd does */
		size_t len = (size_t)num(c, 2);
		Cur m = {take(c, len), NULL, 0};
		if (!m.p) return 0;
		m.e = m.p + len;
		unsigned no = (unsigned)num(&m, 4), state = (unsigned)num(&m, 1), cls = (unsigned)num(&m, 1);
		size_t tn = (size_t)num(&m, 2) * 2;
		const uint8_t *tx = take(&m, tn);
		name(&m, 0, NULL), name(&m, 0, NULL);   /* server, procedure */
		unsigned line = (unsigned)num(&m, 4);
		if (m.bad) { snprintf(t->net.err, sizeof t->net.err, "a message token shorter than its fields"); return -1; }
		if (t->quiet && no == 3980) return 0;   /* our own cancel aborting a procedure (as Postgres's 57014) */
		size_t mark = t->out.n;
		if ((k == 0xAA && str_addf(&t->out, NET_ERR "Msg %u, Level %u, State %u, Line %u\n", no, cls, state, line))
			|| put_u16(&t->out, tx, tn, 1) || (k == 0xAA && str_add(&t->out, NET_ERR, 1)) || ch(&t->out, '\n')) return -1;
		if (t->state == TDS_LOGIN && k == 0xAA)
			snprintf(t->net.err, sizeof t->net.err, "%.*s", (int)(t->out.n - mark - 3), t->out.s + mark + 1);
		return 0;
	}
	case 0xE3: {   /* ENVCHANGE: packet size, transaction begin/commit/rollback */
		size_t len = (size_t)num(c, 2);
		Cur e = {take(c, len), NULL, 0};
		if (!e.p) return 0;
		e.e = e.p + len;
		uint8_t ty = (uint8_t)num(&e, 1);
		if (ty == 4) {
			Str v = {0};
			name(&e, 0, &v);
			const char *p = v.s;
			long z = p ? match_num(&p, p + v.n) : -1;
			if (z >= 512 && z <= 32767) t->pkt = (uint32_t)z;   /* [MS-TDS]'s range: send_msg's buffer holds one */
			str_free(&v);
		}
		if (ty == 8 && num(&e, 1) == 8) t->txn = num(&e, 8);
		if (ty == 9 || ty == 10) t->txn = 0;
		return 0;
	}
	case 0xED: {   /* SSPI: the Windows login's next token from the server; ours goes back as an SSPI message */
		size_t len = (size_t)num(c, 2);
		const char *p = (const char *)take(c, len);
		Str o = {0};
		int r = p && (net_sspi(&t->net, NULL, p, len, &o) < 0 || (o.n && send_msg(t, 0x11, o.s, o.n)));
		str_free(&o);
		return r ? -1 : 0;
	}
	case 0xAD: case 0xA9: case 0xA4: case 0xA5:   /* LOGINACK, ORDER, TABNAME, COLINFO */
		take(c, (size_t)num(c, 2));
		return 0;
	case 0x79: take(c, 4); return 0;                    /* RETURNSTATUS */
	case 0xE4: take(c, (size_t)num(c, 4)); return 0;    /* SESSIONSTATE */
	case 0xAE:                                          /* FEATUREEXTACK */
		while (!c->bad && num(c, 1) != 0xFF) take(c, (size_t)num(c, 4));
		return 0;
	}
	snprintf(t->net.err, sizeof t->net.err, "unknown token 0x%02X", k);
	return -1;
}

int tds_parse(Tds *t)
{
	if (!t->eom && t->msg.n - t->at < 2 * t->tried) return 0;
	while (t->at < t->msg.n) {
		Cur c = {(const uint8_t *)t->msg.s + t->at, (const uint8_t *)t->msg.s + t->msg.n, 0};
		size_t mark = t->out.n;
		uint64_t rows = t->rows;
		if (token(t, &c)) return -1;
		if (c.bad) { t->out.n = mark, t->rows = rows, t->tried = t->msg.n - t->at; break; }
		t->at = (size_t)(c.p - (const uint8_t *)t->msg.s), t->tried = 0;
	}
	/* the whole reply is here: a short token or a missing last DONE is a
	   broken stream, not a wait (a misread null bitmap hung the client 3 h) */
	if (t->eom && (t->at < t->msg.n || t->state == TDS_BUSY)) {
		snprintf(t->net.err, sizeof t->net.err, "the reply ends inside a token or before its last DONE");
		return -1;
	}
	if (t->at == t->msg.n) t->msg.n = t->at = 0;
	else if (t->at > 65536) memmove(t->msg.s, t->msg.s + t->at, t->msg.n - t->at), t->msg.n -= t->at, t->at = 0;
	return 0;
}

static int put_le(Str *b, uint64_t v, size_t n)
{
	char s[8];
	for (size_t i = 0; i < n; i++) s[i] = (char)(v >> (8 * i));
	return str_add(b, s, n);
}
static int put_utf16(Str *b, const char *z)   /* UTF-8 in, UTF-16LE out */
{
	const unsigned char *s = (const unsigned char *)z;
	size_t n = strlen(z);
	unsigned short w[2];
	for (size_t i = 0; i < n; i += uni_len(s, n, i))
		for (int m = uni_put16(w, uni_cp(s, n, i)), x = 0; x < m; x++)
			if (put_le(b, w[x], 2)) return -1;
	return 0;
}

int tds_open(Tds *t, const char *host, int port, const char *user, const char *pass, const char *db, int trust)
{
	*t = (Tds){ .pkt = 4096, .state = TDS_PRELOGIN };
	if (net_open(&t->net, host, port, trust)) return -1;
	/* PRELOGIN: version, encryption ON (always TLS) */
	static const char pre[] = {0, 0, 11, 0, 6, 1, 0, 17, 0, 1, (char)0xFF, 16, 0, 0, 0, 0, 0, 1};
	if (send_msg(t, 0x12, pre, sizeof pre)) return -1;
	/* LOGIN7, sent when the prelogin reply arrives; without a user, the
	   Windows login's first token (the server's name as FreeTDS's tds_get_spn) */
	const char *str[9] = {"homeros", user ? user : "", user ? pass : "", "homeros", host, "", "homeros", "", db};
	Str v = {0}, sp = {0}, *l = &t->login;
	char spn[300];
	snprintf(spn, sizeof spn, "MSSQLSvc/%s:%d", t->net.name, port);
	if (!user && net_sspi(&t->net, spn, NULL, 0, &sp) < 0) return -1;
	uint16_t off = 94, len[9];
	for (int i = 0; i < 9; i++) {
		size_t at = v.n;
		if (put_utf16(&v, str[i])) return -1;
		len[i] = (uint16_t)((v.n - at) / 2);
		if (i == 2)   /* the password's bytes: nibbles swapped, then xor 0xA5 */
			for (size_t j = at; j < v.n; j++) v.s[j] = (char)(((unsigned char)v.s[j] % 16 * 16 + (unsigned char)v.s[j] / 16) ^ 0xA5);
	}
	int r = put_le(l, 94 + v.n + sp.n, 4) || put_le(l, 0x74000004, 4) || put_le(l, 4096, 4) || put_le(l, 7, 4) || put_le(l, (uint64_t)getpid(), 4)
		/* USE_DB, INIT_DB_FATAL, SET_LANG; INIT_LANG_FATAL, ODBC (SSMS's ANSI settings), INT_SECURITY for the Windows login */
		|| put_le(l, 0, 4) || put_le(l, user ? 0x0003E0 : 0x0083E0, 4)
		|| put_le(l, 0, 4) || put_le(l, 0x409, 4);
	for (int i = 0; i < 9 && !r; i++) r = put_le(l, off, 2) || put_le(l, len[i], 2), off = (uint16_t)(off + 2 * len[i]);
	/* client ID; SSPI (its length below 64 KB: Windows's MaxTokenSize is 48,000); no file to attach, no new password */
	r = r || str_add(l, "\0\0\0\0\0\0", 6) || put_le(l, off + sp.n * 65536, 4) || put_le(l, off + sp.n, 4) || put_le(l, off + sp.n, 4)
		|| put_le(l, 0, 4) || str_add(l, v.s, v.n) || str_add(l, sp.s, sp.n);
	memset(v.s, 0, v.n), str_free(&v), str_free(&sp);
	return r ? -1 : 0;
}

int tds_batch(Tds *t, const char *sql)
{
	if (t->state != TDS_READY) return -1;
	Str b = {0};   /* ALL_HEADERS: the transaction descriptor, one request outstanding */
	int r = put_le(&b, 22, 4) || put_le(&b, 18, 4) || put_le(&b, 2, 2) || put_le(&b, t->txn, 8) || put_le(&b, 1, 4)
		|| put_utf16(&b, sql) || send_msg(t, 0x01, b.s, b.n);
	str_free(&b);
	if (!r) t->state = TDS_BUSY;
	return r ? -1 : 0;
}

int tds_cancel(Tds *t)
{
	if (t->state != TDS_BUSY) return 0;
	static const char h[8] = {6, 1, 0, 8, 0, 0, 1, 0};
	t->state = TDS_CANCEL, t->quiet = 1;
	return net_send(&t->net, h, 8) || net_flush(&t->net) ? -1 : 0;
}

int tds_pending(const Tds *t) { return t->net.snd.n > 0; }

static int io(Tds *t, int readable)
{
	if (net_flush(&t->net)) return -1;
	if (!readable) return 0;
	if (net_read(&t->net, &t->raw)) return -1;
	size_t at = 0;
	while (t->raw.n - at >= 8) {   /* whole packets into the message stream */
		const uint8_t *h = (const uint8_t *)t->raw.s + at;
		size_t n = (size_t)h[2] * 256 + h[3];
		if (n < 8) { snprintf(t->net.err, sizeof t->net.err, "bad packet"); return -1; }
		if (t->raw.n - at < n) break;
		if (str_add(&t->msg, (const char *)h + 8, n - 8)) return -1;
		t->eom |= h[1] & 1;
		at += n;
	}
	if (at) memmove(t->raw.s, t->raw.s + at, t->raw.n - at), t->raw.n -= at;   /* raw.s is NULL before the first bytes */
	if (t->state == TDS_PRELOGIN) {
		if (!t->eom) return 0;
		Cur c = {(const uint8_t *)t->msg.s, (const uint8_t *)t->msg.s + t->msg.n, 0};   /* options: token, offset, length */
		int enc = -1;
		for (uint8_t o; !c.bad && (o = (uint8_t)num(&c, 1)) != 0xFF;) {
			const uint8_t *e = take(&c, 4);
			size_t a = e ? (size_t)e[0] * 256 + e[1] : 0;
			if (e && o == 1 && a < t->msg.n) enc = (uint8_t)t->msg.s[a];
		}
		if (enc != 1 && enc != 3) { snprintf(t->net.err, sizeof t->net.err, "the server cannot encrypt (encryption %d)", enc); return -1; }
		t->state = TDS_TLS, t->msg.n = 0;
	}
	if (t->state == TDS_TLS) {   /* handshake bytes in, ours out as one PRELOGIN message */
		Str b = {0};
		int r = net_shake(&t->net, t->msg.s, t->msg.n, &b);
		t->msg.n = 0;
		r = r < 0 || (b.n && send_msg(t, 0x12, b.s, b.n)) ? -1 : r;
		str_free(&b);
		if (r > 0) {   /* LOGIN7 holds the password: not kept past its sending */
			t->net.tls = 1, t->state = TDS_LOGIN, r = send_msg(t, 0x10, t->login.s, t->login.n);
			memset(t->login.s, 0, t->login.n), str_free(&t->login);
		}
		return r;
	}
	int was = t->state;
	if (tds_parse(t)) return -1;
	/* SSMS's session: ARITHABORT on as well as the login's ANSI options
	   (@@options 5496, read in SSMS's defaults; the login alone gives 5432, measured) */
	return was == TDS_LOGIN && t->state == TDS_READY ? tds_batch(t, "set arithabort on") : 0;
}

int tds_io(Tds *t, int readable)
{
	if (io(t, readable) || t->state == TDS_DEAD) { t->state = TDS_DEAD; return -1; }
	return 0;
}

void tds_close(Tds *t)
{
	net_close(&t->net);
	str_free(&t->raw), str_free(&t->msg), str_free(&t->out), str_free(&t->login), str_free(&t->plp);
	free(t->col);
	*t = (Tds){ .net.fd = -1 };
}
