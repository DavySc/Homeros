# Package 8: the Wayland window (build A)

`backend_wl` (include/backend.h) as foot does it: an xdg-shell window with no
decorations, drawn on the CPU into two wl_shm buffers, one poll loop, no
threads, its text shaped by package 7's font.h.

| Part | How | Copies |
|---|---|---|
| Frames | draw only when the last frame's callback came and a buffer is free (Present.tla "framecb"); each buffer keeps the grid it shows, and only the cells that differ from it are drawn; the damage is the rows that differ from the frame on screen, which is the other buffer's when the buffers alternate | foot (build B's damage rule) |
| Changed cells | of a changed row only the span from the first to the last changed cell is drawn, widened on each side to the start of a word after a blank, so the span shapes as the whole row would and no spill crosses its edges | Neovim's grid_put_linebuf (sends only the changed columns) |
| Text | the span is shaped as one run (font.h) and coloured per cell: colour does not change the font, so runs are not split by it | kitty |
| Scroll | the core sends whole grids, not scroll events, so the move is found as the one most changed rows share, by row hashes without the number column (relative numbers change it on every scroll); a row is copied from where it was and then only its changed span drawn (the number), when that span is smaller than in place; copied in the move's direction so no source is drawn over first | Emacs (dispnew.c, scrolling_window, finds the move by row hashes), foot (the copy) |
| Wide symbols | a glyph wider than its cell (73 of Mathlib's 295 notation symbols at 20 px, reviews/package-7.md) spills into the blank cells after it, up to the next non-blank cell, instead of being cut; in Lean it is mostly followed by a space. A ligature's glyph (`->`, `=>`, ` /\ `) sits on its last cell and reaches back over its word; any glyph that starts left of its cell (↔, ⇔) is cut at its word's start, so it may draw over a character before it in the same word, never over the blank before the word. The part of a glyph over an earlier cell of its word takes that cell's colours, so the cursor there shows its half reversed. A partial redraw shapes the blank before its span too, so ` /\ ` joins as typed. Both keep a partial redraw equal to a whole one | foot's overflowing glyphs; foot and kitty for ligatures; kitty's per-cell colours |
| Size | the size xdg_toplevel configures; 800x600 when it leaves the choice to us | foot |
| Scale | fractional-scale-v1 with a viewport: the buffer is the logical size times scale/120, the font is re-opened at 16 px times the scale | foot |
| Keys | xkbcommon keysyms and UTF-32 (with the Control transformation, so Ctrl-R is 18); Backspace is 127, as a terminal sends; client-side repeat at the compositor's rate and delay, on a one-shot timerfd that also fires at once while keys wait in the queue (main.c reads only when an fd is readable) | foot, build B |
| Input method | text-input-v3: committed text becomes keys, the preedit is drawn underlined at the cursor, the cursor rectangle is sent on each move | foot |
| Clipboard | wl_data_device, text/plain;charset=utf-8; our own selection is answered without the compositor; another's is read through a pipe with a 1 s timeout per read, as Neovim's clipboard provider times out; ours is written to a reader by a forked grandchild, so a reader that stalls never stops the editor | Neovim, foot, wl-copy |
| Pointer | cursor-shape-v1's text cursor; a left press is KEY_MOUSE_LEFT, a wheel notch (value120) one KEY_WHEEL_UP/DOWN, a touchpad one per 3 rows scrolled, at the pointer's cell | Neovim GUIs |
| Close | xdg_toplevel.close is KEY_CLOSE; a broken connection to the compositor makes input() return -1, so main.c quits without writing and keeps the crash journal, as Neovim's preserve_exit() does (backend.h) | build B notices it too |
| Colours | grid.h's `faces` (ref/theme.c, Catppuccin Mocha): their colours, underline and strikethrough; colour glyphs (emoji) blended as premultiplied BGRA; no bold or italic, which font.h does not open | ref/tty.c |

The font size, 16 px at scale 1, is a hypothesis: Davy's size is not recorded.

## Tests

`tests/test_wl.c` (in `make test`) runs the backend against real compositors
the test starts headless, with the CI's apt packages (sway 1.9, weston 13,
wtype, wl-clipboard, grim, adwaita-icon-theme):

- weston (kiosk shell, no seat): the configured size, frame callbacks.
- sway (wlroots, pixman; it has the protocols Hyprland has): the size after
  mapping and after a floating resize; scale 1.5 and back; keys through a
  virtual keyboard, including Esc, Backspace, Ctrl-R, Up, Enter, Tab and
  é; key repeat (300 ms delay, 25/s, 1 s hold: 19 keys); the clipboard
  both ways and with too little room, and a reader of 1 MB that stalls 2 s while the editor runs on; an input method (the test is one,
  input-method-v2): preedit redraw and committed text; the text cursor (seen in
  screenshots: the I-beam reaches above the pointer, the default arrow does
  not); left click, wheel and touchpad keys and their cell; no drawing while
  hidden in the scratchpad, drawing again once shown; keys taken 4 at a time all
  arrive; close; the compositor killed, noticed once; a wide symbol
  (⟹) spills into the blank cell after it and is cut at a character; a
  cluster longer than a cell's 4 bytes (👍🏽) drawn from grid.c's store; a
  ligature (` /\ `, `->`) drawn back over its first cell, and with the cursor on that cell
  shown reversed there, not as a bare cursor; rows changed to
  and from ` ↔`, ` ⇔ b`, `x ↔` and `x /\ y` drawn as a whole redraw draws
  them; and a
  screenshot check that after 75 frames of random edits (characters, `->`,
  ⟹, emoji, long clusters, attrs, faces), cursor moves and scrolls either way, into both
  buffers, the screen equals a whole redraw, and so does a row put back as it
  was two frames ago (in the buffer drawn into already, not on screen).

**measured** (the review's probes, `reviews/package-8-repro/`, rerun on the
fixes, 1 run each): `probe-pix` 0 of 120 checks differ (seeds 1 to 12, 200
frames each; 7 of 12 seeds differed before); `probe-keys` 8 of 8 keys; `probe-clip`
longest `input()` 1 ms with a reader stalled 5 s (5,003 ms before), the reader
gets all 1 MB; `gone.sh` under the real editor: it exits, 2 of 2 runs (an
unmodified file; a modified one waits for thread 0's change).

**measured**: 10 of 10 runs pass (system sway 1.9, weston 13) before
font.h, and 15 of 15 earlier runs under nix's sway 1.12 and weston 16; with
font.h and the current drawing, 5 of 5. The tests found two bugs
fixed here: the seat's capabilities arrive in the bind's roundtrip, so a
listener added after it lost the keyboard (3 of 10 runs failed); and a
keyboard or pointer that goes away must be released and fetched again.

**measured**: `tests/plants/wl.txt` has 34 plants (frame callback, repeat,
Backspace, seat listener, clipboard short send, clipboard past max, input
method text, preedit redraw, scale, size, text cursor, mouse cell, wheel,
touchpad step, close, changed cells, cursor move, damage, scroll source,
glyph cut at its cell, span widened left, span widened right, spill past a
character, long cluster read from the cell, damage only rows drawn, keys
left waiting, clipboard written by the editor itself, lost compositor
ignored, background layer ignored, guide drawn as the preedit, ligature
cut at its own cell, glyph cut at the span instead of its word, span shaped
without the blank before it, ligature in its last cell's colours under
the cursor); each was run and each fails the test. Copying a scroll against
its direction has no plant: a row is copied only where the cells it shows
equal the source's, so the screen stays right and only the copy is lost.

## Performance gates

`make build/wl/bench` builds `src/main.c`'s loop with clocks (`src/wl/test/bench.c`).
Run as `build/wl/bench FILE N` while typing N keys; it prints the first frame
committed after start and the frame-ready time after a key (or after the
frame callback when the key came while a frame was on its way).

**measured**, headless sway 1.9 with pixman, 1280x800, 16 px font, this
sandbox's CPU, 5 runs each (10 for the first frame), the thin editor
(ref/core.c, relative numbers) typing into ref/keys.c, and scrolling
(`j` held) through 4,154 lines of Mathlib (Order/Filter/Basic,
MeasureTheory/Integral/Bochner/Basic, Analysis/Normed/Group/Basic), with
wtype:

| | median | range | gate |
|---|---|---|---|
| first frame committed | 9.8 ms | 7.7 to 13.0 ms | under 10 ms: 6 of 10 runs |
| frame ready p99, 200 keys at 10 ms | 3.2 ms | 1.9 to 3.7 ms | under 2 ms: 1 of 5 runs |
| frame ready p99, 2,000 keys at 1 ms | 1.82 ms | 1.59 to 1.93 ms | under 2 ms |
| frame ready p50, typing | 0.27 ms | 0.24 to 0.28 ms | |
| frame ready p99, scrolling (400 lines at 10 ms) | 2.06 ms | 1.39 to 2.31 ms | under 2 ms: 2 of 5 runs |
| frame ready p50, scrolling | 0.84 ms | 0.81 to 0.97 ms | |
| frame ready max | 4.6 ms | 4.2 to 6.1 ms | (typing; the first frame into the second buffer, drawn whole) |
| frames for 2,000 keys at 1 ms | 689 | 687 to 690 | one per compositor repaint |
| wake-ups in 20 s idle | 0 | 0 to 0 | 0 |
| RSS idle, empty file | 12.0 MB | 11.98 to 12.10 MB | under 15 MB |
| threads | 1 | | |

What the numbers come from:

- Scrolling, **measured** against drawing every changed row whole (the
  version before, alternating, 5 runs each): p50 0.84 ms against 2.15 ms
  (2.10 to 2.26), p99 2.06 ms against 4.37 ms (3.89 to 4.63). With relative
  numbers every row changes on a scroll, so copying whole rows found no row
  to copy: p50 2.16 ms (2.07 to 2.62) against 2.14 ms (2.06 to 2.22)
  without it, no measurable difference (5 runs each); copying and then drawing only the changed number is what helps.
- Typing p99 at 200 keys: **measured** (the core alone, 1 run,
  deterministic) from the 155th key the line is wider than the window and
  the thin core scrolls sideways one column per key, so 37 rows change on
  each; a sideways scroll is not copied. Before key 155 a key changes 2 rows
  (the line and the status line).
- First frame: package 7's `font_open` takes 3 to 4 ms (reviews/package-7.md),
  more than the stand-in font it replaced, which had 8.4 ms (7.4 to 14.3,
  10 runs) here.
- Filling rows with memcpy and blending red and blue in one multiply
  (build B's `fill` and `mix`): whole-window frames (`G` and `gg` on 4,154
  lines) present p50 1.76 ms (1.71 to 1.96) against 1.90 ms (1.82 to 2.16),
  **measured**, 10 alternating runs each, faster in 9 of 10 pairs; the ranges
  overlap. `mix` equals dividing each channel by 255 for every input
  (**proved**, exhaustive: 33,554,432 cases, all three channels).
- Starting a new buffer from the other buffer's picture instead of drawing
  it whole: **measured**, no measurable difference (typing p99 2.4 ms
  against 2.4 ms, max 3.3 against 4.0 with overlapping ranges, 5 runs
  each), so not kept.

**pending** (needs Davy's machine): the same on Hyprland with NVIDIA, key to
screen within one refresh, and the first frame there (Hyprland may configure
before mapping, unlike sway, which maps at 800x600 and then resizes).

**proved** (TLC, design/specs/Present.tla, unchanged): drawing only after the
frame callback wastes no frame with either kind of compositor and draws
nothing while hidden.

## Not built, and why

- No wl_output scale or preferred_buffer_scale fallback: Hyprland has
  fractional-scale-v1; weston 13 has neither it nor a seat, so weston is
  tested for size and frames only.
- No drag and drop, primary selection, compose sequences or decorations:
  not in FEATURES.md (hypothesis that compose is covered by the input method).
- A key queue over 1,024 keys between two loop passes drops keys
  (hypothesis: not reachable by typing).
