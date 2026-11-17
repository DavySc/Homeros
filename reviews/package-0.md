# Review of package 0 (PR 1), head ad09f0e

Independent review against `design/` (DESIGN.md "Building it as a Claude project", PROJECT-INSTRUCTIONS.md, CLAUDE.md) and for correctness. Every finding below was reproduced by running it; the results are given with their run counts, and the repro sources are in the project files under `reviews/package-0-repro/` (worth copying to `reviews/r0/` in the repo with the report). Tags as CLAUDE.md asks.

**Verdict: changes requested.** One of the two CI runs on this head is red for a real reason (finding 1), and findings 2 to 6 are small but sit in the foundation every package forks from. Nothing here needs Davy.

## What I ran and what held

- **measured** (1 run): `diff -r` of `design/` against `native-editor-handoff.tar.gz`: identical. The four skills are byte-identical to pstack commit e1cc031.
- **measured** (1 run, tools from `design/env.nix`, gcc 13.3, clang 18.1): `make check` passed in 11 min 9 s: both compilers, 5 unit tests, 1,000 recorded traces, fuzzers, 2,000 live sequences (0 differ with that run's seed), undo oracle 0 of 5,000, Lean, Frama-C 51 of 51, TLC 35 of 35 as expected.
- **measured** (1 run): `make planted` caught 16 of 16, each with its own failure line, in 1 min 9 s.
- **measured** (1 run): thin editor in tmux: open, `A edited`, `j dd u`, `:wq`; file on disk as expected. Package 0's first acceptance clause holds.
- **measured** (1 run): a heap overflow planted in `ref/undo.c` (`malloc(n ? n - 1 : 1)`) is reported by ASan under `make test TESTS=undo`, so the sanitizers are live.
- Headers, `ref/`, `src/main.c` (Render "ok": batch of 64, draw between commands) and CLAUDE.md follow the design's split. PR attribution, pinned skills and licence are in place.

## Findings to fix before merge

### 1. `make conform` fails on a fresh seed: two differences from Neovim, one of which corrupts text

CI ran twice on this head: run 44074983489 passed and run 44075028192 failed in `make conform`, which differ only in the seed (`date +%s`). The failing seed is 1794915425. **measured** (13 seeds, 2,000 sequences each: 1794915424, ...426, 21 to 25 and 27 to 30 give 0 differences; ...425 gives 2 and then a Python traceback; 26 gives 1). Minimised inside the generator's grammar:

```
text 'bar_1\n\t(  foo --\né --\n\t\n'   keys  G dd 3u <C-r>
  nvim cursor [3, 4]    ours [3, 0]
text '--\n  λx (\n'                     keys  w I <BS> x <Esc> 4u <C-r>
  nvim cursor [2, 1]    ours [2, 2]     (lines 'xλx (': byte 2 is inside λ)
```

Both are a counted `u` that runs out of history, then `Ctrl-R`. In the second the cursor is left in the middle of a multi-byte character; the next insert there writes invalid UTF-8 into the buffer, which is what crashed `conform.py` in CI. `core_undo` restores a saved byte column into a line whose content differs and `core_clamp` only clamps to the line length.

So the PR's "0 of 10,000" is true for seeds 11 and 1 and not for the key set: at 3 in 26,000 fresh sequences, a merge's check fails about one time in seven and package 5's gate (10,000 match) is not safe. Fix the rule, add both cases to the recorded traces, and check the fuzzer's invariant too: `fuzz_keys` should abort when the cursor is not on a character boundary, since it did not catch this.

### 2. `tools/conform.py` crashes on the failure it exists to report, and does not print its seed

Same run: the driver printed invalid UTF-8, `subprocess.run(..., text=True)` raised `UnicodeDecodeError`, and the summary line never came. A crash must count as a difference (decode with `errors='replace'` or compare bytes). `make conform` picks `date +%s` as the seed but nothing prints it; I recovered it from the CI log's timestamp. Print the seed on the summary line.

### 3. Save can overwrite an outside change in two windows that Save2's `NoLossAtAll` forbids

`file_load` takes the file's identity after reading (`ref/file.c`, "the identity is taken after reading, so a write during the read is seen at save as a change, never lost"). It is the other way round: a write during the read is folded into the identity, so the save sees no change.

- **measured** (5 of 5 runs): load a 1 GiB file while another process overwrites its first bytes 60 ms in; the buffer lacks the outside write, and `file_save` returns `SAVE_OK` and replaces it. Expected `SAVE_CHANGED`. Take the identity with `fstat` before the first `read`.

`file_save` ends with `stat(real)` by path to refresh the identity. In Save2 `seen' = name.gen + 1` is part of the Rename step; here another program can write between the swap and that `stat`, and its version becomes "ours".

- **measured** (3 of 3 runs, the outside write injected right after `renameat2` returns via an `LD_PRELOAD` shim): the next plain save returns `SAVE_OK` and the outside version is gone. Take the identity from `fstat` on the temp file's descriptor after `fsync`, before the swap; inode, size and mtime do not change with the rename.

### 4. Terminal input corrupts or drops typed text at the 256-byte read boundary

`backend_input` reads 256 bytes and decodes each read on its own.

- **measured** (3 of 3 runs in a pty): `a` plus 200 `é` arriving in one burst gives a file with 199 `é` and `Ã©` where the read split a character. A paste of non-ASCII text longer than 256 bytes is silently corrupted.
- **measured** (3 of 3 runs): 300 `a` arriving together with a window resize gives 299: `KEY_RESIZE` takes a slot of the 256-entry output and the last byte of the read is dropped.

The PR lists "escape sequences split across reads" as package 6's; these two lose text and are a few lines here: keep an incomplete trailing sequence for the next read, and never read more bytes than there are output slots left.

### 5. `uni_prev`'s contract does not state the result; `uni_cp` is not proved at all

CLAUDE.md: kernels "carry Frama-C contracts that state the result, not only bounds". DESIGN.md records this exact trap for the first UTF-8 contract ("`return 1;` proves the same contract").

- **measured** (1 run): with `uni_prev`'s body replaced by `return i - 1;`, `make frama` passes, 37 of 37 goals. Its `ensures \result == i - 1 || wellformed(...)` accepts that. The contract needs the other direction: if a well-formed sequence of length 2 to 4 ends at `i`, the result is its start. `tools/planted.sh` should then plant `return i - 1;` beside the `uni_len` plant.
- **measured** (1 run): `uni_cp` under the same WP options proves 3 of 6 goals (it has no contract and `-wp-fct` leaves it out). It is the UTF-8 decoder, so either give it a result contract or tag it in README as not proved.

Also, DESIGN.md names package 3's contract as "uni.h with result contracts" and package 1's as "text.h with ACSL contracts". The ACSL is in `ref/uni.c`, which package 3 replaces, so the package would write the contract it is judged by. Move the predicates and function contracts to `include/uni.h` (Frama-C takes them from the declaration). `text.h` has no ACSL; add it or record under "Choices made in package 0" that it waits for package 1's request.

### 6. Every package has to edit files it does not own

DESIGN.md cuts the work along headers because "parallel agents break each other's work when they share files". As the harness stands, a package thread that adds a test, a fuzzer or a plant must edit `Makefile` (`TESTS`, `FUZZERS`, and `MODULES` for packages 7, 8, 9, 12 and later) and the `PLANTS` list inside `tools/planted.sh`. CLAUDE.md gives `tools/` to thread 0 and says "a new check adds one [plant]"; `Makefile`, `README.md` and `.github/` have no owner in the Layout table. Twelve parallel threads appending to the same three lines will conflict or break the ownership rule.

Make the shared files discover instead of list: `TESTS` and `FUZZERS` from `tests/test_*.c` and `tests/fuzz_*.c`, modules from `src/*/`, plants from one file per package (for example `tests/plants/<module>.txt` read by `planted.sh`). Then say in the Layout table who owns `Makefile`, `README.md` and `.github/`.

README's "Packages 1 to 12 can start now" also needs a caveat: packages 7 and 12 are specified against `font.h` and `syntax.h`, which do not exist, so they start by asking thread 0 for the header.

## Smaller things, cheap to fix in the same push

7. **measured** (1 run, umask 022): a new file is created with mode 600 (the `mkstemp` default); Neovim creates 644. Apply `0666 & ~umask` when the file did not exist.
8. **measured** (1 run, strace): for a new file given as a bare name (`homeros new.txt`) the directory is never fsynced: `openat("new.txt", O_DIRECTORY) = ENOTDIR`. `dirname()` returns a pointer to "." without writing it into `dir`, and the result is not used. With `./new.txt` the fsync happens.
9. **measured** (1 run each, against nvim 0.12): `dd` on a one-line file then `:w` writes 1 byte (`\n`); Neovim writes 0. Same for an empty file after `ix<Esc>u`. `core_write` does not look at `CORE_EMPTY`.
10. CI runs the whole check twice per push to a PR branch (`on: [push, pull_request]`; two runs were started 19 s apart). Keep `pull_request` and limit `push` to `main`.
11. `fuzz_keys` runs 65 s locally and 87 s in CI for `FUZZ_SECONDS=30`, at about 400 runs per second (**hypothesis** for the cause: each run reopens a file and the reference text is O(n) per call). A `-max_len` would keep it inside its time.
12. PROJECT-INSTRUCTIONS.md and CLAUDE.md ask for the review report under `reviews/`. Please add this text unchanged as `reviews/package-0.md`; I will check it at the re-review.

## Not blocking, for the packages that inherit them

- No check would notice `fsync` being removed from `file_save` (Save2_nofsync's bug in the code, not the model). Package 11's crash tests should cover it.
- `core_line` can `realloc` inside `core_draw` when a longer line scrolls in; package 4 owns the no-allocation rule for the redraw path.
- Package 1's differential fuzzer needs `ref/text.c` and `src/text/*.c` in one binary, and both define `text_new`; the harness has no answer yet.

---
_Generated by [Claude Code](https://claude.ai/code)_
