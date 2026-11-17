# Review of PR 7 (headers and build hooks), head 0c81a86

Independent review against `design/` (DESIGN.md, its specs, PROJECT-INSTRUCTIONS.md) and CLAUDE.md. Every finding was reproduced by running it; the files are in `reviews/pr-7-repro/`. Tools: `tools.nix` (Neovim 0.12.6, TLC, Lean, Frama-C), gcc 13 and clang 18 from apt.

**Verdict: changes requested, for finding 1 only.** The headers are coherent with the design, no check is weakened, and the reference implementations still pass everything. But `src/main.c` now loses pasted text, which `main` does not. Findings 2 to 5 do not block the merge.

## What I ran and what held

- **measured** (1 run): `make check` passed: both compilers, 5 unit tests under ASan and UBSan, 1,013 recorded traces (0 differ), both fuzzers 30 s, 2,000 live sequences against nvim (seed 1794924419, 0 differ), undo-diff 5,000 (0 differ), Lean, Frama-C 77 of 77, all 35 TLA+ configs as expected, `win` (no `src/win` yet).
- **measured** (1 run): `make planted` caught 19 of 19.
- **measured**: CI run 44098220157 on this head is green.
- No check weakened (read of the whole diff): the test and plant changes are the mechanical `e->x` to `e->win->x` / `e->buf->x` renames; `conform-recorded.json` is untouched; `test` links `TEST_SRCS`, which is `SRCS` without the window backend.
- **measured** (1 run, ASan and UBSan, `faces.c` with `fake_syntax.c`): the new face path, which nothing in the tree exercises because `ref/syntax.c` has no grammar, is correct. With a grammar that gives byte p face 1 + p % 10, every drawn cell of a 5,000-byte line carries its byte's face at 8 cursor positions around the 1,024-byte buffer boundaries; `ref/tty.c` sends the Catppuccin colour, sends nothing for an identical frame, and resends a cell whose face alone changed. The 10 colours match catppuccin/nvim's Mocha palette.
- Header contracts against DESIGN.md: `backend.h` (table, clipboard, mouse; Present's `ready`), `core.h` (Neovim's Buf and Win split, the row "Editor core"), `loop.h` (timer, Ctrl-C polling, non-blocking process pipes; rows "Main loop" and "Pipes"), `file.h` (watch on the directory, stat decides, own saves ignored; row "External change"), `undo.h` (time travel and undo file; row "Undo"), `syntax.h` (edit before the change, reparse after the frame; row "Syntax") all match, except the note in finding 4.

## Finding to fix before merge

### 1. A paste larger than the key queue silently loses text (regression from `main`)

`on_input` used to stop reading when the queue was nearly full, so input waited in the terminal (Render.tla models exactly this: `Arrive == inq < Cap`). It now always reads 256 keys and drops what does not fit. The loop runs 64 commands per iteration and reads up to 256, so the 4,096-key queue fills after about 5 KB and the rest of the paste is dropped.

**measured** (`paste.py`, 5 alternating runs per side, 40,000 bytes of numbered lines typed into insert mode through a pty, then `Esc :wq`):

| Build | File after save | Runs |
|---|---|---|
| `main` (613c284) | 40,001 bytes, all 5,000 lines | 5 of 5 |
| PR head (0c81a86), `-t` | 14,080 bytes, 1,760 lines; 26 KB gone, no message | 5 of 5 |
| PR head with `suggested-main.diff` | 40,001 bytes, all 5,000 lines | 5 of 5 |

DESIGN.md lists "a 10 MB paste as one command" as a scenario the editor must handle, and losing typed text silently is the worst outcome an editor has.

The goal of the change (Ctrl-C behind a long paste still interrupts a command) only needs reading past a full queue while a command is running, because between commands the loop itself drains the queue. `suggested-main.diff` (5 lines) does that: between commands a full queue leaves the input in the terminal, and never asks the backend for more keys than fit; only inside a command (`loop_breakcheck`) does it read on and drop. With it `ihello<C-c>` still discards the typeahead (**measured**, 1 run). This is a suggestion; any fix that passes `paste.py` is fine. The paste test belongs in `tests/` with a plant, since nothing in `make check` caught this.

