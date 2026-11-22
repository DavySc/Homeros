/* homeros-infoview: serves VS Code's infoview page (@leanprover/infoview)
   to a browser and carries its calls to the editor and the editor's events
   back (bridge.h).  A port of kittymax's kitty-lean-webview.el, its HTTP
   half: everything here is transport, everything the calls mean is the
   editor's (src/infoview/infoview.c).  A process of its own on the editor's
   pipe, so the editor holds no socket: loop.h queues writes to a child but
   cannot wait for a socket to take more, and a page that stops reading
   must never stall the editor.

   The wire to the browser, plain HTTP/1.1 on 127.0.0.1, every path under a
   secret token (kittymax's):
     GET  /TOKEN/...            the page's files, from ROOT
     GET  /TOKEN/events?page=P  Server-Sent Events, the editor to page P
     POST /TOKEN/call?page=P    an EditorApi call {id, m, a}; answered at
                                once with 204, its result later on P's events
   Results come on the event stream, not as the POST's answer, so a request
   Lean is slow to answer never holds one of the browser's six connections
   to a host.  An event for a page whose stream is down waits in its outbox;
   a page down for PAGE_TIMEOUT seconds is forgotten, and told to reload if
   it comes back (design/specs/Infoview.tla).

   The pipe, one line per message:
     stdin   "P JSON"  an event for page P      stdout  "P JSON"  P's call
             "* JSON"  an event for every page          "+ P"     P's stream opened
             "!"       open the page in a browser       "- P"     P forgotten
   The editor's JSON has no line breaks (it turns them into spaces); a
   call's body is JSON.stringify's, which has none, and any that a client
   sends anyway become spaces too.

   Usage: homeros-infoview ROOT OPENER...  ROOT holds index.html, host.js,
   theme.css and dist/ (the npm package's); OPENER is run with a private
   file that sends the browser to the page, so the token is never on a
   command line (ps shows those to every user). */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "str.h"

enum { MAXC = 64, MAXPAGE = 32, FORGOTTEN = 32, HEAD_MAX = 16384, BODY_MAX = 64 << 20 };
#ifndef PAGE_TIMEOUT
#define PAGE_TIMEOUT 60            /* seconds, kittymax's kitty-lean-webview-page-timeout */
#endif

extern char **environ;


typedef struct Page {
	char id[65];
	struct Conn *stream;       /* NULL: down (or never opened) */
	Str outbox;                /* events while down, as SSE chunks */
	long long gone;            /* when its stream went down */
} Page;

typedef struct Conn {
	int fd;                    /* -1: free */
	Str in, out;
	size_t off;                /* bytes of out already sent */
	Page *page;                /* the page this is the event stream of */
	int close;                 /* close once out is sent */
} Conn;

static Conn c[MAXC];
static Page *pages[MAXPAGE];
static char forgotten[FORGOTTEN][65];
static int nforgotten;
static char token[65], *root, redirect[64];
static char **opener;
static Str line, toed;         /* stdin not yet a line; stdout not yet taken */

static long long now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

/* To the editor, in order, queued: stdout is non-blocking as the sockets
   are, so a busy editor never stops the pages' streams. */
static void to_editor(const char *a, const char *b, size_t n)
{
	str_add(&toed, a, strlen(a));
	str_add(&toed, b, n);
	str_add(&toed, "\n", 1);
}

/* --- pages ---------------------------------------------------------------- */

static Page *find(const char *id)
{
	for (int i = 0; i < MAXPAGE; i++) if (pages[i] && !strcmp(pages[i]->id, id)) return pages[i];
	return NULL;
}

static int was_forgotten(const char *id)
{
	for (int i = 0; i < FORGOTTEN; i++) if (!strcmp(forgotten[i], id)) return 1;
	return 0;
}

