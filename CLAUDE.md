# Homeros: rules for every agent

This repo builds a native text editor for one person, Davy. The design, its evidence and its plan are in `design/`: start with `design/README.md`, then `design/DESIGN.md` and `design/FEATURES.md`. The plan for agents is `design/PROJECT-INSTRUCTIONS.md` and DESIGN.md's "Building it as a Claude project".

## The top rule (Davy's words)

"The goal is absolute perfection and achieving feature completeness with maximum simplicity. In the ideal world no line of code is written that isn't absolutely necessary and no redundant machine instruction is ever made/no clock cycle is ever wasted."

So: the smallest change that does the job; delete before you add; no speculative generality, no plugin system, no option nobody asked for. Personal fit beats generality: if Davy does not use it (FEATURES.md), it is not built.

Davy, 2026-11-18: "I want as little code duplication as possible in the codebase." So: before writing a function, look for one that exists: the headers in `include/` (every shared function, with what it does) and `make index | grep <word>` (every function defined, private ones too); use it; when two places need the same code, it moves to one module both call, through a header (thread 0 for `include/`). `make dupes` (in `make check`) fails on any 50-token run inside function bodies that occurs twice, on two functions of 15 tokens or more that are the same once their locals are renamed, and on a mechanism's telltale outside its owner (`tests/owners.txt`: one owner per mechanism, as Davy asked 2026-11-21: "I don't want multiple ways to do the same thing"), except the findings in `tests/dupes-known.txt`, which only shrinks; a new shared mechanism gets an owners line. A review flags shorter copies too, and what the check cannot see (measured in PR 66's review): a copy with one identifier renamed inside a larger function, a mechanism written without its owners.txt telltale (`128 +`, `- 48`, `strtol`), copies in `tests/` and in macro bodies, a line added to the known list, a listed copy that grows past its first 50 tokens.

