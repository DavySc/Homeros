# Review of package 10 (loop and processes), PR 15

Reviewer: a separate Fable 5.2 thread, 2026-11-17. Reviewed: PR head `a240489` (the package merged with main `9817eb8`), against `include/loop.h` and the package's acceptance line (non-blocking pipe tests, the Ctrl-C test, 0 idle wake-ups). Everything below was run; the sources are in `reviews/package-10-repro/`.

**Verdict: changes requested.** The acceptance line is met and the API is right, so packages 13, 15, 16 and 17 can code against `loop.h` now. Four things must change before the merge: CI is red (finding 1), and the process end has two defects that the reference editor does not have (findings 2 and 3), plus a lost SIGKILL (finding 4).

## What was run

| Check | Result | Tag |
|---|---|---|
| `make check` on the PR merged with main `9817eb8` (same tree as `a240489`) | all passed, 10 min 38 s, 1 run | measured |
| `make planted` | 66 of 66 caught locally, 1 run; **in CI 65 of 66** (finding 1) | measured |
| `Pipe_queue`, `Break_ok` pass; `Pipe_block`, `Break_nocheck`, `Break_full` fail with a property violation | as each `.cfg` expects, 2 runs each (alone and in `check.sh`) | proved (TLC) |
| `test_loop` repeated, `IDLE_SECONDS=1` | 10 of 10 pass on a quiet machine, 10 of 10 pass with 8 busy loops on 4 cores | measured |
| Idle wake-ups, Ctrl-C | 0 wake-ups in 20 s (2 runs) and in 1 s (20 runs); the command stopped 300 to 301 ms after start, key sent at 300 ms (2 runs) | measured |
| 11 plants of my own (`extra-plants.txt`) | 4 caught, 6 not caught, 1 invalid (it breaks the build) (finding 7) | measured |

## Findings

### 1. CI is red: the plant "SIGPIPE kills the editor" is not caught there (blocks the merge)

CI's `make planted` on `a240489` printed `NOT CAUGHT loop: SIGPIPE kills the editor (make test TESTS=loop passed)`. GitHub's runner starts its steps with SIGPIPE ignored; `test_loop` inherits that, so the editor survives a write to a closed pipe without `loop.c`'s `signal(SIGPIPE, SIG_IGN)`.

Reproduced, 3 of 3 runs (`sigpipe-ci.sh` with `test_loop` built without that line): exit 141 with SIGPIPE at its default, exit 0 and all checks passing under `trap '' PIPE`.

Fix: `test_loop`'s `main` sets `signal(SIGPIPE, SIG_DFL)` first, so the check does not depend on who started it.

### 2. The exit is not reported while anything the child started holds its output; Neovim reports it at once

`loop.h` says the exit comes "as Neovim's on_exit". Measured with the same two commands (`holder.c`, `holder.vim`, 3 runs each, Neovim 0.12.6):

| Command | Neovim `on_exit` | PR 15 stream 0 |
|---|---|---|
| `sh -c 'sleep 3 & echo hi'` | 1 to 2 ms | 3003 ms |
| `sh -c 'setsid sleep 3 & echo hi'` | 1 ms | 3004 ms |

With a daemon instead of `sleep 3` the exit never comes and the slot (1 of 32) is never free again. The PR lists the setsid case as residual risk, but the first row needs no setsid, and the reference has neither problem: when the child exits, Neovim reads what is already in the pipes, closes them and reports. Copying that rule also removes finding 3 and is less code than waiting for both ends.

### 3. `proc_kill` after the child was reaped signals a process group that no longer exists, and can kill a stranger

Between the reap and the end of the output, `c->pid` is a pid the kernel has given back. `kill(-c->pid, SIGTERM)` then reaches whoever has that id as a group now, and `hard_kill` sends SIGKILL 2 s later. The comment above `hard_kill` ("-pid cannot reach a stranger before the slot is free") holds only while the group has members; with finding 2's child it has none.

Reproduced, 3 of 3 runs (`stalekill.c`, as root, choosing the pid through `ns_last_pid`): the child exits, its setsid grandchild holds stdout, another process gets the child's old pid and leads its own group as every shell job does, `proc_kill(id)` kills it with signal 15. This is exactly when a user stops a job: it looks stuck.

