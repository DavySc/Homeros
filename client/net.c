#define _POSIX_C_SOURCE 200809L
#include "net.h"
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

int buf_put(Buf *b, const void *s, size_t n)
{
	if (b->n + n > b->cap) {
		size_t cap = b->cap ? b->cap : 4096;
		while (cap < b->n + n) cap *= 2;
		uint8_t *p = realloc(b->p, cap);
		if (!p) return -1;
		b->p = p, b->cap = cap;
	}
	if (n) memcpy(b->p + b->n, s, n);
	b->n += n;
	return 0;
}

int buf_c(Buf *b, int c) { uint8_t x = (uint8_t)c; return buf_put(b, &x, 1); }

/* code point u as UTF-8, with \t \n \r \\ escaped: one value per cell, one row per line */
int buf_cp(Buf *b, uint32_t u)
{
	uint8_t s[4];
	if (u == '\t' || u == '\n' || u == '\r' || u == '\\')
		return buf_c(b, '\\') || buf_c(b, u == '\t' ? 't' : u == '\n' ? 'n' : u == '\r' ? 'r' : '\\');
	if (u < 0x80) return buf_c(b, (int)u);
	if (u < 0x800) { s[0] = 0xC0 | u >> 6; s[1] = 0x80 | (u & 63); return buf_put(b, s, 2); }
	if (u < 0x10000) { s[0] = 0xE0 | u >> 12; s[1] = 0x80 | (u >> 6 & 63); s[2] = 0x80 | (u & 63); return buf_put(b, s, 3); }
	s[0] = 0xF0 | u >> 18; s[1] = 0x80 | (u >> 12 & 63); s[2] = 0x80 | (u >> 6 & 63); s[3] = 0x80 | (u & 63);
	return buf_put(b, s, 4);
}

int buf_esc(Buf *b, const uint8_t *s, size_t n)
{
	size_t a = 0;
	for (size_t i = 0; i < n; i++)
		if (s[i] == '\t' || s[i] == '\n' || s[i] == '\r' || s[i] == '\\') {
			if (buf_put(b, s + a, i - a) || buf_cp(b, s[i])) return -1;
			a = i + 1;
		}
	return buf_put(b, s + a, n - a);
}

static int fail(Net *c, const char *what)
{
	snprintf(c->err, sizeof c->err, "%s: %s", what, ERR_reason_error_string(ERR_get_error()));
	return -1;
}

static int drain(Net *c, Buf *out)   /* what TLS wants sent */
{
	uint8_t b[16384];
	for (int k; (k = BIO_read(SSL_get_wbio(c->ssl), b, sizeof b)) > 0;)
		if (buf_put(out, b, (size_t)k)) return -1;
	return 0;
}

int net_open(Net *c, const char *host, int port, int trust)
{
	memset(c, 0, sizeof *c);
	c->fd = -1;
	struct addrinfo hint = {.ai_socktype = SOCK_STREAM}, *ai;
	char ps[16];
	snprintf(ps, sizeof ps, "%d", port);
	if (getaddrinfo(host, ps, &hint, &ai)) { snprintf(c->err, sizeof c->err, "unknown host %s", host); return -1; }
	c->fd = socket(ai->ai_family, SOCK_STREAM, 0);
	int r = c->fd < 0 || fcntl(c->fd, F_SETFL, O_NONBLOCK) || (connect(c->fd, ai->ai_addr, ai->ai_addrlen) && errno != EINPROGRESS);
	freeaddrinfo(ai);
	if (r) { snprintf(c->err, sizeof c->err, "connect: %s", strerror(errno)); return -1; }
	SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
	c->ssl = ctx && (trust || SSL_CTX_set_default_verify_paths(ctx)) ? SSL_new(ctx) : NULL;
	SSL_CTX_free(ctx);   /* the SSL holds its own reference */
	BIO *rb = BIO_new(BIO_s_mem()), *wb = BIO_new(BIO_s_mem());
	if (!c->ssl || !rb || !wb || !SSL_set1_host(c->ssl, host)) { BIO_free(rb), BIO_free(wb); return fail(c, "TLS setup"); }
	SSL_set_bio(c->ssl, rb, wb);
	SSL_set_connect_state(c->ssl);
	SSL_set_verify(c->ssl, trust ? SSL_VERIFY_NONE : SSL_VERIFY_PEER, NULL);
	return 0;
}

int net_send(Net *c, const void *s, size_t n)
{
	if (!c->tls) return buf_put(&c->snd, s, n);
	if (SSL_write(c->ssl, s, (int)n) != (int)n) return fail(c, "TLS");   /* a memory BIO takes it all */
	return drain(c, &c->snd);
}

int net_shake(Net *c, const uint8_t *in, size_t n, Buf *out)
{
	if (n && BIO_write(SSL_get_rbio(c->ssl), in, (int)n) != (int)n) return fail(c, "TLS");
	int r = SSL_do_handshake(c->ssl);
	if (drain(c, out)) return -1;
	if (r == 1) return 1;   /* the caller sends out, then sets tls */
	if (SSL_get_error(c->ssl, r) == SSL_ERROR_WANT_READ) return 0;
	snprintf(c->err, sizeof c->err, "TLS: %s (certificate: %s)", ERR_reason_error_string(ERR_get_error()), X509_verify_cert_error_string(SSL_get_verify_result(c->ssl)));
	return -1;
}

int net_flush(Net *c)
{
	while (c->snd.n) {
		ssize_t k = write(c->fd, c->snd.p, c->snd.n);
		if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOTCONN)) break;
		if (k <= 0) { snprintf(c->err, sizeof c->err, "write: %s", strerror(errno)); return -1; }
		memmove(c->snd.p, c->snd.p + k, c->snd.n - (size_t)k), c->snd.n -= (size_t)k;
	}
	return 0;
}

int net_read(Net *c, Buf *in)
{
	uint8_t s[65536];
	ssize_t k = read(c->fd, s, sizeof s);
	if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
	if (k <= 0) { snprintf(c->err, sizeof c->err, "the server closed the connection%s%s", k ? ": " : "", k ? strerror(errno) : ""); return -1; }
	if (!c->tls) return buf_put(in, s, (size_t)k);
	if (BIO_write(SSL_get_rbio(c->ssl), s, (int)k) != (int)k) return fail(c, "TLS");
	int m;
	while ((m = SSL_read(c->ssl, s, sizeof s)) > 0)
		if (buf_put(in, s, (size_t)m)) return -1;
	return SSL_get_error(c->ssl, m) == SSL_ERROR_WANT_READ ? 0 : fail(c, "TLS");
}

void net_close(Net *c)
{
	if (c->fd >= 0) close(c->fd);
	SSL_free(c->ssl);
	free(c->snd.p);
	memset(c, 0, sizeof *c);
	c->fd = -1;
}