/* The page, made when new; NULL when the table is full. */
static Page *page(const char *id)
{
	Page *p = find(id);
	if (p) return p;
	for (int i = 0; i < MAXPAGE; i++)
		if (!pages[i]) {
			if (!(p = pages[i] = calloc(1, sizeof *p))) return NULL;
			strcpy(p->id, id);
			p->gone = now();
			return p;
		}
	return NULL;
}

static void conn_close(Conn *k);

/* Gone: its RPC sessions end with it (the editor stops their keep-alives
   at "- P"), what waited for it is dropped, and it reloads if it is back. */
static void forget(Page *p)
{
	if (p->stream) conn_close(p->stream);
	to_editor("- ", p->id, strlen(p->id));
	strcpy(forgotten[nforgotten++ % FORGOTTEN], p->id);
	for (int i = 0; i < MAXPAGE; i++) if (pages[i] == p) pages[i] = NULL;
	free(p->outbox.s);
	free(p);
}

/* One SSE event as one HTTP chunk. */
static void chunk(Str *b, const char *json, size_t n)
{
	str_addf(b, "%zx\r\ndata: ", n + 8);
	str_add(b, json, n);
	str_add(b, "\n\n\r\n", 4);
}

static void send_page(Page *p, const char *json, size_t n)
{
	chunk(p->stream ? &p->stream->out : &p->outbox, json, n);
}

static const char stream_head[] =
	/* chunked: WebKit (libsoup) holds back a stream that has neither a
	   length nor chunks (kittymax) */
	"HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-store\r\n"
	"Transfer-Encoding: chunked\r\n\r\n";

static void open_stream(Conn *k, const char *id)
{
	str_add(&k->out, stream_head, sizeof stream_head - 1);
	if (was_forgotten(id)) {
		/* its subscriptions and waiting answers are gone: start afresh */
		static const char reload[] = "{\"reload\":true}";
		chunk(&k->out, reload, sizeof reload - 1);
		str_add(&k->out, "0\r\n\r\n", 5);
		k->close = 1;
		return;
	}
	Page *p = page(id);
	if (!p) { k->close = 1; return; }
	if (p->stream) conn_close(p->stream);
	p->stream = k;
	k->page = p;
	str_add(&k->out, "3\r\n:\n\n\r\n", 8);   /* a comment: the browser sees the stream is up */
	str_add(&k->out, p->outbox.s, p->outbox.n);
	p->outbox.n = 0;
	to_editor("+ ", id, strlen(id));
}

/* --- HTTP ----------------------------------------------------------------- */

static void respond(Conn *k, const char *status, const char *type, const char *body, size_t n)
{
	str_addf(&k->out, "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nCache-Control: no-store\r\n\r\n",
	     status, type, n);
	str_add(&k->out, body, n);
}

static const char *type_of(const char *name)
{
	static const char *const t[][2] = {
		{ ".js", "text/javascript; charset=utf-8" }, { ".css", "text/css; charset=utf-8" },
		{ ".html", "text/html; charset=utf-8" }, { ".ttf", "font/ttf" },
	};
	size_t n = strlen(name);
	for (size_t i = 0; i < sizeof t / sizeof *t; i++) {
		size_t m = strlen(t[i][0]);
		if (n >= m && !strcmp(name + n - m, t[i][0])) return t[i][1];
	}
	return "application/octet-stream";
}

/* Only the page's own files: plain relative names under ROOT, no "..",
   nothing absolute (kittymax's test: dist/../../etc/passwd,
   dist//etc/passwd and dist/~/.bashrc are 404). */
static int safe(const char *name)
{
	if (!*name || *name == '/' || *name == '.' || *name == '~') return 0;
	for (const char *s = name; *s; s++)
		if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') ||
		      *s == '_' || *s == '-' || *s == '.' || *s == '/'))
			return 0;
	/* ".." can only follow a "/" or start the name: both refused */
	return !strstr(name, "//") && !strstr(name, "/.");
}

