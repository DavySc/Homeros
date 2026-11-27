#include "net.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wincrypt.h>
#define SECURITY_WIN32
#include <security.h>
#else
#include <netdb.h>
#include <unistd.h>
#include <sys/socket.h>
#endif
#include <openssl/err.h>
#include <openssl/ssl.h>
#include "uni.h"

#ifdef _WIN32
#define H(fd) ((HANDLE)(INT_PTR)(fd))   /* loop.h's handle cut to int */
#define sock(c) ((SOCKET)(c)->sock)
#define blocked() (WSAGetLastError() == WSAEWOULDBLOCK || WSAGetLastError() == WSAENOTCONN)   /* ENOTCONN: still connecting */
#define why() say(WSAGetLastError())
static const char *say(long e)   /* a Winsock or SSPI error's text */
{
	static char s[160];
	if (!FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, (DWORD)e, 0, s, sizeof s, NULL)) snprintf(s, sizeof s, "error 0x%lX", (unsigned long)e);
	s[strcspn(s, "\r\n")] = 0;
	return s;
}

/* OpenSSL checks the server against the CAs Windows trusts: its ROOT
   store (curl's CURLSSLOPT_NATIVE_CA), where a domain puts its own CA;
   read once, as it cost 60 to 110 ms a login under Wine (3 runs) */
static int roots(SSL_CTX *ctx)
{
	static X509_STORE *x;
	HCERTSTORE st = x ? NULL : CertOpenSystemStoreW(0, L"ROOT");
	if (st && (x = X509_STORE_new()))
		for (PCCERT_CONTEXT c = NULL; (c = CertEnumCertificatesInStore(st, c));) {
			const unsigned char *p = c->pbCertEncoded;
			X509 *a = d2i_X509(NULL, &p, (long)c->cbCertEncoded);
			if (a) X509_STORE_add_cert(x, a);
			X509_free(a);
		}
	if (st) CertCloseStore(st, 0);
	if (x) SSL_CTX_set1_cert_store(ctx, x);
	return x != NULL;
}

/* What woke the loop: resets the socket's event, notes the connect done
   and the server's close; -1 when the connect was refused, which only this
   says.  Windows reports FD_CLOSE once, with the last bytes, and then
   never signals the event again, so the recv that says 0 would never be
   made (the review of PR 130: 15 of 18 runs on Windows Server 2025; Wine
   signals again): once it came, the event stays set until that recv */
static int woke(Net *c)
{
	WSANETWORKEVENTS ne;
	if (WSAEnumNetworkEvents(sock(c), H(c->fd), &ne)) return 0;
	if (ne.lNetworkEvents & FD_CONNECT && ne.iErrorCode[FD_CONNECT_BIT]) {
		WSASetLastError(ne.iErrorCode[FD_CONNECT_BIT]);
		snprintf(c->err, sizeof c->err, "connect: %s", why());
		return -1;
	}
	if (ne.lNetworkEvents & FD_CONNECT) c->up = 1;
	if (ne.lNetworkEvents & FD_CLOSE) c->up = 2;
	if (c->up == 2) WSASetEvent(H(c->fd));
	return 0;
}

/* has: 1 the credentials, 2 the context too */
struct Sspi { CredHandle cred; CtxtHandle ctx; int has; wchar_t spn[300]; };

/* Negotiate takes Kerberos, or NTLM where Kerberos cannot; the flags are
   those of Microsoft's Go driver (go-mssqldb winsspi.go), whose delegation
   lets a linked server log in as you (where the domain allows it) */