Without the fix to finding 2, the least fix is to send nothing once `c->code >= 0`.

### 4. The SIGKILL after an ignored SIGTERM is lost when the timer table is full

`proc_kill` takes one of the 16 timer slots and ignores `loop_timer`'s -1. Reproduced (`killtable.c`, children that ignore SIGTERM, 1 run each): 16 killed, 0 alive after 4 s; 20 killed, 4 alive; 32 killed, 16 alive. Stopping every job at exit is the obvious caller, and any other package's timers make it happen sooner. A deadline in `struct Proc`, taken into poll's timeout like the timers, cannot fill up and drops `hard_kill`'s use of the table.

### 5. `proc_write` of 0 bytes to an empty queue is undefined behaviour

`memcpy(c->q + c->qlen, b, n)` with `c->q == NULL`. Reproduced (`zero.c`): gcc's UBSan stops at `loop.c:207` "null pointer passed as argument 1", clang's at "applying zero offset to null pointer". Return 0 early when `n == 0`, or test for it.

### 6. Two lines no test or reason needs

The top rule: no line that is not necessary. Removing either leaves `make test TESTS=loop` green (my plants), and I found no scenario for them:

- `sigaddset(&def, SIGCHLD)`: the editor catches SIGCHLD, and exec resets a caught signal to its default. Only an ignored signal (SIGPIPE) is inherited.
- `posix_spawnattr_setsigmask` and `POSIX_SPAWN_SETSIGMASK`: nothing in the editor blocks a signal (no `sigprocmask` in `src/` or `ref/`).

Delete them, or add the test that fails without them.

### 7. Lines that tests do not hold (my plants, not caught)

- the guard `if (!c->pid || c->fd[s] != pf[k].fd) continue;` (a pipe closed by a callback in the same pass)
- the guard `w[i].fd == pf[k].fd + 1` in `loop_once` (a watch removed by a callback)
- `close_in(c)` after a failed write (without it the loop polls a broken pipe until the child exits)
- the queue's compaction (`memmove`): without it nothing fails; if it is there for memory, measure it

Each needs a test and a plant, or goes. `extra-plants.sh` reruns them.

### 8. No model for the timing of the exit

CLAUDE.md asks for a TLA+ model for timing questions about processes. Pipe.tla covers the write queue, not the order of SIGCHLD, the two ends of output and the report, which is where findings 2 and 3 are. State the property first ("the exit is reported once, after the output read so far, within a bounded number of loop passes of the child's exit, whatever else holds the pipes; no signal is sent to a reaped pid") with the current rule as the broken variant.

### 9. Small

- The 14 loop plants each wait out the 20 s idle check only when they are caught late; `IDLE_SECONDS=2` in each plant's target except the idle one would cut the cost (my 11 plants ran that way).
- CI's log shows an orphan `cat` after the loop plants: a failed `CHECK` exits without killing the test's children. Harmless in CI.

## What is good

The acceptance line holds: non-blocking writes (4 MB round trip, the dead.py order), Ctrl-C inside an endless command, 0 idle wake-ups. Both models state the property before the mechanism and their broken variants fail for the stated reason. The timing bounds in the test did not flake in 20 runs, 10 of them under load. The re-entrancy in `loop_once` is handled, and tested for `loop_breakcheck`.

## Ready for package 6's follow-ups?

Yes, with no header change.

- **Esc 50 ms wait**: `loop_timer(fn, ctx, 50)`. Timers round up, so it never fires early (plant caught), and they do not run inside a command.
- **Restoring the terminal on SIGTERM and SIGHUP**: `tty.c` already has a self-pipe on `loop_watch` for SIGWINCH; the same pipe carries these. `loop_once` returns 0 on a signal and watched fds also run inside `loop_breakcheck`, so the terminal is restored even during a long command. Two things for whoever does it: on SIGHUP fd 0 reports end of input on every poll, so `on_input` must stop the loop rather than spin; and children are not killed when the editor exits (Neovim stops its jobs then), which packages 13 and 15 will want, after finding 4.
