# Review of package 12 (syntax colouring, PR 9)

Independent review against `include/syntax.h`, DESIGN.md (row "Syntax", gate "frame budget held with tree-sitter on") and CLAUDE.md. First pass on head ac15572, second on the rebased head e92b0a6. Every finding was reproduced by running it; the files are in `reviews/package-12-repro/` (`build.sh` builds the drivers). Tools: design/env.nix (Neovim 0.12.6, TLC, Lean, Frama-C), gcc 13 and clang 18 from apt, 4 cores.

**Verdict: changes requested.** The colours are right: I could not make them differ from Neovim's, with the package's own oracle or with a second one I wrote that records what Neovim's highlighter really draws. What is not shown is the package's one acceptance gate. On this machine syntax's own share of a key is over 2 ms p99 on two of the five benchmark files, and the reparse runs far longer without asking `stop` than syntax.h says (finding 1). Findings 2 to 4 are small and should go in with the same push. Nothing here needs Davy, except that finding 1 is the measurement DESIGN.md asks for before "no threads" is revisited.

## What I ran and what held

- **measured** (1 run, ac15572): `make check` passed whole (test_syntax 0 of 120 cases, 0 of 494 frames, 0 of 4,000 Lua patterns; fuzz_syntax 136,923 runs in 31 s; 2,000 live key sequences, seed 1794926829, 0 differ; undo-diff; Lean; Frama-C 77 of 77; all TLA+ configs). `make planted` caught 25 of 25.
- **measured** (e92b0a6): the whole check cannot run, because `main` itself does not build since PR 7 merged (package 6's `src/grid`, `src/tty` and their tests predate PR 7's `Cell.face` and `backend_tty`; reported to the coordinator, fix is PR 13). On e92b0a6 merged with main 26295f8, with `src/grid` and `src/tty` set aside locally: `make compilers` passes, `make test TESTS=syntax` passes with SEED 31, 32, 33 and CASES 100 (0 of 900 cases, 0 of 3,766 frames differ from Neovim), and all 7 plants of `tests/plants/syntax.txt` are caught. **The full check on a building main is still owed before the merge.**
- **Colours against what Neovim draws, measured.** `screen.lua` opens a file in `nvim --headless`, scrolls a window to a random line, lets Neovim redraw, and records every extmark `highlighter.lua` sets (it wraps `nvim_buf_set_extmark`); `dump.c` asks package 12 for the same window the way `core_draw` does. `screen.sh 100 SEED`, 10-line windows of C (repo and tree-sitter sources), Lua (Neovim's runtime) and Markdown:
  - ac15572 (coarse faces), seeds 1 and 2: 1 of 293 and 0 of 291 windows differ.
  - e92b0a6 (90 faces), seeds 1 and 2: 1 of 289 and 0 of 290 windows differ (0 of 2,698 lines on seed 2).
  - The one differing window both times is `_query_linter.lua` line 63, a Lua string that Neovim colours with its `query` grammar. Package 12 does not build that grammar, which the PR states.
  - This oracle is independent of `tests/test_syntax.lua` in what matters: Neovim's real redraw from the window's top line, its own iteration, clipping and priorities. It shares one rule with it (later mark wins a priority tie).
  - Reading the screen cells with `nvim__inspect_cell` was tried first and dropped: without a UI the attribute ids were stale (3 runs, 3 different answers).
- **Window against whole text, measured.** `window.c` drives one Syntax as `src/main.c` does (40-line window, random jumps, scrolls and edits, 400 rounds) and a second one that is shown the whole text, and compares the window after every parse.
  - ac15572: 0 of 400 windows differ in each of 7 files (C, Lua, Markdown) on seeds 2 to 5, and 0 of 300 in 3 files on seed 1.
  - e92b0a6: 0 of 400 in keys.c, lsp.lua and DESIGN.md on seeds 1 to 3. README.md differs in 22 to 104 of 400. I traced the first (seed 1, round 53) to tree-sitter, not to the package: the whole-text Syntax equals Neovim replaying the same 30 edits and parses byte for byte (`tests/test_syntax.lua` on the recorded ops), and both lack an italic that a fresh parse of the same text has, in Neovim too. The tree has no error node. So markdown_inline's incremental result depends on history **without** a syntax error; the author's new commit found the same and counts it apart.
  - The check can fail: with the injection query's range planted 300 bytes late, DESIGN.md differs in 6 of 300 windows.
- **The PR's claims, checked.** `#offset!` is used only where the PR says (grep of the four languages' queries). `any-of?`, `has-ancestor?` and `has-parent?` pass when one node passes, as query.lua's handlers do (read against Neovim 0.12.6's source, then exercised by plant x4). The PR text says 7 plants at ac15572; the file had 6 (it has 7 now).

