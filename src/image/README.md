# Package 21: inline images (src/image/)

Images and LaTeX previews in window mode (FEATURES.md, "images and LaTeX inline"; Org's
org-download and LaTeX previews), wanted first for Davy's notes on lattice and order theory:
Hasse diagrams and formulas. The API is `include/image.h`; the core places images as
Neovim's virt_lines: the rows below a line that has an image, which scroll and count as
Neovim's filler lines do (its `w_topfill`).

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
- **Which image a line shows** (`image_line`, `line.c`): Markdown `![alt](path)` outside code
  blocks; Org `[[file:x.png]]`, `[[./x.png]]` or `[[/x.png]]` without a description
  (org-display-inline-images' default) and display math `\[...\]` or `$$...$$` on one line,
  rendered into `ltximg/` beside the note as Org does. Inline math stays text: the image goes
  below the line, not over the fragment. The core asks for every line it counts, many times
  a frame, so the answer is cached (1024 entries) by a hash of the line's bytes, the note's
  directory and the box: an edit makes a new key, so no edit hook, and a frame stats no file
  after the first.
- **The core** (`src/core/`, agreed with package 4): an image's rows are the filler of the line
  below it, as `win_get_fill()`; Neovim's topfill code is copied where it counts rows
  (`plines_win_full`, `topline_back`, `botline_forw`, `check_topfill`, `set_empty_rows`,
  scrolling, `cursor_correct`, H M L, zt zz zb, the page scrolls, the window size change), and
  the cursor never enters filler rows.
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
  keys, slot reuse, no allocation in a frame, LaTeX jobs with stand-in tools, which image a note's line shows), `tests/test_wl.c`
  and `tests/test_win.c` (an image cell on screen), `tests/fuzz_image.c`; 56 plants in
  `tests/plants/image.txt`, all caught.

- Images as filler rows, **measured** against `nvim --headless` with the same rows as
  `virt_lines` (`tests/test_core_screens.py --virt`): 2,750 random cases over 15 seeds
  (scrolling weighted so CTRL-E and CTRL-Y land inside the filler, splits, resizes, edits,
  undo, the popup) all give Neovim's screen but one (below). `tests/test_core_virt.txt`
  records seed 1's 150 and the 8 cases that catch the plants the 150 miss (`--pick`), and
  `make test` replays them. Copying Neovim's counting found quirks the core now copies too:
  CTRL-Y's stale `w_cline_row` (comp_botline() starts there), the "@@@" rows counted with the
  filler of the old `w_botline`, a count or operator never redrawn mid-command. A link edited
  in another window to a smaller image leaves no more filler than the line has
  (`tests/test_core_contract.c`; Neovim's result measured, 1 run). Each counting place has a plant in
  `tests/plants/core.txt`.

- The core's cost, **measured** (a bench linking the core: 4,000-line Org note, 128 x 36
  cells of 8 x 16 pixels, 500 frames of CTRL-E then a draw, -O2, 5 alternating runs of each
  side, median of the run medians and range): a note without images, this PR 0.123 ms a frame
  (0.109-0.141) vs main 0.115 ms (0.075-0.133), no measurable difference. The first version
  took 0.35 ms: the core asks for 264 lines a frame, and each paid the filetype lookup and the
  link scans; a line with no '[' and no '$' now returns before them. With a 400 x 300 image
  every 20 lines, 0.112 ms (0.111-0.114) vs 0.200 ms (0.191-0.202) for the same note with the
  links broken: image rows are copied, not shaped. A cache hit costs 251 ns (10^6 calls, 1 run).

## Residual risk

- One case differs (seed 10, case 16): the completion popup in a window narrower than its
  number column, above an image: Neovim's `w_cline_row` there was not found.
- An image changed on disk with its line unchanged keeps the old picture until the line
  changes or the file is reopened, as Org's inline images (org-redisplay-inline-images).
- A window being split has no width yet, so its images are sized for the screen's width for
  that moment, as Neovim counts virt_lines regardless of width; a window narrower than its
  number column counts them and shows none.
- Lines longer than 4096 bytes are looked at in their first 4096.

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
