/* PostgreSQL frontend/backend protocol 3.0 (the PostgreSQL 17 manual,
   chapter 53; libpq was the second reference).  Whole messages only: each
   carries its length, so nothing restarts (unlike TDS tokens). */
#include "pg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include "match.h"

static int put32(Str *b, uint32_t v) { char s[4] = {(char)(v >> 24), (char)(v >> 16), (char)(v >> 8), (char)v}; return str_add(b, s, 4); }
static uint32_t be(const char *s, int n) { uint32_t v = 0; while (n--) v = v << 8 | (unsigned char)*s++; return v; }

/* a message: type (0 for none), length, body, sealed as one TLS record,
   sent as far as the socket takes it (net_send) */
static int send_msg(Net *c, char type, const char *s, size_t n)
{
	Str b = {0};
	int r = (type && str_add(&b, &type, 1)) || put32(&b, (uint32_t)n + 4) || str_add(&b, s, n) || net_send(c, b.s, b.n);
	str_free(&b);
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
	*p = (Pg){ .state = PG_SSL, .kill.fd = -1, .port = port };
	if (net_open(&p->net, host, port, trust)) return -1;
	if (strlen(pass) >= sizeof p->pass || strlen(host) >= sizeof p->host) return dead(p, "password or host name too long");
	strcpy(p->pass, pass), strcpy(p->host, host);
	const char *kv[] = {"user", user, "database", db, "application_name", "homeros", "client_encoding", "UTF8", "DateStyle", "ISO"};
	int r = send_msg(&p->net, 0, "\x04\xD2\x16\x2F", 4) || put32(&p->start, 196608);   /* SSLRequest; protocol 3.0 */
	for (int i = 0; i < 10 && !r; i++) r = str_add(&p->start, kv[i], strlen(kv[i]) + 1);
	return r || str_add(&p->start, "", 1) ? -1 : 0;
}

int pg_query(Pg *p, const char *sql)
{
	if (p->state != PG_READY) return -1;
	p->state = PG_BUSY;
	return send_msg(&p->net, 'Q', sql, strlen(sql) + 1) || net_flush(&p->net);   /* now: Winsock wakes for writing only after a send would block (net.h) */
}

/* CancelRequest on a second connection, plain TCP as libpq 16 sends it;
   the server ends the query with an error, then ReadyForQuery */
int pg_cancel(Pg *p)
{
	if (p->state != PG_BUSY) return 0;
	Str s = {0};
	int r = str_add(&s, "\x04\xD2\x16\x2E", 4) || put32(&s, p->pid) || put32(&s, p->key);   /* 80877102 */
	p->state = PG_CANCEL, p->quiet = 1;
	r = r || net_open(&p->kill, p->host, p->port, 1) || send_msg(&p->kill, 0, s.s, s.n);
	str_free(&s);
	return r;
}

int pg_pending(const Pg *p) { return p->net.snd.n > 0 || p->kill.snd.n > 0; }

static int b64(char *d, const uint8_t *s, int n) { return EVP_EncodeBlock((unsigned char *)d, s, n); }
static void hmac(const uint8_t *k, const void *s, size_t n, uint8_t out[32]) { unsigned l = 32; HMAC(EVP_sha256(), k, 32, s, n, out, &l); }