## Findings

### 1. The frame gate is not shown to hold, and syntax.h's stop interval is wrong

`src/syntax/bench/run.sh` reproduced with a variant that also prints the parts (`frame2.c`, `run2.sh`; same loop, same seeds). Head e92b0a6 with package 1's rope, 5 runs of 200 keys, own p99 per key in ms (median, range), **measured**:

| file | PR's figure | here | runs over 2 ms |
|---|---|---|---|
| ref/core.c | 0.51 (0.43–1.20) | 1.01 (0.84–2.72) | 2 of 5 |
| xdisp.c | 1.23 (0.59–2.00) | 1.00 (0.77–1.83) | 0 of 5 |
| coding.c | 1.84 (1.55–2.04) | 2.22 (1.93–2.70) | 3 of 5 |
| design/DESIGN.md | 1.30 (1.19–1.74) | 2.40 (1.01–2.98) | 3 of 5 |
| lsp.lua | 0.44 (0.27–0.57) | 0.66 (0.43–1.12) | 0 of 5 |

The raw lines are in `bench-rope.log`. This machine is about 1.5 times slower than the author's on the small files, so the two sets agree; the point is that the margin is a machine's speed, not a property. On ac15572 with `ref/text.c` it was worse (coding.c 2.82, DESIGN.md 2.70, xdisp.c 3.13 median).

Where the time goes (same runs):

- **DESIGN.md: `syntax_faces`.** p99 1.0 to 2.4 ms, median up to 1.27 ms, with the parse nearly never late. Cause not measured; **hypothesis**: one query per injected inline region in the window.
- **coding.c: `syntax_edit` and the stop.** `syntax_edit` alone has p99 1.5 to 2.0 ms there, against 0.25 to 0.40 on xdisp.c, which is 3.5 times larger. `ts_tree_edit` is not it (0.004 ms per edit, 1.5 trees, measured). **Hypothesis**: `ts_parser_reset` of a suspended parse, since on coding.c 114 to 143 of 200 parses are stopped by the next key.
- **The stop interval.** syntax.h says stop is asked "at most 0.86 ms apart". `gap.c` types 460 keys and records the longest stretch of `syntax_update` between two asks (start and return count as asks), 5 seeds, **measured**:

| file | median of the longest stretch | p99 | max | keys over 0.86 ms |
|---|---|---|---|---|
| coding.c | 1.34 to 1.46 ms | 2.5 to 3.2 | 6.2 | 454 to 460 of 460 |
| xdisp.c | 0.33 to 0.58 | 0.95 to 2.75 | 4.3 | 7 to 87 |
| DESIGN.md | 0.33 to 0.42 | 0.75 to 1.15 | 2.5 | 0 to 12 |
| ref/core.c | 0.13 to 0.14 | 0.23 to 0.50 | 1.2 | 0 to 3 |
| lsp.lua | 0.17 to 0.25 | 0.71 to 1.48 | 3.0 | 1 to 7 |

  The first parse of xdisp.c runs 20.8 ms without asking (1 run; 20.6 ms on the first head). In the benchmark a key waited up to 16.6 ms for the parse to come back (xdisp.c, 1 of 1,000 keys). Callgrind puts 90% of `syntax_update` in `ts_parser_parse`, so the long stretches are inside tree-sitter, between its progress callbacks (**hypothesis** for which step).

