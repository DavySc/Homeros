# Package 8, build B: what is tested, measured and pending

Every claim is tagged **proved**, **measured** (with its run count) or
**hypothesis**, as CLAUDE.md asks.  The test is `tests/wl_weston.c`, run by
`make wl-test` (part of `make test`) against weston 16 headless with its test
plugin (`src/wl/weston-test.nix`), on both of weston's renderers.

## The contract

- **proved** (Present.tla, the "framecb" policy, checked by TLC in
  `design/specs/check.sh`): drawing only when no frame callback is pending and
  a buffer is free.  `wl_ready` is that condition.
- **measured** (every `make wl-test`, 2 renderers): `ready` is false after a
  present and true once the callback came; 1 s of back-to-back redraws gives
  61 frames on a 60 Hz output (limit 62).

## Why two renderers

- **measured** (300 frames, 1 run each, buffer switches counted): weston's
  pixman renderer holds a buffer until the next one is shown, so frames
  alternate between the two buffers (299 switches of 300); its GL renderer
  (Mesa llvmpipe) gives a buffer back at commit, as Hyprland does, so one
  buffer is reused (0 switches of 300).  Only GL catches a backend that draws
  without waiting for the frame callback; only pixman exercises the second
  buffer.  So `wl-test` runs everything on both.
- **hypothesis**: Hyprland releases at commit like weston's GL renderer (its
  renderer copies shm buffers into textures).  Pending on Davy's machine.

## Accepted-when checks (weston, both renderers, every `make wl-test`)

| Check | What the test does |
|---|---|
| frame callbacks | ready false until the callback; ≤ 62 frames in 1 s |
| configure size | kiosk shell's configured size at scale 1 and 2 gives rows = 768·scale / cell height, cols likewise |
| scale | output scale 2 opens the font at twice the size and sets buffer scale 2 |
| pixels | 300 random frames drawn piece by piece, with a glyph twice a cell wide (U+27F9) and a margin right of the cells; the screen equals a full redraw, both as damaged (0 pixels differ) and after a full-screen window covers it and leaves (0 differ) |
| keys | a, A, Ctrl-R, Backspace (127, not xkb's 8), Enter, Esc, Up, b; Delete dropped; leftover keys wake the loop |
| key repeat | j held 600 ms with delay 200 ms, rate 50/s gives 19 to 23 keys; stops on release; no timer while idle (poll 2 s returns 0) |
| pointer | click at cell (3,5); wheel 15 units per key, also accumulated over small events |
| IME | text-input v1 against an input method in the test: preedit drawn before any key, commit gives 你 and Enter |
| clipboard | own selection answered locally; another client reads it; another client's selection read in; 1 MB both ways; a 4-byte buffer gets the length and 4 bytes |
| editor | the real `homeros` in a window types, saves and quits |
| compositor gone | weston killed: the editor gets `:qa!` once, then no busy loop |

Plants: `tests/plants/wl.txt`, 20 bugs, each must fail `make wl-test`.
**measured** (`planted.sh` on `wl.txt`, 1 full run on the final code): 19 of
19 caught; the 20th (glyph ink past the last cell, added with package 7's
fonts) is caught too (1 run).  The stale-buffer plant was missed at first: the window that
covers the editor was 100x100, so most rows were never recomposited, and the
capture's kick frames redrew the stale rows; the cover is now the whole
screen and pixman captures without a kick.

## Performance (weston 16 headless, 1600x1000, package 7's fonts)

`tests/wl_weston.c` with `WL_MEASURE=1`, built -O2 without sanitizers;
**measured**, 5 alternating runs per renderer, median (range of the runs'
values):

| | pixman | GL |
|---|---|---|
| frames drawn for 2,000 keys 1 ms apart | 122 (121 to 122) | 122 (122) |
| key to commit, p50 | 8.50 ms (8.40 to 8.69) | 8.55 ms (8.53 to 8.73) |
| key to commit, p99 | 16.60 ms (16.53 to 16.72) | 16.74 ms (16.67 to 16.84) |
| frame ready, one row changed, p50 | 0.086 ms (0.076 to 0.097) | 0.072 ms (0.065 to 0.075) |
| frame ready, one row changed, p99 | 0.25 ms (0.16 to 0.56) | 0.16 ms (0.15 to 0.18) |
| frame ready, whole window, p50 | 3.01 ms (2.99 to 3.10) | 2.92 ms (2.84 to 3.02) |
| frame ready, whole window, p99 | 5.37 ms (4.85 to 8.05) | 4.62 ms (3.85 to 5.95) |

- Key to commit is the wait for the next frame callback at 60 Hz (half a
  frame on average, a frame at worst): keys are coalesced into one frame per
  callback, which is the design (Present.tla), not drawing time.
- **measured** (callgrind, 1 run, whole program): of the drawing work,
  HarfBuzz shaping inside package 7's `font_shape` is about 60% of
  instructions; `paint` (filling, blending glyphs) about 21%.  With a
  stand-in font that shapes nothing the whole window took 1.25 ms (3 runs),
  so that part is this package's.
- `mix` (glyph blending, red and blue in one multiply): every one of its
  50,331,648 inputs (256 coverages × 2^16 channel pairs × 3 lanes) equals the
  per-channel `d + (s - d) * a / 255` (**measured**, 1 exhaustive run of a
  scratch program; the pixel test covers it in the repo).
- The editor itself (`build/hom`, a file of a few lines, weston kiosk
  1600x1000): RSS 12.3 MB, 1 thread, 0 wake-ups in 20 s idle (**measured**,
  5 runs, with the stand-in font).
- First frame committed after exec, by strace timestamps: median 17.3 ms
  (14.9 to 20.4) and 19.5 ms (15.9 to 19.5), two sets of 5 runs, with
  package 7's fonts.  `font_open` alone is 6.1 ms median (5.1 to 8.5, 5
  runs).  The 10 ms gate is not met here; it is to be measured on Davy's
  machine without strace (**hypothesis** that it is near: the container's
  cold fontconfig cache and strace both add time).

## Pending on Davy's Hyprland machine

- The performance gates on Hyprland itself (DESIGN.md's numbers): frame time,
  key-to-photon, idle wake-ups.
- Cursor shape (cursor-shape-v1: Hyprland has it, weston does not).
- text-input v1 with fcitx5 on Hyprland (weston has only v1; Hyprland supports
  v1 in `src/protocols/TextInputV1.cpp`, **hypothesis** that fcitx5 works
  through it as through weston's).
- The wheel unit (15 per notch, weston's value; **hypothesis** for
  Hyprland's touchpads).
- The font size: 16 px at scale 1 (**hypothesis**, Davy's preference unknown).
- Window close (Hyprland's killactive sends xdg_toplevel.close → KEY_CLOSE;
  weston cannot send it, so untested).

## Glyphs larger than their cell

Ink is clipped to the glyph's row and to the cells (package 7's review: 73 of
295 Lean symbols reach outside their cell).  A frame redraws whole rows and
never the margins, so ink let past those bounds would outlive what drew it;
clipping keeps a partial frame equal to a full redraw (the pixel test, with
U+27F9).  Ink overflowing into the next cell of the same row is kept.  Tall
symbols lose what is above or below their row (**hypothesis** that this is rare in
Davy's Lean; not yet compared with foot or kitty).

## Not tested anywhere

- A hidden window (no frame callbacks for a long time): covered by
  Present.tla only (**proved** in the model, not run).
