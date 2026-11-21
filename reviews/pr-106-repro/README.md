Probes for the review of PR 106 (reviews/pr-106.md). Run from the repo root with
`tools-env/bin` on PATH and `make test TESTS=core_search` built.

- `search_wide.py N SEED --record FILE` (PYTHONPATH=tests): the PR's search recorder with wider
  patterns; replay with `build/san/test_core_search FILE -v`. `wide1.txt` is N=600, seed 1.
- `one.py` (PYTHONPATH=tests): single CurSearch cases, Neovim and core side by side.
- `flash_search.lua`, `flash_search.c`: the yank flash over 'hlsearch' matches, Neovim and core.
- `prio.lua`, `split.lua`: Visual and :s c flag against Search; CurSearch in two windows.
- `fuzz_draw.c`: tests/fuzz_keys_all.c drawing a frame after every key (build with the
  Makefile's fuzz line).
- `bench.c FILE PATTERN|-`: ns per frame (build with the Makefile's sources at -O2).
- `yank106-case332.txt`: the one fresh yank case (seed 106) whose view differs; replay with
  `build/san/test_core_visual yank106-case332.txt -v`.
- Fresh seeds that matched are not kept: `tests/test_core_search.py 400 106` and `107`,
  `tests/test_core_visual.py 400 106 --yank`.
