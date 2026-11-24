/* net.h's wakes without a server (net_read, net_flush): a close the
   server sends with its last bytes is read in the end, and queued bytes
   are sent in the end (design/specs/Wake.tla).  On Windows one event
   stands for the socket and a reset clears what it recorded; the windows
   job runs this there, as Wine signals again where Windows does not (the
   review of PR 130). */
#include <string.h>
#include "check.h"
#include "../src/sql/net.h"
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define NB 0   /* main makes the server's end non-blocking */
#define SOCK(c) ((SOCKET)(c)->sock)
#define wait_ms(c, ms) (WaitForSingleObject((HANDLE)(INT_PTR)(c)->fd, ms) == WAIT_OBJECT_0)
#define nap(ms) Sleep(ms)
#else
#include <poll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>
#define SOCKET int
#define NB MSG_DONTWAIT
#define SOCK(c) ((c)->fd)
#define closesocket close
static int wait_ms(const Net *c, int ms)
{
	struct pollfd p = {c->fd, (short)(POLLIN | (c->snd.n ? POLLOUT : 0)), 0};
	return poll(&p, 1, ms) > 0;
}
#define nap(ms) usleep((ms) * 1000)
#endif

/* a blocking listener on 127.0.0.1, a client connecting to it, the server's end */
static SOCKET pair(Net *c)
{
	struct sockaddr_in a = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(0x7F000001) };
	socklen_t n = sizeof a;
	SOCKET l = socket(AF_INET, SOCK_STREAM, 0);
	CHECK(!bind(l, (struct sockaddr *)&a, sizeof a) && !listen(l, 1) && !getsockname(l, (struct sockaddr *)&a, &n));
	CHECK(!net_open(c, "127.0.0.1", ntohs(a.sin_port), 1));
	SOCKET s = accept(l, NULL, NULL);
	closesocket(l);
	return s;
}

/* what the server's end has, read without waiting */
static size_t drain(SOCKET s)
{
	static char b[65536];
	size_t got = 0;
	long k;
	while ((k = recv(s, b, sizeof b, NB)) > 0) got += (size_t)k;
	return got;
}

/* the server reads until the client's socket can take more */
static size_t writable(const Net *c, SOCKET s)
{
	size_t got = 0;
	for (int i = 0; i < 100; i++) {
		got += drain(s);
		fd_set w;
		FD_ZERO(&w);
		FD_SET(SOCK(c), &w);
		struct timeval t = {0, 20000};
		if (select((int)SOCK(c) + 1, NULL, &w, NULL, &t) > 0) break;
	}
	nap(300);
	return got;
}

int main(void)
{
#ifdef _WIN32
	WSADATA w;
	WSAStartup(MAKEWORD(2, 2), &w);
#endif
	/* the server's last bytes and its close, there before the first wake */
	Net c = { .fd = -1 };
	SOCKET s = pair(&c);
	static char b[1000];
	CHECK(send(s, b, sizeof b, 0) == (long)sizeof b && !closesocket(s));
	nap(200);
	Str in = {0};
	int r = 0;
	while (!r && wait_ms(&c, 3000)) r = net_read(&c, &in);
	CHECK(r == -1 && in.n == sizeof b && strstr(c.err, "closed"));
	net_close(&c);
	str_free(&in);

	/* queued until a send would block (Wine took 32 MB): the caller
	   flushes on a wake, the socket turns writable, then the read resets
	   the event (as tds.c's and pg.c's io); every wake must still send
	   what it can (Wake.tla's SendsAll) */
	s = pair(&c);
#ifdef _WIN32
	unsigned long nb = 1;
	CHECK(!ioctlsocket(s, FIONBIO, &nb));
#endif
	static char big[1 << 24];
	size_t all = 0;
	while (!c.snd.n && all < (size_t)1 << 30) {
		CHECK(!net_send(&c, big, sizeof big) && !net_flush(&c));
		all += sizeof big;
	}
	CHECK(c.snd.n);   /* a send would block */
	/* a wake from another cause while the socket is full (the server's
	   byte): the caller's flush blocks, the socket turns writable, the
	   read resets the event and must send (on Windows the write wakes'
	   flushes alone never left bytes queued: the review of PR 130) */
	CHECK(send(s, b, 1, 0) == 1 && wait_ms(&c, 3000));
	CHECK(!net_flush(&c) && c.snd.n);
	size_t got = writable(&c, s);
	CHECK(!net_read(&c, &in));
	for (;;) {
		got += drain(s);
		nap(20);
		if (got == all || !wait_ms(&c, 3000)) break;
		CHECK(!net_flush(&c));
		got += writable(&c, s);
		CHECK(!net_read(&c, &in));
	}
	CHECK(got == all && !c.snd.n);
	net_close(&c);
	closesocket(s);
	str_free(&in);   /* the server's byte */
	fprintf(stderr, "test_sql_wake: a close with the last bytes, a full socket through a reset event\n");
	return 0;
}
