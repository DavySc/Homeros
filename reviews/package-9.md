# Review of package 9 (PR 3, Win32 backend), head 1a741f7

Independent review against `design/` (DESIGN.md "Building it as a Claude project", PROJECT-INSTRUCTIONS.md), CLAUDE.md and `include/backend.h`, and for correctness. Every finding was reproduced by running it; the sources are in `reviews/r9/` (`reviews/r9/run.sh <name>` builds one against `src/win/backend.c` and runs it under Wine). Tags as CLAUDE.md asks. Environment: `design/env.nix`'s tools, Wine 11.0 (nixpkgs `wineWow64Packages.stable`) on Xvfb, gcc 13.3, clang 18.1, 4 cores.

**Verdict: changes requested. Not mergeable as it stands.** The code is small, honest about what is pending, and its pacing claim holds. But CI cannot go green on `main` (finding 1), one drawing bug is asserted as correct by the test (finding 2), and the test never draws a character (finding 3). Nothing here needs Davy.

## What I ran and what held

- **measured** (1 run, 7 min 1 s): `make check` on this head passed: both compilers, 6 unit tests (`test_win` only prints that it is Windows only), 1,013 recorded traces with 0 different, fuzzers 31 s each, 2,000 live sequences with 0 different (seed 1794924017), undo oracle 0 of 5,000, Lean, Frama-C 77 of 77, TLC 42 of 42 as expected.
- **measured** (1 run, Wine on PATH): `make planted` caught 34 of 34 in 3 min 35 s, each of the 15 new plants by its own `CHECK` line or TLC's `WRONG`.
- **measured** (6 runs, 5 of them while TLC used all 4 cores): `src/win/check.sh` passed 6 of 6; the next frame was allowed after 18.80 to 19.13 ms at a 16.67 ms period (P + P/8 is 18.75).
- **proved within its bounds, rerun** (`reviews/r9/pace-sweep.sh`, 39 bounds, 78 TLC runs, 59 s): `NoWaste` passes exactly when Gap > P + D, for P 2 to 4, D 0 to 1 and every Gap from 0 to P + D + 2. `Pace_ok` has 525 states, as DESIGN.md says. The 7 committed configs came out as expected in `make check`.
- **Scope.** The PR edits no header, no Makefile, no CI file and no other package's directory. `tests/test_win.c` and `tests/plants/win.txt` are the package's own by CLAUDE.md's layout table. The edits to `design/DESIGN.md` and the new `design/specs/Pace*` are what CLAUDE.md requires of a new mechanism (a TLA+ model, and a row in "New approaches"), and `design/` is not thread 0's alone, so they are in order. PR 7 (thread 0) does not touch `design/`, so they do not collide. The changes the package needed in shared files (the `win` target, Wine, `LOOP_MSGQ`) were asked of thread 0, not made here: that is the rule followed, not broken.
- **Pending items are recorded, not faked.** The performance gates, DWM's slip, the monitor move, IME and the conformance traces on Windows are listed as pending and tagged hypothesis; nothing in the PR or DESIGN.md claims them.

## Findings to fix before merge

### 1. The PR cannot pass CI on `main`, and does not build on thread 0's branch

Three things, each run:

- **measured** (2 of 2 plants, 1 run): where Wine is not on PATH, which is CI on `main` (`design/env.nix` has no Wine), the `win:` plants end as `FAILED OTHERWISE ... make: *** [<builtin>: w] Error 2` and `make planted` exits 1. Since commit 1a741f7 that is deliberate and right (a missing tool must not pass for a caught plant), but it means the PR is red on `main` whatever else happens.
- **measured** (1 run): on `main`, `make check` does not run the backend at all: `test_win` prints "Windows only" and passes. A green check on this PR would say nothing about `src/win/`.
- **measured** (1 run): merged onto PR 7's branch (e6d9998), `make win` fails to compile: `tests/test_win.c:25: call to undeclared function 'backend_input'` and the same for `backend_fds`, `backend_ready`, `backend_open`. PR 7 turns `backend.h` into a table of functions (`const Backend backend_tty, backend_wl`) with `clip_set`, `clip_get` and `mouse`; it declares no `backend_win`, and its `main.c` picks only the terminal or Wayland.

So the order has to be: PR 7 merges first; this PR is then rebased on it, `backend.c` becomes `const Backend backend_win` (with the clipboard and mouse entries, even if they answer "none" for now), the plants use the `win` target, and `backend_fds` uses `LOOP_MSGQ`. `backend_win` in `backend.h` and its pick in `main.c` have to be asked of thread 0.

