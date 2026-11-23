/* Linux process control by ptrace: one traced process, one thread (prototype). */
#ifndef ZDB_TARGET_H
#define ZDB_TARGET_H
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

/* DWARF register numbers 0-16 (rax rdx rcx rbx rsi rdi rbp rsp r8-r15 rip); the low 8 bytes of xmm0-15 */
typedef struct { uint64_t r[17], xmm[16]; } Regs;
typedef struct { pid_t pid; int memfd; int status; } Target;

bool target_start(Target *t, char *const argv[]);          /* stopped before the first instruction */
bool target_read(const Target *t, uint64_t addr, void *buf, uint64_t n);
bool target_write(const Target *t, uint64_t addr, const void *buf, uint64_t n);
bool target_regs(const Target *t, Regs *r);
bool target_set_regs(const Target *t, const Regs *r);
/* Runs with int3 at each of the traps until one is hit (true, pc = the trap's address) or the
   process ends (false, exit code in *code). */
bool target_run(Target *t, const uint64_t *traps, uint32_t n, uint64_t *pc, int *code);
bool target_step(Target *t);                                /* one instruction; false when the process ended */
void target_kill(Target *t);
#endif