static void serve(Conn *k, const char *name)
{
	char path[4096];
	struct stat st;
	int fd = -1;
	if (!*name) name = "index.html";
	if (safe(name) && snprintf(path, sizeof path, "%s/%s", root, name) < (int)sizeof path)
		fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode)) {
		if (fd >= 0) close(fd);
		respond(k, "404 Not Found", "text/plain", "", 0);
		return;
	}
	str_addf(&k->out, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %lld\r\nCache-Control: no-store\r\n\r\n",
	     type_of(name), (long long)st.st_size);
	char b[65536];
	for (off_t left = st.st_size; left > 0; ) {
		ssize_t r = read(fd, b, left < (off_t)sizeof b ? (size_t)left : sizeof b);
		if (r <= 0 || str_add(&k->out, b, (size_t)r)) { k->close = 1; break; }   /* shorter than it said: the length is a lie, so close */
		left -= r;
	}
	close(fd);
}

/* The value of ?page= in q: letters, digits and '-' (crypto.randomUUID),
   1 to 64 of them; NULL if not. */
static const char *page_of(const char *q, char id[65])
{
	const char *s = strstr(q, "?page=");
	if (!s) return NULL;
	s += 6;
	size_t n = 0;
	while (n < 64 && ((s[n] >= 'a' && s[n] <= 'z') || (s[n] >= 'A' && s[n] <= 'Z') ||
	                  (s[n] >= '0' && s[n] <= '9') || s[n] == '-'))
		n++;
	if (!n || (s[n] && s[n] != '&')) return NULL;
	memcpy(id, s, n);
	id[n] = 0;
	return id;
}

static void request(Conn *k, char *head, char *body, size_t n)
{
	char *sp = strchr(head, ' '), *path = sp ? sp + 1 : NULL, *e = path ? strchr(path, ' ') : NULL, id[65];
	if (!e) { respond(k, "400 Bad Request", "text/plain", "", 0); k->close = 1; return; }
	*sp = *e = 0;
	size_t t = strlen(token);
	if (path[0] != '/' || strncmp(path + 1, token, t) || path[t + 1] != '/') {
		respond(k, "404 Not Found", "text/plain", "", 0);
		return;
	}
	char *rest = path + t + 2, *q = strchr(rest, '?');
	int get = !strcmp(head, "GET");
	if (q) *q = 0;
	if (get && !strcmp(rest, "events")) {
		if (q) *q = '?';
		if (!q || !page_of(q, id)) respond(k, "400 Bad Request", "text/plain", "", 0);
		else open_stream(k, id);
	} else if (!strcmp(head, "POST") && !strcmp(rest, "call")) {
		if (q) *q = '?';
		if (!q || !page_of(q, id)) { respond(k, "400 Bad Request", "text/plain", "", 0); return; }
		respond(k, "204 No Content", "text/plain", "", 0);
		/* a forgotten page's calls are dropped: it reloads when its stream is back */
		if (was_forgotten(id) || !page(id)) return;
		for (size_t i = 0; i < n; i++) if (body[i] == '\n' || body[i] == '\r') body[i] = ' ';
		char pre[68];
		snprintf(pre, sizeof pre, "%s ", id);
		to_editor(pre, body, n);
	} else if (get) serve(k, rest);
	else respond(k, "405 Method Not Allowed", "text/plain", "", 0);
}

/* Every complete request in k->in; 0, or -1 to close (a head too long). */
static int requests(Conn *k)
{
	size_t at = 0;
	while (!k->close) {
		char *h = k->in.s + at, *e = strstr(h, "\r\n\r\n");
		if (!e) { if (k->in.n - at > HEAD_MAX) return -1; break; }
		size_t hn = (size_t)(e - h), len = 0;
		for (char *l = h; l && l < e; l = strstr(l, "\r\n"), l = l ? l + 2 : l)
			if (!strncasecmp(l, "Content-Length:", 15)) len = strtoul(l + 15, NULL, 10);
		if (len > BODY_MAX) return -1;
		/* a body is waited for (and held) only under the token */
		size_t t = strlen(token);
		char *sp = memchr(h, ' ', hn);
		if (len && !(sp && sp[1] == '/' && !strncmp(sp + 2, token, t) && sp[t + 2] == '/')) return -1;
		if (k->in.n - at < hn + 4 + len) break;
		char *eol = strstr(h, "\r\n");
		*eol = 0;                     /* the request line */
		request(k, h, h + hn + 4, len);
		at += hn + 4 + len;
		if (k->page) break;           /* an event stream reads nothing more */
	}
	memmove(k->in.s, k->in.s + at, k->in.n - at);
	k->in.n -= at;
	if (k->in.s) k->in.s[k->in.n] = 0;
	return 0;
}