/* SCRAM-SHA-256 (RFC 5802 and 7677), channel binding off ("n,,") */
static int sasl(Pg *p, uint32_t step, const char *s, size_t n)
{
	char m[512];
	if (step == 10) {   /* the server's mechanisms, NUL-terminated names */
		const char *q = s, *e = s + n, *z;   /* inside the message (the fuzzer) */
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
		const char *ip = it + 3;
		long iter = match_num(&ip, sf + n);
		int sn = (int)strcspn(sa + 3, ",");
		uint8_t salt[128], ck[32], sk[32], sig[32];
		int sl = sn < 4 * 42 ? EVP_DecodeBlock(salt, (const unsigned char *)sa + 3, sn) : -1;   /* 128 bytes hold its decoding */
		if (sl < 0 || iter < 1 || iter > 0x7FFFFFFF) return dead(p, "SCRAM: bad salt or iterations");
		sl -= (sn > 0 && sa[2 + sn] == '=') + (sn > 1 && sa[1 + sn] == '=');   /* EVP_DecodeBlock counts the padding */
		if (!PKCS5_PBKDF2_HMAC(p->pass, (int)strlen(p->pass), salt, sl, (int)iter, EVP_sha256(), 32, p->salted)) return dead(p, "SCRAM: PBKDF2");
		memset(p->pass, 0, sizeof p->pass);
		char fin[160], auth[640], pr[48];
		snprintf(fin, sizeof fin, "c=biws,r=%.*s", (int)strcspn(r + 2, ","), r + 2);
		int al = snprintf(auth, sizeof auth, "%s,%s,%s", p->first, sf, fin);
		hmac(p->salted, "Client Key", 10, ck);
		SHA256(ck, 32, sk);
		hmac(sk, auth, (size_t)al, sig);
		for (int i = 0; i < 32; i++) ck[i] ^= sig[i];
		b64(pr, ck, 32);
		hmac(p->salted, "Server Key", 10, sk);   /* kept to check the server's proof */
		hmac(sk, auth, (size_t)al, p->salted);
		int l = snprintf(m, sizeof m, "%s,p=%s", fin, pr);
		return send_msg(&p->net, 'p', m, (size_t)l);
	}
	if (step == 12) {   /* v=the server's signature: it knew the password too */
		char v[48];
		b64(v, p->salted, 32);
		return n == 2 + strlen(v) && !memcmp(s, "v=", 2) && !memcmp(s + 2, v, n - 2) ? 0 : dead(p, "SCRAM: the server's proof is wrong");
	}
	return dead(p, "unsupported authentication (only SCRAM-SHA-256)");
}

static int message(Pg *p, char k, const char *s, size_t n)
{
	Str *o = &p->out;
	const char *e = s + n;
	switch (k) {
	case 'R': return n < 4 ? dead(p, "bad message") : be(s, 4) == 0 ? 0 : sasl(p, be(s, 4), s + 4, n - 4);
	case 'K': if (n >= 8) p->pid = be(s, 4), p->key = be(s + 4, 4); return 0;
	case 'Z':
		p->txn = n ? s[0] : 'I';
		if (p->state != PG_DEAD && p->kill.fd >= 0) p->ready = 1;
		else if (p->state != PG_DEAD) p->state = PG_READY, p->quiet = 0;
		return 0;
	case 'T': {   /* RowDescription: the names */
		uint32_t c = n >= 2 ? be(s, 2) : 0;
		s += 2;
		for (uint32_t i = 0; i < c && s < e; i++) {
			const char *z = memchr(s, 0, (size_t)(e - s));
			if (!z || e - z < 19) return dead(p, "bad RowDescription");
			if (!p->quiet && (str_add(o, i ? "\t" : NET_HEAD, 1) || net_esc(o, s, (size_t)(z - s)))) return -1;
			s = z + 19;
		}
		p->sets++;
		return p->quiet ? 0 : str_add(o, "\n", 1);
	}
	case 'D': {   /* DataRow: the values, length -1 for NULL */
		uint32_t c = n >= 2 ? be(s, 2) : 0;
		s += 2;
		for (uint32_t i = 0; i < c; i++) {
			uint32_t l = e - s >= 4 ? be(s, 4) : 0;
			if (e - s < 4 || (s += 4, l != 0xFFFFFFFF && (size_t)(e - s) < l)) return dead(p, "bad DataRow");
			if (!p->quiet && (str_add(o, i ? "\t" : NET_ROW, 1) || (l == 0xFFFFFFFF ? str_add(o, NET_NULL, 5) : net_esc(o, s, l)))) return -1;
			if (l != 0xFFFFFFFF) s += l;
		}
		p->rows++;
		return p->quiet ? 0 : str_add(o, "\n", 1);
	}
	case 'C':   /* CommandComplete: its tag, as psql shows it */
		return p->quiet ? 0 : str_add(o, "(", 1) || net_esc(o, s, n ? n - 1 : 0) || str_add(o, ")\n", 2);
	case 'E': case 'N': {   /* severity, SQLSTATE: message (at character n) */
		const char *f[256] = {0};
		for (const char *q = s; q < e && *q;) {
			const char *z = memchr(q + 1, 0, (size_t)(e - q - 1));
			if (!z) break;
			f[(unsigned char)*q] = q + 1, q = z + 1;
		}
		if (k == 'E' && p->state < PG_READY) return dead(p, f['M'] ? f['M'] : "login failed");
		if (p->quiet && f['C'] && !strcmp(f['C'], "57014")) return 0;   /* our own cancel */
		const char *msg = f['M'] ? f['M'] : "";
		return (k == 'E' && str_add(o, NET_ERR, 1)) || str_addf(o, "%s %s: ", f['V'] ? f['V'] : "ERROR", f['C'] ? f['C'] : "") || net_esc(o, msg, strlen(msg))
			|| (f['P'] && str_addf(o, " (at character %s)", f['P'])) || (k == 'E' && str_add(o, NET_ERR, 1)) || str_add(o, "\n", 1);
	}
	}
	return 0;   /* the rest (ParameterStatus, EmptyQueryResponse, notifications...) is not shown */
}

