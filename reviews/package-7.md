# Review of package 7: fonts and shaping (PR 6)

Reviewed: commit bf2be60 (`src/font/`, `tests/test_font*.c`, `tests/fuzz_font.c`, `tests/plants/font.txt`) against `include/font.h` and DESIGN.md's row for package 7. Reviewer: a separate Fable 5.2 thread, briefed with the diff and the contract. Machine: Ubuntu 24.04, 4 cores, CI's apt packages (FreeType 2.13.2, HarfBuzz 8.3.0, fontconfig 2.15.0) and `tools.nix`. Repro sources: `reviews/package-7-repro/` (`probe.c`, `frames.c`; build lines in their headers).

## Verdict

**Approve, with two small changes asked first** (findings 1 and 2: one line of code and README wording). No correctness bug found. The package stays inside its own directories. It must merge after thread 0's branch `claude/project-thread-uxi928` (font.h, `build.mk` support, CI's font packages), which is not on main yet.

## What was run

| Check | Result | Tag |
|---|---|---|
| `make check` (FUZZ_SECONDS 30, CONFORM_N 2000) | all passed, 10 min 18 s; `test_font` 295 of 295; `fuzz_font` 81,239 runs; conformance 0 of 2,000 differ; Frama-C 77/77; 35 of 35 TLA+ configs as expected | measured, 1 run |
| `make planted` | 29 of 29 caught, the 10 font plants among them, each by the check its line names | measured, 1 run |
| `mathlib_symbols.py` on mathlib4 at 6201aea (today's master; the author's 7668accf could not be fetched) | 295 characters, the same set as the list in `test_font.c` | measured, 1 run |
| `bench/hitrate.c`, 8,718 files | 99.99837% (1,529 misses in 93,562,249 lookups); cold first screen 95.957%, worst file 58.82%, 185 files under 90% | measured, 1 run each (deterministic); agrees with the README |
| Emoji with other fonts present (unifont, FreeFont; then all 420 faces on the machine) | 😀, family, rainbow flag, 👍🏽, ❤️ stay one colour glyph each | measured, 1 run per font set |
| Flag 🇳🇱, keycap 1️⃣, e + acute, x + U+20D7, α + hat, 𝓝 + hat, Arabic, Hangul jamo, Devanagari | one cluster each where expected; no crash under ASan and UBSan | measured, 1 run |
| No font at all (`HOMEROS_FONTS` an empty directory) | `font_open` returns -1, no leak reported | measured, 1 run |

Acceptance (DESIGN.md): every Mathlib notation symbol draws (**measured**, reproduced); combining marks and ZWJ emoji shape (**measured**, reproduced); glyph cache hit rate measured (**measured**, reproduced to the digit).

## Findings

### 1. "A cache hit allocates nothing" is not true as written (small fix)