The CI run on this PR (44096643846) is red for a fourth, unrelated reason: `make conform`, seed 1794923547, 1 of 2,000 sequences differs from Neovim (`3 x j o <Tab> <Tab> c ა ␣␣ <Esc> u dd 1 gg l <C-r> i , <Esc>`: Neovim has one line, ours two). That is `ref/` code on `main`, thread 0's to fix, and it will turn other PRs red at random too.

### 2. After a whole redraw the cursor is not drawn at row 0, column 0

**measured** (`reviews/r9/cursor00.c`, 3 of 3 runs):

```
first frame, cursor (0,0) on a space:        background (no cursor)
control: moved away and back to (0,0):       reversed (cursor)
after Ctrl-Z's whole redraw, cursor (0,0):   background (no cursor)
control: whole redraw with cursor at (1,1):  reversed (cursor) at (1,1)
```

`backend_present` reads `pr = shown->crow, pc = shown->ccol` after `grid_resize(shown, ...)` has set them to 0, 0, so a cursor at (0, 0) counts as already shown there (`cur == was`), and an empty cell equals the cleared one, so the cell is skipped. But the DIB was just cleared, so nothing is shown there. It happens on the first frame, after a new size or DPI, and after Ctrl-Z (`main.c` frees `shown`). Today's core always has line numbers in column 0, so it needs line numbers off (FEATURES.md lists that toggle) or any future layout that puts the cursor in the top left corner.

`tests/test_win.c:153` asserts the bug: after the resize the grid's cursor is at (0, 0) and the test demands `at(0, 0) == BG`. **measured** (1 run): with `int pr = all ? -1 : shown->crow` the repro shows the cursor in all four lines and `test_win` then fails. Fix the rule, correct that check, and add a plant for it.

### 3. The test never draws a character: 13 of 15 further plants pass

