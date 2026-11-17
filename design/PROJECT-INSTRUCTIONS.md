# Project instructions (seed for the new Claude project)

Paste this into the project's instructions. The repo starts empty; the design package goes in at `design/`.

## Goal

Build the editor described in `design/DESIGN.md`, with the features in `design/FEATURES.md`, following the repo's `CLAUDE.md`. Davy is the only user. Davy's words on quality are in CLAUDE.md and win over speed.

## How the work is split

1. **Thread 0 first, alone.** It copies the design package to `design/`, writes `CLAUDE.md` from the seed, sets up the build and the check (`make check` or equivalent: both compilers, sanitizers, tests, `design/specs/check.sh`, Lean, Frama-C), then builds a thin editor end to end: text in an array, undo as a list, a terminal grid, a dozen Vim keys, open and save. From what that editor needed it cuts the headers in `include/` and keeps the simple versions in `ref/` as reference implementations. It is done when the thin editor opens, edits and saves a file and every check fails on a planted bug.
2. **Then one thread per package**, in parallel, as the table in DESIGN.md ("Building it as a Claude project") lists: its directory, contract, acceptance check, and when it can be accepted against the real parts. Each thread tests against the reference implementations of the others until those are accepted.
3. **For the rope, the Wayland backend and the org parser**, two threads build the package independently; the one that passes the package's checks with less code and better measurements wins. Record the comparison.
4. **Each package's review** runs in its own thread on Fable 5.2 (`claude-fable-5-2`), reproduces every finding, and writes its report into the repo under `reviews/`. Valid findings are applied before merge.

## Rules for threads

- Only thread 0 changes `include/`. A thread that needs a header change asks thread 0 with the reason.
- Never edit another package's directory. Never weaken a check, a spec or a test to get green.
- Every claim in a commit message, PR or report is tagged proved, measured (with run count) or hypothesis.
- Phase gates are in DESIGN.md's Plan. A phase is done only when every gate item has been run and recorded.
- Ask Davy before anything irreversible or outside the repo.

## Order of phases

0 Core on Linux, 1 Windows (Davy's work laptop, same fonts), 2 LSP and Lean, 3 Lean widgets, 4 daily tools (SQL, terminal, search, integrated magit, debugger, Claude Code integration, file manager), 5 org. Linux desktop: Hyprland only.
