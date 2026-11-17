# Review of package 6 (PR 2), head c639b46

Independent review of the grid and the terminal backend (`src/grid/`, `src/tty/`) against its contract (`include/grid.h`, `include/backend.h`, `design/specs/Render.tla`; accepted when frame diff tests pass and Render's rules exist as tests), `design/PROJECT-INSTRUCTIONS.md` and CLAUDE.md. I was given the diff and the contract, not the author's reasoning. Every finding below was reproduced by running it; run counts are given and the sources are in `reviews/r6/` (run from the repo root, after `make homeros` for the pty ones). Tools: `design/env.nix` (Neovim 0.12.6), gcc 13.3, clang 18.1, 4 cores.

**Verdict: changes requested, all small.** The package meets its acceptance check and I found no fault in the frame diff. Findings 1 and 2 are behaviour the user can hit and are one or two lines each in `src/tty/tty.c`; 3 and 4 are test gaps and three redundant expressions. Nothing here needs Davy.

## What I ran and what held

- **measured** (1 run): `make check` passed in 8 min 5 s: both compilers, 8 unit tests, 1,013 recorded traces, fuzzers 30 s each, 2,000 live sequences (seed 1794923594, 0 differ), undo oracle 0 of 5,000, Lean, Frama-C 77 of 77, all TLA+ configs as expected.
- **measured** (1 run): `make planted` caught 31 of 31, each with its own failure line, in 2 min 14 s. The 13 plants of this package are among them.
- **measured** (1 run): the diff touches `src/grid/`, `src/tty/`, three new `tests/test_*.c`, `tests/test_tty_vt.h`, `tests/plants/grid.txt` and `tests/plants/tty.txt` only. No header, no other package's directory, no Makefile. Thread 0's one grid plant was moved to `tty.txt` and retargeted at `src/tty/tty.c`, not dropped. No check, spec or test is weakened. Line counts are as the PR says (54 + 210 against 42 + 177).
- **measured** (20 runs each, with 6 busy loops on the 4 cores): `test_tty_input` and `test_tty_render` passed 20 of 20. The PR's flakiness claim holds here.
- **measured** (5 alternating runs, gcc -O2, 60x200 grid to /dev/null, 2,000 frames per run, time includes filling the grid; `reviews/r6/bench.c`): one line changed, ref median 29.6 µs (28.7 to 50.2), src 28.5 µs (28.0 to 37.0); every cell changed, ref 117.4 µs (114.6 to 135.0), src 121.6 µs (118.9 to 123.2). Ranges overlap: no measurable difference, as the PR says, and far under the frame budget.
- **measured** (1 run, `reviews/r6/big.c`): 200 frames of a 64x256 grid full of non-ASCII and wide characters (largest frame 226,906 bytes, so the 64 KiB output buffer is flushed mid-frame several times) are shown exactly by the PR's model terminal, and `malloc`, `calloc` and `realloc` are called 0 times in frames 2 to 200. "No allocation in the redraw path" holds for the backend.
- **measured** (4,000 random byte streams, `reviews/r6/inv.c`): `backend_input` on arbitrary bytes (Esc, `[`, digits, lead and continuation bytes, CSI runs longer than the 256-byte buffer) gives the same keys whether each call offers 256 slots, 1 to 8, or 1 to 300. The PR's own input test only feeds well-formed keys; this covers the rest. No stall, no sanitizer report.
- Read against Neovim v0.12.6's source (`grid.c`, `tui/tui.c`, `tui/input.c`): `grid_put_linebuf()` does truncate a left half to `>` keeping its attribute and blanks an uncovered right half keeping its attribute; `print_cell_at_pos()` does clear two cells and reposition; `tinput_timer_cb()` does force the pending keys after `'ttimeoutlen'`. The PR's descriptions of what it copies are accurate, and its one deviation (every non-ASCII character treated as ambiguous) is stated. **hypothesis** for exact equivalence: this is a reading, not a differential test.
- Render's rules exist as tests on the real `src/main.c` (NoTornFrame, Responsive with Batch 64, resize, Ctrl-Z), and both of Render's broken variants are planted and caught. The acceptance check is met.

## Findings to fix before merge

### 1. A resize while the editor is suspended is never seen

After Ctrl-Z the shell is the terminal's foreground job, so a window resize sends SIGWINCH to the shell, not to the stopped editor. `backend_suspend` returns after `raw()` and nothing asks for the size again; `src/main.c` frees `shown` but keeps the old grid.

- **measured** (3 of 3 runs, `python3 reviews/r6/susp.py ./homeros`, a small job-control shell on a pty that takes the terminal while the editor is stopped): 24x80 terminal, Ctrl-Z, resize to 40x100, fg. The editor redraws 24 rows; the lowest row it addresses is 24. The control (the same resize while running) reaches row 40 in 3 of 3.

The screen stays at the old size until the next resize. `test_tty_render`'s shell leaves the editor in the foreground, so it cannot see this. Neovim takes the size again when its TUI restarts after a suspend (`tui_guess_size` on start; **hypothesis**, from reading, my nvim-in-pty run of the same script did not suspend). Fix inside the package: after `raw()` in `backend_suspend`, write a byte to `sigpipe[1]`, so the loop gets a `KEY_RESIZE` and main asks `backend_size` again. Add the case to `test_tty_render` (its shell needs `tcsetpgrp` to itself while the editor is stopped) with a plant.

### 2. Bytes that are not UTF-8 become arrow keys, or go into the file as invalid UTF-8

`key()` decodes any lead byte plus continuation bytes without a range check, and `keys.h` puts the special keys at 0x110000 and up.

- **measured** (1 run per case, `python3 reviews/r6/badkeys.py`: file `one\ntwo\n`, keys `j i <bytes> Z Esc :wq`), with Neovim 0.12.6 on the same input (`reviews/r6/badnvim.py`, 1 run per case):

| Bytes typed | Homeros | Neovim |
| --- | --- | --- |
| `F4 90 80 80` (decodes to 0x110000 = KEY_UP) | cursor moves up: `Zone\ntwo\n` | the four bytes inserted on line 2 |
| `F4 90 80 84` (= KEY_RESIZE) | swallowed (the value is KEY_RESIZE) | (not run) |
| `F7 BF BF BF` (0x1FFFFF) | swallowed | the four bytes inserted |
| `ED A0 80` (surrogate D800) | `ED A0 80` written to the file | U+FFFD |
| `C1 A1`, `E0 80 AF` (overlong `a`, `/`) | `a`, `/` | U+FFFD each |
| `C0 80` (overlong NUL) | swallowed | U+FFFD |
| `C3` then `Z` | `Ã` (U+00C3) | U+FFFD |

A terminal does not send these for typed keys, but a paste of binary or Latin-1 text does. The first two rows are the ones that matter: text bytes must never become `KEY_*`. The smallest fix is to treat a decoded value above 0x10FFFF as a broken sequence (the byte alone); better, also refuse surrogates and overlong forms and give U+FFFD as Neovim does. Extend `test_tty_input`'s tokens with these and plant the range check.

### 3. Checks that were never seen to fail

I ran 28 hand-made mutations of `src/grid/grid.c` and `src/tty/tty.c` against the package's four tests (`reviews/r6/mut.sh`, `reviews/r6/muts.txt`; **measured**, 1 run each): 15 caught, 13 survived. Two of the survivors are equivalent code (finding 4) and one is harmless (several `KEY_RESIZE` per call). The rest are behaviour the PR claims or relies on, with no test that can fail:

1. **`grid_put` accepts width 0 or 3** survives. The PR's item 4 ("refuses widths other than 1 and 2") is tested by `!grid_put(..., 0, ...)`, but with the check removed a width of 0 still returns 0, after overwriting the cell. Check that the cell is unchanged.
2. **A failed resize in `backend_present` leaves the synchronized update open** survives. Item 4's other claim has no test; tag it hypothesis or test it (`--wrap=malloc` in a test of its own).
3. **`backend_close` leaves the terminal raw** survives. Nothing checks the terminal's modes after the editor exits. `tcgetattr` on the pty master in `test_tty_render` after `:q!` shows it (**measured**, 3 of 3 runs, `reviews/r6/term.py`: raw while running, cooked and off the alternate screen after `:q!`).
4. **The read ignores the slot count** survives. The comment says unread bytes stay in the terminal "and keeps poll waking"; with the limit removed, whole keys sit in the backend's buffer and nothing wakes the loop. The tests call `backend_input` until it returns 0, so they cannot see a key that waits for the next key. A test needs to stop calling when `poll` says nothing is readable and then count the keys.
5. **A tail as long as the buffer waits forever** (the `r - i < sizeof b` guard removed) survives: no test sends a 256-byte unfinished CSI. With the guard gone the editor spins and reads nothing more. `reviews/r6/inv.c` covers it and could be added as `tests/fuzz_tty.c` or to `test_tty_input`.
6. **The output buffer is never flushed when full** survives: no test frame exceeds 64 KiB, and `test_grid_diff`'s `present()` would deadlock on its pipe if one did. `reviews/r6/big.c` reads from a file instead.
7. **Erase under reverse video**: with both `\x1b[0m` before the resize clear and `\x1b[0m` at the end of each frame removed, all four tests still pass (**measured**, 1 run). The model's `VT_BADERASE` rule cannot fire, because both tests call `vt_init` at a resize, which resets the model's SGR state. Keep the model's attribute across a resize.
8. Not pinned, smaller: the blanked right half keeps its old attribute (Neovim's rule, and the code is right), `grid_resize` puts the cursor at 0,0, the cursor is hidden during a frame.

Each new check needs its plant in `tests/plants/`.

### 4. Code that is not necessary (the top rule)

- `key()`: `*k = j > 2 ? -1 : s[2] == 'A' ? ...`. If `j > 2` then `s[2]` is a parameter byte, so it is none of `A` to `D` and the chain already gives -1. Removing `j > 2 ? -1 :` changes nothing (**measured**: that mutation survives the tests, and it is equivalent by the loop's exit condition).
- `key()`: the mask `len == 3 ? 0x0Fu : 0x07u`. A four-byte lead is `F0` to `F7`, whose bit 3 is 0, so `0x0F` serves both lengths (**measured**: survives; equivalent for every lead byte the branch can see).
- `backend_present`: one of the two `\x1b[0m` (before the resize clear, at the end of every frame) is enough; each alone passes every test (**measured**, 1 run each). The one at the frame end costs 4 bytes on every frame; keep the one before the clear, once item 3.7 can tell.

## Not blocking

- **measured** (3 of 3 runs each, `reviews/r6/term.py`): SIGTERM or SIGHUP kills the editor with the terminal left raw and on the alternate screen. The reference did the same and no contract asks for more; Neovim restores the terminal. Worth a line in the PR's follow-ups, with the unsaved-text side belonging to package 11's journal.
- The 50 ms wait runs inside `backend_input`, so keys read together with a trailing Esc are also held 50 ms and the loop serves no other fd meanwhile. The PR says so and names the fix (package 10's timer). **hypothesis**: not noticeable when typing; it will matter once LSP output shares the loop.
- With fewer slots than a held tail has bytes, `backend_input` returns 0 until a call offers more. `src/main.c` always offers 256 and the buffer holds 256, so the editor cannot reach it (**measured**: the 4,000-stream run above includes 1-slot calls and ends with a 300-slot drain, as the PR's own test does).
- `uni_cp` in `uni.h` has the same loose notion of well-formed (no overlong, surrogate or range check). That is package 3's contract, not this one.

## Re-review

I will re-run `make check`, `make planted`, the scripts in `reviews/r6/` and the mutations on the new head, and merge when CI is green.
