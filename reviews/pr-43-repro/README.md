# Repro files for the review of PR 43

Run from the top of an integration tree (main + PR 27 + PR 40 + the
`src/lsp/` half of `/mnt/project-files/package-14/lsp-raw.patch` + PR 43),
after `make test TESTS=infoview` and with `ASAN_OPTIONS=detect_leaks=0`
(the repros do not free the test's streams).

| File | What | Finding |
|---|---|---|
| `build.sh NAME` | builds `NAME.c` with the unit test's flags and helpers as `build/san/r43_NAME` | |
| `common.h` | the unit test's set-up for the C repros | |
| `late.c` | `r43_late 0` / `r43_late 1`: the bridge opened before / after Lean answered initialize | 1 |
| `late.mjs`, `run-late.sh` | the same in the real infoview in Chromium | 1 |
| `probe.py CHILD ROOT [name...]` | the child alone: bind, token, traverse, origin, memory, idle, rundir, dash, lost, exit | 2, 6 to 10 |
| `plant5.sh RUNS` | how often the server fuzzer catches the package's own plant 5 | 3 |
| `edge.c` | `r43_edge empty\|chatter\|kill\|leandie\|big N\|stall` | 4, 5, notes |
| `fuzz_call.c`, `fuzz_call.sh SECONDS` | a fuzzer for the editor's side of the pipe | 4 |
| `crash-connect-min` | the input it found, reduced | 4 |
| `extra-plants.txt`, `extra-plants.sh [FILE]` | seven more plants, and planted.sh on one plant file | 6 |
| `plants.log` | both plant runs | 3, 6 |
| `referer.mjs CHILD ROOT` | the Referer Chromium sends from the page to another origin | notes |
| `bench_tap.c`, `bench_tap.sh N PAGES` | the cost of relaying incremental diagnostics | notes |
| `fixes.diff` | fixes for findings 1, 2, 4, 5 and 8, tested against these repros | |