int net_sspi(Net *c, const char *spn, const char *in, size_t n, Str *out)
{
	struct Sspi *s = c->sspi;
	SECURITY_STATUS r = SEC_E_INSUFFICIENT_MEMORY;
	if (!s && !spn) { snprintf(c->err, sizeof c->err, "the server sent a Windows login token unasked"); return -1; }
	if (!s) {
		if (!(s = c->sspi = calloc(1, sizeof *s))) goto fail;
		MultiByteToWideChar(CP_UTF8, 0, spn, -1, s->spn, sizeof s->spn / sizeof *s->spn - 1);
		if ((r = AcquireCredentialsHandleW(NULL, L"Negotiate", SECPKG_CRED_OUTBOUND, NULL, NULL, NULL, NULL, &s->cred, NULL))) goto fail;
		s->has = 1;
	}
	SecBuffer ib = { (ULONG)n, SECBUFFER_TOKEN, (void *)in }, ob = { 0, SECBUFFER_TOKEN, NULL };
	SecBufferDesc id = { SECBUFFER_VERSION, 1, &ib }, od = { SECBUFFER_VERSION, 1, &ob };
	ULONG attr;
	r = InitializeSecurityContextW(&s->cred, s->has > 1 ? &s->ctx : NULL, s->spn,
		ISC_REQ_CONFIDENTIALITY | ISC_REQ_REPLAY_DETECT | ISC_REQ_SEQUENCE_DETECT | ISC_REQ_CONNECTION | ISC_REQ_DELEGATE | ISC_REQ_ALLOCATE_MEMORY,
		0, SECURITY_NETWORK_DREP, s->has > 1 ? &id : NULL, 0, &s->ctx, &od, &attr, NULL);
	if (r == SEC_I_COMPLETE_NEEDED || r == SEC_I_COMPLETE_AND_CONTINUE)   /* as the documentation asks */
		r = CompleteAuthToken(&s->ctx, &od) ? SEC_E_INTERNAL_ERROR : r == SEC_I_COMPLETE_NEEDED ? SEC_E_OK : SEC_I_CONTINUE_NEEDED;
	if (r == SEC_E_OK || r == SEC_I_CONTINUE_NEEDED) s->has = 2;
	int k = ob.cbBuffer && str_add(out, ob.pvBuffer, ob.cbBuffer);
	FreeContextBuffer(ob.pvBuffer);
	if (k) return -1;
	if (r == SEC_E_OK || r == SEC_I_CONTINUE_NEEDED) return r == SEC_E_OK;
fail:
	snprintf(c->err, sizeof c->err, "Windows login: %s", say(r));
	return -1;
}
#else
#define sock(c) ((c)->fd)
#define blocked() (errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOTCONN)   /* ENOTCONN: still connecting */
#define why() strerror(errno)
#define roots SSL_CTX_set_default_verify_paths
int net_sspi(Net *c, const char *spn, const char *in, size_t n, Str *out)
{
	(void)spn, (void)in, (void)n, (void)out;
	snprintf(c->err, sizeof c->err, "a Windows login needs Windows");
	return -1;
}
#endif

static int esc(unsigned u) { return u == '\t' ? 't' : u == '\n' ? 'n' : u == '\r' ? 'r' : u == '\\' ? '\\' : 0; }

int net_cp(Str *b, unsigned u, int raw)
{
	unsigned char s[4] = {'\\', raw ? 0 : (unsigned char)esc(u)};
	return s[1] ? str_add(b, (char *)s, 2) : str_add(b, (char *)s, (size_t)uni_put(s, u));
}

int net_esc(Str *b, const char *s, size_t n)
{
	size_t a = 0;
	for (size_t i = 0; i < n; i++)
		if (esc((unsigned char)s[i])) {
			if (str_add(b, s + a, i - a) || net_cp(b, (unsigned char)s[i], 0)) return -1;
			a = i + 1;
		}
	return str_add(b, s + a, n - a);
}

size_t net_plain(char *s, size_t n)
{
	size_t w = 0;
	for (size_t i = 0; i < n; i++) if ((unsigned char)s[i] < 0xFC) s[w++] = s[i];
	return w;
}
size_t net_unesc(char *s, size_t n)
{
	size_t w = 0;
	for (size_t i = 0; i < n; i++, w++)
		s[w] = s[i] == '\\' && i + 1 < n ? (s[++i] == 't' ? '\t' : s[i] == 'n' ? '\n' : s[i] == 'r' ? '\r' : s[i]) : s[i];
	return w;
}

