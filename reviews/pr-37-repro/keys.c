/* The keys backend_tty.input gives for chunks written with pauses, and when
   each came.  usage: keys <pause ms> <hex chunk>...   (stdin is replaced by a pipe) */
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "backend.h"
#include "keys.h"
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec * 1e3 + (double)t.tv_nsec / 1e6; }
int main(int argc, char **argv)
{
	int p[2], pause = atoi(argv[1]);
	if (pipe(p)) return 1;
	if (!fork()) {
		close(p[0]);
		for (int i = 2; i < argc; i++) {
			unsigned char b[64]; size_t n = strlen(argv[i]) / 2;
			for (size_t j = 0; j < n; j++) { unsigned x; sscanf(argv[i] + 2 * j, "%2x", &x); b[j] = (unsigned char)x; }
			if (write(p[1], b, n) < 0) _exit(1);
			usleep((useconds_t)pause * 1000);
		}
		usleep(400000);
		_exit(0);
	}
	close(p[1]);
	fcntl(p[0], F_SETFL, O_NONBLOCK);
	dup2(p[0], 0);
	double t0 = now();
	for (;;) {
		struct pollfd q = { .fd = 0, .events = POLLIN };
		if (poll(&q, 1, 300) <= 0 || !(q.revents & POLLIN)) break;
		int k[16], n = backend_tty.input(k, 16);
		for (int i = 0; i < n; i++) {
			if (k[i] == KEY_DEL) printf("KEY_DEL"); else if (k[i] >= KEY_UP) printf("KEY+%d", k[i] - KEY_UP);
			else if (k[i] > 32 && k[i] < 127) printf("'%c'", k[i]); else printf("%d", k[i]);
			printf("@%.0fms ", now() - t0);
		}
		if (q.revents & POLLHUP && !n) break;
	}
	puts("");
	return 0;
}