What I ask for: say in the PR what was measured (the gate is not held on every file on a machine 1.5 times slower); find the coding.c cost in `syntax_edit` and the DESIGN.md cost in `syntax_faces`, or record them as open with numbers; and ask thread 0 to correct syntax.h's "0.86 ms". DESIGN.md says a broken budget from tree-sitter parsing is the first reason to revisit "no threads", with a measurement. This is one, so it should reach Davy with the phase 0 gate, not block this merge.

### 2. 26 lines copy a function tree-sitter exports

`range_edit`, `padd` and `psub` are a hand copy of `ts_range_edit`, which tree-sitter 0.27's `api.h` declares. With `ts_range_edit.diff` (1 line in, 27 out) `test_syntax` passes with SEED 21 and 22, CASES 100: 0 of 600 cases and 0 of 2,536 frames differ (**measured**, 2 runs; 1 more run of 450 cases on the first head). "Delete before you add."

### 3. Code the four languages never run, and that no check reaches

17 more bugs planted (`extra-plants.txt`, run by `extra-plants.sh` on e92b0a6 merged with main, 1 run): 8 caught, 2 invalid (they broke the build), 7 not caught:

| plant | why the check passes |
|---|---|
| x9, x10: `syntax_faces` for 8,192 bytes or more keeps old priorities, or colours the first chunk only | that branch's output is compared with nothing, and the editor never takes it: `core_draw` asks for at most 1,024 bytes |
| x12: combined injections not merged | the only combined injections in the four languages are of HTML, which is not built, so `icombined`, `Q->full` and the per-pattern merge never run |
| x13: `@injection.filename` ignored | no query of the four uses it |
| x15: `#contains?` always true | no query of the four uses it |
| x11: `any-of?` must hold for every node | no quantified capture reaches it; equal on these queries |
| x3: captures not clipped to the tree's included ranges | not hit in the default 120 cases |

Also unused by the four languages' queries (grep): `injection.parent`, the `any-` forms, `#eq?` with two captures. By the top rule each of these is deleted until a language Davy uses needs it and brings its test, or it gets a case and a plant now. For x9 and x10 the simplest is to make the contract say "at most CHUNK bytes" (thread 0) and delete the loop. x3 needs a targeted case (a capture that crosses a gap in an injected region).

### 4. The editor finds its queries through an absolute path into `build/`

`build.mk` compiles `SYNTAX_QUERIES` as `$(CURDIR)/build/syntax/src/queries`, a symlink into the nix store. After `make clean`, or if the binary is moved to another machine, every file opens without colours and without a message (**measured**: `dump.c` built with a missing directory prints face 0 for every byte, exit 0). Fine for the check; not for the editor Davy runs. Either embed the eight `.scm` files in the binary (also removes file reads at start-up) or record it as open for the install step.

## Smaller notes

