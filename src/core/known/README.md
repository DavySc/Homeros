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

A file opens with the cursor on line 1's first non-blank since PR 62, so the cases whose line 1 starts with blanks begin with `k:0`, which puts it in column 0 where they were reduced (re-recorded with that token, each still differing as before). Tokens are those of `tests/test_core_screens.py`: `k:` keys, `w<n>:x` CTRL-W x, `pum:a:n:s` a popup with `n` items from item `a` and `s` CTRL-N. "Neovim" and "core" quote the first row that differs.

## Not the core's

| Case | Size | Tokens | Neovim | Core | Cause |
|---|---|---|---|---|---|
| `ruler_after_u.txt`, `ruler_after_u2.txt` | 30x80, 6x40 | `30G x u`; `23G ix<Esc> u` | ruler `2,1` | `2,1-8` | Neovim's own stale state: after `u` its ruler shows a virtual column that was not computed again on a tab (**hypothesis**) |

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

**The cursor's position.** This is the only listed difference in where the cursor goes rather than in what is shown. It was found by the fourth review, on the generator seeds 26 to 31.

| Case | Size | Tokens | Neovim | Core |
|---|---|---|---|---|
| `d_want_wrap_gg.txt` | 6x20 | `3$`, `60G`, `:e f1`, CTRL-^, wrap, `gg` | ruler `1,171-130` | `1,170-129` |

**Top line after jumps, splits and resizes.** These involve wrapped lines or double-width characters.

| Case | Size | Tokens | Neovim | Core |
|---|---|---|---|---|
| `resize.txt` | 10x20 | wrap, `CTRL-D`, `zb`, `:e f0`, `CTRL-W -`, `CTRL-W +` | top line 5 | line 4 |
| `split_3L.txt` | 30x80 | `3L`, `:sp` | the upper window one line further down | |
| `vs_wrap_100G.txt` | 30x20 | wrap, `100G`, `:vs` | another top line | |
| `jump_wide.txt` | 15x60 | `:set lines=21 columns=17`, `7 CTRL-W -`, `100G` | top line 6 | line 5 |
| `undo_topline.txt` | 6x80 | `30G`, `A` + 14 y, `CTRL-E`, `u` | top line 2 | line 1 |
| `d_open_above_skipcol.txt` | 10x40 | `$`, `60G`, wrap, `:sp`, `Onew line<Esc>` | the new line at the top | `--1<<<` (skipcol kept) |
| `append_cmdheight.txt` | 30x60 | `7$`, `CTRL-W -`, wrap, `A` + 80 y | one row further, a `~` row left | |

`:vs` with wrap on screens under 41 columns, where 'winwidth' squeezes the other window, is the first review's point 10. It has no single case: the review's `scan_vs.py` finds 28 of 288 combinations that differ.

## The Visual area (`test_core_visual`)

Cases in the format of `tests/test_core_visual.txt`; replay one with `build/san/test_core_visual -v src/core/known/<case>.txt`. Found on 3,000 fresh cases of `tests/test_core_visual.py` (1,000 each, seeds 1 to 3, **measured**, 1 run): 6 differ (0.2 %). In five the view differs before any highlight is drawn: each has the cursor moved to the line's end by `$` in Visual mode, where Neovim puts it on the NUL. **hypothesis**: the core's `curs_columns()` and `update_topline()` take a cursor on the NUL in Visual mode differently. The sixth differs in the cell under the cursor only, which the block cursor covers.

| Case | Size | State | Neovim | Core |
|---|---|---|---|---|
| `visual_eol_left.txt`, `visual_eol_left2.txt` | 10x20, 24x20 | nowrap, the cursor on a long line, `$` | leftcol 24, 52 | 23, 51 |
| `visual_eol_jump_left.txt`, `visual_eol_jump_left2.txt` | 5x20, 5x80 | nowrap, VIsual far right on a long line, the cursor on a short one, `$` | leftcol 0 | 12, 9 |
| `visual_nul_top.txt` | 24x40 | wrap, `V`, `$` on line 12 | top line 10 | line 9 |
| `visual_cursor_after_filler.txt` | 5x40 | wrap, skipcol 36, the cursor after `>` fillers of double-width characters | the cursor's cell in the area | not |