static void conn_close(Conn *k)
{
	if (k->page) {
		k->page->stream = NULL;
		k->page->gone = now();
	}
	close(k->fd);
	free(k->in.s);
	free(k->out.s);
	memset(k, 0, sizeof *k);
	k->fd = -1;
}

/* --- the editor's side ------------------------------------------------- */

static void open_browser(void)
{
	pid_t pid;
	int n = 0;
	while (opener[n]) n++;
	char **argv = calloc((size_t)n + 2, sizeof *argv), url[80];
	if (!argv) return;
	memcpy(argv, opener, (size_t)n * sizeof *argv);
	snprintf(url, sizeof url, "file://%s", redirect);
	argv[n] = url;
	/* not the editor's pipe: what a browser prints would be read as lines */
	posix_spawn_file_actions_t fa;
	posix_spawn_file_actions_init(&fa);
	posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
	posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
	posix_spawnp(&pid, argv[0], &fa, NULL, argv, environ);   /* reaped: SIGCHLD ignored */
	posix_spawn_file_actions_destroy(&fa);
	free(argv);
}

static void from_editor(void *ctx, char *l, size_t n)
{
	(void)ctx;
	if (n == 1 && *l == '!') { open_browser(); return; }
	char *sp = memchr(l, ' ', n);
	if (!sp) return;
	*sp = 0;
	char *json = sp + 1;
	size_t jn = n - (size_t)(json - l);
	for (size_t i = 0; i < jn; i++) if (json[i] == '\r') json[i] = ' ';
	if (!strcmp(l, "*")) {
		for (int i = 0; i < MAXPAGE; i++) if (pages[i]) send_page(pages[i], json, jn);
	} else {
		Page *p = find(l);     /* none: forgotten while the editor was answering */
		if (p) send_page(p, json, jn);
	}
}

/* --- start ------------------------------------------------------------- */

static int listener(int *port)
{
	struct sockaddr_in a = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
	socklen_t al = sizeof a;
	int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (fd < 0 || bind(fd, (struct sockaddr *)&a, sizeof a) || listen(fd, 16) ||
	    getsockname(fd, (struct sockaddr *)&a, &al))
		return -1;
	*port = ntohs(a.sin_port);
	return fd;
}

/* The private file the browser opens: it holds the address (and so the
   token), readable only by you (mkstemp makes it 0600). */
static int write_redirect(int port)
{
	const char *dir = getenv("XDG_RUNTIME_DIR");
	if (!dir || strlen(dir) > 39) dir = "/tmp";
	snprintf(redirect, sizeof redirect, "%s/homeros-infoview-XXXXXX", dir);
	int fd = mkstemp(redirect);
	if (fd < 0) return -1;
	char html[256];
	int n = snprintf(html, sizeof html,
	                 "<!DOCTYPE html><meta http-equiv=\"refresh\" content=\"0; url=http://127.0.0.1:%d/%s/\">\n",
	                 port, token);
	int ok = write(fd, html, (size_t)n) == n;
	close(fd);
	return ok ? 0 : -1;
}

