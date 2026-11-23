/* homeros-infoview on Windows (src/infoview/serve/serve.c's Windows half),
   black box: this test is the editor (it starts the child with
   proc_spawn_ov and speaks its pipe) and the browser (Winsock).  The rest
   of the bridge is the same code on both systems, tested by test_infoview.
   1. The first line is the private .html file, which sends a browser to
      127.0.0.1 under the token; the page's files are served under it only.
   2. An event stream: "+ P" when it opens, the editor's events on it, a
      32 MB one too, read late so the socket fills (Windows signals a socket
      again only after a send that would block).
   3. A call is answered at once and reaches the editor; while the editor
      reads nothing (a 2 MB call fills the pipe), the pages are still
      served (the pipe out is overlapped, never a blocking write).
   4. At EOF on its stdin the child deletes its file and exits 0.
   Runs on Windows or under Wine (src/infoview/check.sh); elsewhere it says so. */
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "loop.h"
#include "str.h"

static Str got;          /* the child's stdout */
static int ended = -1;   /* its exit code, once reported */
static int port;
static char token[65];

static void out(void *ctx, int stream, const char *b, size_t n)
{
	(void)ctx;
	if (stream == 1) CHECK(!str_add(&got, b, n));
	if (stream == 0) ended = (int)n;
}

/* runs the loop until s is in the child's output, 10 s at most */
static void until(const char *s)
{
	for (ULONGLONG end = GetTickCount64() + 10000; (!got.s || !strstr(got.s, s)) && GetTickCount64() < end; ) loop_once(10);
	CHECK(got.s && strstr(got.s, s));
}

static SOCKET dial(void)
{
	struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons((u_short)port), .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
	SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
	DWORD t = 10000;
	CHECK(s != INVALID_SOCKET && !setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&t, sizeof t));
	CHECK(!connect(s, (struct sockaddr *)&a, sizeof a));
	return s;
}

static void put(SOCKET s, const char *b, size_t n)
{
	while (n) {
		int k = send(s, b, n > 65536 ? 65536 : (int)n, 0);
		CHECK(k > 0);
		b += k; n -= (size_t)k;
	}
}

/* reads from s until r holds want (or n bytes in all, if want is NULL) */
static void take(SOCKET s, Str *r, const char *want, size_t n)
{
	char b[65536];
	while (want ? !r->s || !strstr(r->s, want) : r->n < n) {
		int k = recv(s, b, sizeof b, 0);
		CHECK(k > 0);
		CHECK(!str_add(r, b, (size_t)k));
	}
}

/* one request on a new connection: the status, the body in r */
static int http(const char *method, const char *path, const char *body, size_t bn, Str *r)
{
	char h[512];
	SOCKET s = dial();
	int hn = snprintf(h, sizeof h, "%s %s HTTP/1.1\r\nHost: x\r\nContent-Length: %zu\r\n\r\n", method, path, bn);
	put(s, h, (size_t)hn);
	put(s, body, bn);
	r->n = 0;
	take(s, r, "\r\n\r\n", 0);
	char *e = strstr(r->s, "\r\n\r\n"), *l = strstr(r->s, "Content-Length: ");
	size_t len = l && l < e ? strtoul(l + 16, NULL, 10) : 0, at = (size_t)(e + 4 - r->s);
	take(s, r, NULL, at + len);
	closesocket(s);
	int st = atoi(r->s + 9);
	memmove(r->s, r->s + at, r->n - at);
	r->n -= at;
	r->s[r->n] = 0;
	return st;
}

