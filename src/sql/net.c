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
#include "uni.h"

static int esc(unsigned u) { return u == '\t' ? 't' : u == '\n' ? 'n' : u == '\r' ? 'r' : u == '\\' ? '\\' : 0; }

int net_cp(Str *b, unsigned u)
{
	unsigned char s[4] = {'\\', (unsigned char)esc(u)};
	return s[1] ? str_add(b, (char *)s, 2) : str_add(b, (char *)s, (size_t)uni_put(s, u));
}

int net_esc(Str *b, const char *s, size_t n)
{
	size_t a = 0;
	for (size_t i = 0; i < n; i++)
		if (esc((unsigned char)s[i])) {
			if (str_add(b, s + a, i - a) || net_cp(b, (unsigned char)s[i])) return -1;
			a = i + 1;
		}
	return str_add(b, s + a, n - a);
}

static int fail(Net *c, const char *what)
{
	snprintf(c->err, sizeof c->err, "%s: %s", what, ERR_reason_error_string(ERR_get_error()));
	return -1;
}

static int drain(Net *c, Str *out)   /* what TLS wants sent */
{
	char b[16384];
	for (int k; (k = BIO_read(SSL_get_wbio(c->ssl), b, sizeof b)) > 0;)
		if (str_add(out, b, (size_t)k)) return -1;
	return 0;
}

int net_open(Net *c, const char *host, int port, int trust)
{
	*c = (Net){ .fd = -1 };
	struct addrinfo hint = { .ai_socktype = SOCK_STREAM }, *ai;
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
	if (!c->tls) return str_add(&c->snd, s, n);
	if (SSL_write(c->ssl, s, (int)n) != (int)n) return fail(c, "TLS");   /* a memory BIO takes it all */
	return drain(c, &c->snd);
}

int net_shake(Net *c, const char *in, size_t n, Str *out)
{
	if (n && BIO_write(SSL_get_rbio(c->ssl), in, (int)n) != (int)n) return fail(c, "TLS");
	int r = SSL_do_handshake(c->ssl);
	if (drain(c, out)) return -1;
	if (r == 1) return 1;
	if (SSL_get_error(c->ssl, r) == SSL_ERROR_WANT_READ) return 0;
	snprintf(c->err, sizeof c->err, "TLS: %s (certificate: %s)", ERR_reason_error_string(ERR_get_error()),
		X509_verify_cert_error_string(SSL_get_verify_result(c->ssl)));
	return -1;
}

int net_flush(Net *c)
{
	size_t a = 0;
	while (a < c->snd.n) {
		ssize_t k = send(c->fd, c->snd.s + a, c->snd.n - a, 0);
		if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOTCONN)) break;   /* ENOTCONN: still connecting */
		if (k <= 0) { snprintf(c->err, sizeof c->err, "send: %s", strerror(errno)); return -1; }
		a += (size_t)k;
	}
	memmove(c->snd.s, c->snd.s + a, c->snd.n - a), c->snd.n -= a;
	return 0;
}

int net_read(Net *c, Str *in)
{
	char s[65536];
	ssize_t k = recv(c->fd, s, sizeof s, 0);
	if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
	if (k <= 0) { snprintf(c->err, sizeof c->err, "the server closed the connection%s%s", k ? ": " : "", k ? strerror(errno) : ""); return -1; }
	if (!c->tls) return str_add(in, s, (size_t)k);
	if (BIO_write(SSL_get_rbio(c->ssl), s, (int)k) != (int)k) return fail(c, "TLS");
	int m;
	while ((m = SSL_read(c->ssl, s, sizeof s)) > 0)
		if (str_add(in, s, (size_t)m)) return -1;
	return SSL_get_error(c->ssl, m) == SSL_ERROR_WANT_READ ? 0 : fail(c, "TLS");
}

void net_close(Net *c)
{
	if (c->fd >= 0) close(c->fd);
	SSL_free(c->ssl);
	str_free(&c->snd);
	*c = (Net){ .fd = -1 };
}
