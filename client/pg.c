/* PostgreSQL frontend/backend protocol 3.0 (the PostgreSQL 17 manual,
   chapter 53; libpq was the second reference).  Whole messages only: each
   carries its length, so nothing restarts (unlike TDS tokens). */
#define _POSIX_C_SOURCE 200809L
#include "pg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

static int put32(Buf *b, uint32_t v) { uint8_t s[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v}; return buf_put(b, s, 4); }
static uint32_t be(const uint8_t *s, int n) { uint32_t v = 0; while (n--) v = v << 8 | *s++; return v; }

/* a message: type (0 for none), length, body */
static int send_msg(Net *c, char type, const void *s, size_t n)
{
	Buf b = {0};
	int r = (type && buf_c(&b, type)) || put32(&b, (uint32_t)n + 4) || buf_put(&b, s, n) || net_send(c, b.p, b.n);
	free(b.p);
	return r;
}

static int dead(Pg *p, const char *why)
{
	if (why) snprintf(p->net.err, sizeof p->net.err, "%s", why);
	p->state = PG_DEAD;
	return -1;
}

int pg_open(Pg *p, const char *host, int port, const char *user, const char *pass, const char *db, int trust)
{
	memset(p, 0, sizeof *p);
	p->state = PG_SSL, p->kill.fd = -1;
	if (net_open(&p->net, host, port, trust)) return -1;
	if (strlen(pass) >= sizeof p->pass || strlen(host) >= sizeof p->host) return dead(p, "password or host name too long");
	strcpy(p->pass, pass), strcpy(p->host, host), p->port = port;
	uint8_t ssl[4] = {0x04, 0xD2, 0x16, 0x2F};   /* SSLRequest 80877103 */
	const char *kv[] = {"user", user, "database", db, "application_name", "homeros", "client_encoding", "UTF8", "DateStyle", "ISO"};
	int r = send_msg(&p->net, 0, ssl, 4) || put32(&p->start, 196608);
	for (int i = 0; i < 10 && !r; i++) r = buf_put(&p->start, kv[i], strlen(kv[i]) + 1);
	return r || buf_c(&p->start, 0) ? -1 : 0;
}

int pg_query(Pg *p, const char *sql)
{
	if (p->state != PG_READY) return -1;
	p->state = PG_BUSY;
	return send_msg(&p->net, 'Q', sql, strlen(sql) + 1);
}

/* CancelRequest on a second connection, plain TCP as libpq 16 sends it;
   the server ends the query with an error and then ReadyForQuery */
int pg_cancel(Pg *p)
{
	if (p->state != PG_BUSY) return 0;
	uint8_t s[12] = {0x04, 0xD2, 0x16, 0x2E};   /* 80877102 */
	for (int i = 0; i < 4; i++) s[4 + i] = (uint8_t)(p->pid >> (24 - 8 * i)), s[8 + i] = (uint8_t)(p->key >> (24 - 8 * i));
	p->state = PG_CANCEL, p->quiet = 1;
	return net_open(&p->kill, p->host, p->port, 1) || send_msg(&p->kill, 0, s, 12);
}

int pg_pending(const Pg *p) { return p->net.snd.n > 0 || p->kill.snd.n > 0; }

static int b64(char *d, const uint8_t *s, int n) { return EVP_EncodeBlock((unsigned char *)d, s, n); }
static void hmac(const uint8_t *k, int kn, const void *s, size_t n, uint8_t out[32]) { unsigned l = 32; HMAC(EVP_sha256(), k, kn, s, n, out, &l); }