int main(void)
{
	char exe[MAX_PATH], root[MAX_PATH], p[MAX_PATH + 32], file[MAX_PATH];
	WSADATA wd;
	CHECK(!WSAStartup(MAKEWORD(2, 2), &wd));
	/* the child beside this test, as beside the editor; its ROOT */
	CHECK(GetModuleFileNameA(NULL, exe, MAX_PATH) < MAX_PATH - 32);
	strcpy(strrchr(exe, '\\') + 1, "homeros-infoview.exe");
	CHECK(GetTempPathA(MAX_PATH - 64, root));
	snprintf(root + strlen(root), 64, "homeros-ivwin-%lu", GetCurrentProcessId());
	snprintf(p, sizeof p, "%s\\dist", root);
	CreateDirectoryA(root, NULL);   /* a failed run's may be there */
	CreateDirectoryA(p, NULL);
	snprintf(p, sizeof p, "%s\\index.html", root);
	FILE *f = fopen(p, "wb");
	CHECK(f && fputs("<page>", f) >= 0 && !fclose(f));
	char *argv[] = { exe, root, NULL };
	int id = proc_spawn_ov(argv, NULL, out, NULL);
	CHECK(id >= 0);

	/* 1. the private file, then the page under the token only */
	until("\n");
	snprintf(file, sizeof file, "%.*s", (int)(strchr(got.s, '\n') - got.s), got.s);
	CHECK(strlen(file) > 5 && !strcmp(file + strlen(file) - 5, ".html"));
	got.n = 0; got.s[0] = 0;
	f = fopen(file, "rb");
	char html[512] = "";
	CHECK(f && fread(html, 1, sizeof html - 1, f) > 0 && !fclose(f));
	char *u = strstr(html, "url=http://127.0.0.1:");
	CHECK(u && sscanf(u + 21, "%d/%64[0-9a-f]/", &port, token) == 2 && strlen(token) == 64);
	Str r = { 0 };
	snprintf(p, sizeof p, "/%s/", token);
	CHECK(http("GET", p, "", 0, &r) == 200 && !strcmp(r.s, "<page>"));
	CHECK(http("GET", "/index.html", "", 0, &r) == 404);

	/* 2. a stream: opened, the editor's events on it, one of 32 MB read late */
	SOCKET ev = dial();
	snprintf(p, sizeof p, "GET /%s/events?page=A HTTP/1.1\r\nHost: x\r\n\r\n", token);
	put(ev, p, strlen(p));
	until("+ A\n");
	got.n = 0; got.s[0] = 0;
	CHECK(!proc_write(id, "A {\"n\":1}\n", 10));
	Str s = { 0 };
	for (ULONGLONG end = GetTickCount64() + 100; GetTickCount64() < end; ) loop_once(10);
	take(ev, &s, "data: {\"n\":1}\n\n", 0);
	size_t big = 32 << 20;
	char *b = malloc(big + 16);
	CHECK(b);
	memcpy(b, "A {\"s\":\"", 8);
	for (size_t i = 8; i < big; i++) b[i] = (char)('a' + i % 26);
	memcpy(b + big, "\"}\n", 3);
	CHECK(!proc_write(id, b, big + 3));
	for (ULONGLONG end = GetTickCount64() + 500; GetTickCount64() < end; ) loop_once(10);
	s.n = 0; s.s[0] = 0;
	take(ev, &s, "\"}\n\n", 0);
	char *d = strstr(s.s, "data: {\"s\":\"");
	CHECK(d && !memcmp(d + 6, b + 2, big - 2) && !memcmp(d + 6 + big - 2, "\"}\n\n", 4));

	/* 3. a call, 2 MB, while the editor reads nothing: the page is still served */
	size_t cn = 2 << 20;
	char *call = malloc(cn + 1);
	CHECK(call);
	memset(call, 'x', cn);
	memcpy(call, "{\"id\":1,\"m\":\"", 13);
	memcpy(call + cn - 2, "\"}", 2);
	snprintf(p, sizeof p, "/%s/call?page=A", token);
	CHECK(http("POST", p, call, cn, &r) == 204);
	snprintf(p, sizeof p, "/%s/", token);
	CHECK(http("GET", p, "", 0, &r) == 200 && !strcmp(r.s, "<page>"));
	call[cn] = 0;
	until("\n");
	CHECK(got.n == cn + 3 && !memcmp(got.s, "A ", 2) && !memcmp(got.s + 2, call, cn));

	/* 4. EOF: the file is deleted, exit 0 */
	closesocket(ev);
	proc_close(id);
	for (ULONGLONG end = GetTickCount64() + 10000; ended < 0 && GetTickCount64() < end; ) loop_once(10);
	CHECK(ended == 0 && GetFileAttributesA(file) == INVALID_FILE_ATTRIBUTES);
	snprintf(p, sizeof p, "%s\\index.html", root); DeleteFileA(p);
	snprintf(p, sizeof p, "%s\\dist", root); RemoveDirectoryA(p);
	RemoveDirectoryA(root);
	free(b); free(call); free(r.s); free(s.s); free(got.s);
	puts("test_infoview_win: the child served, streamed, took calls while the editor was busy, and ended");
	return 0;
}
#else
#include <stdio.h>
int main(void) { puts("test_infoview_win: Windows only (src/infoview/check.sh runs it under Wine)"); return 0; }
#endif
