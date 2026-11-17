# Review of package 2 (PR 8), head 5b7f6d5

Independent review of the undo tree and its saved file (`src/undo/`, contract `include/undo.h`, UndoTree.tla, Undo.tla, Undo.lean) against DESIGN.md's package table, PROJECT-INSTRUCTIONS.md and CLAUDE.md. Only the last commit is package 2; the four before it are PR 7. Every finding was reproduced by running it. Repro sources are in `reviews/r2/`; the commands are given with each finding and run from the repo root. Tools: `tools.nix` (Neovim 0.12.6, TLC, Lean, Frama-C), gcc 13.3, clang 18.1.3.

**Verdict: changes requested.** The tree itself matched Neovim everywhere I looked, and the acceptance check holds. Two things have to be fixed before merge: `make check` fails now and then on this head because `tests/test_undo_file.c` runs on the real clock (finding 1), and `undo_read` accepts numbers that overflow on the next edit or move (finding 2). Findings 3 and 4 are small and cheap. Nothing here needs Davy.

## What I ran and what held

- **measured** (1 run, seed 20261117): `make check` passed: both compilers, 6 unit tests, 1,013 recorded traces, 3 fuzzers at 30 s (fuzz_undo 67,365 runs), 2,000 live key sequences, undo oracle 0 of 5,000, `undo_conform` 0 of 5,000 at undolevels 1000, 0 of 5,000 at 3 and 0 of 10 x 4,000 ops, Lean, Frama-C 77 of 77, all TLA+ configs as expected. CI on this head is green (run 44098995158).
- **measured** (1 run): `make planted` caught 27 of 27, the 10 undo plants and thread 0's oracle plant each with its own failure line.
- **measured** (1 run each, 21,030 sequences, 0 differ): the acceptance check, rerun with my own op mix (`reviews/r2/conform2.py`: `:undo N` over the whole number range where the package's only counts `e` edits, g-/g+ runs up to 30, time moves up to 40 s, clock steps up to 20 s, four different weightings). 5,000 x 80 ops at undolevels 1000 (seed 101), 4 (102) and 10 (103); 3,000 x 120 at 2 (104) and 50 (105); 10 x 4,000 at 1000 (106); 20 x 3,000 at 30 (107). So the size cap and `:earlier`/`:later` by time agree with Neovim on the shared clock.
- **measured** (1 run): the fake clock does reach Neovim: with `LD_PRELOAD=fake_time.so HOMEROS_TIME=1700000123`, `os.time()` and `undotree().entries[1].time` are both 1700000123.
- **measured** (200 x 400 random ops at undolevels 1000, 3 and 1; `reviews/r2/newhead.c`): the three facts undo.c's `Head` comment relies on hold in memory (3,247,167 headers looked at at 1000), and so does a fourth it relies on without saying: with no curhead, newhead has no prev and no alt_prev (46,491 states).
- **measured** (5 seeds, 6,142 to 6,897 groups each; `reviews/r2/span.c`): what `UndoWhere` promises holds: bytes outside `[lo, hi)` are unchanged, `cur` is the cursor given when the group opened, `flags` are the cursor's after an undo and the group's `after` after a redo, also after a round trip through the file.
- I compared `undo_time`, `undo_step`, `open_group`, `freeoldest` and `freebranch` with Neovim's `undo_time`, `u_savecommon`, `u_freeheader` and `u_freebranch` line by line and found no rule missing other than the ones the PR names. **hypothesis** until run, which is what the differential runs above are for.

## Findings to fix before merge

### 1. `tests/test_undo_file.c` runs on the real clock, so `make check` fails now and then

The test plays the same ops on a tree and on its copy read back from the file and requires the same text and number after each. New edits take `time(NULL)`; when the second changes between the two copies' edits, their timestamps differ and a later `:earlier Ns` lands elsewhere. The fuzzer and the driver fake `time()`, this test does not.

- **measured**: 2 of 48 runs failed with 6 running at once (`for i in $(seq 48)` through `xargs -P 6`), 0 of 30 one after another. One failed at `test_undo_file.c:55` (`same()`), the other with finding 2's overflow at `undo.c:252`.
- **measured** (deterministic, 1 run each): with a clock that ticks every N calls of `time()` linked in (`reviews/r2/tick.c`): N = 997 and 5,000 fail at line 55, N = 20,011 fails with the overflow at `undo.c:252`, N = 100,003 passes.

```
gcc -DTICK=997 -std=c17 -D_DEFAULT_SOURCE -Iinclude -O1 -g -fsanitize=address,undefined \
  -fno-sanitize-recover=all -o build/tuf tests/test_undo_file.c reviews/r2/tick.c ref/text.c src/undo/undo.c && ./build/tuf
```

Fix: give the test its own `time()` as `fuzz_undo.c` and `undo_drv.c` have, and step it inside `op()` so that time moves are still exercised. With the plant for this (a clock that ticks between the two copies) the old test must fail.

### 2. `undo_read` accepts numbers that overflow later (undefined behaviour)

`seq_last`, `seq_cur`, `save_last`, `save_cur`, `time_cur` and each header's time are taken from the file as they are (`undo.c:660` on, `:609`). The hash covers the text, not the file, so one damaged byte gets through.

- **measured**: my libFuzzer harness on the reader (`reviews/r2/fuzz_read.c`: arbitrary file body behind the right magic and hash; if accepted, 60 random ops and a write and read back) stopped after 7,140 runs: `undo.c:135:24: signed integer overflow: 9223372036854775807 + 1` (`++u->seq_last` on the next edit). With that bounded, after 196 seed files: `undo.c:267:33: -9223372036854775808 + -2` (`time_cur + step`). The repo's own damage test hits a third place, `dist()` at `undo.c:252`, in the runs of finding 1.

```
clang -std=c17 -D_DEFAULT_SOURCE -Iinclude -O1 -g -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all \
  -o build/fuzz_read reviews/r2/fuzz_read.c ref/text.c src/undo/undo.c
./build/fuzz_read reviews/r2/read-seq-overflow.bin; ./build/fuzz_read reviews/r2/read-time-overflow.bin
```

- **measured** (334,038 runs in 241 s): with every number from the file refused above 2^40 (a scratch patch, not in the PR), the same harness finds nothing more: no crash, no leak, every accepted file writes and reads back.

Fix: refuse the file when a number is out of range: `seq_cur <= seq_last`, `save_cur <= save_last`, every header's `save <= save_last`, and `seq_last`, `save_last`, `time_cur` and every header's time below a bound that leaves room for the arithmetic. A fuzz target on the reader would have found this in seconds; `fuzz_read.c` can be taken as it is (as `tests/fuzz_undo_read.c`, with its seed writer), with a plant.

### 3. A file whose newhead is not the newest change of its line is accepted

`open_group` relies on a fact the reader does not check: with no curhead, newhead has no prev. If the edits are alike (typing the same character), a file with newhead changed still applies to the text.

- **measured** (1 run, `reviews/r2/newhead.c`): three changes "x" typed at the start, one byte of the file changed (newhead 3 to 1): accepted. After one more edit numhead is 4 but `u` undoes only 2, the text it calls the oldest is "xx" where it was "", and LeakSanitizer reports changes 2 and 3 (306 bytes in 6 allocations).

```
gcc -std=c17 -D_DEFAULT_SOURCE -Iinclude -Itests -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
  -o build/newhead reviews/r2/newhead.c ref/text.c && ./build/newhead
```

Fix: in the check at `undo.c:680`, also refuse `!curhead && newhead && (newhead->prev || newhead->alt_prev)`. The PR text says a damaged file is refused instead of corrupting text; this case leaves the text alone but loses history, so the sentence should say what is checked.

### 4. Parts of the contract no check would catch if broken

I planted six bugs outside the package's ten (scratch copies, as `planted.sh` does) and ran the package's checks (**measured**, 1 run each):

| Plant | `make test TESTS="undo undo_file"` | `fuzz FUZZERS=undo` 20 s | `conform` 2,000 |
|---|---|---|---|
| A: `flags` after an undo are the group's `after` | passes | passes | caught, 2 of 2,000 differ |
| C: the file loses the saved cursor | passes | passes | not run (no file there) |
| D: `hi` too small after a redo (`tail = l - e->pos`) | passes | passes | passes |
| E: the file loses a change's time | caught | passes | |
| F: the file loses the write marks | caught | passes | |

(Plant B, the file losing `after`, passed `test` when rerun alone; its one failure was finding 1.) So `UndoWhere`'s span is checked only for `lo <= hi <= len`, the cursor and flags through the file not at all, and the flags after an undo by 2 sequences in 2,000 of another package's check. `reviews/r2/span.c` catches A, C and D (1 run each: fails at its lines 32, 30 and 22). Fix: take it as a unit test and add plants A, C and D.

## Observations, not blocking

- **measured** (`reviews/r2/perf.c`, -O2, `ref/text.c`; 5 alternating runs at 10 MB): `undo_write` with no changes takes 0.102 s median (0.099 to 0.107) for 10 MB and 1.0 s for 100 MB (1 run), all of it the SHA-256, at about 100 MB/s, on the one event loop at every `:w`. Neovim hashes the buffer there too. `sha_block` moves seven words per round (`memmove`) and reads the text 64 bytes at a time; if this shows up in the phase gate's numbers, that is where to look.
- **measured** (same runs): `undo_read` of 1,001 one-byte changes on 10 MB takes 0.78 s median (0.73 to 0.83) against 0.12 s (0.115 to 0.125) with none; 100 MB: 8.3 s against 1.1 s (1 run). The difference is the replay of every change on a copy of the text, which costs a `memmove` per change with the array. **hypothesis**: small with the rope; worth measuring again when package 1 lands. The copy also doubles the memory for the text while a file loads.
- `test_undo_file`'s line "N damaged files (M still consistent)" counts 1,582 of 4,000 one-byte damages as accepted. Most are in fields that cannot be checked against the text (cursor, time, flags). That is the same as Neovim, whose undofile has no checksum either, but a checksum over the file would turn all of findings 2 and 3 and these into "refused" with one rule. Not asked for here: it is a new mechanism, and the range checks do the job.
- `undo_write` leaves `path.tmp` behind only if the process dies between `fopen` and `rename`; where the file lives and its naming are the core's (package 4), as the PR says.
- `'undolevels' <= 0` is not implemented, as the PR says; FEATURES.md has 1,000, so nothing is missing.

## After the fixes

Findings 1 and 2 are needed for approval; 3 and 4 are a line and a test and should come with them. I will rerun the 48 parallel runs of `test_undo_file`, the reader fuzzer for 240 s, `newhead`, the span test with its plants, `make check` and `make planted` on the new head. PR 7 has to be on main before this merges.

## Second pass, head a704905

The author cherry-picked this report and applied all four findings. Rerun on the new head:

- **measured**: finding 1: `test_undo_file` passed 48 of 48 with 6 running at once; it has its own clock now (so `tick.c` no longer links with it), and a plant for it.
- **measured**: finding 2: `tests/fuzz_undo_read.c` (this review's harness; the read back after the 60 ops may now be refused, since numbers read just under the bounds can grow past them) ran 238,551 times in 241 s from the earlier corpus with nothing found; both kept inputs pass.
- **measured** (1 run): finding 3: `newhead` prints "damaged file refused".
- Finding 4: `tests/test_undo_span.c` is `span.c`, with plants A, C and D.

### 5. `undo_time` overflows on a count at the end of `long`'s range

- **measured** (1 run, `reviews/r2/step.c`): one change, then `undo_time(u, LONG_MAX, UNDO_SEQ)`: `undo.c:283:17: signed integer overflow: 1 + 9223372036854775807`. The same sum is made for seconds and writes (lines 267, 274, 277). `undo.h` gives no range for `step`, and `:later 9223372036854775807` reaches it if the core passes the number on.

```
gcc -std=c17 -D_DEFAULT_SOURCE -Iinclude -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
  -o build/step reviews/r2/step.c ref/text.c src/undo/undo.c && ./build/step
```

Fix: clamp `step` at the top of `undo_time` for the relative units (to the reader's bounds, which every stored number is below), which changes no result since the target is clamped to the ends anyway; add the extreme counts to `fuzz_undo` or a unit test, with a plant. I missed this in the first pass; it is not from the fixes.
