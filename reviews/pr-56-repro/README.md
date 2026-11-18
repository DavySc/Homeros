# Repro files for the review of PR 56

Everything here was run on e56b19d (the PR's head, on PR 57's 74440bd) unless a file name says otherwise. `TREE` is a checkout of that head after `make homeros`.

## Live Neovim with render-markdown

- `rec.py CASES.py` records named cases from Neovim 0.12.6 with render-markdown.nvim v8.14.0 and catppuccin in tmux, with the PR's own `tests/test_core_markdown.lua`. Unlike the PR's recorder it takes commands besides keys (`wrap`, `vs`, `sp`, `w:<c>`, `e:file`, `C-e`, `visual`, `redraw`), reads every row but the command line, and reads the terminal's cursor.
  `RM_RTP=<render-markdown.nvim>:<catppuccin> python3 rec.py cases4.py > cases4.txt`
- `cases1.py` to `cases8.py`: the cases, by round. `recorded/cases*.txt`: what Neovim showed (so the comparison below runs without the plugins).
- `mdcmp.c` replays a recorded file in the core and compares text, heading backgrounds, heading colours and the cursor's cell:
  `./build.sh TREE mdcmp.c /tmp/mdcmp -O1 -g -fsanitize=address,undefined && /tmp/mdcmp recorded/cases4.txt`
  `mdcmp-e56b19d.out` is its output for all eight rounds.
- `HOMEROS=TREE/homeros python3 rec.py cases1.py > hom1.txt` runs the editor itself in tmux (terminal backend) for the cases made of keys only; `cmprec.py recorded/cases1.txt hom1.txt` compares the two terminals. `cmprec-e56b19d.out` is its output.

## What the PR's test does not notice

- `mut.sh TREE recorded/cases*.txt` applies each line of `muts.txt` to a copy of the tree and says whether `tests/test_core_markdown.c` (the PR's 300 screens) and the cases above notice. `mut-e56b19d.log` is its output.

## Buffer and cursor

- `state.c`: the cursor, the window's top and leftcol, the grid's cursor cell and a hash of the text after every key of 300 key sequences, to be compared between a tree with and one without the PR:
  `./build.sh TREE state.c /tmp/state-pr ...; /tmp/state-pr TREE/tests/test_core_markdown.txt > a; (the same for the base) ; cmp a b`

## Time and allocations

- `bench.c`: `core_draw`'s time per frame on a 50x200 grid and the allocations made inside it (ld `--wrap`):
  `./build.sh TREE bench.c /tmp/bench -O2 -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc; /tmp/bench FILE.md 3000 [wrap]`
  `bench-e56b19d-vs-74440bd.log` and `bench-77466f8-vs-main.log`: 7 alternating runs per side and file.

## Fuzzing

- `fuzz_md.c` is `tests/fuzz_core.c` on files named `*.md` with headings in them, parsed before most frames (`tests/fuzz_core.c` opens `f0`, `f1`, `f2`: no grammar, so no heading is ever drawn there). `fuzz_md.sh TREE SECONDS` builds and runs it (it writes under /tmp/claude-0/pr56).