static int fail(Net *c, const char *what)
{
	const char *why = ERR_reason_error_string(ERR_get_error());
	snprintf(c->err, sizeof c->err, "%s: %s", what, why ? why : "failed");   /* NULL with an empty error queue */
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
	struct addrinfo hint = { .ai_socktype = SOCK_STREAM, .ai_flags = AI_CANONNAME }, *ai;
	char ps[16];
	snprintf(ps, sizeof ps, "%d", port);
#ifdef _WIN32
	static WSADATA up;   /* once; the process's end cleans up */
	int e = up.wVersion ? 0 : WSAStartup(MAKEWORD(2, 2), &up);
	if (e) { snprintf(c->err, sizeof c->err, "Winsock: %s", say(e)); return -1; }
#endif
	if (getaddrinfo(host, ps, &hint, &ai)) { snprintf(c->err, sizeof c->err, "unknown host %s", host); return -1; }
	/* a dotless name grows its domain, as FreeTDS's tds_get_spn: Kerberos knows the server by it */
	snprintf(c->name, sizeof c->name, "%s", !strchr(host, '.') && ai->ai_canonname && strchr(ai->ai_canonname, '.') ? ai->ai_canonname : host);
#ifdef _WIN32
	c->sock = WSASocketW(ai->ai_family, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);   /* not a child's, as SOCK_CLOEXEC */
	WSAEVENT ev = sock(c) == INVALID_SOCKET ? NULL : WSACreateEvent();
	c->fd = ev ? (int)(INT_PTR)ev : -1;
	int r = !ev || WSAEventSelect(sock(c), ev, FD_READ | FD_WRITE | FD_CONNECT | FD_CLOSE)   /* also makes it non-blocking */
		|| (connect(sock(c), ai->ai_addr, (int)ai->ai_addrlen) && WSAGetLastError() != WSAEWOULDBLOCK);
#else
	c->fd = socket(ai->ai_family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);   /* not a child's: the session ends when we close it */
	int r = c->fd < 0 || (connect(c->fd, ai->ai_addr, ai->ai_addrlen) && errno != EINPROGRESS);
#endif
	freeaddrinfo(ai);
	if (r) { snprintf(c->err, sizeof c->err, "connect: %s", why()); return -1; }
	SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
	c->ssl = ctx && (trust || roots(ctx)) ? SSL_new(ctx) : NULL;
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
#ifdef _WIN32   /* before the connect is done (no data can be waiting yet, which this would hide) */
	if (!c->up && woke(c)) return -1;
#endif
	size_t a = 0;
	while (a < c->snd.n) {
		long k = send(sock(c), c->snd.s + a, (int)(c->snd.n - a), 0);
		if (k < 0 && blocked()) break;
		if (k <= 0) { snprintf(c->err, sizeof c->err, "send: %s", why()); return -1; }
		a += (size_t)k;
	}
	if (a) memmove(c->snd.s, c->snd.s + a, c->snd.n - a), c->snd.n -= a;   /* NULL with nothing ever sent */
	return 0;
}

int net_read(Net *c, Str *in)
{
	char s[65536];
#ifdef _WIN32
	/* flushed after the reset: a socket that turned writable since the
	   caller's flush took its FD_WRITE with it (the review of PR 130) */
	if (woke(c) || net_flush(c)) return -1;
	long k = recv(sock(c), s, sizeof s, 0);
	if (k < 0 && WSAGetLastError() == WSAEWOULDBLOCK) return 0;
#else
	long k = recv(c->fd, s, sizeof s, 0);
	if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
#endif
	if (k <= 0) { snprintf(c->err, sizeof c->err, "the server closed the connection%s%s", k ? ": " : "", k ? why() : ""); return -1; }
	if (!c->tls) return str_add(in, s, (size_t)k);
	if (BIO_write(SSL_get_rbio(c->ssl), s, (int)k) != (int)k) return fail(c, "TLS");
	int m;
	while ((m = SSL_read(c->ssl, s, sizeof s)) > 0)
		if (str_add(in, s, (size_t)m)) return -1;
	/* the server's close_notify: what came before it is parsed first (a
	   refused login's reason), the closed socket is the next read's */
	int e = SSL_get_error(c->ssl, m);
	return e == SSL_ERROR_WANT_READ || e == SSL_ERROR_ZERO_RETURN ? 0 : fail(c, "TLS");
}

void net_close(Net *c)
{
#ifdef _WIN32
	if (c->fd != -1) WSACloseEvent(H(c->fd));
	if (c->sock && sock(c) != INVALID_SOCKET) closesocket(sock(c));
	if (c->sspi && c->sspi->has > 1) DeleteSecurityContext(&c->sspi->ctx);
	if (c->sspi && c->sspi->has) FreeCredentialsHandle(&c->sspi->cred);
	free(c->sspi);
#else
	if (c->fd >= 0) close(c->fd);
#endif
	SSL_free(c->ssl);
	str_free(&c->snd);
	*c = (Net){ .fd = -1 };
}
