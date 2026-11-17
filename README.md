# Homeros

A native text editor for one person, Davy. The design, its evidence and its build plan are in [`design/`](design/README.md); the rules every agent follows are in [`CLAUDE.md`](CLAUDE.md).

## Status: package 0 (thin editor, headers, harness)

DESIGN.md splits the work into 18 packages. Package 0 is done: it gives every later package the headers it codes against, a reference implementation to replace, and a check that a merge must pass. Its acceptance rule is "the thin editor opens, edits and saves a file; every check runs and fails on a planted bug".

The thin editor runs in a terminal: `make && ./homeros FILE`. It has the text in one array, undo as a list, code-point Unicode, a terminal grid sent as changed cells inside a synchronized update, and these Vim keys with Davy's settings (expandtab, sw=4, sts=4, autoindent, smarttab): counts, `h j k l 0 ^ $ w b gg G x dd i a I A o O u Ctrl-R :`, Esc, Backspace, Enter and Tab in insert mode, and `:w :w! :q :q! :wq :x`. Saving follows Save2.tla's "exchange" variant: a temp file in the same directory, fsync, `renameat2(RENAME_EXCHANGE)`, a check of what came out, fsync of the directory; it refuses to overwrite a file changed outside since it was read (`:w!` overrides).

## The check

Tools: `nix build -f design/env.nix tools -o tools-env`, put `tools-env/bin` on PATH, run `why3 config detect` once; clang needs its compiler-rt (on Debian/Ubuntu `libclang-rt-18-dev`).

| Target | What it runs |
|---|---|
| `make compilers` | gcc and clang, C17, `-Wall -Wextra -Werror` |
| `make test` | unit tests under ASan and UBSan, and 1,000 recorded Neovim traces replayed |
| `make fuzz` | libFuzzer on the text and on random key sequences, `FUZZ_SECONDS` each (default 30) |
| `make conform` | `CONFORM_N` fresh random key sequences (default 2,000) against live `nvim --headless` |
| `make undo-diff` | the undo tree oracle against Neovim's recorded tree (`design/ref/undo/undo_diff.py`) |
| `make lean` | `design/proofs/Undo.lean`, failing on any `sorry` |
| `make frama` | Frama-C WP on the UTF-8 kernels; every goal must be proved |
| `make specs` | all TLA+ configs through `design/specs/check.sh`; each must pass or fail as its first line says |
| `make check` | all of the above |
| `make planted` | `tools/planted.sh`: plants one bug per check and requires that check to fail |

CI (`.github/workflows/check.yml`) runs `make check` and `make planted`.

## Evidence for package 0

Tags as CLAUDE.md asks.

- **measured**: 0 of 44,000 fresh random key sequences differ from `nvim --headless` 0.12.6 (10,000 each for seeds 12 to 15; 2,000 each for seed 26 and 1794915425, the two seeds the review found failing before the fix); the 1,009 recorded cases (1,000 from seed 101 plus the 9 that once differed) replay with 0 differences.
- **measured**: the undo oracle reports 0 of 5,000 sequences differing from Neovim's tree.
- **proved**: Frama-C WP, 77 of 77 goals, for `uni_len`, `uni_prev` and `uni_cp`, with contracts in `include/uni.h` that state each result (a body that returns 1, or steps one byte back, fails).
- **proved**: TLC passes or fails every design spec config as expected; Lean checks `Undo.lean` with no `sorry`.
- **measured**: `tools/planted.sh` catches all 19 plants in `tests/plants/` (at least one per check), each by the failure its check reports.
- **measured**: `test_uni` checks every 1 to 3 byte input exhaustively and 16.7 M random 4-byte inputs; `test_text` runs 200,000 random edits against an array model.

## Choices made in package 0 (open in the design, picked here)

- **hypothesis**: an operator on an empty buffer (Neovim's `ML_EMPTY`) leaves an empty undo step only at the oldest undo state. This was measured on 15 sequences, but the mechanism inside Neovim is not understood; package 5 revisits it.
- **hypothesis**: `inserted_space` follows Neovim's typeahead batching (queued one-byte characters are inserted as one string). The driver passes "more keys queued" the same way the main loop does.
- `uni_width` is 1 for every code point and not proved; package 3 adds East Asian Width and graphemes.
- `text.h` has no ACSL yet: package 1 asks thread 0 for the contract it is judged by.
- **hypothesis**: after `u` or `Ctrl-R`, curswant follows Neovim's cached cursor column, which a step leaves stale when the replaced lines ran to the end of the buffer (measured on the cases in the recorded traces; derived from `u_undoredo()` and `changed_lines()`).
- A hard-linked file is refused on save (the exchange would split the link); package 11 decides.
- Not yet: undo size cap, undo file, crash journal (packages 2 and 11); escape sequences split across reads (package 6); a loop callback that unwatches another fd during the same pass may skip it (package 10).

## Next

Packages 1 to 12 can start now (13, 15, 16 and 17 after the loop, package 10; 14 after 13), each in its own thread, owning `src/<module>/`. Packages whose header is not in `include/` yet (7 `font.h`, 12 `syntax.h`, and 13 to 17) start by asking thread 0 for it. A package adds its tests, fuzzers and plants as files (`tests/test_<module>*.c`, `tests/fuzz_<module>*.c`, `tests/plants/<module>.txt`); the Makefile finds them, so no package edits a shared file. The rope, the Wayland backend and the org parser are built twice, independently, and the smaller and faster passing version wins. Every package is reviewed by a separate agent before it merges.
