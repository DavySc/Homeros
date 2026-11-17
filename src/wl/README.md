# Package 8: the Wayland window (build A)

`backend_wl` (include/backend.h) as foot does it: an xdg-shell window with no
decorations, drawn on the CPU into two wl_shm buffers, one poll loop, no
threads, its text shaped by package 7's font.h.

| Part | How | Copies |
|---|---|---|
| Frames | draw only when the last frame's callback came and a buffer is free (Present.tla "framecb"); each buffer keeps the grid it shows, so only rows that differ from it are drawn and damaged | foot |
| Text | a changed row is shaped as one run (font.h), coloured per cell: colour does not change the font, so runs are not split by it | kitty |
| Scroll | rows that moved are copied within the buffer, not shaped again: the core sends whole grids, not scroll events, so the move is the one that most changed rows have, counted by row hashes against the rows the buffer shows; a row is copied only if its cells equal the source's, and in the move's direction so no source is drawn over first | Emacs (dispnew.c, scrolling_window: the move found by comparing the rows shown with the rows wanted), foot (the copy) |
| Wide symbols | a glyph is clipped to its row, not its cell, so a symbol wider than its cell (73 of Mathlib's 295 notation symbols at 20 px, reviews/package-7.md) spills into the next cell, in Lean mostly a space | foot's overflowing glyphs |
| Size | the size xdg_toplevel configures; 800x600 when it leaves the choice to us | foot |
| Scale | fractional-scale-v1 with a viewport: the buffer is the logical size times scale/120, the font is re-opened at 16 px times the scale | foot |
| Keys | xkbcommon keysyms and UTF-32 (with the Control transformation, so Ctrl-R is 18); Backspace is 127, as a terminal sends; client-side repeat on a timerfd at the compositor's rate and delay | foot |
| Input method | text-input-v3: committed text becomes keys, the preedit is drawn underlined at the cursor, the cursor rectangle is sent on each move | foot |
| Clipboard | wl_data_device, text/plain;charset=utf-8; our own selection is answered without the compositor; another's is read through a pipe with a 1 s timeout per read, as Neovim's clipboard provider times out | Neovim, foot |
| Pointer | cursor-shape-v1's text cursor; a left press is KEY_MOUSE_LEFT, a wheel notch (value120) one KEY_WHEEL_UP/DOWN, a touchpad one per 3 rows scrolled, at the pointer's cell | Neovim GUIs |
| Close | xdg_toplevel.close is KEY_CLOSE | |
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
  both ways and with too little room; an input method (the test is one,
  input-method-v2): preedit redraw and committed text; the text cursor (seen in
  screenshots: the I-beam reaches above the pointer, the default arrow does
  not); left click, wheel and touchpad keys and their cell; no drawing while
  hidden in the scratchpad, drawing again once shown; close; and a
  screenshot check that after 40 frames of random edits (characters, attrs,
  faces, the cursor) into both buffers the screen equals a whole redraw.

**measured**: 10 of 10 runs pass (system sway 1.9, weston 13), and 15 of 15
earlier runs under nix's sway 1.12 and weston 16. The tests found two bugs
fixed here: the seat's capabilities arrive in the bind's roundtrip, so a
listener added after it lost the keyboard (3 of 10 runs failed); and a
keyboard or pointer that goes away must be released and fetched again.

**measured**: `tests/plants/wl.txt` has 18 plants (frame callback, repeat,
Backspace, seat listener, clipboard short send, clipboard past max, input
method text, preedit redraw, scale, size, text cursor, mouse cell, wheel,
touchpad step, close, changed cells, cursor move, damage); each was run and
each fails the test.

## Performance gates

`make build/wl/bench` builds `src/main.c`'s loop with clocks (`src/wl/test/bench.c`).
Run as `build/wl/bench FILE N` while typing N keys; it prints the first frame
committed after start and the frame-ready time after a key (or after the
frame callback when the key came while a frame was on its way).

**measured**, headless sway 1.9 with pixman, 1280x800, 16 px font, this
sandbox's CPU, 5 runs each, the thin editor (ref/core.c) typing into
ref/keys.c with wtype:

| | median | range | gate |
|---|---|---|---|
| first frame committed | 8.4 ms | 7.4 to 14.3 ms (10 runs) | under 10 ms: 9 of 10 runs |
| frame ready p99, 200 keys at 10 ms | 0.97 ms | 0.75 to 1.84 ms | under 2 ms |
| frame ready p99, 2,000 keys at 1 ms | 0.50 ms | 0.43 to 0.67 ms | under 2 ms |
| frame ready max | 3.4 ms | 3.1 to 4.6 ms | (the first frame into the second buffer, drawn whole) |
| frames for 2,000 keys at 1 ms | 695 | 689 to 701 | one per compositor repaint |
| wake-ups in 20 s idle | 0 | 0 to 0 | 0 |
| RSS idle, empty file | 9.9 MB | 9.86 to 9.91 MB | under 15 MB |
| threads | 1 | | |

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
