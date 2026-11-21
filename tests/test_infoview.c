/* Package 14: the infoview bridge (bridge.h), kittymax's webview tests
   (test/kitty-lean-test.el, kitty-lean-webview-*) on the real child and a
   fake Lean server, the test acting as the browser:
   - only the page's own files are served, only under the token;
   - a page's calls reach Lean and come back as Lean sent them ({} stays
     {}), to that page only; the hello first, once per server;
   - an answer for a page whose stream is down waits for it;
   - a page that goes away is forgotten, its RPC sessions no longer kept
     alive, and told to reload when it comes back; its calls are dropped;
   - only Lean's servers are heard;
   - the page's edits (Try this, insertText) and jumps (showDocument) reach
     the buffer, and Lean hears of them.
   The test binary is also the fake server (--fake LOG) and the browser
   opener (an argument file://...). */
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdarg.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "check.h"
#include "bridge.h"
#include "core.h"
#include "loop.h"
#include "lsp.h"
#include "../src/infoview/scan.h"

static char dir[64], url[128], logpath[128];
static int port;

static long long ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

/* --- the fake Lean server ------------------------------------------------ */

static void reply(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void reply(const char *fmt, ...)
{
	char b[8192];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(b, sizeof b, fmt, ap);
	va_end(ap);
	printf("Content-Length: %d\r\n\r\n%s", n, b);
	fflush(stdout);
}

static int fake(const char *log)
{
	FILE *lg = fopen(log, "a");
	Str in = { 0 };
	char b[65536], held[32] = "";
	int connects = 0;
	ssize_t r;
	setvbuf(lg, NULL, _IOLBF, 0);
	while ((r = read(0, b, sizeof b)) > 0) {
		str_add(&in, b, (size_t)r);
		for (;;) {
			char *e = strstr(in.s, "\r\n\r\n");
			if (!e) break;
			size_t h = (size_t)(e - in.s) + 4, len = strtoul(in.s + 16, NULL, 10);
			if (in.n < h + len) break;
			char *m = in.s + h;
			size_t meth = j_member(m, len, 0, "method"), id = j_member(m, len, 0, "id"), p = j_member(m, len, 0, "params");
			char *name = j_str(m, len, meth, NULL), idv[32] = "";
			if (id != NONE) snprintf(idv, sizeof idv, "%.*s", (int)j_len(m, len, id), m + id);
			if (*held) {    /* the answer held back goes with the next message */
				reply("{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":{\"sessionId\":\"17\"}}", held);
				*held = 0;
			}
			fprintf(lg, "%s %.*s\n", name ? name : "?", (int)(p == NONE ? 0 : j_len(m, len, p)), m + (p == NONE ? 0 : p));
			if (!name) ;
			else if (!strcmp(name, "initialize"))
				reply("{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":{\"capabilities\":{\"hoverProvider\":true,\"x\":{}},"
				      "\"serverInfo\":{\"name\":\"Lean 4 Server\",\"version\":\"0.3.0\"}}}", idv);
			else if (!strcmp(name, "textDocument/didOpen")) {
				size_t u = j_member(m, len, j_member(m, len, p, "textDocument"), "uri");
				int ul = (int)j_len(m, len, u);
				/* Lean's incremental diagnostics: a list, then one more */
				reply("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":%.*s,"
				      "\"version\":0,\"diagnostics\":[{\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":0,\"character\":7}},"
				      "\"severity\":1,\"message\":\"one\"}]}}", ul, m + u);
				reply("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":%.*s,"
				      "\"version\":0,\"isIncremental\":true,\"diagnostics\":[{\"range\":{\"start\":{\"line\":1,\"character\":2},"
				      "\"end\":{\"line\":1,\"character\":9}},\"severity\":3,\"message\":\"two\",\"isSilent\":false}]}}", ul, m + u);
				reply("{\"jsonrpc\":\"2.0\",\"method\":\"$/lean/fileProgress\",\"params\":{\"textDocument\":{\"uri\":%.*s,"
				      "\"version\":0},\"processing\":[]}}", ul, m + u);
			} else if (!strcmp(name, "textDocument/didChange")) {
				size_t u = j_member(m, len, j_member(m, len, p, "textDocument"), "uri");
				int ul = (int)j_len(m, len, u);
				/* a fresh list, one more, the progress: after the bridge listens */
				reply("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":%.*s,"
				      "\"version\":1,\"diagnostics\":[{\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":0,\"character\":1}},"
				      "\"severity\":1,\"message\":\"three\"}]}}", ul, m + u);
				reply("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":%.*s,"
				      "\"version\":1,\"isIncremental\":true,\"diagnostics\":[{\"range\":{\"start\":{\"line\":0,\"character\":1},"
				      "\"end\":{\"line\":0,\"character\":2}},\"severity\":2,\"message\":\"four\"}]}}", ul, m + u);
				reply("{\"jsonrpc\":\"2.0\",\"method\":\"$/lean/fileProgress\",\"params\":{\"textDocument\":{\"uri\":%.*s,"
				      "\"version\":1},\"processing\":[]}}", ul, m + u);
			} else if (!strcmp(name, "$/lean/rpc/connect") && ++connects == 2)
				snprintf(held, sizeof held, "%s", idv);   /* late: its page is forgotten first */
			else if (!strcmp(name, "$/lean/rpc/connect"))
				reply("{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":{\"sessionId\":\"17\"}}", idv);
			else if (id != NONE)    /* any other request: Lean's {} must reach the page as {} */
				reply("{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":{\"goals\":[\"⊢ True\"],\"empty\":{},\"nothing\":null}}", idv);
			free(name);
			memmove(in.s, in.s + h + len, in.n - h - len);
			in.n -= h + len;
			in.s[in.n] = 0;
		}
	}
	return 0;
}

/* --- the browser ------------------------------------------------------- */

static int conn(void)
{
	struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons((unsigned short)port),
	                         .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	CHECK(fd >= 0 && !connect(fd, (struct sockaddr *)&a, sizeof a));
	return fd;
}

/* One request on its own connection: the status, the body in *body. */
static int http(const char *method, const char *path, const char *body, Str *out)
{
	int fd = conn();
	char h[1024];
	int n = snprintf(h, sizeof h, "%s %s HTTP/1.1\r\nHost: x\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
	                 method, path, body ? strlen(body) : 0);
	CHECK(write(fd, h, (size_t)n) == n);
	if (body) CHECK(write(fd, body, strlen(body)) == (ssize_t)strlen(body));
	Str r = { 0 };
	char b[65536];
	ssize_t k;
	/* a Content-Length answer: read until it is all there (the connection
	   stays open for more requests otherwise) */
	for (;;) {
		struct pollfd p = { fd, POLLIN, 0 };
		loop_once(0);
		if (poll(&p, 1, 10) <= 0) continue;
		if ((k = read(fd, b, sizeof b)) <= 0) break;
		str_add(&r, b, (size_t)k);
		char *e = strstr(r.s, "\r\n\r\n"), *cl = strstr(r.s, "Content-Length:");
		if (e && cl && r.n >= (size_t)(e - r.s) + 4 + strtoul(cl + 15, NULL, 10)) break;
	}
	close(fd);
	CHECK(r.s);
	int status = atoi(r.s + 9);
	if (out) { char *e = strstr(r.s, "\r\n\r\n"); out->n = 0; str_add(out, e + 4, strlen(e + 4)); }
	free(r.s);
	return status;
}

static int call(const char *page, const char *json)
{
	char p[512];
	snprintf(p, sizeof p, "/%scall?page=%s", url, page);
	return http("POST", p, json, NULL);
}

/* A page's event stream: its events as they came, one JSON per line. */
typedef struct { int fd; Str raw, ev; } Stream;

static void stream_open(Stream *s, const char *page)
{
	char h[512];
	s->fd = conn();
	s->raw.n = s->ev.n = 0;
	int n = snprintf(h, sizeof h, "GET /%sevents?page=%s HTTP/1.1\r\nHost: x\r\n\r\n", url, page);
	CHECK(write(s->fd, h, (size_t)n) == n);
	fcntl(s->fd, F_SETFL, O_NONBLOCK);
}

static void stream_close(Stream *s) { close(s->fd); s->fd = -1; }

/* Turns the loop for ms and reads every stream's chunks into events. */
static void pump(Stream **ss, int n, int t)
{
	long long end = ms() + t;
	do {
		loop_once(5);
		for (int i = 0; i < n; i++) {
			Stream *s = ss[i];
			char b[65536];
			ssize_t k;
			while (s->fd >= 0 && (k = read(s->fd, b, sizeof b)) > 0) str_add(&s->raw, b, (size_t)k);
			/* SSE lines "data: X" inside chunks: the editor's events never
			   hold a line break, so a line is an event */
			char *d;
			while (s->raw.s && (d = strstr(s->raw.s, "data: "))) {
				char *e = strstr(d, "\n\n");
				if (!e) break;
				str_add(&s->ev, d + 6, (size_t)(e - d - 6));
				str_add(&s->ev, "\n", 1);
				memmove(s->raw.s, e + 2, s->raw.n - (size_t)(e + 2 - s->raw.s) + 1);
				s->raw.n -= (size_t)(e + 2 - s->raw.s);
			}
		}
	} while (ms() < end);
}

static int has(const Stream *s, const char *what) { return s->ev.s && strstr(s->ev.s, what); }

/* Pumps until stream s has what, at most 5 s. */
static int wait_for(Stream **ss, int n, const Stream *s, const char *what)
{
	for (long long end = ms() + 5000; ms() < end; pump(ss, n, 10))
		if (has(s, what)) return 1;
	return 0;
}

static int count(const char *hay, const char *needle)
{
	int c = 0;
	for (const char *p = hay; p && (p = strstr(p, needle)); p += strlen(needle)) c++;
	return c;
}

static char *slurp(const char *path)
{
	Str s = { 0 };
	char b[4096];
	int fd = open(path, O_RDONLY);
	ssize_t k;
	str_add(&s, "", 0);
	while (fd >= 0 && (k = read(fd, b, sizeof b)) > 0) str_add(&s, b, (size_t)k);
	if (fd >= 0) close(fd);
	return s.s;
}

static void put_file(const char *path, const char *text)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	CHECK(fd >= 0 && write(fd, text, strlen(text)) == (ssize_t)strlen(text));
	close(fd);
}

static void copy_file(const char *from, const char *to)
{
	char *s = slurp(from);
	CHECK(*s);
	put_file(to, s);
	free(s);
}

int main(int argc, char **argv)
{
	static Editor e;
	static const char *const roots[] = { "lean-toolchain", NULL };
	char exe[4096], path[256], cpath[256], root[256], s[1024], opened[256];
	if (argc == 3 && !strcmp(argv[1], "--fake")) return fake(argv[2]);
	if (argc == 2 && !strncmp(argv[1], "file://", 7)) {    /* the browser opener */
		snprintf(opened, sizeof opened, "%s/opened", getenv("IV_TEST_DIR"));
		int fd = open(opened, O_WRONLY | O_CREAT | O_APPEND, 0644);
		CHECK(fd >= 0 && write(fd, argv[1], strlen(argv[1])) > 0 && write(fd, "\n", 1) == 1);
		close(fd);
		return 0;
	}
	CHECK(realpath("/proc/self/exe", exe));
	strcpy(dir, "/tmp/homeros-iv-XXXXXX");
	CHECK(mkdtemp(dir));
	setenv("IV_TEST_DIR", dir, 1);
	snprintf(logpath, sizeof logpath, "%s/lean.log", dir);
	/* two servers: lsp shares one between types with the same argv */
	char *const fargv[] = { exe, "--fake", logpath, NULL }, *const cargv[] = { exe, "--fake", logpath, NULL };
	const LspServer table[] = {
		{ ".lean", "lean4", (const char *const *)fargv, roots, 1 },
		{ ".c", "c", (const char *const *)cargv, roots, 0 },
		{ NULL, NULL, NULL, NULL, 0 },
	};
	lsp_servers = table;
	/* the page's files, and a stand-in for the npm package */
	snprintf(root, sizeof root, "%s/root", dir);
	mkdir(root, 0755);
	snprintf(s, sizeof s, "%s/dist", root);
	mkdir(s, 0755);
	const char *page_files[] = { "index.html", "host.js", "theme.css" };
	for (int i = 0; i < 3; i++) {
		char from[256], to[512];
		snprintf(from, sizeof from, "src/infoview/page/%s", page_files[i]);
		snprintf(to, sizeof to, "%s/%s", root, page_files[i]);
		copy_file(from, to);
	}
	snprintf(s, sizeof s, "%s/dist/index.css", root); put_file(s, "body{}");
	snprintf(s, sizeof s, "%s/dist/index.production.min.js", root); put_file(s, "export {}");
	snprintf(s, sizeof s, "%s/secret", dir); put_file(s, "secret");
	snprintf(s, sizeof s, "%s/lean-toolchain", dir); put_file(s, "leanprover/lean4:v4.35.0\n");
	snprintf(path, sizeof path, "%s/A.lean", dir);
	put_file(path, "theorem t : True := by\n  trivial\n");
	snprintf(cpath, sizeof cpath, "%s/b.c", dir);
	put_file(cpath, "int x;\n");
	setenv("HOMEROS_INFOVIEW", root, 1);
	setenv("HOMEROS_OPEN", exe, 1);

	/* no page files: refused, with a message */
	setenv("HOMEROS_INFOVIEW", dir, 1);
	CHECK(!core_open(&e, path));
	CHECK(bridge_open(&e) == -1 && strstr(e.msg, "install.sh"));
	setenv("HOMEROS_INFOVIEW", root, 1);

	/* \w comes after the file is open: Lean has answered initialize before
	   the bridge listens (the page must still get its hello) */
	long long end = ms() + 5000;
	char *o;
	while ((o = slurp(logpath)), !strstr(o, "\ninitialized ") && ms() < end) { free(o); loop_once(10); }
	CHECK(strstr(o, "\ninitialized "));
	free(o);
	/* every write tells the server (didSave, after BufWritePost in core_write) */
	CHECK(!core_write(&e, e.buf, 0));
	while ((o = slurp(logpath)), !strstr(o, "\ntextDocument/didSave ") && ms() < end) { free(o); loop_once(10); }
	CHECK(strstr(o, "\ntextDocument/didSave "));
	free(o);
	CHECK(!bridge_open(&e));
	snprintf(opened, sizeof opened, "%s/opened", dir);
	end = ms() + 5000;
	while ((o = slurp(opened)), count(o, "\n") < 1 && ms() < end) { free(o); loop_once(10); }
	free(o);
	CHECK(!bridge_open(&e));                 /* \w again: the same page, opened again */
	while ((o = slurp(opened)), count(o, "\n") < 2 && ms() < end) { free(o); loop_once(10); }
	/* both opens through one private file, which holds the address */
	char *nl = strchr(o, '\n');
	CHECK(count(o, "\n") == 2 && !strncmp(o, nl + 1, (size_t)(nl - o)));
	*nl = 0;
	struct stat st;
	CHECK(!stat(o + 7, &st) && (st.st_mode & 0777) == 0600);
	char *html = slurp(o + 7), *u = strstr(html, "url=http://127.0.0.1:");
	CHECK(u);
	port = atoi(u + 21);
	char *slash = strchr(u + 21, '/');
	snprintf(url, sizeof url, "%.*s", (int)(strchr(slash, '"') - slash - 1), slash + 1);   /* TOKEN/ */
	char redirect[256], token1[sizeof url];
	snprintf(redirect, sizeof redirect, "%s", o + 7);
	snprintf(token1, sizeof token1, "%s", url);
	/* only this machine can connect: the listener is on 127.0.0.1 */
	char *tcp = slurp("/proc/net/tcp"), lo[32], any[32];
	snprintf(lo, sizeof lo, "0100007F:%04X 00000000:0000 0A", port);
	snprintf(any, sizeof any, "00000000:%04X 00000000:0000 0A", port);
	CHECK(strstr(tcp, lo) && !strstr(tcp, any));
	free(tcp);
	free(html); free(o);

	/* only the page's own files, only under the token */
	Str b = { 0 };
	char p[512];
	snprintf(p, sizeof p, "/%s", url);
	CHECK(http("GET", p, NULL, &b) == 200 && strstr(b.s, "host.js"));
	snprintf(p, sizeof p, "/%sdist/index.css", url);
	CHECK(http("GET", p, NULL, &b) == 200 && !strcmp(b.s, "body{}"));
	snprintf(p, sizeof p, "/%stheme.css", url);
	CHECK(http("GET", p, NULL, &b) == 200 && strstr(b.s, "--vscode-editor-foreground"));
	const char *bad[] = { "dist/../../secret", "dist/../secret", "dist//etc/passwd", "dist/~/.bashrc", "../secret",
	                      "/etc/passwd", "dist/.%2e/secret", ".hidden" };
	for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
		snprintf(p, sizeof p, "/%s%s", url, bad[i]);
		CHECK(http("GET", p, NULL, &b) == 404);
	}
	CHECK(http("GET", "/x/", NULL, &b) == 404);
	snprintf(p, sizeof p, "/%.20s/", url);   /* part of the token */
	CHECK(http("GET", p, NULL, &b) == 404);
	snprintf(p, sizeof p, "/%s", url);       /* one character off */
	p[1] = p[1] == 'a' ? 'b' : 'a';
	CHECK(http("GET", p, NULL, &b) == 404);
	snprintf(p, sizeof p, "/%sindex.html", url);
	CHECK(http("DELETE", p, NULL, &b) == 405);
	/* a body is not waited for (nor held) without the token */
	{
		int fd = conn();
		const char big[] = "POST /nothing HTTP/1.1\r\nContent-Length: 60000000\r\n\r\nxx";
		CHECK(write(fd, big, sizeof big - 1) == (ssize_t)sizeof big - 1);
		char c;
		ssize_t k = -1;
		end = ms() + 2000;
		fcntl(fd, F_SETFL, O_NONBLOCK);
		while (ms() < end && (k = read(fd, &c, 1)) < 0) loop_once(10);
		CHECK(k == 0);
		close(fd);
	}
	/* a page id longer than 64 is no page */
	char id65[66];
	memset(id65, 'a', 65);
	id65[65] = 0;
	CHECK(call(id65, "{\"id\":0,\"m\":\"saveConfig\",\"a\":[null]}") == 400);

	/* the hello: once a Lean file is known and Lean has answered initialize,
	   the cursor first, then what VS Code's initInfoView sends */
	Stream A = { -1, { 0 }, { 0 } }, B = { -1, { 0 }, { 0 } };
	Stream *both[] = { &A, &B };
	stream_open(&A, "pageA");
	stream_open(&B, "pageB");
	bridge_update(&e);
	CHECK(wait_for(both, 2, &A, "\"serverRestarted\""));
	pump(both, 2, 200);
	CHECK(!strncmp(A.ev.s, "{\"m\":\"changedCursorLocation\",", 29));
	CHECK(has(&A, "\"m\":\"initialize\"") && has(&A, "\"m\":\"changedInfoviewConfig\"") && has(&A, "\"Lean 4 Server\""));
	CHECK(has(&B, "\"m\":\"initialize\""));
	/* the diagnostics as one list (the infoview keeps one per file) */
	CHECK(wait_for(both, 2, &A, "\"message\":\"two\""));
	char *d = strstr(A.ev.s, "\"message\":\"one\"");
	CHECK(d && strstr(d, "\"message\":\"two\""));

	/* calls: a session, then a request answered with Lean's own JSON, to
	   the page that asked only */
	char *uri = strdup(lsp_uri(e.buf->lsp));
	snprintf(s, sizeof s, "{\"id\":0,\"m\":\"createRpcSession\",\"a\":[\"%s\"]}", uri);
	CHECK(call("pageA", s) == 204);
	CHECK(wait_for(both, 2, &A, "{\"id\":0,\"k\":\"val\",\"v\":\"17\"}"));
	snprintf(s, sizeof s, "{\"id\":1,\"m\":\"sendClientRequest\",\"a\":[\"%s\",\"$/lean/rpc/call\","
	         "{\"sessionId\":\"17\",\"method\":\"Lean.Widget.getInteractiveGoals\",\"textDocument\":{\"uri\":\"%s\"},"
	         "\"position\":{\"line\":1,\"character\":2},\"params\":{}}]}", uri, uri);
	CHECK(call("pageA", s) == 204);
	CHECK(wait_for(both, 2, &A, "{\"id\":1,\"k\":\"raw\",\"v\":{\"jsonrpc\":\"2.0\""));
	CHECK(has(&A, "\"empty\":{},\"nothing\":null"));
	CHECK(!has(&B, "\"id\":"));
	/* B's id 0 is B's own */
	CHECK(call("pageB", "{\"id\":0,\"m\":\"saveConfig\",\"a\":[null]}") == 204);
	CHECK(wait_for(both, 2, &B, "{\"id\":0,\"k\":\"val\",\"v\":null}"));
	CHECK(count(A.ev.s, "\"id\":0,") == 1);
	/* a call that is no JSON, or has no id, is no error */
	CHECK(call("pageA", "not json") == 204);
	CHECK(call("pageA", "{\"m\":\"saveConfig\"}") == 204);
	pump(both, 2, 50);
	/* an unknown call is answered with an error */
	CHECK(call("pageA", "{\"id\":2,\"m\":\"frobnicate\",\"a\":[]}") == 204);
	CHECK(wait_for(both, 2, &A, "{\"id\":2,\"k\":\"err\""));
	/* no server for the file: an error, not a hang */
	CHECK(call("pageA", "{\"id\":3,\"m\":\"sendClientRequest\",\"a\":[\"file:///nowhere.lean\",\"x\",{}]}") == 204);
	CHECK(wait_for(both, 2, &A, "{\"id\":3,\"k\":\"err\""));
	/* calls short of their arguments (the review's fuzzer found these) */
	CHECK(call("pageA", "{\"id\":13,\"m\":\"createRpcSession\"}") == 204);
	CHECK(wait_for(both, 2, &A, "{\"id\":13,"));
	snprintf(s, sizeof s, "{\"id\":14,\"m\":\"applyEdit\",\"a\":[{\"documentChanges\":[{\"textDocument\":{\"uri\":\"%s\"},"
	         "\"edits\":[]}]}]}", uri);
	CHECK(call("pageA", s) == 204);
	CHECK(wait_for(both, 2, &A, "{\"id\":14,"));

	/* subscriptions: Lean's notifications and our edits to the pages that
	   asked for them */
	CHECK(call("pageA", "{\"id\":4,\"m\":\"subscribeClientNotifications\",\"a\":[\"textDocument/didChange\"]}") == 204);
	CHECK(wait_for(both, 2, &A, "{\"id\":4,"));
	CHECK(!core_edit(&e, 0, 0, "-- c\n", 5));
	lsp_flush(e.buf->lsp);
	CHECK(wait_for(both, 2, &A, "\"sentClientNotification\",\"n\":\"textDocument/didChange\""));
	CHECK(!has(&B, "sentClientNotification"));
	CHECK(call("pageA", "{\"id\":5,\"m\":\"unsubscribeClientNotifications\",\"a\":[\"textDocument/didChange\"]}") == 204);
	CHECK(wait_for(both, 2, &A, "{\"id\":5,"));
	int before = count(A.ev.s, "sentClientNotification");
	CHECK(!core_edit(&e, 0, 5, "", 0));
	lsp_flush(e.buf->lsp);
	pump(both, 2, 300);
	CHECK(count(A.ev.s, "sentClientNotification") == before);
	/* only Lean is heard: another server's edits stay away from the pages */
	CHECK(call("pageA", "{\"id\":10,\"m\":\"subscribeClientNotifications\",\"a\":[\"textDocument/didChange\"]}") == 204);
	CHECK(wait_for(both, 2, &A, "{\"id\":10,"));
	Text *ct = text_new("int x;\n", 7);
	LspDoc *cd = lsp_open(cpath, ct);
	CHECK(ct && cd);
	lsp_edit(cd, ct, 0, 0, "y", 1);
	CHECK(!text_insert(ct, 0, "y", 1));
	lsp_flush(cd);
	end = ms() + 3000;
	char *lg;
	while ((lg = slurp(logpath)), !strstr(lg, "b.c\",\"version\":1") && ms() < end) { free(lg); pump(both, 2, 10); }
	CHECK(strstr(lg, "b.c\",\"version\":1"));      /* the C server has it */
	free(lg);
	pump(both, 2, 100);
	CHECK(!has(&A, "b.c\""));
	lsp_close(cd);
	text_free(ct);
	CHECK(call("pageA", "{\"id\":11,\"m\":\"unsubscribeClientNotifications\",\"a\":[\"textDocument/didChange\"]}") == 204);
	CHECK(wait_for(both, 2, &A, "{\"id\":11,"));

	/* the cursor: one event per move, none when it stays */
	int moves = count(A.ev.s, "changedCursorLocation");
	bridge_update(&e);
	pump(both, 2, 50);
	CHECK(count(A.ev.s, "changedCursorLocation") == moves);
	e.win->line = 1; e.win->col = 2;
	bridge_update(&e);
	CHECK(wait_for(both, 2, &A, "\"start\":{\"line\":1,\"character\":2}"));
	pump(both, 2, 100);
	CHECK(count(A.ev.s, "\"m\":\"initialize\"") == 1);

	/* the page's edits: Try this (applyEdit), insertText above the line */
	snprintf(s, sizeof s, "{\"id\":6,\"m\":\"applyEdit\",\"a\":[{\"documentChanges\":[{\"textDocument\":{\"uri\":\"%s\","
	         "\"version\":1},\"edits\":[{\"range\":{\"start\":{\"line\":1,\"character\":2},\"end\":{\"line\":1,"
	         "\"character\":9}},\"newText\":\"exact trivial\"}]}]}]}", uri);
	CHECK(call("pageA", s) == 204);
	CHECK(wait_for(both, 2, &A, "{\"id\":6,\"k\":\"val\""));
	size_t ln;
	const unsigned char *t = core_line(&e, 1, &ln);
	CHECK(ln == 15 && !memcmp(t, "  exact trivial", 15));
	snprintf(s, sizeof s, "{\"id\":7,\"m\":\"insertText\",\"a\":[\"sorry\\n  -- \\u03b1 x\",\"above\",{\"textDocument\":"
	         "{\"uri\":\"%s\"},\"position\":{\"line\":1,\"character\":4}}]}", uri);
	CHECK(call("pageA", s) == 204);
	CHECK(wait_for(both, 2, &A, "{\"id\":7,\"k\":\"val\""));
	t = core_line(&e, 1, &ln);
	CHECK(ln == 7 && !memcmp(t, "  sorry", 7));
	t = core_line(&e, 2, &ln);
	CHECK(ln == 9 && !memcmp(t, "  -- \xCE\xB1 x", 9));
	/* Lean heard of both (one didChange each) */
	end = ms() + 3000;
	while ((lg = slurp(logpath)), !strstr(lg, "exact trivial") && ms() < end) { free(lg); pump(both, 2, 10); }
	CHECK(strstr(lg, "exact trivial") && strstr(lg, "sorry"));
	free(lg);
	/* showDocument: the cursor goes to the selection (Lean counts UTF-16:
	   x is the 8th unit of its line, at byte 8 after the 2-byte alpha) */
	snprintf(s, sizeof s, "{\"id\":8,\"m\":\"showDocument\",\"a\":[{\"uri\":\"%s\",\"selection\":{\"start\":{\"line\":2,"
	         "\"character\":7},\"end\":{\"line\":2,\"character\":8}}}]}", uri);
	CHECK(call("pageA", s) == 204);
	CHECK(wait_for(both, 2, &A, "{\"id\":8,\"k\":\"val\""));
	CHECK(e.win->line == 2 && e.win->col == 8);

	/* an answer for a page whose stream is down waits for it */
	Stream P = { -1, { 0 }, { 0 } };
	Stream *withp[] = { &A, &B, &P };
	CHECK(call("pageP", "{\"id\":7,\"m\":\"saveConfig\",\"a\":[null]}") == 204);
	pump(withp, 3, 100);
	stream_open(&P, "pageP");
	CHECK(wait_for(withp, 3, &P, "{\"id\":7,\"k\":\"val\",\"v\":null}"));
	CHECK(has(&P, "\"m\":\"initialize\""));
	CHECK(has(&P, "$/lean/fileProgress"));      /* the last one Lean sent */
	/* Lean's incremental diagnostics, joined as vscode-lean4 does: the
	   fresh list and the one after it, and nothing from before */
	char *d3 = strstr(P.ev.s, "\"message\":\"three\"");
	CHECK(d3 && strstr(d3, "\"message\":\"four\"") && !has(&P, "\"message\":\"one\""));
	CHECK(strstr(P.ev.s, "\"m\":") == strstr(P.ev.s, "\"m\":\"changedCursorLocation\""));

	/* sessions are kept alive (every 10 s) while their page is */
	end = ms() + 11000;
	while ((lg = slurp(logpath)), !strstr(lg, "$/lean/rpc/keepAlive") && ms() < end) { free(lg); pump(withp, 3, 50); }
	CHECK(strstr(lg, "$/lean/rpc/keepAlive {\"uri\":\"file://") && strstr(lg, "\"sessionId\":\"17\""));
	free(lg);

	/* a page that goes away is forgotten (PAGE_TIMEOUT, 1 s in this build):
	   its session no longer kept alive, its calls dropped, told to reload */
	snprintf(s, sizeof s, "{\"id\":12,\"m\":\"createRpcSession\",\"a\":[\"%s\"]}", uri);
	CHECK(call("pageA", s) == 204);           /* Lean holds this one back */
	pump(withp, 3, 100);
	stream_close(&A);
	pump(withp, 3, 1500);
	snprintf(s, sizeof s, "%s/lean.log", dir);
	put_file(s, "");
	CHECK(!core_edit(&e, 0, 0, "-- d\n", 5));  /* and answers now: A is gone */
	lsp_flush(e.buf->lsp);
	CHECK(call("pageA", "{\"id\":9,\"m\":\"saveConfig\",\"a\":[null]}") == 204);
	stream_open(&A, "pageA");
	CHECK(wait_for(withp, 3, &A, "{\"reload\":true}"));
	pump(withp, 3, 200);
	CHECK(!has(&A, "\"id\":9"));
	end = ms() + 10500;
	while (ms() < end) pump(withp, 3, 100);
	lg = slurp(logpath);
	CHECK(!strstr(lg, "keepAlive"));
	free(lg);
	/* the others stay */
	CHECK(call("pageB", "{\"id\":1,\"m\":\"saveConfig\",\"a\":[null]}") == 204);
	CHECK(wait_for(withp, 3, &B, "{\"id\":1,\"k\":\"val\""));

	/* the hello comes again only for another server: a restarted one */
	int hellos = count(B.ev.s, "serverRestarted");
	lsp_restart(e.buf->lsp);
	pump(withp, 3, 300);
	CHECK(count(B.ev.s, "serverRestarted") == hellos);

	bridge_free();
	/* the child deletes its file when the editor lets it go */
	end = ms() + 3000;
	while (!access(redirect, F_OK) && ms() < end) loop_once(10);
	CHECK(access(redirect, F_OK));
	/* \w again: a new child, a new token */
	CHECK(!bridge_open(&e));
	end = ms() + 5000;
	while ((o = slurp(opened)), count(o, "\n") < 3 && ms() < end) { free(o); loop_once(10); }
	CHECK(count(o, "\n") == 3);
	*strrchr(o, '\n') = 0;
	html = slurp(strrchr(o, '\n') + 8);
	CHECK(strstr(html, "url=http://127.0.0.1:") && !strstr(html, token1));
	free(html); free(o);
	bridge_free();
	core_free(&e);
	free(uri); free(b.s);
	free(A.raw.s); free(A.ev.s); free(B.raw.s); free(B.ev.s); free(P.raw.s); free(P.ev.s);
	snprintf(s, sizeof s, "rm -rf %s", dir);
	CHECK(!system(s));
	printf("test_infoview: ok\n");
	return 0;
}
