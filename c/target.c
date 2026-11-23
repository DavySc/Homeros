#define _GNU_SOURCE
#include "target.h"
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <sys/personality.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>

bool target_start(Target *t, char *const argv[]) {
    t->pid = fork();
    if (t->pid < 0) return false;
    if (!t->pid) {
        ptrace(PTRACE_TRACEME, 0, 0, 0);
        personality(ADDR_NO_RANDOMIZE);       /* as gdb does: the same addresses every run */
        execv(argv[0], argv);
        _exit(127);
    }
    if (waitpid(t->pid, &t->status, 0) != t->pid || !WIFSTOPPED(t->status)) return false;
    ptrace(PTRACE_SETOPTIONS, t->pid, 0, PTRACE_O_EXITKILL);
    char path[64];
    snprintf(path, sizeof path, "/proc/%d/mem", t->pid);
    t->memfd = open(path, O_RDWR);
    return t->memfd >= 0;
}

/* /proc/pid/mem writes through page protections (the kernel forces them for a tracer) */
bool target_read(const Target *t, uint64_t a, void *b, uint64_t n) { return pread(t->memfd, b, n, (off_t)a) == (ssize_t)n; }
bool target_write(const Target *t, uint64_t a, const void *b, uint64_t n) { return pwrite(t->memfd, b, n, (off_t)a) == (ssize_t)n; }

bool target_regs(const Target *t, Regs *r) {
    struct user_regs_struct u;
    if (ptrace(PTRACE_GETREGS, t->pid, 0, &u)) return false;
    uint64_t v[17] = { u.rax, u.rdx, u.rcx, u.rbx, u.rsi, u.rdi, u.rbp, u.rsp, u.r8, u.r9, u.r10, u.r11, u.r12, u.r13, u.r14, u.r15, u.rip };
    for (int i = 0; i < 17; i++) r->r[i] = v[i];
    return true;
}

bool target_set_pc(const Target *t, uint64_t pc) {
    struct user_regs_struct u;
    if (ptrace(PTRACE_GETREGS, t->pid, 0, &u)) return false;
    u.rip = pc;
    return !ptrace(PTRACE_SETREGS, t->pid, 0, &u);
}

static bool wait_stop(Target *t) {
    for (;;) {
        if (waitpid(t->pid, &t->status, 0) != t->pid) return false;
        if (WIFEXITED(t->status) || WIFSIGNALED(t->status)) return false;
        int sig = WSTOPSIG(t->status);
        if (sig == SIGTRAP) return true;
        ptrace(PTRACE_CONT, t->pid, 0, sig);          /* the program's own signals go to it */
    }
}

bool target_step(Target *t) { return !ptrace(PTRACE_SINGLESTEP, t->pid, 0, 0) && wait_stop(t); }

bool target_run(Target *t, const uint64_t *traps, uint32_t n, uint64_t *pc, int *code) {
    static uint8_t saved[4096];
    if (n > 4096) n = 4096;
    Regs r;
    target_regs(t, &r);
    for (uint32_t i = 0; i < n; i++)                  /* standing on a trap: step off it first */
        if (traps[i] == r.r[16]) { if (!target_step(t)) goto gone; break; }
    uint8_t cc = 0xcc;
    for (uint32_t i = 0; i < n; i++) { target_read(t, traps[i], &saved[i], 1); target_write(t, traps[i], &cc, 1); }
    bool hit = !ptrace(PTRACE_CONT, t->pid, 0, 0) && wait_stop(t);
    if (hit) for (uint32_t i = n; i--;) target_write(t, traps[i], &saved[i], 1);
    if (!hit) goto gone;
    target_regs(t, &r);
    *pc = r.r[16] - 1;
    target_set_pc(t, *pc);
    return true;
gone:
    *code = WIFEXITED(t->status) ? WEXITSTATUS(t->status) : 128 + WTERMSIG(t->status);
    return false;
}

void target_kill(Target *t) { if (t->pid > 0) { kill(t->pid, SIGKILL); waitpid(t->pid, &t->status, 0); close(t->memfd); t->pid = 0; } }