/* SCRAM-SHA-256 (RFC 5802/7677), channel binding off ("n,,") */
static int sasl(Pg *p, uint32_t step, const uint8_t *s, size_t n)
{
	char m[512];
	if (step == 10) {   /* the server's mechanisms */
		const uint8_t *q = s, *e = s + n, *z;   /* NUL-terminated names, inside the message (the fuzzer) */
		while (q < e && *q && !(e - q >= 14 && !memcmp(q, "SCRAM-SHA-256", 14))) q = (z = memchr(q, 0, (size_t)(e - q))) ? z + 1 : e;
		if (q >= e || !*q) return dead(p, "the server offers no SCRAM-SHA-256");
		uint8_t r[18];
		if (RAND_bytes(r, sizeof r) != 1) return dead(p, "no random bytes");
		b64(p->nonce, r, sizeof r);
		int k = snprintf(p->first, sizeof p->first, "n=,r=%s", p->nonce);
		int l = snprintf(m, sizeof m, "SCRAM-SHA-256%c%c%c%c%cn,,%s", 0, 0, 0, 0, k + 3, p->first);
		return send_msg(&p->net, 'p', m, (size_t)l);
	}
	if (step == 11) {   /* r=nonce,s=salt,i=iterations */
		char sf[256], *r, *sa, *it;
		if (n >= sizeof sf) return dead(p, "SCRAM: long server message");
		memcpy(sf, s, n), sf[n] = 0;
		if (!(r = strstr(sf, "r=")) || !(sa = strstr(sf, ",s=")) || !(it = strstr(sf, ",i=")) || strncmp(r + 2, p->nonce, strlen(p->nonce))) return dead(p, "SCRAM: bad server message");
		uint8_t salt[128], ck[32], sk[32], sig[32];
		int sl = EVP_DecodeBlock(salt, (const unsigned char *)sa + 3, (int)(strcspn(sa + 3, ",")));
		if (sl < 0) return dead(p, "SCRAM: bad salt");
		sl -= (sa[3 + strcspn(sa + 3, ",") - 1] == '=') + (sa[3 + strcspn(sa + 3, ",") - 2] == '=');
		if (!PKCS5_PBKDF2_HMAC(p->pass, (int)strlen(p->pass), salt, sl, atoi(it + 3), EVP_sha256(), 32, p->salted)) return dead(p, "SCRAM: PBKDF2");
		memset(p->pass, 0, sizeof p->pass);
		char fin[160], auth[640];
		snprintf(fin, sizeof fin, "c=biws,r=%.*s", (int)strcspn(r + 2, ","), r + 2);
		int al = snprintf(auth, sizeof auth, "%s,%s,%s", p->first, sf, fin);
		hmac(p->salted, 32, "Client Key", 10, ck);
		SHA256(ck, 32, sk);
		hmac(sk, 32, auth, (size_t)al, sig);
		for (int i = 0; i < 32; i++) ck[i] ^= sig[i];
		char pr[48];
		b64(pr, ck, 32);
		hmac(p->salted, 32, "Server Key", 10, sk);   /* kept to check the server's proof */
		hmac(sk, 32, auth, (size_t)al, p->salted);
		int l = snprintf(m, sizeof m, "%s,p=%s", fin, pr);
		return send_msg(&p->net, 'p', m, (size_t)l);
	}
	if (step == 12) {   /* v=server signature: the server knew the password too */
		char v[48];
		b64(v, p->salted, 32);
		return n == 2 + strlen(v) && !memcmp(s, "v=", 2) && !memcmp(s + 2, v, n - 2) ? 0 : dead(p, "SCRAM: the server's proof is wrong");
	}
	return dead(p, "unsupported authentication (only SCRAM-SHA-256)");
}

#define field buf_esc

