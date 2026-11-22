Repro files for the review of PR 119 (git line staging), head 716da9f.

- `scenarios.json`: 31 scenarios I recorded in magit 4402e97 + evil-collection (Emacs 31.1) with `src/git/record/record.py`; `traces.txt` is `totext.py`'s output for them.
- `outside-change.trace.txt`: the one scenario of finding 1 alone.
- `test_gitrv.diff`: `tests/test_git.c` as I ran it: `RV_TRACES=<file>` picks the trace file; `RV_HAZ=1|2` runs `region_check` alone on harder lines (duplicates, lines that start with `-`, `+`, `@@`, `\`, `diff `, tabs and spaces).
  Run: `patch -o tests/test_gitrv.c tests/test_git.c reviews/pr-119-repro/test_gitrv.diff && make test TESTS=gitrv`, then
  `RV_TRACES=reviews/pr-119-repro/traces.txt ./build/san/test_gitrv` and `RV_HAZ=2 SEED=3 GIT_REGION_N=400 GIT_SCENARIO=region-stage ./build/san/test_gitrv`.
- `review-plants.txt`: my 17 plants (planted.sh's format); `review-plants.log`: their results.