`font.h`, the README and the PR say a pass whose glyphs are all cached makes 0 allocations. The test proves it only for the same line shaped twice. **Measured** (`probe allocs`, ASan's malloc hook, 1 run, deterministic), each with 0 cache misses:

| Run, all glyphs already cached | Allocations |
|---|---|
| the same line again | 0 |
| a run longer than any before (201 letters after 3) | 2 (10,400 bytes): HarfBuzz grows its buffer |
| a run whose first letter is of a script not seen first before (`α a` after `a α`; the same for Cyrillic) | 19 (9,572 bytes): HarfBuzz builds a shape plan |
| 𝓝 alone after `a 𝓝` | 19: the same cause |
| any of these a second time | 0 |

Asked: `hb_buffer_pre_allocate(buf, RUN)` in `font_open` removes the first case (one line); say in the README that a script's first run allocates once; add the longer-run case to the test's allocation check. The shape plans stay cached (32 scripts shaped three times: 3,377 allocations in round one, 0 in rounds two and three), so the cost is bounded.

### 2. Shaping every row of a frame takes half of the 2 ms frame budget at the median and nearly all of it at p99 (record it; decision for package 8 and thread 0)

The README argues that no shaped-run cache is needed because a row costs about 10 µs. DESIGN.md's gate is "frame ready under 2 ms p99", and its Fonts paragraph says "a line that did not change is never shaped again". **Measured** (`frames.c`: all of Mathlib paged 60 rows at a time, second pass, every glyph cached, 35,206 frames, 11 runs): shaping the 60 rows of one frame takes a median of 0.98 ms (0.96 to 1.01) and p99 1.91 ms (1.74 to 2.04). That is what one scrolled line costs if the backend reshapes every row whose cells changed, before any drawing.

Font fallback is a small part of it. With every non-ASCII character replaced by `x` (5 alternating runs per side): median 0.94 ms (0.93 to 0.96) against 0.99 ms (0.97 to 1.00), so fallback costs about 0.04 ms of the median; p99 1.84 ms (1.75 to 1.91) against 1.91 ms (1.74 to 2.00), inside the spread, so no measurable difference there. The time is HarfBuzz shaping the main font.

Asked: put this measurement in the README beside the "no shaped-run cache" paragraph and list it as open. Whether rows are cached by their text (DESIGN.md's wording) or the backend avoids reshaping on scroll is for package 8's frame gate, on Davy's machine. Not a reason to hold this package: its own acceptance is met.

### 3. Fallback is looked up again for every cluster on every pass (note)

**Measured** (`probe nofont`, 5 runs, rows of 100 characters, all cache hits): ASCII 5.2 µs per row (5.1 to 5.6); characters from a fallback font (math script) 175 µs (171 to 177); characters no font has (private use, unassigned, Mathlib's U+A7F4) 410 to 655 µs, because all 156 fonts are scanned per character each time. In Mathlib this does not show (finding 2). A screen of 60 rows of 40 CJK characters would cost about 4 ms per frame (**hypothesis**, from 1.75 µs per character). FEATURES.md names no CJK or other fallback-heavy text, so no change is asked; if it ever matters, shaping adjacent fallback clusters together (wezterm reshapes ranges) is the copy to make.

### 4. 73 of the 295 notation symbols draw outside their cell (note for package 8)

**Measured** (`probe wide`, 1 run, deterministic, 20 px, cell 12 px): 73 symbols have ink outside their own cell, 29 by more than 3 px, among them ⟹ (28 px), ⋙ (27), 𝒰 (22), 𝓡 (21), ⨁ ⨂ (20), ℋ (19) and the combining hat alone. The README lists this as a hypothesis with one example; the count belongs there. What to do (clip, scale, or give the symbol two cells) is package 8's, and it is the part of this package Davy will see most in Lean files.

### 5. Smaller points (no change asked unless noted)

- **measured** (1 run): tab and U+0001 come back as the first font's `.notdef` box; `font.h` could say the caller must not pass control characters (thread 0's header).
- **measured** (1 run): a run of 4,999 letters returns 2,048 glyphs without a sign; no window row is that long, but `font.h` does not state the limit.
- **measured** (1 run): 😀 followed by U+FE0E (text presentation) is drawn in colour; the README already lists it as open.
- **measured** (3 runs): resident memory grows by 3.1 to 4.7 MB at `font_open` and by 8.1 to 8.5 MB after a Lean line, one CJK character and one emoji. DESIGN.md's start target is 15 MB with the window prototype at 10.7 to 11.7 MB without fallback fonts, so package 8 should expect to be near or over it.
- `emoji_table.py` and `uni.h` both say Unicode 16; if the Unicode kernels move to 18, this table should follow.

## The author's three concerns

1. **FreeType allocates on a glyph's first draw.** Accepted as it is. DESIGN.md itself tags "no allocation in the redraw path after start-up" as a hypothesis; this package measures it false for first use and true afterwards (with finding 1's corrections). Every allocation is tied to something seen for the first time: a glyph, a fallback font, a script, a longer run. A FreeType memory pool would cover FreeType only (HarfBuzz and fontconfig allocate too) and add code for no measured gain; foot does the same as this package. The real cost is the first load of a large fallback font: the author measured up to 17 ms for one row, and I saw 6 to 27 ms as the slowest frame in 11 runs. That is one late frame per fallback font per session. Thread 0 should reword DESIGN.md's rule to "after first use".
2. **`font_open` takes about 3 ms of the 10 ms first frame.** Accepted. **Measured** here: median 3.86 ms (3.53 to 4.25, 7 runs) on the pinned set, and 4.20 ms (4.06 to 4.34, 6 runs) with all 420 faces on this machine once fontconfig's cache exists (61 ms the one time it had to build it). With the prototype's 5.0 to 5.8 ms that leaves the first frame at about 9 to 10 ms, so package 8's gate is tight and decides it. Most of it is `FcInit`, which foot pays too; the alternative, a saved list of font files, is a new mechanism and would need the evidence CLAUDE.md asks for.
3. **`grid.h`'s `Cell` holds one character.** Agreed, and outside this package: font.h shapes clusters correctly (measured above), but they cannot reach a window until thread 0 changes the grid. The acceptance "combining marks and ZWJ emoji shaped" is met at font.h's level only.

## Simplicity

334 lines for the whole of font.h. Nothing found that could be deleted without losing a tested behaviour; the cache, the fallback rule and the emoji rule are each copied from a named editor and each has a plant.
