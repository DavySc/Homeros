# Repro files for the review of PR 83 (Org colours)

All run from the repository root with `tools-env/bin` on PATH.

- `perf.c`, `perf.log`: what a frame's colours cost far from a headline and on long lines. Build it as `tests/org_drv.c` is built (`make -n org-conform | grep org_drv`), with `-O2` in place of the sanitizer flags and `reviews/pr-83-repro/perf.c` in place of `tests/org_drv.c`.
- `mutate.py DIR N SEED`: `tools/org_conform.py`'s colour texts with 1 to 4 random characters inserted, deleted or doubled; compare with `python3 tools/org_conform.py -f build/san/org_drv DIR/*.org`.
- `mutants.txt`, `mutants.sh`, `mutants.log`: 28 extra plants in the colour code, each run against the colour oracle (2,000 texts, seed 1). `more-lines.txt` holds the generator lines that catch the six that survive for lack of a line.
- `edit-plants.txt`, `edit-plants.log`: 5 extra plants in the edit and window paths, in `tests/plants`' format, run by `tools/planted.sh` in a copy whose `tests/plants/` holds only this file.
- `todo-line.org`, `overview.org`: the two texts where Emacs's batch answer and ours differ without a colour bug (section 1 of the report).