int main(int argc, char **argv)
{
	unsigned char r[32];
	int port, lfd;
	if (argc < 3) { fprintf(stderr, "usage: homeros-infoview ROOT OPENER...\n"); return 2; }
	root = argv[1];
	opener = argv + 2;
	signal(SIGPIPE, SIG_IGN);
	signal(SIGCHLD, SIG_IGN);
	if (getrandom(r, sizeof r, 0) != sizeof r) return 1;
	for (int i = 0; i < 32; i++) snprintf(token + 2 * i, 3, "%02x", r[i]);
	if ((lfd = listener(&port)) < 0 || write_redirect(port)) return 1;
	for (int i = 0; i < MAXC; i++) c[i].fd = -1;
	fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
	fcntl(1, F_SETFL, fcntl(1, F_GETFL) | O_NONBLOCK);
	to_editor(redirect, "", 0);
	for (;;) {
		struct pollfd f[MAXC + 3];
		int nf = 0, timeout = -1;
		long long t = now();
		/* the only timer: the page that goes stale first; none while every
		   page is connected, so an idle bridge never wakes */
		for (int i = 0; i < MAXPAGE; i++)
			if (pages[i] && !pages[i]->stream) {
				long long due = pages[i]->gone + PAGE_TIMEOUT * 1000LL - t;
				if (due <= 0) { forget(pages[i]); continue; }
				if (timeout < 0 || due < timeout) timeout = (int)due;
			}
		f[nf++] = (struct pollfd){ 0, POLLIN, 0 };
		f[nf++] = (struct pollfd){ 1, toed.n ? POLLOUT : 0, 0 };
		f[nf++] = (struct pollfd){ lfd, POLLIN, 0 };
		for (int i = 0; i < MAXC; i++)
			f[nf++] = (struct pollfd){ c[i].fd, (short)(POLLIN | (c[i].out.n > c[i].off ? POLLOUT : 0)), 0 };
		if (poll(f, (nfds_t)nf, timeout) < 0 && errno != EINTR) return 1;
		if (f[0].revents) {
			char b[65536];
			ssize_t n = read(0, b, sizeof b);
			if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) break;   /* the editor is gone */
			if (n > 0) str_lines(&line, b, (size_t)n, from_editor, NULL);
		}
		if (f[1].revents && toed.n) {
			ssize_t n = write(1, toed.s, toed.n);
			if (n < 0 && errno != EAGAIN && errno != EINTR) break;
			if (n > 0) { memmove(toed.s, toed.s + n, toed.n - (size_t)n); toed.n -= (size_t)n; }
		}
		if (f[2].revents) {
			int fd = accept(lfd, NULL, NULL);
			if (fd >= 0) { fcntl(fd, F_SETFL, O_NONBLOCK); fcntl(fd, F_SETFD, FD_CLOEXEC); }
			int i = 0;
			while (i < MAXC && c[i].fd >= 0) i++;
			if (fd >= 0 && i == MAXC) close(fd);
			else if (fd >= 0) c[i].fd = fd;
		}
		for (int i = 0; i < MAXC; i++) {
			Conn *k = &c[i];
			short ev = f[3 + i].revents;
			if (k->fd < 0 || !ev) continue;
			if (ev & POLLIN) {
				char b[65536];
				ssize_t n = recv(k->fd, b, sizeof b, 0);
				if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) { conn_close(k); continue; }
				if (n > 0 && !k->page && (str_add(&k->in, b, (size_t)n) || requests(k))) { conn_close(k); continue; }
			}
			if (ev & (POLLERR | POLLHUP) && !(ev & POLLIN)) { conn_close(k); continue; }
		}
		for (int i = 0; i < MAXC; i++) {
			Conn *k = &c[i];
			if (k->fd < 0 || k->out.n == k->off) continue;
			/* written as soon as queued: most events go out on the turn they come */
			ssize_t n = send(k->fd, k->out.s + k->off, k->out.n - k->off, MSG_NOSIGNAL);
			if (n < 0 && errno != EAGAIN && errno != EINTR) { conn_close(k); continue; }
			if (n > 0) k->off += (size_t)n;
			if (k->off == k->out.n) {
				k->off = k->out.n = 0;
				if (k->close) conn_close(k);
			}
		}
	}
	unlink(redirect);
	return 0;
}