int pg_parse(Pg *p)
{
	while (p->in.n - p->at >= 5) {
		uint32_t l = be(p->in.s + p->at + 1, 4);
		if (l < 4) return dead(p, "bad message length");
		if (p->in.n - p->at - 1 < l) break;
		if (message(p, p->in.s[p->at], p->in.s + p->at + 5, l - 4)) return dead(p, NULL);
		p->at += 1 + l;
	}
	if (p->at == p->in.n) p->in.n = p->at = 0;
	else if (p->at > 65536) memmove(p->in.s, p->in.s + p->at, p->in.n - p->at), p->in.n -= p->at, p->at = 0;
	return 0;
}

int pg_io(Pg *p, int readable)
{
	if (net_flush(&p->net) || (p->kill.fd >= 0 && net_flush(&p->kill))) return dead(p, NULL);
	/* a cancel is done when the server closes its connection, as libpq's
	   PQcancel waits for: a query sent before then could be cancelled
	   instead (the review of PR 133: 16 of 20 runs) */
	if (p->kill.fd >= 0 && !p->kill.snd.n) {
		Str x = {0};
		int gone = net_read(&p->kill, &x);
		str_free(&x);
		if (gone) net_close(&p->kill);
		if (gone && p->ready) p->state = PG_READY, p->quiet = 0, p->ready = 0;
	}
	if (!readable) return 0;
	if (net_read(&p->net, &p->in)) return dead(p, NULL);
	if (p->state == PG_SSL) {
		if (p->in.n < 1) return 0;
		if (p->in.s[0] != 'S') return dead(p, "the server cannot encrypt");
		memmove(p->in.s, p->in.s + 1, --p->in.n);
		p->state = PG_TLS;
	}
	if (p->state == PG_TLS) {   /* handshake bytes as they are, unlike TDS's */
		int r = net_shake(&p->net, p->in.s, p->in.n, &p->net.snd);
		p->in.n = 0;
		if (r < 0) return dead(p, NULL);
		if (r) p->net.tls = 1, p->state = PG_AUTH, r = send_msg(&p->net, 0, p->start.s, p->start.n);
		return r || pg_io(p, 0) ? -1 : 0;
	}
	return pg_parse(p) || pg_io(p, 0) ? -1 : 0;
}

void pg_close(Pg *p)
{
	net_close(&p->net), net_close(&p->kill);
	str_free(&p->in), str_free(&p->out), str_free(&p->start);
	*p = (Pg){ .net.fd = -1, .kill.fd = -1 };
}
