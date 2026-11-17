# Where the core still differs from Neovim

These are the differences from Neovim 0.12 that are known and not yet fixed. Each one has a reduced case recorded from `nvim --headless`, in the format of `tests/test_core_screens.txt`. To replay a case: `build/san/test_core_screens -v src/core/known/<case>.txt`. A fix moves its case into `tests/test_core_screens.txt` and deletes its line here.

How they were found (**measured**, 1 run each, on PR 20 after the third review's fixes):

- the review's 4,400 cases;
- six fresh seeds of each generator, 9,600 cases in all:
  - `tests/test_core_screens.py` (400 each, seeds 20 to 25);
  - the review's `gen2.py` (400 each, seeds 4 to 9);
  - `wide.py` (300 each, seeds 14 to 19);
  - `ctl.py` (300 each, seeds 5 to 10).

On the fresh seeds, 12 cases differ (0.13 %), and each falls in a class below. Two more cases were regressions from the third review's fixes and are fixed: gen2 seed 7 case 363 and ctl seed 7 case 67. Both are now in the recorded traces. Every case listed here differs on d3212dc too. The cases were cut down by the review's `reduce.py`.

Tokens are those of `tests/test_core_screens.py`: `k:` keys, `w<n>:x` CTRL-W x, `pum:a:n:s` a popup with `n` items from item `a` and `s` CTRL-N. "Neovim" and "core" quote the first row that differs.

## Not the core's

| Case | Size | Tokens | Neovim | Core | Cause |
|---|---|---|---|---|---|
| `ruler_after_u.txt`, `ruler_after_u2.txt` | 30x80, 6x40 | `30G x u`; `23G ix<Esc> u` | ruler `2,1` | `2,1-8` | Neovim's own stale state: after `u` its ruler shows a virtual column that was not computed again on a tab (**hypothesis**) |
| `x_last_char.txt`, `x_last_char2.txt` | 10x60, 6x60 | `2 CTRL-F $ x`; `A...<Esc> CTRL-F x` | the view kept | the view scrolls | Package 5: `ref/keys.c` calls `core_edit()` with the cursor still on the deleted character, but Neovim's `del_bytes()` moves it left first (review of PR 20) |

## The core's, not yet found

**Narrow windows.** All of these are 1 to 3 text columns wide.

| Case | Size | Tokens | Neovim | Core |
|---|---|---|---|---|
| `narrow_at.txt`, `narrow_at2.txt` | 30x20 | `enew wrap vs oa<CR>b<CR>c<Esc> pum:10:2:2`; `bd oa<CR>b<CR>c<Esc> wrap vs pum:0:3:0` | `@` on the last row | blank |
| `narrow_wide.txt` | 15x80 | `7 CTRL-W v`, `:set lines=25 columns=78`, wrap | `>` on every row of a wide character, which never fits in 1 column | `>` once |
| `ctrl_b_3col.txt` | 10x60 | `3 CTRL-W v`, wrap, `2 CTRL-B` | `1<<` | `1` |
| `pum_narrow_col.txt` | 6x80 | `vnew`, `7 CTRL-W R`, `3 CTRL-W v`, `pum:7:9:9` | the popup 2 columns further left | |

**Popup row and scroll.** The popup's row when the completed word wraps the line. **hypothesis**: Neovim places it from `w_wrow` as it was before the insertion, while `w_cline_row` and `w_cline_height` are as drawn (`win_update()`). One experiment along this line fixed `pum_row_wrap.txt` but broke 5 recorded cases, so it was not kept.

| Case | Size | Tokens | Neovim | Core |
|---|---|---|---|---|
| `pum_row_wrap.txt` | 6x20 | wrap, `pum:16:11:2` | popup below, rows 1 to 4 | no popup |
| `pum_row_wrap2.txt` | 10x20 | `2 CTRL-W _`, wrap, `pum:4:20:9` | popup at row 1 | another row |
| `pum_3rows.txt` | 24x40 | `CTRL-F`, wrap, `3 CTRL-W _`, `pum:14:11:25` | the line drawn under the popup | blank |
| `pum_scroll_vs.txt` | 15x60 | wrap, `2 CTRL-W _`, `vs`, `3 CTRL-F`, `pum:5:20:12` | the other window's view | another view |
| `pum_vs_skipcol.txt` | 10x40 | `45G vs wrap CTRL-W t CTRL-W w 3 CTRL-F CTRL-W p pum:5:10:4` | no `<<<` | `<<<` (skipcol kept) |

**Top line after jumps, splits and resizes.** These involve wrapped lines or double-width characters.

| Case | Size | Tokens | Neovim | Core |
|---|---|---|---|---|
| `resize.txt` | 10x20 | wrap, `CTRL-D`, `zb`, `:e f0`, `CTRL-W -`, `CTRL-W +` | top line 5 | line 4 |
| `split_3L.txt` | 30x80 | `3L`, `:sp` | the upper window one line further down | |
| `vs_wrap_100G.txt` | 30x20 | wrap, `100G`, `:vs` | another top line | |
| `jump_wide.txt` | 15x60 | `:set lines=21 columns=17`, `7 CTRL-W -`, `100G` | top line 6 | line 5 |
| `undo_topline.txt` | 6x80 | `30G`, `A` + 14 y, `CTRL-E`, `u` | top line 2 | line 1 |
| `append_cmdheight.txt` | 30x60 | `7$`, `CTRL-W -`, wrap, `A` + 80 y | one row further, a `~` row left | |

`:vs` with wrap on screens under 41 columns, where 'winwidth' squeezes the other window, is the first review's point 10. It has no single case: the review's `scan_vs.py` finds 28 of 288 combinations that differ.
