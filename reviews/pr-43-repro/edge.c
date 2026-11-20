/* Probes through the editor's side: r43_edge MODE
     empty    applyEdit with an empty edits array (findings: qsort(NULL))
     chatter  an opener that writes to its stdout (R43_CHATTER), then a call
     kill     the child killed, then \w again
     leandie  Lean dies with a request of the page's unanswered
     big N    a call with an N MB body, and how long the loop stood still
     stall    a stream that is never read, 200,000 cursor moves */
#include "common.h"
#include <dirent.h>

static long long worst;
static void turn_timed(int t)
{
	for (long long end = ms() + t; ms() < end; ) {
		struct timespec a, b;
		clock_gettime(CLOCK_MONOTONIC, &a);
		loop_once(0);
		clock_gettime(CLOCK_MONOTONIC, &b);
		long long d = (b.tv_sec - a.tv_sec) * 1000000LL + (b.tv_nsec - a.tv_nsec) / 1000;
		if (d > worst) worst = d;
		usleep(200);
	}
}

static int child_pid(const char *name)
{
	char p[300], b[300];
	DIR *d = opendir("/proc");
	int found = 0;
	for (struct dirent *de; (de = readdir(d)); ) {
		int pid = atoi(de->d_name);
		snprintf(p, sizeof p, "/proc/%d/stat", pid);
		FILE *f = pid ? fopen(p, "r") : NULL;
		int ppid = 0;
		if (f && fscanf(f, "%*d (%299[^)]) %*c %d", b, &ppid) == 2 && ppid == getpid() && strstr(b, name)) found = pid;
		if (f) fclose(f);
	}
	closedir(d);
	return found;
}