The comment cites `Break.tla "ok"`, which is not in the tree (`grep -rn Break.tla`: only this comment). Cite it once package 10's spec is merged, and make that spec's model say what happens to keys when the queue is full.

## Findings that do not block

### 2. Twenty-five declared functions have no implementation in the tree

**measured** (`link.c`, 1 run): linking a file that names every function in `include/` against `ref/` fails with 25 undefined references: `loop_timer`, `loop_breakcheck`, `proc_*` (4); `undo_time`, `undo_step`, `undo_saved`, `undo_write`, `undo_read`; `file_watch`, `file_watch_read`, `journal_*` (7); `font_*` (5).

DESIGN.md: every package "is tested against the reference implementations of the others, so it can be accepted without waiting for them". For these functions that no longer holds: a package that calls one (the terminal's Esc timeout wants `loop_timer`; the core wants `journal_*` and `undo_time`) cannot link until the owning package merges. `core.h` and `syntax.h` did get refusing stubs in `ref/`; these did not. Either add stubs of the same kind, or state the merge order in README's "Next" (10, 2, 11 and 7 before their callers). The second is less code.

### 3. Ctrl-C keeps the keys that follow it in the same read; Neovim drops those too

**measured** (1 run each, `ctrlc.vim` with `nvim_input`, `keys.py`): `ihello<C-c>world` sent as one batch leaves Neovim 0.12.6's buffer empty; the PR leaves `"\nrld\n"` (the `w`, `o`, `rld` after the Ctrl-C ran). With `world` sent 300 ms later both give `"\nrld\n"`. Neovim flushes everything it has read when it notices `got_int`. An edge case; it belongs to package 5's or 10's conformance work, but the comment "(flush_buffers)" claims more than was tested.

### 4. The crash journal's place departs from DESIGN.md without a record

DESIGN.md, "Crash recovery": "append each command's edits to a journal next to the file". `file.h` puts it in `$XDG_STATE_HOME/homeros/journal/` ("Neovim's place for it"). Copying Neovim is the preferred kind of change and I think it is the better place (no litter in work trees, read-only directories work), but `design/` changes go through DESIGN.md's rules, so this needs a line there or in the PR for Davy to see. **hypothesis** on my side that it is better; nothing measured.

### 5. Small things

- README "Next" now says the headers hold `font.h` and `syntax.h`, and two sentences later still says "Packages whose header is not in `include/` yet (7 `font.h`, 12 `syntax.h`, ...)". The list of packages there omits 4 and 9, which the PR text names.
- `ref/core.c` `core_draw` asks `syntax_faces` for up to 1,024 bytes per line and for every byte left of the view (**measured**, `faces.c`: 5,005 bytes asked for 81 visible cells at byte 4,999), while `syntax.h` says "only the visible bytes are queried". Package 4's core replaces this; worth a word in `core.h` or the ref comment so it is not copied.
- `ref/core.c` `edit()` tells the syntax tree about a change before making it, and on an allocation failure rolls the text back without telling the tree (**hypothesis**, read only; `ref/syntax.c` ignores edits, so nothing observable today). Package 4 should not copy that order of failure handling.
- `keys.h` says KeyState is what "the core draws or keeps per window", but it lives in `Editor`, once. One of the two is meant.
- No new plant came with the PR. The PR text says no new check was added, which is true, but the face drawing and the `-t` flag are new behaviour with no test. `faces.c` could be the test.

## Not checked

The window backend path (`HOMEROS_WL`), `src/<module>/build.mk` with a real library, `make win` with a real `src/win/check.sh`, and the `undo-diff` lines for `tools/undo_conform.py` are hooks with nothing behind them in this tree; they were read, not run. The conformance-harness fix for empty text had not been pushed when this was written.