Davy, 2026-11-18: "Simplicity and minimal lines of code is always best (So long as the solution satisfies functional and technical requirements of course)". So between two solutions that both meet the requirements (FEATURES.md, DESIGN.md's gates, the checks), the shorter and simpler one wins; a review flags code that could be shorter without losing a requirement.

## New approaches

"You're allowed to try never seen before approaches to problems, but you're only allowed to settle on them if they're better than any combination of existing approaches in all scenarios. Remember that we might not be able to think of all possible scenarios."

"Something is only better after you have accumulated a lot of definite proof, this may be a formal proof of its properties or the results of a large body of measurements and tests."

Prefer copying the behaviour of a reference editor (Neovim 0.12 first; Helix, Emacs, VS Code for Lean, SSMS for SQL, as DESIGN.md names per behaviour). A new mechanism needs: the alternatives compared, the scenarios listed, the evidence, the residual risk, in DESIGN.md's "New approaches" table.

## Evidence

- Tag every claim you write down: **proved** (formal proof or exhaustive model check), **measured** (runs recorded, with the count), or **hypothesis**.
- Run things; do not conclude from reading code. Record what you ran and how many times.
- Timing questions (event loop, frame callbacks, LSP, saving, processes) get a TLA+ model checked by TLC, with the property stated before the mechanism and at least one broken variant that must fail. Each `.cfg` starts with `\* expect: pass` or `\* expect: fail`; `design/specs/check.sh` runs them all.
- Measurements: at least 5 alternating runs per side; report median and range; a gap inside the run-to-run spread is "no measurable difference".
- A check you have never seen fail proves nothing: plant a bug and watch it fail. Every check has a plant in `tests/plants/` (run by `tools/planted.sh`); a new check adds one.

## Code

- C17, gcc and clang, `-Wall -Wextra -Werror`. ASan and UBSan on every test run.
- Kernels (UTF-8, graphemes, UTF-16, and what DESIGN.md lists) carry Frama-C contracts that state the result, not only bounds. No bit tricks in proved code: write comparisons, so the provers finish.
- No allocation in the redraw path after first use (buffers grow to the largest frame, then stay). No threads (one event loop); the only exception is the debugger's engine thread, under the six rules in DESIGN.md ("The one exception"). Draw only after the compositor's frame callback.
- Behaviour that copies Neovim is tested against `nvim --headless` (with `swapfile` off) on random input (`tools/conform.py`); undo against `design/ref/undo/undo_diff.py`.
- Comments say why, not what. Where a rule copies a Neovim function, the comment names it.

## Layout

| Path | What | Owner |
|---|---|---|
| `include/` | the headers every package codes against | thread 0 only |
| `ref/` | reference implementations: the thin editor (text in an array, undo as a list, code-point Unicode, terminal grid, a dozen Vim keys, safe save); once a package's `src/<module>/` is on main, the Makefile never builds `ref/<module>.c` again, so the package's PR deletes it (a dead copy only drifts) and moves its plants to `src/` | thread 0 |
| `src/main.c` | the editor's main loop (Render.tla "ok") | thread 0 |
| `src/<module>/` | a package's real implementation; once it has `.c` files the Makefile builds it instead of `ref/<module>.c`; a `build.mk` there adds its libraries (`CPPFLAGS`, `LIBS`, from pkg-config) and objects built outside our warnings (`OBJS`, into `build/<module>/`) | that package |
| `tests/` | unit tests, fuzzers, the Neovim conformance driver and its recorded traces | thread 0; a package adds its own `tests/test_<module>*.c` and `tests/fuzz_<module>*.c`, and replaces `tests/plants/<module>.txt` (the plants aimed at `ref/<module>.c`) with plants for its code; the Makefile and `planted.sh` find them by name |
| `tools/` | `conform.py` (differential test against nvim), `planted.sh` (every check fails on a planted bug), `dupes.py` (copied code) | thread 0 |
| `design/` | the design package, copied unchanged | changes go through DESIGN.md's rules |
| `Makefile`, `README.md`, `.github/` | the build and the check, the overview, CI | thread 0 |
| `assets/` | the app icon and desktop file (PR 55) | thread 0 |
| `reviews/` | one report per package review (`reviews/package-<n>.md`) and its repro files | that review's thread |

Modules and their packages: text 1, undo 2, uni 3, core 4, keys 5, grid and tty 6, font 7, wl 8, win 9, loop 10, file 11, syntax 12, lsp 13, tools 15, org 16, git 17, sqlfmt 18, snip 19, fold 20, sql 22. C libraries and the test fonts come from the distribution (CI's apt line), not nix, whose libraries need a newer glibc than the system compiler links. The headers say what each package adds.

## The check

`make check` runs everything a merge needs: `compilers` (gcc and clang, -Werror), `test` (unit tests under ASan+UBSan, recorded Neovim traces), `fuzz` (libFuzzer, `FUZZ_SECONDS` each), `conform` (random key sequences against live `nvim --headless`, `CONFORM_N`, seed `SEED`, 1 by default; CI runs fresh seeds nightly), `undo-diff`, `lean`, `frama`, `specs` (all TLA+ configs, about 7 min), `dupes`. `make planted` must report every plant caught. Tools: `nix build -f tools.nix tools -o tools-env` (design/env.nix's tools plus Wine), put `tools-env/bin` on PATH and run `why3 config detect` once (Neovim 0.12, TLC, Lean 4.35, Frama-C with Z3 and CVC5); clang needs its compiler-rt (libFuzzer, sanitizers).

## Working in parallel

- Each work package owns one directory (DESIGN.md, "Building it as a Claude project"). Never edit another package's directory.
- Headers in `include/` change only through thread 0. Ask with the reason.
- Every merge runs the whole check: build with both compilers, tests with sanitizers, fuzzers for their set time, `design/specs/check.sh`, `lean design/proofs/*.lean`, Frama-C.
- Every package gets a review by a separate Fable 5.2 agent (model `claude-fable-5-2`) in its own thread before it merges, briefed with the diff and the package's contract, not with the author's conclusions. The reviewer must reproduce each finding by running it, and writes the report under `reviews/`.
- Approved PRs merge as a train, so one run covers them all and a merge does not void the others' runs. Merge them in order onto current main in one branch. Push that branch to the train PR (a draft that is never merged), so CI runs `make check`, every plant shard and the Windows job on the train's tree. When all of them are green, merge the PRs on GitHub in that order (merge commits) and confirm `git rev-parse origin/main^{tree}` is the train's tree. If anything is red, drop the PR at fault and run again. A PR approved during a run joins the next train.
- A review runs `make check` and its package's plants locally, and plants its own bugs; every other plant runs once per train, on the train PR (CI runs the plant shards there only).
- For the rope, the Wayland backend and the org parser, two threads build the package independently; the one that passes the package's checks with less code and better measurements wins. Record the comparison.
- Never weaken a check, a spec or a test to get green.