int main(int argc, char **argv)
{
	int rc;
	char s[2048];
	if (helper(argc, argv, &rc)) return rc;
	const char *mode = argc > 1 ? argv[1] : "";
	setup();
	CHECK(!core_open(&e, path));
	CHECK(open_bridge());
	Stream A = { -1, { 0 }, { 0 } };
	Stream *one[] = { &A };
	stream_open(&A, "pageA");
	bridge_update(&e);
	CHECK(wait_for(one, 1, &A, "\"serverRestarted\""));
	char *uri = strdup(lsp_uri(e.buf->lsp));
	if (!strcmp(mode, "empty")) {
		snprintf(s, sizeof s, "{\"id\":6,\"m\":\"applyEdit\",\"a\":[{\"documentChanges\":[{\"textDocument\":{\"uri\":\"%s\"},\"edits\":[]}]}]}", uri);
		CHECK(call("pageA", s) == 204);
		printf("empty: answered=%d\n", wait_for(one, 1, &A, "{\"id\":6,"));
	} else if (!strcmp(mode, "chatter")) {
		/* the opener is still running (it sleeps 3 s after writing) */
		CHECK(call("pageA", "{\"id\":7,\"m\":\"saveConfig\",\"a\":[null]}") == 204);
		int first = wait_for(one, 1, &A, "{\"id\":7,");
		CHECK(call("pageA", "{\"id\":8,\"m\":\"saveConfig\",\"a\":[null]}") == 204);
		printf("chatter %s: first call answered=%d, second=%d\n", getenv("R43_CHATTER") ? "on" : "off", first,
		       wait_for(one, 1, &A, "{\"id\":8,"));
	} else if (!strcmp(mode, "kill")) {
		int pid = child_pid("homeros-info");
		CHECK(pid);
		kill(pid, SIGKILL);
		turn(300);
		int again = open_bridge();
		printf("kill: \\w after the child was killed opens a page again=%d (new child %d)\n", again, child_pid("homeros-info"));
		Stream B = { -1, { 0 }, { 0 } };
		Stream *two[] = { &B };
		if (again) {
			stream_open(&B, "pageB");
			printf("kill: the new page gets its hello=%d\n", wait_for(two, 1, &B, "\"serverRestarted\""));
		}
	} else if (!strcmp(mode, "leandie")) {
		snprintf(s, sizeof s, "{\"id\":0,\"m\":\"createRpcSession\",\"a\":[\"%s\"]}", uri);
		CHECK(call("pageA", s) == 204);
		CHECK(wait_for(one, 1, &A, "{\"id\":0,\"k\":\"val\""));
		snprintf(s, sizeof s, "{\"id\":1,\"m\":\"createRpcSession\",\"a\":[\"%s\"]}", uri);
		CHECK(call("pageA", s) == 204);           /* the fake holds the second one back */
		pump(one, 1, 200);
		int pid = child_pid("r43_edge");
		CHECK(pid);
		kill(pid, SIGKILL);
		int err = wait_for(one, 1, &A, "{\"id\":1,\"k\":\"err\"");
		printf("leandie: the waiting call is answered with an error=%d; the page is told the server stopped=%d\n", err,
		       count(A.ev.b, "serverStopped") > 1);
		CHECK(call("pageA", "{\"id\":2,\"m\":\"sendClientRequest\",\"a\":[\"x\",\"y\",{}]}") == 204);
		printf("leandie: a call after it is answered=%d\n", wait_for(one, 1, &A, "{\"id\":2,\"k\":\"err\""));
		long long t = ms();
		while (ms() - t < 11000) pump(one, 1, 100);   /* a keep-alive tick with no server */
		printf("leandie: survived a keep-alive tick\n");
	} else if (!strcmp(mode, "big")) {
		size_t mb = argc > 2 ? (size_t)atoi(argv[2]) : 8, n = mb << 20;
		char *body = malloc(n + 200), h[300];
		int k = snprintf(body, 200, "{\"id\":9,\"m\":\"sendClientNotification\",\"a\":[\"%s\",\"x\",\"", uri);
		memset(body + k, 'a', n);
		memcpy(body + k + n - 4, "\"]}", 4);
		int fd = conn();
		int hn = snprintf(h, sizeof h, "POST /%scall?page=pageA HTTP/1.1\r\nContent-Length: %zu\r\n\r\n", url, k + n - 1);
		CHECK(write(fd, h, (size_t)hn) == hn);
		for (size_t off = 0; off < k + n - 1; ) {
			ssize_t w = write(fd, body + off, k + n - 1 - off);
			CHECK(w > 0);
			off += (size_t)w;
		}
		turn_timed(100);
		long long t = ms();
		while (!has(&A, "{\"id\":9,") && ms() - t < 60000) { turn_timed(20); pump(one, 1, 0); }
		printf("big: a %zu MB call from the page: answered=%d, the loop stood still for at most %lld ms\n", mb, has(&A, "{\"id\":9,"), worst / 1000);
		close(fd);
		free(body);
	} else if (!strcmp(mode, "stall")) {
		Stream S = { -1, { 0 }, { 0 } };
		stream_open(&S, "pageS");         /* never read again */
		turn(200);
		struct timespec a, b;
		clock_gettime(CLOCK_MONOTONIC, &a);
		long long worst_update = 0;
		for (int i = 0; i < 200000; i++) {
			struct timespec x, y;
			e.win->line = (size_t)(i & 1);
			clock_gettime(CLOCK_MONOTONIC, &x);
			bridge_update(&e);
			loop_once(0);
			clock_gettime(CLOCK_MONOTONIC, &y);
			long long d = (y.tv_sec - x.tv_sec) * 1000000LL + (y.tv_nsec - x.tv_nsec) / 1000;
			if (d > worst_update) worst_update = d;
		}
		clock_gettime(CLOCK_MONOTONIC, &b);
		int pid = child_pid("homeros-info");
		snprintf(s, sizeof s, "grep VmRSS /proc/%d/status", pid);
		printf("stall: 200000 moves with one unread stream: %.1f us each, worst %lld us; the child's memory: ",
		       ((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec)) / 1000 / 200000, worst_update);
		fflush(stdout);
		if (system(s)) {}
	}
	fflush(stdout);
	bridge_free();
	turn(200);
	core_free(&e);
	cleanup();
	return 0;
}
