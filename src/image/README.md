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
- **Wallpaper** (Davy, 2026-11-23: "install a wallpaper just like in the wezterm foto"): the PNG
  or JPEG `wallpaper` in Homeros' config folder (`~/.config/homeros/wallpaper`,
  `%LOCALAPPDATA%\homeros\wallpaper`), shown behind the text as WezTerm's
  `window_background_image` with background size Cover: scaled to cover the window, aspect kept,
  centred, a fifth of it over the Mocha base (`WALL_OPACITY`, about what Davy's WezTerm screenshot
  shows), wherever a cell has the default background. `image_wallpaper` decodes and lays it over
  the base once (again only when the file changes, looked for at each new window size, so one
  dropped in shows at the next resize) and says which part covers the window; the window's own
  scaler does the rest, so neither a frame nor a resize decodes or resizes anything:
  - Wayland: a synchronized subsurface below the window's surface, its viewport cropping and
    scaling the picture on the compositor (its GPU on Hyprland). The text buffer becomes ARGB,
    clear where the background is the default, drawn exactly as without a wallpaper (moved rows
    are still copied); colours carry alpha ff and `mix`/`over` blend alpha too, so antialiased
    edges and colour glyphs over the clear background come out premultiplied.
  - Win32: GDI's `StretchDIBits` scales the part straight from `image_wallpaper`'s pixels into
    a window-sized bitmap at each new size, COLORONCOLOR while an edge is dragged and HALFTONE
    (smooth) once it is let go; the shared painter (`src/paint`, `paint.h`'s `back`) copies a
    default-background cell's rectangle from that bitmap's pixels and draws its glyphs over it,
    and copies no moved rows, which would carry the wallpaper with the text (`tests/test_win.c`
    scrolls over it).
  Both go through `paint.h`'s `back`: Wayland's is one clear row after its buffers (stride 0),
  Win32's the scaled bitmap.
  Without the file nothing changes: no surface, XRGB buffers, opaque cells.
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
- Wallpaper frame time, **measured** (`bench/run.sh test_image.png 500 5 wall.png`: headless sway,
  pixman, 1280x800, 128x36 cells, a 1920x1080 wallpaper, 5 alternating runs of 500 frames,
  medians per run): scrolling a row per frame 0.691 ms (0.662-0.731) without, 0.698 ms
  (0.660-0.753) with; typing a character per frame 0.118 ms (0.115-0.118) without, 0.116 ms
  (0.113-0.122) with: no measurable difference. The first version drew the wallpaper into the
  text buffer itself, which forbids copying moved rows: scrolling took 3.11-3.44 ms (5 runs, p99
  up to 9 ms), so it was replaced by the subsurface. Its per-size CPU rescale (stb_image_resize,
  sRGB) took 43-214 ms per new size (5 runs), hence the compositor's and GDI's scalers.
- Wallpaper at a new size, **measured** (5 runs of 1,000 sizes): 0.5-0.7 microseconds (a
  `stat`); the first time, decoding the 1920x1080 PNG and laying it over the base, 44-60 ms.
- Wallpaper on Windows, **measured** on Windows itself (`bench/gdi.c` on CI's windows-latest,
  2 runs of 9 alternating runs each, median and range of the first; the second within 3%):
  drawing all 128x36 cells (Consolas 9x19) opaque, no wallpaper, 21.5 ms (21.4-21.9); with a
  `BitBlt` per cell, then transparent text, 31.4 (30.9-31.9), the PR's first version and 47%
  more; transparent text alone 20.9 (20.8-21.3), so the copies were the cost (9.2 ms alone);
  one `BitBlt` per row, then transparent text, 21.0 (20.9-21.7): no measurable difference from
  no wallpaper, hence a copy per run of changed cells. A new size, 1920x1080 picture to a
  1280x800, 1920x1080, 2560x1440 or 3840x2160 window: HALFTONE 15.3, 10.7, 47.7, 77.6 ms,
  COLORONCOLOR 1.2, 1.5, 4.3, 9.2 ms; `StretchDIBits` from the pixels costs what `StretchBlt`
  from a kept bitmap does (inside the spread), so no bitmap is kept and no copy made, and
  dragging an edge on a 4K monitor costs about 9 ms a step. `StretchDIBits` counts a top-down
  picture's source rows from its bottom (**measured** there and under Wine: rows 1 to 3 of 4
  asked, rows 0 to 2 drawn).
- Tests: `tests/test_image.c` (random PNGs against a model of what a window shows, the cache's
  keys, slot reuse, no allocation in a frame, LaTeX jobs with stand-in tools), `tests/test_wl.c`
  and `tests/test_win.c` (an image cell on screen; the wallpaper's pixels, text over it,
  reversed cells and the cursor as without it, the cover's cut, a changed cell's wallpaper
  copied again, moved rows (copied on Wayland, not on Win32), taken away), `tests/fuzz_image.c`;
  68 plants in
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
- Wallpaper, the compositor's time, **measured** under CPU renderers (PR 135's review, CPU
  ticks of the compositor over the same frames, 3 to 5 alternating runs): scrolling costs sway
  1.9 and weston 13 2.2 to 2.4 times the CPU with a wallpaper (an ARGB surface blended over a
  scaled one, where an opaque one was copied); a typed character costs sway 1.9 about 2 ms (it
  recomposites the whole window, 17 times the 0.1 ms without), weston nothing more (it repaints
  the damaged row). **Hypothesis:** on Hyprland's GPU this is next to nothing; not run (no GPU
  here).
- Wallpaper on Hyprland with blur on (its default), **hypothesis** from reading Hyprland's
  source (PR 135's review), not run: a window whose surface is not opaque gets the blurred
  desktop drawn under that surface, that is over the wallpaper's subsurface, so the wallpaper
  may not show, and a blur pass runs per frame. Davy to look once on his laptop: if the desktop
  shows through blurred instead of the picture, a window rule `noblur` for Homeros is the first
  answer to compare with an empty blur region (ext-background-effect-v1).
- Wallpaper: sway 1.9's pixman renderer drops the viewport source's offset (**measured**: an
  8x8 picture's rows 1 to 6 asked, rows 0 to 5 shown), so under it the cover is cut from the
  picture's top or left, not centred; `tests/test_wl.c` checks only that the source's size
  reaches the compositor. The part is cut in whole picture pixels (the source is integral), so a
  picture only a few pixels across is covered off by up to a pixel.
- Wallpaper on Windows, **hypothesis:** the switch to HALFTONE when a drag ends
  (`WM_EXITSIZEMOVE`) is not tested: Wine runs no modal sizing loop for a test to drive, and
  the two modes differ only in smoothness. Maximising or snapping, one size, is smooth at once.
- Images placed in a note over a wallpaper keep their own padding in the base colour (they are
  laid over the base when loaded).
- `GdiFlush` before writing the DIB's bits follows CreateDIBSection's documented rule; Wine
  draws at once, so no test sees it.
