# Editor repo: rules for every agent

This repo builds a native text editor for one person, Davy. The design, its evidence and its plan are in `design/` (copy this package there): start with `design/README.md`, then `design/DESIGN.md` and `design/FEATURES.md`.

## The top rule (Davy's words)

"The goal is absolute perfection and achieving feature completeness with maximum simplicity. In the ideal world no line of code is written that isn't absolutely necessary and no redundant machine instruction is ever made/no clock cycle is ever wasted."

So: the smallest change that does the job; delete before you add; no speculative generality, no plugin system, no option nobody asked for. Personal fit beats generality: if Davy does not use it (FEATURES.md), it is not built.

## New approaches

"You're allowed to try never seen before approaches to problems, but you're only allowed to settle on them if they're better than any combination of existing approaches in all scenarios. Remember that we might not be able to think of all possible scenarios."

"Something is only better after you have accumulated a lot of definite proof, this may be a formal proof of its properties or the results of a large body of measurements and tests."

Prefer copying the behaviour of a reference editor (Neovim 0.12 first; Helix, Emacs, VS Code for Lean, SSMS for SQL, as DESIGN.md names per behaviour). A new mechanism needs: the alternatives compared, the scenarios listed, the evidence, the residual risk, in DESIGN.md's "New approaches" table.

## Evidence

- Tag every claim you write down: **proved** (formal proof or exhaustive model check), **measured** (runs recorded, with the count), or **hypothesis**.
- Run things; do not conclude from reading code. Record what you ran and how many times.
- Timing questions (event loop, frame callbacks, LSP, saving, processes) get a TLA+ model checked by TLC, with the property stated before the mechanism and at least one broken variant that must fail. Each `.cfg` starts with `\* expect: pass` or `\* expect: fail`; `specs/check.sh` runs them all.
- Measurements: at least 5 alternating runs per side; report median and range; a gap inside the run-to-run spread is "no measurable difference".
- A check you have never seen fail proves nothing: plant a bug and watch it fail.

## Code

- C17, gcc and clang, `-Wall -Wextra -Werror`. ASan and UBSan on every test run.
- Kernels (UTF-8, graphemes, UTF-16, and what DESIGN.md lists) carry Frama-C contracts that state the result, not only bounds. No bit tricks in proved code: write comparisons, so the provers finish.
- No allocation in the redraw path after first use (buffers grow to the largest frame, then stay). No threads (one event loop). Draw only after the compositor's frame callback.
- Behaviour that copies Neovim is tested against `nvim --headless` (with `swapfile` off) on random input; undo against `ref/undo/undo_diff.py`.
- Comments say why, not what.

## Working in parallel

- Each work package owns one directory (DESIGN.md, "Building it as a Claude project"). Never edit another package's directory.
- Headers in `include/` change only through thread 0.
- Every merge runs the whole check: build with both compilers, tests with sanitizers, fuzzers for their set time, `specs/check.sh`, `lean proofs/*.lean`, Frama-C.
- Every package gets a review by a separate Fable 5.2 agent (model `claude-fable-5-2`) in its own thread before it merges, briefed with the diff and the package's contract, not with the author's conclusions. The reviewer must reproduce each finding by running it.