- `test_syntax` in `make test` uses a fixed seed, so every merge runs the same 120 cases; `conform` takes a fresh seed each run. `SEED=$(date +%s)` in CI would add coverage for free (thread 0's Makefile).
- `syntax_open` keeps the language in `s->root` as a cast integer until the first update (`started()`). It works; an `int` field is plainer.
- Lua strings that inject `vim` or `query`, and Markdown blocks in languages not built, keep the surrounding colour where Neovim colours them. Stated in the PR; listed here as the visible difference from Neovim.

## Before the merge

1. Main builds again (PR 13), PR 9 rebased on it, whole `make check` and `make planted` green there and in CI.
2. Findings 2 and 3 applied; finding 1's PR text corrected and its two costs found or recorded; finding 4 fixed or recorded.

## Second pass: head 8b9b393 (c8b1f88 answers the review, main ab4bcd4 merged in)

**Verdict: approved for merge, with the Markdown frame share and the stop interval left open and recorded below.**

What I ran:

- **measured** (1 run): `make check` passes here in every target except `win`, which needs Wine that this machine lacks: both compilers, all unit tests under ASan and UBSan (test_syntax 0 of 120 cases, 0 of 508 frames, 0 of 4,000 Lua patterns), 1,014 recorded traces, the fuzzers (fuzz_syntax 130,176 runs in 31 s), 2,000 live sequences (seed 1794934422), undo-diff and undo_conform, Lean, Frama-C 77 of 77, all TLA+ configs. CI run 44128817146 on this head is green, and it includes `win` and `make planted`.
- **measured** (1 run): `make planted` here caught 93, among them all 8 syntax plants; every plant not caught here is a `win` plant that failed for the missing Wine.
- **measured**: `screen.sh 100`, seeds 1 and 2: 1 of 295 and 0 of 294 windows differ from Neovim's real redraw; the one is again the `query` string in `_query_linter.lua`.
- **measured**: `window.c`, 400 rounds, seeds 1 to 3: 0 windows differ in keys.c and lsp.lua; DESIGN.md differs once (seed 1, round 303), and there the whole-text Syntax again equals Neovim replaying the same edits, both differing from a fresh parse (Markdown's history-dependent trees).

The findings:

1. **Frame share.** `syntax_edit` no longer frees the stopped parse: its p99 is 0.01 to 0.07 ms on every file (was 1.5 to 2.0 on coding.c). `src/syntax/bench/run.sh 5 200`, own p99 per key in ms (median, range), **measured**:

   | file | first pass | now | runs over 2 ms |
   |---|---|---|---|
   | ref/core.c | 1.01 | 0.88 (0.76–1.63) | 0 of 5 |
   | xdisp.c | 1.00 | 0.62 (0.52–0.84) | 0 of 5 |
   | coding.c | 2.22 | 1.19 (0.57–1.64) | 0 of 5 |
   | design/DESIGN.md | 2.40 | 2.14 (1.48–2.58) | 3 of 5 |
   | lsp.lua | 0.66 | 0.60 (0.35–0.72) | 0 of 5 |

   **Open:** Markdown. On this machine (about 1.5 times slower than the author's, where 1 of 5 runs was over) `syntax_faces` alone uses the 2 ms budget on DESIGN.md. The author measured the split (57% the block query, 43% the inline queries). The gate "frame budget held with tree-sitter on" is therefore held for C and Lua and not shown for Markdown; it must be closed, by a faster path or by a measurement on Davy's machine, before the phase 0 gate.

   **Open:** the stop interval. `gap.c`, 5 seeds of 460 keys: coding.c median 0.87 to 1.01 ms (was 1.34 to 1.46), p99 2.1 to 3.0, max 6.3; xdisp.c p99 0.9 to 2.0, max 4.1; the first parse of xdisp.c 27.9 ms (1 run). These stretches are inside `ts_parser_parse`. syntax.h still says 0.86 ms; thread 0 has been asked to correct it. A key that arrives in such a stretch waits that long, which is the measurement DESIGN.md names as the reason to revisit "no threads".
2. **Fixed.** `ts_range_edit` is used; the plant follows.
3. **Fixed**, by deleting: `contains?`, the `any-` forms, two-capture `#eq?`, `injection.parent`, `injection.filename`, combined and include-children injections (those patterns are disabled; all are of languages not built) and capture-level priority (grep: no `#set! @capture priority` in the four languages). The block quote case catches the clipping plant. x9 and x10 (ranges of 8,192 bytes or more) wait for thread 0 to bound `syntax_faces` in syntax.h.
4. **Fixed.** The eight queries are compiled in (`build/syntax/queries.c`); no file is read at run time.