**measured** (`reviews/r9/extra-plants.sh`, 1 run, 15 plants in `tools/planted.sh`'s format, each compiling):

| Plant | Result |
| --- | --- |
| no glyph is drawn, only cell backgrounds | not caught |
| astral character split into the wrong surrogates | not caught |
| a wide character drawn one cell wide | not caught |
| the right half of a wide character is not sent to the window | not caught |
| dim text in the normal colour | not caught |
| an uncovered window is not repainted (`WM_PAINT`) | not caught |
| Ctrl-H sent as Backspace | not caught |
| a lone low surrogate makes a key | not caught |
| the new DPI is ignored | not caught |
| the refresh period is not read again on `WM_DISPLAYCHANGE` | not caught |
| Right arrow sent as Left | not caught |
| Down arrow sent as Up | not caught |
| the close button destroys the window | not caught |
| one column too many reported | caught |
| the cursor is not drawn reversed | caught |

Every cell the test presents is a space, so `draw_cell`'s text, its UTF-16 path and its wide-character path are untested: a backend that draws no text at all passes. CLAUDE.md: "a check you have never seen fail proves nothing".

These are testable under Wine. **measured** (`reviews/r9/glyph.c`, 1 run, on the PR's code): an `M` leaves foreground pixels in its cell and a dim `M` leaves dim ones and no normal ones; a reversed wide character fills its right cell (210 of 210 pixels) and leaves the cell after it alone; after painting the window white, `InvalidateRect` and `UpdateWindow` restore a cell to 210 of 210 background pixels; with Control down (`SetKeyboardState`), `WM_CHAR` 8 arrives as 8; `VK_RIGHT` and `VK_DOWN` arrive as themselves; a lone low surrogate gives no key. Put those in `test_win.c` with their plants. `WM_DPICHANGED` and `WM_DISPLAYCHANGE` can be sent to the window the same way; if one of them cannot be tested under Wine, say so next to the code and list it with the pending items.

### 4. Keys can sit undelivered while the loop sleeps (contract between this package, `loop.h` and `main.c`)

**measured** (`reviews/r9/stuck.c`, 3 of 3 runs):

```
300 keys posted, one backend_input(256) gives 256; the next wait times out (the loop would sleep)
with MWMO_INPUTAVAILABLE the wait returns (the loop would read)
```

`backend_input` stops pumping when its ring is full (rightly, so nothing is dropped). `main.c` calls it once per wake. The 44 messages left in the queue are no longer new input, so `MsgWaitForMultipleObjectsEx(..., QS_ALLINPUT, 0)`, which is how PR 7's `loop.h` describes `LOOP_MSGQ`, does not return for them: the keys wait for the next unrelated message. It takes more than 256 key messages between two wakes (a tool pasting by `WM_CHAR`, or key repeat while a long command runs).

The Windows loop is package 10's and not built, so nothing fails today. Record it where it cannot be missed: either ask thread 0 to have `loop.h` say `MWMO_INPUTAVAILABLE` for `LOOP_MSGQ`, or have `backend_input` leave the queue signalled when it stops early. The terminal backend does not have this problem because a readable fd stays readable.

### 5. Pace.tla: the claim holds; three things around it do not

- **The sweep is not in the repo.** The PR, DESIGN.md and `backend.c`'s header comment all say "proved for P 2 to 4, D 0 to 1, every gap", but only 7 configs at P = 3 are committed. I rebuilt the sweep (`reviews/r9/pace-sweep.sh`) and it agrees; commit yours or take this one, so the claim can be rerun.
- **`Fresh` has never been seen to fail.** **measured** (39 of 39 bounds): `Fresh` passes at every Gap, including Gap 0. The model has no timer: `WF_vars(Draw)` assumes the loop wakes when the gap has passed, which is exactly what the waitable timer in `backend_ready` is there to do. So "once typing stops, the screen shows the last key" is proved for the rule, not for the mechanism that could break it. The timer is covered by `test_win`'s `wait_ready` and the "far too late" plant (measured), which is fine, but then the DESIGN.md row and the PR should say that, or the model should gain the wake-up and a variant without it that fails. The PR body's "NoWaste and Fresh hold exactly when Gap > P + D" is true only of `NoWaste`.
- **The "New approaches" row is short of what the rule asks.** It is honest that the choice is not settled. Two gaps: (a) the cost under steady input is not stated. Arithmetic on the model's own picture (`reviews/r9/pace-latency.py`, computed, not a measurement of Windows): 2,000 keys 1 ms apart at 60 Hz give 108 frames, 12 of 120 compositions with nothing new, and key to screen at median 18.3 ms, p99 33.3 ms, against 8.5 and 16.6 ms when drawing on every key (which wastes 1,880 frames). The gates "key to screen within one refresh" and "0 frames unseen" pull against each other here, and phase 1 should measure both. (b) **hypothesis** (from memory, not checked): an alternative that keeps one loop and no D3D device is missing from the comparison, a timer placed from DWM's own vblank time (`qpcVBlank` in the same `DwmGetCompositionTimingInfo` call), which Chromium's Windows vsync provider used. It may allow one frame per refresh instead of one per 1.125.

### 6. Numbers and wording in DESIGN.md

- **measured** (`ls design/specs`): the tree has 9 models and 42 configs, 25 of them `expect: fail`. The edited sentence says "Eight TLA+ models", "all 38" and "22 must fail"; line 16 still says "Seven" and "31". The sentence was already behind before this PR (Git.tla), but the PR rewrote it to numbers that `check.sh` does not produce. Either count what is there or change only Pace's part.
- The "Window, Windows" row now reads "built (package 9)" after a description that says FreeType and HarfBuzz, a fallback font list, IMM32 and one message loop. What is built is the window, keys, the grid from a DIB with GDI glyphs, and pacing. Say that, so the row cannot be read as the design being done.

## Notes, no change needed for merge

- **measured** (`reviews/r9/ringloss.c`, 1 run): `put` drops a key silently when the ring is full and the window procedure is entered from outside `backend_input` (300 `WM_CHAR` sent with `SendMessage`: 256 arrive). An IME commit is not such a path under Wine (270 of 270 arrive); on Windows that is untested and belongs with the pending IME item.
- The close button and Alt-F4 do nothing (listed as a known gap). PR 7's `keys.h` has no key for it either; ask thread 0, since a window that cannot be closed will be the first thing Davy meets on the laptop.
- For thread 0: `tools/planted.sh` counts a plant that does not compile as caught (4 of my first 15 plants were "caught" by `-Werror=unused`), because `error` is among the words it accepts. None of this PR's 15 plants is caught that way.

## Re-review

Fixed in the next push, I will rerun: `make check`, `make planted`, `reviews/r9/extra-plants.sh` (expect 15 caught, or a stated reason), the three repros, and the sweep.
