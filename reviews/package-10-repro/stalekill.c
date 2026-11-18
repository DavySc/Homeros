/* proc_kill after the child was reaped signals a process group id that the
   kernel has already given back.  The child exits at once but a process it
   started (setsid, so outside the group) keeps its stdout open, so the loop
   has the exit code and still waits for the output.  A stranger then gets
   the child's old pid and leads its own group, as every shell job does;
   proc_kill(id) kills the stranger.  Needs root: the pid is chosen through
   /proc/sys/kernel/ns_last_pid. */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include "loop.h"
static int childpid, exited;
static void fn(void *c, int s, const char *b, size_t n)
{
	(void)c;
	if (s == 1 && n && !childpid) childpid = atoi(b);
	if (!s) exited = 1;
}
int main(void)
{
	char *sh[] = { "sh", "-c", "echo $$; setsid sleep 20 & exit 0", NULL };
	int id = proc_spawn(sh, NULL, fn, NULL);
	for (int i = 0; i < 5; i++) loop_once(100);
	printf("child pid %d, reaped by the loop: %s, exit reported: %d\n", childpid,
	       kill(childpid, 0) ? "yes (pid is free)" : "no", exited);
	pid_t st = -1;
	for (int try = 0; try < 20 && st != childpid; try++) {
		FILE *f = fopen("/proc/sys/kernel/ns_last_pid", "w");
		if (!f) { perror("ns_last_pid"); return 2; }
		fprintf(f, "%d", childpid - 1);
		fclose(f);
		st = fork();
		if (!st) { setpgid(0, 0); pause(); _exit(0); }
		if (st != childpid) { kill(st, SIGKILL); waitpid(st, NULL, 0); }
	}
	if (st != childpid) { printf("could not get the pid\n"); return 2; }
	usleep(100000);
	printf("a stranger now has pid %d and leads group %d\n", (int)st, (int)getpgid(st));
	proc_kill(id);
	int status = 0;
	usleep(100000);
	if (waitpid(st, &status, WNOHANG) == st && WIFSIGNALED(status))
		printf("proc_kill(%d) killed the stranger with signal %d\n", id, WTERMSIG(status));
	else { printf("the stranger lives\n"); kill(st, SIGKILL); }
	system("pkill -f 'sleep 20'");
	return 0;
}