static int message(Pg *p, char k, const uint8_t *s, size_t n)
{
	Buf *o = &p->out;
	const uint8_t *e = s + n;
	switch (k) {
	case 'R': return n < 4 ? dead(p, "bad message") : be(s, 4) == 0 ? 0 : sasl(p, be(s, 4), s + 4, n - 4);
	case 'K': if (n >= 8) p->pid = be(s, 4), p->key = be(s + 4, 4); return 0;
	case 'S': return 0;   /* ParameterStatus */
	case 'Z':
		p->txn = n ? (char)s[0] : 'I';
		if (p->state != PG_DEAD) p->state = PG_READY, p->quiet = 0;
		return 0;
	case 'T': {   /* RowDescription: names */
		uint32_t c = n >= 2 ? be(s, 2) : 0;
		s += 2;
		for (uint32_t i = 0; i < c && s < e; i++) {
			const uint8_t *z = memchr(s, 0, (size_t)(e - s));
			if (!z || e - z < 19) return dead(p, "bad RowDescription");
			if (!p->quiet && ((i && buf_c(o, '\t')) || field(o, s, (size_t)(z - s)))) return -1;
			s = z + 19;
		}
		p->sets++;
		return p->quiet ? 0 : buf_c(o, '\n');
	}
	case 'D': {   /* DataRow */
		uint32_t c = n >= 2 ? be(s, 2) : 0;
		s += 2;
		for (uint32_t i = 0; i < c; i++) {
			if (e - s < 4) return dead(p, "bad DataRow");
			uint32_t l = be(s, 4);
			s += 4;
			if (l != 0xFFFFFFFF && (size_t)(e - s) < l) return dead(p, "bad DataRow");
			if (!p->quiet && ((i && buf_c(o, '\t')) || (l == 0xFFFFFFFF ? buf_put(o, "NULL", 4) : field(o, s, l)))) return -1;
			if (l != 0xFFFFFFFF) s += l;
		}
		p->rows++;
		return p->quiet ? 0 : buf_c(o, '\n');
	}
	case 'C':   /* CommandComplete: its tag, as psql shows it */
		return p->quiet ? 0 : buf_c(o, '(') || field(o, s, n ? n - 1 : 0) || buf_put(o, ")\n", 2);
	case 'E': case 'N': {   /* severity, SQLSTATE: message (at character n) */
		const char *f[256] = {0};
		for (const uint8_t *q = s; q < e && *q;) {
			const uint8_t *z = memchr(q + 1, 0, (size_t)(e - q - 1));
			if (!z) break;
			f[*q] = (const char *)q + 1, q = z + 1;
		}
		if (k == 'E' && p->state < PG_READY) return dead(p, f['M'] ? f['M'] : "login failed");
		if (p->quiet && f['C'] && !strcmp(f['C'], "57014")) return 0;   /* our own cancel */
		return buf_put(o, f['V'] ? f['V'] : "ERROR", strlen(f['V'] ? f['V'] : "ERROR")) || buf_c(o, ' ')
			|| buf_put(o, f['C'] ? f['C'] : "", strlen(f['C'] ? f['C'] : "")) || buf_put(o, ": ", 2)
			|| field(o, (const uint8_t *)(f['M'] ? f['M'] : ""), strlen(f['M'] ? f['M'] : ""))
			|| (f['P'] && (buf_put(o, " (at character ", 15) || buf_put(o, f['P'], strlen(f['P'])) || buf_c(o, ')'))) || buf_c(o, '\n');
	}
	case 'I': return 0;   /* EmptyQueryResponse */
	}
	return 0;   /* others (ParameterDescription, NotificationResponse...) are not shown */
}

static int parse(Pg *p)   /* every whole message that has arrived */
{
	while (p->in.n - p->at >= 5) {
		uint32_t l = be(p->in.p + p->at + 1, 4);
		if (l < 4) return dead(p, "bad message length");
		if (p->in.n - p->at - 1 < l) break;
		if (message(p, (char)p->in.p[p->at], p->in.p + p->at + 5, l - 4)) return dead(p, NULL);
		p->at += 1 + l;
	}
	if (p->at == p->in.n) p->in.n = p->at = 0;
	else if (p->at > 65536) memmove(p->in.p, p->in.p + p->at, p->in.n - p->at), p->in.n -= p->at, p->at = 0;
	return 0;
}

int pg_io(Pg *p, int readable)
{
	if (net_flush(&p->net) || (p->kill.fd >= 0 && net_flush(&p->kill))) return dead(p, NULL);
	if (p->kill.fd >= 0 && !p->kill.snd.n) net_close(&p->kill);
	if (!readable) return 0;
	if (net_read(&p->net, &p->in)) return dead(p, NULL);
	if (p->state == PG_SSL) {
		if (p->in.n < 1) return 0;
		if (p->in.p[0] != 'S') return dead(p, "the server cannot encrypt");
		memmove(p->in.p, p->in.p + 1, --p->in.n);
		p->state = PG_TLS;
	}
	if (p->state == PG_TLS) {
		Buf b = {0};
		int r = net_shake(&p->net, p->in.p, p->in.n, &b);
		p->in.n = 0;
		r = r < 0 || buf_put(&p->net.snd, b.p, b.n) ? -1 : r;
		free(b.p);
		if (r < 0) return dead(p, NULL);
		if (r) p->net.tls = 1, p->state = PG_AUTH, r = send_msg(&p->net, 0, p->start.p, p->start.n);
		return r || pg_io(p, 0) ? -1 : 0;
	}
	return parse(p) || pg_io(p, 0) ? -1 : 0;
}

void pg_close(Pg *p)
{
	net_close(&p->net), net_close(&p->kill);
	free(p->in.p), free(p->out.p), free(p->start.p);
	memset(p, 0, sizeof *p);
	p->net.fd = p->kill.fd = -1;
}
