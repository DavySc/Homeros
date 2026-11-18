# Package 8, the Wayland backend: build A (PR 33) against build B (PR 14)

Reviewed 2026-11-17 against main at ab618e3. A is PR 33 at 86a083b, B is PR 14 at 8e9a17b.
Every claim is tagged **measured** (with its run count), **proved** or **hypothesis**.
Probes, scripts and raw logs: `reviews/package-8-repro/` (its README says how to run them).

## Verdict

**Build A wins.** It is the one that fits the editor as it is now and it meets the scroll gate
here; B is five hours behind main and misses that gate. A is **not ready to merge**: the review
found four defects, one of them wrong pixels on screen. B gets three of those four right, so
A's author should port B's handling of them (list at the end). PR 14 is closed, its branch kept.

| | A (PR 33) | B (PR 14) |
|---|---|---|
| Builds against main | yes; `make test TESTS=wl` passes (measured, 1 run; CI `check` green on the PR head) | **no**: `uni_width` changed and cells now hold whole clusters; 6 lines made it compile (`b-port.diff`), after which its own `make wl-test` passes on both renderers (measured, 1 run) |
| Scroll, frame ready p99 (gate 2 ms) | **1.27 ms** (0.82 to 1.82), under the gate in 5 of 5 | 2.52 ms (2.25 to 3.29), over in 5 of 5 |
| Typing, frame ready p99 | 0.25 ms (0.23 to 0.27) | 0.27 ms (0.23 to 0.31): no measurable difference |
| Whole-window frame | 4.0 ms (3.5 to 5.7) | **3.2 ms** (3.0 to 3.3) |
| Idle wake-ups in 20 s, threads | 0, 1 | 0, 1 |
| RSS idle, empty file | **12.4 MB** | 14.1 MB |
| Screen equals a whole redraw | **fails 7 of 12 seeds** (finding 1) | passes 3 of 3 seeds |
| Keys left in its queue | **stranded** (finding 2) | delivered |
| Slow reader of our clipboard | **blocks the editor** (finding 3) | 3 ms |
| Compositor dies | spins at 100% CPU (finding 4) | spins too: its `:qa!` is not a command of the current core |
| Grapheme-cluster cells (PR 24) | yes, with a test and a plant | no (the 6-line port adds it; no test) |
| Theme | grid.h's `faces`, underline, strikethrough | its own 95-line copy of the colour table, written before `faces` existed |
| Input method | text-input-v3, as DESIGN.md names | text-input-v1 (chosen because weston has only v1) |
| Scale | fractional-scale-v1 with a viewport | integer output scale |
| Wheel, touchpad | value120 notches, touchpad by rows | 15 axis units a key |
| Wide Lean symbols | spill into following blank cells (foot) | kept inside the row; cut at the last cell |
| `src/wl/wl.c` | 928 lines | 930 lines (about 835 once it uses `faces`) |
| Test | 487 lines, CI's apt sway and weston | 813 lines, needs weston 16 built by nix with a patched test plugin (37 s here, 1 run) |
| Plants | 24 (CI's planted job was still running when this was written; the author reports all caught) | 20, not rerun |

## How it was measured

Both trees merged with main at ab618e3 (clean merges). B's stand-in port is `b-port.diff`:
`uni_width`'s new signature, `grid_text` for the cell's bytes, scratch arrays sized for
31-byte clusters. Nothing else in B was touched.

One harness for both: A's `src/wl/test/bench.c` (main.c's loop with clocks, only backend.h)
linked against each backend, under one headless sway 1.9 (pixman, 1280x800, 16 px font), keys
from wtype, runs alternating A, B, 5 runs each side (`run.sh`, `bench2.log`). The scroll file is
4,200 lines of this repo's Lean proofs and C. All numbers below are **measured**, median (range).

| Scenario | | A | B |
|---|---|---|---|
| 2,000 keys 1 ms apart | p50 | 0.12 ms (0.10 to 0.13) | 0.12 ms (0.10 to 0.17) |
| | p99 | 0.25 ms (0.23 to 0.27) | 0.27 ms (0.23 to 0.31) |
| | frames | 683 | 685 |
| 200 keys 10 ms apart | p99 | 0.39 ms (0.25 to 3.23) | 0.28 ms (0.25 to 0.31) |
| `j` 400 times, 10 ms apart | p50 | 0.69 ms (0.53 to 0.76) | 1.40 ms (1.33 to 1.69) |
| | p99 | 1.27 ms (0.82 to 1.82) | 2.52 ms (2.25 to 3.29) |
| first whole-window frame | | 4.0 ms (3.5 to 5.7) | 3.2 ms (3.0 to 3.3) |
| first frame committed (20 runs) | | 7.5 ms (6.2 to 17.7), under 10 ms in 16 | 7.3 ms (6.0 to 13.4), under 10 ms in 19 |
| RSS | | 12.4 MB (12.3 to 12.6) | 14.1 MB (14.0 to 14.2) |
| wake-ups in 20 s idle, quiet machine | | 0 in 5 of 5 | 0 in 5 of 5 |

- Scrolling is where they differ. With relative numbers every row changes on a `j`; B shapes
  every row again, A copies the rows and draws the number. A's author measured 2.06 ms on
  Mathlib files; on this file A is under the gate in every run. On Hyprland: **hypothesis**.
