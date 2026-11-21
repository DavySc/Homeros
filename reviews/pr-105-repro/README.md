# Repro files for the review of PR 105

Tools: slang-server 4349e86 built by the PR's own flake (`src/lsp/systemverilog/flake.nix`), Verible v0.0-4298, lowRISC Ibex (shallow clone of master, 2026-11-21), gcc from CI's apt line.

- `format_ibex.c`: `:Format`, a second `:Format`, `u` and three random `gq` ranges per file through the editor, each compared with what `verible-verilog-format` prints for the same text. Copy to `tests/test_keys_zzsv.c`, `make test TESTS=keys_zzsv` builds it, then `build/san/test_keys_zzsv FILE...`.
- `hang.py HOMEROS ctrlc|wait`: the real editor in a pty with a formatter that sleeps; Ctrl-C, and keys typed during the wait.
- `reuse.c`: finding 2, `proc_wait` on a child whose report starts another child. `gcc -D_DEFAULT_SOURCE -Iinclude reuse.c src/loop/loop.c src/loop/timer.c`.
- `ibex_top.f`: the file list `.slang/server.json` named (`{"build": "./ibex_top.f"}`) for the gate runs, at Ibex's root.
- `sv_quiet.c`: the PR's bench with one quiet second before each timed save.
- `e2e_sv.py HOMEROS FILE.sv`: the real editor in a pty inside the Ibex project with slang-server on PATH: types a syntax error, saves, prints what the screen says about it (note 7).
- Extra plants (finding 1 and the reap filter), each in a scratch copy, `make -s test TESTS=...`:
  - `src/keys/ops.c`: `if (stream == 1 && b)` to `if (stream && b)`: `test TESTS=keys_gq` passes (not caught).
  - `src/loop/loop.c`: `if (p[i].pid && (only < 0 || i == only) && waitpid(` to `if (p[i].pid && waitpid(`: caught, `tests/test_loop.c:499`.
