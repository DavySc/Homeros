# Package 21: inline images (src/image/)

Images and LaTeX previews in window mode (FEATURES.md, "images and LaTeX inline"; Org's
org-download and LaTeX previews), wanted first for Davy's notes on lattice and order theory:
Hasse diagrams and formulas. The API is `include/image.h`; the core places images (a later
package 4 PR: Markdown and Org image links, LaTeX fragments, the rows under the link).

## How

- **Decode once, draw by copying.** `image_load` decodes PNG or JPEG with stb_image, scales it
  down to fit the space asked for (stb_image_resize, sRGB, alpha-weighted), lays it over the
  background and pads it to whole cells. A window then shows an `ATTR_IMAGE` cell with one
  `memcpy` per pixel row (`image_draw`), allocating nothing.
- **The cell is the diff.** An image cell's `ch` holds the image's number, the tile's column and
  row and the cache slot's generation, so the backends' existing cell comparison and row hash
  (scroll copies in Wayland) handle images with no new code, and a slot reused for another
  image changes its cells.
- **Cache.** 255 slots keyed by path, mtime, size, box, cell and background (`file_id`, so
  Windows takes UTF-8 paths); least recently used reused first, at most 256 MB of pixels. A file
  that does not decode is remembered too, so it is not read at every frame.
- **LaTeX** as Org's `dvipng` process: `latex`, then `dvipng -T tight -bg Transparent -fg <text
  colour>`, through the loop (`proc_spawn`); the PNG is named by a hash of fragment, colour and
  dpi, so it is never rendered twice nor shown stale, and a failed fragment is not retried.
  Each job writes under its own names (with the pid) and renames its PNG into place last.
  Every failed fragment is remembered for the session (a screen of them, with no latex
  installed, would otherwise start one job after another for ever), and each tool is killed
  after 10 s (a fragment TeX never finishes would hold its job and a core).
- **Terminal:** `Backend.cell` is 0 x 0, so the core places no images and the link text stays,
  as Emacs does in a terminal.

## Evidence

- Decoder, **measured** (5 alternating runs of 20 decodes each, medians per run), stb_image vs
  libpng 1.6 + libjpeg-turbo 2.1: diagram PNG 800x600 1.59-2.21 vs 3.74-6.90 ms; photo JPEG
  1200x900 8.77-11.61 vs 6.28-8.81 ms; noisy PNG 1200x900 overlapping (52.8-64.5 vs
  46.7-52.0). stb_image is one decoder for Linux and Windows (which has no libpng; WIC would be
  a second mechanism), so it wins on code with no measurable loss on the images notes hold.
- Frame time, **measured** (`bench/run.sh hasse.png 500 5`: headless sway, pixman, 1280x800,
  128x36 cells, a 53x18-cell diagram scrolled a row per frame, 5 alternating runs of 500
  frames): present() median 0.737 ms (0.713-0.753) with the image, 0.746 ms (0.709-0.756)
  with text only: no measurable difference.
- LaTeX, **measured**: `$\forall x \in L,\ x \wedge (x \vee y) = x$` at 150 dpi, latex +
  dvipng from TeX Live 2023, 5 runs: 147 ms median (131-222), off the frame (the loop runs it).
- Render race, **proved** (TLC, `design/specs/LatexRender.tla`, 3 editors, 2 lookups each,
  7,327 states): with own names, rename last and one job per hash in an editor, the final PNG is
  always whole and never written again; writing the final name directly, a shared temporary
  name, or no job table each break it.
- Tests: `tests/test_image.c` (random PNGs against a model of what a window shows, the cache's
  keys, slot reuse, no allocation in a frame, LaTeX jobs with stand-in tools), `tests/test_wl.c`
  and `tests/test_win.c` (an image cell on screen), `tests/fuzz_image.c`; 34 plants in
  `tests/plants/image.txt`, all caught.

## Residual risk

- **hypothesis:** a file rewritten with the same size and the same mtime (to the filesystem's
  granularity) is not reread; edits in a drawing program change both in practice.
- **hypothesis:** stb_image is less hardened than libpng against hostile files; images here are
  Davy's own notes.
- Decoding is on the loop: a 1200x900 noisy PNG takes about 55 ms once, on first sight; a 4K
  screenshot scaled into its box took 127-153 ms to decode plus 166-177 ms to resize (PR 102's
  review), again for each new box, since the box is part of the key.
- A latex or dvipng still running when the editor exits is not killed (the timeout ends with
  the editor); one that never finishes then runs on.
- `GdiFlush` before writing the DIB's bits follows CreateDIBSection's documented rule; Wine
  draws at once, so no test sees it.