- A's 200-key p99 had one run at 3.23 ms. The author traces this to the thin core's sideways
  scroll; here it showed in 1 of 5 runs.
- A whole window costs both builds more than 2 ms; most of it is shaping (package 7). B's is
  about a fifth cheaper: it fills a row with memcpy and blends red and blue in one multiply.
- A first idle set, taken while other work ran on the machine, showed 4 and 15 wake-ups for A
  in 2 of 5 runs and 0 for B (`idle1.log`; that count also included involuntary switches). It
  did not repeat on a quiet machine (`idle2.log`). Unexplained; worth one look by A's author.

## Findings in build A (the winner), each reproduced by running it

**1. The screen can keep rows that are no longer true (blocking).** `probe-pix` draws random
frames (edits, cursor moves, scrolls both ways with a relative number column, wide symbols,
clusters over 4 bytes) and every 20 frames compares a screenshot with the screenshot after a
whole redraw. **Measured**: A differs in 7 of 12 seeds (3, 5, 6, 7, 10, 12 and more; the same
frames on a rerun), always a cell of the number column showing an old number; B in 0 of 3.
Cause: A damages only the rows it draws, and it draws the rows that differ from the buffer it
draws into. A row equal to that buffer (two frames old) but different from the frame on screen
is neither drawn nor damaged, so a compositor that copies only the damage keeps showing the
other buffer's row. Damaging the whole buffer makes all six failing seeds pass, and so does a
7-line change that also damages rows differing from the buffer last committed (`a1-fix.diff`,
8 seeds, 0 differ). B's `paint` does exactly this ("damage is the rows that differ from the
frame on screen"). A's own 75-frame test did not reach it. In the real editor under sway I
could not provoke it (`jk.sh`, 6 runs): sway hands a buffer back at once, so A usually reuses
one buffer. A compositor that holds a buffer until the next is shown (weston pixman: 299
switches in 300 frames, B's MEASURED.md) alternates every frame; there `j` then `k` would show
it. That last step is a **hypothesis**, the probe is **measured**. Wanted: the fix, a plant
for it, and the pixel test checking after frames that return a row to its state two frames ago.

**2. Keys left in the queue wait for the next event.** `probe-keys` lets 8 keys arrive in one
read and asks `input()` for 4 at a time. **Measured** (1 run each): A delivers 5 of 8 and its
fds then stay quiet; B delivers 8 of 8, because it arms its timer for "now" while keys wait
(and has a plant for it). main.c asks for at most 256 keys, so this needs more than 256 from
one read: an input-method commit over 256 characters, or repeats piled up behind a stall.

**3. A reader of our clipboard that stalls stops the editor.** `probe-clip` sets 1 MB as the
selection; a client asks for it and reads nothing for 5 s. **Measured** (1 run each): A's
longest `input()` call is 5,003 ms, B's 3 ms. A's `xfer` polls for room and then writes to a
blocking pipe, so the write waits as long as the reader does; the 1 s timeout in its comment
does not hold. B writes from a forked grandchild, as wl-copy does. A non-blocking fd would
also do.

**4. When the compositor dies the editor spins.** `gone.sh` kills sway under the real editor.
**Measured** (1 run each): both builds keep running and use 200 of 200 CPU ticks in 2 s. A
ignores the read error. B notices and types `:qa!`, which the current thin core does not know
(it has `:q`, `:q!`, `:wq`), so it spins as well; its test passes because it tests the backend
alone. The journal keeps the text safe, so this is a hung process, not lost work. A needs the
error handled; what the editor should then do (Neovim exits on SIGHUP) is thread 0's call.

Smaller, from reading the code (**hypothesis**, not run):
- A's preedit asks for a redraw by pushing KEY_RESIZE, which makes main.c resize its grid
  (a malloc) on every preedit change.
- When main.c frees `shown` (cluster store emptied, Ctrl-Z), A frees and reallocates both
  buffers' grids; B only marks its buffers stale.
- A shapes at most 4,096 bytes a row; a row of 31-byte clusters wider than about 132 cells
  would lose its tail.
- Neither build sends KEY_DEL (open since PR 18).

## Why not B

B's drawing is simpler and it is right in the three places above, but it stopped at main as
of PR 6. To win it would need the cluster port with a test, `faces` instead of its own table,
text-input-v3, fractional scale, and something for the scroll gate, which is A's largest piece
of work. Its test also brings a nix-built, patched weston into every CI run. That is more work
than fixing A's four findings, and the result would be A's feature set again.

## What A should take from B

1. Damage against the frame on screen (finding 1), and B's "as damaged" capture in the pixel test.
2. The timer armed while keys wait, with its plant (finding 2).
3. The clipboard written outside the editor's thread (finding 3), and B's 1 MB test both ways.
4. Noticing a lost compositor (finding 4), once thread 0 says what the editor does then.
5. B's `fill` and `mix` (whole window 3.2 ms against 4.0 ms).

## Next

PR 33 is at "changes requested" for findings 1 to 4. When they are in, this thread reruns the
probes and the plants against main as it then is, and merges on green CI.
