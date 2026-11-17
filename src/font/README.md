# Package 7: fonts and shaping

`font.c` implements `include/font.h`: the window backends (packages 8 and 9) hand it one row's run of UTF-8 and get back positioned glyph bitmaps.

## How it works, and what it copies

| Step | What it does | Copied from |
|---|---|---|
| Fonts | fontconfig sorts every font for the family asked (`FcFontSort`, trimmed to fonts that add characters); the first is the main font, the rest are loaded only when a cluster needs one | foot / fcft |
| Shaping | HarfBuzz shapes the whole run with the main font, ligatures on (`->` comes out as JetBrains Mono's arrow); runs are shaped left to right, as Neovim draws them | foot, kitty |
| Fallback | each cluster that came out with a missing glyph is shaped again with the first later font, in fontconfig's order, whose character set has all of its characters, until none is missing or no font is left; joiners, variation selectors and tags need not be in that set, because HarfBuzz hides them | wezterm (`shaper.rs`) |
| Emoji | a cluster with an Emoji_Presentation character (Unicode 16 table, `emoji_table.py`) or U+FE0F goes to a colour font if one has it, even when an earlier font has a plain glyph; colour bitmap strikes (Noto Color Emoji) are scaled to the em by area averaging | fcft, kitty |
| Clusters | the run's characters are uni.h's (`uni_len`, `uni_cp`), not HarfBuzz's own UTF-8 decoding, so a cluster starts exactly where the rest of the editor says a character starts (they differ on overlong forms such as `C0 B2`, which the fuzzer found) | |
| Cache | glyph bitmaps are cached by (font, glyph) in a hash table of 8,192 slots and an 8 MB arena taken at `font_open`; when either fills, it is emptied and the run starts again | |

No shaped-run cache: shaping a row costs a median of about 10 µs (below), and the backend reshapes only rows whose cells changed. A whole frame is another matter: **measured** by the review (`reviews/package-7-repro/frames.c`, all of Mathlib paged 60 rows at a time, every glyph cached, 11 runs), shaping the 60 rows of one frame takes a median of 0.98 ms (0.96 to 1.01) and p99 1.91 ms (1.74 to 2.04) of the 2 ms frame budget, almost all of it HarfBuzz on the main font (fallback adds about 0.04 ms at the median). So a scroll that reshapes every row fits only if drawing is cheap; see Open.

## Evidence

Tags as CLAUDE.md asks. Font set: Ubuntu 24.04's fonts-jetbrains-mono, fonts-noto-core, fonts-noto-cjk, fonts-noto-color-emoji and fonts-dejavu-core (what CI installs), at 20 pixels per em.

- **measured** (`tests/test_font.c`, every `make test`): all 295 non-ASCII characters of Mathlib's notation draw, each alone and after a letter: no `.notdef`, some ink. The list is `mathlib_symbols.py` on mathlib4 at 7668accf6b41 (2026-11-17): the files named `*otation*.lean` (110 characters) and every `notation`, `infix`, `prefix`, `postfix`, `syntax` and `macro` declaration (279), 295 together. Without a CJK font, ／ ＼ ￢ (U+FF0F, FF3C, FFE2) do not draw, so the set needs fonts-noto-cjk.
- **measured** (once): of all 1,364 non-ASCII characters anywhere in Mathlib's sources, 1,362 draw; ꟴ (U+A7F4) and 𐞥 (U+107A5), from Unicode 14, are in none of these fonts.
- **measured** (test): q + U+0303 gives two glyphs in one cluster with the tilde over the q; a + two marks is one cluster; the family 👨‍👩‍👧, the rainbow flag 🏳️‍🌈 and 👍🏽 each give one colour glyph about two cells wide; 😀 is in colour although DejaVu Sans, earlier in fontconfig's order, has a plain one; ❤ is plain and ❤️ is colour; `->` gives other glyphs than `-` and `>` alone; Hebrew stays in logical order.
- **measured** (test): a second pass over a line already drawn hits the cache for every glyph and makes 0 allocations, counted with ASan's malloc hook, which sees the libraries' allocations too; so does a 2,000-glyph run longer than any before, because HarfBuzz's buffer is taken at `font_open` (`hb_buffer_pre_allocate`). Other first uses allocate once each and never again: a run starting in a script not seen first before makes HarfBuzz build and cache a shape plan (19 allocations, measured by the review, 1 run; 32 scripts shaped three times: 3,377 allocations in round one, 0 after). A miss does allocate: FreeType renders into its own buffer, and a fallback font's first use loads it (5,531 allocations after `font_open` for 475 misses and 514,502 rows of the first 2,000 Mathlib files, 1 run). DESIGN.md's "no allocation in the redraw path after start-up" therefore holds for hits only; removing it for misses (a FreeType memory pool) is open.
- **measured** (test): the cache empties itself when its slots fill (8,000 CJK characters) and when its memory fills (every emoji at 60 pixels, about 30 MB), and the first glyph is drawn the same afterwards.
- **measured** (`fuzz_font`, `FUZZ_SECONDS` in every `make check`; 128,000 runs in 60 s here): on arbitrary bytes, at most `max` glyphs; clusters in order and at uni.h character starts; positions near their own cluster; shaping again from the cache gives the same result.
- **measured** (`tests/plants/font.txt`): 11 planted bugs, each caught by the check named in it.

### Glyph cache hit rate (`bench/hitrate.c`)

Every line of all 8,715 files of Mathlib (2,368,917 rows, tabs as 4 spaces, cut at 400 bytes), each drawn once as one run:

- Scrolling through all of Mathlib in one session: 99.998% (93,531,957) of 93.5 M glyph lookups hit (1,529 misses: every glyph Mathlib uses, drawn once). The hit rate is the same in every run (the cache is deterministic); time per row, 5 runs: median 10.05 µs (range 10.01 to 10.17), p99 44.1 µs (43.1 to 45.9), slowest row 3.9 to 17.4 ms (a fallback font's first load, such as Noto CJK).
- A fresh editor's first screen (`-cold -rows 60`, `font_open` per file): 95.96% of 16.8 M lookups hit (1 run, deterministic); the worst file's first screen 58.8%, 185 of 8,715 files under 90%; per row median 9.5 µs, p99 182 µs, since the misses draw their glyphs.

### Start-up

- **measured**: `font_open` takes a median 3.23 ms (3.03 to 3.46, 6 runs, the 5-run benchmark running alongside), most of it `FcInit` loading fontconfig's configuration and caches (2.6 to 4.6 ms in a separate timing of fontconfig alone, 6 runs); the first shaped row 0.09 to 0.12 ms. It counts against the 10 ms first-frame target.

### Open

- **measured** (review, 1 run, deterministic): 73 of the 295 notation symbols have ink outside their 12-pixel cell at 20 pixels, 29 by more than 3 pixels (⟹ 28 px, ⋙ 27, 𝒰 22, 𝓝 21, ⨁ ⨂ 20); clipping, scaling or two cells is the backend's choice (package 8).
- **measured** (review, 11 runs): shaping all 60 rows of a frame takes p99 1.91 ms of the 2 ms frame budget. Whether rows are cached by their text (DESIGN.md's "a line that did not change is never shaped again") or the backend avoids reshaping on scroll is for package 8's frame gate on Davy's machine.
- **measured** (review, 5 runs): a cluster no font has is looked up in every fallback font on every pass (410 to 655 µs for a row of 100 such characters); not seen in Mathlib, so unchanged.
- **hypothesis**: a cluster with U+FE0E (text presentation) is not steered to a plain font; Davy's files have none in Mathlib's notation.
